use std::error::Error;
use std::fmt;
use std::time::Duration;

use lgx_core::{RowId, RowRender, SourceInfo, ViewQuery};

#[derive(Debug, Clone, Eq, PartialEq)]
pub struct SourceUpdate {
    pub appended_rows: u32,
    pub total_rows: u32,
    pub scanned_bytes: u64,
    pub scan_duration: Duration,
}

#[derive(Debug)]
pub struct SourceRows {
    pub rows: Vec<RowRender>,
    pub decode_duration: Duration,
}

#[derive(Debug)]
pub struct SourceError {
    message: String,
}

impl SourceError {
    pub fn new(message: impl Into<String>) -> Self {
        Self {
            message: message.into(),
        }
    }
}

impl fmt::Display for SourceError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        f.write_str(&self.message)
    }
}

impl Error for SourceError {}

pub trait LogSource: Send {
    fn info(&self) -> &SourceInfo;
    fn total_rows(&self) -> u32;
    fn read_rows(&self, start: u32, count: u32) -> Result<SourceRows, SourceError>;
    fn read_rows_by_id(&self, row_ids: &[RowId]) -> Result<SourceRows, SourceError>;
    fn query_row_ids(
        &self,
        start: u32,
        end: Option<u32>,
        query: &ViewQuery,
    ) -> Result<Vec<RowId>, SourceError>;
    fn poll_update(&mut self) -> Result<Option<SourceUpdate>, SourceError>;
}
