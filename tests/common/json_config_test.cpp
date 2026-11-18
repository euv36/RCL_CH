#include "atomic_file.hpp"
#include "json_config.hpp"

#include <cassert>
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
    assert(loaded);
    assert(loaded->at("keep") == "yes");
    assert(warning.empty());

    loaded->at("owned") = 2;
    std::string error;
    assert(atomic_write_text(path, loaded->dump(2), &error));

    auto rewritten = read_json_file(path, &warning);
    assert(rewritten);
    assert(rewritten->at("keep") == "yes");
    assert(rewritten->at("owned") == 2);

    std::ofstream(path, std::ios::trunc) << "{not-json";
    auto corrupt = read_json_file(path, &warning);
    assert(!corrupt);
    assert(!warning.empty());

    std::ofstream(path, std::ios::trunc);
    auto empty = read_json_file(path, &warning);
    assert(!empty);
    assert(!warning.empty());

    std::filesystem::remove(path);
    auto missing = read_json_file(path, &warning);
    assert(!missing);
    assert(!warning.empty());
    std::cout << "json_config_test passed\n";
    return 0;
}
