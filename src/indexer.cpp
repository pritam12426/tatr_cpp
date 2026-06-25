#include "indexer.hpp"
#include "log.hpp"

#include <algorithm>
#include <chrono>
#include <system_error>

using json = nlohmann::json;

// ── Serialisation ────────────────────────────────────────────────────────────

json to_json(const FileInfo &fi)
{
	// Convert file_time_type to a Unix timestamp (seconds since epoch).
	// C++20 clock_cast not universally available yet, so use duration arithmetic.
	const auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
	    fi.last_write_time - fs::file_time_type::clock::now()
	    + std::chrono::system_clock::now());
	const auto unix_sec =
	    std::chrono::duration_cast<std::chrono::seconds>(sctp.time_since_epoch()).count();

	return {
		{"file_name",       fi.file_name},
		{"path_relative",   fi.path_relative.string()},
		{"path_absolute",   fi.path_absolute.string()},
		{"size",            fi.size},
		{"last_write_time", unix_sec},
		{"permissions",     static_cast<int>(fi.permissions)},
		{"is_readable",     fi.is_readable},
	};
}

// ── Config helpers ───────────────────────────────────────────────────────────

IndexerConfig indexer_config_from_json(const json &cfg)
{
	IndexerConfig ic;
	const auto &idx = cfg.at("indexer");

	ic.root      = idx.value("root_directory", ".");
	ic.max_depth = idx.value("depth", 2);
	ic.hidden    = idx.value("include_hidden", false);

	if (idx.contains("extensions"))
		ic.extensions = idx["extensions"].get<std::vector<std::string>>();
	if (idx.contains("ignore"))
		ic.ignore = idx["ignore"].get<std::vector<std::string>>();

	return ic;
}

// ── Snapshot builder ─────────────────────────────────────────────────────────

// `abs_root` is the canonical absolute path of the walk root so we can
// compute a clean relative path regardless of what the iterator returns.
static FileInfo make_file_info(const fs::path &p, const fs::path &abs_root)
{
	std::error_code ec;
	FileInfo fi;
	fi.path_absolute = fs::weakly_canonical(p, ec);
	fi.path_relative = fs::relative(fi.path_absolute, abs_root, ec);
	fi.file_name     = fi.path_relative.filename().string();
	fi.size          = fs::file_size(p, ec);
	if (ec) fi.size  = 0;
	fi.last_write_time = fs::last_write_time(p, ec);
	fi.permissions     = fs::status(p, ec).permissions();
	// Readable if the owner-read bit is set. A proper access(2) check would
	// be more accurate but this is sufficient for a local single-user tool.
	fi.is_readable     = (fi.permissions & fs::perms::owner_read) != fs::perms::none;
	return fi;
}

Snapshot build_snapshot(const IndexerConfig &cfg)
{
	Snapshot snap;
	const auto root = fs::weakly_canonical(cfg.root);

	// Pre-build a set of ignore stems for O(1) lookup.
	auto is_ignored = [&](const fs::path &p) -> bool {
		const std::string stem = p.filename().string();
		for (const auto &ign : cfg.ignore)
			if (stem == ign) return true;
		if (!cfg.hidden && !stem.empty() && stem[0] == '.') return true;
		return false;
	};

	// Pre-build an extension set (stored without the leading dot).
	auto ext_ok = [&](const fs::path &p) -> bool {
		std::string ext = p.extension().string();
		if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
		return std::find(cfg.extensions.begin(), cfg.extensions.end(), ext)
		       != cfg.extensions.end();
	};

	LOG_DEBUG("building snapshot from: ", root.string());

	std::error_code ec;
	for (fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec), end;
	     it != end;
	     it.increment(ec))
	{
		if (ec) {
			LOG_WARN("directory iteration error at '", it->path().string(), "': ", ec.message());
			ec.clear();
			continue;
		}

		if (is_ignored(it->path())) {
			it.disable_recursion_pending();
			continue;
		}
		if (it.depth() >= cfg.max_depth)
			it.disable_recursion_pending();

		if (!it->is_regular_file(ec)) continue;
		if (!ext_ok(it->path()))      continue;

		auto fi = make_file_info(it->path(), root);
		snap.files.emplace(fi.path_relative, std::move(fi));
	}

	LOG_INFO("snapshot built: ", snap.files.size(), " files indexed from ", root.string());
	return snap;
}
