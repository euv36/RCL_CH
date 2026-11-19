#ifndef CAMERA_CPP_PI_CAMERA_SERIAL_LINK_HPP
#define CAMERA_CPP_PI_CAMERA_SERIAL_LINK_HPP

#include <filesystem>
#include <string>
#include <string_view>

namespace camera_cpp {

// Состояние отправки одного ворот в последовательный порт робота.
// Поля повторяют legacy state из rpi_elp_cam.py: last_sent_found,
// last_sent_angle, last_send_time.
struct GateSerialState {
    bool last_sent_found = false;
    int last_sent_angle = -1;
    double last_send_time = 0.0;
};

// Результат обновления одного ворот: send=true означает, что команда готова.
struct SerialCommand {
    bool send = false;
    std::string line;
};

// Формирует команду G<id>,<angle>,<area>\n по правилам legacy.
// Первая детекция отправляется сразу, последующие — при изменении угла >=3°
// и не чаще одного раза в 0.1 с. Потеря ворот отправляет G<id>,-1,0\n один раз.
SerialCommand update_gate_serial(GateSerialState& state, int gate_id, bool found,
                                 float angle_deg, float area_px, double now_seconds);

// Возвращает true, если путь последовательного порта существует.
// Отсутствие порта (Arduino отключён) — нормальная ситуация, не ошибка.
bool serial_port_available(const std::filesystem::path& port);

// Обёртка над UART Arduino. При отсутствии порта объект остаётся неактивным
// и все записи безопасно игнорируются; сервис продолжает работать без робота.
class SerialLink {
public:
    SerialLink(std::string port, bool enabled);
    ~SerialLink();

    SerialLink(const SerialLink&) = delete;
    SerialLink& operator=(const SerialLink&) = delete;

    bool connected() const noexcept;
    bool write(std::string_view line) noexcept;
    void close() noexcept;

private:
    std::string port_;
    bool enabled_;
    int fd_ = -1;
};

} // namespace camera_cpp

#endif
