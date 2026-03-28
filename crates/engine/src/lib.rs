use std::collections::HashMap;
use std::path::Path;
use std::sync::{mpsc, Arc, Mutex};
use std::thread;
use std::time::{Duration, Instant};

use lgx_core::{RowId, RowRef, SourceId, ViewId, ViewInfo, ViewKind, ViewQuery};
use lgx_protocol::{DockerContainer, EngineCommand, EngineEvent};
use lgx_source_api::LogSource;
use lgx_source_docker::DockerLogSource;
use lgx_source_file::FileLogSource;

mod metrics;
use metrics::EngineMetrics;

const INITIAL_LINES: usize = 200;
const POLL_INTERVAL: Duration = Duration::from_millis(200);
const MAX_UI_HZ: u64 = 20;
const UI_EVENT_INTERVAL: Duration = Duration::from_millis(1000 / MAX_UI_HZ);

pub struct EngineHandle {
    _worker: thread::JoinHandle<()>,
}

impl EngineHandle {
    pub fn new() -> (
        Self,
        mpsc::Sender<EngineCommand>,
        mpsc::Receiver<EngineEvent>,
    ) {
        let (cmd_tx, cmd_rx) = mpsc::channel();
        let (evt_tx, evt_rx) = mpsc::channel();

        let state = Arc::new(Mutex::new(EngineState::new()));
        let metrics = Arc::new(EngineMetrics::default());

        let worker_state = Arc::clone(&state);
        let worker_evt = evt_tx.clone();
        let worker_metrics = Arc::clone(&metrics);
        let worker = thread::spawn(move || {
            let mut engine = Engine::new(worker_state, worker_evt, worker_metrics);
            engine.run(cmd_rx);
        });

        start_stats_ticker(evt_tx.clone(), metrics);

        (Self { _worker: worker }, cmd_tx, evt_rx)
    }
}

struct Engine {
    state: Arc<Mutex<EngineState>>,
    evt_tx: mpsc::Sender<EngineEvent>,
    metrics: Arc<EngineMetrics>,
}

impl Engine {
    fn new(
        state: Arc<Mutex<EngineState>>,
        evt_tx: mpsc::Sender<EngineEvent>,
        metrics: Arc<EngineMetrics>,
    ) -> Self {
        Self {
            state,
            evt_tx,
            metrics,
        }
    }

    fn run(&mut self, cmd_rx: mpsc::Receiver<EngineCommand>) {
        while let Ok(cmd) = cmd_rx.recv() {
            match cmd {
                EngineCommand::OpenFile { path } => self.handle_open_file(path),
                EngineCommand::OpenFilteredView {
                    source_view_id,
                    query,
                } => self.handle_open_filtered_view(source_view_id, query),
                EngineCommand::CloseView { view_id } => self.handle_close_view(view_id),
                EngineCommand::JumpToFullView {
                    source_view_id,
                    row_id,
                } => self.handle_jump_to_full_view(source_view_id, row_id),
                EngineCommand::ListDockerContainers => self.handle_list_docker_containers(),
                EngineCommand::OpenDockerContainer {
                    container_id,
                    title,
                } => self.handle_open_docker_container(container_id, title),
                EngineCommand::ToggleMarkRow {
                    source_view_id,
                    row_id,
                } => self.handle_toggle_mark_row(source_view_id, row_id),
                EngineCommand::RequestRows {
                    view_id,
                    start,
                    count,
                } => {
                    self.handle_request_rows(view_id, start, count);
                }
                EngineCommand::SetFollow { view_id, enabled } => {
                    self.handle_set_follow(view_id, enabled);
                }
            }
        }
    }

