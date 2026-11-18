use std::path::PathBuf;
use std::sync::{Arc, Mutex};
use std::time::Duration;

mod improv;

use kmj_capture::{open as open_capture, InputCapture};
use kmj_client::{
    apply_capture, discover, parse_hotkey, start_pairing, start_session, GrabFsm, KnownDevice, Pairing, SecureLink,
    Store,
};
use serde::Serialize;
use tauri::{AppHandle, Emitter, State};

struct Shared {
    store: Mutex<Store>,
    pairing: Mutex<Option<Pairing>>,
    link: Mutex<Option<SecureLink>>,
    capture: Mutex<Option<Box<dyn InputCapture>>>,
    fsm: Mutex<GrabFsm>,
    running: Mutex<bool>,
}

struct AppState(Arc<Shared>);

#[derive(Serialize, Clone)]
struct Status {
    phase: String,
    hotkey: String,
    peers: usize,
    code: String,
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
    let base = std::env::var("HOME").map(PathBuf::from).unwrap_or_else(|_| std::env::temp_dir()).join(".config");
    base.join("kmj").join("client.json")
}

#[tauri::command]
fn status(state: State<AppState>) -> Status {
    let store = state.0.store.lock().expect("store");
    let code = state.0.pairing.lock().expect("pair").as_ref().map(|p| p.code.clone()).unwrap_or_default();
    let phase = if *state.0.running.lock().expect("run") { "capturing" } else { "released" };
    Status { phase: phase.into(), hotkey: store.hotkey.clone(), peers: store.peers.len(), code }
}

#[tauri::command]
fn scan(state: State<AppState>) -> Result<Vec<DeviceRow>, String> {
    let endpoints = discover(Duration::from_millis(1500));
    let mut store = state.0.store.lock().map_err(|e| e.to_string())?;
    store.replace_devices(&endpoints).map_err(|e| e.to_string())?;
    Ok(store.devices.clone().into_iter().map(|device| DeviceRow::from_known(device, &store.peers)).collect())
}

#[tauri::command]
fn pair(state: State<AppState>, host: String, port: u16) -> Result<String, String> {
    let identity = state.0.store.lock().map_err(|e| e.to_string())?.identity.clone();
    let pairing = start_pairing(&format!("{host}:{port}"), &identity, "desk").map_err(|e| e.to_string())?;
    let code = pairing.code.clone();
    *state.0.pairing.lock().map_err(|e| e.to_string())? = Some(pairing);
    Ok(code)
}

#[tauri::command]
async fn confirm_pair(state: State<'_, AppState>) -> Result<(), String> {
    let shared = Arc::clone(&state.0);
    tauri::async_runtime::spawn_blocking(move || {
        let pairing = shared.pairing.lock().map_err(|e| e.to_string())?.take().ok_or("no pairing in progress")?;
        let peer = pairing.confirm_with_timeout(Duration::from_secs(60)).map_err(|e| e.to_string())?;
        shared.store.lock().map_err(|e| e.to_string())?.add_peer(peer).map_err(|e| e.to_string())
    })
    .await
    .map_err(|e| e.to_string())?
}

#[tauri::command]
fn connect_device(state: State<AppState>, host: String, port: u16) -> Result<(), String> {
    let identity = state.0.store.lock().map_err(|e| e.to_string())?.identity.clone();
    let link = start_session(&format!("{host}:{port}"), &identity, "desk").map_err(|e| e.to_string())?;
    *state.0.link.lock().map_err(|e| e.to_string())? = Some(link);
    Ok(())
}

#[tauri::command]
fn disconnect(state: State<AppState>) -> Result<(), String> {
    *state.0.running.lock().map_err(|e| e.to_string())? = false;
    if let Some(grab) = state.0.capture.lock().map_err(|e| e.to_string())?.as_mut() {
        grab.stop();
    }
    *state.0.link.lock().map_err(|e| e.to_string())? = None;
    Ok(())
}

#[tauri::command]
fn set_hotkey(state: State<AppState>, hotkey: String) -> Result<(), String> {
    let chord = parse_hotkey(&hotkey);
    if chord.len() < 2 {
        return Err("use a chord such as ctrl+alt+esc".into());
    }
    let mut store = state.0.store.lock().map_err(|e| e.to_string())?;
    store.hotkey = hotkey;
    store.save().map_err(|e| e.to_string())?;
    *state.0.fsm.lock().map_err(|e| e.to_string())? = GrabFsm::new(chord);
    Ok(())
}

#[tauri::command]
fn capture(app: AppHandle, state: State<AppState>, enable: bool) -> Result<(), String> {
    if !enable {
        *state.0.running.lock().map_err(|e| e.to_string())? = false;
        if let Some(link) = state.0.link.lock().map_err(|e| e.to_string())?.as_mut() {
            let _ = link.send_release();
        }
        if let Some(grab) = state.0.capture.lock().map_err(|e| e.to_string())?.as_mut() {
            grab.stop();
        }
        let _ = app.emit("phase", "released");
        return Ok(());
    }
    if state.0.link.lock().map_err(|e| e.to_string())?.is_none() {
        return Err("connect to the KMJ device first".into());
    }
    let mut grab = open_capture().map_err(|e| e.to_string())?;
    grab.start().map_err(|e| e.to_string())?;
    *state.0.capture.lock().map_err(|e| e.to_string())? = Some(grab);
    state.0.fsm.lock().map_err(|e| e.to_string())?.arm();
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
    loop {
        if !*shared.running.lock().expect("run") {
            std::thread::sleep(Duration::from_millis(30));
            continue;
        }
        let events = shared.capture.lock().expect("cap").as_mut().map(|g| g.poll()).unwrap_or_default();
        let mut release = false;
        if let Some(session) = shared.link.lock().expect("link").as_mut() {
            let mut fsm = shared.fsm.lock().expect("fsm");
            for event in events {
                match apply_capture(session, &mut fsm, event) {
                    Ok(true) => {
                        release = true;
                        break;
                    }
                    Ok(false) => {}
                    Err(_) => {
                        release = true;
                        break;
                    }
                }
            }
        }
        if release {
            *shared.running.lock().expect("run") = false;
            if let Some(grab) = shared.capture.lock().expect("cap").as_mut() {
                grab.stop();
            }
            let _ = app.emit("phase", "released");
        } else {
            std::thread::sleep(Duration::from_millis(4));
        }
    }
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
    });
    let loop_shared = Arc::clone(&shared);
    tauri::Builder::default()
        .manage(AppState(shared))
        .invoke_handler(tauri::generate_handler![
            status,
            scan,
            pair,
            confirm_pair,
            connect_device,
            disconnect,
            set_hotkey,
            capture,
            ble_scan,
            ble_join,
            ble_networks
        ])
        .setup(move |app| {
            let handle = app.handle().clone();
            std::thread::spawn(move || capture_loop(handle, loop_shared));
            Ok(())
        })
        .run(tauri::generate_context!())
        .expect("error while running KMJ");
}
