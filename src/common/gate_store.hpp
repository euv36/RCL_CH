#pragma once

#include <array>
#include <filesystem>
#include <optional>
#include <string>

#include <nlohmann/json.hpp>

#include "contracts.hpp"

namespace camera_cpp {

struct GateColor {
    int r = 255;
    int g = 255;
    int b = 255;
};

struct GateRecord {
    bool enabled = false;
    std::array<int, 3> rgb{{255, 255, 255}};
    std::string role = "unset";
    std::string ts;
};

class GateStore {
public:
    GateStore() = default;

    bool load(const std::string& path);
    bool save_atomic() const;

    void upsert(GateId id, GateColor color, const std::string& role);
    std::array<std::string, 3> roles() const;
    bool enabled(GateId id) const;
    std::optional<GateRecord> gate(GateId id) const;

private:
    static GateRecord parse_record(const nlohmann::json& record);

    std::filesystem::path path_;
    nlohmann::json root_ = nlohmann::json::object({{"version", 1}, {"gates", nlohmann::json::object()}});
};

} // namespace camera_cpp
