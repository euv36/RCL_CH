// pi_core/main.cpp — entrypoint: --probe / --snapshot / normal run.
// Full replacement of rpi_elp_cam.py on the Pi (Idea 4). Log lines and the
// FPS/stage-timing format match the Python original so numbers can be
// compared side by side.
// UNVERIFIED ON HW: the whole Pi pipeline (camera open, decode timings, ZMQ,
// serial) — the logic layer is unit-tested + cv2-parity-checked on Windows
// (see tests/), these IO paths compile only on the Pi.
//
// Thread map (same as Python):
//   main          : decode -> HSV -> gates -> serial -> publish, 5 s FPS log
//   CaptureThread : camera drain (latest frame)
//   Streamer      : JPEG encode + ZMQ PUB 5555
//   ColorListener : ZMQ SUB 5557 colors/stream toggle
// Future modules (tactics, I2C sensors, Ideas 5-8) must be separate
// threads/modules and must not enter the camera hot loop (CPU budget ~1 core).
#include "camera.hpp"
#include "color_listener.hpp"
#include "config.hpp"
#include "gate_logic.hpp"
#include "gate_store.hpp"
#include "gate_tracker.hpp"
#include "log.hpp"
#include "serial_link.hpp"
#include "streamer.hpp"
#include "v4l2_ctl.hpp"

