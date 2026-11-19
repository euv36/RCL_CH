#include "state.hpp"

#include <cstdio>

namespace camera_cpp::laptop {

AppState::AppState() {
    std::snprintf(source_buffer, sizeof(source_buffer), "%s", camera_source_text.c_str());
}

} // namespace camera_cpp::laptop
