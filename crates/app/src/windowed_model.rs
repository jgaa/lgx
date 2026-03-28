use std::any::Any;
use std::cell::RefCell;
use std::sync::mpsc::Sender;

use lgx_protocol::{EngineCommand, ViewId};
use slint::{Model, ModelNotify, ModelTracker, SharedString};
use ui::RowItem;

const PLACEHOLDER_TEXT: &str = "Loading...";

pub struct WindowedRowsModel {
    state: RefCell<State>,
    notify: ModelNotify,
    request_tx: Sender<EngineCommand>,
    window_size: u32,
    prefetch: u32,
    placeholder: RowItem,
}

struct State {
    total_rows: u32,
    window_start: u32,
    rows: Vec<RowItem>,
    view_id: Option<ViewId>,
    last_request: Option<(u32, u32)>,
}

impl WindowedRowsModel {
    pub fn new(request_tx: Sender<EngineCommand>, window_size: u32, prefetch: u32) -> Self {
        Self {
            state: RefCell::new(State {
                total_rows: 0,
                window_start: 0,
                rows: Vec::with_capacity(window_size as usize),
                view_id: None,
                last_request: None,
            }),
            notify: ModelNotify::default(),
            request_tx,
            window_size: window_size.max(1),
            prefetch,
            placeholder: RowItem {
                text: SharedString::from(PLACEHOLDER_TEXT),
                raw_text: SharedString::from(PLACEHOLDER_TEXT),
                sev: 0,
                row_id: -1,
                origin_view_id: -1,
                marked: false,
            },
        }
    }

    pub fn set_view(&self, view_id: ViewId) {
        let mut state = self.state.borrow_mut();
        state.view_id = Some(view_id);
        state.total_rows = 0;
        state.window_start = 0;
        state.rows.clear();
        state.last_request = None;
        drop(state);
        self.notify.reset();
    }

    pub fn clear(&self) {
        let mut state = self.state.borrow_mut();
        state.total_rows = 0;
        state.window_start = 0;
        state.rows.clear();
        state.last_request = None;
        drop(state);
        self.notify.reset();
    }

    pub fn set_total_rows(&self, total_rows: u32) {
        let mut state = self.state.borrow_mut();
        if state.total_rows == total_rows {
            return;
        }
        state.total_rows = total_rows;
        state.last_request = None;
        drop(state);
        self.notify.reset();
    }

    pub fn update_total_rows(&self, total_rows: u32, appended: u32) {
        let mut state = self.state.borrow_mut();
        if state.total_rows == total_rows {
            return;
        }
        let prev = state.total_rows;
        state.total_rows = total_rows;
        state.last_request = None;
        drop(state);
        if appended > 0 && total_rows == prev.saturating_add(appended) {
            self.notify.row_added(prev as usize, appended as usize);
        } else {
            self.notify.reset();
        }
    }

    pub fn apply_rows(&self, start: u32, rows: Vec<RowItem>) {
        let mut state = self.state.borrow_mut();
        let old_start = state.window_start;
        let old_len = state.rows.len();
        state.window_start = start;
        state.rows.clear();
        if rows.len() > self.window_size as usize {
            state
                .rows
                .extend(rows.into_iter().take(self.window_size as usize));
        } else {
            state.rows.extend(rows);
        }
        state.last_request = None;
        let len = state.rows.len();
        drop(state);
        self.notify_range(old_start, old_len);
        self.notify_range(start, len);
    }

    pub fn current_window_start(&self) -> u32 {
        self.state.borrow().window_start
    }

    pub fn current_window_len(&self) -> u32 {
        self.state.borrow().rows.len() as u32
    }

    pub fn request_range(&self, start: u32, count: u32) {
        let count = count.max(1);
        let (view_id, total_rows, start) = {
            let mut state = self.state.borrow_mut();
            let view_id = match state.view_id {
                Some(view_id) => view_id,
                None => return,
            };
            let total_rows = state.total_rows;
            let start = if total_rows == 0 {
                0
            } else {
                start.min(total_rows.saturating_sub(1))
            };
            if state.last_request == Some((start, count)) {
                return;
            }
            state.last_request = Some((start, count));
            (view_id, total_rows, start)
        };
        let effective_count = if total_rows == 0 {
            count.max(1)
        } else {
            let remaining = total_rows.saturating_sub(start);
            count.min(remaining.max(1))
        };
        let _ = self.request_tx.send(EngineCommand::RequestRows {
            view_id,
            start,
            count: effective_count,
        });
    }

    pub fn row_id_at(&self, row: u32) -> Option<u64> {
        let state = self.state.borrow();
        if row < state.window_start
            || row >= state.window_start.saturating_add(state.rows.len() as u32)
        {
            return None;
        }
        let idx = (row - state.window_start) as usize;
        let row_id = state.rows.get(idx)?.row_id;
        (row_id >= 0).then_some(row_id as u64)
    }

    pub fn row_item_at(&self, row: u32) -> Option<RowItem> {
        let state = self.state.borrow();
        if row < state.window_start
            || row >= state.window_start.saturating_add(state.rows.len() as u32)
        {
            return None;
        }
        let idx = (row - state.window_start) as usize;
        state.rows.get(idx).cloned()
    }

    fn maybe_request_for_row(&self, row: u32) {
        let (view_id, total_rows, window_start, window_len, last_request) = {
            let state = self.state.borrow();
            let view_id = match state.view_id {
                Some(view_id) => view_id,
                None => return,
            };
            (
                view_id,
                state.total_rows,
                state.window_start,
                state.rows.len() as u32,
                state.last_request,
            )
        };

        if total_rows == 0 {
            return;
        }
        if row >= window_start && row < window_start.saturating_add(window_len) {
            return;
        }

        let max_start = total_rows.saturating_sub(1);
        let desired_start = row.saturating_sub(self.prefetch).min(max_start);
        let desired_count = self
            .window_size
            .min(total_rows.saturating_sub(desired_start))
            .max(1);
        if last_request == Some((desired_start, desired_count)) {
            return;
        }

        let _ = self.request_tx.send(EngineCommand::RequestRows {
            view_id,
            start: desired_start,
            count: desired_count,
        });

        let mut state = self.state.borrow_mut();
        state.last_request = Some((desired_start, desired_count));
    }

    fn notify_range(&self, start: u32, len: usize) {
        for idx in 0..len {
            self.notify.row_changed(start as usize + idx);
        }
    }
}

impl Model for WindowedRowsModel {
    type Data = RowItem;

    fn row_count(&self) -> usize {
        self.state.borrow().total_rows as usize
    }

    fn row_data(&self, row: usize) -> Option<Self::Data> {
        let row = row as u32;
        {
            let state = self.state.borrow();
            if row >= state.total_rows {
                return None;
            }
            if row >= state.window_start
                && row < state.window_start.saturating_add(state.rows.len() as u32)
            {
                let idx = (row - state.window_start) as usize;
                return state.rows.get(idx).cloned();
            }
        }
        self.maybe_request_for_row(row);
        Some(self.placeholder.clone())
    }

    fn model_tracker(&self) -> &dyn ModelTracker {
        &self.notify
    }

    fn as_any(&self) -> &dyn Any {
        self
    }
}
