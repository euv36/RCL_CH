#include "laptop/cv_widgets.hpp"

#include <algorithm>
#include <string>

#include <opencv2/imgproc.hpp>

namespace camera_cpp::laptop {

namespace {

constexpr int kFont = cv::FONT_HERSHEY_SIMPLEX;
constexpr double kFontScale = 0.5;
const cv::Scalar kTextColor(230, 230, 230);
const cv::Scalar kBorderColor(120, 120, 120);
const cv::Scalar kActiveColor(40, 170, 90);
const cv::Scalar kHoverColor(70, 70, 70);

// Рисует подпись, обрезая её по ширине области.
void draw_label(cv::Mat& canvas, const std::string& text, int x, int y, int max_width) {
    if (canvas.empty() || text.empty() || max_width <= 0) return;
    std::string shown = text;
    int baseline = 0;
    cv::Size size = cv::getTextSize(shown, kFont, kFontScale, 1, &baseline);
    while (size.width > max_width && shown.size() > 1) {
        shown.pop_back();
        size = cv::getTextSize(shown, kFont, kFontScale, 1, &baseline);
    }
    cv::putText(canvas, shown, {x, y}, kFont, kFontScale, kTextColor, 1, cv::LINE_AA);
}

} // namespace

bool CvRect::contains(int px, int py) const noexcept {
    return px >= x && px < x + w && py >= y && py < y + h;
}

void CvButton::draw(cv::Mat& canvas, bool active) const {
    if (canvas.empty()) return;
    const cv::Point tl(rect.x, rect.y);
    const cv::Point br(rect.x + rect.w, rect.y + rect.h);
    cv::rectangle(canvas, tl, br, active ? kActiveColor : kBorderColor, cv::FILLED);
    if (hover) cv::rectangle(canvas, tl, br, kHoverColor, 1);
    draw_label(canvas, label, rect.x + 6, rect.y + rect.h / 2 + 5, rect.w - 12);
}

void CvSlider::draw(cv::Mat& canvas) const {
    if (canvas.empty()) return;
    const cv::Point tl(rect.x, rect.y);
    const cv::Point br(rect.x + rect.w, rect.y + rect.h);
    cv::rectangle(canvas, tl, br, kBorderColor, 1);
    const int span = std::max(0, max - min);
    const int clamped = std::clamp(value, min, max);
    const int offset = span == 0 ? 0 : static_cast<int>((clamped - min) * rect.w / span);
    const int marker_x = rect.x + std::clamp(offset, 0, rect.w);
    cv::rectangle(canvas, {marker_x - 2, rect.y}, {marker_x + 2, rect.y + rect.h},
                  kActiveColor, cv::FILLED);
    draw_label(canvas, label, rect.x, rect.y - 5, rect.w);
}

int CvSlider::value_from_x(int px) const noexcept {
    const int span = max - min;
    if (rect.w <= 0 || span <= 0) return min;
    const int offset = std::clamp(px - rect.x, 0, rect.w);
    return min + static_cast<int>(static_cast<long long>(offset) * span / rect.w);
}

void CvTextInput::draw(cv::Mat& canvas) const {
    if (canvas.empty()) return;
    cv::rectangle(canvas, {rect.x, rect.y}, {rect.x + rect.w, rect.y + rect.h},
                  active ? kActiveColor : kBorderColor, active ? 2 : 1);
    draw_label(canvas, text, rect.x + 6, rect.y + rect.h / 2 + 5, rect.w - 12);
}

void CvLog::push(std::string line) {
    lines_.push_back(std::move(line));
    if (lines_.size() > 10) lines_.pop_front();
}

void CvLog::draw(cv::Mat& canvas, const CvRect& area) const {
    if (canvas.empty()) return;
    cv::rectangle(canvas, {area.x, area.y}, {area.x + area.w, area.y + area.h},
                  kBorderColor, 1);
    const int step = 16;
    int y = area.y + area.h - 6;
    for (auto it = lines_.rbegin(); it != lines_.rend() && y > area.y + 12; ++it) {
        draw_label(canvas, *it, area.x + 6, y, area.w - 12);
        y -= step;
    }
}

} // namespace camera_cpp::laptop
