// pi_core/log.cpp — see log.hpp
#include "log.hpp"

#include <cstdio>
#include <ctime>

namespace pi_core {

namespace {
bool g_verbose = false;
}

void set_verbose(bool verbose) { g_verbose = verbose; }
bool is_verbose() { return g_verbose; }

void log_msg(const std::string& msg, const char* level) {
    // Local time HH:MM:SS, same as Python time.strftime('%H:%M:%S').
    char ts[16];
    std::time_t t = std::time(nullptr);
    std::tm tmv{};
#ifdef _WIN32
    localtime_s(&tmv, &t);
#else
    localtime_r(&t, &tmv);
#endif
    std::snprintf(ts, sizeof(ts), "%02d:%02d:%02d", tmv.tm_hour, tmv.tm_min,
                  tmv.tm_sec);
    std::fprintf(stderr, "[%s] [%s] %s\n", ts, level, msg.c_str());
    std::fflush(stderr);  // Python uses flush=True; keep the same behaviour
}

void log_debug(const std::string& msg) {
    if (g_verbose) log_msg(msg, "DEBUG");
}

}  // namespace pi_core
