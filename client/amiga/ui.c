#include "platform.h"
#include <proto/dos.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>
static struct RastPort *rp, *target;
static char old_ui[6][41];
static void rect(int x, int y, int w, int h, unsigned pen) {
  SetAPen(rp, pen);
  RectFill(rp, x, y, x + w - 1, y + h - 1);
}
static void text(int x, int y, const char *s, unsigned pen) {
  SetAPen(rp, pen);
  Move(rp, x, y);
  Text(rp, (char *)s, strlen(s));
}
static void ui_line(unsigned row, const char *s, unsigned pen) {
  static const int yy[] = {0, 12, 216, 234, 245, 0};
  if (!strcmp(old_ui[row], s))
    return;
  strncpy(old_ui[row], s, 40);
  old_ui[row][40] = 0;
  rect(0, yy[row], 320, row == 2 ? 18 : 11, 0);
  text(4, yy[row] + 8, old_ui[row], pen);
  BltBitMapRastPort(rp->BitMap, 0, yy[row], target, 0, yy[row], 320,
                    row == 2 ? 18 : 11, 0xc0);
}
/* nix13 sprintf uses RawDoFmt: keep variadic numbers long and use strings
 * for prefixes (a promoted C char is not RawDoFmt's 16-bit %c argument). */
void ba_ui_draw(struct RastPort *canvas, struct RastPort *screen,
                const ba_world *w, const ba_editor *e, const char *status) {
  unsigned i;
  char label[80];
  rp = canvas;
  target = screen;
  sprintf(label, "BOTCHED! P%ld  R%ld T%ld", (long)w->player,
          (long)w->snapshot[1], (long)w->snapshot[2]);
  if (w->valid && w->snapshot[0] == 0 && (w->snapshot[14] & 2)) {
    struct DateStamp d;
    long left;
    uint32_t now;
    DateStamp(&d);
    now = 252460800UL + (uint32_t)d.ds_Days * 86400UL + d.ds_Minute * 60UL +
          d.ds_Tick / 50;
    left = (int32_t)(ba_u32(w->snapshot + 23) - now);
    if (left < 0)
      left = 0;
    if (left > 999)
      left = 999;
    sprintf(label + strlen(label), "  ~%lds", left);
  }
  ui_line(0, label, 7);
  /* Four fixed-width colour-coded scores. Only changed text damages this strip.
   */
  sprintf(label, "%ld:P1:%ld P2:%ld P3:%ld P4:%ld", (long)w->player,
          (long)ba_u32(w->players[0] + 10), (long)ba_u32(w->players[1] + 10),
          (long)ba_u32(w->players[2] + 10), (long)ba_u32(w->players[3] + 10));
  if (strcmp(old_ui[1], label)) {
    strncpy(old_ui[1], label, 40);
    old_ui[1][40] = 0;
    rect(0, 12, 320, 11, 0);
    for (i = 0; i < 4; ++i) {
      sprintf(label, "%s%ld %ld", w->player == i + 1 ? ">" : "P", (long)i + 1,
              (long)ba_u32(w->players[i] + 10));
      text(i * 80 + 4, 20, label, 4 + i);
    }
    BltBitMapRastPort(rp->BitMap, 0, 12, target, 0, 12, 320, 11, 0xc0);
  }
  sprintf(label, "%ld%ld%ld%ld%ld%ld%ld", (long)e->orders[0],
          (long)e->orders[1], (long)e->orders[2], (long)e->slot,
          (long)e->locked, (long)e->pending, (long)w->snapshot[2]);
  if (strcmp(old_ui[2], label)) {
    strcpy(old_ui[2], label);
    rect(0, 216, 320, 18, 0);
    for (i = 0; i < 3; ++i) {
      unsigned selected =
          i == ((e->locked && w->snapshot[2]) ? (unsigned)w->snapshot[2] - 1u
                                              : (unsigned)e->slot);
      rect(3 + i * 106, 217, 102, 16, selected ? 9 : 3);
      rect(4 + i * 106, 218, 100, 14, 2);
      sprintf(label, "%ld %s", (long)i + 1, ba_order_name(e->orders[i]));
      text(10 + i * 106, 228, label, selected ? 7 : 1);
    }
    BltBitMapRastPort(rp->BitMap, 0, 216, target, 0, 216, 320, 18, 0xc0);
  }
  if (w->snapshot[0] == 2) {
    sprintf(label, "FINISHED  Winners:%s%s%s%s", w->winners & 1 ? " P1" : "",
            w->winners & 2 ? " P2" : "", w->winners & 4 ? " P3" : "",
            w->winners & 8 ? " P4" : "");
    ui_line(3, label, 7);
  } else
    ui_line(3, status, e->pending ? 7 : 1);
  ui_line(4,
          e->pending  ? "SENDING / awaiting server confirmation"
          : e->locked ? "ACCEPTED / replay or waiting   ESC quit"
                      : "1-9 order <> slot ENTER ready ESC quit",
          e->locked ? 8 : 1);
}
