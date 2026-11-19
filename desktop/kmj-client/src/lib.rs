//! Desktop session: discovery, pairing, the encrypted link, and the capture state machine.

use std::collections::BTreeMap;
use std::fs;
use std::io::{Read, Write};
use std::net::{TcpListener, TcpStream, ToSocketAddrs};
use std::path::PathBuf;
use std::time::Duration;

use kmj_capture::CaptureEvent;
use kmj_proto::{Identity, Link};
use serde::{Deserialize, Serialize};

pub use kmj_capture::open as open_capture;
pub use kmj_proto::Error as ProtoError;

/// Shown when a discovered board does not accept the KM port. The listener runs
/// only while KM Emulator is the active function.
pub const EMULATOR_CLOSED: &str = "Select the KM Emulator function on the device to send keystrokes and mouse movements to the connected computer";
pub const DEVICE_UNREACHABLE: &str = EMULATOR_CLOSED;

pub mod improv;

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error(transparent)]
    Proto(#[from] kmj_proto::Error),
    #[error(transparent)]
    Io(#[from] std::io::Error),
    #[error("{0}")]
    Msg(String),
}

pub type Result<T> = std::result::Result<T, Error>;

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Phase {
    Released,
    Capturing,
}

#[derive(Clone, Copy, Debug, PartialEq, Eq)]
pub enum Decision {
    Forward,
    Swallow,
    Release,
}

pub struct GrabFsm {
    pub phase: Phase,
    down: [bool; 256],
    chord: Vec<u8>,
    latched: bool,
}

impl GrabFsm {
    pub fn new(chord: Vec<u8>) -> Self {
        Self { phase: Phase::Released, down: [false; 256], chord, latched: false }
    }

    pub fn default_chord() -> Self {
        Self::new(vec![0xE0, 0xE2, 0x29])
    }

    pub fn arm(&mut self) {
        self.phase = Phase::Capturing;
        self.down = [false; 256];
        self.latched = false;
    }

    pub fn on_key(&mut self, usage: u8, down: bool) -> Decision {
        if self.phase != Phase::Capturing {
            return Decision::Swallow;
        }
        self.down[usage as usize] = down;
        if !down {
            if !self.chord.iter().all(|key| self.held(*key)) {
                self.latched = false;
            }
            return Decision::Forward;
        }
        let chord = self.chord.iter().all(|key| self.held(*key));
        if chord && !self.latched {
            self.latched = true;
            self.phase = Phase::Released;
            return Decision::Release;
        }
        Decision::Forward
    }

    /// Left and right modifiers satisfy the same chord token.
    fn held(&self, usage: u8) -> bool {
        let other = match usage {
            0xE0 => 0xE4,
            0xE4 => 0xE0,
            0xE1 => 0xE5,
            0xE5 => 0xE1,
            0xE2 => 0xE6,
            0xE6 => 0xE2,
            0xE3 => 0xE7,
            0xE7 => 0xE3,
            _ => return self.down[usage as usize],
        };
        self.down[usage as usize] || self.down[other as usize]
    }
}

fn hotkey_usage(name: &str) -> Option<u8> {
    match name.trim().to_ascii_lowercase().as_str() {
        "ctrl" | "control" => Some(0xE0),
        "shift" => Some(0xE1),
        "alt" => Some(0xE2),
        "super" | "meta" | "win" => Some(0xE3),
        "enter" | "return" => Some(0x28),
        "esc" | "escape" => Some(0x29),
        "backspace" => Some(0x2A),
        "tab" => Some(0x2B),
        "space" => Some(0x2C),
        "minus" => Some(0x2D),
        "equal" => Some(0x2E),
        "leftbracket" => Some(0x2F),
        "rightbracket" => Some(0x30),
        "backslash" => Some(0x31),
        "semicolon" => Some(0x33),
        "quote" => Some(0x34),
        "grave" => Some(0x35),
        "comma" => Some(0x36),
        "period" => Some(0x37),
        "slash" => Some(0x38),
        "insert" => Some(0x49),
        "home" => Some(0x4A),
        "pageup" => Some(0x4B),
        "delete" => Some(0x4C),
        "end" => Some(0x4D),
        "pagedown" => Some(0x4E),
        "right" => Some(0x4F),
        "left" => Some(0x50),
        "down" => Some(0x51),
        "up" => Some(0x52),
        "0" => Some(0x27),
        other => {
            let bytes = other.as_bytes();
            if bytes.len() == 1 && bytes[0].is_ascii_lowercase() {
                return Some(0x04 + (bytes[0] - b'a'));
            }
            if bytes.len() == 1 && bytes[0].is_ascii_digit() && bytes[0] != b'0' {
                return Some(0x1D + (bytes[0] - b'0'));
            }
            let rest = other.strip_prefix('f')?;
            let n: u8 = rest.parse().ok()?;
            match n {
                1..=12 => Some(0x39 + n),
                13..=24 => Some(0x68 + (n - 13)),
                _ => None,
            }
        }
    }
}

fn normalize_token(part: &str) -> Option<String> {
    let name = part.trim().to_ascii_lowercase();
    let canonical = match name.as_str() {
        "control" => "ctrl",
        "escape" => "esc",
        "meta" | "win" => "super",
        "return" => "enter",
        other => other,
    };
    hotkey_usage(canonical)?;
    Some(canonical.to_string())
}

/// Accept a chord only when every part is a known key and at least two keys remain.
pub fn normalize_hotkey(text: &str) -> Option<String> {
    let mut seen = Vec::new();
    let mut names = Vec::new();
    for part in text.split('+') {
        let part = part.trim();
        if part.is_empty() {
            continue;
        }
        let name = normalize_token(part)?;
        let usage = hotkey_usage(&name)?;
        if seen.contains(&usage) {
            continue;
        }
        seen.push(usage);
        names.push(name);
    }
    if names.len() < 2 {
        None
    } else {
        Some(names.join("+"))
    }
}

pub fn parse_hotkey(text: &str) -> Vec<u8> {
    let mut chord = Vec::new();
    for part in text.split('+') {
        let Some(usage) = hotkey_usage(part) else {
            continue;
        };
        if !chord.contains(&usage) {
            chord.push(usage);
        }
    }
    chord
}

#[derive(Clone, Debug)]
pub struct Endpoint {
    pub name: String,
    pub host: String,
    pub port: u16,
    pub identity: Option<[u8; 32]>,
}

#[derive(Clone, Debug, Deserialize, Eq, PartialEq, Serialize)]
pub struct KnownDevice {
    pub id: String,
    pub name: String,
    pub host: String,
    pub port: u16,
}

impl KnownDevice {
    fn from_endpoint(endpoint: &Endpoint) -> Self {
        let id = endpoint
            .identity
            .as_ref()
            .map(|identity| hex(identity))
            .unwrap_or_else(|| format!("{}:{}", endpoint.host, endpoint.port));
        Self { id, name: endpoint.name.clone(), host: endpoint.host.clone(), port: endpoint.port }
    }

    pub fn is_paired(&self, peers: &[[u8; 32]]) -> bool {
        parse_hex32(&self.id).is_ok_and(|identity| peers.contains(&identity))
    }
}

pub fn discover(wait: Duration) -> Vec<Endpoint> {
    let daemon = match mdns_sd::ServiceDaemon::new() {
        Ok(daemon) => daemon,
        Err(_) => return Vec::new(),
    };
    let receiver = match daemon.browse("_kmj._tcp.local.") {
        Ok(receiver) => receiver,
        Err(_) => return Vec::new(),
    };
    let deadline = std::time::Instant::now() + wait;
    let mut found = BTreeMap::new();
    while std::time::Instant::now() < deadline {
        if let Ok(event) = receiver.recv_timeout(Duration::from_millis(200)) {
            if let mdns_sd::ServiceEvent::ServiceResolved(info) = event {
                let addresses = info.get_addresses();
                let host = addresses
                    .iter()
                    .find(|address| address.is_ipv4())
                    .or_else(|| addresses.iter().next())
                    .map(|address| address.to_string())
                    .unwrap_or_default();
                let name = info.get_property_val_str("name").unwrap_or(info.get_fullname()).to_string();
                let identity = info.get_property_val_str("id").and_then(|value| parse_hex32(value).ok());
                if !host.is_empty() {
                    let key = identity
                        .as_ref()
                        .map(|value| format!("id:{}", hex(value)))
                        .unwrap_or_else(|| format!("service:{}", info.get_fullname()));
                    found.insert(key, Endpoint { name, host, port: info.get_port(), identity });
                }
            }
        }
    }
    let _ = daemon.shutdown();
    found.into_values().collect()
}

fn default_theme() -> String {
    "system".into()
}

#[derive(Serialize, Deserialize)]
struct FileStore {
    priv_hex: String,
    peers: Vec<String>,
    hotkey: String,
    #[serde(default)]
    devices: Vec<KnownDevice>,
    #[serde(default = "default_theme")]
    theme: String,
}

pub struct Store {
    path: PathBuf,
    pub identity: Identity,
    pub peers: Vec<[u8; 32]>,
    pub hotkey: String,
    pub devices: Vec<KnownDevice>,
    pub theme: String,
}

impl Store {
    pub fn load(path: PathBuf) -> Result<Self> {
        if !path.exists() {
            let identity = Identity::from_priv(random32());
            let store = Self {
                path,
                identity,
                peers: Vec::new(),
                hotkey: "ctrl+alt+esc".into(),
                devices: Vec::new(),
                theme: default_theme(),
            };
            store.save()?;
            return Ok(store);
        }
        let text = fs::read_to_string(&path)?;
        let file: FileStore = serde_json::from_str(&text).map_err(|err| Error::Msg(err.to_string()))?;
        let identity = Identity::from_priv(parse_hex32(&file.priv_hex)?);
        let peers = file.peers.iter().map(|p| parse_hex32(p)).collect::<Result<Vec<_>>>()?;
        let theme = match file.theme.as_str() {
            "light" | "dark" | "system" => file.theme,
            _ => default_theme(),
        };
        Ok(Self { path, identity, peers, hotkey: file.hotkey, devices: file.devices, theme })
    }

    pub fn save(&self) -> Result<()> {
        if let Some(parent) = self.path.parent() {
            fs::create_dir_all(parent)?;
        }
        let file = FileStore {
            priv_hex: hex(&self.identity.priv_key),
            peers: self.peers.iter().map(|p| hex(p)).collect(),
            hotkey: self.hotkey.clone(),
            devices: self.devices.clone(),
            theme: self.theme.clone(),
        };
        fs::write(&self.path, serde_json::to_string_pretty(&file).unwrap())?;
        Ok(())
    }

    pub fn add_peer(&mut self, pub_key: [u8; 32]) -> Result<()> {
        if !self.peers.iter().any(|p| p == &pub_key) {
            self.peers.push(pub_key);
            self.save()?;
        }
        Ok(())
    }

    pub fn remove_peer(&mut self, pub_key: [u8; 32]) -> Result<()> {
        let before = self.peers.len();
        self.peers.retain(|peer| peer != &pub_key);
        if self.peers.len() != before {
            self.save()?;
        }
        Ok(())
    }

    pub fn remove_peer_id(&mut self, id: &str) -> Result<()> {
        self.remove_peer(parse_hex32(id)?)
    }

    pub fn replace_devices(&mut self, endpoints: &[Endpoint]) -> Result<()> {
        let devices = endpoints.iter().map(KnownDevice::from_endpoint).collect::<Vec<_>>();
        if self.devices != devices {
            self.devices = devices;
            self.save()?;
        }
        Ok(())
    }
}

pub struct Pairing {
    stream: TcpStream,
    link: Link,
    pub code: String,
}

impl Pairing {
    pub fn confirm(self) -> Result<[u8; 32]> {
        self.confirm_with_timeout(Duration::from_secs(3))
    }

    pub fn confirm_with_timeout(mut self, timeout: Duration) -> Result<[u8; 32]> {
        self.link.confirm()?;
        pump(&mut self.stream, &mut self.link, timeout)?;
        if !self.link.paired() {
            let reason = if self.link.fail().is_empty() { "confirmation timed out" } else { self.link.fail() };
            return Err(Error::Msg(format!("pair failed: {reason}")));
        }
        Ok(*self.link.peer_pub())
    }
}

pub struct SecureLink {
    stream: TcpStream,
    link: Link,
}

impl SecureLink {
    pub fn send_key(&mut self, usage: u8, down: bool) -> Result<()> {
        self.send(&[0x02, usage, u8::from(down)])
    }
    pub fn send_mouse(&mut self, dx: i16, dy: i16) -> Result<()> {
        let mut msg = [0u8; 5];
        msg[0] = 0x03;
        msg[1..3].copy_from_slice(&dx.to_le_bytes());
        msg[3..5].copy_from_slice(&dy.to_le_bytes());
        self.send(&msg)
    }
    pub fn send_buttons(&mut self, mask: u8) -> Result<()> {
        self.send(&[0x04, mask])
    }
    pub fn send_wheel(&mut self, v: i8, h: i8) -> Result<()> {
        self.send(&[0x05, v as u8, h as u8])
    }
    pub fn send_release(&mut self) -> Result<()> {
        self.send(&[0x06])
    }
    pub fn send_ping(&mut self, millis: u32, rtt: u32) -> Result<()> {
        let mut msg = [0u8; 9];
        msg[0] = 0x07;
        msg[1..5].copy_from_slice(&millis.to_le_bytes());
        msg[5..9].copy_from_slice(&rtt.to_le_bytes());
        self.send(&msg)
    }
    pub fn poll(&mut self) -> Result<Vec<Vec<u8>>> {
        self.service()?;
        Ok(self.link.take_apps())
    }
    fn send(&mut self, msg: &[u8]) -> Result<()> {
        self.link.send_app(msg)?;
        flush_out(&mut self.stream, &mut self.link)?;
        Ok(())
    }
    fn service(&mut self) -> Result<()> {
        if !read_available(&mut self.stream, &mut self.link)? {
            return Err(Error::Msg("device closed the connection".into()));
        }
        if !self.link.fail().is_empty() {
            return Err(Error::Msg(self.link.fail().to_string()));
        }
        flush_out(&mut self.stream, &mut self.link)?;
        Ok(())
    }
}

pub fn probe_device(addr: &str) -> &'static str {
    match dial(addr, Duration::from_millis(800)) {
        Ok(stream) => {
            drop(stream);
            "ready"
        }
        Err(Error::Msg(msg)) if msg == EMULATOR_CLOSED => "idle",
        Err(_) => "offline",
    }
}

