// pi_core/distance.cpp — see distance.hpp
#include "distance.hpp"

#include "atomic_file.hpp"
#include "log.hpp"

#include <nlohmann/json.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace pi_core {

namespace {
using json = nlohmann::json;

// M_PI is not portable (MSVC/strict clang); use the exact IEEE double value.
constexpr double kPi = 3.14159265358979323846;

// Solves the 3x3 system M x = v by Gaussian elimination with partial
// pivoting. Returns false on a (near-)singular matrix.
bool solve3(const double m[3][3], const double v[3], double x[3]) {
    double a[3][4];
    for (int i = 0; i < 3; ++i) {
        for (int j = 0; j < 3; ++j) a[i][j] = m[i][j];
        a[i][3] = v[i];
    }
    for (int col = 0; col < 3; ++col) {
        int piv = col;
        for (int i = col + 1; i < 3; ++i) {
            if (std::fabs(a[i][col]) > std::fabs(a[piv][col])) piv = i;
        }
        if (std::fabs(a[piv][col]) < 1e-12) return false;
        if (piv != col) {
            for (int j = 0; j < 4; ++j) std::swap(a[col][j], a[piv][j]);
        }
        for (int i = col + 1; i < 3; ++i) {
            const double f = a[i][col] / a[col][col];
            for (int j = col; j < 4; ++j) a[i][j] -= f * a[col][j];
        }
    }
    for (int i = 2; i >= 0; --i) {
        double s = a[i][3];
        for (int j = i + 1; j < 3; ++j) s -= a[i][j] * x[j];
        x[i] = s / a[i][i];
        if (!std::isfinite(x[i])) return false;
    }
    return true;
}

// Sum of squared residuals of the model over the calibration points
// (non-finite predictions count as a huge penalty).
double model_sse(double a, double b, double c, const std::vector<double>& d,
                 const std::vector<double>& r) {
    double sse = 0.0;
    for (size_t i = 0; i < d.size(); ++i) {
        const double denom = b - r[i];
        const double pred = a / denom - c;
        double res = std::isfinite(pred) ? (pred - d[i]) : 1e18;
        if (!std::isfinite(res)) res = 1e18;
        sse += res * res;
    }
    return sse;
}

// A few Gauss-Newton iterations on the ORIGINAL residual (the linearized
// least squares minimizes the algebraized residual instead). Damped by
// accepting only SSE improvements; keeps the input on divergence.
void gauss_newton_polish(double& a, double& b, double& c,
                         const std::vector<double>& d,
                         const std::vector<double>& r) {
    double best[3] = {a, b, c};
    double best_sse = model_sse(a, b, c, d, r);
    for (int it = 0; it < 20; ++it) {
        double JTJ[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
        double JTr[3] = {0, 0, 0};
        bool ok = true;
        for (size_t i = 0; i < d.size(); ++i) {
            const double denom = best[1] - r[i];
            if (std::fabs(denom) < 1e-12) { ok = false; break; }
            const double inv = 1.0 / denom;
            const double J[3] = {inv, -best[0] * inv * inv, -1.0};
            const double res = best[0] / denom - best[2] - d[i];
            if (!std::isfinite(res) || !std::isfinite(J[1])) { ok = false; break; }
            for (int p = 0; p < 3; ++p) {
                JTr[p] += J[p] * res;
                for (int q2 = 0; q2 < 3; ++q2) JTJ[p][q2] += J[p] * J[q2];
            }
        }
        if (!ok) break;
        for (int p = 0; p < 3; ++p) JTJ[p][p] += 1e-12;  // damp
        double x[3];
        if (!solve3(JTJ, JTr, x)) break;
        const double cand[3] = {best[0] - x[0], best[1] - x[1], best[2] - x[2]};
        const double sse = model_sse(cand[0], cand[1], cand[2], d, r);
        if (sse < best_sse) {
            best[0] = cand[0];
            best[1] = cand[1];
            best[2] = cand[2];
            best_sse = sse;
        } else {
            break;
        }
    }
    a = best[0];
    b = best[1];
    c = best[2];
}

}  // namespace

void validate_distance_model(DistanceModel& m) {
    m.valid = false;
    do {
        if (!std::isfinite(m.a) || !std::isfinite(m.b) || !std::isfinite(m.c)) {
            m.error = "модель содержит NaN/inf";
            break;
        }
        if (m.a >= 0.0) {
            m.error = "a>=0: модель не убывает по r (больше ворота должны быть ближе)";
            break;
        }
        // Working interval: calibration span +- 25% extension, CLIPPED to the
        // pole-safe side of the asymptote (evaluation must never jump across
        // the pole even within the allowed extrapolation). Mirrors
        // distance.py _eval_interval.
        if (m.has_interval) {
            const double span = std::max(1.0, m.r_hi - m.r_lo);
            const double ext = std::max(kExtrapMinPx, kExtrapFrac * span);
            const double pole = std::max(1.0, 0.05 * span);
            if (m.b >= m.r_lo && m.b <= m.r_hi) {
                char buf[160];
                std::snprintf(buf, sizeof(buf),
                              "асимптота b=%g внутри вилки калибровки "
                              "r=[%g..%g]", m.b, m.r_lo, m.r_hi);
                m.error = buf;
                break;
            }
            double lo = m.r_lo - ext;
            double hi = m.r_hi + ext;
            if (m.b < m.r_lo) {
                lo = std::max(lo, m.b + pole);
            } else {  // b > r_hi
                hi = std::min(hi, m.b - pole);
            }
            if (lo >= hi) {
                m.error = "рабочий интервал пуст (полюс слишком близко к вилке r)";
                break;
            }
            m.r_eval_lo = lo;
            m.r_eval_hi = hi;
        } else {
            m.r_eval_lo = 0.0;
            m.r_eval_hi = 0.0;
        }
        // Sampled check: finite and monotonically decreasing over the whole
        // working interval (d' = a/(b-r)^2 has no critical points, so this
        // catches NaN regions and numerical surprises).
        bool prev_ok = false;
        double prev = 0.0;
        const double lo = m.has_interval ? m.r_eval_lo : 1.0;
        const double hi = m.has_interval ? m.r_eval_hi : 500.0;
        const double step = std::max(1e-6, (hi - lo) / 64.0);
        for (double r = lo; r <= hi + 1e-9; r += step) {
            const double denom = m.b - r;
            if (std::fabs(denom) <= kEpsDiv) {
                prev_ok = false;
                continue;
            }
            const double dd = m.a / denom - m.c;
            if (!std::isfinite(dd)) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "модель даёт NaN/inf при r=%g", r);
                m.error = buf;
                return;
            }
            if (prev_ok && dd > prev + 1e-9) {
                char buf[96];
                std::snprintf(buf, sizeof(buf), "модель не монотонна (d растёт при r=%g)", r);
                m.error = buf;
                return;
            }
            prev = dd;
            prev_ok = true;
        }
        m.valid = true;
        m.error.clear();
    } while (false);
}

