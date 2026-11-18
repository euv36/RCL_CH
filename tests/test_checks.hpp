#pragma once

#include <cstdlib>
#include <iostream>

#define CAMERA_TEST_CHECK(condition) do { \
    if (!(condition)) { \
        std::cerr << "check failed: " #condition " at line " << __LINE__ << std::endl; \
        std::abort(); \
    } \
} while (0)
