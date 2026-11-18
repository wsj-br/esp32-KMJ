use std::fs;
use std::path::PathBuf;
use std::sync::Mutex;
use std::time::{Duration, Instant};

use serde::{Deserialize, Serialize};
use tauri::{PhysicalPosition, PhysicalSize, Runtime, WebviewWindow, Window};

const SAVE_GAP: Duration = Duration::from_millis(250);
const QUIET_AFTER_RESTORE: Duration = Duration::from_millis(500);
// Client-side decorations (title bar and shadow) are larger than this only
// with an absurd scale factor. A real resize past it is not the restore echo.
const DECOR_MAX: (u32, u32) = (500, 500);

static QUIET_UNTIL: Mutex<Option<Instant>> = Mutex::new(None);
static DECOR: Mutex<(u32, u32)> = Mutex::new((0, 0));
static PENDING: Mutex<Option<Pending>> = Mutex::new(None);

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
    // Extra pixels `inner_size` counts beyond the size passed to `set_size`.
    // GTK's client-side title bar and shadow sit in that gap: saving the
    // reported size and restoring it with `set_size` adds the frame every launch.
    #[serde(default)]
    decor_w: u32,
    #[serde(default)]
    decor_h: u32,
}

#[derive(Clone, Copy)]
enum Pending {
    // `content` is what restore passed to `set_size`. `visual` is the size to
    // keep on disk, which is what the window should come back as.
    Measure { content: (u32, u32), visual: (u32, u32) },
    // Ignore a repeat of the oversized echo until the corrected size lands.
    Settle { visual: (u32, u32), inflated: (u32, u32) },
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
enum RestorePlan {
    Skip,
    Keep { decor: Option<(u32, u32)> },
    Resize { width: u32, height: u32, decor: (u32, u32) },
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

fn content_size(visual: (u32, u32), decor: (u32, u32)) -> (u32, u32) {
    let width = visual.0.saturating_sub(decor.0);
    let height = visual.1.saturating_sub(decor.1);
    if width >= 200 && height >= 150 { (width, height) } else { visual }
}

fn pinned_visual() -> Option<(u32, u32)> {
    PENDING.lock().ok().and_then(|pending| match *pending {
        Some(Pending::Measure { visual, .. } | Pending::Settle { visual, .. }) => Some(visual),
        None => None,
    })
}

fn plan_restore(pending: &mut Option<Pending>, reported: (u32, u32), settling: bool) -> RestorePlan {
    let Some(state) = *pending else {
        return RestorePlan::Keep { decor: None };
    };
    match state {
        Pending::Measure { content, visual } => {
            if reported.0 < content.0 || reported.1 < content.1 {
                if settling {
                    return RestorePlan::Skip;
                }
                *pending = None;
                return RestorePlan::Keep { decor: None };
            }
            let gap = (reported.0 - content.0, reported.1 - content.1);
            if gap.0 > DECOR_MAX.0 || gap.1 > DECOR_MAX.1 {
                if settling {
                    return RestorePlan::Skip;
                }
                *pending = None;
                return RestorePlan::Keep { decor: None };
            }
            if reported == visual {
                // An exact match while the window is still opening can be the
                // size from before GTK adds the frame. Wait that out, then
                // treat a later exact match as "this platform adds nothing".
                if settling && gap == (0, 0) {
                    return RestorePlan::Skip;
                }
                *pending = None;
                return RestorePlan::Keep { decor: Some(gap) };
            }
            if !settling {
                *pending = None;
                return RestorePlan::Keep { decor: None };
            }
            let width = visual.0.saturating_sub(gap.0);
            let height = visual.1.saturating_sub(gap.1);
            if width < 200 || height < 150 {
                *pending = None;
                return RestorePlan::Keep { decor: None };
            }
            *pending = Some(Pending::Settle { visual, inflated: reported });
            RestorePlan::Resize { width, height, decor: gap }
        }
        Pending::Settle { inflated, .. } => {
            if reported == inflated {
                return RestorePlan::Skip;
            }
            *pending = None;
            RestorePlan::Keep { decor: None }
        }
    }
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
        let visual = (geom.width, geom.height);
        let decor = (geom.decor_w, geom.decor_h);
        let content = content_size(visual, decor);
        if let Ok(mut slot) = DECOR.lock() {
            *slot = if content == visual { (0, 0) } else { decor };
        }
        if let Ok(mut pending) = PENDING.lock() {
            *pending = Some(Pending::Measure { content, visual });
        }
        let _ = window.set_size(PhysicalSize::new(content.0, content.1));
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
    let mut geom = load().unwrap_or(Geom {
        x: pos.x,
        y: pos.y,
        width: 950,
        height: 700,
        maximized: false,
        decor_w: 0,
        decor_h: 0,
    });
    if !maximized {
        let size = if let Some(visual) = pinned_visual() {
            visual
        } else {
            let Ok(size) = window.inner_size() else {
                return;
            };
            if size.width < 200 || size.height < 150 {
                return;
            }
            (size.width, size.height)
        };
        geom.x = pos.x;
        geom.y = pos.y;
        geom.width = size.0;
        geom.height = size.1;
    }
    if let Ok(decor) = DECOR.lock() {
        geom.decor_w = decor.0;
        geom.decor_h = decor.1;
    }
    geom.maximized = maximized;
    write(&geom);
}

/// `true` when `size` is the restore echo and must not be written to disk.
///
/// On Linux, GTK reports a size that includes the client-side title bar and
/// shadow, while `set_size` applies the content size. The first resize after a
/// restore is larger than what was requested by exactly that frame.
pub fn absorb_restore_resize<R: Runtime>(window: &Window<R>, size: PhysicalSize<u32>) -> bool {
    let reported = (size.width, size.height);
    let plan = {
        let mut pending = match PENDING.lock() {
            Ok(pending) => pending,
            Err(_) => return false,
        };
        plan_restore(&mut pending, reported, is_quiet())
    };
    match plan {
        RestorePlan::Skip => true,
        RestorePlan::Keep { decor } => {
            if let Some(decor) = decor {
                if let Ok(mut slot) = DECOR.lock() {
                    *slot = decor;
                }
            }
            false
        }
        RestorePlan::Resize { width, height, decor } => {
            if let Ok(mut slot) = DECOR.lock() {
                *slot = decor;
            }
            let _ = window.set_size(PhysicalSize::new(width, height));
            true
        }
    }
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
    use super::{clamp_position, plan_restore, Pending, RestorePlan};

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

    #[test]
    fn frame_around_a_restored_window_is_removed_once() {
        let visual = (1288, 825);
        let mut pending = Some(Pending::Measure { content: visual, visual });
        let inflated = (1340, 934);
        assert_eq!(
            plan_restore(&mut pending, inflated, true),
            RestorePlan::Resize { width: 1236, height: 716, decor: (52, 109) }
        );
        assert_eq!(plan_restore(&mut pending, inflated, true), RestorePlan::Skip);
        assert_eq!(plan_restore(&mut pending, visual, true), RestorePlan::Keep { decor: None });
        assert!(pending.is_none());
    }

    #[test]
    fn a_known_frame_round_trips_without_another_resize() {
        let visual = (1288, 825);
        let decor = (52, 109);
        let mut pending = Some(Pending::Measure { content: (visual.0 - decor.0, visual.1 - decor.1), visual });
        assert_eq!(plan_restore(&mut pending, visual, true), RestorePlan::Keep { decor: Some(decor) });
        assert!(pending.is_none());
    }

    #[test]
    fn an_exact_match_waits_out_the_opening_frame() {
        let visual = (950, 700);
        let mut pending = Some(Pending::Measure { content: visual, visual });
        assert_eq!(plan_restore(&mut pending, visual, true), RestorePlan::Skip);
        assert!(pending.is_some());
        assert_eq!(plan_restore(&mut pending, visual, false), RestorePlan::Keep { decor: Some((0, 0)) });
        assert!(pending.is_none());
    }

    #[test]
    fn a_resize_after_opening_is_kept() {
        let visual = (950, 700);
        let mut pending = Some(Pending::Measure { content: visual, visual });
        assert_eq!(plan_restore(&mut pending, visual, true), RestorePlan::Skip);
        assert_eq!(plan_restore(&mut pending, (980, 730), false), RestorePlan::Keep { decor: None });
        assert!(pending.is_none());
    }

    #[test]
    fn a_smaller_window_after_opening_is_kept() {
        let visual = (950, 700);
        let mut pending = Some(Pending::Measure { content: visual, visual });
        assert_eq!(plan_restore(&mut pending, (800, 600), false), RestorePlan::Keep { decor: None });
        assert!(pending.is_none());
    }
}
