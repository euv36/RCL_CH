#include "laptop/pi_video_client.hpp"

#include <zmq.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <vector>

namespace camera_cpp::laptop {

PiVideoClient::PiVideoClient() = default;

PiVideoClient::~PiVideoClient() {
    disconnect();
}

// Подключается к PUB на Pi; очередь дренируется в receive(), CONFLATE не используется.
bool PiVideoClient::connect(std::string_view host, int port) {
    disconnect();
    context_ = zmq_ctx_new();
    if (context_ == nullptr) return false;

    socket_ = zmq_socket(context_, ZMQ_SUB);
    if (socket_ == nullptr) {
        zmq_ctx_term(context_);
        context_ = nullptr;
        return false;
    }

    // ZMQ_CONFLATE в этой версии libzmq теряет первую часть multipart [JPEG, JSON],
    // поэтому вместо него очередь дренируется в receive() — остаётся последний кадр.
    if (zmq_setsockopt(socket_, ZMQ_SUBSCRIBE, "", 0) != 0) {
        zmq_close(socket_);
        socket_ = nullptr;
        zmq_ctx_term(context_);
        context_ = nullptr;
        return false;
    }

    const std::string endpoint = "tcp://" + std::string(host) + ":" + std::to_string(port);
    if (zmq_connect(socket_, endpoint.c_str()) != 0) {
        zmq_close(socket_);
        socket_ = nullptr;
        zmq_ctx_term(context_);
        context_ = nullptr;
        return false;
    }
    return true;
}

void PiVideoClient::disconnect() {
    if (socket_ != nullptr) {
        zmq_close(socket_);
        socket_ = nullptr;
    }
    if (context_ != nullptr) {
        zmq_ctx_term(context_);
        context_ = nullptr;
    }
}

// Забирает одну multipart-посылку; статус разбирается отдельно от декодирования JPEG.
bool PiVideoClient::receive(PiVideoFrame& out) {
    out = PiVideoFrame{};
    if (socket_ == nullptr) return false;

    bool received_any = false;
    for (int part = 0; part < 2; ++part) {
        zmq_msg_t message;
        zmq_msg_init(&message);
        if (zmq_msg_recv(&message, socket_, ZMQ_DONTWAIT) < 0) {
            zmq_msg_close(&message);
            break;
        }
        received_any = true;

        const auto* data = static_cast<const unsigned char*>(zmq_msg_data(&message));
        const std::size_t size = zmq_msg_size(&message);
        int more = 0;
        std::size_t more_size = sizeof(more);
        zmq_getsockopt(socket_, ZMQ_RCVMORE, &more, &more_size);

        if (part == 0) {
            const cv::Mat buffer(1, static_cast<int>(size), CV_8U,
                                 const_cast<unsigned char*>(data));
            const cv::Mat decoded = cv::imdecode(buffer, cv::IMREAD_COLOR);
            if (!decoded.empty()) {
                cv::cvtColor(decoded, out.rgb, cv::COLOR_BGR2RGB);
            }
        } else {
            out.status_json.assign(reinterpret_cast<const char*>(data), size);
            out.has_status = true;
        }

        zmq_msg_close(&message);
        if (more == 0) break;
    }
    return received_any;
}

bool PiVideoClient::connected() const noexcept {
    return socket_ != nullptr;
}

} // namespace camera_cpp::laptop
