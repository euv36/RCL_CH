// pi_core/gate_store.hpp — persistence of the 3 gate colors (Idea 3 contract):
// /home/twox/saved_gates.json on the Pi, next to cv_kostyl.py on the laptop.
//
// Schema (PROMPT_EN.md Idea 3):
// {"version": 1, "gates": {"1": {"rgb": [r,g,b], "lab": [...]|null,
//                                "enabled": true, "ts": "..."}, "2": ..., "3": ...}}
//
// Pi behaviour: at startup the file is loaded and enabled records are applied
// immediately (gates track right after a restart, no laptop needed). When new
// colors arrive via ZMQ 5557 the Pi updates its copy (atomic write, throttled
// to protect the SD card; final flush on graceful exit).
#pragma once

#include <array>
#include <mutex>
#include <string>
#include <vector>

namespace pi_core {

constexpr int kGateStoreSlots = 3;

struct GateRecord {
    bool present = false;             // key "1"/"2"/"3" existed in the file
    bool has_rgb = false;
    std::array<int, 3> rgb = {0, 0, 0};
    bool has_lab = false;             // "lab": [...] present (6 ints)
    std::array<int, 6> lab = {0, 0, 0, 0, 0, 0};
    bool enabled = false;
    std::string ts;
    std::string role;                 // Idea 9 extension; preserved on update
};

// Thread-safe (internal mutex): load()/save()/record() run on the main
// thread, set_gate_from_network() on the ZMQ color-listener thread.
// A failed load leaves the previous state; load errors are reported through
// the return value (caller logs).
class GateStore {
public:
    // Reads path; missing/corrupt file -> false, records left untouched
    // (caller decides: missing file at startup is normal, corrupt is a WARN).
    bool load(const std::string& path);

    // Builds the full JSON document (version 1) and writes it atomically.
    bool save(const std::string& path) const;

    // Marks gate `gate_id` (1-based) with a color received from the laptop.
    // Keeps lab/role fields that may have been written by the laptop side.
    // Sets enabled=true and stamps ts with the current local time (ISO, s).
    // Idea 9: `role` (optional) — "ours"/"opponent" set the field, "unset"
    // clears it, anything else (incl. "") keeps the previous value.
    void set_gate_from_network(int gate_id, const std::array<int, 3>& rgb,
                               const std::string& role = "");

    // Copy-out snapshot; missing/invalid id -> default record (present=false).
    GateRecord record(int gate_id_1based) const;

    // Throttling support for the writer thread/main loop.
    bool dirty() const;
    void clear_dirty();

private:
    std::array<GateRecord, kGateStoreSlots> recs_;
    mutable bool dirty_ = false;   // set by set_gate_from_network
    mutable std::mutex mu_;
};

// ISO-8601 local timestamp, seconds resolution: "2026-11-17T19:04:05".
std::string iso_timestamp_now();

}  // namespace pi_core
