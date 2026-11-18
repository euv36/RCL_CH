// pi_core/gate_tracker.hpp — HSV inRange + morphology + blob clustering
// (Idea 5) per gate, the C++ mirror of GateTracker in rpi_elp_cam.py.
//
// Idea 5 pipeline in process(): inRange -> OPEN (configurable kernel) ->
// CLOSE -> connectedComponentsWithStats -> gate_cluster::build_gate_object
// (merge close pieces / discard strays, pure functions) ->
// gate_cluster::GateStability (loss after N empty frames, confirmation
// after M). The published result is POST-hysteresis: during the grace
// window the last accepted position is returned with held=true; a loss is
// only reported after N empty frames (serial G<id>,-1,0 then goes out once
// via gate_logic::decide_serial, format unchanged).
//
// Still true:
//  - distance (Idea 6) consumes GateResult {radius, w_px, area_px} — the
//    Idea 5 fields are already in place;
//  - self-mask (Idea 8): set_self_mask() installs the robot-body exclusion
//    zone (built ONCE from the pure self_mask.hpp helper) and process()
//    zeroes it in the per-gate mask right after inRange, BEFORE
//    morphology/CC — no piece on the body (green PCB, carbon tubes) can
//    form a gate object. Zeroing HSV would NOT be sound (a range with
//    v_lo=0 / s_lo=0 / H-wrap through 0 still matches black pixels —
//    see self_mask.hpp).
#pragma once

#include "distance.hpp"
#include "gate_cluster.hpp"
#include "gate_logic.hpp"
#include "self_mask.hpp"

#include <opencv2/core.hpp>

#include <mutex>
#include <vector>

namespace pi_core {

struct GateResult {
    int cx = -1;
    int cy = -1;
    int angle = -1;
    double area = 0.0;      // 1080p-equivalent area (area_px * area_scale)
    double area_px = 0.0;   // raw mask pixels of the merged cluster
    double radius = 0.0;    // Idea 5: merged cluster radius (union-box
                            // circumcircle; see gate_cluster.hpp)
    int w_px = 0;           // Idea 5: merged cluster width
    bool held = false;      // Idea 5: value held by hysteresis (the mask was
                            // empty this frame, loss not yet declared)
    // Idea 6: distance in cm from the merged-cluster size (calibrated model
    // d = a/(b-r) - c from distance_calib.json, else the analytic fallback
    // Z = gate_width_cm * (frame_w / pi) / w_px). NaN = nothing to report
    // (occlusion hold expired / junk radius); when the occlusion hold is
    // active the LAST reliable value is reported with reliable=false —
    // additive fields for the Idea 2 status JSON ("dist_cm"/"dist_reliable",
    // mirror rpi_elp_cam.build_gate_status), hot serial path ignores them.
    double dist_cm = std::nan("");
    bool dist_reliable = false;
};

class GateTracker {
public:
    // width/height = working frame (after rotate), same as Python GateTracker.
    // cluster params come pre-resolved (resolve_cluster_params) — main.cpp
    // mirrors the Python run() scaling of --min-area.
    GateTracker(int width, int height, const Tolerance& tol, double area_min,
                double area_scale, int gate_count,
                const ClusterParams& cluster = ClusterParams());

    int gate_count() const { return gate_count_; }

    // Idea 8: install the robot-body exclusion zone (self_mask.hpp pure
    // helper; keep mask built once into buf_). disabled/invalid params or
    // default SelfMaskParams() turn the mask off (pre-Idea-8 behavior).
    void set_self_mask(const SelfMaskParams& p);

    // Idea 8: the applied circle in px (for the Idea 2 status JSON when the
    // multipart format is repeated here — mirror rpi_elp_cam.build_gate_status:
    // top-level "self_mask": {"cx","cy","r","r2"} only when enabled).
    SelfMaskParams self_mask() const {
        std::lock_guard<std::mutex> lock(mu_);
        return self_mask_;
    }

    // From the ZMQ color-listener thread (also used by saved_gates load).
    // Mirrors update_gate_from_rgb(): RGB -> HSV (validated pure function),
    // tolerance (H,S,V), hue wraparound into 1..2 ranges. A real COLOR
    // CHANGE resets the gate's hysteresis (repeated identical colors at
    // ~5/s must not); the Python side does the same.
    void update_gate_from_rgb(int gate_idx, int r, int g, int b);

    // Idea 9: per-gate role ("ours"/"opponent", Unset = none) from the color
    // message / saved_gates.json. Metadata only — the hot path ignores it;
    // the status JSON (Idea 2 multipart) reports it when set. Note: a later
    // update_gate_from_rgb() resets the role to Unset, exactly like the
    // Python params-dict replacement (the laptop re-sends role with colors).
    void set_gate_role(int gate_idx, GateRole role);

    // Idea 6: distance model (loaded from distance_calib.json by main.cpp;
    // invalid/absent -> analytic fallback Z = gate_width * (w/pi) / w_px).
    // Per-gate DistEstimator state (occlusion hold) lives here, like the
    // GateStability hysteresis; a real color change resets it.
    void set_distance_model(const DistanceModel& model);

    // Detects one gate on a precomputed BGR->HSV image. Returns false when
    // the gate is disabled or the hysteresis publishes "lost" (== Python
    // None; during the grace window it returns true with held=true).
    bool process(const cv::Mat& hsv, int gate_idx, GateResult& out);

    // Per-pixel mask painting on the stream frame (project convention:
    // mask overlay only, no bbox, no text).
    void draw(cv::Mat& frame, int gate_idx, const cv::Vec3b& bgr);

    bool is_enabled(int gate_idx) const;

    // Idea 5: accumulated clustering stage time (seconds), for the FPS log
    // "(кластер X)" — main.cpp resets it with the other stage timers.
    double cluster_time_s = 0.0;

private:
    struct Buffers {
        std::vector<GateParams> params;
        std::vector<cv::Mat> masks;   // one per gate
        cv::Mat tmp;
        bool tmp_valid = false;       // tmp allocated for current size
        // Idea 5: connectedComponentsWithStats outputs (reused, no
        // per-frame allocation of the labels buffer).
        cv::Mat labels;
        cv::Mat stats;
        cv::Mat centroids;
        cv::Mat open_kernel;          // ones(open_kernel x open_kernel)
        cv::Mat self_keep;            // Idea 8: 255=search / 0=robot body
                                      // (built once per params change)
        bool self_keep_valid = false;
    };

    int width_;
    int height_;
    int cx0_;
    int cy0_;
    Tolerance tol_;
    double area_min_;
    double area_scale_;
    int gate_count_;
    ClusterParams cluster_;
    std::vector<GateStability> stability_;
    std::vector<GateComponent> comps_;  // reused per process() call
    std::vector<DistEstimator> dist_est_;  // Idea 6: one per gate

    mutable std::mutex mu_;  // guards buf_.params / self_mask_ (listener thread)
    SelfMaskParams self_mask_;
    Buffers buf_;
};

}  // namespace pi_core
