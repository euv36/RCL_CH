#pragma once

#include <array>
#include <string>
#include <vector>

namespace camera_cpp::laptop {

// Цвет ворот для отправки на Pi: идентификатор, RGB и роль ours/opponent/unset.
struct GateColorOut {
    int id = 0;
    std::array<int, 3> rgb{0, 0, 0};
    std::string role = "unset";
};

// Формирует JSON 5557 в формате legacy: gates[].id/rgb/role и rgb только для G1.
std::string build_gate_payload(const std::vector<GateColorOut>& gates);

// Издатель ворот (5557) и настроек камеры (5559) для Pi.
class GatePublisher {
public:
    GatePublisher() = default;
    ~GatePublisher();

    GatePublisher(const GatePublisher&) = delete;
    GatePublisher& operator=(const GatePublisher&) = delete;

    // Привязывает оба PUB-сокета; возвращает false при ошибке.
    bool bind(int colors_port = 5557, int cam_port = 5559);

    // Отправляет цвета ворот; сообщение не блокирует UI.
    void publish_gates(const std::vector<GateColorOut>& gates);

    // Отправляет команду set с восемью контролами камеры.
    void publish_cam_settings(const std::array<float, 8>& controls);

    // Закрывает сокеты и контекст; повторный вызов безопасен.
    void close();

    bool bound() const noexcept;

private:
    void* context_ = nullptr;
    void* colors_socket_ = nullptr;
    void* cam_socket_ = nullptr;
};

} // namespace camera_cpp::laptop
