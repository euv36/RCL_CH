// pi_core/config.hpp — CLI flags mirroring rpi_elp_cam.py (see
// README_CAMERA.md for the flag table) plus --print-serial.
#pragma once

#include <string>

namespace pi_core {

// Defaults = constants at the top of rpi_elp_cam.py.
struct Config {
    std::string device = "/dev/video0";
    int capture_w = 1920, capture_h = 1080;
    int track_w = 640, track_h = 360;
    int camera_fps = 30;
    bool stream_size_set = false;  // --stream-size given
    int stream_w = 0, stream_h = 0;  // 0 = same as track size
    int stream_fps = 26;
    int jpeg_quality = 55;
    bool stream_on_start = true;   // --no-stream clears it

    std::string laptop_ip = "10.192.226.77";  // DEFAULT_LAPTOP_IP; moves with Wi-Fi
    int video_port = 5555;
    int rgb_port = 5557;

    bool serial_enabled = true;    // --no-serial clears it
    std::string serial_port = "/dev/serial0";
    bool print_serial = false;     // --print-serial: serial lines -> stdout
                                   // (overrides --no-serial; smoke test
                                   // without an Arduino)

    int exposure = 300;            // UVC exposure_time_absolute; ~26 fps on 1080p
    int gain = 0;                  // brightness=64 washes colors out — don't
    int brightness = 0;
    int gamma = 100;               // 72..500; raise for shadows instead
    int saturation = 60;           // 0..128
    int contrast = 32;             // 0..64
    int power_line = 1;            // 0=off, 1=50 Hz, 2=60 Hz

    int tol_h = 40, tol_s = 100, tol_v = 100;  // DEFAULT_TOLERANCE (H, S, V)
    int gates = 2;                 // user runs with --gates 3 for 3 colors

    // Idea 5: blob clustering + loss hysteresis. 0 / <=0 = auto for the
    // width-scaled ones (2.5% / 10% of the track width), mirroring the
    // Python defaults in gate_cluster.py / rpi_elp_cam.py --help.
    double merge_gap = 0.0;        // --merge-gap PX (0 = 2.5% of width)
    double min_area = 0.0;         // --min-area PX, 1080p-equivalent (0 = 100)
    double max_stray = 0.0;        // --max-stray PX (0 = 10% of width)
    int lost_frames = 5;           // --lost-frames (empty frames before -1,0)
    int confirm_frames = 3;        // --confirm-frames (accept a new object)
    int open_kernel = 3;           // --open-kernel (odd, 1 = OPEN off)

    // Idea 8: self-mask (robot body exclusion, self_mask.hpp). PX in the
    // working frame; 0 / <=0 = auto (frame center, r = 0.22*min(w,h),
    // no annulus). The camera_settings.json "self_mask" section (fractions)
    // will override these once the C++ settings-file auto-apply lands
    // (Idea 1's known next step) — same order as the camera controls.
    double self_cx = 0.0;          // --self-cx PX (0 = frame center)
    double self_cy = 0.0;          // --self-cy PX (0 = frame center)
    double self_r = 0.0;           // --self-r PX (0 = 0.22*min(w,h))
    double self_r2 = 0.0;          // --self-r2 PX (0 = full disk)
    bool self_mask_on = true;      // --no-self-mask clears it

    // Idea 6: gate distance from gate size (d = a/(b-r) - c). The model is
    // loaded from distance_calib.json (written atomically by the
    // calibrator); missing/corrupt file -> the analytic fallback
    // Z = gate_width_cm * (frame_w / pi) / w_px (rules width, a parameter).
    // Occlusion -> dist_reliable=false + hold the last reliable value.
    bool dist_on = true;               // --no-dist clears it
    std::string dist_calib_path = "/home/twox/distance_calib.json";  // --dist-calib
    double dist_hold = 2.0;            // --dist-hold SEC (0 = no hold)
    double dist_drop = 0.6;            // --dist-drop FRAC (0 = heuristic off)
    double dist_fill = 0.25;           // --dist-fill FRAC (0 = heuristic off)
    double gate_width_cm = 45.0;       // --gate-width CM (rules: 45-60)

    int rotate = 0;                // 0/90/180/270
    int threads = 0;               // 0 = auto -> 1 (see hsv note in README)
    double duration = 0.0;         // seconds, 0 = run forever

    bool probe = false;
    bool snapshot = false;
    std::string snapshot_file;
    bool verbose = false;

    // Contract file locations (Idea 3). Override with --gates-file for tests.
    std::string saved_gates_path = "/home/twox/saved_gates.json";
};

// Parses argv; on a bad value prints usage to stderr and exits(2) — same
// behaviour class as argparse. Returns the config.
Config parse_args(int argc, char** argv);

// Usage text (kept in the header so main() can print it on request).
void print_usage();

}  // namespace pi_core
