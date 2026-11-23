/* Concrete fn_* mapping, without real I/O: real connectivity has a separate opt-in test. */
#include "botched/client/nio_fujinet.h"
#include "fujinet-nio.h"
#include <string.h>
#include <stdio.h>
static uint8_t result, returned_flags;
static uint16_t returned_count;
static fn_write_diagnostics_t diag;
static int wrong;
uint8_t fn_open(fn_handle_t *h, uint8_t method, const char *url, uint8_t flags) {
    if (method || strcmp(url, "tcp://test:7879") || flags != FN_OPEN_STREAM_NO_PROBE)
        wrong = 1;
    *h = 1;
    return result;
}
uint8_t fn_info(fn_handle_t h, uint16_t *status, uint32_t *length, uint8_t *flags) {
    (void)h;
    *status = 0;
    *length = 0;
    *flags = returned_flags;
    return result;
}
uint8_t fn_read(fn_handle_t h, uint32_t offset, uint8_t *bytes, uint16_t n, uint16_t *read, uint8_t *flags) {
    (void)h;
    (void)offset;
    (void)bytes;
    (void)n;
    *read = returned_count;
    *flags = returned_flags;
    return result;
}
uint8_t fn_write(fn_handle_t h, uint32_t offset, const uint8_t *bytes, uint16_t n, uint16_t *written) {
    (void)h;
    (void)offset;
    (void)bytes;
    (void)n;
    *written = returned_count;
    return result;
}
void fn_write_get_last_diagnostics(fn_write_diagnostics_t *out) { *out = diag; }
uint8_t fn_close(fn_handle_t h) {
    (void)h;
    return result;
}
#define CHECK(x)                                                                                             \
    do {                                                                                                     \
        if (!(x))                                                                                            \
            return __LINE__;                                                                                 \
    } while (0)
int main(void) {
    uint8_t data[32] = {0}, flags;
    uint16_t handle, count;
    result = FN_OK;
    CHECK(bn_fujinet_ops.open(NULL, "tcp://test:7879", &handle) == BN_IO_OK);
    CHECK(!wrong);
    returned_flags = 1;
    CHECK(bn_fujinet_ops.info(NULL, handle, &flags) == BN_IO_OK);
    CHECK(flags == BN_INFO_READY);
    result = FN_ERR_NOT_READY;
    CHECK(bn_fujinet_ops.info(NULL, handle, &flags) == BN_IO_AGAIN);
    CHECK(flags == 0);
    result = FN_ERR_BUSY;
    CHECK(bn_fujinet_ops.info(NULL, handle, &flags) == BN_IO_AGAIN);
    diag.exchanges = 3;
    diag.service_retry = 2;
    diag.zero_accepted = 1;
    CHECK(bn_fujinet_ops.write(NULL, handle, 0, data, 32, &count) == BN_IO_AGAIN);
    diag.zero_accepted = 0;
    CHECK(bn_fujinet_ops.write(NULL, handle, 0, data, 32, &count) == BN_IO_LOST);
    diag.service_retry = 3;
    result = FN_ERR_NOT_READY;
    CHECK(bn_fujinet_ops.write(NULL, handle, 0, data, 32, &count) == BN_IO_AGAIN);
    result = FN_ERR_TIMEOUT;
    CHECK(bn_fujinet_ops.write(NULL, handle, 0, data, 32, &count) == BN_IO_LOST);
    result = FN_OK;
    returned_count = 3;
    CHECK(bn_fujinet_ops.write(NULL, handle, 0, data, 32, &count) == BN_IO_OK);
    CHECK(count == 3);
    returned_flags = FN_READ_TRUNCATED | FN_READ_MORE_AVAILABLE | FN_READ_AVAILABLE;
    returned_count = 32;
    CHECK(bn_fujinet_ops.read(NULL, handle, 0, data, 32, &count, &flags) == BN_IO_OK);
    CHECK(count == 32);
    CHECK(flags == 0);
    returned_flags = FN_READ_EOF;
    returned_count = 0;
    CHECK(bn_fujinet_ops.read(NULL, handle, 32, data, 32, &count, &flags) == BN_IO_OK);
    CHECK(flags == BN_EOF);
    result = FN_ERR_NOT_READY;
    CHECK(bn_fujinet_ops.read(NULL, handle, 32, data, 32, &count, &flags) == BN_IO_AGAIN);
    result = FN_ERR_BUSY;
    CHECK(bn_fujinet_ops.read(NULL, handle, 32, data, 32, &count, &flags) == BN_IO_AGAIN);
    result = FN_ERR_TRANSPORT;
    CHECK(bn_fujinet_ops.read(NULL, handle, 32, data, 32, &count, &flags) == BN_IO_LOST);
    result = FN_OK;
    CHECK(bn_fujinet_ops.close(NULL, handle) == BN_IO_OK);
    puts("NIO binding NotReady/DeviceBusy/ambiguous-write mapping PASS");
    return 0;
}
