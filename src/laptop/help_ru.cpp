#include "help_ru.hpp"

namespace camera_cpp::laptop {

// Возвращает разделы справки INFO на русском в порядке upper row -> details.
const std::vector<HelpSection>& HelpRu::sections() {
    static const std::vector<HelpSection> kSections{
        {"main", "Главное", {
            "Ctrl+V — вставить адрес источника из буфера обмена.",
            "INFO — показать/скрыть эту справку.",
            "SAVE G1 — сохранить цвет ворот G1 на Pi.",
        }},
        {"gates", "Ворота", {
            "GATES — панель порогов и цветов ворот.",
            "SAVE G1..G3 — отправить обновлённые цвета ворот на Pi.",
        }},
        {"radar", "Радар", {
            "RADAR — включить/выключить панель радара.",
        }},
        {"camera", "Камера", {
            "CAM SET — настройки камеры: exposure, brightness.",
        }},
        {"hsv", "HSV маска", {
            "HSV MASK — показать HSV-представление с масками ворот.",
        }},
        {"field", "Поле", {
            "BIND FIELD — привязать камеру к полю.",
            "POS — пресет позиции: CENTER / OWN GOAL / MANUAL.",
        }},
        {"status", "Статус", {
            "Статус соединения с Pi и состояние захвата кадра.",
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
