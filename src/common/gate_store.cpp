#include "gate_store.hpp"

#include <algorithm>

#include "atomic_file.hpp"
#include "json_config.hpp"

namespace camera_cpp {
namespace {

std::string gate_key(GateId id) {
    return std::to_string(static_cast<int>(id));
}

} // namespace

bool GateStore::load(const std::string& path) {
    path_ = path;
    std::string warning;
    const auto loaded = read_json_file(path_, &warning);
    if (!loaded || !loaded->is_object()) {
        root_ = nlohmann::json::object({{"version", 1}, {"gates", nlohmann::json::object()}});
        return false;
    }

    root_ = *loaded;
    if (!root_.contains("version")) {
        root_["version"] = 1;
    }
    if (!root_.contains("gates") || !root_["gates"].is_object()) {
        root_["gates"] = nlohmann::json::object();
    }
    for (int id = 1; id <= 3; ++id) {
        const std::string key = std::to_string(id);
        const auto existing = root_["gates"].value(key, nlohmann::json::object());
        root_["gates"][key] = existing.is_object() ? existing : nlohmann::json::object();
    }
    return true;
}

GateRecord GateStore::parse_record(const nlohmann::json& gate) {
    GateRecord record;
    if (!gate.is_object()) {
        return record;
    }

    if (const auto rgb = gate.find("rgb"); rgb != gate.end() && rgb->is_array() && rgb->size() >= 3) {
        for (std::size_t index = 0; index < 3; ++index) {
            try {
                record.rgb[index] = std::clamp((*rgb)[index].get<long long>(), 0LL, 255LL);
            } catch (...) {
                record.rgb[index] = 255;
            }
        }
    }

    try {
        record.enabled = gate.value("enabled", false);
    } catch (...) {
        record.enabled = false;
    }

    if (const auto role = gate.find("role"); role != gate.end() && role->is_string()) {
        const std::string value = role->get<std::string>();
        if (value == "ours" || value == "opponent") {
            record.role = value;
        }
    }

    if (const auto ts = gate.find("ts"); ts != gate.end() && ts->is_string()) {
        record.ts = ts->get<std::string>();
    }

    return record;
}

bool GateStore::save_atomic() const {
    std::string error;
    return atomic_write_text(path_, root_.dump(2), &error);
}

void GateStore::upsert(GateId id, GateColor color, const std::string& role) {
    nlohmann::json& gate = root_["gates"][gate_key(id)];
    gate["rgb"] = nlohmann::json::array({
        std::clamp(color.r, 0, 255),
        std::clamp(color.g, 0, 255),
        std::clamp(color.b, 0, 255),
    });
    gate["enabled"] = true;
    gate["role"] = (role == "ours" || role == "opponent") ? role : "unset";
}

std::array<std::string, 3> GateStore::roles() const {
    std::array<std::string, 3> out{};
    for (std::size_t index = 0; index < out.size(); ++index) {
        const auto value = gate(static_cast<GateId>(static_cast<int>(index) + 1));
        out[index] = value ? value->role : "unset";
    }
    return out;
}

bool GateStore::enabled(GateId id) const {
    const auto value = gate(id);
    return value && value->enabled;
}

std::optional<GateRecord> GateStore::gate(GateId id) const {
    const auto gates = root_.find("gates");
    if (gates == root_.end() || !gates->is_object()) {
        return parse_record(nlohmann::json::object());
    }
    const auto key = gate_key(id);
    const auto value = gates->find(key);
    return value == gates->end() ? parse_record(nlohmann::json::object()) : parse_record(*value);
}

} // namespace camera_cpp