    fn handle_open_file(&mut self, path: String) {
        let ids = {
            let mut state = self.state.lock().expect("engine state lock");
            let source_id = SourceId(state.next_source_id);
            state.next_source_id += 1;
            let view_id = ViewId(state.next_view_id);
            state.next_view_id += 1;
            (source_id, view_id)
        };

        let (source_id, view_id) = ids;
        let _ = self.evt_tx.send(EngineEvent::ViewBusy {
            view_id,
            busy: true,
        });

        let evt_tx = self.evt_tx.clone();
        let metrics = Arc::clone(&self.metrics);
        let state = Arc::clone(&self.state);
        thread::spawn(move || {
            let index_start = Instant::now();
            match FileLogSource::open(source_id, Path::new(&path)) {
                Ok(source) => {
                    let scan_duration = index_start.elapsed();
                    let total_rows = source.total_rows();
                    let indexed_bytes = source.indexed_bytes();
                    metrics.record_index_scan(indexed_bytes, total_rows as u64, scan_duration);

                    let display_path = source.path().display().to_string();
                    let source = Arc::new(Mutex::new(SourceHandle {
                        source: Box::new(source),
                    }));
                    {
                        let mut state = state.lock().expect("engine state lock");
                        state.sources.insert(source_id, Arc::clone(&source));
                        state.views.insert(
                            view_id,
                            ViewInfo {
                                id: view_id,
                                source_id,
                                kind: ViewKind::Full,
                                parent: None,
                                title: display_path.clone(),
                                total_rows,
                                follow_enabled: false,
                                query: ViewQuery::default(),
                            },
                        );
                        state.runtime_views.insert(view_id, RuntimeView::Full);
                    }

                    let _ = evt_tx.send(EngineEvent::FileOpened {
                        source_id,
                        view_id,
                        path: path.clone(),
                        total_lines_estimate: Some(total_rows),
                    });
                    let _ = evt_tx.send(EngineEvent::ViewStats {
                        view_id,
                        total_rows,
                    });

                    let rows = {
                        let source = source.lock().expect("source lock");
                        source.read_rows(0, INITIAL_LINES as u32)
                    };
                    match rows {
                        Ok(rows) => {
                            metrics
                                .record_rows_rendered(rows.rows.len() as u64, rows.decode_duration);
                            let _ = evt_tx.send(EngineEvent::RowsReady {
                                view_id,
                                start: 0,
                                rows: rows.rows,
                            });
                            let _ = evt_tx.send(EngineEvent::ViewBusy {
                                view_id,
                                busy: false,
                            });
                        }
                        Err(err) => {
                            let _ = evt_tx.send(EngineEvent::Error {
                                message: format!("failed to read {path}: {err}"),
                            });
                            let _ = evt_tx.send(EngineEvent::ViewBusy {
                                view_id,
                                busy: false,
                            });
                            return;
                        }
                    }

                    let poll_state = Arc::clone(&state);
                    let poll_evt = evt_tx.clone();
                    let poll_source = Arc::clone(&source);
                    let poll_metrics = Arc::clone(&metrics);
                    thread::spawn(move || {
                        let mut pending_appended: u32 = 0;
                        let mut pending_total_rows: u32 = 0;
                        let mut last_emit = Instant::now();
                        loop {
                            thread::sleep(POLL_INTERVAL);
                            let update = {
                                let mut source = poll_source.lock().expect("source lock");
                                source.poll_update()
                            };

                            match update {
                                Ok(Some(update)) => {
                                    poll_metrics.record_index_scan(
                                        update.scanned_bytes,
                                        update.appended_rows as u64,
                                        update.scan_duration,
                                    );
                                    if update.appended_rows > 0 {
                                        poll_metrics.record_index_update();
                                    }

                                    let view_updates = {
                                        let mut state =
                                            poll_state.lock().expect("engine state lock");
                                        state.apply_source_update(source_id, &poll_source, &update)
                                    };
                                    for (updated_view_id, appended, total_rows) in view_updates {
                                        if updated_view_id == view_id {
                                            pending_total_rows = total_rows;
                                            pending_appended =
                                                pending_appended.saturating_add(appended);
                                        } else if appended > 0 {
                                            let _ = poll_evt.send(EngineEvent::RowsAppended {
                                                view_id: updated_view_id,
                                                appended,
                                                total_rows,
                                            });
                                            poll_metrics
                                                .record_rows_appended_event(appended as u64);
                                        }
                                    }
                                }
                                Ok(None) => {}
                                Err(err) => {
                                    let _ = poll_evt.send(EngineEvent::Error {
                                        message: format!("poll update failed for {path}: {err}"),
                                    });
                                }
                            }

                            if pending_appended > 0 && last_emit.elapsed() >= UI_EVENT_INTERVAL {
                                let _ = poll_evt.send(EngineEvent::RowsAppended {
                                    view_id,
                                    appended: pending_appended,
                                    total_rows: pending_total_rows,
                                });
                                poll_metrics.record_rows_appended_event(pending_appended as u64);
                                pending_appended = 0;
                                last_emit = Instant::now();
                            }
                        }
                    });
                }
                Err(err) => {
                    let message = format!("failed to open {path}: {err}");
                    let _ = evt_tx.send(EngineEvent::Error { message });
                    let _ = evt_tx.send(EngineEvent::ViewBusy {
                        view_id,
                        busy: false,
                    });
                }
            }
        });
    }

