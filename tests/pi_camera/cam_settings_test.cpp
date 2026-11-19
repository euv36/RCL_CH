#include "pi_camera/cam_settings.hpp"

#include <map>
#include <memory>
#include <vector>

#include "../test_checks.hpp"

using namespace camera_cpp;

class FakeRunner : public CommandRunner {
public:
    std::map<std::string, CommandResult> results;
    mutable std::vector<std::string> calls;

    CommandResult run(const std::string& command) override {
        calls.push_back(command);
        const auto found = results.find(command);
        if (found == results.end()) {
            if (command.find("-c ") == std::string::npos) {
                return {0, "", ""};
            }
            return {1, "", "not found"};
        }
        return found->second;
    }
};

int main() {
    auto fake = std::make_shared<FakeRunner>();
    const std::string device = "/dev/video0";
    for (const auto& [control, value] : std::vector<std::pair<std::string,int>>{
             {"exposure", 5000}, {"auto_exposure", 1}, {"gain", 0},
             {"brightness", 64}, {"gamma", 72}, {"saturation", 128}, {"contrast", 64},
             {"power_line_frequency", 2}, {"exposure_dynamic_framerate", 0}}) {
        const std::string readback = control + ": " + std::to_string(value);
        fake->results["v4l2-ctl --device=" + device + " --set-ctrl=" + control + "=" + std::to_string(value)] = {0, "", ""};
        fake->results["v4l2-ctl --device=" + device + " --get-ctrl=" + control] = {0, readback, ""};
    }
    fake->results["v4l2-ctl --device=" + device + " --get-ctrl=brightness"] = {0, "brightness: -64", ""};
    // Unparsable readback and a failed get must not become an invented "ok".
    fake->results["v4l2-ctl --device=" + device + " --get-ctrl=gamma"] = {0, "gamma: ?", ""};
    fake->results["v4l2-ctl --device=" + device + " --get-ctrl=contrast"] = {1, "", "read failed"};

    CamSettings settings(fake);
    const auto report = settings.apply_controls(device, {
        {"exposure", 999999},
        {"brightness", 1000},
        {"gamma", 100},
        {"contrast", 32},
        {"unknown_control", 7},
    });

    CAMERA_TEST_CHECK(report.controls.at("exposure").requested == "5000");
    CAMERA_TEST_CHECK(report.controls.at("exposure").status == "ok");
    CAMERA_TEST_CHECK(report.controls.at("brightness").status == "mismatch");
    CAMERA_TEST_CHECK(report.controls.at("gamma").status == "unconfirmed");
    CAMERA_TEST_CHECK(report.controls.at("contrast").status == "unconfirmed");
    CAMERA_TEST_CHECK(report.controls.at("auto_exposure").requested == "1");
    CAMERA_TEST_CHECK(report.controls.at("auto_exposure").status == "ok");
    CAMERA_TEST_CHECK(report.controls.at("unknown_control").status == "n/a");
    CAMERA_TEST_CHECK(!report.ok());
    CAMERA_TEST_CHECK(!fake->calls.empty());

    return 0;
}
