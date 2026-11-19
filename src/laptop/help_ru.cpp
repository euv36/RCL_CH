#include "help_ru.hpp"

namespace camera_cpp::laptop {

// Возвращает разделы справки INFO на русском в порядке upper row -> details.
const std::vector<HelpSection>& HelpRu::sections() {
    static const std::vector<HelpSection> kSections{
        {"main", "Главное", {
            "Ctrl+V — вставить адрес источника из буфера обмена.",
            "CONNECT — подключиться к Pi по IP или открыть локальную камеру по индексу.",
            "BITMAP/L/A/B — переключить вид основного кадра: RGB или каналы Lab.",
            "PAUSE — приостановить обработку кадров.",
            "TARGET G1..G3 — выбрать ворота для калибровки порогов LAB.",
            "SAVE G1..G3 — сохранить цвет и пороги ворот на ПК и отправить на Pi.",
            "INFO — показать/скрыть эту справку.",
        }},
        {"gates", "Ворота", {
            "GATES — панель порогов и цветов ворот (слайдеры L/A/B min/max).",
            "Слайдеры L/A/B min/max — настройка диапазона цвета выбранного TARGET.",
            "Клик по слайдеру — изменить значение; сохраняется в saved_gates.json.",
            "SAVE G1..G3 — отправить обновлённые цвета ворот на Pi (PUB 5557).",
            "R/+/- — пересчёт/сдвиг порогов LAB по выделению в кадре.",
        }},
        {"radar", "Радар", {
            "RADAR — включить/выключить панель радара (окно RADAR).",
            "Радар показывает найденные ворота: угол, дистанцию, роль OURS/OPP.",
            "BIND FIELD — привязать камеру к полю (field_map.json).",
            "POS — пресет позиции камеры: CENTER / OWN GOAL / MANUAL.",
        }},
        {"camera", "Камера", {
            "CAM SET — настройки камеры (окно CAM SET).",
            "Слайдеры exposure/brightness/... — UVC-контроли; диапазоны канонические.",
            "SAFE DEFAULTS — сбросить настройки к безопасным значениям.",
            "SAVE NOW — сохранить настройки локально (camera_settings.json).",
            "SEND TO PI — отправить настройки на Pi (PUB 5559).",
            "ROLE OURS/OPP — назначить роль выбранным воротам.",
            "Масштаб окна CAM SET — перетащите край окна или +/- .",
        }},
        {"hsv", "HSV маска", {
            "HSV MASK — показать HSV-представление с масками ворот.",
            "Маски ворот подсвечиваются цветами из saved_gates.json.",
        }},
        {"field", "Поле", {
            "BIND FIELD — привязать камеру к полю по найденным воротам.",
            "POS — пресет позиции: CENTER / OWN GOAL / MANUAL.",
            "TARGET G1..G3 — выбрать ворота для привязки/калибровки.",
        }},
        {"status", "Статус", {
            "Статус соединения с Pi и состояние захвата кадра.",
            "STALE — статус Pi устарел (нет кадров > 2 с).",
        }},
    };
    return kSections;
}

std::string HelpRu::combined_text() {
    std::string text;
    for (const HelpSection& section : sections()) {
        if (!text.empty()) {
            text += "\n";
        }
        text += section.title + "\n";
        for (const std::string& line : section.lines) {
            text += line + "\n";
        }
    }
    return text;
}

} // namespace camera_cpp::laptop
