#include "server.hpp"
#include "indexer.hpp"
#include "pandoc.hpp"
#include "log.hpp"
#include "project_config.hpp"

#include "crow.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>

// ── Crow log bridge ───────────────────────────────────────────────────────────

// Routes Crow's internal log messages through our Logger so everything
// ends up in one place / one format.
struct CrowLogHandler : crow::ILogHandler {
	void log(const std::string &message, crow::LogLevel level) override
	{
		static constexpr Logger::Level lmap[] = {
			Logger::Level::DBG,    // crow::LogLevel::Debug
			Logger::Level::INFO,   // crow::LogLevel::Info
			Logger::Level::WARN,   // crow::LogLevel::Warning
			Logger::Level::ERROR,  // crow::LogLevel::Error
			Logger::Level::ERROR,  // crow::LogLevel::Critical
		};
		const int idx = static_cast<int>(level);
		const Logger::Level lvl = (idx >= 0 && idx < 5) ? lmap[idx] : Logger::Level::INFO;
		g_logger.write_log(lvl, nullptr, 0, nullptr, true, message);
	}
};

// ── Session ID ────────────────────────────────────────────────────────────────

// Simple monotonic counter; fine for a local single-user server.
static std::string new_session_id()
{
	static std::atomic<uint64_t> counter{0};
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	    std::chrono::system_clock::now().time_since_epoch()).count();
	return std::to_string(ms) + "_" + std::to_string(++counter);
}

// ── Route helpers ─────────────────────────────────────────────────────────────

// Read a file from disk into a string.  Returns empty string + logs on error.
static std::string read_file(const std::filesystem::path &p)
{
	std::ifstream f(p, std::ios::binary);
	if (!f) {
		LOG_ERROR("cannot open file: ", p.string());
		return {};
	}
	std::ostringstream ss;
	ss << f.rdbuf();
	return ss.str();
}

// ── Server ────────────────────────────────────────────────────────────────────

void run_server(const nlohmann::json &config)
{
	// ── Crow log integration ──────────────────────────────────────────────────

	static CrowLogHandler crow_log_handler;
	crow::logger::setHandler(&crow_log_handler);

	const std::string log_level = config.value("/logging/level"_json_pointer, "info");
	if      (log_level == "debug") crow::logger::setLogLevel(crow::LogLevel::Debug);
	else if (log_level == "warn")  crow::logger::setLogLevel(crow::LogLevel::Warning);
	else if (log_level == "error") crow::logger::setLogLevel(crow::LogLevel::Error);
	else                           crow::logger::setLogLevel(crow::LogLevel::Info);

	// ── Pre-build indexer snapshot ────────────────────────────────────────────

	const IndexerConfig idx_cfg = indexer_config_from_json(config);
	Snapshot snap = build_snapshot(idx_cfg);

	// ── Pandoc config ─────────────────────────────────────────────────────────
	// resolve_pandoc_binary() exits the process if pandoc cannot be found.

	// value() returns the default ("") if the key is absent or null,
	// avoiding the operator[] assertion that fires on null JSON values.
	const std::string pandoc_override =
	    config.value("/pandoc/binary_path"_json_pointer, std::string{});
	const std::string pandoc_bin = resolve_pandoc_binary(pandoc_override);

	// ── Routes ────────────────────────────────────────────────────────────────

	crow::SimpleApp app;
	app.loglevel(crow::LogLevel::Warning);

	// GET /
	// Returns the full runtime configuration as JSON.
	CROW_ROUTE(app, "/")
	([&config]() {
		crow::response res(200, config.dump(2));
		res.set_header("Content-Type", "application/json");
		res.add_header("Access-Control-Allow-Origin", "*");
		return res;
	});

	// GET /rpc
	// Returns an array of FileInfo objects for every indexed file.
	CROW_ROUTE(app, "/rpc")
	([&snap]() {
		nlohmann::json arr = nlohmann::json::array();
		for (const auto &[_, fi] : snap.files)
			arr.push_back(to_json(fi));
		crow::response res(200, arr.dump(2));
		res.set_header("Content-Type", "application/json");
		res.add_header("Access-Control-Allow-Origin", "*");
		return res;
	});

	// POST /rpc/refresh
	// Re-walks the filesystem and refreshes the in-memory snapshot.
	CROW_ROUTE(app, "/rpc/refresh").methods(crow::HTTPMethod::POST)
	([&snap, &idx_cfg]() {
		snap = build_snapshot(idx_cfg);
		LOG_INFO("snapshot refreshed (", snap.files.size(), " files)");
		nlohmann::json res = {{"ok", true}, {"count", snap.files.size()}};
		return crow::response(200, res.dump());
	});

	// GET /render?path=<relative-md-path>
	// Renders the requested Markdown file via pandoc and returns the HTML.
	// Theme flags are read from config and passed straight to pandoc argv.
	CROW_ROUTE(app, "/render")
	([&snap, &pandoc_bin, &config](const crow::request &req) {
		const auto path_param = req.url_params.get("path");
		if (!path_param)
			return crow::response(400, "missing ?path= parameter");

		const std::filesystem::path md_path(path_param);

		if (snap.files.find(md_path) == snap.files.end())
			return crow::response(404, "file not indexed: " + md_path.string());

		const std::string session_id = new_session_id();
		const auto session_dir = make_session_dir(session_id);
		const auto flags = theme_flags(config);

		try {
			const auto html_path = render_markdown(pandoc_bin, md_path, session_dir, flags);
			const std::string html = read_file(html_path);
			if (html.empty())
				return crow::response(500, "rendered file is empty or unreadable");
			crow::response res(200, html);
			res.set_header("Content-Type", "text/html; charset=utf-8");
			return res;
		} catch (const std::exception &e) {
			LOG_ERROR("render failed: ", e.what());
			return crow::response(500, std::string("pandoc rendering failed: ") + e.what());
		}
	});

	// ── Start ─────────────────────────────────────────────────────────────────

	const std::string host = config.value("/server/host"_json_pointer, "127.0.0.1");
	const int         port = config.value("/server/port"_json_pointer, 7878);

	LOG_INFO(MAIN_BINARY " v" PROJECT_VERSION " — listening on ", host, ":", port);

	app.bindaddr(host).port(port).multithreaded().run();
}
