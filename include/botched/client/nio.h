/* Portable C99 Botched/NIO transport. No allocation, clock, sleep or renderer.
 * Call step() from the foreground, with a monotonic wrapping millisecond clock.
 * Each step issues at most one NetworkDevice operation. Service calls themselves
 * may block/retry inside the platform library; host must budget that separately. */
#ifndef BOTCHED_CLIENT_NIO_H
#define BOTCHED_CLIENT_NIO_H
#include "botched/wire/v1.h"
#ifdef __cplusplus
extern "C" {
#endif
#define BN_CHUNK 32u
#define BN_REQUEST 96u
#define BN_REPLY 256u
/* Service callback results. AGAIN proves zero progress; LOST permits exact read
 * replay only. An ambiguous write must be LOST, never AGAIN. Successful callbacks
 * return actual accepted counts, not requested counts. Handles are nonzero. */
enum { BN_IO_OK, BN_IO_AGAIN, BN_IO_LOST, BN_IO_ERROR };
/* Info availability permits trying I/O; it does not prove TCP connect completion. */
enum { BN_INFO_READY = 1, BN_PEER_CLOSED = 2, BN_EOF = 1 };
typedef struct {
    uint8_t (*open)(void *, const char *, uint16_t *);
    uint8_t (*info)(void *, uint16_t, uint8_t *);
    uint8_t (*write)(void *, uint16_t, uint32_t, const uint8_t *, uint16_t, uint16_t *);
    uint8_t (*read)(void *, uint16_t, uint32_t, uint8_t *, uint16_t, uint16_t *, uint8_t *);
    uint8_t (*close)(void *, uint16_t);
} bn_ops;
enum { BN_WAIT = 0, BN_READY = 1, BN_RESPONSE = 2, BN_FAILED = -1 };
enum {
    BN_ERROR_NONE,
    BN_ERROR_CONFIG,
    BN_ERROR_OPEN,
    BN_ERROR_LINK,
    BN_ERROR_CLOSE,
    BN_ERROR_TIMEOUT,
    BN_ERROR_PROTOCOL,
    BN_ERROR_SESSION,
    BN_ERROR_SEQUENCE
};
typedef struct {
    const bn_ops *ops;
    void *context;
    const char *url; /* caller owns unchanged NUL-terminated tcp:// URL until disposal */
    uint8_t request[BN_REQUEST], reply[BN_REPLY], hello[32];
    uint8_t epoch[16], secret[16];
    bw_stream stream;
    uint32_t read_cursor, write_cursor, next_exchange, started, timeout_ms;
    uint16_t handle, payload, request_size, reply_size, sent, received, expected;
    uint8_t profile, phase, pending, negotiated, resume, mode, retries, recoveries;
    uint8_t error, apply, eof;
} bn_client;
/* init owns no NIO library/channel initialization. timeout must be 1..INT32_MAX.
 * The first step opens NetworkDevice TCP, waits for Info, then negotiates HELLO. */
int bn_init(bn_client *, const bn_ops *, void *, const char *url, uint16_t payload, uint8_t profile,
            uint32_t now, uint32_t timeout_ms);
int bn_step(bn_client *, uint32_t now);
/* Complete payload including opcode/exchange; retains an exact private copy.
 * Only legal while READY/RESPONSE, and exchange must equal next_exchange (no wrap).
 * Return 0 leaves all state unchanged. The reply view expires on next request. */
int bn_request(bn_client *, const uint8_t *, uint16_t, uint32_t now);
/* Explicit disconnect/resume, including an uncertain pending request. Retains
 * epoch, secret, exchange, stream/acknowledgement and request. */
int bn_reconnect(bn_client *, uint32_t now);
/* Close releases the owned handle. On failure it is retained for another dispose;
 * do not reuse/init the object until this returns 1 or its channel is shut down. */
int bn_dispose(bn_client *);
/* After BN_RESPONSE: payload at reply+2, reply_size bytes. For PAGE, apply indicates
 * fresh records; bw_stream_accept already checked the whole page before publication.
 * stream.ack_version never regresses. Use a new client for a different epoch/match,
 * or explicitly reinitialize stream preserving/resetting ack as B2 documents. */
#ifdef __cplusplus
}
#endif
#endif