    fn handle_list_docker_containers(&mut self) {
        match DockerLogSource::list_containers() {
            Ok(containers) => {
                let containers = containers
                    .into_iter()
                    .map(|container| DockerContainer {
                        id: container.id,
                        name: container.name,
                        image: container.image,
                        status: container.status,
                    })
                    .collect();
                let _ = self
                    .evt_tx
                    .send(EngineEvent::DockerContainersListed { containers });
            }
            Err(err) => {
                let _ = self.evt_tx.send(EngineEvent::Error {
                    message: format!("failed to list Docker containers: {err}"),
                });
            }
        }
    }

    fn handle_open_docker_container(&mut self, container_id: String, title: String) {
        let ids = {
            let mut state = self.state.lock().expect("engine state lock");
            let source_id = SourceId(state.next_source_id);
            state.next_source_id += 1;
            let view_id = ViewId(state.next_view_id);
            state.next_view_id += 1;
            (source_id, view_id)
        };

        let (source_id, view_id) = ids;
        let _ = self.evt_tx.send(EngineEvent::ViewBusy {
            view_id,
            busy: true,
        });

        let evt_tx = self.evt_tx.clone();
        let metrics = Arc::clone(&self.metrics);
        let state = Arc::clone(&self.state);
        thread::spawn(move || {
            let index_start = Instant::now();
            match DockerLogSource::open(source_id, &container_id, &title) {
                Ok(source) => {
                    let scan_duration = index_start.elapsed();
                    let total_rows = source.total_rows();
                    metrics.record_index_scan(total_rows as u64, total_rows as u64, scan_duration);

                    let source = Arc::new(Mutex::new(SourceHandle {
                        source: Box::new(source),
                    }));
                    {
                        let mut state = state.lock().expect("engine state lock");
                        state.sources.insert(source_id, Arc::clone(&source));
                        state.views.insert(
                            view_id,
                            ViewInfo {
                                id: view_id,
                                source_id,
                                kind: ViewKind::Full,
                                parent: None,
                                title: title.clone(),
                                total_rows,
                                follow_enabled: true,
                                query: ViewQuery::default(),
                            },
                        );
                        state.runtime_views.insert(view_id, RuntimeView::Full);
                    }

                    let _ = evt_tx.send(EngineEvent::ViewOpened {
                        source_id,
                        view_id,
                        kind: ViewKind::Full,
                        title: title.clone(),
                        total_rows,
                        query: ViewQuery::default(),
                    });
                    let _ = evt_tx.send(EngineEvent::ViewStats {
                        view_id,
                        total_rows,
                    });

                    let rows = {
                        let source = source.lock().expect("source lock");
                        source.read_rows(0, INITIAL_LINES as u32)
                    };
                    match rows {
                        Ok(rows) => {
                            metrics
                                .record_rows_rendered(rows.rows.len() as u64, rows.decode_duration);
                            let _ = evt_tx.send(EngineEvent::RowsReady {
                                view_id,
                                start: 0,
                                rows: rows.rows,
                            });
                            let _ = evt_tx.send(EngineEvent::ViewBusy {
                                view_id,
                                busy: false,
                            });
                        }
                        Err(err) => {
                            let _ = evt_tx.send(EngineEvent::Error {
                                message: format!("failed to read Docker logs for {title}: {err}"),
                            });
                            let _ = evt_tx.send(EngineEvent::ViewBusy {
                                view_id,
                                busy: false,
                            });
                            return;
                        }
                    }

                    let poll_state = Arc::clone(&state);
                    let poll_evt = evt_tx.clone();
                    let poll_source = Arc::clone(&source);
                    let poll_metrics = Arc::clone(&metrics);
                    thread::spawn(move || {
                        let mut pending_appended = 0u32;
                        let mut pending_total_rows = 0u32;
                        let mut last_emit = Instant::now();
                        loop {
                            thread::sleep(POLL_INTERVAL);
                            let update = {
                                let mut source = poll_source.lock().expect("source lock");
                                source.poll_update()
                            };

                            match update {
                                Ok(Some(update)) => {
                                    poll_metrics.record_index_scan(
                                        update.scanned_bytes,
                                        update.appended_rows as u64,
                                        update.scan_duration,
                                    );
                                    let view_updates = {
                                        let mut state =
                                            poll_state.lock().expect("engine state lock");
                                        state.apply_source_update(source_id, &poll_source, &update)
                                    };
                                    for (updated_view_id, appended, total_rows) in view_updates {
                                        if updated_view_id == view_id {
                                            pending_total_rows = total_rows;
                                            pending_appended =
                                                pending_appended.saturating_add(appended);
                                        } else if appended > 0 {
                                            let _ = poll_evt.send(EngineEvent::RowsAppended {
                                                view_id: updated_view_id,
                                                appended,
                                                total_rows,
                                            });
                                        }
                                    }
                                }
                                Ok(None) => {}
                                Err(err) => {
                                    let _ = poll_evt.send(EngineEvent::Error {
                                        message: format!(
                                            "poll update failed for Docker container {title}: {err}"
                                        ),
                                    });
                                }
                            }

                            if pending_appended > 0 && last_emit.elapsed() >= UI_EVENT_INTERVAL {
                                let _ = poll_evt.send(EngineEvent::RowsAppended {
                                    view_id,
                                    appended: pending_appended,
                                    total_rows: pending_total_rows,
                                });
                                pending_appended = 0;
                                last_emit = Instant::now();
                            }
                        }
                    });
                }
                Err(err) => {
                    let _ = evt_tx.send(EngineEvent::Error {
                        message: format!("failed to open Docker container {title}: {err}"),
                    });
                    let _ = evt_tx.send(EngineEvent::ViewBusy {
                        view_id,
                        busy: false,
                    });
                }
            }
        });
    }

