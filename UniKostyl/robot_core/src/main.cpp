// robot_core main — the Pi-side strategy program (PROMPT_ROBOT §3).
//
// // UNVERIFIED ON HW: this binary (termios + i2c-dev + zmq) has never run on
// the real Pi. The pure logic it drives (FSM, mixing, heading laws, ball/line
// math, gate parsing) IS unit-tested on PC — see tests/.
//
// Threads (fixed-rate, no busy-wait; CPU budget ~1 core is far from reached —
// the tick work is microseconds):
//   - control tick (default 100 Hz): builds SensorInputs from the telemetry +
//     ball + line + gate snapshots, runs the tactics FSM, sends the drive
//     command to the Mega (binary T_DRIVE/T_STOP frames, PROTOCOL.md v1),
//   - locator poll (50 Hz): I2C Locator reads via Ball (stale-value policy),
//   - uart RX (in UartLink): Mega telemetry lines -> TelemetryState,
//   - gate status: non-blocking ZMQ poll inside the control tick.
//
// Graceful degradation (PROMPT_ROBOT §4/§7): absent camera (--no-gates or no
// frames) = ball-only/line-only with a logged reason; absent Mega telemetry =
// commands stop (the Mega watchdog stops the motors); absent Locator = ball
// not found + reason. Nothing blocks on IO.
//
// When running this program, the camera core MUST run with --no-serial (the
// old G<id>,… camera lines would share /dev/serial0 with this link).

#include "config.hpp"
#include "tactics.hpp"
#include "ball_logic.hpp"
#include "line_logic.hpp"
#include "gate_status.hpp"
#include "uart_link_linux.hpp"
#include "locator_i2c_linux.hpp"
#include "zmq_status_sub.hpp"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

using namespace robot_core;

namespace {

std::atomic<bool> g_running{true};

void on_sigint(int) {
    g_running = false;
}

unsigned long mono_ms() {
    timespec ts{};
    ::clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long)(ts.tv_sec * 1000UL + ts.tv_nsec / 1000000UL);
}

void transition_logger(void* /*user*/, const TacticsTransition& t) {
    std::printf("[fsm] %s -> %s (%s) @%lu\n",
                tactics_state_name(t.from), tactics_state_name(t.to),
                t.reason, t.now_ms);
}

struct Cli {
    std::string serial_dev = "/dev/serial0";
    std::string i2c_dev = "/dev/i2c-1";
    std::string gates_endpoint = "tcp://127.0.0.1:5555";
    bool no_serial = false;      // print TX lines instead of the device
    bool no_i2c = false;         // no Locator (ball-only degrade, bench mode)
    bool no_gates = false;       // no camera status (ball-only degrade, bench)
    int tick_hz = 100;
    int duration_s = 0;          // 0 = run until SIGINT
    bool verbose = false;
    RobotConfig cfg;
};

Cli parse_cli(int argc, char** argv) {
    Cli c;
    for (int i = 1; i < argc; i++) {
        const char* a = argv[i];
        auto next_val = [&]() -> const char* {
            return (i + 1 < argc) ? argv[++i] : "";
        };
        if (std::strcmp(a, "--role") == 0) {
            const char* v = next_val();
            c.cfg.role = (std::strcmp(v, "goalkeeper") == 0) ? Role::kGoalkeeper
                                                             : Role::kForward;
        } else if (std::strcmp(a, "--gk-variant") == 0) {
            const char* v = next_val();
            c.cfg.gk_variant = (std::strcmp(v, "classic") == 0)
                                   ? GoalkeeperVariant::kClassic
                                   : GoalkeeperVariant::kDefense;
        } else if (std::strcmp(a, "--demo") == 0) {
            c.cfg.play_mode = false;
        } else if (std::strcmp(a, "--tick-hz") == 0) {
            c.tick_hz = clamp_tick_hz(std::atoi(next_val()));
        } else if (std::strcmp(a, "--serial-dev") == 0) {
            c.serial_dev = next_val();
        } else if (std::strcmp(a, "--no-serial") == 0) {
            c.no_serial = true;
        } else if (std::strcmp(a, "--i2c-dev") == 0) {
            c.i2c_dev = next_val();
        } else if (std::strcmp(a, "--no-i2c") == 0) {
            c.no_i2c = true;
        } else if (std::strcmp(a, "--gates-endpoint") == 0) {
            c.gates_endpoint = next_val();
        } else if (std::strcmp(a, "--no-gates") == 0) {
            c.no_gates = true;
        } else if (std::strcmp(a, "--duration") == 0) {
            c.duration_s = std::atoi(next_val());
        } else if (std::strcmp(a, "--verbose") == 0) {
            c.verbose = true;
        } else {
            std::printf("[cli] unknown flag %s (ignored)\n", a);
        }
    }
    return c;
}

}  // namespace

