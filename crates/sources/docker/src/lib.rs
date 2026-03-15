use std::io::{Read, Write};
use std::os::unix::net::UnixStream;
use std::time::{Instant, SystemTime, UNIX_EPOCH};

use lgx_core::{RowId, RowRender, SourceId, SourceInfo, SourceKind, SourceLocator, ViewQuery};
use lgx_parsing::{MetaParser, ParserKind, ParserSelector};
use lgx_source_api::{LogSource, SourceError, SourceRows, SourceUpdate};
use serde::Deserialize;

const DOCKER_SOCKET: &str = "/var/run/docker.sock";
const API_VERSION: &str = "v1.41";

#[derive(Debug, Clone)]
pub struct DockerContainerSummary {
    pub id: String,
    pub name: String,
    pub image: String,
    pub state: String,
    pub status: String,
}

pub struct DockerLogSource {
    client: DockerClient,
    info: SourceInfo,
    rows: Vec<DockerRow>,
    parser: MetaParser,
    last_body_bytes: usize,
    poll_since_secs: u64,
}

#[derive(Clone)]
struct DockerRow {
    text: String,
    ts: i64,
    sev: u8,
    flags: u8,
}

#[derive(Clone)]
struct DockerClient {
    socket_path: String,
}

#[derive(Deserialize)]
struct DockerContainerJson {
    #[serde(rename = "Id")]
    id: String,
    #[serde(rename = "Names", default)]
    names: Vec<String>,
    #[serde(rename = "Image", default)]
    image: String,
    #[serde(rename = "State", default)]
    state: String,
    #[serde(rename = "Status", default)]
    status: String,
}

impl DockerClient {
    fn new() -> Self {
        Self {
            socket_path: DOCKER_SOCKET.to_string(),
        }
    }

    fn get(&self, path: &str) -> Result<Vec<u8>, SourceError> {
        let mut stream = UnixStream::connect(&self.socket_path)
            .map_err(|err| SourceError::new(err.to_string()))?;
        let request = format!("GET {path} HTTP/1.1\r\nHost: docker\r\nConnection: close\r\n\r\n");
        stream
            .write_all(request.as_bytes())
            .map_err(|err| SourceError::new(err.to_string()))?;
        let mut response = Vec::new();
        stream
            .read_to_end(&mut response)
            .map_err(|err| SourceError::new(err.to_string()))?;
        parse_http_response(&response)
    }

    pub fn list_containers(&self) -> Result<Vec<DockerContainerSummary>, SourceError> {
        let path = format!("/{API_VERSION}/containers/json");
        let body = self.get(&path)?;
        let containers: Vec<DockerContainerJson> =
            serde_json::from_slice(&body).map_err(|err| SourceError::new(err.to_string()))?;
        Ok(containers
            .into_iter()
            .map(|container| DockerContainerSummary {
                id: container.id.clone(),
                name: container
                    .names
                    .first()
                    .map(|name| name.trim_start_matches('/').to_string())
                    .unwrap_or(container.id.chars().take(12).collect()),
                image: container.image,
                state: container.state,
                status: container.status,
            })
            .collect())
    }

    fn fetch_logs(
        &self,
        container_id: &str,
        tail_all: bool,
        since_secs: Option<u64>,
    ) -> Result<(Vec<String>, usize), SourceError> {
        let mut path =
            format!("/{API_VERSION}/containers/{container_id}/logs?stdout=1&stderr=1&timestamps=1");
        if tail_all {
            path.push_str("&tail=all");
        }
        if let Some(since_secs) = since_secs {
            path.push_str(&format!("&since={since_secs}"));
        }
        let body = self.get(&path)?;
        let lines = decode_docker_log_body(&body);
        Ok((lines, body.len()))
    }
}

impl DockerLogSource {
    pub fn open(
        source_id: SourceId,
        container_id: &str,
        container_name: &str,
    ) -> Result<Self, SourceError> {
        let client = DockerClient::new();
        let (lines, body_bytes) = client.fetch_logs(container_id, true, None)?;
        let parser = select_parser(&lines);
        let rows = lines
            .iter()
            .map(|line| parse_row(line, &parser))
            .collect::<Vec<_>>();
        Ok(Self {
            client,
            info: SourceInfo {
                id: source_id,
                kind: SourceKind::Docker,
                display_name: format!("docker:{container_name}"),
                locator: SourceLocator::DockerContainer {
                    id: container_id.to_string(),
                },
            },
            rows,
            parser,
            last_body_bytes: body_bytes,
            poll_since_secs: now_secs(),
        })
    }

    pub fn list_containers() -> Result<Vec<DockerContainerSummary>, SourceError> {
        DockerClient::new().list_containers()
    }

    fn container_id(&self) -> &str {
        match &self.info.locator {
            SourceLocator::DockerContainer { id } => id,
            _ => "",
        }
    }
}

