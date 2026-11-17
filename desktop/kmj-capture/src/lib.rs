//! Local keyboard and mouse capture. Events are suppressed while a grab is held.

use std::io;

#[derive(Debug, Clone)]
pub enum CaptureEvent {
    Key { usage: u8, down: bool },
    Mouse { dx: i32, dy: i32 },
    Buttons { mask: u8 },
    Wheel { v: i8, h: i8 },
}

pub trait InputCapture: Send {
    fn start(&mut self) -> io::Result<()>;
    fn stop(&mut self);
    fn poll(&mut self) -> Vec<CaptureEvent>;
}

pub fn open() -> io::Result<Box<dyn InputCapture>> {
    #[cfg(target_os = "linux")]
    {
        if std::env::var_os("WAYLAND_DISPLAY").is_some() {
            return Ok(Box::new(linux::EvdevGrab::new()?));
        }
        return Ok(Box::new(linux::X11Grab::new()?));
    }
    #[cfg(target_os = "windows")]
    {
        return Ok(Box::new(windows::WinGrab::new()));
    }
    #[cfg(target_os = "macos")]
    {
        return Ok(Box::new(macos::MacGrab::new()));
    }
    #[allow(unreachable_code)]
    Err(io::Error::new(io::ErrorKind::Unsupported, "capture is not built for this OS"))
}

#[cfg(target_os = "linux")]
mod linux;
#[cfg(target_os = "macos")]
mod macos;
#[cfg(target_os = "windows")]
mod windows;

pub fn linux_key_to_hid(code: u16) -> Option<u8> {
    // Linux input event codes (linux/input-event-codes.h) to HID keyboard usages.
    Some(match code {
        1 => 0x29,  // ESC
        2 => 0x1E,  // 1
        3 => 0x1F,
        4 => 0x20,
        5 => 0x21,
        6 => 0x22,
        7 => 0x23,
        8 => 0x24,
        9 => 0x25,
        10 => 0x26,
        11 => 0x27, // 0
        12 => 0x2D,
        13 => 0x2E,
        14 => 0x2A, // backspace
        15 => 0x2B, // tab
        16 => 0x14, // Q
        17 => 0x1A,
        18 => 0x08,
        19 => 0x15,
        20 => 0x17,
        21 => 0x1C,
        22 => 0x18,
        23 => 0x0C,
        24 => 0x12,
        25 => 0x13,
        26 => 0x2F,
        27 => 0x30,
        28 => 0x28, // enter
        29 => 0xE0, // left ctrl
        30 => 0x04, // A
        31 => 0x16,
        32 => 0x07,
        33 => 0x09,
        34 => 0x0A,
        35 => 0x0B,
        36 => 0x0D,
        37 => 0x0E,
        38 => 0x0F,
        39 => 0x33,
        40 => 0x34,
        41 => 0x35,
        42 => 0xE1, // left shift
        43 => 0x31,
        44 => 0x1D, // Z
        45 => 0x1B,
        46 => 0x06,
        47 => 0x19,
        48 => 0x05,
        49 => 0x11,
        50 => 0x10,
        51 => 0x36,
        52 => 0x37,
        53 => 0x38,
        54 => 0xE5, // right shift
        56 => 0xE2, // left alt
        57 => 0x2C, // space
        58 => 0x39, // caps
        97 => 0xE4, // right ctrl
        100 => 0xE6, // right alt
        103 => 0x52, // up
        105 => 0x50,
        106 => 0x4F,
        108 => 0x51,
        125 => 0xE3, // left meta
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::linux_key_to_hid;

    #[test]
    fn maps_chord_keys() {
        assert_eq!(linux_key_to_hid(29), Some(0xE0));
        assert_eq!(linux_key_to_hid(56), Some(0xE2));
        assert_eq!(linux_key_to_hid(1), Some(0x29));
        assert_eq!(linux_key_to_hid(30), Some(0x04));
    }
}
