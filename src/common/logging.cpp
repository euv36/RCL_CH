#include "logging.hpp"

#include <chrono>
#include <ctime>
#include <iomanip>
#include <iostream>
#include <sstream>

namespace {
std::string timestamp() {
    const auto now = std::chrono::system_clock::now();
    const auto time = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &time);
#else
    localtime_r(&time, &tm);
#endif
    std::ostringstream out;
    out << std::put_time(&tm, "%H:%M:%S");
    return out.str();
}

void emit(const char* level, const std::string& message, std::ostream& stream) {
    stream << "[" << timestamp() << "] [" << level << "] " << message << std::endl;
}
}

void log_info(const std::string& message) { emit("INFO", message, std::cout); }
void log_warn(const std::string& message) { emit("WARN", message, std::cerr); }
void log_error(const std::string& message) { emit("ERROR", message, std::cerr); }
