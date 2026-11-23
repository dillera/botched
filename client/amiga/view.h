#ifndef BOTCHED_AMIGA_VIEW_H
#define BOTCHED_AMIGA_VIEW_H
#include "play.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Local presentation only: never serialized or used for game resolution. */
typedef struct {
  uint8_t choice, overview, inspect, panel, x, y, cx, cy, round, loaded;
} ba_view;
enum { BA_QUIT = 1, BA_REFRESH = 2, BA_CHANGED = 4, BA_SEND = 8 };
void ba_view_sync(ba_view *, const ba_world *);
void ba_view_center(ba_view *, const ba_world *, int x, int y);
unsigned ba_view_key(ba_view *, ba_editor *, const ba_world *, unsigned key);
void ba_decimal(uint32_t value, char out[11]);
void ba_score_text(uint32_t score, char out[3]);
const char *ba_player_label(unsigned player);
unsigned ba_priority(unsigned round, unsigned tick, unsigned count,
                     unsigned index);
unsigned ba_terrain(const ba_world *, unsigned x, unsigned y);
const uint8_t *ba_attribute(const ba_world *, unsigned x, unsigned y);
const uint8_t *ba_hazard(const ba_world *, unsigned kind, unsigned id);
int ba_tarred(const ba_world *, unsigned x, unsigned y);
int ba_related(const ba_world *, unsigned sx, unsigned sy, unsigned x,
               unsigned y);
#ifdef __cplusplus
}
#endif
#endif
