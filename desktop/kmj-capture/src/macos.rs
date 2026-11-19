//! Suppressing `CGEventTap`. Needs Accessibility and Input Monitoring.
//! The tap is created with `kCGEventTapOptionDefault` (0). Listen-only (1)
//! cannot drop events. A thread runs the tap's run loop; `poll` drains the queue.

use std::io::{self, ErrorKind};
use std::os::raw::c_void;
use std::sync::atomic::{AtomicBool, AtomicPtr, Ordering};
use std::sync::{mpsc, Arc, Mutex};
use std::thread::JoinHandle;
use std::time::Duration;

use crate::mac_events::{
    MacTapInput, MacTapState, HID_EVENT_MASK, TAP_DISABLED_BY_TIMEOUT, TAP_DISABLED_BY_USER,
    TAP_HEAD, TAP_LOCATION_HID, TAP_OPTIONS_SUPPRESS,
};
use crate::{CaptureEvent, InputCapture};

struct Shared {
    queue: Mutex<Vec<CaptureEvent>>,
    logic: Mutex<MacTapState>,
    tap: AtomicPtr<c_void>,
}

pub struct MacGrab {
    shared: Arc<Shared>,
    stop_flag: Arc<AtomicBool>,
    run_loop: Arc<AtomicPtr<c_void>>,
    thread: Option<JoinHandle<()>>,
}

impl MacGrab {
    pub fn new() -> Self {
        Self {
            shared: Arc::new(Shared {
                queue: Mutex::new(Vec::new()),
                logic: Mutex::new(MacTapState::default()),
                tap: AtomicPtr::new(std::ptr::null_mut()),
            }),
            stop_flag: Arc::new(AtomicBool::new(false)),
            run_loop: Arc::new(AtomicPtr::new(std::ptr::null_mut())),
            thread: None,
        }
    }
}

impl Drop for MacGrab {
    fn drop(&mut self) {
        self.stop();
    }
}

impl InputCapture for MacGrab {
    fn start(&mut self) -> io::Result<()> {
        if self.thread.is_some() {
            return Ok(());
        }
        self.stop_flag.store(false, Ordering::Release);
        self.shared
            .queue
            .lock()
            .unwrap_or_else(|err| err.into_inner())
            .clear();
        *self
            .shared
            .logic
            .lock()
            .unwrap_or_else(|err| err.into_inner()) = MacTapState::default();

        let shared = Arc::clone(&self.shared);
        let stop_flag = Arc::clone(&self.stop_flag);
        let run_loop = Arc::clone(&self.run_loop);
        let (ready_tx, ready_rx) = mpsc::channel();
        let handle = std::thread::Builder::new()
            .name("kmj-capture".into())
            .spawn(move || tap_thread(shared, stop_flag, run_loop, ready_tx))
            .map_err(|err| io::Error::new(ErrorKind::Other, err.to_string()))?;

        match ready_rx.recv_timeout(Duration::from_secs(2)) {
            Ok(Ok(())) => {
                self.thread = Some(handle);
                Ok(())
            }
            Ok(Err(err)) => {
                let _ = handle.join();
                Err(err)
            }
            Err(_) => {
                self.stop_flag.store(true, Ordering::Release);
                let loop_ref = self.run_loop.swap(std::ptr::null_mut(), Ordering::AcqRel);
                if !loop_ref.is_null() {
                    unsafe { CFRunLoopStop(loop_ref) };
                }
                let _ = handle.join();
                Err(io::Error::new(
                    ErrorKind::TimedOut,
                    "event tap did not start",
                ))
            }
        }
    }

    fn stop(&mut self) {
        self.stop_flag.store(true, Ordering::Release);
        let loop_ref = self.run_loop.swap(std::ptr::null_mut(), Ordering::AcqRel);
        if !loop_ref.is_null() {
            unsafe { CFRunLoopStop(loop_ref) };
        }
        if let Some(thread) = self.thread.take() {
            let _ = thread.join();
        }
    }

    fn poll(&mut self) -> Vec<CaptureEvent> {
        if self.thread.is_none() {
            return Vec::new();
        }
        self.shared
            .queue
            .lock()
            .unwrap_or_else(|err| err.into_inner())
            .drain(..)
            .collect()
    }
}

struct TapSession {
    tap: *mut c_void,
    source: *mut c_void,
    loop_ref: *mut c_void,
    added: bool,
    shared: Arc<Shared>,
    run_loop: Arc<AtomicPtr<c_void>>,
}

impl Drop for TapSession {
    fn drop(&mut self) {
        unsafe {
            if !self.tap.is_null() {
                CGEventTapEnable(self.tap, 0);
            }
            if self.added && !self.loop_ref.is_null() {
                CFRunLoopRemoveSource(self.loop_ref, self.source, kCFRunLoopCommonModes);
            }
            self.shared
                .tap
                .store(std::ptr::null_mut(), Ordering::Release);
            self.run_loop.store(std::ptr::null_mut(), Ordering::Release);
            if !self.source.is_null() {
                CFRelease(self.source);
            }
            if !self.tap.is_null() {
                CFRelease(self.tap);
            }
        }
    }
}

