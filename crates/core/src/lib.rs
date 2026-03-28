use std::fmt;
use std::path::PathBuf;

#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub struct SourceId(pub u64);

#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub struct ViewId(pub u64);

#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub struct RowId(pub u64);

#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub enum SourceKind {
    File,
    Docker,
}

#[derive(Debug, Copy, Clone, Eq, PartialEq, Hash)]
pub enum ViewKind {
    Full,
    Filtered,
    Marked,
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct SourceInfo {
    pub id: SourceId,
    pub kind: SourceKind,
    pub display_name: String,
    pub locator: SourceLocator,
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub enum SourceLocator {
    File { path: PathBuf },
    DockerContainer { id: String },
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct ViewInfo {
    pub id: ViewId,
    pub source_id: SourceId,
    pub kind: ViewKind,
    pub parent: Option<ViewId>,
    pub title: String,
    pub total_rows: u32,
    pub follow_enabled: bool,
    pub query: ViewQuery,
}

#[derive(Debug, Clone, Eq, PartialEq, Default)]
pub struct ViewQuery {
    pub severity_mask: Option<u8>,
    pub min_severity: Option<u8>,
    pub max_severity: Option<u8>,
    pub required_flags: u8,
    pub text: Option<String>,
    pub text_is_regex: bool,
    pub text_message_only: bool,
    pub text_case_insensitive: bool,
}

impl ViewQuery {
    pub fn is_empty(&self) -> bool {
        self.severity_mask.is_none()
            && self.min_severity.is_none()
            && self.max_severity.is_none()
            && self.required_flags == 0
            && self
                .text
                .as_ref()
                .map(|text| text.is_empty())
                .unwrap_or(true)
    }
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct RowRef {
    pub view_id: ViewId,
    pub row_id: RowId,
}

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct RowRender {
    pub row_id: RowId,
    pub text: String,
    pub ts: i64,
    pub sev: u8,
    pub flags: u8,
    pub origin: Option<RowRef>,
}

impl fmt::Display for SourceId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl fmt::Display for ViewId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}

impl fmt::Display for RowId {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{}", self.0)
    }
}
