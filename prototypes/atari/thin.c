#include "thin.h"
#include <string.h>
th_world th_w;
th_client th;
#ifndef TH_ASM
uint32_t TH_FAST th_u32(const uint8_t *p) {
    return (uint32_t)p[0] | ((uint32_t)p[1]<<8) | ((uint32_t)p[2]<<16) | ((uint32_t)p[3]<<24);
}
uint8_t *TH_FAST th_find(const uint8_t *id) {
    uint8_t i;
    for(i=0;i<th_w.crate_count;++i) if(!memcmp(th_w.crates[i],id,4)) return th_w.crates[i];
    return 0;
}
#endif
static uint16_t u16(const uint8_t *p) { return p[0] | ((uint16_t)p[1]<<8); }
static void put32(uint8_t *p, uint32_t v) {
    p[0]=(uint8_t)v; p[1]=(uint8_t)(v>>8); p[2]=(uint8_t)(v>>16); p[3]=(uint8_t)(v>>24);
}
static uint8_t fail(void) { th.error=1; th_w.valid=th_w.map_valid=0; return 0; }
static uint8_t pid(uint8_t p) { return p>=1 && p<=4; }
static uint8_t xy(const uint8_t *p) { return p[0]<20 && p[1]<16; }
static uint8_t device(uint8_t p) { return p>=1 && p<=9; }
void th_reset(void) { memset(&th,0,sizeof th); memset(&th_w,0,sizeof th_w); th.exchange=1; }
/* One shape/bounds validation per record; server owns legal game transitions.
 * Fields used as renderer/index inputs get explicit domain checks here. */
