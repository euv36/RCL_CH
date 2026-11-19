// Loopback integration check for Task 11. The service is started with a fake
// V4L2 capture backend and a fake v4l2-ctl command runner; only 127.0.0.1
// ports 15555/15557/15559 are used. No camera or external host is touched.
#include <zmq.h>

#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <thread>

#include "../test_checks.hpp"

namespace fs = std::filesystem;
using json = nlohmann::json;
using test_clock = std::chrono::steady_clock;

#ifndef CAMERA_SERVICE_PATH
#define CAMERA_SERVICE_PATH "camera_service"
#endif

namespace {

bool wait_for_file(const fs::path& path, int timeout_ms) {
    const auto deadline = test_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (test_clock::now() < deadline) {
        std::error_code ec;
        if (fs::exists(path, ec) && fs::file_size(path, ec) > 0) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

bool wait_for_text(const fs::path& path, const std::string& needle, int timeout_ms) {
    const auto deadline = test_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (test_clock::now() < deadline) {
        std::ifstream in(path);
        if (in) {
            std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
            if (text.find(needle) != std::string::npos) {
                return true;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return false;
}

json read_json_file_retry(const fs::path& path, int timeout_ms) {
    const auto deadline = test_clock::now() + std::chrono::milliseconds(timeout_ms);
    while (test_clock::now() < deadline) {
        std::ifstream in(path);
        if (in) {
            try {
                return json::parse(in);
            } catch (...) {
                // Atomic rename can be observed mid-write on some filesystems.
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    return json();
}

void* connect_socket(void* context, int type, const std::string& endpoint, int timeout_ms) {
    void* socket = zmq_socket(context, type);
    CAMERA_TEST_CHECK(socket != nullptr);
    if (type == ZMQ_SUB) {
        CAMERA_TEST_CHECK(zmq_setsockopt(socket, ZMQ_SUBSCRIBE, "", 0) == 0);
    }
    CAMERA_TEST_CHECK(zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms)) == 0);
    CAMERA_TEST_CHECK(zmq_connect(socket, endpoint.c_str()) == 0);
    return socket;
}

bool send_json(void* socket, const json& value) {
    const std::string text = value.dump();
    return zmq_send(socket, text.data(), text.size(), 0) == static_cast<int>(text.size());
}

} // namespace

int main() {
    const auto unique = std::chrono::system_clock::now().time_since_epoch().count();
    const fs::path dir = fs::temp_directory_path() / ("camera_cpp_zmq_loopback_" + std::to_string(unique));
    std::error_code ec;
    fs::create_directories(dir, ec);
    CAMERA_TEST_CHECK(!ec);

    const fs::path ready = dir / "ready";
    const fs::path state = dir / "state.json";
    const fs::path v4l2_log = dir / "v4l2.log";
    const fs::path service_log = dir / "service.log";
    const fs::path gates_file = dir / "saved_gates.json";

    const std::string command =
        std::string("\"") + CAMERA_SERVICE_PATH + "\""
        " --fake-camera --bind-host 127.0.0.1"
        " --port-video 15555 --port-colors 15557 --cam-settings-port 15559"
        " --fake-v4l2-log \"" + v4l2_log.string() + "\""
        " --gates-file \"" + gates_file.string() + "\""
        " --test-ready-file \"" + ready.string() + "\""
        " --test-state-file \"" + state.string() + "\""
        " --frame-delay-ms 50 --max-frames 200"
        " > \"" + service_log.string() + "\" 2>&1";
    // cmd.exe /c strips the first and last quote of an unbraced command, which
    // mangles quoted Windows paths. An outer quote pair preserves them.
    const std::string shell_command = "\"" + command + "\"";
    std::thread service_thread([&]() { std::system(shell_command.c_str()); });

    if (!wait_for_file(ready, 15000)) {
        std::ifstream debug_log(service_log);
        std::cerr << "service log: "
                  << std::string((std::istreambuf_iterator<char>(debug_log)), std::istreambuf_iterator<char>())
                  << std::endl;
    }
    CAMERA_TEST_CHECK(wait_for_file(ready, 1000));
    void* context = zmq_ctx_new();
    CAMERA_TEST_CHECK(context != nullptr);

    // 1. Multipart [JPEG-like, JSON] on 15555.
    void* video = connect_socket(context, ZMQ_SUB, "tcp://127.0.0.1:15555", 3000);
    zmq_msg_t image;
    zmq_msg_t status_msg;
    zmq_msg_init(&image);
    zmq_msg_init(&status_msg);
    CAMERA_TEST_CHECK(zmq_recvmsg(video, &image, 0) >= 0);
    int more = 0;
    std::size_t more_size = sizeof(more);
    CAMERA_TEST_CHECK(zmq_getsockopt(video, ZMQ_RCVMORE, &more, &more_size) == 0);
    CAMERA_TEST_CHECK(more == 1);
    CAMERA_TEST_CHECK(zmq_recvmsg(video, &status_msg, 0) >= 0);
    CAMERA_TEST_CHECK(zmq_msg_size(&image) > 4);
    const auto* jpeg = static_cast<const unsigned char*>(zmq_msg_data(&image));
    const std::size_t jpeg_size = zmq_msg_size(&image);
    CAMERA_TEST_CHECK(jpeg[0] == 0xFF && jpeg[1] == 0xD8);
    CAMERA_TEST_CHECK(jpeg[jpeg_size - 2] == 0xFF && jpeg[jpeg_size - 1] == 0xD9);

    const std::string status_text(static_cast<const char*>(zmq_msg_data(&status_msg)), zmq_msg_size(&status_msg));
    const json status = json::parse(status_text);
    CAMERA_TEST_CHECK(status.at("src").get<std::string>() == "pi");
    CAMERA_TEST_CHECK(status.at("gates").is_object());
    CAMERA_TEST_CHECK(status.at("gates").at("1").contains("area"));
    zmq_msg_close(&image);
    zmq_msg_close(&status_msg);
    zmq_close(video);

    // 2. Color message on 15557 must update the gate and its detection threshold.
    // The service binds SUB, so the probe must publish (PUSH/SUB is not a
    // compatible ZMQ pattern). Allow the subscription a moment to propagate.
    void* colors = connect_socket(context, ZMQ_PUB, "tcp://127.0.0.1:15557", 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const json color_message = {
        {"gates", json::array({{
            {"id", 1},
            {"rgb", json::array({200, 40, 40})},
            {"lab", json::array({50, 80, -10, 20, -20, 10})},
            {"role", "ours"},
        }})},
    };
    for (int attempt = 0; attempt < 5; ++attempt) {
        CAMERA_TEST_CHECK(send_json(colors, color_message));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    json snapshot;
    const auto gate_deadline = test_clock::now() + std::chrono::milliseconds(5000);
    while (test_clock::now() < gate_deadline) {
        snapshot = read_json_file_retry(state, 200);
        if (!snapshot.is_null() && snapshot.contains("gates") &&
            snapshot.at("gates").at(0).value("configured", false)) {
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    CAMERA_TEST_CHECK(!snapshot.is_null());
    const json gate = snapshot.at("gates").at(0);
    CAMERA_TEST_CHECK(gate.at("enabled").get<bool>());
    CAMERA_TEST_CHECK(gate.at("configured").get<bool>());
    // GateStore LAB (L 0..100, a/b -128..127) -> OpenCV 8-bit LAB.
    CAMERA_TEST_CHECK(gate.at("l_min").get<int>() == 128);
    CAMERA_TEST_CHECK(gate.at("l_max").get<int>() == 204);
    CAMERA_TEST_CHECK(gate.at("a_min").get<int>() == 118);
    CAMERA_TEST_CHECK(gate.at("a_max").get<int>() == 148);
    CAMERA_TEST_CHECK(gate.at("b_min").get<int>() == 108);
    CAMERA_TEST_CHECK(gate.at("b_max").get<int>() == 138);
    zmq_close(colors);

    // 3. CAM SET on 15559 must be applied through the fake v4l2-ctl backend.
    void* settings = connect_socket(context, ZMQ_PUB, "tcp://127.0.0.1:15559", 1000);
    std::this_thread::sleep_for(std::chrono::milliseconds(300));
    const json cam_set = {
        {"cmd", "set"},
        {"controls", {{"brightness", 42}, {"gamma", 100}, {"unknown_control", 7}}},
    };
    for (int attempt = 0; attempt < 20; ++attempt) {
        CAMERA_TEST_CHECK(send_json(settings, cam_set));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    CAMERA_TEST_CHECK(wait_for_text(v4l2_log, "--set-ctrl=brightness=42", 5000));
    CAMERA_TEST_CHECK(wait_for_text(v4l2_log, "--set-ctrl=gamma=100", 5000));
    CAMERA_TEST_CHECK(wait_for_text(v4l2_log, "--set-ctrl=auto_exposure=1", 5000));

    std::ifstream log_in(v4l2_log);
    const std::string log_text((std::istreambuf_iterator<char>(log_in)), std::istreambuf_iterator<char>());
    CAMERA_TEST_CHECK(log_text.find("--set-ctrl=unknown_control") == std::string::npos);
    CAMERA_TEST_CHECK(log_text.find("--get-ctrl=brightness") != std::string::npos);
    zmq_close(settings);

    // No ack is invented on either listener port.
    void* no_ack = connect_socket(context, ZMQ_SUB, "tcp://127.0.0.1:15559", 150);
    char sink[512];
    CAMERA_TEST_CHECK(zmq_recv(no_ack, sink, sizeof(sink), 0) == -1);
    zmq_close(no_ack);

    zmq_ctx_term(context);
    service_thread.join();

    fs::remove_all(dir, ec);
    return 0;
}