pub fn start_pairing(addr: &str, identity: &Identity, name: &str) -> Result<Pairing> {
    let mut stream = connect(addr)?;
    let mut link = Link::client(identity.clone());
    link.set_next_eph(random32());
    link.begin_client(1, name)?;
    pump(&mut stream, &mut link, Duration::from_secs(3))?;
    if !link.waiting_confirm() {
        return Err(Error::Msg(format!("no code: {}", link.fail())));
    }
    let code = link.pair_code().to_string();
    Ok(Pairing { stream, link, code })
}

pub fn start_session(addr: &str, identity: &Identity, name: &str) -> Result<SecureLink> {
    let mut stream = connect(addr)?;
    let mut link = Link::client(identity.clone());
    link.set_next_eph(random32());
    link.begin_client(2, name)?;
    pump(&mut stream, &mut link, Duration::from_secs(3))?;
    if !link.is_secure() {
        return Err(Error::Msg(format!("session failed: {}", link.fail())));
    }
    // The device drops a quiet session after three seconds. Keep reads short so
    // the capture loop can poll without waiting on the handshake timeout.
    stream.set_read_timeout(Some(Duration::from_millis(1)))?;
    Ok(SecureLink { stream, link })
}

pub fn apply_capture(link: &mut SecureLink, fsm: &mut GrabFsm, event: CaptureEvent) -> Result<bool> {
    match event {
        CaptureEvent::Key { usage, down } => match fsm.on_key(usage, down) {
            Decision::Forward => link.send_key(usage, down).map(|_| false),
            Decision::Swallow => Ok(false),
            Decision::Release => {
                link.send_release()?;
                Ok(true)
            }
        },
        CaptureEvent::Mouse { dx, dy } => {
            link.send_mouse(clamp_i16(dx), clamp_i16(dy))?;
            Ok(false)
        }
        CaptureEvent::Buttons { mask } => {
            link.send_buttons(mask)?;
            Ok(false)
        }
        CaptureEvent::Wheel { v, h } => {
            link.send_wheel(v, h)?;
            Ok(false)
        }
    }
}

