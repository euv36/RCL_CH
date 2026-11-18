#include "field_map.hpp"

#include <filesystem>
#include <fstream>
#include <cmath>
#include <nlohmann/json.hpp>

#include "../test_checks.hpp"

using camera_cpp::FieldMap;
using camera_cpp::FieldObservationInput;

static std::filesystem::path temp_path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

int main() {
    const auto path = temp_path("camera_cpp_field_map_test.json");
    std::filesystem::remove(path);

    FieldObservationInput measured;
    measured.enabled = true;
    measured.found = true;
    measured.angle_deg = 15.0F;
    measured.dist_cm = 150.0F;
    measured.reliable = true;
    measured.gate_id = 1;
    FieldObservationInput missing_gate;
    missing_gate.enabled = false;
    FieldMap empty;
    auto refused = empty.bind_from_results({}, 0.0F, 0.0F);
    CAMERA_TEST_CHECK(!refused.ok);
    CAMERA_TEST_CHECK(refused.warning == "no measured gates");

    {
        std::ofstream file(path, std::ios::binary);
        file << "{\"version\":1,\"unknown\":\"keep\",\"boundary\":{\"corner\":7},\"gates\":{}}";
    }
    FieldMap map;
    CAMERA_TEST_CHECK(map.load(path.string()));
    auto bound = map.bind_from_results({measured, missing_gate}, 0.0F, 0.0F);
    CAMERA_TEST_CHECK(bound.ok);
    CAMERA_TEST_CHECK(map.save_atomic());

    nlohmann::json after;
    {
        std::ifstream file(path, std::ios::binary);
        after = nlohmann::json::parse(file);
    }
    CAMERA_TEST_CHECK(after.at("unknown") == "keep");
    CAMERA_TEST_CHECK(after.at("boundary").at("corner") == 7);
    CAMERA_TEST_CHECK(after.at("gates").size() == 1);

    auto directions = map.estimated_directions(10.0F);
    CAMERA_TEST_CHECK(directions.size() == 1);
    CAMERA_TEST_CHECK(directions.begin()->second.estimated);
    CAMERA_TEST_CHECK(std::abs(directions.begin()->second.angle_deg - 25.0F) < 0.001F);

    std::filesystem::remove(path);
    return 0;
}
