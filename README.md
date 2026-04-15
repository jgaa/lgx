# LGX: High-Performance Multi-Source Log Viewer

![Screen dump](images/lgx.jpg)

## 1. Vision & Goals

The goal of this project is to build a **modern, high-performance log viewer for Linux that scales from single files to multi-million-line logs and streaming sources**.

The application should feel closer to an **IDE for logs** than a traditional text viewer:

* Fast, responsive UI regardless of log size
* Multiple windows and tabs
* Easy marking of log lines with multiple colors
* Extensible log source architecture
* Direct support for log streams from Docker, Systemd, and Android (connected devices)
* Correct parsing and coloring for common log formats

In short: a log viewer for serious developers and DevOps professionals.

I have used `glogg` as my log viewer of choice for a long time, but it is beginning to show its age and lacks built-in support for log streams. Viewing and analyzing logs is an important part of my daily work, so having the best tools available matters.

This is not the first log viewer I have created. But it is by far the best.

---

## 2. Features

* Open multiple logs in tabs, like a web browser
* Open local files, pipe streams, Systemd journal, Docker container logs, and Android `logcat` streams
* Apply multiple filters to a single log source
* Separate pane for marked lines
* Treat streams like normal logs: follow, navigate, filter and mark
* Auto-detect and colorize common log formats (generic text, Logfault-style, Logcat, Systemd)
* Per-source format override when auto-detection is insufficient
* Fast navigation (first/last line, ±10%, next/previous warning or error)
* Jump to an exact line number
* Optional follow mode for live logs
* Optional line wrapping
* Adjustable zoom (UI, mouse, keyboard)
* Selection-aware copy (single and multi-line)
* Per-line actions: mark/unmark, copy, expanded view
* Filter and marked views as horizontal or vertical splits
* Recent file and stream history
* Session-aware UI (wrapping, scanner selection, etc.)

---

## 3. How It Works

LGX is designed around the idea that **streams should behave like files** as much as possible.

### Streams as files

Live sources (pipe commands, Docker logs, Android logcat, Systemd logs) are handled as stream providers.

They write incoming log events to a temporary file and reuse the same indexing, paging, and line-fetching pipeline as regular files.

This gives streamed logs the same behavior as file-backed logs.

---

### Fast initial scanning

The first pass over a source is optimized for startup speed.

Log scanners provide fast line-level classification so LGX can quickly build a lightweight index (offsets, line boundaries, log levels). This allows the UI to become usable quickly, even for very large files.

Scanners also support incremental updates. When a file grows or a stream appends data, LGX extends the existing state instead of rebuilding it.

---

### Memory-conscious paging and LRU cache

LGX does not keep full log contents in memory.

Instead, it stores metadata (page descriptors, line counts, indexing data), while actual text pages are stored in a shared LRU page cache.

* Active pages remain in memory
* Older, unused pages are evicted

This keeps large logs responsive without turning each open tab into a full in-memory copy.

---

## 4. How to Use

### Opening logs

Use the menus to open sources:

* `File -> Open` — open a file
* `Sources -> Open Pipe Stream` — run a command (e.g. `journalctl -f`)
* `Sources -> Docker -> Open Running Containers`
* `Sources -> Logcat -> Open Devices`
* `File -> Recent` / `Sources -> Recent` — reopen previous sources

---

### Main view controls

The toolbar operates on the main tab:

* Toggle follow mode
* Toggle line wrapping
* Jump to first/last line
* Jump ±10%
* Jump to next/previous warning or error

The status bar shows:

* Source
* Line count
* Current line
* Format
* File size
* Ingestion rate
* Zoom level

Jump operations use the page cache, making them fast even on large logs.

---

### Menus and context menus

Useful menu paths:

* `View -> Follow -> Enabled`
* `View -> Log Lines -> Wrap long lines`
* `View -> Zoom`
* `View -> Format` (override scanner)
* `View -> Goto Line`
* `Windows -> Add Filter View`
* `Windows -> Add Marked View`

Context menu (log view):

* Right-click → `Copy Line`, `Copy Selection`, `Show`
* Long-press → expanded line view
* Gutter click → mark/unmark line. If you hold down keys 1 - 5, you get different colors for trhe mark.

---

### Keyboard and mouse shortcuts

