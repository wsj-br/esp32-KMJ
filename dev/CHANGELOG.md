# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Use conventional types (**Added**, **Changed**, **Fixed**, **Removed**), a short **scope** (subsystem or UI area), and a clear description.

Add new entries at the top of `## [Unreleased]`. When a version is published, move those entries to `## [x.y.z] - YYYY-MM-DD`. The current version is `0.1.1` and has not been published.

## [Unreleased]

- **Added**: firmware — support for the Waveshare ESP32-S3-Touch-LCD-1.85B as a second board: `kmj-ws185b` and `kmj-ws185b-debug` build environments, a round-native 360x360 UI, the ST77916 QSPI panel, CST816S touch, the BQ27220 battery gauge, and the physical BOOT button as the home/wake key. New per-device scripts `build-tdisplay-s3.sh`, `flash-tdisplay-s3.sh`, `build-ws185b.sh`, `flash-ws185b.sh`, and `build-all-firmware.sh`; releases add `KMJ_<version>_firmware_ws185b.bin`.
- **Changed**: firmware — the default hostname and device name are per board with a MAC suffix: `KMJ-TD-XX` on the LilyGO, `KMJ-TL-XX` on the Waveshare, where XX is the last byte of the Wi-Fi MAC in uppercase hex. A board that already saved a name through Wi-Fi setup keeps it; a LilyGO that never saved one changes from `kmj` / `KMJ` to `KMJ-TD-XX`.
- **Fixed**: desktop — the macOS Dock shows the KMJ icon when the app is started with `cargo run`. That build is not a Tauri dev build, so Tauri does not apply `icon.icns` itself.
- **Fixed**: scripts — dependency upgrade connects over IPv4, so a disabled IPv6 interface cannot stall the registry lookups.
- **Fixed**: firmware — touch setup passes the reset pin to the CST226 driver, so the controller is hardware-reset before its check code is read. Without that pin the driver issued a software reset and rejected the chip at 0x5A. An idle status byte of 0x00 is acknowledged instead of being discarded, so later taps are reported.
- **Fixed**: capture — macOS releases a stuck Command or Option before the next letter, and forwards key-up events that Quartz marks as repeats. The board sends each key transition to USB as it arrives, and packs the keyboard report so a released key cannot hide the ones still held.
- **Changed**: desktop — the version and build timestamp in the window footer use 8px type.
- **Changed**: desktop — a board that is on the network but not accepting the KM port asks the user to select KM Emulator, instead of saying the device is not reachable.
- **Fixed**: capture — macOS runs a suppressing HID event tap on a run-loop thread and forwards keyboard, pointer, button, and scroll events.
- **Fixed**: desktop — the chord-key event payload is compiled only on Linux, so other hosts no longer warn that it is never constructed.
- **Changed**: desktop — the device list shows the IP address without the port.
- **Changed**: docs — QEMU's default user-mode NIC does not carry the mDNS multicast that Scan uses, and it has no Bluetooth controller for Scan networks.
- **Fixed**: capture — the macOS grab is `Send`, so `kmj-capture` builds. The event-tap pointer is owned by `MacGrab` and only used through `&mut self`.
- **Fixed**: scripts — `run-desktop.sh` and `build-desktop.sh` no longer fail on macOS Bash 3.2 when launched with no extra Cargo arguments.
- **Changed**: docs — after `rustup-init` on macOS, the current shell loads Cargo with `. "$HOME/.cargo/env"` before the desktop scripts.
- **Changed**: docs — the MacPorts PlatformIO installer removes any existing penv and runs with MacPorts Python 3.14. Apple's `python3` links LibreSSL and prints `NotOpenSSLWarning` on every `pio` command.
- **Changed**: docs — firmware builds require PlatformIO Core 6.2.0 or newer. The MacPorts `platformio` port is 6.1.19 and cannot be used.
- **Added**: docs — Homebrew, MacPorts, and winget commands for the macOS and Windows build dependencies.
- **Changed**: release — `.github/workflows/release.yml` runs when a GitHub release is published. It builds the `kmj` firmware image and desktop installers for Linux amd64 and arm64, macOS Intel and Apple Silicon, and Windows x64, then attaches them to that release.
- **Added**: desktop — the window footer shows the app version and build timestamp, right aligned, in small muted text.
- **Changed**: firmware — the configuration screen shows the firmware version before the build timestamp.
- **Added**: scripts — `scripts/version.sh` prints the shared version, or sets it to an `x.y.z` value.
- **Added**: docs — `dev/release-new-version-prompt.md` is the prompt for writing `release-notes/RELEASE_NOTES_<version>.md` and folding the changelog.
- **Added**: license — MIT license, copyright Waldemar Scudeller Jr.
- **Added**: scripts — `write-notices.sh` builds the AppImage with copyright files, then writes `NOTICES` for those system libraries, the Rust crates, and the firmware libraries.
- **Changed**: scripts — `build-installers.sh` skips linuxdeploy's copyright-file scan when building an AppImage. Pass `--copyright-files` to keep it.
- **Changed**: docs — prerequisites list Tauri CLI 2 for installer builds.
- **Changed**: scripts — `build-installers.sh` installs or updates `tauri-cli` to the latest stable 2.x when the installed copy is missing or older.
- **Fixed**: scripts — `build-installers.sh` no longer passes `--manifest-path` to `cargo tauri build`. Tauri CLI 2 rejects that flag and finds `desktop/app/src-tauri` from the repo root on its own.
- **Added**: scripts — `scripts/build-installers.sh` builds the host OS installers with Tauri CLI 2 and prints the bundle paths.
- **Changed**: docs — development notes live in `dev/DEVEL.md`. The README keeps how to set up and use the board and the desktop app. The protocol is `dev/proto/PROTOCOL.md`.
- **Added**: release — `scripts/release.sh` publishes a GitHub release from the shared version in the desktop crate, the Tauri config, and the firmware. CI runs when that release is published.
- **Changed**: desktop — light, dark, and system themes. The default window is 950×700. Decorations and a restored size stay consistent across launches.
- **Changed**: capture — more key mappings and release-chord detection on Linux and Windows.
- **Fixed**: firmware — a KM session that just came up is no longer closed at once. The idle check used an unsigned subtraction that wrapped when the frame was handled after the poll sample.
- **Changed**: firmware — the last key on the KM screen clears after two seconds. Key and move counts clear when the link drops.
- **Changed**: firmware — the board stays advertised on Wi-Fi in every function. The KM listener, and therefore capture, runs only while KM Emulator is the active function.
- **Added**: brand — logo, app icon, and favicon. On Linux, `scripts/run-desktop.sh` installs the dash icon for `com.kmj.desktop`.
- **Added**: desktop — window position and size are saved and restored on the next launch when that rectangle still hits a display.
- **Added**: firmware — Wi-Fi setup reboots into a Bluetooth-only mode with no UI, HID, or KM server, then returns to the normal firmware when setup ends. A crash, power cycle, or watchdog boots normally.
- **Added**: firmware — `kmj-debug` keeps a serial log and a fault hook. `scripts/flash-firmware.sh kmj-debug` reboots the board into the bootloader. Entering the bootloader shows that state on the panel before the USB restart.
- **Changed**: firmware — the panel shows a build stamp and the device name. The jiggler countdown freezes the time remaining when the function stops.
- **Changed**: desktop — pairing and configuration screens, including finding the board and sending a network.
- **Added**: firmware — KM Emulator, Mouse Jiggler, and Configuration on the LilyGO T-Display-S3 Pro, injecting a USB keyboard and mouse into the computer the board is plugged into.
- **Added**: desktop — Tauri client that discovers `_kmj._tcp` devices, pairs, and builds on Linux, Windows, and macOS. Linux capture is complete. Windows and macOS capture are implemented and still need a check on that OS. The release chord is Ctrl+Alt+Esc and is never forwarded.
- **Added**: protocol — version 1 handshake and encrypted session, recorded in `dev/proto/PROTOCOL.md`, with locked vectors in `dev/proto/vectors/handshake.txt`.
- **Added**: scripts — `build-firmware.sh`, `flash-firmware.sh`, `build-desktop.sh`, and `run-desktop.sh`. Host logic tests run with `pio test -e native`.
