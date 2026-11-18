//! KMJ handshake and transport. Byte layout is `proto/PROTOCOL.md`.

use blake2::digest::consts::U32;
use blake2::digest::Digest;
use blake2::Blake2b;
use chacha20poly1305::aead::{Aead, KeyInit, Payload};
use chacha20poly1305::{XChaCha20Poly1305, XNonce};
use x25519_dalek::{PublicKey, StaticSecret};

pub const CLIENT_HELLO: u8 = 0x10;
pub const SERVER_HELLO: u8 = 0x11;
pub const PAIR_EPH: u8 = 0x20;
pub const PAIR_EPH_REPLY: u8 = 0x21;
pub const PAIR_ACCEPT: u8 = 0x23;
pub const PAIR_ACCEPT_REPLY: u8 = 0x24;
pub const PAIR_REJECT: u8 = 0x25;
pub const SESS_EPH: u8 = 0x30;
pub const SESS_EPH_REPLY: u8 = 0x31;
pub const SESS_CLIENT_MAC: u8 = 0x32;
pub const SESS_SERVER_MAC: u8 = 0x33;
pub const ENCRYPTED: u8 = 0x7E;

pub const MSG_HELLO: u8 = 0x01;
pub const MSG_KEY: u8 = 0x02;
pub const MSG_MOUSE_MOVE: u8 = 0x03;
pub const MSG_MOUSE_BUTTONS: u8 = 0x04;
pub const MSG_WHEEL: u8 = 0x05;
pub const MSG_RELEASE_ALL: u8 = 0x06;
pub const MSG_PING: u8 = 0x07;
pub const MSG_PONG: u8 = 0x08;
pub const MSG_STATUS: u8 = 0x09;

const PAIR_LABEL: &[u8] = b"KMJ-PAIR-v1";
const SESS_LABEL: &[u8] = b"KMJ-SESS-v1";

