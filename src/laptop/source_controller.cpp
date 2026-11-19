#include "source_controller.hpp"

#include "clipboard_text.hpp"

#include <algorithm>
#include <cctype>
#include <limits>
#include <string_view>

namespace camera_cpp::laptop {
namespace {

constexpr int kDefaultPiPort = 5555;
constexpr int kMaxCameraIndex = 64;

std::string trim(std::string_view text) {
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) {
        return {};
    }
    const auto last = text.find_last_not_of(" \t\r\n");
    return std::string(text.substr(first, last - first + 1));
}

bool is_all_digits(std::string_view text) {
    return !text.empty() && std::all_of(text.begin(), text.end(), [](const char ch) {
        return std::isdigit(static_cast<unsigned char>(ch)) != 0;
    });
}

bool is_hostname(std::string_view text) {
    if (text.empty() || text.size() > 253 ||
        text.front() == '.' || text.back() == '.' ||
        text.front() == '-' || text.back() == '-') {
        return false;
    }
    bool has_alnum = false;
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0) {
            has_alnum = true;
        } else if (ch != '.' && ch != '-') {
            return false;
        }
    }
    return has_alnum;
}

bool parse_number(std::string_view text, long long& value) {
    if (!is_all_digits(text)) {
        return false;
    }
    value = 0;
    for (const char ch : text) {
        value = value * 10 + (ch - '0');
        if (value > std::numeric_limits<int>::max()) {
            return false;
        }
    }
    return true;
}

ConnectRequest invalid(const std::string& message) {
    ConnectRequest request;
    request.error = message;
    return request;
}

} // namespace

// Разбирает текст поля CONNECT: IPv4, hostname и порт через двоеточие.
// При любой ошибке возвращает invalid() с человекочитаемым сообщением.
ConnectRequest parse_connect(const std::string& text) {
    const std::string value = trim(text);
    if (value.empty()) {
        return invalid("Пустой адрес источника");
    }

    std::string lowered = value;
    std::transform(lowered.begin(), lowered.end(), lowered.begin(), [](const char ch) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
    });
    if (lowered == "local") {
        ConnectRequest request;
        request.kind = AppState::SourceKind::Local;
        request.camera_index = -1;
        request.camera_source_text = value;
        return request;
    }
    if (is_all_digits(value)) {
        long long index = 0;
        if (!parse_number(value, index) || index > kMaxCameraIndex) {
            return invalid("Номер камеры вне диапазона 0..64");
        }
        ConnectRequest request;
        request.kind = AppState::SourceKind::Local;
        request.camera_index = static_cast<int>(index);
        request.camera_source_text = value;
        return request;
    }

    if (std::count(value.begin(), value.end(), ':') > 1) {
        return invalid("Некорректный адрес источника");
    }
    const auto colon = value.find(':');
    const std::string_view host = colon == std::string::npos ? std::string_view(value) : std::string_view(value).substr(0, colon);
    int port = kDefaultPiPort;
    if (colon != std::string::npos) {
        long long parsed_port = 0;
        const std::string_view port_text = std::string_view(value).substr(colon + 1);
        if (!parse_number(port_text, parsed_port) || parsed_port < 1 || parsed_port > 65535) {
            return invalid("Порт вне диапазона 1..65535");
        }
        port = static_cast<int>(parsed_port);
    }
    if (!is_hostname(host)) {
        return invalid("Некорректный адрес источника");
    }

    ConnectRequest request;
    request.kind = AppState::SourceKind::Pi;
    request.host = std::string(host);
    request.port = port;
    request.camera_source_text = value;
    return request;
}

void apply_connect(AppState& state, const ConnectRequest& request) {
    state.connect_error = request.error;
    if (!request.valid()) {
        state.source_kind = AppState::SourceKind::None;
        state.pi_host.clear();
        state.pi_port = kDefaultPiPort;
        state.local_index = -1;
        state.pi_status_summary.clear();
        return;
    }
    state.source_kind = request.kind;
    state.local_index = request.camera_index;
    state.pi_host = request.host;
    state.pi_port = request.port;
    state.camera_source_text = request.camera_source_text;
    state.connect_error.clear();
    state.pi_status_summary = "Адрес принят; ожидание статуса";
}

void append_source_text(AppState& state, std::string_view text) {
    std::string combined = state.camera_source_text;
    combined.append(text);
    state.camera_source_text = sanitize_clipboard_bytes(combined);
}

void handle_source_backspace(AppState& state) {
    drop_last_utf8_codepoint(state.camera_source_text);
}

} // namespace camera_cpp::laptop
