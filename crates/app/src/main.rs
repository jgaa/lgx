use std::cell::RefCell;
use std::collections::{HashMap, HashSet};
use std::fs;
use std::path::PathBuf;
use std::process::Command;
use std::rc::Rc;
use std::sync::mpsc::{Receiver, Sender};

use chrono::{Local, TimeZone, Utc};
use lgx_engine::EngineHandle;
use lgx_protocol::{EngineCommand, EngineEvent, SourceId, ViewId, ViewKind, ViewQuery};
use serde::{Deserialize, Serialize};
use slint::{ComponentHandle, ModelRc, SharedString, Timer, TimerMode, VecModel};
use ui::MainWindow;

mod windowed_model;
use windowed_model::WindowedRowsModel;

const WINDOW_SIZE: u32 = 800;
const PREFETCH: u32 = 200;
const ROW_HEIGHT_PX: f32 = 28.0;
const JUMP_CONTEXT_ROWS: u32 = 3;
const FLAG_TS_VALID: u8 = 1;
const RECENT_FILES_LIMIT: usize = 50;
const MARKED_PANE_MIN_WIDTH: f32 = 180.0;
const MARKED_PANE_MIN_HEIGHT: f32 = 120.0;
const FILTER_PANE_MIN_WIDTH: f32 = 220.0;
const FILTER_PANE_MIN_HEIGHT: f32 = 120.0;

#[derive(Debug, Clone, Serialize, Deserialize, Default)]
struct AppConfig {
    #[serde(default)]
    recent_files: Vec<RecentFileRecord>,
}

#[derive(Debug, Clone, Serialize, Deserialize)]
struct RecentFileRecord {
    path: String,
    opened_at: i64,
}

#[derive(Debug, Clone)]
struct RecentFileState {
    path: String,
    opened_at: i64,
    checked: bool,
    exists: bool,
}

struct ViewState {
    source_id: SourceId,
    title: String,
    model: Rc<WindowedRowsModel>,
    total_rows: u32,
    follow_enabled: bool,
    auto_scroll: bool,
    busy: bool,
    new_lines_text: String,
    panes: PaneState,
}

struct PaneState {
    marked_view_id: Option<ViewId>,
    filter_view_id: Option<ViewId>,
    marked_model: Rc<WindowedRowsModel>,
    filter_model: Rc<WindowedRowsModel>,
    marked_keys: HashSet<i32>,
    marked_total_rows: u32,
    filter_total_rows: u32,
    marked_pane_open: bool,
    filter_pane_open: bool,
    pending_filter_request: bool,
    filter_query: ViewQuery,
}

impl PaneState {
    fn new(cmd_tx: &Sender<EngineCommand>) -> Self {
        Self {
            marked_view_id: None,
            filter_view_id: None,
            marked_model: Rc::new(WindowedRowsModel::new(
                cmd_tx.clone(),
                WINDOW_SIZE,
                PREFETCH,
            )),
            filter_model: Rc::new(WindowedRowsModel::new(
                cmd_tx.clone(),
                WINDOW_SIZE,
                PREFETCH,
            )),
            marked_keys: HashSet::new(),
            marked_total_rows: 0,
            filter_total_rows: 0,
            marked_pane_open: false,
            filter_pane_open: false,
            pending_filter_request: false,
            filter_query: ViewQuery::default(),
        }
    }
}

struct AppState {
    tabs: Vec<ViewId>,
    views: HashMap<ViewId, ViewState>,
    active_view: Option<ViewId>,
    tabs_model: Rc<VecModel<ui::TabItem>>,
    empty_rows_model: Rc<VecModel<ui::RowItem>>,
    docker_model: Rc<VecModel<ui::DockerContainerItem>>,
    recent_files_model: Rc<VecModel<ui::RecentFileItem>>,
    recent_files: Vec<RecentFileState>,
    recent_dialog_open: bool,
    show_utc_time: bool,
    vertical_split: bool,
    cmd_tx: Sender<EngineCommand>,
}

impl AppState {
    fn new(cmd_tx: Sender<EngineCommand>) -> Self {
        Self {
            tabs: Vec::new(),
            views: HashMap::new(),
            active_view: None,
            tabs_model: Rc::new(VecModel::default()),
            empty_rows_model: Rc::new(VecModel::default()),
            docker_model: Rc::new(VecModel::default()),
            recent_files_model: Rc::new(VecModel::default()),
            recent_files: Vec::new(),
            recent_dialog_open: false,
            show_utc_time: false,
            vertical_split: false,
            cmd_tx,
        }
    }

    fn add_view(
        &mut self,
        view_id: ViewId,
        source_id: SourceId,
        title: String,
        model: Rc<WindowedRowsModel>,
    ) {
        self.tabs.push(view_id);
        self.views.insert(
            view_id,
            ViewState {
                source_id,
                title,
                model,
                total_rows: 0,
                follow_enabled: true,
                auto_scroll: true,
                busy: false,
                new_lines_text: String::new(),
                panes: PaneState::new(&self.cmd_tx),
            },
        );
        self.active_view = Some(view_id);
        self.sync_tabs_model();
    }

    fn find_view_by_source_id(&self, source_id: SourceId) -> Option<ViewId> {
        self.views
            .iter()
            .find_map(|(view_id, view)| (view.source_id == source_id).then_some(*view_id))
    }

    fn sync_tabs_model(&self) {
        let rows: Vec<ui::TabItem> = self
            .tabs
            .iter()
            .filter_map(|view_id| {
                self.views.get(view_id).map(|view| ui::TabItem {
                    title: view.title.clone().into(),
                    active: Some(*view_id) == self.active_view,
                })
            })
            .collect();
        self.tabs_model.set_vec(rows);
    }

    fn sync_recent_files_model(&self) {
        let rows: Vec<ui::RecentFileItem> = self
            .recent_files
            .iter()
            .map(|item| ui::RecentFileItem {
                path: item.path.clone().into(),
                opened_at: format_recent_opened_at(item.opened_at).into(),
                checked: item.checked,
                exists: item.exists,
            })
            .collect();
        self.recent_files_model.set_vec(rows);
    }
}