fn clamp_i16(value: i32) -> i16 {
    value.clamp(i16::MIN as i32, i16::MAX as i32) as i16
}

fn connect(addr: &str) -> Result<TcpStream> {
    dial(addr, Duration::from_millis(1000))
}

fn dial(addr: &str, timeout: Duration) -> Result<TcpStream> {
    let mut targets = addr.to_socket_addrs()?;
    let addr = targets.next().ok_or_else(|| Error::Msg("bad address".into()))?;
    let stream = TcpStream::connect_timeout(&addr, timeout).map_err(classify_dial)?;
    stream.set_nodelay(true)?;
    stream.set_read_timeout(Some(Duration::from_millis(50)))?;
    stream.set_write_timeout(Some(Duration::from_secs(2)))?;
    Ok(stream)
}

fn classify_dial(err: std::io::Error) -> Error {
    if is_refused(&err) {
        Error::Msg(EMULATOR_CLOSED.into())
    } else if is_offline(&err) {
        Error::Msg(DEVICE_UNREACHABLE.into())
    } else {
        Error::Io(err)
    }
}

fn is_refused(err: &std::io::Error) -> bool {
    err.kind() == std::io::ErrorKind::ConnectionRefused || err.raw_os_error() == Some(111)
}

fn is_offline(err: &std::io::Error) -> bool {
    err.kind() == std::io::ErrorKind::TimedOut
        || err.kind() == std::io::ErrorKind::AddrNotAvailable
        || matches!(err.raw_os_error(), Some(110) | Some(101) | Some(113))
}

