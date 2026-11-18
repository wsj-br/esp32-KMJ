# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

Use conventional types (**Added**, **Changed**, **Fixed**, **Removed**), a short **scope** (subsystem or UI area), and a clear description.

Add new entries at the top of `## [Unreleased]`. When a version is published, move those entries to `## [x.y.z] - YYYY-MM-DD`. The current version is `0.1.0` and has not been published.

## [Unreleased]

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
- **Added**: desktop — Tauri client that discovers `_kmj._tcp` devices, pairs, and builds on Linux, Windows, and macOS. Linux capture is complete, Windows still needs release validation, and macOS capture remains incomplete. The release chord is Ctrl+Alt+Esc and is never forwarded.
- **Added**: protocol — version 1 handshake and encrypted session, recorded in `dev/proto/PROTOCOL.md`, with locked vectors in `dev/proto/vectors/handshake.txt`.
- **Added**: scripts — `build-firmware.sh`, `flash-firmware.sh`, `build-desktop.sh`, and `run-desktop.sh`. Host logic tests run with `pio test -e native`.
