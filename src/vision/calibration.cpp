#include "vision/calibration.hpp"

#include <cmath>

namespace camera_cpp {

bool calibration_model_valid(const CalibrationModel& model) {
    return std::isfinite(model.min_radius_px) && std::isfinite(model.max_radius_px) &&
           std::isfinite(model.min_distance_cm) && std::isfinite(model.max_distance_cm) &&
           model.min_radius_px > 0.0F && model.max_radius_px >= model.min_radius_px &&
           model.min_distance_cm >= 0.0F && model.max_distance_cm > model.min_distance_cm;
}

} // namespace camera_cpp
