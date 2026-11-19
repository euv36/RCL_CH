#include "clipboard.hpp"

#include "clipboard_text.hpp"

#include <SDL.h>

namespace camera_cpp::laptop {

std::string paste_text_from_clipboard() {
    char* raw = SDL_GetClipboardText();
    if (raw == nullptr) {
        return {};
    }
    const std::string text = sanitize_clipboard_bytes(raw);
    SDL_free(raw);
    return text;
}

} // namespace camera_cpp::laptop
