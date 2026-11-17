//! Desktop session: discovery, pairing, the encrypted link, and the capture state machine.

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
            if !self.chord.iter().all(|key| self.down[*key as usize]) {
                self.latched = false;
            }
            return Decision::Forward;
        }
        let chord = self.chord.iter().all(|key| self.down[*key as usize]);
        if chord && !self.latched {
            self.latched = true;
            self.phase = Phase::Released;
            return Decision::Release;
        }
        Decision::Forward
    }
}

pub fn parse_hotkey(text: &str) -> Vec<u8> {
    text.split('+')
        .filter_map(|part| match part.trim().to_ascii_lowercase().as_str() {
            "ctrl" | "control" => Some(0xE0),
            "alt" => Some(0xE2),
            "shift" => Some(0xE1),
            "super" | "meta" | "win" => Some(0xE3),
            "esc" | "escape" => Some(0x29),
            other if other.len() == 1 => {
                let c = other.as_bytes()[0];
                if c.is_ascii_lowercase() { Some(0x04 + (c - b'a')) } else { None }
            }
            _ => None,
        })
        .collect()
}

#[derive(Clone, Debug)]
pub struct Endpoint {
    pub name: String,
    pub host: String,
    pub port: u16,
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
    let mut found = Vec::new();
    while std::time::Instant::now() < deadline {
        if let Ok(event) = receiver.recv_timeout(Duration::from_millis(200)) {
            if let mdns_sd::ServiceEvent::ServiceResolved(info) = event {
                let host = info.get_addresses().iter().next().map(|a| a.to_string()).unwrap_or_default();
                let name = info.get_property_val_str("name").unwrap_or(info.get_fullname()).to_string();
                if !host.is_empty() {
                    found.push(Endpoint { name, host, port: info.get_port() });
                }
            }
        }
    }
    let _ = daemon.shutdown();
    found
}

#[derive(Serialize, Deserialize)]
struct FileStore {
    priv_hex: String,
    peers: Vec<String>,
    hotkey: String,
}

pub struct Store {
    path: PathBuf,
    pub identity: Identity,
    pub peers: Vec<[u8; 32]>,
    pub hotkey: String,
}

impl Store {
    pub fn load(path: PathBuf) -> Result<Self> {
        if !path.exists() {
            let identity = Identity::from_priv(random32());
            let store = Self { path, identity, peers: Vec::new(), hotkey: "ctrl+alt+esc".into() };
            store.save()?;
            return Ok(store);
        }
        let text = fs::read_to_string(&path)?;
        let file: FileStore = serde_json::from_str(&text).map_err(|err| Error::Msg(err.to_string()))?;
        let identity = Identity::from_priv(parse_hex32(&file.priv_hex)?);
        let peers = file.peers.iter().map(|p| parse_hex32(p)).collect::<Result<Vec<_>>>()?;
        Ok(Self { path, identity, peers, hotkey: file.hotkey })
    }

    pub fn save(&self) -> Result<()> {
        if let Some(parent) = self.path.parent() {
            fs::create_dir_all(parent)?;
        }
        let file = FileStore {
            priv_hex: hex(&self.identity.priv_key),
            peers: self.peers.iter().map(|p| hex(p)).collect(),
            hotkey: self.hotkey.clone(),
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
}

pub struct Pairing {
    stream: TcpStream,
    link: Link,
    pub code: String,
}

impl Pairing {
    pub fn confirm(mut self) -> Result<[u8; 32]> {
        self.link.confirm()?;
        pump(&mut self.stream, &mut self.link, Duration::from_secs(3))?;
        if !self.link.paired() {
            return Err(Error::Msg(format!("pair failed: {}", self.link.fail())));
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
        read_available(&mut self.stream, &mut self.link)?;
        flush_out(&mut self.stream, &mut self.link)?;
        Ok(self.link.take_apps())
    }
    fn send(&mut self, msg: &[u8]) -> Result<()> {
        self.link.send_app(msg)?;
        flush_out(&mut self.stream, &mut self.link)?;
        Ok(())
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
            link.send_mouse(dx as i16, dy as i16)?;
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

fn connect(addr: &str) -> Result<TcpStream> {
    let mut targets = addr.to_socket_addrs()?;
    let addr = targets.next().ok_or_else(|| Error::Msg("bad address".into()))?;
    let stream = TcpStream::connect_timeout(&addr, Duration::from_secs(3))?;
    stream.set_nodelay(true)?;
    stream.set_read_timeout(Some(Duration::from_millis(50)))?;
    stream.set_write_timeout(Some(Duration::from_secs(2)))?;
    Ok(stream)
}

fn pump(stream: &mut TcpStream, link: &mut Link, budget: Duration) -> Result<()> {
    let start = std::time::Instant::now();
    flush_out(stream, link)?;
    while start.elapsed() < budget {
        read_available(stream, link)?;
        flush_out(stream, link)?;
        if link.waiting_confirm() || link.is_secure() || link.paired() || !link.fail().is_empty() {
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

fn read_available(stream: &mut TcpStream, link: &mut Link) -> Result<()> {
    let mut buf = [0u8; 512];
    loop {
        match stream.read(&mut buf) {
            Ok(0) => break,
            Ok(n) => {
                let _ = link.feed(&buf[..n]);
            }
            Err(err) if err.kind() == std::io::ErrorKind::WouldBlock || err.kind() == std::io::ErrorKind::TimedOut => {
                break
            }
            Err(err) => return Err(err.into()),
        }
    }
    Ok(())
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
    fn store_roundtrip() {
        let path = std::env::temp_dir().join(format!("kmj-test-{}-{}.json", std::process::id(), random32()[0]));
        let mut store = Store::load(path.clone()).unwrap();
        store.add_peer([1u8; 32]).unwrap();
        let loaded = Store::load(path).unwrap();
        assert_eq!(loaded.peers, vec![[1u8; 32]]);
        assert_eq!(loaded.identity.pub_key, store.identity.pub_key);
    }
}
