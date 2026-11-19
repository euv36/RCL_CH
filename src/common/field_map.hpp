#pragma once

#include <filesystem>
#include <map>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace camera_cpp {

struct FieldObservationInput {
    int slot = -1;
    bool enabled = false;
    bool found = false;
    float angle = 0.0F;
    float dist_cm = 0.0F;
    bool held = false;
};

struct FieldBindResult {
    bool ok = false;
    std::string warning;
};

struct EstimatedDirection {
    float angle_deg = 0.0F;
    float dist_cm = 0.0F;
    bool estimated = true;
};

class FieldMap {
public:
    FieldMap(float field_width_cm = 4500.0F, float gate_width_cm = 450.0F, float gate_height_cm = 200.0F);

    bool load(const std::string& path);

    FieldBindResult bind_from_results(const std::vector<FieldObservationInput>& observations,
                                      float heading_offset_deg,
                                      float heading_delta_deg);
    bool save_atomic() const;
    std::map<int, EstimatedDirection> estimated_directions(float heading_delta_deg) const;

private:
    std::filesystem::path path_;
    nlohmann::json root_;
};

} // namespace camera_cpp