fn main() {
    let window = MainWindow::new().expect("create main window");
    let (engine, cmd_tx, evt_rx) = EngineHandle::new();
    let _engine = engine;

    window.set_page_size(WINDOW_SIZE as i32);

    let app_state = Rc::new(RefCell::new(AppState::new(cmd_tx.clone())));
    {
        let state = app_state.borrow();
        window.set_tabs(ModelRc::new(state.tabs_model.clone()));
        window.set_items(ModelRc::new(state.empty_rows_model.clone()));
        window.set_marked_items(ModelRc::new(state.empty_rows_model.clone()));
        window.set_filter_items(ModelRc::new(state.empty_rows_model.clone()));
        window.set_docker_containers(ModelRc::new(state.docker_model.clone()));
        window.set_recent_files(ModelRc::new(state.recent_files_model.clone()));
    }

    let cmd_tx_open = cmd_tx.clone();
    let state_for_open = app_state.clone();
    let ui_weak = window.as_weak();
    window.on_open_file(move |path| {
        open_file_and_record(&cmd_tx_open, &state_for_open, path.to_string());
        if let Some(ui) = ui_weak.upgrade() {
            ui.set_open_file_dialog_open(false);
        }
    });

    let cmd_tx_follow = cmd_tx.clone();
    let state_for_follow = app_state.clone();
    window.on_set_follow(move |enabled| {
        let active_view = state_for_follow.borrow().active_view;
        if let Some(view_id) = active_view {
            if let Some(view) = state_for_follow.borrow_mut().views.get_mut(&view_id) {
                view.follow_enabled = enabled;
            }
            let _ = cmd_tx_follow.send(EngineCommand::SetFollow { view_id, enabled });
        }
    });

    let state_for_auto_scroll = app_state.clone();
    let ui_weak = window.as_weak();
    window.on_set_auto_scroll(move |enabled| {
        let active_view = state_for_auto_scroll.borrow().active_view;
        if let Some(view_id) = active_view {
            if let Some(view) = state_for_auto_scroll.borrow_mut().views.get_mut(&view_id) {
                view.auto_scroll = enabled;
                if enabled && view.total_rows > 0 {
                    let start = view.total_rows.saturating_sub(WINDOW_SIZE);
                    view.model.request_range(start, WINDOW_SIZE);
                }
            }
            if let Some(ui) = ui_weak.upgrade() {
                ui.set_auto_scroll(enabled);
            }
        }
    });

    let state_for_time_mode = app_state.clone();
    let ui_weak = window.as_weak();
    window.on_set_show_utc_time(move |enabled| {
        {
            let mut state = state_for_time_mode.borrow_mut();
            state.show_utc_time = enabled;
        }
        if let Some(ui) = ui_weak.upgrade() {
            ui.set_show_utc_time(enabled);
            refresh_visible_rows(&ui, &state_for_time_mode);
        }
    });

    let state_for_split_mode = app_state.clone();
    let ui_weak = window.as_weak();
    window.on_set_vertical_split(move |enabled| {
        {
            let mut state = state_for_split_mode.borrow_mut();
            state.vertical_split = enabled;
        }
        if let Some(ui) = ui_weak.upgrade() {
            ui.set_vertical_split(enabled);
        }
    });

    let state_for_jump = app_state.clone();
    window.on_jump_to(move |text| {
        if let Ok(value) = text.trim().parse::<u32>() {
            let state = state_for_jump.borrow();
            if let Some(view_id) = state.active_view {
                if let Some(view) = state.views.get(&view_id) {
                    view.model
                        .request_range(value.saturating_sub(1), WINDOW_SIZE);
                }
            }
        }
    });

    let state_for_tabs = app_state.clone();
    window.on_activate_tab({
        let ui_weak = window.as_weak();
        move |index| {
            if index < 0 {
                return;
            }
            if let Some(ui) = ui_weak.upgrade() {
                let view_id = {
                    let state = state_for_tabs.borrow();
                    state.tabs.get(index as usize).copied()
                };
                if let Some(view_id) = view_id {
                    activate_view(&ui, &state_for_tabs, view_id);
                }
            }
        }
    });

    let cmd_tx_filter = cmd_tx.clone();
    let state_for_filter = app_state.clone();
    let ui_weak = window.as_weak();
    window.on_apply_filters(move || {
        let Some(ui) = ui_weak.upgrade() else {
            return;
        };
        let active_view = state_for_filter.borrow().active_view;
        if let Some(source_view_id) = active_view {
            let query = build_filter_query(&ui);
            if query.is_empty() {
                clear_filters(&ui, &state_for_filter, &cmd_tx_filter);
                return;
            }
            close_filter_view_for_active(&state_for_filter, &cmd_tx_filter);
            {
                let mut state = state_for_filter.borrow_mut();
                if let Some(view) = state.views.get_mut(&source_view_id) {
                    view.panes.pending_filter_request = true;
                    view.panes.filter_query = query.clone();
                    view.panes.filter_pane_open = true;
                }
            }
            reset_filter_pane_size(&ui);
            ui.set_filter_pane_visible(true);
            ui.set_filter_total_matches(0);
            ui.set_selected_filter_row(-1);
            let _ = cmd_tx_filter.send(EngineCommand::OpenFilteredView {
                source_view_id,
                query,
            });
        }
    });

    let state_for_clear = app_state.clone();
    let cmd_tx_clear = cmd_tx.clone();
    let ui_weak = window.as_weak();
    window.on_clear_filters(move || {
        let Some(ui) = ui_weak.upgrade() else {
            return;
        };
        clear_filters(&ui, &state_for_clear, &cmd_tx_clear);
    });

    let cmd_tx_jump_full = cmd_tx.clone();
    let state_for_jump_full = app_state.clone();
    let ui_weak = window.as_weak();
    window.on_jump_to_full_view(move || {
        let Some(ui) = ui_weak.upgrade() else {
            return;
        };
        let selected_row = ui.get_selected_row();
        if selected_row < 0 {
            return;
        }
        let state = state_for_jump_full.borrow();
        let Some(view_id) = state.active_view else {
            return;
        };
        let Some(view) = state.views.get(&view_id) else {
            return;
        };
        let Some(row_id) = view.model.row_id_at(selected_row as u32) else {
            return;
        };
        let _ = cmd_tx_jump_full.send(EngineCommand::JumpToFullView {
            source_view_id: view_id,
            row_id: lgx_protocol::RowId(row_id),
        });
    });

    let ui_weak = window.as_weak();
    let state_for_mark = app_state.clone();
    let cmd_tx_mark = cmd_tx.clone();
    window.on_toggle_mark_row(move |row| {
        if row < 0 {
            return;
        }
        if let Some(ui) = ui_weak.upgrade() {
            toggle_mark_row(&ui, &state_for_mark, &cmd_tx_mark, row as u32);
        }
    });

    let cmd_tx_marked = cmd_tx.clone();
    let state_for_marked = app_state.clone();
    let ui_weak = window.as_weak();
    window.on_activate_marked_row(move |row| {
        if row < 0 {
            return;
        }
        if let Some(ui) = ui_weak.upgrade() {
            activate_marked_row(&ui, &state_for_marked, &cmd_tx_marked, row as usize);
        }
    });

    let cmd_tx_filter_jump = cmd_tx.clone();
    let state_for_filter_jump = app_state.clone();
    let ui_weak = window.as_weak();
    window.on_activate_filter_row(move |row| {
        if row < 0 {
            return;
        }
        if let Some(ui) = ui_weak.upgrade() {
            activate_filter_row(
                &ui,
                &state_for_filter_jump,
                &cmd_tx_filter_jump,
                row as usize,
            );
        }
    });

    let ui_weak = window.as_weak();
    window.on_open_goto_dialog(move || {
        if let Some(ui) = ui_weak.upgrade() {
            ui.set_goto_dialog_open(true);
        }
    });

    let cmd_tx_open_dialog = cmd_tx.clone();
    let state_for_open_dialog = app_state.clone();
    let ui_weak = window.as_weak();
    window.on_open_open_file_dialog(move || {
        let cmd_tx_open_dialog = cmd_tx_open_dialog.clone();
        let state_for_open_dialog = state_for_open_dialog.clone();
        let ui_weak = ui_weak.clone();
        // Defer the native picker until after the menu callback unwinds.
        Timer::single_shot(std::time::Duration::default(), move || {
            if let Some(ui) = ui_weak.upgrade() {
                match pick_file_path() {
                    FilePickResult::Selected(path) => {
                        open_file_and_record(
                            &cmd_tx_open_dialog,
                            &state_for_open_dialog,
                            path.to_string_lossy().into_owned(),
                        );
                    }
                    FilePickResult::Cancelled => {}
                    FilePickResult::Unavailable => {
                        ui.set_open_file_dialog_open(true);
                    }
                }
            }
        });
    });

    let ui_weak = window.as_weak();
    window.on_close_open_file_dialog(move || {
        if let Some(ui) = ui_weak.upgrade() {
            ui.set_open_file_dialog_open(false);
        }
    });

    let state_for_recent_open = app_state.clone();
    let ui_weak = window.as_weak();
    window.on_open_recent_dialog(move || {
        if let Some(ui) = ui_weak.upgrade() {
            refresh_recent_files(&state_for_recent_open);
            {
                let mut state = state_for_recent_open.borrow_mut();
                state.recent_dialog_open = true;
                state.sync_recent_files_model();
            }
            ui.set_recent_dialog_open(true);
        }
    });

    let state_for_recent_close = app_state.clone();
    let ui_weak = window.as_weak();
    window.on_close_recent_dialog(move || {
        if let Some(ui) = ui_weak.upgrade() {
            {
                let mut state = state_for_recent_close.borrow_mut();
                state.recent_dialog_open = false;
                for item in &mut state.recent_files {
                    item.checked = false;
                }
                state.sync_recent_files_model();
            }
            ui.set_recent_dialog_open(false);
        }
    });

    let state_for_recent_toggle = app_state.clone();
    window.on_toggle_recent_file(move |row| {
        if row < 0 {
            return;
        }
        let mut state = state_for_recent_toggle.borrow_mut();
        if let Some(item) = state.recent_files.get_mut(row as usize) {
            item.checked = !item.checked;
            state.sync_recent_files_model();
        }
    });

    let state_for_recent_remove = app_state.clone();
    window.on_remove_recent_file(move |row| {
        if row < 0 {
            return;
        }
        let recent_files = remove_recent_file(row as usize);
        replace_recent_files_state(&state_for_recent_remove, recent_files);
    });

    let cmd_tx_recent_open = cmd_tx.clone();
    let state_for_recent_files_open = app_state.clone();
    let ui_weak = window.as_weak();
    window.on_open_selected_recent_files(move || {
        let paths = {
            let state = state_for_recent_files_open.borrow();
            state
                .recent_files
                .iter()
                .filter(|item| item.checked)
                .map(|item| item.path.clone())
                .collect::<Vec<_>>()
        };
        for path in paths {
            open_file_and_record(&cmd_tx_recent_open, &state_for_recent_files_open, path);
        }
        if let Some(ui) = ui_weak.upgrade() {
            {
                let mut state = state_for_recent_files_open.borrow_mut();
                state.recent_dialog_open = false;
                for item in &mut state.recent_files {
                    item.checked = false;
                }
                state.sync_recent_files_model();
            }
            ui.set_recent_dialog_open(false);
        }
    });

    let ui_weak = window.as_weak();
    window.on_close_goto_dialog(move || {
        if let Some(ui) = ui_weak.upgrade() {
            ui.set_goto_dialog_open(false);
        }
    });

    let cmd_tx_docker = cmd_tx.clone();
    window.on_refresh_docker(move || {
        let _ = cmd_tx_docker.send(EngineCommand::ListDockerContainers);
    });

    let ui_weak = window.as_weak();
    let state_for_marked_pane = app_state.clone();
    window.on_set_marked_pane_visible(move |requested_visible| {
        if let Some(ui) = ui_weak.upgrade() {
            let visible = {
                let mut state = state_for_marked_pane.borrow_mut();
                let Some(active_view) = state.active_view else {
                    return;
                };
                let Some(view) = state.views.get_mut(&active_view) else {
                    return;
                };
                view.panes.marked_pane_open = requested_visible;
                view.panes.marked_view_id.is_some() && view.panes.marked_pane_open
            };
            if visible {
                reset_marked_pane_size(&ui);
            }
            ui.set_marked_pane_visible(visible);
        }
    });

    let ui_weak = window.as_weak();
    let state_for_filter_pane = app_state.clone();
    let cmd_tx_filter_pane = cmd_tx.clone();
    window.on_set_filter_pane_visible(move |requested_visible| {
        if let Some(ui) = ui_weak.upgrade() {
            let active_view = state_for_filter_pane.borrow().active_view;
            {
                let mut state = state_for_filter_pane.borrow_mut();
                if let Some(active_view) = active_view {
                    if let Some(view) = state.views.get_mut(&active_view) {
                        view.panes.filter_pane_open = requested_visible;
                    }
                }
            }
            if requested_visible {
                reset_filter_pane_size(&ui);
            }
            if !requested_visible {
                close_filter_view_for_active(&state_for_filter_pane, &cmd_tx_filter_pane);
                reset_filter_controls(&ui);
                ui.set_filter_total_matches(0);
                ui.set_selected_filter_row(-1);
            }
            ui.set_filter_pane_visible(requested_visible);
        }
    });

    let cmd_tx_open_docker = cmd_tx.clone();
    window.on_open_docker_container(move |id, title| {
        let _ = cmd_tx_open_docker.send(EngineCommand::OpenDockerContainer {
            container_id: id.to_string(),
            title: title.to_string(),
        });
    });

    let event_timer = Timer::default();
    let ui_weak = window.as_weak();
    let state_for_events = app_state.clone();
    let cmd_tx_events = cmd_tx.clone();
    event_timer.start(
        TimerMode::Repeated,
        std::time::Duration::from_millis(16),
        move || {
            if let Some(ui) = ui_weak.upgrade() {
                drain_events(&ui, &state_for_events, &cmd_tx_events, &evt_rx);
            }
        },
    );

    for path in collect_startup_files() {
        open_file_and_record(&cmd_tx, &app_state, path.to_string_lossy().into_owned());
    }

    window.run().expect("run main window");
}