int main(int argc, char** argv) {
    Cli cli = parse_cli(argc, argv);
    std::signal(SIGINT, on_sigint);
    std::signal(SIGTERM, on_sigint);

    std::printf("robot_core: role=%s gk=%s play=%s tick=%dHz\n",
                cli.cfg.role == Role::kForward ? "forward" : "goalkeeper",
                cli.cfg.gk_variant == GoalkeeperVariant::kDefense ? "defense" : "classic",
                cli.cfg.play_mode ? "yes" : "demo", cli.tick_hz);

    // ---- Mega link ----
    UartLink uart(cli.serial_dev, cli.no_serial);
    if (!uart.open_link()) {
        std::printf("[main] no Mega link -> motors cannot be commanded; exiting\n");
        return 1;
    }
    uart.start_rx();

    // ---- Locator ----
    Ball ball(*(new NullBallTransport()));
    std::unique_ptr<LocatorI2c> locator;
    if (cli.no_i2c) {
        std::printf("[main] --no-i2c: Locator absent, ball-only degrade (ball never found)\n");
    } else {
        locator.reset(new LocatorI2c(cli.i2c_dev, kLocatorAddress));
        if (!locator->open_bus()) {
            std::printf("[main] Locator bus open failed (checked 0x%02X on %s) -> "
                        "degrading to no-ball\n",
                        kLocatorAddress, cli.i2c_dev.c_str());
        } else {
            std::printf("[main] Locator bus open on %s (address 0x%02X). "
                        "VOLTAGE CHECK REMINDER: verify 3.3V vs 5V I2C levels "
                        "before first connect (see locator_i2c_linux.hpp)\n",
                        cli.i2c_dev.c_str(), kLocatorAddress);
        }
    }

    // ---- camera gate status ----
    std::unique_ptr<GateStatusSub> gates;
    if (cli.no_gates) {
        std::printf("[main] --no-gates: camera status absent -> ball-only/line-only\n");
    } else {
        gates.reset(new GateStatusSub(cli.gates_endpoint));
        if (!gates->start()) {
            std::printf("[main] camera status unavailable -> ball-only/line-only\n");
        }
    }

    // ---- FSM ----
    TacticsFSM fsm(cli.cfg);
    if (cli.verbose) {
        fsm.set_logger(transition_logger, nullptr);
    }

    std::mutex ball_mtx;
    std::atomic<bool> loc_running{true};

    // Locator poll thread (50 Hz fixed rate — Idea 12: "I2C Locator at ~50 Hz
    // is enough; no busy-wait").
    std::thread locator_thread([&]() {
        auto next = std::chrono::steady_clock::now();
        const auto period = std::chrono::milliseconds(20);
        while (loc_running) {
            next += period;
            unsigned long now = mono_ms();
            if (locator) {
                std::lock_guard<std::mutex> lock(ball_mtx);
                ball.update(now);
            }
            std::this_thread::sleep_until(next);
        }
    });

    // ---- control tick ----
    const auto period = std::chrono::microseconds(1000000 / cli.tick_hz);
    auto next_tick = std::chrono::steady_clock::now();
    unsigned long last_status_ms = 0;
    unsigned long work_ns_accum = 0;
    unsigned long ticks = 0;
    LineView line;
    bool line_valid = false;

    while (g_running) {
        next_tick += period;
        auto t0 = std::chrono::steady_clock::now();
        unsigned long now = mono_ms();

        // 1. camera status (non-blocking).
        if (gates) {
            gates->poll(now);
        }

        // 2. telemetry snapshot.
        TelemetryState tel = uart.telemetry();
        bool tele_fresh =
            tel.have_heading &&
            (now - tel.last_frame_ms <= cli.cfg.telemetry_stale_ms);

        // 3. catcher bit into the ball (the microswitch lives on the Mega).
        if (!cli.no_i2c) {
            std::lock_guard<std::mutex> lock(ball_mtx);
            ball.set_catcher(tel.catcher_closed);
        }
        BallView bv;
        {
            std::lock_guard<std::mutex> lock(ball_mtx);
            bv = ball_view(ball, now);
        }

        // 4. line view (raw Mega values -> binarize + angle on the Pi).
        line_valid = tel.have_lines &&
                     (now - tel.lines_ms <= 500UL);  // line staleness guard
        if (line_valid) {
            const int* th = (cli.cfg.role == Role::kGoalkeeper)
                                ? line_thresholds_goalkeeper
                                : line_thresholds_forward;
            line_update(line, tel.line_raw, th);
        }

        // 5. FSM tick.
        SensorInputs in;
        in.now_ms = now;
        in.heading = tel.heading;
        in.heading_still = tel.heading_still;
        in.telemetry_fresh = tele_fresh;
        in.line = line_valid ? &line : nullptr;
        in.ball = &bv;
        if (gates) {
            in.gates = &gates->view();
            in.gates_fresh = gate_status_fresh(gates->view(), now,
                                               cli.cfg.camera_stale_ms);
        }
        DriveOutput out = fsm.tick(in);

        // 6. send the drive command (always: zero wheels keep feeding the Mega
        // watchdog while the Pi is alive; only Pi death stops the feed).
        DriveBits cmd;
        cmd.pwm[0] = out.wheels.lf;
        cmd.pwm[1] = out.wheels.lb;
        cmd.pwm[2] = out.wheels.rf;
        cmd.pwm[3] = out.wheels.rb;
        cmd.kicker_fire = out.kicker_fire;
        cmd.dribbler_on = out.dribbler_on;
        uart.send_drive(cmd);

        ticks++;

        // 7. per-second status line (same style as the camera core).
        unsigned long work_ns =
            (unsigned long)std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - t0)
                .count();
        work_ns_accum += work_ns;
        if (now - last_status_ms >= 1000) {
            last_status_ms = now;
            double cpu_pct = 100.0 * (double)work_ns_accum / 1e9;  // of 1 core
            work_ns_accum = 0;
            std::printf(
                "[tick] st=%s trans=%lu kick=%lu degr=%lu tele=%lu bad=%lu "
                "ball=%s(%d,%d) line=%s gates=%s cpu=%.2f%%\n",
                tactics_state_name(fsm.state()), fsm.transitions(),
                fsm.kicker_requests(), fsm.degrades(), tel.frames,
                tel.bad_frames, bv.found ? "found" : "lost", bv.angle,
                bv.distance, line_valid ? "ok" : "stale",
                (in.gates_fresh) ? "fresh" : "absent", cpu_pct);
            tel = uart.telemetry();  // refresh counters baseline (no-op read)
        }

        std::this_thread::sleep_until(next_tick);
        if (cli.duration_s > 0 && ticks >= (unsigned long)cli.duration_s * (unsigned long)cli.tick_hz) {
            break;
        }
    }

    // ---- shutdown: STOP then close (the Mega watchdog is the backstop) ----
    std::printf("[main] stopping: sending STOP\n");
    uart.send_stop();
    loc_running = false;
    locator_thread.join();
    uart.stop_rx();
    uart.close_link();
    return 0;
}
