// pi_core/gate_cluster.hpp — Idea 5: blob clustering + loss hysteresis.
// PURE logic (no OpenCV/ZMQ): a 1-in-1 mirror of UniKostyl/gate_cluster.py so
// Python and C++ can be parity-checked on identical synthetic inputs
// (pi_core_tests --cluster-parity FILE <-> out/parity_check5.py).
//
// Pipeline per frame and color (the caller feeds CC components):
//   min-area filter (caller) -> merge components with a box gap
//   <= merge_gap_px (the opponent's body splitting the gate) -> reject
//   merged clusters shaped like tape/lamp posts -> anchor
//   (nearest to the previous position while tracking, largest otherwise)
//   -> discard clusters farther than max_stray_px from the anchor (t-shirt)
//   -> ONE gate object: exact area-weighted centroid + union box + radius.
// Radius = circumcircle of the union box (0.5*sqrt(w^2+h^2)) — an upper
// bound of the pixel-level minEnclosingCircle (exact for compact blobs).
// Chosen deliberately: deterministic, cheap, and BIT-EXACT between Python
// and C++ (sqrt of exact squares; sqrt is correctly rounded per IEEE-754,
// hypot() is not guaranteed identical across libms). Idea 6's distance
// calibration fits its constants to whatever consistent radius this is.
//
// Hysteresis (GateStability) — same semantics as the Python class:
//   - a gate is declared lost only after N consecutive empty frames
//     (lost_frames) and then reported lost ONCE (serial G<id>,-1,0 format
//     unchanged, only the trigger moment moved);
//   - a new detection is accepted after M consecutive frames
//     (confirm_frames);
//   - while tracking, the cluster nearest the previous position wins;
//     a far new object takes over only after M stable frames (no
//     intermediate "lost" blip if it confirms before N empty frames);
//   - during the grace window (< N empty frames) the last accepted
//     position is published with the held flag;
//   - the very first detection of a fresh tracker is accepted immediately
//     (legacy behavior; nothing to protect yet).
#pragma once

#include <vector>

namespace pi_core {

constexpr int kMaxClusterCandidates = 256;  // safety valve, same as Python
constexpr double kMaxClusterAspect = 6.5;      // reject tape/lamp-like clusters
constexpr double kMinClusterSideFrac = 0.008;  // reject ultra-thin clusters

struct ClusterParams {
    double merge_gap_px = 16.0;  // 2.5% of a 640-wide frame
    double min_area_px = 0.0;    // 0 = filter off (Pi passes scaled area_min)
    double max_stray_px = 64.0;  // 10% of 640; also the track-continuity radius
    int open_kernel = 3;         // odd, 1 = OPEN off
    int lost_frames = 5;
    int confirm_frames = 3;
    double max_aspect = kMaxClusterAspect;  // max(w,h)/min(w,h)
    double min_cluster_side_px = 0.0;       // auto = rint(kMinClusterSideFrac*min(w,h))
};

// Python gate_cluster.resolve_params(): values <= 0 mean "auto" for the
// width-scaled defaults (2.5% / 10% of width), open_kernel forced odd >= 1.
ClusterParams resolve_cluster_params(int width, int height, double merge_gap_px,
                                     double min_area_px, double max_stray_px,
                                     int open_kernel, int lost_frames,
                                     int confirm_frames,
                                     double max_aspect = 0.0,
                                     double min_cluster_side_px = 0.0);

struct GateComponent {
    double cx = 0.0, cy = 0.0;        // centroid (CC centroids)
    double area = 0.0;                // pixel area (CC stats CC_STAT_AREA)
    double x = 0.0, y = 0.0;          // bounding box (CC stats LEFT/TOP)
    double w = 0.0, h = 0.0;          // box size (CC stats WIDTH/HEIGHT)
};

struct GateObject {
    bool valid = false;
    double cx = 0.0, cy = 0.0;  // merged centroid (exact area-weighted mean)
    double area = 0.0;          // merged pixel area
    double radius = 0.0;        // union-box circumcircle (see header comment)
    double x = 0.0, y = 0.0, w = 0.0, h = 0.0;  // union box
    std::vector<int> members;   // indices of member components (diagnostics)
    int n_components = 0;       // components inside the published object
};

// Euclidean gap between two axis-aligned rects (0 when overlapping).
double rect_gap(double x1, double y1, double w1, double h1, double x2,
                double y2, double w2, double h2);

// int(math.degrees(math.atan2(cy0 - cy, cx - cx0)) % 360), float centroid.
// 0 deg = right, CCW; result 0..359. Mirrors gate_logic::gate_angle but for
// sub-pixel centroids (Idea 5 works on float cluster centroids).
int angle_from_center(double cx, double cy, int cx0, int cy0);

// ONE gate object from already min-area-filtered CC components (pure).
// has_prev/prev_x/prev_y: the published position of the previous frame.
GateObject build_gate_object(const std::vector<GateComponent>& comps,
                             const ClusterParams& p, bool has_prev,
                             double prev_x, double prev_y);

class GateStability {
public:
    GateStability(int lost_frames, int confirm_frames, double jump_px);

    // One frame. det == nullptr means "nothing this frame" (also the state
    // after a min-area quick reject). Returns true when a gate object should
    // be published; `published` receives the object (held copy during grace).
    bool update(const GateObject* det, GateObject& published);

    // True exactly on the frame the loss fires (the consumer sends the
    // single G<id>,-1,0 line; serial format unchanged).
    bool lost_event() const { return lost_event_; }

    // True when the published object is held from an earlier frame (grace),
    // i.e. the mask was empty/far this frame but the loss is not declared.
    bool last_held() const { return last_held_; }

    // Previous position for the anchor preference (Python held_xy()).
    bool has_prev() const { return found_ && has_held_; }
    double prev_x() const { return held_.cx; }
    double prev_y() const { return held_.cy; }

    void reset();

private:
    static double dist(const GateObject& a, const GateObject& b);
    void accept(const GateObject& det);

    int lost_frames_;
    int confirm_frames_;
    double jump_px_;

    bool found_ = false;       // published state
    bool ever_found_ = false;  // some detection was accepted at least once
    int empty_count_ = 0;      // consecutive frames without the held object
    bool has_held_ = false;
    GateObject held_;          // last accepted detection
    bool has_pending_ = false;
    int pending_count_ = 0;    // consecutive frames the candidate is seen
    GateObject pending_;
    bool lost_event_ = false;
    bool last_held_ = false;
};

}  // namespace pi_core
