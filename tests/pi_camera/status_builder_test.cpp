#include "pi_camera/status_builder.hpp"

#include <iostream>

#include "../test_checks.hpp"

using namespace camera_cpp;

int main() {
    StatusBuilder builder;
    std::array<GateStatus, 3> gates{};
    gates[0] = GateStatus{1, true, true, 42.5F, 55.0F, 125.0F, true, false};
    gates[2] = GateStatus{3, true, false, 0.0F, 0.0F, 0.0F, false, false};

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

    return 0;
}
