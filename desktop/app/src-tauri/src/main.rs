use std::path::PathBuf;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::{Arc, Mutex};
use std::time::{Duration, Instant};

mod improv;
mod window_state;

use kmj_capture::{open as open_capture, InputCapture};
use kmj_client::{
    apply_capture, discover, normalize_hotkey, parse_hotkey, probe_device, start_pairing, start_session, GrabFsm,
    KnownDevice, Pairing, SecureLink, Store, EMULATOR_CLOSED,
};
use serde::Serialize;
use tauri::{AppHandle, Emitter, Manager, State, WindowEvent};

struct Shared {
    store: Mutex<Store>,
    pairing: Mutex<Option<Pairing>>,
    link: Mutex<Option<SecureLink>>,
    capture: Mutex<Option<Box<dyn InputCapture>>>,
    fsm: Mutex<GrabFsm>,
    running: Mutex<bool>,
    notice: Mutex<String>,
    intent: Mutex<u64>,
    chord_watch: Mutex<Option<Arc<AtomicBool>>>,
}

struct AppState(Arc<Shared>);

#[cfg(target_os = "linux")]
#[derive(Clone, Serialize)]
struct ChordKey {
    usage: u8,
    down: bool,
}

#[derive(Serialize, Clone)]
struct Status {
    phase: String,
    hotkey: String,
    theme: String,
    peers: usize,
    code: String,
    notice: String,
    linked: bool,
}

#[derive(Serialize, Clone)]
struct DeviceRow {
    id: String,
    name: String,
    host: String,
    port: u16,
    paired: bool,
}

impl DeviceRow {
    fn from_known(device: KnownDevice, peers: &[[u8; 32]]) -> Self {
        let paired = device.is_paired(peers);
        Self { id: device.id, name: device.name, host: device.host, port: device.port, paired }
    }
}

fn store_path() -> PathBuf {
    let home = std::env::var("HOME")
        .or_else(|_| std::env::var("USERPROFILE"))
        .map(PathBuf::from)
        .unwrap_or_else(|_| std::env::temp_dir());
    home.join(".config").join("kmj").join("client.json")
}

#[tauri::command]
fn build_stamp() -> &'static str {
    env!("KMJ_BUILD_TIMESTAMP")
}

#[tauri::command]
fn status(state: State<AppState>) -> Status {
    let store = state.0.store.lock().expect("store");
    let code = state.0.pairing.lock().expect("pair").as_ref().map(|p| p.code.clone()).unwrap_or_default();
    let phase = if *state.0.running.lock().expect("run") { "capturing" } else { "released" };
    let notice = state.0.notice.lock().expect("notice").clone();
    let linked = state.0.link.lock().expect("link").is_some();
    Status {
        phase: phase.into(),
        hotkey: store.hotkey.clone(),
        theme: store.theme.clone(),
        peers: store.peers.len(),
        code,
        notice,
        linked,
    }
}

fn intent_is(shared: &Shared, intent: u64) -> Result<bool, String> {
    Ok(*shared.intent.lock().map_err(|e| e.to_string())? == intent)
}

fn apply_window_theme(window: &tauri::WebviewWindow, theme: &str) {
    let theme = match theme {
        "light" => Some(tauri::Theme::Light),
        "dark" => Some(tauri::Theme::Dark),
        _ => None,
    };
    let _ = window.set_theme(theme);
}

#[tauri::command]
fn scan(state: State<AppState>) -> Result<Vec<DeviceRow>, String> {
    let endpoints = discover(Duration::from_millis(1500));
    let mut store = state.0.store.lock().map_err(|e| e.to_string())?;
    store.replace_devices(&endpoints).map_err(|e| e.to_string())?;
    Ok(store.devices.clone().into_iter().map(|device| DeviceRow::from_known(device, &store.peers)).collect())
}

