#include "config.hpp"
#include "log.hpp"

#include <cstdlib>
#include <fstream>

using json = nlohmann::json;

fs::path config_dir()
{
	if (const char *env = std::getenv("XDG_CONFIG_HOME"))
		return fs::path(env) / "tatr";
	return fs::path(std::getenv("HOME")) / ".config" / "tatr";
}

json load_config()
{
	// Themes are intentionally left as an empty object here; the user's
	// config file is the authoritative source for theme flag arrays.
	json defaults = {
		{"theme", "light"},
		{"indexer", {
			{"depth", 1},
			{"root_directory", "."},
			{"ignore", {".git", "node_modules", "target", ".cache"}},
			{"include_hidden", false},
			{"extensions", {"md", "markdown"}},
			{"sort", {{"field", "name"}, {"order", "asc"}}}
		}},
		{"server", {
			{"host", "127.0.0.1"},
			{"port", 7878},
			{"auto_open_browser", false},
			{"browser", nullptr}
		}},
		{"pandoc", {
			{"binary_path", ""},
			{"themes", json::object()}
		}},
		{"watch", {
			{"enabled", false},
			{"polling", false},
			{"debounce_ms", 500}
		}},
		{"cache", {
			{"enabled", true},
			{"directory", "~/.cache/tatr"},
			{"max_size_mb", 512},
			{"rendered_html", true},
			{"search_index", true}
		}},
		{"logging", {
			{"level", "info"},
			{"file", nullptr},
			{"no_timestamp", false}
		}},
		{"ui", {
			{"default_page_size", 50},
			{"show_file_icons", true},
			{"show_hidden_metadata", false},
			{"default_sort", "name"}
		}}
	};

	// Try user config first, then system-wide /etc/tatr/config.json,
	// then fall back to hardcoded defaults.
	std::ifstream f(config_dir() / "config.json");
	bool from_system = false;
	if (!f.is_open()) {
		f.open(fs::path("/etc/tatr") / "config.json");
		from_system = true;
	}

	if (f.is_open()) {
		if (from_system)
			LOG_INFO("loading config from /etc/tatr/config.json");
		else
			LOG_INFO("loading config from: ", (config_dir() / "config.json").string());
		try {
			defaults.merge_patch(json::parse(f));
		} catch (const json::parse_error &e) {
			LOG_ERROR("failed to parse config: ", e.what());
		}
	}
	return defaults;
}

void merge_config_file(json &cfg, const std::string &path)
{
	std::ifstream f(path);
	if (!f.is_open()) {
		LOG_ERROR("cannot open --config: ", path);
		std::exit(1);
	}
	try {
		LOG_INFO("merging config from: ", path);
		cfg.merge_patch(json::parse(f));
	} catch (const json::parse_error &e) {
		LOG_ERROR("failed to parse --config: ", e.what());
		std::exit(1);
	}
}
