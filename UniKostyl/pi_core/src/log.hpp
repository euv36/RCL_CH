// pi_core/log.hpp — logging, format-identical to rpi_elp_cam.py:
//   [HH:MM:SS] [LEVEL] message
// Logs go to stderr; stdout is reserved for data (--print-serial echo of the
// exact serial lines), so smoke tests can do:
//   ./pi_core ... --print-serial 2>run.log | grep -E '^G[0-9]+,'
#pragma once

#include <string>

namespace pi_core {

// Levels used: "INFO", "WARN", "ERROR", "DEBUG" (same strings as Python).
void log_msg(const std::string& msg, const char* level = "INFO");

void set_verbose(bool verbose);
bool is_verbose();

// debug() prints only when --verbose (same as Python debug()).
void log_debug(const std::string& msg);

}  // namespace pi_core
