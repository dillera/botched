#include "botched/client/nio_fujinet.h"
#include "fujinet-nio.h"
static uint8_t status(uint8_t rc) {
    if (rc == FN_OK)
        return BN_IO_OK;
    if (rc == FN_ERR_BUSY || rc == FN_ERR_NOT_READY)
        return BN_IO_AGAIN;
    if (rc == FN_ERR_IO || rc == FN_ERR_TRANSPORT || rc == FN_ERR_TIMEOUT)
        return BN_IO_LOST;
    return BN_IO_ERROR;
}
static uint8_t open_tcp(void *ctx, const char *url, uint16_t *handle) {
    (void)ctx;
    /* Method 0, no translation, self-framed stream; never evict another client. */
    return status(fn_open(handle, 0, url, FN_OPEN_STREAM_NO_PROBE));
}
static uint8_t info_tcp(void *ctx, uint16_t handle, uint8_t *state) {
    uint16_t http = 0;
    uint32_t length = 0;
    uint8_t flags = 0, rc;
    (void)ctx;
    rc = fn_info(handle, &http, &length, &flags);
    *state = 0;
    /* NetworkDevice v1 Info flags describe metadata, not socket state. The
     * simple fn_info API does not expose TCP pseudo-headers. Info success allows
     * attempting Write; Write/Read NotReady and EOF remain authoritative. */
    if (rc == FN_OK)
        *state = BN_INFO_READY;
    return status(rc);
}
static uint8_t write_tcp(void *ctx, uint16_t handle, uint32_t offset, const uint8_t *bytes, uint16_t size,
                         uint16_t *written) {
    uint8_t rc;
    fn_write_diagnostics_t d;
    (void)ctx;
    *written = 0;
    rc = fn_write(handle, offset, bytes, size, written);
    if (rc == FN_OK)
        return BN_IO_OK;
    fn_write_get_last_diagnostics(&d);
    /* fn_write may report zero after some internal partial writes. Only explicit
     * zero-progress exchanges permit another call on this TCP stream. */
    if (!*written && (rc == FN_ERR_BUSY || rc == FN_ERR_NOT_READY) && d.exchanges &&
        (uint32_t)d.transport_retry + d.service_retry + d.zero_accepted == d.exchanges)
        return BN_IO_AGAIN;
    return BN_IO_LOST;
}
static uint8_t read_tcp(void *ctx, uint16_t handle, uint32_t offset, uint8_t *bytes, uint16_t size,
                        uint16_t *read, uint8_t *flags) {
    uint8_t rc, f = 0;
    (void)ctx;
    *read = 0;
    *flags = 0;
    rc = fn_read(handle, offset, bytes, size, read, &f);
    if (f & FN_READ_EOF)
        *flags = BN_EOF;
    /* TRUNCATED means the requested chunk was filled, not data loss. */
    return status(rc);
}
static uint8_t close_tcp(void *ctx, uint16_t handle) {
    (void)ctx;
    return status(fn_close(handle));
}
const bn_ops bn_fujinet_ops = {open_tcp, info_tcp, write_tcp, read_tcp, close_tcp};
