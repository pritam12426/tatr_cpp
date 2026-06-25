# Tatr CPP — Agent Guide

## Build & Run

```sh
make all          # production build → ./tatr (O3)
make debug -B O_DEBUG=1   # debug build (-g3 -DDEBUG)
make clean        # rm build/ and tatr binary
./tatr            # starts server on 127.0.0.1:7878
```

No CMake. Simple Makefile at root.

## Dependencies (pkg-config)

- `zlib`, `openssl`, `libcrypto` — install via brew (macOS) or apt (Linux).
- Crow (`crow.h`) — header-only, install via brew or from system.
- Vendored in `third_party/`: `argparse/argparse.hpp`, `subprocess.h`.

## Architecture

Single C++20 binary. Entrypoint: `src/main.cpp`.

| Module | File | Role |
|---|---|---|
| CLI | `src/cli.cpp` | argparse subcommands per config section (indexer, server, pandoc, watch, cache, features, search, logging, ui, theme) |
| Config | `src/config.cpp` | XDG → `ect/config.json` fallback → `--config` merge → CLI override |
| Indexer | `src/indexer.cpp` | recursive directory walk → `Snapshot` (unordered_map of FileInfo) |
| Pandoc | `src/pandoc.cpp` | subprocess.h → pandoc, output in `/tmp/tatr/<session_id>/` |
| Render Cache | `src/render_cache.cpp` | thread-safe mtime-based cache of rendered HTML paths |
| Server | `src/server.cpp` | Crow HTTP server, routes: `GET /`, `GET /rpc`, `POST /rpc/refresh`, `GET /render?path=` |
| Watcher | `src/watcher.cpp` | inotify (Linux), kqueue (macOS/BSD), polling fallback |
| Logger | `src/log.cpp` | Global `g_logger`, macros: `LOG_DEBUG`, `LOG_INFO`, `LOG_WARN`, `LOG_ERROR`, `LOG_PERROR` |

### Routes

- `GET /` — full runtime config as JSON
- `GET /rpc` — indexed file list as JSON array
- `POST /rpc/refresh` — re-index + clear cache
- `GET /render?path=<relative-md-path>` — pandoc render → HTML

### Frontend

Static files in `fround_end/` served by Crow (embedded in routes).

## Config

Load order (later = higher precedence):
1. Hardcoded defaults in `src/config.cpp`
2. `$XDG_CONFIG_HOME/tatr/config.json` or `~/.config/tatr/config.json`
3. `ect/config.json` (bundled fallback)
4. `--config <file>` flag
5. Subcommand flags (e.g. `tatr server --port 8080`)

## Style & Code Conventions

- `#pragma once` for headers.
- Custom logger with variadic macros; no `std::cout`/`printf`.
- Namespace `fs = std::filesystem`.
- `nlohmann/json` with `_json_pointer` syntax.
- `.clang-tidy` at root but no CI enforces it.

## Known Issues

- `/tmp/tatr/` is never cleaned between runs.
- `AUTHER_NAME` typo in `project_config.hpp` (used for attribution).
- `PROJECT_VERSION = "1.1.0"` but `PROJECT_VERSION_MAJOR/MINOR/PATCH = 0/1/0` in `project_config.hpp`.
- No signal handling (SIGINT/SIGTERM).
- No test framework or tests.




Hello this pritam I have did some shit here