fn send_open_file(cmd_tx: &Sender<EngineCommand>, path: String) {
    let _ = cmd_tx.send(EngineCommand::OpenFile { path });
}

fn open_file_and_record(
    cmd_tx: &Sender<EngineCommand>,
    app_state: &Rc<RefCell<AppState>>,
    path: String,
) {
    let path = path.trim().to_string();
    if path.is_empty() {
        return;
    }
    send_open_file(cmd_tx, path.clone());
    let recent_files = update_recent_files(&path);
    replace_recent_files_state(app_state, recent_files);
}

fn refresh_recent_files(app_state: &Rc<RefCell<AppState>>) {
    let recent_files = load_recent_files();
    replace_recent_files_state(app_state, recent_files);
}

fn replace_recent_files_state(
    app_state: &Rc<RefCell<AppState>>,
    recent_files: Vec<RecentFileRecord>,
) {
    let mut state = app_state.borrow_mut();
    state.recent_files = recent_files
        .into_iter()
        .map(|item| RecentFileState {
            path: item.path.clone(),
            opened_at: item.opened_at,
            checked: false,
            exists: std::path::Path::new(&item.path).exists(),
        })
        .collect();
    state.sync_recent_files_model();
}

enum FilePickResult {
    Selected(PathBuf),
    Cancelled,
    Unavailable,
}

fn pick_file_path() -> FilePickResult {
    #[cfg(target_os = "linux")]
    {
        pick_file_path_linux()
    }
    #[cfg(target_os = "macos")]
    {
        pick_file_path_macos()
    }
    #[cfg(not(any(target_os = "linux", target_os = "macos")))]
    {
        FilePickResult::Unavailable
    }
}

#[cfg(target_os = "linux")]
fn pick_file_path_linux() -> FilePickResult {
    let zenity = Command::new("zenity")
        .args(["--file-selection", "--title=Open Log File"])
        .output();
    match interpret_dialog_output(zenity) {
        FilePickResult::Unavailable => {}
        result => return result,
    }

    let kdialog = Command::new("kdialog")
        .args(["--getopenfilename", ".", "*", "Open Log File"])
        .output();
    interpret_dialog_output(kdialog)
}

#[cfg(target_os = "macos")]
fn pick_file_path_macos() -> FilePickResult {
    let script = "POSIX path of (choose file with prompt \"Open Log File\")";
    let output = Command::new("osascript").args(["-e", script]).output();
    interpret_dialog_output(output)
}