    fn handle_request_rows(&mut self, view_id: ViewId, start: u32, count: u32) {
        self.metrics.record_rows_requested(count as u64);
        let request_start = Instant::now();
        let (source, runtime_view, sources) = {
            let state = self.state.lock().expect("engine state lock");
            let view = match state.views.get(&view_id) {
                Some(view) => view,
                None => {
                    let _ = self.evt_tx.send(EngineEvent::Error {
                        message: format!("unknown view {view_id}"),
                    });
                    return;
                }
            };
            let runtime_view = match state.runtime_views.get(&view_id) {
                Some(view) => view.clone(),
                None => {
                    let _ = self.evt_tx.send(EngineEvent::Error {
                        message: format!("missing runtime view {view_id}"),
                    });
                    return;
                }
            };
            let source = state.sources.get(&view.source_id).cloned();
            (source, runtime_view, state.sources.clone())
        };

        let rows = match &runtime_view {
            RuntimeView::Full => {
                let Some(source) = source else {
                    let _ = self.evt_tx.send(EngineEvent::Error {
                        message: format!("unknown source for view {view_id}"),
                    });
                    return;
                };
                let source = source.lock().expect("source lock");
                source.read_rows(start, count)
            }
            RuntimeView::Filtered { row_ids } => {
                let Some(source) = source else {
                    let _ = self.evt_tx.send(EngineEvent::Error {
                        message: format!("unknown source for view {view_id}"),
                    });
                    return;
                };
                let source = source.lock().expect("source lock");
                let start = start as usize;
                let end = start.saturating_add(count as usize).min(row_ids.len());
                source.read_rows_by_id(&row_ids[start..end])
            }
            RuntimeView::Marked { rows } => {
                let start = start as usize;
                let end = start.saturating_add(count as usize).min(rows.len());
                let slice = &rows[start..end];
                let mut rendered = Vec::with_capacity(slice.len());
                let decode_start = Instant::now();
                for entry in slice {
                    let Some(source) = sources.get(&entry.source_id) else {
                        continue;
                    };
                    let source = source.lock().expect("source lock");
                    if let Ok(source_rows) = source.read_rows_by_id(&[entry.row_id]) {
                        if let Some(mut row) = source_rows.rows.into_iter().next() {
                            row.origin = Some(RowRef {
                                view_id: entry.source_view_id,
                                row_id: entry.row_id,
                            });
                            rendered.push(row);
                        }
                    }
                }
                Ok(lgx_source_api::SourceRows {
                    rows: rendered,
                    decode_duration: decode_start.elapsed(),
                })
            }
        };
        match rows {
            Ok(mut rows) => {
                if let RuntimeView::Filtered { .. } = runtime_view {
                    for row in &mut rows.rows {
                        row.origin = Some(RowRef {
                            view_id,
                            row_id: row.row_id,
                        });
                    }
                }
                self.metrics
                    .record_rows_rendered(rows.rows.len() as u64, rows.decode_duration);
                self.metrics
                    .record_request_rows_time(request_start.elapsed());
                let _ = self.evt_tx.send(EngineEvent::RowsReady {
                    view_id,
                    start,
                    rows: rows.rows,
                });
            }
            Err(err) => {
                let _ = self.evt_tx.send(EngineEvent::Error {
                    message: format!("failed to read rows for {view_id}: {err}"),
                });
            }
        }
    }

