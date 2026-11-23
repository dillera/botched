#include "platform.h"
#include <graphics/gfxbase.h>
#include <proto/exec.h>
#include <proto/graphics.h>
#include <proto/intuition.h>
#include <stdio.h>
#include <string.h>
struct Window *ba_window;
static struct Screen *screen;
static UWORD palette[8] = {0x112, 0xddd, 0x456, 0x964,
                           0xf66, 0x6df, 0x8e7, 0xfd5};
int ba_display_open(void) {
  struct NewScreen ns = {0};
  struct NewWindow nw = {0};
  ns.Width = 320;
  ns.Height = 200;
  ns.Depth = 3;
  ns.DetailPen = 1;
  ns.BlockPen = 2;
  ns.Type = CUSTOMSCREEN;
  screen = OpenScreen(&ns);
  if (!screen)
    return 0;
  LoadRGB4(&screen->ViewPort, palette, 8);
  nw.Width = 320;
  nw.Height = 200;
  nw.Type = CUSTOMSCREEN;
  nw.Screen = screen;
  nw.Flags = BORDERLESS | BACKDROP | ACTIVATE | RMBTRAP | NOCAREREFRESH;
  nw.IDCMPFlags = IDCMP_RAWKEY;
  ba_window = OpenWindow(&nw);
  if (!ba_window) {
    ba_display_close();
    return 0;
  }
  ShowTitle(screen, FALSE);
  return 1;
}
void ba_display_close(void) {
  if (ba_window) {
    CloseWindow(ba_window);
    ba_window = 0;
  }
  if (screen) {
    CloseScreen(screen);
    screen = 0;
  }
}
static void text(int x, int y, const char *s) {
  Move(ba_window->RPort, x, y);
  Text(ba_window->RPort, (char *)s, strlen(s));
}
void ba_draw(const ba_world *w, const char *status) {
  struct RastPort *rp = ba_window->RPort;
  unsigned x, y, i, k;
  int ox = (320 - w->width * 12) / 2, oy = 18;
  static const char glyph[] = " #SD>vCL!TXUP?";
  char label[40];
  SetRast(rp, 0);
  SetDrMd(rp, JAM1);
  SetAPen(rp, 1);
  text(8, 9, "BOTCHED!");
  if (w->map_valid)
    for (y = 0; y < w->height; ++y)
      for (x = 0; x < w->width; ++x) {
        i = y * w->width + x;
        k = (w->tiles[i / 2] >> ((i & 1) * 4)) & 15;
        SetAPen(rp, k == 1 ? 2 : (k == 8 ? 0 : 3));
        RectFill(rp, ox + x * 12, oy + y * 10, ox + x * 12 + 10,
                 oy + y * 10 + 8);
        if (k > 1) {
          char s[2] = {glyph[k < 14 ? k : 0], 0};
          SetAPen(rp, 1);
          text(ox + x * 12 + 2, oy + y * 10 + 8, s);
        }
      }
  if (w->valid) {
    for (i = 0; i < w->crates_count; ++i)
      if (!w->crates[i][6]) {
        x = w->crates[i][4];
        y = w->crates[i][5];
        SetAPen(rp, 7);
        RectFill(rp, ox + x * 12 + 3, oy + y * 10 + 2, ox + x * 12 + 7,
                 oy + y * 10 + 6);
      }
    for (i = 0; i < 4; ++i)
      if (w->players[i][0] && !(w->players[i][1] & 1)) {
        const uint8_t *p = w->players[i];
        char s[2] = {'1' + i, 0};
        x = ox + p[2] * 12;
        y = oy + p[3] * 10;
        SetAPen(rp, 4 + i);
        RectFill(rp, x + 1, y, x + 10, y + 8);
        SetAPen(rp, 0);
        text(x + 2, y + 8, s);
        SetAPen(rp, 1);
        if (p[4] == 0 || p[4] == 2) {
          Move(rp, x + 4, y + (p[4] == 0 ? 0 : 8));
          Draw(rp, x + 7, y + (p[4] == 0 ? 0 : 8));
        } else {
          Move(rp, x + (p[4] == 1 ? 10 : 1), y + 2);
          Draw(rp, x + (p[4] == 1 ? 10 : 1), y + 6);
        }
      }
  }
  SetAPen(rp, 1);
  text(4, 187, status);
  sprintf(label, "R refresh  ESC quit  You: %ld", (long)w->player);
  text(4, 198, label);
  WaitBlit();
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
    for (p = 0; p < 3; ++p)
      fwrite(screen->BitMap.Planes[p], 1, 8000, f);
    fclose(f);
  }
}
