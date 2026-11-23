#include "model.h"
#include <string.h>
static unsigned u16(const uint8_t *p) { return p[0] | (unsigned)p[1] << 8; }
uint32_t ba_u32(const uint8_t *p) {
  return p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 |
         (uint32_t)p[3] << 24;
}
void ba_begin(ba_world *w, uint8_t op) {
  w->failed = 0;
  if (op == 0x17) {
    w->map_valid = w->valid = 0;
    w->tile_count = w->attributes_count = 0;
  }
  if (op == 0x15) {
    w->valid = 0;
    w->players_count = 0;
    w->crates_count = w->groups_count = w->hazards_count = 0;
    memset(w->players, 0, sizeof w->players);
  }
}
/* Called only by bw_visit_page after B2 grammar validation. Local destination
 * capacities and renderer coordinates are checked independently. */
void ba_record(void *ctx, const uint8_t *record, size_t n) {
  ba_world *w = ctx;
  const uint8_t *b = record + 2;
  unsigned tag = record[0], a;
  if (w->failed || n < 2)
    return;
  switch (tag) {
  case 3:
    w->player = b[0];
    memcpy(w->credential, b + 2, n - 4);
    break;
  case 6:
    w->failed = b[0] ? b[0] : 255;
    break;
  case 9:
    memcpy(w->snapshot, b, 31);
    break;
  case 10:
    if (b[0] < 1 || b[0] > 4 ||
        (!(b[1] & 1) && (b[2] >= w->width || b[3] >= w->height)))
      goto bad;
    memcpy(w->players[b[0] - 1], b, 27);
    ++w->players_count;
    break;
  case 11:
    if (w->crates_count >= 324 ||
        (!b[6] && (b[4] >= w->width || b[5] >= w->height)))
      goto bad;
    memcpy(w->crates[w->crates_count++], b, 7);
    break;
  case 12:
    if (w->groups_count >= 26)
      goto bad;
    memcpy(w->groups[w->groups_count++], b, 2);
    break;
  case 13:
    if (w->hazards_count >= 18)
      goto bad;
    memcpy(w->hazards[w->hazards_count++], b, 4);
    break;
  case 16:
    a = 5 + b[4];
    if (n - 2 > sizeof w->map || b[a] > 20 || b[a + 1] > 16 || !b[a] ||
        !b[a + 1])
      goto bad;
    if (w->map_valid &&
        (memcmp(w->map, b, 4) || w->width != b[a] || w->height != b[a + 1]))
      goto bad;
    memset(w->map, 0, sizeof w->map);
    memcpy(w->map, b, n - 2);
    w->width = b[a];
    w->height = b[a + 1];
    break;
  case 17:
    a = u16(b);
    if (a != w->tile_count || a + b[2] > 320 || a & 1)
      goto bad;
    memcpy(w->tiles + a / 2, b + 3, (b[2] + 1) / 2);
    w->tile_count += b[2];
    break;
  case 18:
    if (w->attributes_count >= 320 || b[0] >= w->width || b[1] >= w->height)
      goto bad;
    memcpy(w->attributes[w->attributes_count++], b, 5);
    break;
  default:
    break;
  }
  return;
bad:
  w->failed = 255;
}
int ba_finish(ba_world *w, uint8_t op) {
  if (w->failed)
    return 0;
  if (op == 0x17) {
    if (w->tile_count != (unsigned)w->width * w->height ||
        w->attributes_count != u16(w->map + 9 + w->map[4]))
      return 0;
    w->map_valid = 1;
  }
  if (op == 0x15) {
    if (!w->map_valid || w->players_count != w->snapshot[15] ||
        w->crates_count != u16(w->snapshot + 16) ||
        w->groups_count != w->snapshot[18] ||
        w->hazards_count != w->snapshot[19] + w->snapshot[20])
      return 0;
    w->valid = 1;
  }
  return 1;
}
