#pragma once

#include <filesystem>
#include <optional>
#include <shared_mutex>
#include <unordered_map>

namespace fs = std::filesystem;

// ── RenderCache ───────────────────────────────────────────────────────────────
//
// Thread-safe in-memory cache that maps a source Markdown path to the last
// rendered HTML file and the mtime at render time.
//
// Lookup logic (used by /render):
//   1. Find the entry for `md_path`.
//   2. Compare the stored mtime with the file's current mtime.
//   3. Hit  → return the cached html_path (caller reads it from disk).
//   4. Miss → caller runs pandoc, then calls store() with the new paths.
//
// The actual HTML is stored on disk under /tmp/tatr/; the cache only holds
// the path to it.  This avoids keeping potentially large HTML strings in RAM.

struct CacheEntry {
	fs::file_time_type mtime;     // mtime of the source .md at render time
	fs::path           html_path; // path to the rendered .html on disk
};

class RenderCache {
public:
	// Returns the cached html_path if `md_path` is cached AND its current
	// on-disk mtime still matches the stored mtime.  Returns nullopt otherwise.
	std::optional<fs::path> lookup(const fs::path &md_path) const;

	// Store a new entry (or overwrite an existing one).
	// `mtime` should be the mtime you read just before calling pandoc.
	void store(const fs::path &md_path,
	           fs::file_time_type mtime,
	           const fs::path &html_path);

	// Remove every entry — called by the file watcher on any change so the
	// next request re-renders from scratch.
	void invalidate_all();

	// Remove the single entry for `md_path` — called when the watcher sees
	// that one specific file was modified.
	void invalidate(const fs::path &md_path);

	std::size_t size() const;

private:
	mutable std::shared_mutex                          mutex_;
	std::unordered_map<fs::path, CacheEntry>           entries_;
};
