#include "narration.h"
#include "botched/json.hpp"
#include "visual.h"
#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <vector>
static void check(bool x, const char *s) {
  if (!x)
    throw std::runtime_error(s);
}
int main(int argc, char **argv) {
  try {
    check(argc == 2, "fixture path");
    std::ifstream file(argv[1]);
    std::stringstream text;
    text << file.rdbuf();
    auto json = botched::Json::parse(text.str());
    check(bool(json), "fixture JSON");
    ba_narration story{};
    unsigned seen = 0, count = 0;
    std::vector<uint8_t> order, move, push;
    for (const auto &v : json->find("examples")->items()) {
      auto name = *v.get_string("name");
      if (name.rfind("EVENT-", 0) != 0)
        continue;
      std::istringstream hex(*v.get_string("hex"));
      unsigned n;
      std::vector<uint8_t> b;
      while (hex >> std::hex >> n)
        b.push_back(n);
      ba_narrate(&story, b.data(), b.size());
      auto c = ba_caption_at(&story, 0);
      check(c && c->event == b[2] && c->line[0][0], "event caption missing");
      check(strlen(c->line[0]) <= 52 && strlen(c->line[1]) <= 52,
            "caption width");
      if (b[2] == 26)
        check(story.phase == 2,
              "turret capture is machinery, not the last player order");
      seen |= 1u << (b[2] - 1);
      ++count;
      if (b[2] == 5)
        order = b;
      if (b[2] == 8)
        move = b;
      if (b[2] == 9)
        push = b;
      if (name == "EVENT-ROBOT_PUSHED")
        check(strstr(c->line[0], "P1 > PUSH P2") &&
                  strstr(c->line[1], "FACING KEPT"),
              "push actor/victim");
      if (name == "EVENT-CRATE_DELIVERED")
        check(strstr(c->line[0], "DELIVER +5") && strstr(c->line[1], "SCORE 5"),
              "delivery exact score");
      if (name == "EVENT-LASER_HIT")
        check(strstr(c->line[0], "LASER 2,3 > P1") && strstr(c->line[1], "1/3"),
              "laser source and durability");
      if (name == "EVENT-GROUP_TOGGLED")
        check(strstr(c->line[1], "GROUP Z REVERSED"), "belt group");
      if (name == "EVENT-TURRET_FIRED")
        check(strstr(c->line[0], "FIRE AT P2") &&
                  strstr(c->line[1], "0/3; OWNER P1"),
              "turret victim and damage");
      if (name == "EVENT-SCORE_AWARDED") {
        check(strstr(c->line[0], "SHUNT"), "authoritative SHUNT amount/reason");
      }
      ba_animation a{};
      ba_animation_begin(&a, b[2]);
      ba_animation_record(&a, b.data(), b.size());
      std::fill(b.begin(), b.end(), 0);
      check(c->line[0][0], "caption borrowed page bytes");
    }
    check(seen == 0x3fffffff && count >= 30, "all frozen event types covered");
    check(story.count == 16 && !ba_caption_at(&story, 16), "history bounded");
    for (int d : {-16, 16}) {
      ba_animation a{};
      ba_animation_begin(&a, 8);
      int prev = 100;
      for (unsigned frame = 0; frame < a.duration; ++frame) {
        int x = ba_lerp(100, 100 + d, &a);
        check(abs(x - prev) <= 2, "spatial step jumps");
        prev = x;
        ba_animation_tick(&a);
      }
      check(ba_lerp(100, 100 + d, &a) == 100 + d, "endpoint omitted");
    }
    story = {};
    ba_narrate(&story, order.data(), order.size());
    check(story.phase == 1, "order phase");
    ba_narrate(&story, push.data(), push.size());
    check(story.phase == 3, "push cause must not be fabricated");
    move.back() = 1;
    ba_narrate(&story, move.data(), move.size());
    check(story.phase == 2, "conveyor machinery phase");
    auto old = story;
    uint8_t bad[] = {21, 0};
    ba_narrate(&story, bad, 2);
    check(!memcmp(&story, &old, sizeof story),
          "invalid record changed history");
    ba_narration_refresh(&story);
    check(!story.phase && !story.actor &&
              strstr(ba_caption_at(&story, 0)->line[0], "REFRESHED"),
          "snapshot must not invent replay");
    check(sizeof story < 8192, "caption budget");
    std::cout << "PASS: " << count << " golden event variants; history "
              << sizeof story
              << " bytes, phases, signed motion and endpoints\n";
  } catch (const std::exception &e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
