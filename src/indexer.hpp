#pragma once

#include <filesystem>
#include <nlohmann/json.hpp>
#include <string>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

// ── Types ────────────────────────────────────────────────────────────────────

struct FileInfo {
	fs::path           path;  // as discovered (relative or absolute)
	fs::path           path_absolute;
	uintmax_t          size            = 0;
	fs::file_time_type last_write_time = {};
	fs::perms          permissions     = fs::perms::none;
};

// Serialise to JSON so Crow routes can return it directly.
nlohmann::json to_json(const FileInfo &fi);

// Keyed by the path used during the walk (relative when root is ".").
struct Snapshot {
	std::unordered_map<fs::path, FileInfo> files;
};

// ── Builder ──────────────────────────────────────────────────────────────────

struct IndexerConfig {
	fs::path                 root       = ".";
	int                      max_depth  = 1;
	bool                     hidden     = false;
	std::vector<std::string> extensions = { "md", "markdown" };
	std::vector<std::string> ignore     = { ".git", "node_modules", "target", ".cache" };
};

IndexerConfig indexer_config_from_json(const nlohmann::json &cfg);

// Walks the filesystem and returns a fresh snapshot.
Snapshot build_snapshot(const IndexerConfig &cfg);
