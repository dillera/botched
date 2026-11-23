/* Acceptance storage model, not a gameplay client. All authoritative snapshot/map
 * fields retained as wire bytes; display strings streamed. No native alignment
 * assumptions, allocator, second snapshot, tile lookup index or event history. */
#ifndef B5_MIRROR_H
#define B5_MIRROR_H
#include <stdint.h>
#include <string.h>
#ifndef B5_VARIABLE
#define B5_VARIABLE 911
#endif
struct b5_world {
    uint8_t map_meta[43], snapshot[31], status[26], players[108];
    uint8_t variable[B5_VARIABLE];
    uint16_t crates, groups, hazards, attributes;
    uint16_t crate_base, group_base, hazard_base, attribute_base;
    uint8_t failed;
};
static struct b5_world world;
/* Capacity limits/offsets set from immutable map census before admission. */
static void b5_store(void *ctx, const uint8_t *record, size_t n) {
    const uint8_t *b = record + 2;
    uint16_t pos = 0, end = 0;
    (void)ctx;
    switch (record[0]) {
    case 16: memcpy(world.map_meta, b, n - 2); return;
    case 9: memcpy(world.snapshot, b, n - 2); return;
    case 14: memcpy(world.status, b, n - 2); return;
    case 10: memcpy(world.players + (b[0] - 1) * 27, b, 27); return;
    case 11: pos = world.crate_base + world.crates++ * 7; end = world.group_base; break;
    case 12: pos = world.group_base + world.groups++ * 2; end = world.hazard_base; break;
    case 13: pos = world.hazard_base + world.hazards++ * 4; end = world.attribute_base; break;
    case 18: pos = world.attribute_base + world.attributes++ * 5; end = B5_VARIABLE; break;
    case 17:
        pos = ((uint16_t)b[0] | ((uint16_t)b[1] << 8)) / 2;
        n = (b[2] + 1) / 2 + 2;
        b += 3;
        end = world.crate_base;
        break;
    default: return; /* context lives in caller storage, text is streamed */
    }
    if (pos > end || n - 2 > (size_t)(end - pos)) { world.failed = 1; return; }
    memcpy(world.variable + pos, b, n - 2);
}
#endif
