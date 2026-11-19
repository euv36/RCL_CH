#include "cam_settings.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>
#include <utility>

namespace camera_cpp {
namespace {

struct Range { int low; int high; };

// Local mirror of the canonical eight-control table in
// src/common/cam_settings_config.cpp (owned by another task, not edited here).
// The eight bounds below must stay identical to kControls there. auto_exposure
// is deliberately not a user control: it is forced to manual separately, and
// this extra row only clamps any incoming value to 1.
const std::unordered_map<std::string, Range>& ranges() {
    static const std::unordered_map<std::string, Range> table = {
        {"exposure", {1, 5000}},
        {"gain", {0, 100}},
        {"brightness", {-64, 64}},
        {"gamma", {72, 500}},
        {"saturation", {0, 128}},
        {"contrast", {0, 64}},
        {"power_line_frequency", {0, 2}},
        {"exposure_dynamic_framerate", {0, 1}},
        {"auto_exposure", {1, 1}},
    };
    return table;
}

bool parse_readback(const std::string& text, int& value) {
    const auto last = text.find_last_of(": \t");
    std::istringstream tail(last == std::string::npos ? text : text.substr(last + 1));
    int parsed = 0;
    if (!(tail >> parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

std::string quote_value(int value) {
    return std::to_string(value);
}

} // namespace

bool ApplyReport::ok() const {
    for (const auto& [_, item] : controls) {
        // "unconfirmed" is not success: set worked, but the camera never gave
        // us a usable readback to verify it.
        if (item.status == "error" || item.status == "mismatch" || item.status == "unconfirmed") {
            return false;
        }
    }
    return true;
}

CamSettings::CamSettings(std::shared_ptr<CommandRunner> runner) : runner_(std::move(runner)) {}

// Применяет управления камерой через v4l2-ctrl и считывает фактические значения.
// auto_exposure принудительно остаётся 1, попытки выключить его отклоняются.
ApplyReport CamSettings::apply_controls(const std::string& device, const std::unordered_map<std::string, int>& controls) const {
    ApplyReport report;
    if (!runner_) {
        report.controls.emplace("__runner__", ControlResult{"", "error"});
        return report;
    }

    auto apply_one = [&](const std::string& name, int requested, const Range& range) {
        const int applied = std::clamp(requested, range.low, range.high);
        const std::string value = quote_value(applied);
        const std::string set_command = "v4l2-ctl --device=" + device + " --set-ctrl=" + name + "=" + value;
        const CommandResult set = runner_->run(set_command);
        if (set.exit_code != 0) {
            report.controls.emplace(name, ControlResult{value, "error"});
            return;
        }
        const std::string get_command = "v4l2-ctl --device=" + device + " --get-ctrl=" + name;
        const CommandResult get = runner_->run(get_command);
        int readback = 0;
        if (get.exit_code == 0 && parse_readback(get.out, readback)) {
            report.controls.emplace(name, ControlResult{value, readback == applied ? "ok" : "mismatch"});
        } else {
            // Set succeeded, but verification did not: a failed or unparsable
            // readback must never be reported as an acknowledged "ok".
            report.controls.emplace(name, ControlResult{value, "unconfirmed"});
        }
    };

    apply_one("auto_exposure", 1, Range{1, 1});
    for (const auto& [name, requested] : controls) {
        const auto found = ranges().find(name);
        if (found == ranges().end()) {
            report.controls.emplace(name, ControlResult{std::to_string(requested), "n/a"});
            continue;
        }
        apply_one(name, requested, found->second);
    }
    return report;
}

} // namespace camera_cpp
