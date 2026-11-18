#pragma once

#include <string_view>

namespace contracts {
inline constexpr int kPiFrameW = 640;
inline constexpr int kPiFrameH = 360;
inline constexpr int kLaptopPortraitW = 720;
inline constexpr int kLaptopPortraitH = 1280;
inline constexpr int kVideoFps = 30;
}

namespace ports {
inline constexpr int kVideoStatus = 5555;
inline constexpr int kColors = 5557;
inline constexpr int kCamSettings = 5559;
}

enum class GateId : int {
    One = 1,
    Two = 2,
    Three = 3,
};

namespace status_keys {
inline constexpr std::string_view kSource = "src";
inline constexpr std::string_view kFps = "fps";
inline constexpr std::string_view kGates = "gates";
inline constexpr std::string_view kEnabled = "enabled";
inline constexpr std::string_view kFound = "found";
inline constexpr std::string_view kHeld = "held";
inline constexpr std::string_view kAngle = "angle";
inline constexpr std::string_view kRadiusPx = "r_px";
inline constexpr std::string_view kDistCm = "dist_cm";
}
