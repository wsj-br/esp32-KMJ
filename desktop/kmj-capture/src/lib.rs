//! Local keyboard and mouse capture. Events are suppressed while a grab is held.

use std::io;

#[derive(Debug, Clone, PartialEq, Eq)]
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
    Err(io::Error::new(
        io::ErrorKind::Unsupported,
        "capture is not built for this OS",
    ))
}

// Pure Quartz decoding. macOS uses it; other hosts compile it only under
// `cargo test`, so the app build does not warn that the mapping is unused.
#[cfg(any(test, target_os = "macos"))]
mod mac_events;

#[cfg(target_os = "linux")]
mod linux;

#[cfg(target_os = "linux")]
pub use linux::spawn_chord_watch;
#[cfg(target_os = "macos")]
mod macos;
#[cfg(target_os = "windows")]
mod windows;

pub fn linux_key_to_hid(code: u16) -> Option<u8> {
    // Linux input event codes (linux/input-event-codes.h) to HID keyboard usages.
    Some(match code {
        1 => 0x29, // ESC
        2 => 0x1E, // 1
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
        54 => 0xE5,                          // right shift
        55 => 0x55,                          // keypad *
        56 => 0xE2,                          // left alt
        57 => 0x2C,                          // space
        58 => 0x39,                          // caps
        59..=68 => 0x3A + (code as u8 - 59), // F1-F10
        69 => 0x53,                          // num lock
        70 => 0x47,                          // scroll lock
        71 => 0x5F,                          // keypad 7
        72 => 0x60,
        73 => 0x61,
        74 => 0x56, // keypad -
        75 => 0x5C, // keypad 4
        76 => 0x5D,
        77 => 0x5E,
        78 => 0x57, // keypad +
        79 => 0x59, // keypad 1
        80 => 0x5A,
        81 => 0x5B,
        82 => 0x62,                             // keypad 0
        83 => 0x63,                             // keypad .
        86 => 0x64,                             // non-US \ |
        87 => 0x44,                             // F11
        88 => 0x45,                             // F12
        96 => 0x58,                             // keypad enter
        97 => 0xE4,                             // right ctrl
        98 => 0x54,                             // keypad /
        99 => 0x46,                             // print screen
        100 => 0xE6,                            // right alt
        102 => 0x4A,                            // home
        103 => 0x52,                            // up
        104 => 0x4B,                            // page up
        105 => 0x50,                            // left
        106 => 0x4F,                            // right
        107 => 0x4D,                            // end
        108 => 0x51,                            // down
        109 => 0x4E,                            // page down
        110 => 0x49,                            // insert
        111 => 0x4C,                            // delete
        113 => 0x7F,                            // mute
        114 => 0x81,                            // volume down
        115 => 0x80,                            // volume up
        117 => 0x67,                            // keypad =
        119 => 0x48,                            // pause
        125 => 0xE3,                            // left meta
        126 => 0xE7,                            // right meta
        127 | 139 => 0x65,                      // compose / menu
        183..=194 => 0x68 + (code as u8 - 183), // F13-F24
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
        assert_eq!(linux_key_to_hid(59), Some(0x3A));
        assert_eq!(linux_key_to_hid(68), Some(0x43));
        assert_eq!(linux_key_to_hid(87), Some(0x44));
        assert_eq!(linux_key_to_hid(88), Some(0x45));
        assert_eq!(linux_key_to_hid(183), Some(0x68));
        assert_eq!(linux_key_to_hid(194), Some(0x73));
        assert_eq!(linux_key_to_hid(102), Some(0x4A));
        assert_eq!(linux_key_to_hid(111), Some(0x4C));
        assert_eq!(linux_key_to_hid(55), Some(0x55));
        assert_eq!(linux_key_to_hid(96), Some(0x58));
        assert_eq!(linux_key_to_hid(126), Some(0xE7));
    }
}
