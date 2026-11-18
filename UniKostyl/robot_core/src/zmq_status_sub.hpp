#pragma once
// zmq_status_sub.hpp — camera gate status consumption over ZMQ.
//
// // UNVERIFIED ON HW: libzmq SUB to the camera's multipart [JPEG, JSON] on
// 5555 has never been run from this program. Compile target: Pi only (needs
// libzmq3-dev; the header is vendored cppzmq, same as pi_core).
//
// Contract (PROMPT_EN §4 / Idea 2): the camera core publishes multipart
// [JPEG, JSON status] on tcp://*:5555; robot_core SUBscribes (connect),
// drops the JPEG part and parses the JSON part via gate_status.* (lenient).
// With the camera absent (--no-gates / no frames), robot_core degrades to
// ball-only/line-only and logs the reason — never blocks, never crashes.

#include "gate_status.hpp"
#include <string>

namespace robot_core {

class GateStatusSub {
public:
    // endpoint default: tcp://127.0.0.1:5555 (the camera core runs on the
    // same Pi; the laptop does not publish this stream).
    explicit GateStatusSub(const std::string& endpoint);
    ~GateStatusSub();

    bool start();
    void stop();

    // Non-blocking poll: drains pending multipart messages, parses the newest
    // JSON status into the internal view. Returns true when a NEW status was
    // consumed this call.
    bool poll(unsigned long now_ms);

    // Latest parsed status (may be invalid — absence degrades gracefully).
    const GateStatusView& view() const { return view_; }

private:
    std::string endpoint_;
    void* ctx_ = nullptr;   // zmq context (void* to keep this header testable)
    void* sock_ = nullptr;  // zmq SUB socket
    GateStatusView view_;
    bool started_ = false;
};

}  // namespace robot_core
