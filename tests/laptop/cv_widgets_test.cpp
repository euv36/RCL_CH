// Тест OpenCV-виджетов: геометрия, слайдер, лог и безопасная отрисовка.
#include <opencv2/core.hpp>

#include <string>

#include "../test_checks.hpp"
#include "laptop/cv_widgets.hpp"

int main() {
    using camera_cpp::laptop::CvButton;
    using camera_cpp::laptop::CvLog;
    using camera_cpp::laptop::CvRect;
    using camera_cpp::laptop::CvSlider;
    using camera_cpp::laptop::CvTextInput;

    const CvRect rect{10, 10, 100, 50};
    CAMERA_TEST_CHECK(rect.contains(10, 10));
    CAMERA_TEST_CHECK(rect.contains(109, 59));
    CAMERA_TEST_CHECK(!rect.contains(110, 10));
    CAMERA_TEST_CHECK(!rect.contains(10, 60));
    CAMERA_TEST_CHECK(!rect.contains(9, 9));

    CvSlider slider;
    slider.rect = {0, 0, 100, 10};
    slider.min = 0;
    slider.max = 100;
    slider.value = 50;
    CAMERA_TEST_CHECK(slider.value_from_x(0) == 0);
    CAMERA_TEST_CHECK(slider.value_from_x(50) == 50);
    CAMERA_TEST_CHECK(slider.value_from_x(100) == 100);
    CAMERA_TEST_CHECK(slider.value_from_x(500) == 100);
    CAMERA_TEST_CHECK(slider.value_from_x(-10) == 0);

    // Лог держит только последние 10 записей.
    CvLog log;
    for (int i = 1; i <= 12; ++i) log.push("line " + std::to_string(i));
    cv::Mat canvas(240, 300, CV_8UC3, cv::Scalar::all(0));
    log.draw(canvas, {0, 0, 300, 240});
    CAMERA_TEST_CHECK(canvas.at<cv::Vec3b>(0, 0)[0] == 120);

    // Отрисовка на пустом кадре не бросает.
    cv::Mat empty;
    CvButton button;
    button.rect = {0, 0, 80, 30};
    button.label = "CONNECT";
    button.draw(empty, false);
    slider.draw(empty);
    CvTextInput input;
    input.rect = {0, 0, 200, 30};
    input.text = "10.132.122.51";
    input.draw(empty);
    log.draw(empty, {0, 0, 200, 100});

    return 0;
}
