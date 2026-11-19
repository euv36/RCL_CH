#include "pi_camera/camera_engine.hpp"

#include <utility>

namespace camera_cpp {

CameraEngine::CameraEngine(std::unique_ptr<CaptureBackend> backend)
    : capture_(std::move(backend)) {}

bool CameraEngine::open(const CaptureConfig& config, CaptureOpenReport* report) {
    if (!capture_.open(config, report)) {
        negotiated_ = CaptureConfig{};
        return false;
    }
    // Report the values the device actually negotiated, not the requested ones.
    negotiated_ = capture_.negotiated_config();
    return true;
}

// Читает кадр с отдельным буфером обработки: каждый EngineFrame владеет
// своей памятью, поэтому старые кадры остаются неизменными после новых чтений.
std::optional<EngineFrame> CameraEngine::read() {
    auto frame = capture_.read();
    if (!frame.has_value()) {
        return std::nullopt;
    }

    EngineFrame output;
    output.full_frame = std::move(*frame);
    output.processing_frame = downscale_and_rotate(output.full_frame);
    output.negotiated = negotiated_;
    return output;
}

// Читает кадр, переиспользуя внутренний буфер обработки. Это убирает
// аллокацию 640x360 на каждый кадр в непрерывном цикле сервиса; вызывающий
// обязан использовать processing_frame до следующего вызова read_reusing().
bool CameraEngine::read_reusing(EngineFrame& output) {
    auto frame = capture_.read();
    if (!frame.has_value()) {
        return false;
    }

    output.full_frame = std::move(*frame);
    downscale_and_rotate(output.full_frame, processing_buffer_);
    output.processing_frame = processing_buffer_;
    output.negotiated = negotiated_;
    return true;
}

void CameraEngine::close() {
    capture_.close();
    negotiated_ = CaptureConfig{};
    processing_buffer_.release();
}

} // namespace camera_cpp
