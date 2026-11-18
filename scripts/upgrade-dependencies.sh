#!/usr/bin/env bash
# Upgrade the tools and dependencies used by this repo.
#
# Tools, installed locally and not committed:
#   rustup update stable
#   pio upgrade
#
# Dependencies that are committed:
#   Direct Cargo requirements under desktop/, then desktop/Cargo.lock
#   PlatformIO lib_deps in firmware/platformio.ini
#   Vendored firmware/lib/monocypher and firmware/lib/SensorLib
#   GitHub Actions version tags in .github/workflows/ci.yml
#
# The PlatformIO platform stays on the pioarduino stable URL. After the
# lib_deps pins move, pio pkg update refreshes the local package cache.
# dtolnay/rust-toolchain@stable already follows the stable channel.
#
# A requirement keeps its precision. "0.11" becomes "0.13" when 0.13.5 is
# current, and "2" stays "2" while the latest release is still 2.x.
#
# If a later check fails, the committed files are restored. Tool upgrades stay.
#
# Usage:
#   ./scripts/upgrade-dependencies.sh
#   ./scripts/upgrade-dependencies.sh --dry-run
#   ./scripts/upgrade-dependencies.sh --skip-verify
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

DRY_RUN=false
SKIP_VERIFY=false
SKIP_TOOLS=false
SKIP_DESKTOP=false
SKIP_FIRMWARE=false
SKIP_ACTIONS=false
RESTORE=false
SNAP=""
PY_FILE=""

fail() {
  echo "Error: $*" >&2
  if [[ "$RESTORE" == true ]]; then
    restore_snapshot || echo "Restore failed. Snapshot remains at $SNAP" >&2
    RESTORE=false
  fi
  exit 1
}

usage() {
  cat <<'EOF'
Usage: ./scripts/upgrade-dependencies.sh [options]

Upgrade rustup and PlatformIO, then move dependencies to their latest
releases: Cargo requirements and desktop/Cargo.lock, PlatformIO lib_deps,
vendored Monocypher and SensorLib, and GitHub Actions version tags.

Options:
  --dry-run        Print the planned changes. Do not install tools or edit files.
  --skip-verify    Do not run Cargo tests, the native firmware tests, or pio run.
  --skip-tools     Do not run rustup update or pio upgrade.
  --skip-desktop   Do not upgrade Cargo manifests or the lockfile.
  --skip-firmware  Do not upgrade PlatformIO libraries or vendored libraries.
  --skip-actions   Do not upgrade GitHub Actions tags.
  -h, --help       Show this help.

If verification fails, committed dependency files are restored from a snapshot.
rustup and PlatformIO stay at the versions this script installed.
EOF
}

for arg in "$@"; do
  case "$arg" in
    --dry-run) DRY_RUN=true ;;
    --skip-verify) SKIP_VERIFY=true ;;
    --skip-tools) SKIP_TOOLS=true ;;
    --skip-desktop) SKIP_DESKTOP=true ;;
    --skip-firmware) SKIP_FIRMWARE=true ;;
    --skip-actions) SKIP_ACTIONS=true ;;
    -h|--help)
      usage
      exit 0
      ;;
    *)
      echo "Unknown argument: $arg" >&2
      usage >&2
      exit 1
      ;;
  esac
done

if command -v pio >/dev/null 2>&1; then
  PIO=(pio)
elif command -v platformio >/dev/null 2>&1; then
  PIO=(platformio)
else
  PIO=()
fi

