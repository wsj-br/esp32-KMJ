#!/usr/bin/env bash
# Build the AppImage with copyright files, then write third-party notices.
# Usage: scripts/write-notices.sh
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bundle_dir="${CARGO_TARGET_DIR:-$root/desktop/target}/release/bundle"
out="$root/NOTICES"

usage() {
  cat <<'EOF'
Usage: ./scripts/write-notices.sh

Build the Linux AppImage with linuxdeploy's copyright-file scan, then write
NOTICES at the repository root. The file contains:

  - Debian copyright files for the system libraries in the AppImage
  - license and notice files for the Rust crates in desktop/Cargo.lock
  - license and notice files for the firmware libraries and the Arduino-ESP32 framework

The AppImage build is the slow step and takes a few minutes. The default
installer build still skips the copyright scan.
EOF
}

if [[ "${1:-}" == "-h" || "${1:-}" == "--help" ]]; then
  usage
  exit 0
fi
if [[ $# -gt 0 ]]; then
  usage >&2
  exit 1
fi

cd "$root"
echo "Building the AppImage with copyright files..."
"$root/scripts/build-installers.sh" --copyright-files --bundles appimage

shopt -s nullglob
appdirs=("$bundle_dir"/appimage/*.AppDir)
if [[ ${#appdirs[@]} -eq 0 ]]; then
  echo "No AppDir under $bundle_dir/appimage after the AppImage build." >&2
  exit 1
fi
if [[ ${#appdirs[@]} -gt 1 ]]; then
  echo "More than one AppDir under $bundle_dir/appimage." >&2
  printf '  %s\n' "${appdirs[@]}" >&2
  exit 1
fi

echo "Fetching Rust crate sources..."
cargo fetch --locked --manifest-path "$root/desktop/Cargo.toml"

python3 - "$root" "${appdirs[0]}" "$out" <<'PY'
import json
import os
import re
import sys
import urllib.request
from pathlib import Path

root = Path(sys.argv[1])
appdir = Path(sys.argv[2])
out = Path(sys.argv[3])

LICENSE_NAME = re.compile(
    r"^(license|licence|copying|notice|unlicense|copyright)(\..*)?$",
    re.IGNORECASE,
)


def read_text(path: Path) -> str:
    return path.read_text(encoding="utf-8", errors="replace").strip()


def package_block(title: str, body: str) -> str:
    return f"======== {title} ========\n\n{body.strip()}\n\n-------\n\n"


def license_files(directory: Path, include_src: bool) -> list[Path]:
    found = []
    if not directory.is_dir():
        return found
    for dirpath, dirnames, filenames in os.walk(directory):
        current = Path(dirpath)
        rel = current.relative_to(directory)
        parts = rel.parts
        if parts and parts[0] in {"examples", "docs", "doc", "scripts", "test", "tests", ".git"}:
            dirnames.clear()
            continue
        if not parts:
            dirnames[:] = [name for name in dirnames if include_src and name == "src"]
        for name in filenames:
            if LICENSE_NAME.match(name):
                found.append(current / name)
    return sorted(found)


def files_body(paths: list[Path], base: Path) -> str:
    chunks = []
    for path in paths:
        rel = path.relative_to(base).as_posix()
        chunks.append(f"{rel}\n\n{read_text(path)}")
    return "\n\n".join(chunks)


def debian_sections() -> list[str]:
    doc = appdir / "usr" / "share" / "doc"
    files = sorted(path for path in doc.rglob("copyright") if path.is_file())
    if not files:
        sys.exit(f"No copyright files in {doc} after the AppImage build.")
    sections = ["Linux AppImage system libraries\n\n"]
    for path in files:
        sections.append(package_block(path.parent.name, read_text(path)))
    return sections


def parse_lock(path: Path) -> list[dict]:
    packages = []
    current = None
    for line in path.read_text(encoding="utf-8").splitlines():
        if line == "[[package]]":
            if current:
                packages.append(current)
            current = {}
            continue
        if current is None or line.startswith(" ") or line.startswith("\t"):
            continue
        if line.startswith("name = "):
            current["name"] = line.split('"', 2)[1]
        elif line.startswith("version = ") and "version" not in current:
            current["version"] = line.split('"', 2)[1]
        elif line.startswith("source = "):
            current["source"] = line.split('"', 2)[1]
    if current:
        packages.append(current)
    return [pkg for pkg in packages if pkg.get("source", "").startswith("registry+")]


def find_crate(name: str, version: str) -> Path | None:
    registry = Path.home() / ".cargo" / "registry" / "src"
    if not registry.is_dir():
        return None
    for root in registry.iterdir():
        candidate = root / f"{name}-{version}"
        if candidate.is_dir():
            return candidate
    return None


def crate_meta(crate_dir: Path) -> tuple[str, str]:
    text = (crate_dir / "Cargo.toml").read_text(encoding="utf-8", errors="replace")
    license_match = re.search(r'(?m)^license\s*=\s*"([^"]*)"', text)
    authors_match = re.search(r"(?m)^authors\s*=\s*\[(.*?)\]", text, re.DOTALL)
    license_name = license_match.group(1) if license_match else ""
    authors = ""
    if authors_match:
        authors = ", ".join(re.findall(r'"([^"]+)"', authors_match.group(1)))
    return license_name, authors


def rust_sections() -> list[str]:
    packages = parse_lock(root / "desktop" / "Cargo.lock")
    sections = ["Rust crates\n\n"]
    missing = []
    for pkg in sorted(packages, key=lambda item: (item["name"], item["version"])):
        crate_dir = find_crate(pkg["name"], pkg["version"])
        if crate_dir is None:
            missing.append(f"{pkg['name']} {pkg['version']}")
            continue
        license_name, authors = crate_meta(crate_dir)
        files = license_files(crate_dir, include_src=False)
        title = f"{pkg['name']} {pkg['version']}"
        if license_name:
            title += f" ({license_name})"
        parts = []
        if authors:
            parts.append(f"Authors: {authors}")
        if files:
            parts.append(files_body(files, crate_dir))
        elif license_name:
            parts.append(f"Declared license: {license_name}\nThis crate ships no license file.")
        else:
            missing.append(f"{pkg['name']} {pkg['version']} (no license)")
            continue
        sections.append(package_block(title, "\n\n".join(parts)))
    if missing:
        sys.exit("Rust crates with no source or license:\n" + "\n".join(missing))
    return sections


def library_version(directory: Path) -> tuple[str, str]:
    manifest = directory / "library.json"
    if manifest.is_file():
        data = json.loads(manifest.read_text(encoding="utf-8"))
        return str(data.get("version") or ""), str(data.get("license") or "")
    props = directory / "library.properties"
    version = ""
    if props.is_file():
        for line in props.read_text(encoding="utf-8", errors="replace").splitlines():
            if line.startswith("version="):
                version = line.split("=", 1)[1].strip()
                break
    return version, ""


def firmware_library_sections() -> tuple[list[str], list[str]]:
    libdeps = root / "firmware" / ".pio" / "libdeps" / "kmj"
    if not libdeps.is_dir():
        sys.exit(f"No firmware libraries in {libdeps}. Build the firmware once so PlatformIO downloads them.")
    sections = []
    bundled = []
    for directory in sorted(path for path in libdeps.iterdir() if path.is_dir()):
        version, license_name = library_version(directory)
        files = license_files(directory, include_src=True)
        if not files:
            bundled.append(f"{directory.name} {version}".strip())
            continue
        title = directory.name
        if version:
            title += f" {version}"
        if license_name:
            title += f" ({license_name})"
        sections.append(package_block(title, files_body(files, directory)))
    return sections, bundled


def monocypher_section() -> str:
    header = root / "firmware" / "lib" / "monocypher" / "src" / "monocypher.h"
    manifest = root / "firmware" / "lib" / "monocypher" / "library.json"
    version = json.loads(manifest.read_text(encoding="utf-8")).get("version", "")
    lines = []
    for line in header.read_text(encoding="utf-8").splitlines():
        if line.startswith("//"):
            lines.append(line[3:] if line.startswith("// ") else line[2:])
        elif lines and line.strip() == "":
            lines.append("")
        else:
            break
    title = f"monocypher {version} (BSD-2-Clause OR CC0-1.0)".strip()
    return package_block(title, "\n".join(lines).strip())


def arduino_section(bundled: list[str]) -> str:
    package = Path.home() / ".platformio" / "packages" / "framework-arduinoespressif32" / "package.json"
    if not package.is_file():
        sys.exit(f"Arduino-ESP32 package metadata not found at {package}.")
    data = json.loads(package.read_text(encoding="utf-8"))
    version = data["version"]
    license_name = data.get("license") or ""
    url = f"https://raw.githubusercontent.com/espressif/arduino-esp32/{version}/LICENSE.md"
    try:
        with urllib.request.urlopen(url, timeout=30) as response:
            text = response.read().decode("utf-8").strip()
    except Exception as exc:
        sys.exit(f"Could not download {url}: {exc}")
    title = f"Arduino-ESP32 {version}"
    if license_name:
        title += f" ({license_name})"
    note = ""
    if bundled:
        note = "Libraries from this framework with no separate license file: " + ", ".join(bundled) + ".\n\n"
    return package_block(title, f"{note}LICENSE.md\n\n{text}")


intro = """Third-party notices for KMJ.

The Linux AppImage section is the Debian copyright file for each system
library linuxdeploy bundled. The Rust section is every registry crate in
desktop/Cargo.lock. The firmware section is the PlatformIO libraries, the
vendored Monocypher sources, and the Arduino-ESP32 framework.

Regenerate this file with ./scripts/write-notices.sh.

"""

parts = [intro]
parts.extend(debian_sections())
parts.append("Rust crates\n\n")
parts.extend(rust_sections()[1:])
firmware, bundled = firmware_library_sections()
parts.append("Firmware libraries\n\n")
parts.extend(firmware)
parts.append(monocypher_section())
parts.append(arduino_section(bundled))

tmp = out.with_suffix(out.suffix + ".tmp")
tmp.write_text("".join(parts), encoding="utf-8")
os.replace(tmp, out)
count = "".join(parts).count("\n======== ")
print(f"Wrote {out} ({count} packages)")
PY
