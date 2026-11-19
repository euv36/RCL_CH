#include "gate_cluster.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>

namespace {
constexpr double kInfinity = std::numeric_limits<double>::infinity();
constexpr float kFallbackFrameWidth = 1280.0f;
constexpr float kFallbackFrameHeight = 720.0f;

struct Point {
    double x = 0.0;
    double y = 0.0;
};

double cross(const Point& origin, const Point& a, const Point& b) {
    return (a.x - origin.x) * (b.y - origin.y) -
           (a.y - origin.y) * (b.x - origin.x);
}

std::vector<Point> convex_hull(std::vector<Point> points) {
    if (points.size() < 3) return points;
    std::sort(points.begin(), points.end(), [](const Point& left, const Point& right) {
        if (left.x != right.x) return left.x < right.x;
        return left.y < right.y;
    });
    std::vector<Point> hull(points.size() * 2);
    std::size_t size = 0;
    for (const Point& point : points) {
        while (size >= 2 && cross(hull[size - 2], hull[size - 1], point) <= 1e-12) --size;
        hull[size++] = point;
    }
    const std::size_t lower = size + 1;
    for (std::size_t i = points.size(); i-- > 0;) {
        const Point& point = points[i];
        while (size >= lower && cross(hull[size - 2], hull[size - 1], point) <= 1e-12) --size;
        hull[size++] = point;
    }
    hull.resize(size > 0 ? size - 1 : 0);
    return hull;
}

// Minimum-area enclosing rectangle via rotating calipers. The orientation is
// derived from the convex hull, so a thin diagonal lamp stand cannot hide
// behind a square axis-aligned bounding box.
void oriented_extents(const std::vector<Point>& points, double& min_side, double& max_side) {
    min_side = 0.0;
    max_side = 0.0;
    if (points.empty()) return;

    const std::vector<Point> hull = convex_hull(points);
    if (hull.size() < 2) return;
    if (hull.size() == 2) {
        const double dx = hull[1].x - hull[0].x;
        const double dy = hull[1].y - hull[0].y;
        max_side = std::sqrt(dx * dx + dy * dy);
        return;
    }

    const std::size_t count = hull.size();
    auto dot = [&hull](std::size_t index, double x, double y) {
        return hull[index].x * x + hull[index].y * y;
    };
    auto advance_max = [&hull, count, dot](std::size_t& index, double x, double y) {
        for (std::size_t step = 0; step < count; ++step) {
            const std::size_t next = (index + 1) % count;
            if (dot(next, x, y) <= dot(index, x, y) + 1e-12) break;
            index = next;
        }
    };
    auto advance_min = [&hull, count, dot](std::size_t& index, double x, double y) {
        for (std::size_t step = 0; step < count; ++step) {
            const std::size_t next = (index + 1) % count;
            if (dot(next, x, y) >= dot(index, x, y) - 1e-12) break;
            index = next;
        }
    };

    std::size_t max_u = 0, min_u = 0, max_v = 0, min_v = 0;
    double best_area = kInfinity;
    for (std::size_t i = 0; i < count; ++i) {
        const Point& start = hull[i];
        const Point& end = hull[(i + 1) % count];
        const double dx = end.x - start.x;
        const double dy = end.y - start.y;
        const double length = std::sqrt(dx * dx + dy * dy);
        if (length <= 1e-12) continue;
        const double ux = dx / length;
        const double uy = dy / length;
        const double vx = -uy;
        const double vy = ux;

        if (i == 0) {
            for (std::size_t index = 1; index < count; ++index) {
                if (dot(index, ux, uy) > dot(max_u, ux, uy)) max_u = index;
                if (dot(index, ux, uy) < dot(min_u, ux, uy)) min_u = index;
                if (dot(index, vx, vy) > dot(max_v, vx, vy)) max_v = index;
                if (dot(index, vx, vy) < dot(min_v, vx, vy)) min_v = index;
            }
        } else {
            advance_max(max_u, ux, uy);
            advance_min(min_u, ux, uy);
            advance_max(max_v, vx, vy);
            advance_min(min_v, vx, vy);
        }

        const double width = dot(max_u, ux, uy) - dot(min_u, ux, uy);
        const double height = dot(max_v, vx, vy) - dot(min_v, vx, vy);
        const double area = width * height;
        if (area < best_area) {
            best_area = area;
            min_side = std::min(width, height);
            max_side = std::max(width, height);
        }
    }
}
}

