#include "assets.h"
#include "assets_data.h"
const uint16_t ba_palette[16] = {0x112, 0x223, 0x345, 0x567, 0x899, 0xeec,
                                 0xf76, 0x6cf, 0x8d7, 0xc9f, 0xea4, 0x963,
                                 0xf43, 0x488, 0xbbb, 0xfd8};
unsigned ba_asset_pixel(unsigned k, unsigned x, unsigned y) {
  unsigned p;
  if (k >= BA_ASSETS || x >= 16 || y >= 16)
    return 0;
  p = y * 16 + x;
  return (pixels[k][p / 2] >> ((p & 1) * 4)) & 15;
}
