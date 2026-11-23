/* Presentation only: all movements/outcomes come from validated server events.
 * Fresh snapshots reconcile metadata after replay, as in the Linux playback. */
#include "botched/wire/v1.h"
#include "model.h"
#include <string.h>
static void put(uint8_t *p, uint32_t v) {
  unsigned i;
  for (i = 0; i < 4; ++i)
    p[i] = (uint8_t)(v >> (i * 8));
}
static void add(uint8_t *p, unsigned n) { put(p, ba_u32(p) + n); }
static uint8_t *crate(ba_world *w, const uint8_t *id, int create) {
  unsigned i;
  for (i = 0; i < w->crates_count; ++i)
    if (!memcmp(w->crates[i], id, 4))
      return w->crates[i];
  if (!create)
    return 0;
  if (w->crates_count == 324) {
    w->failed = 255;
    return 0;
  }
  memcpy(w->crates[w->crates_count], id, 4);
  return w->crates[w->crates_count++];
}
static void erase(ba_world *w, const uint8_t *id) {
  uint8_t *c = crate(w, id, 0);
  if (c) {
    --w->crates_count;
    if (c != w->crates[w->crates_count])
      memcpy(c, w->crates[w->crates_count], 7);
  }
}
static uint8_t *hazard(ba_world *w, unsigned kind, unsigned id) {
  unsigned i;
  for (i = 0; i < w->hazards_count; ++i)
    if (w->hazards[i][0] == kind && w->hazards[i][1] == id)
      return w->hazards[i] + 2;
  w->failed = 255;
  return 0;
}
const char *ba_event_name(unsigned e) {
  static const char *names[] = {"",
                                "Planning closed",
                                "Round started",
                                "Round ended",
                                "Match ended",
                                "Order executed",
                                "Order had no effect",
                                "Robot disabled: skipped",
                                "Robot moved",
                                "Robot pushed",
                                "Move blocked",
                                "Robot turned",
                                "Crate spawned",
                                "Crate picked up",
                                "Crate dropped",
                                "Crate delivered",
                                "Crate moved",
                                "Crate destroyed",
                                "Conveyors toggled",
                                "Laser hit",
                                "Robot disabled",
                                "Respawn redirected",
                                "Robot respawned",
                                "Trap armed",
                                "Trap triggered",
                                "Trap hit",
                                "Turret captured",
                                "Turret charging",
                                "Turret fired",
                                "Slowed by tar",
                                "Score awarded"};
  return e <= 30 ? names[e] : "Invalid event";
}
int ba_event(ba_world *w, const uint8_t *record, size_t n) {
  const uint8_t *b, *p;
  uint8_t *r, *c;
  unsigned e, i, j;
  if (!w->valid || !bw_validate_record(record, n) || record[0] != 21)
    return 0;
  b = record + 2;
  p = b + 11;
  e = b[0];
  w->snapshot[1] = b[5];
  w->snapshot[2] = b[6];
#define PLAYER(id) (w->players[(id) - 1])
  switch (e) {
  case 1:
    w->snapshot[0] = 1;
    break;
  case 2:
    w->snapshot[0] = 0;
    w->snapshot[1] = p[0];
    w->snapshot[2] = 0;
    break;
  case 3:
  case 4:
    j = e == 3 ? 2 : 3;
    for (i = 0; i < p[j - 1]; ++i, j += 13)
      memcpy(PLAYER(p[j]) + 10, p + j + 1, 12);
    if (e == 4) {
      w->snapshot[0] = 2;
      w->winners = p[0];
    }
    break;
  case 8:
    memcpy(PLAYER(p[0]) + 2, p + 3, 2);
    break;
  case 9:
    memcpy(PLAYER(p[0]) + 2, p + 4, 2);
    break;
  case 11:
    PLAYER(p[0])[4] = p[2];
    break;
  case 12:
    c = crate(w, p, 1);
    if (c) {
      memcpy(c + 4, p + 6, 2);
      c[6] = 0;
    }
    break;
  case 13:
  case 14:
    r = PLAYER(p[0]);
    c = crate(w, p + 1, 1);
    if (!c)
      break;
    if (e == 13) {
      r[1] |= 2;
      memcpy(r + 6, p + 1, 4);
      c[4] = c[5] = 0;
      c[6] = p[0];
    } else {
      r[1] &= ~2;
      memset(r + 6, 0, 4);
      memcpy(c + 4, p + 5, 2);
      c[6] = 0;
    }
    break;
  case 15:
    r = PLAYER(p[0]);
    r[1] &= ~2;
    memset(r + 6, 0, 4);
    memcpy(r + 10, p + 7, 4);
    add(r + 14, 1);
    erase(w, p + 1);
    break;
  case 16:
    c = crate(w, p, 1);
    if (c) {
      memcpy(c + 4, p + 6, 2);
      c[6] = 0;
    }
    break;
  case 17:
    erase(w, p);
    for (i = 0; i < 4; ++i)
      if (!memcmp(w->players[i] + 6, p, 4)) {
        w->players[i][1] &= ~2;
        memset(w->players[i] + 6, 0, 4);
      }
    break;
  case 18:
    for (i = 0; i < w->groups_count; ++i)
      if (w->groups[i][0] == p[1])
        w->groups[i][1] = p[4];
    break;
  case 19:
    r = PLAYER(p[0]);
    r[5] = p[3];
    add(r + 18, 1);
    break;
  case 20:
    r = PLAYER(p[0]);
    r[1] |= 1;
    r[2] = r[3] = 0;
    break;
  case 22:
    r = PLAYER(p[0]);
    r[1] &= ~3;
    memcpy(r + 2, p + 1, 2);
    r[4] = p[3];
    r[5] = 0;
    memset(r + 6, 0, 4);
    break;
  case 23:
    c = hazard(w, 1, p[1]);
    if (c) {
      c[0] = 1;
      c[1] = p[6];
    }
    break;
  case 24:
    c = hazard(w, 1, p[0]);
    if (c)
      c[0] = 0;
    break;
  case 25:
    r = PLAYER(p[1]);
    r[5] = p[2];
    add(r + 18, 1);
    break;
  case 26:
    c = hazard(w, 2, p[1]);
    if (c)
      c[1] = p[5];
    break;
  case 27:
    c = hazard(w, 2, p[0]);
    if (c)
      memcpy(c, p + 3, 2);
    break;
  case 28:
    c = hazard(w, 2, p[0]);
    if (c)
      c[0] = 0;
    r = PLAYER(p[4]);
    r[5] = p[7];
    add(r + 18, 1);
    break;
  case 30:
    r = PLAYER(p[0]);
    memcpy(r + 10, p + 4, 4);
    r[1] |= 4;
    add(r + 22, p[1]);
    break;
  default:
    break;
  }
  /* Defensive renderer bounds are map-specific, beyond B2 wire bounds. */
  for (i = 0; i < 4; ++i)
    if (w->players[i][0] && !(w->players[i][1] & 1) &&
        (w->players[i][2] >= w->width || w->players[i][3] >= w->height))
      w->failed = 255;
  for (i = 0; i < w->crates_count; ++i)
    if (!w->crates[i][6] &&
        (w->crates[i][4] >= w->width || w->crates[i][5] >= w->height))
      w->failed = 255;
  if (w->failed)
    w->valid = 0;
  return !w->failed;
}