GateClusterParams resolve_cluster_params(int width, int height,
                                         float merge_gap_px,
                                         float max_stray_px,
                                         int open_kernel,
                                         int lost_frames,
                                         int confirm_frames,
                                         float max_aspect,
                                         int min_cluster_side_px) {
    const int frame_width = width > 1 ? width : 1;
    const int frame_height = height > 1 ? height : 1;
    GateClusterParams p{};
    p.merge_gap_px = merge_gap_px > 0.0f
        ? merge_gap_px
        : static_cast<float>(std::rint(0.025 * frame_width));
    p.max_stray_px = max_stray_px > 0.0f
        ? max_stray_px
        : static_cast<float>(std::rint(0.10 * frame_width));
    p.open_kernel = open_kernel > 1 ? open_kernel : 1;
    if (p.open_kernel % 2 == 0) ++p.open_kernel;
    p.lost_frames = lost_frames > 1 ? lost_frames : 1;
    p.confirm_frames = confirm_frames > 1 ? confirm_frames : 1;
    p.max_aspect = max_aspect > 0.0f ? max_aspect : 6.5f;
    p.min_cluster_side_px = min_cluster_side_px > 0
        ? min_cluster_side_px
        : static_cast<int>(std::rint(0.008 * std::min(frame_width, frame_height)));
    return p;
}

double gate_rect_gap(float x1, float y1, float w1, float h1,
                     float x2, float y2, float w2, float h2) {
    const double dx = std::max(0.0, std::max(static_cast<double>(x1), static_cast<double>(x2)) -
                                     std::min(static_cast<double>(x1 + w1), static_cast<double>(x2 + w2)));
    const double dy = std::max(0.0, std::max(static_cast<double>(y1), static_cast<double>(y2)) -
                                     std::min(static_cast<double>(y1 + h1), static_cast<double>(y2 + h2)));
    return std::sqrt(dx * dx + dy * dy);
}

