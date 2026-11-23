/* Feasibility prototype: one foreground owner, bounded shipped-map profile.
 * Not a general decoder or a graphical client. See docs/atari-feasibility.md. */
#ifndef BOTCHED_THIN_H
#define BOTCHED_THIN_H
#include <stdint.h>
#include <stddef.h>
#ifdef __cplusplus
extern "C" {
#endif
#ifdef __CC65__
#define TH_FAST __fastcall__
#else
#define TH_FAST
#endif
#define TH_CRATES 100
#define TH_ATTRS 31
#define TH_PAYLOAD 240
/* Packed wire bodies. Never use received coordinates/IDs as unchecked indices. */
typedef struct {
    uint8_t tiles[160], attributes[TH_ATTRS][5];
    uint8_t players[4][27], crates[TH_CRATES][7];
    uint8_t groups[26], hazards[2][9][2]; /* value, owner; indexed by validated IDs */
    uint8_t map[43], snapshot[31], status[26];
    uint8_t width, height, attrs, crate_count, valid, map_valid;
} th_world;
typedef struct {
    uint8_t rx[240], tx[96];
    uint8_t match[17], credential[65], epoch[16], player;
    uint16_t exchange;
    uint32_t ack, cursor, transfer, offset, total, high, next_key;
    uint8_t origin, flags, pos, size, tx_size, error, active, finished, have_epoch;
    uint8_t seen_players, seen_groups, seen_hazards, tile_bytes;
} th_client;
extern th_world th_w;
extern th_client th;
/* C implementation and optional identical 6502 assembly implementation. */
uint32_t TH_FAST th_u32(const uint8_t *);
uint8_t *TH_FAST th_find(const uint8_t *);
/* Assembly offsets are checked here, not assumed silently. */
typedef char th_crate_layout[(offsetof(th_world,crates)==423 && offsetof(th_world,crate_count)==1288)?1:-1];
void th_reset(void);
/* Validates one entire page before changing the mirror. Payload is already in rx.
 * 1=accepted; 0=fail closed. No generic reference codec linked. */
uint8_t th_accept(uint8_t size);
/* Consume one validated record. EVENT views borrow rx until the NEXT request.
 * Returns tag or 0 when drained; permits renderer backpressure between events. */
uint8_t th_next(const uint8_t **body, uint8_t *size);
uint8_t th_hello(void);
uint8_t th_join(const char *match);
uint8_t th_inspect_map(const char *match);
uint8_t th_bind(void);
uint8_t th_query(uint8_t opcode);
uint8_t th_fetch(void);
uint8_t th_submit(uint8_t round, uint8_t revision, const uint8_t orders[3]);
#ifdef __cplusplus
}
#endif
#endif
