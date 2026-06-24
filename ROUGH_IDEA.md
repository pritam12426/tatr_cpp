# Rough Idea

> Running brain-dump / design scratchpad.
> Nothing here is final. Update freely as the project evolves.

---

## What is tatr?

A fast, local-first Markdown note browser.
No Electron. No cloud. No node_modules weighing 300 MB.
Just a small C++ binary that:

1. Indexes your Markdown files.
2. Renders them to HTML on-demand via Pandoc.
3. Serves the result to your browser over a local HTTP server (Crow).

---

## Current State (v1.1.0)

- [x] Config loading (XDG + `ect/config.json` fallback, CLI overrides)
- [x] Argument parser with per-section subcommands
- [x] Logger with color, timestamps, source-location support
- [x] Recursive directory indexer → `Snapshot` (depth, extension, ignore, hidden filter)
- [x] Pandoc IPC via `subprocess.h` → `/tmp/tatr/<session>/` output
- [x] Crow HTTP server wired up
- [x] `GET /` → full JSON config
- [x] `GET /rpc` → file index as JSON array
- [x] `POST /rpc/refresh` → re-index live
- [x] `GET /render?path=` → run pandoc, return HTML

---

## Planned Routes

```
GET  /                        full runtime config (JSON)
GET  /rpc                     list all indexed files (FileInfo[])
POST /rpc/refresh             re-walk filesystem, rebuild snapshot
GET  /render?path=<rel>       render one .md → HTML via pandoc
GET  /raw?path=<rel>          serve the raw .md source
```

---

## Data Flow

```
CLI args
    │
    ▼
load_config()          ← XDG / ect/config.json / --config flag
    │
    ▼
apply_subcommands()    ← override any key via subcommand flags
    │
    ▼
run_server(config)
    ├── build_snapshot()          walks root_directory
    │       └── FileInfo map      path, size, mtime, perms
    │
    └── Crow routes
            ├── /rpc              serialise snapshot → JSON
            ├── /render           subprocess.h → pandoc → HTML
```

---

## Pandoc / IPC Design

Each render request gets its own session directory:

```
/tmp/tatr/
└── <timestamp>_<counter>/
    ├── note.html
    └── another-note.html
```

- Session ID = `<unix_ms>_<monotonic_counter>` (see `pandoc.cpp`).
- Rendered files are **not cleaned up automatically** yet.
  Plan: sweep `/tmp/tatr/` on startup, or add a `TTL` config key.
- Pandoc flags in use: `--standalone` (full HTML doc with `<head>`).
  Future: `--css`, `--template`, `--lua-filter` for custom themes.
- Pandoc binary is resolved from config `pandoc.binary_path`, else `PATH`.

---

## FileInfo / Snapshot

```cpp
struct FileInfo {
    fs::path           path;
    fs::path           path_absolute;
    uintmax_t          size;
    fs::file_time_type last_write_time;
    fs::perms          permissions;
};

using Snapshot = unordered_map<fs::path, FileInfo>;
```

The snapshot lives in memory for the lifetime of the server.
`POST /rpc/refresh` rebuilds it without restarting.

Future ideas:
- Persist snapshot to `cache/search_index` on disk.
- Store parsed front-matter (title, tags, date) inside `FileInfo`.
- Diff old vs new snapshot to detect renames / deletes for watcher.

---

## File Watcher (todo)

Config key: `watch.enabled`.

Plan:
- Linux: inotify via a small wrapper, or `inotify-cpp`.
- macOS: FSEvents or `kqueue`.
- Fallback: polling loop (`watch.polling = true`, `watch.debounce_ms`).

On change event → call `build_snapshot()` → optionally push
an SSE (Server-Sent Event) to any open browser tabs so they
auto-reload without a manual refresh.

---

## Backlinks (todo)

During indexing, scan each file for `[[WikiLink]]` and
`[text](relative/path.md)` patterns.
Build a reverse map: `target → list of files that reference it`.
Expose via `GET /backlinks?path=<rel>`.

---

## Caching (todo)

Config keys: `cache.*`.

Two things to cache:
1. `rendered_html` — skip re-running pandoc if the `.md` mtime hasn't changed.
   Key: `<abs_path>:<mtime>` → `<session_dir>/file.html`.
2. `search_index` — serialise the inverted index to `cache.directory`
   so startup is instant on second run.

Cache eviction: LRU by file count, or simple `max_size_mb` check.

---

## Browser Auto-open (todo)

Config key: `server.auto_open_browser`.

```cpp
// pseudo
if (config["server"]["auto_open_browser"]) {
    std::string url = "http://" + host + ":" + std::to_string(port);
    subprocess_run({"xdg-open", url});   // Linux
    // subprocess_run({"open", url});    // macOS
}
```

Respect `server.browser` override if set.

---

## Themes / Pandoc CSS (todo)

Config key: `pandoc.themes`.

Pass a `--css` flag to pandoc pointing at a bundled stylesheet.
Ship `light.css` and `dark.css` under `ect/themes/`.
Let the config select which one is injected:

```
GET /render?path=note.md   →   pandoc … --css /ect/themes/light.css
```

---

## Potential Third-party Additions

| Library | Why |
|---|---|
| `inotify-cpp` | Clean C++17 wrapper around inotify for the watcher |
| `cmark-gfm` | In-process Markdown → HTML (skip pandoc for simple files) |
| `re2` or `std::regex` | Front-matter / wikilink parsing |
| `fts` / `sqlite` FTS5 | Persistent full-text search index |

---

## Known Rough Edges / TODOs

- [ ] `/tmp/tatr/` is never cleaned up between runs.
- [ ] `CrowLogHandler` is a raw `new` — make it a `static` local or `unique_ptr`.
- [ ] `project_config.hpp` has `PROJECT_VERSION_MAJOR/MINOR/PATCH = 0/1/0`
      but `PROJECT_VERSION = "1.1.0"` — these should agree.
- [ ] `AUTHER_NAME` typo in `project_config.hpp` (should be `AUTHOR`).
- [ ] `config.json` has `pandoc.themes` with empty `"some"` key — remove or document.
- [ ] No graceful shutdown signal handling yet (SIGINT / SIGTERM).
- [ ] `GET /` returns the config including any sensitive overrides — consider
      a `/rpc/status` route with a safe subset instead.
