#include "laptop/gate_publisher.hpp"

#include <zmq.h>

#include <algorithm>
#include <cstddef>

#include <nlohmann/json.hpp>

#include "cam_settings_config.hpp"

namespace camera_cpp::laptop {

namespace {

constexpr int kSendHighWaterMark = 16;

// Отправляет текст без блокировки: потеря сообщения допустима, UI важнее.
void send_text(void* socket, const std::string& text) {
    if (socket == nullptr) return;
    (void)zmq_send(socket, text.data(), text.size(), ZMQ_DONTWAIT);
}

} // namespace

std::string build_gate_payload(const std::vector<GateColorOut>& gates) {
    std::vector<GateColorOut> sorted = gates;
    std::sort(sorted.begin(), sorted.end(),
              [](const GateColorOut& a, const GateColorOut& b) { return a.id < b.id; });

    nlohmann::json root;
    root["gates"] = nlohmann::json::array();
    for (const GateColorOut& gate : sorted) {
        nlohmann::json item;
        item["id"] = gate.id;
        item["rgb"] = nlohmann::json::array({gate.rgb[0], gate.rgb[1], gate.rgb[2]});
        if (gate.role == "ours" || gate.role == "opponent") {
            item["role"] = gate.role;
        }
        root["gates"].push_back(std::move(item));
        if (gate.id == 1) {
            root["rgb"] = nlohmann::json::array({gate.rgb[0], gate.rgb[1], gate.rgb[2]});
        }
    }
    return root.dump();
}

GatePublisher::~GatePublisher() {
    close();
}

bool GatePublisher::bind(int colors_port, int cam_port) {
    close();
    context_ = zmq_ctx_new();
    if (context_ == nullptr) return false;

    colors_socket_ = zmq_socket(context_, ZMQ_PUB);
    cam_socket_ = zmq_socket(context_, ZMQ_PUB);
    if (colors_socket_ == nullptr || cam_socket_ == nullptr) {
        close();
        return false;
    }

    const int high_water_mark = kSendHighWaterMark;
    (void)zmq_setsockopt(colors_socket_, ZMQ_SNDHWM, &high_water_mark, sizeof(high_water_mark));
    (void)zmq_setsockopt(cam_socket_, ZMQ_SNDHWM, &high_water_mark, sizeof(high_water_mark));

    const std::string colors_endpoint = "tcp://*:" + std::to_string(colors_port);
    const std::string cam_endpoint = "tcp://*:" + std::to_string(cam_port);
    if (zmq_bind(colors_socket_, colors_endpoint.c_str()) != 0 ||
        zmq_bind(cam_socket_, cam_endpoint.c_str()) != 0) {
        close();
        return false;
    }
    return true;
}

void GatePublisher::publish_gates(const std::vector<GateColorOut>& gates) {
    send_text(colors_socket_, build_gate_payload(gates));
}

void GatePublisher::publish_cam_settings(const std::array<float, 8>& controls) {
    const auto ranges = CamSettingsConfig::ranges();
    nlohmann::json message;
    message["cmd"] = "set";
    message["controls"] = nlohmann::json::object();
    for (std::size_t index = 0; index < ranges.size() && index < controls.size(); ++index) {
        message["controls"][ranges[index].first] = static_cast<int>(controls[index]);
    }
    send_text(cam_socket_, message.dump());
}

void GatePublisher::close() {
    if (colors_socket_ != nullptr) {
        zmq_close(colors_socket_);
        colors_socket_ = nullptr;
    }
    if (cam_socket_ != nullptr) {
        zmq_close(cam_socket_);
        cam_socket_ = nullptr;
    }
    if (context_ != nullptr) {
        zmq_ctx_term(context_);
        context_ = nullptr;
    }
}

bool GatePublisher::bound() const noexcept {
    return colors_socket_ != nullptr && cam_socket_ != nullptr;
}

} // namespace camera_cpp::laptop
