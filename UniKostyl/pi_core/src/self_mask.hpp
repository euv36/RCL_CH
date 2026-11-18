// pi_core/self_mask.hpp — Idea 8 (PROMPT_EN.md §5): exclusion of the robot
// body from the color search area. PURE logic (no OpenCV/ZMQ) so it builds
// and unit-tests anywhere (pi_core_tests); gate_tracker.cpp wraps it into a
// cv::Mat and zeroes the per-gate masks before morphology/CC.
//
// The camera looks down at the robot: the green Raspberry Pi PCB and black
// carbon-fiber tubes sit in the center of view. The exclusion zone (a disk,
// optionally an annulus r..r2) is cut out of every per-gate mask right after
// inRange — no piece on the body can ever form a gate object.
//
// WHY the mask side (not zeroing HSV/BGR): a zeroed pixel is black
// (H=0, S=0, V=0) and a gate range containing 0 in any channel (dark colors
// have v_lo=0; hue wrap ranges include H=0; grey has s_lo=0) still MATCHES
// black — the carbon tubes would leak through exactly in the worst case.
// `d <= r` on the mask side is exact for any configured range.
//
// Units/defaults (mirror UniKostyl/self_mask.py 1-in-1):
//   * CLI flags --self-cx/--self-cy/--self-r/--self-r2 — PX in the working
//     frame, <= 0 = auto: frame center, r = 0.22 * min(w, h), no annulus;
//   * camera_settings.json section "self_mask" — FRACTIONS of width/height/
//     min(w,h) (one file serves the Pi 640x360 and the laptop 720x1280);
//     the fraction -> px conversion happens at the caller (C++ does not read
//     the settings file yet — Idea 1's C++ auto-apply is a known next step;
//     feed the resolved px into resolve_self_mask()).
// The reference pipeline used the annulus r 110..355 px (fractions ~0.15 /
// 0.49 at a 720 px min-dimension). ZONE_RADIUS_FRAC ("angle output zone")
// and the RING "cut center %" are DIFFERENT entities — never mixed here.
//
// Rasterization is deliberately the exact equation dx*dx + dy*dy (IEEE-754
// binary64, pixel (x, y) at integer coordinates), NOT cv::circle — identical
// math in Python/numpy and here gives a bit-exact keep mask (checked by
// out/selfmask_check.py vs pi_core_tests --selfmask-parity).
#pragma once

#include <cstdint>

namespace pi_core {

constexpr double kSelfMaskRFrac = 0.22;   // inner radius, fraction of min(w,h)
constexpr double kSelfMaskCXFrac = 0.5;   // default center (frame center)
constexpr double kSelfMaskCYFrac = 0.5;

struct SelfMaskParams {
    bool enabled = false;
    double cx = 0.0;   // px in the working frame
    double cy = 0.0;
    double r = 0.0;    // inner radius px (>0 when enabled)
    double r2 = 0.0;   // outer radius px; 0 = full disk (no annulus)
};

// CLI-semantics resolution (mirrors self_mask.py resolve_params): values
// <= 0 / non-finite are replaced by the defaults above; r2 <= r collapses
// to 0 (plain disk). `enabled` comes from the --no-self-mask flag.
SelfMaskParams resolve_self_mask(int width, int height, bool enabled,
                                 double cx, double cy, double r, double r2);

// PURE point test: pixel (px, py) at integer coordinates is inside the
// exclusion zone. Same equation as build_keep_mask / Python excluded().
bool self_mask_excludes(const SelfMaskParams& p, double px, double py);

// Builds the keep mask into buf (w*h bytes, row-major): 255 = searchable,
// 0 = excluded (robot body). Built ONCE per parameter/size change — never
// per frame; the per-frame cost is one cv::bitwise_and per gate.
void build_keep_mask(const SelfMaskParams& p, int width, int height,
                     std::uint8_t* buf);

}  // namespace pi_core
