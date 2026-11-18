// pi_core/gate_tracker.cpp — see gate_tracker.hpp
// UNVERIFIED ON HW: inRange/morphology/CC behaviour compiles only on the
// Pi — verify stage timings (expect hsv ~1.1 ms, gates ~11.3 ms + clustering
// for 3 gates) with `pi_core --duration 12 --no-serial --gates 3`.
// Idea 8: the per-gate self-mask zero-out (cv::bitwise_and with the keep
// mask after inRange, before morphology) is covered by the pure unit tests
// (build_keep_mask + AND semantics in tests/test_logic.cpp) and by the
// Python pipeline parity in out/selfmask_check.py; the cv::Mat wiring here
// itself needs the Pi smoke (expect the "Self-маска (Idea 8)" log line and
// no gate detections while the camera faces the robot body).
#include "gate_tracker.hpp"
#include "hsv_color.hpp"
#include "log.hpp"

#include <opencv2/imgproc.hpp>

#include <chrono>
#include <cmath>
#include <cstdio>

namespace pi_core {

namespace {
// MORPH_KERNEL = np.ones((3,3), np.uint8) — the fixed CLOSE kernel.
cv::Mat morph_kernel3() {
    return cv::Mat::ones(3, 3, CV_8UC1);
}
}  // namespace

GateTracker::GateTracker(int width, int height, const Tolerance& tol,
                         double area_min, double area_scale, int gate_count,
                         const ClusterParams& cluster)
    : width_(width),
      height_(height),
      cx0_(width / 2),
      cy0_(height / 2),
      tol_(tol),
      area_min_(area_min),
      area_scale_(area_scale),
      gate_count_(gate_count < 1 ? 1 : gate_count),
      cluster_(cluster) {
    buf_.params.resize(gate_count_);
    buf_.masks.resize(gate_count_);
    buf_.tmp.create(height_, width_, CV_8UC1);
    buf_.labels.create(height_, width_, CV_32S);
    // Idea 5: OPEN kernel is configurable (odd, 1 = OPEN off); CLOSE stays 3x3.
    if (cluster_.open_kernel > 1) {
        buf_.open_kernel = cv::Mat::ones(cluster_.open_kernel,
                                         cluster_.open_kernel, CV_8UC1);
    }
    for (int i = 0; i < gate_count_; ++i) {
        buf_.masks[i].create(height_, width_, CV_8UC1);
        buf_.masks[i].setTo(0);
        // Hysteresis per gate: loss after N empty frames, confirmation of a
        // new object after M frames, continuity radius = max_stray_px (one
        // spatial scale of "same object", as in the Python tracker).
        stability_.emplace_back(cluster_.lost_frames, cluster_.confirm_frames,
                                cluster_.max_stray_px);
        // Idea 6: distance estimator per gate (guards + occlusion heuristics
        // + hold). Model installed later via set_distance_model() by main().
        dist_est_.emplace_back();
    }
    // All gates start disabled: the Pi tracks nothing until the laptop sends
    // colors (SAVE G1..G3 -> {"gates":[...]}) or saved_gates.json is loaded.
}

void GateTracker::set_distance_model(const DistanceModel& model) {
    for (auto& est : dist_est_) est.set_model(model);
}

void GateTracker::update_gate_from_rgb(int gate_idx, int r, int g, int b) {
    if (gate_idx < 0 || gate_idx >= gate_count_) return;
    // Clamp to valid ranges; out-of-range values would be rejected by
    // build_gate_params and the gate would silently never track.
    if (r < 0) r = 0; else if (r > 255) r = 255;
    if (g < 0) g = 0; else if (g > 255) g = 255;
    if (b < 0) b = 0; else if (b > 255) b = 255;

    // Laptop sends RGB, so this is COLOR_RGB2HSV (validated bit-exact).
    const Hsv px = rgb_to_hsv_8u(r, g, b);

    GateParams p;
    if (!build_gate_params(px.h, px.s, px.v, tol_, p)) {
        log_msg("Gate " + std::to_string(gate_idx + 1) +
                    ": RGB вне диапазона, ворота не обновлены",
                "WARN");
        return;
    }

    GateParams old;
    {
        std::lock_guard<std::mutex> lock(mu_);
        old = buf_.params[gate_idx];
        buf_.params[gate_idx] = p;
    }
    // Idea 5: a real color change resets the hysteresis (the previous gate
    // position is meaningless for the new color). Repeated reception of the
    // SAME color (~5/s from the laptop) must NOT reset it.
    bool changed = old.enabled != p.enabled || old.s_lo != p.s_lo ||
                   old.s_hi != p.s_hi || old.v_lo != p.v_lo ||
                   old.v_hi != p.v_hi ||
                   old.h_ranges.size() != p.h_ranges.size();
    if (!changed) {
        for (size_t i = 0; i < p.h_ranges.size(); ++i) {
            if (old.h_ranges[i].lo != p.h_ranges[i].lo ||
                old.h_ranges[i].hi != p.h_ranges[i].hi) {
                changed = true;
                break;
            }
        }
    }
    if (changed) {
        stability_[gate_idx].reset();
        // Idea 6: a new color invalidates the distance hold/reference too.
        dist_est_[gate_idx].reset();
    }

    // Same message as the Python debug() log (colors arrive 5/s — DEBUG only).
    char buf[160];
    std::snprintf(buf, sizeof(buf),
                  "GATE %d: RGB=%d,%d,%d -> HSV H=%d S=%d V=%d, "
                  "H-диапазоны=%zu",
                  gate_idx + 1, r, g, b, px.h, px.s, px.v, p.h_ranges.size());
    log_debug(buf);
}

bool GateTracker::is_enabled(int gate_idx) const {
    if (gate_idx < 0 || gate_idx >= gate_count_) return false;
    std::lock_guard<std::mutex> lock(mu_);
    return buf_.params[gate_idx].enabled;
}

// Idea 9: role is gate metadata — one locked scalar write; the hot path
// (process) copies params under the same mutex and ignores the field.
void GateTracker::set_gate_role(int gate_idx, GateRole role) {
    if (gate_idx < 0 || gate_idx >= gate_count_) return;
    std::lock_guard<std::mutex> lock(mu_);
    buf_.params[gate_idx].role = role;
}

// Idea 8: install the robot-body exclusion zone. The keep mask (255 = the
// pixel is searchable, 0 = robot body) is built ONCE per parameter change
// from the pure self_mask helper (bit-exact with the Python numpy build);
// per frame process() only does one cv::bitwise_and per gate.
void GateTracker::set_self_mask(const SelfMaskParams& p) {
    std::lock_guard<std::mutex> lock(mu_);
    self_mask_ = p;
    if (!p.enabled || !(p.r > 0.0)) {
        self_mask_ = SelfMaskParams();  // off — pre-Idea-8 behavior
        buf_.self_keep.release();
        buf_.self_keep_valid = false;
        return;
    }
    buf_.self_keep.create(height_, width_, CV_8UC1);
    // create() yields a continuous buffer — build straight into it.
    build_keep_mask(p, width_, height_, buf_.self_keep.data);
    buf_.self_keep_valid = true;
}

bool GateTracker::process(const cv::Mat& hsv, int gate_idx, GateResult& out) {
    if (gate_idx < 0 || gate_idx >= gate_count_) return false;

    // Copy the params under the lock; the mask computation itself works on
    // the copy so the listener thread can update colors mid-frame.
    GateParams p;
    {
        std::lock_guard<std::mutex> lock(mu_);
        p = buf_.params[gate_idx];
    }
    if (!p.enabled || p.h_ranges.empty()) return false;

    cv::Mat& mask = buf_.masks[gate_idx];
    cv::Mat& tmp = buf_.tmp;
    if (mask.size() != hsv.size() || mask.type() != CV_8UC1) {
        mask.create(hsv.size(), CV_8UC1);
    }
    if (tmp.size() != hsv.size() || tmp.type() != CV_8UC1) {
        tmp.create(hsv.size(), CV_8UC1);
    }

    // inRange per hue interval, OR the pieces together (wraparound).
    const cv::Scalar lo0(p.h_ranges[0].lo, p.s_lo, p.v_lo);
    const cv::Scalar hi0(p.h_ranges[0].hi, p.s_hi, p.v_hi);
    cv::inRange(hsv, lo0, hi0, mask);
    for (size_t i = 1; i < p.h_ranges.size(); ++i) {
        const cv::Scalar loi(p.h_ranges[i].lo, p.s_lo, p.v_lo);
        const cv::Scalar hii(p.h_ranges[i].hi, p.s_hi, p.v_hi);
        cv::inRange(hsv, loi, hii, tmp);
        cv::bitwise_or(mask, tmp, mask);
    }

    // Idea 8: the robot body is cut out of the search BEFORE the mask is
    // consumed (morphology/CC/moments) — no piece on the body (green PCB,
    // carbon tubes) can form a gate object. Zeroing HSV would NOT be sound:
    // a range with v_lo=0/s_lo=0/H-wrap-through-0 still matches black
    // pixels (see self_mask.hpp). One bitwise_and per gate (~0.05 ms at
    // 640x360 — measured in out/selfmask_check.py).
    {
        std::lock_guard<std::mutex> lock(mu_);
        if (buf_.self_keep_valid && buf_.self_keep.size() == mask.size()) {
            cv::bitwise_and(mask, buf_.self_keep, mask);
        }
    }

    // Morphology: OPEN (speckle, configurable kernel) + CLOSE (holes, 3x3).
    // Same order as the Python tracker.
    static const cv::Mat kernel3 = morph_kernel3();
    if (!buf_.open_kernel.empty()) {
        cv::morphologyEx(mask, tmp, cv::MORPH_OPEN, buf_.open_kernel);
        cv::morphologyEx(tmp, mask, cv::MORPH_CLOSE, kernel3);
    } else {
        cv::morphologyEx(mask, tmp, cv::MORPH_CLOSE, kernel3);
        tmp.copyTo(mask);
    }

    // ---- Idea 5: clustering + hysteresis (timed for the FPS log) ----
    const auto t_cl0 = std::chrono::steady_clock::now();
    // Quick reject by the total mask area (countNonZero — a plain counter,
    // NOT the forbidden findNonZero: no per-pixel allocation).
    const double min_area = cluster_.min_area_px;
    GateObject det;
    bool has_det = false;
    if (cv::countNonZero(mask) >= min_area) {
        const int n = cv::connectedComponentsWithStats(
            mask, buf_.labels, buf_.stats, buf_.centroids, 8, CV_32S);
        comps_.clear();
        for (int i = 1; i < n; ++i) {
            const double a =
                static_cast<double>(buf_.stats.at<int>(i, cv::CC_STAT_AREA));
            if (a < min_area) continue;
            GateComponent c;
            c.cx = buf_.centroids.at<double>(i, 0);
            c.cy = buf_.centroids.at<double>(i, 1);
            c.area = a;
            c.x = static_cast<double>(
                buf_.stats.at<int>(i, cv::CC_STAT_LEFT));
            c.y = static_cast<double>(
                buf_.stats.at<int>(i, cv::CC_STAT_TOP));
            c.w = static_cast<double>(
                buf_.stats.at<int>(i, cv::CC_STAT_WIDTH));
            c.h = static_cast<double>(
                buf_.stats.at<int>(i, cv::CC_STAT_HEIGHT));
            comps_.push_back(c);
        }
        // Anchor preference towards the published (held) position, then the
        // hysteresis decides what is published this frame.
        const bool hp = stability_[gate_idx].has_prev();
        det = build_gate_object(comps_, cluster_, hp,
                                stability_[gate_idx].prev_x(),
                                stability_[gate_idx].prev_y());
        has_det = det.valid;
    }

    GateObject pub;
    const bool published = stability_[gate_idx].update(
        has_det ? &det : nullptr, pub);
    const double cl_ms = std::chrono::duration<double>(
                             std::chrono::steady_clock::now() - t_cl0).count();
    cluster_time_s += cl_ms;

    if (!published) return false;

    out.cx = static_cast<int>(pub.cx);
    out.cy = static_cast<int>(pub.cy);
    out.angle = angle_from_center(pub.cx, pub.cy, cx0_, cy0_);
    out.area = pub.area * area_scale_;
    out.area_px = pub.area;
    out.radius = pub.radius;
    out.w_px = static_cast<int>(pub.w);
    out.held = stability_[gate_idx].last_held();

    // Idea 6: distance from the merged-cluster size (radius — the same
    // measure the calibrator fits). DistEstimator applies the guards
    // (division / 0..500 cm clamp / NaN / r-interval), the occlusion
    // heuristics (held frame, sudden radius drop on a stationary centroid,
    // low box fill) and the T-second hold of the last reliable value.
    // One formula eval per gate — microseconds.
    const double now_dist =
        std::chrono::duration<double>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();
    const DistEval ev =
        dist_est_[gate_idx].update(pub.radius, pub.w, pub.h, pub.area,
                                   pub.cx, pub.cy, out.held, now_dist,
                                   std::min(width_, height_), width_);
    out.dist_cm = ev.dist_cm;
    out.dist_reliable = ev.reliable;
    return true;
}

void GateTracker::draw(cv::Mat& frame, int gate_idx,
                       const cv::Vec3b& bgr) {
    if (gate_idx < 0 || gate_idx >= gate_count_) return;
    cv::Mat& mask = buf_.masks[gate_idx];
    if (mask.empty()) return;
    // frame[mask != 0] = color — per-pixel mask, nothing else on the frame
    // (setTo is the optimized OpenCV equivalent of the numpy assignment).
    frame.setTo(cv::Scalar(bgr[0], bgr[1], bgr[2]), mask);
}

}  // namespace pi_core
