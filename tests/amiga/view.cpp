#include "view.h"
#include "binary_codec.hpp"
#include "botched/engine.hpp"
#include "visual.h"
#include <cstring>
#include <iostream>
#include <stdexcept>
static void check(bool v, const char *why) {
  if (!v)
    throw std::runtime_error(why);
}
int main() {
  try {
    ba_world w{};
    w.width = 20;
    w.height = 16;
    w.tile_count = 320;
    w.valid = w.map_valid = 1;
    w.player = 1;
    w.players_count = 4;
    w.snapshot[1] = 1;
    w.players[0][0] = 1;
    w.players[0][2] = 19;
    w.players[0][3] = 15;
    auto before = w;
    ba_view v{};
    ba_editor e{{BA_EMPTY, BA_EMPTY, BA_EMPTY}, 0, 1, 0, 0};
    ba_view_sync(&v, &w);
    check(v.cx == 8 && v.cy == 6, "camera clamps to legal maximum map");
    ba_view_key(&v, &e, &w, 0x4d);
    check(e.orders[0] == BA_EMPTY && v.choice == 1, "browsing must not edit");
    ba_view_key(&v, &e, &w, 0x40);
    check(e.orders[0] == 1, "Space places browsed command");
    ba_view_key(&v, &e, &w, 0x42);
    ba_view_key(&v, &e, &w, 8);
    check(e.slot == 1 && e.orders[1] == 7 && v.choice == 7,
          "Tab and numeric shortcut");
    ba_view_key(&v, &e, &w, 0x51);
    auto saved = e;
    check(v.panel == 2 && !ba_view_key(&v, &e, &w, 0x44) &&
              !memcmp(&e, &saved, sizeof e),
          "score panel cannot submit");
    check(ba_view_key(&v, &e, &w, 0x45) == BA_CHANGED && !v.panel,
          "Escape closes panel first");
    ba_view_key(&v, &e, &w, 0x17);
    for (int i = 0; i < 32; ++i) {
      ba_view_key(&v, &e, &w, 0x4f);
      ba_view_key(&v, &e, &w, 0x4c);
    }
    check(v.x == 0 && v.y == 0 && v.cx == 0 && v.cy == 0,
          "inspection at origin");
    for (int i = 0; i < 32; ++i) {
      ba_view_key(&v, &e, &w, 0x4e);
      ba_view_key(&v, &e, &w, 0x4d);
    }
    check(v.x == 19 && v.y == 15 && v.cx == 8 && v.cy == 6,
          "inspection at far edge");
    ba_view_key(&v, &e, &w, 0x34);
    check(v.overview, "overview toggles");
    ba_view_key(&v, &e, &w, 0x17);
    ba_view_key(&v, &e, &w, 9);
    check((ba_view_key(&v, &e, &w, BA_LOCK_KEY) & BA_SEND) && e.pending,
          "submission begins pending");
    auto orders = e;
    ba_view_key(&v, &e, &w, 1);
    ba_view_key(&v, &e, &w, 0x40);
    check(!memcmp(&orders, &e, sizeof e), "pending programme immutable");
    e.pending = 0;
    e.locked = 1;
    orders = e;
    ba_view_key(&v, &e, &w, 2);
    check(!memcmp(&orders, &e, sizeof e), "locked programme immutable");
    {
      ba_editor keyboard{{BA_EMPTY, BA_EMPTY, BA_EMPTY}, 0, 1, 0, 0},
          mouse = keyboard;
      ba_view kv{}, mv{};
      check(!ba_view_key(&kv, &keyboard, &w, BA_LOCK_KEY),
            "incomplete keyboard Lock");
      check(!ba_view_click(&mv, &mouse, &w, 250, 214), "incomplete mouse Lock");
      uint8_t body[18];
      check(!ba_submit_body(body, 1, 1, keyboard.orders),
            "empty cannot reach wire");
      for (unsigned n : {9u, 2u, 8u}) {
        ba_view_key(&kv, &keyboard, &w, n);
        ba_view_click(&mv, &mouse, &w, 240, 54 + (n - 1) * 16 + 4);
        check(!memcmp(&keyboard, &mouse, sizeof mouse),
              "mouse/keyboard placement equivalence");
      }
      check(ba_complete(&mouse) && mouse.orders[0] == 8,
            "explicit WAIT and sequential fill");
      auto full = mouse;
      ba_view_click(&mv, &mouse, &w, 240, 58);
      check(!memcmp(&full, &mouse, sizeof mouse),
            "full palette cannot overwrite");
      ba_view_click(&mv, &mouse, &w, 80, 212);
      check(mouse.slot == 1 && mouse.orders[1] == BA_EMPTY,
            "occupied slot clears");
      ba_view_key(&kv, &keyboard, &w, 0x4f);
      ba_view_key(&kv, &keyboard, &w, 0x41);
      check(!memcmp(&keyboard, &mouse, sizeof mouse),
            "Backspace equals slot clear");
      kv.choice = 4;
      ba_view_key(&kv, &keyboard, &w, 0x44);
      ba_view_click(&mv, &mouse, &w, 240, 54 + 4 * 16 + 4);
      check(!memcmp(&keyboard, &mouse, sizeof mouse) && !keyboard.pending,
            "Enter places, never submits");
      check((ba_view_key(&kv, &keyboard, &w, BA_LOCK_KEY) & BA_SEND) &&
                (ba_view_click(&mv, &mouse, &w, 250, 214) & BA_SEND),
            "equivalent explicit Lock");
      full = mouse;
      check(!ba_view_click(&mv, &mouse, &w, 80, 212) &&
                !ba_view_click(&mv, &mouse, &w, 250, 214) &&
                !memcmp(&full, &mouse, sizeof mouse),
            "pending mouse immutable");
      check(ba_view_click(&mv, &mouse, &w, 12, 48) & BA_CHANGED,
            "board click inspection");
      check(mv.inspect && mv.x == mv.cx && mv.y == mv.cy, "detail coordinates");
      mv.overview = 1;
      check(ba_view_click(&mv, &mouse, &w, 175, 175) & BA_CHANGED,
            "overview board click");
      check(mv.x < w.width && mv.y < w.height, "overview bounds");
      mouse.pending = 0;
      w.snapshot[1] = 2;
      ba_editor_sync(&mouse, &w);
      check(!ba_complete(&mouse) && mouse.orders[0] == BA_EMPTY,
            "new round starts empty");
      w.snapshot[1] = 1;
    }
    for (unsigned n = 2; n <= 4; ++n)
      for (unsigned r = 1; r <= 200; ++r)
        for (unsigned t = 1; t <= 3; ++t) {
          auto actual =
              botched::priority_order(botched::absolute_tick(r, t), n);
          for (unsigned i = 0; i < n; ++i)
            check(ba_priority(r, t, n, i) == unsigned(actual[i]),
                  "preview disagrees with engine");
        }
    check(!ba_priority(0, 1, 4, 0) && !ba_priority(1, 1, 0, 0),
          "no priority before match");
    struct {
      uint32_t value;
      const char *label;
    } scores[] = {{0, "00"},   {9, "09"},    {10, "10"},        {99, "99"},
                  {100, "++"}, {1000, "++"}, {UINT32_MAX, "++"}};
    for (auto s : scores) {
      char full[11];
      ba_decimal(s.value, full);
      check(std::to_string(s.value) == full,
            "exact unsigned display including UINT32_MAX");
      char out[3];
      ba_score_text(s.value, out);
      check(!strcmp(out, s.label), "two-digit/overflow display");
      uint8_t wire[4] = {uint8_t(s.value), uint8_t(s.value >> 8),
                         uint8_t(s.value >> 16), uint8_t(s.value >> 24)};
      check(ba_u32(wire) == s.value, "exact value preserved for inspection");
    }
    for (unsigned i = 1; i <= 4; ++i)
      check(strlen(ba_player_label(i)) == 4, "label width");
    check(!memcmp(&w, &before, sizeof w),
          "view must never mutate authoritative mirror");
    {
      ba_world map{};
      map.width = 8;
      map.height = 8;
      map.tile_count = 64;
      auto tile = [&](unsigned x, unsigned y, unsigned t) {
        unsigned c = y * 8 + x;
        map.tiles[c / 2] |= t << ((c & 1) * 4);
      };
      tile(2, 2, 7);
      tile(4, 2, 1);
      map.attributes_count = 1;
      map.attributes[0][0] = 2;
      map.attributes[0][1] = 2;
      map.attributes[0][4] = 1;
      check(ba_related(&map, 2, 2, 3, 2) && !ba_related(&map, 2, 2, 5, 2) &&
                !ba_related(&map, 2, 2, 1, 2),
            "laser inspection follows facing and static walls");
      tile(2, 5, 11);
      tile(4, 5, 1);
      check(ba_related(&map, 2, 5, 3, 5) && !ba_related(&map, 2, 5, 4, 5) &&
                !ba_related(&map, 2, 5, 5, 5),
            "turret inspection bounded geometry");
      tile(6, 6, 10);
      check(ba_tarred(&map, 7, 7) && !ba_tarred(&map, 0, 0),
            "tar geometry and boundary");
    }
    {
      ba_world map{};
      map.width = 8;
      map.height = 8;
      map.tile_count = 64;
      auto tile = [&](unsigned x, unsigned y, unsigned t) {
        unsigned c = y * 8 + x, shift = (c & 1) * 4;
        map.tiles[c / 2] = (map.tiles[c / 2] & ~(15 << shift)) | (t << shift);
      };
      tile(3, 3, 9);
      map.attributes_count = 1;
      map.attributes[0][0] = 3;
      map.attributes[0][1] = 3;
      map.attributes[0][2] = 1;
      map.hazards_count = 1;
      map.hazards[0][0] = 1;
      map.hazards[0][1] = 1;
      uint8_t danger[320];
      ba_danger(&map, danger);
      check(!danger[2 * 8 + 2], "disarmed no AOE");
      map.hazards[0][2] = 1;
      ba_danger(&map, danger);
      unsigned count = 0;
      for (auto x : danger)
        count += !!(x & BA_BURST);
      check(count == 8 && !danger[3 * 8 + 3], "armed surrounding eight cells");
      tile(2, 2, 1);
      ba_danger(&map, danger);
      check(!danger[2 * 8 + 2] && (danger[4 * 8 + 4] & BA_BURST),
            "wall excluded, diagonal included");
      // A validated binary event, including rearming, drives presentation
      // state.
      map.valid = 1;
      auto event = [&](unsigned code) {
        botched::binary::Fields f;
        f.b(code).i(1).b(1).b(1).i(1);
        if (code == 24)
          f.b(1).b(3).b(3).b(0).b(1);
        else
          f.b(1).b(1).b(3).b(3).b(1).b(0).b(1);
        botched::binary::Records records;
        records.add(21, f.span());
        check(ba_event(&map, records.bytes().data(), records.bytes().size()),
              "valid hazard event");
        ba_danger(&map, danger);
      };
      event(24);
      check(!(danger[4 * 8 + 4] & BA_BURST), "trigger event removes AOE");
      event(23);
      check(danger[4 * 8 + 4] & BA_BURST, "arm event restores AOE");
      map.hazards[0][2] = 0;
      ba_danger(&map, danger);
      check(!danger[4 * 8 + 4], "disarmed AOE removed");
      tile(6, 6, 10);
      ba_danger(&map, danger);
      check((danger[7 * 8 + 7] & BA_TAR) && !danger[6 * 8 + 6],
            "persistent tar area");
      tile(1, 1, 7);
      tile(5, 1, 1);
      map.attributes_count = 2;
      map.attributes[1][0] = 1;
      map.attributes[1][1] = 1;
      map.attributes[1][4] = 1;
      ba_danger(&map, danger);
      check((danger[1 * 8 + 4] & BA_LASER_H) && !danger[1 * 8 + 5] &&
                !danger[1 * 8 + 6],
            "laser stops at obstacle");
      tile(4, 5, 11);
      ba_danger(&map, danger);
      check((danger[5 * 8 + 2] & BA_TURRET) && !(danger[5 * 8 + 1] & BA_TURRET),
            "turret range two");
    }
    for (unsigned y = 1; y <= 16; ++y)
      for (unsigned x = 1; x <= 20; ++x) {
        w.width = x;
        w.height = y;
        ba_view_center(&v, &w, 100, 100);
        check(v.cx == (x > 12 ? x - 12 : 0) && v.cy == (y > 10 ? y - 10 : 0),
              "all legal viewport dimensions");
      }
    for (unsigned k = 0; k < BA_ASSETS; ++k)
      for (unsigned y = 0; y < 16; ++y)
        for (unsigned x = 0; x < 16; ++x)
          check(ba_asset_pixel(k, x, y) < 16, "OCS colour bound");
    check(ba_asset_pixel(BA_ASSETS, 0, 0) == 0 && ba_asset_pixel(1, 16, 0) == 0,
          "asset bounds");
    std::cout << "PASS: command discovery, pending/locked input, camera "
                 "bounds, 2/3/4-player rotation and exact scores\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
