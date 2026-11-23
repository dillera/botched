/* Runs unchanged production codec on sim65's NMOS 6502. Generated fixtures are
 * read-only benchmark input, not a second production receive buffer. */
#include "botched/client/nio.h"
#include <stdio.h>
#include <string.h>

#include "corpus.h"
#include "mirror.h"
#ifdef __CC65__
extern void b5_mark(void);
extern unsigned b5_used(void);
extern unsigned b5_hw_used(void);
#else
#define b5_mark() ((void)0)
#define b5_used() 0u
#define b5_hw_used() 0u
#endif
static bn_client client;
static uint8_t caller[96 + 65 + 17 + 225 + 4];
static unsigned i, j, k, used, hw, max_used, max_hw, ok;
static size_t written;
static int apply;
static uint32_t start, elapsed;
static uint32_t tick(void) {
#ifdef __CC65__
    *(volatile uint8_t *)0xffc0 = 0;
    return *(volatile uint32_t *)0xffc2;
#else
    return 0; /* host run is semantic/allocation acceptance, not a cycle measurement */
#endif
}
static void report(const char *name) {
    used = b5_used(); hw = b5_hw_used();
    if (used > max_used) max_used = used;
    if (hw > max_hw) max_hw = hw;
    printf("%s cycles=%lu software=%u hardware=%u ok=%u\n", name, (unsigned long)elapsed, used, hw, ok);
}
int main(void) {
#ifdef __CC65__
    *(volatile uint8_t *)0xffc1 = 0;
#endif
    printf("sizes client=%u stream=%u world=%u caller=%u reader=%u writer=%u\n",
           (unsigned)sizeof client, (unsigned)sizeof(bw_stream), (unsigned)sizeof world, (unsigned)sizeof caller,
           (unsigned)sizeof(bw_reader), (unsigned)sizeof(bw_writer));
    b5_mark(); start = tick(); elapsed = tick() - start; ok = 1; report("measurement-empty");
    for (i = 0; i < FIXTURE_COUNT; ++i) {
        b5_mark(); start = tick();
        ok = fixtures[i].kind ? bw_validate_packet(fixtures[i].p, fixtures[i].n, 240)
                              : bw_validate_record(fixtures[i].p, fixtures[i].n);
        elapsed = tick() - start;
        if (!ok) return 1;
        report(fixtures[i].name);
        for (j = 0; j < fixtures[i].n; ++j) {
            if (fixtures[i].kind ? bw_validate_packet(fixtures[i].p, j, 240)
                                 : bw_validate_record(fixtures[i].p, j)) return 7;
        }
        if (fixtures[i].kind) {
            if (!bw_encode_packet(fixtures[i].p[0], (uint16_t)fixtures[i].p[1] | ((uint16_t)fixtures[i].p[2]<<8),
                 fixtures[i].p+3, fixtures[i].n-3, client.reply, sizeof client.reply, &written)) return 8;
        } else if (!bw_encode_record(fixtures[i].p[0], fixtures[i].p+2, fixtures[i].n-2,
                                    client.reply, sizeof client.reply, &written)) return 9;
        if (written != fixtures[i].n || memcmp(client.reply, fixtures[i].p, written)) return 10;
        used = b5_used(); hw = b5_hw_used();
        if (used > max_used) max_used = used;
        if (hw > max_hw) max_hw = hw;
    }
    memcpy(client.reply, full_page, 240);
    client.reply[240] = 0;
    if (bw_validate_packet(client.reply, 241, 240) || bw_validate_packet(full_page, 240, 128)) return 12;
    b5_mark(); start = tick();
    ok = bw_encode_packet(0x14, 1, submit + 3, sizeof submit - 3, caller, 96, &written);
    elapsed = tick() - start;
    if (!ok || written != sizeof submit || memcmp(caller, submit, written)) return 2;
    report("SUBMIT-encode");
    b5_mark(); start = tick();
    ok = bw_encode_packet(0x82, 1, full_page + 3, 237, client.reply, sizeof client.reply, &written);
    elapsed = tick() - start;
    if (!ok || written != 240 || memcmp(client.reply, full_page, written)) return 11;
    report("PAGE-full-240-encode");
    for (k = 0; k < 2; ++k) {
        bw_stream_init(&client.stream, 0);
        memset(&world, 0, sizeof world);
        world.crate_base = TILE_BYTES;
        world.group_base = TILE_BYTES + CRATES * 7;
        world.hazard_base = world.group_base + GROUPS * 2;
        world.attribute_base = world.hazard_base + HAZARDS * 4;
        b5_mark(); start = tick();
        ok = 1;
        for (j = 0; j < PAGE_COUNT; ++j) {
            memcpy(client.reply + 2, pages[j].p, pages[j].n);
            /* B4 complete() validates once before stream_accept; a consumer's
             * visit_page validates again. Count that real three-pass path. */
            if (k && !bw_validate_packet(client.reply + 2, pages[j].n, 240)) { ok = 0; break; }
            if (!bw_stream_accept(&client.stream, client.reply + 2, pages[j].n, &apply)) { ok = 0; break; }
            if (k && apply && !bw_visit_page(client.reply + 2, pages[j].n, b5_store, 0)) { ok = 0; break; }
        }
        elapsed = tick() - start;
        if (!ok || !client.stream.complete || world.failed) return 3;
        if (k && (world.crates != CRATES || world.groups != GROUPS || world.hazards != HAZARDS)) return 4;
        report(k ? "snapshot-validate-and-store" : "snapshot-validate");
    }
    world.crates = world.groups = world.hazards = 0;
    b5_mark(); start = tick();
    for (j = 0; j < PAGE_COUNT; ++j) {
        for (i = 22; i < pages[j].n; i += pages[j].p[i+1] + 2)
            b5_store(0, pages[j].p + i, pages[j].p[i+1] + 2);
    }
    elapsed = tick() - start;
    if (world.failed || world.crates != CRATES) return 6;
    report("snapshot-store-only-prevalidated");
    /* Exact copy check, including every reserved crate slot and restored u32 IDs. */
    for (j = 0; j < CRATES; ++j) {
        const uint8_t *p = world.variable + TILE_BYTES + j * 7;
        if (p[0] != (uint8_t)(70000UL+j) || p[1] != (uint8_t)((70000UL+j)>>8) || p[2] != 1) return 5;
    }
    printf("peak software=%u hardware=%u\n", max_used, max_hw);
    return 0;
}
