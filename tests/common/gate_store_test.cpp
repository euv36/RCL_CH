#include "gate_store.hpp"

#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>

#include "../test_checks.hpp"

using camera_cpp::GateColor;
using camera_cpp::GateStore;

static std::filesystem::path temp_path(const char* name) {
    return std::filesystem::temp_directory_path() / name;
}

int main() {
    const auto path = temp_path("camera_cpp_gate_store_test.json");
    std::filesystem::remove(path);
    {
        std::ofstream file(path, std::ios::binary);
        file << "{\"gates\": {\"1\": {\"rgb\": [1, 2, 3], \"enabled\": true, \"role\": \"ours\"}, "
                "\"2\": {\"rgb\": [4, 5, 6], \"future_slot\": 77}, "
                "\"3\": {\"rgb\": [7, 8, 9], \"role\": \"opponent\"}}, "
                "\"future_root\": {\"keep\": true}}";
    }

    GateStore store;
    CAMERA_TEST_CHECK(store.load(path.string()));
    CAMERA_TEST_CHECK(store.roles()[0] == "ours");
    CAMERA_TEST_CHECK(store.roles()[1] == "unset");
    CAMERA_TEST_CHECK(store.roles()[2] == "opponent");
    CAMERA_TEST_CHECK(!store.enabled(GateId::Two));

    store.upsert(GateId::Two, GateColor{12, 34, 56}, "opponent");
    CAMERA_TEST_CHECK(store.save_atomic());
    nlohmann::json after;
    {
        std::ifstream file(path, std::ios::binary);
        after = nlohmann::json::parse(file);
    }
    CAMERA_TEST_CHECK(after.at("future_root").at("keep") == true);
    CAMERA_TEST_CHECK(after.at("gates").at("2").at("future_slot") == 77);
    CAMERA_TEST_CHECK(after.at("gates").at("2").at("enabled") == true);
    CAMERA_TEST_CHECK(after.at("gates").at("2").at("role") == "opponent");
    CAMERA_TEST_CHECK(after.at("gates").at("2").at("rgb").at(0) == 12);

    std::filesystem::remove(path);
    GateStore disabled_store;
    CAMERA_TEST_CHECK(!disabled_store.load(path.string()));
    CAMERA_TEST_CHECK(disabled_store.roles()[0] == "unset");
    CAMERA_TEST_CHECK(!disabled_store.enabled(GateId::One));
    return 0;
}
