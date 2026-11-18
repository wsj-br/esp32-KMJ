# AGENTS.md

KMJ turns a LilyGO T-Display-S3 Pro into a USB keyboard/mouse injector, driven over Wi-Fi by a Tauri desktop app. Two components in one repo: PlatformIO firmware (`firmware/`) and a Rust workspace (`desktop/`). Run everything from the repo root.

Details live in [`dev/DEVEL.md`](dev/DEVEL.md) (build, hardware checks, release) and [`dev/proto/PROTOCOL.md`](dev/proto/PROTOCOL.md) (wire format). Pins, USB mode and flashing modes are in [`.cursor/rules/t-display-s3.mdc`](.cursor/rules/t-display-s3.mdc). Read those instead of guessing, and do not copy them here.

## Validation: what to run before finishing

CI runs only when a GitHub release is published, never on pushes or PRs. Check locally.

| Change | Run |
| --- | --- |
| `firmware/src/logic/**` (platform-independent) | `pio test -e native -d firmware` |
| Any firmware change | `pio run -e kmj -d firmware` (the build CI uses) |
| `desktop/kmj-proto`, `kmj-capture`, `kmj-client` | `cargo test --manifest-path desktop/Cargo.toml --workspace --exclude kmj-app` |
| `desktop/app` (Tauri) | `./scripts/build-desktop.sh` |
| Protocol or handshake | Both firmware and desktop checks above, and keep `dev/proto/PROTOCOL.md` and the vectors in sync |

Use the host tests first. They need no board. Add a Unity test in `firmware/test/test_logic` for new logic. Do not move code out of `src/logic` if it needs to stay host-testable: the `native` env compiles only `src/logic/*.cpp`.

## Hardware: ask first

- Never run `scripts/flash-firmware.sh` or `pio run ... -t upload` unless the user asks. `kmj-debug` flashing reboots the board into the bootloader, and `kmj` needs manual BOOT/RESET.
- Never reset, monitor or open `/dev/ttyACM0` unprompted.
- `kmj` has no serial port at runtime. Use `kmj-debug` (`KMJ_DEBUG_LOG=1`) for logging.
- Hardware verification steps are in `dev/DEVEL.md` under "Hardware checks". Say that you did not run them rather than claiming a pass.

## Firmware (`firmware/`)

- Layers: `hal/` drivers, `port/` LVGL glue, `ui/` LVGL 9 screens, `app/` runtime, Wi-Fi, BLE, KM server and settings, `logic/` pure host-testable code (crypto, session, link, jiggler, keys, Improv RPC).
- The board id `lilygo-t-display-s3` is the non-Pro variant. Its pin macros are wrong for this board. Use `firmware/include/pins.hpp` and `src/hal/display_lgfx.cpp`.
- Keep `ARDUINO_USB_MODE=0` in both envs. Do not restore the board manifest's mode 1.
- Memory is tight: 8 MB OPI PSRAM, NimBLE allocations are wrapped through `tools/patch_nimble.py` and `-Wl,--wrap` flags. Do not remove those flags.
- Touch and the PMU share one I2C bus. Take the lock in `hal/i2c_lock`. The SD card shares the TFT SPI bus, so keep its CS (GPIO14) high.
- Do not edit vendored code: `firmware/lib/monocypher` and `firmware/lib/SensorLib`. `scripts/upgrade-dependencies.sh` updates them.
- `firmware/compile_commands.json`, `.pio/` and `.cache/` are generated. Do not edit them.
- There is no desktop UI simulator. UI changes can only be verified on the device.
- Safety property: the firmware releases all keys and buttons within 3 s of losing the desktop. Do not weaken it.

## Desktop (`desktop/`)

- Crates: `kmj-proto` (handshake and framing), `kmj-capture` (OS input grab: Linux X11 and evdev, Windows hooks, macOS incomplete), `kmj-client` (discovery, pairing, capture state machine), `app` (Tauri window and commands; UI in `app/ui`).
- `kmj-app` is excluded from `cargo test`. It needs Tauri system libraries (listed in `dev/DEVEL.md`).
- The release chord (Ctrl+Alt+Esc by default) must never be forwarded to the board. The client sends `RELEASE_ALL` before dropping the grab.
- Capture changes can compile on every OS without being correct. Only a person on that OS can verify them. Say so.
- `~/.config/kmj/client.json` holds identity and device keys. Never read, print, delete or commit it.
- `desktop/target/`, `desktop/app/src-tauri/target/` and `desktop/app/node_modules/` are huge and generated. Exclude them from searches.

## Protocol

- `dev/proto/vectors/handshake.txt` holds locked test vectors. Do not change them to make a test pass. A change there means a wire-format change, so bump the protocol version and update `PROTOCOL.md`.

## Conventions

- Add a changelog entry at the top of `## [Unreleased]` in `dev/CHANGELOG.md`: `- **Added|Changed|Fixed|Removed**: <scope> — <description>`.
- Docs are plain and factual. Put development notes in `dev/DEVEL.md`, and keep `README.md` to setup and use.
- Versions in `desktop/Cargo.toml`, `desktop/app/src-tauri/tauri.conf.json` and `firmware/include/pins.hpp` must match (`MAJOR.MINOR.PATCH`). `scripts/version.sh` prints that version, or sets it everywhere it is recorded. Release notes go in `release-notes/RELEASE_NOTES_<version>.md`.
- Do not run `scripts/release.sh` without `--dry-run` unless the user asks. It deletes and recreates the version tag and publishes a GitHub release.
- Helper scripts under `scripts/` are Bash. Reuse them instead of inventing new build commands.
