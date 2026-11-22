cmake_minimum_required(VERSION 3.20)
project(botched LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(BOTCHED_BUILD_TESTS "Build unit tests" ON)
option(BOTCHED_BUILD_APPS "Build the headless server host and reference simulator executables" ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic -Wshadow)
endif()

# --- Deterministic rules library: no platform, rendering or network dependencies.
add_library(botched_core STATIC
  src/types.cpp
  src/json.cpp
  src/map.cpp
  src/state.cpp
  src/engine.cpp
  src/serialize.cpp
  src/validate.cpp
  src/match.cpp
)
target_include_directories(botched_core PUBLIC include)

# --- Portable, allocation-free payload codec. No transport or engine dependencies.
add_library(botched_wire STATIC wire/v1.c)
target_include_directories(botched_wire PUBLIC include)
set_target_properties(botched_wire PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)

# --- Server orchestration (rooms, sessions, API dispatch). Transport-agnostic.
add_library(botched_server STATIC
  server/server.cpp
  server/bot.cpp
  server/map_loader.cpp
  apps/ascii_render.cpp   # board rendering for --trace
)
target_include_directories(botched_server PUBLIC server apps)
target_link_libraries(botched_server PUBLIC botched_core)
# Last-resort default for the maps directory (the source tree); executables first look next to themselves.
target_compile_definitions(botched_server PRIVATE BOTCHED_DEFAULT_MAPS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/maps")

add_library(botched_binary STATIC server/binary_codec.cpp server/binary_bridge.cpp)
target_link_libraries(botched_binary PUBLIC botched_server botched_wire)

# --- POSIX reference apps: simulated players and the stdio server host.
add_library(botched_sim_lib STATIC
  apps/sim_runner.cpp
)
target_include_directories(botched_sim_lib PUBLIC apps)
target_link_libraries(botched_sim_lib PUBLIC botched_server)

# --- Minimal POSIX multiplayer MVP (M1-A): TCP host + terminal client. POSIX sockets only.
if(UNIX)
  add_library(botched_tcp_host STATIC server/tcp_host.cpp)
  target_include_directories(botched_tcp_host PUBLIC server)
  target_link_libraries(botched_tcp_host PUBLIC botched_server)

  add_library(botched_client_lib STATIC apps/client_core.cpp)
  target_include_directories(botched_client_lib PUBLIC apps server)
  target_link_libraries(botched_client_lib PUBLIC botched_sim_lib)
endif()

# --- Optional SDL2 graphical client (`botched-client --ui`). Built when SDL2 + SDL2_ttf are found.
option(BOTCHED_SDL "Build the SDL2 graphical client" ON)
set(BOTCHED_HAVE_SDL OFF)
if(UNIX AND BOTCHED_SDL)
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(BOTCHED_SDL2 IMPORTED_TARGET sdl2 SDL2_ttf)
  endif()
  if(BOTCHED_SDL2_FOUND)
    set(BOTCHED_HAVE_SDL ON)
  else()
    message(STATUS "SDL2/SDL2_ttf not found: botched-client --ui will be unavailable")
  endif()
endif()
add_library(botched_ui_model STATIC apps/ui_model.cpp apps/ui_playback.cpp)
target_include_directories(botched_ui_model PUBLIC apps)
target_compile_features(botched_ui_model PUBLIC cxx_std_20)
target_link_libraries(botched_ui_model PUBLIC botched_core)
if(BOTCHED_HAVE_SDL)
  add_library(botched_sdl_ui STATIC apps/sdl_ui.cpp)
  target_include_directories(botched_sdl_ui PUBLIC apps)
  target_link_libraries(botched_sdl_ui PUBLIC botched_client_lib botched_ui_model PkgConfig::BOTCHED_SDL2)
endif()

if(BOTCHED_BUILD_APPS)
  add_executable(botched-server apps/botched_server_main.cpp)
  target_link_libraries(botched-server PRIVATE botched_server)

  add_executable(botched-sim apps/botched_sim.cpp)
  target_link_libraries(botched-sim PRIVATE botched_sim_lib)

  if(UNIX)
    add_executable(botched-tcp-server apps/botched_tcpd_main.cpp)
    target_link_libraries(botched-tcp-server PRIVATE botched_tcp_host)
    add_executable(botched-client apps/botched_client_main.cpp)
    target_link_libraries(botched-client PRIVATE botched_client_lib)
    if(BOTCHED_HAVE_SDL)
      target_link_libraries(botched-client PRIVATE botched_sdl_ui)
      target_compile_definitions(botched-client PRIVATE BOTCHED_HAVE_SDL=1)
    endif()
  endif()
endif()

# --- Install: <prefix>/bin/<executables> and <prefix>/share/botched/maps/*.map. The servers find the maps
# next to themselves (../share/botched/maps), so an installed tree works from any working directory.
if(BOTCHED_BUILD_APPS)
  install(TARGETS botched-server botched-sim RUNTIME DESTINATION bin)
  if(UNIX)
    install(TARGETS botched-tcp-server botched-client RUNTIME DESTINATION bin)
  endif()
endif()
install(DIRECTORY resources/maps DESTINATION share/botched FILES_MATCHING PATTERN "*.map")

if(BOTCHED_BUILD_TESTS)
  enable_testing()
  file(GLOB BOTCHED_TEST_SOURCES CONFIGURE_DEPENDS tests/test_*.cpp)
  add_executable(botched_tests tests/test_main.cpp ${BOTCHED_TEST_SOURCES})
  target_include_directories(botched_tests PRIVATE tests tests/third_party)
  target_compile_definitions(botched_tests PRIVATE BOTCHED_WIRE_FIXTURES="${CMAKE_CURRENT_SOURCE_DIR}/docs/binary-v1/fixtures.json")
  target_compile_definitions(botched_tests PRIVATE BOTCHED_TEST_MAPS_DIR="${CMAKE_CURRENT_SOURCE_DIR}/resources/maps")
  target_link_libraries(botched_tests PRIVATE botched_sim_lib botched_ui_model botched_binary)
  if(UNIX)
    target_link_libraries(botched_tests PRIVATE botched_tcp_host botched_client_lib)
  endif()
  add_test(NAME botched_tests COMMAND botched_tests)
  add_executable(botched_wire_c_test tests/wire_c_test.c)
  set_target_properties(botched_wire_c_test PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED YES C_EXTENSIONS NO)
  target_link_libraries(botched_wire_c_test PRIVATE botched_wire)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(botched_wire_c_test PRIVATE -Werror -pedantic-errors -Wvla)
    if(UNIX AND NOT APPLE)
      target_compile_definitions(botched_wire_c_test PRIVATE BW_WRAP_ALLOC=1)
      target_link_options(botched_wire_c_test PRIVATE -Wl,--wrap=malloc -Wl,--wrap=calloc -Wl,--wrap=realloc -Wl,--wrap=free)
    endif()
  endif()
  add_test(NAME botched_wire_c_test COMMAND botched_wire_c_test)
endif()
