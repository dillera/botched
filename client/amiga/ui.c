#include "platform.h"
#include <proto/dos.h>
#include <proto/graphics.h>
#include <stdio.h>
#include <string.h>
static struct RastPort *rp, *target;
static uint32_t cache[5];
static unsigned valid, copies, old_choice = 255;
static int copy_rects[16][4];
static const char *orders[] = {"FORWARD 1", "FORWARD 2",  "BACK 1",
                               "TURN LEFT", "TURN RIGHT", "TURN 180",
                               "DROP",      "TOGGLE",     "WAIT"};
static const char *explain[] = {"FORWARD 1: ONE CELL; TAR PREVENTS MOVEMENT.",
                                "FORWARD 2: UP TO TWO CELLS; TAR SLOWS YOU.",
                                "BACK 1: REVERSE ONE CELL; KEEP YOUR FACING.",
                                "TURN LEFT: FACE 90 DEGREES LEFT. NO MOVE.",
                                "TURN RIGHT: FACE 90 DEGREES RIGHT. NO MOVE.",
                                "TURN 180: FACE THE OPPOSITE WAY. NO MOVE.",
                                "DROP: LEAVE CARGO HERE. DOES NOT SCORE.",
                                "TOGGLE: ACTIVATE ALL CONTROLS IN REACH.",
                                "WAIT: NO ORDER ACTION. MACHINES STILL ACT."};
static const char *terrain[] = {
    "FLOOR",    "WALL",         "SPAWN",       "DOCK",        "DISPENSER",
    "CONVEYOR", "BELT CONTROL", "FIXED LASER", "PIT",         "BURST TRAP",
    "TAR DRUM", "TURRET",       "TURRET PAD",  "TRAP CONTROL"};
