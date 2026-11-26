/* Link-only footprint probe, NOT a platform gameplay client. Never talks to NIO
 * in acceptance: it retains the exact production transport/library dependency
 * graph so the linker accounts for all code and writable library storage. */
#include "botched/client/nio_fujinet.h"
#include "fujinet-nio.h"
#include "mirror.h"
bn_client b5_client;
uint8_t b5_caller[96 + 65 + 17 + 225 + 4];
int main(void) {
    if (fn_init() != FN_OK) return 1;
    bn_init(&b5_client, &bn_fujinet_ops, 0, (const char *)b5_caller + 178, 240, 2, 0, 1000);
    bn_step(&b5_client, 1);
    bw_visit_page(b5_client.reply + 2, b5_client.reply_size, b5_store, 0);
    bn_dispose(&b5_client);
    fn_shutdown();
    return world.failed;
}
