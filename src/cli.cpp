#include "cli.hpp"

#include <string>
#include <vector>

using json = nlohmann::json;

// ── Build ────────────────────────────────────────────────────────────────────

void build_cli(Cli &cli)
{
	cli.root.add_argument("--config").help("Path to a config file to merge");
	cli.root.add_argument("--log-level").help("Log level: debug | info | warn | error");
	cli.root.add_argument("--log-file").help("Write logs to this file");
	cli.root.add_argument("--log-no-timestamp").help("Suppress timestamps in log output").flag();
	cli.root.add_argument("--pandoc-binary-path").help("Path to pandoc binary");


	auto add = [&](const std::string &n) -> argparse::ArgumentParser & {
		return cli.add_sub(n);
	};

	// indexer
	{
		auto &s = add("indexer").add_description("Configure the file indexer: depth, root, ignored paths");
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
		auto &s = add("server").add_description("Configure the HTTP server: host, port, browser auto-open");
		s.add_argument("--host").help("Bind address");
		s.add_argument("--port").help("Port").scan<'i', int>();
		s.add_argument("--auto-open-browser").help("Open browser on start").flag();
		s.add_argument("--browser").help("Browser command override");
	}
	// watch
	{
		auto &s = add("watch").add_description("Configure file watching: enable, polling, debounce");
		s.add_argument("--enabled").help("Enable file watching").flag();
		s.add_argument("--polling").help("Use polling instead of inotify").flag();
		s.add_argument("--debounce-ms").help("Debounce delay in ms").scan<'i', int>();
	}
	// cache
	{
		auto &s = add("cache").add_description("Configure render and search cache: directory, size limits");
		s.add_argument("--enabled").help("Enable caching").flag();
		s.add_argument("--directory").help("Cache directory path");
		s.add_argument("--max-size-mb").help("Cache size cap in MB").scan<'i', int>();
		s.add_argument("--rendered-html").help("Cache rendered HTML").flag();
		s.add_argument("--search-index").help("Cache search index").flag();
	}
	// ui
	{
		auto &s = add("ui").add_description("Configure the web UI defaults: page size, icons, sort");
		s.add_argument("--page-size").help("Items per page").scan<'i', int>();
		s.add_argument("--file-icons").help("Show file icons").flag();
		s.add_argument("--hidden-metadata").help("Show hidden metadata").flag();
		s.add_argument("--default-sort").help("Default sort field");
	}
	// theme
	{
		auto &s = add("theme").add_description("Select the pandoc theme: light, dark, or custom");
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
	         "indexer", "server", "watch", "cache",
	         "ui", "theme"
	     })
		apply_one(cli, name, cfg);
}
