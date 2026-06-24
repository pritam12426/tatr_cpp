#include "config.hpp"

#include <cstdlib>
#include <fstream>
#include <iostream>

using json = nlohmann::json;

fs::path config_dir()
{
	if (const char *env = std::getenv("XDG_CONFIG_HOME"))
		return fs::path(env) / "tatr";
	return fs::path(std::getenv("HOME")) / ".config" / "tatr";
}

json load_config()
{
	// These defaults must stay in sync with ect/config.json.
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
			{"binary_path", nullptr},
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
		{"search", {
			{"title_only", false},
			{"content_search", true},
			{"case_sensitive", false},
			{"fuzzy_search", true}
		}},
		{"logging", {
			{"level", "info"},
			{"file", nullptr}
		}},
		{"ui", {
			{"default_page_size", 50},
			{"show_file_icons", true},
			{"show_hidden_metadata", false},
			{"default_sort", "name"}
		}}
	};

	// Try XDG path first, then the bundled etc/ fallback.
	std::ifstream f(config_dir() / "config.json");
	if (!f.is_open())
		f.open(fs::path("ect") / "config.json");

	if (f.is_open()) {
		try {
			defaults.merge_patch(json::parse(f));
		} catch (const json::parse_error &e) {
			std::cerr << "warning: failed to parse config: " << e.what() << '\n';
		}
	}
	return defaults;
}

void merge_config_file(json &cfg, const std::string &path)
{
	std::ifstream f(path);
	if (!f.is_open()) {
		std::cerr << "error: cannot open --config: " << path << '\n';
		std::exit(1);
	}
	try {
		cfg.merge_patch(json::parse(f));
	} catch (const json::parse_error &e) {
		std::cerr << "error: failed to parse --config: " << e.what() << '\n';
		std::exit(1);
	}
}
