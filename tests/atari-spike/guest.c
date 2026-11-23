/* Headless transport/decoder exercise only. No graphics, keyboard or game rules. */
#include "stream.h"
#include "endpoint.h"
#include <stdint.h>
volatile uint8_t guest_stage,guest_round,guest_error;
volatile uint16_t guest_pages,guest_events,guest_sw,guest_hw,guest_marks[7];
static uint16_t now(void) { return ((uint16_t)*(volatile uint8_t*)0x13<<8) | *(volatile uint8_t*)0x14; }
extern void b5_mark(void);
extern unsigned b5_used(void),b5_hw_used(void);
static const uint8_t orders[3]={1,7,8};
static const uint8_t *body;
static uint8_t len,tag;
static int8_t rc;
static uint8_t response(void) {
    if(!st_begin()) return 0;
    do { rc=st_step(); } while(!rc);
    if(rc<0) return 0;
    ++guest_pages;
    while((tag=th_next(&body,&len))) if(tag==21) ++guest_events;
    return !th.error;
}
static uint8_t transfer(void) {
    if(!response())return 0;
    while(th.rx[0]==0x82 && !th.finished) { if(!th_fetch() || !response()) return 0; }
    return 1;
}
int main(void) {
    guest_stage=1;th_reset();b5_mark();guest_marks[0]=now();
    if(!st_open(SPIKE_URL)) goto error;
    guest_stage=2;if(!th_hello() || !response())goto error;
    guest_stage=3;if(!th_inspect_map(SPIKE_MATCH) || !transfer() || !th_w.map_valid)goto error;
    guest_stage=4;if(!th_join(SPIKE_MATCH) || !transfer())goto error;
    guest_stage=5;if(!th_query(0x15) || !transfer() || !th_w.valid)goto error;
    guest_marks[1]=now();
    for(guest_round=1;guest_round<=3;++guest_round) {
        guest_stage=6;if(!th_submit(guest_round,1,orders) || !transfer())goto error;
        guest_stage=7;if(!th_query(0x16) || !transfer())goto error;
        guest_stage=8;if(!th_query(0x15) || !transfer() || !th_w.valid)goto error;
        guest_marks[guest_round+1]=now();
    }
    if(th_w.snapshot[0]!=2)goto error;
    guest_marks[5]=now();
    if(!st_close())goto error;
    guest_marks[6]=now();
    guest_sw=b5_used();guest_hw=b5_hw_used();
    guest_stage=128;
    for(;;){}
error:
    guest_error=th.error?th.error:st_io.status;
    guest_stage|=64;
    for(;;){}
}
