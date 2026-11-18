
# KMJ - Development

How to build, run, and verify the KMJ firmware and desktop client. See the [protocol reference](proto/PROTOCOL.md) for the wire format and the [changelog](CHANGELOG.md) for notable changes.

Run commands from the repository root unless noted otherwise. The helper scripts require Bash.

## Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) for firmware builds and tests.
- Rust 1.80 or newer for the desktop workspace.
- A C/C++ toolchain for PlatformIO's `native` test environment.
- Platform-specific [Tauri prerequisites](https://v2.tauri.app/start/prerequisites/) when building the desktop app.
- Tauri CLI 2 for installer builds: `cargo install tauri-cli --version "^2"`. `scripts/build-installers.sh` installs or updates it to the latest stable 2.x when the installed copy is missing or older.

On Debian or Ubuntu, the packages used by CI are:

```bash
sudo apt-get install pkg-config libx11-dev libevdev-dev libdbus-1-dev \
  libwebkit2gtk-4.1-dev libgtk-3-dev libayatana-appindicator3-dev \
  librsvg2-dev patchelf
```

## Layout

| Path | What it is |
| --- | --- |
| [`brand/`](../brand/) | Logo, app icon, and favicon source SVGs. |
| [`firmware/`](../firmware/) | PlatformIO firmware. `kmj` is HID only; `kmj-debug` adds USB serial. |
| [`desktop/`](../desktop/) | Rust workspace containing the protocol, capture, client, and Tauri app crates. |
| [`dev/proto/PROTOCOL.md`](proto/PROTOCOL.md) | Wire format. Locked vectors are in [`dev/proto/vectors/handshake.txt`](proto/vectors/handshake.txt). |
| [`dev/CHANGELOG.md`](CHANGELOG.md) | Notable changes. Add new entries under `[Unreleased]`. |
| [`scripts/`](../scripts/) | Build, flash, launch, installer, dependency, and release helpers. |
| [`release-notes/`](../release-notes/) | `RELEASE_NOTES_<version>.md` files consumed by `scripts/release.sh`. |

## Helper scripts

The scripts under `scripts/` are documented below. Run them from any directory; each resolves the repository root itself.

| Script | Purpose and defaults |
| --- | --- |
| `build-firmware.sh [kmj\|kmj-debug]` | Builds firmware. Defaults to the HID-only `kmj` environment and passes remaining arguments to `pio run`. |
| `flash-firmware.sh [kmj\|kmj-debug]` | Enters the ROM bootloader, builds, and uploads. Defaults to `kmj-debug`; accepts PlatformIO upload arguments such as `--upload-port`. |
| `build-desktop.sh [--release]` | Builds `kmj-app` in debug mode by default. Remaining arguments are passed to `cargo build`. |
| `run-desktop.sh [--release]` | Builds and runs `kmj-app`. On Linux it also installs the user-level launcher and icons. |
| `build-installers.sh` | Builds native installers with Tauri CLI 2. Installs or updates `tauri-cli` to the latest stable 2.x when the installed copy is missing or older. The AppImage build skips linuxdeploy's copyright-file scan unless `--copyright-files` is passed. Extra arguments go to `cargo tauri build`. |
| `write-notices.sh` | Builds the AppImage with copyright files, then writes `NOTICES` for those system libraries, the Rust crates, and the firmware libraries. |
| `upgrade-dependencies.sh` | Updates tools and project dependencies, then verifies affected components. Use `--dry-run` or `--help` before applying changes. |
| `version.sh [x.y.z]` | Prints the shared version. With `x.y.z`, writes that version to the desktop crate, the lockfile, the Tauri config, the firmware, the panel field, and the changelog's current-version sentence. Each part must be an integer with no leading zeros; 0 is allowed. |
| `release.sh` | Recreates the version tag at HEAD and publishes a GitHub release. Use `--dry-run` first; `--help` lists clean-tree options. |

## Firmware

Build or flash from the repository root:

```bash
./scripts/build-firmware.sh kmj
./scripts/flash-firmware.sh kmj        # HID-only build (hold BOOT, tap RESET, release BOOT)
./scripts/flash-firmware.sh kmj-debug  # HID + serial; auto-reboots into the bootloader
```

You can also invoke PlatformIO directly, for example `pio run -e kmj -d firmware`. Override the default `/dev/ttyACM0` upload port with `--upload-port` or `PLATFORMIO_UPLOAD_PORT`.

`kmj` has no serial port while running. Hold **BOOT**, tap **RESET**, release **BOOT**, then upload. `kmj-debug` keeps a CDC port at 115200, allowing `scripts/flash-firmware.sh kmj-debug` (through `firmware/tools/bootloader.py`) to enter download mode without button presses. Disconnecting USB also removes that serial port.

Host tests, which do not need the board:

```bash
pio test -e native -d firmware
```

The `native` environment compiles only the platform-independent logic. CI builds `kmj` and runs these tests, but it never flashes a board.

### Hardware constraints

The target is the T-Display-S3 **Pro**: ESP32-S3R8, 16 MB flash, 8 MB OPI PSRAM, and a 222×480 ST7796 panel. PlatformIO uses board ID `lilygo-t-display-s3` for the shared module, but the non-Pro variant's pin macros are not valid for this board. The firmware keeps the Pro mappings in `firmware/include/pins.hpp` and `firmware/src/hal/display_lgfx.cpp`.

- TFT SPI: SCK 18, MOSI 17, MISO 8, CS 39, DC 9, reset 47.
- Backlight: GPIO 48.
- SD card: CS 14 on the shared SPI bus; keep it high while unused.
- Touch and SY6970 PMU I2C: SDA 5, SCL 6; touch reset 13 and IRQ 21.
- Buttons: GPIO 0, 12, and 16. GPIO 15 is a camera pin, not panel power.
- USB HID requires `ARDUINO_USB_MODE=0`; do not restore the board manifest's hardware-CDC mode.

## UI development

The panel UI in `firmware/src/ui` uses LVGL 9 and the LovyanGFX flush in `firmware/src/port/lvgl_port.cpp`. There is currently no ready-to-run desktop simulator. A simulator would need LVGL's SDL driver (`LV_USE_SDL`) and a `BoardLink` that supplies pointer events in place of the CST226. Iterate on the device with `pio run -e kmj -d firmware -t upload`, or use `pio test -e native -d firmware` for host logic.

## Desktop client

The Rust workspace contains the Tauri application and three support crates:

- `kmj-app` owns the window, commands, settings UI, and capture loop.
- `kmj-proto` implements the handshake in `dev/proto/PROTOCOL.md`.
- `kmj-capture` grabs the local keyboard and pointer.
- `kmj-client` discovers the board, pairs, and runs the capture state machine.

Identity and paired-device keys are stored in `~/.config/kmj/client.json`. Treat this file as sensitive: deleting it creates a new identity and requires pairing again. Window geometry is stored in `~/.config/kmj/window.json` and restored when it still intersects a display.

The window lists discovered `_kmj._tcp` devices, pairs, connects, and captures. The default release chord is Ctrl+Alt+Esc. That chord is never forwarded. The client then sends `RELEASE_ALL` and drops the local grab.

The app's **Unpair** action removes the device key from `client.json`; it does not remove the desktop key stored on the board. The current device UI does not expose `RemovePeer`, so **Unpair** is not a complete board-side revocation mechanism.

```bash
cargo test --manifest-path desktop/Cargo.toml --workspace --exclude kmj-app
./scripts/build-desktop.sh
./scripts/run-desktop.sh
./scripts/run-desktop.sh --release
```

For a cross-platform command that does not depend on Bash:

```bash
cargo run --manifest-path desktop/Cargo.toml -p kmj-app
```

Build artifacts are written under `desktop/target/debug/` or `desktop/target/release/`. CI tests the support crates and compiles the app on Linux, Windows, and macOS. Cross-platform compilation does not validate native input capture; complete each platform's manual checks below.

### Wi-Fi setup

On the device, open Configuration and tap **Wi-Fi setup**, then **Allow**. In the app, **Find device**, pick the board, and **Send** the network name and password. **Networks** asks the board which access points it can see. Provisioning uses the host's Bluetooth adapter. macOS asks for Bluetooth permission the first time.

### Capture

On a complete capture backend, local key and pointer events are suppressed and forwarded. Ctrl+Alt+Esc (or the chord saved in Settings) is detected locally, not sent, and ends the grab. The client sends `RELEASE_ALL` first, so a stuck modifier on the target is cleared. If the process dies, the operating system removes the hook or grab.

#### Linux

On GNOME, the dash icon comes from a `.desktop` file whose name matches the GTK application id `com.kmj.desktop`. `scripts/run-desktop.sh` installs that entry (and the icon) under `~/.local/share`. Close the running window and start it again from the script; the GTK id is single-instance, so a second launch activates the first.

On X11 the client grabs the keyboard and pointer with X11 and reads motion from `MotionNotify`. The pointer is warped back toward the center of the root window if it reaches the edge, so the relative stream does not stall.

On Wayland (`WAYLAND_DISPLAY` is set) the client opens event devices and takes `EVIOCGRAB`. That needs read/write access to `/dev/input/event*`. Membership of the `input` group is the usual way:

```bash
sudo usermod -aG input "$USER"
```

Log out and back in. A portal or libei grab is not implemented. If no device can be grabbed, Capture returns a permission error instead of forwarding events that the local desktop still receives.

#### Windows

The backend installs `WH_KEYBOARD_LL` and `WH_MOUSE_LL` and returns a non-zero value from each hook procedure so handled events are swallowed. Relative motion is calculated from successive low-level mouse coordinates. CI checks that this backend compiles; verify hook delivery, suppression, release-chord handling, and pointer deltas on a Windows host before release.

#### macOS

The macOS capture backend is not yet complete. It creates a suppressing HID event tap, but it does not attach that tap to a run loop or enqueue keyboard and pointer events for forwarding. CI currently verifies compilation only.

Finishing this backend requires an active event-tap run loop, conversion of `CGEvent` values to `CaptureEvent`, and clean teardown. The app will also require Accessibility and Input Monitoring permission; quit and reopen KMJ after granting them.

### Build installers

`scripts/build-installers.sh` wraps Tauri CLI 2 and builds the host OS installers. It installs `tauri-cli`, or updates it to the latest stable 2.x, when the installed copy is missing or older. Extra arguments are forwarded to `cargo tauri build`. The AppImage build sets `DISABLE_COPYRIGHT_FILES_DEPLOYMENT=1`, so linuxdeploy does not run `dpkg-query` once per bundled library. Pass `--copyright-files` to keep that scan.

```bash
./scripts/build-installers.sh
./scripts/build-installers.sh --bundles deb
./scripts/build-installers.sh --copyright-files
./scripts/write-notices.sh
```

`scripts/write-notices.sh` builds the AppImage with `--copyright-files` and `--bundles appimage`, then writes `NOTICES` at the repository root. The file has the AppImage Debian copyright files, the Rust crates from `desktop/Cargo.lock`, and the firmware libraries (PlatformIO libraries, vendored Monocypher, and the Arduino-ESP32 framework). A `-------` line separates each package. That build takes a few minutes. The next default installer build deletes the AppDir and skips the copyright scan again.

Linux needs the WebKitGTK development packages listed in Prerequisites and produces `.deb` / AppImage files. Windows produces an NSIS or MSI bundle. macOS produces a `.dmg`. Bundles land in `desktop/target/release/bundle/`.

The taskbar and Dock icons come from `desktop/app/src-tauri/icons/`: Windows embeds `icon.ico` in the executable, macOS uses `icon.icns` in the `.app` (and `cargo run` applies it to the Dock), and Linux uses the PNG set plus the `.desktop` entry installed by `scripts/run-desktop.sh`.

## Hardware checks

These checks need a LilyGO T-Display-S3 Pro, a source computer running the desktop app, and a USB target. CI does not flash hardware.

1. Build `firmware` with `pio run -e kmj` and flash it. On Windows, Linux, and macOS, the board enumerates as a composite HID keyboard and mouse named `KMJ Composite`, VID `413C`, PID `250E`.
2. From Configuration, tap Wi-Fi setup and Allow. In the desktop app, find the device and send a network. The status strip shows the SSID, RSSI, and IP. Forget Wi-Fi and confirm the strip returns to `Wi-Fi off`.
3. On KM Emulator, tap Pair. Pair the desktop app. The six-digit codes match. Cancelling on either side does not store a peer. A second computer that has not paired is rejected. The KM screen shows whether a desktop is stored.
4. Connect and capture. Type, move, click, and scroll on the desktop. The target computer receives them. The KM screen counts keys and moves and shows the last key.
5. Press the configured release chord (Ctrl+Alt+Esc by default). The desktop overlay clears, the target receives no further keys, and no modifier stays down.
6. Kill the desktop process while a key is held. Within 3 seconds the firmware releases every key and button.
7. Unplug Wi-Fi or walk the desktop off the LAN with a key held. The same 3 second failsafe releases HID.
8. Leave KM for the jiggler. The pointer eases out and back on one axis and returns to its start. Nudge draws the 40 px loop. USB disconnect stops reports and reconnect resumes them. The 8 hour limit stops unattended jiggling.
9. Home key: from KM, Jiggler, or Configuration it returns home and stops that function. On home it blanks the backlight. A press while blank wakes the same screen. A long press blanks the backlight without stopping the function.
10. Soak: leave a connected KM session idle for an hour with Wi-Fi setup not running. The link stays up and the heap reading on Configuration does not collapse.

Full-screen LVGL transitions are limited by the 40 MHz SPI panel; partial status-line updates should remain responsive.

Record both host operating systems and whether steps 5 to 7 left any key down.

## Dependency updates

`scripts/upgrade-dependencies.sh` updates Rust dependencies, PlatformIO libraries, vendored Monocypher and SensorLib sources, and GitHub Actions versions. It also updates local Rust and PlatformIO tools unless told not to.

```bash
./scripts/upgrade-dependencies.sh --dry-run
./scripts/upgrade-dependencies.sh
```

Run `./scripts/upgrade-dependencies.sh --help` for component-specific skip flags. If verification fails, the script restores committed dependency files, but it does not roll back tool upgrades.

## Release

`scripts/release.sh` publishes a GitHub release from HEAD. It requires Git, Python 3, an authenticated GitHub CLI, an `origin` remote, and a clean working tree by default.

`./scripts/version.sh` prints the shared `MAJOR.MINOR.PATCH` version. `./scripts/version.sh x.y.z` writes that version to `desktop/Cargo.toml`, `desktop/Cargo.lock`, `desktop/app/src-tauri/tauri.conf.json`, `firmware/include/pins.hpp`, the panel field in `firmware/src/ui/model.hpp`, and the current-version sentence in `dev/CHANGELOG.md`. Each part must be an integer with no leading zeros; 0 is allowed. Notes come from `release-notes/RELEASE_NOTES_<version>.md`. The steps for writing that file and folding `dev/CHANGELOG.md` are in [`release-new-version-prompt.md`](release-new-version-prompt.md). If `v<version>` already exists as a tag or GitHub release, the script deletes it and recreates it at HEAD. Publishing the release is the trigger for `.github/workflows/ci.yml`; CI does not run on ordinary pushes or pull requests. The release script itself does not build firmware or the desktop app.

```bash
./scripts/release.sh --dry-run
./scripts/release.sh
```
