#include "clipboard_text.hpp"

#include <algorithm>
#include <cstdint>

namespace camera_cpp::laptop {
namespace {

constexpr std::size_t kMaxChars = 256;

void append_utf8(std::string& out, std::uint32_t codepoint) {
    if (codepoint <= 0x7F) {
        out.push_back(static_cast<char>(codepoint));
    } else if (codepoint <= 0x7FF) {
        out.push_back(static_cast<char>(0xC0 | (codepoint >> 6)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0xFFFF) {
        out.push_back(static_cast<char>(0xE0 | (codepoint >> 12)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    } else if (codepoint <= 0x10FFFF) {
        out.push_back(static_cast<char>(0xF0 | (codepoint >> 18)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (codepoint & 0x3F)));
    }
}

bool decode_utf8(std::string_view text, std::size_t& offset, std::uint32_t& codepoint, std::size_t& length) {
    const auto first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80) {
        codepoint = first;
        length = 1;
        return true;
    }

    std::size_t needed = 0;
    std::uint32_t value = 0;
    if ((first & 0xE0) == 0xC0) {
        needed = 2;
        value = first & 0x1F;
    } else if ((first & 0xF0) == 0xE0) {
        needed = 3;
        value = first & 0x0F;
    } else if ((first & 0xF8) == 0xF0) {
        needed = 4;
        value = first & 0x07;
    } else {
        return false;
    }
    if (offset + needed > text.size()) {
        return false;
    }
    for (std::size_t i = 1; i < needed; ++i) {
        const auto next = static_cast<unsigned char>(text[offset + i]);
        if ((next & 0xC0) != 0x80) {
            return false;
        }
        value = (value << 6) | (next & 0x3F);
    }
    if ((needed == 2 && value < 0x80) || (needed == 3 && value < 0x800) ||
        (needed == 4 && value < 0x10000) || value > 0x10FFFF ||
        (value >= 0xD800 && value <= 0xDFFF)) {
        return false;
    }
    codepoint = value;
    length = needed;
    return true;
}

bool decode_utf16(std::string_view payload, bool little_endian, std::string& out) {
    for (std::size_t i = 0; i + 1 < payload.size(); i += 2) {
        const auto lo = static_cast<std::uint8_t>(payload[i]);
        const auto hi = static_cast<std::uint8_t>(payload[i + 1]);
        std::uint32_t unit = little_endian ? (static_cast<std::uint32_t>(lo) | (static_cast<std::uint32_t>(hi) << 8))
                                           : (static_cast<std::uint32_t>(hi) | (static_cast<std::uint32_t>(lo) << 8));
        std::uint32_t codepoint = unit;
        if (unit >= 0xD800 && unit <= 0xDBFF && i + 3 < payload.size()) {
            const auto lo2 = static_cast<std::uint8_t>(payload[i + 2]);
            const auto hi2 = static_cast<std::uint8_t>(payload[i + 3]);
            const std::uint32_t second = little_endian
                ? (static_cast<std::uint32_t>(lo2) | (static_cast<std::uint32_t>(hi2) << 8))
                : (static_cast<std::uint32_t>(hi2) | (static_cast<std::uint32_t>(lo2) << 8));
            if (second >= 0xDC00 && second <= 0xDFFF) {
                codepoint = 0x10000 + ((unit - 0xD800) << 10) + (second - 0xDC00);
                i += 2;
            }
        }
        append_utf8(out, codepoint);
    }
    return true;
}

bool detect_utf16(std::string_view raw, bool& little_endian, std::size_t& payload_offset) {
    if (raw.size() >= 2) {
        const auto b0 = static_cast<unsigned char>(raw[0]);
        const auto b1 = static_cast<unsigned char>(raw[1]);
        if (b0 == 0xFF && b1 == 0xFE) {
            little_endian = true;
            payload_offset = 2;
            return true;
        }
        if (b0 == 0xFE && b1 == 0xFF) {
            little_endian = false;
            payload_offset = 2;
            return true;
        }
    }
    if (raw.size() < 4 || (raw.size() % 2) != 0) {
        return false;
    }
    std::size_t zero_even = 0;
    std::size_t zero_odd = 0;
    for (std::size_t i = 0; i < raw.size(); ++i) {
        if (raw[i] == '\0') {
            if ((i % 2) == 0) {
                ++zero_even;
            } else {
                ++zero_odd;
            }
        }
    }
    // Require both a meaningful count and a clear parity imbalance: a short
    // UTF-8 string with one embedded NUL must not be mistaken for UTF-16BE.
    const std::size_t minimum_zeros = std::max<std::size_t>(3, raw.size() / 4);
    if (zero_odd >= minimum_zeros && zero_odd >= zero_even * 2) {
        little_endian = true;
        return true;
    }
    if (zero_even >= minimum_zeros && zero_even >= zero_odd * 2) {
        little_endian = false;
        return true;
    }
    return false;
}

std::size_t invalid_sequence_length(unsigned char first) {
    if ((first & 0xE0) == 0xC0) {
        return 2;
    }
    if ((first & 0xF0) == 0xE0) {
        return 3;
    }
    if ((first & 0xF8) == 0xF0) {
        return 4;
    }
    return 1;
}

std::string sanitize_utf8(std::string_view text) {
    std::string out;
    std::size_t chars = 0;
    for (std::size_t offset = 0; offset < text.size();) {
        std::uint32_t codepoint = 0;
        std::size_t length = 0;
        if (!decode_utf8(text, offset, codepoint, length)) {
            // Drop the whole malformed sequence, not just its lead byte:
            // otherwise the following byte could be emitted as a stray char.
            offset += std::min(invalid_sequence_length(static_cast<unsigned char>(text[offset])), text.size() - offset);
            continue;
        }
        offset += length;
        if (codepoint < 0x20 || (codepoint >= 0x7F && codepoint <= 0x9F) || codepoint == 0x2028 ||
            codepoint == 0x2029 || codepoint == 0xFEFF) {
            continue;
        }
        if (chars == kMaxChars) {
            break;
        }
        append_utf8(out, codepoint);
        ++chars;
    }
    const auto last = out.find_last_not_of(" \t");
    if (last == std::string::npos) {
        out.clear();
    } else {
        out.resize(last + 1);
        const auto first = out.find_first_not_of(" \t");
        if (first != 0 && first != std::string::npos) {
            out.erase(0, first);
        }
    }
    return out;
}

} // namespace

// Очищает данные буфера обмена: UTF-16 переводится в UTF-8, битые последовательности отбрасываются.
// Результат безопасен для вставки в текстовое поле CONNECT.
std::string sanitize_clipboard_bytes(std::string_view raw) {
    if (raw.empty()) {
        return {};
    }
    bool little_endian = true;
    std::size_t payload_offset = 0;
    if (!detect_utf16(raw, little_endian, payload_offset)) {
        return sanitize_utf8(raw);
    }
    std::string decoded;
    decode_utf16(raw.substr(payload_offset), little_endian, decoded);
    return sanitize_utf8(decoded);
}

void drop_last_utf8_codepoint(std::string& text) {
    while (!text.empty()) {
        const auto ch = static_cast<unsigned char>(text.back());
        text.pop_back();
        if ((ch & 0xC0) != 0x80) {
            break;
        }
    }
}

} // namespace camera_cpp::laptop
