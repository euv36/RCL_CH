// pi_core/gate_store.cpp — see gate_store.hpp
#include "gate_store.hpp"

#include "atomic_file.hpp"
#include "log.hpp"

#include <nlohmann/json.hpp>

#include <cstdio>
#include <ctime>

namespace pi_core {

namespace {
using json = nlohmann::json;

std::string gate_key(int gate_id_1based) {
    return std::to_string(gate_id_1based);
}
}  // namespace

bool GateStore::load(const std::string& path) {
    std::string data;
    if (!read_file(path, data)) return false;  // missing/unreadable: normal
    std::lock_guard<std::mutex> lock(mu_);

    json doc = json::parse(data, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        log_msg("saved_gates.json повреждён (не JSON) — файл игнорируется",
                "WARN");
        return false;
    }

    auto gates_it = doc.find("gates");
    if (gates_it == doc.end() || !gates_it->is_object()) {
        log_msg("saved_gates.json без секции gates — файл игнорируется", "WARN");
        return false;
    }

    for (int id = 1; id <= kGateStoreSlots; ++id) {
        auto it = gates_it->find(gate_key(id));
        if (it == gates_it->end() || !it->is_object()) continue;
        GateRecord& rec = recs_[id - 1];
        rec = GateRecord{};  // reset before filling
        rec.present = true;

        auto rgb = it->find("rgb");
        if (rgb != it->end() && rgb->is_array() && rgb->size() == 3) {
            bool ok = true;
            for (int i = 0; i < 3; ++i) {
                const auto& v = (*rgb)[i];
                if (!v.is_number()) { ok = false; break; }
                const double d = v.get<double>();
                rec.rgb[i] = static_cast<int>(d < 0 ? 0 : (d > 255 ? 255 : d));
            }
            rec.has_rgb = ok;
        }
        auto lab = it->find("lab");
        if (lab != it->end() && lab->is_array() && lab->size() == 6) {
            bool ok = true;
            for (int i = 0; i < 6; ++i) {
                const auto& v = (*lab)[i];
                if (!v.is_number()) { ok = false; break; }
                rec.lab[i] = v.get<int>();
            }
            rec.has_lab = ok;
        }
        auto en = it->find("enabled");
        rec.enabled = (en != it->end() && en->is_boolean()) ? en->get<bool>()
                                                            : false;
        auto ts = it->find("ts");
        if (ts != it->end() && ts->is_string()) rec.ts = ts->get<std::string>();
        auto role = it->find("role");
        if (role != it->end() && role->is_string())
            rec.role = role->get<std::string>();
    }
    return true;
}

bool GateStore::save(const std::string& path) const {
    std::lock_guard<std::mutex> lock(mu_);
    json gates = json::object();
    for (int id = 1; id <= kGateStoreSlots; ++id) {
        const GateRecord& rec = recs_[id - 1];
        json slot = json::object();
        if (rec.has_rgb) {
            slot["rgb"] = {rec.rgb[0], rec.rgb[1], rec.rgb[2]};
        } else {
            slot["rgb"] = nullptr;
        }
        if (rec.has_lab) {
            slot["lab"] = {rec.lab[0], rec.lab[1], rec.lab[2], rec.lab[3],
                           rec.lab[4], rec.lab[5]};
        } else {
            slot["lab"] = nullptr;
        }
        slot["enabled"] = rec.enabled;
        slot["ts"] = rec.ts.empty() ? iso_timestamp_now() : rec.ts;
        if (!rec.role.empty()) slot["role"] = rec.role;  // Idea 9 passthrough
        gates[gate_key(id)] = slot;
    }
    json doc;
    doc["version"] = 1;
    doc["gates"] = gates;
    return atomic_write(path, doc.dump(2) + "\n");
}

void GateStore::set_gate_from_network(int gate_id,
                                      const std::array<int, 3>& rgb,
                                      const std::string& role) {
    if (gate_id < 1 || gate_id > kGateStoreSlots) return;
    std::lock_guard<std::mutex> lock(mu_);
    GateRecord& rec = recs_[gate_id - 1];
    rec.present = true;
    rec.has_rgb = true;
    rec.rgb = rgb;
    rec.enabled = true;
    // Idea 9: role rides along when the laptop sent one. The wire never
    // carries "unset" (absent field = keep), but accept it for symmetry
    // with the Python gate_store (explicit clear); unknown -> keep.
    if (role == "ours" || role == "opponent") {
        rec.role = role;
    } else if (role == "unset") {
        rec.role.clear();
    }
    rec.ts = iso_timestamp_now();
    dirty_ = true;  // main loop coalesces the atomic write (max 1 per 2 s)
}

GateRecord GateStore::record(int gate_id_1based) const {
    if (gate_id_1based < 1 || gate_id_1based > kGateStoreSlots)
        return GateRecord{};
    std::lock_guard<std::mutex> lock(mu_);
    return recs_[gate_id_1based - 1];
}

bool GateStore::dirty() const {
    std::lock_guard<std::mutex> lock(mu_);
    return dirty_;
}

void GateStore::clear_dirty() {
    std::lock_guard<std::mutex> lock(mu_);
    dirty_ = false;
}

std::string iso_timestamp_now() {
    char buf[32];
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::snprintf(buf, sizeof(buf), "%04d-%02d-%02dT%02d:%02d:%02d",
                  tmv.tm_year + 1900, tmv.tm_mon + 1, tmv.tm_mday,
                  tmv.tm_hour, tmv.tm_min, tmv.tm_sec);
    return std::string(buf);
}

}  // namespace pi_core
