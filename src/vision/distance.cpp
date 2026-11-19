#include "vision/distance.hpp"

#include <algorithm>
#include <cmath>

namespace camera_cpp {

const double kPi = 3.14159265358979323846;
const float kDistMinCm = 0.0F;
const float kDistMaxCm = 500.0F;

DistanceEstimator::DistanceEstimator(float gate_width_cm, float hold_seconds, float drop_fraction)
    : gate_width_cm_(gate_width_cm), hold_seconds_(hold_seconds), drop_fraction_(drop_fraction) {}

void DistanceEstimator::reset() {
    last_reliable_.reset();
}

DistResult DistanceEstimator::update(float pixel_radius, float frame_width, float gate_width_cm) {
    const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    return update(pixel_radius, frame_width, gate_width_cm, now);
}

// Оценивает расстояние до ворот по пиксельному радиусу с медианным сглаживанием.
// reliable=true только после накопления достаточного числа достоверных отсчётов.
DistResult DistanceEstimator::update(float pixel_radius, float frame_width, float gate_width_cm, double now_seconds) {
    const float width_cm = gate_width_cm < 0.0F ? gate_width_cm_ : gate_width_cm;
    if (!std::isfinite(frame_width) || frame_width <= 0.0F || !std::isfinite(width_cm) || width_cm <= 0.0F) {
        return DistResult{};
    }

    if (!std::isfinite(now_seconds)) {
        const auto now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        now_seconds = now;
    }

    const auto held_result = [&]() -> DistResult {
        if (last_reliable_ && std::isfinite(now_seconds) && now_seconds - last_reliable_->now_seconds <= hold_seconds_) {
            return DistResult{last_reliable_->cm, false};
        }
        return DistResult{};
    };

    if (!std::isfinite(pixel_radius) || pixel_radius <= 0.0F) {
        return held_result();
    }

    if (last_reliable_ && pixel_radius < last_reliable_->radius * drop_fraction_) {
        return held_result();
    }

    const float cm = width_cm * (frame_width / static_cast<float>(kPi)) / std::max(pixel_radius, 1.0F);
    if (!std::isfinite(cm) || cm < kDistMinCm) {
        return held_result();
    }
    if (cm > kDistMaxCm) {
        return DistResult{kDistMaxCm, false};
    }

    last_reliable_ = Sample{pixel_radius, cm, now_seconds};
    return DistResult{cm, true};
}

} // namespace camera_cpp
