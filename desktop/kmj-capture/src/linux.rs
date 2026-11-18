use std::io::{self, ErrorKind};
use std::os::fd::AsRawFd;
use std::sync::atomic::{AtomicBool, Ordering};
use std::sync::Arc;
use std::time::Duration;

use evdev::{Device, EventSummary, KeyCode, RelativeAxisCode};
use x11rb::connection::Connection;
use x11rb::protocol::xproto::{ConnectionExt, EventMask, GrabMode, KeyButMask};
use x11rb::protocol::Event;
use x11rb::rust_connection::RustConnection;

use crate::{linux_key_to_hid, CaptureEvent, InputCapture};

pub struct X11Grab {
    conn: RustConnection,
    root: u32,
    last_x: i16,
    last_y: i16,
    have_pos: bool,
    buttons: u8,
}

impl X11Grab {
    pub fn new() -> io::Result<Self> {
        let (conn, screen) = RustConnection::connect(None).map_err(io_err)?;
        let root = conn.setup().roots[screen].root;
        Ok(Self { conn, root, last_x: 0, last_y: 0, have_pos: false, buttons: 0 })
    }
}

impl InputCapture for X11Grab {
    fn start(&mut self) -> io::Result<()> {
        self.conn
            .grab_keyboard(false, self.root, x11rb::CURRENT_TIME, GrabMode::ASYNC, GrabMode::ASYNC)
            .map_err(io_err)?
            .reply()
            .map_err(io_err)?;
        self.conn
            .grab_pointer(
                false,
                self.root,
                EventMask::BUTTON_PRESS | EventMask::BUTTON_RELEASE | EventMask::POINTER_MOTION,
                GrabMode::ASYNC,
                GrabMode::ASYNC,
                x11rb::NONE,
                x11rb::NONE,
                x11rb::CURRENT_TIME,
            )
            .map_err(io_err)?
            .reply()
            .map_err(io_err)?;
        Ok(())
    }

    fn stop(&mut self) {
        let _ = self.conn.ungrab_keyboard(x11rb::CURRENT_TIME);
        let _ = self.conn.ungrab_pointer(x11rb::CURRENT_TIME);
        let _ = self.conn.flush();
    }

    fn poll(&mut self) -> Vec<CaptureEvent> {
        let mut out = Vec::new();
        loop {
            let event = match self.conn.poll_for_event() {
                Ok(Some(event)) => event,
                _ => break,
            };
            match event {
                Event::KeyPress(ev) | Event::KeyRelease(ev) => {
                    let down = matches!(event, Event::KeyPress(_));
                    // X keycodes are typically evdev+8. Fall back to the offset used by Xorg.
                    let code = ev.detail.saturating_sub(8) as u16;
                    if let Some(usage) = linux_key_to_hid(code) {
                        out.push(CaptureEvent::Key { usage, down });
                    }
                }
                Event::MotionNotify(ev) => {
                    if self.have_pos {
                        let dx = ev.event_x - self.last_x;
                        let dy = ev.event_y - self.last_y;
                        if dx != 0 || dy != 0 {
                            out.push(CaptureEvent::Mouse { dx: dx as i32, dy: dy as i32 });
                        }
                    }
                    self.last_x = ev.event_x;
                    self.last_y = ev.event_y;
                    self.have_pos = true;
                    warp_if_edge(&self.conn, self.root, ev.root_x, ev.root_y);
                }
                Event::ButtonPress(ev) | Event::ButtonRelease(ev) => {
                    let down = matches!(event, Event::ButtonPress(_));
                    match ev.detail {
                        1 => set_bit(&mut self.buttons, 0x01, down),
                        2 => set_bit(&mut self.buttons, 0x04, down),
                        3 => set_bit(&mut self.buttons, 0x02, down),
                        4 if down => out.push(CaptureEvent::Wheel { v: 1, h: 0 }),
                        5 if down => out.push(CaptureEvent::Wheel { v: -1, h: 0 }),
                        8 => set_bit(&mut self.buttons, 0x08, down),
                        9 => set_bit(&mut self.buttons, 0x10, down),
                        _ => {}
                    }
                    if matches!(ev.detail, 1 | 2 | 3 | 8 | 9) {
                        out.push(CaptureEvent::Buttons { mask: self.buttons });
                    }
                }
                _ => {}
            }
        }
        out
    }
}

