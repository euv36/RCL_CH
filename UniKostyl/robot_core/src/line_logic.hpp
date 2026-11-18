#pragma once
// line_logic.hpp — 16 raw line values -> binarization -> line angle.
//
// Ported from rcj_arduino_src/line.cpp (READ-ONLY reference). In the split the
// Mega streams RAW ADC values (mega_fw/src/lines_fw.cpp) and this module does
// the thresholding + countAngle() with the reference weight tables.
//
// Reference semantics preserved exactly:
//   - per-sensor thresholds (FORWARD / GOALKEEPER tables from line.hpp);
//   - bin[i] = raw[i] > th[i];
//   - the 16 sensors form 8 PAIRS (2i, 2i+1); a pair counts once;
//   - weightSet1 = {0,45,90,135,180,225,270,315}, weightSet2 =
//     {0,45,90,135,-180,-135,-90,-45};
//   - angle1 = sum(weightSet1 over triggered pairs) / count (integer division),
//     angle2 likewise;
//   - angle = (pair 0 triggered || angle1 > 180) ? angle2 : angle1;
//   - count == 0 -> angle = -1 (no line).
// The "first circle" = even-index sensors, "second circle" = odd-index
// sensors (rcj isFirstCircleLine/isSecondCircleLine — the preemption trigger
// of checkLine()).

namespace robot_core {

constexpr int kLineSensNum = 16;

// rcj line.hpp lineSensorPos — mux channel per sensor INDEX. The Mega applies
// this table when polling; it is repeated here so raw[i] stays meaningful.
extern const int line_sensor_pos[kLineSensNum];

// rcj line.hpp per-sensor thresholds, per robot.
extern const int line_thresholds_forward[kLineSensNum];
extern const int line_thresholds_goalkeeper[kLineSensNum];

struct LineView {
    int raw[kLineSensNum] = {0};
    bool bin[kLineSensNum] = {false};
    int angle = -1;              // -1 = no pair triggered
    int pair_count = 0;          // triggered pairs (rcj sensOnLine after countAngle)
    bool first_circle = false;   // any even-index sensor triggered
    bool second_circle = false;  // any odd-index sensor triggered
};

// Binarize raw values with a threshold table (rcj binValues[i] = values[i] >
// lineSensorsTH[i] inside readSens_()).
void line_binarize(const int raw[kLineSensNum], const int th[kLineSensNum],
                   bool out_bin[kLineSensNum]);

// rcj Line::countAngle() over binarized values; fills angle + pair_count.
int line_count_angle(const bool bin[kLineSensNum]);

// Full update: binarize + angle + circle flags (rcj updateStatus()).
void line_update(LineView& view, const int raw[kLineSensNum], const int th[kLineSensNum]);

}  // namespace robot_core
