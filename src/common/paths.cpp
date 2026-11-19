#include "paths.hpp"

#include <system_error>

namespace {
std::filesystem::path find_repository_root() {
    std::error_code ec;
    auto current = std::filesystem::current_path(ec);
    if (ec) {
        return std::filesystem::current_path();
    }
    for (auto candidate = current; !candidate.empty(); candidate = candidate.parent_path()) {
        if (std::filesystem::exists(candidate / ".git", ec) || std::filesystem::exists(candidate / "CMakeLists.txt", ec)) {
            return candidate;
        }
        if (candidate == candidate.parent_path()) {
            break;
        }
    }
    return current;
}
}

// Ищет контрактный файл сначала в local/, затем в корне репозитория.
// Локальные файлы имеют приоритет, чтобы тесты не задевали эталонные данные.
std::filesystem::path contract_path(const std::string& name) {
    const auto root = find_repository_root();
    const auto local_path = root / "local" / name;
    std::error_code ec;
    if (std::filesystem::exists(local_path, ec) || std::filesystem::is_directory(root / "local", ec)) {
        return local_path;
    }
    return root / name;
}