// Группирует компоненты в ворота по геометрии и выбирает лучший кандидат.
// Приоритет отдают позиции из предыдущего кадра, чтобы не терять ворота.
GateObject build_gate_object(const std::vector<GateComponent>& components,
                             const GateClusterParams& params,
                             std::optional<float> previous_x,
                             std::optional<float> previous_y) {
    const int count = static_cast<int>(components.size());
    if (count == 0) return {};

    std::vector<int> keep;
    keep.reserve(static_cast<size_t>(std::min(count, std::max(1, params.max_candidates))));
    for (int i = 0; i < count; ++i) keep.push_back(i);
    if (count > params.max_candidates) {
        std::vector<int> order(keep);
        std::stable_sort(order.begin(), order.end(), [&components](int a, int b) {
            if (components[a].area != components[b].area)
                return components[a].area > components[b].area;
            return a < b;
        });
        order.resize(static_cast<size_t>(params.max_candidates));
        std::sort(order.begin(), order.end());
        keep.swap(order);
    }

    const float merge_gap_px = params.merge_gap_px > 0.0f
        ? params.merge_gap_px
        : static_cast<float>(std::rint(params.merge_gap_frac * kFallbackFrameWidth));
    const float max_stray_px = params.max_stray_px > 0.0f
        ? params.max_stray_px
        : static_cast<float>(std::rint(params.max_stray_frac * kFallbackFrameWidth));
    const int min_side_px = params.min_cluster_side_px > 0
        ? params.min_cluster_side_px
        : static_cast<int>(std::rint(params.min_cluster_side_frac * std::min(kFallbackFrameWidth, kFallbackFrameHeight)));

    const int member_count = static_cast<int>(keep.size());
    std::vector<int> parent(static_cast<size_t>(member_count));
    for (int i = 0; i < member_count; ++i) parent[static_cast<size_t>(i)] = i;
    const auto find = [&parent](int value) {
        int root = value;
        while (parent[static_cast<size_t>(root)] != root) {
            const int next = parent[static_cast<size_t>(root)];
            parent[static_cast<size_t>(root)] = parent[static_cast<size_t>(next)];
            root = parent[static_cast<size_t>(root)];
        }
        return root;
    };

    const double mg = merge_gap_px;
    for (int a = 0; a < member_count; ++a) {
        const GateComponent& ca = components[static_cast<size_t>(keep[static_cast<size_t>(a)])];
        for (int b = a + 1; b < member_count; ++b) {
            const GateComponent& cb = components[static_cast<size_t>(keep[static_cast<size_t>(b)])];
            const double dx = std::max(
                0.0, std::max(static_cast<double>(ca.x), static_cast<double>(cb.x)) -
                         std::min(static_cast<double>(ca.x + ca.w), static_cast<double>(cb.x + cb.w)));
            const double dy = std::max(
                0.0, std::max(static_cast<double>(ca.y), static_cast<double>(cb.y)) -
                         std::min(static_cast<double>(ca.y + ca.h), static_cast<double>(cb.y + cb.h)));
            if (dx * dx + dy * dy <= mg * mg) {
                const int ra = find(a);
                const int rb = find(b);
                if (ra != rb) parent[static_cast<size_t>(std::max(ra, rb))] = std::min(ra, rb);
            }
        }
    }

    std::map<int, std::vector<int>> groups;
    for (int i = 0; i < member_count; ++i) groups[find(i)].push_back(i);
    std::vector<std::vector<int>> cluster_lists;
    cluster_lists.reserve(groups.size());
    for (auto& pair : groups) cluster_lists.push_back(std::move(pair.second));
    std::sort(cluster_lists.begin(), cluster_lists.end(), [](const auto& left, const auto& right) {
        return left.front() < right.front();
    });

    struct Aggregate {
        double cx = 0.0, cy = 0.0, area = 0.0, x = kInfinity, y = kInfinity;
        double x1 = -kInfinity, y1 = -kInfinity, w = 0.0, h = 0.0;
        int members = 0;
        std::vector<Point> corners;
    };
    std::vector<Aggregate> aggregates;
    aggregates.reserve(cluster_lists.size());
    for (const auto& members : cluster_lists) {
        Aggregate aggregate{};
        for (int local : members) {
            const GateComponent& component = components[static_cast<size_t>(keep[static_cast<size_t>(local)])];
            aggregate.area += component.area;
            aggregate.cx += static_cast<double>(component.area) * component.cx;
            aggregate.cy += static_cast<double>(component.area) * component.cy;
            aggregate.x = std::min(aggregate.x, static_cast<double>(component.x));
            aggregate.y = std::min(aggregate.y, static_cast<double>(component.y));
            aggregate.x1 = std::max(aggregate.x1, static_cast<double>(component.x + component.w));
            aggregate.y1 = std::max(aggregate.y1, static_cast<double>(component.y + component.h));
            aggregate.corners.push_back(Point{static_cast<double>(component.x), static_cast<double>(component.y)});
            aggregate.corners.push_back(Point{static_cast<double>(component.x + component.w), static_cast<double>(component.y)});
            aggregate.corners.push_back(Point{static_cast<double>(component.x), static_cast<double>(component.y + component.h)});
            aggregate.corners.push_back(Point{static_cast<double>(component.x + component.w),
                                              static_cast<double>(component.y + component.h)});
            ++aggregate.members;
        }
        aggregate.cx /= aggregate.area;
        aggregate.cy /= aggregate.area;
        aggregate.w = aggregate.x1 - aggregate.x;
        aggregate.h = aggregate.y1 - aggregate.y;
        aggregates.push_back(aggregate);
    }

    aggregates.erase(std::remove_if(aggregates.begin(), aggregates.end(),
                                    [min_side_px, &params](const Aggregate& aggregate) {
                                        double min_side = 0.0;
                                        double max_side = 0.0;
                                        oriented_extents(aggregate.corners, min_side, max_side);
                                        return min_side <= 0.0 || min_side < min_side_px ||
                                               max_side / min_side > params.max_aspect;
                                    }),
                     aggregates.end());
    if (aggregates.empty()) return {};

    size_t best = 0;
    if (previous_x.has_value() && previous_y.has_value()) {
        double best_distance_squared = kInfinity;
        for (size_t i = 0; i < aggregates.size(); ++i) {
            const double dx = aggregates[i].cx - *previous_x;
            const double dy = aggregates[i].cy - *previous_y;
            const double d2 = dx * dx + dy * dy;
            if (d2 < best_distance_squared ||
                (d2 == best_distance_squared &&
                 (aggregates[i].area > aggregates[best].area ||
                  (aggregates[i].area == aggregates[best].area && i < best)))) {
                best_distance_squared = d2;
                best = i;
            }
        }
    } else {
        for (size_t i = 1; i < aggregates.size(); ++i) {
            if (aggregates[i].area > aggregates[best].area) best = i;
        }
    }

    std::vector<size_t> kept_indices;
    kept_indices.push_back(best);
    for (size_t i = 0; i < aggregates.size(); ++i) {
        if (i == best) continue;
        const double dx = aggregates[i].cx - aggregates[best].cx;
        const double dy = aggregates[i].cy - aggregates[best].cy;
        if (std::sqrt(dx * dx + dy * dy) <= max_stray_px) kept_indices.push_back(i);
    }

    GateObject result{};
    result.found = true;
    double area = 0.0, weighted_x = 0.0, weighted_y = 0.0;
    double x0 = kInfinity, y0 = kInfinity, x1 = -kInfinity, y1 = -kInfinity;
    for (size_t index : kept_indices) {
        const Aggregate& aggregate = aggregates[index];
        area += aggregate.area;
        weighted_x += aggregate.area * aggregate.cx;
        weighted_y += aggregate.area * aggregate.cy;
        x0 = std::min(x0, aggregate.x);
        y0 = std::min(y0, aggregate.y);
        x1 = std::max(x1, aggregate.x1);
        y1 = std::max(y1, aggregate.y1);
        result.members += aggregate.members;
    }
    result.area = static_cast<float>(area);
    result.cx = static_cast<float>(weighted_x / area);
    result.cy = static_cast<float>(weighted_y / area);
    result.bbox = GateRect{static_cast<float>(x0), static_cast<float>(y0),
                           static_cast<float>(x1 - x0), static_cast<float>(y1 - y0)};
    result.radius = static_cast<float>(0.5 * std::sqrt(result.bbox.w * result.bbox.w +
                                                      result.bbox.h * result.bbox.h));
    return result;
}

