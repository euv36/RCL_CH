// pi_core/self_mask.cpp — see self_mask.hpp (Idea 8, pure logic).
#include "self_mask.hpp"

#include <cmath>

namespace pi_core {

SelfMaskParams resolve_self_mask(int width, int height, bool enabled,
                                 double cx, double cy, double r, double r2) {
    const int w = width < 1 ? 1 : width;
    const int h = height < 1 ? 1 : height;

    auto fix = [](double v) {
        return (std::isfinite(v) && v > 0.0) ? v : 0.0;
    };

    SelfMaskParams p;
    p.enabled = enabled;
    const double cxv = fix(cx);
    const double cyv = fix(cy);
    const double rv = fix(r);
    p.cx = cxv > 0.0 ? cxv : kSelfMaskCXFrac * static_cast<double>(w);
    p.cy = cyv > 0.0 ? cyv : kSelfMaskCYFrac * static_cast<double>(h);
    p.r = rv > 0.0 ? rv
                   : kSelfMaskRFrac *
                         static_cast<double>(w < h ? w : h);  // min(w, h)
    // Annulus with the outer radius inside the inner one is a config
    // mistake, not a crash — collapse to a plain disk (Python make_params).
    p.r2 = (std::isfinite(r2) && r2 > p.r) ? r2 : 0.0;
    if (!(p.r > 0.0)) p.enabled = false;  // unreachable via defaults, guard
    return p;
}

bool self_mask_excludes(const SelfMaskParams& p, double px, double py) {
    if (!p.enabled) return false;
    const double dx = px - p.cx;
    const double dy = py - p.cy;
    const double d2 = dx * dx + dy * dy;
    if (d2 <= p.r * p.r) return true;
    if (p.r2 > 0.0 && d2 >= p.r2 * p.r2) return true;
    return false;
}

void build_keep_mask(const SelfMaskParams& p, int width, int height,
                     std::uint8_t* buf) {
    const int w = width < 1 ? 1 : width;
    const int h = height < 1 ? 1 : height;
    if (buf == nullptr) return;
    if (!p.enabled || !(p.r > 0.0)) {
        // Nothing excluded — the whole frame stays searchable.
        for (int i = 0; i < w * h; ++i) buf[i] = 255;
        return;
    }
    const double r2v = p.r * p.r;
    const double r2o = p.r2 * p.r2;  // r2 == 0 -> outer test disabled below
    const bool annulus = p.r2 > 0.0;
    // Identical math to Python build_keep_mask (bit-exact parity):
    // keep iff d2 > r^2 and (no annulus or d2 < r2^2).
    for (int y = 0; y < h; ++y) {
        const double dy = static_cast<double>(y) - p.cy;
        const double dy2 = dy * dy;
        std::uint8_t* row = buf + static_cast<size_t>(y) * w;
        for (int x = 0; x < w; ++x) {
            const double dx = static_cast<double>(x) - p.cx;
            const double d2 = dx * dx + dy2;
            const bool keep =
                (d2 > r2v) && (!annulus || d2 < r2o);
            row[x] = keep ? 255 : 0;
        }
    }
}

}  // namespace pi_core
