#include "server.hpp"
#include "indexer.hpp"
#include "pandoc.hpp"
#include "render_cache.hpp"
#include "watcher.hpp"
#include "log.hpp"
#include "project_config.hpp"

#include "crow.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

// ── Crow log bridge ───────────────────────────────────────────────────────────

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

static std::string new_session_id()
{
	static std::atomic<uint64_t> counter{0};
	const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
	    std::chrono::system_clock::now().time_since_epoch()).count();
	return std::to_string(ms) + "_" + std::to_string(++counter);
}

// ── Route helpers ─────────────────────────────────────────────────────────────

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

	// ── Indexer ───────────────────────────────────────────────────────────────

	const IndexerConfig idx_cfg = indexer_config_from_json(config);

	// Snapshot is shared between routes and the watcher callback — protect
	// with a mutex since the watcher runs on a different thread.
	std::mutex   snap_mutex;
	Snapshot     snap = build_snapshot(idx_cfg);

	// ── Render cache ──────────────────────────────────────────────────────────

	RenderCache cache;

	// ── Pandoc ────────────────────────────────────────────────────────────────

	const std::string pandoc_override =
	    config.value("/pandoc/binary_path"_json_pointer, std::string{});
	const std::string pandoc_bin = resolve_pandoc_binary(pandoc_override);

	// ── File watcher ──────────────────────────────────────────────────────────

	const bool watch_enabled = config.value("/watch/enabled"_json_pointer, false);
	const unsigned debounce  = config.value("/watch/debounce_ms"_json_pointer, 500u);

	std::vector<std::string> watch_exts;
	if (config.contains("indexer") && config["indexer"].contains("extensions"))
		watch_exts = config["indexer"]["extensions"].get<std::vector<std::string>>();
	else
		watch_exts = {"md", "markdown"};

	auto on_change = [&](const fs::path &changed) {
		LOG_INFO("change detected: ", changed.string());
		{
			std::lock_guard lock(snap_mutex);
			snap = build_snapshot(idx_cfg);
		}
		std::error_code ec;
		if (fs::is_regular_file(changed, ec))
			cache.invalidate(changed);
		else
			cache.invalidate_all();
	};

	Watcher watcher(idx_cfg.root, watch_exts, on_change, debounce);
	if (watch_enabled)
		watcher.start();

	// ── Routes ────────────────────────────────────────────────────────────────

	crow::SimpleApp app;

	// GET /
	// Full runtime config as JSON.
	CROW_ROUTE(app, "/")
	([&config]() {
		crow::response res(200, config.dump(2));
		res.set_header("Content-Type", "application/json");
		res.add_header("Access-Control-Allow-Origin", "*");
		return res;
	});

	// GET /rpc
	// Every indexed file as a JSON array of FileInfo.
	CROW_ROUTE(app, "/rpc")
	([&snap, &snap_mutex]() {
		nlohmann::json arr = nlohmann::json::array();
		{
			std::lock_guard lock(snap_mutex);
			for (const auto &[_, fi] : snap.files)
				arr.push_back(to_json(fi));
		}
		crow::response res(200, arr.dump(2));
		res.set_header("Content-Type", "application/json");
		res.add_header("Access-Control-Allow-Origin", "*");
		return res;
	});

	// POST /rpc/refresh
	// Force a full re-index and cache clear, regardless of watcher state.
	CROW_ROUTE(app, "/rpc/refresh").methods(crow::HTTPMethod::POST)
	([&snap, &snap_mutex, &idx_cfg, &cache]() {
		{
			std::lock_guard lock(snap_mutex);
			snap = build_snapshot(idx_cfg);
		}
		cache.invalidate_all();
		LOG_INFO("manual refresh: snapshot rebuilt, render cache cleared");
		nlohmann::json body = {{"ok", true}, {"count", snap.files.size()}};
		crow::response res(200, body.dump());
		res.set_header("Content-Type", "application/json");
		res.add_header("Access-Control-Allow-Origin", "*");
		return res;
	});

	// GET /render?path=<relative-md-path>
	// 1. Check render cache (mtime-based).
	// 2. Miss → run pandoc, store result in cache.
	// 3. Return the HTML.
	CROW_ROUTE(app, "/render")
	([&snap, &snap_mutex, &pandoc_bin, &config, &cache](const crow::request &req) {
		const auto path_param = req.url_params.get("path");
		if (!path_param)
			return crow::response(400, "missing ?path= parameter");

		const fs::path md_path(path_param);

		fs::path abs_path;
		{
			std::lock_guard lock(snap_mutex);
			const auto it = snap.files.find(md_path);
			if (it == snap.files.end())
				return crow::response(404, "file not indexed: " + md_path.string());
			abs_path = it->second.path_absolute;
		}

		// ── Cache lookup ──────────────────────────────────────────────────────
		if (const auto cached = cache.lookup(md_path)) {
			const std::string html = read_file(*cached);
			if (!html.empty()) {
				crow::response res(200, html);
				res.set_header("Content-Type", "text/html; charset=utf-8");
				res.set_header("X-Cache", "HIT");
				res.add_header("Access-Control-Allow-Origin", "*");
				return res;
			}
			cache.invalidate(md_path);
		}

		// ── Cache miss — render ───────────────────────────────────────────────

		std::error_code ec;
		const auto mtime = fs::last_write_time(abs_path, ec);
		if (ec)
			return crow::response(500, "cannot stat source file: " + abs_path.string());

		const auto session_dir = make_session_dir(new_session_id());
		const auto flags       = theme_flags(config);

		try {
			const auto html_path = render_markdown(pandoc_bin, abs_path, session_dir, flags);
			cache.store(md_path, mtime, html_path);

			const std::string html = read_file(html_path);
			if (html.empty())
				return crow::response(500, "rendered file is empty or unreadable");

			crow::response res(200, html);
			res.set_header("Content-Type", "text/html; charset=utf-8");
			res.set_header("X-Cache", "MISS");
			res.add_header("Access-Control-Allow-Origin", "*");
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
	if (watch_enabled)
		LOG_INFO("file watcher active (debounce ", debounce, " ms)");
	else
		LOG_INFO("file watcher disabled (set watch.enabled=true to enable)");
	LOG_INFO("render cache enabled");

	app.bindaddr(host).port(port).multithreaded().run();

	watcher.stop();

	// ── Cleanup temp session directory (like rm -rfv) ────────────────────────
	{
		const fs::path tmp("/tmp/tatr");
		std::error_code ec2;
		if (!fs::exists(tmp, ec2))
			return;

		// Collect entries bottom-up so children are deleted before parents.
		std::vector<fs::path> all;
		const fs::recursive_directory_iterator end_it;
		for (auto it = fs::recursive_directory_iterator(tmp, ec2);
		     it != end_it; it.increment(ec2))
			all.push_back(it->path());

		LOG_DEBUG("Cleaning up:");
		for (auto it = all.rbegin(); it != all.rend(); ++it) {
			fs::remove(*it, ec2);
			LOG_DEBUG("removed ", it->string());
		}
		fs::remove(tmp, ec2);
		LOG_DEBUG("removed ", tmp.string());
	}
}
