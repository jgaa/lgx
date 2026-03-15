use std::fs::File;
use std::path::{Path, PathBuf};
use std::time::{Duration, Instant};

use lgx_core::{RowId, RowRender, SourceId, SourceInfo, SourceKind, SourceLocator, ViewQuery};
use lgx_parsing::{MetaParser, ParserKind, ParserSelector};
use lgx_source_api::{LogSource, SourceError, SourceRows, SourceUpdate};
use memmap2::Mmap;

#[derive(Debug)]
pub enum FileError {
    Io(std::io::Error),
    LineTooLong(usize),
    OffsetOverflow(u64),
}

impl std::fmt::Display for FileError {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        match self {
            FileError::Io(err) => write!(f, "{err}"),
            FileError::LineTooLong(len) => write!(f, "line length {len} exceeds u32::MAX"),
            FileError::OffsetOverflow(offset) => {
                write!(f, "line offset {offset} exceeds usize::MAX")
            }
        }
    }
}

impl std::error::Error for FileError {
    fn source(&self) -> Option<&(dyn std::error::Error + 'static)> {
        match self {
            FileError::Io(err) => Some(err),
            _ => None,
        }
    }
}

impl From<std::io::Error> for FileError {
    fn from(err: std::io::Error) -> Self {
        FileError::Io(err)
    }
}

pub struct FileIndex {
    path: PathBuf,
    file: File,
    mmap: Mmap,
    offsets: Vec<u64>,
    lens: Vec<u32>,
    ts: Vec<i64>,
    sev: Vec<u8>,
    flags: Vec<u8>,
    parser: MetaParser,
    indexed_bytes: u64,
    pending_line_start: Option<u64>,
}

pub struct UpdateResult {
    pub appended_lines: u32,
    pub total_lines: u32,
    pub scanned_bytes: u64,
    pub scan_duration: Duration,
}

pub struct FileLogSource {
    info: SourceInfo,
    index: FileIndex,
}

const SCAN_CHUNK_BYTES: usize = 4 * 1024 * 1024;

impl FileIndex {
    pub fn open(path: &Path) -> Result<Self, FileError> {
        let path = path.to_path_buf();
        let file = File::open(&path)?;
        let mmap = unsafe { Mmap::map(&file)? };
        Ok(Self {
            path,
            file,
            mmap,
            offsets: Vec::new(),
            lens: Vec::new(),
            ts: Vec::new(),
            sev: Vec::new(),
            flags: Vec::new(),
            parser: MetaParser::new(ParserKind::Plain),
            indexed_bytes: 0,
            pending_line_start: None,
        })
    }

    pub fn build_index(&mut self) -> Result<(), FileError> {
        let bytes = &self.mmap[..];
        let mut offsets = Vec::new();
        let mut lens = Vec::new();
        let mut ts = Vec::new();
        let mut sev = Vec::new();
        let mut flags = Vec::new();
        let mut line_start: u64 = 0;

        let selector = ParserSelector::new(32);
        let parser = selector.select(sample_lines(bytes, 32));

        for (i, &b) in bytes.iter().enumerate() {
            if b == b'\n' {
                let mut len = i as u64 - line_start;
                if len > 0 && bytes[i - 1] == b'\r' {
                    len -= 1;
                }
                push_line_with_meta(
                    &mut offsets,
                    &mut lens,
                    &mut ts,
                    &mut sev,
                    &mut flags,
                    bytes,
                    &parser,
                    line_start,
                    len,
                )?;
                line_start = i as u64 + 1;
            }
        }

        self.pending_line_start = if line_start < bytes.len() as u64 {
            Some(line_start)
        } else {
            None
        };

        self.offsets = offsets;
        self.lens = lens;
        self.ts = ts;
        self.sev = sev;
        self.flags = flags;
        self.parser = parser;
        self.indexed_bytes = bytes.len() as u64;
        Ok(())
    }

    pub fn line_count(&self) -> usize {
        self.offsets.len()
    }

    pub fn path(&self) -> &Path {
        &self.path
    }

    pub fn indexed_bytes(&self) -> u64 {
        self.indexed_bytes
    }

    pub fn get_line_bytes(&self, row: usize) -> Option<&[u8]> {
        let start = *self.offsets.get(row)?;
        let len = *self.lens.get(row)? as usize;
        let start_usize = usize::try_from(start).ok()?;
        let end = start_usize.checked_add(len)?;
        Some(&self.mmap[start_usize..end])
    }

    pub fn get_range_bytes(&self, start: usize, count: usize) -> Vec<&[u8]> {
        let end = start.saturating_add(count).min(self.line_count());
        let mut rows = Vec::with_capacity(end.saturating_sub(start));
        for row in start..end {
            if let Some(bytes) = self.get_line_bytes(row) {
                rows.push(bytes);
            }
        }
        rows
    }

