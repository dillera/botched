#include "stream.h"
#include "binary_bridge.hpp"
#include "ui_playback.hpp"
#include <algorithm>
#include <cstring>
#include <fstream>
#include <filesystem>
#include <iostream>
#include <random>
using namespace botched;
using namespace botched::binary;
static void check(bool b,const char *why) { if(!b) throw std::runtime_error(why); }
static Json query(const char *t,const std::string &id="") { auto j=Json::object();j.set("type",t);if(!id.empty())j.set("match_id",id);return j; }
static server::ManualClock clock_;
static server::GameServer server_(server::ServerConfig{},clock_);
static Bridge bridge(server_,clock_);
static Bytes input,output,cache,saved;
static unsigned calls=0,replays=0,connection=0;
static int pending=0;
static bool force_eof=false,force_io=false;
static uint8_t actual=0;
static std::vector<Bytes> capture;
extern "C" uint8_t fn_stream_init(fn_stream_io_t *s) { std::memset(s,0,sizeof *s);s->version=1;return 0; }
extern "C" uint8_t fn_stream_open(fn_stream_io_t *) { ++connection;input.clear();output.clear();pending=0;return 0; }
extern "C" uint8_t fn_stream_close(fn_stream_io_t *) { bridge.disconnect(connection);return 0; }
static bool busy() { return ++calls%11==0; }
extern "C" uint8_t fn_stream_write(fn_stream_io_t *s) {
    if(force_io) {
        if(saved.empty()) saved=Bytes(s->data,s->data+s->length);
        check(saved==Bytes(s->data,s->data+s->length),"uncertain operation replaced");
        return 0x80;
    }
    if(pending) { check(pending==3 && saved==Bytes(s->data,s->data+s->length),"changed uncertain write");s->actual=actual;pending=0;++replays;return 0; }
    if(busy()) { s->actual=0;return 3; }
    s->actual=std::min<unsigned>(7,s->length);
    input.insert(input.end(),s->data,s->data+s->actual);
    if(input.size()>=2 && input.size()==2+unsigned(input[0])) {
        auto p=bridge.handle(connection,std::span<const uint8_t>(input).subspan(2));
        check(output.empty(),"overlapping requests");output={uint8_t(p.size()),0};output.insert(output.end(),p.begin(),p.end());input.clear();
    }
    if(calls%17==0) { pending=3;actual=s->actual;saved=Bytes(s->data,s->data+s->length);return 0x80; }
    return 0;
}
extern "C" uint8_t fn_stream_read(fn_stream_io_t *s) {
    if(force_eof) {force_eof=false;s->actual=0;s->flags=1;return 0;}
    if(pending) { check(pending==2 && saved.size()==s->length,"changed uncertain read size");s->actual=actual;std::copy(cache.begin(),cache.end(),s->received);pending=0;++replays;return 0; }
    if(busy()) { s->actual=0;return 4; }
    s->actual=std::min<unsigned>({31,s->length,unsigned(output.size())});s->flags=0;
    std::copy_n(output.begin(),s->actual,s->received);output.erase(output.begin(),output.begin()+s->actual);
    if(calls%19==0) { pending=2;actual=s->actual;cache=Bytes(s->received,s->received+s->actual);saved.resize(s->length);return 0x80; }
    return 0;
}
static ui::BoardView expected;
static bool playback=false;
static Bytes safety_page;
static void compare() {
    for(const auto&r:expected.robots) {
        const auto*p=th_w.players[r.id-1];
        check(bool(p[1]&1)==r.disabled,"disabled");
        if(r.active)check(p[2]==r.x && p[3]==r.y,"position");
        check(p[4]==unsigned(r.heading) && p[5]==r.damage,"heading/damage");
        check(th_u32(p+6)==unsigned(r.carry_id),"carrying");
        check(th_u32(p+10)==unsigned(r.score) && th_u32(p+14)==unsigned(r.deliveries) && th_u32(p+22)==unsigned(r.sabotage),"score");
    }
    unsigned loose=0;
    for(unsigned i=0;i<th_w.crate_count;++i) if(!th_w.crates[i][6]) {
        ++loose;auto*p=th_w.crates[i];
        check(std::any_of(expected.crates.begin(),expected.crates.end(),[&](auto&c){return c.id==int(th_u32(p)) && c.x==p[4] && c.y==p[5];}),"crate");
    }
    check(loose==expected.crates.size(),"crate count");
    for(auto&t:expected.traps)check(th_w.hazards[0][t.id-1][0]==t.armed && th_w.hazards[0][t.id-1][1]==t.owner,"trap");
    for(auto&t:expected.turrets)check(th_w.hazards[1][t.id-1][0]==t.charge && th_w.hazards[1][t.id-1][1]==t.owner,"turret");
}
static void exchange() {
    check(st_begin(),"begin");int rc=0;unsigned budget=100000;
    while(!rc && --budget)rc=st_step();
    check(rc==1 && budget,"transport response");
    if(th.rx[0]==0x82)capture.emplace_back(th.rx,th.rx+th.size);
    const auto ack_before=th.ack;
    const uint8_t*b;uint8_t n;
    while(auto tag=th_next(&b,&n)) if(tag==21 && playback) {
        auto e=decode_event({b-2,size_t(n+2)},"m");ui::apply_event(expected,e);compare();
    }
    check(!th.error,"decoder apply");
    if(th.rx[0]==0x82 && th.origin==0x16 && !(th.flags&1)) check(th.ack==ack_before,"premature acknowledgement");
}
static void drain() { exchange();while(!th.finished && th.rx[0]==0x82) {check(th_fetch(),"fetch");exchange();} }
static void save(const std::filesystem::path &dir,const std::string &name) {
    std::ofstream f(dir/(name+".bin"),std::ios::binary);
    unsigned bytes=0;
    for(auto&p:capture) { f.put(uint8_t(p.size()));f.put(0);f.write((char*)p.data(),p.size());bytes+=p.size(); }
    std::cout<<name<<" pages="<<capture.size()<<" payload="<<bytes<<'\n';capture.clear();
}
int main(int argc,char**argv) {
 try {
    check(argc==2,"output directory");std::filesystem::path dir=argv[1];std::filesystem::create_directories(dir);
    for(auto map:{"training-01","factory-01","foundry-01","laserworks-01","trapworks-01","turretworks-01","tarworks-01","shuntworks-01","crossroads-01"}) {
        auto create=query("CREATE_MATCH");create.set("map_id",map);create.set("player_count",4);create.set("bots",3);create.set("rounds",6);create.set("planning_seconds",0);
        auto m=server_.handle(create).get_string("match_id").value();
        th_reset();check(st_open("tcp://test:7879"),"open");check(th_hello(),"hello");exchange();
        check(th_inspect_map(m.c_str()),"preflight map");drain();check(th_w.map_valid,"preflight admission");capture.clear();
        check(th_join(m.c_str()),"join");drain();capture.clear();
        check(th_query(0x17),"map query");drain();check(th_w.map_valid,"map admission");save(dir,std::string(map)+"-map");
        check(th_query(0x15),"snapshot");drain();check(th_w.valid,"valid snapshot");check(th.ack==0,"snapshot acknowledged events");safety_page=capture.front();save(dir,std::string(map)+"-initial");
        auto snap=server_.handle(query("GET_SNAPSHOT",m));expected=ui::read_board(*snap.find("snapshot"));compare();
        for(int round=1;round<=6;++round) {
            const uint8_t orders[3]={1,7,8};check(th_submit(round,1,orders),"submit");drain();capture.clear();
            playback=true;check(th_query(0x16),"events");drain();playback=false;
            save(dir,std::string(map)+"-round"+std::to_string(round));
            check(th_query(0x15),"fresh snapshot");drain();save(dir,std::string(map)+"-after"+std::to_string(round));
            auto after=server_.handle(query("GET_SNAPSHOT",m));expected=ui::read_board(*after.find("snapshot"));compare();
            if(round==3) {
                auto watermark=th.ack;
                check(th_query(0x18) && st_begin(),"disconnect request");
                force_eof=true;int disconnected=0;
                for(unsigned attempts=0;!disconnected && attempts<1000;++attempts)disconnected=st_step();
                check(disconnected==-1,"missing EOF failure");
                check(st_close() && st_open("tcp://test:7879"),"reconnect after known EOF");
                check(th_hello(),"reattach hello");exchange();
                check(th_bind(),"reattach bind");drain();capture.clear();
                check(th_query(0x17),"reattach map");drain();capture.clear();
                check(th_query(0x15),"reattach snapshot");drain();capture.clear();compare();
                check(th.ack==watermark,"reattach watermark changed");
            }
        }
        check(th_w.snapshot[0]==2,"match completion");check(st_close(),"close");
    }
    /* Replay primitives for later GUI: independent transcript plus every B1 event
       remains covered by corpus/fixture tests in the measurement runner. */
    check(replays>0,"missing carrier replay coverage");
    // Mutate every byte of a structurally valid snapshot, reaching record/index
    // checks instead of almost always failing the outer header. No heap in parser.
    for(unsigned off=0;off<safety_page.size();++off)for(unsigned value=0;value<256;++value) {
        th_reset();std::copy(safety_page.begin(),safety_page.end(),th.rx);th.rx[off]=value;
        th.exchange=th.rx[1]|unsigned(th.rx[2])<<8;
        if(th_accept(safety_page.size())) {
            const uint8_t*b;uint8_t n;
            while(th_next(&b,&n)) {}
        }
    }
    th_reset();th.ack=100;th.cursor=2147483647UL;
    Records r;Fields context;context.str("m1");r.add(1,context.span());
    Fields meta;meta.i(2147483647).i(0);r.add(22,meta.span());
    Fields page;page.i(1).i(0).i(r.bytes().size()).i(1).b(0x16).b(1).b(r.bytes().size());
    Bytes raw(page.span().begin(),page.span().end());raw.insert(raw.end(),r.bytes().begin(),r.bytes().end());auto payload=packet(0x82,1,raw);
    check(bw_validate_packet(payload.data(),payload.size(),240),"future cursor fixture");
    std::copy(payload.begin(),payload.end(),th.rx);check(th_accept(payload.size()),"future cursor accepted");
    {const uint8_t*b;uint8_t n;while(th_next(&b,&n)) {}}
    check(th.ack==100,"ack regressed");
    th.active=0;th.finished=1;th.player=1;th.match[0]=16;th.credential[0]=64;
    std::memset(th.match+1,'m',16);std::memset(th.credential+1,'a',64);
    check(th_bind() && bw_validate_packet(th.tx+2,th.tx[0],240),"maximum BIND encoder");
    th.exchange=65535;check(!th_query(0x15),"exchange wrapped");
    // Bounds fuzz: no renderer or reference codec is involved in the decoder.
    std::mt19937 random(42);
    for(unsigned i=0;i<100000;++i) {
        th_reset();for(auto&b:th.rx)b=uint8_t(random());
        th.rx[0]=0x82;th.rx[1]=1;th.rx[2]=0;
        (void)th_accept(3+random()%238);
    }
    // Valid PAGE envelopes with adversarial record bodies reach all shape checks.
    for(unsigned trial=0;trial<100000;++trial) {
        th_reset();unsigned n=random()%217;
        for(auto&b:th.rx)b=uint8_t(random());
        th.rx[0]=0x82;th.rx[1]=1;th.rx[2]=0;
        auto put=[](uint8_t*p,unsigned v){for(unsigned i=0;i<4;++i)p[i]=uint8_t(v>>(8*i));};
        put(th.rx+3,1);put(th.rx+7,0);put(th.rx+11,n+2);put(th.rx+15,1);
        th.rx[19]=0x15+trial%3;th.rx[20]=1;th.rx[21]=n+2;
        th.rx[22]=1+trial%22;th.rx[23]=n;
        if(th_accept(n+24)) {const uint8_t*b;uint8_t size;while(th_next(&b,&size)) {}}
    }
    th_reset();check(st_open("tcp://test:7879") && th_hello() && st_begin(),"uncertain failure setup");
    saved.clear();force_io=true;
    check(st_step()==0 && st_step()==0 && st_step()==-1,"uncertain retry bound");
    check(!st_close() && !st_open("tcp://test:7879"),"overwrote uncertain operation");
    std::cout<<"THIN PASS: 9 maps, 54 rounds, every event compared with existing playback; retries="<<replays<<'\n';
    return 0;
 } catch(const std::exception&e) { std::cerr<<e.what()<<" op="<<int(th.tx[2])<<" tag="<<int(th.rx[th.pos])<<" origin="<<int(th.origin)<<" error="<<int(th.error)<<'\n';return 1; }
}
