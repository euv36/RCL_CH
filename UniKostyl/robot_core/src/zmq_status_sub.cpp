// zmq_status_sub.cpp — cppzmq/libzmq SUB plumbing for the camera status.
//
// // UNVERIFIED ON HW: compiled only (libzmq present on the Pi via
// `apt install libzmq3-dev`; the laptop does not need this binary).
// The wire contract itself is owned by the camera core (Idea 2 multipart);
// this side only consumes part 2 (JSON) and must tolerate part 1 (JPEG).

#include "zmq_status_sub.hpp"
#include <zmq.hpp>
#include <cstdio>

namespace robot_core {

GateStatusSub::GateStatusSub(const std::string& endpoint) : endpoint_(endpoint) {}

GateStatusSub::~GateStatusSub() {
    stop();
}

bool GateStatusSub::start() {
    try {
        ctx_ = new zmq::context_t(1);
        auto* s = new zmq::socket_t(*(zmq::context_t*)ctx_, zmq::socket_type::sub);
        // SUB connect to the camera PUB (same pattern as the laptop video
        // client; the camera binds tcp://*:5555).
        s->connect(endpoint_);
        s->set(zmq::sockopt::subscribe, "");  // all topics (the stream has none)
        // Do not queue stale frames for a late joiner.
        s->set(zmq::sockopt::conflate, 1);
        sock_ = s;
        started_ = true;
        std::printf("[gates] SUB connected to %s\n", endpoint_.c_str());
        return true;
    } catch (const zmq::error_t& e) {
        // ZMQ disconnect/failure = degradation (PROMPT_ROBOT §7): log, keep
        // running ball-only. No exception escapes into the control loop.
        std::printf("[gates] SUB connect failed: %s (ball-only mode)\n", e.what());
        stop();
        return false;
    }
}

void GateStatusSub::stop() {
    delete (zmq::socket_t*)sock_;
    sock_ = nullptr;
    delete (zmq::context_t*)ctx_;
    ctx_ = nullptr;
    started_ = false;
}

bool GateStatusSub::poll(unsigned long now_ms) {
    if (!started_ || !sock_) {
        return false;
    }
    auto* s = (zmq::socket_t*)sock_;
    bool got_any = false;
    // Drain everything pending (conflate keeps it to ~1 frame anyway); use
    // the newest COMPLETE multipart [JPEG, JSON] pair.
    std::string json_part;
    try {
        while (true) {
            zmq::message_t msg;
            if (!s->recv(msg, zmq::recv_flags::dontwait)) {
                break;
            }
            // Multipart: [JPEG, JSON] — part 1 is the frame; if this message
            // is the second part of a pair we parse it. With conflate we may
            // occasionally see a lone JSON part (older single-part senders) —
            // try to parse only when it looks like JSON.
            const char* data = msg.data<char>();
            if (msg.size() > 0 && data[0] == '{') {
                json_part.assign(data, msg.size());
                got_any = true;
            } else {
                // JPEG (or garbage): peek for a following part and parse it.
                if (s->recv(msg, zmq::recv_flags::dontwait)) {
                    const char* j = msg.data<char>();
                    if (msg.size() > 0 && j[0] == '{') {
                        json_part.assign(j, msg.size());
                        got_any = true;
                    }
                }
            }
        }
    } catch (const zmq::error_t&) {
        return false;  // transient socket error: keep the last view
    }
    if (got_any && !json_part.empty()) {
        parse_gate_status(json_part, now_ms, view_);
        return true;
    }
    return false;
}

}  // namespace robot_core
