#include "json_config.hpp"

#include <fstream>
#include <sstream>

std::optional<nlohmann::json> read_json_file(const std::filesystem::path& path, std::string* warning) {
    auto set_warning = [warning](const std::string& message) {
        if (warning) {
            *warning = message;
        }
        return std::nullopt;
    };
    if (warning) {
        warning->clear();
    }

    std::error_code ec;
    if (!std::filesystem::exists(path, ec) || !std::filesystem::is_regular_file(path, ec)) {
        return set_warning("missing or unreadable JSON file: " + path.string());
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return set_warning("could not open JSON file: " + path.string());
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    const auto text = buffer.str();
    if (text.empty()) {
        return set_warning("empty JSON file: " + path.string());
    }

    try {
        auto value = nlohmann::json::parse(text);
        if (!value.is_object()) {
            return set_warning("JSON root must be an object: " + path.string());
        }
        if (warning) {
            warning->clear();
        }
        return value;
    } catch (const nlohmann::json::exception& ex) {
        return set_warning(std::string("malformed JSON: ") + ex.what());
    }
}
