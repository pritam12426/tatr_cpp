#include "pandoc.hpp"
#include "log.hpp"

#include <nlohmann/json.hpp>
#include "subprocess.h"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

using json = nlohmann::json;

// ── Binary resolution ─────────────────────────────────────────────────────────

std::string resolve_pandoc_binary(const std::string &override_path)
{
	// 1. Explicit override from config.
	if (!override_path.empty()) {
		std::error_code ec;
		if (fs::is_regular_file(override_path, ec))
			return override_path;
		// Config pointed at something that doesn't exist — hard error.
		LOG_ERROR("pandoc.binary_path is set to '", override_path,
		          "' but that file does not exist.");
		std::exit(1);
	}

	// 2. Walk every directory in $PATH looking for "pandoc".
	const char *path_env = std::getenv("PATH");
	if (!path_env) {
		LOG_ERROR("$PATH is not set; cannot locate pandoc.");
		std::exit(1);
	}

	const std::string path_str(path_env);
	std::size_t start = 0;
	while (start < path_str.size()) {
		const std::size_t colon = path_str.find(':', start);
		const std::string dir   = path_str.substr(
		    start, colon == std::string::npos ? std::string::npos : colon - start);
		start = (colon == std::string::npos) ? path_str.size() : colon + 1;

		if (dir.empty()) continue;

		fs::path candidate = fs::path(dir) / "pandoc";
		std::error_code ec;
		if (fs::is_regular_file(candidate, ec)) {
			LOG_DEBUG("found pandoc at: ", candidate.string());
			return candidate.string();
		}
	}

	LOG_ERROR("pandoc not found in $PATH and pandoc.binary_path is not set.\n"
	          "       Install pandoc (https://pandoc.org/installing.html) or set\n"
	          "       pandoc.binary_path in your config.");
	std::exit(1);
}

// ── Session directory ─────────────────────────────────────────────────────────

fs::path make_session_dir(const std::string &session_id)
{
	fs::path dir = fs::path("/tmp") / "tatr" / session_id;
	std::error_code ec;
	fs::create_directories(dir, ec);
	if (ec)
		LOG_ERROR("failed to create session dir '", dir.string(), "': ", ec.message());
	return dir;
}

// ── Theme flags ───────────────────────────────────────────────────────────────

std::vector<std::string> theme_flags(const json &config)
{
	std::vector<std::string> flags;

	if (!config.contains("pandoc") || !config["pandoc"].is_object())
		return flags;
	const auto &pandoc = config["pandoc"];

	if (!pandoc.contains("themes") || !pandoc["themes"].is_object())
		return flags;
	const auto &themes = pandoc["themes"];

	// null or missing theme → render with no extra flags (pandoc's own defaults).
	if (!config.contains("theme") || config["theme"].is_null())
		return flags;
	const std::string theme = config["theme"].get<std::string>();
	if (theme.empty())
		return flags;

	if (!themes.contains(theme) || !themes[theme].is_array()) {
		LOG_WARN("theme '", theme, "' not found in pandoc.themes; rendering without theme flags.");
		return flags;
	}

	for (const auto &item : themes[theme]) {
		if (!item.is_string()) {
			LOG_WARN("pandoc.themes.", theme, " contains a non-string entry; skipping.");
			continue;
		}
		flags.push_back(item.get<std::string>());
	}
	return flags;
}

// ── Renderer ──────────────────────────────────────────────────────────────────

fs::path render_markdown(
    const std::string              &pandoc_bin,
    const fs::path                 &md_path,
    const fs::path                 &session_dir,
    const std::vector<std::string> &extra_flags)
{
	const fs::path out_path = session_dir / (md_path.stem().string() + ".html");

	const std::string in_str  = md_path.string();
	const std::string out_str = out_path.string();

	// Build a flat argv: pandoc <in> -o <out> [theme flags...]
	// subprocess.h expects a null-terminated const char*[].
	std::vector<std::string> argv_storage = { pandoc_bin, in_str, "-o", out_str };
	for (const auto &f : extra_flags)
		argv_storage.push_back(f);

	// If none of the theme flags included --standalone, add it as a safety net
	// so we always get a full HTML document back.
	bool has_standalone = false;
	for (const auto &f : extra_flags)
		if (f == "--standalone") { has_standalone = true; break; }
	if (!has_standalone)
		argv_storage.push_back("--standalone");

	std::vector<const char *> argv;
	argv.reserve(argv_storage.size() + 1);
	for (const auto &s : argv_storage)
		argv.push_back(s.c_str());
	argv.push_back(nullptr);

	LOG_DEBUG("running: ", [&]() {
		std::string s;
		for (const auto *a : argv) { if (a) { s += a; s += ' '; } }
		return s;
	}());

	subprocess_s proc{};
	const int flags = subprocess_option_no_window
	                | subprocess_option_inherit_environment;

	if (subprocess_create(argv.data(), flags, &proc) != 0)
		throw std::runtime_error(
		    std::string("subprocess_create failed for pandoc: ") + std::strerror(errno));

	int exit_code = -1;
	const int rc  = subprocess_join(&proc, &exit_code);
	subprocess_destroy(&proc);

	if (rc != 0)
		throw std::runtime_error("subprocess_join failed (rc=" + std::to_string(rc) + ")");

	if (exit_code != 0)
		throw std::runtime_error(
		    "pandoc exited with code " + std::to_string(exit_code) + " for input: " + in_str);

	LOG_DEBUG("pandoc rendered: ", in_str, " → ", out_str);
	return out_path;
}
