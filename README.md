
# LGX: High-Performance Multi-Source Log Viewer

## 1. Vision & Goals

The goal of this project is to build a **modern, high-performance log viewer for Linux (and eventually cross-platform)** that scales from single multi-million-line log files to **live, remote, and structured log sources** such as Docker, Loki, and other streaming backends.

The application should feel closer to an **IDE for logs** than a traditional text viewer:

* Fast, responsive UI regardless of log size
* Multiple windows and tabs
* Strong support for structured logs (JSON)
* Extensible log source architecture
* Direct support for log-streams from Docker, Systemd, Android (for connected devices).
* Correct parsing and coloring for common log formats. 

Basically a log-viewer for lazy developers and devops people.

## 2. Features

LGX is built to handle both ordinary log files and live log streams without
switching mental models.

Interesting features already implemented include:

* Open multiple logs at once in tabs.
* Open local files, pipe streams, Docker container logs, and Android `logcat`
  streams.
* Treat streams like normal logs: they can be followed, navigated, filtered,
  marked, and reopened from recent history.
* Auto-detect and colorize common log formats including generic text logs,
  Logfault-style logs, and Logcat.
* Per-source format override when auto-detection is not enough.
* Fast navigation to first/last line, jump up or down by 10%, and jump directly
  to the next or previous warning or error.
* Go to an exact line number.
* Optional follow mode for live logs.
* Optional line wrapping for long entries.
* Adjustable log zoom from the UI, mouse, and keyboard.
* Selection-aware copy support for both single lines and multi-line selections.
* Per-line actions including mark/unmark, copy, and expanded line view.
* Filter views and marked views that can be opened as horizontal or vertical
  splits alongside the primary log view.
* Recent-file and recent-stream menus for quickly restoring previous sessions.
* Session-oriented UI details such as remembered per-source wrapping and scanner
  selection.

## 3. Architecture

LGX is designed around the idea that **streams should behave like files** as far
as possible.

### Streams as files

Live sources such as pipe commands, Docker logs, and Android logcat are exposed
through `StreamSource`. A stream provider only has to produce bytes. LGX then
spools those bytes into a temporary file and delegates indexing, paging, and
line fetches to the same `FileSource` pipeline used for ordinary files.

That gives streamed logs the same behavior as file-backed logs:

* the same scanners
* the same paging and caching
* the same navigation model
* the same selection and copy behavior
* the same follow-mode semantics

In practice, this keeps the application architecture simpler and avoids building
one feature set for files and another for live sources.

### Fast initial scanning

The first pass over a source is optimized for startup speed. Log scanners expose
fast line-level classification methods so LGX can quickly build a lightweight
index with offsets, line boundaries, and log-level metadata before deeper work
is needed. That means the UI can become useful quickly even for large files.

The scanners also support incremental refresh. When a file grows or a stream
appends new bytes, LGX extends the existing source state instead of rescanning
the whole log from scratch.

### Memory-conscious paging and LRU cache

LGX does not keep the full text of every opened source resident in memory.
`LogSource` stores stable metadata such as page descriptors, line counts, and
per-page indexing data, while the actual loaded page payloads live in a shared
process-wide `GlobalPageCache`.

That cache is:

* keyed by `{source_id, page_index}`
* shared across all log sources
* protected against duplicate in-flight page loads
* trimmed with an LRU policy

Pages that are actively referenced stay alive, while older unreferenced pages
can be evicted. This keeps large logs usable without turning every open tab into
a full in-memory copy of the source.

## 4. How to Use

### Opening logs

Use the main menus to open the source you want:

* `File -> Open` opens a regular file.
* `Sources -> Open Pipe Stream` runs a command such as `journalctl -f` and opens
  its output as a live source.
* `Sources -> Docker -> Open Running Containers` opens one or more container
  logs.
* `Sources -> Logcat -> Open Devices` opens one or more Android logcat streams.
* `File -> Recent` and `Sources -> Recent` reopen previously used files and
  streams.

### Main view controls

The main toolbar focuses on the active log tab:

* toggle follow mode
* toggle line wrapping
* jump to first or last line
* jump up or down by 10%
* jump to previous or next warning
* jump to previous or next error

The status bar shows the current source, line count, current line, active
format, file size, ingestion rate, and zoom percentage.

### Menus and context menus

Useful menu paths:

* `View -> Follow -> Enabled` toggles follow mode.
* `View -> Log Lines -> Wrap long lines` toggles wrapping.
* `View -> Zoom` sets a fixed zoom percentage.
* `View -> Format` overrides the scanner/format for the current log.
* `View -> Goto Line` jumps directly to a line number.
* `Windows -> Add Filter View` opens a filtered split view.
* `Windows -> Add Marked View` opens a split view containing only marked lines.

Interesting context menus inside a log view:

* right-click a line to `Copy Line`, `Copy Selection`, or `Show`
* long-press a line to open the same expanded line popup as `Show`
* click the gutter marker to mark or unmark a line

### Keyboard and mouse shortcuts

Current shortcuts and gestures:

* `f` toggles follow mode.
* `Ctrl+L` opens `Goto Line`.
* `Ctrl+F` opens a horizontal filter split.
* `Ctrl+Shift+F` opens a vertical filter split.
* `Ctrl+M` opens a horizontal marked split.
* `Ctrl+Shift+M` opens a vertical marked split.
* `Up` and `Down` scroll by line.
* `PgUp` and `PgDown` scroll by page.
* `Left` and `Right` scroll horizontally when wrapping is off.
* `Ctrl+Left` jumps to the start of the selected or current line.
* `Ctrl+Right` jumps to the end of the selected or current line.
* `Ctrl+Mouse Wheel` changes log zoom.
* plain mouse wheel scrolls the log view.
* dragging the vertical or horizontal scrollbar works as expected for manual
  navigation.

Behavior worth knowing:

* scrolling upward manually disables follow mode
* wrapping disables horizontal scrolling because the view becomes line-wrapped
* `Copy` works on the current selection from the active log view

## Status:

**Under initial development**
