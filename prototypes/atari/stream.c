#include "stream.h"
#include <string.h>
fn_stream_io_t st_io;
static uint8_t phase,sent,received,wanted,prefix[2],prefix_count,retry,loaded;
uint8_t st_open(const char *url) {
    uint8_t n=0,rc;
    if(phase) return 0;
    while(n<121 && url[n]) ++n;
    if(n<7 || n>120 || memcmp(url,"tcp://",6)) return 0;
    phase=0;
    if(fn_stream_init(&st_io)) return 0;
    st_io.length=n;memcpy(st_io.data,url,n);
    rc=fn_stream_open(&st_io);
    /* Unknown Open completion is not safe to replace with another operation. */
    if(rc) { phase=255;return 0; }
    return 1;
}
uint8_t st_begin(void) {
    if(phase || th.active || th.tx_size<5 || th.tx_size>96) return 0;
    sent=received=prefix_count=retry=loaded=0;wanted=0;phase=1;return 1;
}
static int8_t lost(uint8_t uncertain) { phase=uncertain?255:254;th_w.valid=0;return -1; }
int8_t st_step(void) {
    uint8_t rc,n,at;
    if(phase!=1 && phase!=2) return phase>=254?-1:1;
    if(!loaded) {
        if(phase==1) { st_io.length=th.tx_size-sent;memcpy(st_io.data,th.tx+sent,st_io.length); }
        else st_io.length=prefix_count<2?120:(wanted-received>120?120:wanted-received);
        loaded=1;
    }
    rc=phase==1?fn_stream_write(&st_io):fn_stream_read(&st_io);
    /* Library retains token/sequence on 0x80. Never alter context, operation,
     * length or bytes before replay. It already retries internally three times. */
    if(rc==FN_STREAM_IO_ERROR) return ++retry<=2?0:lost(1);
    if(rc==3 || rc==4) return 0; /* validated zero progress: library advanced sequence */
    if(rc || st_io.actual>st_io.length) return lost(0);
    retry=0;loaded=0;n=st_io.actual;
    if(phase==1) {
        sent+=n;
        if(sent==th.tx_size) phase=2;
        return 0;
    }
    at=0;
    while(prefix_count<2 && at<n) prefix[prefix_count++]=st_io.received[at++];
    if(prefix_count==2 && !wanted) {
        if(prefix[1] || prefix[0]<3 || prefix[0]>240) return lost(0);
        wanted=prefix[0];
    }
    if(n-at>wanted-received) return lost(0); /* no unsolicited/coalesced response allowed */
    memcpy(th.rx+received,st_io.received+at,n-at);received+=n-at;
    if(wanted && received==wanted) {
        phase=0;
        return th_accept(wanted)?1:lost(0);
    }
    if(st_io.flags&1) return lost(0);
    return 0;
}
uint8_t st_close(void) {
    /* Do not replace an uncertain lower-link request with Close. */
    if(phase==255) return 0;
    if(fn_stream_close(&st_io)) { phase=255;return 0; }
    phase=0;return 1;
}
