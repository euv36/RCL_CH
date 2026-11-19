#pragma once

#include <deque>
#include <string>

#include <opencv2/core.hpp>

namespace camera_cpp::laptop {

// Прямоугольник в пикселях кадра; граница справа/снизу не входит.
struct CvRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool contains(int px, int py) const noexcept;
};

// Кнопка с подписью; active подсвечивает нажатое состояние.
struct CvButton {
    CvRect rect;
    std::string label;
    bool hover = false;

    void draw(cv::Mat& canvas, bool active) const;
};

// Горизонтальный слайдер с целочисленным значением.
struct CvSlider {
    CvRect rect;
    int value = 0;
    int min = 0;
    int max = 100;
    std::string label;

    void draw(cv::Mat& canvas) const;
    int value_from_x(int px) const noexcept;
};

// Поле ввода IP/индекса камеры.
struct CvTextInput {
    CvRect rect;
    std::string text;
    bool active = false;

    void draw(cv::Mat& canvas) const;
};

// Кольцевой лог на последние 10 строк.
class CvLog {
public:
    void push(std::string line);
    void draw(cv::Mat& canvas, const CvRect& area) const;

private:
    std::deque<std::string> lines_;
};

} // namespace camera_cpp::laptop
