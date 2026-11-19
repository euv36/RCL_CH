#include "pi_camera/v4l2_capture.hpp"
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <algorithm>
#include <cctype>
#include <cmath>
#include <string>

namespace camera_cpp {
namespace {

constexpr const char* kDefaultDevice = "/dev/video0";

std::string fourcc_to_string(int code) {
    std::string result(4, '\0');
    if (code == 0) return result;
    for (int i = 0; i < 4; ++i) {
        result[static_cast<std::size_t>(i)] =
            static_cast<char>((static_cast<unsigned int>(code) >> (8 * i)) & 0xFFU);
    }
    return result;
}

int fourcc_from_string(const std::string& fourcc) {
    if (fourcc.size() != 4) return 0;
    return cv::VideoWriter::fourcc(fourcc[0], fourcc[1], fourcc[2], fourcc[3]);
}

bool readable_fourcc(const std::string& fourcc) {
    return fourcc.size() == 4 && std::all_of(fourcc.begin(), fourcc.end(), [](unsigned char value) {
        return value >= 0x20U && value < 0x7FU;
    });
}

void append_warning(std::string& target, const std::string& extra) {
    if (extra.empty()) return;
    if (!target.empty()) target += "; ";
    target += extra;
}

std::string size_text(int width, int height) {
    return std::to_string(width) + "x" + std::to_string(height);
}

class OpenCvBackend final : public CaptureBackend {
public:
    bool open(const CaptureConfig& config, std::string* error) override {
        capture_.release();
        if (error) error->clear();
        const int fourcc = fourcc_from_string(config.fourcc);
        if (fourcc == 0) {
            if (error) *error = "capture fourcc must be exactly four characters";
            return false;
        }

        capture_.open(config.device, cv::CAP_V4L2);
        if (!capture_.isOpened()) {
            if (error) *error = "OpenCV V4L2 backend could not open " + config.device;
            return false;
        }

        // FOURCC must be applied before size/fps: V4L2 negotiates pixel format first.
        capture_.set(cv::CAP_PROP_FOURCC, static_cast<double>(fourcc));
        capture_.set(cv::CAP_PROP_FRAME_WIDTH, static_cast<double>(config.width));
        capture_.set(cv::CAP_PROP_FRAME_HEIGHT, static_cast<double>(config.height));
        capture_.set(cv::CAP_PROP_FPS, static_cast<double>(config.fps));
        return true;
    }

    bool read(cv::Mat& frame, std::string* error) override {
        if (error) error->clear();
        if (!capture_.isOpened()) {
            if (error) *error = "capture is not opened";
            return false;
        }
        if (!capture_.read(frame) || frame.empty()) {
            if (error) *error = "failed to capture frame";
            return false;
        }
        return true;
    }

    bool properties(CaptureProperties& properties, std::string* error) const override {
        if (error) error->clear();
        if (!capture_.isOpened()) {
            if (error) *error = "capture is not opened";
            return false;
        }

        properties.width = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_WIDTH));
        properties.height = static_cast<int>(capture_.get(cv::CAP_PROP_FRAME_HEIGHT));
        const double fps = capture_.get(cv::CAP_PROP_FPS);
        properties.fps = fps > 0.0 ? static_cast<int>(std::lround(fps)) : 0;
        properties.fourcc = fourcc_to_string(static_cast<int>(capture_.get(cv::CAP_PROP_FOURCC)));

        if (properties.width <= 0 || properties.height <= 0 || !readable_fourcc(properties.fourcc)) {
            if (error) *error = "device did not report usable width, height and FOURCC";
            return false;
        }
        return true;
    }

    void close() override {
        capture_.release();
    }

private:
    cv::VideoCapture capture_;
};

} // namespace

bool normalize_capture_config(CaptureConfig& config, std::string* error, std::string* warning) {
    if (error) error->clear();
    if (warning) warning->clear();
    if (config.width <= 0 || config.height <= 0 || config.fps <= 0) {
        if (error) *error = "width, height and fps must be positive";
        return false;
    }

    std::string fourcc = config.fourcc;
    std::transform(fourcc.begin(), fourcc.end(), fourcc.begin(), [](unsigned char value) {
        return static_cast<char>(std::toupper(value));
    });
    if (fourcc.size() != 4) {
        if (error) *error = "capture fourcc must be exactly four characters";
        return false;
    }
    if (fourcc != "MJPG") {
        if (warning) *warning = "capture fourcc should normally be MJPG on HD USB Camera";
    }
    config.fourcc = fourcc;

    if (config.device.empty()) {
        config.device = kDefaultDevice;
    }
    return true;
}

