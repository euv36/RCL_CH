#pragma once

#include <string>

namespace camera_cpp::laptop {

// Reads the system clipboard via SDL2 and returns a sanitized UTF-8 string.
std::string paste_text_from_clipboard();

} // namespace camera_cpp::laptop
