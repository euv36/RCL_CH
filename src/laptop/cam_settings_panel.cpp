#include "cam_settings_panel.hpp"

#include <array>

namespace camera_cpp::laptop {
namespace {

// Must stay a subset of common/cam_settings_config.cpp kControls. Notably
// auto_exposure is intentionally absent: the Pi apply path forces manual mode.
constexpr std::array<const char*, 8> kUserEditableControls{
    "exposure", "gain", "brightness", "gamma",
    "saturation", "contrast", "power_line_frequency", "exposure_dynamic_framerate",
};

} // namespace

const char* cam_apply_status_text(CamApplyStatus status) {
    switch (status) {
        case CamApplyStatus::Idle:
            return "Ожидание";
        case CamApplyStatus::Sending:
            return "Отправка запроса на Pi...";
        case CamApplyStatus::Applied:
            return "Применено локально: настройки сохранены";
        case CamApplyStatus::Rejected:
            return "Отклонено: Pi сообщил об ошибке";
        case CamApplyStatus::Offline:
            return "Pi недоступен: настройки не применены";
    }
    return "Неизвестное состояние";
}

bool cam_control_is_user_editable(const std::string& name) {
    for (const char* control : kUserEditableControls) {
        if (name == control) {
            return true;
        }
    }
    return false;
}

} // namespace camera_cpp::laptop
