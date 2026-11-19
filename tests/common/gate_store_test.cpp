#include "gate_store.hpp"

#include <array>
#include <filesystem>
#include <fstream>
#include <string>
#include <nlohmann/json.hpp>

#include "../test_checks.hpp"

using camera_cpp::GateColor;
using camera_cpp::GateLab;
using camera_cpp::GateStore;

namespace {

std::filesystem::path temp_path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

void write_file(const std::filesystem::path& path, const std::string& text) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    CAMERA_TEST_CHECK(file.good());
    file << text;
    file.close();
    CAMERA_TEST_CHECK(file.good());
}

nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    CAMERA_TEST_CHECK(file.good());
    return nlohmann::json::parse(file);
}

bool iso_local_timestamp(const std::string& value) {
    if (value.size() != 19) {
        return false;
    }
    if (value[4] != '-' || value[7] != '-' || value[10] != 'T' ||
        value[13] != ':' || value[16] != ':') {
        return false;
    }
    for (std::size_t index = 0; index < value.size(); ++index) {
        if (index == 4 || index == 7 || index == 10 || index == 13 || index == 16) {
            continue;
        }
        if (value[index] < '0' || value[index] > '9') {
            return false;
        }
    }
    return true;
}

} // namespace

int main() {
    // Round-trip: roles, unknown keys, only slots 1..3, native enabled default.
    {
        const auto path = temp_path("camera_cpp_gate_store_test.json");
        std::filesystem::remove(path);
        write_file(path,
            "{\"version\": 1, \"future_root\": {\"keep\": true}, "
            "\"gates\": {"
            "\"1\": {\"rgb\": [1, 2, 3], \"enabled\": true, \"role\": \"ours\", "
            "\"future_slot\": 77, \"lab\": [10, 90, -5, 5, -6, 6], "
            "\"ts\": \"2026-11-17T19:00:00\"}, "
            "\"2\": {\"rgb\": [4, 5, 6], \"future_slot\": 78}, "
            "\"3\": {\"rgb\": [7, 8, 9], \"role\": \"opponent\"}, "
            "\"4\": {\"rgb\": [9, 9, 9]}}}");

        GateStore store;
        CAMERA_TEST_CHECK(store.load(path.string()));
        CAMERA_TEST_CHECK(store.roles()[0] == "ours");
        CAMERA_TEST_CHECK(store.roles()[1] == "unset");
        CAMERA_TEST_CHECK(store.roles()[2] == "opponent");
        CAMERA_TEST_CHECK(store.enabled(GateId::One));
        CAMERA_TEST_CHECK(!store.enabled(GateId::Two)); // missing enabled -> false
        CAMERA_TEST_CHECK(!store.gate(static_cast<GateId>(4)).has_value());

        const auto first = store.gate(GateId::One);
        CAMERA_TEST_CHECK(first.has_value());
        CAMERA_TEST_CHECK(first->rgb == (std::array<int, 3>{{1, 2, 3}}));
        CAMERA_TEST_CHECK(first->lab.has_value());
        CAMERA_TEST_CHECK(*first->lab == (GateLab{{10, 90, -5, 5, -6, 6}}));
        CAMERA_TEST_CHECK(first->ts == "2026-11-17T19:00:00");

        store.upsert(GateId::Two, GateColor{12, 34, 56}, "opponent");
        CAMERA_TEST_CHECK(store.save_atomic());

        const auto after = read_json(path);
        CAMERA_TEST_CHECK(after.at("version") == 1);
        CAMERA_TEST_CHECK(after.at("future_root").at("keep") == true);
        CAMERA_TEST_CHECK(!after.at("gates").contains("4"));
        CAMERA_TEST_CHECK(after.at("gates").at("1").at("future_slot") == 77);
        CAMERA_TEST_CHECK(after.at("gates").at("1").at("lab").is_array());
        CAMERA_TEST_CHECK(after.at("gates").at("2").at("future_slot") == 78);
        CAMERA_TEST_CHECK(after.at("gates").at("2").at("enabled") == true);
        CAMERA_TEST_CHECK(after.at("gates").at("2").at("role") == "opponent");
        CAMERA_TEST_CHECK(after.at("gates").at("2").at("rgb").at(0) == 12);
        CAMERA_TEST_CHECK(after.at("gates").at("2").at("lab").is_null());
        CAMERA_TEST_CHECK(iso_local_timestamp(after.at("gates").at("2").at("ts").get<std::string>()));
    }

    // Strict version check: only the integer 1 is accepted.
    {
        const auto path = temp_path("camera_cpp_gate_store_version_test.json");
        write_file(path, "{\"version\": 2, \"gates\": {\"1\": {\"rgb\": [1, 2, 3]}}}");
        GateStore newer;
        CAMERA_TEST_CHECK(!newer.load(path.string()));
        CAMERA_TEST_CHECK(!newer.gate(GateId::One).has_value());
        CAMERA_TEST_CHECK(newer.roles()[0] == "unset");

        write_file(path, "{\"version\": \"1\", \"gates\": {\"1\": {\"rgb\": [1, 2, 3]}}}");
        GateStore string_version;
        CAMERA_TEST_CHECK(!string_version.load(path.string()));

        write_file(path, "{\"version\": 1.0, \"gates\": {\"1\": {\"rgb\": [1, 2, 3]}}}");
        GateStore float_version;
        CAMERA_TEST_CHECK(!float_version.load(path.string()));

        write_file(path, "{\"gates\": {\"1\": {\"rgb\": [1, 2, 3]}}}");
        GateStore missing_version;
        CAMERA_TEST_CHECK(!missing_version.load(path.string()));
    }

    // Malformed RGB is rejected, never coerced to white.
    {
        const auto path = temp_path("camera_cpp_gate_store_malformed_test.json");
        write_file(path,
            "{\"version\": 1, \"gates\": {"
            "\"1\": {\"rgb\": [1, 2, \"bad\"], \"enabled\": true, \"future_slot\": 9}, "
            "\"2\": {\"rgb\": [1, 2, 3, 4]}, "
            "\"3\": {\"rgb\": [1.5, 2, 3]}}}");
        GateStore store;
        CAMERA_TEST_CHECK(store.load(path.string()));
        CAMERA_TEST_CHECK(!store.gate(GateId::One).has_value());
        CAMERA_TEST_CHECK(!store.gate(GateId::Two).has_value());
        CAMERA_TEST_CHECK(!store.gate(GateId::Three).has_value());
        CAMERA_TEST_CHECK(store.save_atomic());
        CAMERA_TEST_CHECK(read_json(path).at("gates").empty());

        write_file(path, "{\"version\": 1, \"gates\": {\"1\": {\"rgb\": [0, 255, 256]}}}");
        GateStore out_of_range;
        CAMERA_TEST_CHECK(out_of_range.load(path.string()));
        CAMERA_TEST_CHECK(!out_of_range.gate(GateId::One).has_value());
    }

    // Unset/unknown roles are stored as an absent key.
    {
        const auto path = temp_path("camera_cpp_gate_store_role_test.json");
        write_file(path,
            "{\"version\": 1, \"gates\": {"
            "\"1\": {\"rgb\": [1, 2, 3], \"role\": \"ours\"}, "
            "\"2\": {\"rgb\": [4, 5, 6], \"role\": \"unset\"}, "
            "\"3\": {\"rgb\": [7, 8, 9], \"role\": \"weird\"}}}");
        GateStore store;
        CAMERA_TEST_CHECK(store.load(path.string()));
        CAMERA_TEST_CHECK(store.roles()[0] == "ours");
        CAMERA_TEST_CHECK(store.roles()[1] == "unset");
        CAMERA_TEST_CHECK(store.roles()[2] == "unset");

        store.upsert(GateId::One, GateColor{10, 20, 30}, "unset");
        store.upsert(GateId::Three, GateColor{30, 20, 10}, "bogus");
        CAMERA_TEST_CHECK(store.roles()[0] == "unset");
        CAMERA_TEST_CHECK(store.roles()[2] == "unset");
        CAMERA_TEST_CHECK(store.save_atomic());

        const auto after = read_json(path);
        CAMERA_TEST_CHECK(!after.at("gates").at("1").contains("role"));
        CAMERA_TEST_CHECK(!after.at("gates").at("2").contains("role"));
        CAMERA_TEST_CHECK(!after.at("gates").at("3").contains("role"));
    }

    // LAB is nullable; timestamps are ISO local time; malformed LAB -> null.
    {
        const auto path = temp_path("camera_cpp_gate_store_lab_test.json");
        write_file(path,
            "{\"version\": 1, \"gates\": {"
            "\"1\": {\"rgb\": [1, 2, 3], \"lab\": [0, 100, -128, 127, -128, 127], \"ts\": \"old\"}, "
            "\"2\": {\"rgb\": [4, 5, 6], \"lab\": [101, 90, -5, 5, -6, 6]}, "
            "\"3\": {\"rgb\": [7, 8, 9], \"lab\": [10, 90, -5, 5, -6]}}}");
        GateStore store;
        CAMERA_TEST_CHECK(store.load(path.string()));

        const auto first = store.gate(GateId::One);
        CAMERA_TEST_CHECK(first.has_value());
        CAMERA_TEST_CHECK(first->lab.has_value());
        CAMERA_TEST_CHECK(*first->lab == (GateLab{{0, 100, -128, 127, -128, 127}}));
        CAMERA_TEST_CHECK(first->ts == "old");
        CAMERA_TEST_CHECK(!store.gate(GateId::Two)->lab.has_value());
        CAMERA_TEST_CHECK(!store.gate(GateId::Three)->lab.has_value());

        store.upsert(GateId::One, GateColor{1, 2, 3}, "ours", GateLab{{5, 95, -10, 10, -20, 20}});
        store.upsert(GateId::Three, GateColor{7, 8, 9}, "unset");
        CAMERA_TEST_CHECK(store.save_atomic());

        const auto after = read_json(path);
        CAMERA_TEST_CHECK(after.at("gates").at("1").at("lab").at(0) == 5);
        CAMERA_TEST_CHECK(after.at("gates").at("1").at("lab").at(5) == 20);
        CAMERA_TEST_CHECK(after.at("gates").at("1").at("role") == "ours");
        CAMERA_TEST_CHECK(iso_local_timestamp(after.at("gates").at("1").at("ts").get<std::string>()));
        CAMERA_TEST_CHECK(after.at("gates").at("2").at("lab").is_null());
        CAMERA_TEST_CHECK(after.at("gates").at("3").at("lab").is_null());
        CAMERA_TEST_CHECK(!after.at("gates").at("3").contains("role"));
    }

    // A missing file yields an empty store with native defaults.
    {
        const auto path = temp_path("camera_cpp_gate_store_missing_test.json");
        std::filesystem::remove(path);
        GateStore store;
        CAMERA_TEST_CHECK(!store.load(path.string()));
        CAMERA_TEST_CHECK(!store.gate(GateId::One).has_value());
        CAMERA_TEST_CHECK(!store.enabled(GateId::One));
        CAMERA_TEST_CHECK(store.roles()[0] == "unset");
    }

    return 0;
}
