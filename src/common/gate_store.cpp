#include "gate_store.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <ctime>
#include <iomanip>
#include <sstream>

#include "atomic_file.hpp"
#include "json_config.hpp"

namespace camera_cpp {
namespace {

constexpr int kVersion = 1;

bool valid_gate_id(GateId id) {
    const int value = static_cast<int>(id);
    return value >= 1 && value <= 3;
}

std::string gate_key(GateId id) {
    return std::to_string(static_cast<int>(id));
}

bool is_slot_key(const std::string& key) {
    return key == "1" || key == "2" || key == "3";
}

bool valid_lab(const nlohmann::json& value, GateLab* out) {
    if (!value.is_array() || value.size() != 6) {
        return false;
    }
    for (std::size_t index = 0; index < out->size(); ++index) {
        const auto& component = value[index];
        if (!component.is_number()) {
            return false;
        }
        const double number = component.get<double>();
        if (!std::isfinite(number)) {
            return false;
        }
        const double low = index < 2 ? 0.0 : -128.0;
        const double high = index < 2 ? 100.0 : 127.0;
        if (number < low || number > high) {
            return false;
        }
        (*out)[index] = static_cast<int>(number);
    }
    return true;
}

bool valid_rgb(const nlohmann::json& value, std::array<int, 3>* out) {
    if (!value.is_array() || value.size() != 3) {
        return false;
    }
    for (std::size_t index = 0; index < out->size(); ++index) {
        const auto& component = value[index];
        // No coercion: floats, strings, bools and out-of-range values are
        // malformed and the whole record is rejected instead of going white.
        if (!component.is_number_integer() && !component.is_number_unsigned()) {
            return false;
        }
        const long long number = component.get<long long>();
        if (number < 0 || number > 255) {
            return false;
        }
        (*out)[index] = static_cast<int>(number);
    }
    return true;
}

std::string iso_local_timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S");
    return out.str();
}

} // namespace

bool GateStore::load(const std::string& path) {
    path_ = path;
    root_ = nlohmann::json::object({{"version", kVersion}, {"gates", nlohmann::json::object()}});

    std::string warning;
    const auto loaded = read_json_file(path_, &warning);
    if (!loaded || !loaded->is_object()) {
        return false;
    }

    // Strict check: "1" as text, 1.0 and a missing version are all rejected.
    const auto version = loaded->find("version");
    if (version == loaded->end() ||
        (!version->is_number_integer() && !version->is_number_unsigned()) ||
        version->get<long long>() != kVersion) {
        return false;
    }

    nlohmann::json gates = nlohmann::json::object();
    const auto loaded_gates = loaded->find("gates");
    if (loaded_gates != loaded->end() && loaded_gates->is_object()) {
        for (auto it = loaded_gates->begin(); it != loaded_gates->end(); ++it) {
            if (!is_slot_key(it.key()) || !parse_record(it.value())) {
                continue; // only valid slots 1..3 are materialized
            }

            nlohmann::json record = it.value();
            const auto role = record.find("role");
            if (role == record.end() || !role->is_string()) {
                record.erase("role");
            } else {
                const std::string value = role->get<std::string>();
                if (value != "ours" && value != "opponent") {
                    record.erase("role"); // unset is an absent key
                }
            }
            const auto lab = record.find("lab");
            if (lab != record.end()) {
                GateLab parsed{};
                if (!valid_lab(*lab, &parsed)) {
                    record["lab"] = nullptr;
                }
            }
            gates[it.key()] = std::move(record); // keep unknown fields verbatim
        }
    }

    root_ = *loaded; // keep unknown root-level keys verbatim
    root_["version"] = kVersion;
    root_["gates"] = std::move(gates);
    return true;
}

std::optional<GateRecord> GateStore::parse_record(const nlohmann::json& gate) {
    if (!gate.is_object()) {
        return std::nullopt;
    }

    GateRecord record;
    const auto rgb = gate.find("rgb");
    if (rgb == gate.end() || !valid_rgb(*rgb, &record.rgb)) {
        return std::nullopt;
    }

    const auto enabled = gate.find("enabled");
    if (enabled != gate.end() && enabled->is_boolean()) {
        record.enabled = enabled->get<bool>();
    } // native plan: absent/invalid -> false

    const auto role = gate.find("role");
    if (role != gate.end() && role->is_string()) {
        const std::string value = role->get<std::string>();
        if (value == "ours" || value == "opponent") {
            record.role = value;
        }
    }

    const auto ts = gate.find("ts");
    if (ts != gate.end() && ts->is_string()) {
        record.ts = ts->get<std::string>();
    }

    GateLab lab{};
    const auto lab_it = gate.find("lab");
    if (lab_it != gate.end() && valid_lab(*lab_it, &lab)) {
        record.lab = lab;
    }

    return record;
}

bool GateStore::save_atomic() const {
    nlohmann::json payload = nlohmann::json::object();
    nlohmann::json gates = nlohmann::json::object();
    const auto found = root_.find("gates");
    if (found != root_.end() && found->is_object()) {
        for (const auto key : {"1", "2", "3"}) {
            const auto slot = found->find(key);
            if (slot != found->end() && slot->is_object()) {
                gates[key] = *slot;
            }
        }
    }
    for (auto it = root_.begin(); it != root_.end(); ++it) {
        if (it.key() != "version" && it.key() != "gates") {
            payload[it.key()] = it.value();
        }
    }
    payload["version"] = kVersion;
    payload["gates"] = std::move(gates);

    std::string error;
    return atomic_write_text(path_, payload.dump(2), &error);
}

void GateStore::upsert(GateId id, GateColor color, const std::string& role,
                       const std::optional<GateLab>& lab) {
    if (!valid_gate_id(id)) {
        return;
    }
    if (!root_.is_object()) {
        root_ = nlohmann::json::object();
    }
    auto& gates = root_["gates"];
    if (!gates.is_object()) {
        gates = nlohmann::json::object();
    }
    auto& gate = gates[gate_key(id)];
    if (!gate.is_object()) {
        gate = nlohmann::json::object();
    }

    gate["rgb"] = nlohmann::json::array({
        std::clamp(color.r, 0, 255),
        std::clamp(color.g, 0, 255),
        std::clamp(color.b, 0, 255),
    });
    gate["enabled"] = true;
    gate["ts"] = iso_local_timestamp();
    if (lab) {
        gate["lab"] = nlohmann::json::array({
            (*lab)[0], (*lab)[1], (*lab)[2], (*lab)[3], (*lab)[4], (*lab)[5],
        });
    } else {
        GateLab parsed{};
        if (!gate.contains("lab") || !valid_lab(gate.at("lab"), &parsed)) {
            gate["lab"] = nullptr;
        }
    }

    if (role == "ours" || role == "opponent") {
        gate["role"] = role;
    } else {
        gate.erase("role");
    }
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
    if (!valid_gate_id(id)) {
        return std::nullopt;
    }
    const auto gates = root_.find("gates");
    if (gates == root_.end() || !gates->is_object()) {
        return std::nullopt;
    }
    const auto slot = gates->find(gate_key(id));
    if (slot == gates->end()) {
        return std::nullopt;
    }
    return parse_record(*slot);
}

} // namespace camera_cpp
