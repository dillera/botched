#include "platform.h"
#include <graphics/gfxbase.h>
#include <proto/dos.h>
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
void ba_draw(const ba_world *w, const ba_editor *editor, const char *status) {
  struct RastPort *rp = ba_window->RPort;
  unsigned x, y, i, k, a = 0, j, dir;
  int ox = (320 - w->width * 12) / 2, oy = 16;
  static const char glyph[] = " #SD>vCL!TXUP?";
  char label[40];
  SetRast(rp, 0);
  SetDrMd(rp, JAM1);
  SetAPen(rp, 1);
  sprintf(label, "BOTCHED! P%ld Round %ld Tick %ld", (long)w->player,
          (long)w->snapshot[1], (long)w->snapshot[2]);
  text(4, 9, label);
  if (w->map_valid)
    for (y = 0; y < w->height; ++y)
      for (x = 0; x < w->width; ++x) {
        i = y * w->width + x;
        k = (w->tiles[i / 2] >> ((i & 1) * 4)) & 15;
        SetAPen(rp, k == 1 ? 2 : (k == 8 ? 0 : 3));
        RectFill(rp, ox + x * 12, oy + y * 8, ox + x * 12 + 10, oy + y * 8 + 7);
        if (k > 1) {
          char s[2] = {glyph[k < 14 ? k : 0], 0};
          if (a < w->attributes_count && w->attributes[a][0] == x &&
              w->attributes[a][1] == y) {
            const uint8_t *attr = w->attributes[a++];
            if (k == 5 || k == 7) {
              dir = attr[4];
              if (k == 5)
                for (j = 0; j < w->groups_count; ++j)
                  if (w->groups[j][0] == attr[2] && w->groups[j][1])
                    dir = (dir + 2) % 4;
              s[0] = "^>v<"[dir & 3];
            }
            if (k == 9 || k == 11)
              for (j = 0; j < w->hazards_count; ++j)
                if (w->hazards[j][0] == (k == 9 ? 1 : 2) &&
                    w->hazards[j][1] == attr[2])
                  s[0] = k == 9 ? (w->hazards[j][2] ? 'T' : 't')
                                : '0' + w->hazards[j][2];
          }
          SetAPen(rp, 1);
          text(ox + x * 12 + 2, oy + y * 8 + 7, s);
        }
      }
  if (w->valid) {
    for (i = 0; i < w->crates_count; ++i)
      if (!w->crates[i][6]) {
        x = w->crates[i][4];
        y = w->crates[i][5];
        SetAPen(rp, 7);
        RectFill(rp, ox + x * 12 + 3, oy + y * 8 + 2, ox + x * 12 + 7,
                 oy + y * 8 + 5);
      }
    for (i = 0; i < 4; ++i)
      if (w->players[i][0] && !(w->players[i][1] & 1)) {
        const uint8_t *p = w->players[i];
        char s[2] = {'1' + i, 0};
        x = ox + p[2] * 12;
        y = oy + p[3] * 8;
        SetAPen(rp, 4 + i);
        RectFill(rp, x + 1, y, x + 10, y + 7);
        SetAPen(rp, 0);
        text(x + 2, y + 7, s);
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
  text(4, 153, status);
  for (i = 0; i < 3; ++i) {
    SetAPen(rp, i == ((editor->locked && w->snapshot[2]) ? w->snapshot[2] - 1
                                                         : editor->slot)
                    ? 7
                    : 1);
    sprintf(label, "%ld:%s", (long)i + 1, ba_order_name(editor->orders[i]));
    text(4 + i * 104, 165, label);
  }
  SetAPen(rp, 1);
  text(4, 177, "Left/Right slot  Up/Down order");
  text(4, 188, "1-9 order  ENTER ready  ESC quit");
  if (w->snapshot[0] == 2) {
    char winners[9];
    unsigned count = 0;
    for (i = 0; i < 4; ++i)
      if (w->winners & (1u << i)) {
        winners[count++] = '1' + i;
        winners[count++] = ' ';
      }
    winners[count] = 0;
    sprintf(label, "Winner(s): %s Score %ld", winners,
            w->player ? (long)ba_u32(w->players[w->player - 1] + 10) : 0L);
  } else
    sprintf(label, "%s  P%ld score %ld",
            editor->pending  ? "Sending..."
            : editor->locked ? "Waiting / replay"
                             : "Edit orders",
            (long)w->player,
            w->player ? (long)ba_u32(w->players[w->player - 1] + 10) : 0L);
  if (w->valid && w->snapshot[0] == 0 && (w->snapshot[14] & 2)) {
    struct DateStamp d;
    uint32_t unix_now, end;
    long left;
    DateStamp(&d);
    unix_now = 252460800UL + (uint32_t)d.ds_Days * 86400UL +
               d.ds_Minute * 60UL + d.ds_Tick / 50;
    end = ba_u32(w->snapshot + 23);
    left = (int32_t)(end - unix_now);
    if (left < 0)
      left = 0;
    if (left > 999)
      left = 999;
    sprintf(label, "%s  P%ld ~%lds left",
            editor->pending  ? "Sending"
            : editor->locked ? "Waiting"
                             : "Edit orders",
            (long)w->player, left);
  }
  text(4, 199, label);
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
