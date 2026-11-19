#pragma once

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace camera_cpp {

class GateStore;
class CommandRunner;

class ColorListener {
public:
    explicit ColorListener(int bind_port = 5557);
    ~ColorListener();

    ColorListener(const ColorListener&) = delete;
    ColorListener& operator=(const ColorListener&) = delete;

    bool start(GateStore& store);
    void shutdown();
    bool update_from_json(const std::string& json_text, GateStore& store, std::string* error = nullptr) const;

private:
    void loop(GateStore& store);

    int bind_port_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    void* context_ = nullptr;
    void* socket_ = nullptr;
};

class CamSettingsListener {
public:
    explicit CamSettingsListener(int bind_port = 5559);
    ~CamSettingsListener();

    CamSettingsListener(const CamSettingsListener&) = delete;
    CamSettingsListener& operator=(const CamSettingsListener&) = delete;

    bool start(std::shared_ptr<CommandRunner> runner, const std::string& device = "/dev/video0");
    void shutdown();
    bool set_last_message(const std::string& json_text, std::string* controls_json = nullptr) const;

private:
    void loop(std::shared_ptr<CommandRunner> runner, std::string device);

    int bind_port_;
    std::atomic<bool> stop_{false};
    std::thread thread_;
    void* context_ = nullptr;
    void* socket_ = nullptr;
    mutable std::mutex last_message_mutex_;
    mutable std::string last_message_;
};

} // namespace camera_cpp
