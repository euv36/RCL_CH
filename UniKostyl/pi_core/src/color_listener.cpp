// pi_core/color_listener.cpp — see color_listener.hpp
// UNVERIFIED ON HW: ZMQ SUB connect + CONFLATE interop with the laptop PUB —
// verify on the Pi per deploy.sh step 8 (feed colors on port 5567).
#include "color_listener.hpp"
#include "log.hpp"

#include <chrono>
#include <cstring>
#include <thread>

namespace pi_core {

ColorListener::ColorListener(zmq::context_t& ctx, std::string laptop_ip,
                             int port, GateTracker& tracker, GateStore& store,
                             Streamer& streamer, int gate_count)
    : ctx_(ctx),
      laptop_ip_(std::move(laptop_ip)),
      port_(port),
      tracker_(tracker),
      store_(store),
      streamer_(streamer),
      gate_count_(gate_count) {}

ColorListener::~ColorListener() { stop(); }

void ColorListener::start() {
    thread_ = std::thread([this] { run(); });
}

void ColorListener::stop() {
    stop_.store(true);
    if (thread_.joinable()) thread_.join();
}

void ColorListener::run() {
    zmq::socket_t sock(ctx_, zmq::socket_type::sub);
    sock.set(zmq::sockopt::subscribe, "");  // all messages
    sock.set(zmq::sockopt::conflate, 1);
    sock.set(zmq::sockopt::rcvtimeo, 200);  // ms
    sock.set(zmq::sockopt::linger, 0);
    const std::string endpoint =
        "tcp://" + laptop_ip_ + ":" + std::to_string(port_);
    try {
        sock.connect(endpoint);
        log_msg("Цвета: SUB " + endpoint);
    } catch (const zmq::error_t& e) {
        log_msg(std::string("Не удалось открыть SUB-сокет: ") + e.what(),
                "ERROR");
        return;
    }

    bool got_colors = false;
    // 8 s of silence almost always means a stale --laptop-ip (DHCP moved the
    // laptop). Python warns once; the same trap, so never stay silent.
    auto t_start = std::chrono::steady_clock::now();
    bool silence_warned = false;
    bool gate_drop_warned = false;

    std::string payload;
    payload.reserve(512);

    while (!stop_.load()) {
        zmq::message_t msg;
        try {
            if (!sock.recv(msg, zmq::recv_flags::none)) {
                // rcvtimeo elapsed
                if (!got_colors && !silence_warned) {
                    const double dt = std::chrono::duration<double>(
                                          std::chrono::steady_clock::now() -
                                          t_start)
                                          .count();
                    if (dt > 8.0) {
                        log_msg(
                            "Цвета не приходят с " + endpoint +
                                " за 8 c — проверьте --laptop-ip (ipconfig "
                                "на ноутбуке)",
                            "WARN");
                        silence_warned = true;
                    }
                }
                continue;
            }
        } catch (const zmq::error_t& e) {
            log_debug(std::string("Ошибка приёма: ") + e.what());
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
            continue;
        }

        payload.assign(static_cast<const char*>(msg.data()), msg.size());
        const ColorsMessage parsed = parse_colors_message(payload);
        if (!parsed.valid) continue;  // malformed JSON: silently ignore

        if (parsed.has_stream) {
            streamer_.set_enabled(parsed.stream_value);
        }

        got_colors = true;  // any valid dict counts, same as Python

        // gates: [{id, rgb, role?}, ...] — gate_roles[i] parsed 1:1 with gates[i]
        for (size_t gi = 0; gi < parsed.gates.size(); ++gi) {
            const auto& gate_rgb = parsed.gates[gi];
            const int gate_id = gate_rgb.first;
            if (gate_id - 1 >= gate_count_ || gate_id < 1) {
                // The Pi may run with --gates 2 while the laptop sends id=3 —
                // warn once instead of silently dropping (Python behaviour).
                if (!gate_drop_warned) {
                    gate_drop_warned = true;
                    log_msg(
                        "Получены цвета ворот id=" + std::to_string(gate_id) +
                            ", но Pi запущен с --gates " +
                            std::to_string(gate_count_) +
                            " — ворота игнорируются. Перезапустите Pi с "
                            "--gates " + std::to_string(gate_id),
                        "WARN");
                }
                continue;
            }
            tracker_.update_gate_from_rgb(gate_id - 1, gate_rgb.second[0],
                                          gate_rgb.second[1],
                                          gate_rgb.second[2]);
            // Idea 9: role is optional and only present when the laptop sent
            // one; empty string keeps the previous role (old laptop safe).
            std::string role;
            if (gi < parsed.gate_roles.size()) role = parsed.gate_roles[gi];
            if (!role.empty()) {
                tracker_.set_gate_role(gate_id - 1,
                                       gate_role_from_string(role));
            }
            store_.set_gate_from_network(gate_id, gate_rgb.second, role);
        }

        if (parsed.has_legacy_rgb) {
            // legacy {"rgb": [...]} = gate 1
            tracker_.update_gate_from_rgb(0, parsed.legacy_rgb[0],
                                          parsed.legacy_rgb[1],
                                          parsed.legacy_rgb[2]);
            std::array<int, 3> rgb = {parsed.legacy_rgb[0],
                                      parsed.legacy_rgb[1],
                                      parsed.legacy_rgb[2]};
            store_.set_gate_from_network(1, rgb);
        }
    }

    try {
        sock.close();
    } catch (...) {
    }
}

}  // namespace pi_core