    fn handle_open_filtered_view(&mut self, source_view_id: ViewId, query: ViewQuery) {
        let (view_id, source_id, parent_title) = {
            let mut state = self.state.lock().expect("engine state lock");
            let source_id = match state.views.get(&source_view_id) {
                Some(view) => view.source_id,
                None => {
                    let _ = self.evt_tx.send(EngineEvent::Error {
                        message: format!("unknown source view {source_view_id}"),
                    });
                    return;
                }
            };
            let parent_title = state
                .views
                .get(&source_view_id)
                .map(|view| view.title.clone())
                .unwrap_or_default();
            let view_id = ViewId(state.next_view_id);
            state.next_view_id += 1;
            (view_id, source_id, parent_title)
        };

        let source = {
            let state = self.state.lock().expect("engine state lock");
            match state.sources.get(&source_id) {
                Some(source) => Arc::clone(source),
                None => {
                    let _ = self.evt_tx.send(EngineEvent::Error {
                        message: format!("unknown source {source_id}"),
                    });
                    return;
                }
            }
        };

        let row_ids = {
            let source = source.lock().expect("source lock");
            match source.query_row_ids(0, None, &query) {
                Ok(row_ids) => row_ids,
                Err(err) => {
                    let _ = self.evt_tx.send(EngineEvent::Error {
                        message: format!("failed to create filtered view: {err}"),
                    });
                    return;
                }
            }
        };

        let title = filtered_view_title(&parent_title, &query);
        let total_rows = row_ids.len().min(u32::MAX as usize) as u32;
        {
            let mut state = self.state.lock().expect("engine state lock");
            state.views.insert(
                view_id,
                ViewInfo {
                    id: view_id,
                    source_id,
                    kind: ViewKind::Filtered,
                    parent: Some(source_view_id),
                    title: title.clone(),
                    total_rows,
                    follow_enabled: false,
                    query: query.clone(),
                },
            );
            state
                .runtime_views
                .insert(view_id, RuntimeView::Filtered { row_ids });
        }

        let _ = self.evt_tx.send(EngineEvent::ViewOpened {
            source_id,
            view_id,
            kind: ViewKind::Filtered,
            title,
            total_rows,
            query,
        });
        let _ = self.evt_tx.send(EngineEvent::ViewStats {
            view_id,
            total_rows,
        });
    }

    fn handle_set_follow(&mut self, view_id: ViewId, enabled: bool) {
        let total_rows = {
            let mut state = self.state.lock().expect("engine state lock");
            let view = match state.views.get_mut(&view_id) {
                Some(view) => view,
                None => {
                    let _ = self.evt_tx.send(EngineEvent::Error {
                        message: format!("unknown view {view_id}"),
                    });
                    return;
                }
            };
            view.follow_enabled = enabled;
            view.total_rows
        };

        if enabled {
            let _ = self.evt_tx.send(EngineEvent::RowsAppended {
                view_id,
                appended: 0,
                total_rows,
            });
            self.metrics.record_rows_appended_event(0);
        }
    }

    fn handle_close_view(&mut self, view_id: ViewId) {
        let mut state = self.state.lock().expect("engine state lock");
        state.views.remove(&view_id);
        state.runtime_views.remove(&view_id);
        if state.marked_view_id == Some(view_id) {
            state.marked_view_id = None;
        }
    }

    fn handle_jump_to_full_view(&mut self, source_view_id: ViewId, row_id: RowId) {
        let target = {
            let state = self.state.lock().expect("engine state lock");
            let source_id = match state.views.get(&source_view_id) {
                Some(view) => view.source_id,
                None => {
                    let _ = self.evt_tx.send(EngineEvent::Error {
                        message: format!("unknown source view {source_view_id}"),
                    });
                    return;
                }
            };
            state.views.iter().find_map(|(view_id, view)| {
                (view.source_id == source_id && matches!(view.kind, ViewKind::Full))
                    .then_some(*view_id)
            })
        };

        match target {
            Some(view_id) => {
                let row = row_id.0.min(u32::MAX as u64) as u32;
                let _ = self.evt_tx.send(EngineEvent::JumpTarget { view_id, row });
            }
            None => {
                let _ = self.evt_tx.send(EngineEvent::Error {
                    message: "no full view exists for this source".to_string(),
                });
            }
        }
    }

