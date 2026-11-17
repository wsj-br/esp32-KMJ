//! CGEventTap capture. Needs Accessibility and Input Monitoring permission.
//! The tap callback returns NULL to suppress the event.

use std::io;
use std::os::raw::{c_int, c_void};

use crate::{CaptureEvent, InputCapture};

pub struct MacGrab {
    tap: *mut c_void,
}

impl MacGrab {
    pub fn new() -> Self {
        Self { tap: std::ptr::null_mut() }
    }
}

impl InputCapture for MacGrab {
    fn start(&mut self) -> io::Result<()> {
        unsafe {
            let tap = CGEventTapCreate(
                0, // kCGHIDEventTap
                0, // head insert
                1, // listen only is 1; default tap can modify. 0x1 is kCGEventTapOptionDefault? 
                (1 << 10) | (1 << 11) | (1 << 1) | (1 << 2) | (1 << 6) | (1 << 22),
                tap_callback,
                std::ptr::null_mut(),
            );
            if tap.is_null() {
                return Err(io::Error::new(
                    io::ErrorKind::PermissionDenied,
                    "CGEventTap failed. Grant Accessibility and Input Monitoring, then reopen KMJ.",
                ));
            }
            self.tap = tap;
        }
        Ok(())
    }

    fn stop(&mut self) {
        self.tap = std::ptr::null_mut();
    }

    fn poll(&mut self) -> Vec<CaptureEvent> {
        Vec::new()
    }
}

extern "C" fn tap_callback(
    _proxy: *mut c_void,
    _kind: c_int,
    _event: *mut c_void,
    _user: *mut c_void,
) -> *mut c_void {
    // NULL drops the event, which is the suppression the grab needs.
    std::ptr::null_mut()
}

#[link(name = "CoreGraphics", kind = "framework")]
extern "C" {
    fn CGEventTapCreate(
        tap: u32,
        place: u32,
        options: u32,
        events: u64,
        callback: extern "C" fn(*mut c_void, c_int, *mut c_void, *mut c_void) -> *mut c_void,
        user: *mut c_void,
    ) -> *mut c_void;
}