fn tap_thread(
    shared: Arc<Shared>,
    stop_flag: Arc<AtomicBool>,
    run_loop: Arc<AtomicPtr<c_void>>,
    ready: mpsc::Sender<io::Result<()>>,
) {
    let user = Arc::as_ptr(&shared) as *mut c_void;
    let tap = unsafe {
        CGEventTapCreate(
            TAP_LOCATION_HID,
            TAP_HEAD,
            TAP_OPTIONS_SUPPRESS,
            HID_EVENT_MASK,
            tap_callback,
            user,
        )
    };
    if tap.is_null() {
        let _ = ready.send(Err(io::Error::new(
            ErrorKind::PermissionDenied,
            "CGEventTap failed. Grant Accessibility and Input Monitoring to this KMJ binary, then reopen KMJ.",
        )));
        return;
    }
    let source = unsafe { CFMachPortCreateRunLoopSource(std::ptr::null(), tap, 0) };
    if source.is_null() {
        unsafe { CFRelease(tap) };
        let _ = ready.send(Err(io::Error::new(
            ErrorKind::Other,
            "CFMachPortCreateRunLoopSource failed",
        )));
        return;
    }
    let mut session = TapSession {
        tap,
        source,
        loop_ref: std::ptr::null_mut(),
        added: false,
        shared,
        run_loop,
    };
    unsafe {
        session.loop_ref = CFRunLoopGetCurrent();
        CFRunLoopAddSource(session.loop_ref, session.source, kCFRunLoopCommonModes);
        session.added = true;
        CGEventTapEnable(session.tap, 1);
        session.shared.tap.store(session.tap, Ordering::Release);
        session.run_loop.store(session.loop_ref, Ordering::Release);
        let _ = ready.send(Ok(()));
        // `CFRunLoopStop` from `stop` is a no-op until this call, so the flag
        // covers a stop that lands in that gap. `CFRunLoopRun` returns when stopped.
        if !stop_flag.load(Ordering::Acquire) {
            CFRunLoopRun();
        }
    }
}

extern "C" fn tap_callback(
    _proxy: *mut c_void,
    kind: u32,
    event: *mut c_void,
    user: *mut c_void,
) -> *mut c_void {
    match std::panic::catch_unwind(|| on_tap(kind, event, user)) {
        Ok(value) => value,
        Err(_) => event,
    }
}

fn on_tap(kind: u32, event: *mut c_void, user: *mut c_void) -> *mut c_void {
    if user.is_null() {
        return event;
    }
    // The pointer stays valid until `tap_thread` returns. `stop` joins that thread.
    let shared = unsafe { &*(user as *const Shared) };
    if kind == TAP_DISABLED_BY_TIMEOUT || kind == TAP_DISABLED_BY_USER {
        let tap = shared.tap.load(Ordering::Acquire);
        if !tap.is_null() {
            unsafe { CGEventTapEnable(tap, 1) };
        }
        return event;
    }
    if event.is_null() {
        return std::ptr::null_mut();
    }
    let input = unsafe { read_tap(kind, event) };
    let (suppress, events) = shared
        .logic
        .lock()
        .unwrap_or_else(|err| err.into_inner())
        .handle(input);
    if !events.is_empty() {
        shared
            .queue
            .lock()
            .unwrap_or_else(|err| err.into_inner())
            .extend(events);
    }
    if suppress {
        std::ptr::null_mut()
    } else {
        event
    }
}

unsafe fn read_tap(kind: u32, event: *mut c_void) -> MacTapInput {
    let field = |id: u32| CGEventGetIntegerValueField(event, id);
    MacTapInput {
        kind,
        keycode: field(9) as u16,
        autorepeat: field(8) != 0,
        flags: CGEventGetFlags(event),
        dx: narrow(field(4)),
        dy: narrow(field(5)),
        button: field(3),
        continuous: field(88) != 0,
        line_v: narrow(field(11)),
        line_h: narrow(field(12)),
        point_v: narrow(field(96)),
        point_h: narrow(field(97)),
    }
}

fn narrow(value: i64) -> i32 {
    value.clamp(i32::MIN as i64, i32::MAX as i64) as i32
}

#[link(name = "CoreFoundation", kind = "framework")]
extern "C" {
    static kCFRunLoopCommonModes: *const c_void;

    fn CFMachPortCreateRunLoopSource(
        allocator: *const c_void,
        port: *mut c_void,
        order: isize,
    ) -> *mut c_void;
    fn CFRunLoopGetCurrent() -> *mut c_void;
    fn CFRunLoopAddSource(run_loop: *mut c_void, source: *mut c_void, mode: *const c_void);
    fn CFRunLoopRemoveSource(run_loop: *mut c_void, source: *mut c_void, mode: *const c_void);
    fn CFRunLoopRun();
    fn CFRunLoopStop(run_loop: *mut c_void);
    fn CFRelease(value: *const c_void);
}

#[link(name = "CoreGraphics", kind = "framework")]
extern "C" {
    fn CGEventTapCreate(
        tap: u32,
        place: u32,
        options: u32,
        events: u64,
        callback: extern "C" fn(*mut c_void, u32, *mut c_void, *mut c_void) -> *mut c_void,
        user: *mut c_void,
    ) -> *mut c_void;
    fn CGEventTapEnable(tap: *mut c_void, enable: u8);
    fn CGEventGetIntegerValueField(event: *mut c_void, field: u32) -> i64;
    fn CGEventGetFlags(event: *mut c_void) -> u64;
}
