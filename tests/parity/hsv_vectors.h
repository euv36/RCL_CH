#pragma once

#include <cstdint>

struct HsvVector {
    uint8_t r;
    uint8_t g;
    uint8_t b;
    int l;
    int a;
    int b_channel;
};

inline constexpr HsvVector kHsvVectors[] = {
    {0, 0, 0, 0, 0, 0},
    {255, 255, 255, 100, 0, 0},
    {0, 255, 0, 87, -86, 83},
    {255, 0, 0, 53, 80, 67},
    {255, 255, 0, 97, -22, 95},
    {0, 0, 255, 32, 79, -108},
    {255, 128, 0, 67, 43, 74},
};

inline constexpr int kHsvVectorCount = static_cast<int>(sizeof(kHsvVectors) / sizeof(kHsvVectors[0]));
