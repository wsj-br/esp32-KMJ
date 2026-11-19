//! Quartz virtual-key and event-tap decoding. This module is pure so the
//! mapping can be tested on every host. `macos.rs` fills `MacTapInput` from
//! a `CGEvent` and forwards the resulting `CaptureEvent`s.

use crate::CaptureEvent;

/// `kCGHIDEventTap`.
pub const TAP_LOCATION_HID: u32 = 0;
/// `kCGHeadInsertEventTap`.
pub const TAP_HEAD: u32 = 0;
/// `kCGEventTapOptionDefault`. Value 1 is listen-only and cannot drop events.
pub const TAP_OPTIONS_SUPPRESS: u32 = 0;

pub const TAP_DISABLED_BY_TIMEOUT: u32 = 0xFFFF_FFFE;
pub const TAP_DISABLED_BY_USER: u32 = 0xFFFF_FFFF;

const LEFT_DOWN: u32 = 1;
const LEFT_UP: u32 = 2;
const RIGHT_DOWN: u32 = 3;
const RIGHT_UP: u32 = 4;
const MOVED: u32 = 5;
const LEFT_DRAG: u32 = 6;
const RIGHT_DRAG: u32 = 7;
const KEY_DOWN: u32 = 10;
const KEY_UP: u32 = 11;
const FLAGS: u32 = 12;
const SCROLL: u32 = 22;
const OTHER_DOWN: u32 = 25;
const OTHER_UP: u32 = 26;
const OTHER_DRAG: u32 = 27;

/// Event-type bits the tap asks Quartz to deliver.
pub const HID_EVENT_MASK: u64 = (1 << LEFT_DOWN)
    | (1 << LEFT_UP)
    | (1 << RIGHT_DOWN)
    | (1 << RIGHT_UP)
    | (1 << MOVED)
    | (1 << LEFT_DRAG)
    | (1 << RIGHT_DRAG)
    | (1 << KEY_DOWN)
    | (1 << KEY_UP)
    | (1 << FLAGS)
    | (1 << SCROLL)
    | (1 << OTHER_DOWN)
    | (1 << OTHER_UP)
    | (1 << OTHER_DRAG);

/// Trackpad point deltas are pixels. A notch is one HID wheel step.
const PIXELS_PER_NOTCH: i32 = 16;

const CAPS_KEYCODE: u16 = 0x39;
const CAPS_USAGE: u8 = 0x39;

#[derive(Debug, Clone, Copy)]
pub struct MacTapInput {
    pub kind: u32,
    pub keycode: u16,
    pub autorepeat: bool,
    pub flags: u64,
    pub dx: i32,
    pub dy: i32,
    pub button: i64,
    pub continuous: bool,
    pub line_v: i32,
    pub line_h: i32,
    pub point_v: i32,
    pub point_h: i32,
}

impl Default for MacTapInput {
    fn default() -> Self {
        Self {
            kind: 0,
            keycode: 0,
            autorepeat: false,
            flags: 0,
            dx: 0,
            dy: 0,
            button: 0,
            continuous: false,
            line_v: 0,
            line_h: 0,
            point_v: 0,
            point_h: 0,
        }
    }
}

#[derive(Debug, Default)]
pub struct MacTapState {
    buttons: u8,
    /// HID modifier bits currently reported. Synced from Quartz flags so a
    /// missed key-up cannot leave Command or Option held.
    mods: u8,
    caps_on: bool,
    scroll_v: i32,
    scroll_h: i32,
}

impl MacTapState {
    pub fn handle(&mut self, input: MacTapInput) -> (bool, Vec<CaptureEvent>) {
        let mut events = Vec::new();
        match input.kind {
            KEY_DOWN | KEY_UP => self.on_key(input, &mut events),
            FLAGS => self.on_flags(input, &mut events),
            MOVED | LEFT_DRAG | RIGHT_DRAG | OTHER_DRAG => self.on_move(input, &mut events),
            LEFT_DOWN | LEFT_UP | RIGHT_DOWN | RIGHT_UP | OTHER_DOWN | OTHER_UP => {
                if let Some((bit, down)) = button_change(&input) {
                    self.set_button(bit, down, &mut events);
                }
            }
            SCROLL => self.on_scroll(input, &mut events),
            _ => {}
        }
        (suppresses(input.kind), events)
    }

