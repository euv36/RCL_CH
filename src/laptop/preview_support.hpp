#pragma once

#include <cstdint>
#include <vector>

#include <opencv2/core.hpp>

namespace camera_cpp::laptop {

// Готовые байты RGBA для загрузки в SDL-текстуру предпросмотра.
struct RgbaImage {
    std::vector<std::uint8_t> pixels;
    int width = 0;
    int height = 0;
};

// Прямоугольник вывода картинки внутри окна с сохранением пропорций.
struct PreviewRect {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

// Переводит BGR-кадр OpenCV в плотный RGBA-буфер (по 4 байта на пиксель).
RgbaImage bgr_to_rgba(const cv::Mat& bgr);

// Вписывает кадр в окно без обрезки поля зрения, по центру.
PreviewRect fit_rect(int window_w, int window_h, int image_w, int image_h);

} // namespace camera_cpp::laptop