    pub fn get_line_meta(&self, row: usize) -> Option<(i64, u8, u8)> {
        let ts = *self.ts.get(row)?;
        let sev = *self.sev.get(row)?;
        let flags = *self.flags.get(row)?;
        Some((ts, sev, flags))
    }

    pub fn query_rows(&self, start: usize, end: usize, query: &ViewQuery) -> Vec<RowId> {
        let end = end.min(self.line_count());
        let mut rows = Vec::with_capacity(end.saturating_sub(start));
        for row in start..end {
            let (_, sev, _) = match self.get_line_meta(row) {
                Some(meta) => meta,
                None => continue,
            };
            if let Some(min) = query.min_severity {
                if sev < min {
                    continue;
                }
            }
            if let Some(max) = query.max_severity {
                if sev > max {
                    continue;
                }
            }
            let flags = self.flags.get(row).copied().unwrap_or(0);
            if query.required_flags != 0 && flags & query.required_flags != query.required_flags {
                continue;
            }
            rows.push(RowId(row as u64));
        }
        rows
    }

    pub fn poll_update(&mut self) -> Result<Option<UpdateResult>, FileError> {
        let new_size = self.file.metadata()?.len();
        if new_size <= self.indexed_bytes {
            return Ok(None);
        }

        self.mmap = unsafe { Mmap::map(&self.file)? };

        let bytes = &self.mmap[..];
        let mut line_start = self.pending_line_start.unwrap_or(self.indexed_bytes);
        let mut scan_pos = line_start as usize;
        let mut appended: u32 = 0;
        let mut scanned_bytes: u64 = 0;
        let scan_start = Instant::now();

        while scan_pos < bytes.len() {
            let end = (scan_pos + SCAN_CHUNK_BYTES).min(bytes.len());
            scanned_bytes = scanned_bytes.saturating_add((end - scan_pos) as u64);
            for i in scan_pos..end {
                if bytes[i] == b'\n' {
                    let mut len = i as u64 - line_start;
                    if len > 0 && bytes[i - 1] == b'\r' {
                        len -= 1;
                    }
                    push_line_with_meta(
                        &mut self.offsets,
                        &mut self.lens,
                        &mut self.ts,
                        &mut self.sev,
                        &mut self.flags,
                        bytes,
                        &self.parser,
                        line_start,
                        len,
                    )?;
                    appended = appended.saturating_add(1);
                    line_start = i as u64 + 1;
                }
            }
            scan_pos = end;
        }

        self.pending_line_start = if line_start < new_size {
            Some(line_start)
        } else {
            None
        };
        self.indexed_bytes = new_size;

        if appended == 0 {
            return Ok(None);
        }

        let total_lines = self.offsets.len().min(u32::MAX as usize) as u32;
        Ok(Some(UpdateResult {
            appended_lines: appended,
            total_lines,
            scanned_bytes,
            scan_duration: scan_start.elapsed(),
        }))
    }
}

impl FileLogSource {
    pub fn open(source_id: SourceId, path: &Path) -> Result<Self, FileError> {
        let mut index = FileIndex::open(path)?;
        index.build_index()?;
        let path_buf = path.to_path_buf();
        let display_name = path_buf.display().to_string();
        Ok(Self {
            info: SourceInfo {
                id: source_id,
                kind: SourceKind::File,
                display_name,
                locator: SourceLocator::File { path: path_buf },
            },
            index,
        })
    }

    pub fn indexed_bytes(&self) -> u64 {
        self.index.indexed_bytes()
    }

    pub fn line_count(&self) -> usize {
        self.index.line_count()
    }

    pub fn path(&self) -> &Path {
        self.index.path()
    }
}

impl LogSource for FileLogSource {
    fn info(&self) -> &SourceInfo {
        &self.info
    }

    fn total_rows(&self) -> u32 {
        self.index.line_count().min(u32::MAX as usize) as u32
    }

    fn read_rows(&self, start: u32, count: u32) -> Result<SourceRows, SourceError> {
        let start = start as usize;
        let count = count as usize;
        let decode_start = Instant::now();
        let end = start.saturating_add(count).min(self.index.line_count());
        let mut rows = Vec::with_capacity(end.saturating_sub(start));
        for row in start..end {
            if let Some(bytes) = self.index.get_line_bytes(row) {
                let (ts, sev, flags) = self.index.get_line_meta(row).unwrap_or((0, 0, 0));
                rows.push(RowRender {
                    row_id: RowId(row as u64),
                    text: String::from_utf8_lossy(bytes).into_owned(),
                    ts,
                    sev,
                    flags,
                    origin: None,
                });
            }
        }
        Ok(SourceRows {
            rows,
            decode_duration: decode_start.elapsed(),
        })
    }

