#include "platform.h"
#include "visual.h"
#include <exec/memory.h>
#include <graphics/gfxbase.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <string.h>
struct Window *ba_window;
static struct Screen *screen;
static struct TextFont font;
static ULONG charloc[96];
static UBYTE *fontdata;
static ba_view old_view;
static unsigned board_mode;
static struct BitMap back, atlas, ground;
static int ground_target;
static uint32_t ground_keys[320];
static uint8_t danger[320], inspect_cells[320], hazard_state[72];
static uint16_t hazard_cells[320];
static unsigned hazard_count;
static struct RastPort canvas;
static UBYTE *mask;
static uint32_t previous[320], signatures[320];
static uint8_t dirty[320], tile_images[320], last_groups[52];
static int initialized, ox, oy;
static ULONG draws, cells, full, ticks, worst;
/* KS1.3 DOS timestamp: 20 ms resolution. Includes task preemption, not
 * cycle-level profiling; measurements explicitly report this quantization. */
ULONG ba_display_time(void) {
  struct DateStamp d;
  DateStamp(&d);
  return (ULONG)d.ds_Days * 4320000UL + (ULONG)d.ds_Minute * 3000UL + d.ds_Tick;
}
static int bitmap(struct BitMap *b, unsigned width, unsigned height) {
  unsigned p;
  InitBitMap(b, 4, width, height);
  for (p = 0; p < 4; ++p)
    if (!(b->Planes[p] = AllocRaster(width, height)))
      return 0;
  return 1;
}
static void free_bitmap(struct BitMap *b, unsigned width, unsigned height) {
  unsigned p;
  for (p = 0; p < 4; ++p)
    if (b->Planes[p]) {
      FreeRaster(b->Planes[p], width, height);
      b->Planes[p] = 0;
    }
}
int ba_display_open(void) {
  struct NewScreen ns = {0};
  struct NewWindow nw = {0};
  unsigned k, x, y, p;
  ns.Width = 320;
  ns.Height = 256;
  ns.Depth = 4;
  ns.DetailPen = 1;
  ns.BlockPen = 2;
  ns.Type = CUSTOMSCREEN;
  screen = OpenScreen(&ns);
  if (!screen)
    return 0;
  LoadRGB4(&screen->ViewPort, (UWORD *)ba_palette, 16);
  nw.Width = 320;
  nw.Height = 256;
  nw.Type = CUSTOMSCREEN;
  nw.Screen = screen;
  nw.Flags = BORDERLESS | BACKDROP | ACTIVATE | RMBTRAP | NOCAREREFRESH;
  nw.IDCMPFlags = IDCMP_RAWKEY | IDCMP_MOUSEBUTTONS;
  ba_window = OpenWindow(&nw);
  if (!ba_window || !bitmap(&back, 320, 256) || !bitmap(&ground, 320, 256) ||
      !bitmap(&atlas, 16, BA_ASSETS * 16) ||
      !(mask = AllocMem(BA_ASSETS * 32, MEMF_CHIP | MEMF_CLEAR)) ||
      !(fontdata = AllocMem(576, MEMF_CHIP | MEMF_CLEAR))) {
    ba_display_close();
    return 0;
  }
  InitRastPort(&canvas);
  canvas.BitMap = &back;
  memset(&font, 0, sizeof font);
  font.tf_YSize = 8;
  font.tf_XSize = 6;
  font.tf_Baseline = 6;
  font.tf_Flags = FPF_DESIGNED;
  font.tf_LoChar = 32;
  font.tf_HiChar = 127;
  font.tf_Modulo = 72;
  font.tf_CharData = fontdata;
  font.tf_CharLoc = charloc;
  for (k = 0; k < 96; ++k) {
    charloc[k] = ((ULONG)(k * 6) << 16) | 5;
    for (y = 0; y < 8; ++y)
      for (x = 0; x < 5; ++x)
        if (ba_glyphs[k][y] & (0x80 >> x))
          fontdata[y * 72 + (k * 6 + x) / 8] |= 0x80 >> ((k * 6 + x) & 7);
  }
  SetFont(&canvas, &font);
  ba_ui_reset();
  SetRast(&canvas, 0);
  SetRast(ba_window->RPort, 0);
  for (p = 0; p < 4; ++p)
    memset(atlas.Planes[p], 0, BA_ASSETS * 32);
  for (k = 0; k < BA_ASSETS; ++k)
    for (y = 0; y < 16; ++y)
      for (x = 0; x < 16; ++x) {
        unsigned color = ba_asset_pixel(k, x, y), pos = k * 32 + y * 2 + x / 8,
                 bit = 0x80 >> (x & 7);
        for (p = 0; p < 4; ++p)
          if (color & (1 << p))
            atlas.Planes[p][pos] |= bit;
        if (color)
          mask[pos] |= bit;
      }
  ShowTitle(screen, FALSE);
  return 1;
}
void ba_display_close(void) {
  WaitBlit();
  if (fontdata) {
    FreeMem(fontdata, 576);
    fontdata = 0;
  }
  if (mask) {
    FreeMem(mask, BA_ASSETS * 32);
    mask = 0;
  }
  free_bitmap(&atlas, 16, BA_ASSETS * 16);
  free_bitmap(&back, 320, 256);
  free_bitmap(&ground, 320, 256);
  if (ba_window) {
    CloseWindow(ba_window);
    ba_window = 0;
  }
  if (screen) {
    CloseScreen(screen);
    screen = 0;
  }
}
/* Board-only clipping. UI uses independent full-screen drawing primitives. */
static void rect(int x, int y, int w, int h, unsigned pen) {
  int right = x + w, bottom = y + h;
  int left = ground_target ? 0 : 4, top = ground_target ? 0 : 40;
  int maxx = ground_target ? 320 : 196, maxy = ground_target ? 256 : 200;
  if (x < left)
    x = left;
  if (y < top)
    y = top;
  if (right > maxx)
    right = maxx;
  if (bottom > maxy)
    bottom = maxy;
  if (right <= x || bottom <= y)
    return;
  SetAPen(&canvas, pen);
  RectFill(&canvas, x, y, right - 1, bottom - 1);
}
void ba_ui_icon(struct RastPort *r, unsigned image, int x, int y) {
  BltMaskBitMapRastPort(&atlas, 0, image * 16, r, x, y, 16, 16, 0xe0, mask);
}
static void sprite(unsigned image, int x, int y) {
  int sx = 0, sy = 0, width = 16, height = 16;
  if (x < 4) {
    sx = 4 - x;
    width -= sx;
    x = 4;
  }
  if (y < 40) {
    sy = 40 - y;
    height -= sy;
    y = 40;
  }
  if (x + width > 196)
    width = 196 - x;
  if (y + height > 200)
    height = 200 - y;
  if (width > 0 && height > 0)
    BltMaskBitMapRastPort(&atlas, sx, image * 16 + sy, &canvas, x, y, width,
                          height, 0xe0, mask);
}
static void mark(const ba_world *w, int x, int y, uint32_t hash) {
  int xx, yy;
  for (yy = y / 16; yy <= (y + 15) / 16; ++yy)
    for (xx = x / 16; xx <= (x + 15) / 16; ++xx)
      if (xx >= 0 && yy >= 0 && xx < w->width && yy < w->height)
        signatures[yy * w->width + xx] =
            signatures[yy * w->width + xx] * 33 + hash;
}
static void objects(const ba_world *w, const ba_animation *a, int paint) {
  unsigned i, j;
  int x, y;
  const uint8_t *p, *q;
  for (i = 0; i < w->crates_count; ++i) {
    p = w->crates[i];
    if (p[6])
      continue;
    x = p[4] * 16;
    y = p[5] * 16;
    if (a->active && a->event == 16)
      for (j = 0; j < a->before.crates_count; ++j) {
        q = a->before.crates[j];
        if (!q[6] && !memcmp(p, q, 4)) {
          x = ba_lerp(q[4] * 16, x, a);
          y = ba_lerp(q[5] * 16, y, a);
          break;
        }
      }
    if (paint)
      sprite(BA_CRATE, ox + x, oy + y);
    else
      mark(w, x, y, 10000 + x * 17 + y);
  }
  /* Pickups/delivery/destruction briefly retain the old loose crate;
   * authoritative ownership has already changed. This never changes the world
   * or event cursor. */
  if (a->active && a->frame < a->duration / 2 && a->event >= 13 &&
      a->event <= 17)
    for (i = 0; i < a->before.crates_count; ++i) {
      p = a->before.crates[i];
      if (p[6])
        continue;
      for (j = 0; j < w->crates_count; ++j)
        if (!w->crates[j][6] && !memcmp(p, w->crates[j], 4))
          break;
      if (j == w->crates_count) {
        x = p[4] * 16;
        y = p[5] * 16;
        if (paint)
          sprite(BA_CRATE, ox + x, oy + y);
        else
          mark(w, x, y, 20000 + x * 17 + y);
      }
    }
  for (i = 0; i < 4; ++i) {
    unsigned dir, changed, flash;
    p = w->players[i];
    q = a->before.players[i];
    if (!p[0])
      continue;
    if (p[1] & 1) {
      if (!a->active || (q[1] & 1) || a->frame >= a->duration / 2)
        continue;
      p = q;
    }
    x = p[2] * 16;
    y = p[3] * 16;
    dir = p[4];
    changed = a->active && memcmp(w->players[i], q, 27);
    if (a->active && q[0] && !(q[1] & 1)) {
      x = ba_lerp(q[2] * 16, x, a);
      y = ba_lerp(q[3] * 16, y, a);
      if (a->frame < a->duration / 2)
        dir = q[4];
    }
    flash = changed && (a->frame & 1);
    if (!paint) {
      mark(w, x, y,
           30000 + i * 1000 + x * 17 + y + dir * 300 + p[1] * 100 +
               p[5] * 9000 + flash * 77);
      continue;
    }
    sprite(BA_ROBOT + i * 4 + (dir & 3), ox + x, oy + y);
    if (p[1] & 2)
      rect(ox + x + 11, oy + y + 12, 4, 4, 10);
    if (i + 1 == w->player) {
      unsigned corner;
      for (corner = 0; corner < 4; ++corner) {
        int xx = ox + x + (corner & 1 ? 12 : 0),
            yy = oy + y + (corner & 2 ? 15 : 0);
        rect(xx, yy, 4, 1, 5);
        rect(ox + x + (corner & 1 ? 15 : 0), oy + y + (corner & 2 ? 12 : 0), 1,
             4, 5);
      }
    }
    if (changed && (a->event == 15 || a->event == 30)) {
      unsigned n = a->frame;
      rect(ox + x + 7, oy + y + 1, 1, 3, 15);
      rect(ox + x + 6, oy + y + 2, 3, 1, 15);
      rect(ox + x + 2 + n, oy + y + 1, 1, 1, 5);
      rect(ox + x + 13 - n, oy + y + 9, 1, 1, 5);
    }
    if (p[5])
      rect(ox + x + 4, oy + y + 15, p[5] * 2, 1, 12);
    if (flash) {
      unsigned pen =
          (a->event == 19 || a->event == 20 || a->event == 25 || a->event == 28)
              ? 12
              : 5;
      rect(ox + x + 1, oy + y, 14, 1, pen);
      rect(ox + x + 1, oy + y + 15, 14, 1, pen);
    }
  }
}
static void outline(int x, int y, int size, unsigned pen) {
  rect(x, y, size, 1, pen);
  rect(x, y + size - 1, size, 1, pen);
  rect(x, y, 1, size, pen);
  rect(x + size - 1, y, 1, size, pen);
}
static void detail_tile(const ba_world *w, unsigned cell, int x, int y) {
  unsigned tile = tile_images[cell],
           k = ba_terrain(w, cell % w->width, cell / w->width);
  const uint8_t *at = 0, *h = 0;
  BltBitMapRastPort(&atlas, 0, tile * 16, &canvas, x, y, 16, 16, 0xc0);
  /* Sparse edge patterns sit below objects. Distinct geometry, not only colour.
   */
  if (danger[cell] & BA_TAR) {
    rect(x + 1, y + 12, 3, 1, 9);
    rect(x + 5, y + 14, 3, 1, 9);
    rect(x + 9, y + 12, 3, 1, 9);
    rect(x + 13, y + 14, 2, 1, 9);
  }
  if (danger[cell] & BA_BURST) {
    rect(x + 1, y + 1, 3, 1, 12);
    rect(x + 1, y + 1, 1, 3, 12);
    rect(x + 12, y + 14, 3, 1, 12);
    rect(x + 14, y + 12, 1, 3, 12);
  }
  if (danger[cell] & BA_TURRET) {
    rect(x + 7, y + 1, 2, 2, 4);
    rect(x + 1, y + 7, 2, 2, 4);
  }
  if (danger[cell] & BA_LASER_H)
    rect(x, y + 7, 16, 1, 12);
  if (danger[cell] & BA_LASER_V)
    rect(x + 7, y, 1, 16, 12);
  if (k == 9 || k == 11)
    at = ba_attribute(w, cell % w->width, cell / w->width);
  if (at && (k == 9 || k == 11))
    h = ba_hazard(w, k == 9 ? 1 : 2, at[2]);
  if (h) {
    /* Authoritative armed/charge and owner; never advance machinery locally. */
    if (k == 9 && !h[2])
      rect(x + 5, y + 5, 6, 6, 3);
    if (k == 11) {
      rect(x + 3, y + 12, 3, 2, h[2] > 0 ? 15 : 0);
      rect(x + 9, y + 12, 3, 2, h[2] > 1 ? 15 : 0);
    }
    if (h[3]) {
      char n[2] = {'0' + h[3], 0};
      WaitBlit();
      SetAPen(&canvas, 6 + h[3] - 1);
      Move(&canvas, x + 10, y + 6);
      Text(&canvas, n, 1);
    }
  }
}
static void overview(const ba_world *w) {
  unsigned x, y, i, k;
  int bx = 4 + (192 - w->width * 8) / 2, by = 40 + (160 - w->height * 8) / 2;
  rect(4, 40, 192, 160, 0);
  for (y = 0; y < w->height; ++y)
    for (x = 0; x < w->width; ++x) {
      const uint8_t *at = ba_attribute(w, x, y);
      k = ba_terrain(w, x, y);
      rect(bx + x * 8, by + y * 8, 8, 8,
           k == 1   ? 2
           : k == 8 ? 0
           : k == 5 ? 13
           : k      ? 3
                    : 1);
      if (danger[y * w->width + x] & BA_TAR)
        rect(bx + x * 8 + 1, by + y * 8 + 6, 6, 1, 9);
      if (danger[y * w->width + x] & BA_BURST)
        outline(bx + x * 8 + 1, by + y * 8 + 1, 6, 12);
      if (danger[y * w->width + x] & BA_TURRET)
        rect(bx + x * 8 + 1, by + y * 8 + 1, 1, 1, 4);
      if (danger[y * w->width + x] & BA_LASER_H)
        rect(bx + x * 8, by + y * 8 + 3, 8, 1, 12);
      if (danger[y * w->width + x] & BA_LASER_V)
        rect(bx + x * 8 + 3, by + y * 8, 1, 8, 12);
      if (k == 3 && at)
        outline(bx + x * 8 + 1, by + y * 8 + 1, 6, 5 + at[2]);
      if (k == 9 || k == 7 || k == 11)
        rect(bx + x * 8 + 3, by + y * 8 + 3, 2, 2, 12);
    }
  for (i = 0; i < w->crates_count; ++i)
    if (!w->crates[i][6])
      rect(bx + w->crates[i][4] * 8 + 2, by + w->crates[i][5] * 8 + 2, 4, 4,
           10);
  for (i = 0; i < w->players_count; ++i) {
    const uint8_t *p = w->players[i];
    char n[2] = {'1' + i, 0};
    if (p[1] & 1)
      continue;
    x = bx + p[2] * 8;
    y = by + p[3] * 8;
    rect(x, y, 8, 8, 6 + i);
    WaitBlit();
    SetAPen(&canvas, 0);
    Move(&canvas, x + 1, y + 6);
    Text(&canvas, n, 1);
  }
  x = bx + ba_viewport.cx * 8;
  y = by + ba_viewport.cy * 8;
  k = (w->width < 12 ? w->width : 12) * 8;
  i = (w->height < 10 ? w->height : 10) * 8;
  rect(x, y, k, 1, 5);
  rect(x, y + i - 1, k, 1, 5);
  rect(x, y, 1, i, 5);
  rect(x + k - 1, y, 1, i, 5);
  if (ba_viewport.inspect)
    outline(bx + ba_viewport.x * 8, by + ba_viewport.y * 8, 8, 15);
}
void ba_draw(const ba_world *w, const ba_editor *e, const char *status,
             const ba_animation *a) {
  unsigned i, force, geometry_changed, inspect_changed;
  int changed = 0;
  ULONG begin = ba_display_time(), elapsed;
  ba_view_sync(&ba_viewport, w);
  /* Keep existing sequential animation; only bring a moved subject into view.
   * Rich action focus/narration belongs to the next slice. */
  if (a->active && a->frame == 0 && !ba_viewport.inspect &&
      !ba_viewport.overview)
    for (i = 0; i < w->players_count; ++i)
      if (!(w->players[i][1] & 1) &&
          memcmp(w->players[i] + 2, a->before.players[i] + 2, 2)) {
        if (w->players[i][2] < ba_viewport.cx ||
            w->players[i][2] >= ba_viewport.cx + 12 ||
            w->players[i][3] < ba_viewport.cy ||
            w->players[i][3] >= ba_viewport.cy + 10)
          ba_view_center(&ba_viewport, w, w->players[i][2], w->players[i][3]);
        break;
      }
  force = !initialized || board_mode != ba_viewport.panel ||
          old_view.cx != ba_viewport.cx || old_view.cy != ba_viewport.cy ||
          old_view.overview != ba_viewport.overview;
  if (!initialized || board_mode != ba_viewport.panel) {
    /* Full-width overlays also touched the gutters. Restore those on close. */
    WaitBlit();
    SetAPen(&canvas, 0);
    RectFill(&canvas, 0, 38, 319, 203);
    BltBitMapRastPort(&back, 0, 38, ba_window->RPort, 0, 38, 320, 166, 0xc0);
    ba_ui_reset();
  }
  inspect_changed = !initialized || old_view.inspect != ba_viewport.inspect ||
                    old_view.x != ba_viewport.x || old_view.y != ba_viewport.y;
  board_mode = ba_viewport.panel;
  old_view = ba_viewport;
  ox =
      4 + (w->width < 12 ? (192 - w->width * 16) / 2 : 0) - ba_viewport.cx * 16;
  oy = 40 + (w->height < 10 ? (160 - w->height * 16) / 2 : 0) -
       ba_viewport.cy * 16;
  SetDrMd(&canvas, JAM1);
  if (w->map_valid && !ba_viewport.panel) {
    if (force) {
      rect(4, 40, 192, 160, 0);
      ++full;
    }
    geometry_changed = !initialized || hazard_count != w->hazards_count ||
                       memcmp(hazard_state, w->hazards, sizeof hazard_state);
    if (geometry_changed) {
      ba_danger(w, danger);
      memset(hazard_cells, 0, sizeof hazard_cells);
      for (i = 0; i < w->attributes_count; ++i) {
        const uint8_t *at = w->attributes[i], *h;
        unsigned k = ba_terrain(w, at[0], at[1]);
        if (k != 9 && k != 11)
          continue;
        h = ba_hazard(w, k == 9 ? 1 : 2, at[2]);
        if (h)
          hazard_cells[at[1] * w->width + at[0]] = h[2] + h[3] * 4;
      }
      memcpy(hazard_state, w->hazards, sizeof hazard_state);
      hazard_count = w->hazards_count;
    }
    if (!initialized || memcmp(last_groups, w->groups, sizeof last_groups)) {
      for (i = 0; i < w->tile_count; ++i)
        tile_images[i] = ba_tile_image(w, i);
      memcpy(last_groups, w->groups, sizeof last_groups);
    }
    if (inspect_changed) {
      memset(inspect_cells, 0, sizeof inspect_cells);
      if (ba_viewport.inspect)
        for (i = 0; i < w->tile_count; ++i)
          inspect_cells[i] = ba_related(w, ba_viewport.x, ba_viewport.y,
                                        i % w->width, i / w->width);
    }
    /* Retained planar terrain: pans become one blit, not 120 library calls.
     * Only authoritative hazard/group changes repaint the cached tiles. */
    WaitBlit();
    ground_target = 1;
    canvas.BitMap = &ground;
    for (i = 0; i < w->tile_count; ++i) {
      uint32_t key = tile_images[i] + 1 + ((uint32_t)danger[i] << 8) +
                     ((uint32_t)hazard_cells[i] << 16);
      if (!initialized || key != ground_keys[i])
        detail_tile(w, i, (i % w->width) * 16, (i / w->width) * 16);
      ground_keys[i] = key;
      signatures[i] = key;
      if (ba_viewport.inspect) {
        if (inspect_cells[i])
          signatures[i] += 0x1000000;
        if (i == (unsigned)ba_viewport.y * w->width + ba_viewport.x)
          signatures[i] += 0x2000000;
      }
    }
    WaitBlit();
    ground_target = 0;
    canvas.BitMap = &back;
    if (force && !ba_viewport.overview) {
      unsigned width = w->width < 12 ? w->width : 12,
               height = w->height < 10 ? w->height : 10;
      BltBitMapRastPort(&ground, ba_viewport.cx * 16, ba_viewport.cy * 16,
                        &canvas, ox + ba_viewport.cx * 16,
                        oy + ba_viewport.cy * 16, width * 16, height * 16,
                        0xc0);
    }
    if (w->valid)
      objects(w, a, 0);
    for (i = 0; i < w->tile_count; ++i) {
      int x = ox + (i % w->width) * 16, y = oy + (i / w->width) * 16;
      dirty[i] = force || previous[i] != signatures[i];
      if (dirty[i]) {
        ++changed;
        if (!ba_viewport.overview && x >= 4 && x < 196 && y >= 40 && y < 200) {
          if (!force)
            BltBitMapRastPort(&ground, (i % w->width) * 16, (i / w->width) * 16,
                              &canvas, x, y, 16, 16, 0xc0);
          if (ba_viewport.inspect && inspect_cells[i])
            outline(x, y, 16, 10);
        }
      }
    }
    if (changed) {
      if (ba_viewport.overview)
        overview(w);
      else {
        if (w->valid)
          objects(w, a, 1);
        if (ba_viewport.inspect)
          outline(ox + ba_viewport.x * 16, oy + ba_viewport.y * 16, 16, 5);
      }
    }
    if (changed && (force || ba_viewport.overview))
      BltBitMapRastPort(&back, 4, 40, ba_window->RPort, 4, 40, 192, 160, 0xc0);
    else
      for (i = 0; i < w->tile_count; ++i)
        if (dirty[i]) {
          int x = ox + (i % w->width) * 16, y = oy + (i / w->width) * 16;
          if (x >= 4 && x < 196 && y >= 40 && y < 200)
            BltBitMapRastPort(&back, x, y, ba_window->RPort, x, y, 16, 16,
                              0xc0);
        }
    memcpy(previous, signatures, sizeof previous);
    initialized = 1;
  }
  ba_ui_draw(&canvas, ba_window->RPort, w, e, status);
  WaitBlit();
  elapsed = ba_display_time() - begin;
  ticks += elapsed;
  if (elapsed > worst)
    worst = elapsed;
  ++draws;
  cells += changed;
}
void ba_display_metrics(void) {
  printf("RENDER draws=%lu dirty_cells=%lu full_boards=%lu fields=%lu "
         "max_fields=%lu chip_bitplanes=81920 chip_atlas=%lu chip_font=576 "
         "chip_ground=40960\n",
         draws, cells, full, ticks, worst, (ULONG)BA_ASSETS * 160);
}
void ba_capture(const ba_world *w) {
  FILE *f;
  unsigned p;
  /* Explicit portable layout, no host/68000 struct padding in evidence. */
  f = fopen("BT:state.bin", "wb");
  if (f) {
    fputc(w->width, f);
    fputc(w->height, f);
    fputc(w->player, f);
    fputc(w->valid, f);
    fwrite(w->tiles, 1, 160, f);
    fwrite(w->snapshot, 1, 31, f);
    fwrite(w->players, 1, 108, f);
    fclose(f);
  }
  WaitBlit();
  f = fopen("BT:screen.bin", "wb");
  if (f) {
    for (p = 0; p < 4; ++p)
      fwrite(screen->BitMap.Planes[p], 1, 10240, f);
    fclose(f);
  }
}
