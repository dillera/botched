#include "botched/wire/v1.h"
#include <limits.h>
#include <string.h>

void bw_reader_init(bw_reader *r, const uint8_t *p, size_t n) {
    r->data = p;
    r->size = n;
    r->pos = 0;
    r->error = (!p && n);
}
const uint8_t *bw_read_bytes(bw_reader *r, size_t n) {
    const uint8_t *p;
    if (r->error || n > r->size - r->pos) {
        r->error = 1;
        return NULL;
    }
    p = r->data ? r->data + r->pos : NULL;
    r->pos += n;
    return p;
}
uint8_t bw_read_u8(bw_reader *r) {
    const uint8_t *p = bw_read_bytes(r, 1);
    return p ? p[0] : 0;
}
uint16_t bw_read_u16(bw_reader *r) {
    const uint8_t *p = bw_read_bytes(r, 2);
    return p ? (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)) : 0;
}
uint32_t bw_read_u32(bw_reader *r) {
    const uint8_t *p = bw_read_bytes(r, 4);
    return p ? ((uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24)) : 0;
}
int32_t bw_read_i32(bw_reader *r) {
    uint32_t v = bw_read_u32(r);
    return v <= INT32_MAX ? (int32_t)v : -(int32_t)(UINT32_MAX - v) - 1;
}
void bw_writer_init(bw_writer *w, uint8_t *p, size_t n) {
    w->data = p;
    w->capacity = n;
    w->pos = 0;
    w->error = (!p && n);
}
void bw_write_bytes(bw_writer *w, const uint8_t *p, size_t n) {
    if (w->error || n > w->capacity - w->pos || (!p && n)) {
        w->error = 1;
        return;
    }
    if (n)
        memmove(w->data + w->pos, p, n);
    w->pos += n;
}
void bw_write_u8(bw_writer *w, uint8_t v) { bw_write_bytes(w, &v, 1); }
void bw_write_u16(bw_writer *w, uint16_t v) {
    uint8_t b[2];
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    bw_write_bytes(w, b, 2);
}
void bw_write_u32(bw_writer *w, uint32_t v) {
    uint8_t b[4];
    b[0] = (uint8_t)v;
    b[1] = (uint8_t)(v >> 8);
    b[2] = (uint8_t)(v >> 16);
    b[3] = (uint8_t)(v >> 24);
    bw_write_bytes(w, b, 4);
}
static int done(bw_reader *r) { return !r->error && r->pos == r->size; }
static int byte_range(bw_reader *r, unsigned lo, unsigned hi) {
    unsigned v = bw_read_u8(r);
    return !r->error && v >= lo && v <= hi;
}
static int mask(bw_reader *r, unsigned allowed) { return (bw_read_u8(r) & ~allowed) == 0 && !r->error; }
static int zeros(bw_reader *r, size_t n) {
    const uint8_t *p = bw_read_bytes(r, n);
    size_t i;
    if (!p)
        return 0;
    for (i = 0; i < n; ++i)
        if (p[i])
            return 0;
    return 1;
}
static int ident(unsigned c) {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '.' ||
           c == '_' || c == ':' || c == '-';
}
static int string(bw_reader *r, unsigned max, int empty, int token) {
    unsigned n = bw_read_u8(r), i;
    const uint8_t *p;
    if (n > max || (!n && !empty))
        return 0;
    p = bw_read_bytes(r, n);
    if (!p)
        return 0;
    for (i = 0; i < n; ++i)
        if (token ? !ident(p[i]) : (p[i] < 32 || p[i] > 126))
            return 0;
    return 1;
}
static int key(bw_reader *r) {
    unsigned m = bw_read_u8(r);
    if (m == 0)
        return !r->error;
    if (m == 1) {
        bw_read_u32(r);
        return !r->error;
    }
    return m == 2 && string(r, 64, 0, 1);
}
static int scope(bw_reader *r) {
    unsigned m = bw_read_u8(r);
    return m == 0 ? !r->error : m == 1 && string(r, 16, 0, 1);
}
static int pid(bw_reader *r) { return byte_range(r, 1, 4); }
static int xy(bw_reader *r, int outside) {
    unsigned x = bw_read_u8(r), y = bw_read_u8(r);
    return !r->error && (x < (outside ? 21u : 20u) || (outside && x == 255)) &&
           (y < (outside ? 17u : 16u) || (outside && y == 255));
}
static int positive(bw_reader *r) {
    uint32_t v = bw_read_u32(r);
    return !r->error && v > 0 && v <= INT32_MAX;
}
static int handle_id(bw_reader *r) { return bw_read_u32(r) != 0 && !r->error; }
static int counter(bw_reader *r) { return bw_read_u32(r) <= INT32_MAX && !r->error; }
static int rows(bw_reader *r) {
    unsigned n = bw_read_u8(r), i, prev = 0;
    if (n < 2 || n > 4)
        return 0;
    for (i = 0; i < n; ++i) {
        unsigned p = bw_read_u8(r);
        if (p <= prev || p > 4 || !counter(r) || !counter(r) || !counter(r))
            return 0;
        prev = p;
    }
    return 1;
}
static int device(bw_reader *r, unsigned expected) {
    unsigned tag = bw_read_u8(r), id = bw_read_u8(r);
    return !r->error && tag == expected && (tag ? (id >= 1 && id <= 9) : id == 0);
}
static int event(bw_reader *r) {
    unsigned e = bw_read_u8(r), v, flags, by;
    uint32_t ver = bw_read_u32(r);
    if (!ver || ver > INT32_MAX || !byte_range(r, 1, 200) || !byte_range(r, 0, 3) || !positive(r))
        return 0;
    switch (e) {
    case 1:
        return mask(r, 15);
    case 2:
        return byte_range(r, 1, 200);
    case 3:
        return byte_range(r, 1, 200) && rows(r);
    case 4:
        return byte_range(r, 1, 15) && byte_range(r, 0, 2) && rows(r);
    case 5:
    case 7:
        return pid(r) && byte_range(r, 0, 8);
    case 6:
        return pid(r) && byte_range(r, 0, 8) && byte_range(r, 0, 3);
    case 8:
        return pid(r) && xy(r, 0) && xy(r, 0) && byte_range(r, 0, 1);
    case 9:
        return pid(r) && pid(r) && xy(r, 0) && xy(r, 0);
    case 10:
        return pid(r) && byte_range(r, 0, 3) && xy(r, 1) && byte_range(r, 0, 1);
    case 11:
        return pid(r) && byte_range(r, 0, 3) && byte_range(r, 0, 3);
    case 12:
        if (!positive(r))
            return 0;
        v = bw_read_u16(r);
        return v >= 1 && v <= 999 && xy(r, 0);
    case 13:
    case 14:
        return pid(r) && positive(r) && xy(r, 0);
    case 15:
        return pid(r) && positive(r) && xy(r, 0) && counter(r);
    case 16:
        return positive(r) && xy(r, 0) && xy(r, 0);
    case 17:
        return positive(r) && xy(r, 0) && byte_range(r, 0, 1);
    case 18:
        if (!pid(r) || !byte_range(r, 0, 25))
            return 0;
        v = bw_read_u16(r);
        return v >= 1 && v <= 320 && byte_range(r, 0, 1);
    case 19:
        return pid(r) && xy(r, 0) && byte_range(r, 1, 3);
    case 20:
        if (!pid(r))
            return 0;
        v = bw_read_u8(r);
        if (v > 3 || !xy(r, 0))
            return 0;
        flags = bw_read_u8(r);
        by = bw_read_u8(r);
        if (flags > 1 || by > 4 || (!flags && by))
            return 0;
        if (v == 1 && (flags || by))
            return 0;
        return (v < 2 || flags) && device(r, v >= 2 ? v - 1 : 0);
    case 21:
        return pid(r) && xy(r, 0) && xy(r, 0);
    case 22:
        return pid(r) && xy(r, 0) && byte_range(r, 0, 3);
    case 23:
        return pid(r) && byte_range(r, 1, 9) && xy(r, 0) && byte_range(r, 0, 1) && byte_range(r, 0, 4) &&
               pid(r);
    case 24:
        return byte_range(r, 1, 9) && xy(r, 0) && byte_range(r, 0, 4) && byte_range(r, 1, 15);
    case 25:
        return byte_range(r, 1, 9) && pid(r) && byte_range(r, 1, 3) && byte_range(r, 0, 4) &&
               byte_range(r, 0, 1);
    case 26:
        return pid(r) && byte_range(r, 1, 9) && xy(r, 0) && byte_range(r, 0, 4) && pid(r);
    case 27:
        return byte_range(r, 1, 9) && xy(r, 0) && byte_range(r, 1, 2) && byte_range(r, 0, 4);
    case 28:
        return byte_range(r, 1, 9) && xy(r, 0) && byte_range(r, 0, 4) && pid(r) && byte_range(r, 0, 3) &&
               byte_range(r, 1, 2) && byte_range(r, 1, 3) && byte_range(r, 0, 1);
    case 29:
        return pid(r) && byte_range(r, 1, 1) && xy(r, 0);
    case 30:
        if (!pid(r))
            return 0;
        by = bw_read_u8(r);
        v = bw_read_u8(r);
        if (v > 4 || by != (v == 4 ? 8u : (v == 1 || v == 3 ? 3u : 1u)) || !pid(r) || !counter(r))
            return 0;
        return device(r, v == 4 ? 0 : (v < 2 ? 1 : 2));
    default:
        return 0;
    }
}
static int record_body(unsigned tag, const uint8_t *p, size_t n) {
    bw_reader r;
    unsigned a, b, c, d, i, flags;
    uint32_t x, y;
    bw_reader_init(&r, p, n);
    switch (tag) {
    case 1:
        if (!string(&r, 16, 0, 1))
            return 0;
        break;
    case 2:
        if (!byte_range(&r, 2, 4) || !byte_range(&r, 0, 3) || !byte_range(&r, 1, 200) ||
            bw_read_u16(&r) > 1000)
            return 0;
        if (p[1] >= p[0])
            return 0;
        break;
    case 3:
        if (!pid(&r) || !byte_range(&r, 0, 1) || !string(&r, 64, 0, 0))
            return 0;
        break;
    case 4:
        if (!byte_range(&r, 2, 4))
            return 0;
        break;
    case 5:
        bw_read_i32(&r);
        bw_read_i32(&r);
        bw_read_i32(&r);
        if (!counter(&r))
            return 0;
        a = bw_read_u8(&r);
        b = bw_read_u8(&r);
        c = bw_read_u8(&r);
        if (a > 1 || b > 23 || c > 4 || (a != (b == 0)) ||
            (b == 0 ? c > 1 : (b == 14 ? (c != 2 && c != 3) : c != 4)) || !key(&r))
            return 0;
        break;
    case 6:
        if (!byte_range(&r, 1, 15))
            return 0;
        break;
    case 7:
        if (!string(&r, 32, 1, 1) || !key(&r))
            return 0;
        break;
    case 8:
        if (!byte_range(&r, 1, 1) || !pid(&r))
            return 0;
        x = bw_read_u32(&r);
        a = bw_read_u8(&r);
        if (a > 1 || (a && x))
            return 0;
        break;
    case 9:
        if (!byte_range(&r, 0, 2) || !byte_range(&r, 1, 200) || !byte_range(&r, 0, 3) ||
            !byte_range(&r, 1, 200))
            return 0;
        a = bw_read_u16(&r);
        if (a > 1000 || !positive(&r) || !handle_id(&r))
            return 0;
        flags = bw_read_u8(&r);
        if (flags > 3 || ((flags & 1) != 0) != (a != 0) || !byte_range(&r, 2, 4))
            return 0;
        b = bw_read_u16(&r);
        if (b > 324 || !byte_range(&r, 0, 26) || !byte_range(&r, 0, 9) || !byte_range(&r, 0, 9) ||
            !mask(&r, 15) || !mask(&r, 15))
            return 0;
        if (flags & 2)
            bw_read_bytes(&r, 8);
        else if (!zeros(&r, 8))
            return 0;
        break;
    case 10:
        if (!pid(&r))
            return 0;
        flags = bw_read_u8(&r);
        if (flags > 15)
            return 0;
        if (flags & 1) {
            if (!zeros(&r, 2))
                return 0;
        } else if (!xy(&r, 0))
            return 0;
        if (!byte_range(&r, 0, 3) || !byte_range(&r, 0, 3))
            return 0;
        if (flags & 2) {
            if ((flags & 1) || !positive(&r))
                return 0;
        } else if (!zeros(&r, 4))
            return 0;
        if (!counter(&r) || !counter(&r) || !counter(&r))
            return 0;
        if (flags & 4) {
            if (!counter(&r))
                return 0;
        } else if (!zeros(&r, 4))
            return 0;
        a = bw_read_u8(&r);
        if (flags & 8) {
            if (a < 1 || a > 4 || a == p[0] || (flags & 1))
                return 0;
        } else if (a)
            return 0;
        break;
    case 11:
        if (!positive(&r))
            return 0;
        a = bw_read_u8(&r);
        b = bw_read_u8(&r);
        c = bw_read_u8(&r);
        if (c > 4 || (c ? (a || b) : (a >= 20 || b >= 16)))
            return 0;
        break;
    case 12:
        if (!byte_range(&r, 0, 25) || !byte_range(&r, 0, 1))
            return 0;
        break;
    case 13:
        a = bw_read_u8(&r);
        if (a < 1 || a > 2 || !byte_range(&r, 1, 9) || !byte_range(&r, 0, a == 1 ? 1 : 2) ||
            !byte_range(&r, 0, 4))
            return 0;
        break;
    case 14:
        a = bw_read_u8(&r);
        b = bw_read_u8(&r);
        c = bw_read_u8(&r);
        d = bw_read_u8(&r);
        x = bw_read_u16(&r);
        y = bw_read_u32(&r);
        if (a > 3 || d < 1 || d > 200 || x > 1000 || y > INT32_MAX)
            return 0;
        if (a == 3) {
            if (b || c || y || !zeros(&r, 1))
                return 0;
            b = bw_read_u8(&r);
            c = bw_read_u8(&r);
            d = bw_read_u8(&r);
            if (c < 2 || c > 4 || b > c || d >= c || !zeros(&r, 4))
                return 0;
        } else {
            if (b < 1 || b > 200 || c > 3 || !y || !byte_range(&r, 2, 4) || !zeros(&r, 2))
                return 0;
            b = bw_read_u8(&r);
            if (b > 3 || !mask(&r, 15) || !mask(&r, 15))
                return 0;
            c = bw_read_u8(&r);
            d = bw_read_u8(&r);
            if (c > 15 || (a == 2 ? !c : c != 0) || d != b)
                return 0;
            c = 0;
            for (i = 0; i < d; ++i) {
                unsigned id = bw_read_u8(&r);
                if (id <= c || id > 4 || !byte_range(&r, 0, 2))
                    return 0;
                c = id;
            }
        }
        break;
    case 15:
        if (!byte_range(&r, 1, 3))
            return 0;
        x = bw_read_u32(&r);
        y = bw_read_u32(&r);
        a = bw_read_u8(&r);
        if (x > 65535 || y > x || a > 64 || a > x - y || a != (x - y > 64 ? 64 : x - y))
            return 0;
        for (i = 0; i < a; ++i)
            if (!byte_range(&r, 32, 126))
                return 0;
        break;
    case 16:
        if (!handle_id(&r) || !string(&r, 32, 0, 1))
            return 0;
        a = bw_read_u8(&r);
        b = bw_read_u8(&r);
        c = bw_read_u16(&r);
        d = bw_read_u16(&r);
        if (a < 8 || a > 20 || b < 8 || b > 16 || c != a * b || d > c)
            return 0;
        break;
    case 17:
        a = bw_read_u16(&r);
        b = bw_read_u8(&r);
        if (b < 1 || b > 128 || a + b > 320)
            return 0;
        for (i = 0; i < (b + 1) / 2; ++i) {
            c = bw_read_u8(&r);
            if ((c & 15) > 13 || (c >> 4) > 13 || ((b & 1) && i == b / 2 && (c >> 4)))
                return 0;
        }
        break;
    case 18:
        if (!xy(&r, 0))
            return 0;
        a = bw_read_u16(&r);
        if (a > 999 || !byte_range(&r, 0, 3))
            return 0;
        break;
    case 19:
        bw_read_u16(&r);
        break;
    case 20:
        if (!handle_id(&r) || !string(&r, 32, 0, 1))
            return 0;
        a = bw_read_u8(&r);
        b = bw_read_u8(&r);
        if (a < 2 || b > 4 || a > b)
            return 0;
        break;
    case 21:
        if (!event(&r))
            return 0;
        break;
    case 22:
        bw_read_i32(&r);
        if (bw_read_u32(&r) > 32768)
            return 0;
        break;
    default:
        return 0;
    }
    return done(&r);
}
int bw_validate_record(const uint8_t *p, size_t n) {
    return p && n >= 2 && n <= 101 && p[1] == n - 2 && record_body(p[0], p + 2, n - 2);
}
int bw_validate_packet(const uint8_t *p, size_t n, uint16_t max_payload) {
    bw_reader r;
    unsigned op, a, b, c, i;
    uint32_t x, y, z;
    uint16_t ex;
    if (!p || max_payload < 128 || max_payload > 240 || n < 3 || n > max_payload)
        return 0;
    bw_reader_init(&r, p, n);
    op = bw_read_u8(&r);
    ex = bw_read_u16(&r);
    switch (op) {
    case 1:
        a = bw_read_u8(&r);
        b = bw_read_u8(&r);
        bw_read_u8(&r);
        c = bw_read_u16(&r);
        if (a > b || c < 128 || c > 240 || !byte_range(&r, 1, 3))
            return 0;
        if (bw_read_u32(&r) & ~63UL)
            return 0;
        a = bw_read_u8(&r);
        if (a > 1 || ex)
            return 0;
        if (a)
            bw_read_bytes(&r, 16);
        else if (!zeros(&r, 16))
            return 0;
        break;
    case 2:
        if (!byte_range(&r, 1, 2) || !string(&r, 16, 0, 1))
            return 0;
        bw_read_i32(&r);
        if (!string(&r, 64, 1, 0))
            return 0;
        break;
    case 3:
        if (!handle_id(&r))
            return 0;
        bw_read_u32(&r);
        break;
    case 4:
    case 0x83:
        if (!handle_id(&r))
            return 0;
        break;
    case 0x10:
        break;
    case 0x11:
        a = bw_read_u8(&r);
        if (a > 63 || !string(&r, 32, !(a & 1), 1) || (!(a & 1) && p[4]))
            return 0;
        b = bw_read_u8(&r);
        c = bw_read_u8(&r);
        i = bw_read_u8(&r);
        x = bw_read_u16(&r);
        y = bw_read_u32(&r);
        if ((a & 2) ? b < 2 || b > 4 : b != 0)
            return 0;
        if ((a & 4) ? c > 3 : c != 0)
            return 0;
        if ((a & 8) ? i < 1 || i > 200 : i != 0)
            return 0;
        if ((a & 16) ? x > 1000 : x != 0)
            return 0;
        if ((a & 32) ? y > 86400 : y != 0)
            return 0;
        break;
    case 0x12:
        if (!string(&r, 16, 0, 1))
            return 0;
        break;
    case 0x13:
    case 0x15:
    case 0x17:
    case 0x18:
        if (!scope(&r))
            return 0;
        break;
    case 0x14:
        a = bw_read_u8(&r);
        if (a > 1)
            return 0;
        if (a)
            bw_read_i32(&r);
        bw_read_i32(&r);
        bw_read_i32(&r);
        for (i = 0; i < 3; ++i) {
            a = bw_read_u8(&r);
            if (a > 8 && a != 255)
                return 0;
        }
        if (!byte_range(&r, 0, 1) || !key(&r))
            return 0;
        break;
    case 0x16:
        if (!scope(&r))
            return 0;
        bw_read_i32(&r);
        a = bw_read_u16(&r);
        if (a < 1 || a > 16)
            return 0;
        break;
    case 0x81:
        if (ex || !byte_range(&r, 1, 1) || !zeros(&r, 1))
            return 0;
        a = bw_read_u16(&r);
        if (a < 128 || a > 240 || !byte_range(&r, 1, 3) || bw_read_u32(&r) != 63)
            return 0;
        bw_read_bytes(&r, 32);
        bw_read_u16(&r);
        if (bw_read_u16(&r) != 1800)
            return 0;
        break;
    case 0x82:
        if (!handle_id(&r))
            return 0;
        x = bw_read_u32(&r);
        y = bw_read_u32(&r);
        z = bw_read_u32(&r);
        a = bw_read_u8(&r);
        b = bw_read_u8(&r);
        c = bw_read_u8(&r);
        if ((a != 2 && (a < 0x10 || a > 0x18)) || b > 3 || !y || y > BW_MAX_TRANSFER || x > y || c > y - x ||
            c != n - 22 || !c || z > INT32_MAX || ((b & 1) != 0) != (x + c == y))
            return 0;
        if ((b & 2) ? z != 0 : ((a == 0x15 || a == 0x16) ? z == 0 : (a != 0x14 && a != 0x18 && z != 0)))
            return 0;
        /* Even at P128 the entire remainder fits: splitting it cannot be greedy. */
        if (!(b & 1) && y - x <= BW_MIN_PAYLOAD - 22)
            return 0;
        while (!r.error && r.pos < r.size) {
            size_t start = r.pos;
            bw_read_u8(&r);
            a = bw_read_u8(&r);
            bw_read_bytes(&r, a);
            if (r.error || !bw_validate_record(p + start, a + 2))
                return 0;
        }
        break;
    case 0xff:
        if (!byte_range(&r, 1, 10))
            return 0;
        break;
    default:
        return 0;
    }
    return done(&r);
}
int bw_encode_record(uint8_t tag, const uint8_t *body, size_t n, uint8_t *out, size_t cap, size_t *written) {
    if (!out || !written || n > 99 || cap < n + 2 || (!body && n) || !record_body(tag, body, n))
        return 0;
    if (n)
        memmove(out + 2, body, n);
    out[0] = tag;
    out[1] = (uint8_t)n;
    *written = n + 2;
    return 1;
}
int bw_encode_packet(uint8_t op, uint16_t ex, const uint8_t *body, size_t n, uint8_t *out, size_t cap,
                     size_t *written) {
    uint8_t temp[240];
    if (!out || !written || n > 237 || cap < n + 3 || (!body && n))
        return 0;
    temp[0] = op;
    temp[1] = (uint8_t)ex;
    temp[2] = (uint8_t)(ex >> 8);
    if (n)
        memcpy(temp + 3, body, n);
    if (!bw_validate_packet(temp, n + 3, 240))
        return 0;
    memmove(out, temp, n + 3);
    *written = n + 3;
    return 1;
}
int bw_visit_page(const uint8_t *p, size_t n, bw_record_callback cb, void *ctx) {
    size_t pos;
    if (!bw_validate_packet(p, n, 240) || p[0] != 0x82)
        return 0;
    if (cb)
        for (pos = 22; pos < n; pos += p[pos + 1] + 2)
            cb(ctx, p + pos, p[pos + 1] + 2);
    return 1;
}
void bw_stream_init(bw_stream *s, uint32_t ack) {
    memset(s, 0, sizeof(*s));
    s->ack_version = ack;
}
static uint32_t u32at(const uint8_t *p) {
    bw_reader r;
    bw_reader_init(&r, p, 4);
    return bw_read_u32(&r);
}
static uint16_t u16at(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static int text_record(bw_stream *s, const uint8_t *b, unsigned field) {
    uint32_t total = u32at(b + 1), off = u32at(b + 5);
    if (b[0] != field || off != s->text_offset)
        return 0;
    if (!off) {
        s->text_total = total;
        s->text_field = (uint8_t)field;
    }
    if (total != s->text_total || field != s->text_field)
        return 0;
    s->text_offset += b[9];
    if (s->text_offset == total) {
        s->text_offset = 0;
        s->text_total = 0;
        s->text_field = 0;
        return 2;
    }
    return 1;
}
static int grammar(bw_stream *s, const uint8_t *rec) {
    unsigned tag = rec[0], n = rec[1], i;
    const uint8_t *b = rec + 2;
    int t;
    uint32_t v, seq;
    if (s->flags & 2) {
        if (s->stage == 0) {
            if (tag != 6)
                return 0;
            s->stage = 1;
        } else if (s->stage == 1) {
            if (tag != 7)
                return 0;
            s->stage = 2;
        } else if (s->stage == 2) {
            if (tag != 15 || (t = text_record(s, b, 3)) == 0)
                return 0;
            if (t == 2)
                s->stage = 255;
        } else
            return 0;
        return 1;
    }
    if (s->origin == 0x10) {
        if (!s->stage) {
            if (tag != 19)
                return 0;
            s->remaining = u16at(b);
            s->stage = s->remaining ? 1 : 255;
        } else if (s->stage == 1) {
            if (tag != 20)
                return 0;
            s->stage = 2;
        } else if (s->stage == 2 || s->stage == 3) {
            if (tag != 15 || (t = text_record(s, b, s->stage - 1)) == 0)
                return 0;
            if (t == 2) {
                if (s->stage == 2)
                    s->stage = 3;
                else
                    s->stage = --s->remaining ? 1 : 255;
            }
        } else
            return 0;
        return 1;
    }
    if (!s->stage) {
        if (tag != 1)
            return 0;
        s->stage = 1;
        return 1;
    }
    switch (s->origin) {
    case 2:
        if (s->stage != 1 || tag != 8)
            return 0;
        s->stage = 255;
        break;
    case 0x11:
        if (s->stage != 1 || tag != 2)
            return 0;
        s->stage = 255;
        break;
    case 0x12:
        if (s->stage == 1 && tag == 3)
            s->stage = 2;
        else if (s->stage == 2 && tag == 8)
            s->stage = 255;
        else
            return 0;
        break;
    case 0x13:
        if (s->stage != 1 || tag != 4)
            return 0;
        s->stage = 255;
        break;
    case 0x14:
        if (s->stage == 1) {
            if (tag != 5 || u32at(b + 12) != s->high_water)
                return 0;
            s->stage = 2;
        } else if (s->stage == 2) {
            if (tag != 15 || (t = text_record(s, b, 3)) == 0)
                return 0;
            if (t == 2)
                s->stage = 255;
        } else
            return 0;
        break;
    case 0x18:
        if (s->stage != 1 || tag != 14 || u32at(b + 6) != s->high_water)
            return 0;
        s->stage = 255;
        break;
    case 0x16:
        if (s->stage == 1) {
            bw_reader r;
            if (tag != 22)
                return 0;
            bw_reader_init(&r, b, n);
            s->since = bw_read_i32(&r);
            s->remaining = bw_read_u32(&r);
            if (s->since < 0 || (s->high_water <= (uint32_t)s->since && s->remaining) ||
                (s->high_water > (uint32_t)s->since && s->high_water - (uint32_t)s->since > 16))
                return 0;
            s->stage = s->remaining ? 2 : 255;
        } else if (s->stage == 2) {
            if (tag != 21)
                return 0;
            v = u32at(b + 1);
            seq = u32at(b + 7);
            if (v <= (uint32_t)s->since || v > s->high_water || v < s->event_version)
                return 0;
            if (s->event_version) {
                if (b[5] < s->event_round || (b[5] == s->event_round && b[6] < s->event_tick))
                    return 0;
                if (b[5] == s->event_round && b[6] == s->event_tick) {
                    if (seq != s->event_sequence + 1)
                        return 0;
                } else if (seq != 1)
                    return 0;
            }
            s->event_version = v;
            s->event_round = b[5];
            s->event_tick = b[6];
            s->event_sequence = seq;
            if (!--s->remaining)
                s->stage = 255;
        } else
            return 0;
        break;
    case 0x15:
    case 0x17:
        if (s->stage == 1) {
            if (tag != 16)
                return 0;
            i = 5 + b[4];
            s->text_total = u32at(b); /* map handle; no text in map/snapshot streams */
            s->width = b[i];
            s->height = b[i + 1];
            s->tiles = u16at(b + i + 2);
            s->attributes = u16at(b + i + 4);
            s->stage = 2;
            break;
        }
        if (s->origin == 0x17) {
            if (s->stage == 2) {
                if (tag != 17 || u16at(b) != s->tile_next ||
                    b[2] != (s->tiles - s->tile_next > 128 ? 128 : s->tiles - s->tile_next))
                    return 0;
                s->tile_next = (uint16_t)(s->tile_next + b[2]);
                if (s->tile_next == s->tiles)
                    s->stage = s->attributes ? 3 : 255;
            } else if (s->stage == 3) {
                if (tag != 18 || b[0] >= s->width || b[1] >= s->height)
                    return 0;
                v = (uint32_t)b[1] * s->width + b[0] + 1;
                if (v <= s->event_sequence)
                    return 0;
                s->event_sequence = v;
                if (!--s->attributes)
                    s->stage = 255;
            } else
                return 0;
            break;
        }
        if (s->stage == 2) {
            if (tag != 9 || !s->high_water || u32at(b + 10) != s->text_total)
                return 0;
            s->counts[0] = b[15];
            s->counts[1] = u16at(b + 16);
            s->counts[2] = b[18];
            s->counts[3] = b[19];
            s->counts[4] = b[20];
            s->stage = 3;
            s->last_id = 0;
            break;
        }
        while (s->stage >= 3 && s->stage <= 7 && !s->counts[s->stage - 3]) {
            ++s->stage;
            s->last_id = 0;
            s->event_sequence = 0;
        }
        if (s->stage == 3) {
            if (tag != 10 || b[0] != s->last_id + 1 ||
                (!(b[1] & 1) && (b[2] >= s->width || b[3] >= s->height)))
                return 0;
            s->last_id = b[0];
        } else if (s->stage == 4) {
            if (tag != 11 || (v = u32at(b)) <= s->event_sequence ||
                (!b[6] && (b[4] >= s->width || b[5] >= s->height)))
                return 0;
            s->event_sequence = v;
        } else if (s->stage == 5) {
            if (tag != 12 || b[0] + 1 <= s->last_id)
                return 0;
            s->last_id = (uint8_t)(b[0] + 1);
        } else if (s->stage == 6 || s->stage == 7) {
            if (tag != 13 || b[0] != s->stage - 5 || b[1] <= s->last_id)
                return 0;
            s->last_id = b[1];
        } else
            return 0;
        --s->counts[s->stage - 3];
        break;
    default:
        return 0;
    }
    return 1;
}
int bw_stream_accept(bw_stream *s, const uint8_t *p, size_t n, int *apply) {
    bw_stream next;
    uint32_t tid, off, total, h;
    size_t pos;
    unsigned origin, flags;
    int stale;
    if (!s || !apply || !bw_validate_packet(p, n, 240) || p[0] != 0x82)
        return 0;
    tid = u32at(p + 3);
    off = u32at(p + 7);
    total = u32at(p + 11);
    h = u32at(p + 15);
    origin = p[19];
    flags = p[20] & 2;
    if (tid < s->transfer) {
        *apply = 0;
        return 1;
    }
    next = *s;
    if (tid != s->transfer) {
        if (off)
            return 0;
        bw_stream_init(&next, s->ack_version);
        next.transfer = tid;
        next.total = total;
        next.high_water = h;
        next.origin = (uint8_t)origin;
        next.flags = (uint8_t)flags;
    } else if (total != s->total || h != s->high_water || origin != s->origin || flags != s->flags)
        return 0;
    if (off < next.next_offset) {
        if (off + p[21] > next.next_offset)
            return 0;
        *apply = 0;
        return 1;
    }
    if (off != next.next_offset || next.complete)
        return 0;
    for (pos = 22; pos < n; pos += p[pos + 1] + 2)
        if (!grammar(&next, p + pos))
            return 0;
    next.next_offset += p[21];
    if (p[20] & 1) {
        if (next.origin == 0x15 && !(next.flags & 2)) {
            while (next.stage >= 3 && next.stage <= 7 && !next.counts[next.stage - 3])
                ++next.stage;
            if (next.stage == 8)
                next.stage = 255;
        }
        if (next.stage != 255)
            return 0;
        next.complete = 1;
        if (!(next.flags & 2) && next.origin == 0x16 && h > next.ack_version)
            next.ack_version = h;
    } else if (next.stage == 255)
        return 0;
    stale = origin == 0x16 && !(flags & 2) && h <= s->ack_version;
    *s = next;
    *apply = !stale;
    return 1;
}
