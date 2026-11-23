#ifndef BOTCHED_AMIGA_PLAY_H
#define BOTCHED_AMIGA_PLAY_H
#include "botched/wire/v1.h"
#include "model.h"
#ifdef __cplusplus
extern "C" {
#endif
/* Page publication is copied once; a borrowed record never crosses a job. */
typedef struct {
  uint8_t bytes[240];
  uint16_t size, pos;
  uint32_t cursor, ack, high, batch_events;
  uint8_t final, active, actor, order;
} ba_replay;
typedef struct {
  uint8_t orders[3], slot, round, locked, pending;
} ba_editor;
void ba_editor_sync(ba_editor *, const ba_world *);
int ba_edit(ba_editor *, unsigned raw_key); /* 1=changed, 2=submit, 0=ignored */
const char *ba_order_name(unsigned);
int ba_submit_body(uint8_t out[18], uint32_t round, uint32_t key,
                   const uint8_t orders[3]);
int ba_replay_page(ba_replay *, const uint8_t *, size_t, int apply);
int ba_replay_next(ba_replay *,
                   ba_world *); /* event ID; zero=page drained; -1=invalid */
int ba_snapshot_ready(const ba_replay *, int have_world, uint32_t high);
void ba_replay_snapshot(ba_replay *,
                        uint32_t high); /* never acknowledges events */
#ifdef __cplusplus
}
#endif
#endif
