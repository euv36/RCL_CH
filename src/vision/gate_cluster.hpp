#pragma once

#include <optional>
#include <vector>

struct GateRect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
};

struct GateComponent {
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
    int area = 0;
    float cx = 0.0f;
    float cy = 0.0f;
};

struct GateClusterParams {
    float merge_gap_frac = 0.025f;
    float max_stray_frac = 0.10f;
    int open_kernel = 3;
    int lost_frames = 5;
    int confirm_frames = 3;
    int max_candidates = 256;
    float max_aspect = 6.5f;
    float min_cluster_side_frac = 0.008f;

    // Absolute values normally filled by resolve_cluster_params(). A zero value
    // asks build_gate_object() to derive the fallback for a 1280x720 frame.
    float merge_gap_px = 0.0f;
    float max_stray_px = 0.0f;
    int min_cluster_side_px = 0;
};

struct GateObject {
    bool found = false;
    float cx = 0.0f;
    float cy = 0.0f;
    float radius = 0.0f;
    float area = 0.0f;
    GateRect bbox{};
    int members = 0;
};

struct GateStabilityResult {
    bool found = false;
    bool held = false;
    float cx = 0.0f;
    float cy = 0.0f;
    float radius = 0.0f;
    float area = 0.0f;
    int members = 0;
    bool lost_event = false;
};

GateClusterParams resolve_cluster_params(int width, int height,
                                          float merge_gap_px = 0.0f,
                                          float max_stray_px = 0.0f,
                                          int open_kernel = 0,
                                          int lost_frames = 0,
                                          int confirm_frames = 0,
                                          float max_aspect = 0.0f,
                                          int min_cluster_side_px = 0);

double gate_rect_gap(float x1, float y1, float w1, float h1,
                     float x2, float y2, float w2, float h2);
GateObject build_gate_object(const std::vector<GateComponent>& components,
                              const GateClusterParams& params,
                              std::optional<float> previous_x,
                              std::optional<float> previous_y);

class GateStability {
public:
    GateStability(int confirm_frames = 3, int lost_frames = 5,
                  float max_jump_px = 128.0f);
    GateStabilityResult update(bool detected, float cx, float cy, float radius,
                               float area, int members);
    void reset();

private:
    struct SimpleObject {
        float cx, cy, radius, area;
        int members;
    };
    static double distance(const SimpleObject& a, const SimpleObject& b);
    void accept(const SimpleObject& value);
    GateStabilityResult held_result(bool held) const;

    int confirm_frames_ = 3;
    int lost_frames_ = 5;
    float max_jump_px_ = 128.0f;

    bool track_established_ = false;
    bool ever_found_ = false;
    int empty_count_ = 0;
    bool has_held_ = false;
    SimpleObject held_{};
    bool has_pending_ = false;
    int pending_count_ = 0;
    SimpleObject pending_{};
};
