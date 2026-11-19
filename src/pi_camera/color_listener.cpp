#include "color_listener.hpp"

#include <algorithm>
#include <cerrno>
#include <cmath>
#include <cstring>
#include <iostream>
#include <optional>
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

// Saved legacy Lab bounds are L 0..100 and a/b -128..127. Reject anything else
// here so GateStore::upsert never silently replaces a good range with null.
std::optional<GateLab> lab_from_json(const nlohmann::json& value) {
    if (!value.is_array() || value.size() != 6) {
        return std::nullopt;
    }
    GateLab lab{};
    for (std::size_t index = 0; index < lab.size(); ++index) {
        const auto& component = value[index];
        if (!component.is_number()) {
            return std::nullopt;
        }
        const double number = component.get<double>();
        if (!std::isfinite(number)) {
            return std::nullopt;
        }
        const double low = index < 2 ? 0.0 : -128.0;
        const double high = index < 2 ? 100.0 : 127.0;
        if (number < low || number > high) {
            return std::nullopt;
        }
        lab[index] = static_cast<int>(number);
    }
    return lab;
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
            const auto lab = item.contains("lab") ? lab_from_json(item.at("lab")) : std::nullopt;
            store.upsert(to_gate_id(id), *color, role_from_json(item), lab);
            changed = true;
        }
    }

    if (root.is_object() && root.contains("rgb") && !changed) {
        // Keep legacy top-level `rgb` as gate-1 compatibility input.
        const auto color = color_from_json(root.at("rgb"));
        if (color) {
            const auto lab = root.contains("lab") ? lab_from_json(root.at("lab")) : std::nullopt;
            store.upsert(to_gate_id(1), *color, {}, lab);
            changed = true;
        }
    }
    if (changed) {
        return store.save_atomic();
    }
    return false;
}

bool bind_subscriber(void*& context, void*& socket, const std::string& bind_host, int port, std::string* error) {
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
    // A one-message queue keeps only the newest settings/colors. ZMQ_CONFLATE
    // is intentionally not used: it interacts badly with multipart messages.
    int receive_hwm = 1;
    if (zmq_setsockopt(socket, ZMQ_RCVHWM, &receive_hwm, sizeof(receive_hwm)) != 0) {
        if (error) *error = "zmq_setsockopt(ZMQ_RCVHWM) failed";
    }
    if (zmq_setsockopt(socket, ZMQ_SUBSCRIBE, "", 0) != 0) {
        if (error) *error = "zmq_setsockopt(ZMQ_SUBSCRIBE) failed";
        zmq_close(socket);
        zmq_ctx_term(context);
        socket = nullptr;
        context = nullptr;
        return false;
    }
    const std::string endpoint = "tcp://" + bind_host + ":" + std::to_string(port);
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
    const int received = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
    if (received < 0) {
        return std::nullopt; // timeout, interrupt or terminal error: keep polling
    }
    const std::size_t first_size = std::min(static_cast<std::size_t>(received), sizeof(buffer) - 1);
    std::string result(buffer, first_size);

    // Drain multipart remainder and every already-queued frame. Only the last
    // complete message is applied, so a stale CAM SET cannot win over a newer
    // one that arrived while the listener was working.
    for (;;) {
        int more = 0;
        std::size_t more_size = sizeof(more);
        if (zmq_getsockopt(socket, ZMQ_RCVMORE, &more, &more_size) != 0 || more) {
            const int part = zmq_recv(socket, buffer, sizeof(buffer) - 1, 0);
            if (part < 0) {
                break;
            }
            continue;
        }
        const int next = zmq_recv(socket, buffer, sizeof(buffer) - 1, ZMQ_DONTWAIT);
        if (next < 0) {
            break;
        }
        result.assign(buffer, std::min(static_cast<std::size_t>(next), sizeof(buffer) - 1));
    }
    return result;
}

} // namespace

ColorListener::ColorListener(int bind_port) : bind_port_(bind_port) {}
ColorListener::~ColorListener() { shutdown(); }

