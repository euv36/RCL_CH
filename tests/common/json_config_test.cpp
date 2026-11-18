#include "../test_checks.hpp"
#include "atomic_file.hpp"
#include "json_config.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
std::filesystem::path make_temp_path(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / "camera_cpp_json_tests";
    std::filesystem::create_directories(dir);
    return dir / (name + "." + std::to_string(static_cast<unsigned long long>(std::hash<std::string>{}(name)) % 1000000));
}
}

int main() {
    const auto path = make_temp_path("config");
    std::ofstream(path) << R"({"version":1,"keep":"yes","owned":1})";

    std::string warning;
    auto loaded = read_json_file(path, &warning);
    CAMERA_TEST_CHECK(loaded);
    CAMERA_TEST_CHECK(loaded->at("keep") == "yes");
    CAMERA_TEST_CHECK(warning.empty());

    loaded->at("owned") = 2;
    std::string error;
    CAMERA_TEST_CHECK(atomic_write_text(path, loaded->dump(2), &error));

    auto rewritten = read_json_file(path, &warning);
    CAMERA_TEST_CHECK(rewritten);
    CAMERA_TEST_CHECK(rewritten->at("keep") == "yes");
    CAMERA_TEST_CHECK(rewritten->at("owned") == 2);

    std::ofstream(path, std::ios::trunc) << "{not-json";
    auto corrupt = read_json_file(path, &warning);
    CAMERA_TEST_CHECK(!corrupt);
    CAMERA_TEST_CHECK(!warning.empty());

    std::ofstream(path, std::ios::trunc);
    auto empty = read_json_file(path, &warning);
    CAMERA_TEST_CHECK(!empty);
    CAMERA_TEST_CHECK(!warning.empty());

    std::filesystem::remove(path);
    auto missing = read_json_file(path, &warning);
    CAMERA_TEST_CHECK(!missing);
    CAMERA_TEST_CHECK(!warning.empty());
    std::cout << "json_config_test passed\n";
    return 0;
}
