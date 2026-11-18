// pi_core/distance.hpp — Idea 6: gate distance from gate size, pure logic.
//
// Model (PROMPT_EN.md §5 Idea 6, the empirical reference curve):
//     d = a / (b - r) - c        [cm, r in px]
// where r is the merged-cluster radius (union-box circumcircle, Idea 5
// gate_cluster::GateObject.radius — the same measure the calibrator fits).
// Constants are fitted from calibration points (robot at known distances)
// stored in distance_calib.json (atomic writes; the file lives next to the
// binary on the Pi, /home/twox/distance_calib.json).
//
// Fit without external linear algebra: the model linearizes exactly,
//     (d + c)(b - r) = a   =>   b*d + (c*b - a) - c*r = r*d,
// so [d, 1, -r] · (b, q, c) = r*d (q = c*b - a) is a 3-variable linear
// least-squares problem solved via normal equations + Gaussian elimination
// with partial pivoting, then polished by a few Gauss-Newton iterations on
// the ORIGINAL residual. Exact (noise-free) calibration points are
// recovered exactly.
//
// Garbage protection (a bug here sends the robot insane):
//   * division: |b - r| > kEpsDiv AND r inside the calibration r-interval
//     extended by 25% of the span (extrapolating across the asymptote is
//     garbage — rejected outright);
//   * d clamped to the working range 0..500 cm; outside the range the
//     value is reported unreliable (clamped value still returned);
//   * the model is validated at fit AND at load: finite a/b/c, strictly
//     decreasing d over r (a bigger gate means closer — slope sign is the
//     sign of a, so a < 0 is mandatory), asymptote b outside the
//     calibration r-span, no NaN anywhere on the interval. An invalid or
//     corrupt model falls back to the analytic formula
//     Z = gate_width_cm * (frame_w / pi) / w_px (the shape of the legacy
//     unikostyl.py estimate; goal width is a rules parameter, 45-60 cm).
//   * occlusion: when the opponent partially covers the gate the visible
//     size underestimates. Heuristics (per gate, see DistEstimator):
//     held frame (mask empty, hysteresis holds the position), sudden
//     radius drop with a stationary centroid, low box fill area/(w*h)
//     (pieces merged across the opponent's body). An unreliable value
//     HOLDS the last reliable one for T seconds instead of jumping.
//
// No OpenCV/ZMQ here: this file builds and unit-tests anywhere (it joins
// pi_core_tests). JSON via the vendored nlohmann header, file IO via
// atomic_file (tmp + fsync + rename). Python mirror: UniKostyl/distance.py.
#pragma once

#include <string>
#include <vector>

namespace pi_core {

// ---- constants shared with distance.py ----
constexpr double kDistMinCm = 0.0;      // working range
constexpr double kDistMaxCm = 500.0;
constexpr double kEpsDiv = 1e-6;        // division guard: |b - r| must exceed
constexpr double kExtrapFrac = 0.25;    // allowed r extrapolation, span fraction
constexpr double kExtrapMinPx = 10.0;
constexpr int kDistMinPoints = 3;       // fit needs >= 3 points (4-6 advised)
constexpr double kDefaultHoldS = 2.0;
constexpr double kDefaultDropFrac = 0.6;
constexpr double kDefaultFillMin = 0.25;
constexpr double kDefaultMoveFrac = 0.08;
constexpr double kDefaultGateWidthCm = 45.0;  // rules: goal inner width 45-60

// ---- model ----
struct DistanceModel {
    bool valid = false;
    std::string error;                    // rejection reason (empty when valid)
    double a = 0.0, b = 0.0, c = 0.0;
    int frame_w = 0, frame_h = 0;         // calibration frame size (0 = unknown)
    double r_lo = 0.0, r_hi = 0.0;        // calibration r span (0 when no points)
    double r_eval_lo = 0.0, r_eval_hi = 0.0;
    bool has_interval = false;            // true when points define the span
    std::vector<double> points;           // [d0, r0, d1, r1, ...] (validated)
};

// Evaluates the guards and fills .valid / .error / .r_eval_* on the model.
void validate_distance_model(DistanceModel& m);

// Least-squares fit of (a, b, c) from points [(dist_cm, r_px), ...].
// Returns false (with .error set) on degenerate/garbage input or when the
// fitted model fails validation. `frame_w/frame_h` are recorded verbatim.
bool fit_distance_model(const std::vector<double>& d_cm,
                        const std::vector<double>& r_px, int frame_w,
                        int frame_h, DistanceModel& out);

// Analytic fallback (no calibration): Z = W * (frame_w / pi) / w_px — the
// same shape as the legacy unikostyl.py estimate (FOCAL_DIVISOR = pi) but
// with the GATE width from the rules. Returns -1.0 on junk input.
double analytic_distance_cm(double w_px, double frame_w, double gate_width_cm);

// JSON (contract schema): {"version":1,"model":"a/(b-r)-c","a":..,"b":..,
// "c":..,"points":[[dist_cm,r_px],...],"frame_w":..,"frame_h":..,"ts":".."}.
// Parse never throws: corrupt/foreign content -> valid=false.
DistanceModel distance_model_from_json(const std::string& text);
DistanceModel load_distance_model(const std::string& path);   // missing -> invalid
std::string distance_model_to_json(const DistanceModel& m, const std::string& ts);
bool save_distance_model(const std::string& path, const DistanceModel& m,
                         const std::string& ts);              // atomic write

// ---- evaluation ----
struct DistEval {
    double dist_cm;   // NaN = nothing to report (held window expired, junk...)
    bool reliable;    // false = occluded/clamped/suspicious — consumer decides
};

// One evaluation of the model (or nothing) for a radius measured on a frame
// whose min dimension is frame_min_dim. The calibration frame scale is
// absorbed: r_calib = r_px * min_dim(calib) / min_dim(frame). Never throws,
// never returns inf (NaN on any doubt).
DistEval eval_distance(const DistanceModel& m, double r_px, int frame_min_dim);

// ---- occlusion heuristics + hold (per gate state, mirrors GateDistEstimator) ----
class DistEstimator {
public:
    DistEstimator(double hold_s = kDefaultHoldS,
                  double drop_frac = kDefaultDropFrac,
                  double fill_min = kDefaultFillMin,
                  double move_frac = kDefaultMoveFrac,
                  double gate_width_cm = kDefaultGateWidthCm);

    void set_model(const DistanceModel& m);   // resets the state
    void reset();                             // new color / restart

    // One frame for one gate. radius/w/h/area of the published (post-
    // hysteresis) object in px; held = the hysteresis kept the last accepted
    // object (mask empty this frame). now_s = monotonic seconds.
    // frame_min_dim / frame_w are of the WORKING frame. Never throws.
    DistEval update(double radius, double w_px, double h_px, double area_px,
                    double cx, double cy, bool held, double now_s,
                    int frame_min_dim, int frame_w);

private:
    DistanceModel model_;
    bool has_model_ = false;
    double hold_s_;
    double drop_frac_;
    double fill_min_;
    double move_frac_;
    double gate_w_;
    // hold state
    double hold_val_ = 0.0;
    double hold_t_ = 0.0;
    bool has_hold_ = false;
    // last RELIABLE reference (drop heuristic + continuity)
    double ref_radius_ = 0.0;
    double ref_cx_ = 0.0, ref_cy_ = 0.0;
    bool has_ref_ = false;
};

}  // namespace pi_core
