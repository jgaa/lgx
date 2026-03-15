# LGX Project Design

This document aligns the implementation design with [product-spec.md](../product-spec.md).

## Product Target

The product target remains the same:

* LGX is a desktop log IDE, not a single-file viewer.
* A view is a query over a log source.
* MVP includes:
  * local file open
  * growing file follow
  * severity filtering
  * multiple tabs
  * jump to full view
  * stable virtualization
  * basic Docker container log viewing

Android, structured JSON views, cross-view search, and multi-window behavior remain post-MVP.

## Current Implementation

The codebase currently implements a narrow but useful vertical slice:

* One top-level Slint window
* One active view at a time
* Local file source only
* Full-file mmap indexing with per-line offsets and lengths
* Incremental polling for appended file content
* Cached timestamp and severity metadata at ingest time
* Windowed row fetching for large files
* Follow toggle, row jump, and severity color toggle

The current app does not yet implement the core "log IDE" model from the product spec:

* no tabs
* no multiple independent views over the same source
* no query model
* no filtering UI or engine-side filtered materialization
* no jump from filtered view to full view
* no startup handling for positional file arguments
* no marked-lines view
* no split view composition
* no proper go-to-line dialog
* no Docker source
* no source abstraction used by the engine
* uses external slider controls rather than native per-view scrollbars

## Design Direction For MVP

The MVP should be built by generalizing the current local-file pipeline instead of replacing it.

### 1. Preserve the current fast path

Keep these existing strengths as the baseline:

* indexed file access via offsets and lengths
* cached metadata arrays for timestamp and severity
* incremental append detection
* bounded row-range requests from UI to engine

### 2. Introduce real source and view layers

The engine should stop treating "open file" as the same thing as "open view".

Target model:

* `Source`: owns the raw index and append updates
* `View`: references one source plus a query and display state
* `View rows`: either all row ids or a filtered subset of row ids

For MVP, only local file and Docker-backed sources are required, but both must fit the same engine contract.

### 3. Keep the UI engine boundary strict

The product spec requirement still stands:

* UI does not access files or backends directly
* UI sends commands to the engine
* Engine emits view-oriented events back to UI

That boundary exists today in a limited form and should be extended rather than bypassed.

### 4. Scope the MVP UI to tabs and splits, not multi-window

The product spec allows multiple windows, but MVP only explicitly requires multiple tabs.

To avoid premature UI complexity, the implementation plan should:

* add a tabbed workspace first
* support split panes within a window for the marked-lines workflow
* model windows in core/protocol only if needed for later expansion
* defer detachable tabs and independent top-level windows until after MVP

### 5. Treat Docker as another source, not a special-case screen

The product spec mentions a dedicated Docker window, but the important MVP behavior is:

* list running containers
* open a container log as a normal followable view
* apply the same virtualization and filtering behavior as file views

The container list can be a panel or tab in MVP. It does not need full docking support yet.

For implementation, Docker support should use the local Docker Engine socket/API directly rather than shelling out to the `docker` CLI. That keeps source behavior inside the engine boundary and avoids coupling runtime behavior to external command invocation.

## Required Architectural Changes

### Core crate

`crates/core` is still a stub. It should become the home for:

* ids: `SourceId`, `ViewId`, optionally `TabId`
* severity enum/constants
* row metadata
* query types for MVP filtering
* view state and display mode enums

### Source API crate

`crates/source-api` is also still a stub. It should define the minimum engine-facing abstraction for:

* opening a source
* fetching raw rows by row id / range
* reading cached metadata
* receiving append/update notifications

MVP does not need an over-generalized trait hierarchy, but it does need enough structure to support both file and Docker-backed sources.

For Docker specifically, the source contract should support:

* container discovery and refresh
* streaming log bytes from the Engine API
* conversion of streamed records into the same indexed row model used by file-backed views

### Engine crate

The engine must evolve from "single full-file view" to:

* source registry
* view registry
* view materialization jobs
* filtered row-id lists
* append propagation from source to all derived views

### UI crate

The UI must evolve from one list to a workspace with:

* tabs
* split panes
* per-tab follow state
* filter controls
* native per-view scrollbars
* marker icon beside line numbers
* marked-lines pane management
* go-to-line dialog triggered from menu or `Ctrl+L`
* jump-to-full-view action for filtered tabs
* Docker container list entry point

### App crate

`crates/app` should handle startup file arguments before the UI is shown:

* parse supported `--arg value` style options normally
* treat remaining positional arguments as log files
* open those files into tabs during initial UI setup

## Non-MVP Items

These remain explicitly out of MVP scope even if the architecture should not block them:

* Android logcat integration
* Loki or other remote backends
* structured JSON tree view
* cross-view search
* multiple top-level windows
* saved sessions / workspaces