fn set_bit(mask: &mut u8, bit: u8, down: bool) {
    if down {
        *mask |= bit;
    } else {
        *mask &= !bit;
    }
}

fn warp_if_edge(conn: &RustConnection, root: u32, x: i16, y: i16) {
    let width = conn.setup().roots[0].width_in_pixels as i16;
    let height = conn.setup().roots[0].height_in_pixels as i16;
    if x < 40 || y < 40 || x > width - 40 || y > height - 40 {
        let _ = conn.warp_pointer(x11rb::NONE, root, 0, 0, 0, 0, width / 2, height / 2);
        let _ = conn.flush();
    }
}

pub struct EvdevGrab {
    devices: Vec<Device>,
    buttons: u8,
}

impl EvdevGrab {
    pub fn new() -> io::Result<Self> {
        let mut devices = Vec::new();
        let mut entries = evdev::enumerate().collect::<Vec<_>>();
        entries.sort_by_key(|(path, _)| path.clone());
        for (_path, mut device) in entries {
            if is_board_hid(&device) {
                continue;
            }
            let keys = device.supported_keys().map(|k| k.contains(KeyCode::KEY_A)).unwrap_or(false);
            let rel = device
                .supported_relative_axes()
                .map(|a| a.contains(RelativeAxisCode::REL_X))
                .unwrap_or(false);
            if (keys || rel) && device.grab().is_ok() {
                if set_nonblocking(&device).is_ok() {
                    devices.push(device);
                } else {
                    let _ = device.ungrab();
                }
            }
        }
        if devices.is_empty() {
            return Err(io::Error::new(
                ErrorKind::PermissionDenied,
                "no input devices could be grabbed; add the user to the input group",
            ));
        }
        Ok(Self { devices, buttons: 0 })
    }
}

impl InputCapture for EvdevGrab {
    fn start(&mut self) -> io::Result<()> {
        Ok(())
    }

    fn stop(&mut self) {
        for device in &mut self.devices {
            let _ = device.ungrab();
        }
    }

    fn poll(&mut self) -> Vec<CaptureEvent> {
        let mut out = Vec::new();
        for device in &mut self.devices {
            loop {
                match device.fetch_events() {
                    Ok(events) => {
                        for ev in events {
                            push_evdev(ev, &mut self.buttons, &mut out);
                        }
                    }
                    Err(_) => break,
                }
            }
        }
        let _ = Duration::from_millis(0);
        out
    }
}

/// Read keyboard keys until `stop` is set. Keyboard-only devices are grabbed so a
/// compositor shortcut such as GNOME's Ctrl+Alt+Escape (cycle panels) cannot swallow
/// the key before it is recorded. The pointer is left alone.
pub fn spawn_chord_watch(stop: Arc<AtomicBool>, emit: impl Fn(u8, bool) + Send + 'static) -> io::Result<()> {
    let mut devices = open_chord_keyboards()?;
    std::thread::Builder::new()
        .name("kmj-chord".into())
        .spawn(move || {
            while !stop.load(Ordering::SeqCst) {
                let mut idle = true;
                for device in &mut devices {
                    loop {
                        match device.fetch_events() {
                            Ok(events) => {
                                idle = false;
                                for ev in events {
                                    if let EventSummary::Key(_, code, value) = ev.destructure() {
                                        if value == 2 {
                                            continue;
                                        }
                                        if let Some(usage) = linux_key_to_hid(code.code()) {
                                            emit(usage, value != 0);
                                        }
                                    }
                                }
                            }
                            Err(_) => break,
                        }
                    }
                }
                if idle {
                    std::thread::sleep(Duration::from_millis(4));
                }
            }
            for device in &mut devices {
                let _ = device.ungrab();
            }
        })
        .map_err(|err| io::Error::new(ErrorKind::Other, err.to_string()))?;
    Ok(())
}