    fn handle_toggle_mark_row(&mut self, source_view_id: ViewId, row_id: RowId) {
        let (marked_view_id, should_open, total_rows, existed_before) = {
            let mut state = self.state.lock().expect("engine state lock");
            let source_view = match state.views.get(&source_view_id).cloned() {
                Some(view) => view,
                None => {
                    let _ = self.evt_tx.send(EngineEvent::Error {
                        message: format!("unknown source view {source_view_id}"),
                    });
                    return;
                }
            };
            let marked_view_id = match state.marked_view_id {
                Some(view_id) => view_id,
                None => {
                    let view_id = ViewId(state.next_view_id);
                    state.next_view_id += 1;
                    state.views.insert(
                        view_id,
                        ViewInfo {
                            id: view_id,
                            source_id: source_view.source_id,
                            kind: ViewKind::Marked,
                            parent: None,
                            title: "Marked".to_string(),
                            total_rows: 0,
                            follow_enabled: false,
                            query: ViewQuery::default(),
                        },
                    );
                    state
                        .runtime_views
                        .insert(view_id, RuntimeView::Marked { rows: Vec::new() });
                    state.marked_view_id = Some(view_id);
                    view_id
                }
            };

            let existed_before = matches!(
                state.runtime_views.get(&marked_view_id),
                Some(RuntimeView::Marked { rows }) if !rows.is_empty()
            );
            let runtime = state.runtime_views.get_mut(&marked_view_id);
            let mut should_open = false;
            let total_rows = match runtime {
                Some(RuntimeView::Marked { rows }) => {
                    if let Some(index) = rows.iter().position(|entry| {
                        entry.source_view_id == source_view_id && entry.row_id == row_id
                    }) {
                        rows.remove(index);
                    } else {
                        rows.push(MarkedRowEntry {
                            source_view_id,
                            source_id: source_view.source_id,
                            row_id,
                        });
                        if rows.len() == 1 {
                            should_open = true;
                        }
                    }
                    rows.len().min(u32::MAX as usize) as u32
                }
                _ => 0,
            };
            if let Some(view) = state.views.get_mut(&marked_view_id) {
                view.total_rows = total_rows;
            }
            (marked_view_id, should_open, total_rows, existed_before)
        };

        if should_open {
            let _ = self.evt_tx.send(EngineEvent::ViewOpened {
                source_id: SourceId(0),
                view_id: marked_view_id,
                kind: ViewKind::Marked,
                title: "Marked".to_string(),
                total_rows,
                query: ViewQuery::default(),
            });
        }
        let _ = self.evt_tx.send(EngineEvent::ViewStats {
            view_id: marked_view_id,
            total_rows,
        });
        if existed_before || should_open {
            let _ = self.evt_tx.send(EngineEvent::RowsAppended {
                view_id: marked_view_id,
                appended: 0,
                total_rows,
            });
        }
    }
}

fn filtered_view_title(parent_title: &str, query: &ViewQuery) -> String {
    let mut parts = Vec::new();
    if let Some(mask) = query.severity_mask {
        let labels = [
            (1u8 << 0, "trace"),
            (1u8 << 1, "debug"),
            (1u8 << 2, "info"),
            (1u8 << 3, "warn"),
            (1u8 << 4, "error"),
            (1u8 << 5, "fatal"),
        ]
        .into_iter()
        .filter_map(|(bit, label)| (mask & bit != 0).then_some(label))
        .collect::<Vec<_>>();
        parts.push(format!("sev={}", labels.join("|")));
    }
    if let Some(min) = query.min_severity {
        parts.push(format!("sev>={min}"));
    }
    if query.required_flags & 1 != 0 {
        parts.push("has-ts".to_string());
    }
    if query.required_flags & 2 != 0 {
        parts.push("has-sev".to_string());
    }
    if let Some(text) = query.text.as_ref().filter(|text| !text.is_empty()) {
        let mode = if query.text_is_regex { "re" } else { "text" };
        let scope = if query.text_message_only {
            "msg"
        } else {
            "raw"
        };
        let case = if query.text_case_insensitive {
            "icase"
        } else {
            "case"
        };
        parts.push(format!("{mode}:{scope}:{case}:{text}"));
    }
    if parts.is_empty() {
        parent_title.to_string()
    } else {
        format!("{parent_title} [{}]", parts.join(", "))
    }
}