    fn on_key(&mut self, input: MacTapInput, events: &mut Vec<CaptureEvent>) {
        // A letter's flags are the live modifier state. Releasing a stuck
        // Command or Option here is what lets the letter reach the target
        // as a character instead of a shortcut that types nothing.
        self.sync_modifiers(input.flags, events);
        if input.kind == KEY_DOWN && input.autorepeat {
            return;
        }
        let Some(usage) = macos_keycode_to_hid(input.keycode) else {
            return;
        };
        if is_modifier(usage) {
            return;
        }
        events.push(CaptureEvent::Key {
            usage,
            down: input.kind == KEY_DOWN,
        });
    }

    fn on_flags(&mut self, input: MacTapInput, events: &mut Vec<CaptureEvent>) {
        self.sync_modifiers(input.flags, events);
        if input.keycode != CAPS_KEYCODE {
            return;
        }
        let on = input.flags & 0x0001_0000 != 0;
        if on == self.caps_on {
            return;
        }
        self.caps_on = on;
        events.push(CaptureEvent::Key {
            usage: CAPS_USAGE,
            down: true,
        });
        events.push(CaptureEvent::Key {
            usage: CAPS_USAGE,
            down: false,
        });
    }

    fn sync_modifiers(&mut self, flags: u64, events: &mut Vec<CaptureEvent>) {
        let want = modifier_mask(flags);
        let changed = self.mods ^ want;
        for bit in 0..8u8 {
            let mask = 1u8 << bit;
            if changed & mask != 0 && want & mask == 0 {
                events.push(CaptureEvent::Key {
                    usage: 0xE0 + bit,
                    down: false,
                });
            }
        }
        for bit in 0..8u8 {
            let mask = 1u8 << bit;
            if changed & mask != 0 && want & mask != 0 {
                events.push(CaptureEvent::Key {
                    usage: 0xE0 + bit,
                    down: true,
                });
            }
        }
        self.mods = want;
    }

    fn on_move(&mut self, input: MacTapInput, events: &mut Vec<CaptureEvent>) {
        if let Some(bit) = drag_button(&input) {
            self.set_button(bit, true, events);
        }
        if input.dx != 0 || input.dy != 0 {
            events.push(CaptureEvent::Mouse {
                dx: input.dx,
                dy: input.dy,
            });
        }
    }

    fn on_scroll(&mut self, input: MacTapInput, events: &mut Vec<CaptureEvent>) {
        if input.continuous && (input.point_v != 0 || input.point_h != 0) {
            self.scroll_v += input.point_v;
            self.scroll_h += input.point_h;
            push_axis_steps(
                &mut self.scroll_v,
                &mut self.scroll_h,
                PIXELS_PER_NOTCH,
                events,
            );
        } else {
            push_line_steps(input.line_v, input.line_h, events);
        }
    }

    fn set_button(&mut self, bit: u8, down: bool, events: &mut Vec<CaptureEvent>) {
        let next = if down {
            self.buttons | bit
        } else {
            self.buttons & !bit
        };
        if next != self.buttons {
            self.buttons = next;
            events.push(CaptureEvent::Buttons { mask: next });
        }
    }
}

fn suppresses(kind: u32) -> bool {
    matches!(
        kind,
        LEFT_DOWN
            | LEFT_UP
            | RIGHT_DOWN
            | RIGHT_UP
            | MOVED
            | LEFT_DRAG
            | RIGHT_DRAG
            | KEY_DOWN
            | KEY_UP
            | FLAGS
            | SCROLL
            | OTHER_DOWN
            | OTHER_UP
            | OTHER_DRAG
    )
}

fn is_modifier(usage: u8) -> bool {
    usage == CAPS_USAGE || (0xE0..=0xE7).contains(&usage)
}

fn button_change(input: &MacTapInput) -> Option<(u8, bool)> {
    let (number, down) = match input.kind {
        LEFT_DOWN => (0, true),
        LEFT_UP => (0, false),
        RIGHT_DOWN => (1, true),
        RIGHT_UP => (1, false),
        OTHER_DOWN => (input.button, true),
        OTHER_UP => (input.button, false),
        _ => return None,
    };
    hid_button(number).map(|bit| (bit, down))
}

fn drag_button(input: &MacTapInput) -> Option<u8> {
    match input.kind {
        LEFT_DRAG => Some(0x01),
        RIGHT_DRAG => Some(0x02),
        OTHER_DRAG => hid_button(input.button),
        _ => None,
    }
}

