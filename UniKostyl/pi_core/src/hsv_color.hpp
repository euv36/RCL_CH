// pi_core/hsv_color.hpp — pure-logic RGB->HSV conversion for one 8-bit pixel,
// bit-exact with OpenCV cv2.cvtColor(..., cv2.COLOR_RGB2HSV) scalar path
// (8-bit, hue range 180). Validated against cv2 on 200k random pixels
// (0 mismatches; see tests/test_logic.cpp --vectors for the on-Pi parity run).
//
// Why reimplement it: gate colors arrive as single RGB pixels from the laptop
// (5 Hz). Allocating a 1x1 cv::Mat + cvtColor per update would work, but a pure
// function keeps the gate-color path testable without OpenCV and allocation-free.
//
// Algorithm source: OpenCV modules/imgproc/src/color_hsv.simd.hpp, struct
// RGB2HSV_b, scalar tail (hrange=180), tables:
//   sdiv_table[i]  = round((255 << 12) / i)      (round-to-nearest, cvRound)
//   hdiv_table[i]  = round((180 << 12) / (6*i))
// Exact .5 fractions are impossible for i in 1..255 (255<<12 = 2^12*3*5*17,
// 180<<12/(6i) = 2^13*15/i: a .5 result would need the denominator to absorb
// 2^13 > 255), so int(x + 0.5) == cvRound(x) here.
#pragma once

#include <cstdint>

namespace pi_core {

struct Hsv {
    int h;  // 0..179 (OpenCV 8-bit hue scale)
    int s;  // 0..255
    int v;  // 0..255
};

// r,g,b in 0..255. Input order is RGB — the laptop sends RGB in the JSON.
inline Hsv rgb_to_hsv_8u(int r, int g, int b) {
    // Division tables, same construction as OpenCV TablesSingleton.
    struct Tables {
        int sdiv[256];
        int hdiv[256];
        Tables() {
            sdiv[0] = 0;
            hdiv[0] = 0;
            for (int i = 1; i < 256; ++i) {
                sdiv[i] = static_cast<int>((255 << 12) / (1.0 * i) + 0.5);
                hdiv[i] = static_cast<int>((180 << 12) / (6.0 * i) + 0.5);
            }
        }
    };
    static const Tables tables;  // magic statics: thread-safe, built once

    constexpr int kHsvShift = 12;

    int v = b;
    int vmin = b;
    if (v < g) v = g;
    if (v < r) v = r;
    if (vmin > g) vmin = g;
    if (vmin > r) vmin = r;

    int diff = v - vmin;  // 0..255, no overflow
    const int vr = (v == r) ? -1 : 0;
    const int vg = (v == g) ? -1 : 0;

    int s = (diff * tables.sdiv[v] + (1 << (kHsvShift - 1))) >> kHsvShift;
    int h = (vr & (g - b)) +
            (~vr & ((vg & (b - r + 2 * diff)) + (~vg & (r - g + 4 * diff))));
    h = (h * tables.hdiv[diff] + (1 << (kHsvShift - 1))) >> kHsvShift;
    h += h < 0 ? 180 : 0;  // hr = 180 for COLOR_RGB2HSV

    // Result range is proven 0..179 for h and 0..255 for s; saturate anyway
    // like OpenCV saturate_cast<uchar>.
    if (h < 0) h = 0;
    if (h > 255) h = 255;
    if (s < 0) s = 0;
    if (s > 255) s = 255;
    return Hsv{h, s, v};
}

}  // namespace pi_core
