#include "pi_camera/camera_engine.hpp"

#include <utility>

namespace camera_cpp {

CameraEngine::CameraEngine(std::unique_ptr<CaptureBackend> backend)
    : capture_(std::move(backend)) {}

bool CameraEngine::open(const CaptureConfig& config) {
    if (!capture_.open(config)) {
        return false;
    }
    negotiated_ = config;
    return true;
}

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

void CameraEngine::close() {
    capture_.close();
}

} // namespace camera_cpp
