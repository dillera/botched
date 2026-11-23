#include "view.h"
#include <string.h>
static unsigned limit(int v, unsigned max) {
  return v < 0 ? 0 : (unsigned)v > max ? max : (unsigned)v;
}
void ba_view_center(ba_view *v, const ba_world *w, int x, int y) {
  v->cx = limit(x - 6, w->width > 12 ? w->width - 12 : 0);
  v->cy = limit(y - 5, w->height > 10 ? w->height - 10 : 0);
}
void ba_view_sync(ba_view *v, const ba_world *w) {
  if (!w->valid || !w->player)
    return;
  if (!v->loaded || (w->snapshot[0] == 0 && v->round != w->snapshot[1])) {
    const uint8_t *p = w->players[w->player - 1];
    v->loaded = 1;
    v->round = w->snapshot[1];
    if (!v->inspect) {
      v->x = p[2];
      v->y = p[3];
      ba_view_center(v, w, v->x, v->y);
    }
  }
}
unsigned ba_view_key(ba_view *v, ba_editor *e, const ba_world *w,
                     unsigned key) {
  int edit;
  if (key & 0x80)
    return 0;
  ba_view_sync(v, w);
  if (key == 0x45) {
    if (v->panel)
      v->panel = 0;
    else if (v->inspect)
      v->inspect = 0;
    else
      return BA_QUIT;
    return BA_CHANGED;
  }
  if (key == 0x50 || key == 0x51) {
    unsigned panel = key == 0x50 ? 1 : 2;
    v->panel = v->panel == panel ? 0 : panel;
    return BA_CHANGED;
  }
  if (key == 0x13)
    return BA_REFRESH;
  if (v->panel)
    return 0;
  if (key == 0x34) {
    v->overview ^= 1;
    return BA_CHANGED;
  }
  if (key == 0x17 && w->map_valid) {
    v->inspect ^= 1;
    return BA_CHANGED;
  }
  if (key == BA_LOCK_KEY) {
    if (ba_edit(e, key) != 2)
      return 0;
    e->pending = 1;
    return BA_SEND | BA_CHANGED;
  }
  if (v->inspect) {
    if (key == 0x4c && v->y)
      --v->y;
    else if (key == 0x4d && v->y + 1 < w->height)
      ++v->y;
    else if (key == 0x4f && v->x)
      --v->x;
    else if (key == 0x4e && v->x + 1 < w->width)
      ++v->x;
    else
      return 0;
    /* Pan only when the cursor reaches the edge, including overview selection.
     */
    if (v->x < v->cx)
      v->cx = v->x;
    if (v->x >= v->cx + 12)
      v->cx = v->x - 11;
    if (v->y < v->cy)
      v->cy = v->y;
    if (v->y >= v->cy + 10)
      v->cy = v->y - 9;
    return BA_CHANGED;
  }
  if (key == 0x4c || key == 0x4d) {
    v->choice = (v->choice + (key == 0x4c ? 8 : 1)) % 9;
    return BA_CHANGED;
  }
  if (key == 0x40 || key == 0x44)
    key = v->choice + 1;
  edit = ba_edit(e, key);
  if (edit && key >= 1 && key <= 9)
    v->choice = key - 1;
  if (edit == 2) {
    e->pending = 1;
    return BA_SEND | BA_CHANGED;
  }
  return edit ? BA_CHANGED : 0;
}
unsigned ba_view_click(ba_view *v, ba_editor *e, const ba_world *w, int x,
                       int y) {
  unsigned slot;
  int bx, by, size;
  ba_view_sync(v, w);
  if (v->panel)
    return 0;
  if (y >= 204 && y < 228) {
    if (x >= 224 && x < 316)
      return ba_view_key(v, e, w, BA_LOCK_KEY);
    if (x >= 4 && x < 219) {
      slot = (x - 4) / 73;
      if ((x - 4) % 73 < 69 && ba_slot(e, slot))
        return BA_CHANGED;
    }
    return 0;
  }
  if (!v->inspect && x >= 202 && x < 314 && y >= 54 && y < 198)
    return ba_view_key(v, e, w, 1 + (y - 54) / 16);
  if (!w->map_valid || x < 4 || x >= 196 || y < 40 || y >= 200)
    return 0;
  size = v->overview ? 8 : 16;
  bx = 4 + (w->width * size < 192 ? (192 - w->width * size) / 2 : 0);
  by = 40 + (w->height * size < 160 ? (160 - w->height * size) / 2 : 0);
  if (x < bx || y < by)
    return 0;
  x = (x - bx) / size + (v->overview ? 0 : v->cx);
  y = (y - by) / size + (v->overview ? 0 : v->cy);
  if (x >= w->width || y >= w->height)
    return 0;
  v->x = x;
  v->y = y;
  v->inspect = 1;
  if (v->overview)
    ba_view_center(v, w, x, y);
  return BA_CHANGED;
}
void ba_decimal(uint32_t value, char out[11]) {
  char reverse[10];
  unsigned n = 0, i;
  do {
    reverse[n++] = '0' + value % 10;
    value /= 10;
  } while (value);
  for (i = 0; i < n; ++i)
    out[i] = reverse[n - i - 1];
  out[n] = 0;
}
void ba_score_text(uint32_t score, char out[3]) {
  out[0] = score > 99 ? '+' : '0' + score / 10;
  out[1] = score > 99 ? '+' : '0' + score % 10;
  out[2] = 0;
}
const char *ba_player_label(unsigned p) {
  static const char *labels[] = {"----", "PLY1", "PLY2", "PLY3", "PLY4"};
  return labels[p <= 4 ? p : 0];
}
unsigned ba_priority(unsigned round, unsigned tick, unsigned n, unsigned i) {
  if (!round || tick < 1 || tick > 3 || n < 2 || n > 4 || i >= n)
    return 0;
  return (((round - 1) * 3 + tick - 1) % n + i) % n + 1;
}
unsigned ba_terrain(const ba_world *w, unsigned x, unsigned y) {
  unsigned cell = y * w->width + x;
  if (x >= w->width || y >= w->height)
    return 0;
  return (w->tiles[cell / 2] >> ((cell & 1) * 4)) & 15;
}
const uint8_t *ba_attribute(const ba_world *w, unsigned x, unsigned y) {
  unsigned i;
  for (i = 0; i < w->attributes_count; ++i)
    if (w->attributes[i][0] == x && w->attributes[i][1] == y)
      return w->attributes[i];
  return 0;
}
const uint8_t *ba_hazard(const ba_world *w, unsigned kind, unsigned id) {
  unsigned i;
  for (i = 0; i < w->hazards_count; ++i)
    if (w->hazards[i][0] == kind && w->hazards[i][1] == id)
      return w->hazards[i];
  return 0;
}
int ba_tarred(const ba_world *w, unsigned x, unsigned y) {
  int dx, dy;
  for (dy = -1; dy <= 1; ++dy)
    for (dx = -1; dx <= 1; ++dx)
      if ((dx || dy) && ba_terrain(w, x + dx, y + dy) == 10)
        return 1;
  return 0;
}
/* Static inspection geometry only: never select victims or simulate a shot. */
static int clear_ray(const ba_world *w, int sx, int sy, int x, int y) {
  int dx = (x > sx) - (x < sx), dy = (y > sy) - (y < sy);
  if (dx && dy)
    return 0;
  while (sx != x || sy != y) {
    unsigned t;
    sx += dx;
    sy += dy;
    if (sx < 0 || sy < 0 || sx >= w->width || sy >= w->height)
      return 0;
    t = ba_terrain(w, sx, sy);
    if (t == 1 || t == 7 || t == 9 || t == 10 || t == 11)
      return 0;
  }
  return 1;
}
int ba_related(const ba_world *w, unsigned sx, unsigned sy, unsigned x,
               unsigned y) {
  const uint8_t *a, *b;
  unsigned k = ba_terrain(w, sx, sy), t;
  int dx = (int)x - (int)sx, dy = (int)y - (int)sy;
  /* Most inspection cells have no links. Avoid two attribute scans per map
   * cell for floor, walls, cargo, docks and spawns. */
  if (k != 6 && k != 7 && k != 9 && k != 10 && k != 11 && k != 12 && k != 13)
    return 0;
  if (k == 9 || k == 10)
    return dx >= -1 && dx <= 1 && dy >= -1 && dy <= 1;
  if (k == 11)
    return ((!dx && dy >= -2 && dy <= 2) || (!dy && dx >= -2 && dx <= 2)) &&
           clear_ray(w, sx, sy, x, y);
  a = ba_attribute(w, sx, sy);
  if (k == 7 && a) {
    unsigned dir = a[4];
    int aligned = (dir == 0 && !dx && dy < 0) || (dir == 1 && !dy && dx > 0) ||
                  (dir == 2 && !dx && dy > 0) || (dir == 3 && !dy && dx < 0);
    return aligned && clear_ray(w, sx, sy, x, y);
  }
  b = ba_attribute(w, x, y);
  t = ba_terrain(w, x, y);
  if (!a || !b || a[2] != b[2] || a[3] != b[3])
    return 0;
  return (k == 6 && (t == 5 || t == 6)) || (k == 13 && t == 9) ||
         (k == 12 && t == 11);
}

