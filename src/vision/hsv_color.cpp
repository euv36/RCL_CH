#include "hsv_color.hpp"

#include <algorithm>
#include <cmath>

namespace {
constexpr float kDelta = 6.0f / 29.0f;
constexpr float kDelta2 = kDelta * kDelta;
constexpr float kDelta3 = kDelta2 * kDelta;
constexpr float kWhiteX = 0.95047f;
constexpr float kWhiteY = 1.00000f;
constexpr float kWhiteZ = 1.08883f;

float linearize(float c) {
    return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
}

float delinearize(float c) {
    c = std::clamp(c, 0.0f, 1.0f);
    return c <= 0.0031308f ? 12.92f * c : 1.055f * std::pow(c, 1.0f / 2.4f) - 0.055f;
}

float lab_f(float t) {
    return t > kDelta3 ? std::pow(t, 1.0f / 3.0f) : (t / (3.0f * kDelta2)) + (4.0f / 29.0f);
}

float lab_f_inverse(float t) {
    return t > kDelta ? t * t * t : 3.0f * kDelta2 * (t - 4.0f / 29.0f);
}

float clamp_component(float value) {
    return std::clamp(value, -128.0f, 127.0f);
}

uint8_t round_clamp_u8(float value) {
    return static_cast<uint8_t>(std::clamp(std::lround(value * 255.0f), 0L, 255L));
}
}

RgbLab rgb_to_lab_u8(uint8_t r, uint8_t g, uint8_t b) {
    const float R = linearize(static_cast<float>(r) / 255.0f);
    const float G = linearize(static_cast<float>(g) / 255.0f);
    const float B = linearize(static_cast<float>(b) / 255.0f);

    // D65, sRGB adaptation, matching OpenCV's normal RGB2Lab math.
    const float X = (0.4124564f * R + 0.3575761f * G + 0.1804375f * B) / kWhiteX;
    const float Y = (0.2126729f * R + 0.7151522f * G + 0.0721750f * B) / kWhiteY;
    const float Z = (0.0193339f * R + 0.1191920f * G + 0.9503041f * B) / kWhiteZ;

    const float fx = lab_f(X);
    const float fy = lab_f(Y);
    const float fz = lab_f(Z);

    RgbLab out;
    out.L = std::clamp(116.0f * fy - 16.0f, 0.0f, 100.0f);
    out.A = clamp_component(500.0f * (fx - fy));
    out.B = clamp_component(200.0f * (fy - fz));
    return out;
}

RgbLab lab_to_rgb_u8(float l, float a, float b) {
    const float fy = (std::clamp(l, 0.0f, 100.0f) + 16.0f) / 116.0f;
    const float fx = fy + clamp_component(a) / 500.0f;
    const float fz = fy - clamp_component(b) / 200.0f;

    const float X = kWhiteX * lab_f_inverse(fx);
    const float Y = kWhiteY * lab_f_inverse(fy);
    const float Z = kWhiteZ * lab_f_inverse(fz);

    const float R = 3.2404542f * X - 1.5371385f * Y - 0.4985314f * Z;
    const float G = -0.9692660f * X + 1.8760108f * Y + 0.0415560f * Z;
    const float B = 0.0556434f * X - 0.2040259f * Y + 1.0572252f * Z;

    RgbLab out;
    out.L = round_clamp_u8(delinearize(R));
    out.A = round_clamp_u8(delinearize(G));
    out.B = round_clamp_u8(delinearize(B));
    return out;
}

bool lab_in_range(const RgbLab& c, const LabRange& r) {
    return c.L >= r.l_min && c.L <= r.l_max
        && c.A >= r.a_min && c.A <= r.a_max
        && c.B >= r.b_min && c.B <= r.b_max;
}
