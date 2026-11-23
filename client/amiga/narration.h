#ifndef BOTCHED_AMIGA_NARRATION_H
#define BOTCHED_AMIGA_NARRATION_H
#include "model.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Owned captions only: no page pointers, credentials or second event store. */
typedef struct {
  char line[2][53];
  uint8_t round, tick, event;
} ba_caption;
typedef struct {
  ba_caption history[16];
  uint8_t head, count, scroll, round, tick, actor, order, phase;
  uint8_t done[3], skipped[3], machinery, refreshed;
  uint32_t serial;
} ba_narration;
extern ba_narration ba_story;
/* phases: 0 edge/planning, 1 order, 2 machinery, 3 source known/phase
 * unresolved, 4 local failure (status takes precedence over captions) */
void ba_narrate(ba_narration *, const uint8_t *, unsigned);
void ba_narration_refresh(ba_narration *);
const ba_caption *ba_caption_at(const ba_narration *, unsigned age);
#ifdef __cplusplus
}
#endif
#endif