fn interpret_dialog_output(output: std::io::Result<std::process::Output>) -> FilePickResult {
    match output {
        Ok(result) if result.status.success() => {
            let path = String::from_utf8_lossy(&result.stdout).trim().to_string();
            if path.is_empty() {
                FilePickResult::Cancelled
            } else {
                FilePickResult::Selected(PathBuf::from(path))
            }
        }
        Ok(result) if result.status.code() == Some(1) => FilePickResult::Cancelled,
        Ok(_) => FilePickResult::Unavailable,
        Err(err) if err.kind() == std::io::ErrorKind::NotFound => FilePickResult::Unavailable,
        Err(_) => FilePickResult::Cancelled,
    }
}

fn config_path() -> Option<PathBuf> {
    let home = std::env::var_os("HOME")?;
    let mut path = PathBuf::from(home);
    path.push(".config");
    path.push("lgx.yaml");
    Some(path)
}

fn load_recent_files() -> Vec<RecentFileRecord> {
    let Some(path) = config_path() else {
        return Vec::new();
    };
    load_recent_files_from_path(&path)
}

fn load_recent_files_from_path(path: &std::path::Path) -> Vec<RecentFileRecord> {
    let contents = match fs::read_to_string(path) {
        Ok(contents) => contents,
        Err(err) if err.kind() == std::io::ErrorKind::NotFound => return Vec::new(),
        Err(_) => return Vec::new(),
    };
    match serde_yaml::from_str::<AppConfig>(&contents) {
        Ok(config) => sanitize_recent_files(config.recent_files),
        Err(_) => Vec::new(),
    }
}

fn update_recent_files(path: &str) -> Vec<RecentFileRecord> {
    let Some(config_path) = config_path() else {
        return Vec::new();
    };
    update_recent_files_at_path(&config_path, path)
}

fn update_recent_files_at_path(config_path: &std::path::Path, path: &str) -> Vec<RecentFileRecord> {
    let mut recent_files = load_recent_files_from_path(&config_path);
    let opened_at = Utc::now().timestamp();
    recent_files.retain(|item| item.path != path);
    recent_files.insert(
        0,
        RecentFileRecord {
            path: path.to_string(),
            opened_at,
        },
    );
    recent_files.truncate(RECENT_FILES_LIMIT);
    let recent_files = sanitize_recent_files(recent_files);
    let _ = write_config(
        config_path,
        &AppConfig {
            recent_files: recent_files.clone(),
        },
    );
    recent_files
}

fn remove_recent_file(index: usize) -> Vec<RecentFileRecord> {
    let Some(config_path) = config_path() else {
        return Vec::new();
    };
    remove_recent_file_at_path(&config_path, index)
}

fn remove_recent_file_at_path(
    config_path: &std::path::Path,
    index: usize,
) -> Vec<RecentFileRecord> {
    let mut recent_files = load_recent_files_from_path(config_path);
    if index >= recent_files.len() {
        return recent_files;
    }
    recent_files.remove(index);
    let recent_files = sanitize_recent_files(recent_files);
    let _ = write_config(
        config_path,
        &AppConfig {
            recent_files: recent_files.clone(),
        },
    );
    recent_files
}

fn write_config(path: &std::path::Path, config: &AppConfig) -> Result<(), std::io::Error> {
    if let Some(parent) = path.parent() {
        fs::create_dir_all(parent)?;
    }
    let yaml = serde_yaml::to_string(config)
        .map_err(|err| std::io::Error::new(std::io::ErrorKind::Other, err.to_string()))?;
    fs::write(path, yaml)
}

fn sanitize_recent_files(mut recent_files: Vec<RecentFileRecord>) -> Vec<RecentFileRecord> {
    recent_files.retain(|item| !item.path.trim().is_empty());
    recent_files.sort_by(|left, right| right.opened_at.cmp(&left.opened_at));
    let mut deduped = Vec::with_capacity(recent_files.len().min(RECENT_FILES_LIMIT));
    let mut seen = HashSet::new();
    for item in recent_files {
        if seen.insert(item.path.clone()) {
            deduped.push(item);
            if deduped.len() >= RECENT_FILES_LIMIT {
                break;
            }
        }
    }
    deduped
}

fn format_recent_opened_at(opened_at: i64) -> String {
    match Local.timestamp_opt(opened_at, 0).single() {
        Some(dt) => dt.format("%Y-%m-%d %H:%M:%S").to_string(),
        None => String::new(),
    }
}

fn collect_startup_files() -> Vec<PathBuf> {
    parse_startup_files(std::env::args_os().skip(1))
}

fn parse_startup_files<I>(args: I) -> Vec<PathBuf>
where
    I: IntoIterator,
    I::Item: Into<std::ffi::OsString>,
{
    let args = args.into_iter().map(Into::into).collect::<Vec<_>>();
    let mut files = Vec::new();
    let mut index = 0usize;
    while index < args.len() {
        let arg = &args[index];
        if let Some(text) = arg.to_str() {
            if text.starts_with("--") && index + 1 < args.len() {
                index += 2;
                continue;
            }
        }
        files.push(PathBuf::from(arg));
        index += 1;
    }
    files
}

fn drain_events(
    ui: &MainWindow,
    app_state: &Rc<RefCell<AppState>>,
    cmd_tx: &Sender<EngineCommand>,
    evt_rx: &Receiver<EngineEvent>,
) {
    while let Ok(event) = evt_rx.try_recv() {
        apply_event(ui, app_state, cmd_tx, event);
    }
}

fn owner_view_for_marked_pane(
    app_state: &Rc<RefCell<AppState>>,
    marked_view_id: ViewId,
) -> Option<ViewId> {
    let state = app_state.borrow();
    state.views.iter().find_map(|(view_id, view)| {
        (view.panes.marked_view_id == Some(marked_view_id)).then_some(*view_id)
    })
}

fn owner_view_for_filter_pane(
    app_state: &Rc<RefCell<AppState>>,
    filter_view_id: ViewId,
) -> Option<ViewId> {
    let state = app_state.borrow();
    state.views.iter().find_map(|(view_id, view)| {
        (view.panes.filter_view_id == Some(filter_view_id)).then_some(*view_id)
    })
}

fn reset_marked_pane_size(ui: &MainWindow) {
    let window = ui.window();
    let logical_size = window.size().to_logical(window.scale_factor());
    if ui.get_vertical_split() {
        ui.set_marked_pane_height((logical_size.height * 0.3).max(MARKED_PANE_MIN_HEIGHT));
    } else {
        ui.set_marked_pane_width((logical_size.width * 0.3).max(MARKED_PANE_MIN_WIDTH));
    }
}

fn reset_filter_pane_size(ui: &MainWindow) {
    let window = ui.window();
    let logical_size = window.size().to_logical(window.scale_factor());
    if ui.get_vertical_split() {
        ui.set_filter_pane_height((logical_size.height * 0.3).max(FILTER_PANE_MIN_HEIGHT));
    } else {
        ui.set_filter_pane_width((logical_size.width * 0.3).max(FILTER_PANE_MIN_WIDTH));
    }
}

