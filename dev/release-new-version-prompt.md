Create a new release notes file `release-notes/RELEASE_NOTES_<x.y.z>.md` for **KMJ** using the instructions below. This file is used by [`scripts/release.sh`](https://github.com/wsj-br/esp32-KMJ/blob/main/scripts/release.sh). Publishing the GitHub release is what starts [`.github/workflows/ci.yml`](https://github.com/wsj-br/esp32-KMJ/blob/main/.github/workflows/ci.yml): firmware host tests, a `kmj` firmware build, desktop crate tests, and a release build of the Tauri app on Linux, Windows, and macOS. CI does not flash a board and does not upload firmware binaries or desktop installers.

**Before you start:** run the checks CI runs. CI does not run on pushes or pull requests, so these are the pre-release checks:

1. `pio test -e native -d firmware`
2. `pio run -e kmj -d firmware`
3. `cargo test --manifest-path desktop/Cargo.toml --workspace --exclude kmj-app`
4. `cargo build --release -p kmj-app --manifest-path desktop/Cargo.toml`

Fix any failures. `./scripts/build-desktop.sh` is the debug app build; the release command above is what CI runs. Optionally run `./scripts/build-installers.sh` on your machine for a host-OS installer smoke test (slow; CI does not run it). Do not flash, monitor, or open the board serial port. Hardware checks in [`dev/DEVEL.md`](https://github.com/wsj-br/esp32-KMJ/blob/main/dev/DEVEL.md) need a person and a board; do not claim they passed.

**Instructions:**

1. **Read the target version (`x.y.z`)** with `./scripts/version.sh`. It prints the shared `MAJOR.MINOR.PATCH` from `desktop/Cargo.toml`, `desktop/Cargo.lock`, `desktop/app/src-tauri/tauri.conf.json`, `firmware/include/pins.hpp`, the panel field in `firmware/src/ui/model.hpp`, and the current-version sentence in `dev/CHANGELOG.md`.
   If you are cutting the release, the version should already be bumped on your branch. If the user asked you to bump it, run `./scripts/version.sh x.y.z`. The script rejects a value that is not `x.y.z` with each part an integer and no leading zeros (`0` is allowed). Do not edit those version strings by hand.
2. **Open `dev/CHANGELOG.md`.**
3. **Copy all entries under** `## [Unreleased]` up to (but not including) the next `## [` heading (the previous shipped version). Entries use `- **Added|Changed|Fixed|Removed**: <scope> — <description>`.
4. **Format the new file** to match the newest existing notes under [`release-notes/`](https://github.com/wsj-br/esp32-KMJ/tree/main/release-notes/). There is no prior file until the first release; use the example below until one exists.
   - Title: `# KMJ <version> - Release Notes`
   - **Release date:** `YYYY-MM-DD` (use the authoritative “today” from context when running this task)
   - Short opening paragraph summarizing the themes of the release (user-focused: the board firmware and the desktop app).
   - Horizontal rule (`---`), then `## Changes`
     - First line: point readers to the full changelog for this version, e.g. [CHANGELOG.md](https://github.com/wsj-br/esp32-KMJ/blob/main/dev/CHANGELOG.md) — section **`[x.y.z] - YYYY-MM-DD`**.
     - Group content into collapsible `<details>` blocks (not top-level `##` headings). Use these summaries when content exists:
       - **Highlights** — Most important user-visible changes (features, fixes, polish); not every changelog bullet verbatim.
       - **Improvements** — Smaller polish, tooling, packaging, CI, and quality-of-life items.
       - **Fixes** — Bug fixes.
       - **Documentation** — Links to README and relevant `dev/` docs (see below).
     - Smaller releases may omit empty `<details>` blocks (e.g. no separate Improvements) but keep the same overall shape.
   - State capture status plainly when this release touches it: Linux capture is complete, Windows still needs release validation, and the macOS capture backend is incomplete. Do not describe macOS capture as working.
   - Do not weaken or misstate the safety properties: the release chord (Ctrl+Alt+Esc by default) is never forwarded, and the firmware releases all keys and buttons within 3 seconds of losing the desktop.
   - `## License` — KMJ is under the **MIT License**; copyright line and link to [`LICENSE`](https://github.com/wsj-br/esp32-KMJ/blob/main/LICENSE) as in the example.
     - Under License, include a collapsible **Disclaimer** `<details>` with the product-names disclaimer in the example, plus a link to [`NOTICES`](https://github.com/wsj-br/esp32-KMJ/blob/main/NOTICES).
   - `## Downloads` — This release does not attach build artifacts. Tell readers to build from the tag. Firmware: [`dev/DEVEL.md`](https://github.com/wsj-br/esp32-KMJ/blob/main/dev/DEVEL.md) (`./scripts/build-firmware.sh` / `./scripts/flash-firmware.sh`). Desktop app: `./scripts/build-desktop.sh` or `./scripts/run-desktop.sh`. Host installers from `./scripts/build-installers.sh` are Linux `.deb` and AppImage, Windows NSIS or MSI, and macOS `.dmg`; they are not uploaded by CI. Do not invent asset filenames, checksums, or a claim that GitHub Assets contain binaries. End with a link to the release: `https://github.com/wsj-br/esp32-KMJ/releases/tag/v<version>` (the tag includes the `v` prefix).
   - Closing thank-you line after a horizontal rule, matching the example.
   - Do **not** paste the raw `[Unreleased]` changelog verbatim as the only content; synthesize Highlights / Improvements / Fixes first.
5. **Update `dev/CHANGELOG.md`**:
   - Move everything from `[Unreleased]` into `## [x.y.z] - YYYY-MM-DD` (today’s date).
   - Leave an empty `## [Unreleased]` section at the top for future work.
   - Update the intro sentence that names the current version so it reads `The current version is `<x.y.z>`.` Drop “and has not been published” once this version is the one being released.

**Example shape** (adapt content to the release; keep this structure):

```markdown
# KMJ 0.1.0 - Release Notes

**Release date:** 2026-11-18

Short intro paragraph describing what this release emphasizes for users.

---

## Changes

For a complete, line-by-line list of changes (Added / Changed / Fixed / Removed), see [CHANGELOG.md](https://github.com/wsj-br/esp32-KMJ/blob/main/dev/CHANGELOG.md) — section **[0.1.0] - 2026-11-18**.

<details>
<summary><strong>Highlights</strong></summary>

- **Topic**: …
- **Topic**: …

</details>

<details>
<summary><strong>Improvements</strong></summary>

- **Topic**: …
- **Topic**: …

</details>

<details>
<summary><strong>Fixes</strong></summary>

- **Topic**: …
- **Topic**: …

</details>

<details>
<summary><strong>Documentation</strong></summary>

- **[README](https://github.com/wsj-br/esp32-KMJ/blob/main/README.md)** — Setup and use
- **[DEVEL.md](https://github.com/wsj-br/esp32-KMJ/blob/main/dev/DEVEL.md)** — Build, hardware checks, and release
- **[PROTOCOL.md](https://github.com/wsj-br/esp32-KMJ/blob/main/dev/proto/PROTOCOL.md)** — Wire format

</details>


## License

Copyright © YYYY Waldemar Scudeller Jr.

KMJ is released under the **MIT License**. See [LICENSE](https://github.com/wsj-br/esp32-KMJ/blob/main/LICENSE).

<details>
<summary><strong>Disclaimer</strong></summary>

Product names, including LilyGO and T-Display-S3 Pro, belong to their respective owners and are used for identification purposes only. This software is not affiliated with or endorsed by any of the mentioned brands.

See [NOTICES](https://github.com/wsj-br/esp32-KMJ/blob/main/NOTICES) for third-party dependencies.

</details>

## Downloads

Build this tag locally. CI checks the firmware and the desktop app; it does not attach binaries to this release.

- **Firmware**: Build and flash from this tag. See [DEVEL.md](https://github.com/wsj-br/esp32-KMJ/blob/main/dev/DEVEL.md#firmware). The board is a LilyGO T-Display-S3 Pro. The `kmj` build is HID only.
- **Desktop**: Build or run the app with `./scripts/build-desktop.sh` or `./scripts/run-desktop.sh`. See [DEVEL.md](https://github.com/wsj-br/esp32-KMJ/blob/main/dev/DEVEL.md#desktop-client).
- **Installers**: `./scripts/build-installers.sh` builds host installers (Linux `.deb` and AppImage, Windows NSIS or MSI, macOS `.dmg`) into `desktop/target/release/bundle/`. Those files are not uploaded here.

See the [release](https://github.com/wsj-br/esp32-KMJ/releases/tag/v0.1.0) for this tag.


---

*Thank you for using KMJ. Feedback and issue reports on the project repository are welcome.*
```

**Documentation links** (inside the Documentation `<details>`): always include README, `DEVEL.md`, and `PROTOCOL.md`. There is no separate product-docs site. Add another `dev/` doc only when this release materially changes it.

**Do not:**

- Run `./scripts/release.sh` without `--dry-run` unless the user explicitly asks. The script deletes and recreates the `v<version>` tag and publishes the GitHub release, which starts CI.
- Flash the board, or reset, monitor, or open its serial port.
- Read, print, or commit `~/.config/kmj/client.json`.
- Change `dev/proto/vectors/handshake.txt` to make a test pass. A change there is a wire-format change: bump the protocol version and update `PROTOCOL.md`.

**Summary:** Produce `release-notes/RELEASE_NOTES_<x.y.z>.md` in the shape above, fold `CHANGELOG` `[Unreleased]` into the new version section, and leave the changelog ready for the next iteration. After the notes are on `main`, publish with `./scripts/release.sh --dry-run` then `./scripts/release.sh` (see [DEVEL.md](https://github.com/wsj-br/esp32-KMJ/blob/main/dev/DEVEL.md#release)). Only run the second command when the user asks.
