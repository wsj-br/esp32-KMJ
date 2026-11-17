//! Low-level keyboard and mouse hooks. A message pump on the capture thread
//! keeps the hooks alive. Raw Input supplies relative motion.

use std::io;

use crate::{CaptureEvent, InputCapture};

pub struct WinGrab {
    running: bool,
}

impl WinGrab {
    pub fn new() -> Self {
        Self { running: false }
    }
}

impl InputCapture for WinGrab {
    fn start(&mut self) -> io::Result<()> {
        // The hook procedure and GetMessage pump are installed from the
        // dedicated capture thread in `kmj_client`. This object only tracks
        // the requested state so the rest of the app can be tested elsewhere.
        self.running = true;
        install_hooks()
    }

    fn stop(&mut self) {
        self.running = false;
        remove_hooks();
    }

    fn poll(&mut self) -> Vec<CaptureEvent> {
        if !self.running {
            return Vec::new();
        }
        drain_events()
    }
}

fn install_hooks() -> io::Result<()> {
    use windows_sys::Win32::Foundation::HINSTANCE;
    use windows_sys::Win32::UI::WindowsAndMessaging::{
        SetWindowsHookExW, WH_KEYBOARD_LL, WH_MOUSE_LL,
    };
    unsafe {
        let keyboard = SetWindowsHookExW(WH_KEYBOARD_LL, Some(keyboard_proc), 0 as HINSTANCE, 0);
        let mouse = SetWindowsHookExW(WH_MOUSE_LL, Some(mouse_proc), 0 as HINSTANCE, 0);
        if keyboard.is_null() || mouse.is_null() {
            return Err(io::Error::last_os_error());
        }
        KEYBOARD = keyboard;
        MOUSE = mouse;
    }
    Ok(())
}

fn remove_hooks() {
    use windows_sys::Win32::UI::WindowsAndMessaging::UnhookWindowsHookEx;
    unsafe {
        if !KEYBOARD.is_null() {
            UnhookWindowsHookEx(KEYBOARD);
            KEYBOARD = std::ptr::null_mut();
        }
        if !MOUSE.is_null() {
            UnhookWindowsHookEx(MOUSE);
            MOUSE = std::ptr::null_mut();
        }
    }
}

fn drain_events() -> Vec<CaptureEvent> {
    QUEUE.lock().unwrap_or_else(|err| err.into_inner()).drain(..).collect()
}

fn push_event(event: CaptureEvent) {
    QUEUE.lock().unwrap_or_else(|err| err.into_inner()).push(event);
}

fn hid_from_vk(vk: u32) -> Option<u8> {
    Some(match vk {
        0x1B => 0x29,
        0x11 | 0xA2 => 0xE0,
        0xA3 => 0xE4,
        0x10 | 0xA0 => 0xE1,
        0xA1 => 0xE5,
        0x12 | 0xA4 => 0xE2,
        0xA5 => 0xE6,
        0x5B => 0xE3,
        0x08 => 0x2A,
        0x09 => 0x2B,
        0x0D => 0x28,
        0x20 => 0x2C,
        0x25 => 0x50,
        0x26 => 0x52,
        0x27 => 0x4F,
        0x28 => 0x51,
        code if (0x30..=0x39).contains(&code) => {
            if code == 0x30 { 0x27 } else { 0x1E + (code as u8 - 0x31) }
        }
        code if (0x41..=0x5A).contains(&code) => 0x04 + (code as u8 - 0x41),
        _ => return None,
    })
}

use std::sync::Mutex;

use windows_sys::Win32::Foundation::LRESULT;
use windows_sys::Win32::UI::WindowsAndMessaging::HHOOK;

static mut KEYBOARD: HHOOK = std::ptr::null_mut();
static mut MOUSE: HHOOK = std::ptr::null_mut();
static QUEUE: Mutex<Vec<CaptureEvent>> = Mutex::new(Vec::new());
static LAST_X: Mutex<i32> = Mutex::new(0);
static LAST_Y: Mutex<i32> = Mutex::new(0);
static BUTTONS: Mutex<u8> = Mutex::new(0);

#[repr(C)]
struct KbDll {
    vk: u32,
    scan: u32,
    flags: u32,
    time: u32,
    extra: usize,
}

#[repr(C)]
struct MsDll {
    x: i32,
    y: i32,
    data: u32,
    flags: u32,
    time: u32,
    extra: usize,
}

unsafe extern "system" fn keyboard_proc(code: i32, wparam: usize, lparam: isize) -> LRESULT {
    use windows_sys::Win32::UI::WindowsAndMessaging::CallNextHookEx;
    if code >= 0 {
        let info = &*(lparam as *const KbDll);
        let down = wparam == 0x100 || wparam == 0x104;
        if let Some(usage) = hid_from_vk(info.vk) {
            push_event(CaptureEvent::Key { usage, down });
        }
        return 1;
    }
    CallNextHookEx(KEYBOARD, code, wparam, lparam)
}

unsafe extern "system" fn mouse_proc(code: i32, wparam: usize, lparam: isize) -> LRESULT {
    use windows_sys::Win32::UI::WindowsAndMessaging::CallNextHookEx;
    if code >= 0 {
        let info = &*(lparam as *const MsDll);
        match wparam as u32 {
            0x200 => {
                let mut x = LAST_X.lock().unwrap_or_else(|err| err.into_inner());
                let mut y = LAST_Y.lock().unwrap_or_else(|err| err.into_inner());
                let dx = info.x - *x;
                let dy = info.y - *y;
                *x = info.x;
                *y = info.y;
                if dx != 0 || dy != 0 {
                    push_event(CaptureEvent::Mouse { dx, dy });
                }
            }
            0x201 | 0x202 | 0x204 | 0x205 | 0x207 | 0x208 => {
                let bit = match wparam as u32 {
                    0x201 | 0x202 => 0x01,
                    0x204 | 0x205 => 0x02,
                    _ => 0x04,
                };
                let down = matches!(wparam as u32, 0x201 | 0x204 | 0x207);
                let mut buttons = BUTTONS.lock().unwrap_or_else(|err| err.into_inner());
                if down { *buttons |= bit; } else { *buttons &= !bit; }
                push_event(CaptureEvent::Buttons { mask: *buttons });
            }
            0x20A => {
                let wheel = (info.data >> 16) as i16;
                push_event(CaptureEvent::Wheel { v: (wheel / 120) as i8, h: 0 });
            }
            _ => {}
        }
        return 1;
    }
    CallNextHookEx(MOUSE, code, wparam, lparam)
}