bool fit_distance_model(const std::vector<double>& d_cm,
                        const std::vector<double>& r_px, int frame_w,
                        int frame_h, DistanceModel& out) {
    out = DistanceModel();
    if (d_cm.size() != r_px.size() || d_cm.size() < kDistMinPoints) {
        out.error = "точек " + std::to_string(d_cm.size()) + " < " +
                    std::to_string(kDistMinPoints);
        return false;
    }
    std::vector<double> d_ok, r_ok;
    for (size_t i = 0; i < d_cm.size(); ++i) {
        if (!std::isfinite(d_cm[i]) || !std::isfinite(r_px[i])) continue;
        if (d_cm[i] <= 0.0 || r_px[i] <= 0.0) {
            out.error = "точка: нужны dist_cm > 0 и r_px > 0";
            return false;
        }
        d_ok.push_back(d_cm[i]);
        r_ok.push_back(r_px[i]);
    }
    if (d_ok.size() < kDistMinPoints) {
        out.error = "валидных точек " + std::to_string(d_ok.size()) + " < " +
                    std::to_string(kDistMinPoints);
        return false;
    }
    double r_lo = r_ok[0], r_hi = r_ok[0];
    for (double v : r_ok) {
        r_lo = std::min(r_lo, v);
        r_hi = std::max(r_hi, v);
    }
    if (r_hi - r_lo < 1e-9) {
        out.error = "все r одинаковые — фит вырожден";
        return false;
    }

    // Linearization: b*d + q - c*r = r*d, q = c*b - a. Normal equations
    // A^T A x = A^T y with A rows [d_i, 1, -r_i], y_i = r_i*d_i.
    double M[3][3] = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}};
    double v[3] = {0, 0, 0};
    for (size_t i = 0; i < d_ok.size(); ++i) {
        const double row[3] = {d_ok[i], 1.0, -r_ok[i]};
        const double y = r_ok[i] * d_ok[i];
        for (int p = 0; p < 3; ++p) {
            v[p] += row[p] * y;
            for (int q2 = 0; q2 < 3; ++q2) M[p][q2] += row[p] * row[q2];
        }
    }
    double x[3];
    if (!solve3(M, v, x)) {
        out.error = "система вырождена — точки слишком похожи";
        return false;
    }
    double b = x[0], q = x[1], c = x[2];
    double a = c * b - q;
    gauss_newton_polish(a, b, c, d_ok, r_ok);

    out.a = a;
    out.b = b;
    out.c = c;
    out.points.clear();
    for (size_t i = 0; i < d_ok.size(); ++i) {
        out.points.push_back(d_ok[i]);
        out.points.push_back(r_ok[i]);
    }
    out.frame_w = frame_w > 0 ? frame_w : 0;
    out.frame_h = frame_h > 0 ? frame_h : 0;
    out.r_lo = r_lo;
    out.r_hi = r_hi;
    out.has_interval = true;
    validate_distance_model(out);
    return out.valid;
}

