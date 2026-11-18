# robot_core — Pi-side strategy core (PROMPT_ROBOT phase 1, Idea 12).
#
# Build on the Pi (Debian 13):
#   sudo apt install -y build-essential cmake libzmq3-dev
#   cd /home/twox/robot_core
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j 3
#   ./build/robot_core_tests            # pure-logic unit tests (no ZMQ needed)
#
# Windows (PC-first, PROMPT_ROBOT §7): only the pure logic + tests are built,
# with `python -m ziglang c++ -O2` — see README.md for the exact command; the
# Linux-only IO files (termios / i2c-dev / zmq) never compile on Windows.
#
# Vendor headers (single-file, copied from pi_core/vendor):
#   vendor/nlohmann/json.hpp — nlohmann/json v3.12.0
#   vendor/zmq.hpp           — cppzmq v4.11.0 (compiled lib = libzmq3-dev)
cmake_minimum_required(VERSION 3.18)
project(robot_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

find_package(Threads REQUIRED)

# ---------------- pure logic library (builds anywhere) ----------------
add_library(robot_core_lib STATIC
  src/config.cpp
  src/angles.cpp
  src/omni.cpp
  src/drive_limits.cpp
  src/heading_ctrl.cpp
  src/ball_logic.cpp
  src/line_logic.cpp
  src/gate_status.cpp
  src/link_codec.cpp
  src/tactics.cpp
)
target_include_directories(robot_core_lib PUBLIC src vendor)

# ---------------- logic unit tests (no OpenCV/ZMQ/termios) ----------------
# Deliberately links ONLY the pure library: same rule as pi_core_tests.
add_executable(robot_core_tests
  tests/test_logic.cpp
)
target_link_libraries(robot_core_tests PRIVATE robot_core_lib Threads::Threads)

# ---------------- main binary (Linux/Pi only) ----------------
# UNVERIFIED ON HW: termios/i2c-dev/zmq have never run on the real hardware.
if(UNIX AND NOT WIN32)
  find_library(ZMQ_LIBRARY NAMES zmq libzmq REQUIRED)
  add_executable(robot_core
    src/main.cpp
    src/uart_link_linux.cpp
    src/locator_i2c_linux.cpp
    src/zmq_status_sub.cpp
  )
  target_include_directories(robot_core PRIVATE src vendor)
  target_link_libraries(robot_core PRIVATE robot_core_lib ${ZMQ_LIBRARY}
                                           Threads::Threads)
endif()
