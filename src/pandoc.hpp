#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <vector>

namespace fs = std::filesystem;

// ── Binary resolution ─────────────────────────────────────────────────────────

// Returns the absolute path to the pandoc executable.
//
// Resolution order:
//   1. `override_path` if non-empty (from config pandoc.binary_path).
//   2. Each directory in $PATH, searching for "pandoc".
//
// Exits the process with an error message if pandoc cannot be found.
std::string resolve_pandoc_binary(const std::string &override_path = "");

// ── Session directory ─────────────────────────────────────────────────────────

// Creates (if needed) /tmp/tatr/<session_id>/ and returns the path.
// Logs an error but does not exit if creation fails.
fs::path make_session_dir(const std::string &session_id);

// ── Theme flags ───────────────────────────────────────────────────────────────

// Reads pandoc.themes.<theme_name> from the JSON config and returns it as a
// flat vector of strings ready to be appended to the pandoc argv.
//
// The config value must be a JSON array of strings, e.g.:
//   ["--template", "~/.config/tatr/theme/light/template.html", "--standalone"]
//
// Returns an empty vector if the theme or the themes object is absent.
std::vector<std::string> theme_flags(const nlohmann::json &config);

// ── Renderer ──────────────────────────────────────────────────────────────────

// Converts `md_path` to `<session_dir>/<stem>.html` by spawning pandoc via
// subprocess.h.  `extra_flags` are appended to the pandoc argv verbatim
// (pass the output of theme_flags() here).
//
// Returns the output path on success, throws std::runtime_error on failure.
fs::path render_markdown(const std::string              &pandoc_bin,
                         const fs::path                 &md_path,
                         const fs::path                 &session_dir,
                         const std::vector<std::string> &extra_flags = {});