* `f` — toggle follow mode
* `Ctrl+L` — go to line
* `Ctrl+F` — horizontal filter split
* `Ctrl+Shift+F` — vertical filter split
* `Ctrl+M` — horizontal marked split
* `Ctrl+Shift+M` — vertical marked split
* Arrow keys — scroll by line
* `PgUp` / `PgDown` — scroll by page
* `Left` / `Right` — horizontal scroll (when wrapping is off)
* `Ctrl+Left` / `Ctrl+Right` — jump to start/end of line
* `Ctrl + Mouse Wheel` — zoom
* Mouse wheel — scroll
* Scrollbars — manual navigation
* Double-click zoom value → reset to 100%

Behavior notes:

* Scrolling up disables follow mode
* Wrapping disables horizontal scrolling
* Copy works on the current selection

---

## 5. Build from Source

LGX uses CMake, Qt 6, Boost headers, QCoro, and optionally `libsystemd` for the Systemd source.

### Debian / Ubuntu

```sh
sudo apt install \
  build-essential cmake ninja-build git pkg-config \
  qt6-base-dev qt6-declarative-dev qt6-svg-dev \
  libboost-dev libsystemd-dev
```

### Fedora

```sh
sudo dnf install \
  gcc-c++ cmake ninja-build git pkgconf-pkg-config \
  qt6-qtbase-devel qt6-qtdeclarative-devel qt6-qtsvg-devel \
  boost-devel systemd-devel
```

### Arch Linux

```sh
sudo pacman -S \
  base-devel cmake ninja git pkgconf \
  qt6-base qt6-declarative qt6-svg \
  boost systemd
```

### Build

```sh
cmake -S . -B build -G Ninja
cmake --build build
```

The application binary will be available as `build/bin/lgx`.

---

## Systemd

To access full system logs (not just your user logs), add your user to the appropriate group:

```sh
sudo usermod -aG systemd-journal $USER
```

Then log out and back in.

To disable the Systemd source at configure time:

```sh
cmake -S . -B build -G Ninja -DLGX_ENABLE_SYSTEMD_SOURCE=OFF
```

---

## 6. Flatpak

The `flatpak/` directory contains everything needed to build and distribute LGX as a Flatpak.

> **Note:** LGX is a system-level developer tool. The Flatpak requests broad
> permissions (`--filesystem=host`, systemd D-Bus, Docker socket) by design —
> convenience takes priority over sandboxing.

### Prerequisites

```sh
# Debian / Ubuntu
sudo apt install flatpak flatpak-builder

# Fedora
sudo dnf install flatpak flatpak-builder
```

Add the KDE Flatpak repository (provides the Qt 6 runtime):

```sh
flatpak remote-add --if-not-exists flathub https://dl.flathub.org/repo/flathub.flatpakrepo
flatpak install flathub org.kde.Platform//6.8 org.kde.Sdk//6.8
```

### Build locally

Run from the repository root:

```sh
flatpak-builder \
  --repo=repo \
  --force-clean \
  builddir \
  flatpak/io.github.jgaa.lgx.yaml
```

This clones the `qcoro` and `logfault` dependencies automatically before building.

### Run directly from the build directory

```sh
flatpak-builder --run builddir flatpak/io.github.jgaa.lgx.yaml lgx
```

### Export and install a bundle

```sh
# Create a single-file bundle
flatpak build-bundle repo lgx.flatpak io.github.jgaa.lgx

# Install it
flatpak install lgx.flatpak

# Launch
flatpak run io.github.jgaa.lgx
```

### Feature notes and required overrides

| Feature | Status inside Flatpak |
|---|---|
| Open local files | ✅ Works (via `--filesystem=host`) |
| systemd journal | ✅ Works (via `--talk-name=org.freedesktop.systemd1`) |
| Docker logs | ⚠️ Requires Docker socket access — see below |
| Pipe / shell commands | ✅ Works via `flatpak-spawn --host` |

#### Docker socket access

Docker's socket lives at `/var/run/docker.sock`. Grant access once with:

```sh
flatpak override --user --filesystem=/var/run/docker.sock io.github.jgaa.lgx
```

#### Full system journal (as non-root)

Add your user to the `systemd-journal` group on the **host**, then log out and back in:

```sh
sudo usermod -aG systemd-journal $USER
```

### Submitting to Flathub

1. Follow the [Flathub submission guide](https://docs.flathub.org/docs/for-app-authors/submission) — new applications require creating a dedicated repository under the `flathub` GitHub organisation.
2. Before submitting, replace the `tag:` lines for `qcoro` and `logfault` in the manifest with the exact `commit:` SHA (run `git ls-remote <url> refs/tags/<tag>` to retrieve them), and verify the Boost archive `sha256:` checksum.
3. Open a pull request to the `flathub/flathub` repository as described in the guide.

---

## Status

**Beta**
