#pragma once

#include <filesystem>
#include <functional>
#include <string>
#include <thread>
#include <atomic>
#include <vector>

namespace fs = std::filesystem;

// ── Watcher ───────────────────────────────────────────────────────────────────
//
// Watches a directory tree for file changes and fires a callback.
//
// Platform support:
//   Linux  — inotify (kernel event-driven, no polling)
//   macOS  — kqueue  (kernel event-driven, no polling)
//   other  — polling fallback (stat every `poll_interval_ms` milliseconds)
//
// The callback is invoked from the watcher's background thread.
// It receives the path that changed (relative to root).
// Callers must ensure any shared state touched in the callback is thread-safe.
//
// Usage:
//   Watcher w(root, extensions_to_watch, [](const fs::path &changed) {
//       // rebuild snapshot, clear cache, …
//   });
//   w.start();
//   // … server runs …
//   w.stop();   // blocks until the background thread exits

using WatchCallback = std::function<void(const fs::path &changed_path)>;

class Watcher {
public:
	Watcher(fs::path                    root,
	        std::vector<std::string>    extensions,   // without dot, e.g. {"md","markdown"}
	        WatchCallback               on_change,
	        unsigned                    poll_interval_ms = 500);

	~Watcher();

	// Not copyable or movable (owns a thread and platform fd).
	Watcher(const Watcher &)            = delete;
	Watcher &operator=(const Watcher &) = delete;

	void start();  // spawns background thread; no-op if already running
	void stop();   // signals thread to exit and joins; no-op if not running

private:
	fs::path                 root_;
	std::vector<std::string> extensions_;
	WatchCallback            on_change_;
	unsigned                 poll_interval_ms_;

	std::thread              thread_;
	std::atomic<bool>        running_{false};

	void run();

	// Polling fallback — always compiled so platform backends can fall back to
	// it (e.g. when inotify_init1 or kqueue fails at runtime).
	void run_poll();

#if defined(__linux__)
	void run_inotify();
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
	void run_kqueue();
#endif
};
