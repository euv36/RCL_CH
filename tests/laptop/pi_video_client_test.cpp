// Тест клиента видео Pi: фейковый ZMQ PUB на 15555, без Pi и без камеры.
#include <zmq.h>

#include <opencv2/imgcodecs.hpp>
#include <opencv2/core.hpp>

#include <chrono>
#include <string>
#include <thread>
#include <vector>

#include "../test_checks.hpp"
#include "laptop/pi_video_client.hpp"

namespace {

std::vector<unsigned char> make_jpeg(int width, int height) {
    cv::Mat image(height, width, CV_8UC3, cv::Scalar(20, 60, 180));
    std::vector<unsigned char> bytes;
    CAMERA_TEST_CHECK(cv::imencode(".jpg", image, bytes));
    return bytes;
}

int send_part(void* socket, const std::vector<unsigned char>& bytes, int flags) {
    return zmq_send(socket, bytes.data(), bytes.size(), flags);
}

void send_multipart(void* socket, const std::vector<unsigned char>& jpeg,
                    const std::string& status) {
    CAMERA_TEST_CHECK(send_part(socket, jpeg, ZMQ_SNDMORE) == static_cast<int>(jpeg.size()));
    CAMERA_TEST_CHECK(zmq_send(socket, status.data(), status.size(), 0) ==
                      static_cast<int>(status.size()));
}

} // namespace

int main() {
    void* context = zmq_ctx_new();
    CAMERA_TEST_CHECK(context != nullptr);
    void* publisher = zmq_socket(context, ZMQ_PUB);
    CAMERA_TEST_CHECK(publisher != nullptr);
    CAMERA_TEST_CHECK(zmq_bind(publisher, "tcp://127.0.0.1:15555") == 0);

    camera_cpp::laptop::PiVideoClient client;
    CAMERA_TEST_CHECK(!client.connected());
    CAMERA_TEST_CHECK(client.connect("127.0.0.1", 15555));
    CAMERA_TEST_CHECK(client.connected());

    const auto jpeg = make_jpeg(640, 360);
    const std::string status = R"({"fps":11.5,"src":"pi","gates":{}})";

    // PUB/SUB slow joiner: повторяем до первого успешного приёма.
    bool received = false;
    camera_cpp::laptop::PiVideoFrame frame;
    for (int attempt = 0; attempt < 60 && !received; ++attempt) {
        send_multipart(publisher, jpeg, status);
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        received = client.receive(frame);
    }
    CAMERA_TEST_CHECK(received);
    CAMERA_TEST_CHECK(frame.rgb.cols == 640);
    CAMERA_TEST_CHECK(frame.rgb.rows == 360);
    CAMERA_TEST_CHECK(frame.has_status);
    CAMERA_TEST_CHECK(frame.status_json == status);

    // Одиночный JPEG: статуса нет, картинка есть.
    bool received_single = false;
    camera_cpp::laptop::PiVideoFrame single;
    for (int attempt = 0; attempt < 60 && !received_single; ++attempt) {
        CAMERA_TEST_CHECK(send_part(publisher, jpeg, 0) == static_cast<int>(jpeg.size()));
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        received_single = client.receive(single);
    }
    CAMERA_TEST_CHECK(received_single);
    CAMERA_TEST_CHECK(single.rgb.cols == 640);
    CAMERA_TEST_CHECK(!single.has_status);

    client.disconnect();
    CAMERA_TEST_CHECK(!client.connected());

    zmq_close(publisher);
    zmq_ctx_term(context);
    return 0;
}