#[tauri::command]
fn pair(state: State<AppState>, host: String, port: u16, intent: u64) -> Result<String, String> {
    if !intent_is(&state.0, intent)? {
        return Err("cancelled".into());
    }
    let identity = state.0.store.lock().map_err(|e| e.to_string())?.identity.clone();
    let pairing = start_pairing(&format!("{host}:{port}"), &identity, "desk").map_err(|e| e.to_string())?;
    let code = pairing.code.clone();
    let mut slot = state.0.pairing.lock().map_err(|e| e.to_string())?;
    let current = *state.0.intent.lock().map_err(|e| e.to_string())?;
    if current != intent {
        return Err("cancelled".into());
    }
    *slot = Some(pairing);
    Ok(code)
}

#[tauri::command]
async fn confirm_pair(state: State<'_, AppState>, intent: u64) -> Result<(), String> {
    if !intent_is(&state.0, intent)? {
        *state.0.pairing.lock().map_err(|e| e.to_string())? = None;
        return Err("cancelled".into());
    }
    let shared = Arc::clone(&state.0);
    tauri::async_runtime::spawn_blocking(move || {
        if !intent_is(&shared, intent)? {
            *shared.pairing.lock().map_err(|e| e.to_string())? = None;
            return Err("cancelled".into());
        }
        let pairing = shared.pairing.lock().map_err(|e| e.to_string())?.take().ok_or("no pairing in progress")?;
        let peer = pairing.confirm_with_timeout(Duration::from_secs(60)).map_err(|e| e.to_string())?;
        shared.store.lock().map_err(|e| e.to_string())?.add_peer(peer).map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
fn connect_device(state: State<AppState>, host: String, port: u16, intent: u64) -> Result<(), String> {
    if !intent_is(&state.0, intent)? {
        return Ok(());
    }
    let identity = state.0.store.lock().map_err(|e| e.to_string())?.identity.clone();
    let opened = start_session(&format!("{host}:{port}"), &identity, "desk");
    if !intent_is(&state.0, intent)? {
        return Ok(());
    }
    match opened {
        Ok(link) => {
            *state.0.link.lock().map_err(|e| e.to_string())? = Some(link);
            state.0.notice.lock().map_err(|e| e.to_string())?.clear();
            Ok(())
        }
        Err(err) => {
            *state.0.link.lock().map_err(|e| e.to_string())? = None;
            let message = err.to_string();
            if message == EMULATOR_CLOSED {
                state.0.notice.lock().map_err(|e| e.to_string())?.clear();
            }
            Err(message)
        }
    }
}

#[tauri::command]
fn device_reach(host: String, port: u16) -> String {
    probe_device(&format!("{host}:{port}")).into()
}

#[tauri::command]
fn disconnect(state: State<AppState>, intent: u64) -> Result<(), String> {
    if !intent_is(&state.0, intent)? {
        return Ok(());
    }
    *state.0.running.lock().map_err(|e| e.to_string())? = false;
    if let Some(grab) = state.0.capture.lock().map_err(|e| e.to_string())?.as_mut() {
        grab.stop();
    }
    *state.0.link.lock().map_err(|e| e.to_string())? = None;
    Ok(())
}

#[tauri::command]
fn set_intent(state: State<AppState>, intent: u64) -> Result<(), String> {
    let mut current = state.0.intent.lock().map_err(|e| e.to_string())?;
    if intent >= *current {
        *current = intent;
    }
    Ok(())
}

#[tauri::command]
fn unpair(state: State<AppState>, id: String) -> Result<(), String> {
    state.0.store.lock().map_err(|e| e.to_string())?.remove_peer_id(&id).map_err(|e| e.to_string())
}

#[tauri::command]
fn set_theme(app: AppHandle, state: State<AppState>, theme: String) -> Result<(), String> {
    if !matches!(theme.as_str(), "system" | "light" | "dark") {
        return Err("theme must be system, light, or dark".into());
    }
    {
        let mut store = state.0.store.lock().map_err(|e| e.to_string())?;
        store.theme = theme.clone();
        store.save().map_err(|e| e.to_string())?;
    }
    if let Some(window) = app.get_webview_window("main") {
        apply_window_theme(&window, &theme);
    }
    Ok(())
}

#[tauri::command]
fn set_chord_listen(app: AppHandle, state: State<AppState>, on: bool) -> Result<(), String> {
    let mut watch = state.0.chord_watch.lock().map_err(|e| e.to_string())?;
    if let Some(stop) = watch.take() {
        stop.store(true, Ordering::SeqCst);
    }
    if !on {
        return Ok(());
    }
    #[cfg(target_os = "linux")]
    {
        let stop = Arc::new(AtomicBool::new(false));
        let emit_app = app.clone();
        kmj_capture::spawn_chord_watch(Arc::clone(&stop), move |usage, down| {
            let _ = emit_app.emit("chord-key", ChordKey { usage, down });
        })
        .map_err(|e| e.to_string())?;
        *watch = Some(stop);
        return Ok(());
    }
    #[cfg(not(target_os = "linux"))]
    {
        let _ = app;
        Err("recording a chord from the keyboard is only available on Linux".into())
    }
}

#[tauri::command]
fn set_hotkey(state: State<AppState>, hotkey: String) -> Result<(), String> {
    let Some(hotkey) = normalize_hotkey(&hotkey) else {
        return Err("use a chord such as ctrl+alt+esc".into());
    };
    let chord = parse_hotkey(&hotkey);
    let mut store = state.0.store.lock().map_err(|e| e.to_string())?;
    store.hotkey = hotkey;
    store.save().map_err(|e| e.to_string())?;
    *state.0.fsm.lock().map_err(|e| e.to_string())? = GrabFsm::new(chord);
    Ok(())
}

#[tauri::command]
fn capture(app: AppHandle, state: State<AppState>) -> Result<(), String> {
    {
        let mut sessions = state.0.link.lock().map_err(|e| e.to_string())?;
        let Some(link) = sessions.as_mut() else {
            drop(sessions);
            let notice = state.0.notice.lock().map_err(|e| e.to_string())?.clone();
            return Err(if notice.is_empty() { "connect to the KMJ device first".into() } else { notice });
        };
        if let Err(err) = link.poll().and_then(|_| link.send_ping(0, 0)) {
            *sessions = None;
            return Err(err.to_string());
        }
    }
    let mut grab = open_capture().map_err(|e| e.to_string())?;
    grab.start().map_err(|e| e.to_string())?;
    *state.0.capture.lock().map_err(|e| e.to_string())? = Some(grab);
    state.0.fsm.lock().map_err(|e| e.to_string())?.arm();
    state.0.notice.lock().map_err(|e| e.to_string())?.clear();
    *state.0.running.lock().map_err(|e| e.to_string())? = true;
    let _ = app.emit("phase", "capturing");
    Ok(())
}

#[tauri::command]
async fn ble_scan() -> Result<Vec<improv::BleDevice>, String> {
    improv::scan().await
}

#[tauri::command]
async fn ble_join(id: String, ssid: String, password: String) -> Result<String, String> {
    improv::join(&id, &ssid, &password).await
}

#[tauri::command]
async fn ble_networks(id: String) -> Result<Vec<improv::WifiNetwork>, String> {
    improv::networks(&id).await
}

fn capture_loop(app: AppHandle, shared: Arc<Shared>) {
    let started = std::time::Instant::now();
    let mut last_ping = started;
    loop {
        if let Some(reason) = service_link(&shared, &mut last_ping, started) {
            end_capture(&app, &shared, Some(reason));
            std::thread::sleep(Duration::from_millis(30));
            continue;
        }
        if !*shared.running.lock().expect("run") {
            std::thread::sleep(Duration::from_millis(30));
            continue;
        }
        let events = shared.capture.lock().expect("cap").as_mut().map(|g| g.poll()).unwrap_or_default();
        let mut release = false;
        let mut failure = None;
        if let Some(session) = shared.link.lock().expect("link").as_mut() {
            let mut fsm = shared.fsm.lock().expect("fsm");
            for event in events {
                match apply_capture(session, &mut fsm, event) {
                    Ok(true) => {
                        release = true;
                        break;
                    }
                    Ok(false) => {}
                    Err(err) => {
                        failure = Some(err.to_string());
                        release = true;
                        break;
                    }
                }
            }
        } else {
            failure = Some("device connection closed".into());
            release = true;
        }
        if release {
            end_capture(&app, &shared, failure);
        } else {
            std::thread::sleep(Duration::from_millis(4));
        }
    }
}

fn service_link(shared: &Shared, last_ping: &mut std::time::Instant, started: std::time::Instant) -> Option<String> {
    let mut sessions = shared.link.lock().expect("link");
    let session = sessions.as_mut()?;
    if let Err(err) = session.poll() {
        *sessions = None;
        return Some(err.to_string());
    }
    if last_ping.elapsed() >= Duration::from_secs(1) {
        let millis = u32::try_from(started.elapsed().as_millis()).unwrap_or(u32::MAX);
        if let Err(err) = session.send_ping(millis, 0) {
            *sessions = None;
            return Some(err.to_string());
        }
        *last_ping = std::time::Instant::now();
    }
    None
}

fn end_capture(app: &AppHandle, shared: &Shared, failure: Option<String>) {
    *shared.running.lock().expect("run") = false;
    if let Some(grab) = shared.capture.lock().expect("cap").as_mut() {
        grab.stop();
    }
    if let Some(reason) = failure {
        *shared.notice.lock().expect("notice") = reason;
    }
    let _ = app.emit("phase", "released");
}

fn main() {
    let store = Store::load(store_path()).expect("store");
    let fsm = GrabFsm::new(parse_hotkey(&store.hotkey));
    let shared = Arc::new(Shared {
        store: Mutex::new(store),
        pairing: Mutex::new(None),
        link: Mutex::new(None),
        capture: Mutex::new(None),
        fsm: Mutex::new(fsm),
        running: Mutex::new(false),
        notice: Mutex::new(String::new()),
        intent: Mutex::new(0),
        chord_watch: Mutex::new(None),
    });
    let theme_shared = Arc::clone(&shared);
    let loop_shared = Arc::clone(&shared);
    tauri::Builder::default()
        .manage(AppState(shared))
        .invoke_handler(tauri::generate_handler![
            build_stamp,
            status,
            scan,
            pair,
            confirm_pair,
            connect_device,
            device_reach,
            disconnect,
            set_intent,
            unpair,
            set_theme,
            set_hotkey,
            set_chord_listen,
            capture,
            ble_scan,
            ble_join,
            ble_networks
        ])
        .setup(move |app| {
            if let Some(window) = app.get_webview_window("main") {
                window_state::restore(&window);
                let theme = theme_shared.store.lock().expect("store").theme.clone();
                apply_window_theme(&window, &theme);
                let _ = window.show();
            }
            let handle = app.handle().clone();
            std::thread::spawn(move || capture_loop(handle, loop_shared));
            Ok(())
        })
        .on_window_event({
            let last_save = Mutex::new(Instant::now() - Duration::from_secs(1));
            move |window, event| match event {
                WindowEvent::Resized(size) => {
                    if !window_state::absorb_restore_resize(window, *size) {
                        window_state::save_debounced(window, &last_save);
                    }
                }
                WindowEvent::Moved(_) => {
                    window_state::save_debounced(window, &last_save);
                }
                WindowEvent::CloseRequested { .. } | WindowEvent::Destroyed => {
                    window_state::save(window);
                }
                _ => {}
            }
        })
        .run(tauri::generate_context!())
        .expect("error while running KMJ");
}
