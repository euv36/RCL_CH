// locator_i2c_linux.cpp — Linux i2c-dev backend of the Locator.
//
// // UNVERIFIED ON HW: compiled only; never probed against the real chip.
// Bench: `i2cdetect -y 1` must list 0x0E before trusting anything else.
//
// ══ VOLTAGE WARNING: see locator_i2c_linux.hpp — 3.3 V (Pi) vs 5 V (Mega).
// A level shifter is mandatory if the Locator board is a 5 V I2C device.

#include "locator_i2c_linux.hpp"
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <linux/i2c-dev.h>
#include <cstdio>

namespace robot_core {

LocatorI2c::LocatorI2c(const std::string& dev, uint8_t address)
    : dev_(dev), address_(address) {}

LocatorI2c::~LocatorI2c() {
    close_bus();
}

bool LocatorI2c::open_bus() {
    if (fd_ >= 0) return true;
    // UNVERIFIED ON HW: standard i2c-dev slave setup.
    fd_ = ::open(dev_.c_str(), O_RDWR);
    if (fd_ < 0) {
        std::printf("[locator] cannot open %s\n", dev_.c_str());
        return false;
    }
    if (::ioctl(fd_, I2C_SLAVE, address_) < 0) {
        std::printf("[locator] I2C_SLAVE ioctl failed for 0x%02X\n", address_);
        ::close(fd_);
        fd_ = -1;
        return false;
    }
    return true;
}

void LocatorI2c::close_bus() {
    if (fd_ >= 0) {
        ::close(fd_);
        fd_ = -1;
    }
}

void LocatorI2c::reopen() {
    close_bus();
    open_bus();
}

// rcj Locator::readAngle/readStrength pattern: write the register address,
// then read 1 byte. Uses plain write/read (no SMBus smbus2 dependency), which
// is what the Arduino Wire flow does too.
bool LocatorI2c::read_register(uint8_t reg, uint8_t& out) {
    if (!open_bus()) {
        failures_++;
        return false;
    }
    ssize_t w = ::write(fd_, &reg, 1);
    if (w != 1) {
        failures_++;
        // A busy/absent chip must not wedge the control loop: the Ball's
        // stale-value policy handles the failure (PROMPT_ROBOT §7).
        return false;
    }
    ssize_t r = ::read(fd_, &out, 1);
    if (r != 1) {
        failures_++;
        return false;
    }
    return true;
}

}  // namespace robot_core