impl LogSource for DockerLogSource {
    fn info(&self) -> &SourceInfo {
        &self.info
    }

    fn total_rows(&self) -> u32 {
        self.rows.len().min(u32::MAX as usize) as u32
    }

    fn read_rows(&self, start: u32, count: u32) -> Result<SourceRows, SourceError> {
        let decode_start = Instant::now();
        let start = start as usize;
        let end = start.saturating_add(count as usize).min(self.rows.len());
        let rows = self.rows[start..end]
            .iter()
            .enumerate()
            .map(|(offset, row)| RowRender {
                row_id: RowId((start + offset) as u64),
                text: row.text.clone(),
                ts: row.ts,
                sev: row.sev,
                flags: row.flags,
                origin: None,
            })
            .collect();
        Ok(SourceRows {
            rows,
            decode_duration: decode_start.elapsed(),
        })
    }

    fn read_rows_by_id(&self, row_ids: &[RowId]) -> Result<SourceRows, SourceError> {
        let decode_start = Instant::now();
        let rows = row_ids
            .iter()
            .filter_map(|row_id| {
                self.rows.get(row_id.0 as usize).map(|row| RowRender {
                    row_id: *row_id,
                    text: row.text.clone(),
                    ts: row.ts,
                    sev: row.sev,
                    flags: row.flags,
                    origin: None,
                })
            })
            .collect();
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
        let end = end.min(self.rows.len());
        let mut row_ids = Vec::new();
        for index in start..end {
            let row = &self.rows[index];
            if let Some(min) = query.min_severity {
                if row.sev < min {
                    continue;
                }
            }
            if let Some(max) = query.max_severity {
                if row.sev > max {
                    continue;
                }
            }
            if query.required_flags != 0 && row.flags & query.required_flags != query.required_flags
            {
                continue;
            }
            row_ids.push(RowId(index as u64));
        }
        Ok(row_ids)
    }

    fn poll_update(&mut self) -> Result<Option<SourceUpdate>, SourceError> {
        let scan_start = Instant::now();
        let since_secs = self.poll_since_secs.saturating_sub(1);
        let (lines, body_bytes) =
            self.client
                .fetch_logs(self.container_id(), false, Some(since_secs))?;
        self.poll_since_secs = now_secs();
        if lines.is_empty() {
            self.last_body_bytes = body_bytes;
            return Ok(None);
        }
        let overlap = overlap_len(&self.rows, &lines);
        let appended_lines = &lines[overlap..];
        if appended_lines.is_empty() {
            self.last_body_bytes = body_bytes;
            return Ok(None);
        }
        self.rows.extend(
            appended_lines
                .iter()
                .map(|line| parse_row(line, &self.parser)),
        );
        let appended = appended_lines.len().min(u32::MAX as usize) as u32;
        let scanned_bytes = body_bytes.saturating_sub(self.last_body_bytes) as u64;
        self.last_body_bytes = body_bytes;
        Ok(Some(SourceUpdate {
            appended_rows: appended,
            total_rows: self.total_rows(),
            scanned_bytes,
            scan_duration: scan_start.elapsed(),
        }))
    }
}

fn overlap_len(existing: &[DockerRow], incoming: &[String]) -> usize {
    let max_overlap = existing.len().min(incoming.len());
    for overlap in (1..=max_overlap).rev() {
        let existing_suffix = &existing[existing.len() - overlap..];
        if existing_suffix
            .iter()
            .zip(incoming.iter().take(overlap))
            .all(|(row, incoming)| row.text == *incoming)
        {
            return overlap;
        }
    }
    0
}

fn now_secs() -> u64 {
    SystemTime::now()
        .duration_since(UNIX_EPOCH)
        .unwrap_or_default()
        .as_secs()
}

fn select_parser(lines: &[String]) -> MetaParser {
    let selector = ParserSelector::new(32);
    let samples = lines
        .iter()
        .take(32)
        .map(|line| line.as_bytes())
        .collect::<Vec<_>>();
    if samples.is_empty() {
        MetaParser::new(ParserKind::Plain)
    } else {
        selector.select(samples)
    }
}

fn parse_row(line: &str, parser: &MetaParser) -> DockerRow {
    let meta = parser.parse_line(line.as_bytes());
    DockerRow {
        text: line.to_string(),
        ts: meta.ts,
        sev: meta.sev,
        flags: meta.flags,
    }
}

fn parse_http_response(response: &[u8]) -> Result<Vec<u8>, SourceError> {
    let header_end = response
        .windows(4)
        .position(|window| window == b"\r\n\r\n")
        .ok_or_else(|| SourceError::new("invalid HTTP response"))?;
    let headers = &response[..header_end];
    let body = &response[header_end + 4..];
    let status_line_end = headers
        .windows(2)
        .position(|window| window == b"\r\n")
        .unwrap_or(headers.len());
    let status_line = String::from_utf8_lossy(&headers[..status_line_end]);
    if !status_line.contains(" 200 ") {
        return Err(SourceError::new(format!(
            "docker API request failed: {status_line}"
        )));
    }
    if has_chunked_transfer_encoding(headers) {
        decode_chunked_body(body)
    } else {
        Ok(body.to_vec())
    }
}

