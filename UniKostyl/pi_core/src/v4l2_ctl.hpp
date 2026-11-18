// pi_core/v4l2_ctl.hpp — camera control via v4l2-ctl subprocess, mirroring
// v4l2_set / apply_camera_controls / restore_camera_controls in
// rpi_elp_cam.py. Errors are never fatal (a missing v4l2-ctl or a rejected
// control must not take the tracker down).
#pragma once

#include <string>
#include <utility>
#include <vector>

namespace pi_core {

// One batched `v4l2-ctl -d DEV -c name=value ...` call; on failure retries
// control-by-control (a single unsupported control used to silently drop
// gamma/saturation/contrast when the whole batch failed — same trap as
// documented in UniKostyl/AGENTS.md). Returns true if the batch or at least
// one single control succeeded.
bool v4l2_set(const std::string& device,
              const std::vector<std::pair<std::string, int>>& controls);

// Manual exposure is the main way to keep 26 fps (auto drops to ~15 fps).
bool apply_camera_controls(const std::string& device, int exposure, int gain,
                           int brightness, int power_line, int gamma,
                           int saturation, int contrast);

// Returns the camera to auto exposure so other programs can use it.
// NOTE: exposure_time_absolute must NOT be in this batch — the control is
// inactive in auto mode and v4l2-ctl would fail the whole call with
// Permission denied.
bool restore_camera_controls(const std::string& device);

}  // namespace pi_core
