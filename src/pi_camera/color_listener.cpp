#include "color_listener.hpp"

#include <cerrno>
#include <cstring>
#include <utility>

#include <nlohmann/json.hpp>
#include <zmq.h>

#include "cam_settings.hpp"
#include "common/contracts.hpp"
#include "common/gate_store.hpp"

namespace camera_cpp {
namespace {

constexpr std::size_t kMaxPayloadBytes = 64 * 1024;

bool valid_gate_id(int id) {
    return id >= 1 && id <= 3;
}

GateId to_gate_id(int id) {
    return static_cast<GateId>(id);
}

std::optional<GateColor> color_from_json(const nlohmann::json& value) {
    if (!value.is_array() || value.size() != 3) {
        return std::nullopt;
    }
    GateColor color;
    int channels[3] = {};
    for (std::size_t index = 0; index < 3; ++index) {
        if (!value[index].is_number_integer() && !value[index].is_number_unsigned()) {
            return std::nullopt;
        }
        const int component = value[index].get<int>();
        if (component < 0 || component > 255) {
            return std::nullopt;
        }
        channels[index] = component;
    }
    color.r = channels[0];
    color.g = channels[1];
    color.b = channels[2];
    return color;
}

std::string role_from_json(const nlohmann::json& gate) {
    if (!gate.contains("role") || !gate.at("role").is_string()) {
        return {};
    }
    const auto role = gate.at("role").get<std::string>();
    return role == "ours" || role == "opponent" ? role : std::string{};
}

bool update_store_from_root(const nlohmann::json& root, GateStore& store) {
    const nlohmann::json* gates = nullptr;
    if (root.is_array()) {
        gates = &root;
    } else if (root.is_object() && root.contains("gates") && root.at("gates").is_array()) {
        gates = &root.at("gates");
    }

    bool changed = false;
    if (gates) {
        for (const auto& item : *gates) {
            if (!item.is_object() || !item.contains("id") || !item.contains("rgb")) {
                continue;
            }
            const int id = item.at("id").is_number_integer() ? item.at("id").get<int>() : 0;
            const auto color = color_from_json(item.at("rgb"));
            if (!valid_gate_id(id) || !color) {
                continue;
            }
            store.upsert(to_gate_id(id), *color, role_from_json(item));
            changed = true;
        }
    }

    if (root.is_object() && root.contains("rgb") && !changed && !store.enabled(to_gate_id(1))) {
        // Keep legacy top-level `rgb` as gate-1 compatibility input.
        const auto color = color_from_json(root.at("rgb"));
        if (color) {
            store.upsert(to_gate_id(1), *color, {});
            changed = true;
        }
    }
    if (changed) {
        return store.save_atomic();
    }
    return false;
}

bool bind_subscriber(void*& context, void*& socket, int port, std::string* error) {
    context = zmq_ctx_new();
    if (context == nullptr) {
        if (error) *error = "zmq_ctx_new failed";
        return false;
    }
    socket = zmq_socket(context, ZMQ_SUB);
    if (socket == nullptr) {
        if (error) *error = "zmq_socket failed";
        zmq_ctx_term(context);
        context = nullptr;
        return false;
    }
    int timeout_ms = 100;
    zmq_setsockopt(socket, ZMQ_RCVTIMEO, &timeout_ms, sizeof(timeout_ms));
    int linger_ms = 0;
    zmq_setsockopt(socket, ZMQ_LINGER, &linger_ms, sizeof(linger_ms));
    if (zmq_setsockopt(socket, ZMQ_SUBSCRIBE, "", 0) != 0) {
        if (error) *error = "zmq_setsockopt(ZMQ_SUBSCRIBE) failed";
        zmq_close(socket);
        zmq_ctx_term(context);
        socket = nullptr;
        context = nullptr;
        return false;
    }
    const std::string endpoint = "tcp://*:" + std::to_string(port);
    if (zmq_bind(socket, endpoint.c_str()) != 0) {
        if (error) *error = std::string("zmq_bind failed: ") + std::strerror(errno);
        zmq_close(socket);
        zmq_ctx_term(context);
        socket = nullptr;
        context = nullptr;
        return false;
    }
    return true;
}

void close_subscriber(void*& socket, void*& context) {
    if (socket) {
        zmq_close(socket);
        socket = nullptr;
    }
    if (context) {
        zmq_ctx_term(context);
        context = nullptr;
    }
}

std::optional<std::string> receive(void* socket) {
    char buffer[kMaxPayloadBytes]{};
    const int size = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
    if (size < 0) {
        if (errno == EAGAIN || errno == EINTR) {
            return std::nullopt;
        }
        return std::nullopt;
    }
    return std::string(buffer, static_cast<std::size_t>(size));
}

} // namespace

ColorListener::ColorListener(int bind_port) : bind_port_(bind_port) {}
ColorListener::~ColorListener() { shutdown(); }

bool ColorListener::start(GateStore& store) {
    shutdown();
    stop_.store(false);
    std::string error;
    if (!bind_subscriber(context_, socket_, bind_port_, &error)) {
        return false;
    }
    thread_ = std::thread(&ColorListener::loop, this, std::ref(store));
    return true;
}

void ColorListener::shutdown() {
    stop_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    close_subscriber(socket_, context_);
}

bool ColorListener::update_from_json(const std::string& json_text, GateStore& store, std::string* error) const {
    try {
        const auto root = nlohmann::json::parse(json_text);
        const bool changed = update_store_from_root(root, store);
        return changed;
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return false;
    }
}

void ColorListener::loop(GateStore& store) {
    while (!stop_.load()) {
        const auto payload = receive(socket_);
        if (payload) {
            std::string error;
            update_from_json(*payload, store, &error);
        }
    }
}

CamSettingsListener::CamSettingsListener(int bind_port) : bind_port_(bind_port) {}
CamSettingsListener::~CamSettingsListener() { shutdown(); }

bool CamSettingsListener::start(std::shared_ptr<CommandRunner> runner, const std::string& device) {
    shutdown();
    stop_.store(false);
    std::string error;
    if (!bind_subscriber(context_, socket_, bind_port_, &error)) {
        return false;
    }
    thread_ = std::thread(&CamSettingsListener::loop, this, std::move(runner), device);
    return true;
}

void CamSettingsListener::shutdown() {
    stop_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    close_subscriber(socket_, context_);
}

bool CamSettingsListener::set_last_message(const std::string& json_text, std::string* controls_json) const {
    std::string captured = json_text;
    {
        std::lock_guard<std::mutex> lock(last_message_mutex_);
        last_message_ = captured;
    }

    nlohmann::json root;
    try {
        root = nlohmann::json::parse(json_text);
    } catch (...) {
        return false;
    }
    if (!root.is_object() || root.value("cmd", "") != "set" || !root.contains("controls") || !root.at("controls").is_object()) {
        return false;
    }

    std::unordered_map<std::string, int> controls;
    for (const auto& [name, value] : root.at("controls").items()) {
        if (!value.is_number()) {
            controls[name] = 0;
        } else {
            controls[name] = value.get<int>();
        }
    }
    if (controls_json) {
        *controls_json = root.at("controls").dump();
    }
    return true;
}

void CamSettingsListener::loop(std::shared_ptr<CommandRunner> runner, std::string device) {
    CamSettings settings(std::move(runner));
    while (!stop_.load()) {
        const auto payload = receive(socket_);
        if (!payload) {
            continue;
        }
        std::string controls_json;
        if (!set_last_message(*payload, &controls_json)) {
            continue;
        }
        try {
            const auto controls = nlohmann::json::parse(controls_json).get<std::unordered_map<std::string, int>>();
            settings.apply_controls(device, controls);
        } catch (...) {
            // Keep listening after malformed messages; service stays up.
        }
    }
}

} // namespace camera_cpp
