/* sim65 measured decoder path: already-framed pages, no transport wait time.
 * Seed mirror is built outside timing. Frame copies into the page are included. */
#include "thin.h"
#include <stdio.h>
#include <string.h>
#include "corpus.h"
#ifdef BASELINE
#include "botched/wire/v1.h"
static bw_stream reference;
static int apply;
static void discard(void *c,const uint8_t *p,size_t n) { (void)c;(void)p;(void)n; }
#endif
extern void b5_mark(void);
extern unsigned b5_used(void),b5_hw_used(void);
static uint32_t begin,cycles,checksum;
static unsigned i,sw,hw;
static uint8_t tag,len,ok;
static const uint8_t *body;
static uint32_t tick(void) { *(volatile uint8_t*)0xffc0=0;return *(volatile uint32_t*)0xffc2; }
static uint8_t play(const struct packet *p) {
    memcpy(th.rx,p->data,p->n);
#ifdef BASELINE
    /* Reference lower bound: three B4 validation passes, no world/event update.
     * Reset at the start of a standalone benchmark logical response. */
    if(!th_u32(p->data+7)) bw_stream_init(&reference,reference.ack_version);
    return bw_validate_packet(th.rx,p->n,240) && bw_stream_accept(&reference,th.rx,p->n,&apply) && bw_visit_page(th.rx,p->n,discard,0);
#else
    th.exchange=(uint16_t)p->data[1]|((uint16_t)p->data[2]<<8);
    if(!th_accept(p->n))return 0;
    while((tag=th_next(&body,&len))) { (void)tag; }
    return !th.error;
#endif
}
int main(void) {
    *(volatile uint8_t*)0xffc1=0;
    th_reset();
    for(i=0;i<SEED_COUNT;++i)if(!play(seed+i))return 1;
    b5_mark();begin=tick();
    for(i=0;i<INPUT_COUNT;++i)if(!play(input+i))return 2;
    cycles=tick()-begin;sw=b5_used();hw=b5_hw_used();
    /* Checksum covers every mirror byte, not just counts; compare C and asm builds. */
    for(i=0;i<sizeof th_w;++i)checksum=checksum*33+((uint8_t*)&th_w)[i];
    printf("%s cycles=%lu software=%u hardware=%u world=%u client=%u checksum=%lu\n",CASE_NAME,(unsigned long)cycles,sw,hw,(unsigned)sizeof th_w,(unsigned)sizeof th,(unsigned long)checksum);
    /* Specialized request encoder keeps the frozen B1 21-byte numeric submission. */
    th.active=0;th.exchange=1;th.next_key=7;
    b5_mark();begin=tick();
    ok=th_submit(3,1,orders);
    cycles=tick()-begin;sw=b5_used();hw=b5_hw_used();
    if(!ok || th.tx_size!=23 || memcmp(th.tx+2,submit,21))return 3;
    printf("submit cycles=%lu software=%u hardware=%u\n",(unsigned long)cycles,sw,hw);
    return 0;
}
