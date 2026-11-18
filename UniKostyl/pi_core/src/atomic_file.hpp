// pi_core/atomic_file.hpp — crash/power-loss-safe file writing (tmp + rename).
// Contract files (saved_gates.json, camera_settings.json, ...) may be written
// mid-match; the Pi can lose power at any moment, so a partial file must never
// appear under the real name.
#pragma once

#include <string>
#include <vector>

namespace pi_core {

// Writes data to `path` atomically: writes `path + ".tmp"`, fsyncs, renames
// onto `path` (atomic on POSIX and on Windows since Windows 10 for the same
// directory). Returns false and leaves any existing file untouched on failure.
bool atomic_write(const std::string& path, const std::string& data);

// Convenience for reading a whole small file; returns false if unreadable.
bool read_file(const std::string& path, std::string& out);

}  // namespace pi_core
