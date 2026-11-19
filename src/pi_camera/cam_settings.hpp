#pragma once

#include <memory>
#include <string>
#include <unordered_map>

namespace camera_cpp {

struct CommandResult {
    int exit_code = -1;
    std::string out;
    std::string err;
};

class CommandRunner {
public:
    virtual ~CommandRunner() = default;
    virtual CommandResult run(const std::string& command) = 0;
};

struct ControlResult {
    std::string requested;
    std::string status;
};

struct ApplyReport {
    std::unordered_map<std::string, ControlResult> controls;
    bool ok() const;
};

class CamSettings {
public:
    explicit CamSettings(std::shared_ptr<CommandRunner> runner);
    ApplyReport apply_controls(const std::string& device, const std::unordered_map<std::string, int>& controls) const;

private:
    std::shared_ptr<CommandRunner> runner_;
};

} // namespace camera_cpp