fn pump(stream: &mut TcpStream, link: &mut Link, budget: Duration) -> Result<()> {
    let start = std::time::Instant::now();
    flush_out(stream, link)?;
    while start.elapsed() < budget {
        let open = read_available(stream, link)?;
        flush_out(stream, link)?;
        if !open || !link.fail().is_empty() || link.waiting_confirm() || link.is_secure() || link.paired() {
            break;
        }
        std::thread::sleep(Duration::from_millis(10));
    }
    Ok(())
}

fn flush_out(stream: &mut TcpStream, link: &mut Link) -> Result<()> {
    let bytes = link.take_out();
    if !bytes.is_empty() {
        stream.write_all(&bytes)?;
    }
    Ok(())
}

fn read_available(stream: &mut TcpStream, link: &mut Link) -> Result<bool> {
    let mut buf = [0u8; 512];
    loop {
        match stream.read(&mut buf) {
            Ok(0) => return Ok(false),
            Ok(n) => link.feed(&buf[..n])?,
            Err(err) if err.kind() == std::io::ErrorKind::WouldBlock || err.kind() == std::io::ErrorKind::TimedOut => {
                return Ok(true);
            }
            Err(err) => return Err(err.into()),
        }
    }
}

fn random32() -> [u8; 32] {
    let mut out = [0u8; 32];
    getrandom_fill(&mut out);
    out
}

