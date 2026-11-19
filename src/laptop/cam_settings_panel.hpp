#pragma once

#include <string>

namespace camera_cpp::laptop {

enum class CamApplyStatus { Idle, Sending, Applied, Rejected, Offline };

const char* cam_apply_status_text(CamApplyStatus status);
bool cam_control_is_user_editable(const std::string& name);

} // namespace camera_cpp::laptop
