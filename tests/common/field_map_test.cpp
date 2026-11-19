#include "field_map.hpp"

#include <filesystem>
#include <fstream>
#include <cmath>
#include <limits>
#include <string>
#include <nlohmann/json.hpp>

#include "../test_checks.hpp"

using camera_cpp::FieldMap;
using camera_cpp::FieldObservationInput;

static std::filesystem::path temp_path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

static void write_text(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary);
    file << text;
}

static nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    return nlohmann::json::parse(file);
}

int main() {
    const auto path = temp_path("camera_cpp_field_map_test.json");
    const auto invalid_path = temp_path("camera_cpp_field_map_invalid_test.json");
    const auto extra_path = temp_path("camera_cpp_field_map_extra_test.json");
    std::filesystem::remove(path);
    std::filesystem::remove(invalid_path);
    std::filesystem::remove(extra_path);

    FieldObservationInput measured;
    measured.slot = 0;
    measured.enabled = true;
    measured.found = true;
    measured.angle = 15.0F;
    measured.dist_cm = 150.0F;
    measured.held = false;
    FieldObservationInput missing_gate;
    missing_gate.slot = 1;
    missing_gate.enabled = false;
    FieldMap empty;
    auto refused = empty.bind_from_results({}, 0.0F, 0.0F);
    CAMERA_TEST_CHECK(!refused.ok);
    CAMERA_TEST_CHECK(refused.warning == "no measured gates");

    write_text(path,
               "{\"version\":1,\"unknown\":\"keep\",\"boundary\":{\"corner\":7},\"gates\":{}}");
    FieldMap map;
    CAMERA_TEST_CHECK(map.load(path.string()));
    auto bound = map.bind_from_results({measured, missing_gate}, 0.0F, 0.0F);
    CAMERA_TEST_CHECK(bound.ok);
    CAMERA_TEST_CHECK(map.save_atomic());

    nlohmann::json after = read_json(path);
    CAMERA_TEST_CHECK(after.at("unknown") == "keep");
    CAMERA_TEST_CHECK(after.at("boundary").at("corner") == 7);
    CAMERA_TEST_CHECK(after.at("gates").size() == 1);
    CAMERA_TEST_CHECK(!after.contains("field_width_cm"));
    CAMERA_TEST_CHECK(!after.contains("gate_width_cm"));
    CAMERA_TEST_CHECK(!after.contains("gate_height_cm"));
    CAMERA_TEST_CHECK(!after.contains("heading_delta_deg"));
    CAMERA_TEST_CHECK(!after.at("gates").at("1").contains("enabled"));
    CAMERA_TEST_CHECK(!after.at("gates").at("1").contains("found"));
    CAMERA_TEST_CHECK(!after.at("gates").at("1").contains("held"));
    CAMERA_TEST_CHECK(after.at("bound_at").is_string());
    CAMERA_TEST_CHECK(std::abs(after.at("gates").at("1").at("angle_deg").get<float>() - 15.0F) < 0.001F);
    CAMERA_TEST_CHECK(std::abs(after.at("gates").at("1").at("dist_cm").get<float>() - 150.0F) < 0.001F);
    CAMERA_TEST_CHECK(after.at("gates").at("1").at("estimated") == false);

    // Heading delta is subtracted: 15 - 10 = 5, not 25.
    auto directions = map.estimated_directions(10.0F);
    CAMERA_TEST_CHECK(directions.size() == 1);
    CAMERA_TEST_CHECK(directions.begin()->second.estimated);
    CAMERA_TEST_CHECK(std::abs(directions.begin()->second.angle_deg - 5.0F) < 0.001F);
    CAMERA_TEST_CHECK(std::abs(directions.begin()->second.dist_cm - 150.0F) < 0.001F);

    // bind_from_results must not write bound_at when there are no gates.
    write_text(extra_path, "{\"version\":1,\"bound_at\":null,\"gates\":{}}");
    FieldMap unbound;
    CAMERA_TEST_CHECK(unbound.load(extra_path.string()));
    CAMERA_TEST_CHECK(!unbound.bind_from_results({}, 0.0F, 0.0F).ok);
    CAMERA_TEST_CHECK(unbound.save_atomic());
    CAMERA_TEST_CHECK(read_json(extra_path).at("bound_at").is_null());

    // Version != 1, corrupt JSON and missing files are rejected.
    write_text(invalid_path, "{\"version\":2,\"gates\":{}}");
    FieldMap wrong_version;
    CAMERA_TEST_CHECK(!wrong_version.load(invalid_path.string()));
    write_text(invalid_path, "{\"version\":1,\"gates\":");
    FieldMap corrupt;
    CAMERA_TEST_CHECK(!corrupt.load(invalid_path.string()));
    std::filesystem::remove(invalid_path);
    FieldMap missing;
    CAMERA_TEST_CHECK(!missing.load(invalid_path.string()));

    // Distances <= 0 or non-finite become null; above 500 cm is capped.
    FieldObservationInput zero_dist = measured;
    zero_dist.slot = 1;
    zero_dist.dist_cm = 0.0F;
    FieldObservationInput nan_dist = measured;
    nan_dist.slot = 2;
    nan_dist.dist_cm = std::numeric_limits<float>::quiet_NaN();
    FieldObservationInput capped_dist = measured;
    capped_dist.slot = 0;
    capped_dist.dist_cm = 900.0F;
    write_text(extra_path, "{\"version\":1,\"bound_at\":null,\"gates\":{}}");
    FieldMap distances;
    CAMERA_TEST_CHECK(distances.load(extra_path.string()));
    CAMERA_TEST_CHECK(distances.bind_from_results({capped_dist, zero_dist, nan_dist}, 0.0F, 0.0F).ok);
    CAMERA_TEST_CHECK(distances.save_atomic());
    nlohmann::json saved_distances = read_json(extra_path);
    CAMERA_TEST_CHECK(saved_distances.at("bound_at").is_string());
    CAMERA_TEST_CHECK(std::abs(saved_distances.at("gates").at("1").at("dist_cm").get<float>() - 500.0F) < 0.001F);
    CAMERA_TEST_CHECK(saved_distances.at("gates").at("2").at("dist_cm").is_null());
    CAMERA_TEST_CHECK(saved_distances.at("gates").at("3").at("dist_cm").is_null());

    // load() normalizes gate angles into 0..360 and drops invalid records.
    write_text(extra_path,
               "{\"version\":1,\"bound_at\":\"2026-01-01T00:00:00\","
               "\"robot_pos\":\"center\",\"heading_offset_deg\":10,"
               "\"gates\":{\"1\":{\"angle_deg\":370,\"dist_cm\":900,\"estimated\":false},"
               "\"2\":{\"angle_deg\":\"bad\"},\"4\":{\"angle_deg\":5}}}");
    FieldMap normalized;
    CAMERA_TEST_CHECK(normalized.load(extra_path.string()));
    auto wrapped = normalized.estimated_directions(0.0F);
    CAMERA_TEST_CHECK(wrapped.size() == 1);
    CAMERA_TEST_CHECK(std::abs(wrapped.at(1).angle_deg - 10.0F) < 0.001F);
    CAMERA_TEST_CHECK(std::abs(wrapped.at(1).dist_cm - 500.0F) < 0.001F);

    // Subtraction wraps into 0..360; non-finite delta is rejected.
    auto subtracted = normalized.estimated_directions(15.0F);
    CAMERA_TEST_CHECK(std::abs(subtracted.at(1).angle_deg - 355.0F) < 0.001F);
    auto nonfinite = normalized.estimated_directions(std::numeric_limits<float>::quiet_NaN());
    CAMERA_TEST_CHECK(std::abs(nonfinite.at(1).angle_deg - 10.0F) < 0.001F);

    // bind_from_results normalizes negative angles too (slot 0 -> gate 1).
    FieldObservationInput negative_angle = measured;
    negative_angle.angle = -10.0F;
    negative_angle.held = true;
    write_text(extra_path, "{\"version\":1,\"bound_at\":null,\"gates\":{}}");
    FieldMap angle_map;
    CAMERA_TEST_CHECK(angle_map.load(extra_path.string()));
    CAMERA_TEST_CHECK(angle_map.bind_from_results({negative_angle}, 0.0F, 0.0F).ok);
    CAMERA_TEST_CHECK(angle_map.save_atomic());
    CAMERA_TEST_CHECK(std::abs(read_json(extra_path).at("gates").at("1").at("angle_deg").get<float>() - 350.0F) < 0.001F);
    CAMERA_TEST_CHECK(read_json(extra_path).at("gates").at("1").at("estimated") == true);

    std::filesystem::remove(path);
    std::filesystem::remove(invalid_path);
    std::filesystem::remove(extra_path);
    return 0;
}