    fn read_rows_by_id(&self, row_ids: &[RowId]) -> Result<SourceRows, SourceError> {
        let decode_start = Instant::now();
        let mut rows = Vec::with_capacity(row_ids.len());
        for row_id in row_ids {
            let row = row_id.0 as usize;
            if let Some(bytes) = self.index.get_line_bytes(row) {
                let (ts, sev, flags) = self.index.get_line_meta(row).unwrap_or((0, 0, 0));
                rows.push(RowRender {
                    row_id: *row_id,
                    text: String::from_utf8_lossy(bytes).into_owned(),
                    ts,
                    sev,
                    flags,
                    origin: None,
                });
            }
        }
        Ok(SourceRows {
            rows,
            decode_duration: decode_start.elapsed(),
        })
    }

    fn query_row_ids(
        &self,
        start: u32,
        end: Option<u32>,
        query: &ViewQuery,
    ) -> Result<Vec<RowId>, SourceError> {
        let start = start as usize;
        let end = end.unwrap_or(self.total_rows()) as usize;
        Ok(self.index.query_rows(start, end, query))
    }

    fn poll_update(&mut self) -> Result<Option<SourceUpdate>, SourceError> {
        self.index
            .poll_update()
            .map(|update| {
                update.map(|update| SourceUpdate {
                    appended_rows: update.appended_lines,
                    total_rows: update.total_lines,
                    scanned_bytes: update.scanned_bytes,
                    scan_duration: update.scan_duration,
                })
            })
            .map_err(SourceError::from)
    }
}

fn push_line(
    offsets: &mut Vec<u64>,
    lens: &mut Vec<u32>,
    offset: u64,
    len: u64,
) -> Result<(), FileError> {
    if offset > usize::MAX as u64 {
        return Err(FileError::OffsetOverflow(offset));
    }
    if len > u32::MAX as u64 {
        let len = usize::try_from(len).unwrap_or(usize::MAX);
        return Err(FileError::LineTooLong(len));
    }
    offsets.push(offset);
    lens.push(len as u32);
    Ok(())
}

fn push_line_with_meta(
    offsets: &mut Vec<u64>,
    lens: &mut Vec<u32>,
    ts: &mut Vec<i64>,
    sev: &mut Vec<u8>,
    flags: &mut Vec<u8>,
    bytes: &[u8],
    parser: &MetaParser,
    offset: u64,
    len: u64,
) -> Result<(), FileError> {
    push_line(offsets, lens, offset, len)?;
    let start = usize::try_from(offset).map_err(|_| FileError::OffsetOverflow(offset))?;
    let end = start
        .checked_add(len as usize)
        .ok_or(FileError::OffsetOverflow(offset))?;
    let line = &bytes[start..end];
    let meta = parser.parse_line(line);
    ts.push(meta.ts);
    sev.push(meta.sev);
    flags.push(meta.flags);
    Ok(())
}

fn sample_lines<'a>(bytes: &'a [u8], max_lines: usize) -> Vec<&'a [u8]> {
    let mut lines = Vec::new();
    let mut line_start = 0usize;
    for (i, &b) in bytes.iter().enumerate() {
        if b == b'\n' {
            let mut end = i;
            if end > line_start && bytes[end - 1] == b'\r' {
                end -= 1;
            }
            lines.push(&bytes[line_start..end]);
            line_start = i + 1;
            if lines.len() >= max_lines {
                break;
            }
        }
    }
    lines
}

impl From<FileError> for SourceError {
    fn from(err: FileError) -> Self {
        SourceError::new(err.to_string())
    }
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs::{self, File, OpenOptions};
    use std::io::Write;
    use std::path::PathBuf;

    fn write_temp(contents: &[u8]) -> PathBuf {
        let mut path = std::env::temp_dir();
        let unique = format!(
            "lgx_file_index_test_{}_{}",
            std::process::id(),
            std::time::SystemTime::now()
                .duration_since(std::time::UNIX_EPOCH)
                .unwrap()
                .as_nanos()
        );
        path.push(unique);
        let mut file = File::create(&path).expect("create temp file");
        file.write_all(contents).expect("write temp file");
        path
    }

    fn read_lines(index: &FileIndex) -> Vec<Vec<u8>> {
        (0..index.line_count())
            .filter_map(|row| index.get_line_bytes(row).map(|b| b.to_vec()))
            .collect()
    }

    #[test]
    fn indexes_lf() {
        let path = write_temp(b"a\nb\nc\n");
        let mut index = FileIndex::open(&path).expect("open");
        index.build_index().expect("build");
        assert_eq!(index.line_count(), 3);
        assert_eq!(
            read_lines(&index),
            vec![b"a".to_vec(), b"b".to_vec(), b"c".to_vec()]
        );
        let _ = fs::remove_file(path);
    }

