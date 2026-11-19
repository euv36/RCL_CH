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

    // Nested objects and UTF-8 strings must round-trip without corruption.
    std::ofstream(path, std::ios::trunc)
        << R"({"nested":{"a":{"b":[1,2,3]},"note":"\u041f\u0440\u0438\u0432\u0435\u0442"},"unknown":true})";
    auto nested = read_json_file(path, &warning);
    CAMERA_TEST_CHECK(nested);
    CAMERA_TEST_CHECK(nested->at("nested").at("a").at("b").size() == 3);
    const std::string utf8 = nested->at("nested").at("note").get<std::string>();
    CAMERA_TEST_CHECK(utf8 == "\xd0\x9f\xd1\x80\xd0\xb8\xd0\xb2\xd0\xb5\xd1\x82");
    nested->at("nested").at("a").at("b")[0] = 9;
    CAMERA_TEST_CHECK(atomic_write_text(path, nested->dump(2), &error));
    auto nested_again = read_json_file(path, &warning);
    CAMERA_TEST_CHECK(nested_again);
    CAMERA_TEST_CHECK(nested_again->at("nested").at("a").at("b")[0] == 9);
    CAMERA_TEST_CHECK(nested_again->at("unknown") == true);

    // A valid JSON document whose root is not an object is rejected safely.
    std::ofstream(path, std::ios::trunc) << R"([1,2,3])";
    auto array_root = read_json_file(path, &warning);
    CAMERA_TEST_CHECK(!array_root);
    CAMERA_TEST_CHECK(!warning.empty());

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
