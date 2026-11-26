/* Compiled as strict C99 and linked without the C++ engine/runtime. */
#include "botched/wire/v1.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#ifdef BW_WRAP_ALLOC
static int armed = 0;
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
static void seen(void *context, const uint8_t *p, size_t n) {
    unsigned *count = (unsigned *)context;
    (void)p;
    (void)n;
    ++*count;
}
int main(void) {
    static const uint8_t submit[] = {0x14, 1, 0, 0, 3, 0, 0, 0, 1, 0, 0, 0, 1, 4, 7, 1, 1, 7, 0, 0, 0};
    static const uint8_t context[] = {1, 3, 2, 'm', '1'};
    uint8_t body[96], output[240], sentinel[240];
    bw_writer w;
    bw_reader r;
    size_t n = 0, i;
    unsigned calls = 0;
    bw_stream state;
    int apply = 0;
#ifdef BW_WRAP_ALLOC
    armed = 1;
#endif
    bw_writer_init(&w, body, sizeof body);
    bw_write_u8(&w, 0);
    bw_write_u32(&w, 3);
    bw_write_u32(&w, 1);
    bw_write_u8(&w, 1);
    bw_write_u8(&w, 4);
    bw_write_u8(&w, 7);
    bw_write_u8(&w, 1);
    bw_write_u8(&w, 1);
    bw_write_u32(&w, 7);
    CHECK(!w.error);
    CHECK(bw_encode_packet(0x14, 1, body, w.pos, output, sizeof output, &n));
    CHECK(n == sizeof submit);
    CHECK(!memcmp(output, submit, n));
    bw_reader_init(&r, output + 3, n - 3);
    CHECK(bw_read_u8(&r) == 0);
    CHECK(bw_read_i32(&r) == 3);
    CHECK(bw_read_i32(&r) == 1);
    for (i = 0; i < n; ++i)
        CHECK(!bw_validate_packet(output, i, 240));
    memset(sentinel, 0xa5, sizeof sentinel);
    CHECK(!bw_encode_packet(0x14, 1, body, w.pos, sentinel, 3, &n));
    CHECK(sentinel[0] == 0xa5);
    /* Error result after a good context: validation must not invoke either callback. */
    bw_writer_init(&w, output, sizeof output);
    bw_write_u8(&w, 0x82);
    bw_write_u16(&w, 1);
    bw_write_u32(&w, 1);
    bw_write_u32(&w, 0);
    bw_write_u32(&w, 8);
    bw_write_u32(&w, 0);
    bw_write_u8(&w, 0x11);
    bw_write_u8(&w, 1);
    bw_write_u8(&w, 8);
    bw_write_bytes(&w, context, sizeof context);
    bw_write_u8(&w, 0x7f);
    bw_write_u8(&w, 1);
    bw_write_u8(&w, 0);
    CHECK(!bw_visit_page(output, w.pos, seen, &calls));
    CHECK(calls == 0);
    bw_stream_init(&state, 100);
    CHECK(!bw_stream_accept(&state, output, w.pos, &apply));
    CHECK(state.ack_version == 100);
#ifdef BW_WRAP_ALLOC
    armed = 0;
#endif
    printf("C99 no-allocation checks passed; sizeof(bw_stream)=%lu, reader=%lu, writer=%lu\n",
           (unsigned long)sizeof state, (unsigned long)sizeof r, (unsigned long)sizeof w);
    return 0;
}
