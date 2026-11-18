#pragma once

#include <filesystem>
#include <string>

// Resolves a runtime contract file: local/<name> when it exists (or local/ is the writable seed directory), otherwise the repository root.
// Walking upward from the current working directory locates the repository by .git or CMakeLists.txt.
std::filesystem::path contract_path(const std::string& name);
