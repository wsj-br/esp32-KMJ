use std::fs;
use std::path::PathBuf;
use std::sync::Mutex;
use std::time::{Duration, Instant};

use serde::{Deserialize, Serialize};
use tauri::{PhysicalPosition, PhysicalSize, Runtime, WebviewWindow, Window};

const SAVE_GAP: Duration = Duration::from_millis(250);
const QUIET_AFTER_RESTORE: Duration = Duration::from_millis(500);

static QUIET_UNTIL: Mutex<Option<Instant>> = Mutex::new(None);

fn begin_quiet() {
    if let Ok(mut until) = QUIET_UNTIL.lock() {
        *until = Some(Instant::now() + QUIET_AFTER_RESTORE);
    }
}

fn is_quiet() -> bool {
    QUIET_UNTIL.lock().ok().and_then(|until| *until).is_some_and(|until| Instant::now() < until)
}

#[derive(Clone, Serialize, Deserialize)]
struct Geom {
    x: i32,
    y: i32,
    width: u32,
    height: u32,
    #[serde(default)]
    maximized: bool,
}

fn path() -> PathBuf {
    let home = std::env::var("HOME")
        .or_else(|_| std::env::var("USERPROFILE"))
        .map(PathBuf::from)
        .unwrap_or_else(|_| std::env::temp_dir());
    home.join(".config").join("kmj").join("window.json")
}

fn load() -> Option<Geom> {
    let text = fs::read_to_string(path()).ok()?;
    serde_json::from_str(&text).ok()
}

fn write(geom: &Geom) {
    let path = path();
    if let Some(parent) = path.parent() {
        let _ = fs::create_dir_all(parent);
    }
    if let Ok(text) = serde_json::to_string_pretty(geom) {
        let _ = fs::write(path, text);
    }
}

fn work_areas(window: &WebviewWindow) -> Vec<(i32, i32, u32, u32)> {
    window
        .available_monitors()
        .unwrap_or_default()
        .into_iter()
        .map(|monitor| {
            let area = monitor.work_area();
            (area.position.x, area.position.y, area.size.width, area.size.height)
        })
        .collect()
}

fn clamp_position(x: i32, y: i32, width: u32, height: u32, areas: &[(i32, i32, u32, u32)]) -> (i32, i32) {
    if areas.is_empty() {
        return (x, y);
    }
    let w = width as i32;
    let h = height as i32;
    let bar = h.min(48);
    let visible = |ax, ay, aw: u32, ah: u32| {
        x < ax + aw as i32 && x + w > ax && y < ay + ah as i32 && y + bar > ay
    };
    if areas.iter().any(|&(ax, ay, aw, ah)| visible(ax, ay, aw, ah)) {
        return (x, y);
    }
    let (ax, ay, _, _) = areas[0];
    (ax + 48, ay + 48)
}

pub fn restore(window: &WebviewWindow) {
    let Some(geom) = load() else {
        return;
    };
    begin_quiet();
    let (x, y) = clamp_position(geom.x, geom.y, geom.width, geom.height, &work_areas(window));
    if geom.width >= 200 && geom.height >= 150 {
        let _ = window.set_size(PhysicalSize::new(geom.width, geom.height));
    }
    let _ = window.set_position(PhysicalPosition::new(x, y));
    if geom.maximized {
        let _ = window.maximize();
    }
}

pub fn save<R: Runtime>(window: &Window<R>) {
    if is_quiet() || window.is_minimized().unwrap_or(false) {
        return;
    }
    let Ok(pos) = window.outer_position() else {
        return;
    };
    let maximized = window.is_maximized().unwrap_or(false);
    let mut geom = load().unwrap_or(Geom { x: pos.x, y: pos.y, width: 980, height: 680, maximized: false });
    if !maximized {
        let Ok(size) = window.inner_size() else {
            return;
        };
        if size.width < 200 || size.height < 150 {
            return;
        }
        geom.x = pos.x;
        geom.y = pos.y;
        geom.width = size.width;
        geom.height = size.height;
    }
    geom.maximized = maximized;
    write(&geom);
}

pub fn save_debounced<R: Runtime>(window: &Window<R>, last: &std::sync::Mutex<Instant>) {
    let mut last = last.lock().expect("window save");
    if last.elapsed() < SAVE_GAP {
        return;
    }
    *last = Instant::now();
    drop(last);
    save(window);
}

#[cfg(test)]
mod tests {
    use super::clamp_position;

    #[test]
    fn keeps_a_window_that_still_hits_a_monitor() {
        let areas = [(0, 0, 1920, 1080)];
        assert_eq!(clamp_position(100, 80, 980, 680, &areas), (100, 80));
    }

    #[test]
    fn moves_a_window_that_misses_every_monitor() {
        let areas = [(0, 0, 1920, 1080)];
        assert_eq!(clamp_position(4000, 2000, 980, 680, &areas), (48, 48));
    }
}
