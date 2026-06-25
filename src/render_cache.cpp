#include "render_cache.hpp"
#include "log.hpp"

#include <system_error>

// ── RenderCache ───────────────────────────────────────────────────────────────

std::optional<fs::path> RenderCache::lookup(const fs::path &md_path) const
{
	std::shared_lock lock(mutex_);

	const auto it = entries_.find(md_path);
	if (it == entries_.end())
		return std::nullopt;

	// Re-read the current mtime from disk to detect changes since last render.
	std::error_code ec;
	const auto current_mtime = fs::last_write_time(md_path, ec);
	if (ec) {
		// File disappeared or unreadable — treat as a miss; let the caller
		// decide what to do with a missing file.
		return std::nullopt;
	}

	if (current_mtime != it->second.mtime) {
		// File was modified since we cached it.
		LOG_DEBUG("cache miss (mtime changed): ", md_path.string());
		return std::nullopt;
	}

	// Cached HTML must still exist on disk.
	if (!fs::exists(it->second.html_path, ec)) {
		LOG_DEBUG("cache miss (html gone): ", md_path.string());
		return std::nullopt;
	}

	LOG_DEBUG("cache hit: ", md_path.string());
	return it->second.html_path;
}

void RenderCache::store(const fs::path     &md_path,
                        fs::file_time_type  mtime,
                        const fs::path     &html_path)
{
	std::unique_lock lock(mutex_);
	entries_.insert_or_assign(md_path, CacheEntry{mtime, html_path});
	LOG_DEBUG("cache store: ", md_path.string(), " → ", html_path.string());
}

void RenderCache::invalidate_all()
{
	std::unique_lock lock(mutex_);
	const std::size_t n = entries_.size();
	entries_.clear();
	LOG_INFO("render cache cleared (", n, " entries)");
}

void RenderCache::invalidate(const fs::path &md_path)
{
	std::unique_lock lock(mutex_);
	const std::size_t removed = entries_.erase(md_path);
	if (removed)
		LOG_DEBUG("cache invalidated: ", md_path.string());
}

std::size_t RenderCache::size() const
{
	std::shared_lock lock(mutex_);
	return entries_.size();
}