#[derive(Debug, thiserror::Error)]
pub enum Error {
    #[error("{0}")]
    Proto(&'static str),
    #[error("decrypt")]
    Decrypt,
}

pub type Result<T> = std::result::Result<T, Error>;

pub fn hash32(parts: &[&[u8]]) -> [u8; 32] {
    let mut hasher = Blake2b::<U32>::new();
    for part in parts {
        hasher.update(part);
    }
    hasher.finalize().into()
}

pub fn x25519_public(priv_key: &[u8; 32]) -> [u8; 32] {
    PublicKey::from(&StaticSecret::from(*priv_key)).to_bytes()
}

pub fn x25519_shared(priv_key: &[u8; 32], peer: &[u8; 32]) -> [u8; 32] {
    StaticSecret::from(*priv_key)
        .diffie_hellman(&PublicKey::from(*peer))
        .to_bytes()
}

pub fn nonce_from_counter(counter: u64) -> [u8; 24] {
    let mut nonce = [0u8; 24];
    nonce[16..].copy_from_slice(&counter.to_le_bytes());
    nonce
}

pub fn aead_seal(key: &[u8; 32], nonce: &[u8; 24], plain: &[u8]) -> Result<(Vec<u8>, [u8; 16])> {
    let cipher = XChaCha20Poly1305::new(key.into());
    let out = cipher
        .encrypt(
            XNonce::from_slice(nonce),
            Payload { msg: plain, aad: b"" },
        )
        .map_err(|_| Error::Decrypt)?;
    let (body, tag) = out.split_at(out.len() - 16);
    let mut mac = [0u8; 16];
    mac.copy_from_slice(tag);
    Ok((body.to_vec(), mac))
}

pub fn aead_open(key: &[u8; 32], nonce: &[u8; 24], cipher: &[u8], mac: &[u8; 16]) -> Result<Vec<u8>> {
    let aead = XChaCha20Poly1305::new(key.into());
    let mut joined = Vec::with_capacity(cipher.len() + 16);
    joined.extend_from_slice(cipher);
    joined.extend_from_slice(mac);
    aead.decrypt(XNonce::from_slice(nonce), Payload { msg: &joined, aad: b"" })
        .map_err(|_| Error::Decrypt)
}

#[derive(Clone)]
pub struct Identity {
    pub priv_key: [u8; 32],
    pub pub_key: [u8; 32],
}

impl Identity {
    pub fn from_priv(priv_key: [u8; 32]) -> Self {
        let pub_key = x25519_public(&priv_key);
        Self { priv_key, pub_key }
    }
}

#[derive(Clone)]
pub struct SessionKeys {
    pub c2d: [u8; 32],
    pub d2c: [u8; 32],
    pub mac_key: [u8; 32],
}

pub struct PairTranscript {
    pub client_static: [u8; 32],
    pub device_static: [u8; 32],
    pub client_eph: [u8; 32],
    pub device_eph: [u8; 32],
    pub shared: [u8; 32],
}

pub fn pair_code(t: &PairTranscript) -> String {
    let material = hash32(&[
        &t.shared,
        PAIR_LABEL,
        &t.client_static,
        &t.device_static,
        &t.client_eph,
        &t.device_eph,
    ]);
    let value = ((material[0] as u32) << 16) | ((material[1] as u32) << 8) | material[2] as u32;
    format!("{:06}", value % 1_000_000)
}

pub fn pair_mac(t: &PairTranscript, role: u8) -> [u8; 16] {
    let full = hash32(&[
        &t.shared,
        &[role],
        PAIR_LABEL,
        &t.client_static,
        &t.device_static,
        &t.client_eph,
        &t.device_eph,
    ]);
    let mut mac = [0u8; 16];
    mac.copy_from_slice(&full[..16]);
    mac
}

pub fn session_keys_client(
    client: &Identity,
    device_static: &[u8; 32],
    client_eph_priv: &[u8; 32],
    device_eph: &[u8; 32],
) -> SessionKeys {
    let client_eph = x25519_public(client_eph_priv);
    let ss_es = x25519_shared(client_eph_priv, device_static);
    let ss_ee = x25519_shared(client_eph_priv, device_eph);
    let ss_se = x25519_shared(&client.priv_key, device_eph);
    derive_keys(&client.pub_key, device_static, &client_eph, &ss_es, &ss_ee, &ss_se)
}

pub fn session_keys_device(
    device: &Identity,
    client_static: &[u8; 32],
    client_eph: &[u8; 32],
    device_eph_priv: &[u8; 32],
) -> SessionKeys {
    let _device_eph = x25519_public(device_eph_priv);
    let ss_es = x25519_shared(&device.priv_key, client_eph);
    let ss_ee = x25519_shared(device_eph_priv, client_eph);
    let ss_se = x25519_shared(device_eph_priv, client_static);
    derive_keys(client_static, &device.pub_key, client_eph, &ss_es, &ss_ee, &ss_se)
}

fn derive_keys(
    client_static: &[u8; 32],
    device_static: &[u8; 32],
    client_eph: &[u8; 32],
    ss_es: &[u8; 32],
    ss_ee: &[u8; 32],
    ss_se: &[u8; 32],
) -> SessionKeys {
    let _ = client_eph;
    let ck0 = hash32(&[SESS_LABEL, client_static, device_static]);
    let ck1 = hash32(&[&ck0, ss_es]);
    let ck2 = hash32(&[&ck1, ss_ee]);
    let ck3 = hash32(&[&ck2, ss_se]);
    SessionKeys {
        c2d: hash32(&[b"c2d", &ck3]),
        d2c: hash32(&[b"d2c", &ck3]),
        mac_key: hash32(&[b"mac", &ck3]),
    }
}

pub fn session_mac(
    keys: &SessionKeys,
    client_static: &[u8; 32],
    device_static: &[u8; 32],
    client_eph: &[u8; 32],
    device_eph: &[u8; 32],
    role: u8,
) -> [u8; 16] {
    let full = hash32(&[
        &keys.mac_key,
        &[role],
        client_static,
        device_static,
        client_eph,
        device_eph,
    ]);
    let mut mac = [0u8; 16];
    mac.copy_from_slice(&full[..16]);
    mac
}

pub fn encode_frame(out: &mut Vec<u8>, kind: u8, body: &[u8]) {
    out.push(kind);
    let len = body.len() as u16;
    out.extend_from_slice(&len.to_le_bytes());
    out.extend_from_slice(body);
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
enum Stage {
    WaitHello,
    WaitServer,
    WaitPairEph,
    WaitPairAccept,
    WaitSessEph,
    WaitSessMac,
    Secure,
    Closed,
}

#[derive(Clone, Copy, PartialEq, Eq, Debug)]
pub enum Role {
    Device,
    Client,
}

pub struct Link {
    role: Role,
    identity: Identity,
    name: String,
    armed: bool,
    known: Vec<[u8; 32]>,
    eph_priv: Option<[u8; 32]>,
    next_eph: Option<[u8; 32]>,
    peer_pub: [u8; 32],
    peer_name: String,
    client_eph: [u8; 32],
    device_eph: [u8; 32],
    code: String,
    wait_confirm: bool,
    local_confirmed: bool,
    peer_accepted: bool,
    paired: bool,
    stage: Stage,
    keys: Option<SessionKeys>,
    tx_n: u64,
    rx_n: u64,
    inbound: Vec<u8>,
    outbound: Vec<u8>,
    fail: String,
    intent: u8,
    apps: Vec<Vec<u8>>,
}

impl Link {
    pub fn device(identity: Identity, name: &str) -> Self {
        let mut link = Self::blank(Role::Device, identity);
        link.name = name.to_string();
        link.stage = Stage::WaitHello;
        link
    }

    pub fn client(identity: Identity) -> Self {
        Self::blank(Role::Client, identity)
    }

    fn blank(role: Role, identity: Identity) -> Self {
        Self {
            role,
            identity,
            name: String::new(),
            armed: false,
            known: Vec::new(),
            eph_priv: None,
            next_eph: None,
            peer_pub: [0; 32],
            peer_name: String::new(),
            client_eph: [0; 32],
            device_eph: [0; 32],
            code: String::new(),
            wait_confirm: false,
            local_confirmed: false,
            peer_accepted: false,
            paired: false,
            stage: Stage::Closed,
            keys: None,
            tx_n: 0,
            rx_n: 0,
            inbound: Vec::new(),
            outbound: Vec::new(),
            fail: String::new(),
            intent: 0,
            apps: Vec::new(),
        }
    }

    pub fn set_next_eph(&mut self, key: [u8; 32]) {
        self.next_eph = Some(key);
    }

    pub fn arm(&mut self, armed: bool) {
        self.armed = armed;
    }

    pub fn allow_peer(&mut self, pub_key: [u8; 32]) {
        self.known.push(pub_key);
    }

    pub fn pair_code(&self) -> &str {
        &self.code
    }

    pub fn waiting_confirm(&self) -> bool {
        self.wait_confirm
    }

    pub fn is_secure(&self) -> bool {
        self.stage == Stage::Secure
    }

    pub fn paired(&self) -> bool {
        self.paired
    }

    pub fn peer_pub(&self) -> &[u8; 32] {
        &self.peer_pub
    }

    pub fn fail(&self) -> &str {
        &self.fail
    }

    pub fn begin_client(&mut self, intent: u8, name: &str) -> Result<()> {
        if self.role != Role::Client {
            return Err(Error::Proto("not client"));
        }
        self.intent = intent;
        self.name = name.to_string();
        let mut body = vec![1, intent];
        body.extend_from_slice(&self.identity.pub_key);
        let nbytes = name.as_bytes();
        let n = nbytes.len().min(31);
        body.push(n as u8);
        body.extend_from_slice(&nbytes[..n]);
        self.stage = Stage::WaitServer;
        encode_frame(&mut self.outbound, CLIENT_HELLO, &body);
        Ok(())
    }

    pub fn confirm(&mut self) -> Result<()> {
        if !self.wait_confirm {
            return Ok(());
        }
        self.local_confirmed = true;
        self.wait_confirm = false;
        if self.role == Role::Client {
            self.send_pair_mac(b'C', PAIR_ACCEPT)?;
            return Ok(());
        }
        if self.peer_accepted {
            self.send_pair_mac(b'D', PAIR_ACCEPT_REPLY)?;
            self.paired = true;
            self.armed = false;
            self.stage = Stage::Closed;
        }
        Ok(())
    }

    pub fn reject(&mut self) {
        encode_frame(&mut self.outbound, PAIR_REJECT, &[]);
        self.mark_fail("rejected");
    }

    pub fn take_out(&mut self) -> Vec<u8> {
        std::mem::take(&mut self.outbound)
    }

    pub fn feed(&mut self, data: &[u8]) -> Result<()> {
        if self.stage == Stage::Closed || data.is_empty() {
            return Ok(());
        }
        self.inbound.extend_from_slice(data);
        while self.stage != Stage::Closed {
            if self.inbound.len() < 3 {
                break;
            }
            let len = u16::from_le_bytes([self.inbound[1], self.inbound[2]]) as usize;
            if len > 700 {
                self.mark_fail("frame too large");
                return Err(Error::Proto("frame too large"));
            }
            if self.inbound.len() < 3 + len {
                break;
            }
            let kind = self.inbound[0];
            let body = self.inbound[3..3 + len].to_vec();
            self.inbound.drain(..3 + len);
            self.handle(kind, &body)?;
        }
        Ok(())
    }

    pub fn send_app(&mut self, msg: &[u8]) -> Result<()> {
        let keys = self.keys.as_ref().ok_or(Error::Proto("no keys"))?;
        if self.stage != Stage::Secure {
            return Err(Error::Proto("not secure"));
        }
        let key = if self.role == Role::Client { keys.c2d } else { keys.d2c };
        let nonce = nonce_from_counter(self.tx_n);
        let (cipher, mac) = aead_seal(&key, &nonce, msg)?;
        self.tx_n += 1;
        let mut body = cipher;
        body.extend_from_slice(&mac);
        encode_frame(&mut self.outbound, ENCRYPTED, &body);
        Ok(())
    }

    pub fn recv_app(&mut self) -> Result<Vec<Vec<u8>>> {
        Ok(Vec::new())
    }

    fn mark_fail(&mut self, reason: &'static str) {
        if self.stage != Stage::Closed {
            self.fail = reason.to_string();
            self.stage = Stage::Closed;
            self.wait_confirm = false;
        }
    }

    fn mint_eph(&mut self) -> Result<[u8; 32]> {
        if let Some(key) = self.eph_priv {
            return Ok(key);
        }
        let key = self.next_eph.take().ok_or(Error::Proto("no eph"))?;
        self.eph_priv = Some(key);
        Ok(key)
    }

    fn send_eph(&mut self, kind: u8) -> Result<[u8; 32]> {
        let priv_key = self.mint_eph()?;
        let pub_key = x25519_public(&priv_key);
        if self.role == Role::Client {
            self.client_eph = pub_key;
        } else {
            self.device_eph = pub_key;
        }
        encode_frame(&mut self.outbound, kind, &pub_key);
        Ok(pub_key)
    }

    fn transcript(&self) -> Result<PairTranscript> {
        let eph_priv = self.eph_priv.ok_or(Error::Proto("no eph"))?;
        let peer_eph = if self.role == Role::Client { self.device_eph } else { self.client_eph };
        let (client_static, device_static) = if self.role == Role::Client {
            (self.identity.pub_key, self.peer_pub)
        } else {
            (self.peer_pub, self.identity.pub_key)
        };
        Ok(PairTranscript {
            client_static,
            device_static,
            client_eph: self.client_eph,
            device_eph: self.device_eph,
            shared: x25519_shared(&eph_priv, &peer_eph),
        })
    }

    fn send_pair_mac(&mut self, role: u8, kind: u8) -> Result<()> {
        let t = self.transcript()?;
        let mac = pair_mac(&t, role);
        encode_frame(&mut self.outbound, kind, &mac);
        Ok(())
    }

    fn derive(&mut self) -> Result<()> {
        let eph_priv = self.eph_priv.ok_or(Error::Proto("no eph"))?;
        let keys = if self.role == Role::Client {
            session_keys_client(&self.identity, &self.peer_pub, &eph_priv, &self.device_eph)
        } else {
            session_keys_device(&self.identity, &self.peer_pub, &self.client_eph, &eph_priv)
        };
        self.keys = Some(keys);
        Ok(())
    }

    fn handle(&mut self, kind: u8, body: &[u8]) -> Result<()> {
        match (self.role, self.stage, kind) {
            (Role::Device, Stage::WaitHello, CLIENT_HELLO) => self.on_client_hello(body),
            (Role::Client, Stage::WaitServer, SERVER_HELLO) => self.on_server_hello(body),
            (Role::Device, Stage::WaitPairEph, PAIR_EPH) if body.len() == 32 => {
                self.client_eph.copy_from_slice(body);
                self.send_eph(PAIR_EPH_REPLY)?;
                let t = self.transcript()?;
                self.code = pair_code(&t);
                self.wait_confirm = true;
                self.stage = Stage::WaitPairAccept;
                Ok(())
            }
            (Role::Client, Stage::WaitPairEph, PAIR_EPH_REPLY) if body.len() == 32 => {
                self.device_eph.copy_from_slice(body);
                let t = self.transcript()?;
                self.code = pair_code(&t);
                self.wait_confirm = true;
                self.stage = Stage::WaitPairAccept;
                Ok(())
            }
            (Role::Device, Stage::WaitPairAccept, PAIR_ACCEPT) if body.len() == 16 => {
                let t = self.transcript()?;
                let expect = pair_mac(&t, b'C');
                if expect.as_slice() != body {
                    self.mark_fail("pair mac");
                    return Err(Error::Proto("pair mac"));
                }
                self.peer_accepted = true;
                if self.local_confirmed {
                    self.send_pair_mac(b'D', PAIR_ACCEPT_REPLY)?;
                    self.paired = true;
                    self.stage = Stage::Closed;
                }
                Ok(())
            }
            (Role::Client, Stage::WaitPairAccept, PAIR_ACCEPT_REPLY) if body.len() == 16 => {
                let t = self.transcript()?;
                let expect = pair_mac(&t, b'D');
                if expect.as_slice() != body {
                    self.mark_fail("pair mac");
                    return Err(Error::Proto("pair mac"));
                }
                self.paired = true;
                self.stage = Stage::Closed;
                Ok(())
            }
            (_, _, PAIR_REJECT) => {
                self.mark_fail("rejected");
                Err(Error::Proto("rejected"))
            }
            (Role::Device, Stage::WaitSessEph, SESS_EPH) if body.len() == 32 => {
                self.client_eph.copy_from_slice(body);
                self.send_eph(SESS_EPH_REPLY)?;
                self.derive()?;
                self.stage = Stage::WaitSessMac;
                Ok(())
            }
            (Role::Client, Stage::WaitSessEph, SESS_EPH_REPLY) if body.len() == 32 => {
                self.device_eph.copy_from_slice(body);
                self.derive()?;
                let keys = self.keys.clone().unwrap();
                let mac = session_mac(&keys, &self.identity.pub_key, &self.peer_pub, &self.client_eph, &self.device_eph, b'C');
                encode_frame(&mut self.outbound, SESS_CLIENT_MAC, &mac);
                self.stage = Stage::WaitSessMac;
                Ok(())
            }
            (Role::Device, Stage::WaitSessMac, SESS_CLIENT_MAC) if body.len() == 16 => {
                let keys = self.keys.clone().unwrap();
                let expect = session_mac(&keys, &self.peer_pub, &self.identity.pub_key, &self.client_eph, &self.device_eph, b'C');
                if expect.as_slice() != body {
                    self.mark_fail("session mac");
                    return Err(Error::Proto("session mac"));
                }
                let mac = session_mac(&keys, &self.peer_pub, &self.identity.pub_key, &self.client_eph, &self.device_eph, b'D');
                encode_frame(&mut self.outbound, SESS_SERVER_MAC, &mac);
                self.stage = Stage::Secure;
                self.tx_n = 0;
                self.rx_n = 0;
                Ok(())
            }
            (Role::Client, Stage::WaitSessMac, SESS_SERVER_MAC) if body.len() == 16 => {
                let keys = self.keys.clone().unwrap();
                let expect = session_mac(&keys, &self.identity.pub_key, &self.peer_pub, &self.client_eph, &self.device_eph, b'D');
                if expect.as_slice() != body {
                    self.mark_fail("session mac");
                    return Err(Error::Proto("session mac"));
                }
                self.stage = Stage::Secure;
                self.tx_n = 0;
                self.rx_n = 0;
                Ok(())
            }
            (_, Stage::Secure, ENCRYPTED) if body.len() >= 16 => {
                let keys = self.keys.clone().unwrap();
                let key = if self.role == Role::Client { keys.d2c } else { keys.c2d };
                let (cipher, macb) = body.split_at(body.len() - 16);
                let mut mac = [0u8; 16];
                mac.copy_from_slice(macb);
                let nonce = nonce_from_counter(self.rx_n);
                let plain = aead_open(&key, &nonce, cipher, &mac)?;
                self.rx_n += 1;
                self.apps.push(plain);
                Ok(())
            }
            _ => {
                self.mark_fail("unexpected frame");
                Err(Error::Proto("unexpected frame"))
            }
        }
    }

    pub fn take_apps(&mut self) -> Vec<Vec<u8>> {
        std::mem::take(&mut self.apps)
    }

    fn send_server_hello(&mut self, status: u8) {
        let mut body = vec![1, status];
        body.extend_from_slice(&self.identity.pub_key);
        let nbytes = self.name.as_bytes();
        let n = nbytes.len().min(31);
        body.push(n as u8);
        body.extend_from_slice(&nbytes[..n]);
        encode_frame(&mut self.outbound, SERVER_HELLO, &body);
    }

    fn on_client_hello(&mut self, body: &[u8]) -> Result<()> {
        if body.len() < 35 || body[0] != 1 {
            self.mark_fail("bad hello");
            return Err(Error::Proto("bad hello"));
        }
        self.intent = body[1];
        self.peer_pub.copy_from_slice(&body[2..34]);
        let nlen = body[34] as usize;
        if 35 + nlen > body.len() {
            self.mark_fail("bad hello");
            return Err(Error::Proto("bad hello"));
        }
        self.peer_name = String::from_utf8_lossy(&body[35..35 + nlen]).into_owned();
        if self.intent == 1 {
            if !self.armed {
                self.send_server_hello(2);
                self.mark_fail("pairing not armed");
                return Err(Error::Proto("pairing not armed"));
            }
            self.send_server_hello(0);
            self.stage = Stage::WaitPairEph;
            return Ok(());
        }
        if self.intent == 2 {
            if !self.known.iter().any(|k| k == &self.peer_pub) {
                self.send_server_hello(3);
                self.mark_fail("unknown peer");
                return Err(Error::Proto("unknown peer"));
            }
            self.send_server_hello(1);
            self.stage = Stage::WaitSessEph;
            return Ok(());
        }
        self.send_server_hello(5);
        self.mark_fail("bad intent");
        Err(Error::Proto("bad intent"))
    }

    fn on_server_hello(&mut self, body: &[u8]) -> Result<()> {
        if body.len() < 35 || body[0] != 1 {
            self.mark_fail("bad hello");
            return Err(Error::Proto("bad hello"));
        }
        let status = body[1];
        self.peer_pub.copy_from_slice(&body[2..34]);
        let nlen = body[34] as usize;
        if 35 + nlen > body.len() {
            self.mark_fail("bad hello");
            return Err(Error::Proto("bad hello"));
        }
        self.peer_name = String::from_utf8_lossy(&body[35..35 + nlen]).into_owned();
        if status == 2 {
            self.mark_fail("pairing not armed");
            return Err(Error::Proto("pairing not armed"));
        }
        if status == 3 {
            self.mark_fail("unknown peer");
            return Err(Error::Proto("unknown peer"));
        }
        if self.intent == 1 && status == 0 {
            self.send_eph(PAIR_EPH)?;
            self.stage = Stage::WaitPairEph;
            return Ok(());
        }
        if self.intent == 2 && status == 1 {
            self.send_eph(SESS_EPH)?;
            self.stage = Stage::WaitSessEph;
            return Ok(());
        }
        self.mark_fail("unexpected status");
        Err(Error::Proto("unexpected status"))
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn exchange(a: &mut Link, b: &mut Link) {
        for _ in 0..8 {
            let out = a.take_out();
            if !out.is_empty() {
                let _ = b.feed(&out);
            }
            let back = b.take_out();
            if !back.is_empty() {
                let _ = a.feed(&back);
            }
        }
    }

    fn unhex(s: &str) -> [u8; 32] {
        let mut out = [0u8; 32];
        for i in 0..32 {
            out[i] = u8::from_str_radix(&s[i * 2..i * 2 + 2], 16).unwrap();
        }
        out
    }

    #[test]
    fn blake2_x25519_and_aead_match_firmware() {
        let empty = hash32(&[]);
        assert_eq!(
            hex(&empty),
            "0e5751c026e543b2e8ab2eb06099daa1d1e5df47778f7787faab45cdf12fe3a8"
        );
        let alice = Identity::from_priv(unhex(
            "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",
        ));
        assert_eq!(
            hex(&alice.pub_key),
            "8520f0098930a754748b7ddcb43ef75a0dbf3a0d26381af4eba4a98eaa9b4e6a"
        );
        let mut key = [7u8; 32];
        let nonce = nonce_from_counter(3);
        let (cipher, mac) = aead_seal(&key, &nonce, &[1, 2, 3, 4, 5]).unwrap();
        assert_eq!(hex(&cipher), "2a4ccae3c3");
        assert_eq!(hex(&mac), "0bfe9b01fae115b8c393a0cabc207b60");
        key[0] ^= 1;
        assert!(aead_open(&key, &nonce, &cipher, &mac).is_err());
    }

    #[test]
    fn pair_and_session_match_firmware_vectors() {
        let device = Identity::from_priv(unhex(
            "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",
        ));
        let client = Identity::from_priv(unhex(
            "5dab087e624a8a4b79e17f8b83800ee66f3bb1292618b6fd1c2f8b27ff88e0eb",
        ));
        let mut dev = Link::device(device.clone(), "KMJ");
        let mut cli = Link::client(client.clone());
        dev.arm(true);
        dev.set_next_eph([0xaa; 32]);
        cli.set_next_eph([0xbb; 32]);
        cli.begin_client(1, "desk").unwrap();
        exchange(&mut cli, &mut dev);
        assert!(dev.waiting_confirm() && cli.waiting_confirm());
        assert_eq!(dev.pair_code(), "131422");
        assert_eq!(dev.pair_code(), cli.pair_code());
        cli.confirm().unwrap();
        exchange(&mut cli, &mut dev);
        assert!(!dev.paired() && !cli.paired());
        dev.confirm().unwrap();
        exchange(&mut cli, &mut dev);
        assert!(dev.paired() && cli.paired());

        let mut dev = Link::device(device, "KMJ");
        let mut cli = Link::client(client.clone());
        dev.allow_peer(client.pub_key);
        dev.set_next_eph([0xaa; 32]);
        cli.set_next_eph([0xbb; 32]);
        cli.begin_client(2, "desk").unwrap();
        exchange(&mut cli, &mut dev);
        assert!(dev.is_secure() && cli.is_secure());
        let device_pub = Identity::from_priv(unhex(
            "77076d0a7318a57d3c16c17251b26645df4c2f87ebc0992ab177fba51db92c2a",
        ))
        .pub_key;
        let keys = session_keys_client(&client, &device_pub, &[0xbb; 32], &x25519_public(&[0xaa; 32]));
        assert_eq!(
            hex(&keys.c2d),
            "f5619350ef423d9a2e7d51648d661a46b7b61fe4c6982cb8b9db96c470b2554e"
        );
        assert_eq!(
            hex(&keys.d2c),
            "0b67a8c969b6f1ecd936c57e60802a4f577045240d128fda79b7bd6fc72a317b"
        );
        cli.send_app(&[0x02, 0x04, 0x01]).unwrap();
        exchange(&mut cli, &mut dev);
        assert_eq!(dev.take_apps(), vec![vec![0x02, 0x04, 0x01]]);
    }

    fn hex(bytes: &[u8]) -> String {
        bytes.iter().map(|b| format!("{b:02x}")).collect()
    }
}
