#pragma once
// locator_i2c_linux.hpp — the IR ball Locator on the PI's I2C bus.
//
// // UNVERIFIED ON HW: Linux i2c-dev (/dev/i2c-1) has never been run against
// the real Locator here. Compile target: Pi only.
//
// ══ VOLTAGE WARNING (PROMPT_ROBOT §3 — do not skip) ══
// The Mega runs 5 V logic; the Pi's I2C (SDA1/SCL1 = GPIO2/3, /dev/i2c-1) is
// 3.3 V. The Locator module was powered from the MEGA before the split. Before
// connecting it to the Pi:
//   1. Measure the module's VCC input range and its I2C pull-up rail.
//   2. If the module or its pull-ups are 5 V, a LEVEL SHIFTER IS MANDATORY
//      (e.g. BSS138 MOSFET pair) — 5 V pull-ups on SDA/SCL will back-feed the
//      Pi GPIOs through the protection diodes.
//   3. Report the measured/assumed voltage in the acceptance report; do not
//      silently assume 3.3 V.
// (As of this phase the wiring has NOT been measured — that is a hardware
// step for the user. Nothing here assumes a voltage.)

#include "ball_logic.hpp"
#include <string>

namespace robot_core {

// IBallTransport backend on Linux i2c-dev: the same single-register reads the
// rcj Locator struct issued (register-select write + 1-byte read).
class LocatorI2c : public IBallTransport {
public:
    // dev = "/dev/i2c-1" (SDA1/SCL1). address = 0x0E.
    LocatorI2c(const std::string& dev, uint8_t address);
    ~LocatorI2c() override;

    bool open_bus();
    void close_bus();
    bool ok() const { return fd_ >= 0; }

    bool read_register(uint8_t reg, uint8_t& out) override;

    // Force a fresh open (bus recovery after repeated failures).
    void reopen();

private:
    std::string dev_;
    uint8_t address_;
    int fd_ = -1;
    unsigned long failures_ = 0;
};

}  // namespace robot_core
