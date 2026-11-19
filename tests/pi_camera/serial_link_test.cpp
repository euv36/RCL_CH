#include "pi_camera/serial_link.hpp"

#include "../test_checks.hpp"

#include <filesystem>

using camera_cpp::GateSerialState;
using camera_cpp::SerialCommand;
using camera_cpp::serial_port_available;
using camera_cpp::update_gate_serial;

int main() {
    GateSerialState state;

    const SerialCommand first = update_gate_serial(state, 1, true, 10.0F, 123.9F, 1.0);
    CAMERA_TEST_CHECK(first.send);
    CAMERA_TEST_CHECK(first.line == "G1,10,123\n");

    const SerialCommand stable = update_gate_serial(state, 1, true, 10.0F, 123.0F, 1.05);
    CAMERA_TEST_CHECK(!stable.send);

    const SerialCommand small = update_gate_serial(state, 1, true, 12.0F, 123.0F, 1.05);
    CAMERA_TEST_CHECK(!small.send);

    const SerialCommand too_soon = update_gate_serial(state, 1, true, 15.0F, 123.0F, 1.08);
    CAMERA_TEST_CHECK(!too_soon.send);

    const SerialCommand changed = update_gate_serial(state, 1, true, 15.0F, 123.0F, 1.20);
    CAMERA_TEST_CHECK(changed.send);
    CAMERA_TEST_CHECK(changed.line == "G1,15,123\n");

    const SerialCommand near_wrap = update_gate_serial(state, 1, true, 359.0F, 123.0F, 1.30);
    CAMERA_TEST_CHECK(near_wrap.send);
    const SerialCommand wrapped_small = update_gate_serial(state, 1, true, 1.0F, 123.0F, 1.35);
    CAMERA_TEST_CHECK(!wrapped_small.send);
    const SerialCommand wrapped = update_gate_serial(state, 1, true, 2.0F, 123.0F, 1.50);
    CAMERA_TEST_CHECK(wrapped.send);
    CAMERA_TEST_CHECK(wrapped.line == "G1,2,123\n");

    const SerialCommand lost = update_gate_serial(state, 1, false, 0.0F, 0.0F, 2.3);
    CAMERA_TEST_CHECK(lost.send);
    CAMERA_TEST_CHECK(lost.line == "G1,-1,0\n");
    const SerialCommand lost_again = update_gate_serial(state, 1, false, 0.0F, 0.0F, 2.4);
    CAMERA_TEST_CHECK(!lost_again.send);

    const SerialCommand reacquired = update_gate_serial(state, 1, true, 42.0F, 7.0F, 2.5);
    CAMERA_TEST_CHECK(reacquired.send);
    CAMERA_TEST_CHECK(reacquired.line == "G1,42,7\n");

    const std::filesystem::path missing =
        std::filesystem::temp_directory_path() / "camera_cpp_serial_port_does_not_exist";
    CAMERA_TEST_CHECK(!serial_port_available(missing));
    return 0;
}
