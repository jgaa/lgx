
# LGX: High-Performance Multi-Source Log Viewer

## 1. Vision & Goals

The goal of this project is to build a **modern, high-performance log viewer for Linux (and eventually cross-platform)** that scales from single multi-million-line log files to **live, remote, and structured log sources** such as Docker, Loki, and other streaming backends.

The application should feel closer to an **IDE for logs** than a traditional text viewer:

* Fast, responsive UI regardless of log size
* Multiple windows and tabs
* Views as first-class, query-driven objects
* Strong support for structured logs (JSON)
* Extensible log source architecture

Performance, correctness, and extensibility take priority over visual effects.

---

## 2. Core Requirements

### 2.1 User Interface Requirements

#### Windows & Tabs

* Support **multiple top-level windows**
* Each window contains a **tab bar**, similar to a web browser
* Tabs can be:

  * Reordered
  * Detached into new windows
  * Closed independently
* Windows must be **resizable** and layout-independent

#### Views

* Each tab displays exactly one **View**
* A View is defined as a *query over a log source*
* Views are independent and can coexist over the same source
* Views update incrementally as data is fetched or streamed

#### Contextual Interaction

* Right-click on tokens (UUIDs, IDs, JSON fields, timestamps) enables:

  * Pop ups a menu that allows:
    * Creating a new view with a derived filter
    * Opening the new view in:

        * the same window
        * a new tab
        * a new window

---

### 2.2 Log Display Requirements

#### Line Presentation Modes

Each view supports multiple display modes:

1. **Compact Mode**

   * Single-line summary
   * User-selectable fields (e.g. timestamp, severity, message)
   * Optimized for scanning and density

2. **Raw Mode**

   * Displays the original log line verbatim

3. **Structured / JSON Mode**

   * Detect JSON logs automatically
   * Allow users to select which fields appear in the compact row
   * Full structured payload available via expansion

#### Expandable Rows

* Each row may be expanded inline or in a detail panel
* Expanded view may include:

  * Raw line
  * Pretty-printed JSON
  * Field/value table
  * Copy actions

---

### 2.3 Search & Filtering

#### View-Local Search

* Search within a single view
* Supports substring and regex
* Highlights matches
* Next/previous navigation

#### Cross-View Search

* Search for a term across **all open views**
* Results presented in a unified result panel
* Clicking a result:

  * Activates the correct tab/window
  * Scrolls to the matching row

---

## 3. Log Sources & Data Access

### 3.1 Generic Log Source Interface

All log data must be accessed through a **generic source abstraction**, decoupled from the UI.

Sources may include:

* Local files
* Docker container logs
* Loki
* Journalctl
* apk logcat
* Custom or future backends

The UI and view engine **must not assume**:

* Line numbers
* Filesystem access
* Random access capability

---

### 3.2 Source Capabilities

Each source declares its capabilities:

* Bounded fetch (pagination)
* Time-range queries
* Streaming / follow mode
* Backward fetch
* Native query language
* Structured fields

The UI adapts based on these capabilities.

---

### 3.3 Fetching & Streaming

Sources support:

* **Paged fetching** using opaque cursors
* **Directional traversal** (forward/backward)
* **Streaming** for live logs

The core engine merges fetched and streamed events into views incrementally.

---

## 4. Architecture Overview

### 4.1 High-Level Architecture

```
+------------------+
|        UI        |
| (Windows/Tabs)   |
+--------+---------+
         |
         v
+------------------+
|     View Layer   |
|  (Queries, Rows) |
+--------+---------+
         |
         v
+------------------+
|  Engine / Core   |
|  - Filtering    |
|  - Search       |
|  - Indexing     |
+--------+---------+
         |
         v
+------------------+
|   Log Sources    |
| File / Docker / |
| Loki / Others   |
+------------------+
```

---

### 4.2 Core Concepts

#### LogEvent

A normalized event consumed by the UI:

* Optional timestamp
* Optional severity
* Message (string)
* Optional structured fields
* Source metadata
* Opaque cursor

#### View

* References a single source
* Contains:

  * Query
  * Time range (optional)
  * Display configuration
* Maintains a **materialized index** of matching events
* Is virtualized (only visible rows are rendered)

#### Source Registry

* Manages creation and lifecycle of log sources
* Allows adding new source types without touching UI code

---

### 4.3 Performance Strategy

* No full file loads
* No per-line widgets
* Virtualized rendering
* Background jobs are:

  * Incremental
  * Cancelable
* Parsing and JSON decoding is lazy and cached
* Local files use memory mapping and line offset indexing

---

## 5. Extensibility & Future Plans

### 5.1 Planned Source Integrations

* Docker (container + compose support)
* Loki (LogQL integration where possible)
* journalctl
* Remote file access (SSH / SFTP)
* Custom TCP / HTTP log streams

---

### 5.2 Advanced Features (Post-MVP)

* Saved views and workspaces
* Query history and templates
* Correlation ID highlighting across views
* Time-synchronized scrolling between views
* Export filtered views
* Plugin system for custom parsers and enrichers

---

## 6. Technology Stack

* **Language:** Rust
* **UI:** Slint (preferred) or winit + egui
* **Async:** Tokio
* **Local files:** memmap2
* **Streaming:** async streams (futures)
* **Parsing:** serde_json (lazy), custom field extractors

---

## 7. Milestones

### Milestone 1 – Core Viewer (MVP)

* Single window, tabbed UI
* Local file source
* Compact + raw display
* Virtualized scrolling
* Basic filtering

### Milestone 2 – Views & Queries

* Multiple independent views per source
* Right-click → new filtered view
* Expandable rows
* JSON detection and field selection

### Milestone 3 – Search & Multi-Window

* Global search across views
* Multiple windows
* Tab detaching

### Milestone 4 – Streaming & External Sources

* Tail / follow support
* Docker source
* Generic streaming support

### Milestone 5 – Loki & Time-Range Fetching

* Loki integration
* Time-range navigation
* Cursor-based pagination

---

## 8. Non-Goals (for now)

* Full metrics/alerting system
* Distributed tracing UI
* Cloud-hosted backend
* Heavy visualization dashboards

