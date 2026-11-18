#include "hsv_vectors.h"
#include "../../src/vision/hsv_color.hpp"

#include <cmath>
#include <iostream>

int main() {
    int failures = 0;
    for (int i = 0; i < kHsvVectorCount; ++i) {
        const auto& v = kHsvVectors[i];
        const auto lab = rgb_to_lab_u8(v.r, v.g, v.b);
        const bool ok = std::fabs(lab.L - static_cast<float>(v.l)) <= 2.0f
            && std::fabs(lab.A - static_cast<float>(v.a)) <= 2.0f
            && std::fabs(lab.B - static_cast<float>(v.b_channel)) <= 2.0f;
        if (!ok) {
            ++failures;
            std::cerr << "vector " << i << " failed: got L=" << lab.L << " A=" << lab.A << " B=" << lab.B << "\n";
        }
    }
    if (failures != 0) {
        std::cerr << failures << " parity vector(s) failed\n";
        return 1;
    }
    std::cout << kHsvVectorCount << " parity vectors passed\n";
    return 0;
}