double analytic_distance_cm(double w_px, double frame_w, double gate_width_cm) {
    if (!std::isfinite(w_px) || !std::isfinite(frame_w) ||
        !std::isfinite(gate_width_cm)) {
        return -1.0;
    }
    if (frame_w <= 0.0 || gate_width_cm <= 0.0 || w_px <= 0.0) return -1.0;
    return gate_width_cm * (frame_w / kPi) / std::max(w_px, 1.0);
}

DistEval eval_distance(const DistanceModel& m, double r_px, int frame_min_dim) {
    DistEval out;
    out.dist_cm = std::nan("");
    out.reliable = false;
    if (!m.valid) return out;
    double r = r_px;
    if (!std::isfinite(r) || r <= 0.0) return out;
    const int calib_min = (m.frame_w > 0 && m.frame_h > 0)
                              ? std::min(m.frame_w, m.frame_h)
                              : 0;
    if (calib_min > 0 && frame_min_dim > 0 && frame_min_dim != calib_min) {
        r = r * calib_min / static_cast<double>(frame_min_dim);
    }
    const double denom = m.b - r;
    if (!std::isfinite(denom) || std::fabs(denom) <= kEpsDiv) return out;
    if (m.has_interval && (r < m.r_eval_lo || r > m.r_eval_hi)) return out;
    const double d = m.a / denom - m.c;
    if (!std::isfinite(d)) return out;
    if (d < kDistMinCm || d > kDistMaxCm) {
        out.dist_cm = std::min(std::max(d, kDistMinCm), kDistMaxCm);
        out.reliable = false;
        return out;
    }
    out.dist_cm = d;
    out.reliable = true;
    return out;
}

