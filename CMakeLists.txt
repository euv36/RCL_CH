cmake_minimum_required(VERSION 3.25)
project(camera_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Warnings.cmake)
include(cmake/Dependencies.cmake)
include(cmake/LaptopDependencies.cmake)

enable_testing()
add_subdirectory(src)

add_executable(kCameraCppLaptopDependencyGateTest tests/laptop/dependency_gate_test.cpp)
target_link_libraries(kCameraCppLaptopDependencyGateTest PRIVATE camera_warnings)
target_compile_definitions(kCameraCppLaptopDependencyGateTest PRIVATE CAMERA_CPP_HAS_LAPTOP_DEPS=${CAMERA_CPP_LAPTOP_DEPS_VALUE})
add_test(NAME laptop_dependency_gate COMMAND kCameraCppLaptopDependencyGateTest)
