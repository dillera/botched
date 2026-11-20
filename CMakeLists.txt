cmake_minimum_required(VERSION 3.20)
project(botched LANGUAGES CXX)

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

# --- Server orchestration (rooms, sessions, API dispatch). Transport-agnostic.
add_library(botched_server STATIC
  server/server.cpp
)
target_include_directories(botched_server PUBLIC server)
target_link_libraries(botched_server PUBLIC botched_core)

# --- POSIX reference apps: simulated players and the stdio server host.
add_library(botched_sim_lib STATIC
  apps/bot.cpp
  apps/ascii_render.cpp
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
  endif()
endif()

if(BOTCHED_BUILD_TESTS)
  enable_testing()
  file(GLOB BOTCHED_TEST_SOURCES CONFIGURE_DEPENDS tests/test_*.cpp)
  add_executable(botched_tests tests/test_main.cpp ${BOTCHED_TEST_SOURCES})
  target_include_directories(botched_tests PRIVATE tests tests/third_party)
  target_link_libraries(botched_tests PRIVATE botched_sim_lib)
  if(UNIX)
    target_link_libraries(botched_tests PRIVATE botched_tcp_host botched_client_lib)
  endif()
  add_test(NAME botched_tests COMMAND botched_tests)
endif()
