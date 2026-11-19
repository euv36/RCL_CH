#include "camera_engine.hpp"
#include "cam_settings.hpp"
#include "color_listener.hpp"
#include "status_builder.hpp"

#include <chrono>
#include <cmath>
#include <cstdio>
#include <filesystem>
#include <iostream>
#include <string>
#include <thread>
#include <vector>

#ifndef _WIN32
#include <sys/wait.h>
#endif

#include <opencv2/imgcodecs.hpp>
#include <zmq.h>

#include "common/gate_store.hpp"
#include "common/paths.hpp"
#include "vision/tracking.hpp"

namespace {

using namespace camera_cpp;

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

void print_usage() {
    std::cout << "Usage: camera_service [--laptop-ip <ip>] [--device <path>] [--port-video <port>] "
              << "[--self-mask-cx <0-1>] [--self-mask-cy <0-1>] [--self-mask-r <0-1>] "
              << "[--self-mask-r2 <0-1>] [--self-mask-enabled] [--no-serial] [--once]\n";
    std::cout << "  --laptop-ip  Legacy address; camera service publishes status/video and does not connect to it.\n";
    std::cout << "  --device     Camera device (default /dev/video0).\n";
    std::cout << "  --port-video Video/status publish port (default 5555).\n";
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

bool read_port(const std::string& text, int& value) {
    try {
        size_t used = 0;
        const int parsed = std::stoi(text, &used);
        if (used != text.size() || parsed < 1 || parsed > 65535) {
            return false;
        }
        value = parsed;
        return true;
    } catch (...) {
        return false;
    }
}

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

} // namespace

int main(int argc, char** argv) {
    using namespace camera_cpp;

    std::string device = "/dev/video0";
    std::string laptop_ip = "10.132.122.120";
    int port_video = 5555;
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
        } else if (arg == "--port-video") {
            if (!read_port(need_value("--port-video"), port_video)) {
                std::cerr << "--port-video must be 1..65535" << std::endl;
                return 2;
            }
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
    (void)laptop_ip;
    (void)no_serial;

    const auto saved_path = contract_path("saved_gates.json");
    GateStore store;
    if (!store.load(saved_path.string())) {
        std::cerr << "warning: saved gates unavailable; starting with all gates disabled" << std::endl;
    }
    std::cout << "loaded " << saved_path.string() << std::endl;

    auto runner = std::make_shared<ProcessCommandRunner>();
    ColorListener color_listener(ports::kColors);
    CamSettingsListener settings_listener(ports::kCamSettings);
    std::cout << "listeners: colors tcp://*:" << ports::kColors
              << " cam_settings tcp://*:" << ports::kCamSettings << std::endl;
    if (!color_listener.start(store)) {
        std::cerr << "color listener failed to bind tcp://*:" << ports::kColors << std::endl;
    }
    if (!settings_listener.start(runner, device)) {
        std::cerr << "cam settings listener failed to bind tcp://*:" << ports::kCamSettings << std::endl;
    }

    CameraEngine engine;
    if (!engine.open(CaptureConfig{1920, 1080, 30, "MJPG", device})) {
        engine.close();
        color_listener.shutdown();
        settings_listener.shutdown();
        std::cerr << "camera open failed: " << device << std::endl;
        return 3;
    }
    std::cout << "camera open: " << device << " 1920x1080 MJPG @30; publish tcp://*:" << port_video << std::endl;

    void* context = zmq_ctx_new();
    void* publisher = context ? zmq_socket(context, ZMQ_PUB) : nullptr;
    int linger = 0;
    int publish_ok = publisher && zmq_setsockopt(publisher, ZMQ_LINGER, &linger, sizeof(linger)) == 0
        && zmq_bind(publisher, ("tcp://*:" + std::to_string(port_video)).c_str()) == 0;
    if (context == nullptr || !publisher || !publish_ok) {
        std::cerr << "failed to bind video/status port " << port_video << std::endl;
    }

    TrackingEngine tracking;
    TrackInput track_input;
    track_input.self_mask = self_mask;

    auto previous = std::chrono::steady_clock::now();
    auto next_frame = previous;
    float fps = 0.0F;
    int frames_processed = 0;
    int frames_published = 0;

    while (true) {
        const auto frame = engine.read();
        if (!frame) {
            std::cerr << "camera read failed" << std::endl;
            break;
        }
        ++frames_processed;

        const auto now = std::chrono::steady_clock::now();
        const double seconds = std::chrono::duration<double>(now - previous).count();
        previous = now;
        if (seconds > 0.0 && seconds < 1.0) {
            const float instantaneous = static_cast<float>(1.0 / seconds);
            fps = fps == 0.0F ? instantaneous : fps * 0.82F + instantaneous * 0.18F;
        }
        track_input.portrait_rgb = &frame->processing_frame;
        const auto tracked = tracking.track(track_input);

        std::array<GateStatus, 3> statuses{};
        for (size_t index = 0; index < statuses.size(); ++index) {
            const GateId id = static_cast<GateId>(index + 1);
            const auto& result = tracked[index];
            statuses[index] = GateStatus{
                static_cast<int>(index + 1),
                store.enabled(id) || result.enabled,
                result.found,
                result.angle_deg,
                result.radius_px,
                result.dist_cm,
                result.dist_reliable,
                result.held,
            };
        }

        const auto status = StatusBuilder{}.build(statuses, fps, {640, 360}, "pi");
        if (publish_ok && publish_status_video(publisher, frame->processing_frame, status)) {
            ++frames_published;
        }

        if (once) {
            break;
        }

        next_frame += std::chrono::milliseconds(33);
        if (next_frame < std::chrono::steady_clock::now()) {
            next_frame = std::chrono::steady_clock::now();
        }
        std::this_thread::sleep_until(next_frame);
    }

    if (publisher) {
        zmq_close(publisher);
    }
    if (context) {
        zmq_ctx_term(context);
    }
    engine.close();
    color_listener.shutdown();
    settings_listener.shutdown();

    if (once) {
        return frames_processed > 0 && frames_published > 0 ? 0 : 1;
    }
    std::cout << "service stopped" << std::endl;
    return 0;
}
