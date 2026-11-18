# Desktop client

The app is a Tauri window over three Rust crates:

- `kmj-proto` implements the handshake in `proto/PROTOCOL.md`.
- `kmj-capture` grabs the local keyboard and pointer.
- `kmj-client` discovers the board, pairs, and runs the capture state machine.

Identity and paired device keys are stored in `~/.config/kmj/client.json`. Window position and size are stored in `~/.config/kmj/window.json` and restored on the next launch if that rectangle still hits a display.

## Wi-Fi setup

On the device, open Configuration and tap **Wi-Fi setup**, then **Allow**. In the app, **Find device**, pick the board, and **Send** the network name and password. **Networks** asks the board which access points it can see. Linux uses the system Bluetooth adapter. macOS asks for Bluetooth permission the first time.

## Capture

While Capture is on, local key and pointer events are suppressed and forwarded. Ctrl+Alt+Esc (or the chord saved in the window) is detected locally, not sent, and ends the grab. The client sends `RELEASE_ALL` first, so a stuck modifier on the target is cleared. If the process dies, the operating system removes the hook or grab.

### Linux

On GNOME, the dash icon comes from a `.desktop` file whose name matches the GTK application id `com.kmj.desktop`. `scripts/run-desktop.sh` installs that entry (and the icon) under `~/.local/share`. Close the running window and start it again from the script; the GTK id is single-instance, so a second launch activates the first.

On X11 the client grabs the keyboard and pointer with X11 and reads motion from `MotionNotify`. The pointer is warped back toward the center of the root window if it reaches the edge, so the relative stream does not stall.

On Wayland (`WAYLAND_DISPLAY` is set) the client opens event devices and takes `EVIOCGRAB`. That needs read/write access to `/dev/input/event*`. Membership of the `input` group is the usual way:

```bash
sudo usermod -aG input "$USER"
```

Log out and back in. A portal or libei grab is not implemented. If no device can be grabbed, Capture returns a permission error instead of forwarding events that the local desktop still receives.

### Windows

The capture thread installs `WH_KEYBOARD_LL` and `WH_MOUSE_LL` and returns a non-zero value from the hook procedure so the event is swallowed. Relative motion is the delta in the low-level mouse hook structure. The hook thread must keep pumping messages. That happens on the capture thread started by the app.

### macOS

`CGEventTapCreate` at the HID tap suppresses events by returning NULL from the callback. The first launch fails until Accessibility and Input Monitoring are granted in System Settings. Quit and reopen KMJ after granting them. The cursor is not drawn on the target. The local cursor stays where the tap left it.

## Build installers

After `cargo install tauri-cli --version "^2"`:

```bash
cargo tauri build --manifest-path desktop/app/src-tauri/Cargo.toml
```

Linux needs the WebKitGTK development packages. Windows produces an NSIS or MSI bundle. macOS produces a `.dmg`. CI compiles the app on all three runners with `cargo build -p kmj-app`.

The taskbar and Dock icons come from `desktop/app/src-tauri/icons/`: Windows embeds `icon.ico` in the executable, macOS uses `icon.icns` in the `.app` (and `cargo run` applies it to the Dock), and Linux uses the PNG set plus the `.desktop` entry installed by `scripts/run-desktop.sh`.
