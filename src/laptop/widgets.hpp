#pragma once

#include "state.hpp"

namespace camera_cpp::laptop {

void apply_dark_theme();
void paste_into_source(AppState& state);

// Отдельные панели; каждая читает/обновляет только своё подмножество состояния.
void draw_source_panel(AppState& state);
void draw_video_panels(AppState& state);
void draw_gates_panel(AppState& state);
void draw_cam_settings(AppState& state);
void draw_radar(AppState& state);
void draw_hsv_mask(AppState& state);
void draw_help(AppState& state);

// Корневой immediate-mode макет и верхний ряд кнопок.
void draw_shell(AppState& state);

} // namespace camera_cpp::laptop