static uint8_t event_ok(const uint8_t *b, uint8_t n) {
    static const uint8_t sizes[31]={0,12,12,0,0,13,14,13,17,17,16,14,19,18,18,22,19,18,16,15,19,16,15,18,16,16,17,16,20,15,21};
    const uint8_t *p;
    uint8_t e,i,count;
    if(n<12) return 0;
    p=b+11;
    e=b[0];
    if(e<1 || e>30 || !b[5] || b[5]>200 || b[6]>3) return 0;
    if(e==3 || e==4) {
        i=e==3?1:2;
        if(n<11+i+1) return 0;
        if(e==3 ? (!p[0] || p[0]>200) : (!p[0] || p[0]>15 || p[1]>2)) return 0;
        count=p[i];
        if(count<2 || count>4 || n!=12+i+13*count) return 0;
        for(++i; count; --count,i+=13) if(!pid(p[i])) return 0;
        return 1;
    }
    if(n!=sizes[e]) return 0;
    switch(e) {
    case 1: return p[0]<16;
    case 2: return p[0]>=1 && p[0]<=200;
    case 5: case 7: return pid(p[0]) && p[1]<=8;
    case 6: return pid(p[0]) && p[1]<=8 && p[2]<4;
    case 8: return pid(p[0]) && xy(p+1) && xy(p+3) && p[5]<=1;
    case 9: return pid(p[0]) && pid(p[1]) && xy(p+2) && xy(p+4);
    case 10: return pid(p[0]) && p[1]<4; /* off-board target is intentionally signed */
    case 11: return pid(p[0]) && p[1]<4 && p[2]<4;
    case 12: return xy(p+6);
    case 13: case 14: case 15: return pid(p[0]) && xy(p+5);
    case 16: return xy(p+4) && xy(p+6);
    case 17: return xy(p+4) && p[6]<=1;
    case 18: return pid(p[0]) && p[1]<26 && p[4]<2;
    case 19: return pid(p[0]) && xy(p+1) && p[3]<=3;
    case 20: return pid(p[0]) && p[1]<4 && xy(p+2) && p[5]<=4 && p[6]<=2 && p[7]<=9;
    case 21: return pid(p[0]) && xy(p+1) && xy(p+3);
    case 22: return pid(p[0]) && xy(p+1) && p[3]<4;
    case 23: return pid(p[0]) && device(p[1]) && xy(p+2) && p[4]<2 && p[5]<=4 && p[6]<=4;
    case 24: return device(p[0]) && xy(p+1) && p[3]<=4 && p[4]<16;
    case 25: return device(p[0]) && pid(p[1]) && p[2]<=3 && p[3]<=4;
    case 26: return pid(p[0]) && device(p[1]) && xy(p+2) && p[4]<=4 && p[5]<=4;
    case 27: return device(p[0]) && xy(p+1) && p[3]<=2 && p[4]<=4;
    case 28: return device(p[0]) && xy(p+1) && p[3]<=4 && pid(p[4]) && p[5]<4 && p[7]<=3;
    case 29: return pid(p[0]) && p[1]<=8 && xy(p+2);
    case 30: return pid(p[0]) && p[2]<=4 && pid(p[3]) && p[8]<=2 && p[9]<=9;
    default: return 1;
    }
}
static uint8_t shape(uint8_t tag,const uint8_t *b,uint8_t n) {
    uint8_t a,i;
    switch(tag) {
    case 1: return n>=1 && b[0]<=16 && n==b[0]+1;
    case 2: return n==5;
    case 3: return n>=3 && pid(b[0]) && b[2]<=64 && n==b[2]+3;
    case 4: return n==1;
    case 5: return n>=20 && n<=85; /* only display status; key/echo remain opaque */
    case 6: return n==1;
    case 7: return n<=99;
    case 8: return n==7 && b[0]==1 && pid(b[1]);
    case 9: return n==31 && b[0]<=3 && b[15]>=2 && b[15]<=4 && u16(b+16)<=TH_CRATES && b[18]<=26 && b[19]<=9 && b[20]<=9;
    case 10: return n==27 && pid(b[0]) && b[1]<16 && ((b[1]&1)||xy(b+2)) && b[4]<4 && b[5]<=3;
    case 11: return n==7 && b[6]<=4 && (b[6]||xy(b+4));
    case 12: return n==2 && b[0]<26 && b[1]<2;
    case 13: return n==4 && b[0]>=1 && b[0]<=2 && device(b[1]) && b[2]<=(b[0]==1?1:2) && b[3]<=4;
    case 14:
        if(n<18 || b[0]>3 || b[17]>3 || n!=18+2*b[17]) return 0;
        for(i=18;i<n;i+=2) if(!pid(b[i]) || b[i+1]>2) return 0;
        return 1;
    case 15: return n>=10 && n<=74 && b[9]==n-10; /* display text streamed, no cache */
    case 16:
        if(n<11 || b[4]>32 || n!=11+b[4]) return 0;
        a=5+b[4];
        return b[a]>=8 && b[a]<=20 && b[a+1]>=8 && b[a+1]<=16 && u16(b+a+2)==(uint16_t)b[a]*b[a+1] && u16(b+a+4)<=TH_ATTRS;
    case 17:
        if(n<4 || !b[2] || b[2]>128 || n!=3+(b[2]+1)/2 || u16(b)+b[2]>320 || (b[0]&1)) return 0;
        for(i=3;i<n;++i) if((b[i]&15)>13 || (b[i]>>4)>13) return 0;
        return 1;
    case 18: return n==5 && xy(b);
    case 19: return n==2;
    case 20: return n<=39; /* map catalogue is not used by this join-only prototype */
    case 21: return event_ok(b,n);
    case 22: return n==8;
    default: return 0;
    }
}
uint8_t th_accept(uint8_t n) {
    uint8_t pos,tag,len,crates,attrs,tiles;
    uint32_t off,total,id;
    if(th.active || n<3 || n>240) return fail();
    if(th.rx[0]==0x81) {
        if(n!=48 || u16(th.rx+1)!=0 || th.rx[3]!=1 || th.rx[4] || u16(th.rx+5)!=240 || th.rx[7]!=2) return fail();
        if(th.have_epoch && memcmp(th.epoch,th.rx+12,16)) th.ack=th.cursor=0;
        memcpy(th.epoch,th.rx+12,16); th.have_epoch=1; th.exchange=1;
        th_w.valid=th_w.map_valid=0; th.transfer=0; th.offset=0; return 1;
    }
    if(u16(th.rx+1)!=th.exchange || th.exchange==65535u) return fail();
    if(th.rx[0]==0xff) { if(n!=4) return fail(); th.error=th.rx[3]; ++th.exchange; return 1; }
    if(th.rx[0]!=0x82 || n<25 || th.rx[20]>3 || th.rx[21]!=n-22) return fail();
    id=th_u32(th.rx+3); off=th_u32(th.rx+7); total=th_u32(th.rx+11);
    if(!id || !total || total>4194304UL || off>total || (uint32_t)(n-22)>total-off || ((th.rx[20]&1)!=0)!=(off+n-22==total)) return fail();
    if(off && (id!=th.transfer || off!=th.offset || total!=th.total || th.rx[19]!=th.origin || th_u32(th.rx+15)!=th.high)) return fail();
    if(!off && th.offset && !th.finished) return fail();
    crates=(!off && th.rx[19]==0x15)?0:th_w.crate_count;
    attrs=(!off && th.rx[19]==0x17)?0:th_w.attrs;
    tiles=(!off && th.rx[19]==0x17)?0:th.tile_bytes;
    for(pos=22;pos<n;) {
        if(n-pos<2) return fail();
        tag=th.rx[pos];len=th.rx[pos+1];
        if(len>n-pos-2 || !shape(tag,th.rx+pos+2,len)) return fail();
        if(tag==11 && ++crates>TH_CRATES) return fail();
        if(tag==18 && ++attrs>TH_ATTRS) return fail();
        if(tag==17) {
            if(u16(th.rx+pos+2)!=(uint16_t)tiles*2 || len-3>160-tiles) return fail();
            tiles+=(uint8_t)(len-3);
        }
        pos+=(uint8_t)(len+2);
    }
    th.size=n; th.pos=22; th.active=1; th.finished=0;
    th.transfer=id; th.offset=off; th.total=total; th.high=th_u32(th.rx+15);
    th.origin=th.rx[19]; th.flags=th.rx[20];
    if(!off) {
        if(th.origin==0x15) { th_w.valid=0; th_w.crate_count=0; memset(th_w.groups,0,sizeof th_w.groups);memset(th_w.hazards,0,sizeof th_w.hazards); th.seen_players=th.seen_groups=th.seen_hazards=0; }
        if(th.origin==0x17) { th_w.map_valid=0; th_w.attrs=0; th.tile_bytes=0; }
    }
    ++th.exchange;
    return 1;
}
static uint8_t *player(uint8_t id) { return th_w.players[id-1]; }
static uint8_t *crate(const uint8_t *id,uint8_t add) {
    uint8_t *found=th_find(id);
    if(found) return found;
    if(!add || th_w.crate_count==TH_CRATES) return 0;
    memcpy(th_w.crates[th_w.crate_count],id,4);
    return th_w.crates[th_w.crate_count++];
}
static void erase(const uint8_t *id) {
    uint8_t *found=th_find(id);
    if(found) { --th_w.crate_count;memcpy(found,th_w.crates[th_w.crate_count],7); }
}
static void count_up(uint8_t *p,uint8_t add) { put32(p,th_u32(p)+add); }
static void apply_event(const uint8_t *b) {
    const uint8_t *p=b+11;
    uint8_t *r,*c,i,j,e=b[0];
    switch(e) {
    case 1: th_w.snapshot[0]=1; break;
    case 2: th_w.snapshot[0]=0; th_w.snapshot[1]=p[0]; th_w.snapshot[2]=0; break;
    case 3: case 4:
        j=e==3?2:3;
        for(i=0;i<p[j-1];++i,j+=13) memcpy(player(p[j])+10,p+j+1,12);
        if(e==4) { th_w.snapshot[0]=2; th_w.status[16]=p[0]; }
        break;
    case 8: memcpy(player(p[0])+2,p+3,2); break;
    case 9: memcpy(player(p[0])+2,p+4,2); break;
    case 11: player(p[0])[4]=p[2]; break;
    case 12:
        c=crate(p,1); if(!c) { fail(); break; }
        memcpy(c+4,p+6,2);c[6]=0;break;
    case 13: case 14:
        r=player(p[0]); c=crate(p+1,1); if(!c) { fail(); break; }
        if(e==13) { r[1]|=2;memcpy(r+6,p+1,4);c[4]=c[5]=0;c[6]=p[0]; }
        else { r[1]&=(uint8_t)~2;memset(r+6,0,4);memcpy(c+4,p+5,2);c[6]=0; }
        break;
    case 15:
        r=player(p[0]);r[1]&=(uint8_t)~2;memset(r+6,0,4);memcpy(r+10,p+7,4);count_up(r+14,1);erase(p+1);break;
    case 16:
        c=crate(p,1);if(!c) { fail();break; } memcpy(c+4,p+6,2);c[6]=0;break;
    case 17:
        erase(p);
        for(i=0;i<4;++i) if(!memcmp(player(i+1)+6,p,4)) { player(i+1)[1]&=(uint8_t)~2;memset(player(i+1)+6,0,4); }
        break;
    case 18: th_w.groups[p[1]]=p[4];break;
    case 19: r=player(p[0]);r[5]=p[3];count_up(r+18,1);break;
    case 20: r=player(p[0]);r[1]|=1;r[2]=r[3]=0;break;
    case 22: r=player(p[0]);r[1]&=(uint8_t)~3;memcpy(r+2,p+1,2);r[4]=p[3];r[5]=0;memset(r+6,0,4);break;
    case 23: th_w.hazards[0][p[1]-1][0]=1;th_w.hazards[0][p[1]-1][1]=p[6];break;
    case 24: th_w.hazards[0][p[0]-1][0]=0;break;
    case 25: r=player(p[1]);r[5]=p[2];count_up(r+18,1);break;
    case 26: th_w.hazards[1][p[1]-1][1]=p[5];break;
    case 27: memcpy(th_w.hazards[1][p[0]-1],p+3,2);break;
    case 28: th_w.hazards[1][p[0]-1][0]=0;r=player(p[4]);r[5]=p[7];count_up(r+18,1);break;
    case 30: r=player(p[0]);memcpy(r+10,p+4,4);r[1]|=4;count_up(r+22,p[1]);break;
    default: break; /* narration/order/no-op/tar/redirect; exposed to animation consumer */
    }
}
static uint8_t map_fits(void) {
    uint16_t i,tiles=(uint16_t)th_w.width*th_w.height,w=4,last=0,index,value;
    uint8_t kind,a,needed=0;
    if(th.tile_bytes!=(tiles+1)/2 || th_w.attrs!=u16(th_w.map+9+th_w.map[4])) return 0;
    for(i=0;i<tiles;++i) {
        kind=(th_w.tiles[i/2]>>((i&1)*4))&15;
        if(kind!=1 && kind!=7 && kind!=8 && kind!=9 && kind!=10 && kind!=11) ++w;
        if(kind!=0 && kind!=1 && kind!=8 && kind!=10) { if(++needed>TH_ATTRS) return 0; }
    }
    if(w>TH_CRATES || needed!=th_w.attrs) return 0;
    for(a=0;a<th_w.attrs;++a) {
        const uint8_t *p=th_w.attributes[a];
        if(p[0]>=th_w.width || p[1]>=th_w.height) return 0;
        index=(uint16_t)p[1]*th_w.width+p[0];
        if(a && index<=last) return 0;
        last=index;kind=(th_w.tiles[index/2]>>((index&1)*4))&15;value=u16(p+2);
        switch(kind) {
        case 2: case 3: if(value<1 || value>4) return 0;break;
        case 4: if(value<1 || value>999) return 0;break;
        case 5: case 6: if(value>25) return 0;break;
        case 7: if(value) return 0;break;
        case 9: case 11: case 12: case 13: if(value<1 || value>9) return 0;break;
        default:return 0;
        }
        if((kind==2 || kind==5 || kind==7)?p[4]>3:p[4]!=0) return 0;
    }
    return 1;
}
uint8_t th_next(const uint8_t **body,uint8_t *size) {
    const uint8_t *b;
    uint8_t tag,n,a;
    if(!th.active || th.error) return 0;
    tag=th.rx[th.pos];n=th.rx[th.pos+1];b=th.rx+th.pos+2;
    *body=b;*size=n;
    switch(tag) {
    case 1: memcpy(th.match,b,n);break;
    case 3: th.player=b[0];memcpy(th.credential,b+2,n-2);break;
    case 8: th.player=b[1];th.next_key=b[6]?0xffffffffUL:th_u32(b+2);break;
    case 9: memcpy(th_w.snapshot,b,31);break;
    case 10: memcpy(player(b[0]),b,27);++th.seen_players;break;
    case 11: if(th_w.crate_count==TH_CRATES) { fail(); break; } memcpy(th_w.crates[th_w.crate_count++],b,7);break;
    case 12: th_w.groups[b[0]]=b[1];++th.seen_groups;break;
    case 13: memcpy(th_w.hazards[b[0]-1][b[1]-1],b+2,2);++th.seen_hazards;break;
    case 14: memset(th_w.status,0,sizeof th_w.status);memcpy(th_w.status,b,n);break;
    case 16: memset(th_w.map,0,sizeof th_w.map);memcpy(th_w.map,b,n);a=5+b[4];th_w.width=b[a];th_w.height=b[a+1];break;
    case 17: memcpy(th_w.tiles+u16(b)/2,b+3,n-3);th.tile_bytes+=(uint8_t)(n-3);break;
    case 18: memcpy(th_w.attributes[th_w.attrs++],b,5);break;
    case 21: apply_event(b);break;
    default: break;
    }
    if(th.error) { th.active=0; return 0; }
    th.pos+=(uint8_t)(n+2);
    if(th.pos==th.size) {
        th.active=0;th.offset+=th.size-22;
        if(th.flags&1) {
            th.finished=1;
            if(!(th.flags&2)) {
                if(th.origin==0x16) { if(th.high>th.ack) th.ack=th.high; if(th.high>th.cursor) th.cursor=th.high; }
                if(th.origin==0x15) {
                    if(th_w.crate_count!=u16(th_w.snapshot+16) || th.seen_players!=th_w.snapshot[15] || th.seen_groups!=th_w.snapshot[18] || th.seen_hazards!=th_w.snapshot[19]+th_w.snapshot[20]) fail();
                    else { th_w.valid=1; th.cursor=th.high; }
                }
                if(th.origin==0x17) {
                    if(!map_fits()) fail();
                    else th_w.map_valid=1;
                }
            }
        }
    }
    return tag;
}
static uint8_t start(uint8_t op,uint8_t n) {
    if(th.active || !th.exchange || th.exchange==65535u) return 0;
    memset(th.tx,0,96);th.tx[0]=n;th.tx[2]=op;th.tx[3]=(uint8_t)th.exchange;th.tx[4]=(uint8_t)(th.exchange>>8);th.tx_size=n+2;th.error=0;return 1;
}
uint8_t th_hello(void) {
    if(!start(1,30)) return 0;
    th.tx[3]=th.tx[4]=0;th.tx[5]=th.tx[6]=1;th.tx[8]=240;th.tx[10]=2;th.tx[11]=63;return 1;
}
uint8_t th_join(const char *match) {
    uint8_t n=0;
    while(n<17 && match[n]) ++n;
    if(!n || n>16 || !th_w.map_valid || th.match[0]!=n || memcmp(th.match+1,match,n) || !start(0x12,n+4)) return 0;
    th.tx[5]=n;memcpy(th.tx+6,match,n);return 1;
}
uint8_t th_inspect_map(const char *match) {
    uint8_t n=0;
    while(n<17 && match[n]) ++n;
    if(!n || n>16 || !start(0x17,n+5)) return 0;
    th.tx[5]=1;th.tx[6]=n;memcpy(th.tx+7,match,n);return 1;
}
uint8_t th_bind(void) {
    uint8_t m=th.match[0],c=th.credential[0];
    if(!m || m>16 || !c || c>64 || !pid(th.player) || !start(2,10+m+c)) return 0;
    th.tx[5]=1;memcpy(th.tx+6,th.match,m+1);th.tx[7+m]=th.player;memcpy(th.tx+11+m,th.credential,c+1);return 1;
}
uint8_t th_query(uint8_t op) {
    if(op!=0x15 && op!=0x16 && op!=0x17 && op!=0x18) return 0;
    if(!start(op,op==0x16?10:4)) return 0;
    if(op==0x16) { put32(th.tx+6,th.cursor);th.tx[10]=16; }
    return 1;
}
uint8_t th_fetch(void) {
    if(th.finished || !th.transfer || !start(3,11)) return 0;
    put32(th.tx+5,th.transfer);put32(th.tx+9,th.offset);return 1;
}
uint8_t th_submit(uint8_t round,uint8_t revision,const uint8_t orders[3]) {
    if(!round || !revision || orders[0]>8 || orders[1]>8 || orders[2]>8 || th.next_key==0xffffffffUL || !start(0x14,21)) return 0;
    put32(th.tx+6,round);put32(th.tx+10,revision);memcpy(th.tx+14,orders,3);th.tx[17]=1;th.tx[18]=1;put32(th.tx+19,th.next_key++);return 1;
}
