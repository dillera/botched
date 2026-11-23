#include "assets.h"
const uint16_t ba_palette[16] = {0x112, 0xeed, 0x234, 0x456, 0xf65, 0x5de,
                                 0x9d6, 0xfd6, 0x789, 0xa75, 0xdb9, 0xfff,
                                 0xf34, 0xa7c, 0x324, 0x122};
unsigned ba_asset_pixel(unsigned k, unsigned x, unsigned y) {
  unsigned c = (x == 15 || y == 11) ? 0 : 2, d, u, v;
  if (k >= BA_ROBOT && k < BA_CRATE) {
    unsigned owner = 4 + (k - BA_ROBOT) / 4;
    d = (k - BA_ROBOT) % 4;
    if (x < 2 || x > 13 || y < 1 || y > 10)
      return 0;
    if (x == 2 || x == 13)
      return (y & 1) ? 8 : 3; /* tracks */
    if (y == 10)
      return 3;
    if (x == 3 || x == 12 || y == 1)
      return 1;
    if ((d == 0 && y == 2 && x >= 6 && x <= 9) ||
        (d == 2 && y == 8 && x >= 6 && x <= 9) ||
        (d == 1 && x == 11 && y >= 4 && y <= 6) ||
        (d == 3 && x == 4 && y >= 4 && y <= 6))
      return 11;
    if (x >= 6 && x <= 9 && y >= 4 && y <= 6)
      return 0; /* visor */
    return owner;
  }
  if (k == BA_CRATE) {
    if (x < 4 || x > 11 || y < 2 || y > 9)
      return 0;
    if (y == 2 || x == 4)
      return 10;
    if (y == 9 || x == 11)
      return 9;
    if (x == 5 || x == 10 || y == 3 || y == 8 || x + y == 12)
      return 9;
    return 7;
  }
  if (k == 1) { /* bolted raised steel */
    if (y == 0 || x == 0)
      return 8;
    if (y >= 9 || x >= 14)
      return 0;
    if ((x == 2 || x == 12) && (y == 2 || y == 7))
      return 10;
    return y == 1 ? 3 : 2;
  }
  if (k == 8) { /* recessed shaft */
    if (x == 1 || y == 1)
      return 3;
    if (x >= 3 && x <= 12 && y >= 3 && y <= 9)
      return 0;
    return ((x + y) % 4 == 0) ? 9 : 15;
  }
  if (k >= 14 && k <= 17) {
    d = k - 14;
    u = x;
    v = y;
    if (d == 1 || d == 3) {
      u = y + 2;
      v = x * 3 / 4;
    }
    if (d == 2 || d == 1)
      v = 11 - v;
    if (x == 0 || x == 14 || y == 0 || y == 10)
      return 8;
    if ((v == 3 && u >= 6 && u <= 9) || (v == 4 && (u == 5 || u == 10)) ||
        (v == 5 && (u == 4 || u == 11)) || (v == 7 && u >= 7 && u <= 8))
      return 10;
    return v % 3 == 0 ? 3 : 15;
  }
  if (k >= 18 && k <= 21) {
    if (x == 1 || x == 13 || y == 1 || y == 9)
      return 4 + k - 18;
    if (x >= 4 && x <= 10 && y >= 4 && y <= 7)
      return (x & 1) ? 9 : 10;
    return c;
  }
  if (k >= 39 && k <= 42) {
    if (x >= 3 && x <= 12 && y >= 2 && y <= 9) {
      d = k - 39;
      if ((d == 0 && y < 4) || (d == 2 && y > 7) || (d == 1 && x > 10) ||
          (d == 3 && x < 5))
        return 12;
      return ((x + y) & 1) ? 3 : 8;
    }
    return c;
  }
  switch (k) {
  case 0:
    if ((x == 2 || x == 12) && y == 2)
      c = 3;
    break;
  case 2:
    if ((x == 3 || x == 11) && y >= 3 && y <= 8)
      c = 5;
    if ((y == 3 || y == 8) && x >= 3 && x <= 11)
      c = 5;
    break;
  case 3:
    return ba_asset_pixel(18, x, y);
  case 4: /* dispenser hopper and yellow chute */
    if (x >= 2 && x <= 12 && y >= 1 && y <= 8)
      c = (y == 1 || x == 2) ? 8 : 3;
    if (x >= 5 && x <= 10 && y >= 3 && y <= 6)
      c = 0;
    if (y == 9 && x >= 4 && x <= 11)
      c = (x & 1) ? 7 : 9;
    break;
  case 5:
    return ba_asset_pixel(14, x, y);
  case 6:
  case 13: /* consoles with coloured switches */
    if (x >= 3 && x <= 11 && y >= 2 && y <= 9)
      c = y == 2 ? 8 : 3;
    if (x >= 5 && x <= 9 && y >= 3 && y <= 5)
      c = k == 6 ? 5 : 12;
    if (y == 7 && (x == 5 || x == 8))
      c = 7;
    break;
  case 7:
    return ba_asset_pixel(39, x, y);
  case 9: /* trap, crossed jaws */
    if (x >= 2 && x <= 12 && y >= 2 && y <= 9)
      c = 15;
    if ((x + y == 12 || x == y + 2) && y >= 3 && y <= 8)
      c = 12;
    if (y == 2 || y == 9)
      c = (x % 3 == 0) ? 9 : c;
    break;
  case 10: /* tar generator: violet bubbling tank */
    if (x >= 3 && x <= 11 && y >= 2 && y <= 9)
      c = (x == 3 || y == 2) ? 8 : 14;
    if ((x == 5 && y == 4) || (x == 9 && y == 7))
      c = 13;
    break;
  case 11: /* turret spindle/barrel */
    if (x >= 3 && x <= 12 && y >= 4 && y <= 9)
      c = 3;
    if (x >= 5 && x <= 10 && y >= 3 && y <= 8)
      c = 8;
    if (x >= 7 && x <= 8 && y >= 1 && y <= 6)
      c = 10;
    break;
  case 12: /* control ring */
    if ((y == 2 || y == 9) && x >= 4 && x <= 11)
      c = 7;
    if ((x == 3 || x == 12) && y >= 3 && y <= 8)
      c = 7;
    if (x == 7 && y == 5)
      c = 8;
    break;
  }
  return c;
}
