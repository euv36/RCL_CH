// Тест издателя ворот/настроек: фейковые порты 15557/15559, без Pi.
#include <zmq.h>

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "../test_checks.hpp"
#include "laptop/gate_publisher.hpp"

namespace {

using json = nlohmann::json;

std::string receive_text(void* socket) {
    char buffer[2048];
    const int size = zmq_recv(socket, buffer, sizeof(buffer), 0);
    CAMERA_TEST_CHECK(size >= 0);
    return std::string(buffer, static_cast<std::size_t>(size));
}

} // namespace

int main() {
    using camera_cpp::laptop::GateColorOut;
    using camera_cpp::laptop::GatePublisher;

    // Формат legacy: gates[].id/rgb/role и rgb только для ворот 1.
    const std::vector<GateColorOut> gates = {
        {1, {10, 20, 30}, "ours"},
        {2, {40, 50, 60}, "opponent"},
    };
    const json payload = json::parse(camera_cpp::laptop::build_gate_payload(gates));
    CAMERA_TEST_CHECK(payload.contains("gates"));
    CAMERA_TEST_CHECK(payload["gates"].size() == 2);
    CAMERA_TEST_CHECK(payload["gates"][0]["id"] == 1);
    CAMERA_TEST_CHECK(payload["gates"][0]["rgb"] == json::array({10, 20, 30}));
    CAMERA_TEST_CHECK(payload["gates"][0]["role"] == "ours");
    CAMERA_TEST_CHECK(payload["gates"][1]["role"] == "opponent");
    CAMERA_TEST_CHECK(payload["rgb"] == json::array({10, 20, 30}));

    // Роль unset не добавляет поле.
    const json unset = json::parse(camera_cpp::laptop::build_gate_payload(
        {{3, {1, 2, 3}, "unset"}}));
    CAMERA_TEST_CHECK(!unset["gates"][0].contains("role"));
    CAMERA_TEST_CHECK(!unset.contains("rgb"));

    // Пустой список.
    CAMERA_TEST_CHECK(camera_cpp::laptop::build_gate_payload({}) == R"({"gates":[]})");

    // Интеграция: SUB получает сообщение после publish_gates.
    void* context = zmq_ctx_new();
    CAMERA_TEST_CHECK(context != nullptr);
    void* subscriber = zmq_socket(context, ZMQ_SUB);
    CAMERA_TEST_CHECK(subscriber != nullptr);
    CAMERA_TEST_CHECK(zmq_setsockopt(subscriber, ZMQ_SUBSCRIBE, "", 0) == 0);
    CAMERA_TEST_CHECK(zmq_connect(subscriber, "tcp://127.0.0.1:15557") == 0);

    GatePublisher publisher;
    CAMERA_TEST_CHECK(publisher.bind(15557, 15559));
    bool received = false;
    for (int attempt = 0; attempt < 60 && !received; ++attempt) {
        publisher.publish_gates(gates);
        zmq_pollitem_t item{subscriber, 0, ZMQ_POLLIN, 0};
        if (zmq_poll(&item, 1, 20) > 0) received = true;
    }
    CAMERA_TEST_CHECK(received);
    const json delivered = json::parse(receive_text(subscriber));
    CAMERA_TEST_CHECK(delivered["gates"].size() == 2);

    // Настройки камеры: команда set с восемью контролами.
    void* cam_subscriber = zmq_socket(context, ZMQ_SUB);
    CAMERA_TEST_CHECK(cam_subscriber != nullptr);
    CAMERA_TEST_CHECK(zmq_setsockopt(cam_subscriber, ZMQ_SUBSCRIBE, "", 0) == 0);
    CAMERA_TEST_CHECK(zmq_connect(cam_subscriber, "tcp://127.0.0.1:15559") == 0);
    const std::array<float, 8> controls{300, 0, 0, 100, 60, 32, 1, 0};
    bool cam_received = false;
    for (int attempt = 0; attempt < 60 && !cam_received; ++attempt) {
        publisher.publish_cam_settings(controls);
        zmq_pollitem_t item{cam_subscriber, 0, ZMQ_POLLIN, 0};
        if (zmq_poll(&item, 1, 20) > 0) cam_received = true;
    }
    CAMERA_TEST_CHECK(cam_received);
    const json cam = json::parse(receive_text(cam_subscriber));
    CAMERA_TEST_CHECK(cam["cmd"] == "set");
    CAMERA_TEST_CHECK(cam["controls"]["exposure"] == 300);
    CAMERA_TEST_CHECK(cam["controls"]["exposure_dynamic_framerate"] == 0);
    CAMERA_TEST_CHECK(cam["controls"].size() == 8);

    publisher.close();
    zmq_close(cam_subscriber);
    zmq_close(subscriber);
    zmq_ctx_term(context);
    return 0;
}
