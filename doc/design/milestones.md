# LGX MVP Milestones

This milestone plan is aligned to [product-spec.md](../product-spec.md) and the implementation that already exists in the repo.

## Current Checkpoint

Completed in code:

* workspace and Slint app skeleton
* local file open
* file indexing with offsets and lengths
* cached timestamp and severity parsing during indexing
* range-based row requests
* follow for growing files
* basic windowed list model for large logs
* basic severity colorization and performance counters

Not completed:

* tabs
* split panes
* query-backed views
* severity filtering
* marked-lines view
* jump to full view
* startup positional file handling
* proper go-to-line dialog
* Docker source and container list

## Milestone 1 - Extract shared core and source contracts

Goal: stop encoding the data model implicitly inside the current file-view path.

Deliverables:

* move ids and row metadata into `crates/core`
* define query and view types in `crates/core`
* define a minimal engine-facing source contract in `crates/source-api`
* add `row_id` to protocol row payloads

Acceptance:

* engine code no longer treats "opened file" and "view" as the same concept
* file source can be wrapped behind the new source contract without losing current behavior

## Milestone 2 - Refactor engine from one file view to many views

Goal: support multiple concurrent views over the same source.

Deliverables:

* source registry in engine
* view registry in engine
* full-view row retrieval by `ViewId`
* independent follow state per view
* event routing that does not assume one current view exists globally

Acceptance:

* one file can back more than one engine view
* opening a second view does not duplicate file indexing work

## Milestone 3 - Add tabs to the UI workspace

Goal: meet the MVP requirement for multiple tabs.

Deliverables:

* tab strip in Slint UI
* split-pane support inside a window
* create, switch, and close tabs
* per-tab state for selection, horizontal scroll, and follow toggle
* command flow for opening a file into a new tab
* startup opening of positional file arguments into tabs
* replace the external slider/drawer controls with native per-view scrolling

Acceptance:

* user can keep multiple file views open simultaneously
* switching tabs preserves each tab's scroll and follow state
* log views use their own natural scrollbars rather than external controls

## Milestone 4 - Implement severity-filtered views

Goal: turn LGX into a query-driven viewer for the first real MVP query.

Deliverables:

* `Query { severity_min }` in core
* engine materializes filtered `Vec<RowId>` by scanning cached severity metadata
* append path reevaluates only new rows for filtered views
* UI control to create a filtered view from a selected severity threshold

Acceptance:

* severity filtering does not rescan raw text
* filtered views remain responsive on large files
* filtered views keep updating while follow is enabled

## Milestone 5 - Jump from filtered view to full view

Goal: preserve context between derived and raw exploration.

Deliverables:

* include `row_id` in row payloads
* engine command to open or focus the source full view at a specific row
* UI action from a filtered tab to navigate to the raw full view

Acceptance:

* selecting a row in a filtered tab can open or focus the full tab at the same underlying row

## Milestone 6 - Add marked-lines view and proper go-to-line UX

Goal: support lightweight investigation workflows without the current toy controls.

Deliverables:

* clickable marker icon beside the line number in each row
* one marked-lines view per window
* automatic split creation if the user marks a line and no marked-lines view exists yet
* navigation from marked lines back to source rows
* modal go-to-line dialog opened from menu or `Ctrl+L`
* remove permanent go-to-line input from the main layout

Acceptance:

* user can mark rows from any normal log view
* the first mark opens a docked marked-lines pane automatically
* marked rows can navigate back to the source row
* go-to-line no longer consumes permanent layout space

## Milestone 7 - Add basic Docker log viewing

Goal: satisfy the final MVP source requirement.

Deliverables:

* Docker source adapter in `crates/sources/docker` or equivalent
* direct Docker Engine socket/API integration for:
  * container discovery
  * container metadata refresh
  * log streaming
* engine support for Docker-backed sources using the same view model
* UI container list with:
  * container name
  * status
  * image
* open container logs into a normal follow-enabled tab

Acceptance:

* user can select a running container and open its logs
* container log tabs support the same virtualization and follow behavior as file tabs
* severity filtering works if metadata can be derived from the container output format
* implementation does not depend on invoking the `docker` CLI

## Milestone 8 - MVP hardening

Goal: close the main product risks before calling the build MVP-complete.

Deliverables:

* tests for:
  * filtered view materialization
  * append propagation into filtered views
  * jump-to-full-view row mapping
  * Docker source lifecycle basics
* performance checks using `logsim`
* documentation refresh for actual architecture and user-visible MVP scope

Acceptance:

* app remains responsive on large files and active append workloads
* the implemented feature set matches the MVP section of `product-spec.md`
