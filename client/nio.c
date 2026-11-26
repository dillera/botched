#include "botched/client/nio.h"
#include <string.h>
#include <limits.h>
enum { OPEN = 1, INFO, SEND, RECEIVE, READY, RESPONSE, CLOSE, FAILED };
static uint16_t get16(const uint8_t *p) { return (uint16_t)(p[0] | (uint16_t)p[1] << 8); }
static int fail(bn_client *c, uint8_t error) {
    c->error = error;
    c->phase = FAILED;
    return BN_FAILED;
}
static int recovery(bn_client *c) {
    if (!c->negotiated || c->recoveries >= 2)
        return fail(c, BN_ERROR_LINK);
    ++c->recoveries;
    c->resume = 1;
    c->phase = c->handle ? CLOSE : OPEN;
    return BN_WAIT;
}
static void frame_start(bn_client *c, uint8_t mode) {
    c->mode = mode;
    c->sent = 0;
    c->received = 0;
    c->expected = 2;
    c->phase = SEND;
    c->retries = 0;
}
static void hello(bn_client *c) {
    bw_writer w;
    bw_writer_init(&w, c->hello, sizeof c->hello);
    bw_write_u16(&w, 30);
    bw_write_u8(&w, 1);
    bw_write_u16(&w, 0);
    bw_write_u8(&w, 1);
    bw_write_u8(&w, 1);
    bw_write_u8(&w, 0);
    bw_write_u16(&w, c->payload);
    bw_write_u8(&w, c->profile);
    bw_write_u32(&w, 63);
    bw_write_u8(&w, c->resume);
    bw_write_bytes(&w, c->secret, 16);
    frame_start(c, 0);
}
int bn_init(bn_client *c, const bn_ops *ops, void *context, const char *url, uint16_t payload,
            uint8_t profile, uint32_t now, uint32_t timeout) {
    size_t n = 0;
    if (!c)
        return 0;
    memset(c, 0, sizeof(*c));
    c->phase = FAILED;
    c->error = BN_ERROR_CONFIG;
    if (!ops || !ops->open || !ops->info || !ops->read || !ops->write || !ops->close || !url ||
        payload < 128 || payload > 240 || profile < 1 || profile > 3 || !timeout || timeout > INT32_MAX)
        return 0;
    while (url[n] && n <= 224) {
        if ((unsigned char)url[n] < 33 || (unsigned char)url[n] > 126)
            return 0;
        ++n;
    }
    /* Existing Open with method/flags/lengths plus raw FujiBus header fits 256. */
    if (n < 7 || n > 224 || memcmp(url, "tcp://", 6))
        return 0;
    c->ops = ops;
    c->context = context;
    c->url = url;
    c->payload = payload;
    c->profile = profile;
    c->started = now;
    c->timeout_ms = timeout;
    c->next_exchange = 1;
    c->phase = OPEN;
    c->error = 0;
    bw_stream_init(&c->stream, 0);
    return 1;
}
int bn_dispose(bn_client *c) {
    if (c->handle) {
        if (c->ops->close(c->context, c->handle) != BN_IO_OK) {
            c->error = BN_ERROR_CLOSE;
            c->phase = FAILED;
            return 0;
        }
        c->handle = 0;
    }
    c->phase = FAILED;
    return 1;
}
int bn_reconnect(bn_client *c, uint32_t now) {
    if (!c->negotiated || c->error == BN_ERROR_SESSION || c->error == BN_ERROR_PROTOCOL)
        return 0;
    c->started = now;
    c->recoveries = 0;
    c->error = 0;
    return recovery(c) == BN_WAIT;
}
int bn_request(bn_client *c, const uint8_t *payload, uint16_t size, uint32_t now) {
    if ((c->phase != READY && c->phase != RESPONSE) || c->pending || c->next_exchange > 65535 ||
        size > BN_REQUEST - 2 || !bw_validate_packet(payload, size, c->payload) || payload[0] == 1 ||
        payload[0] >= 0x80 || get16(payload + 1) != c->next_exchange)
        return 0;
    memmove(c->request + 2, payload, size);
    c->request[0] = (uint8_t)size;
    c->request[1] = 0;
    c->request_size = (uint16_t)(size + 2);
    c->pending = 1;
    c->started = now;
    c->recoveries = 0;
    c->apply = 0;
    c->error = 0;
    if (c->eof)
        return recovery(c) == BN_WAIT;
    frame_start(c, 1);
    return 1;
}
static int complete(bn_client *c) {
    uint8_t *p = c->reply + 2;
    uint16_t n = (uint16_t)(c->received - 2), last;
    if (!bw_validate_packet(p, n, c->payload))
        return fail(c, BN_ERROR_PROTOCOL);
    if (!c->mode) {
        if (p[0] != 0x81)
            return fail(c, BN_ERROR_SESSION);
        if (p[7] != c->profile || get16(p + 5) != c->payload)
            return fail(c, BN_ERROR_PROTOCOL);
        last = get16(p + 44);
        if (c->resume) {
            if (memcmp(c->epoch, p + 12, 16) || memcmp(c->secret, p + 28, 16))
                return fail(c, BN_ERROR_SESSION);
            if ((uint32_t)last != c->next_exchange - 1 && !(c->pending && (uint32_t)last == c->next_exchange))
                return fail(c, BN_ERROR_SEQUENCE);
        } else {
            if (last)
                return fail(c, BN_ERROR_SEQUENCE);
            memcpy(c->epoch, p + 12, 16);
            memcpy(c->secret, p + 28, 16);
            c->negotiated = 1;
        }
        if (c->pending) {
            frame_start(c, 1);
            return BN_WAIT;
        }
        c->phase = READY;
        return BN_READY;
    }
    if (get16(p + 1) != c->next_exchange || (p[0] != 0x82 && p[0] != 0x83 && p[0] != 0xff))
        return fail(c, BN_ERROR_SEQUENCE);
    if (p[0] == 0xff && (p[3] == 4 || p[3] == 5 || p[3] == 6))
        return fail(c, BN_ERROR_SESSION);
    c->apply = 0;
    if (p[0] == 0x82) {
        int apply = 0;
        if (!bw_stream_accept(&c->stream, p, n, &apply))
            return fail(c, BN_ERROR_PROTOCOL);
        c->apply = (uint8_t)apply;
    }
    ++c->next_exchange;
    c->pending = 0;
    c->reply_size = n;
    c->phase = RESPONSE;
    return BN_RESPONSE;
}
int bn_step(bn_client *c, uint32_t now) {
    uint8_t rc, flags = 0;
    uint16_t count = 0, want;
    uint32_t *cursor;
    uint8_t *data;
    if (c->phase == FAILED)
        return BN_FAILED;
    if (c->phase == READY)
        return BN_READY;
    if (c->phase == RESPONSE)
        return BN_RESPONSE;
    if ((uint32_t)(now - c->started) >= c->timeout_ms)
        return fail(c, BN_ERROR_TIMEOUT);
    if (c->phase == CLOSE) {
        rc = c->ops->close(c->context, c->handle);
        if (rc == BN_IO_AGAIN)
            return BN_WAIT;
        if (rc != BN_IO_OK)
            return fail(c, BN_ERROR_CLOSE);
        c->handle = 0;
        c->phase = OPEN;
        return BN_WAIT;
    }
    if (c->phase == OPEN) {
        rc = c->ops->open(c->context, c->url, &c->handle);
        if (rc == BN_IO_AGAIN && !c->handle)
            return BN_WAIT;
        if (rc != BN_IO_OK || !c->handle)
            return fail(c, BN_ERROR_OPEN);
        c->read_cursor = c->write_cursor = 0;
        c->eof = 0;
        c->phase = INFO;
        return BN_WAIT;
    }
    if (c->phase == INFO) {
        rc = c->ops->info(c->context, c->handle, &flags);
        if (rc == BN_IO_AGAIN)
            return BN_WAIT;
        if (rc != BN_IO_OK || (flags & BN_PEER_CLOSED))
            return recovery(c);
        if (!(flags & BN_INFO_READY))
            return BN_WAIT;
        hello(c);
        return BN_WAIT;
    }
    if (c->phase == SEND) {
        want = (uint16_t)((c->mode ? c->request_size : 32) - c->sent);
        if (want > BN_CHUNK)
            want = BN_CHUNK;
        data = (c->mode ? c->request : c->hello) + c->sent;
        cursor = &c->write_cursor;
        if (*cursor > UINT32_MAX - want)
            return recovery(c);
        rc = c->ops->write(c->context, c->handle, *cursor, data, want, &count);
        if (count > want)
            return fail(c, BN_ERROR_PROTOCOL);
        if (rc == BN_IO_AGAIN && !count)
            return BN_WAIT;
        if (rc != BN_IO_OK)
            return recovery(c); /* write completion may be ambiguous */
        if (!count)
            return BN_WAIT;
        *cursor += count;
        c->sent = (uint16_t)(c->sent + count);
        if (c->sent == (c->mode ? c->request_size : 32))
            c->phase = RECEIVE;
        return BN_WAIT;
    }
    if (c->phase != RECEIVE)
        return fail(c, BN_ERROR_PROTOCOL);
    want = (uint16_t)(c->expected - c->received);
    if (want > BN_CHUNK)
        want = BN_CHUNK;
    cursor = &c->read_cursor;
    if (*cursor > UINT32_MAX - want)
        return recovery(c);
    rc = c->ops->read(c->context, c->handle, *cursor, c->reply + c->received, want, &count, &flags);
    if (count > want)
        return fail(c, BN_ERROR_PROTOCOL);
    if (rc == BN_IO_AGAIN && !count)
        return BN_WAIT;
    /* A single <=32-byte read is replayable at exactly the same offset AND size.
     * Never advance on ambiguous/malformed results or change the next read size. */
    if (rc == BN_IO_LOST && !count && c->retries++ < 2)
        return BN_WAIT;
    if (rc != BN_IO_OK)
        return recovery(c);
    c->retries = 0;
    *cursor += count;
    c->received = (uint16_t)(c->received + count);
    c->eof = (uint8_t)(flags & BN_EOF);
    if (c->received == c->expected) {
        if (c->expected == 2) {
            want = get16(c->reply);
            if (want < 3 || want > c->payload)
                return fail(c, BN_ERROR_PROTOCOL);
            c->expected = (uint16_t)(want + 2);
        } else
            return complete(c);
    }
    if (c->eof)
        return recovery(c);
    return BN_WAIT;
}
