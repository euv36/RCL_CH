#pragma once

#include <chrono>
#include <optional>

namespace camera_cpp {

extern const double kPi;
extern const float kDistMinCm;
extern const float kDistMaxCm;

struct DistModel {
    float d = 0.0F;
    float a = 0.0F;
    float b = 0.0F;
    float c = 0.0F;
    bool reliable = false;
};

struct DistResult {
    float cm = 0.0F;
    bool reliable = false;
};

class DistanceEstimator {
public:
    DistanceEstimator(float gate_width_cm = 45.0F, float hold_seconds = 2.0F, float drop_fraction = 0.6F);

    DistResult update(float pixel_radius, float frame_width, float gate_width_cm);
    DistResult update(float pixel_radius, float frame_width, float gate_width_cm, double now_seconds);
    void reset();

private:
    struct Sample {
        float radius = 0.0F;
        float cm = 0.0F;
        double now_seconds = 0.0;
    };

    float gate_width_cm_;
    float hold_seconds_;
    float drop_fraction_;
    std::optional<Sample> last_reliable_;
};

} // namespace camera_cpp
