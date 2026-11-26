#include "visual.h"
#include "botched/wire/v1.h"
void ba_animation_begin(ba_animation *a, unsigned e) {
  a->event = e;
  a->frame = 0;
  a->active = 1;
  a->effect = 0;
  /* Deliberate robot travel: 12 steps, or 16 for a clearly readable push.
   * Keep low-information events short rather than slowing the whole replay. */
  a->duration =
      e == 9                                                             ? 16
      : e == 8                                                           ? 12
      : e == 16                                                          ? 8
      : (e == 15 || e == 20 || e == 24 || e == 28 || e == 30)            ? 5
      : (e == 11 || e == 13 || e == 14 || e == 19 || e == 25 || e == 29) ? 3
                                                                         : 1;
}
void ba_animation_record(ba_animation *a, const uint8_t *r, unsigned n) {
  const uint8_t *p;
  unsigned e;
  if (!r || n < 13 || !bw_validate_record(r, n) || r[0] != 21)
    return; /* called after validated replay apply */
  p = r + 13;
  e = r[2];
  a->subject = 0;
  if (e == 8 || e == 9)
    a->subject = p[0];
  if (e == 19) {
    a->effect = 1;
    a->sx = p[1];
    a->sy = p[2];
    a->subject = p[0];
  }
  if (e == 28) {
    a->effect = 1;
    a->sx = p[1];
    a->sy = p[2];
    a->subject = p[4];
  }
  if (e == 24) {
    a->effect = 2;
    a->sx = p[1];
    a->sy = p[2];
  }
  if (e == 10) {
    a->effect = 3;
    a->sx = p[2];
    a->sy = p[3];
  }
  if (a->subject && a->subject <= 4) {
    a->tx = a->before.players[a->subject - 1][2];
    a->ty = a->before.players[a->subject - 1][3];
  }
}
int ba_animation_tick(ba_animation *a) {
  if (!a->active)
    return 0;
  if (++a->frame < a->duration)
    return 0;
  a->active = 0;
  return 1;
}
int ba_lerp(int from, int to, const ba_animation *a) {
  if (!a->active)
    return to;
  return from + (to - from) * (int)a->frame / (int)a->duration;
}
unsigned ba_tile_image(const ba_world *w, unsigned cell) {
  unsigned k = (w->tiles[cell / 2] >> ((cell & 1) * 4)) & 15, i, j, dir;
  if (k > 13)
    return 0;
  for (i = 0; i < w->attributes_count; ++i) {
    const uint8_t *a = w->attributes[i];
    if ((unsigned)a[1] * w->width + a[0] != cell)
      continue;
    if (k == 2)
      return BA_SPAWN + (a[2] - 1) % 4;
    if (k == 3)
      return 18 + (a[2] - 1) % 4;
    if (k == 7)
      return 39 + (a[4] & 3);
    if (k == 5) {
      dir = a[4];
      for (j = 0; j < w->groups_count; ++j)
        if (w->groups[j][0] == a[2] && w->groups[j][1])
          dir = (dir + 2) & 3;
      return 14 + (dir & 3);
    }
    break;
  }
  return k;
}