fn getrandom_fill(out: &mut [u8]) {
    if let Ok(mut file) = fs::File::open("/dev/urandom") {
        let _ = file.read_exact(out);
        return;
    }
    for (i, byte) in out.iter_mut().enumerate() {
        *byte = (i as u8).wrapping_mul(17).wrapping_add(3);
    }
}

fn parse_hex32(text: &str) -> Result<[u8; 32]> {
    if text.len() != 64 {
        return Err(Error::Msg("expected 32-byte hex".into()));
    }
    let mut out = [0u8; 32];
    for i in 0..32 {
        out[i] = u8::from_str_radix(&text[i * 2..i * 2 + 2], 16).map_err(|err| Error::Msg(err.to_string()))?;
    }
    Ok(out)
}

fn hex(bytes: &[u8]) -> String {
    bytes.iter().map(|b| format!("{b:02x}")).collect()
}

/// Test helper: a device-side TCP listener using the same protocol.
pub fn serve_one(listener: TcpListener, identity: Identity, armed: bool, peers: Vec<[u8; 32]>, eph: [u8; 32]) {
    let (mut stream, _) = listener.accept().expect("accept");
    stream.set_nodelay(true).ok();
    stream.set_read_timeout(Some(Duration::from_millis(50))).ok();
    let mut link = Link::device(identity, "KMJ");
    link.arm(armed);
    for peer in peers {
        link.allow_peer(peer);
    }
    link.set_next_eph(eph);
    let start = std::time::Instant::now();
    while start.elapsed() < Duration::from_secs(2) {
        let _ = read_available(&mut stream, &mut link);
        let _ = flush_out(&mut stream, &mut link);
        if link.waiting_confirm() {
            link.confirm().ok();
            let _ = flush_out(&mut stream, &mut link);
        }
        if link.paired() || link.is_secure() {
            if link.is_secure() {
                let linger = std::time::Instant::now();
                while linger.elapsed() < Duration::from_millis(400) {
                    let _ = read_available(&mut stream, &mut link);
                    let _ = flush_out(&mut stream, &mut link);
                    std::thread::sleep(Duration::from_millis(10));
                }
            }
            break;
        }
        std::thread::sleep(Duration::from_millis(5));
    }
}

