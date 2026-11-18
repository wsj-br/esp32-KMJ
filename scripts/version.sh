#!/usr/bin/env bash
# Print the shared KMJ version, or set it to x.y.z.
# Usage: scripts/version.sh [x.y.z]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

usage() {
  cat <<'EOF'
Usage: ./scripts/version.sh [x.y.z]

With no arguments, print the current version.
With x.y.z, set that version in every place the project records it.

Each of x, y, and z must be an integer written without a sign or leading
zeros. 0 is allowed, so 0.1.0 is valid. 01.2.3, 1.2, and 1.2.3-beta are not.

Writes:
  desktop/Cargo.toml                         workspace package version
  desktop/Cargo.lock                         workspace packages
  desktop/app/src-tauri/tauri.conf.json      product version
  firmware/include/pins.hpp                  kFirmwareVersion
  firmware/src/ui/model.hpp                  panel version field
  dev/CHANGELOG.md                           current-version sentence

The new version must fit in the panel field (char version[N], including the
terminating NUL).
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi

if [[ $# -gt 1 ]]; then
  echo "Error: too many arguments." >&2
  usage >&2
  exit 1
fi

if ! command -v python3 >/dev/null 2>&1; then
  echo "Error: python3 is not on PATH." >&2
  exit 1
fi

mode="show"
if [[ $# -eq 1 ]]; then
  mode="set"
fi

exec python3 - "$root" "$mode" "${1:-}" <<'PY'
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
mode = sys.argv[2]
new_version = sys.argv[3]
version_re = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)$")


def fail(message):
    print(f"Error: {message}", file=sys.stderr)
    sys.exit(1)


def read_text(rel):
    path = root / rel
    if not path.is_file():
        fail(f"file not found: {rel}")
    return path.read_text(encoding="utf-8")


def one_match(text, pattern, rel, label):
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        fail(f"{rel}: expected one {label}, found {len(matches)}")
    return matches[0]


def workspace_version(cargo, rel):
    match = one_match(
        cargo,
        re.compile(r'(?m)^\[workspace\.package\]\nversion = "([^"]+)"'),
        rel,
        "workspace.package version",
    )
    return match.group(1), match


def member_names(cargo):
    block = one_match(
        cargo,
        re.compile(r"(?ms)^members = \[(.*?)\]"),
        "desktop/Cargo.toml",
        "workspace members list",
    )
    members = re.findall(r'"([^"]+)"', block.group(1))
    if not members:
        fail("desktop/Cargo.toml: workspace members list is empty")
    names = []
    for member in members:
        rel = f"desktop/{member}/Cargo.toml"
        manifest = read_text(rel)
        if re.search(r"(?m)^version\.workspace = true\s*$", manifest) is None:
            fail(f"{rel}: package does not use the workspace version")
        name = one_match(manifest, re.compile(r'(?m)^name = "([^"]+)"'), rel, "package name")
        names.append(name.group(1))
    return names


def panel_limit(model):
    match = one_match(
        model,
        re.compile(r'char version\[(\d+)\] = "([^"]*)"'),
        "firmware/src/ui/model.hpp",
        "panel version field",
    )
    size = int(match.group(1))
    if size < 2:
        fail("firmware/src/ui/model.hpp: panel version field is too small")
    return size - 1, match.group(2)


cargo_rel = "desktop/Cargo.toml"
tauri_rel = "desktop/app/src-tauri/tauri.conf.json"
pins_rel = "firmware/include/pins.hpp"
model_rel = "firmware/src/ui/model.hpp"
changelog_rel = "dev/CHANGELOG.md"
lock_rel = "desktop/Cargo.lock"

cargo = read_text(cargo_rel)
tauri = read_text(tauri_rel)
pins = read_text(pins_rel)
model = read_text(model_rel)
changelog = read_text(changelog_rel)
lock = read_text(lock_rel)

current_cargo, cargo_match = workspace_version(cargo, cargo_rel)
names = member_names(cargo)

tauri_match = one_match(
    tauri,
    re.compile(r'("version"\s*:\s*")([^"]+)(")'),
    tauri_rel,
    "version field",
)
try:
    tauri_json = json.loads(tauri)
except json.JSONDecodeError as exc:
    fail(f"{tauri_rel}: {exc}")
if tauri_json.get("version") != tauri_match.group(2):
    fail(f"{tauri_rel}: the first version field is not the product version")

pins_match = one_match(
    pins,
    re.compile(r'(kFirmwareVersion\[\] = ")([^"]+)(")'),
    pins_rel,
    "kFirmwareVersion",
)
panel_max, current_panel = panel_limit(model)
changelog_match = one_match(
    changelog,
    re.compile(r"(The current version is `)([^`]+)(`)"),
    changelog_rel,
    "current-version sentence",
)

lock_matches = []
for name in names:
    lock_matches.append(
        (
            name,
            one_match(
                lock,
                re.compile(rf'(?m)^\[\[package\]\]\nname = "{re.escape(name)}"\nversion = "([^"]+)"'),
                lock_rel,
                f"{name} package version",
            ),
        )
    )

found = {
    cargo_rel: current_cargo,
    tauri_rel: tauri_match.group(2),
    pins_rel: pins_match.group(2),
    model_rel: current_panel,
    changelog_rel: changelog_match.group(2),
}
for name, match in lock_matches:
    found[f"{lock_rel} ({name})"] = match.group(1)

if mode == "show":
    values = set(found.values())
    if len(values) != 1:
        print("Error: version mismatch:", file=sys.stderr)
        for rel, value in found.items():
            print(f"  {rel}: {value}", file=sys.stderr)
        sys.exit(1)
    print(values.pop())
    sys.exit(0)

if version_re.fullmatch(new_version) is None:
    fail(
        "version must be x.y.z, where x, y, and z are integers with no leading zeros. "
        "0 is allowed (for example 0.1.0). "
        f"Got: {new_version}"
    )
if len(new_version) > panel_max:
    fail(
        f"version is {len(new_version)} characters. "
        f"The firmware panel field holds {panel_max}."
    )

if set(found.values()) == {new_version}:
    print(f"Version is already {new_version}")
    sys.exit(0)

cargo_out = cargo[: cargo_match.start(1)] + new_version + cargo[cargo_match.end(1) :]
tauri_out = tauri[: tauri_match.start(2)] + new_version + tauri[tauri_match.end(2) :]
pins_out = pins[: pins_match.start(2)] + new_version + pins[pins_match.end(2) :]
model_out, panel_count = re.subn(
    r'(char version\[\d+\] = ")([^"]*)(")',
    rf"\g<1>{new_version}\g<3>",
    model,
    count=1,
)
if panel_count != 1:
    fail(f"{model_rel}: could not replace the panel version")
changelog_out = (
    changelog[: changelog_match.start(2)] + new_version + changelog[changelog_match.end(2) :]
)

# Replace from the end so a longer or shorter version does not move later matches.
lock_out = lock
for _name, match in sorted(lock_matches, key=lambda item: item[1].start(1), reverse=True):
    lock_out = lock_out[: match.start(1)] + new_version + lock_out[match.end(1) :]

replacements = {
    cargo_rel: cargo_out,
    tauri_rel: tauri_out,
    pins_rel: pins_out,
    model_rel: model_out,
    changelog_rel: changelog_out,
    lock_rel: lock_out,
}
for rel, text in replacements.items():
    (root / rel).write_text(text, encoding="utf-8")

print(f"Set version to {new_version}")
PY
