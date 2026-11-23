#include "visual.h"
void ba_animation_begin(ba_animation *a, unsigned e) {
  a->event = e;
  a->frame = 0;
  a->active = 1;
  a->duration = (e == 8 || e == 9 || e == 16)
                    ? 8
                    : ((e >= 11 && e <= 30 && e != 18 && e != 21) ? 6 : 1);
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
