# LGX — Product Specification

## 1. Product Vision

**LGX is a high-performance, desktop log IDE** for engineers who need to explore massive, live, and distributed log streams without friction.

It scales from:

* Multi-GB local log files
* High-volume growing logs
* Docker container logs
* Android device logs (logcat)
* Remote log backends (future: Loki)

It should feel like:

> VS Code for logs — fast, composable, exploratory.

---

# 2. Core Product Principles

### 2.1 Log IDE, not a log viewer

LGX is built around:

* Views as queries
* Multi-window exploration
* Fast contextual navigation
* Zero re-parsing when filtering

### 2.2 Instant Feedback

* Scrolling must feel native even on millions of lines
* Filtering must feel immediate
* Live follow must never freeze UI

### 2.3 Uniform Abstraction

All log sources behave the same in the UI:

* Local files
* Growing files
* Docker containers
* Android logcat
* Future streaming backends

---

# 3. Core UX Model

## 3.1 Windows

* Multiple top-level windows supported
* Windows are independent.
* Each window can contain multiple tabs.

## 3.2 Tabs and Splitters

Tabs and splitters allows user convenenty access to information

* Each tab and pane in a splitted window represents a **View**.
* Tabs are lightweight and disposable.
* Tabs can be duplicated.
* Tabs can be dragged between windows.
* A window may show multiple docked views at once.
* Each view owns its own natural scrollbars.
* Auxiliary controls must not live in drawers outside the actual log view.

## 3.3 Views

A **View = query over a log source**.

A View contains:

* Source reference
* Query definition
* Display configuration
* Scroll position
* Follow state

Views are:

* Cheap to create
* Incrementally evaluated
* Cancelable

Special MVP view:

* Each window may contain one marked-lines view collecting user-selected rows from other views in that window.

---

# 4. Log Sources

All sources implement a common abstraction:

## 4.1 Local File

* Static file
* Growing file (tail -f)
* Multi-GB support
* Efficient random access
* If the application is started with positional arguments not consumed as `--arg value` options, each remaining argument is treated as a log file to open

## 4.2 Docker Logs

LGX includes a dedicated **Docker window**:

### Docker Window Features

* Lists all running containers
* Shows container:

  * Name
  * Status
  * Image
  * Uptime

### Log Interaction

* Single click → open new log view
* Double click → open in new window
* Toggle:

  * Auto-follow
* Automatic:

  * Option to auto-open logs for new containers
  * Option to auto-create background views for all containers

### Live Updates

* Detect new containers
* Detect stopped containers
* Auto-refresh container list

### Per-container View Behavior

* Follow by default
* Filterable like any other view
* Jump to full raw view
* Independent tabs per container

---

## 4.3 Android Logs (logcat)

LGX includes an **Android Devices window**:

### Device Discovery

* Detect connected devices via ADB
* Show device list
* Allow selection per device

### Logcat Integration

* Start streaming `logcat`
* Support filtering by:

  * Tag
  * Priority (V/D/I/W/E/F)
  * PID (optional)
* Multi-device support

Each device:

* Has its own source
* Can open multiple views with filters

---

## 4.4 Future Remote Sources

(Not required for MVP but supported by architecture)

* Loki
* Journalctl (local systemd)
* Custom HTTP streams

---

# 5. View Types

Each view can switch display modes:

## 5.1 Compact View (default)

* Timestamp
* Severity
* Message
* Optional selected fields

## 5.2 Raw View

* Exact original line
* No parsing assumptions

## 5.3 Structured JSON View

* Expandable tree
* Collapsible fields
* Field selection for compact mode

---

# 6. Filtering & Query Model

## 6.1 Core Filters

* Severity ≥ level
* Time range
* Text contains
* Field equals (future)
* AND / OR / NOT

## 6.2 Right-Click Exploration

User can:

* Right-click severity → filter ≥ this severity
* Right-click timestamp → filter time range
* Right-click token → filter contains token
* Right-click JSON field → filter field=value

Each action can:

* Replace current view
* Open in new tab
* Open in new window

## 6.3 Marked Lines

Users can mark interesting log lines for later comparison.

* Each row shows a clickable marker icon beside the line number
* Clicking the marker adds or removes the row from the current window's marked-lines view
* If no marked-lines view exists in the current window, LGX creates one by splitting the current view and docking the marked-lines view there
* Marked rows preserve a reference to the original source row so the user can navigate back

---

# 7. Navigation & Exploration

## 7.1 Jump to Full View

From a filtered view:

* Jump to the same row in the full view
* Preserve context

## 7.2 Cross-View Search

* Search across all open views
* Result list grouped by view
* Selecting result activates correct window/tab

## 7.3 Follow Mode

* Per-view toggle
* Auto-scroll when enabled
* Does not yank scroll when disabled

## 7.4 Go To Line

* "Go to line" is invoked from menu or keyboard shortcut `Ctrl+L`
* It opens a dialog popup scoped to the active view
* It does not occupy permanent space in the main view layout

---

# 8. Performance Expectations

LGX must:

* Handle multi-GB log files
* Handle sustained 50k+ lines/sec append rates
* Remain responsive while indexing
* Keep memory stable regardless of file size
* Never load full file text into memory
* Use the natural scrollbars within each log view rather than separate external slider or drawer controls

---

# 9. Docker-Specific Product Requirements

### 9.1 Container List Window

* Dockable panel
* Filter containers by name/image
* Sort by status or name

### 9.2 Auto-Open Policy

User-configurable:

* [ ] Open logs for new containers automatically
* [ ] Open only containers matching pattern
* [ ] Background log collection without visible tab

### 9.3 Multi-Container View

Future capability:

* Combine logs from multiple containers into one view
* Include container name column

---

# 10. Android-Specific Product Requirements

### 10.1 Device Panel

* Show:

  * Device name
  * Android version
  * Connection status

### 10.2 Logcat View

* Priority column
* Tag column
* PID column
* Filter by tag/pid
* Colorize priority

---

# 11. Non-Goals (for now)

* Metrics dashboards
* Distributed tracing UI
* Cloud-hosted backend
* Alerting systems

---

# 12. MVP Definition

LGX MVP must support:

* Local file open
* Positional file arguments on startup
* Growing file follow
* Severity filtering
* Multiple tabs
* Split view with marked-lines pane
* Jump to full view
* Go to line dialog via `Ctrl+L` or menu
* Stable virtualization
* Basic Docker container log viewing

Android logcat can be phase 2, but architecture must support it.

---

# 13. Future Differentiators

* Session persistence
* Saved queries
* Workspace restore
* Log diffing
* Correlation view (multi-source time aligned)
* Structured field extraction

---

# 14. Product Identity

**Name:** LGX
**Tagline:**

> Fast, live log exploration without compromise.

**Positioning:**

* For backend engineers
* For SREs
* For embedded/Android engineers
* For container-heavy workflows