static uint32_t hash(uint32_t h, const void *bytes, unsigned n) {
  const unsigned char *p = bytes;
  while (n--)
    h = h * 33 + *p++;
  return h;
}
void ba_ui_reset(void) {
  valid = 0;
  old_choice = 255;
}
static void rect(int x, int y, int w, int h, unsigned pen) {
  SetAPen(rp, pen);
  RectFill(rp, x, y, x + w - 1, y + h - 1);
}
static void frame(int x, int y, int w, int h, unsigned pen) {
  rect(x, y, w, 1, pen);
  rect(x, y + h - 1, w, 1, pen);
  rect(x, y, 1, h, pen);
  rect(x + w - 1, y, 1, h, pen);
}
static void text(int x, int y, const char *s, unsigned pen) {
  unsigned n = strlen(s), limit = (320 - x) / 6;
  WaitBlit();
  SetAPen(rp, pen);
  Move(rp, x, y + 6);
  Text(rp, (char *)s, n < limit ? n : limit);
}
static int region(unsigned id, uint32_t key, int x, int y, int w, int h) {
  if ((valid & (1u << id)) && cache[id] == key)
    return 0;
  WaitBlit();
  cache[id] = key;
  valid |= 1u << id;
  rect(x, y, w, h, 0);
  return 1;
}
static void publish(int x, int y, int w, int h) {
  if (copies < 16) {
    copy_rects[copies][0] = x;
    copy_rects[copies][1] = y;
    copy_rects[copies][2] = w;
    copy_rects[copies++][3] = h;
  }
}
static long remaining(const ba_world *w) {
  struct DateStamp d;
  uint32_t now;
  long n;
  if (!w->valid || w->snapshot[0] || !(w->snapshot[14] & 2))
    return -1;
  DateStamp(&d);
  now = 252460800UL + (uint32_t)d.ds_Days * 86400UL + d.ds_Minute * 60UL +
        d.ds_Tick / 50;
  n = (int32_t)(ba_u32(w->snapshot + 23) - now);
  return n < 0 ? 0 : n > 86400 ? 86400 : n;
}
static void top(const ba_world *w) {
  char b[80], score[3];
  unsigned i, j, n = w->players_count;
  long left = remaining(w);
  uint32_t h = hash(1, w->snapshot, 31);
  for (i = 0; i < w->players_count; ++i) {
    h = hash(h, w->players[i] + 10, 4);
    h = hash(h, w->players[i] + 5, 1);
    h = hash(h, w->players[i] + 1, 1);
  }
  h = hash(h, &left, sizeof left);
  if (!region(0, h, 0, 0, 320, 38))
    return;
  text(4, 2, "BOTCHED!", 15);
  sprintf(b, "YOU P%ld R%ld/%ld", (long)w->player, (long)w->snapshot[1],
          (long)w->snapshot[3]);
  text(62, 2, b, 6 + (w->player ? w->player - 1 : 0));
  text(172, 2,
       !w->valid             ? "LINK"
       : w->snapshot[0] == 2 ? "DONE"
       : w->snapshot[0] == 1 ? "REPLAY"
                             : "PLAN",
       5);
  if (w->snapshot[0] == 1)
    sprintf(b, "T%ld", (long)w->snapshot[2]);
  else if (left >= 0)
    sprintf(b, "~%lds", left);
  else
    strcpy(b, "NONE");
  text(222, 2, b, 15);
  if (w->snapshot[14] & 1) {
    sprintf(b, "TO%ld", (long)(w->snapshot[4] | w->snapshot[5] << 8));
    text(278, 2, b, 4);
  }
  for (i = 0; i < 4; ++i) {
    int x = 4 + i * 79;
    unsigned p = i + 1, pen = 6 + i;
    frame(x, 12, 75, 12, p == w->player ? pen : 2);
    text(x + 3, 14, ba_player_label(i < n ? p : 0), pen);
    if (i < n) {
      ba_score_text(ba_u32(w->players[i] + 10), score);
      text(x + 33, 14, score, score[0] == '+' ? 15 : 5);
      for (j = 0; j < 3; ++j) {
        int full = !(w->players[i][1] & 1) && j + w->players[i][5] < 3;
        rect(x + 51 + j * 6, 15, 4, 5, full ? pen : 2);
      }
    }
  }
  for (i = 1; i <= 3; ++i) {
    int x = 4 + (i - 1) * 105;
    frame(x, 26, 102, 12, w->snapshot[0] == 1 && w->snapshot[2] == i ? 5 : 3);
    sprintf(b, "T%ld", (long)i);
    text(x + 3, 28, b, 4);
    for (j = 0; j < n; ++j) {
      unsigned p = ba_priority(w->snapshot[1], i, n, j);
      sprintf(b, "%ld", (long)p);
      text(x + 23 + j * 15, 28, b, 5 + p);
    }
    text(x + 86, 28, "M", 15);
  }
  publish(0, 0, 320, 38);
}
static void inspect(const ba_world *w) {
  char b[48], exact[11];
  unsigned i, k = ba_terrain(w, ba_viewport.x, ba_viewport.y), line = 104;
  const uint8_t *a = ba_attribute(w, ba_viewport.x, ba_viewport.y), *h = 0;
  text(205, 43, "INSPECT", 15);
  sprintf(b, "X%ld Y%ld", (long)ba_viewport.x, (long)ba_viewport.y);
  text(205, 55, b, 4);
  text(205, 68, k < 14 ? terrain[k] : "UNKNOWN", 5);
  if (a) {
    if (k == 5 || k == 6) {
      char group[2] = {'A' + a[2], 0};
      sprintf(b, "GROUP %s", group);
    } else if (k == 2 || k == 3)
      sprintf(b, "OWNER P%ld", (long)a[2]);
    else
      sprintf(b, "ID %ld", (long)(a[2] | a[3] << 8));
    text(205, 80, b, 4);
    if (k == 5) {
      static const char *directions[] = {"NORTH", "EAST", "SOUTH", "WEST"};
      unsigned dir =
          ba_tile_image(w, ba_viewport.y * w->width + ba_viewport.x) - 14;
      sprintf(b, "MOVES %s", directions[dir & 3]);
      text(205, 92, b, 15);
    }
    if (k == 9 || k == 11)
      h = ba_hazard(w, k == 9 ? 1 : 2, a[2]);
  }
  if (h) {
    if (k == 9)
      strcpy(b, h[2] ? "ARMED" : "OFF");
    else
      sprintf(b, "CHARGE %ld/2", (long)h[2]);
    text(205, 92, b, 15);
    if (h[3])
      sprintf(b, "OWNER P%ld", (long)h[3]);
    else
      strcpy(b, "OWNER NONE");
    text(205, line, b, 5);
    line += 12;
  }
  for (i = 0; i < w->players_count; ++i) {
    const uint8_t *p = w->players[i];
    if (!(p[1] & 1) && p[2] == ba_viewport.x && p[3] == ba_viewport.y) {
      char heading[2] = {"NESW"[p[4] & 3], 0};
      sprintf(b, "P%ld %s", (long)i + 1, ba_player_label(i + 1));
      text(205, line, b, 6 + i);
      line += 12;
      ba_decimal(ba_u32(p + 10), exact);
      sprintf(b, "SCORE %s", exact);
      text(205, line, b, 5);
      line += 12;
      sprintf(b, "FACE %s DAMAGE %ld", heading, (long)p[5]);
      text(205, line, b, 4);
      line += 12;
      text(205, line, p[1] & 2 ? "CARRYING CRATE" : "EMPTY HANDED", 15);
      line += 12;
    }
  }
  for (i = 0; i < w->crates_count; ++i)
    if (!w->crates[i][6] && w->crates[i][4] == ba_viewport.x &&
        w->crates[i][5] == ba_viewport.y && line < 174) {
      text(205, line, "LOOSE CRATE", 15);
      line += 12;
      break;
    }
  if (ba_tarred(w, ba_viewport.x, ba_viewport.y) && line < 174)
    text(205, line, "TAR: SLOW ORDERS", 15);
  text(205, 185, "ARROWS MOVE", 4);
}
static void panel(const ba_world *w) {
  char b[80], exact[11], deliveries[11];
  unsigned i;
  static const unsigned icons[] = {43, 18, 4, 15, 6, 8, 40, 9, 13, 11, 12, 10};
  static const char *names[] = {"SPAWN",       "DOCK +5",      "DISPENSER",
                                "CONVEYOR",    "BELT CONTROL", "PIT DISABLES",
                                "FIXED LASER", "BURST TRAP",   "TRAP CONTROL",
                                "TURRET",      "TURRET PAD",   "TAR DRUM"};
  if (ba_viewport.panel == 1) {
    text(5, 42, "FIELD GUIDE / F1 OR ESC RETURNS", 15);
    for (i = 0; i < 12; ++i) {
      int x = 5 + (i % 2) * 158, y = 55 + (i / 2) * 23;
      ba_ui_icon(rp, icons[i], x, y);
      text(x + 22, y + 4, names[i], 5);
    }
  } else {
    text(5, 42, "EXACT SCORES / LOCAL DISPLAY LABELS", 15);
    for (i = 0; i < w->players_count; ++i) {
      int y = 58 + i * 34;
      ba_decimal(ba_u32(w->players[i] + 10), exact);
      sprintf(b, "P%ld  %s  SCORE %s", (long)i + 1, ba_player_label(i + 1),
              exact);
      text(10, y, b, 6 + i);
      ba_decimal(ba_u32(w->players[i] + 14), deliveries);
      sprintf(b, "DAMAGE %ld/3 DELIVERIES %s CARGO %s", (long)w->players[i][5],
              deliveries, w->players[i][1] & 2 ? "YES" : "NO");
      text(10, y + 10, b, 4);
      if (w->players[i][1] & 1)
        text(10, y + 20, "DISABLED / RESPAWN PENDING", 12);
    }
  }
}
static void order_row(unsigned i, const ba_editor *e) {
  char b[4];
  int y = 54 + i * 16;
  unsigned hot = i == ba_viewport.choice;
  rect(202, y, 112, 16, hot ? 2 : 1);
  frame(202, y, 112, 15, hot ? 5 : 2);
  sprintf(b, "%ld", (long)i + 1);
  text(205, y + 4, b, hot ? 15 : 4);
  ba_ui_icon(rp, BA_ORDER + i, 213, y);
  text(231, y + 4, orders[i], e->locked || e->pending ? 4 : 5);
}
static void side(const ba_world *w, const ba_editor *e) {
  unsigned i;
  char b[40];
  ba_view keyview = ba_viewport;
  uint32_t h;
  keyview.choice = 0;
  h = hash(1, &keyview, sizeof keyview);
  h = hash(h, &e->locked, 2);
  h = hash(h, w->snapshot, 3);
  for (i = 0; i < w->players_count; ++i) {
    const uint8_t *p = w->players[i];
    unsigned off = !ba_viewport.overview && !(p[1] & 1) &&
                   (p[2] < ba_viewport.cx || p[2] >= ba_viewport.cx + 12 ||
                    p[3] < ba_viewport.cy || p[3] >= ba_viewport.cy + 10);
    h = hash(h, &off, sizeof off);
  }
  h = hash(h, &w->winners, 1);
  if (ba_viewport.inspect || ba_viewport.panel == 2) {
    h = hash(h, w, sizeof *w);
  }
  if (!region(1, h, ba_viewport.panel ? 0 : 200, 40,
              ba_viewport.panel ? 320 : 116, 160)) {
    if (!ba_viewport.panel && !ba_viewport.inspect && w->snapshot[0] != 2 &&
        old_choice != ba_viewport.choice) {
      if (old_choice < 9) {
        order_row(old_choice, e);
        publish(202, 54 + old_choice * 16, 112, 16);
      }
      order_row(ba_viewport.choice, e);
      publish(202, 54 + ba_viewport.choice * 16, 112, 16);
      old_choice = ba_viewport.choice;
    }
    return;
  }
  if (ba_viewport.panel) {
    panel(w);
    publish(0, 40, 320, 160);
    return;
  }
  frame(200, 40, 116, 160, 3);
  if (ba_viewport.inspect)
    inspect(w);
  else if (w->valid && w->snapshot[0] == 2) {
    text(205, 45, "MATCH FINISHED", 15);
    text(205, 64, "WINNERS", 5);
    for (i = 0; i < w->players_count; ++i)
      if (w->winners & (1u << i)) {
        sprintf(b, "P%ld %s", (long)i + 1, ba_player_label(i + 1));
        text(205, 80 + i * 14, b, 6 + i);
      }
    text(205, 152, "F2: EXACT SCORES", 4);
    text(205, 180, "ESC: QUIT", 5);
  } else {
    text(205, 43, "COMMANDS", 15);
    text(261, 43, "OFF", 4);
    for (i = 0; i < w->players_count; ++i) {
      const uint8_t *p = w->players[i];
      if (!ba_viewport.overview && !(p[1] & 1) &&
          (p[2] < ba_viewport.cx || p[2] >= ba_viewport.cx + 12 ||
           p[3] < ba_viewport.cy || p[3] >= ba_viewport.cy + 10)) {
        char n[2] = {'1' + i, 0};
        text(282 + i * 7, 43, n, 6 + i);
      }
    }
    for (i = 0; i < 9; ++i)
      order_row(i, e);
    old_choice = ba_viewport.choice;
  }
  publish(200, 40, 116, 160);
}
static void programme(const ba_world *w, const ba_editor *e,
                      const char *status) {
  char b[40];
  unsigned i, ready = w->valid && w->player &&
                      (w->snapshot[22] & (1u << (w->player - 1)));
  uint32_t h = hash(1, e, sizeof *e);
  h = hash(h, w->snapshot, 23);
  h = hash(h, status, strlen(status));
  if (!region(2, h, 0, 204, 320, 24))
    return;
  for (i = 0; i < 3; ++i) {
    int x = 4 + i * 73;
    unsigned selected = i == e->slot;
    rect(x, 204, 69, 24, selected ? 2 : 1);
    frame(x, 204, 69, 24, selected ? 5 : 3);
    sprintf(b, "%ld", (long)i + 1);
    text(x + 3, 207, b, 15);
    ba_ui_icon(rp, BA_ORDER + e->orders[i], x + 9, 210);
    text(x + 27, 213, ba_order_name(e->orders[i]), 5);
  }
  rect(224, 204, 92, 24, (!e->locked && !e->pending) ? 13 : 1);
  frame(224, 204, 92, 24, 3);
  text(229, 207,
       e->pending                        ? "SENDING"
       : !w->valid                       ? "CONNECTING"
       : w->snapshot[0] == 2             ? "FINISHED"
       : w->snapshot[0] == 1             ? "REPLAY"
       : !strncmp(status, "Accepted", 8) ? "ACCEPTED"
       : e->locked                       ? (ready ? "LOCKED" : "NOT EDITABLE")
                                         : "ENTER: LOCK",
       5);
  text(229, 217,
       e->pending            ? "PLEASE WAIT"
       : w->snapshot[0] == 1 ? "SERVER EVENTS"
                             : "3 ORDERS",
       15);
  publish(0, 204, 320, 24);
}
void ba_ui_draw(struct RastPort *canvas, struct RastPort *screen,
                const ba_world *w, const ba_editor *e, const char *status) {
  char lines[3][54];
  unsigned i;
  uint32_t h;
  copies = 0;
  memset(lines, 0, sizeof lines);
  WaitBlit();
  rp = canvas;
  target = screen;
  top(w);
  side(w, e);
  programme(w, e, status);
  if (ba_viewport.panel == 1) {
    strcpy(lines[0], "3 PIPS = DURABILITY. GOLD BOX = NEUTRAL CRATE.");
    strcpy(lines[1], "PICKUP/OWN-DOCK DELIVERY ARE AUTOMATIC.");
    strcpy(lines[2], "F1 / ESC RETURN. SERVER CLOCK CONTINUES.");
  } else if (ba_viewport.panel == 2) {
    strcpy(lines[0], "++ MEANS SCORE ABOVE 99; EXACT VALUES ABOVE.");
    strcpy(lines[1], "PLY1-PLY4 ARE LOCAL LABELS, NOT BOT NAMES.");
    strcpy(lines[2], "F2 / ESC RETURN. SERVER CLOCK CONTINUES.");
  } else if (ba_viewport.inspect) {
    strcpy(lines[0], "INSPECTION: OUTLINE SHOWS LINKS / HAZARD RANGE.");
    strcpy(lines[1], "ARROWS MOVE CURSOR / PAN. NO OUTCOME PREDICTION.");
    strcpy(lines[2], "I / ESC RETURN   V MAP   F2 EXACT SCORES");
  } else {
    strncpy(lines[0],
            (!w->valid || e->pending || e->locked)
                ? status
                : explain[ba_viewport.choice],
            53);
    lines[0][53] = 0;
    strcpy(lines[1], "UP/DN CHOOSE SPACE SET TAB SLOT ENTER LOCK");
    strcpy(lines[2], "V MAP I INSPECT F1 GUIDE F2 SCORES ESC QUIT");
  }
  h = hash(1, lines, sizeof lines);
  if (region(3, h, 0, 230, 320, 26)) {
    for (i = 0; i < 3; ++i)
      text(4, 231 + i * 8, lines[i], i ? 4 : 5);
    publish(0, 230, 320, 26);
  }
  h = hash(1, &ba_viewport, sizeof ba_viewport);
  if (region(4, h, 0, 200, 320, 4)) {
    if (w->height > 10 && ba_viewport.cy + 10 < w->height) {
      rect(96, 200, 5, 1, 15);
      rect(97, 201, 3, 1, 15);
      rect(98, 202, 1, 1, 15);
    }
    publish(0, 200, 320, 4);
  }
  WaitBlit();
  for (i = 0; i < copies; ++i) {
    int *r = copy_rects[i];
    BltBitMapRastPort(rp->BitMap, r[0], r[1], target, r[0], r[1], r[2], r[3],
                      0xc0);
  }
}
