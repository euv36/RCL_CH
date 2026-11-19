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
    negotiated_ = CaptureConfig{};
}

} // namespace camera_cpp
