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
static struct TextFont *font;
static struct BitMap back, atlas;
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
  struct TextAttr fixed = {(STRPTR)"topaz.font", 8, FS_NORMAL, FPF_ROMFONT};
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
  nw.IDCMPFlags = IDCMP_RAWKEY;
  ba_window = OpenWindow(&nw);
  if (!ba_window || !bitmap(&back, 320, 256) ||
      !bitmap(&atlas, 16, BA_ASSETS * 12) ||
      !(mask = AllocMem(BA_ASSETS * 24, MEMF_CHIP | MEMF_CLEAR))) {
    ba_display_close();
    return 0;
  }
  InitRastPort(&canvas);
  canvas.BitMap = &back;
  font = OpenFont(&fixed);
  if (!font) {
    ba_display_close();
    return 0;
  }
  SetFont(&canvas, font);
  SetRast(&canvas, 0);
  SetRast(ba_window->RPort, 0);
  for (p = 0; p < 4; ++p)
    memset(atlas.Planes[p], 0, BA_ASSETS * 24);
  for (k = 0; k < BA_ASSETS; ++k)
    for (y = 0; y < 12; ++y)
      for (x = 0; x < 16; ++x) {
        unsigned color = ba_asset_pixel(k, x, y), pos = k * 24 + y * 2 + x / 8,
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
  if (font) {
    CloseFont(font);
    font = 0;
  }
  if (mask) {
    FreeMem(mask, BA_ASSETS * 24);
    mask = 0;
  }
  free_bitmap(&atlas, 16, BA_ASSETS * 12);
  free_bitmap(&back, 320, 256);
  if (ba_window) {
    CloseWindow(ba_window);
    ba_window = 0;
  }
  if (screen) {
    CloseScreen(screen);
    screen = 0;
  }
}
static void rect(int x, int y, int w, int h, unsigned pen) {
  SetAPen(&canvas, pen);
  RectFill(&canvas, x, y, x + w - 1, y + h - 1);
}
static void sprite(unsigned image, int x, int y) {
  BltMaskBitMapRastPort(&atlas, 0, image * 12, &canvas, x, y, 16, 12, 0xe0,
                        mask);
}
static void mark(const ba_world *w, int x, int y, uint32_t hash) {
  int xx, yy;
  for (yy = y / 12; yy <= (y + 11) / 12; ++yy)
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
    y = p[5] * 12;
    if (a->active && a->event == 16)
      for (j = 0; j < a->before.crates_count; ++j) {
        q = a->before.crates[j];
        if (!q[6] && !memcmp(p, q, 4)) {
          x = ba_lerp(q[4] * 16, x, a);
          y = ba_lerp(q[5] * 12, y, a);
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
        y = p[5] * 12;
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
    y = p[3] * 12;
    dir = p[4];
    changed = a->active && memcmp(w->players[i], q, 27);
    if (a->active && q[0] && !(q[1] & 1)) {
      x = ba_lerp(q[2] * 16, x, a);
      y = ba_lerp(q[3] * 12, y, a);
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
      rect(ox + x + 9, oy + y + 2, 4, 3, 7);
    if (i + 1 == w->player)
      rect(ox + x + 6, oy + y + 10, 4, 1, 11);
    if (changed && (a->event == 15 || a->event == 30)) {
      unsigned n = a->frame;
      rect(ox + x + 7, oy + y + 1, 1, 3, 7);
      rect(ox + x + 6, oy + y + 2, 3, 1, 7);
      rect(ox + x + 2 + n, oy + y + 1, 1, 1, 11);
      rect(ox + x + 13 - n, oy + y + 9, 1, 1, 11);
    }
    if (p[5])
      rect(ox + x + 4, oy + y + 9, p[5] * 2, 1, 12);
    if (flash) {
      unsigned pen =
          (a->event == 19 || a->event == 20 || a->event == 25 || a->event == 28)
              ? 12
              : 11;
      rect(ox + x + 1, oy + y, 14, 1, pen);
      rect(ox + x + 1, oy + y + 11, 14, 1, pen);
    }
  }
}
void ba_draw(const ba_world *w, const ba_editor *e, const char *status,
             const ba_animation *a) {
  unsigned i, j;
  int changed = 0;
  ULONG begin = ba_display_time(), elapsed;
  ox = (320 - w->width * 16) / 2;
  oy = 24 + (192 - w->height * 12) / 2;
  SetDrMd(&canvas, JAM1);
  if (w->map_valid) {
    if (!initialized || memcmp(last_groups, w->groups, sizeof last_groups)) {
      for (i = 0; i < w->tile_count; ++i)
        tile_images[i] = ba_tile_image(w, i);
      memcpy(last_groups, w->groups, sizeof last_groups);
    }
    for (i = 0; i < w->tile_count; ++i)
      signatures[i] = tile_images[i] + 1;
    for (j = 0; j < w->hazards_count; ++j)
      for (i = 0; i < w->attributes_count; ++i) {
        const uint8_t *at = w->attributes[i], *h = w->hazards[j];
        unsigned cell = at[1] * w->width + at[0];
        unsigned kind = (w->tiles[cell / 2] >> ((cell & 1) * 4)) & 15;
        if (at[2] == h[1] && kind == (h[0] == 1 ? 9 : 11))
          signatures[cell] += h[2] * 256 + h[3] * 1024;
      }
    if (w->valid)
      objects(w, a, 0);
    for (i = 0; i < w->tile_count; ++i) {
      dirty[i] = !initialized || previous[i] != signatures[i];
      if (dirty[i]) {
        int x = ox + (i % w->width) * 16, y = oy + (i / w->width) * 12;
        unsigned tile = tile_images[i];
        BltBitMapRastPort(&atlas, 0, tile * 12, &canvas, x, y, 16, 12, 0xc0);
        if ((signatures[i] & 0xffffff00) && (tile == 9 || tile == 11)) {
          /* Actual armed/charge state, not a simulated hazard cycle. */
          for (j = 0; j < w->attributes_count; ++j)
            if (w->attributes[j][0] == i % w->width &&
                w->attributes[j][1] == i / w->width) {
              unsigned h;
              for (h = 0; h < w->hazards_count; ++h)
                if (w->hazards[h][1] == w->attributes[j][2] &&
                    w->hazards[h][0] == (tile == 9 ? 1 : 2))
                  rect(x + 3, y + 10, 1 + w->hazards[h][2] * 3, 1,
                       w->hazards[h][2] ? 12 : 3);
            }
        }
        ++changed;
      }
    }
    if (changed && w->valid)
      objects(w, a, 1);
    for (i = 0; i < w->tile_count; ++i)
      if (dirty[i]) {
        int x = ox + (i % w->width) * 16, y = oy + (i / w->width) * 12;
        BltBitMapRastPort(&back, x, y, ba_window->RPort, x, y, 16, 12, 0xc0);
        previous[i] = signatures[i];
      }
    if (!initialized) {
      ++full;
      initialized = 1;
    }
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
         "max_fields=%lu chip_bitplanes=81920 chip_atlas=%lu\n",
         draws, cells, full, ticks, worst, (ULONG)BA_ASSETS * 120);
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
