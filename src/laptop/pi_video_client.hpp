#pragma once

#include <opencv2/core.hpp>

#include <string>
#include <string_view>

namespace camera_cpp::laptop {

// Один кадр, полученный из видеопотока Pi: RGB-изображение и необязательный JSON статуса.
struct PiVideoFrame {
    cv::Mat rgb;
    bool has_status = false;
    std::string status_json;
};

// Клиент видеопотока Pi по ZMQ SUB: подписывается на 5555 и разбирает multipart [JPEG, JSON].
class PiVideoClient {
public:
    PiVideoClient();
    ~PiVideoClient();

    PiVideoClient(const PiVideoClient&) = delete;
    PiVideoClient& operator=(const PiVideoClient&) = delete;

    bool connect(std::string_view host, int port = 5555);
    void disconnect();
    bool receive(PiVideoFrame& out);
    bool connected() const noexcept;

private:
    void* context_ = nullptr;
    void* socket_ = nullptr;
};

} // namespace camera_cpp::laptop
