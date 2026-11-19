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

// File order: [Lmin, Lmax, Amin, Amax, Bmin, Bmax].
using GateLab = std::array<int, 6>;

struct GateRecord {
    // Native plan: a missing or malformed "enabled" parses as false (the
    // legacy Python default was true). Gates are inactive until enabled.
    bool enabled = false;
    std::array<int, 3> rgb{{255, 255, 255}};
    // No LAB range is represented as null; malformed ranges are never
    // replaced with a fabricated default.
    std::optional<GateLab> lab;
    std::string role = "unset";
    std::string ts;
};

class GateStore {
public:
    GateStore() = default;

    // Reads saved_gates.json. Returns false for a missing/corrupt file or a
    // version other than the integer 1; in that case the store stays empty.
    bool load(const std::string& path);
    bool save_atomic() const;

    // Updates one of slots 1..3. An empty lab preserves a valid saved range
    // and writes null for a new or malformed one; a populated lab replaces
    // it. "unset" (and any unknown role) removes the role key entirely.
    void upsert(GateId id, GateColor color, const std::string& role,
                const std::optional<GateLab>& lab = std::nullopt);
    std::array<std::string, 3> roles() const;
    bool enabled(GateId id) const;
    std::optional<GateRecord> gate(GateId id) const;

private:
    static std::optional<GateRecord> parse_record(const nlohmann::json& gate);

    std::filesystem::path path_;
    nlohmann::json root_ = nlohmann::json::object({{"version", 1}, {"gates", nlohmann::json::object()}});
};

} // namespace camera_cpp
