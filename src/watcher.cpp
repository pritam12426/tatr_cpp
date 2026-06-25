#include "watcher.hpp"
#include "log.hpp"

#include <algorithm>
#include <chrono>
#include <system_error>
#include <unordered_map>

#if defined(__linux__)
#  include <sys/inotify.h>
#  include <sys/select.h>
#  include <unistd.h>
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
#  include <sys/event.h>
#  include <sys/time.h>
#  include <sys/types.h>
#  include <fcntl.h>
#  include <unistd.h>
#endif

// ── Helpers ───────────────────────────────────────────────────────────────────

static bool ext_matches(const fs::path              &p,
                        const std::vector<std::string> &exts)
{
	std::string ext = p.extension().string();
	if (!ext.empty() && ext[0] == '.') ext = ext.substr(1);
	return std::find(exts.begin(), exts.end(), ext) != exts.end();
}

// ── Watcher ───────────────────────────────────────────────────────────────────

Watcher::Watcher(fs::path                 root,
                 std::vector<std::string> extensions,
                 WatchCallback            on_change,
                 unsigned                 poll_interval_ms)
    : root_(std::move(root))
    , extensions_(std::move(extensions))
    , on_change_(std::move(on_change))
    , poll_interval_ms_(poll_interval_ms)
{}

Watcher::~Watcher()
{
	stop();
}

void Watcher::start()
{
	if (running_.exchange(true)) return;  // already running
	thread_ = std::thread(&Watcher::run, this);
	LOG_INFO("file watcher started on: ", root_.string());
}

void Watcher::stop()
{
	if (!running_.exchange(false)) return;  // already stopped
	if (thread_.joinable()) thread_.join();
	LOG_INFO("file watcher stopped");
}

void Watcher::run()
{
#if defined(__linux__)
	run_inotify();
#elif defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)
	run_kqueue();
#else
	run_poll();
#endif
}

// ── Linux — inotify ───────────────────────────────────────────────────────────
#if defined(__linux__)

void Watcher::run_inotify()
{
	const int ifd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (ifd < 0) {
		LOG_ERROR("inotify_init1 failed; falling back to polling");
		run_poll();
		return;
	}

	// Map watch descriptor → directory path so we can reconstruct the full
	// path when an event fires.
	std::unordered_map<int, fs::path> wd_to_dir;

	// Helper: add a watch on a single directory.
	auto add_watch = [&](const fs::path &dir) {
		const int wd = inotify_add_watch(
		    ifd, dir.c_str(),
		    IN_CLOSE_WRITE | IN_MOVED_TO | IN_MOVED_FROM |
		    IN_CREATE      | IN_DELETE   | IN_ONLYDIR);
		if (wd < 0)
			LOG_WARN("inotify_add_watch failed for ", dir.string());
		else
			wd_to_dir[wd] = dir;
	};

	// Watch root and every sub-directory up to a reasonable depth.
	add_watch(root_);
	std::error_code ec;
	for (fs::recursive_directory_iterator it(root_,
	         fs::directory_options::skip_permission_denied, ec), end;
	     it != end; it.increment(ec))
	{
		if (ec) { ec.clear(); continue; }
		if (it->is_directory(ec))
			add_watch(it->path());
	}

	// Event buffer — inotify events are variable-length.
	constexpr std::size_t BUF = 4096;
	alignas(struct inotify_event) char buf[BUF];

	while (running_.load()) {
		// Use select() with a timeout so we can check running_ periodically.
		fd_set fds;
		FD_ZERO(&fds);
		FD_SET(ifd, &fds);
		struct timeval tv{ 0, static_cast<suseconds_t>(poll_interval_ms_ * 1000) };

		const int sel = select(ifd + 1, &fds, nullptr, nullptr, &tv);
		if (sel < 0) break;
		if (sel == 0) continue;  // timeout — loop and recheck running_

		const ssize_t len = read(ifd, buf, BUF);
		if (len <= 0) continue;

		for (ssize_t i = 0; i < len; ) {
			const auto *ev = reinterpret_cast<const struct inotify_event *>(buf + i);
			i += sizeof(struct inotify_event) + ev->len;

			if (ev->len == 0) continue;
			const std::string name(ev->name);
			if (name.empty() || name[0] == '.') continue;  // hidden / temp

			const auto dir_it = wd_to_dir.find(ev->wd);
			if (dir_it == wd_to_dir.end()) continue;

			const fs::path full = dir_it->second / name;

			// If a new sub-directory was created, start watching it too.
			if ((ev->mask & IN_CREATE) && (ev->mask & IN_ISDIR))
				add_watch(full);

			if (ev->mask & IN_ISDIR) continue;  // only care about files

			if (!ext_matches(full, extensions_)) continue;

			const fs::path rel = fs::relative(full, root_, ec);
			on_change_(ec ? full : rel);
		}
	}

	// Clean up all watches.
	for (const auto &[wd, _] : wd_to_dir)
		inotify_rm_watch(ifd, wd);
	close(ifd);
}

