#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>
#include <nlohmann/json.hpp>

namespace fs = std::filesystem;

// ── Types ────────────────────────────────────────────────────────────────────

struct FileInfo {
	std::string        file_name;      // e.g. "README.md"
	fs::path           path_relative;  // e.g. "docs/README.md"
	fs::path           path_absolute;  // e.g. "/Users/pritam/.../docs/README.md"
	uintmax_t          size            = 0;
	fs::file_time_type last_write_time = {};
	fs::perms          permissions     = fs::perms::none;
	bool               is_readable     = false;  // current process can read this file
};

// Serialise to JSON so Crow routes can return it directly.
nlohmann::json to_json(const FileInfo &fi);

// Keyed by the path used during the walk (relative when root is ".").
struct Snapshot {
	std::unordered_map<fs::path, FileInfo> files;
};

// ── Builder ──────────────────────────────────────────────────────────────────

struct IndexerConfig {
	fs::path                  root       = ".";
	int                       max_depth  = 2;
	bool                      hidden     = false;
	std::vector<std::string>  extensions = {"md", "markdown"};
	std::vector<std::string>  ignore     = {".git", "node_modules", "target", ".cache"};
};

IndexerConfig indexer_config_from_json(const nlohmann::json &cfg);

// Walks the filesystem and returns a fresh snapshot.
Snapshot build_snapshot(const IndexerConfig &cfg);
