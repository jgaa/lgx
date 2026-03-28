pub use lgx_core::{RowId, RowRender, SourceId, ViewId, ViewKind, ViewQuery};

#[derive(Debug, Clone)]
pub struct DockerContainer {
    pub id: String,
    pub name: String,
    pub image: String,
    pub status: String,
}

#[derive(Debug, Clone)]
pub enum EngineCommand {
    OpenFile {
        path: String,
    },
    RequestRows {
        view_id: ViewId,
        start: u32,
        count: u32,
    },
    SetFollow {
        view_id: ViewId,
        enabled: bool,
    },
    OpenFilteredView {
        source_view_id: ViewId,
        query: ViewQuery,
    },
    CloseView {
        view_id: ViewId,
    },
    JumpToFullView {
        source_view_id: ViewId,
        row_id: RowId,
    },
    ListDockerContainers,
    OpenDockerContainer {
        container_id: String,
        title: String,
    },
    ToggleMarkRow {
        source_view_id: ViewId,
        row_id: RowId,
    },
}

#[derive(Debug, Clone)]
pub enum EngineEvent {
    FileOpened {
        source_id: SourceId,
        view_id: ViewId,
        path: String,
        total_lines_estimate: Option<u32>,
    },
    ViewOpened {
        source_id: SourceId,
        view_id: ViewId,
        kind: ViewKind,
        title: String,
        total_rows: u32,
        query: ViewQuery,
    },
    JumpTarget {
        view_id: ViewId,
        row: u32,
    },
    DockerContainersListed {
        containers: Vec<DockerContainer>,
    },
    ViewStats {
        view_id: ViewId,
        total_rows: u32,
    },
    ViewBusy {
        view_id: ViewId,
        busy: bool,
    },
    RowsReady {
        view_id: ViewId,
        start: u32,
        rows: Vec<RowRender>,
    },
    RowsAppended {
        view_id: ViewId,
        appended: u32,
        total_rows: u32,
    },
    PerfStats {
        index_lps: f64,
        indexed_mb_s: f64,
        rows_req_s: f64,
        ui_events_s: f64,
    },
    Error {
        message: String,
    },
}
