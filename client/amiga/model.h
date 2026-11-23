#ifndef BOTCHED_AMIGA_MODEL_H
#define BOTCHED_AMIGA_MODEL_H
#include <stddef.h>
#include <stdint.h>
/* B1 global bounds, not capacities inferred from one short match. Wire bodies
 * stay little endian. No Amiga types or client-side game simulation here. */
typedef struct {
  uint8_t tiles[160], attributes[320][5], players[4][27];
  uint8_t crates[324][7], groups[26][2], hazards[18][4];
  uint8_t map[43], snapshot[31], credential[65];
  uint16_t attributes_count, crates_count, groups_count, hazards_count;
  uint16_t tile_count;
  uint8_t width, height, player, players_count, map_valid, valid, failed;
} ba_world;
#ifdef __cplusplus
extern "C" {
#endif
void ba_begin(ba_world *, uint8_t origin);
void ba_record(void *, const uint8_t *, size_t);
int ba_finish(ba_world *, uint8_t origin);
uint32_t ba_u32(const uint8_t *);
#ifdef __cplusplus
}
#endif
#endif