/* Potential hazard geometry, not predicted victims. Objects do not truncate
 * these guides: they may move before the authoritative machinery phase. */
void ba_danger(const ba_world *w, uint8_t out[320]) {
  unsigned sx, sy, x, y, k, flag;
  const uint8_t *a, *h;
  memset(out, 0, 320);
  for (sy = 0; sy < w->height; ++sy)
    for (sx = 0; sx < w->width; ++sx) {
      k = ba_terrain(w, sx, sy);
      flag = 0;
      if (k == 10)
        flag = BA_TAR;
      else if (k == 11)
        flag = BA_TURRET;
      else if (k == 7) {
        a = ba_attribute(w, sx, sy);
        if (a)
          flag = (a[4] & 1) ? BA_LASER_H : BA_LASER_V;
      } else if (k == 9) {
        a = ba_attribute(w, sx, sy);
        h = a ? ba_hazard(w, 1, a[2]) : 0;
        if (h && h[2])
          flag = BA_BURST;
      }
      if (!flag)
        continue;
      for (y = 0; y < w->height; ++y)
        for (x = 0; x < w->width; ++x)
          if ((x != sx || y != sy) && ba_related(w, sx, sy, x, y)) {
            unsigned t = ba_terrain(w, x, y);
            if (flag == BA_BURST && t == 1)
              continue;
            if (flag == BA_TAR &&
                (t == 1 || t == 7 || t == 8 || t == 9 || t == 10 || t == 11))
              continue;
            out[y * w->width + x] |= flag;
          }
    }
}
