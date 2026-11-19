#include "camera_engine.hpp"
#include "cam_settings.hpp"
#include "color_listener.hpp"
#include "status_builder.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <mutex>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <opencv2/imgcodecs.hpp>
#include <zmq.h>

#include "common/atomic_file.hpp"
#include "common/gate_store.hpp"
#include "common/paths.hpp"
#include "vision/hsv_color.hpp"
#include "vision/tracking.hpp"

namespace {

using namespace camera_cpp;

constexpr int kDefaultVideoPort = 5555;
constexpr int kDefaultColorsPort = 5557;
constexpr int kDefaultCamSettingsPort = 5559;

struct GuardedGateStore {
    GateStore store;
    std::mutex mutex;
};

class ProcessCommandRunner : public CommandRunner {
public:
    CommandResult run(const std::string& command) override {
        const std::string full_command = command + " 2>&1";
#ifdef _WIN32
        FILE* pipe = _popen(full_command.c_str(), "r");
#else
        FILE* pipe = popen(full_command.c_str(), "r");
#endif
        if (!pipe) {
            return CommandResult{1, "", "popen failed"};
        }

        char buffer[512];
        std::string output;
        while (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
            output += buffer;
        }

#ifdef _WIN32
        int raw_status = _pclose(pipe);
#else
        int raw_status = pclose(pipe);
#endif
        int exit_code = raw_status;
#ifndef _WIN32
        if (WIFEXITED(raw_status)) {
            exit_code = WEXITSTATUS(raw_status);
        }
#endif
        return CommandResult{exit_code, output, ""};
    }
};

// Test-only backend: produces synthetic frames and never opens a camera device.
class FakeCaptureBackend final : public CaptureBackend {
public:
    bool open(const CaptureConfig& config, std::string* error) override {
        config_ = config;
        if (error) error->clear();
        return config.width > 0 && config.height > 0;
    }

    bool read(cv::Mat& frame, std::string* error) override {
        if (config_.width <= 0 || config_.height <= 0) {
            if (error) *error = "fake backend not opened";
            return false;
        }
        frame = cv::Mat(config_.height, config_.width, CV_8UC3, cv::Scalar(40, 80, 120));
        if (error) error->clear();
        return true;
    }

    bool properties(CaptureProperties& properties, std::string* error) const override {
        properties.width = config_.width;
        properties.height = config_.height;
        properties.fps = config_.fps;
        properties.fourcc = config_.fourcc;
        if (error) error->clear();
        return true;
    }

    void close() override {
        config_ = {};
    }

private:
    CaptureConfig config_{};
};

// Test-only v4l2-ctl replacement: records commands and returns truthful
// readbacks from the values it was asked to set. No shell, no device access.
class FakeV4l2Runner final : public CommandRunner {
public:
    explicit FakeV4l2Runner(std::filesystem::path log_path) : log_path_(std::move(log_path)) {}

