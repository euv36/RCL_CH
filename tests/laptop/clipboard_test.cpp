#include "laptop/clipboard_text.hpp"

#include <string>
#include <string_view>

#include "../test_checks.hpp"

using camera_cpp::laptop::drop_last_utf8_codepoint;
using camera_cpp::laptop::sanitize_clipboard_bytes;

namespace {

std::string utf16le(std::string_view text) {
    std::string out;
    out.reserve(text.size() * 2);
    for (const char ch : text) {
        out.push_back(ch);
        out.push_back('\0');
    }
    return out;
}

} // namespace

int main() {
    CAMERA_TEST_CHECK(sanitize_clipboard_bytes("192.168.0.199") == "192.168.0.199");
    CAMERA_TEST_CHECK(sanitize_clipboard_bytes(u8"Привет") == u8"Привет");

    const std::string broad_without_bom = utf16le("10.132.122.51");
    CAMERA_TEST_CHECK(sanitize_clipboard_bytes(broad_without_bom) == "10.132.122.51");

    std::string broad_with_bom("\xFF\xFE", 2);
    broad_with_bom += broad_without_bom;
    CAMERA_TEST_CHECK(sanitize_clipboard_bytes(broad_with_bom) == "10.132.122.51");

    std::string with_controls = "10.0.0.1";
    with_controls.push_back('\0');
    with_controls.push_back('\r');
    with_controls.push_back('\n');
    with_controls.push_back('\x01');
    CAMERA_TEST_CHECK(sanitize_clipboard_bytes(with_controls) == "10.0.0.1");
    CAMERA_TEST_CHECK(sanitize_clipboard_bytes("").empty());

    const std::string too_long(300, 'a');
    CAMERA_TEST_CHECK(sanitize_clipboard_bytes(too_long).size() == 256);

    const std::string invalid_utf8("\xC3\x28", 2);
    CAMERA_TEST_CHECK(sanitize_clipboard_bytes(invalid_utf8).empty());

    std::string cyrillic = u8"abcПривет";
    drop_last_utf8_codepoint(cyrillic);
    CAMERA_TEST_CHECK(cyrillic == u8"abcПриве");
    drop_last_utf8_codepoint(cyrillic);
    CAMERA_TEST_CHECK(cyrillic == u8"abcПрив");

    return 0;
}