fn activate_view(ui: &MainWindow, app_state: &Rc<RefCell<AppState>>, view_id: ViewId) {
    let (
        items_model,
        marked_items_model,
        filter_items_model,
        total_rows,
        follow_enabled,
        auto_scroll,
        busy,
        new_lines_text,
        show_utc_time,
        vertical_split,
        marked_visible,
        filter_visible,
        filter_total_rows,
        filter_query,
    ) = {
        let mut state = app_state.borrow_mut();
        state.active_view = Some(view_id);
        state.sync_tabs_model();
        match state.views.get(&view_id) {
            Some(view) => (
                ModelRc::new(view.model.clone()),
                ModelRc::new(view.panes.marked_model.clone()),
                ModelRc::new(view.panes.filter_model.clone()),
                view.total_rows,
                view.follow_enabled,
                view.auto_scroll,
                view.busy,
                view.new_lines_text.clone(),
                state.show_utc_time,
                state.vertical_split,
                view.panes.marked_view_id.is_some() && view.panes.marked_pane_open,
                view.panes.filter_pane_open,
                view.panes.filter_total_rows,
                view.panes.filter_query.clone(),
            ),
            None => (
                ModelRc::new(state.empty_rows_model.clone()),
                ModelRc::new(state.empty_rows_model.clone()),
                ModelRc::new(state.empty_rows_model.clone()),
                0,
                true,
                true,
                false,
                String::new(),
                state.show_utc_time,
                state.vertical_split,
                false,
                false,
                0,
                ViewQuery::default(),
            ),
        }
    };

    ui.set_items(items_model);
    ui.set_marked_items(marked_items_model);
    ui.set_filter_items(filter_items_model);
    ui.set_total_rows(total_rows as i32);
    ui.set_follow_enabled(follow_enabled);
    ui.set_auto_scroll(auto_scroll);
    ui.set_busy(busy);
    ui.set_new_lines_text(new_lines_text.into());
    ui.set_show_utc_time(show_utc_time);
    ui.set_vertical_split(vertical_split);
    ui.set_selected_row(-1);
    ui.set_selected_marked_row(-1);
    ui.set_selected_filter_row(-1);
    ui.set_marked_pane_visible(marked_visible);
    ui.set_filter_pane_visible(filter_visible);
    ui.set_filter_total_matches(filter_total_rows as i32);
    sync_filter_controls(ui, &filter_query);
}

fn refresh_visible_rows(ui: &MainWindow, app_state: &Rc<RefCell<AppState>>) {
    let (active_model, marked_model, filter_model) = {
        let state = app_state.borrow();
        let active_model = state
            .active_view
            .and_then(|view_id| state.views.get(&view_id).map(|view| view.model.clone()));
        let marked_model = state.active_view.and_then(|view_id| {
            state.views.get(&view_id).and_then(|view| {
                (view.panes.marked_view_id.is_some() && view.panes.marked_pane_open)
                    .then_some(view.panes.marked_model.clone())
            })
        });
        let filter_model = state.active_view.and_then(|view_id| {
            state.views.get(&view_id).and_then(|view| {
                view.panes
                    .filter_pane_open
                    .then_some(view.panes.filter_model.clone())
            })
        });
        (active_model, marked_model, filter_model)
    };

    if let Some(model) = active_model {
        let start = model.current_window_start();
        let count = model.current_window_len().max(1);
        model.request_range(start, count);
    }
    if let Some(model) = marked_model {
        let start = model.current_window_start();
        let count = model.current_window_len().max(1);
        model.request_range(start, count);
    }
    if let Some(model) = filter_model {
        let start = model.current_window_start();
        let count = model.current_window_len().max(1);
        model.request_range(start, count);
    }
    ui.set_status_text(SharedString::default());
}

fn make_row_item(
    row: lgx_protocol::RowRender,
    origin_view_id: ViewId,
    marked: bool,
    show_utc_time: bool,
) -> ui::RowItem {
    let text = format_row_text(&row.text, row.ts, row.flags, show_utc_time);
    ui::RowItem {
        text: text.into(),
        raw_text: row.text.into(),
        sev: row.sev as i32,
        row_id: row.row_id.0.min(i32::MAX as u64) as i32,
        origin_view_id: origin_view_id.0.min(i32::MAX as u64) as i32,
        marked,
    }
}

fn format_row_text(raw_text: &str, ts: i64, flags: u8, show_utc_time: bool) -> String {
    if flags & FLAG_TS_VALID == 0 {
        return raw_text.to_string();
    }
    let bytes = raw_text.as_bytes();
    if !looks_like_logfault_text(bytes) {
        return raw_text.to_string();
    }
    let prefix_end = find_logfault_prefix_end(bytes);
    if prefix_end <= 24 || prefix_end > raw_text.len() {
        return raw_text.to_string();
    }
    let formatted_ts = format_timestamp(ts, show_utc_time);
    let suffix = &raw_text[prefix_end..];
    format!("{formatted_ts}{suffix}")
}

fn looks_like_logfault_text(line: &[u8]) -> bool {
    if line.len() < 35 {
        return false;
    }
    matches!(
        (
            line.get(4),
            line.get(7),
            line.get(10),
            line.get(13),
            line.get(16),
            line.get(19),
            line.get(23)
        ),
        (
            Some(b'-'),
            Some(b'-'),
            Some(b' '),
            Some(b':'),
            Some(b':'),
            Some(b'.'),
            Some(b' ')
        )
    )
}

fn find_logfault_prefix_end(line: &[u8]) -> usize {
    let mut spaces = 0usize;
    for (idx, byte) in line.iter().enumerate() {
        if *byte == b' ' {
            spaces += 1;
            if spaces == 4 {
                return idx;
            }
        }
    }
    line.len()
}

fn format_timestamp(ts: i64, show_utc_time: bool) -> String {
    if show_utc_time {
        if let Some(dt) = Utc.timestamp_millis_opt(ts).single() {
            return dt.format("%Y-%m-%d %H:%M:%S%.3f UTC").to_string();
        }
    } else if let Some(dt) = Local.timestamp_millis_opt(ts).single() {
        return dt.format("%Y-%m-%d %H:%M:%S%.3f %Z").to_string();
    }
    if let Some(dt) = Utc.timestamp_millis_opt(ts).single() {
        return dt.format("%Y-%m-%d %H:%M:%S%.3f UTC").to_string();
    }
    String::new()
}

