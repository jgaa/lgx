use std::sync::atomic::{AtomicU64, Ordering};
use std::time::Duration;

#[derive(Default)]
pub struct EngineMetrics {
    indexed_bytes_total: AtomicU64,
    indexed_lines_total: AtomicU64,
    index_updates_total: AtomicU64,
    rows_requested_total: AtomicU64,
    rows_rendered_total: AtomicU64,
    rows_appended_events_total: AtomicU64,
    append_lines_aggregated_total: AtomicU64,
    index_scan_time_total_ms: AtomicU64,
    request_rows_time_total_ms: AtomicU64,
    string_decode_time_total_ms: AtomicU64,
}

#[derive(Clone, Copy, Default)]
#[allow(dead_code)]
pub struct MetricsSnapshot {
    pub indexed_bytes_total: u64,
    pub indexed_lines_total: u64,
    pub index_updates_total: u64,
    pub rows_requested_total: u64,
    pub rows_rendered_total: u64,
    pub rows_appended_events_total: u64,
    pub append_lines_aggregated_total: u64,
    pub index_scan_time_total_ms: u64,
    pub request_rows_time_total_ms: u64,
    pub string_decode_time_total_ms: u64,
}

impl EngineMetrics {
    pub fn record_index_scan(&self, bytes: u64, lines: u64, duration: Duration) {
        self.indexed_bytes_total.fetch_add(bytes, Ordering::Relaxed);
        self.indexed_lines_total.fetch_add(lines, Ordering::Relaxed);
        self.index_scan_time_total_ms
            .fetch_add(duration.as_millis() as u64, Ordering::Relaxed);
    }

    pub fn record_index_update(&self) {
        self.index_updates_total.fetch_add(1, Ordering::Relaxed);
    }

    pub fn record_rows_requested(&self, rows: u64) {
        self.rows_requested_total.fetch_add(rows, Ordering::Relaxed);
    }

    pub fn record_rows_rendered(&self, rows: u64, decode_duration: Duration) {
        self.rows_rendered_total.fetch_add(rows, Ordering::Relaxed);
        self.string_decode_time_total_ms
            .fetch_add(decode_duration.as_millis() as u64, Ordering::Relaxed);
    }

    pub fn record_rows_appended_event(&self, appended: u64) {
        self.rows_appended_events_total
            .fetch_add(1, Ordering::Relaxed);
        self.append_lines_aggregated_total
            .fetch_add(appended, Ordering::Relaxed);
    }

    pub fn record_request_rows_time(&self, duration: Duration) {
        self.request_rows_time_total_ms
            .fetch_add(duration.as_millis() as u64, Ordering::Relaxed);
    }

    pub fn snapshot(&self) -> MetricsSnapshot {
        MetricsSnapshot {
            indexed_bytes_total: self.indexed_bytes_total.load(Ordering::Relaxed),
            indexed_lines_total: self.indexed_lines_total.load(Ordering::Relaxed),
            index_updates_total: self.index_updates_total.load(Ordering::Relaxed),
            rows_requested_total: self.rows_requested_total.load(Ordering::Relaxed),
            rows_rendered_total: self.rows_rendered_total.load(Ordering::Relaxed),
            rows_appended_events_total: self.rows_appended_events_total.load(Ordering::Relaxed),
            append_lines_aggregated_total: self
                .append_lines_aggregated_total
                .load(Ordering::Relaxed),
            index_scan_time_total_ms: self.index_scan_time_total_ms.load(Ordering::Relaxed),
            request_rows_time_total_ms: self.request_rows_time_total_ms.load(Ordering::Relaxed),
            string_decode_time_total_ms: self.string_decode_time_total_ms.load(Ordering::Relaxed),
        }
    }
}
