#include "atomic_file.hpp"

#include <cerrno>
#include <chrono>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <string>
#include <system_error>
#include <vector>

#if defined(_WIN32)
#include <io.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace {
void remove_stale_temps(const std::filesystem::path& path) {
    const auto parent = path.parent_path();
    const auto prefix = path.filename().string() + ".tmp.";
    std::error_code ec;
    for (const auto& entry : std::filesystem::directory_iterator(parent, ec)) {
        const auto name = entry.path().filename().string();
        if (name.rfind(prefix, 0) == 0) {
            std::filesystem::remove(entry.path(), ec);
        }
    }
}

std::filesystem::path make_temp_path(const std::filesystem::path& path) {
    static unsigned counter = 0;
    const auto nanos = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count();
    const auto suffix = std::to_string(counter++) + "." + std::to_string(nanos);
    return path.parent_path() / (path.filename().string() + ".tmp." + suffix);
}

bool write_all(int fd, const char* data, size_t size) {
    size_t written = 0;
    while (written < size) {
        const auto remaining = size - written;
#if defined(_WIN32)
        const int chunk = remaining > 0x7fffffff ? 0x7fffffff : static_cast<int>(remaining);
        const int rc = _write(fd, data + written, chunk);
#else
        const ssize_t rc = ::write(fd, data + written, remaining);
#endif
        if (rc < 0 && errno == EINTR) {
            continue;
        }
        if (rc <= 0) {
            return false;
        }
        written += static_cast<size_t>(rc);
    }
    return true;
}
}

// Атомарно записывает текст: сначала в временный файл рядом, затем rename().
// При ошибке временный файл удаляется, а original остаётся без изменений.
bool atomic_write_text(const std::filesystem::path& path, std::string_view text, std::string* error) {
    auto set_error = [error](const std::string& message) {
        if (error) {
            *error = message;
        }
        return false;
    };
    if (error) {
        error->clear();
    }

    const auto parent = path.parent_path().empty() ? std::filesystem::path(".") : path.parent_path();
    std::error_code ec;
    if (!std::filesystem::exists(parent, ec) || !std::filesystem::is_directory(parent, ec)) {
        return set_error("parent directory does not exist: " + parent.string());
    }

    remove_stale_temps(path);
    const auto temp = make_temp_path(path);

#if defined(_WIN32)
    const int fd = _wopen(temp.wstring().c_str(), _O_WRONLY | _O_CREAT | _O_TRUNC | _O_BINARY, 0600);
#else
    const int fd = ::open(temp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
#endif
    if (fd < 0) {
        return set_error("open temp failed: " + std::string(std::strerror(errno)));
    }

    bool ok = write_all(fd, text.data(), text.size());
    if (ok) {
#if defined(_WIN32)
        ok = _commit(fd) == 0;
#elif defined(__linux__)
        ok = ::fdatasync(fd) == 0;
#else
        ok = ::fsync(fd) == 0;
#endif
    }
#if defined(_WIN32)
    const int close_rc = _close(fd);
#else
    const int close_rc = ::close(fd);
#endif
    ok = ok && close_rc == 0;

    std::string rename_error;
    if (ok) {
#if defined(_WIN32)
        if (!MoveFileExW(temp.wstring().c_str(), path.wstring().c_str(),
                         MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            ok = false;
            rename_error = "MoveFileExW failed with error " + std::to_string(GetLastError());
        }
#else
        std::filesystem::rename(temp, path, ec);
        if (ec) {
            ok = false;
            rename_error = ec.message();
        }
#endif
    }

#if !defined(_WIN32)
    // Persist the directory entry so the rename survives a power loss.
    if (ok) {
        const int dir_fd = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY | O_CLOEXEC);
        if (dir_fd >= 0) {
            ::fsync(dir_fd);
            ::close(dir_fd);
        }
    }
#endif

    if (!ok) {
        std::filesystem::remove(temp, ec);
        return set_error(rename_error.empty() ? "atomic rename failed" : "atomic rename failed: " + rename_error);
    }
    if (error) {
        error->clear();
    }
    return true;
}
