// pi_core/serial_link.hpp — Pi -> Arduino serial output, /dev/serial0 @115200,
// contract per PROMPT_EN.md section 4:
//   G<id>,<angle>,<area>\n   (angle changed by >=3 deg)
//   G<id>,-1,0\n             (gate lost, exactly once)
//   HB\n                     (every 0.5 s)
// Formatting/state decisions live in gate_logic (pure); this class only moves
// bytes. --print-serial replaces the device with stdout for smoke tests
// without an Arduino.
#pragma once

#include <memory>
#include <string>

namespace pi_core {

class SerialSink {
public:
    virtual ~SerialSink() = default;

    // Writes payload bytes; returns false on failure (caller keeps the
    // decision state so the same line is retried next frame, like Python).
    virtual bool write(const std::string& payload) = 0;
    virtual const char* name() const = 0;
};

// Real device. Open failure is not fatal: the caller logs a WARN and runs
// without Arduino (same as Python SerialLink with pyserial missing/unavailable).
std::unique_ptr<SerialSink> open_serial_device(const std::string& port,
                                               int baud = 115200);

// --print-serial sink: exact serial lines to stdout (unbuffered, flushed).
std::unique_ptr<SerialSink> open_stdout_sink();

}  // namespace pi_core
