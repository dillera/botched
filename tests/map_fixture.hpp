// Test access to the map resources: the training-01 resource file is the single definition of the
// spec section 7 map (it used to be compiled into the library).
#pragma once

#include <fstream>
#include <iterator>
#include <string>

#include "doctest.h"
#include "map_loader.hpp"

#ifndef BOTCHED_TEST_MAPS_DIR
#error "BOTCHED_TEST_MAPS_DIR must point at resources/maps"
#endif

namespace botched {

inline std::string read_map_resource(const std::string& file) {
    std::ifstream f(std::string(BOTCHED_TEST_MAPS_DIR) + "/" + file, std::ios::binary);
    REQUIRE_MESSAGE(f.good(), "cannot open map resource " << file);
    return std::string((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

inline const std::string& training01_text() {
    static const std::string text = read_map_resource("training-01.map");
    return text;
}

// The map exactly as the server serves it (parsed and validated from the resource file).
inline const Map& training01() {
    static const Map m = [] {
        std::vector<std::string> errors;
        auto lm = server::parse_map_resource("training-01.map", training01_text(), errors);
        REQUIRE_MESSAGE(lm.has_value(), (errors.empty() ? "parse failed" : errors[0]));
        return *lm->map;
    }();
    return m;
}

}  // namespace botched