fn hid_button(number: i64) -> Option<u8> {
    if (0..8).contains(&number) {
        Some(1u8 << number)
    } else {
        None
    }
}

/// Device-dependent bits from `IOLLEvent.h`, with the combined mask as a
/// fallback only when that family has no side bit. A release that clears the
/// side bit must not be turned back into a press by the combined mask.
fn modifier_mask(flags: u64) -> u8 {
    const LCTL: u64 = 0x0000_0001;
    const LSHIFT: u64 = 0x0000_0002;
    const RSHIFT: u64 = 0x0000_0004;
    const LCMD: u64 = 0x0000_0008;
    const RCMD: u64 = 0x0000_0010;
    const LALT: u64 = 0x0000_0020;
    const RALT: u64 = 0x0000_0040;
    const RCTL: u64 = 0x0000_2000;
    const SHIFT: u64 = 0x0002_0000;
    const CTL: u64 = 0x0004_0000;
    const ALT: u64 = 0x0008_0000;
    const CMD: u64 = 0x0010_0000;
    let mut mask = 0u8;
    if flags & LCTL != 0 {
        mask |= 1 << 0;
    }
    if flags & LSHIFT != 0 {
        mask |= 1 << 1;
    }
    if flags & LALT != 0 {
        mask |= 1 << 2;
    }
    if flags & LCMD != 0 {
        mask |= 1 << 3;
    }
    if flags & RCTL != 0 {
        mask |= 1 << 4;
    }
    if flags & RSHIFT != 0 {
        mask |= 1 << 5;
    }
    if flags & RALT != 0 {
        mask |= 1 << 6;
    }
    if flags & RCMD != 0 {
        mask |= 1 << 7;
    }
    if flags & (LCTL | RCTL) == 0 && flags & CTL != 0 {
        mask |= 1 << 0;
    }
    if flags & (LSHIFT | RSHIFT) == 0 && flags & SHIFT != 0 {
        mask |= 1 << 1;
    }
    if flags & (LALT | RALT) == 0 && flags & ALT != 0 {
        mask |= 1 << 2;
    }
    if flags & (LCMD | RCMD) == 0 && flags & CMD != 0 {
        mask |= 1 << 3;
    }
    mask
}

fn push_axis_steps(
    v_accum: &mut i32,
    h_accum: &mut i32,
    quantum: i32,
    out: &mut Vec<CaptureEvent>,
) {
    loop {
        let v = take_chunk(v_accum, quantum);
        let h = take_chunk(h_accum, quantum);
        if v == 0 && h == 0 {
            break;
        }
        out.push(CaptureEvent::Wheel { v, h });
    }
}

fn push_line_steps(mut v: i32, mut h: i32, out: &mut Vec<CaptureEvent>) {
    while v != 0 || h != 0 {
        let dv = v.clamp(-127, 127);
        let dh = h.clamp(-127, 127);
        v -= dv;
        h -= dh;
        out.push(CaptureEvent::Wheel {
            v: dv as i8,
            h: dh as i8,
        });
    }
}

fn take_chunk(accum: &mut i32, quantum: i32) -> i8 {
    if quantum <= 0 {
        return 0;
    }
    let steps = *accum / quantum;
    if steps == 0 {
        return 0;
    }
    let emitted = steps.clamp(-127, 127);
    *accum -= emitted * quantum;
    emitted as i8
}