fn open_chord_keyboards() -> io::Result<Vec<Device>> {
    let mut entries = evdev::enumerate().collect::<Vec<_>>();
    entries.sort_by_key(|(path, _)| path.clone());
    let mut grabbed = Vec::new();
    let mut readable = Vec::new();
    for (_path, mut device) in entries {
        if is_board_hid(&device) {
            continue;
        }
        let keys = device
            .supported_keys()
            .map(|keys| keys.contains(KeyCode::KEY_ESC) || keys.contains(KeyCode::KEY_A))
            .unwrap_or(false);
        if !keys || set_nonblocking(&device).is_err() {
            continue;
        }
        let pointer = device
            .supported_relative_axes()
            .map(|axes| axes.contains(RelativeAxisCode::REL_X))
            .unwrap_or(false);
        if !pointer && device.grab().is_ok() {
            grabbed.push(device);
        } else {
            readable.push(device);
        }
    }
    if !grabbed.is_empty() {
        return Ok(grabbed);
    }
    if readable.is_empty() {
        return Err(io::Error::new(
            ErrorKind::PermissionDenied,
            "no keyboard could be read; add the user to the input group",
        ));
    }
    Ok(readable)
}

fn is_board_hid(device: &Device) -> bool {
    // The board is plugged into this computer. Grabbing its own HID interfaces
    // feeds every injected report back into the session.
    device.name().is_some_and(|name| {
        let name = name.to_ascii_lowercase();
        name.contains("lilygo") || name.contains("kmj")
    })
}

fn set_nonblocking(device: &Device) -> io::Result<()> {
    let fd = device.as_raw_fd();
    let flags = unsafe { libc::fcntl(fd, libc::F_GETFL) };
    if flags < 0 || unsafe { libc::fcntl(fd, libc::F_SETFL, flags | libc::O_NONBLOCK) } < 0 {
        return Err(io::Error::last_os_error());
    }
    Ok(())
}

fn push_evdev(ev: evdev::InputEvent, buttons: &mut u8, out: &mut Vec<CaptureEvent>) {
    match ev.destructure() {
        EventSummary::Key(_, code, value) => {
            let down = value != 0;
            if let Some(usage) = linux_key_to_hid(code.code()) {
                out.push(CaptureEvent::Key { usage, down });
            }
            let bit = match code {
                KeyCode::BTN_LEFT => 0x01,
                KeyCode::BTN_RIGHT => 0x02,
                KeyCode::BTN_MIDDLE => 0x04,
                _ => 0,
            };
            if bit != 0 {
                set_bit(buttons, bit, down);
                out.push(CaptureEvent::Buttons { mask: *buttons });
            }
        }
        EventSummary::RelativeAxis(_, axis, value) => match axis {
            RelativeAxisCode::REL_X => out.push(CaptureEvent::Mouse { dx: value, dy: 0 }),
            RelativeAxisCode::REL_Y => out.push(CaptureEvent::Mouse { dx: 0, dy: value }),
            RelativeAxisCode::REL_WHEEL => out.push(CaptureEvent::Wheel { v: value as i8, h: 0 }),
            RelativeAxisCode::REL_HWHEEL => out.push(CaptureEvent::Wheel { v: 0, h: value as i8 }),
            _ => {}
        },
        _ => {}
    }
}

fn io_err(err: impl std::fmt::Display) -> io::Error {
    io::Error::new(ErrorKind::Other, err.to_string())
}

#[allow(dead_code)]
fn _fd<T: AsRawFd>(value: &T) -> i32 {
    value.as_raw_fd()
}

#[allow(dead_code)]
fn _mask(mask: KeyButMask) -> u16 {
    u32::from(mask) as u16
}
