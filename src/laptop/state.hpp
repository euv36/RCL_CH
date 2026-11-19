#pragma once

#include <string>

namespace camera_cpp::laptop {

struct AppState {
    enum class SourceKind { Local, Pi, None };

    AppState();

    std::string camera_source_text = "10.132.122.51";
    char source_buffer[512]{};
    bool input_active = false;
    SourceKind source_kind = SourceKind::None;
    std::string pi_status_summary;
    double fps_ui = 0;

    std::string pi_host;
    int pi_port = 5555;
    int local_index = -1;
    std::string connect_error;
};

} // namespace camera_cpp::laptop
