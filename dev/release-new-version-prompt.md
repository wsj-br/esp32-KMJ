Create a new release notes file `release-notes/RELEASE_NOTES_<x.y.z>.md` for **KMJ** using the instructions below. This file is used by [`scripts/release.sh`](https://github.com/wsj-br/esp32-KMJ/blob/main/scripts/release.sh). Publishing the GitHub release is what starts [`.github/workflows/release.yml`](https://github.com/wsj-br/esp32-KMJ/blob/main/.github/workflows/release.yml). It builds the `kmj` firmware image and desktop installers for Linux amd64 and arm64, macOS Intel and Apple Silicon, and Windows x64, then attaches them to the release. It does not flash a board.

**Before you start:** run the checks CI runs. CI does not run on pushes or pull requests, so these are the pre-release checks:

1. `pio test -e native -d firmware`
2. `pio run -e kmj -d firmware`
3. `cargo test --manifest-path desktop/Cargo.toml --workspace --exclude kmj-app`
4. `cargo build --release -p kmj-app --manifest-path desktop/Cargo.toml`

Fix any failures. `./scripts/build-desktop.sh` is the debug app build. The release workflow builds installers with `./scripts/build-installers.sh` on each target; a local run of that script checks the host installer only. Do not flash, monitor, or open the board serial port. Hardware checks in [`dev/DEVEL.md`](https://github.com/wsj-br/esp32-KMJ/blob/main/dev/DEVEL.md) need a person and a board; do not claim they passed.

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
   - State capture status plainly when this release touches it: Linux capture is complete. Windows and macOS capture are implemented; a person on that OS still has to confirm suppression, forwarding, and the release chord. Compilation alone is not that confirmation.
   - Do not weaken or misstate the safety properties: the release chord (Ctrl+Alt+Esc by default) is never forwarded, and the firmware releases all keys and buttons within 3 seconds of losing the desktop.
   - `## License` — KMJ is under the **MIT License**; copyright line and link to [`LICENSE`](https://github.com/wsj-br/esp32-KMJ/blob/main/LICENSE) as in the example.
     - Under License, include a collapsible **Disclaimer** `<details>` with the product-names disclaimer in the example, plus a link to [`NOTICES`](https://github.com/wsj-br/esp32-KMJ/blob/main/NOTICES).
   - `## Downloads` — The release attaches the unsigned assets listed in [`dev/DEVEL.md`](https://github.com/wsj-br/esp32-KMJ/blob/main/dev/DEVEL.md#release). Name those files. Do not invent checksums or extra filenames. End with a link to the release: `https://github.com/wsj-br/esp32-KMJ/releases/tag/v<version>` (the tag includes the `v` prefix).
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

This release attaches unsigned build artifacts. Each file has a `.sha256` checksum beside it. The names are listed in [DEVEL.md](https://github.com/wsj-br/esp32-KMJ/blob/main/dev/DEVEL.md#release).

- **Firmware**: `KMJ_<version>_firmware.bin` is the merged `kmj` image for a LilyGO T-Display-S3 Pro. Flash it at offset `0x0`. It is HID only.
- **Linux**: `.deb` and AppImage for amd64 and arm64.
- **macOS**: `.dmg` for Intel (`x64`) and Apple Silicon (`aarch64`).
- **Windows**: `KMJ_<version>_x64-setup.exe`.

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