fn apply_event(
    ui: &MainWindow,
    app_state: &Rc<RefCell<AppState>>,
    cmd_tx: &Sender<EngineCommand>,
    event: EngineEvent,
) {
    match event {
        EngineEvent::FileOpened {
            source_id,
            view_id,
            path,
            ..
        } => {
            handle_view_opened(
                ui,
                app_state,
                cmd_tx,
                view_id,
                source_id,
                path,
                ViewKind::Full,
                ViewQuery::default(),
            );
        }
        EngineEvent::ViewOpened {
            source_id,
            view_id,
            kind,
            title,
            total_rows,
            query,
            ..
        } => {
            if kind == ViewKind::Marked {
                let owner_view_id = app_state.borrow().find_view_by_source_id(source_id);
                let is_active = {
                    let mut state = app_state.borrow_mut();
                    let Some(owner_view_id) = owner_view_id else {
                        return;
                    };
                    let is_active = state.active_view == Some(owner_view_id);
                    if let Some(view) = state.views.get_mut(&owner_view_id) {
                        view.panes.marked_view_id = Some(view_id);
                        view.panes.marked_pane_open = true;
                        view.panes.marked_total_rows = total_rows;
                        view.panes.marked_model.set_view(view_id);
                        view.panes.marked_model.set_total_rows(total_rows);
                    }
                    is_active
                };
                if is_active {
                    reset_marked_pane_size(ui);
                    ui.set_marked_pane_visible(true);
                }
                if let Some(owner_view_id) = owner_view_id {
                    if let Some(view) = app_state.borrow().views.get(&owner_view_id) {
                        if total_rows > 0 {
                            view.panes.marked_model.request_range(0, total_rows.max(1));
                        }
                    }
                }
            } else if kind == ViewKind::Filtered {
                let owner_view_id = app_state.borrow().find_view_by_source_id(source_id);
                let pending_filter = {
                    let mut state = app_state.borrow_mut();
                    let Some(owner_view_id) = owner_view_id else {
                        return;
                    };
                    let Some(view) = state.views.get_mut(&owner_view_id) else {
                        return;
                    };
                    if !view.panes.pending_filter_request {
                        false
                    } else {
                        view.panes.pending_filter_request = false;
                        view.panes.filter_view_id = Some(view_id);
                        view.panes.filter_total_rows = total_rows;
                        view.panes.filter_model.set_view(view_id);
                        view.panes.filter_model.set_total_rows(total_rows);
                        view.panes.filter_model.clear();
                        true
                    }
                };
                if pending_filter {
                    if app_state.borrow().active_view == owner_view_id {
                        reset_filter_pane_size(ui);
                        ui.set_filter_pane_visible(true);
                        ui.set_filter_total_matches(total_rows as i32);
                        ui.set_selected_filter_row(-1);
                    }
                    if let Some(owner_view_id) = owner_view_id {
                        if let Some(view) = app_state.borrow().views.get(&owner_view_id) {
                            if total_rows > 0 {
                                view.panes
                                    .filter_model
                                    .request_range(0, total_rows.max(1).min(WINDOW_SIZE));
                            }
                        }
                    }
                } else {
                    handle_view_opened(
                        ui, app_state, cmd_tx, view_id, source_id, title, kind, query,
                    );
                    if app_state.borrow().active_view == Some(view_id) {
                        ui.set_total_rows(total_rows as i32);
                    }
                }
            } else {
                handle_view_opened(
                    ui, app_state, cmd_tx, view_id, source_id, title, kind, query,
                );
                if app_state.borrow().active_view == Some(view_id) {
                    ui.set_total_rows(total_rows as i32);
                }
            }
        }
        EngineEvent::ViewStats {
            view_id,
            total_rows,
        } => {
            if let Some(owner_view_id) = owner_view_for_marked_pane(app_state, view_id) {
                let should_show = {
                    let mut state = app_state.borrow_mut();
                    let Some(view) = state.views.get_mut(&owner_view_id) else {
                        return;
                    };
                    let should_show =
                        total_rows > view.panes.marked_total_rows && !view.panes.marked_pane_open;
                    view.panes.marked_total_rows = total_rows;
                    view.panes.marked_model.set_total_rows(total_rows);
                    view.panes.marked_model.request_range(0, total_rows.max(1));
                    if should_show {
                        view.panes.marked_pane_open = true;
                    }
                    should_show
                };
                if should_show && app_state.borrow().active_view == Some(owner_view_id) {
                    reset_marked_pane_size(ui);
                    ui.set_marked_pane_visible(true);
                }
                return;
            }
            if let Some(owner_view_id) = owner_view_for_filter_pane(app_state, view_id) {
                {
                    let mut state = app_state.borrow_mut();
                    let Some(view) = state.views.get_mut(&owner_view_id) else {
                        return;
                    };
                    view.panes.filter_total_rows = total_rows;
                    view.panes.filter_model.set_total_rows(total_rows);
                    if total_rows > 0 && view.panes.filter_model.current_window_len() == 0 {
                        view.panes
                            .filter_model
                            .request_range(0, total_rows.max(1).min(WINDOW_SIZE));
                    }
                }
                if app_state.borrow().active_view == Some(owner_view_id) {
                    ui.set_filter_total_matches(total_rows as i32);
                }
                return;
            }
            let is_active = {
                let mut state = app_state.borrow_mut();
                if let Some(view) = state.views.get_mut(&view_id) {
                    view.total_rows = total_rows;
                    view.model.set_total_rows(total_rows);
                    if total_rows > 0 && view.model.current_window_len() == 0 {
                        view.model.request_range(0, WINDOW_SIZE);
                    }
                }
                state.active_view == Some(view_id)
            };
            if is_active {
                ui.set_total_rows(total_rows as i32);
            }
        }
        EngineEvent::JumpTarget { view_id, row } => {
            activate_view(ui, app_state, view_id);
            let model = {
                let state = app_state.borrow();
                state.views.get(&view_id).map(|view| view.model.clone())
            };
            if let Some(model) = model {
                let start = row.saturating_sub(WINDOW_SIZE / 4);
                model.request_range(start, WINDOW_SIZE);
                let top_row = row.saturating_sub(JUMP_CONTEXT_ROWS);
                ui.set_log_viewport_y(-((top_row as f32) * ROW_HEIGHT_PX));
                ui.set_selected_row(row.min(i32::MAX as u32) as i32);
            }
            ui.set_status_text(format!("Jumped to row {}", row + 1).into());
        }
        EngineEvent::DockerContainersListed { containers } => {
            let state = app_state.borrow();
            let rows = containers
                .into_iter()
                .map(|container| ui::DockerContainerItem {
                    id: container.id.into(),
                    name: container.name.into(),
                    image: container.image.into(),
                    status: container.status.into(),
                })
                .collect::<Vec<_>>();
            state.docker_model.set_vec(rows);
            ui.set_status_text("".into());
        }
        EngineEvent::ViewBusy { view_id, busy } => {
            let is_active = {
                let mut state = app_state.borrow_mut();
                if let Some(view) = state.views.get_mut(&view_id) {
                    view.busy = busy;
                }
                state.active_view == Some(view_id)
            };
            if is_active {
                ui.set_busy(busy);
            }
        }
        EngineEvent::RowsReady {
            view_id,
            start,
            rows,
        } => {
            if let Some(owner_view_id) = owner_view_for_marked_pane(app_state, view_id) {
                let show_utc_time = app_state.borrow().show_utc_time;
                let items = rows
                    .into_iter()
                    .map(|row| {
                        let origin_view_id = row
                            .origin
                            .as_ref()
                            .map(|origin| origin.view_id)
                            .unwrap_or(view_id);
                        make_row_item(row, origin_view_id, true, show_utc_time)
                    })
                    .collect::<Vec<_>>();
                {
                    let mut state = app_state.borrow_mut();
                    let Some(view) = state.views.get_mut(&owner_view_id) else {
                        return;
                    };
                    view.panes.marked_model.apply_rows(start, items.clone());
                    view.panes.marked_keys = items
                        .into_iter()
                        .map(|item| item.row_id)
                        .collect();
                }
                if let Some(active) = app_state.borrow().views.get(&owner_view_id) {
                    if app_state.borrow().active_view == Some(owner_view_id) {
                        active.model.request_range(
                            active.model.current_window_start(),
                            active.model.current_window_len().max(1),
                        );
                    }
                }
                return;
            }
            if let Some(owner_view_id) = owner_view_for_filter_pane(app_state, view_id) {
                let show_utc_time = app_state.borrow().show_utc_time;
                let items = rows
                    .into_iter()
                    .map(|row| {
                        let origin_view_id = row
                            .origin
                            .as_ref()
                            .map(|origin| origin.view_id)
                            .unwrap_or(view_id);
                        make_row_item(row, origin_view_id, false, show_utc_time)
                    })
                    .collect::<Vec<_>>();
                {
                    let state = app_state.borrow();
                    if let Some(view) = state.views.get(&owner_view_id) {
                        view.panes.filter_model.apply_rows(start, items);
                    }
                }
                return;
            }
            let is_active = {
                let mut state = app_state.borrow_mut();
                let show_utc_time = state.show_utc_time;
                if let Some(view) = state.views.get_mut(&view_id) {
                    let items = rows
                        .into_iter()
                        .map(|row| {
                            let marked = view
                                .panes
                                .marked_keys
                                .contains(&(row.row_id.0.min(i32::MAX as u64) as i32));
                            make_row_item(row, view_id, marked, show_utc_time)
                        })
                        .collect();
                    view.model.apply_rows(start, items);
                    if view.follow_enabled {
                        view.new_lines_text.clear();
                    }
                }
                state.active_view == Some(view_id)
            };
            if is_active {
                ui.set_status_text(SharedString::default());
                if ui.get_follow_enabled() {
                    ui.set_new_lines_text(SharedString::default());
                }
            }
        }
        EngineEvent::RowsAppended {
            view_id,
            appended,
            total_rows,
        } => {
            if let Some(owner_view_id) = owner_view_for_marked_pane(app_state, view_id) {
                let should_show = {
                    let mut state = app_state.borrow_mut();
                    let Some(view) = state.views.get_mut(&owner_view_id) else {
                        return;
                    };
                    let should_show =
                        total_rows > view.panes.marked_total_rows && !view.panes.marked_pane_open;
                    view.panes.marked_total_rows = total_rows;
                    view.panes.marked_model.update_total_rows(total_rows, appended);
                    view.panes.marked_model.request_range(0, total_rows.max(1));
                    if should_show {
                        view.panes.marked_pane_open = true;
                    }
                    should_show
                };
                if should_show && app_state.borrow().active_view == Some(owner_view_id) {
                    reset_marked_pane_size(ui);
                    ui.set_marked_pane_visible(true);
                }
                return;
            }
            if let Some(owner_view_id) = owner_view_for_filter_pane(app_state, view_id) {
                {
                    let mut state = app_state.borrow_mut();
                    let Some(view) = state.views.get_mut(&owner_view_id) else {
                        return;
                    };
                    view.panes.filter_total_rows = total_rows;
                    view.panes.filter_model.update_total_rows(total_rows, appended);
                    if appended > 0 {
                        let start = view.panes.filter_model.current_window_start();
                        let count = view.panes.filter_model.current_window_len().max(1);
                        view.panes.filter_model.request_range(start, count);
                    }
                }
                if app_state.borrow().active_view == Some(owner_view_id) {
                    ui.set_filter_total_matches(total_rows as i32);
                }
                return;
            }
            let (is_active, follow_enabled, auto_scroll, model) = {
                let mut state = app_state.borrow_mut();
                let mut result = (false, false, false, None);
                let active_view = state.active_view;
                if let Some(view) = state.views.get_mut(&view_id) {
                    view.total_rows = total_rows;
                    view.model.update_total_rows(total_rows, appended);
                    if (!view.follow_enabled || !view.auto_scroll) && appended > 0 {
                        view.new_lines_text = format!("+{appended} new lines");
                    } else if view.follow_enabled && view.auto_scroll {
                        view.new_lines_text.clear();
                    }
                    result = (
                        active_view == Some(view_id),
                        view.follow_enabled,
                        view.auto_scroll,
                        Some(view.model.clone()),
                    );
                }
                result
            };

            if follow_enabled && auto_scroll {
                let window_size = ui.get_page_size().max(1) as u32;
                let start = total_rows.saturating_sub(window_size);
                if let Some(model) = model {
                    let window_start = model.current_window_start();
                    let window_len = model.current_window_len();
                    let needed_len = total_rows.saturating_sub(start);
                    if window_start != start || window_len < needed_len {
                        model.request_range(start, window_size);
                    }
                } else {
                    let _ = cmd_tx.send(EngineCommand::RequestRows {
                        view_id,
                        start,
                        count: window_size,
                    });
                }
            }

            if is_active {
                ui.set_total_rows(total_rows as i32);
                let new_lines_text = {
                    let state = app_state.borrow();
                    state
                        .views
                        .get(&view_id)
                        .map(|view| view.new_lines_text.clone())
                        .unwrap_or_default()
                };
                ui.set_new_lines_text(new_lines_text.into());
            }
        }
        EngineEvent::Error { message } => {
            ui.set_status_text(message.into());
        }
        EngineEvent::PerfStats {
            index_lps,
            indexed_mb_s,
            rows_req_s,
            ui_events_s,
        } => {
            let text = format!(
                "Index: {:>6.0} lps | IO: {:>6.1} MB/s | Req: {:>5.0}/s | UI: {:>4.0}/s",
                index_lps, indexed_mb_s, rows_req_s, ui_events_s
            );
            ui.set_perf_text(text.into());
        }
    }
}

