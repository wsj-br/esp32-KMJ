# KMJ - Development

How to build, run, and verify the KMJ firmware and desktop client. See the [protocol reference](proto/PROTOCOL.md) for the wire format and the [changelog](CHANGELOG.md) for notable changes.

Run commands from the repository root unless noted otherwise. The helper scripts require Bash.

## Prerequisites

- [PlatformIO Core](https://docs.platformio.org/en/latest/core/installation/index.html) 6.2.0 or newer for firmware builds and tests. The espressif32 platform in `firmware/platformio.ini` rejects older cores with `IncompatiblePlatform`.
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

### macOS

The Xcode Command Line Tools provide the C/C++ compiler for `pio test -e native` and the SDK the desktop app links against. WebKit ships with macOS. Install the tools, then rustup with Homebrew or MacPorts. `rustup default stable` (or `rustup-init -y`) installs a current stable toolchain, which satisfies the Rust 1.80 requirement. Firmware builds also need PlatformIO Core 6.2.0 or newer. Confirm with `pio --version`. Core 6.1.19 downloads the espressif32 platform and then deletes it with `IncompatiblePlatform`.

```bash
xcode-select --install
```

#### Homebrew

```bash
brew install platformio rustup
export PATH="$(brew --prefix rustup)/bin:$PATH"
rustup default stable
```

The `rustup` formula is keg-only, so that `export` has to remain on `PATH`. Add it to the shell startup file, then open a new terminal before running `cargo`.

A current Homebrew `platformio` formula is 6.2.0 or newer. If `pio --version` still reports 6.1.x, the installed bottle is older:

```bash
brew update
brew upgrade platformio
```

#### MacPorts

```bash
sudo port install rustup python314
rustup-init -y
```

`rustup-init` installs the stable toolchain and records `~/.cargo/bin` in the shell startup file. The shell that ran `rustup-init` does not see `cargo` until it loads that file. Open a new terminal, or run this in the current one:

```bash
. "$HOME/.cargo/env"
```

`./scripts/build-desktop.sh` and `./scripts/run-desktop.sh` need `cargo` on `PATH`. Without it they stop with `cargo is not on PATH`.

Do not install the MacPorts `platformio` port. It is 6.1.19, and its `pio upgrade` command refuses to update the core. If that port is already installed, remove it before the installer. Otherwise `/opt/local/bin/pio` stays on `PATH` and is still 6.1.19:

```bash
sudo port uninstall platformio
```

Install PlatformIO Core with the upstream installer, and run that installer with MacPorts Python. `/usr/bin/python3` is Apple's Python 3.9, linked to LibreSSL 2.8.3. urllib3 then prints `NotOpenSSLWarning` on every `pio` command. `/opt/local/bin/python3.14` is linked to OpenSSL 3. Remove `~/.platformio/penv` first so the installer creates it with Python 3.14. A penv left by `/usr/bin/python3` keeps printing the warning. Run the installer outside the repository so `get-platformio.py` is not left in the working tree:

```bash
rm -rf "$HOME/.platformio/penv"
curl -fsSL -o /tmp/get-platformio.py https://raw.githubusercontent.com/platformio/platformio-core-installer/master/get-platformio.py
/opt/local/bin/python3.14 /tmp/get-platformio.py
```

The installer creates `~/.platformio/penv`. Put that `bin` directory on `PATH` ahead of `/opt/local/bin` in the shell startup file:

```bash
export PATH="$HOME/.platformio/penv/bin:$PATH"
```

Open a new terminal. `pio --version` reports 6.2.0 or newer and does not print `NotOpenSSLWarning`.

### Windows

winget installs the Microsoft C++Build Tools, the WebView2 Runtime, rustup, and Python 3.12. The Build Tools package below includes the Desktop development with C++ workload, which supplies the MSVC linker and Windows SDK used to compile the desktop app. WebView2 is already present on Windows 11 and on Windows 10 from version 1803; the install below adds it when it is missing. PlatformIO Core is installed with pip afterwards, which is also how CI installs it. PlatformIO downloads the MinGW toolchain it uses for `pio test -e native` on Windows.

```powershell
winget install --id Microsoft.VisualStudio.2022.BuildTools -e --override "--wait --passive --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
winget install --id Microsoft.EdgeWebView2Runtime -e
winget install --id Rustlang.Rustup -e
winget install --id Python.Python.3.12 -e
```

Open a new terminal so `cargo`, `python`, and `pip` are on `PATH`, then:

```powershell
pip install platformio
```

That installs the current PlatformIO Core release. If `pio --version` reports a core older than 6.2.0, run `pip install --upgrade platformio`.

Keep rustup's MSVC host triple (`x86_64-pc-windows-msvc` or `aarch64-pc-windows-msvc`). Tauri links the desktop app with that toolchain. Confirm it with `rustup show`.

## Layout

| Path | What it is |
| --- | --- |
| [`brand/`](../brand/) | Logo, app icon, and favicon source SVGs. |
| [`firmware/`](../firmware/) | PlatformIO firmware for the LilyGO T-Display-S3 Pro (`kmj`, `kmj-debug`) and the Waveshare ESP32-S3-Touch-LCD-1.85B (`kmj-ws185b`, `kmj-ws185b-debug`). The HID-only envs have no serial port; the `-debug` envs add USB serial. |
| [`desktop/`](../desktop/) | Rust workspace containing the protocol, capture, client, and Tauri app crates. |
| [`dev/proto/PROTOCOL.md`](proto/PROTOCOL.md) | Wire format. Locked vectors are in [`dev/proto/vectors/handshake.txt`](proto/vectors/handshake.txt). |
| [`dev/CHANGELOG.md`](CHANGELOG.md) | Notable changes. Add new entries under `[Unreleased]`. |
| [`scripts/`](../scripts/) | Build, flash, launch, installer, dependency, and release helpers. |
| [`release-notes/`](../release-notes/) | `RELEASE_NOTES_<version>.md` files consumed by `scripts/release.sh`. |

## Helper scripts

The scripts under `scripts/` are documented below. Run them from any directory; each resolves the repository root itself.

| Script | Purpose and defaults |
| --- | --- |
| `build-firmware.sh [env]` | Builds firmware. Envs: `kmj`, `kmj-debug`, `kmj-ws185b`, `kmj-ws185b-debug`. Passes remaining arguments to `pio run`. |
| `flash-firmware.sh [--first-flash] [env]` | Enters the ROM bootloader, builds, and uploads for any env. Defaults to `kmj-debug`; accepts PlatformIO upload arguments such as `--upload-port`. |
| `build-tdisplay-s3.sh [kmj\|kmj-debug]` | Builds for the LilyGO T-Display-S3 Pro. Defaults to `kmj`. |
| `flash-tdisplay-s3.sh [--first-flash] [kmj\|kmj-debug]` | Flashes the LilyGO. Defaults to `kmj-debug`. |
| `build-ws185b.sh [kmj-ws185b\|kmj-ws185b-debug]` | Builds for the Waveshare 1.85B. Defaults to `kmj-ws185b`. |
| `flash-ws185b.sh [--first-flash] [kmj-ws185b\|kmj-ws185b-debug]` | Flashes the Waveshare. Defaults to `kmj-ws185b-debug`; use `--first-flash` while the stock firmware is installed. |
| `build-all-firmware.sh` | Builds the production image for every supported board. |
| `package-firmware.sh <env> <asset-name>` | Packages a built env into a single merged flash image plus `.sha256` in `dist/`, the way the release workflow does. |
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
./scripts/build-tdisplay-s3.sh kmj      # LilyGO build
./scripts/build-ws185b.sh               # Waveshare build
./scripts/flash-tdisplay-s3.sh kmj-debug   # HID + serial; auto-reboots into the bootloader
./scripts/flash-ws185b.sh --first-flash    # Waveshare first upload while stock firmware is installed
```

You can also invoke PlatformIO directly, for example `pio run -e kmj -d firmware`. Pass `--upload-port` or `PLATFORMIO_UPLOAD_PORT` to choose the port. Without one, the flash scripts use a single `/dev/serial/by-id/usb-*` entry (falling back to a single `/dev/ttyACM*`) and refuse to guess when several boards are attached.

The HID-only envs (`kmj`, `kmj-ws185b`) have no serial port while running. Hold **BOOT**, tap **RESET**, release **BOOT**, then upload. The `-debug` envs keep a CDC port at 115200, allowing their flash scripts (through `firmware/tools/bootloader.py`) to enter download mode without button presses. Disconnecting USB also removes that serial port. On the Waveshare, use `--first-flash` until KMJ is installed: the stock firmware's USB-Serial/JTAG port ignores the 1200 baud reboot, so the script builds and then flashes with esptool's default reset instead.

Host tests, which do not need the board:

```bash
pio test -e native -d firmware
```

The `native` environment compiles only the platform-independent logic. The release workflow runs these tests, builds `kmj` and `kmj-ws185b`, and attaches a merged image for each board to the GitHub release. It never flashes a board.

### Hardware constraints: LilyGO T-Display-S3 Pro

The target is the T-Display-S3 **Pro**: ESP32-S3R8, 16 MB flash, 8 MB OPI PSRAM, and a 222×480 ST7796 panel. PlatformIO uses board ID `lilygo-t-display-s3` for the shared module, but the non-Pro variant's pin macros are not valid for this board. The firmware keeps the Pro mappings in `firmware/include/boards/tdisplay_s3.hpp` and `firmware/src/hal/display_lgfx.cpp`.

- TFT SPI: SCK 18, MOSI 17, MISO 8, CS 39, DC 9, reset 47.
- Backlight: GPIO 48.
- SD card: CS 14 on the shared SPI bus; keep it high while unused.
- Touch and SY6970 PMU I2C: SDA 5, SCL 6; touch reset 13 and IRQ 21.
- Buttons: GPIO 0, 12, and 16. GPIO 15 is a camera pin, not panel power.
- USB HID requires `ARDUINO_USB_MODE=0`; do not restore the board manifest's hardware-CDC mode.

### Hardware constraints: Waveshare ESP32-S3-Touch-LCD-1.85B

ESP32-S3R8, 16 MB flash, 8 MB OPI PSRAM, and a 360×360 round ST77916 panel over QSPI. Same pio board ID as the LilyGO; the Waveshare mappings live in `firmware/include/boards/ws185b.hpp`.

- LCD QSPI: CS 21, PCLK 40, data 46/45/42/41, reset 3.
- Backlight: GPIO 5, plain PWM.
- Touch CST816S I2C: SDA 11, SCL 10, reset 1, IRQ 4, address 0x15.
- BQ27220 battery gauge on the same I2C bus at 0x55, next to the unused QMI8658 (0x6B), ES8311 (0x30), ES7210 (0x80), and PCF85063 (0x51).
- Speaker amplifier enable on GPIO 9: held low at boot.
- SD card is on the SDMMC controller (GPIO 12–17), separate from the display bus.
- Buttons: BOOT on GPIO 0 is the home/wake button; the PWR button is a hardware power latch the firmware cannot read. There is no touch home key.
- USB HID requires `ARDUINO_USB_MODE=0` here as well.

## UI development

The panel UI in `firmware/src/ui` uses LVGL 9 and the LovyanGFX flush in `firmware/src/port/lvgl_port.cpp`. Screen construction is per board: `screens_wide.cpp` (LilyGO 480×222) and `screens_round.cpp` (Waveshare 360×360 round, geometry from `firmware/src/logic/layout_round.hpp`, verified against the circle by the native tests). Painting and navigation are shared in `ui.cpp`. There is currently no ready-to-run desktop simulator. A simulator would need LVGL's SDL driver (`LV_USE_SDL`) and a `BoardLink` that supplies pointer events in place of the touch driver. Iterate on the device with the flash scripts, or use `pio test -e native -d firmware` for host logic and layout checks.

## Desktop client

The Rust workspace contains the Tauri application and three support crates:

- `kmj-app` owns the window, commands, settings UI, and capture loop.
- `kmj-proto` implements the handshake in `dev/proto/PROTOCOL.md`.
- `kmj-capture` grabs the local keyboard and pointer.
- `kmj-client` discovers the board, pairs, and runs the capture state machine.

Identity and paired-device keys are stored in `~/.config/kmj/client.json`. Treat this file as sensitive: deleting it creates a new identity and requires pairing again. Window geometry is stored in `~/.config/kmj/window.json` and restored when it still intersects a display.

The window lists discovered `_kmj._tcp` devices, pairs, connects, and captures. The default release chord is Ctrl+Alt+Esc. That chord is never forwarded. The client then sends `RELEASE_ALL` and drops the local grab.

**Scan** browses `_kmj._tcp` over mDNS for 1.5 seconds. That is link-local multicast to `224.0.0.251` on UDP port 5353. QEMU's default user-mode NIC (SLIRP) is NAT and does not forward it, so Scan reports no devices while the board is on the host's LAN. The guest needs a bridged or vmnet-shared NIC on the same link as the board. **Scan networks** uses Bluetooth, which QEMU does not provide unless a controller is passed through to the guest.

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

Build artifacts are written under `desktop/target/debug/` or `desktop/target/release/`. The release workflow tests the support crates and builds installers for Linux amd64 and arm64, macOS Intel and Apple Silicon, and Windows x64. Cross-platform compilation does not validate native input capture; complete each platform's manual checks below.

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

The backend creates a suppressing HID event tap (`kCGHIDEventTap`, `kCGEventTapOptionDefault`) and runs it on a dedicated thread. The callback converts keyboard, modifier, pointer, button, and scroll events into `CaptureEvent` values and returns NULL so the source desktop does not receive them. `poll` drains that queue. Stopping the grab disables the tap, stops the run loop, and releases the port.

Grant Accessibility and Input Monitoring to the KMJ binary that you launch. Quit KMJ and open it again after changing either permission. A `cargo run` binary and an installed `.app` are different programs, so each one needs its own grant. `CGEventTapCreate` fails without them, and Capture shows that error.

CI checks that this backend compiles. Confirm on a Mac that keys, pointer motion, buttons, and scrolling are suppressed locally and arrive on the target, and that the release chord ends the grab.

### Build installers

`scripts/build-installers.sh` wraps Tauri CLI 2 and builds the host OS installers. It installs `tauri-cli`, or updates it to the latest stable 2.x, when the installed copy is missing or older. Extra arguments are forwarded to `cargo tauri build`. The AppImage build sets `DISABLE_COPYRIGHT_FILES_DEPLOYMENT=1`, so linuxdeploy does not run `dpkg-query` once per bundled library. Pass `--copyright-files` to keep that scan.

```bash
./scripts/build-installers.sh
./scripts/build-installers.sh --bundles deb
./scripts/build-installers.sh --copyright-files
./scripts/write-notices.sh
```

`scripts/write-notices.sh` builds the AppImage with `--copyright-files` and `--bundles appimage`, then writes `NOTICES` at the repository root. The file has the AppImage Debian copyright files, the Rust crates from `desktop/Cargo.lock`, and the firmware libraries (PlatformIO libraries, vendored Monocypher, and the Arduino-ESP32 framework). A `-------` line separates each package. That build takes a few minutes. The next default installer build deletes the AppDir and skips the copyright scan again.

Linux needs the WebKitGTK development packages listed in Prerequisites and produces `.deb` / AppImage files. Windows produces an NSIS or MSI bundle. macOS produces a `.dmg`. Bundles land in `desktop/target/release/bundle/`. The release workflow builds the Linux `.deb` and AppImage, the Windows NSIS installer, and the macOS `.dmg`, and attaches those files to the GitHub release. The asset names are listed under [Release](#release).

The taskbar and Dock icons come from `desktop/app/src-tauri/icons/`: Windows embeds `icon.ico` in the executable, macOS uses `icon.icns` in the `.app`, and Linux uses the PNG set plus the `.desktop` entry installed by `scripts/run-desktop.sh`. `cargo run` is not a Tauri dev build (`custom-protocol` is on), so Tauri does not set the Dock icon. The app applies `icon.icns` itself when the event loop is ready.

## Hardware checks

These checks need a supported board, a source computer running the desktop app, and a USB target. CI does not flash hardware. The LilyGO checks below assume the T-Display-S3 Pro; the Waveshare-specific additions follow.

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

### Waveshare 1.85B hardware checks

Same steps as above on a Waveshare ESP32-S3-Touch-LCD-1.85B with `kmj-ws185b` / `kmj-ws185b-debug`, plus the board-specific ones:

1. Flash with `scripts/flash-ws185b.sh --first-flash` while the stock firmware is installed.
2. Display: colours are not inverted or channel-swapped, the image is not mirrored, and nothing is cut off by the round bezel (`invert` / `rgb_order` / rotation in `firmware/src/hal/display_lgfx.cpp`).
3. Touch: taps map to the right on-screen positions across the whole round panel; edges respond.
4. Backlight: all 16 steps are visible and monotonic; polarity is not inverted.
5. BOOT button: short press wakes a blank display, returns home from other screens, and blanks the display on home; a long press blanks it without stopping the function.
6. Battery gauge: Configuration shows a plausible voltage and percentage; with USB power the status shows charging or USB, and without a battery it shows no battery.
7. Wi-Fi setup (Allow → send credentials) and pairing work as on the LilyGO; the board appears in the desktop app as `KMJ-TL-XX`, where XX matches the last byte of its Wi-Fi MAC.

Run once on 2026-11-19 with the `kmj-ws185b-debug` build of 0.1.1: display, orientation, touch mapping (touch answers as `CST816T`), backlight, BOOT wake/home, Wi-Fi setup over BLE, pairing, and capture all passed; the debug heartbeats stayed stable afterwards. The release-style merged `KMJ_<version>_firmware_ws185b.bin` image and the soak step were not run.

### Default device names

The default hostname and device name are per board: `KMJ-TD-XX` on the LilyGO and `KMJ-TL-XX` on the Waveshare, where XX is the last byte of the board's Wi-Fi MAC in uppercase hex (for example MAC `78:ab:bb:9b:7c:9f` gives `KMJ-TD-9F`). The name is recomputed at boot from the fixed MAC and is not stored. A name saved through Configuration → Wi-Fi setup (Improv) takes priority, so rename there if two boards of the same type end up with the same suffix. The mDNS service type stays `_kmj._tcp`.

## Dependency updates

`scripts/upgrade-dependencies.sh` updates Rust dependencies, PlatformIO libraries, vendored Monocypher and SensorLib sources, and GitHub Actions versions. It also updates local Rust and PlatformIO tools unless told not to. HTTPS lookups use IPv4 only, with a short connect timeout on each address. crates.io is queried at about one request per second.

```bash
./scripts/upgrade-dependencies.sh --dry-run
./scripts/upgrade-dependencies.sh
```

Run `./scripts/upgrade-dependencies.sh --help` for component-specific skip flags. If verification fails, the script restores committed dependency files, but it does not roll back tool upgrades.

## Release

`scripts/release.sh` publishes a GitHub release from HEAD. It requires Git, Python 3, an authenticated GitHub CLI, an `origin` remote, and a clean working tree by default.

`./scripts/version.sh` prints the shared `MAJOR.MINOR.PATCH` version. `./scripts/version.sh x.y.z` writes that version to `desktop/Cargo.toml`, `desktop/Cargo.lock`, `desktop/app/src-tauri/tauri.conf.json`, `firmware/include/pins.hpp`, the panel field in `firmware/src/ui/model.hpp`, and the current-version sentence in `dev/CHANGELOG.md`. Each part must be an integer with no leading zeros; 0 is allowed. Notes come from `release-notes/RELEASE_NOTES_<version>.md`. The steps for writing that file and folding `dev/CHANGELOG.md` are in [`release-new-version-prompt.md`](release-new-version-prompt.md). If `v<version>` already exists as a tag or GitHub release, the script deletes it and recreates it at HEAD. Publishing the release is the trigger for `.github/workflows/release.yml`; CI does not run on ordinary pushes or pull requests. The release script itself does not build firmware or the desktop app. After every build succeeds, the workflow attaches the unsigned assets below to that release. Each file has a sibling `.sha256` checksum.

| File | What it is |
| --- | --- |
| `KMJ_<version>_firmware.bin` | Merged `kmj` image for the LilyGO T-Display-S3 Pro. Flash it at offset `0x0`. |
| `KMJ_<version>_firmware_ws185b.bin` | Merged `kmj-ws185b` image for the Waveshare ESP32-S3-Touch-LCD-1.85B. Flash it at offset `0x0`. |
| `KMJ_<version>_amd64.deb` | Linux amd64 Debian package. |
| `KMJ_<version>_amd64.AppImage` | Linux amd64 AppImage. |
| `KMJ_<version>_arm64.deb` | Linux arm64 Debian package. |
| `KMJ_<version>_aarch64.AppImage` | Linux arm64 AppImage. The AppImage arch token is `aarch64`. |
| `KMJ_<version>_x64.dmg` | macOS Intel disk image. |
| `KMJ_<version>_aarch64.dmg` | macOS Apple Silicon disk image. |
| `KMJ_<version>_x64-setup.exe` | Windows x64 NSIS installer. |

```bash
esptool.py --chip esp32s3 write_flash 0x0 KMJ_<version>_firmware.bin
./scripts/release.sh --dry-run
./scripts/release.sh
```
