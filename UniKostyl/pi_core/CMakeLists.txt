# pi_core — C++ replacement of rpi_elp_cam.py (Idea 4).
#
# Build on the Pi (Debian 13):
#   sudo apt install -y build-essential cmake libopencv-dev libzmq3-dev
#   cd /home/twox/pi_core
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
#   cmake --build build -j 3
# Run the logic unit tests (no camera needed):
#   ./build/pi_core_tests
# Parity vs the Pi's real OpenCV:
#   python3 tests/parity_hsv.py > /tmp/hsv_vectors.txt
#   ./build/pi_core_tests --vectors /tmp/hsv_vectors.txt
# Smoke tests: see deploy.sh in this directory.
#
# Vendor headers (single-file, official releases):
#   vendor/nlohmann/json.hpp — nlohmann/json v3.12.0
#   vendor/zmq.hpp  — cppzmq v4.11.0
# libzmq itself (the compiled library) comes from libzmq3-dev on the Pi;
# OpenCV >= 4.5 from libopencv-dev.
cmake_minimum_required(VERSION 3.18)
project(pi_core CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
set(CMAKE_CXX_FLAGS_RELEASE "-O3")

find_package(OpenCV REQUIRED COMPONENTS core imgproc imgcodecs videoio)
find_library(ZMQ_LIBRARY NAMES zmq libzmq REQUIRED)
find_package(Threads REQUIRED)

# ---------------- main binary ----------------
add_executable(pi_core
  src/main.cpp
  src/log.cpp
  src/gate_logic.cpp
  src/gate_cluster.cpp
  src/self_mask.cpp
  src/distance.cpp
  src/gate_store.cpp
  src/color_message.cpp
  src/atomic_file.cpp
  src/config.cpp
  src/gate_tracker.cpp
  src/camera.cpp
  src/v4l2_ctl.cpp
  src/serial_link.cpp
  src/streamer.cpp
  src/color_listener.cpp
)
target_include_directories(pi_core PRIVATE src vendor)
target_link_libraries(pi_core PRIVATE ${OpenCV_LIBS} ${ZMQ_LIBRARY}
                                      Threads::Threads)

# ---------------- logic unit tests ----------------
# Deliberately does NOT link OpenCV/ZMQ: gate params, angle math, serial
# policy, HSV conversion, blob clustering (Idea 5), gate_store and
# colors-JSON must build anywhere.
add_executable(pi_core_tests
  tests/test_logic.cpp
  src/log.cpp
  src/gate_logic.cpp
  src/gate_cluster.cpp
  src/self_mask.cpp
  src/distance.cpp
  src/gate_store.cpp
  src/color_message.cpp
  src/atomic_file.cpp
  src/config.cpp
)
target_include_directories(pi_core_tests PRIVATE src vendor tests)
target_link_libraries(pi_core_tests PRIVATE Threads::Threads)
