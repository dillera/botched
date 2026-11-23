#include "model.h"
#include "binary_bridge.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace botched;
using namespace botched::binary;
static void check(bool ok, const char *message) {
  if (!ok)
    throw std::runtime_error(message);
}
static Json command(const char *name, const std::string &id = "") {
  Json j = Json::object();
  j.set("type", name);
  if (!id.empty())
    j.set("match_id", id);
  return j;
}
int main() {
  try {
    server::ManualClock clock;
    server::GameServer server(server::ServerConfig{}, clock);
    Bridge bridge(server, clock);
    unsigned connection = 0;
    for (auto name : {"training-01", "factory-01", "foundry-01",
                      "laserworks-01", "trapworks-01", "turretworks-01",
                      "tarworks-01", "shuntworks-01", "crossroads-01"}) {
      auto create = command("CREATE_MATCH");
      create.set("map_id", name);
      create.set("player_count", 4);
      create.set("bots", 3);
      create.set("planning_seconds", 0);
      auto made = server.handle(create);
      auto id = made.get_string("match_id").value();
      ++connection;
      Fields hello;
      hello.b(1).b(1).b(0).h(240).b(3).i(63).b(0);
      for (int i = 0; i < 16; ++i)
        hello.b(0);
      check(bridge.handle(connection, packet(1, 0, hello.span()))[0] == 0x81,
            "hello");
      ba_world world{};
      bw_stream stream;
      bw_stream_init(&stream, 0);
      unsigned xid = 1;
      for (auto op : {0x12, 0x17, 0x15}) {
        auto request = command(op == 0x12   ? "JOIN_MATCH"
                               : op == 0x17 ? "GET_MAP"
                                            : "GET_SNAPSHOT",
                               id);
        auto reply = bridge.handle(connection, encode_request(request, xid++));
        ba_begin(&world, op);
        for (;;) {
          int apply = 0;
          check(bw_stream_accept(&stream, reply.data(), reply.size(), &apply),
                "stream");
          if (apply)
            check(bw_visit_page(reply.data(), reply.size(), ba_record, &world),
                  "records");
          if (stream.complete)
            break;
          Fields f;
          f.i(stream.transfer).i(stream.next_offset);
          reply = bridge.handle(connection, packet(3, xid++, f.span()));
        }
        check(ba_finish(&world, op), "model finish");
      }
      check(world.valid && world.map_valid && world.players_count == 4,
            "complete world");
      auto snapshot = server.handle(command("GET_SNAPSHOT", id));
      const auto &players = snapshot.find("snapshot")->find("players")->items();
      for (const auto &p : players) {
        const auto *b = world.players[p.get_int("id").value() - 1];
        check(b[2] == p.get_int("x").value() && b[3] == p.get_int("y").value(),
              "position");
        check(ba_u32(b + 10) == p.get_int("score").value(), "score");
      }
      auto map_reply = server.handle(command("GET_MAP", id));
      auto map = map_from_json(*map_reply.find("map"));
      check(map.ok(), "JSON map");
      check(world.width == map.map->width && world.height == map.map->height,
            "map dimensions");
      for (unsigned i = 0; i < map.map->tiles.size(); ++i)
        check(((world.tiles[i / 2] >> ((i & 1) * 4)) & 15) ==
                  static_cast<unsigned>(map.map->tiles[i].terrain),
              "terrain");
      auto valid = world;
      uint8_t bad[9] = {11, 7, 1, 0, 0, 0, 250, 250, 0};
      ba_record(&world, bad, sizeof bad);
      check(world.failed, "unchecked coordinate");
      world = valid;
      world.crates_count = 324;
      bad[6] = bad[7] = 1;
      ba_record(&world, bad, sizeof bad);
      check(world.failed, "crate capacity");
      world = valid;
      ba_begin(&world, 0x15);
      check(!ba_finish(&world, 0x15) && !world.valid, "partial publication");
      bridge.disconnect(connection);
    }
    std::cout << "Amiga model: nine authoritative map/snapshot transfers and "
                 "bounds checks passed\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
