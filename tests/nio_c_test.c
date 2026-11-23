#include "botched/client/nio.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#ifdef BW_WRAP_ALLOC
static int armed;
void *__real_malloc(size_t);
void *__real_calloc(size_t, size_t);
void *__real_realloc(void *, size_t);
void __real_free(void *);
void *__wrap_malloc(size_t n) {
    if (armed)
        abort();
    return __real_malloc(n);
}
void *__wrap_calloc(size_t n, size_t m) {
    if (armed)
        abort();
    return __real_calloc(n, m);
}
void *__wrap_realloc(void *p, size_t n) {
    if (armed)
        abort();
    return __real_realloc(p, n);
}
void __wrap_free(void *p) {
    if (armed)
        abort();
    __real_free(p);
}
#endif
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
static uint8_t incoming[96], outgoing[50];
static uint16_t used, total, read_pos;
static uint32_t tx, rx;
static int bad;
static uint8_t open_tcp(void *p, const char *url, uint16_t *h) {
    (void)p;
    (void)url;
    *h = 1;
    return BN_IO_OK;
}
static uint8_t info(void *p, uint16_t h, uint8_t *f) {
    (void)p;
    (void)h;
    *f = BN_INFO_READY;
    return BN_IO_OK;
}
static uint8_t close_tcp(void *p, uint16_t h) {
    (void)p;
    (void)h;
    return BN_IO_OK;
}
static uint8_t write_tcp(void *p, uint16_t h, uint32_t offset, const uint8_t *data, uint16_t n,
                         uint16_t *count) {
    uint16_t want;
    (void)p;
    (void)h;
    if (offset != tx || n > 32)
        bad = 1;
    *count = n > 3 ? 3 : n;
    memcpy(incoming + used, data, *count);
    used += *count;
    tx += *count;
    if (used >= 2) {
        want = (uint16_t)(incoming[0] + 2);
        if (used == want) {
            memset(outgoing, 0, sizeof outgoing);
            read_pos = 0;
            if (incoming[2] == 1) {
                total = 50;
                outgoing[0] = 48;
                outgoing[2] = 0x81;
                outgoing[5] = 1;
                outgoing[7] = 128;
                outgoing[9] = 3;
                outgoing[10] = 63;
                memset(outgoing + 14, 0x22, 16);
                memset(outgoing + 30, 0x11, 16);
                outgoing[48] = 8;
                outgoing[49] = 7;
            } else {
                total = 6;
                outgoing[0] = 4;
                outgoing[2] = 255;
                outgoing[3] = incoming[3];
                outgoing[4] = incoming[4];
                outgoing[5] = 2;
            }
            used = 0;
        }
    }
    return BN_IO_OK;
}
static uint8_t read_tcp(void *p, uint16_t h, uint32_t offset, uint8_t *data, uint16_t n, uint16_t *count,
                        uint8_t *flags) {
    (void)p;
    (void)h;
    if (offset != rx || n > 32)
        bad = 1;
    *count = n > 7 ? 7 : n;
    if (*count > total - read_pos)
        *count = (uint16_t)(total - read_pos);
    memcpy(data, outgoing + read_pos, *count);
    read_pos += *count;
    rx += *count;
    *flags = 0;
    return BN_IO_OK;
}
int main(void) {
    bn_client c;
    uint32_t now;
    int rc = 0;
    static const bn_ops ops = {open_tcp, info, write_tcp, read_tcp, close_tcp};
    uint8_t list[] = {16, 1, 0};
#ifdef BW_WRAP_ALLOC
    armed = 1;
#endif
    CHECK(bn_init(&c, &ops, NULL, "tcp://test:7879", 128, 3, 0, 1000));
    for (now = 0; now < 500 && rc == BN_WAIT; ++now)
        rc = bn_step(&c, now);
    CHECK(rc == BN_READY);
    CHECK(!bad);
    CHECK(bn_request(&c, list, sizeof list, now));
    rc = BN_WAIT;
    while (now < 900 && rc == BN_WAIT)
        rc = bn_step(&c, now++);
    CHECK(rc == BN_RESPONSE);
    CHECK(c.next_exchange == 2);
    CHECK(c.reply_size == 4);
    CHECK(!bad);
    CHECK(bn_dispose(&c));
#ifdef BW_WRAP_ALLOC
    armed = 0;
#endif
    printf("C99 NIO no-allocation PASS: client=%lu, request=%lu, reply=%lu, hello=%lu\n",
           (unsigned long)sizeof c, (unsigned long)sizeof c.request, (unsigned long)sizeof c.reply,
           (unsigned long)sizeof c.hello);
    return 0;
}
