#include "config.hpp"

namespace robot_core {

int clamp_tick_hz(int hz) {
    if (hz < 50) return 50;
    if (hz > 100) return 100;
    return hz;
}

}  // namespace robot_core