#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/videoio.hpp>
#include <zmq.hpp>

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstring>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace pi_core {
namespace {

std::atomic<bool> g_stop{false};

void on_signal(int) { g_stop.store(true); }

using Clock = std::chrono::steady_clock;

double seconds_since(const Clock::time_point& t0) {
    return std::chrono::duration<double>(Clock::now() - t0).count();
}

// Python %.1f for the camera fps log.
std::string fmt_1f(double v) {
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%.1f", v);
    return buf;
}

// UNVERIFIED ON HW: OpenCV imdecode flags carry the same numeric values as
// cv2 (IMREAD_REDUCED_COLOR_2 = 17, IMREAD_REDUCED_COLOR_4 = 33), so the
// "Декодирование: <flag>" log prints the same number as Python.
int choose_decode_flag(int capture_w, int track_w) {
    if (capture_w < 1000) return cv::IMREAD_COLOR;
    if (track_w <= 420) return cv::IMREAD_REDUCED_COLOR_4;
    if (track_w <= 1000) return cv::IMREAD_REDUCED_COLOR_2;
    return cv::IMREAD_COLOR;
}

int decode_width_for(int capture_w, int decode_flag) {
    switch (decode_flag) {
        case cv::IMREAD_REDUCED_COLOR_2: return capture_w / 2;
        case cv::IMREAD_REDUCED_COLOR_4: return capture_w / 4;
        default: return capture_w;
    }
}

// Decode raw MJPG straight to the reduced size, then fit to track size.
// Python frame_from_raw(); a backend-provided 3-channel frame is used as-is.
bool frame_from_raw(const cv::Mat& data, int decode_flag, int track_w,
                    int track_h, cv::Mat& out) {
    cv::Mat frame;
    if (data.channels() == 3) {
        frame = data;
    } else {
        frame = cv::imdecode(data, decode_flag);
        if (frame.empty()) return false;
    }
    if (frame.cols != track_w || frame.rows != track_h) {
        // Downscale -> INTER_AREA, (unlikely) upscale -> INTER_LINEAR.
        const int interp =
            frame.cols >= track_w ? cv::INTER_AREA : cv::INTER_LINEAR;
        cv::Mat resized;
        cv::resize(frame, resized, cv::Size(track_w, track_h), 0, 0, interp);
        out = resized;
    } else {
        out = frame;
    }
    return true;
}

// ---------------- probe / snapshot ----------------

int probe_camera(const Config& args) {
    apply_camera_controls(args.device, args.exposure, args.gain,
                          args.brightness, args.power_line, args.gamma,
                          args.saturation, args.contrast);
    std::unique_ptr<RawCamera> cam;
    try {
        cam = std::make_unique<RawCamera>(args.device, args.capture_w,
                                          args.capture_h, args.camera_fps);
    } catch (const std::exception& e) {
        log_msg(std::string("probe: ") + e.what(), "ERROR");
        restore_camera_controls(args.device);
        return 1;
    }
    int rc = 0;
    try {
        log_msg("Фактический режим: " + std::to_string(cam->actual_w()) + "x" +
                std::to_string(cam->actual_h()) + " @ " +
                fmt_1f(cam->actual_fps()) + " fps, FOURCC=" +
                fourcc_str(cam->actual_fourcc()) + std::string(", raw=") +
                (cam->raw_mode() ? "True" : "False"));

        const int frames = 120;
        cv::Mat last;
        int n = 0;
        const auto t0 = Clock::now();
        for (int i = 0; i < frames; ++i) {
            cv::Mat data;
            if (!cam->read(data)) break;
            last = data;
            ++n;
        }
        const double dt = seconds_since(t0);
        if (dt > 1e-6) {
            char buf[128];
            std::snprintf(buf, sizeof(buf),
                          "Сырой захват: %.1f fps (%d кадров за %.2f с)",
                          n / dt, n, dt);
            log_msg(buf);
        }

        if (!last.empty() && last.channels() != 3) {
            const int k = 20;
            const std::pair<int, const char*> flags[] = {
                {cv::IMREAD_COLOR, "full"},
                {cv::IMREAD_REDUCED_COLOR_2, "reduced_2"},
                {cv::IMREAD_REDUCED_COLOR_4, "reduced_4"}};
            for (const auto& fp : flags) {
                cv::Mat img;
                for (int i = 0; i < 2; ++i) img = cv::imdecode(last, fp.first);
                if (img.empty()) continue;  // Python would crash here; we skip
                const auto tb = Clock::now();
                for (int i = 0; i < k; ++i) img = cv::imdecode(last, fp.first);
                const double dtk = seconds_since(tb);
                char buf[128];
                std::snprintf(buf, sizeof(buf),
                              "Декодирование %-9s: %5.2f мс (%5.1f fps), %dx%d",
                              fp.second, dtk / k * 1000.0, k / dtk, img.cols,
                              img.rows);
                log_msg(buf);
            }
        }
    } catch (const std::exception& e) {
        log_msg(std::string("probe failed: ") + e.what(), "ERROR");
        rc = 1;
    }
    cam->release();
    restore_camera_controls(args.device);
    return rc;
}

int snapshot(const Config& args) {
    apply_camera_controls(args.device, args.exposure, args.gain,
                          args.brightness, args.power_line, args.gamma,
                          args.saturation, args.contrast);
    std::unique_ptr<RawCamera> cam;
    try {
        cam = std::make_unique<RawCamera>(args.device, args.capture_w,
                                          args.capture_h, args.camera_fps);
    } catch (const std::exception& e) {
        log_msg(std::string("snapshot: ") + e.what(), "ERROR");
        restore_camera_controls(args.device);
        return 1;
    }
    int rc = 0;
    try {
        for (int i = 0; i < 25; ++i) {  // let the camera settle
            cv::Mat skip;
            cam->read(skip);
        }
        cv::Mat data;
        if (!cam->read(data)) throw std::runtime_error("Не удалось получить кадр");

        cv::Mat full, small;
        if (data.channels() == 3) {
            full = data;
            small = data;
        } else {
            full = cv::imdecode(data, cv::IMREAD_COLOR);
            small = cv::imdecode(data, cv::IMREAD_REDUCED_COLOR_2);
        }
        if (full.empty() || small.empty())
            throw std::runtime_error("Не удалось декодировать MJPG");
        cv::resize(small, small, cv::Size(args.track_w, args.track_h), 0, 0,
                   cv::INTER_AREA);

        // base + "_full" + ext (os.path.splitext equivalent for simple names)
        std::string file = args.snapshot_file;
        std::string base = file, ext = "";
        const auto dot = file.rfind('.');
        if (dot != std::string::npos && dot > 0) {
            base = file.substr(0, dot);
            ext = file.substr(dot);
        }
        if (ext.empty()) ext = ".jpg";
        const std::string full_path = base + "_full" + ext;
        const std::vector<int> q = {cv::IMWRITE_JPEG_QUALITY, 90};
        cv::imwrite(file, small, q);
        cv::imwrite(full_path, full, q);
        log_msg("Сохранено: " + file + " (" + std::to_string(args.track_w) +
                "x" + std::to_string(args.track_h) + ") и " + full_path + " (" +
                std::to_string(full.cols) + "x" + std::to_string(full.rows) +
                ")");
    } catch (const std::exception& e) {
        log_msg(e.what(), "ERROR");
        rc = 1;
    }
    cam->release();
    restore_camera_controls(args.device);
    return rc;
}

// ---------------- serial handling (per gate) ----------------

void handle_gate_serial(SerialGateState& st, bool found,
                        const GateResult& info, SerialSink* serial,
                        double now, int gate_id) {
    const SerialDecision d = decide_serial(st, found, found ? info.angle : -1,
                                           found ? info.area : 0.0, now);
    if (d.action == SerialAction::SendAngle && serial != nullptr) {
        if (serial->write(format_gate_line(gate_id, d.angle, d.area))) {
            commit_sent_angle(st, d.angle, now);
        }
    } else if (d.action == SerialAction::SendLost && serial != nullptr) {
        if (serial->write(format_lost_line(gate_id))) {
            commit_sent_lost(st);
        }
    }
}

// ---------------- main run loop ----------------

int run(const Config& args) {
    cv::setUseOptimized(true);
    // CRITICAL: >1 OpenCV thread makes MJPG decode ~3x slower on this Pi
    // (REDUCED_2: 15.3 ms @ 1 thread vs 39.5 ms @ 4, measured — see
    // UniKostyl/AGENTS.md). Default is strictly 1.
    const int threads = args.threads > 0 ? args.threads : 1;
    cv::setNumThreads(threads);

    if (args.capture_w != 1920 || args.capture_h != 1080) {
        log_msg(
            "ВНИМАНИЕ: режим камеры отличается от 1920x1080. "
            "Нижние режимы обрезают угол обзора — полный FOV не гарантирован.",
            "WARN");
    }

    log_msg("Захват " + std::to_string(args.capture_w) + "x" +
            std::to_string(args.capture_h) + " MJPG -> обработка " +
            std::to_string(args.track_w) + "x" + std::to_string(args.track_h) +
            " (полный кадр, без кропа)");

    apply_camera_controls(args.device, args.exposure, args.gain,
                          args.brightness, args.power_line, args.gamma,
                          args.saturation, args.contrast);

    std::unique_ptr<RawCamera> camera;
    try {
        camera = std::make_unique<RawCamera>(args.device, args.capture_w,
                                             args.capture_h, args.camera_fps);
    } catch (const std::exception& e) {
        log_msg(e.what(), "ERROR");
        return 1;
    }
    log_msg("Камера: " + std::to_string(camera->actual_w()) + "x" +
            std::to_string(camera->actual_h()) + " @ " +
            fmt_1f(camera->actual_fps()) + " fps, FOURCC=" +
            fourcc_str(camera->actual_fourcc()));

    CaptureThread capture(*camera);
    capture.start();

    // Serial sink: device, stdout (--print-serial) or none (--no-serial).
    std::unique_ptr<SerialSink> serial;
    if (args.print_serial) {
        serial = open_stdout_sink();
    } else if (args.serial_enabled) {
        serial = open_serial_device(args.serial_port);
        if (!serial) {
            log_msg("Serial " + args.serial_port +
                        " недоступен — работаем без Arduino",
                    "WARN");
        }
    }

    const int decode_flag =
        choose_decode_flag(camera->actual_w(), args.track_w);
    const int decode_w = decode_width_for(camera->actual_w(), decode_flag);
    {
        char buf[160];
        std::snprintf(buf, sizeof(buf),
                      "Декодирование: %d (примерно %d px по ширине), "
                      "OpenCV threads=%d",
                      decode_flag, decode_w, cv::getNumThreads());
        log_msg(buf);
    }

    // After rotation the working frame may become portrait.
    int work_w = args.track_w, work_h = args.track_h;
    if (args.rotate == 90 || args.rotate == 270) {
        work_w = args.track_h;
        work_h = args.track_w;
    }

    // Area thresholds scale with resolution so tuning at 640x360 transfers
    // to other track sizes (same as Python).
    const double area_scale =
        (1920.0 * 1080.0) / static_cast<double>(work_w * work_h);
    const double area_ratio = 100.0 * work_w * work_h / (1920.0 * 1080.0);
    const double area_min = area_ratio < 8.0 ? 8.0 : area_ratio;

    // Idea 5: clustering + hysteresis. --min-area is given in the 1080p
    // equivalent and scales like area_min (same as Python run()).
    double min_area_px = 0.0;
    if (args.min_area > 0.0) {
        min_area_px = args.min_area * work_w * work_h / (1920.0 * 1080.0);
        if (min_area_px < 1.0) min_area_px = 1.0;
    }
    const ClusterParams cluster = resolve_cluster_params(
        work_w, work_h, args.merge_gap, min_area_px, args.max_stray,
        args.open_kernel, args.lost_frames, args.confirm_frames);

    GateTracker tracker(
        work_w, work_h, Tolerance{args.tol_h, args.tol_s, args.tol_v},
        area_min, area_scale, args.gates, cluster);
    {
        char buf[192];
        std::snprintf(buf, sizeof(buf),
                      "Кластеризация (Idea 5): merge_gap=%.0f px, "
                      "min_area=%.0f px, max_stray=%.0f px, open=%dx%d, "
                      "потеря через %d пустых кадров, подтверждение %d кадров",
                      cluster.merge_gap_px, cluster.min_area_px,
                      cluster.max_stray_px, cluster.open_kernel,
                      cluster.open_kernel, cluster.lost_frames,
                      cluster.confirm_frames);
        log_msg(buf);
    }

    // Idea 8: robot self-mask (Pi PCB + carbon tubes in the center of view
    // are cut out of the color search before morphology/CC). CLI flags are
    // px in the working frame (<= 0 = auto: frame center,
    // r = 0.22*min(w,h)). When the camera_settings.json auto-apply lands in
    // C++ (Idea 1's known next step), its "self_mask" section (fractions)
    // must override these flags the same way the Python run() does — and
    // when Idea 2's multipart status is repeated here, report the applied
    // circle as a top-level "self_mask" key exactly like
    // rpi_elp_cam.build_gate_status (tracker.self_mask() has the values).
    const SelfMaskParams self_p = resolve_self_mask(
        work_w, work_h, args.self_mask_on, args.self_cx, args.self_cy,
        args.self_r, args.self_r2);
    tracker.set_self_mask(self_p);
    if (self_p.enabled) {
        if (self_p.r >= 0.45 * static_cast<double>(work_w < work_h ? work_w
                                                                   : work_h)) {
            log_msg("self-маска: радиус покрывает почти весь кадр " +
                        std::to_string(work_w) + "x" +
                        std::to_string(work_h) + " — проверьте cx/cy/r",
                    "WARN");
        }
        char buf[160];
        if (self_p.r2 > 0.0) {
            std::snprintf(buf, sizeof(buf),
                          "Self-маска (Idea 8): центр (%.0f, %.0f), "
                          "r=%.0f px, кольцо до r2=%.0f px — корпус "
                          "исключён из поиска цвета",
                          self_p.cx, self_p.cy, self_p.r, self_p.r2);
        } else {
            std::snprintf(buf, sizeof(buf),
                          "Self-маска (Idea 8): центр (%.0f, %.0f), "
                          "r=%.0f px — корпус исключён из поиска цвета",
                          self_p.cx, self_p.cy, self_p.r);
        }
        log_msg(buf);
    } else {
        log_msg("Self-маска выключена (--no-self-mask)");
    }

    // Idea 6: distance calibration (distance_calib.json, written atomically
    // by the calibrator out/dist_calibrate.py). Missing/corrupt file or a
    // rejected model -> analytic fallback
    // Z = gate_width_cm * (frame_w / pi) / w_px (rules width, --gate-width).
    if (args.dist_on) {
        DistanceModel dist_model = load_distance_model(args.dist_calib_path);
        if (dist_model.valid) {
            tracker.set_distance_model(dist_model);
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "Дистанция (Idea 6): калибровка %s: a=%.4g, b=%.4g, "
                          "c=%.4g, точек %zu, кадр %dx%d",
                          args.dist_calib_path.c_str(), dist_model.a,
                          dist_model.b, dist_model.c,
                          dist_model.points.size() / 2,
                          dist_model.frame_w, dist_model.frame_h);
            log_msg(buf);
        } else {
            char buf[192];
            std::snprintf(buf, sizeof(buf),
                          "Дистанция (Idea 6): калибровки нет (%s) — "
                          "аналитическая модель Z = %.0f см * (ширина/π) / "
                          "w_px; откалибруйте: out/dist_calibrate.py",
                          dist_model.error.empty() ? "файл не найден"
                                                   : dist_model.error.c_str(),
                          args.gate_width_cm);
            log_msg(buf);
        }
        char buf[128];
        std::snprintf(buf, sizeof(buf),
                      "Дистанция: удержание %.1f с при заслонении, порог "
                      "уменьшения %.2f, мин. заполненность %.2f",
                      args.dist_hold, args.dist_drop, args.dist_fill);
        log_msg(buf);
    } else {
        log_msg("Дистанция (Idea 6) выключена (--no-dist)");
    }

    // saved_gates.json (Idea 3): gates become active right after a restart,
    // no laptop needed. A missing file is normal (first run).
    GateStore store;
    const bool loaded = store.load(args.saved_gates_path);
    if (loaded) {
        std::string applied;
        for (int id = 1; id <= args.gates && id <= 3; ++id) {
            const GateRecord rec = store.record(id);
            if (rec.enabled && rec.has_rgb) {
                tracker.update_gate_from_rgb(id - 1, rec.rgb[0], rec.rgb[1],
                                             rec.rgb[2]);
                // Idea 9: restore the role into tracker params so the status
                // JSON reports it right after a restart (Python does the
                // same in run(); "unset"/unknown -> Unset = not reported).
                tracker.set_gate_role(id - 1,
                                      gate_role_from_string(rec.role));
                if (!applied.empty()) applied += ", ";
                applied += std::string("G") + std::to_string(id);
            }
        }
        if (!applied.empty()) {
            log_msg("Загружены цвета из " + args.saved_gates_path + ": " +
                    applied);
        } else {
            log_msg(args.saved_gates_path +
                    ": сохранённых включённых цветов нет — ждём от ноутбука");
        }
    } else {
        log_msg(args.saved_gates_path +
                " не найден — ворота активируются цветами от ноутбука");
    }

    zmq::context_t ctx;

    Streamer streamer(ctx, args.video_port, args.stream_fps, args.jpeg_quality,
                      args.stream_on_start);
    streamer.start();

    ColorListener listener(ctx, args.laptop_ip, args.rgb_port, tracker, store,
                           streamer, args.gates);
    listener.start();

    // Overlay palette (BGR), same values as Python.
    const cv::Vec3b palette[] = {
        cv::Vec3b(0, 255, 0),   cv::Vec3b(255, 0, 255), cv::Vec3b(0, 255, 255),
        cv::Vec3b(255, 255, 0), cv::Vec3b(0, 128, 255), cv::Vec3b(255, 0, 0)};

    const bool stream_size_differs =
        args.stream_size_set &&
        (args.stream_w != work_w || args.stream_h != work_h);
    const int stream_w = args.stream_size_set ? args.stream_w : work_w;
    const int stream_h = args.stream_size_set ? args.stream_h : work_h;
    if (stream_size_differs) {
        log_msg("Видео будет уменьшено до " + std::to_string(stream_w) + "x" +
                std::to_string(stream_h));
    }

    cv::Mat hsv(work_h, work_w, CV_8UC3);  // reused every frame

    long long last_seq = -1;
    const auto run_start = Clock::now();
    double fps_window_t0 = 0.0;
    int fps_count = 0;
    double t_dec = 0.0, t_hsv = 0.0, t_gates = 0.0, t_stream = 0.0;
    bool slow_hint_shown = false;
    // -1000 so the first log lines fire immediately (Python's monotonic() on
    // the Pi is seconds-since-boot, i.e. already >> thresholds at startup).
    double angle_log_t0 = -1000.0;
    double last_hb = -1000.0;
    double last_activation_log = -1000.0;
    double last_store_save = -1000.0;
    std::vector<SerialGateState> serial_rt(args.gates);
    std::vector<std::string> frame_angles;
    frame_angles.reserve(static_cast<size_t>(args.gates));
    cv::Mat frame, stream_frame;

    const bool has_end = args.duration > 0.0;
    const auto end_time =
        has_end ? run_start + std::chrono::duration_cast<Clock::duration>(
                                 std::chrono::duration<double>(args.duration))
                : Clock::time_point::max();

    log_msg("Готово. Ноутбук: " + args.laptop_ip + ":" +
            std::to_string(args.rgb_port) + ", видео: :" +
            std::to_string(args.video_port) + ", Serial: " +
            (args.print_serial
                 ? std::string("stdout (--print-serial)")
                 : (serial ? args.serial_port : std::string("выкл"))));
    if (has_end) {
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Ограничение по времени: %.0f с",
                      args.duration);
        log_msg(buf);
    }

    long long hot_loop_errors = 0;

    while (!g_stop.load() && Clock::now() < end_time) {
        try {
            cv::Mat data;
            long long seq = -1;
            if (!capture.get_latest(last_seq, data, seq)) {
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                continue;
            }
            last_seq = seq;

            const auto ta = Clock::now();
            if (!frame_from_raw(data, decode_flag, args.track_w, args.track_h,
                                frame)) {
                continue;
            }
            if (args.rotate) {
                cv::Mat rotated;
                int code = -1;
                if (args.rotate == 90) code = cv::ROTATE_90_CLOCKWISE;
                else if (args.rotate == 180) code = cv::ROTATE_180;
                else if (args.rotate == 270) code = cv::ROTATE_90_COUNTERCLOCKWISE;
                if (code >= 0) {
                    cv::rotate(frame, rotated, code);
                    frame = rotated;
                }
            }
            if (frame.cols != work_w || frame.rows != work_h) {
                const int interp =
                    frame.cols >= work_w ? cv::INTER_AREA : cv::INTER_LINEAR;
                cv::Mat resized;
                cv::resize(frame, resized, cv::Size(work_w, work_h), 0, 0,
                           interp);
                frame = resized;
            }
            const auto tb = Clock::now();

            cv::cvtColor(frame, hsv, cv::COLOR_BGR2HSV);
            const auto tc = Clock::now();

            const double now = seconds_since(run_start);
            bool any_gate = false;
            frame_angles.clear();
            for (int gate_idx = 0; gate_idx < args.gates; ++gate_idx) {
                if (!tracker.is_enabled(gate_idx)) continue;
                any_gate = true;
                GateResult info;
                const bool found = tracker.process(hsv, gate_idx, info);
                handle_gate_serial(serial_rt[gate_idx], found, info,
                                   serial.get(), now, gate_idx + 1);
                char ab[48];
                if (found) {
                    tracker.draw(frame, gate_idx, palette[gate_idx % 6]);
                    std::snprintf(ab, sizeof(ab), "G%d=%d°", gate_idx + 1,
                                  info.angle);
                } else {
                    std::snprintf(ab, sizeof(ab), "G%d=—", gate_idx + 1);
                }
                frame_angles.emplace_back(ab);
            }
            const auto td = Clock::now();

            // Angles go to the log once per second — never onto the frame.
            if (!frame_angles.empty() && now - angle_log_t0 >= 1.0) {
                std::string line = "Углы: ";
                for (size_t i = 0; i < frame_angles.size(); ++i) {
                    if (i) line += ", ";
                    line += frame_angles[i];
                }
                log_msg(line);
                angle_log_t0 = now;
            }

            if (serial && heartbeat_due(last_hb, now)) {
                if (serial->write(kHeartbeatLine)) last_hb = now;
            }

            if (frame.cols != stream_w || frame.rows != stream_h) {
                cv::resize(frame, stream_frame, cv::Size(stream_w, stream_h),
                           0, 0, cv::INTER_AREA);
            } else {
                stream_frame = frame;
            }
            streamer.publish(stream_frame);
            const auto te = Clock::now();

            t_dec += std::chrono::duration<double>(tb - ta).count();
            t_hsv += std::chrono::duration<double>(tc - tb).count();
            t_gates += std::chrono::duration<double>(td - tc).count();
            t_stream += std::chrono::duration<double>(te - td).count();
            ++fps_count;

            if (now - fps_window_t0 >= 5.0) {
                const double elapsed = now - fps_window_t0;
                const double rate = fps_count / elapsed;
                char buf[512];
                std::snprintf(
                    buf, sizeof(buf),
                    "FPS: обработка %.1f, захват %.1f, "
                    "видео %.1f; кадр %dx%d; "
                    "стадии: декод %.1f мс, hsv %.1f, ворота %.1f "
                    "(кластер %.1f), "
                    "видео %.1f; экспозиция=%d, gain=%d, brightness=%d, "
                    "gamma=%d",
                    fps_count / elapsed, capture.fps(), streamer.sent_fps(),
                    work_w, work_h, t_dec / fps_count * 1000.0,
                    t_hsv / fps_count * 1000.0, t_gates / fps_count * 1000.0,
                    tracker.cluster_time_s / fps_count * 1000.0,
                    t_stream / fps_count * 1000.0, args.exposure, args.gain,
                    args.brightness, args.gamma);
                log_msg(buf);
                const long long errs = capture.errors();
                if (errs > 0) {
                    log_msg("Ошибок чтения камеры: " + std::to_string(errs),
                            "WARN");
                }
                if (rate < 20.0 && !slow_hint_shown) {
                    slow_hint_shown = true;
                    log_msg(
                        "Низкий FPS. Проверьте: 1) governor = performance "
                        "(echo performance | sudo tee "
                        "/sys/devices/system/cpu/cpufreq/policy0/"
                        "scaling_governor); 2) --threads 1; 3) питание "
                        "(vcgencmd get_throttled).",
                        "WARN");
                }
                fps_count = 0;
                fps_window_t0 = now;
                t_dec = t_hsv = t_gates = t_stream = 0.0;
                tracker.cluster_time_s = 0.0;  // Idea 5: clustering stage
            }

            if (!any_gate && now - last_activation_log > 5.0) {
                log_msg(
                    "Ворота не активированы: Pi ждёт цвета от ноутбука — "
                    "в приложении выделите цвет рамкой R и нажмите SAVE "
                    "G1..G3");
                last_activation_log = now;
            }

            // saved_gates.json: coalesced atomic write (SD-card friendly).
            if (store.dirty() && now - last_store_save >= 2.0) {
                if (store.save(args.saved_gates_path)) {
                    store.clear_dirty();
                } else {
                    log_msg("Не удалось сохранить " + args.saved_gates_path,
                            "WARN");
                }
                last_store_save = now;
            }
        } catch (const std::exception& e) {
            // A single bad frame must never kill the match (PROMPT 2.3).
            ++hot_loop_errors;
            if (hot_loop_errors <= 3 || hot_loop_errors % 100 == 0) {
                log_msg(std::string("Ошибка кадра: ") + e.what(), "ERROR");
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
    }

    if (g_stop.load()) log_msg("Остановка по Ctrl+C");

    // ---- shutdown (Python finally block) ----
    streamer.stop();
    listener.stop();
    capture.stop();
    camera->release();
    restore_camera_controls(args.device);
    serial.reset();
    if (store.dirty()) {
        store.save(args.saved_gates_path);
        store.clear_dirty();
    }
    try {
        ctx.close();
    } catch (...) {
    }
    log_msg("Завершено");
    return 0;
}

}  // namespace
}  // namespace pi_core

int main(int argc, char** argv) {
    using namespace pi_core;
    std::signal(SIGINT, on_signal);
    std::signal(SIGTERM, on_signal);
    // A closed pipe (e.g. `... | head`) must not kill the process.
    std::signal(SIGPIPE, SIG_IGN);

    const Config args = parse_args(argc, argv);
    set_verbose(args.verbose);

    if (args.probe) return probe_camera(args);
    if (args.snapshot) return snapshot(args);
    return run(args);
}
