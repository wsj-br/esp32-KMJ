# KMJ

USB keyboard and mouse for the [LilyGO T-Display-S3 Pro](https://wiki.lilygo.cc/products/t-display-series/t-display-s3-pro/), plus a desktop client that forwards a captured keyboard and pointer over the local network.

The panel is an LVGL instrument UI with three functions:

- **KM Emulator** listens for an encrypted TCP session, pairs a desktop, and injects HID reports into the computer the board is plugged into.
- **Mouse Jiggler** is the eased out-and-back motion from `esp32-Mouse-Jiggler`, with the same nudge.
- **Configuration** joins Wi-Fi and shows link, power, and system status.

The red-circle home key turns the backlight on, returns to the home screen, or turns the backlight off. A long press turns the backlight off and leaves the current function running.

## Layout

| Path | What it is |
| --- | --- |
| `firmware/` | PlatformIO firmware. `kmj` is HID only. `kmj-debug` adds USB serial. |
| `desktop/` | Rust workspace: protocol, capture, client, and the Tauri app. |
| `proto/PROTOCOL.md` | The byte layout both sides implement. |

## Firmware

From the repository root, or with `pio` directly inside `firmware/`:

```bash
./scripts/flash-firmware.sh kmj        # HID-only build (hold BOOT, tap RESET, release BOOT)
./scripts/flash-firmware.sh kmj-debug  # HID + serial; auto-reboots into the bootloader
```

`kmj` has no serial port while it is running. Hold **BOOT**, tap **RESET**, release **BOOT**, then upload. `kmj-debug` keeps a CDC port at 115200 so `scripts/flash-firmware.sh kmj-debug` (via `firmware/tools/bootloader.py`) can reboot the board into download mode automatically without button presses. Disconnecting USB also drops that port.

Host tests, which do not need the board:

```bash
pio test -e native -d firmware
```

Pins, USB-OTG, and the reason the non-Pro variant macros must not be used are in `.cursor/rules/t-display-s3.mdc`.

Wi-Fi setup starts on the device: Configuration, **Wi-Fi setup**, then **Allow**. In the desktop app, choose the device, enter the network, and send it. Bluetooth turns off once the board has joined, so it does not stay up during a KM session. Pairing is armed from KM Emulator. The device and desktop each show a six-digit code. Confirming the match on the device completes pairing automatically in the desktop app. The protocol is in `proto/PROTOCOL.md`.

## Desktop

Rust 1.80 or newer. On Linux, X11 uses a pointer and keyboard grab. A Wayland session uses an evdev grab, which needs permission to open `/dev/input` (the `input` group or a udev rule). macOS needs Accessibility and Input Monitoring. Details are in `docs/desktop.md`.

```bash
cargo test --manifest-path desktop/Cargo.toml --workspace --exclude kmj-app
cargo build --manifest-path desktop/Cargo.toml -p kmj-app
```

The window lists discovered `_kmj._tcp` devices, pairs, connects, and captures. The default release chord is Ctrl+Alt+Esc. That chord is never forwarded. The client then sends `RELEASE_ALL` and drops the local grab.

## Hardware checks

`docs/hardware-test.md` is the enumeration, stuck-key, latency, and soak procedure. Those checks need the board and a host of each OS. They are not run by CI.