    #[test]
    fn indexes_crlf() {
        let path = write_temp(b"a\r\nb\r\nc\r\n");
        let mut index = FileIndex::open(&path).expect("open");
        index.build_index().expect("build");
        assert_eq!(index.line_count(), 3);
        assert_eq!(
            read_lines(&index),
            vec![b"a".to_vec(), b"b".to_vec(), b"c".to_vec()]
        );
        let _ = fs::remove_file(path);
    }

    #[test]
    fn indexes_no_final_newline() {
        let path = write_temp(b"first\nsecond");
        let mut index = FileIndex::open(&path).expect("open");
        index.build_index().expect("build");
        assert_eq!(index.line_count(), 1);
        assert_eq!(read_lines(&index), vec![b"first".to_vec()]);
        let _ = fs::remove_file(path);
    }

    #[test]
    fn indexes_empty_file() {
        let path = write_temp(b"");
        let mut index = FileIndex::open(&path).expect("open");
        index.build_index().expect("build");
        assert_eq!(index.line_count(), 0);
        let _ = fs::remove_file(path);
    }

    #[test]
    fn poll_updates_partial_lines() {
        let path = write_temp(b"");
        let mut index = FileIndex::open(&path).expect("open");
        index.build_index().expect("build");
        assert_eq!(index.line_count(), 0);

        let mut file = OpenOptions::new()
            .append(true)
            .open(&path)
            .expect("append file");
        file.write_all(b"a").expect("append a");

        let update = index.poll_update().expect("poll");
        assert!(update.is_none());
        assert_eq!(index.line_count(), 0);

        file.write_all(b"\n").expect("append newline");
        let update = index.poll_update().expect("poll");
        assert!(matches!(
            update,
            Some(UpdateResult {
                appended_lines: 1,
                ..
            })
        ));
        assert_eq!(index.line_count(), 1);
        assert_eq!(read_lines(&index), vec![b"a".to_vec()]);

        file.write_all(b"b\nc\n").expect("append b/c");
        let update = index.poll_update().expect("poll");
        assert!(matches!(
            update,
            Some(UpdateResult {
                appended_lines: 2,
                ..
            })
        ));
        assert_eq!(
            read_lines(&index),
            vec![b"a".to_vec(), b"b".to_vec(), b"c".to_vec()]
        );

        let _ = fs::remove_file(path);
    }

    #[test]
    fn poll_updates_crlf() {
        let path = write_temp(b"");
        let mut index = FileIndex::open(&path).expect("open");
        index.build_index().expect("build");

        let mut file = OpenOptions::new()
            .append(true)
            .open(&path)
            .expect("append file");
        file.write_all(b"a\r").expect("append partial");
        let update = index.poll_update().expect("poll");
        assert!(update.is_none());
        assert_eq!(index.line_count(), 0);

        file.write_all(b"\n").expect("append newline");
        let update = index.poll_update().expect("poll");
        assert!(matches!(
            update,
            Some(UpdateResult {
                appended_lines: 1,
                ..
            })
        ));
        assert_eq!(read_lines(&index), vec![b"a".to_vec()]);

        let _ = fs::remove_file(path);
    }

    #[test]
    fn poll_updates_long_line_across_chunks() {
        let path = write_temp(b"");
        let mut index = FileIndex::open(&path).expect("open");
        index.build_index().expect("build");

        let mut file = OpenOptions::new()
            .append(true)
            .open(&path)
            .expect("append file");
        let mut line = vec![b'a'; SCAN_CHUNK_BYTES + 10];
        line.push(b'\n');
        file.write_all(&line).expect("append long line");

        let update = index.poll_update().expect("poll");
        assert!(matches!(
            update,
            Some(UpdateResult {
                appended_lines: 1,
                ..
            })
        ));
        assert_eq!(index.line_count(), 1);
        let bytes = index.get_line_bytes(0).expect("line bytes");
        assert_eq!(bytes.len(), SCAN_CHUNK_BYTES + 10);

        let _ = fs::remove_file(path);
    }

    #[test]
    fn poll_updates_large_append_multi_chunks() {
        let path = write_temp(b"");
        let mut index = FileIndex::open(&path).expect("open");
        index.build_index().expect("build");

        let mut file = OpenOptions::new()
            .append(true)
            .open(&path)
            .expect("append file");

        let line_len = 80usize;
        let mut line = vec![b'x'; line_len - 1];
        line.push(b'\n');
        let count = (SCAN_CHUNK_BYTES / line_len) + 10;
        let mut data = Vec::with_capacity(line_len * count);
        for _ in 0..count {
            data.extend_from_slice(&line);
        }
        file.write_all(&data).expect("append data");

        let update = index.poll_update().expect("poll");
        assert!(
            matches!(update, Some(UpdateResult { appended_lines: n, .. }) if n == count as u32)
        );
        assert_eq!(index.line_count(), count);

        let _ = fs::remove_file(path);
    }
}
