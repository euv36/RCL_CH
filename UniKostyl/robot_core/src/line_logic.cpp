#include "line_logic.hpp"

namespace robot_core {

// rcj line.hpp / line.cpp (READ-ONLY reference).
const int line_sensor_pos[kLineSensNum] = {12, 13, 10, 11, 9, 8, 6, 7, 4, 5, 2, 3, 1, 0, 15, 14};

const int line_thresholds_goalkeeper[kLineSensNum] = {600, 1000, 650, 650, 350, 700, 650, 300, 700, 700, 700, 700, 600, 600, 900, 250};
const int line_thresholds_forward[kLineSensNum] = {300, 1000, 350, 350, 260, 550, 380, 200, 350, 470, 500, 470, 330, 250, 140, 50};

void line_binarize(const int raw[kLineSensNum], const int th[kLineSensNum],
                   bool out_bin[kLineSensNum]) {
    for (int i = 0; i < kLineSensNum; i++) {
        out_bin[i] = raw[i] > th[i];
    }
}

int line_count_angle(const bool bin[kLineSensNum]) {
    // rcj Line::countAngle(), exact port including the integer division and
    // the pair-0 / angle1>180 tie-break.
    static const int weightSet1[] = {0, 45, 90, 135, 180, 225, 270, 315};
    static const int weightSet2[] = {0, 45, 90, 135, -180, -135, -90, -45};
    int angle1 = 0, angle2 = 0;
    bool generalBinValues[kLineSensNum / 2];
    int sensOnLine = 0;
    for (int i = 0; i < kLineSensNum; i += 2) {
        generalBinValues[i / 2] = bin[i] || bin[i + 1];
        if (generalBinValues[i / 2]) {
            angle1 += weightSet1[i / 2];
            angle2 += weightSet2[i / 2];
            sensOnLine++;
        }
    }
    if (sensOnLine == 0) {
        return -1;
    }
    angle1 /= sensOnLine;
    angle2 /= sensOnLine;
    if (generalBinValues[0] || angle1 > 180) {
        return angle2;
    }
    return angle1;
}

void line_update(LineView& view, const int raw[kLineSensNum], const int th[kLineSensNum]) {
    for (int i = 0; i < kLineSensNum; i++) {
        view.raw[i] = raw[i];
    }
    line_binarize(raw, th, view.bin);
    // rcj readSens_(): first circle = even index, second circle = odd index.
    view.first_circle = false;
    view.second_circle = false;
    for (int i = 0; i < kLineSensNum; i++) {
        if (view.bin[i]) {
            if (i % 2) {
                view.second_circle = true;
            } else {
                view.first_circle = true;
            }
        }
    }
    view.angle = line_count_angle(view.bin);
    view.pair_count = 0;
    for (int i = 0; i < kLineSensNum; i += 2) {
        if (view.bin[i] || view.bin[i + 1]) {
            view.pair_count++;
        }
    }
}

}  // namespace robot_core