    CommandResult run(const std::string& command) override {
        {
            std::ofstream out(log_path_, std::ios::app);
            if (!out) {
                return CommandResult{1, "", "fake v4l2 log unavailable"};
            }
            out << command << '\n';
        }

        const auto set_pos = command.find("--set-ctrl=");
        if (set_pos != std::string::npos) {
            const std::string pair = command.substr(set_pos + std::string("--set-ctrl=").size());
            const auto equals = pair.find('=');
            if (equals != std::string::npos) {
                const std::string name = pair.substr(0, equals);
                try {
                    size_t used = 0;
                    const int value = std::stoi(pair.substr(equals + 1), &used);
                    if (used != pair.size() - equals - 1) {
                        return CommandResult{1, "", "invalid fake value"};
                    }
                    values_[name] = value;
                    return CommandResult{0, "", ""};
                } catch (...) {
                    return CommandResult{1, "", "invalid fake value"};
                }
            }
        }

        const auto get_pos = command.find("--get-ctrl=");
        if (get_pos != std::string::npos) {
            const std::string name = command.substr(get_pos + std::string("--get-ctrl=").size());
            const auto found = values_.find(name);
            if (found == values_.end()) {
                return CommandResult{1, "", "control never set"};
            }
            return CommandResult{0, name + ": " + std::to_string(found->second), ""};
        }
        return CommandResult{0, "", ""};
    }

private:
    std::filesystem::path log_path_;
    std::map<std::string, int> values_;
};

void print_usage() {
    std::cout << "Usage: camera_service [--laptop-ip <ip>] [--device <path>] [--port-video <port>] "
              << "[--port-colors <port>] [--cam-settings-port <port>] [--bind-host <host>] "
              << "[--self-mask-cx <0-1>] [--self-mask-cy <0-1>] [--self-mask-r <0-1>] "
              << "[--self-mask-r2 <0-1>] [--self-mask-enabled] [--no-serial] [--once]\n";
    std::cout << "  --laptop-ip         Legacy address; this service only publishes and never connects to it.\n";
    std::cout << "  --no-serial         Do not use a robot serial link (this service never opens one).\n";
    std::cout << "  --port-video        Video/status publish port (default 5555).\n";
    std::cout << "  --port-colors       Gate color listener port (default 5557).\n";
    std::cout << "  --cam-settings-port CAM SET listener port (default 5559).\n";
    std::cout << "  --bind-host         Bind address for all ZMQ sockets (default *).\n";
    std::cout << "  --fake-camera       Test only: use a synthetic capture backend.\n";
    std::cout << "  --fake-v4l2-log <p> Test only: fake v4l2-ctl commands and append them to <p>.\n";
    std::cout << "  --gates-file <p>    Test only: saved-gate JSON path.\n";
    std::cout << "  --test-ready-file <p>, --test-state-file <p> Test only: startup/state probes.\n";
    std::cout << "  --max-frames <n>, --frame-delay-ms <ms> Test only: bounded run.\n";
}

bool read_float(const std::string& text, float& value) {
    try {
        size_t used = 0;
        const float parsed = std::stof(text, &used);
        if (used != text.size() || !std::isfinite(parsed)) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

bool read_int_in_range(const std::string& text, int low, int high, int& value) {
    try {
        size_t used = 0;
        const int parsed = std::stoi(text, &used);
        if (used != text.size() || parsed < low || parsed > high) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

// Кодирует кадр в JPEG и отправляет двухчастный ZMQ-пакет: JPEG + JSON статуса.
// Первая часть помечается SNDMORE, вторая содержит status.dump().
bool publish_status_video(void* socket, const cv::Mat& frame, const nlohmann::json& status) {
    std::vector<unsigned char> jpeg;
    if (!cv::imencode(".jpg", frame, jpeg, {cv::IMWRITE_JPEG_QUALITY, 90})) {
        return false;
    }

    const std::string status_text = status.dump();
    zmq_msg_t first;
    if (zmq_msg_init_size(&first, jpeg.size()) != 0) {
        return false;
    }
    std::copy(jpeg.begin(), jpeg.end(), static_cast<unsigned char*>(zmq_msg_data(&first)));
    if (zmq_msg_send(&first, socket, ZMQ_SNDMORE) == -1) {
        zmq_msg_close(&first);
        return false;
    }
    zmq_msg_close(&first);

    return zmq_send(socket, status_text.data(), status_text.size(), 0) != -1;
}

// GateStore stores legacy CIELAB (L 0..100, a/b -128..127); TrackingEngine
// thresholds are raw OpenCV 8-bit Lab (L 0..255, a/b 0..255 with 128 neutral).
// Переводит legacy L в диапазон 0..255 8-битного OpenCV Lab.
int lab_l_to_u8(int value) {
    return std::clamp(static_cast<int>(std::lround(value * 255.0 / 100.0)), 0, 255);
}

// Переводит legacy a/b в диапазон 0..255 со сдвигом на 128 (нейтральный канал).
int lab_ab_to_u8(int value) {
    return std::clamp(static_cast<int>(std::lround(value + 128.0)), 0, 255);
}

GateThreshold threshold_from_legacy_lab(const GateLab& lab) {
    GateThreshold threshold;
    threshold.l_min = lab_l_to_u8(lab[0]);
    threshold.l_max = lab_l_to_u8(lab[1]);
    threshold.a_min = lab_ab_to_u8(lab[2]);
    threshold.a_max = lab_ab_to_u8(lab[3]);
    threshold.b_min = lab_ab_to_u8(lab[4]);
    threshold.b_max = lab_ab_to_u8(lab[5]);
    if (threshold.l_min > threshold.l_max) std::swap(threshold.l_min, threshold.l_max);
    if (threshold.a_min > threshold.a_max) std::swap(threshold.a_min, threshold.a_max);
    if (threshold.b_min > threshold.b_max) std::swap(threshold.b_min, threshold.b_max);
    threshold.configured = true;
    return threshold;
}

// An enabled gate without a saved Lab range must still be detectable. Derive a
// tolerant window from its stored RGB instead of leaving it unconfigured.
GateThreshold threshold_from_rgb(const std::array<int, 3>& color) {
    const RgbLab lab = rgb_to_lab_u8(static_cast<uint8_t>(std::clamp(color[0], 0, 255)),
                                     static_cast<uint8_t>(std::clamp(color[1], 0, 255)),
                                     static_cast<uint8_t>(std::clamp(color[2], 0, 255)));
    GateLab legacy{};
    legacy[0] = std::clamp(static_cast<int>(std::lround(lab.L)) - 10, 0, 100);
    legacy[1] = std::clamp(static_cast<int>(std::lround(lab.L)) + 10, 0, 100);
    legacy[2] = std::clamp(static_cast<int>(std::lround(lab.A)) - 15, -128, 127);
    legacy[3] = std::clamp(static_cast<int>(std::lround(lab.A)) + 15, -128, 127);
    legacy[4] = std::clamp(static_cast<int>(std::lround(lab.B)) - 15, -128, 127);
    legacy[5] = std::clamp(static_cast<int>(std::lround(lab.B)) + 15, -128, 127);
    return threshold_from_legacy_lab(legacy);
}

} // namespace

int main(int argc, char** argv) {
    using namespace camera_cpp;

    std::string device = "/dev/video0";
    std::string laptop_ip = "10.132.122.120";
    std::string bind_host = "*";
    std::string gates_file;
    std::string fake_v4l2_log;
    std::string test_ready_file;
    std::string test_state_file;
    int port_video = kDefaultVideoPort;
    int port_colors = kDefaultColorsPort;
    int port_cam_settings = kDefaultCamSettingsPort;
    int max_frames = 0;
    int frame_delay_ms = 33;
    bool fake_camera = false;
    bool no_serial = false;
    bool once = false;
    SelfMask self_mask{false, 0.5F, 0.5F, 0.22F, 0.0F};

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto need_value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) {
                std::cerr << "missing value for " << name << std::endl;
                std::exit(2);
            }
            return argv[++i];
        };

        if (arg == "--help" || arg == "-h") {
            print_usage();
            return 0;
        } else if (arg == "--device") {
            device = need_value("--device");
        } else if (arg == "--laptop-ip") {
            laptop_ip = need_value("--laptop-ip");
        } else if (arg == "--bind-host") {
            bind_host = need_value("--bind-host");
        } else if (arg == "--gates-file") {
            gates_file = need_value("--gates-file");
        } else if (arg == "--fake-v4l2-log") {
            fake_v4l2_log = need_value("--fake-v4l2-log");
        } else if (arg == "--test-ready-file") {
            test_ready_file = need_value("--test-ready-file");
        } else if (arg == "--test-state-file") {
            test_state_file = need_value("--test-state-file");
        } else if (arg == "--port-video") {
            if (!read_int_in_range(need_value("--port-video"), 1, 65535, port_video)) return 2;
        } else if (arg == "--port-colors") {
            if (!read_int_in_range(need_value("--port-colors"), 1, 65535, port_colors)) return 2;
        } else if (arg == "--cam-settings-port") {
            if (!read_int_in_range(need_value("--cam-settings-port"), 1, 65535, port_cam_settings)) return 2;
        } else if (arg == "--max-frames") {
            if (!read_int_in_range(need_value("--max-frames"), 0, 1000000, max_frames)) return 2;
        } else if (arg == "--frame-delay-ms") {
            if (!read_int_in_range(need_value("--frame-delay-ms"), 0, 60000, frame_delay_ms)) return 2;
        } else if (arg == "--fake-camera") {
            fake_camera = true;
        } else if (arg == "--no-serial") {
            no_serial = true;
        } else if (arg == "--once") {
            once = true;
        } else if (arg == "--self-mask-cx") {
            if (!read_float(need_value("--self-mask-cx"), self_mask.cx)) return 2;
            self_mask.enabled = true;
        } else if (arg == "--self-mask-cy") {
            if (!read_float(need_value("--self-mask-cy"), self_mask.cy)) return 2;
            self_mask.enabled = true;
        } else if (arg == "--self-mask-r") {
            if (!read_float(need_value("--self-mask-r"), self_mask.r)) return 2;
            self_mask.enabled = true;
        } else if (arg == "--self-mask-r2") {
            if (!read_float(need_value("--self-mask-r2"), self_mask.r2)) return 2;
            self_mask.enabled = true;
        } else if (arg == "--self-mask-enabled") {
            self_mask.enabled = true;
        } else {
            std::cerr << "unknown option: " << arg << std::endl;
            print_usage();
            return 2;
        }
    }

    if (self_mask.cx < 0.0F || self_mask.cx > 1.0F || self_mask.cy < 0.0F || self_mask.cy > 1.0F
        || self_mask.r <= 0.0F || self_mask.r > 1.0F || self_mask.r2 < 0.0F) {
        std::cerr << "self-mask fractions are out of range" << std::endl;
        return 2;
    }
    if (laptop_ip.empty() || bind_host.empty()) {
        std::cerr << "--laptop-ip and --bind-host must be non-empty" << std::endl;
        return 2;
    }
    if (port_video == port_colors || port_video == port_cam_settings || port_colors == port_cam_settings) {
        std::cerr << "video, color and CAM SET ports must be distinct" << std::endl;
        return 2;
    }
    if (once) {
        max_frames = 1;
    }
    std::cout << "laptop-ip (publish-only, never connected): " << laptop_ip << std::endl;
    std::cout << "serial link: " << (no_serial ? "disabled by --no-serial" : "not managed by this service") << std::endl;

    GuardedGateStore gates;
    const auto saved_path = gates_file.empty() ? contract_path("saved_gates.json") : std::filesystem::path(gates_file);
    if (!gates.store.load(saved_path.string())) {
        std::cerr << "warning: saved gates unavailable; starting with all gates disabled" << std::endl;
    }
    std::cout << "loaded " << saved_path.string() << std::endl;

    auto runner = fake_v4l2_log.empty()
        ? std::static_pointer_cast<CommandRunner>(std::make_shared<ProcessCommandRunner>())
        : std::static_pointer_cast<CommandRunner>(std::make_shared<FakeV4l2Runner>(fake_v4l2_log));
    ColorListener color_listener(port_colors);
    CamSettingsListener settings_listener(port_cam_settings);
    std::cout << "listeners: colors tcp://" << bind_host << ":" << port_colors
              << " cam_settings tcp://" << bind_host << ":" << port_cam_settings << std::endl;
    if (!color_listener.start(gates.store, bind_host, &gates.mutex)) {
        std::cerr << "color listener failed to bind tcp://" << bind_host << ":" << port_colors << std::endl;
    }
    if (!settings_listener.start(runner, device, bind_host)) {
        std::cerr << "cam settings listener failed to bind tcp://" << bind_host << ":" << port_cam_settings << std::endl;
    }

    std::unique_ptr<CaptureBackend> backend;
    if (fake_camera) {
        backend = std::make_unique<FakeCaptureBackend>();
    }
    CameraEngine engine(backend ? std::move(backend) : make_open_cv_v4l2_backend());
    CaptureOpenReport open_report;
    if (!engine.open(CaptureConfig{1920, 1080, 30, "MJPG", device}, &open_report)) {
        engine.close();
        color_listener.shutdown();
        settings_listener.shutdown();
        std::cerr << "camera open failed: " << device;
        if (!open_report.error.empty()) std::cerr << " (" << open_report.error << ")";
        std::cerr << std::endl;
        return 3;
    }
    // Requested mode is not the negotiated mode; print readback warnings instead
    // of claiming that 1920x1080 MJPG @30 was actually obtained.
    if (!open_report.warning.empty()) {
        std::cerr << "capture warning: " << open_report.warning << std::endl;
    }
    std::cout << "camera requested: " << device << " 1920x1080 MJPG @30; publish tcp://"
              << bind_host << ":" << port_video << std::endl;

    void* context = zmq_ctx_new();
    void* publisher = context ? zmq_socket(context, ZMQ_PUB) : nullptr;
    int linger = 0;
    const std::string endpoint = "tcp://" + bind_host + ":" + std::to_string(port_video);
    int publish_ok = publisher && zmq_setsockopt(publisher, ZMQ_LINGER, &linger, sizeof(linger)) == 0
        && zmq_bind(publisher, endpoint.c_str()) == 0;
    if (context == nullptr || !publisher || !publish_ok) {
        std::cerr << "failed to bind video/status port " << port_video << std::endl;
        if (publisher) zmq_close(publisher);
        if (context) zmq_ctx_term(context);
        engine.close();
        color_listener.shutdown();
        settings_listener.shutdown();
        return 4;
    }

    if (!test_ready_file.empty()) {
        std::string error;
        if (!atomic_write_text(test_ready_file, "ready", &error)) {
            std::cerr << "failed to write test ready file: " << error << std::endl;
            zmq_close(publisher);
            zmq_ctx_term(context);
            engine.close();
            color_listener.shutdown();
            settings_listener.shutdown();
            return 5;
        }
    }

    TrackingEngine tracking;
    TrackInput track_input;
    track_input.self_mask = self_mask;

    auto previous = std::chrono::steady_clock::now();
    auto next_frame = previous;
    float fps = 0.0F;
    int frames_processed = 0;
    int frames_published = 0;

    while (max_frames == 0 || frames_processed < max_frames) {
        camera_cpp::EngineFrame frame;
        if (!engine.read_reusing(frame)) {
            std::cerr << "camera read failed" << std::endl;
            break;
        }
        ++frames_processed;

        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - previous).count();
        previous = now;
        if (seconds > 0.0) {
            const float instantaneous = static_cast<float>(1.0 / seconds);
            fps = fps == 0.0F ? instantaneous : fps * 0.82F + instantaneous * 0.18F;
        }
        track_input.portrait_rgb = &frame.processing_frame;

        // Snapshot shared state once per frame. No lock is held during tracking.
        std::array<std::string, 3> roles{};
        {
            std::lock_guard<std::mutex> lock(gates.mutex);
            for (std::size_t index = 0; index < track_input.thresholds.size(); ++index) {
                const GateId id = static_cast<GateId>(index + 1);
                const auto record = gates.store.gate(id);
                if (!record || !record->enabled) {
                    track_input.thresholds[index] = GateThreshold{};
                    roles[index] = "unset";
                    continue;
                }
                track_input.thresholds[index] = record->lab
                    ? threshold_from_legacy_lab(*record->lab)
                    : threshold_from_rgb(record->rgb);
                roles[index] = record->role.empty() ? "unset" : record->role;
            }
        }

        const auto tracked = tracking.track(track_input);
        std::array<GateStatus, 3> statuses{};
        for (std::size_t index = 0; index < statuses.size(); ++index) {
            const auto& result = tracked[index];
            statuses[index] = GateStatus{
                static_cast<int>(index + 1),
                result.enabled,
                result.found,
                result.angle_deg,
                result.radius_px,
                result.dist_cm,
                result.dist_reliable,
                result.held,
                result.bbox.empty() ? 0.0F
                    : static_cast<float>(result.bbox.width) * static_cast<float>(result.bbox.height),
                roles[index],
            };
        }

        const auto status = StatusBuilder{}.build(statuses, fps, {640, 360}, "pi");
        if (publish_status_video(publisher, frame.processing_frame, status)) {
            ++frames_published;
        }

        if (!test_state_file.empty()) {
            nlohmann::json snapshot;
            snapshot["frames"] = frames_processed;
            snapshot["published"] = frames_published;
            snapshot["fps"] = fps;
            snapshot["gates"] = nlohmann::json::array();
            for (std::size_t index = 0; index < statuses.size(); ++index) {
                const auto& threshold = track_input.thresholds[index];
                snapshot["gates"].push_back({
                    {"id", statuses[index].id},
                    {"enabled", statuses[index].enabled},
                    {"configured", threshold.configured},
                    {"l_min", threshold.l_min}, {"l_max", threshold.l_max},
                    {"a_min", threshold.a_min}, {"a_max", threshold.a_max},
                    {"b_min", threshold.b_min}, {"b_max", threshold.b_max},
                    {"found", statuses[index].found},
                    {"held", statuses[index].held},
                    {"area", statuses[index].area_px},
                    {"role", statuses[index].role},
                });
            }
            const std::string last = settings_listener.last_message();
            try {
                snapshot["last_cam_set"] = last.empty() ? nlohmann::json(nullptr) : nlohmann::json::parse(last);
            } catch (...) {
                snapshot["last_cam_set"] = nlohmann::json(nullptr);
            }
            std::string state_error;
            atomic_write_text(test_state_file, snapshot.dump(), &state_error);
        }

        next_frame += std::chrono::milliseconds(frame_delay_ms);
        if (next_frame < std::chrono::steady_clock::now()) {
            next_frame = std::chrono::steady_clock::now();
        }
        std::this_thread::sleep_until(next_frame);
    }

    zmq_close(publisher);
    zmq_ctx_term(context);
    engine.close();
    color_listener.shutdown();
    settings_listener.shutdown();

    if (once || max_frames > 0) {
        return frames_processed > 0 && frames_published > 0 ? 0 : 1;
    }
    std::cout << "service stopped" << std::endl;
    return 0;
}