fn handle_view_opened(
    ui: &MainWindow,
    app_state: &Rc<RefCell<AppState>>,
    cmd_tx: &Sender<EngineCommand>,
    view_id: ViewId,
    source_id: SourceId,
    title: String,
    kind: ViewKind,
    _query: ViewQuery,
) {
    let model = Rc::new(WindowedRowsModel::new(
        cmd_tx.clone(),
        WINDOW_SIZE,
        PREFETCH,
    ));
    model.set_view(view_id);
    {
        let mut state = app_state.borrow_mut();
        state.add_view(view_id, source_id, title.clone(), model);
        if let Some(view) = state.views.get_mut(&view_id) {
            view.follow_enabled = matches!(kind, ViewKind::Full);
        }
    }
    activate_view(ui, app_state, view_id);
    let enabled = app_state
        .borrow()
        .views
        .get(&view_id)
        .map(|view| view.follow_enabled)
        .unwrap_or(false);
    ui.set_status_text(format!("Opened {title}").into());
    let _ = cmd_tx.send(EngineCommand::SetFollow { view_id, enabled });
}

fn toggle_mark_row(
    _ui: &MainWindow,
    app_state: &Rc<RefCell<AppState>>,
    cmd_tx: &Sender<EngineCommand>,
    absolute_row: u32,
) {
    let state = app_state.borrow();
    let Some(view_id) = state.active_view else {
        return;
    };
    let Some(view) = state.views.get(&view_id) else {
        return;
    };
    let Some(row_id) = view.model.row_id_at(absolute_row) else {
        return;
    };
    let _ = cmd_tx.send(EngineCommand::ToggleMarkRow {
        source_view_id: view_id,
        row_id: lgx_protocol::RowId(row_id),
    });
}

fn activate_marked_row(
    ui: &MainWindow,
    app_state: &Rc<RefCell<AppState>>,
    cmd_tx: &Sender<EngineCommand>,
    index: usize,
) {
    let state = app_state.borrow();
    let Some(active_view) = state.active_view else {
        return;
    };
    let Some(view) = state.views.get(&active_view) else {
        return;
    };
    let Some(entry) = view.panes.marked_model.row_item_at(index as u32) else {
        return;
    };
    let _ = cmd_tx.send(EngineCommand::JumpToFullView {
        source_view_id: ViewId(entry.origin_view_id.max(0) as u64),
        row_id: lgx_protocol::RowId(entry.row_id.max(0) as u64),
    });
    ui.set_status_text("Jumping to marked line".into());
}

fn clear_filters(
    ui: &MainWindow,
    app_state: &Rc<RefCell<AppState>>,
    cmd_tx: &Sender<EngineCommand>,
) {
    close_filter_view_for_active(app_state, cmd_tx);
    reset_filter_controls(ui);
    ui.set_filter_total_matches(0);
    ui.set_selected_filter_row(-1);
    ui.set_status_text("Cleared filter view".into());
}

fn activate_filter_row(
    ui: &MainWindow,
    app_state: &Rc<RefCell<AppState>>,
    cmd_tx: &Sender<EngineCommand>,
    index: usize,
) {
    let (source_view_id, row_id) = {
        let state = app_state.borrow();
        let Some(active_view) = state.active_view else {
            return;
        };
        let Some(view) = state.views.get(&active_view) else {
            return;
        };
        let Some(filter_view_id) = view.panes.filter_view_id else {
            return;
        };
        let Some(entry) = view.panes.filter_model.row_item_at(index as u32) else {
            return;
        };
        (
            filter_view_id,
            lgx_protocol::RowId(entry.row_id.max(0) as u64),
        )
    };
    let _ = cmd_tx.send(EngineCommand::JumpToFullView {
        source_view_id,
        row_id,
    });
    ui.set_status_text("Jumping to filtered line".into());
}

