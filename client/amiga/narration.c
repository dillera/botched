#include "narration.h"
#include "play.h"
#include <string.h>
ba_narration ba_story;
static void add(char *s, const char *v) {
  unsigned n = strlen(s);
  while (*v && n < 52)
    s[n++] = *v++;
  s[n] = 0;
}
static void number(char *s, uint32_t v) {
  char b[11];
  unsigned n = 0;
  do {
    b[n++] = '0' + v % 10;
    v /= 10;
  } while (v);
  while (n) {
    char c[2] = {b[--n], 0};
    add(s, c);
  }
}
static void player(char *s, unsigned p) {
  add(s, p ? "P" : "NEUTRAL");
  if (p)
    number(s, p);
}
static void xy(char *s, unsigned x, unsigned y) {
  if (x > 127) {
    add(s, "-");
    number(s, 256 - x);
  } else
    number(s, x);
  add(s, ",");
  if (y > 127) {
    add(s, "-");
    number(s, 256 - y);
  } else
    number(s, y);
}
static ba_caption *entry(ba_narration *s, unsigned event) {
  ba_caption *c = &s->history[s->head];
  memset(c, 0, sizeof *c);
  c->round = s->round;
  c->tick = s->tick;
  c->event = event;
  s->head = (s->head + 1) % 16;
  if (s->count < 16)
    ++s->count;
  if (s->scroll && s->scroll < 15)
    ++s->scroll;
  ++s->serial;
  return c;
}
const ba_caption *ba_caption_at(const ba_narration *s, unsigned age) {
  return age < s->count ? &s->history[(s->head + 15 - age) % 16] : 0;
}
void ba_narration_refresh(ba_narration *s) {
  ba_caption *c;
  s->phase = s->actor = s->tick = 0;
  s->refreshed = 1;
  memset(s->done, 0, 3);
  memset(s->skipped, 0, 3);
  s->machinery = 0;
  c = entry(s, 0);
  add(c->line[0], "STATE REFRESHED");
  add(c->line[1], "AUTHORITATIVE SNAPSHOT; NO REPLAY INFERRED.");
}
void ba_narrate(ba_narration *s, const uint8_t *r, unsigned n) {
  const uint8_t *p;
  unsigned e, t;
  ba_caption *c;
  char *a, *b;
  static const char *heading[] = {"N", "E", "S", "W"};
  static const char *noop[] = {"NO CARGO", "TILE OCCUPIED", "NO CONTROL",
                               "TAR"};
  if (!r || n < 13 || !bw_validate_record(r, n) || r[0] != 21)
    return;
  s->refreshed = 0;
  e = r[2];
  p = r + 13;
  t = r[8];
  if (s->round != r[7]) {
    memset(s->done, 0, 3);
    memset(s->skipped, 0, 3);
    s->machinery = 0;
    s->actor = 0;
    s->phase = 0;
    s->tick = 0;
  }
  if (s->tick && (s->tick != t || s->round != r[7])) {
    if (s->phase == 1 && s->actor)
      s->done[s->tick - 1] |= 1u << (s->actor - 1);
    if (s->phase == 2)
      s->machinery |= 1u << (s->tick - 1);
    s->phase = s->actor = 0;
  }
  s->round = r[7];
  s->tick = t;
  if (e >= 5 && e <= 7) {
    if (s->actor && t && (s->phase == 1 || s->phase == 3))
      s->done[t - 1] |= 1u << (s->actor - 1);
    s->actor = p[0];
    s->order = p[1];
    s->phase = 1;
    if (e == 7 && t)
      s->skipped[t - 1] |= 1u << (p[0] - 1);
  }
  if ((e == 8 && p[5] == 1) || (e == 10 && p[4] == 1) || e == 16 || e == 19 ||
      e == 24 || e == 25 || e == 26 || e == 27 || e == 28 ||
      (e == 20 && p[1] != 0)) {
    if (s->actor && t)
      s->done[t - 1] |= 1u << (s->actor - 1);
    s->phase = 2;
  }
  /* PUSH lacks cause on v1. Do not guess ORDER vs BELT across page boundaries.
   */
  if (e == 9 && s->phase != 2) {
    s->phase = 3;
    if (s->actor != p[1])
      s->order = 255;
    s->actor = p[1];
  }
  if (e == 8 && p[5] == 0) {
    s->phase = 1;
    s->actor = p[0];
  }
  if ((e == 3 || e == 12 || e == 21 || e == 22) && t && s->phase == 2)
    s->machinery |= 1u << (t - 1);
  if (e <= 4 || e == 21 || e == 22 || e == 12)
    s->phase = 0;
  c = entry(s, e);
  a = c->line[0];
  b = c->line[1];
  switch (e) {
  case 1:
    add(a, "PLANNING CLOSED");
    add(b, "REPLAYING SERVER RESULTS; CLOCK CONTINUES.");
    break;
  case 2:
    add(a, "ROUND ");
    number(a, p[0]);
    add(a, " STARTED");
    add(b, "PRIORITY ROTATES. SERVER CLOCK CONTINUES.");
    break;
  case 3:
    add(a, "ROUND COMPLETE");
    add(b, "ALL THREE TICKS RESOLVED.");
    break;
  case 4:
    add(a, "MATCH COMPLETE");
    add(b, "FINAL SCORES ARE AUTHORITATIVE.");
    break;
  case 5:
  case 6:
  case 7:
    player(a, p[0]);
    add(a, " > ");
    add(a, ba_order_name(p[1]));
    add(b, e == 5   ? "ORDER EXECUTED"
           : e == 7 ? "SKIPPED: ROBOT DISABLED"
                    : "NO EFFECT: ");
    if (e == 6)
      add(b, p[2] < 4 ? noop[p[2]] : "SERVER REASON");
    break;
  case 8:
    player(a, p[0]);
    add(a, p[5] ? " > BELT MOVE" : " > MOVE");
    xy(b, p[1], p[2]);
    add(b, " TO ");
    xy(b, p[3], p[4]);
    break;
  case 9:
    player(a, p[1]);
    add(a, " > PUSH ");
    player(a, p[0]);
    xy(b, p[2], p[3]);
    add(b, " TO ");
    xy(b, p[4], p[5]);
    add(b, "; FACING KEPT");
    break;
  case 10:
    player(a, p[0]);
    add(a, p[4] ? " > BELT BLOCKED" : " > MOVE BLOCKED");
    add(b, "OBSTRUCTION AT ");
    xy(b, p[2], p[3]);
    break;
  case 11:
    player(a, p[0]);
    add(a, " > TURN");
    add(b, heading[p[1]]);
    add(b, " TO ");
    add(b, heading[p[2]]);
    break;
  case 12:
    add(a, "DISPENSER > CRATE ");
    number(a, ba_u32(p));
    add(b, "SPAWN AT ");
    xy(b, p[6], p[7]);
    break;
  case 13:
  case 14:
  case 15:
    player(a, p[0]);
    add(a, e == 13 ? " > PICK UP" : e == 14 ? " > DROP" : " > DELIVER +5");
    add(b, "CRATE ");
    number(b, ba_u32(p + 1));
    if (e == 15) {
      add(b, "; SCORE ");
      number(b, ba_u32(p + 7));
    } else {
      add(b, " AT ");
      xy(b, p[5], p[6]);
    }
    break;
  case 16:
    add(a, "BELT > CRATE ");
    number(a, ba_u32(p));
    xy(b, p[4], p[5]);
    add(b, " TO ");
    xy(b, p[6], p[7]);
    break;
  case 17:
    add(a, "CRATE ");
    number(a, ba_u32(p));
    add(a, " > DESTROYED");
    add(b, p[6] ? "PIT AT " : "DROP BLOCKED AT ");
    xy(b, p[4], p[5]);
    break;
  case 18:
    player(a, p[0]);
    add(a, " > TOGGLE BELTS");
    add(b, "GROUP ");
    {
      char g[2] = {'A' + p[1], 0};
      add(b, g);
    }
    add(b, p[4] ? " REVERSED" : " NORMAL");
    break;
  case 19:
    add(a, "LASER ");
    xy(a, p[1], p[2]);
    add(a, " > ");
    player(a, p[0]);
    add(b, "DURABILITY ");
    number(b, 3 - p[3]);
    add(b, "/3");
    break;
  case 20:
    player(a, p[0]);
    add(a, " > DISABLED");
    add(b, p[1] == 0   ? "PIT"
           : p[1] == 1 ? "LASER"
           : p[1] == 2 ? "TRAP"
                       : "TURRET");
    add(b, "; RESPAWN AT ROUND END");
    break;
  case 21:
    player(a, p[0]);
    add(a, " > RESPAWN REDIRECTED");
    add(b, "DESTINATION ");
    xy(b, p[3], p[4]);
    break;
  case 22:
    player(a, p[0]);
    add(a, " > RESPAWN");
    add(b, "AT ");
    xy(b, p[1], p[2]);
    add(b, "; DURABILITY 3/3");
    break;
  case 23:
    player(a, p[0]);
    add(a, " > ARM TRAP ");
    number(a, p[1]);
    add(b, "OWNER ");
    player(b, p[6]);
    add(b, "; AREA ACTIVE");
    break;
  case 24:
    add(a, "TRAP ");
    number(a, p[0]);
    add(a, " > BURST");
    add(b, "OWNER ");
    player(b, p[3]);
    add(b, "; NOW DISARMED");
    break;
  case 25:
    add(a, "TRAP ");
    number(a, p[0]);
    add(a, " > HIT ");
    player(a, p[1]);
    add(b, "DURABILITY ");
    number(b, 3 - p[2]);
    add(b, "/3; OWNER ");
    player(b, p[3]);
    break;
  case 26:
    player(a, p[0]);
    add(a, " > CLAIM TURRET ");
    number(a, p[1]);
    add(b, "OWNER ");
    player(b, p[5]);
    break;
  case 27:
    add(a, "TURRET ");
    number(a, p[0]);
    add(a, " > CHARGE ");
    number(a, p[3]);
    add(a, "/2");
    add(b, "OWNER ");
    player(b, p[4]);
    break;
  case 28:
    add(a, "TURRET ");
    number(a, p[0]);
    add(a, " > FIRE AT ");
    player(a, p[4]);
    add(b, "DURABILITY ");
    number(b, 3 - p[7]);
    add(b, "/3; OWNER ");
    player(b, p[3]);
    break;
  case 29:
    player(a, p[0]);
    add(a, " > TAR: ");
    add(a, ba_order_name(p[1]));
    add(b, "MOVEMENT RESTRICTED BY TAR");
    break;
  case 30:
    player(a, p[0]);
    add(a, " > +");
    number(a, p[1]);
    {
      static const char *reasons[] = {" TRAP HIT", " TRAP DISABLE",
                                      " TURRET HIT", " TURRET DISABLE",
                                      " SHUNT"};
      add(a, reasons[p[2]]);
    }
    add(b, "VICTIM ");
    player(b, p[3]);
    add(b, "; SCORE ");
    number(b, ba_u32(p + 4));
    break;
  }
}
