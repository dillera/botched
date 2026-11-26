#include "binary_bridge.hpp"
#include "nio_peer.hpp"
#include "session.h"
#include <stdexcept>
#define CHECK(x)                                                               \
  do {                                                                         \
    if (!(x))                                                                  \
      throw std::runtime_error(#x);                                            \
  } while (0)
#include <cstring>
#include <iostream>
#include <map>
using namespace botched;
using namespace botched::binary;
namespace {
Json query(const char *name, const std::string &match = "") {
  auto j = Json::object();
  j.set("type", name);
  if (!match.empty())
    j.set("match_id", match);
  return j;
}
struct Service {
  server::ManualClock clock;
  server::GameServer server{server::ServerConfig{}, clock};
  Bridge bridge{server, clock};
  struct Stream {
    Bytes input, output, cache;
    uint32_t tx = 0, rx = 0, cached_offset = 0;
    uint16_t cached_size = 0;
  };
  std::map<uint16_t, Stream> streams;
  uint16_t next = 1;
  unsigned calls = 0, opens = 0, closes = 0, replays = 0, again = 0,
           partial_read = 0, partial_write = 0;
  uint8_t drop_write = 0;
  bool expire_on_drop = false;
  unsigned close_failures = 0, close_calls = 0;
  uint32_t ticks = 0, cancel_at = 0;
  bool stalled = false, stall_once = false;
  uint8_t stall_after = 0;
  std::vector<Bytes> requests;
  static uint32_t now(void *p) { return get(p).ticks; }
  static void pause(void *p) {
    auto &s = get(p);
    s.ticks += 20;
    s.clock.advance_ms(20);
  }
  static int cancelled(void *p) {
    auto &s = get(p);
    return s.cancel_at && s.ticks >= s.cancel_at;
  }
  bool drop_read = false, broken = false, bad_count = false, close_busy = false;
  unsigned max_chunk = 0;
  static Service &get(void *p) { return *static_cast<Service *>(p); }
  bool busy() {
    if (++calls % 7 == 0) {
      ++again;
      return true;
    }
    return false;
  }
  static uint8_t open(void *p, const char *, uint16_t *handle) {
    auto &s = get(p);
    *handle = 0;
    if (s.busy())
      return BN_IO_AGAIN;
    if (s.stall_once) {
      s.stalled = false;
      s.stall_once = false;
    }
    *handle = s.next++;
    s.streams.emplace(*handle, Stream{});
    ++s.opens;
    return BN_IO_OK;
  }
  static uint8_t info(void *p, uint16_t, uint8_t *flags) {
    auto &s = get(p);
    if (s.busy())
      return BN_IO_AGAIN;
    *flags = BN_INFO_READY;
    return BN_IO_OK;
  }
  static uint8_t close(void *p, uint16_t h) {
    auto &s = get(p);
    ++s.close_calls;
    if (s.close_failures) {
      --s.close_failures;
      return BN_IO_LOST;
    }
    if (s.close_busy) {
      s.close_busy = false;
      return BN_IO_AGAIN;
    }
    s.bridge.disconnect(h);
    s.streams.erase(h);
    ++s.closes;
    return BN_IO_OK;
  }
  static uint8_t write(void *p, uint16_t h, uint32_t offset,
                       const uint8_t *data, uint16_t wanted, uint16_t *count) {
    auto &s = get(p);
    *count = 0;
    s.max_chunk = std::max<unsigned>(s.max_chunk, wanted);
    if (s.busy())
      return BN_IO_AGAIN;
    auto &stream = s.streams.at(h);
    CHECK(offset == stream.tx);
    if (s.broken) {
      s.broken = false;
      return BN_IO_LOST;
    }
    *count = std::min<uint16_t>(wanted, 3);
    if (*count < wanted)
      ++s.partial_write;
    stream.tx += *count;
    stream.input.insert(stream.input.end(), data, data + *count);
    if (stream.input.size() >= 2) {
      size_t n = stream.input[0] | unsigned(stream.input[1]) << 8;
      if (stream.input.size() == n + 2) {
        auto op = stream.input[2];
        s.requests.emplace_back(stream.input.begin() + 2, stream.input.end());
        auto reply = s.bridge.handle(h, {stream.input.data() + 2, n});
        stream.output.push_back(reply.size());
        stream.output.push_back(0);
        stream.output.insert(stream.output.end(), reply.begin(), reply.end());
        stream.input.clear();
        if (op == s.stall_after) {
          s.stall_after = 0;
          s.stalled = true;
          s.stall_once = true;
        }
        if (s.drop_write == op) {
          s.drop_write = 0;
          if (s.expire_on_drop)
            s.clock.advance_ms(1800001);
          *count = 0;
          return BN_IO_LOST;
        }
      }
    }
    return BN_IO_OK;
  }
  static uint8_t read(void *p, uint16_t h, uint32_t offset, uint8_t *data,
                      uint16_t wanted, uint16_t *count, uint8_t *flags) {
    auto &s = get(p);
    *count = 0;
    *flags = 0;
    s.max_chunk = std::max<unsigned>(s.max_chunk, wanted);
    if (s.busy())
      return BN_IO_AGAIN;
    if (s.stalled)
      return BN_IO_AGAIN;
    if (s.bad_count) {
      *count = wanted + 1;
      s.bad_count = false;
      return BN_IO_OK;
    }
    auto &stream = s.streams.at(h);
    if (offset < stream.rx) {
      CHECK(offset == stream.cached_offset);
      CHECK(wanted == stream.cached_size);
      ++s.replays;
      *count = stream.cache.size();
      std::memcpy(data, stream.cache.data(), *count);
      return BN_IO_OK;
    }
    CHECK(offset == stream.rx);
    if (stream.output.empty())
      return BN_IO_AGAIN;
    *count =
        std::min<size_t>(std::min<uint16_t>(wanted, 5), stream.output.size());
    if (*count < wanted)
      ++s.partial_read;
    stream.cache.assign(stream.output.begin(), stream.output.begin() + *count);
    stream.cached_size = wanted;
    stream.cached_offset = offset;
    stream.output.erase(stream.output.begin(), stream.output.begin() + *count);
    stream.rx += *count;
    if (s.drop_read) {
      s.drop_read = false;
      *count = 0;
      return BN_IO_LOST;
    }
    std::memcpy(data, stream.cache.data(), *count);
    return BN_IO_OK;
  }
  static const bn_ops ops;
};
const bn_ops Service::ops{Service::open, Service::info, Service::write,
                          Service::read, Service::close};
} // namespace

static void setup(Service &io, ba_session &s, int rounds = 3, int seconds = 0,
                  int bots = 3) {
  auto q = query("CREATE_MATCH");
  q.set("map_id", "foundry-01");
  q.set("player_count", 4);
  q.set("bots", bots);
  q.set("rounds", rounds);
  q.set("planning_seconds", seconds);
  CHECK(io.server.handle(q).get_bool("ok").value());
  s.url = "tcp://test:7879";
  s.match = "m1";
  s.ops = &Service::ops;
  s.io_context = &io;
  s.hooks = {Service::now, Service::pause, Service::cancelled, &io};
}
static void run(ba_session &s, unsigned op, uint32_t cursor = 0,
                unsigned round = 0, const uint8_t *orders = nullptr) {
  ba_session_run(&s, op, cursor, round, orders);
  CHECK(s.result == 0);
}
static void same_retry(Service &io, unsigned op) {
  std::vector<Bytes> v;
  for (auto &r : io.requests)
    if (r[0] == op)
      v.push_back(r);
  CHECK(v.size() == 2);
  CHECK(v[0] == v[1]);
}
static unsigned drain(ba_session &s, ba_replay &r, ba_world &w) {
  unsigned count = 0;
  run(s, BA_EVENTS, r.cursor);
  do {
    CHECK(ba_replay_page(&r, s.notice.page, s.notice.size, s.notice.apply));
    CHECK(ba_replay_page(&r, s.notice.page, s.notice.size, 0));
    auto ack = r.ack;
    while (r.active) {
      int e = ba_replay_next(&r, &w);
      CHECK(e >= 0);
      if (e) {
        ++count;
        CHECK(r.ack == ack);
      }
    }
    if (r.final)
      break;
    run(s, BA_FETCH);
  } while (true);
  return count;
}
int main() {
  try {
    for (unsigned op : {0x12u, 0x14u})
      for (bool timeout : {false, true}) {
        Service io;
        ba_session s{};
        setup(io, s);
        if (op == 0x12) {
          if (timeout)
            io.stall_after = op;
          else
            io.drop_write = op;
        }
        run(s, BA_LOAD);
        CHECK(s.world.player == 1);
        if (op == 0x14) {
          auto key = s.world.next_key;
          if (timeout)
            io.stall_after = op;
          else
            io.drop_write = op;
          uint8_t orders[] = {1, 7, 8};
          run(s, BA_SUBMIT, 0, 1, orders);
          CHECK(s.notice.accepted);
          CHECK(s.world.next_key == key + 1);
        }
        same_retry(io, op);
        CHECK(io.opens == 2);
        CHECK(s.resumes == (timeout ? 1u : 0u));
        CHECK(io.server.find_match("m1") != nullptr);
        CHECK(ba_session_close(&s));
        CHECK(io.streams.empty());
      }
    for (unsigned op : {0x12u, 0x14u}) {
      Service io;
      ba_session s{};
      setup(io, s);
      if (op == 0x14)
        run(s, BA_LOAD);
      io.drop_write = op;
      io.expire_on_drop = true;
      uint8_t orders[] = {8, 8, 8};
      ba_session_run(&s, op == 0x12 ? BA_LOAD : BA_SUBMIT, 0, 1, orders);
      CHECK(s.result == 100 + BN_ERROR_SESSION);
      CHECK(s.client.pending);
      CHECK(!bn_reconnect(&s.client, io.ticks));
      auto requests = io.requests.size();
      auto key = s.world.next_key;
      ba_session_run(&s, BA_SUBMIT, 0, 1, orders);
      CHECK(s.result == 100 + BN_ERROR_SESSION);
      CHECK(s.world.next_key == key && io.requests.size() == requests);
      ba_session_run(&s, BA_LOAD, 0, 0, nullptr);
      CHECK(s.result == 208 && io.requests.size() == requests);
      unsigned n = 0;
      for (auto &r : io.requests)
        if (r[0] == op)
          ++n;
      CHECK(n == 1);
      CHECK(ba_session_close(&s));
    }
    {
      Service io;
      ba_session s{};
      setup(io, s);
      run(s, BA_LOAD);
      io.stalled = true;
      auto before = io.ticks;
      ba_session_run(&s, BA_EVENTS, 0, 0, nullptr);
      CHECK(s.result == 100 + BN_ERROR_TIMEOUT);
      CHECK(io.ticks - before >= 60000 && io.ticks - before < 60100);
      CHECK(s.resumes == 1);
      CHECK(ba_session_close(&s));
    }
    {
      Service io;
      ba_session s{};
      setup(io, s);
      run(s, BA_LOAD);
      io.stalled = true;
      io.cancel_at = io.ticks + 100;
      ba_session_run(&s, BA_EVENTS, 0, 0, nullptr);
      CHECK(s.result == BA_CANCELLED);
      CHECK(s.client.pending);
      io.close_failures = 2;
      CHECK(ba_session_close(&s));
      CHECK(s.close_attempts == 3);
      CHECK(!s.client.handle);
    }
    {
      Service io;
      ba_session s{};
      setup(io, s);
      run(s, BA_LOAD);
      auto handle = s.client.handle;
      io.close_failures = 99;
      CHECK(!ba_session_close(&s));
      CHECK(s.close_attempts == 3);
      CHECK(s.client.handle == handle);
      CHECK(io.streams.count(handle));
      io.close_failures = 0;
      CHECK(ba_session_close(&s));
    }
    {
      Service io;
      ba_session s{};
      setup(io, s, 200, 0);
      run(s, BA_LOAD);
      ba_world shown = s.world;
      ba_replay r{};
      ba_replay_snapshot(&r, s.notice.high);
      auto baseline = r.cursor;
      uint8_t orders[] = {1, 7, 8};
      run(s, BA_SUBMIT, 0, 1, orders);
      run(s, BA_SUBMIT, 0, 2, orders);
      unsigned events = drain(s, r, shown);
      CHECK(events > 0);
      CHECK(r.cursor > baseline && r.ack == r.cursor);
      // Eventless SUBMITs while waiting for other seats are covered by A1b.
      // Accumulate >512 event-bearing versions without consuming history.
      auto ack = r.ack;
      for (unsigned round = 3; round <= 150; ++round)
        run(s, BA_SUBMIT, 0, round, orders);
      ba_session_run(&s, BA_EVENTS, r.cursor, 0, nullptr);
      CHECK(s.result == 15);
      CHECK(r.ack == ack);
      run(s, BA_SNAPSHOT);
      CHECK(!ba_snapshot_ready(&r, 1, s.notice.high));
      shown = s.world;
      ba_replay_snapshot(&r, s.notice.high);
      CHECK(r.ack == ack);
      CHECK(drain(s, r, shown) == 0);
      CHECK(r.ack == r.cursor);
      CHECK(ba_session_close(&s));
    }
    {
      Service io;
      ba_session s{};
      setup(io, s, 4, 20);
      run(s, BA_LOAD);
      ba_world shown = s.world;
      ba_replay r{};
      ba_replay_snapshot(&r, s.notice.high);
      io.clock.advance_ms(20001);
      io.server.poll();
      run(s, BA_EVENTS, r.cursor);
      auto high = s.notice.high;
      io.clock.advance_ms(20001);
      io.server.poll();
      unsigned count = 0;
      for (;;) {
        CHECK(ba_replay_page(&r, s.notice.page, s.notice.size, s.notice.apply));
        while (r.active) {
          int e = ba_replay_next(&r, &shown);
          CHECK(e >= 0);
          count += e > 0;
        }
        if (r.final)
          break;
        run(s, BA_FETCH);
      }
      CHECK(count > 0 && r.cursor == high);
      run(s, BA_SNAPSHOT);
      CHECK(!ba_snapshot_ready(&r, 1, s.notice.high));
      auto held = s.notice.high;
      CHECK(drain(s, r, shown) > 0);
      CHECK(r.cursor >= held);
      CHECK(ba_snapshot_ready(&r, 1, held));
      CHECK(ba_session_close(&s));
    }
    {
      Service io;
      ba_session s{};
      setup(io, s, 3, 0, 0);
      ba_session_run(&s, BA_LOAD, 0, 0, nullptr);
      CHECK(s.result == 11 && s.world.map_valid);
      for (int i = 0; i < 3; ++i)
        CHECK(
            io.server.handle(query("JOIN_MATCH", "m1")).get_bool("ok").value());
      run(s, BA_SNAPSHOT);
      ba_world shown = s.world;
      ba_replay r{};
      ba_replay_snapshot(&r, s.notice.high);
      auto baseline = r.cursor;
      uint8_t orders[] = {8, 8, 8};
      run(s, BA_SUBMIT, 0, 1, orders);
      CHECK(s.notice.accepted);
      io.drop_read = true;
      CHECK(drain(s, r, shown) == 0);
      CHECK(io.replays == 1 && r.cursor > baseline && r.ack == r.cursor);
      CHECK(ba_session_close(&s));
    }
    std::cout << "A1c session: lost JOIN/SUBMIT, timeout resume, expiry, "
                 "cancellation, failed Close, retained history and advancing "
                 "immutable replay PASS\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << "\n";
    return 1;
  }
}
