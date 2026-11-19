#include "pi_camera/v4l2_capture.hpp"

#include <opencv2/imgproc.hpp>

#include "../test_checks.hpp"

using camera_cpp::downscale_and_rotate;

int main() {
    cv::Mat frame(1080, 1920, CV_8UC3, cv::Scalar(10, 20, 30));
    cv::rectangle(frame, cv::Rect(100, 100, 200, 200), cv::Scalar(200, 100, 50), cv::FILLED);

    const cv::Mat expected = downscale_and_rotate(frame);
    CAMERA_TEST_CHECK(expected.size() == cv::Size(640, 360));
    CAMERA_TEST_CHECK(expected.type() == CV_8UC3);

    cv::Mat out;
    downscale_and_rotate(frame, out);
    CAMERA_TEST_CHECK(out.size() == expected.size());
    CAMERA_TEST_CHECK(out.type() == expected.type());
    CAMERA_TEST_CHECK(cv::norm(out, expected, cv::NORM_INF) == 0.0);

    // A caller-owned output buffer must be reused instead of reallocated
    // on every frame: keeping a Mat header pins the first allocation, so a
    // fresh allocation would move the data pointer.
    cv::Mat pinned_header = out;
    cv::Mat pinned_content = out.clone();
    cv::Mat frame2(1080, 1920, CV_8UC3, cv::Scalar(50, 60, 70));
    downscale_and_rotate(frame2, out);
    CAMERA_TEST_CHECK(out.data == pinned_header.data);
    CAMERA_TEST_CHECK(cv::norm(out, pinned_content, cv::NORM_INF) > 0.0);

    cv::Mat empty;
    downscale_and_rotate(empty, out);
    CAMERA_TEST_CHECK(out.empty());
    return 0;
}
