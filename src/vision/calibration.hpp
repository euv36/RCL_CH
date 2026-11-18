#pragma once

namespace camera_cpp {

struct CalibrationModel {
    float min_radius_px = 0.0F;
    float max_radius_px = 0.0F;
    float min_distance_cm = 0.0F;
    float max_distance_cm = 500.0F;
};

bool calibration_model_valid(const CalibrationModel& model);

} // namespace camera_cpp
