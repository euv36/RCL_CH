#pragma once

#include <filesystem>
#include <string>
#include <string_view>

// Atomically replaces path with text by writing a sibling temp file and renaming it.
// Returns false and leaves any existing original file untouched on failure.
bool atomic_write_text(const std::filesystem::path& path, std::string_view text, std::string* error);
