#pragma once

#include <cstdint>

struct RgbLab {
    float L = 0.0f;
    float A = 0.0f;
    float B = 0.0f;
};

struct LabRange {
    float l_min = 0.0f;
    float l_max = 100.0f;
    float a_min = -128.0f;
    float a_max = 127.0f;
    float b_min = -128.0f;
    float b_max = 127.0f;
};

using rgb_lab = RgbLab;
using lab_range = LabRange;
using Lab = RgbLab;

RgbLab rgb_to_lab_u8(uint8_t r, uint8_t g, uint8_t b);
RgbLab lab_to_rgb_u8(float l, float a, float b);
bool lab_in_range(const RgbLab& c, const LabRange& r);
