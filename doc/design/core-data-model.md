# LGX Core Data Model

This document defines the data model needed to reach the MVP in [product-spec.md](../product-spec.md), based on the current implementation.

## Current Reality

Today the real data model lives implicitly in `crates/protocol`, `crates/engine`, `crates/parsing`, and `crates/sources/file`:

* `SourceId`, `ViewId`, and `RowRender` live in `protocol`
* `FileIndex` stores:
  * `offsets: Vec<u64>`
  * `lens: Vec<u32>`
  * `ts: Vec<i64>`
  * `sev: Vec<u8>`
  * `flags: Vec<u8>`
* engine `View` only stores:
  * `source_id`
  * `follow_enabled`
  * `total_rows`

That is enough for one unfiltered file-backed view, but not for the MVP query model.

## Target MVP Model

## Stable IDs

Move shared ids into `crates/core`:

```rust
pub struct SourceId(pub u64);
pub struct ViewId(pub u64);
pub struct TabId(pub u64);
pub struct RowId(pub u32);
```

`TabId` is needed for the UI workspace even if multiple windows stay out of scope for MVP.

## Row Metadata

The existing cached metadata approach is correct and should remain compact:

```rust
#[repr(C)]
pub struct RowMeta {
    pub ts_millis: i64,
    pub sev: u8,
    pub flags: u8,
    pub _pad: u16,
}
```

Notes:

* `ts_millis` matches the current parser output and avoids unnecessary churn.
* `flags` should include at least:
  * timestamp present
  * severity present
  * json hint

## Source Index Layout

For MVP, retain the current structure-of-arrays layout for file-backed random access:

```rust
pub struct SourceIndex {
    pub offsets: Vec<u64>,
    pub lens: Vec<u32>,
    pub ts_millis: Vec<i64>,
    pub sev: Vec<u8>,
    pub flags: Vec<u8>,
}
```

This layout is already implemented for files and is the basis for fast filtering.

## Source

A source owns raw rows and their metadata.

```rust
pub struct Source {
    pub id: SourceId,
    pub kind: SourceKind,
    pub capabilities: SourceCapabilities,
}
```

For MVP:

```rust
pub enum SourceKind {
    File,
    DockerContainer,
}
```

Capabilities needed for MVP:

```rust
pub struct SourceCapabilities {
    pub can_follow: bool,
    pub has_cached_meta: bool,
    pub supports_random_row_access: bool,
}
```

For Docker-backed sources, `supports_random_row_access` will likely be provided by an internal append-only spool or equivalent local buffering layer fed from the Docker Engine API stream.

## Query

The current product MVP only requires severity filtering, but the query type should leave room for immediate next steps.

```rust
pub struct Query {
    pub severity_min: Option<u8>,
    pub text_contains: Option<String>,
}
```

Rules:

* `severity_min` is required for MVP
* `text_contains` is optional for MVP and should be treated as stretch work
* AND/OR/NOT remains post-MVP

## View

A view is a query over one source.

```rust
pub struct View {
    pub id: ViewId,
    pub source_id: SourceId,
    pub kind: ViewKind,
    pub query: Query,
    pub row_ids: ViewRows,
    pub follow: bool,
}
```

Where:

```rust
pub enum ViewKind {
    Full,
    Filtered { base_view: ViewId },
}

pub enum ViewRows {
    FullSource,
    Materialized(Vec<RowId>),
}
```

Why this split matters:

* full views can stream directly from the source index
* filtered views can map visible rows through `Vec<RowId>`
* jump-to-full-view becomes a stable row-id navigation problem instead of text matching

## Tab State

The UI needs explicit per-tab state for MVP:

```rust
pub struct TabState {
    pub tab_id: TabId,
    pub view_id: ViewId,
    pub title: String,
    pub selected_row: Option<u32>,
    pub horizontal_offset: f32,
}
```

This state does not belong in the source or parser layers.

For MVP window composition, the UI layer also needs split placement metadata so the marked-lines view can be docked into the current window.

```rust
pub enum DockTarget {
    Tab,
    SplitRight,
    SplitBottom,
}
```

## Marked Lines

Marked lines are a derived per-window collection, not a new source type.

```rust
pub struct MarkedLine {
    pub source_view_id: ViewId,
    pub source_row_id: RowId,
    pub label: Option<String>,
}
```

Rules:

* each window owns at most one marked-lines view for MVP
* the marked-lines view stores stable references back to the source row
* navigation from a marked line back to the source row should use `ViewId` + `RowId`

## Protocol Rows

The engine-to-UI row payload can stay close to the current shape:

```rust
pub struct RowRender {
    pub row_id: RowId,
    pub text: String,
    pub ts_millis: i64,
    pub sev: u8,
    pub flags: u8,
}
```

The missing field today is `row_id`. It is required for:

* jump to full view
* stable selection when switching tabs
* future search result activation

## Append Semantics

Appends should be source-driven, not full-view-driven.

When a source receives new rows:

* full views update their total row count directly
* filtered views evaluate only the appended metadata range
* the engine emits per-view append or invalidation events

This is the key behavior needed to make follow + filtering work together.

For Docker sources this same append path should be fed by socket/API log stream events, not by subprocess output from `docker logs`.