// Поднимает ZMQ SUB на порту цветов и запускает фоновый поток приёма.
// Все обновления GateStore защищаются общим мьютексом с основным циклом.
bool ColorListener::start(GateStore& store, const std::string& bind_host, std::mutex* store_mutex) {
    shutdown();
    stop_.store(false);
    store_mutex_ = store_mutex;
    std::string error;
    if (!bind_subscriber(context_, socket_, bind_host, bind_port_, &error)) {
        return false;
    }
    thread_ = std::thread(&ColorListener::loop, this, std::ref(store), store_mutex_);
    return true;
}

void ColorListener::shutdown() {
    stop_.store(true);
    if (thread_.joinable()) {
        thread_.join();
    }
    close_subscriber(socket_, context_);
    store_mutex_ = nullptr;
}

// Разбирает legacy JSON цветов ворот и применяет его к хранилищу.
// Неизвестные поля игнорируются, ошибка возвращается через error.
bool ColorListener::update_from_json(const std::string& json_text, GateStore& store, std::string* error) const {
    try {
        const auto root = nlohmann::json::parse(json_text);
        return update_store_from_root(root, store);
    } catch (const std::exception& ex) {
        if (error) *error = ex.what();
        return false;
    }
}

void ColorListener::loop(GateStore& store, std::mutex* store_mutex) {
    while (!stop_.load()) {
        const auto payload = receive(socket_);
        if (!payload) {
            continue;
        }
        std::string error;
        if (store_mutex) {
            std::lock_guard<std::mutex> lock(*store_mutex);
            update_from_json(*payload, store, &error);
        } else {
            update_from_json(*payload, store, &error);
        }
        if (!error.empty()) {
            std::cerr << "color message ignored: " << error << std::endl;
        }
    }
}

CamSettingsListener::CamSettingsListener(int bind_port) : bind_port_(bind_port) {}
CamSettingsListener::~CamSettingsListener() { shutdown(); }

bool CamSettingsListener::start(std::shared_ptr<CommandRunner> runner, const std::string& device,
                                const std::string& bind_host) {
    shutdown();
    stop_.store(false);
    std::string error;
    if (!bind_subscriber(context_, socket_, bind_host, bind_port_, &error)) {
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

std::string CamSettingsListener::last_message() const {
    std::lock_guard<std::mutex> lock(last_message_mutex_);
    return last_message_;
}

// Проверяет команду set из JSON и сохраняет последние управления для UI.
// Поле auto_exposure не принимается от клиента: Pi задаёт его сам.
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

    const auto& incoming = root.at("controls");
    nlohmann::json normalized = nlohmann::json::object();
    bool saw_legacy_exposure = false;
    bool saw_canonical_exposure = incoming.contains("exposure");
    for (auto it = incoming.begin(); it != incoming.end(); ++it) {
        const std::string& name = it.key();
        // Malformed values are dropped, never coerced to 0 and applied.
        if (!it.value().is_number_integer() && !it.value().is_number_unsigned()) {
            continue;
        }
        std::string target = name;
        if (name == "exposure_time_absolute") {
            saw_legacy_exposure = true;
            if (saw_canonical_exposure) {
                continue;
            }
            target = "exposure";
        }
        normalized[target] = it.value().get<int>();
    }
    if (saw_legacy_exposure && !saw_canonical_exposure) {
        std::cout << "cam_set: mapped legacy control exposure_time_absolute to exposure" << std::endl;
    } else if (saw_legacy_exposure && saw_canonical_exposure) {
        std::cerr << "cam_set: ignored legacy exposure_time_absolute because exposure is present" << std::endl;
    }
    if (normalized.empty()) {
        return false;
    }
    if (controls_json) {
        *controls_json = normalized.dump();
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
            const ApplyReport report = settings.apply_controls(device, controls);
            for (const auto& [name, result] : report.controls) {
                std::cout << "cam_set " << name << " requested=" << result.requested
                          << " status=" << result.status << std::endl;
            }
        } catch (...) {
            // Keep listening after malformed messages; service stays up.
        }
    }
}

} // namespace camera_cpp