require_pio() {
  [[ ${#PIO[@]} -gt 0 ]] || fail "pio is not on PATH. Install PlatformIO, then run this script again."
}

upgrade_tools() {
  if [[ "$DRY_RUN" == true ]]; then
    echo "[dry-run] Would run: rustup update stable"
    echo "[dry-run] Would run: pio upgrade"
    return 0
  fi
  command -v rustup >/dev/null 2>&1 || fail "rustup is not on PATH."
  require_pio
  echo "Updating the stable Rust toolchain..."
  rustup update stable
  echo "Updating PlatformIO..."
  "${PIO[@]}" upgrade
}

collect_desktop_files() {
  DESKTOP_FILES=()
  local path
  while IFS= read -r -d '' path; do
    DESKTOP_FILES+=("${path#"$ROOT"/}")
  done < <(find "$ROOT/desktop" \( -name target -o -name node_modules \) -prune -o \( -name Cargo.toml -o -name Cargo.lock \) -print0)
  [[ ${#DESKTOP_FILES[@]} -gt 0 ]] || fail "No Cargo.toml files under desktop/"
}

snapshot_files() {
  SNAP="$(mktemp -d)"
  local rel
  for rel in "${DESKTOP_FILES[@]}"; do
    mkdir -p "$SNAP/$(dirname "$rel")"
    cp -a "$ROOT/$rel" "$SNAP/$rel"
  done
  mkdir -p "$SNAP/firmware/lib" "$SNAP/.github/workflows"
  cp -a "$ROOT/firmware/platformio.ini" "$SNAP/firmware/platformio.ini"
  cp -a "$ROOT/firmware/lib/monocypher" "$SNAP/firmware/lib/monocypher"
  cp -a "$ROOT/firmware/lib/SensorLib" "$SNAP/firmware/lib/SensorLib"
  cp -a "$ROOT/.github/workflows/ci.yml" "$SNAP/.github/workflows/ci.yml"
}

restore_snapshot() {
  local rel
  echo "Restoring dependency files from before the upgrade." >&2
  echo "Snapshot: $SNAP" >&2
  for rel in "${DESKTOP_FILES[@]}"; do
    cp -a "$SNAP/$rel" "$ROOT/$rel"
  done
  cp -a "$SNAP/firmware/platformio.ini" "$ROOT/firmware/platformio.ini"
  cp -a "$SNAP/.github/workflows/ci.yml" "$ROOT/.github/workflows/ci.yml"
  rm -rf "$ROOT/firmware/lib/monocypher" "$ROOT/firmware/lib/SensorLib"
  cp -a "$SNAP/firmware/lib/monocypher" "$ROOT/firmware/lib/monocypher"
  cp -a "$SNAP/firmware/lib/SensorLib" "$ROOT/firmware/lib/SensorLib"
}

on_err() {
  local status=$?
  trap - ERR
  if [[ "$RESTORE" == true ]]; then
    restore_snapshot || echo "Restore failed. Snapshot remains at $SNAP" >&2
  fi
  exit "$status"
}

files_differ() {
  ! cmp -s "$1" "$2"
}

tree_differs() {
  ! diff -rq "$1" "$2" >/dev/null
}

verify_desktop() {
  echo "Running desktop tests..."
  cargo test --manifest-path "$ROOT/desktop/Cargo.toml" --workspace --exclude kmj-app
  if [[ "$(uname -s)" == "Linux" ]] && ! pkg-config --exists gtk+-3.0 webkit2gtk-4.1; then
    echo "GTK 3 and WebKitGTK 4.1 are not installed, so the kmj-app compile check was skipped."
    return 0
  fi
  echo "Checking that the desktop app compiles..."
  cargo check --manifest-path "$ROOT/desktop/Cargo.toml" -p kmj-app
}

verify_firmware() {
  require_pio
  echo "Running native firmware tests..."
  "${PIO[@]}" test -e native -d "$ROOT/firmware"
  echo "Building the kmj firmware..."
  "${PIO[@]}" run -e kmj -d "$ROOT/firmware"
}

PY_FILE="$(mktemp)"
trap 'rm -f "$PY_FILE"' EXIT
cat >"$PY_FILE" <<'PY'
import json
import os
import re
import shutil
import sys
import tarfile
import tempfile
import urllib.error
import urllib.request
from pathlib import Path

root = Path(sys.argv[1])
dry_run = os.environ.get("UPGRADE_DRY_RUN") == "1"
skip_desktop = os.environ.get("UPGRADE_SKIP_DESKTOP") == "1"
skip_firmware = os.environ.get("UPGRADE_SKIP_FIRMWARE") == "1"
skip_actions = os.environ.get("UPGRADE_SKIP_ACTIONS") == "1"

HEADERS = {
    "User-Agent": "esp32-KMJ-upgrade-dependencies",
    "Accept": "application/vnd.github+json, application/json",
}


def fail(message):
    raise SystemExit(message)


def get_json(url):
    request = urllib.request.Request(url, headers=HEADERS)
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            return json.load(response)
    except urllib.error.HTTPError as exc:
        detail = exc.read().decode("utf-8", "replace")[:300]
        fail(f"GET {url} failed: HTTP {exc.code} {detail}")


def download(url, dest):
    request = urllib.request.Request(url, headers=HEADERS)
    try:
        with urllib.request.urlopen(request, timeout=180) as response, dest.open("wb") as handle:
            shutil.copyfileobj(response, handle)
    except urllib.error.HTTPError as exc:
        fail(f"GET {url} failed: HTTP {exc.code}")


def version_key(value):
    numbers = []
    for part in value.lstrip("v").split("."):
        match = re.match(r"(\d+)", part)
        if not match:
            break
        numbers.append(int(match.group(1)))
    return tuple(numbers)


def looks_like_requirement(value):
    return re.fullmatch(r"\d+(?:\.\d+){0,2}", value) is not None


def is_dependency_section(header):
    return (
        header == "dependencies"
        or header.endswith(".dependencies")
        or header.endswith("-dependencies")
    )


CRATE_LATEST = {}


def latest_crate(name):
    if name not in CRATE_LATEST:
        data = get_json(f"https://crates.io/api/v1/crates/{name}")
        version = (data.get("crate") or {}).get("max_stable_version") or ""
        if not version:
            fail(f"No stable crates.io release for {name}")
        CRATE_LATEST[name] = version
    return CRATE_LATEST[name]


def same_precision(current, latest):
    current_parts = current.split(".")
    latest_parts = latest.split(".")
    if len(current_parts) >= len(latest_parts):
        return latest
    return ".".join(latest_parts[: len(current_parts)])


def write_if_changed(path, original, updated):
    if updated == original or dry_run:
        return
    path.write_text(updated)


def upgrade_cargo_file(path):
    original = path.read_text(encoding="utf-8")
    newline = "\n" if original.endswith("\n") else ""
    lines = original.splitlines()
    header = ""
    output = []
    index = 0
    changed = 0
    while index < len(lines):
        line = lines[index]
        header_match = re.match(r"^\[(.+)\]\s*$", line)
        if header_match:
            header = header_match.group(1)
            output.append(line)
            index += 1
            continue
        if not is_dependency_section(header):
            output.append(line)
            index += 1
            continue
        assignment = re.match(r"^([A-Za-z0-9_-]+) = (.*)$", line)
        if not assignment:
            output.append(line)
            index += 1
            continue
        name, rest = assignment.group(1), assignment.group(2)
        if rest.startswith('"'):
            version_match = re.match(r'"([^"]+)"\s*(#.*)?$', rest)
            if not version_match or not looks_like_requirement(version_match.group(1)):
                output.append(line)
                index += 1
                continue
            current = version_match.group(1)
            updated = same_precision(current, latest_crate(name))
            if updated != current:
                comment = version_match.group(2)
                suffix = f" {comment}" if comment else ""
                output.append(f'{name} = "{updated}"{suffix}')
                print(f"{path.relative_to(root)}: {name} {current} -> {updated}")
                changed += 1
            else:
                output.append(line)
            index += 1
            continue
        if rest.startswith("{"):
            block = []
            depth = 0
            cursor = index
            while cursor < len(lines):
                block.append(lines[cursor])
                depth += lines[cursor].count("{") - lines[cursor].count("}")
                cursor += 1
                if depth <= 0:
                    break
            text = "\n".join(block)
            if re.search(r"\bpath\s*=", text):
                output.extend(block)
                index = cursor
                continue
            package_match = re.search(r'\bpackage\s*=\s*"([^"]+)"', text)
            version_match = re.search(r'\bversion\s*=\s*"([^"]+)"', text)
            crate = package_match.group(1) if package_match else name
            if not version_match or not looks_like_requirement(version_match.group(1)):
                output.extend(block)
                index = cursor
                continue
            current = version_match.group(1)
            updated = same_precision(current, latest_crate(crate))
            if updated != current:
                text = text.replace(f'version = "{current}"', f'version = "{updated}"', 1)
                output.extend(text.split("\n"))
                print(f"{path.relative_to(root)}: {crate} {current} -> {updated}")
                changed += 1
            else:
                output.extend(block)
            index = cursor
            continue
        output.append(line)
        index += 1
    write_if_changed(path, original, "\n".join(output) + newline)
    return changed


def upgrade_desktop():
    if skip_desktop:
        return
    manifests = [
        path
        for path in (root / "desktop").rglob("Cargo.toml")
        if "target" not in path.parts and "node_modules" not in path.parts
    ]
    changed = sum(upgrade_cargo_file(path) for path in sorted(manifests))
    if changed == 0:
        print("Cargo requirements are already at the latest releases.")
    else:
        print(f"Cargo requirements updated: {changed}")


def registry_latest(owner, name):
    data = get_json(f"https://api.registry.platformio.org/v3/packages/{owner}/library/{name}")
    version = data.get("version") or {}
    latest = version.get("name") or ""
    files = version.get("files") or []
    if not latest or not files:
        fail(f"No PlatformIO registry release for {owner}/{name}")
    return latest, files[0]["download_url"]


def upgrade_lib_deps():
    path = root / "firmware/platformio.ini"
    original = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"^(\s*)([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)@(\^?)(\d+(?:\.\d+)*)\s*$"
    )
    output = []
    changed = 0
    for line in original.splitlines():
        match = pattern.match(line)
        if not match:
            output.append(line)
            continue
        indent, owner, name, caret, current = match.groups()
        latest, _url = registry_latest(owner, name)
        updated = same_precision(current, latest)
        if updated == current:
            output.append(line)
            continue
        output.append(f"{indent}{owner}/{name}@{caret}{updated}")
        print(f"firmware/platformio.ini: {owner}/{name} {current} -> {updated}")
        changed += 1
    newline = "\n" if original.endswith("\n") else ""
    write_if_changed(path, original, "\n".join(output) + newline)
    if changed == 0:
        print("PlatformIO lib_deps are already at the latest releases.")


def replace_library_tree(archive, destination):
    with tempfile.TemporaryDirectory() as temporary:
        extracted = Path(temporary) / "src"
        extracted.mkdir()
        with tarfile.open(archive, "r:*") as tar:
            tar.extractall(extracted, filter="data")
        manifests = list(extracted.rglob("library.json"))
        if not manifests:
            fail(f"Archive has no library.json: {archive}")
        library_root = min(manifests, key=lambda item: len(item.parts)).parent
        if destination.exists():
            shutil.rmtree(destination)
        shutil.copytree(library_root, destination)


def upgrade_sensorlib():
    manifest_path = root / "firmware/lib/SensorLib/library.json"
    current = json.loads(manifest_path.read_text(encoding="utf-8"))["version"]
    latest, url = registry_latest("lewisxhe", "SensorLib")
    if version_key(latest) <= version_key(current):
        print(f"SensorLib {current} is current")
        return
    print(f"firmware/lib/SensorLib: {current} -> {latest}")
    if dry_run:
        return
    with tempfile.TemporaryDirectory() as temporary:
        archive = Path(temporary) / "SensorLib.tar.gz"
        download(url, archive)
        replace_library_tree(archive, root / "firmware/lib/SensorLib")


def upgrade_monocypher():
    manifest_path = root / "firmware/lib/monocypher/library.json"
    original = manifest_path.read_text(encoding="utf-8")
    current = json.loads(original)["version"]
    release = get_json("https://api.github.com/repos/LoupVaillant/Monocypher/releases/latest")
    tag = release.get("tag_name") or ""
    latest = tag.lstrip("v")
    if not latest:
        fail("Monocypher latest release has no tag")
    if version_key(latest) <= version_key(current):
        print(f"Monocypher {current} is current")
        return
    print(f"firmware/lib/monocypher: {current} -> {latest}")
    if dry_run:
        return
    tarball = release.get("tarball_url")
    if not tarball:
        fail("Monocypher latest release has no source archive")
    with tempfile.TemporaryDirectory() as temporary:
        archive = Path(temporary) / "monocypher.tar.gz"
        download(tarball, archive)
        found = {}
        with tarfile.open(archive, "r:*") as tar:
            for member in tar.getmembers():
                if not member.isfile():
                    continue
                filename = Path(member.name).name
                if Path(member.name).parent.name == "src" and filename in {"monocypher.c", "monocypher.h"}:
                    source = tar.extractfile(member)
                    if source is None:
                        fail(f"Could not read {member.name} from the Monocypher archive")
                    found[filename] = source.read()
        if set(found) != {"monocypher.c", "monocypher.h"}:
            fail("Monocypher archive is missing src/monocypher.c or src/monocypher.h")
        source_dir = root / "firmware/lib/monocypher/src"
        for filename, payload in found.items():
            (source_dir / filename).write_bytes(payload)
    updated = re.sub(r'("version"\s*:\s*")[^"]+"', rf'\g<1>{latest}"', original, count=1)
    updated = re.sub(r"(Vendored Monocypher )[0-9.]+", rf"\g<1>{latest}", updated, count=1)
    manifest_path.write_text(updated)


def tag_exists(repository, tag):
    request = urllib.request.Request(
        f"https://api.github.com/repos/{repository}/git/ref/tags/{tag}",
        headers=HEADERS,
    )
    try:
        with urllib.request.urlopen(request, timeout=60) as response:
            json.load(response)
        return True
    except urllib.error.HTTPError as exc:
        if exc.code == 404:
            return False
        detail = exc.read().decode("utf-8", "replace")[:300]
        fail(f"GET {repository} tag {tag} failed: HTTP {exc.code} {detail}")


def action_ref(repository, current):
    release = get_json(f"https://api.github.com/repos/{repository}/releases/latest")
    tag = release.get("tag_name") or ""
    if not re.fullmatch(r"v\d+(?:\.\d+)*", tag):
        fail(f"{repository} latest release tag is not a version: {tag or '<missing>'}")
    if re.fullmatch(r"v\d+", current):
        major = "v" + tag[1:].split(".", 1)[0]
        if tag_exists(repository, major):
            return major
    return tag


def upgrade_actions():
    if skip_actions:
        return
    path = root / ".github/workflows/ci.yml"
    original = path.read_text(encoding="utf-8")
    pattern = re.compile(
        r"^(\s*- uses: )([A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+)@(v\d+(?:\.\d+)*)(\s*)$"
    )
    output = []
    changed = 0
    for line in original.splitlines():
        match = pattern.match(line)
        if not match:
            output.append(line)
            continue
        prefix, repository, current, suffix = match.groups()
        updated = action_ref(repository, current)
        if updated == current:
            output.append(line)
            continue
        output.append(f"{prefix}{repository}@{updated}{suffix}")
        print(f".github/workflows/ci.yml: {repository} {current} -> {updated}")
        changed += 1
    newline = "\n" if original.endswith("\n") else ""
    write_if_changed(path, original, "\n".join(output) + newline)
    if changed == 0:
        print("GitHub Actions tags are already at the latest releases.")


upgrade_desktop()
if not skip_firmware:
    upgrade_lib_deps()
    upgrade_sensorlib()
    upgrade_monocypher()
    print("PlatformIO platform stays on the pioarduino stable URL.")
upgrade_actions()
PY

run_upgrade_py() {
  python3 "$PY_FILE" "$ROOT"
}

if [[ "$SKIP_TOOLS" == false ]]; then
  upgrade_tools
fi

collect_desktop_files

export UPGRADE_SKIP_DESKTOP=0
export UPGRADE_SKIP_FIRMWARE=0
export UPGRADE_SKIP_ACTIONS=0
[[ "$SKIP_DESKTOP" == true ]] && UPGRADE_SKIP_DESKTOP=1
[[ "$SKIP_FIRMWARE" == true ]] && UPGRADE_SKIP_FIRMWARE=1
[[ "$SKIP_ACTIONS" == true ]] && UPGRADE_SKIP_ACTIONS=1

if [[ "$DRY_RUN" == true ]]; then
  export UPGRADE_DRY_RUN=1
  run_upgrade_py
  if [[ "$SKIP_DESKTOP" == false ]]; then
    command -v cargo >/dev/null 2>&1 || fail "cargo is not on PATH."
    echo "Lockfile preview with the current Cargo requirements:"
    cargo update --dry-run --manifest-path "$ROOT/desktop/Cargo.toml"
  fi
  if [[ "$SKIP_FIRMWARE" == false ]]; then
    echo "[dry-run] Would run: pio pkg update -d firmware --no-save"
  fi
  exit 0
fi

export UPGRADE_DRY_RUN=0
snapshot_files
RESTORE=true
trap on_err ERR
trap 'rm -f "$PY_FILE"' EXIT

run_upgrade_py

if [[ "$SKIP_DESKTOP" == false ]]; then
  command -v cargo >/dev/null 2>&1 || fail "cargo is not on PATH."
  echo "Updating desktop/Cargo.lock..."
  cargo update --manifest-path "$ROOT/desktop/Cargo.toml"
fi

if [[ "$SKIP_FIRMWARE" == false ]]; then
  require_pio
  edited_ini="$(mktemp)"
  cp -a "$ROOT/firmware/platformio.ini" "$edited_ini"
  echo "Updating the local PlatformIO packages..."
  "${PIO[@]}" pkg update -d "$ROOT/firmware" --no-save
  if ! cmp -s "$ROOT/firmware/platformio.ini" "$edited_ini"; then
    echo "Kept the lib_deps versions written by this script."
    cp -a "$edited_ini" "$ROOT/firmware/platformio.ini"
  fi
  rm -f "$edited_ini"
fi

desktop_changed=false
firmware_changed=false
actions_changed=false
for rel in "${DESKTOP_FILES[@]}"; do
  if files_differ "$ROOT/$rel" "$SNAP/$rel"; then
    desktop_changed=true
  fi
done
if files_differ "$ROOT/firmware/platformio.ini" "$SNAP/firmware/platformio.ini" \
  || tree_differs "$ROOT/firmware/lib/monocypher" "$SNAP/firmware/lib/monocypher" \
  || tree_differs "$ROOT/firmware/lib/SensorLib" "$SNAP/firmware/lib/SensorLib"; then
  firmware_changed=true
fi
if files_differ "$ROOT/.github/workflows/ci.yml" "$SNAP/.github/workflows/ci.yml"; then
  actions_changed=true
fi

if [[ "$desktop_changed" == false && "$firmware_changed" == false && "$actions_changed" == false ]]; then
  echo "Dependency files are already at the latest versions."
else
  echo "Updated dependency files."
  if [[ "$SKIP_VERIFY" == false ]]; then
    if [[ "$desktop_changed" == true ]]; then
      verify_desktop
    fi
    if [[ "$firmware_changed" == true ]]; then
      verify_firmware
    fi
  else
    echo "Verification skipped."
  fi
fi

RESTORE=false
trap - ERR
rm -rf "$SNAP"
echo "Upgrade finished."
