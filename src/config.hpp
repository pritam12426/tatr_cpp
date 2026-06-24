#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

// Returns the user-specific config directory:
//   $XDG_CONFIG_HOME/tatr  or  ~/.config/tatr
fs::path config_dir();

// Loads config with built-in defaults, then merges the user config file
// (XDG location first, then etc/config.json as fallback).
nlohmann::json load_config();

// Merges an additional config file into an existing config (e.g. --config flag).
// Exits the process on parse error.
void merge_config_file(nlohmann::json &cfg, const std::string &path);
