#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

namespace camera_cpp {

class CamSettingsConfig {
public:
    bool load(const std::string& path);
    std::optional<float> get(const std::string& control) const;
    bool set(const std::string& control, float value);
    bool save() const;
    static std::vector<std::pair<std::string, std::pair<int, int>>> ranges();

private:
    std::filesystem::path path_;
    nlohmann::json root_;
};

} // namespace camera_cpp