struct EngineState {
    next_source_id: u64,
    next_view_id: u64,
    sources: HashMap<SourceId, Arc<Mutex<SourceHandle>>>,
    views: HashMap<ViewId, ViewInfo>,
    runtime_views: HashMap<ViewId, RuntimeView>,
    marked_view_id: Option<ViewId>,
}

impl EngineState {
    fn new() -> Self {
        Self {
            next_source_id: 1,
            next_view_id: 1,
            sources: HashMap::new(),
            views: HashMap::new(),
            runtime_views: HashMap::new(),
            marked_view_id: None,
        }
    }

    fn apply_source_update(
        &mut self,
        source_id: SourceId,
        source: &Arc<Mutex<SourceHandle>>,
        update: &lgx_source_api::SourceUpdate,
    ) -> Vec<(ViewId, u32, u32)> {
        let mut events = Vec::new();
        let appended_start = update.total_rows.saturating_sub(update.appended_rows);

        for (view_id, view) in &mut self.views {
            if view.source_id != source_id {
                continue;
            }
            match self.runtime_views.get_mut(view_id) {
                Some(RuntimeView::Full) => {
                    view.total_rows = update.total_rows;
                    events.push((*view_id, update.appended_rows, update.total_rows));
                }
                Some(RuntimeView::Filtered { row_ids }) => {
                    let appended_ids = {
                        let source = source.lock().expect("source lock");
                        source.query_row_ids(appended_start, Some(update.total_rows), &view.query)
                    };
                    if let Ok(appended_ids) = appended_ids {
                        let appended = appended_ids.len().min(u32::MAX as usize) as u32;
                        row_ids.extend(appended_ids);
                        view.total_rows = row_ids.len().min(u32::MAX as usize) as u32;
                        events.push((*view_id, appended, view.total_rows));
                    }
                }
                Some(RuntimeView::Marked { .. }) => {}
                None => {}
            }
        }

        events
    }
}

#[derive(Clone)]
enum RuntimeView {
    Full,
    Filtered { row_ids: Vec<RowId> },
    Marked { rows: Vec<MarkedRowEntry> },
}

#[derive(Clone)]
struct MarkedRowEntry {
    source_view_id: ViewId,
    source_id: SourceId,
    row_id: RowId,
}

struct SourceHandle {
    source: Box<dyn LogSource>,
}

impl SourceHandle {
    fn read_rows(
        &self,
        start: u32,
        count: u32,
    ) -> Result<lgx_source_api::SourceRows, lgx_source_api::SourceError> {
        self.source.read_rows(start, count)
    }

    fn read_rows_by_id(
        &self,
        row_ids: &[RowId],
    ) -> Result<lgx_source_api::SourceRows, lgx_source_api::SourceError> {
        self.source.read_rows_by_id(row_ids)
    }

    fn query_row_ids(
        &self,
        start: u32,
        end: Option<u32>,
        query: &ViewQuery,
    ) -> Result<Vec<RowId>, lgx_source_api::SourceError> {
        self.source.query_row_ids(start, end, query)
    }

    fn poll_update(
        &mut self,
    ) -> Result<Option<lgx_source_api::SourceUpdate>, lgx_source_api::SourceError> {
        self.source.poll_update()
    }
}

fn start_stats_ticker(evt_tx: mpsc::Sender<EngineEvent>, metrics: Arc<EngineMetrics>) {
    thread::spawn(move || {
        let mut last_snapshot = metrics.snapshot();
        let mut last_at = Instant::now();
        loop {
            thread::sleep(Duration::from_secs(1));
            let now = Instant::now();
            let elapsed = now.duration_since(last_at);
            let seconds = elapsed.as_secs_f64().max(0.001);
            let snapshot = metrics.snapshot();

            let indexed_lines = snapshot
                .indexed_lines_total
                .saturating_sub(last_snapshot.indexed_lines_total);
            let indexed_bytes = snapshot
                .indexed_bytes_total
                .saturating_sub(last_snapshot.indexed_bytes_total);
            let rows_requested = snapshot
                .rows_requested_total
                .saturating_sub(last_snapshot.rows_requested_total);
            let ui_events = snapshot
                .rows_appended_events_total
                .saturating_sub(last_snapshot.rows_appended_events_total);

            let index_lps = indexed_lines as f64 / seconds;
            let indexed_mb_s = indexed_bytes as f64 / (1024.0 * 1024.0 * seconds);
            let rows_req_s = rows_requested as f64 / seconds;
            let ui_events_s = ui_events as f64 / seconds;

            let _ = evt_tx.send(EngineEvent::PerfStats {
                index_lps,
                indexed_mb_s,
                rows_req_s,
                ui_events_s,
            });

            last_snapshot = snapshot;
            last_at = now;
        }
    });
}

