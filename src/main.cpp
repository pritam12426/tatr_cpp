#include "cli.hpp"
#include "config.hpp"
#include "log.hpp"
#include "project_config.hpp"
#include "server.hpp"

#include <cstdlib>
#include <iostream>

int main(int argc, char *argv[])
{
	// ── Parse CLI ─────────────────────────────────────────────────────────────

	Cli cli(MAIN_BINARY, MAIN_BINARY " " PROJECT_VERSION);
	build_cli(cli);

	try {
		cli.root.parse_args(argc, argv);
	} catch (const std::exception &err) {
		std::cerr << err.what() << '\n' << cli.root;
		return 1;
	}

	// ── Load config (file → CLI overrides) ───────────────────────────────────

	auto config = load_config();

	// --config flag merges an additional file on top.
	if (auto v = cli.root.present("--config"))
		merge_config_file(config, *v);

	// Subcommand flags take final precedence.
	apply_subcommands(cli, config);

	// Global logging flags override config.
	if (auto v = cli.root.present("--log-level"))
		config["logging"]["level"] = *v;
	if (auto v = cli.root.present("--log-file"))
		config["logging"]["file"] = *v;
	if (cli.root.is_used("--log-no-timestamp"))
		config["logging"]["no_timestamp"] = true;
	if (auto v = cli.root.present("--pandoc-binary-path"))
		config["pandoc"]["binary_path"] = *v;

	// ── Configure logger ──────────────────────────────────────────────────────

	const std::string level = config.value("/logging/level"_json_pointer, "info");
	if      (level == "debug") g_logger.set_level(Logger::Level::DBG);
	else if (level == "warn")  g_logger.set_level(Logger::Level::WARN);
	else if (level == "error") g_logger.set_level(Logger::Level::ERROR);
	else                       g_logger.set_level(Logger::Level::INFO);

	if (config.value("/logging/no_timestamp"_json_pointer, false))
		g_logger.set_show_timestamp(false);

	// ── Run ───────────────────────────────────────────────────────────────────

	run_server(config);
	return 0;
}