#endif  // __linux__

// ── macOS / BSD — kqueue ──────────────────────────────────────────────────────
#if defined(__APPLE__) || defined(__FreeBSD__) || defined(__OpenBSD__) || defined(__NetBSD__)

void Watcher::run_kqueue()
{
	const int kq = kqueue();
	if (kq < 0) {
		LOG_ERROR("kqueue() failed; falling back to polling");
		run_poll();
		return;
	}

	// kqueue watches open file descriptors, not paths.
	// We open every .md file and every directory under root.
	// Map fd → path so we can report what changed.
	std::unordered_map<int, fs::path> fd_to_path;
	std::vector<struct kevent>        changes;

	auto add_fd = [&](const fs::path &p) {
		const int fd = open(p.c_str(), O_RDONLY | O_EVTONLY | O_CLOEXEC);
		if (fd < 0) return;
		fd_to_path[fd] = p;
		struct kevent ev{};
		EV_SET(&ev, static_cast<uintptr_t>(fd), EVFILT_VNODE,
		       EV_ADD | EV_CLEAR,
		       NOTE_WRITE | NOTE_RENAME | NOTE_DELETE | NOTE_ATTRIB,
		       0, nullptr);
		changes.push_back(ev);
	};

	// Watch root directory and all matching files.
	add_fd(root_);
	std::error_code ec;
	for (fs::recursive_directory_iterator it(root_,
	         fs::directory_options::skip_permission_denied, ec), end;
	     it != end; it.increment(ec))
	{
		if (ec) { ec.clear(); continue; }
		if (it->is_directory(ec) || ext_matches(it->path(), extensions_))
			add_fd(it->path());
	}

	// Register all at once.
	if (!changes.empty())
		kevent(kq, changes.data(), static_cast<int>(changes.size()),
		       nullptr, 0, nullptr);

	struct kevent events[32];

	while (running_.load()) {
		// Timeout so we can recheck running_.
		struct timespec ts{ 0, static_cast<long>(poll_interval_ms_) * 1'000'000L };
		const int n = kevent(kq, nullptr, 0, events, 32, &ts);
		if (n < 0) break;

		for (int i = 0; i < n; ++i) {
			const int fd = static_cast<int>(events[i].ident);
			const auto it = fd_to_path.find(fd);
			if (it == fd_to_path.end()) continue;

			const fs::path &full = it->second;

			if (fs::is_directory(full, ec)) {
				// A directory changed — new files may have appeared.
				// We fire with the directory path; server.cpp will refresh
				// the whole snapshot anyway.
				on_change_(fs::relative(full, root_, ec));
				continue;
			}

			if (!ext_matches(full, extensions_)) continue;

			const fs::path rel = fs::relative(full, root_, ec);
			on_change_(ec ? full : rel);
		}
	}

	for (const auto &[fd, _] : fd_to_path)
		close(fd);
	close(kq);
}

#endif  // __APPLE__ || BSD

// ── Polling fallback ──────────────────────────────────────────────────────────
// Always compiled. Used on unsupported platforms or as a runtime fallback
// when the preferred backend fails to initialise.

void Watcher::run_poll()
{
	using Clock = std::chrono::steady_clock;
	using MtimeMap = std::unordered_map<fs::path, fs::file_time_type>;

	// Build an initial snapshot of mtimes.
	MtimeMap last_mtimes;
	std::error_code ec;

	auto collect = [&]() {
		for (fs::recursive_directory_iterator it(root_,
		         fs::directory_options::skip_permission_denied, ec), end;
		     it != end; it.increment(ec))
		{
			if (ec) { ec.clear(); continue; }
			if (!it->is_regular_file(ec)) continue;
			if (!ext_matches(it->path(), extensions_)) continue;
			last_mtimes[it->path()] = fs::last_write_time(it->path(), ec);
		}
	};

	collect();

	while (running_.load()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(poll_interval_ms_));

		MtimeMap current;
		for (fs::recursive_directory_iterator it(root_,
		         fs::directory_options::skip_permission_denied, ec), end;
		     it != end; it.increment(ec))
		{
			if (ec) { ec.clear(); continue; }
			if (!it->is_regular_file(ec)) continue;
			if (!ext_matches(it->path(), extensions_)) continue;

			const fs::path &p     = it->path();
			const auto      mtime = fs::last_write_time(p, ec);
			current[p] = mtime;

			const auto prev = last_mtimes.find(p);
			if (prev == last_mtimes.end() || prev->second != mtime) {
				// New or modified file.
				const fs::path rel = fs::relative(p, root_, ec);
				on_change_(ec ? p : rel);
			}
		}

		// Detect deletions.
		for (const auto &[p, _] : last_mtimes) {
			if (current.find(p) == current.end()) {
				const fs::path rel = fs::relative(p, root_, ec);
				on_change_(ec ? p : rel);
			}
		}

		last_mtimes = std::move(current);
	}
}