fn has_chunked_transfer_encoding(headers: &[u8]) -> bool {
    String::from_utf8_lossy(headers).lines().any(|line| {
        line.to_ascii_lowercase()
            .starts_with("transfer-encoding: chunked")
    })
}

fn decode_chunked_body(body: &[u8]) -> Result<Vec<u8>, SourceError> {
    let mut decoded = Vec::new();
    let mut offset = 0usize;

    while offset < body.len() {
        let size_line_end = body[offset..]
            .windows(2)
            .position(|window| window == b"\r\n")
            .map(|pos| offset + pos)
            .ok_or_else(|| SourceError::new("invalid chunked response: missing chunk size"))?;
        let size_text = String::from_utf8_lossy(&body[offset..size_line_end]);
        let size_hex = size_text.split(';').next().unwrap_or("").trim();
        let chunk_size = usize::from_str_radix(size_hex, 16)
            .map_err(|_| SourceError::new("invalid chunked response: bad chunk size"))?;
        offset = size_line_end + 2;
        if chunk_size == 0 {
            return Ok(decoded);
        }
        let chunk_end = offset
            .checked_add(chunk_size)
            .ok_or_else(|| SourceError::new("invalid chunked response: chunk overflow"))?;
        if chunk_end + 2 > body.len() {
            return Err(SourceError::new(
                "invalid chunked response: truncated chunk body",
            ));
        }
        decoded.extend_from_slice(&body[offset..chunk_end]);
        if &body[chunk_end..chunk_end + 2] != b"\r\n" {
            return Err(SourceError::new(
                "invalid chunked response: missing chunk terminator",
            ));
        }
        offset = chunk_end + 2;
    }

    Err(SourceError::new(
        "invalid chunked response: missing terminating chunk",
    ))
}

fn decode_docker_log_body(body: &[u8]) -> Vec<String> {
    let decoded = if body.len() >= 8 && looks_like_docker_stream(body) {
        decode_multiplexed_stream(body)
    } else {
        String::from_utf8_lossy(body).into_owned()
    };
    decoded
        .lines()
        .map(|line| line.trim_end_matches('\r').to_string())
        .filter(|line| !line.is_empty())
        .collect()
}

fn looks_like_docker_stream(body: &[u8]) -> bool {
    matches!(body[0], 0 | 1 | 2)
}

fn decode_multiplexed_stream(body: &[u8]) -> String {
    let mut out = String::new();
    let mut offset = 0usize;
    while offset + 8 <= body.len() {
        let len = u32::from_be_bytes([
            body[offset + 4],
            body[offset + 5],
            body[offset + 6],
            body[offset + 7],
        ]) as usize;
        offset += 8;
        if offset + len > body.len() {
            break;
        }
        out.push_str(&String::from_utf8_lossy(&body[offset..offset + len]));
        offset += len;
    }
    out
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn decodes_raw_body() {
        let lines = decode_docker_log_body(b"one\ntwo\n");
        assert_eq!(lines, vec!["one".to_string(), "two".to_string()]);
    }

    #[test]
    fn decodes_multiplexed_body() {
        let mut body = Vec::new();
        body.extend_from_slice(&[1, 0, 0, 0, 0, 0, 0, 4]);
        body.extend_from_slice(b"one\n");
        body.extend_from_slice(&[2, 0, 0, 0, 0, 0, 0, 4]);
        body.extend_from_slice(b"two\n");
        let lines = decode_docker_log_body(&body);
        assert_eq!(lines, vec!["one".to_string(), "two".to_string()]);
    }

    #[test]
    fn parses_http_response() {
        let response = b"HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc";
        let body = parse_http_response(response).expect("body");
        assert_eq!(body, b"abc");
    }

    #[test]
    fn parses_chunked_http_response() {
        let response =
            b"HTTP/1.1 200 OK\r\nTransfer-Encoding: chunked\r\n\r\n3\r\nabc\r\n4\r\n1234\r\n0\r\n\r\n";
        let body = parse_http_response(response).expect("body");
        assert_eq!(body, b"abc1234");
    }

    #[test]
    fn overlap_dedupes_prefix_already_seen() {
        let existing = vec![
            DockerRow {
                text: "one".to_string(),
                ts: 0,
                sev: 0,
                flags: 0,
            },
            DockerRow {
                text: "two".to_string(),
                ts: 0,
                sev: 0,
                flags: 0,
            },
        ];
        let incoming = vec!["two".to_string(), "three".to_string()];
        assert_eq!(overlap_len(&existing, &incoming), 1);
    }
}