fn close_filter_view_for_active(app_state: &Rc<RefCell<AppState>>, cmd_tx: &Sender<EngineCommand>) {
    let active_view = app_state.borrow().active_view;
    if let Some(active_view) = active_view {
        close_filter_view_for_owner(app_state, cmd_tx, active_view);
    }
}

fn close_filter_view_for_owner(
    app_state: &Rc<RefCell<AppState>>,
    cmd_tx: &Sender<EngineCommand>,
    owner_view_id: ViewId,
) {
    let view_id = {
        let mut state = app_state.borrow_mut();
        let Some(view) = state.views.get_mut(&owner_view_id) else {
            return;
        };
        view.panes.pending_filter_request = false;
        view.panes.filter_total_rows = 0;
        view.panes.filter_query = ViewQuery::default();
        view.panes.filter_model.clear();
        view.panes.filter_view_id.take()
    };
    if let Some(view_id) = view_id {
        let _ = cmd_tx.send(EngineCommand::CloseView { view_id });
    }
}

fn build_filter_query(ui: &MainWindow) -> ViewQuery {
    let severity_mask = severity_mask_from_ui(ui);
    let text = ui.get_filter_text().trim().to_string();
    ViewQuery {
        severity_mask,
        min_severity: None,
        max_severity: None,
        required_flags: 0,
        text: (!text.is_empty()).then_some(text),
        text_is_regex: ui.get_filter_text_regex(),
        text_message_only: ui.get_filter_text_message_only(),
        text_case_insensitive: ui.get_filter_text_icase(),
    }
}

fn severity_mask_from_ui(ui: &MainWindow) -> Option<u8> {
    let states = [
        ui.get_filter_sev_trace(),
        ui.get_filter_sev_debug(),
        ui.get_filter_sev_info(),
        ui.get_filter_sev_warn(),
        ui.get_filter_sev_error(),
        ui.get_filter_sev_fatal(),
    ];
    if states.iter().all(|checked| *checked) {
        return None;
    }
    Some(
        states
            .into_iter()
            .enumerate()
            .fold(
                0u8,
                |mask, (index, checked)| {
                    if checked {
                        mask | (1u8 << index)
                    } else {
                        mask
                    }
                },
            ),
    )
}

fn sync_filter_controls(ui: &MainWindow, query: &ViewQuery) {
    let severity_mask = query.severity_mask.or_else(|| {
        query.min_severity.map(|min| {
            let mut mask = 0u8;
            for sev in min..=6 {
                mask |= 1u8 << (sev - 1);
            }
            mask
        })
    });
    let has_severity_filter = severity_mask.is_some();
    ui.set_filter_sev_trace(!has_severity_filter || severity_selected(severity_mask, 1));
    ui.set_filter_sev_debug(!has_severity_filter || severity_selected(severity_mask, 2));
    ui.set_filter_sev_info(!has_severity_filter || severity_selected(severity_mask, 3));
    ui.set_filter_sev_warn(!has_severity_filter || severity_selected(severity_mask, 4));
    ui.set_filter_sev_error(!has_severity_filter || severity_selected(severity_mask, 5));
    ui.set_filter_sev_fatal(!has_severity_filter || severity_selected(severity_mask, 6));
    ui.set_filter_text(query.text.clone().unwrap_or_default().into());
    ui.set_filter_text_regex(query.text_is_regex);
    ui.set_filter_text_message_only(query.text_message_only);
    ui.set_filter_text_icase(query.text_case_insensitive);
}

fn severity_selected(mask: Option<u8>, sev: u8) -> bool {
    mask.map(|value| value & (1u8 << (sev - 1)) != 0)
        .unwrap_or(true)
}

fn reset_filter_controls(ui: &MainWindow) {
    ui.set_filter_sev_trace(true);
    ui.set_filter_sev_debug(true);
    ui.set_filter_sev_info(true);
    ui.set_filter_sev_warn(true);
    ui.set_filter_sev_error(true);
    ui.set_filter_sev_fatal(true);
    ui.set_filter_text(SharedString::default());
    ui.set_filter_text_regex(false);
    ui.set_filter_text_message_only(false);
    ui.set_filter_text_icase(false);
}

#[cfg(test)]
mod tests {
    use super::{
        collect_startup_files, load_recent_files_from_path, parse_startup_files,
        remove_recent_file_at_path, sanitize_recent_files, update_recent_files_at_path,
        RecentFileRecord,
    };
    use std::fs;
    use std::path::PathBuf;

    fn parse_args(args: &[&str]) -> Vec<PathBuf> {
        parse_startup_files(args.iter().copied())
    }

    fn temp_config_path(name: &str) -> PathBuf {
        let mut path = std::env::temp_dir();
        path.push(format!(
            "lgx_recent_test_{}_{}.yaml",
            std::process::id(),
            name
        ));
        path
    }

    #[test]
    fn positional_args_become_files() {
        let files = parse_args(&["first.log", "--theme", "light", "second.log"]);
        assert_eq!(
            files,
            vec![PathBuf::from("first.log"), PathBuf::from("second.log")]
        );
    }

    #[test]
    fn dangling_long_option_is_treated_as_file() {
        let files = parse_args(&["--lonely"]);
        assert_eq!(files, vec![PathBuf::from("--lonely")]);
    }

    #[test]
    fn helper_is_callable() {
        let _ = collect_startup_files();
    }

    #[test]
    fn recent_files_are_kept_most_recent_first_without_duplicates() {
        let path = temp_config_path("recent_order");
        let _ = fs::remove_file(&path);

        let first = update_recent_files_at_path(&path, "/tmp/one.log");
        let second = update_recent_files_at_path(&path, "/tmp/two.log");
        let third = update_recent_files_at_path(&path, "/tmp/one.log");

        assert_eq!(first.len(), 1);
        assert_eq!(
            second.first().map(|item| item.path.as_str()),
            Some("/tmp/two.log")
        );
        assert_eq!(
            third.first().map(|item| item.path.as_str()),
            Some("/tmp/one.log")
        );
        assert_eq!(third.len(), 2);

        let loaded = load_recent_files_from_path(&path);
        assert_eq!(loaded.len(), 2);
        assert_eq!(loaded[0].path, "/tmp/one.log");
        let _ = fs::remove_file(path);
    }

    #[test]
    fn recent_file_can_be_removed_from_config() {
        let path = temp_config_path("recent_remove");
        let _ = fs::remove_file(&path);

        let _ = update_recent_files_at_path(&path, "/tmp/one.log");
        let _ = update_recent_files_at_path(&path, "/tmp/two.log");
        let remaining = remove_recent_file_at_path(&path, 0);

        assert_eq!(remaining.len(), 1);
        assert_eq!(remaining[0].path, "/tmp/one.log");

        let loaded = load_recent_files_from_path(&path);
        assert_eq!(loaded.len(), 1);
        assert_eq!(loaded[0].path, "/tmp/one.log");
        let _ = fs::remove_file(path);
    }

    #[test]
    fn sanitize_recent_files_sorts_and_limits() {
        let input = (0..60)
            .map(|idx| RecentFileRecord {
                path: format!("/tmp/{idx}.log"),
                opened_at: idx as i64,
            })
            .chain(std::iter::once(RecentFileRecord {
                path: "/tmp/59.log".to_string(),
                opened_at: 999,
            }))
            .collect::<Vec<_>>();
        let recent = sanitize_recent_files(input);
        assert_eq!(recent.len(), 50);
        assert_eq!(recent[0].path, "/tmp/59.log");
    }
}
