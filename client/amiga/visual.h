#ifndef BOTCHED_AMIGA_VISUAL_H
#define BOTCHED_AMIGA_VISUAL_H
#include "assets.h"
#include "model.h"
#ifdef __cplusplus
extern "C" {
#endif
/* A presentation-only copy. Never written back to the authoritative mirror. */
typedef struct {
  ba_world before;
  uint8_t event, frame, duration, active;
  uint8_t effect, sx, sy, tx, ty, subject;
} ba_animation;
void ba_animation_begin(ba_animation *, unsigned event);
void ba_animation_record(ba_animation *, const uint8_t *, unsigned);
int ba_animation_tick(ba_animation *); /* exactly one completion per event */
int ba_lerp(int from, int to, const ba_animation *);
unsigned ba_tile_image(const ba_world *, unsigned cell);
#ifdef __cplusplus
}
#endif
#endif