DistanceModel distance_model_from_json(const std::string& text) {
    DistanceModel m;
    json doc = json::parse(text, nullptr, false);
    if (doc.is_discarded() || !doc.is_object()) {
        m.error = "не JSON";
        return m;
    }
    int version = 0;
    if (auto it = doc.find("version"); it != doc.end() && it->is_number()) {
        version = it->get<int>();
    }
    if (version != 1) {
        m.error = "версия " + std::to_string(version) + " != 1";
        return m;
    }
    if (auto it = doc.find("model"); it != doc.end() && it->is_string()) {
        if (it->get<std::string>() != "a/(b-r)-c") {
            m.error = "неизвестная модель";
            return m;
        }
    }
    auto num = [&](const char* key, double& val) -> bool {
        auto it = doc.find(key);
        if (it == doc.end() || !it->is_number()) return false;
        val = it->get<double>();
        return std::isfinite(val);
    };
    if (!num("a", m.a) || !num("b", m.b) || !num("c", m.c)) {
        m.error = "a/b/c отсутствуют или не конечны";
        return m;
    }
    if (auto it = doc.find("frame_w"); it != doc.end() && it->is_number()) {
        m.frame_w = it->get<int>();
    }
    if (auto it = doc.find("frame_h"); it != doc.end() && it->is_number()) {
        m.frame_h = it->get<int>();
    }
    if (auto it = doc.find("points"); it != doc.end() && it->is_array()) {
        for (const auto& p : *it) {
            if (!p.is_array() || p.size() < 2 || !p[0].is_number() ||
                !p[1].is_number()) {
                continue;
            }
            const double d = p[0].get<double>();
            const double r = p[1].get<double>();
            if (!std::isfinite(d) || !std::isfinite(r) || d <= 0.0 || r <= 0.0) {
                continue;
            }
            m.points.push_back(d);
            m.points.push_back(r);
        }
    }
    if (m.points.size() >= 2) {
        m.r_lo = m.r_hi = m.points[1];
        for (size_t i = 1; i < m.points.size(); i += 2) {
            m.r_lo = std::min(m.r_lo, m.points[i]);
            m.r_hi = std::max(m.r_hi, m.points[i]);
        }
        m.has_interval = true;
    } else {
        m.points.clear();
        m.has_interval = false;
    }
    validate_distance_model(m);
    return m;
}

DistanceModel load_distance_model(const std::string& path) {
    std::string text;
    if (!read_file(path, text)) {
        DistanceModel m;
        m.error = "файл не читается";
        return m;
    }
    return distance_model_from_json(text);
}

std::string distance_model_to_json(const DistanceModel& m, const std::string& ts) {
    json doc;
    doc["version"] = 1;
    doc["model"] = "a/(b-r)-c";
    doc["a"] = m.a;
    doc["b"] = m.b;
    doc["c"] = m.c;
    json pts = json::array();
    for (size_t i = 0; i + 1 < m.points.size(); i += 2) {
        pts.push_back(json::array({m.points[i], m.points[i + 1]}));
    }
    doc["points"] = pts;
    doc["frame_w"] = m.frame_w;
    doc["frame_h"] = m.frame_h;
    doc["ts"] = ts;
    return doc.dump(1) + "\n";
}

bool save_distance_model(const std::string& path, const DistanceModel& m,
                         const std::string& ts) {
    return atomic_write(path, distance_model_to_json(m, ts));
}

// ---------------- DistEstimator ----------------
DistEstimator::DistEstimator(double hold_s, double drop_frac, double fill_min,
                             double move_frac, double gate_width_cm)
    : hold_s_(hold_s < 0.0 ? 0.0 : hold_s),
      drop_frac_(drop_frac < 0.0 ? 0.0 : (drop_frac > 1.0 ? 1.0 : drop_frac)),
      fill_min_(fill_min < 0.0 ? 0.0 : fill_min),
      move_frac_(move_frac < 0.0 ? 0.0 : move_frac),
      gate_w_(gate_width_cm) {}

