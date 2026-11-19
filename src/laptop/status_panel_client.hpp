#ifndef CAMERA_CPP_LAPTOP_STATUS_PANEL_CLIENT_HPP
#define CAMERA_CPP_LAPTOP_STATUS_PANEL_CLIENT_HPP

#include <array>
#include <chrono>
#include <string>
#include <string_view>

namespace camera_cpp::laptop {

// Состояние одного ворот в статусе Pi (только данные для отображения на ПК).
struct GateStatusView {
    int id = 0;
    bool enabled = false;
    bool found = false;
    bool held = false;
    float angle_deg = 0.0F;
    float area_px = 0.0F;
    float dist_cm = 0.0F;
    bool dist_reliable = false;
    std::string role = "unset";
};

// Разобранный статус Pi; stale=true означает, что данные устарели.
struct ParsedStatus {
    std::array<GateStatusView, 3> gates{};
    float fps = 0.0F;
    std::string src;
    std::array<int, 2> frame_wh{0, 0};
    bool valid = false;
    bool stale = false;
};

// Разбирает JSON статуса; битый или пустой текст даёт valid=false без исключений.
// stale_seconds задаёт порог устаревания относительно received_at.
ParsedStatus parse_status_payload(std::string_view text,
                                  std::chrono::steady_clock::time_point received_at,
                                  std::chrono::steady_clock::time_point now,
                                  double stale_seconds = 2.0);

} // namespace camera_cpp::laptop

#endif