#[cfg(test)]
mod tests {
    use super::*;
    use std::fs;
    use std::path::PathBuf;
    use std::sync::mpsc::RecvTimeoutError;

    fn temp_file_path(name: &str) -> PathBuf {
        let mut path = std::env::temp_dir();
        let pid = std::process::id();
        path.push(format!("lgx_engine_{pid}_{name}.log"));
        path
    }

    fn recv_until<F>(evt_rx: &mpsc::Receiver<EngineEvent>, mut predicate: F) -> Option<EngineEvent>
    where
        F: FnMut(&EngineEvent) -> bool,
    {
        let start = Instant::now();
        while start.elapsed() < Duration::from_secs(3) {
            match evt_rx.recv_timeout(Duration::from_millis(100)) {
                Ok(event) if predicate(&event) => return Some(event),
                Ok(_) => {}
                Err(RecvTimeoutError::Timeout) => {}
                Err(RecvTimeoutError::Disconnected) => return None,
            }
        }
        None
    }

    #[test]
    fn file_source_read_rows_respects_range() {
        let path = temp_file_path("range");
        let _ = fs::remove_file(&path);
        fs::write(&path, "a\nb\nc\n").expect("write temp file");
        let source = FileLogSource::open(SourceId(1), &path).expect("open source");

        let rows = source.read_rows(1, 2).expect("read rows").rows;
        let texts: Vec<String> = rows.into_iter().map(|row| row.text).collect();
        assert_eq!(texts, vec!["b".to_string(), "c".to_string()]);

        let rows = source.read_rows(5, 2).expect("read rows").rows;
        assert!(rows.is_empty());

        let _ = fs::remove_file(&path);
    }

    #[test]
    fn engine_opens_filtered_view_from_full_view() {
        let path = temp_file_path("filtered");
        let _ = fs::remove_file(&path);
        fs::write(&path, "INFO first\nWARN second\nERROR third\n").expect("write temp file");

        let (_engine, cmd_tx, evt_rx) = EngineHandle::new();
        cmd_tx
            .send(EngineCommand::OpenFile {
                path: path.display().to_string(),
            })
            .expect("open file");

        let full_view_id = match recv_until(&evt_rx, |event| {
            matches!(event, EngineEvent::FileOpened { .. })
        }) {
            Some(EngineEvent::FileOpened { view_id, .. }) => view_id,
            other => panic!("expected FileOpened, got {other:?}"),
        };

        cmd_tx
            .send(EngineCommand::OpenFilteredView {
                source_view_id: full_view_id,
                query: ViewQuery {
                    min_severity: Some(4),
                    max_severity: None,
                    required_flags: 0,
                },
            })
            .expect("open filtered view");

        let filtered_view_id = match recv_until(&evt_rx, |event| {
            matches!(
                event,
                EngineEvent::ViewOpened {
                    kind: ViewKind::Filtered,
                    ..
                }
            )
        }) {
            Some(EngineEvent::ViewOpened { view_id, .. }) => view_id,
            other => panic!("expected ViewOpened filtered, got {other:?}"),
        };

        cmd_tx
            .send(EngineCommand::RequestRows {
                view_id: filtered_view_id,
                start: 0,
                count: 10,
            })
            .expect("request filtered rows");

        let rows = match recv_until(
            &evt_rx,
            |event| matches!(event, EngineEvent::RowsReady { view_id, .. } if *view_id == filtered_view_id),
        ) {
            Some(EngineEvent::RowsReady { rows, .. }) => rows,
            other => panic!("expected RowsReady, got {other:?}"),
        };

        let texts = rows.into_iter().map(|row| row.text).collect::<Vec<_>>();
        assert_eq!(
            texts,
            vec!["WARN second".to_string(), "ERROR third".to_string()]
        );

        let _ = fs::remove_file(&path);
    }
}
