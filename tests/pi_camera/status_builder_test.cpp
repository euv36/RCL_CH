#include "pi_camera/status_builder.hpp"

#include <cmath>
#include <iostream>

#include "../test_checks.hpp"

using namespace camera_cpp;

int main() {
    StatusBuilder builder;
    std::array<GateStatus, 3> gates{};
    gates[0] = GateStatus{1, true, true, 42.5F, 55.0F, 125.0F, true, false};
    gates[2] = GateStatus{3, true, false, 0.0F, 0.0F, 0.0F, false, false};

    // A disabled gate must never be published as found/held; negative angles
    // from atan2 must be wrapped to the 0..359 wire range.
    GateStatus disabled{};
    disabled.id = 2;
    disabled.enabled = false;
    disabled.found = true;
    disabled.held = true;
    disabled.angle_deg = -45.0F;
    disabled.radius_px = 8.0F;
    disabled.dist_cm = 33.0F;
    gates[1] = disabled;

    const auto status = builder.build(gates, 23.5F, {640, 360}, "pi");
    CAMERA_TEST_CHECK(status.at("fps").get<float>() == 23.5F);
    CAMERA_TEST_CHECK(status.at("src").get<std::string>() == "pi");
    CAMERA_TEST_CHECK(status.at("frame_wh").at(0).get<int>() == 640);
    CAMERA_TEST_CHECK(status.at("frame_wh").at(1).get<int>() == 360);
    CAMERA_TEST_CHECK(status.at("gates").at("1").at("enabled").get<bool>());
    CAMERA_TEST_CHECK(status.at("gates").at("1").at("found").get<bool>());
    CAMERA_TEST_CHECK(status.at("gates").at("1").at("angle").get<float>() == 42.5F);
    CAMERA_TEST_CHECK(status.at("gates").at("3").at("enabled").get<bool>());
    CAMERA_TEST_CHECK(!status.at("gates").at("3").at("found").get<bool>());

    CAMERA_TEST_CHECK(!status.at("gates").at("2").at("found").get<bool>());
    CAMERA_TEST_CHECK(!status.at("gates").at("2").at("held").get<bool>());
    const float wrapped = status.at("gates").at("2").at("angle").get<float>();
    CAMERA_TEST_CHECK(wrapped >= 0.0F && wrapped < 360.0F);
    CAMERA_TEST_CHECK(std::fabs(wrapped - 315.0F) < 1.0e-4F);

    // The wire contract carries gate area; it must be present and numeric.
    CAMERA_TEST_CHECK(status.at("gates").at("1").contains("area"));
    CAMERA_TEST_CHECK(status.at("gates").at("1").at("area").is_number());

    // Non-finite FPS must not be published as a real measurement.
    const auto bad_fps = builder.build(gates, std::nanf(""), {640, 360}, "pi");
    CAMERA_TEST_CHECK(bad_fps.at("fps").get<float>() == 0.0F);

    return 0;
}
