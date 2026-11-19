#include "laptop/help_ru.hpp"

#include <array>
#include <string>

#include "../test_checks.hpp"

using camera_cpp::laptop::HelpRu;

int main() {
    const auto& sections = HelpRu::sections();

    const std::array<std::string, 7> expected_ids{
        "main", "gates", "radar", "camera", "hsv", "field", "status"};
    CAMERA_TEST_CHECK(sections.size() == expected_ids.size());
    for (std::size_t i = 0; i < expected_ids.size(); ++i) {
        CAMERA_TEST_CHECK(sections[i].id == expected_ids[i]);
        CAMERA_TEST_CHECK(!sections[i].title.empty());
        CAMERA_TEST_CHECK(!sections[i].lines.empty());
    }

    const std::string text = HelpRu::combined_text();
    const std::array<std::string, 11> required{
        "Ctrl+V", "CAM SET", "RADAR", "BIND FIELD", "POS", "HSV MASK",
        "INFO", "SAVE G1", "GATES", "exposure", "brightness"};
    for (const auto& token : required) {
        CAMERA_TEST_CHECK(text.find(token) != std::string::npos);
    }
    return 0;
}
