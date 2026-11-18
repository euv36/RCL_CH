// pi_core/v4l2_ctl.cpp — see v4l2_ctl.hpp
//
// Runs /usr/bin/v4l2-ctl via fork/exec with a 3 s timeout (subprocess.run
// timeout=3 in Python). Output is captured for error messages.
// UNVERIFIED ON HW: control names/values and v4l2-ctl paths — verify with
// `pi_core --probe` (apply + restore both run there).
#include "v4l2_ctl.hpp"
#include "log.hpp"

#include <sys/types.h>
#include <sys/wait.h>
#include <cerrno>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <string>
#include <thread>
#include <unistd.h>
#include <vector>

extern char** environ;

namespace pi_core {

namespace {

// Captures a pipe fd into a string until EOF or deadline.
std::string drain_fd(int fd, double timeout_s) {
    std::string out;
    const auto deadline_us = static_cast<long long>(timeout_s * 1e6);
    char buf[512];
    // Non-blocking so a read after POLLHUP can never hang.
    const int fl = ::fcntl(fd, F_GETFL, 0);
    if (fl >= 0) ::fcntl(fd, F_SETFL, fl | O_NONBLOCK);
    struct pollfd pfd;
    pfd.fd = fd;
    pfd.events = POLLIN;
    long long waited = 0;
    for (;;) {
        const int r = ::poll(&pfd, 1, 50);
        if (r < 0) {
            if (errno == EINTR) continue;
            break;
        }
        if (r == 0) {
            waited += 50;
            if (waited >= deadline_us) break;
            continue;
        }
        if (pfd.revents & (POLLIN | POLLHUP | POLLERR)) {
            const ssize_t n = ::read(fd, buf, sizeof(buf));
            if (n <= 0) break;  // EOF or error
            out.append(buf, static_cast<size_t>(n));
            waited = 0;
        }
    }
    return out;
}

// Runs argv with captured output and a hard timeout. Returns exit code
// (>=0) or -1 on spawn/timeout failure.
int run_command(const std::vector<std::string>& args, double timeout_s,
                std::string& out_err) {
    std::string stdout_acc, stderr_acc;
    int out_pipe[2], err_pipe[2];
    if (::pipe(out_pipe) != 0 || ::pipe(err_pipe) != 0) {
        out_err = "pipe() failed";
        return -1;
    }

    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addclose(&actions, out_pipe[0]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[0]);
    posix_spawn_file_actions_adddup2(&actions, out_pipe[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, err_pipe[1], STDERR_FILENO);
    posix_spawn_file_actions_addclose(&actions, out_pipe[1]);
    posix_spawn_file_actions_addclose(&actions, err_pipe[1]);

    std::vector<char*> argv;
    argv.reserve(args.size() + 1);
    for (const auto& a : args)
        argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = -1;
    const int spawn_rc = ::posix_spawnp(&pid, argv[0], &actions, nullptr,
                                        argv.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    ::close(out_pipe[1]);
    ::close(err_pipe[1]);

    if (spawn_rc != 0) {
        ::close(out_pipe[0]);
        ::close(err_pipe[0]);
        if (spawn_rc == ENOENT || errno == ENOENT) {
            out_err = "ENOENT";
            return -1;
        }
        out_err = "posix_spawnp failed: " + std::to_string(spawn_rc);
        return -1;
    }

    // Drain both pipes until EOF or the deadline.
    const auto t0 = std::chrono::steady_clock::now();
    double elapsed = 0.0;
    {
        // Simple sequential drain: v4l2-ctl writes little output; 2 s budget
        // total with 3 s enforced below on the waitpid side.
        stdout_acc = drain_fd(out_pipe[0], 2.0);
        stderr_acc = drain_fd(err_pipe[0], 2.0);
        elapsed = std::chrono::duration<double>(
                      std::chrono::steady_clock::now() - t0)
                      .count();
    }
    ::close(out_pipe[0]);
    ::close(err_pipe[0]);

    out_err = stdout_acc + stderr_acc;

    // waitpid with the remaining timeout; kill on overrun (timeout=3).
    int status = 0;
    const double budget = timeout_s > elapsed ? timeout_s - elapsed : 0.0;
    const auto deadline =
        std::chrono::steady_clock::now() +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(budget > 0.0 ? budget : 0.001));
    int rc = -1;
    for (;;) {
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            rc = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
            break;
        }
        if (done < 0) break;  // waitpid error
        if (std::chrono::steady_clock::now() > deadline) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            out_err = "v4l2-ctl не ответил за 3 с";
            rc = -1;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    return rc;
}

std::string clean_err(const std::string& raw) {
    std::string s;
    for (char c : raw) {
        if (c == '\n') s.push_back(' ');
        else s.push_back(c);
    }
    return s;
}

}  // namespace

bool v4l2_set(const std::string& device,
              const std::vector<std::pair<std::string, int>>& controls) {
    std::vector<std::string> batch = {"v4l2-ctl", "-d", device};
    for (const auto& c : controls) {
        batch.push_back("-c");
        batch.push_back(c.first + "=" + std::to_string(c.second));
    }
    std::string err;
    const int rc = run_command(batch, 3.0, err);
    if (rc == 0) return true;
    if (rc < 0 && err == "ENOENT") {
        log_msg("v4l2-ctl не найден; управление экспозицией недоступно", "WARN");
        return false;
    }
    if (rc < 0 && err.find("не ответил") != std::string::npos) {
        log_msg("v4l2-ctl не ответил за 3 с", "WARN");
        return false;
    }

    // Batch failed — retry control-by-control (same pattern as Python).
    bool ok_any = false;
    for (const auto& c : controls) {
        std::string single_err;
        const int single_rc = run_command(
            {"v4l2-ctl", "-d", device, "-c",
             c.first + "=" + std::to_string(c.second)},
            3.0, single_err);
        if (single_rc == 0) {
            ok_any = true;
        } else {
            log_msg("Контрол " + c.first + "=" + std::to_string(c.second) +
                        " не применён (" + clean_err(single_err) + ")",
                    "WARN");
        }
    }
    return ok_any;
}

bool apply_camera_controls(const std::string& device, int exposure, int gain,
                           int brightness, int power_line, int gamma,
                           int saturation, int contrast) {
    const bool ok = v4l2_set(device,
                             {{"auto_exposure", 1},
                              {"exposure_time_absolute", exposure},
                              {"gain", gain},
                              {"brightness", brightness},
                              {"gamma", gamma},
                              {"saturation", saturation},
                              {"contrast", contrast},
                              {"exposure_dynamic_framerate", 0},
                              {"power_line_frequency", power_line},
                              {"backlight_compensation", 0}});
    if (ok) {
        log_msg("Камера: ручная экспозиция=" + std::to_string(exposure) +
                ", gain=" + std::to_string(gain) +
                ", brightness=" + std::to_string(brightness) +
                ", gamma=" + std::to_string(gamma) +
                ", saturation=" + std::to_string(saturation) +
                ", contrast=" + std::to_string(contrast) +
                ", power_line=" + std::to_string(power_line) + "Hz");
    }
    return ok;
}

bool restore_camera_controls(const std::string& device) {
    // No exposure_time_absolute here (inactive in auto mode, see header).
    return v4l2_set(device,
                    {{"auto_exposure", 3},
                     {"gain", 0},
                     {"brightness", 0},
                     {"gamma", 100},
                     {"saturation", 60},
                     {"contrast", 32},
                     {"exposure_dynamic_framerate", 1},
                     {"backlight_compensation", 1}});
}

}  // namespace pi_core
