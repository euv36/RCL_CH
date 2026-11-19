#pragma once

#include <string>
#include <string_view>

#include "state.hpp"

namespace camera_cpp::laptop {

struct ConnectRequest {
    AppState::SourceKind kind = AppState::SourceKind::None;
    int camera_index = -1;
    std::string host;
    int port = 0;
    std::string camera_source_text;
    std::string error;

    bool valid() const { return error.empty() && kind != AppState::SourceKind::None; }
};

ConnectRequest parse_connect(const std::string& text);
void apply_connect(AppState& state, const ConnectRequest& request);

// Keyboard/paste input helpers used by the SDL event loop.
void append_source_text(AppState& state, std::string_view text);
void handle_source_backspace(AppState& state);

} // namespace camera_cpp::laptop
