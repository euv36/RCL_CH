#include "../test_checks.hpp"
#include "atomic_file.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
std::filesystem::path make_temp_path(const std::string& name) {
    auto dir = std::filesystem::temp_directory_path() / "camera_cpp_atomic_tests";
    std::filesystem::create_directories(dir);
    return dir / (name + "." + std::to_string(static_cast<unsigned long long>(std::hash<std::string>{}(name)) % 1000000));
}

void remove_quiet(const std::filesystem::path& path) {
    std::error_code ec;
    std::filesystem::remove(path, ec);
}
}

int main() {
    const auto path = make_temp_path("write");
    remove_quiet(path);
    std::string error;
    CAMERA_TEST_CHECK(atomic_write_text(path, "hello", &error));
    std::ifstream in(path);
    std::string content;
    in >> content;
    CAMERA_TEST_CHECK(content == "hello");
    CAMERA_TEST_CHECK(error.empty());

    const auto nested = make_temp_path("nested") / "child" / "file.txt";
    remove_quiet(nested.parent_path());
    remove_quiet(nested);
    CAMERA_TEST_CHECK(!atomic_write_text(nested, "nope", &error));
    CAMERA_TEST_CHECK(!error.empty());

    const auto stale_owner = make_temp_path("stale");
    remove_quiet(stale_owner);
    for (int i = 1; i <= 3; ++i) {
        std::ofstream(stale_owner.string() + ".tmp.999." + std::to_string(i)) << "old";
    }
    CAMERA_TEST_CHECK(atomic_write_text(stale_owner, "new", &error));
    CAMERA_TEST_CHECK(std::ifstream(stale_owner).good());
    CAMERA_TEST_CHECK(!std::filesystem::exists(stale_owner.string() + ".tmp.999.1"));
    CAMERA_TEST_CHECK(!std::filesystem::exists(stale_owner.string() + ".tmp.999.2"));
    CAMERA_TEST_CHECK(!std::filesystem::exists(stale_owner.string() + ".tmp.999.3"));

    remove_quiet(path);
    remove_quiet(stale_owner);
    remove_quiet(nested.parent_path());
    std::cout << "atomic_file_test passed\n";
    return 0;
}
