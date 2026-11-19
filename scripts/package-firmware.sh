#!/usr/bin/env bash
# Package a built firmware environment into a single merged flash image,
# exactly the way the release workflow does it.
# Usage: scripts/package-firmware.sh <env> <asset-base-name>
#   scripts/package-firmware.sh kmj KMJ_0.1.1_firmware
#   scripts/package-firmware.sh kmj-ws185b KMJ_0.1.1_firmware_ws185b
# Writes <asset-base-name>.bin and .bin.sha256 into dist/ at the repo root.
# The env must be built first (scripts/build-firmware.sh <env>).
#
# The merged image is taken from the build directory's flasher_args.json when
# the platform provides it (esptool merge-bin at the documented offsets), and
# otherwise from firmware.factory.bin, which the pioarduino platform already
# combines from bootloader/partitions/boot_app0/app at offset 0x0.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

env="${1:?usage: package-firmware.sh <env> <asset-base-name>}"
name="${2:?usage: package-firmware.sh <env> <asset-base-name>}"

build_dir="$root/firmware/.pio/build/$env"
if [[ ! -f "$build_dir/flasher_args.json" && ! -f "$build_dir/firmware.factory.bin" ]]; then
  echo "No flasher_args.json or firmware.factory.bin in $build_dir; build the env first:" >&2
  echo "  scripts/build-firmware.sh $env" >&2
  exit 1
fi

mkdir -p "$root/dist"

if [[ -f "$build_dir/flasher_args.json" ]]; then
  esptool_py="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
  pio_py="$HOME/.platformio/penv/bin/python"
  if [[ ! -f "$esptool_py" || ! -x "$pio_py" ]]; then
    echo "esptool or the PlatformIO python was not found (~/.platformio); cannot package." >&2
    exit 1
  fi

  # flasher_args.json carries the offsets, files and flash settings; esptool
  # runs with PlatformIO's penv python (pyserial).
  mapfile -t merge_args < <("$pio_py" - "$build_dir" <<'PY'
import json
import sys
from pathlib import Path

build = Path(sys.argv[1])
args = json.loads((build / "flasher_args.json").read_text(encoding="utf-8"))
files = args.get("flash_files")
if not isinstance(files, dict) or not files:
    sys.exit("flasher_args.json has no flash_files")
settings = args.get("flash_settings") or {}
for key in ("flash_mode", "flash_freq", "flash_size"):
    if not settings.get(key):
        sys.exit(f"flasher_args.json flash_settings is missing {key}")


def resolve(name):
    path = Path(name)
    for candidate in (path, build / name, build.parent / name):
        if candidate.is_file():
            return str(candidate)
    sys.exit(f"flash file not found: {name}")


print("--flash-mode", settings["flash_mode"])
print("--flash-freq", settings["flash_freq"])
print("--flash-size", settings["flash_size"])
for offset, name in sorted(files.items(), key=lambda item: int(item[0], 16)):
    print(offset)
    print(resolve(name))
PY
)
  "$pio_py" "$esptool_py" --chip esp32s3 merge-bin \
    --output "$root/dist/$name.bin" "${merge_args[@]}"
else
  cp "$build_dir/firmware.factory.bin" "$root/dist/$name.bin"
fi

(
  cd "$root/dist"
  sha256sum "$name.bin" > "$name.bin.sha256"
)
echo "packaged dist/$name.bin"