std::unique_ptr<CaptureBackend> make_open_cv_v4l2_backend() {
    return std::make_unique<OpenCvBackend>();
}

struct V4l2Capture::Impl {
    std::unique_ptr<CaptureBackend> backend;
    CaptureConfig config{};
    bool opened = false;
};

V4l2Capture::V4l2Capture() : impl_(std::make_unique<Impl>()) {
    impl_->backend = make_open_cv_v4l2_backend();
}

V4l2Capture::V4l2Capture(std::unique_ptr<CaptureBackend> backend) : impl_(std::make_unique<Impl>()) {
    impl_->backend = backend ? std::move(backend) : make_open_cv_v4l2_backend();
}

V4l2Capture::~V4l2Capture() = default;

bool V4l2Capture::open(const CaptureConfig& config, CaptureOpenReport* report) {
    if (report) *report = CaptureOpenReport{};

    std::string error;
    std::string warning;
    CaptureConfig requested = config;
    if (!normalize_capture_config(requested, &error, &warning)) {
        if (report) {
            report->error = error.empty() ? "invalid capture configuration" : error;
            report->warning = warning;
        }
        return false;
    }

    close();
    if (!impl_->backend->open(requested, &error)) {
        if (report) {
            report->error = error.empty() ? "capture backend could not be opened" : error;
            report->warning = warning;
        }
        return false;
    }

    CaptureProperties properties;
    if (!impl_->backend->properties(properties, &error)) {
        impl_->backend->close();
        if (report) {
            report->error = "could not verify negotiated capture properties"
                + (error.empty() ? std::string() : std::string(": ") + error);
            report->warning = warning;
        }
        return false;
    }

    CaptureConfig negotiated = requested;
    negotiated.width = properties.width;
    negotiated.height = properties.height;
    negotiated.fourcc = properties.fourcc;
    if (properties.fps > 0) {
        negotiated.fps = properties.fps;
    } else {
        append_warning(warning, "device did not report FPS; using requested " + std::to_string(requested.fps));
    }

    if (properties.width != requested.width || properties.height != requested.height) {
        append_warning(warning, "device negotiated " + size_text(properties.width, properties.height)
            + " instead of requested " + size_text(requested.width, requested.height)
            + "; full-FOV 1920x1080 capture is not verified");
    }
    if (properties.fourcc != requested.fourcc) {
        append_warning(warning, "device negotiated fourcc " + properties.fourcc
            + " instead of requested " + requested.fourcc);
    }

    impl_->config = negotiated;
    impl_->opened = true;
    if (report) {
        report->ok = true;
        report->warning = warning;
    }
    return true;
}

const CaptureConfig& V4l2Capture::negotiated_config() const {
    return impl_->config;
}

std::optional<cv::Mat> V4l2Capture::read() {
    if (!impl_->opened) return std::nullopt;
    cv::Mat frame;
    std::string error;
    if (!impl_->backend->read(frame, &error) || frame.empty()) return std::nullopt;
    return frame;
}

void V4l2Capture::close() {
    if (impl_->backend) impl_->backend->close();
    impl_->opened = false;
    impl_->config = CaptureConfig{};
}

// Масштабирует полный кадр до рабочего размера Pi 640x360 без обрезки поля зрения.
// INTER_AREA усредняет исходные пиксели и не даёт алиасинга при сильном уменьшении;
// это OpenCV-примитив, а не ручной цикл по пикселям.
void downscale_and_rotate(const cv::Mat& full_frame, cv::Mat& processing) {
    if (full_frame.empty()) {
        processing.release();
        return;
    }
    // Память выделяется только при первом вызове или смене размера/типа:
    // между кадрами буфер переиспользуется, что убирает аллокацию на кадр.
    cv::resize(full_frame, processing, {640, 360}, 0.0, 0.0, cv::INTER_AREA);
}

// Удобная обёртка для вызывающих, которым нужен новый кадр, а не переиспользование буфера.
cv::Mat downscale_and_rotate(const cv::Mat& full_frame) {
    cv::Mat processing;
    downscale_and_rotate(full_frame, processing);
    return processing;
}

} // namespace camera_cpp
