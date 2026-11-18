// pi_core/gate_cluster.cpp — see gate_cluster.hpp.
// Every accumulation order, rounding and comparison below mirrors
// UniKostyl/gate_cluster.py so the parity vectors agree bit-for-bit:
//   - merge decisions compare SQUARED distances ((dx*dx+dy*dy) <= mg*mg),
//     exactly like the Python numpy pair matrix;
//   - stray/stability distances use std::sqrt like Python math.sqrt;
//   - cluster/final accumulation runs in member-index / kept-cluster order;
//   - anchor tie-breaks: distance, then area desc, then index asc.
#include "gate_cluster.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <utility>

namespace pi_core {

namespace {
constexpr double kInf = std::numeric_limits<double>::infinity();
}

ClusterParams resolve_cluster_params(int width, int height, double merge_gap_px,
                                     double min_area_px, double max_stray_px,
                                     int open_kernel, int lost_frames,
                                     int confirm_frames,
                                     double max_aspect,
                                     double min_cluster_side_px) {
    const int w = width > 1 ? width : 1;
    const int h = height > 1 ? height : 1;
    ClusterParams p;
    // Python round() and std::rint() both round half-to-even by default.
    p.merge_gap_px =
        merge_gap_px > 0.0 ? merge_gap_px : std::rint(0.025 * w);
    p.max_stray_px =
        max_stray_px > 0.0 ? max_stray_px : std::rint(0.10 * w);
    p.min_area_px = min_area_px > 0.0 ? min_area_px : 0.0;
    int k = open_kernel;
    if (k < 1) k = 1;
    if (k % 2 == 0) k += 1;
    p.open_kernel = k;
    p.lost_frames = lost_frames > 1 ? lost_frames : 1;
    p.confirm_frames = confirm_frames > 1 ? confirm_frames : 1;
    p.max_aspect =
        max_aspect > 0.0 ? max_aspect : kMaxClusterAspect;
    p.min_cluster_side_px =
        min_cluster_side_px > 0.0
            ? min_cluster_side_px
            : std::rint(kMinClusterSideFrac *
                        static_cast<double>(std::min(w, h)));
    return p;
}

double rect_gap(double x1, double y1, double w1, double h1, double x2,
                double y2, double w2, double h2) {
    const double dx =
        std::max(0.0, std::max(x1, x2) - std::min(x1 + w1, x2 + w2));
    const double dy =
        std::max(0.0, std::max(y1, y2) - std::min(y1 + h1, y2 + h2));
    return std::sqrt(dx * dx + dy * dy);
}

int angle_from_center(double cx, double cy, int cx0, int cy0) {
    // Same operation order as gate_logic::gate_angle / Python:
    // degrees = atan2 * (180/pi), fmod, sign fix, truncation.
    const double rad2deg = 180.0 / 3.14159265358979323846;
    const double deg = std::atan2(static_cast<double>(cy0) - cy,
                                  cx - static_cast<double>(cx0)) * rad2deg;
    double m = std::fmod(deg, 360.0);
    if (m < 0.0) m += 360.0;
    if (m >= 360.0) m -= 360.0;  // paranoia, unreachable after the fix
    return static_cast<int>(m);
}

GateObject build_gate_object(const std::vector<GateComponent>& comps,
                             const ClusterParams& p, bool has_prev,
                             double prev_x, double prev_y) {
    const int n = static_cast<int>(comps.size());
    if (n == 0) return GateObject{};

    // 1. Candidate cap: largest MAX_CANDIDATES by (area desc, index asc),
    //    then back to index-ascending order (deterministic, same as Python).
    std::vector<int> keep;
    keep.reserve(n > kMaxClusterCandidates ? kMaxClusterCandidates : n);
    for (int i = 0; i < n; ++i) keep.push_back(i);
    if (n > kMaxClusterCandidates) {
        std::vector<int> order(keep);
        std::stable_sort(order.begin(), order.end(),
                         [&comps](int a, int b) {
                             if (comps[a].area != comps[b].area)
                                 return comps[a].area > comps[b].area;
                             return a < b;
                         });
        order.resize(kMaxClusterCandidates);
        std::sort(order.begin(), order.end());
        keep.swap(order);
    }
    const int m = static_cast<int>(keep.size());

    // 2. Union-find over pairs with box gap <= merge_gap_px (SQUARED compare,
    //    bit-exact with the Python numpy pair matrix).
    std::vector<int> parent(m);
    for (int i = 0; i < m; ++i) parent[i] = i;
    const auto find = [&parent](int a) {
        while (parent[a] != a) {
            parent[a] = parent[parent[a]];
            a = parent[a];
        }
        return a;
    };
    const double mg = p.merge_gap_px;
    for (int a = 0; a < m; ++a) {
        const GateComponent& ca = comps[keep[a]];
        for (int b = a + 1; b < m; ++b) {
            const GateComponent& cb = comps[keep[b]];
            const double dx = std::max(
                0.0, std::max(ca.x, cb.x) - std::min(ca.x + ca.w, cb.x + cb.w));
            const double dy = std::max(
                0.0, std::max(ca.y, cb.y) - std::min(ca.y + ca.h, cb.y + cb.h));
            if (dx * dx + dy * dy <= mg * mg) {
                const int ra = find(a);
                const int rb = find(b);
                if (ra != rb) parent[std::max(ra, rb)] = std::min(ra, rb);
            }
        }
    }

    // 3. Cluster aggregates in member order; clusters ordered by their
    //    smallest member index (independent of union-find root choice).
    std::map<int, std::vector<int>> groups;
    for (int j = 0; j < m; ++j) groups[find(j)].push_back(j);
    std::vector<std::vector<int>> cluster_lists;
    cluster_lists.reserve(groups.size());
    for (auto& kv : groups) cluster_lists.push_back(kv.second);
    std::sort(cluster_lists.begin(), cluster_lists.end(),
              [](const std::vector<int>& a, const std::vector<int>& b) {
                  return a.front() < b.front();
              });

    struct Agg {
        double cx, cy, area, x, y, w, h;
        std::vector<int> members;  // global component indices
    };
    std::vector<Agg> aggs;
    aggs.reserve(cluster_lists.size());
    for (const auto& local_members : cluster_lists) {
        Agg agg;
        agg.area = 0.0;
        agg.cx = 0.0;
        agg.cy = 0.0;
        agg.x = kInf;
        agg.y = kInf;
        double x1 = -kInf, y1 = -kInf;
        for (int j : local_members) {
            const GateComponent& c = comps[keep[j]];
            agg.area += c.area;
            agg.cx += c.area * c.cx;
            agg.cy += c.area * c.cy;
            if (c.x < agg.x) agg.x = c.x;
            if (c.y < agg.y) agg.y = c.y;
            if (c.x + c.w > x1) x1 = c.x + c.w;
            if (c.y + c.h > y1) y1 = c.y + c.h;
            agg.members.push_back(keep[j]);
        }
        agg.cx /= agg.area;
        agg.cy /= agg.area;
        agg.w = x1 - agg.x;
        agg.h = y1 - agg.y;
        aggs.push_back(std::move(agg));
    }

    // 3b. Shape guard on merged clusters: thin tape/lamp posts are valid
    // colour blobs but not gate objects. Applied AFTER merge so split pieces
    // of one real goal can still pass.
    aggs.erase(std::remove_if(aggs.begin(), aggs.end(),
                              [&p](const Agg& agg) {
                                  const double small =
                                      std::min(agg.w, agg.h);
                                  if (small <= 0.0 ||
                                      small < p.min_cluster_side_px) {
                                      return true;
                                  }
                                  return std::max(agg.w, agg.h) / small >
                                         p.max_aspect;
                              }),
               aggs.end());
    if (aggs.empty()) return GateObject{};

    // 4. Anchor: nearest to the previous position while tracking (ties:
    //    larger area, then lower index); largest cluster otherwise.
    int best = 0;
    if (has_prev) {
        double best_d2 = kInf;
        for (int k = 0; k < static_cast<int>(aggs.size()); ++k) {
            const double d2 = (aggs[k].cx - prev_x) * (aggs[k].cx - prev_x) +
                              (aggs[k].cy - prev_y) * (aggs[k].cy - prev_y);
            if (d2 < best_d2 ||
                (d2 == best_d2 &&
                 (aggs[k].area > aggs[best].area ||
                  (aggs[k].area == aggs[best].area && k < best)))) {
                best_d2 = d2;
                best = k;
            }
        }
    } else {
        for (int k = 1; k < static_cast<int>(aggs.size()); ++k) {
            if (aggs[k].area > aggs[best].area) best = k;
        }
    }

    // 5. Stray rejection: clusters farther than max_stray_px from the anchor
    //    centroid are foreign objects (sqrt form, like Python math.sqrt).
    const double stray = p.max_stray_px;
    std::vector<int> kept;  // indices into aggs, anchor first
    kept.push_back(best);
    for (int k = 0; k < static_cast<int>(aggs.size()); ++k) {
        if (k == best) continue;
        const double d = std::sqrt(
            (aggs[k].cx - aggs[best].cx) * (aggs[k].cx - aggs[best].cx) +
            (aggs[k].cy - aggs[best].cy) * (aggs[k].cy - aggs[best].cy));
        if (d <= stray) kept.push_back(k);
    }

    // 6. Final object: accumulate over kept clusters in kept order (anchor
    //    first — same float accumulation order as Python).
    GateObject out;
    out.valid = true;
    double area = 0.0, sx = 0.0, sy = 0.0;
    double x0 = kInf, y0 = kInf, x1 = -kInf, y1 = -kInf;
    for (int k : kept) {
        const Agg& a = aggs[k];
        area += a.area;
        sx += a.area * a.cx;
        sy += a.area * a.cy;
        if (a.x < x0) x0 = a.x;
        if (a.y < y0) y0 = a.y;
        if (a.x + a.w > x1) x1 = a.x + a.w;
        if (a.y + a.h > y1) y1 = a.y + a.h;
        out.members.insert(out.members.end(), a.members.begin(),
                           a.members.end());
        out.n_components += static_cast<int>(a.members.size());
    }
    out.area = area;
    out.cx = sx / area;
    out.cy = sy / area;
    out.x = x0;
    out.y = y0;
    out.w = x1 - x0;
    out.h = y1 - y0;
    out.radius = 0.5 * std::sqrt(out.w * out.w + out.h * out.h);
    return out;
}

// ---------------- GateStability ----------------

GateStability::GateStability(int lost_frames, int confirm_frames,
                             double jump_px)
    : lost_frames_(lost_frames > 1 ? lost_frames : 1),
      confirm_frames_(confirm_frames > 1 ? confirm_frames : 1),
      jump_px_(jump_px) {}

double GateStability::dist(const GateObject& a, const GateObject& b) {
    return std::sqrt((a.cx - b.cx) * (a.cx - b.cx) +
                     (a.cy - b.cy) * (a.cy - b.cy));
}

void GateStability::accept(const GateObject& det) {
    held_ = det;
    has_held_ = true;
    found_ = true;
    ever_found_ = true;
    empty_count_ = 0;
    has_pending_ = false;
    pending_count_ = 0;
}

void GateStability::reset() {
    found_ = false;
    ever_found_ = false;
    empty_count_ = 0;
    has_held_ = false;
    held_ = GateObject{};
    has_pending_ = false;
    pending_count_ = 0;
    pending_ = GateObject{};
    lost_event_ = false;
    last_held_ = false;
}

bool GateStability::update(const GateObject* det, GateObject& published) {
    lost_event_ = false;
    last_held_ = false;

    if (det == nullptr) {
        has_pending_ = false;
        pending_count_ = 0;
        if (found_) {
            ++empty_count_;
            if (empty_count_ >= lost_frames_) {
                // Loss declared: publish nothing; the consumer emits the
                // single G<id>,-1,0 line (format unchanged).
                found_ = false;
                lost_event_ = true;
                return false;
            }
            // Grace: keep reporting the last accepted position.
            published = held_;
            last_held_ = true;
            return true;
        }
        return false;
    }

    if (found_ && has_held_ && dist(*det, held_) <= jump_px_) {
        // Same object (track continuity) — accept immediately.
        accept(*det);
        published = held_;
        return true;
    }

    // New/far candidate: accumulate M consecutive frames near ITS position
    // (the candidate may drift — compare with its latest position).
    if (has_pending_ && dist(*det, pending_) <= jump_px_) {
        ++pending_count_;
        pending_ = *det;
    } else {
        pending_ = *det;
        has_pending_ = true;
        pending_count_ = 1;
    }

    if (!found_) {
        if (!ever_found_) {
            // Fresh tracker: accept immediately (legacy behavior).
            accept(*det);
            published = held_;
            return true;
        }
        if (pending_count_ >= confirm_frames_) {
            accept(*det);
            published = held_;
            return true;
        }
        return false;
    }

    // Found, but this frame's object is far from the held position: the
    // held object was not seen — accumulate emptiness (grace/loss) while
    // the candidate waits.
    ++empty_count_;
    if (pending_count_ >= confirm_frames_) {
        // Stable replacement: switch WITHOUT an intermediate loss blip.
        accept(*det);
        published = held_;
        return true;
    }
    if (empty_count_ >= lost_frames_) {
        found_ = false;
        lost_event_ = true;
        return false;
    }
    published = held_;
    last_held_ = true;
    return true;
}

}  // namespace pi_core