#[allow(dead_code)]
fn _listener_type(_: TcpListener) {}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn release_chord_is_local() {
        let mut fsm = GrabFsm::default_chord();
        fsm.arm();
        assert_eq!(fsm.on_key(0xE0, true), Decision::Forward);
        assert_eq!(fsm.on_key(0xE2, true), Decision::Forward);
        assert_eq!(fsm.on_key(0x29, true), Decision::Release);
        assert_eq!(fsm.phase, Phase::Released);
        assert_eq!(parse_hotkey("Ctrl+Alt+Esc"), vec![0xE0, 0xE2, 0x29]);
    }

    #[test]
    fn parses_recorded_chords() {
        assert_eq!(parse_hotkey("ctrl+alt+f1"), vec![0xE0, 0xE2, 0x3A]);
        assert_eq!(parse_hotkey("f12"), vec![0x45]);
        assert_eq!(parse_hotkey("f13"), vec![0x68]);
        assert_eq!(parse_hotkey("shift+a"), vec![0xE1, 0x04]);
        assert_eq!(parse_hotkey("ctrl+1"), vec![0xE0, 0x1E]);
        assert_eq!(normalize_hotkey("Ctrl+Alt+F1").as_deref(), Some("ctrl+alt+f1"));
        assert_eq!(normalize_hotkey("control+escape").as_deref(), Some("ctrl+esc"));
        assert_eq!(normalize_hotkey("escape"), None);
        assert_eq!(normalize_hotkey("ctrl+nope"), None);
    }

    #[test]
    fn either_modifier_side_releases() {
        let mut fsm = GrabFsm::new(parse_hotkey("ctrl+alt+f1"));
        fsm.arm();
        assert_eq!(fsm.on_key(0xE4, true), Decision::Forward);
        assert_eq!(fsm.on_key(0xE6, true), Decision::Forward);
        assert_eq!(fsm.on_key(0x3A, true), Decision::Release);
        assert_eq!(fsm.phase, Phase::Released);
    }

    #[test]
    fn loopback_pair_and_key() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let device = Identity::from_priv([7u8; 32]);
        let client = Identity::from_priv([9u8; 32]);
        let client_pub = client.pub_key;
        let handle = std::thread::spawn(move || serve_one(listener, device, true, Vec::new(), [3u8; 32]));
        let pairing = start_pairing(&format!("127.0.0.1:{port}"), &client, "desk").unwrap();
        assert_eq!(pairing.code.len(), 6);
        let peer = pairing.confirm().unwrap();
        handle.join().unwrap();
        assert_ne!(peer, [0u8; 32]);

        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let device = Identity::from_priv([7u8; 32]);
        let handle = std::thread::spawn(move || serve_one(listener, device, false, vec![client_pub], [4u8; 32]));
        let mut link = start_session(&format!("127.0.0.1:{port}"), &client, "desk").unwrap();
        link.send_key(0x04, true).unwrap();
        link.send_release().unwrap();
        handle.join().unwrap();
    }

    #[test]
    fn closed_port_means_the_emulator_is_not_open() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        drop(listener);
        let addr = format!("127.0.0.1:{port}");
        assert_eq!(probe_device(&addr), "idle");
        match start_session(&addr, &Identity::from_priv([9u8; 32]), "desk") {
            Err(err) => assert_eq!(err.to_string(), EMULATOR_CLOSED),
            Ok(_) => panic!("closed port opened a session"),
        }
    }

    #[test]
    fn open_port_is_ready_for_capture() {
        let listener = TcpListener::bind("127.0.0.1:0").unwrap();
        let port = listener.local_addr().unwrap().port();
        let handle = std::thread::spawn(move || {
            let _ = listener.accept();
        });
        assert_eq!(probe_device(&format!("127.0.0.1:{port}")), "ready");
        handle.join().unwrap();
    }

    #[test]
    fn store_roundtrip() {
        let path = std::env::temp_dir().join(format!("kmj-test-{}-{}.json", std::process::id(), random32()[0]));
        let mut store = Store::load(path.clone()).unwrap();
        store.add_peer([1u8; 32]).unwrap();
        store.add_peer([2u8; 32]).unwrap();
        store.add_peer([1u8; 32]).unwrap();
        store
            .replace_devices(&[Endpoint {
                name: "Desk board".into(),
                host: "192.0.2.8".into(),
                port: 42424,
                identity: Some([1u8; 32]),
            }])
            .unwrap();
        let loaded = Store::load(path.clone()).unwrap();
        assert_eq!(loaded.peers, vec![[1u8; 32], [2u8; 32]]);
        assert_eq!(loaded.identity.pub_key, store.identity.pub_key);
        assert_eq!(loaded.devices.len(), 1);
        assert_eq!(loaded.devices[0].name, "Desk board");
        assert!(loaded.devices[0].is_paired(&loaded.peers));
        assert_eq!(loaded.theme, "system");

        let mut loaded = loaded;
        loaded.remove_peer([1u8; 32]).unwrap();
        loaded.theme = "light".into();
        loaded.save().unwrap();
        let loaded = Store::load(path.clone()).unwrap();
        assert_eq!(loaded.peers, vec![[2u8; 32]]);
        assert!(!loaded.devices[0].is_paired(&loaded.peers));
        assert_eq!(loaded.theme, "light");

        let mut loaded = loaded;
        loaded.replace_devices(&[]).unwrap();
        assert!(Store::load(path).unwrap().devices.is_empty());
    }
}
