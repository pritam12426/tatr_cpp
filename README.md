# Tatr CPP

Fast, local-first Markdown note browsing without Electron. Built with C++20, Pandoc, and Crow.

## Quick Start

```sh
brew install zlib openssl crow pandoc
make all
./tatr
# → http://127.0.0.1:7878
```

## Usage

```
tatr [--log-level LEVEL] [--log-file FILE] [--log-no-timestamp]
     [--pandoc-binary-path PATH] [--config FILE]
     {cache,indexer,server,theme,ui,watch}
```

Global flags:

| Flag | Description |
|---|---|
| `--config` | Path to an extra config file to merge |
| `--log-level` | Log level: `debug`, `info`, `warn`, `error` |
| `--log-file` | Write logs to a file instead of stderr |
| `--log-no-timestamp` | Suppress timestamps in log output |
| `--pandoc-binary-path` | Custom path to the pandoc binary |

Subcommands configure the corresponding config section, e.g. `tatr server --port 9090`.

## Build

| Command | Binary | Flags |
|---|---|---|
| `make all` | `./tatr` | `-O3` (release) |
| `make debug -B O_DEBUG=1` | `./tatr` | `-g3 -DDEBUG` |
| `make clean` | — | Removes `build/` and `tatr` |

Dependencies: `zlib`, `openssl`, `libcrypto`, Crow (`crow.h`), Pandoc (runtime).
All resolved via `pkg-config`. Vendored: `argparse`, `subprocess.h`.

## Routes

| Method | Path | Description |
|---|---|---|
| `GET` | `/` | Runtime config as JSON |
| `GET` | `/rpc` | Indexed file list as JSON array |
| `POST` | `/rpc/refresh` | Re-index + clear cache |
| `GET` | `/render?path=<relative-path>` | Render a Markdown file to HTML |

The frontend (`fround_end/`) is an SPA served by Crow — browse, search, and preview your notes in the browser.

## Config

Load order (later wins):

1. Hardcoded defaults in `src/config.cpp`
2. `$XDG_CONFIG_HOME/tatr/config.json` or `~/.config/tatr/config.json`
3. `/etc/tatr/config.json` (system-wide fallback)
4. `--config <file>` flag
5. Subcommand flags (e.g. `tatr server --port 8080`)

### Config file keys

```json
{
  "theme": "light",
  "indexer": {
    "depth": 3,
    "root_directory": "~/Documents",
    "ignore": [".git", "node_modules"],
    "include_hidden": false,
    "extensions": ["md"],
    "sort": { "field": "name", "order": "asc" }
  },
  "server": {
    "host": "127.0.0.1",
    "port": 7878,
    "auto_open_browser": true,
    "browser": null
  },
  "pandoc": {
    "binary_path": "",
    "themes": {}
  },
  "watch": {
    "enabled": true,
    "polling": false,
    "debounce_ms": 500
  },
  "cache": {
    "enabled": true,
    "directory": "~/.cache/tatr",
    "max_size_mb": 512,
    "rendered_html": true,
    "search_index": true
  },
  "logging": {
    "level": "info",
    "file": null,
    "no_timestamp": false
  },
  "ui": {
    "default_page_size": 50,
    "show_file_icons": true,
    "show_hidden_metadata": false,
    "default_sort": "name"
  }
}
```

The `pandoc.themes` object maps theme names to arrays of pandoc CLI flags.
Define your own themes in your user config (the hardcoded defaults leave it empty).

## Architecture

```
src/
├── main.cpp         Entrypoint — CLI parse → config → logger → run_server
├── cli.cpp          argparse subcommands per config section
├── config.cpp       XDG → /etc/tatr → hardcoded default merge
├── indexer.cpp      Recursive directory walk → Snapshot (path → FileInfo)
├── pandoc.cpp       Subprocess wrapper for pandoc (render, theme flags, binary resolution)
├── render_cache.cpp Mtime-based cache of rendered HTML paths
├── server.cpp       Crow HTTP server with /, /rpc, /render routes
├── watcher.cpp      inotify (Linux) / kqueue (macOS) / polling fallback
└── log.cpp          Thread-safe logger with level, timestamp, color
```

The frontend lives in `fround_end/` (HTML, JS, CSS) and communicates with the server via `fetch()`.

## License

MIT
