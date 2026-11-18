// pi_core/atomic_file.cpp — see atomic_file.hpp
#include "atomic_file.hpp"

#include <cstdio>
#include <string>

#ifdef _WIN32
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#ifndef O_DIRECTORY
#define O_DIRECTORY 0
#endif
#endif

namespace pi_core {

bool atomic_write(const std::string& path, const std::string& data) {
    const std::string tmp = path + ".tmp";
    // Open with O_TRUNC and write everything; flush to disk before renaming.
#ifdef _WIN32
    FILE* f = nullptr;
    if (fopen_s(&f, tmp.c_str(), "wb") != 0) return false;
#else
    FILE* f = std::fopen(tmp.c_str(), "wb");
#endif
    if (!f) return false;
    const size_t n = data.size();
    if (n > 0 && std::fwrite(data.data(), 1, n, f) != n) {
        std::fclose(f);
        std::remove(tmp.c_str());
        return false;
    }
    // Flush userspace buffers, then force the blocks to stable storage.
    if (std::fflush(f) != 0) {
        std::fclose(f);
        std::remove(tmp.c_str());
        return false;
    }
#ifdef _WIN32
    _commit(_fileno(f));  // best effort
#else
    fsync(fileno(f));     // best effort; some filesystems (tmpfs) may fail, ignore
#endif
    std::fclose(f);

#ifdef _WIN32
    // Windows rename() does not replace an existing destination (POSIX does,
    // atomically). Windows is dev/test-only; the Pi keeps the atomic path.
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(path.c_str());
        if (std::rename(tmp.c_str(), path.c_str()) != 0) {
            std::remove(tmp.c_str());
            return false;
        }
    }
#else
    if (std::rename(tmp.c_str(), path.c_str()) != 0) {
        std::remove(tmp.c_str());
        return false;
    }
    // Persist the directory entry too (rename durability on ext4).
    const int dfd = ::open(path.c_str(), O_RDONLY | O_DIRECTORY);
    if (dfd >= 0) {
        ::fsync(dfd);
        ::close(dfd);
    }
#endif
    return true;
}

bool read_file(const std::string& path, std::string& out) {
    FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return false;
    out.clear();
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        out.append(buf, n);
    }
    const bool ok = std::ferror(f) == 0;
    std::fclose(f);
    return ok;
}

}  // namespace pi_core
