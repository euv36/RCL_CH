#pragma once

#include <filesystem>
#include <optional>
#include <string>
#include <nlohmann/json.hpp>

// Reads a JSON object. Returns nullopt and a non-empty warning for a missing,
// unreadable, empty, malformed, or non-object file. Never throws.
std::optional<nlohmann::json> read_json_file(const std::filesystem::path& path, std::string* warning);
