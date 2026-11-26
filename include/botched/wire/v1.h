/* Botched wire 1.0. C99, caller-owned buffers, no allocation or I/O.
 * Views borrow input until the caller releases it. Validate before reading/applying.
 * Byte layouts: docs/binary-v1/wire.md. This API does not serialize C structs. */
#ifndef BOTCHED_WIRE_V1_H
#define BOTCHED_WIRE_V1_H
#include <stddef.h>
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BW_MAX_PAYLOAD 240u
#define BW_MIN_PAYLOAD 128u
#define BW_MAX_TRANSFER 4194304UL

typedef struct {
    const uint8_t *data;
    size_t size, pos;
    int error;
} bw_reader;
typedef struct {
    uint8_t *data;
    size_t capacity, pos;
    int error;
} bw_writer;
void bw_reader_init(bw_reader *, const uint8_t *, size_t);
uint8_t bw_read_u8(bw_reader *);
uint16_t bw_read_u16(bw_reader *);
uint32_t bw_read_u32(bw_reader *);
int32_t bw_read_i32(bw_reader *);
const uint8_t *bw_read_bytes(bw_reader *, size_t);
void bw_writer_init(bw_writer *, uint8_t *, size_t);
void bw_write_u8(bw_writer *, uint8_t);
void bw_write_u16(bw_writer *, uint16_t);
void bw_write_u32(bw_writer *, uint32_t);
void bw_write_bytes(bw_writer *, const uint8_t *, size_t);
int bw_validate_record(const uint8_t *, size_t);
int bw_validate_packet(const uint8_t *, size_t, uint16_t max_payload);
/* Transactional encoders: invalid input or insufficient capacity leaves output untouched. */
int bw_encode_record(uint8_t tag, const uint8_t *body, size_t size, uint8_t *output, size_t capacity,
                     size_t *written);
int bw_encode_packet(uint8_t opcode, uint16_t exchange, const uint8_t *body, size_t size, uint8_t *output,
                     size_t capacity, size_t *written);
typedef void (*bw_record_callback)(void *, const uint8_t *, size_t);
/* No callback on a structurally bad packet, even when a prefix contains good records. */
int bw_visit_page(const uint8_t *, size_t, bw_record_callback, void *);

/* Stateful logical response validation. On a new adapter session, initialize with the
 * existing acknowledged version; transfer IDs are scoped to that session. Reset the
 * acknowledged version only for a different server epoch/match.
 * ack_version is monotonic within this identity. Stale/duplicate pages never apply.
 * Snapshot consumers rebuild an invalid mirror until final completion; this tracks grammar,
 * not map occupancy/gameplay. Map-specific checks belong to the caller's admitted mirror. */
typedef struct {
    uint32_t ack_version, transfer, next_offset, total, high_water;
    uint32_t remaining, text_total, text_offset, event_version, event_sequence;
    int32_t since;
    uint16_t counts[5], tiles, attributes, tile_next;
    uint8_t origin, flags, stage, text_field, event_round, event_tick;
    uint8_t last_id, width, height, complete;
} bw_stream;
void bw_stream_init(bw_stream *, uint32_t acknowledged_version);
/* returns 1 on accepted page (including harmless duplicate), 0 on invalid page.
 * apply=1 means fresh records may now be applied; 0 means no callbacks/state updates.
 * The stream object is unchanged on error. */
int bw_stream_accept(bw_stream *, const uint8_t *, size_t, int *apply);
#ifdef __cplusplus
}
#endif
#endif
