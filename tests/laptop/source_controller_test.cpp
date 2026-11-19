#include "laptop/source_controller.hpp"

#include <string>

#include "../test_checks.hpp"

using camera_cpp::laptop::AppState;
using camera_cpp::laptop::ConnectRequest;
using camera_cpp::laptop::apply_connect;
using camera_cpp::laptop::append_source_text;
using camera_cpp::laptop::handle_source_backspace;
using camera_cpp::laptop::parse_connect;

int main() {
    const ConnectRequest pi = parse_connect("10.132.122.51");
    CAMERA_TEST_CHECK(pi.valid());
    CAMERA_TEST_CHECK(pi.kind == AppState::SourceKind::Pi);
    CAMERA_TEST_CHECK(pi.host == "10.132.122.51");
    CAMERA_TEST_CHECK(pi.port == 5555);
    CAMERA_TEST_CHECK(pi.error.empty());

    const ConnectRequest local_auto = parse_connect("local");
    CAMERA_TEST_CHECK(local_auto.valid());
    CAMERA_TEST_CHECK(local_auto.kind == AppState::SourceKind::Local);
    CAMERA_TEST_CHECK(local_auto.camera_index == 0);

    const ConnectRequest local_one = parse_connect("1");
    CAMERA_TEST_CHECK(local_one.valid());
    CAMERA_TEST_CHECK(local_one.kind == AppState::SourceKind::Local);
    CAMERA_TEST_CHECK(local_one.camera_index == 1);

    const ConnectRequest named_port = parse_connect("raspberrypi.local:5557");
    CAMERA_TEST_CHECK(named_port.valid());
    CAMERA_TEST_CHECK(named_port.kind == AppState::SourceKind::Pi);
    CAMERA_TEST_CHECK(named_port.host == "raspberrypi.local");
    CAMERA_TEST_CHECK(named_port.port == 5557);

    const ConnectRequest invalid_empty = parse_connect("");
    CAMERA_TEST_CHECK(!invalid_empty.valid());
    CAMERA_TEST_CHECK(!invalid_empty.error.empty());

    const ConnectRequest invalid_text = parse_connect("not a host!");
    CAMERA_TEST_CHECK(!invalid_text.valid());
    CAMERA_TEST_CHECK(!invalid_text.error.empty());

    const ConnectRequest invalid_index = parse_connect("999999");
    CAMERA_TEST_CHECK(!invalid_index.valid());
    CAMERA_TEST_CHECK(!invalid_index.error.empty());

    const ConnectRequest invalid_port = parse_connect("10.0.0.1:70000");
    CAMERA_TEST_CHECK(!invalid_port.valid());
    CAMERA_TEST_CHECK(!invalid_port.error.empty());

    AppState state;
    CAMERA_TEST_CHECK(state.camera_source_text == "10.132.122.51");
    apply_connect(state, pi);
    CAMERA_TEST_CHECK(state.source_kind == AppState::SourceKind::Pi);
    CAMERA_TEST_CHECK(state.pi_host == "10.132.122.51");
    CAMERA_TEST_CHECK(state.pi_port == 5555);
    CAMERA_TEST_CHECK(state.connect_error.empty());

    apply_connect(state, invalid_text);
    CAMERA_TEST_CHECK(state.source_kind == AppState::SourceKind::None);
    CAMERA_TEST_CHECK(!state.connect_error.empty());

    AppState input;
    input.camera_source_text = "10";
    append_source_text(input, ".0.1");
    CAMERA_TEST_CHECK(input.camera_source_text == "10.0.1");
    append_source_text(input, std::string("\x01\n", 2));
    CAMERA_TEST_CHECK(input.camera_source_text == "10.0.1");
    handle_source_backspace(input);
    CAMERA_TEST_CHECK(input.camera_source_text == "10.0.");

    return 0;
}
