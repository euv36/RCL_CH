cmake_minimum_required(VERSION 3.25)
project(camera_cpp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(cmake/Warnings.cmake)
include(cmake/Dependencies.cmake)

enable_testing()
add_subdirectory(src)
