#pragma once

#include <string>
#include <string_view>

namespace camera_cpp::laptop {

// Converts SDL clipboard bytes to a trimmed, printable UTF-8 string.
// Handles UTF-8 and both BOM-less and BOM-prefixed UTF-16LE/BE packets.
std::string sanitize_clipboard_bytes(std::string_view raw);

// Removes the last UTF-8 code point; ASCII fallback keeps the string valid.
void drop_last_utf8_codepoint(std::string& text);

} // namespace camera_cpp::laptop
