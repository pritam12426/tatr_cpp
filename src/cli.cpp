#include "cli.hpp"

#include <string>
#include <vector>

using json = nlohmann::json;

// ── Build ────────────────────────────────────────────────────────────────────

void build_cli(Cli &cli)
{
	cli.root.add_argument("--config").help("Path to a config file to merge");

	auto add = [&](const std::string &n) -> argparse::ArgumentParser & {
		return cli.add_sub(n);
	};

	// indexer
	{
		auto &s = add("indexer");
		s.add_argument("--depth").help("Recursion depth").scan<'i', int>();
		s.add_argument("--root-directory").help("Root directory");
		s.add_argument("--ignore").help("Comma-separated ignore list");
		s.add_argument("--include-hidden").help("Include hidden files").flag();
		s.add_argument("--extensions").help("Comma-separated extensions");
		s.add_argument("--sort-field").help("Sort field: name | date | size");
		s.add_argument("--sort-order").help("Sort order: asc | desc");
	}
	// server
	{
		auto &s = add("server");
		s.add_argument("--host").help("Bind address");
		s.add_argument("--port").help("Port").scan<'i', int>();
		s.add_argument("--auto-open-browser").help("Open browser on start").flag();
		s.add_argument("--browser").help("Browser command override");
	}
	// pandoc
	{
		auto &s = add("pandoc");
		s.add_argument("--binary-path").help("Path to pandoc binary");
	}
	// watch
	{
		auto &s = add("watch");
		s.add_argument("--enabled").help("Enable file watching").flag();
		s.add_argument("--polling").help("Use polling instead of inotify").flag();
		s.add_argument("--debounce-ms").help("Debounce delay in ms").scan<'i', int>();
	}
	// cache
	{
		auto &s = add("cache");
		s.add_argument("--enabled").help("Enable caching").flag();
		s.add_argument("--directory").help("Cache directory path");
		s.add_argument("--max-size-mb").help("Cache size cap in MB").scan<'i', int>();
		s.add_argument("--rendered-html").help("Cache rendered HTML").flag();
		s.add_argument("--search-index").help("Cache search index").flag();
	}
	// features
	{
		auto &s = add("features");
		s.add_argument("--wikilinks").help("Enable [[wikilinks]]").flag();
		s.add_argument("--backlinks").help("Enable backlinks panel").flag();
		s.add_argument("--tags").help("Enable tag index").flag();
		s.add_argument("--graph").help("Enable graph view").flag();
	}
	// search
	{
		auto &s = add("search");
		s.add_argument("--title-only").help("Search titles only").flag();
		s.add_argument("--content").help("Enable full-content search").flag();
		s.add_argument("--case-sensitive").help("Case-sensitive search").flag();
		s.add_argument("--fuzzy").help("Enable fuzzy matching").flag();
	}
	// logging
	{
		auto &s = add("logging");
		s.add_argument("--level").help("Log level: debug | info | warn | error");
		s.add_argument("--file").help("Write logs to this file");
		s.add_argument("--timestamp").help("Prepend timestamps").flag();
	}
	// ui
	{
		auto &s = add("ui");
		s.add_argument("--page-size").help("Items per page").scan<'i', int>();
		s.add_argument("--file-icons").help("Show file icons").flag();
		s.add_argument("--hidden-metadata").help("Show hidden metadata").flag();
		s.add_argument("--default-sort").help("Default sort field");
	}
	// theme
	{
		auto &s = add("theme");
		s.add_argument("theme").help("Theme name: light | dark");
	}
}

// ── Apply ────────────────────────────────────────────────────────────────────

static void apply_one(Cli &cli, const std::string &name, json &cfg)
{
	if (!cli.root.is_subcommand_used(name))
		return;

	auto &s = cli.root.at<argparse::ArgumentParser>(name);

	// "theme" is top-level; every other subcommand maps to cfg[name][key].
	if (name == "theme") {
		cfg["theme"] = s.get<std::string>("theme");
		return;
	}

	// Get a reference to the correct nested section so all helpers below
	// write into cfg["server"], cfg["indexer"], etc. — not the root object.
	json &sec = cfg[name];

	auto set_bool = [&](const char *key, const char *flag) {
		if (s.is_used(flag)) sec[key] = (s[flag] == true);
	};
	auto set_str = [&](const char *key, const char *flag) {
		if (auto v = s.present(flag)) sec[key] = *v;
	};
	auto set_int = [&](const char *key, const char *flag) {
		if (auto v = s.present<int>(flag)) sec[key] = *v;
	};
	auto split_csv = [&](const char *key, const char *flag) {
		auto v = s.present(flag);
		if (!v) return;
		std::vector<std::string> items;
		std::string sv = *v;
		for (size_t pos = 0, found; ; pos = found + 1) {
			found = sv.find(',', pos);
			items.push_back(sv.substr(pos, found == std::string::npos ? found : found - pos));
			if (found == std::string::npos) break;
		}
		sec[key] = items;
	};

	if (name == "indexer") {
		set_int( "depth",          "--depth");
		set_str( "root_directory", "--root-directory");
		set_bool("include_hidden", "--include-hidden");
		// sort is a nested object inside indexer — write directly.
		if (auto v = s.present("--sort-field")) sec["sort"]["field"] = *v;
		if (auto v = s.present("--sort-order")) sec["sort"]["order"] = *v;
		split_csv("ignore",     "--ignore");
		split_csv("extensions", "--extensions");
	} else if (name == "server") {
		set_str( "host",              "--host");
		set_int( "port",              "--port");
		set_bool("auto_open_browser", "--auto-open-browser");
		set_str( "browser",           "--browser");
	} else if (name == "pandoc") {
		set_str("binary_path", "--binary-path");
	} else if (name == "watch") {
		set_bool("enabled",     "--enabled");
		set_bool("polling",     "--polling");
		set_int( "debounce_ms", "--debounce-ms");
	} else if (name == "cache") {
		set_bool("enabled",       "--enabled");
		set_str( "directory",     "--directory");
		set_int( "max_size_mb",   "--max-size-mb");
		set_bool("rendered_html", "--rendered-html");
		set_bool("search_index",  "--search-index");
	} else if (name == "features") {
		set_bool("wikilinks", "--wikilinks");
		set_bool("backlinks", "--backlinks");
		set_bool("tags",      "--tags");
		set_bool("graph",     "--graph");
	} else if (name == "search") {
		set_bool("title_only",     "--title-only");
		set_bool("content_search", "--content");
		set_bool("case_sensitive", "--case-sensitive");
		set_bool("fuzzy_search",   "--fuzzy");
	} else if (name == "logging") {
		set_str("level", "--level");
		set_str("file",  "--file");
	} else if (name == "ui") {
		set_int( "default_page_size",    "--page-size");
		set_bool("show_file_icons",      "--file-icons");
		set_bool("show_hidden_metadata", "--hidden-metadata");
		set_str( "default_sort",         "--default-sort");
	}
}

void apply_subcommands(Cli &cli, json &cfg)
{
	for (const auto *name : {
	         "indexer", "server", "pandoc", "watch", "cache",
	         "features", "search", "logging", "ui", "theme"
	     })
		apply_one(cli, name, cfg);
}
