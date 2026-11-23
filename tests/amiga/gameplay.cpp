#include "binary_bridge.hpp"
#include "play.h"
#include "visual.h"
#include "ui_playback.hpp"
#include <algorithm>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace botched;
using namespace botched::binary;
static void check(bool x, const char *s) {
  if (!x)
    throw std::runtime_error(s);
}
static Json q(const char *type, const std::string &id = "") {
  auto j = Json::object();
  j.set("type", type);
  if (!id.empty())
    j.set("match_id", id);
  return j;
}
static void compare(const ba_world &w, const ui::BoardView &v) {
  for (auto &r : v.robots) {
    auto p = w.players[r.id - 1];
    check(bool(p[1] & 1) == r.disabled, "disabled");
    if (r.active)
      check(p[2] == r.x && p[3] == r.y, "xy");
    check(p[4] == unsigned(r.heading) && p[5] == r.damage, "heading/damage");
    check(ba_u32(p + 6) == unsigned(r.carry_id), "carry");
    check(ba_u32(p + 10) == unsigned(r.score) &&
              ba_u32(p + 14) == unsigned(r.deliveries) &&
              ba_u32(p + 22) == unsigned(r.sabotage),
          "scores");
  }
  unsigned loose = 0;
  for (unsigned i = 0; i < w.crates_count; ++i)
    if (!w.crates[i][6]) {
      ++loose;
      auto p = w.crates[i];
      check(std::any_of(v.crates.begin(), v.crates.end(),
                        [&](auto &c) {
                          return c.id == int(ba_u32(p)) && c.x == p[4] &&
                                 c.y == p[5];
                        }),
            "crate");
    }
  check(loose == v.crates.size(), "crates");
  for (unsigned i = 0; i < w.groups_count; ++i)
    check(bool(w.groups[i][1]) == v.reversed[w.groups[i][0]], "group");
  for (unsigned i = 0; i < w.hazards_count; ++i) {
    auto h = w.hazards[i];
    if (h[0] == 1) {
      auto t = v.trap(h[1]);
      check(t && h[2] == t->armed && h[3] == t->owner, "trap");
    } else {
      auto t = v.turret(h[1]);
      check(t && h[2] == t->charge && h[3] == t->owner, "turret");
    }
  }
}
int main() {
  try {
    server::ManualClock clock;
    server::GameServer server(server::ServerConfig{}, clock);
    Bridge bridge(server, clock);
    unsigned conn = 0, total_events = 0;
    for (auto map : {"training-01", "factory-01", "foundry-01", "laserworks-01",
                     "trapworks-01", "turretworks-01", "tarworks-01",
                     "shuntworks-01", "crossroads-01"}) {
      auto create = q("CREATE_MATCH");
      create.set("map_id", map);
      create.set("player_count", 4);
      create.set("bots", 3);
      create.set("rounds", 3);
      create.set("planning_seconds", 1);
      auto id = server.handle(create).get_string("match_id").value();
      ++conn;
      unsigned xid = 1;
      Fields hello;
      hello.b(1).b(1).b(0).h(240).b(3).i(63).b(0);
      for (int i = 0; i < 16; ++i)
        hello.b(0);
      check(bridge.handle(conn, packet(1, 0, hello.span()))[0] == 0x81,
            "hello");
      bw_stream stream;
      bw_stream_init(&stream, 0);
      ba_world world{};
      ba_replay replay{};
      auto transfer = [&](Bytes request, unsigned op,
                          ui::BoardView *expected = nullptr) {
        auto bytes = bridge.handle(conn, request);
        check(bridge.handle(conn, request) == bytes, "exchange replay differs");
        ba_begin(&world, op);
        uint32_t h = 0;
        for (;;) {
          int apply;
          check(bw_stream_accept(&stream, bytes.data(), bytes.size(), &apply),
                "stream");
          h = stream.high_water;
          if (op == 0x16) {
            auto ack = replay.ack;
            check(ba_replay_page(&replay, bytes.data(), bytes.size(), apply),
                  "publish");
            int duplicate = 1;
            check(bw_stream_accept(&stream, bytes.data(), bytes.size(),
                                   &duplicate) &&
                      !duplicate,
                  "duplicate applied");
            check(
                ba_replay_page(&replay, bytes.data(), bytes.size(), duplicate),
                "duplicate rejected");
            while (replay.active) {
              unsigned pos = replay.pos;
              while (pos < replay.size && replay.bytes[pos] != 21)
                pos += replay.bytes[pos + 1] + 2;
              ba_animation animation{}; animation.before=world;
              int e = ba_replay_next(&replay, &world);
              check(e >= 0, "event rejected");
              if (e) {
                ++total_events;
                ba_world authoritative=world;
                ba_animation_begin(&animation,e);
                unsigned completed=0,frames=0;
                while(animation.active) {
                  for(unsigned p=0;p<4;++p) {
                    int x=ba_lerp(animation.before.players[p][2]*16,world.players[p][2]*16,&animation);
                    check(x>=0 && x<320,"animation bounds");
                  }
                  check(replay.ack==ack,"ack advanced during animation");
                  completed+=ba_animation_tick(&animation);++frames;
                }
                check(completed==1 && frames==animation.duration,"animation completion");
                for(unsigned p=0;p<4;++p)
                  check(ba_lerp(animation.before.players[p][2]*16,world.players[p][2]*16,&animation)==world.players[p][2]*16,"animation endpoint");
                check(!ba_animation_tick(&animation),"duplicate completion");
                check(!memcmp(&authoritative,&world,sizeof world),"animation mutated mirror");
                for(unsigned tile=0;tile<world.tile_count;++tile)
                  check(ba_tile_image(&world,tile)<BA_ASSETS,"tile atlas bounds");
                check(replay.ack == ack, "early ack");
                if (expected) {
                  auto event = decode_event(
                      {replay.bytes + pos, size_t(replay.bytes[pos + 1] + 2)},
                      id);
                  ui::apply_event(*expected, event);
                  compare(world, *expected);
                }
              }
            }
          } else if (apply)
            check(bw_visit_page(bytes.data(), bytes.size(), ba_record, &world),
                  "records");
          if (stream.complete)
            break;
          Fields f;
          f.i(stream.transfer).i(stream.next_offset);
          bytes = bridge.handle(conn, packet(3, xid++, f.span()));
        }
        check(ba_finish(&world, op), "world finish");
        if (op == 0x15) {
          auto ack = replay.ack;
          ba_replay_snapshot(&replay, h);
          check(replay.ack == ack, "snapshot acknowledged");
        }
      };
      for (auto op : {0x12, 0x17, 0x15})
        transfer(encode_request(q(op == 0x12   ? "JOIN_MATCH"
                                  : op == 0x17 ? "GET_MAP"
                                               : "GET_SNAPSHOT",
                                  id),
                                xid++),
                 op);
      auto snap = server.handle(q("GET_SNAPSHOT", id));
      auto expected = ui::read_board(*snap.find("snapshot"));
      for (unsigned round = 1; round <= 3; ++round) {
        ba_editor edit{};
        ba_editor_sync(&edit, &world);
        check(!edit.locked && edit.orders[0] == BA_EMPTY, "editor reset");
        check(ba_edit(&edit, 2) == 1 && ba_edit(&edit, 0x4e) == 1 &&
                  ba_edit(&edit, 8) == 1,
              "edit controls");
        check(ba_edit(&edit, BA_LOCK_KEY) == 0, "incomplete lock disabled");
        check(ba_edit(&edit, 9) == 1, "explicit WAIT");
        check(ba_edit(&edit, BA_LOCK_KEY) == 2, "explicit lock");
        edit.pending = 1;
        check(ba_edit(&edit, 1) == 0 && ba_edit(&edit, BA_LOCK_KEY) == 0,
              "pending changed");
        if (round != 2) {
          uint8_t b[18];
          check(ba_submit_body(b, round, world.next_key++, edit.orders),
                "submit body");
          auto req = packet(0x14, xid++, b);
          transfer(req, 0x14);
          transfer(packet(0x14, xid++, b),
                   0x14); // same H3 key with a fresh exchange

          auto changed = req;
          changed.back() ^= 1;
          check(bridge.handle(conn, changed)[0] == 0xff,
                "exchange reuse accepted");
        } else
          clock.advance_ms(
              1001); // actual authoritative deadline -> default WAIT
        Fields f;
        f.b(0).i(replay.cursor).h(16);
        transfer(packet(0x16, xid++, f.span()), 0x16, &expected);
        transfer(encode_request(q("GET_SNAPSHOT", id), xid++), 0x15);
        snap = server.handle(q("GET_SNAPSHOT", id));
        expected = ui::read_board(*snap.find("snapshot"));
        compare(world, expected);
      }
      check(world.snapshot[0] == 2, "not finished");
      bridge.disconnect(conn);
    }
    // Eventless high waters, snapshots and future cursors do not regress ACK.
    ba_replay r{};
    r.ack = 50;
    r.cursor = 90;
    Records records;
    Fields c;
    c.str("m1");
    records.add(1, c.span());
    Fields meta;
    meta.i(90).i(0);
    records.add(22, meta.span());
    Fields p;
    p.i(1)
        .i(0)
        .i(records.bytes().size())
        .i(40)
        .b(0x16)
        .b(1)
        .b(records.bytes().size());
    Bytes body(p.span().begin(), p.span().end());
    body.insert(body.end(), records.bytes().begin(), records.bytes().end());
    auto page = packet(0x82, 1, body);
    ba_world w{};
    check(ba_replay_page(&r, page.data(), page.size(), 1), "empty page");
    check(ba_replay_next(&r, &w) == 0 && r.ack == 50 && r.cursor == 90,
          "future regressed");
    ba_replay_snapshot(&r, 100);
    check(r.ack == 50 && r.cursor == 100, "snapshot cursor");
    check(!ba_snapshot_ready(&r, 1, 110) && ba_snapshot_ready(&r, 0, 110),
          "snapshot race not held");
    page[15] = 110;
    page[16] = page[17] = page[18] = 0;
    check(ba_replay_page(&r, page.data(), page.size(), 1),
          "eventless version page");
    check(ba_replay_next(&r, &w) == 0 && r.ack == 110 && r.cursor == 110,
          "eventless version not consumed");
    check(ba_snapshot_ready(&r, 1, 110), "caught-up snapshot held");
    std::cout << "A1b nine maps, 27 rounds, " << total_events
              << " events: playback equivalence, deadlines, submission/replay "
                 "and cursors PASS\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