void DistEstimator::set_model(const DistanceModel& m) {
    model_ = m;
    has_model_ = m.valid;
    reset();
}

void DistEstimator::reset() {
    has_hold_ = false;
    hold_val_ = 0.0;
    hold_t_ = 0.0;
    has_ref_ = false;
    ref_radius_ = ref_cx_ = ref_cy_ = 0.0;
}

DistEval DistEstimator::update(double radius, double w_px, double h_px,
                               double area_px, double cx, double cy, bool held,
                               double now_s, int frame_min_dim, int frame_w) {
    DistEval out;
    out.dist_cm = std::nan("");
    out.reliable = false;

    // Held frame: the mask was empty, the hysteresis re-publishes the last
    // accepted object. The size is unconfirmed -> unreliable + hold (the
    // radius did not change, so the held value equals the last one anyway).
    if (held) {
        if (has_hold_ && now_s - hold_t_ <= hold_s_) {
            out.dist_cm = hold_val_;
        }
        return out;
    }

    if (!std::isfinite(radius) || radius <= 0.0) {
        // Junk input: nothing to compute this frame, but the hold window
        // still applies (grace) — matches the Python GateDistEstimator.
        if (has_hold_ && now_s - hold_t_ <= hold_s_) {
            out.dist_cm = hold_val_;
        }
        return out;
    }

    // Occlusion heuristic 1: the centroid stayed but the radius suddenly
    // dropped -> somebody is covering the gate (visible size underestimates,
    // the distance would overestimate).
    bool occluded = false;
    if (drop_frac_ > 0.0 && has_ref_ && std::isfinite(cx) && std::isfinite(cy)) {
        const double dx = cx - ref_cx_;
        const double dy = cy - ref_cy_;
        const double move = std::sqrt(dx * dx + dy * dy);
        const double move_max =
            frame_min_dim > 0 ? move_frac_ * frame_min_dim : 0.0;
        if (radius < drop_frac_ * ref_radius_ && move <= move_max) {
            occluded = true;
        }
    }

    // Occlusion heuristic 2: holes in the union box (area/(w*h) low) — the
    // gate pieces were merged across the opponent's body / the object is
    // sparse: the box size is not the gate size.
    bool fill_ok = true;
    if (fill_min_ > 0.0) {
        if (std::isfinite(w_px) && std::isfinite(h_px) && std::isfinite(area_px) &&
            w_px > 0.0 && h_px > 0.0 && area_px > 0.0) {
            fill_ok = area_px / (w_px * h_px) >= fill_min_;
        } else {
            fill_ok = false;
        }
    }

    if (occluded || !fill_ok) {
        if (has_hold_ && now_s - hold_t_ <= hold_s_) {
            out.dist_cm = hold_val_;
        }
        return out;
    }

    // Size -> distance: calibrated model or the analytic fallback.
    double d = std::nan("");
    bool ok = false;
    if (has_model_) {
        const DistEval ev = eval_distance(model_, radius, frame_min_dim);
        d = ev.dist_cm;
        ok = ev.reliable;
    } else {
        d = analytic_distance_cm(w_px, static_cast<double>(frame_w), gate_w_);
        if (std::isfinite(d) && d > 0.0) {
            if (d < kDistMinCm || d > kDistMaxCm) {
                d = std::min(std::max(d, kDistMinCm), kDistMaxCm);
                ok = false;
            } else {
                ok = true;
            }
        } else {
            d = std::nan("");
            ok = false;
        }
    }

    if (ok) {
        has_hold_ = true;
        hold_val_ = d;
        hold_t_ = now_s;
        ref_radius_ = radius;
        ref_cx_ = cx;
        ref_cy_ = cy;
        has_ref_ = true;
        out.dist_cm = d;
        out.reliable = true;
    } else if (has_hold_ && now_s - hold_t_ <= hold_s_) {
        out.dist_cm = hold_val_;  // unreliable, hold the last reliable value
    }
    return out;
}

}  // namespace pi_core