/// macOS virtual keycode (`Events.h`, `kVK_*`) to a HID keyboard usage.
pub fn macos_keycode_to_hid(code: u16) -> Option<u8> {
    Some(match code {
        0x00 => 0x04, // A
        0x01 => 0x16, // S
        0x02 => 0x07, // D
        0x03 => 0x09, // F
        0x04 => 0x0B, // H
        0x05 => 0x0A, // G
        0x06 => 0x1D, // Z
        0x07 => 0x1B, // X
        0x08 => 0x06, // C
        0x09 => 0x19, // V
        0x0A => 0x64, // ISO section
        0x0B => 0x05, // B
        0x0C => 0x14, // Q
        0x0D => 0x1A, // W
        0x0E => 0x08, // E
        0x0F => 0x15, // R
        0x10 => 0x1C, // Y
        0x11 => 0x17, // T
        0x12 => 0x1E, // 1
        0x13 => 0x1F, // 2
        0x14 => 0x20, // 3
        0x15 => 0x21, // 4
        0x16 => 0x23, // 6
        0x17 => 0x22, // 5
        0x18 => 0x2E, // =
        0x19 => 0x26, // 9
        0x1A => 0x24, // 7
        0x1B => 0x2D, // -
        0x1C => 0x25, // 8
        0x1D => 0x27, // 0
        0x1E => 0x30, // ]
        0x1F => 0x12, // O
        0x20 => 0x18, // U
        0x21 => 0x2F, // [
        0x22 => 0x0C, // I
        0x23 => 0x13, // P
        0x24 => 0x28, // return
        0x25 => 0x0F, // L
        0x26 => 0x0D, // J
        0x27 => 0x34, // '
        0x28 => 0x0E, // K
        0x29 => 0x33, // ;
        0x2A => 0x31, // \
        0x2B => 0x36, // ,
        0x2C => 0x38, // /
        0x2D => 0x11, // N
        0x2E => 0x10, // M
        0x2F => 0x37, // .
        0x30 => 0x2B, // tab
        0x31 => 0x2C, // space
        0x32 => 0x35, // `
        0x33 => 0x2A, // delete (backspace)
        0x35 => 0x29, // escape
        0x36 => 0xE7, // right command
        0x37 => 0xE3, // command
        0x38 => 0xE1, // shift
        0x39 => 0x39, // caps lock
        0x3A => 0xE2, // option
        0x3B => 0xE0, // control
        0x3C => 0xE5, // right shift
        0x3D => 0xE6, // right option
        0x3E => 0xE4, // right control
        0x40 => 0x6C, // F17
        0x41 => 0x63, // keypad .
        0x43 => 0x55, // keypad *
        0x45 => 0x57, // keypad +
        0x47 => 0x53, // keypad clear
        0x48 => 0x80, // volume up
        0x49 => 0x81, // volume down
        0x4A => 0x7F, // mute
        0x4B => 0x54, // keypad /
        0x4C => 0x58, // keypad enter
        0x4E => 0x56, // keypad -
        0x4F => 0x6D, // F18
        0x50 => 0x6E, // F19
        0x51 => 0x67, // keypad =
        0x52 => 0x62, // keypad 0
        0x53 => 0x59, // keypad 1
        0x54 => 0x5A,
        0x55 => 0x5B,
        0x56 => 0x5C,
        0x57 => 0x5D,
        0x58 => 0x5E,
        0x59 => 0x5F, // keypad 7
        0x5A => 0x6F, // F20
        0x5B => 0x60, // keypad 8
        0x5C => 0x61, // keypad 9
        0x60 => 0x3E, // F5
        0x61 => 0x3F, // F6
        0x62 => 0x40, // F7
        0x63 => 0x3C, // F3
        0x64 => 0x41, // F8
        0x65 => 0x42, // F9
        0x67 => 0x44, // F11
        0x69 => 0x68, // F13
        0x6A => 0x6B, // F16
        0x6B => 0x69, // F14
        0x6D => 0x43, // F10
        0x6F => 0x45, // F12
        0x71 => 0x6A, // F15
        0x72 => 0x49, // help (insert)
        0x73 => 0x4A, // home
        0x74 => 0x4B, // page up
        0x75 => 0x4C, // forward delete
        0x76 => 0x3D, // F4
        0x77 => 0x4D, // end
        0x78 => 0x3B, // F2
        0x79 => 0x4E, // page down
        0x7A => 0x3A, // F1
        0x7B => 0x50, // left
        0x7C => 0x4F, // right
        0x7D => 0x51, // down
        0x7E => 0x52, // up
        _ => return None,
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    fn key(keycode: u16, down: bool) -> MacTapInput {
        MacTapInput {
            kind: if down { KEY_DOWN } else { KEY_UP },
            keycode,
            ..MacTapInput::default()
        }
    }

    fn flags(keycode: u16, bits: u64) -> MacTapInput {
        MacTapInput {
            kind: FLAGS,
            keycode,
            flags: bits,
            ..MacTapInput::default()
        }
    }

    #[test]
    fn maps_letters_modifiers_and_navigation() {
        assert_eq!(macos_keycode_to_hid(0x00), Some(0x04));
        assert_eq!(macos_keycode_to_hid(0x0C), Some(0x14));
        assert_eq!(macos_keycode_to_hid(0x06), Some(0x1D));
        assert_eq!(macos_keycode_to_hid(0x35), Some(0x29));
        assert_eq!(macos_keycode_to_hid(0x3B), Some(0xE0));
        assert_eq!(macos_keycode_to_hid(0x3A), Some(0xE2));
        assert_eq!(macos_keycode_to_hid(0x37), Some(0xE3));
        assert_eq!(macos_keycode_to_hid(0x36), Some(0xE7));
        assert_eq!(macos_keycode_to_hid(0x7A), Some(0x3A));
        assert_eq!(macos_keycode_to_hid(0x7B), Some(0x50));
        assert_eq!(macos_keycode_to_hid(0x7E), Some(0x52));
        assert_eq!(macos_keycode_to_hid(0x4C), Some(0x58));
        assert_eq!(macos_keycode_to_hid(0x75), Some(0x4C));
        assert_eq!(macos_keycode_to_hid(0x0A), Some(0x64));
        assert_eq!(macos_keycode_to_hid(0x3F), None);
    }

    #[test]
    fn suppressing_tap_covers_pointer_keys_and_scroll() {
        assert_eq!(TAP_OPTIONS_SUPPRESS, 0);
        for bit in [1, 2, 3, 4, 5, 6, 7, 10, 11, 12, 22, 25, 26, 27] {
            assert_ne!(HID_EVENT_MASK & (1u64 << bit), 0, "missing event {bit}");
        }
    }

    #[test]
    fn release_chord_is_ctrl_alt_esc() {
        let mut state = MacTapState::default();
        let (_, ctrl) = state.handle(flags(0x3B, 0x0000_0001 | 0x0004_0000));
        let (_, alt) = state.handle(flags(
            0x3A,
            0x0000_0001 | 0x0004_0000 | 0x0000_0020 | 0x0008_0000,
        ));
        let mut esc_key = key(0x35, true);
        esc_key.flags = 0x0000_0001 | 0x0004_0000 | 0x0000_0020 | 0x0008_0000;
        let (_, esc) = state.handle(esc_key);
        assert_eq!(
            ctrl,
            vec![CaptureEvent::Key {
                usage: 0xE0,
                down: true
            }]
        );
        assert_eq!(
            alt,
            vec![CaptureEvent::Key {
                usage: 0xE2,
                down: true
            }]
        );
        assert_eq!(
            esc,
            vec![CaptureEvent::Key {
                usage: 0x29,
                down: true
            }]
        );
    }

    #[test]
    fn modifier_without_side_bit_uses_the_combined_flag() {
        let mut state = MacTapState::default();
        let (_, events) = state.handle(flags(0x3B, 0x0004_0000));
        assert_eq!(
            events,
            vec![CaptureEvent::Key {
                usage: 0xE0,
                down: true
            }]
        );
        let (_, events) = state.handle(flags(0x3E, 0x0000_2000));
        assert_eq!(
            events,
            vec![
                CaptureEvent::Key {
                    usage: 0xE0,
                    down: false
                },
                CaptureEvent::Key {
                    usage: 0xE4,
                    down: true
                }
            ]
        );
    }

    #[test]
    fn caps_lock_toggles_once_per_flag_change() {
        let mut state = MacTapState::default();
        let press = CaptureEvent::Key {
            usage: 0x39,
            down: true,
        };
        let release = CaptureEvent::Key {
            usage: 0x39,
            down: false,
        };
        let (_, on) = state.handle(flags(0x39, 0x0001_0000));
        let (_, held) = state.handle(flags(0x39, 0x0001_0000));
        let (_, off) = state.handle(flags(0x39, 0));
        assert_eq!(on, vec![press.clone(), release.clone()]);
        assert!(held.is_empty());
        assert_eq!(off, vec![press, release]);
    }

    #[test]
    fn a_missed_modifier_release_does_not_swallow_the_next_letter() {
        let mut state = MacTapState::default();
        let _ = state.handle(flags(0x37, 0x0000_0008 | 0x0010_0000));
        let (_, events) = state.handle(key(0x00, true));
        assert_eq!(
            events,
            vec![
                CaptureEvent::Key {
                    usage: 0xE3,
                    down: false
                },
                CaptureEvent::Key {
                    usage: 0x04,
                    down: true
                }
            ]
        );
    }

    #[test]
    fn key_up_is_forwarded_even_if_the_repeat_field_is_set() {
        let mut state = MacTapState::default();
        let _ = state.handle(key(0x00, true));
        let mut up = key(0x00, false);
        up.autorepeat = true;
        let (_, events) = state.handle(up);
        assert_eq!(
            events,
            vec![CaptureEvent::Key {
                usage: 0x04,
                down: false
            }]
        );
    }

    #[test]
    fn autorepeat_and_modifier_key_events_are_swallowed() {
        let mut state = MacTapState::default();
        let mut repeat = key(0x00, true);
        repeat.autorepeat = true;
        let (suppress, events) = state.handle(repeat);
        assert!(suppress);
        assert!(events.is_empty());
        let (suppress, events) = state.handle(key(0x3B, true));
        assert!(suppress);
        assert!(events.is_empty());
        let (suppress, events) = state.handle(key(0x00, true));
        assert!(suppress);
        assert_eq!(
            events,
            vec![CaptureEvent::Key {
                usage: 0x04,
                down: true
            }]
        );
    }

    #[test]
    fn pointer_buttons_use_hid_bits() {
        let mut state = MacTapState::default();
        let (_, left) = state.handle(MacTapInput {
            kind: LEFT_DOWN,
            ..MacTapInput::default()
        });
        let (_, right) = state.handle(MacTapInput {
            kind: RIGHT_DOWN,
            ..MacTapInput::default()
        });
        let (_, middle) = state.handle(MacTapInput {
            kind: OTHER_DOWN,
            button: 2,
            ..MacTapInput::default()
        });
        let (_, up) = state.handle(MacTapInput {
            kind: LEFT_UP,
            ..MacTapInput::default()
        });
        assert_eq!(left, vec![CaptureEvent::Buttons { mask: 0x01 }]);
        assert_eq!(right, vec![CaptureEvent::Buttons { mask: 0x03 }]);
        assert_eq!(middle, vec![CaptureEvent::Buttons { mask: 0x07 }]);
        assert_eq!(up, vec![CaptureEvent::Buttons { mask: 0x06 }]);
    }

    #[test]
    fn pointer_deltas_pass_through_and_zero_motion_is_still_suppressed() {
        let mut state = MacTapState::default();
        let (_, moved) = state.handle(MacTapInput {
            kind: MOVED,
            dx: 5,
            dy: -3,
            ..MacTapInput::default()
        });
        let (suppress, idle) = state.handle(MacTapInput {
            kind: MOVED,
            ..MacTapInput::default()
        });
        let (_, drag) = state.handle(MacTapInput {
            kind: LEFT_DRAG,
            dx: 1,
            dy: 2,
            ..MacTapInput::default()
        });
        assert_eq!(moved, vec![CaptureEvent::Mouse { dx: 5, dy: -3 }]);
        assert!(suppress);
        assert!(idle.is_empty());
        assert_eq!(
            drag,
            vec![
                CaptureEvent::Buttons { mask: 0x01 },
                CaptureEvent::Mouse { dx: 1, dy: 2 }
            ]
        );
    }

    #[test]
    fn wheel_notches_and_trackpad_pixels() {
        let mut state = MacTapState::default();
        let (_, notch) = state.handle(MacTapInput {
            kind: SCROLL,
            line_v: 1,
            line_h: -1,
            ..MacTapInput::default()
        });
        assert_eq!(notch, vec![CaptureEvent::Wheel { v: 1, h: -1 }]);
        let (_, partial) = state.handle(MacTapInput {
            kind: SCROLL,
            continuous: true,
            point_v: 10,
            ..MacTapInput::default()
        });
        assert!(partial.is_empty());
        let (_, step) = state.handle(MacTapInput {
            kind: SCROLL,
            continuous: true,
            point_v: 10,
            point_h: -32,
            ..MacTapInput::default()
        });
        assert_eq!(step, vec![CaptureEvent::Wheel { v: 1, h: -2 }]);
    }

    #[test]
    fn unknown_events_are_not_suppressed() {
        let mut state = MacTapState::default();
        let (suppress, events) = state.handle(MacTapInput {
            kind: 14,
            ..MacTapInput::default()
        });
        assert!(!suppress);
        assert!(events.is_empty());
    }
}
