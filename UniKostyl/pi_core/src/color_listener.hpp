// pi_core/color_listener.hpp — ZMQ SUB (connect) to the laptop's PUB on
// tcp://<laptop-ip>:<rgb-port>, mirrors RGBListener in rpi_elp_cam.py:
// SUBSCRIBE "", CONFLATE 1, RCVTIMEO 200 ms, LINGER 0.
// The Pi connects, so the PUB must bind on the laptop (cv_kostyl.py /
// out/send_colors.py). Applies gate colors to GateTracker + GateStore and
// forwards {"stream":...} to the Streamer.
#pragma once

#include "color_message.hpp"
#include "gate_logic.hpp"
#include "gate_store.hpp"
#include "gate_tracker.hpp"
#include "streamer.hpp"

#include <atomic>
#include <string>
#include <thread>

#include <zmq.hpp>

namespace pi_core {

class ColorListener {
public:
    ColorListener(zmq::context_t& ctx, std::string laptop_ip, int port,
                  GateTracker& tracker, GateStore& store, Streamer& streamer,
                  int gate_count);
    ~ColorListener();

    ColorListener(const ColorListener&) = delete;
    ColorListener& operator=(const ColorListener&) = delete;

    void start();
    void stop();

private:
    void run();

    zmq::context_t& ctx_;
    std::string laptop_ip_;
    int port_;
    GateTracker& tracker_;
    GateStore& store_;
    Streamer& streamer_;
    int gate_count_;

    std::thread thread_;
    std::atomic<bool> stop_{false};
};

}  // namespace pi_core
