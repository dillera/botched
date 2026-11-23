#ifndef BOTCHED_AMIGA_ASSETS_H
#define BOTCHED_AMIGA_ASSETS_H
#include <stdint.h>
#ifdef __cplusplus
extern "C" {
#endif
#define BA_TILE_W 16
#define BA_TILE_H 16
#define BA_ASSETS 56
#define BA_ROBOT 22
#define BA_CRATE 38
extern const uint8_t ba_glyphs[96][8];
#define BA_SPAWN 43
#define BA_ORDER 47
extern const uint16_t ba_palette[16];
/* Original procedural pixel art, expanded once into a Chip atlas at startup.
 * 0..13 terrain; 14..17 belts; 18..21 docks; 22..37 robots; 38 crate;
 * 39..42 directional emitters; 43..46 spawns; 47..55 order icons. Sprite pen
 * zero is transparent. */
unsigned ba_asset_pixel(unsigned image, unsigned x, unsigned y);
#ifdef __cplusplus
}
#endif
#endif
