#include "play.h"
#include <string.h>
const char *ba_order_name(unsigned n) {
  static const char *names[] = {"FWD1",   "FWD2", "BACK",   "LEFT", "RIGHT",
                                "AROUND", "DROP", "TOGGLE", "WAIT"};
  return n < 9 ? names[n] : "?";
}
void ba_editor_sync(ba_editor *e, const ba_world *w) {
  if (e->round != w->snapshot[1]) {
    e->round = w->snapshot[1];
    e->orders[0] = e->orders[1] = e->orders[2] = BA_EMPTY;
    e->slot = 0;
    e->pending = 0;
  }
  e->locked = !w->valid || w->snapshot[0] != 0 || !w->player ||
              !!(w->snapshot[22] & (1u << (w->player - 1)));
}
int ba_complete(const ba_editor *e) {
  return e->orders[0] < 9 && e->orders[1] < 9 && e->orders[2] < 9;
}
int ba_place(ba_editor *e, unsigned order) {
  unsigned i, slot;
  if (e->pending || e->locked || order > 8)
    return 0;
  for (i = 0; i < 3; ++i) {
    slot = (e->slot + i) % 3;
    if (e->orders[slot] == BA_EMPTY) {
      e->orders[slot] = order;
      e->slot = slot;
      return 1;
    }
  }
  return 0; /* Full means full: never silently replace an order. */
}
int ba_slot(ba_editor *e, unsigned slot) {
  if (e->pending || e->locked || slot >= 3)
    return 0;
  e->slot = slot;
  e->orders[slot] = BA_EMPTY;
  return 1;
}
int ba_edit(ba_editor *e, unsigned key) {
  if (e->pending || e->locked)
    return 0;
  if (key == BA_LOCK_KEY)
    return ba_complete(e) ? 2 : 0;
  if (key == 0x4f)
    e->slot = (e->slot + 2) % 3;
  else if (key == 0x4e || key == 0x42)
    e->slot = (e->slot + 1) % 3;
  else if (key >= 1 && key <= 9)
    return ba_place(e, key - 1);
  else if (key == 0x41)
    return ba_slot(e, e->slot);
  else
    return 0;
  return 1;
}
int ba_submit_body(uint8_t out[18], uint32_t round, uint32_t key,
                   const uint8_t orders[3]) {
  bw_writer w;
  if (!round || round > 200 || orders[0] > 8 || orders[1] > 8 || orders[2] > 8)
    return 0;
  bw_writer_init(&w, out, 18);
  bw_write_u8(&w, 0);
  bw_write_u32(&w, round);
  bw_write_u32(&w, 1);
  bw_write_bytes(&w, orders, 3);
  bw_write_u8(&w, 1);
  bw_write_u8(&w, 1);
  bw_write_u32(&w, key);
  return !w.error;
}
int ba_replay_page(ba_replay *r, const uint8_t *p, size_t n, int apply) {
  if (!bw_validate_packet(p, n, 240) || p[0] != 0x82 || p[19] != 0x16 ||
      (p[20] & 2))
    return 0;
  /* The B4 stream validator decides freshness. Replayed pages are never shown
   * twice. */
  if (!apply)
    return 1;
  if (r->active)
    return 0;
  memcpy(r->bytes, p, n);
  r->size = n;
  r->pos = 22;
  r->high = ba_u32(p + 15);
  r->final = p[20] & 1;
  r->active = 1;
  return 1;
}
int ba_replay_next(ba_replay *r, ba_world *w) {
  if (!r->active)
    return 0;
  while (r->pos < r->size) {
    const uint8_t *p = r->bytes + r->pos;
    unsigned n = p[1] + 2;
    r->pos += n;
    if (p[0] == 22)
      r->batch_events = ba_u32(p + 6);
    if (p[0] == 21) {
      if (n > sizeof r->event_record)
        return -1;
      memcpy(r->event_record, p, n);
      r->event_size = n;
      r->actor = (p[2] >= 5 && p[2] <= 7) ? p[13] : 0;
      r->order = r->actor ? p[14] : 8;
      if (!ba_event(w, p, n))
        return -1;
      return p[2];
    }
  }
  r->active = 0;
  if (r->final) {
    if (r->high > r->ack)
      r->ack = r->high;
    if (r->high > r->cursor)
      r->cursor = r->high;
  }
  return 0;
}
void ba_replay_snapshot(ba_replay *r, uint32_t high) { r->cursor = high; }

int ba_snapshot_ready(const ba_replay *r, int have_world, uint32_t high) {
  return !have_world || high <= r->cursor;
}