GateStability::GateStability(int confirm_frames, int lost_frames, float max_jump_px)
    : confirm_frames_(confirm_frames > 1 ? confirm_frames : 1),
      lost_frames_(lost_frames > 1 ? lost_frames : 1),
      max_jump_px_(max_jump_px) {}

double GateStability::distance(const SimpleObject& a, const SimpleObject& b) {
    const double dx = a.cx - b.cx;
    const double dy = a.cy - b.cy;
    return std::sqrt(dx * dx + dy * dy);
}

void GateStability::accept(const SimpleObject& value) {
    held_ = value;
    has_held_ = true;
    track_established_ = true;
    ever_found_ = true;
    empty_count_ = 0;
    has_pending_ = false;
    pending_count_ = 0;
}

GateStabilityResult GateStability::held_result(bool held) const {
    GateStabilityResult result{};
    result.found = track_established_;
    result.held = held;
    result.cx = held_.cx;
    result.cy = held_.cy;
    result.radius = held_.radius;
    result.area = held_.area;
    result.members = held_.members;
    return result;
}

void GateStability::reset() {
    track_established_ = false;
    ever_found_ = false;
    empty_count_ = 0;
    has_held_ = false;
    held_ = {};
    has_pending_ = false;
    pending_count_ = 0;
    pending_ = {};
}

// Обновляет стабильность detections: держит последнюю позицию короткую
// окклюзию и сбрасывает её после заданного числа пустых кадров.
GateStabilityResult GateStability::update(bool detected, float cx, float cy,
                                          float radius, float area, int members) {
    if (!detected) {
        has_pending_ = false;
        pending_count_ = 0;
        if (track_established_) {
            ++empty_count_;
            if (empty_count_ >= lost_frames_) {
                track_established_ = false;
                GateStabilityResult result{};
                result.lost_event = true;
                return result;
            }
            return held_result(true);
        }
        return {};
    }

    const SimpleObject candidate{cx, cy, radius, area, members};
    if (track_established_ && has_held_ &&
        distance(candidate, held_) <= max_jump_px_) {
        accept(candidate);
        return held_result(false);
    }

    if (has_pending_ && distance(candidate, pending_) <= max_jump_px_) {
        ++pending_count_;
        pending_ = candidate;
    } else {
        pending_ = candidate;
        has_pending_ = true;
        pending_count_ = 1;
    }

    if (!ever_found_) {
        accept(candidate);
        return held_result(false);
    }
    if (pending_count_ >= confirm_frames_) {
        accept(candidate);
        return held_result(false);
    }

    if (track_established_) {
        ++empty_count_;
        if (empty_count_ >= lost_frames_) {
            track_established_ = false;
            GateStabilityResult result{};
            result.lost_event = true;
            return result;
        }
        return held_result(true);
    }
    return {};
}
