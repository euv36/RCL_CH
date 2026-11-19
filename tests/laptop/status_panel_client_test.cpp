// Тест разбора статуса Pi: валидный/битый JSON и контроль устаревания.
#include <chrono>
#include <string>

#include "../test_checks.hpp"
#include "laptop/status_panel_client.hpp"

namespace {

using clock = std::chrono::steady_clock;

} // namespace

int main() {
    const auto now = clock::now();

    // Валидный статус: три ворота, отключённые нормализуются в не-найденные.
    const std::string valid = R"({
        "fps": 12.5, "src": "pi", "frame_wh": [640, 360],
        "gates": {
            "1": {"id": 1, "enabled": true, "found": true, "held": false,
                  "angle": 123.5, "area": 456.7, "dist_cm": 148.0,
                  "dist_reliable": true, "role": "ours"},
            "2": {"id": 2, "enabled": false, "found": true, "angle": 99},
            "3": {"id": 3, "enabled": true, "found": false}
        }
    })";
    const auto parsed = camera_cpp::laptop::parse_status_payload(valid, now, now);
    CAMERA_TEST_CHECK(parsed.valid);
    CAMERA_TEST_CHECK(!parsed.stale);
    CAMERA_TEST_CHECK(parsed.fps > 12.4F && parsed.fps < 12.6F);
    CAMERA_TEST_CHECK(parsed.src == "pi");
    CAMERA_TEST_CHECK(parsed.frame_wh[0] == 640);
    CAMERA_TEST_CHECK(parsed.frame_wh[1] == 360);
    CAMERA_TEST_CHECK(parsed.gates[0].found);
    CAMERA_TEST_CHECK(parsed.gates[0].id == 1);
    CAMERA_TEST_CHECK(parsed.gates[0].role == "ours");
    CAMERA_TEST_CHECK(parsed.gates[1].enabled == false);
    CAMERA_TEST_CHECK(parsed.gates[1].found == false);
    CAMERA_TEST_CHECK(parsed.gates[2].id == 3);
    CAMERA_TEST_CHECK(!parsed.gates[2].found);

    // Битый и пустой JSON не бросают исключений.
    const auto broken = camera_cpp::laptop::parse_status_payload("{oops", now, now);
    CAMERA_TEST_CHECK(!broken.valid);
    const auto empty = camera_cpp::laptop::parse_status_payload("", now, now);
    CAMERA_TEST_CHECK(!empty.valid);

    // Устаревший статус.
    const auto stale_at = now - std::chrono::seconds(3);
    const auto stale = camera_cpp::laptop::parse_status_payload(valid, stale_at, now, 2.0);
    CAMERA_TEST_CHECK(stale.valid);
    CAMERA_TEST_CHECK(stale.stale);

    return 0;
}
