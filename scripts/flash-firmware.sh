#!/usr/bin/env bash
# Reboot the board into the ROM bootloader, verify the upload port exists,
# build if needed, and flash the KMJ firmware.
# Usage: scripts/flash-firmware.sh [--first-flash] [env] [extra pio run args]
#
# Envs: kmj | kmj-debug | kmj-ws185b | kmj-ws185b-debug. The per-device
# wrappers scripts/flash-tdisplay-s3.sh and scripts/flash-ws185b.sh validate
# the env for their board and provide friendlier defaults.
#
# Port selection: --upload-port or PLATFORMIO_UPLOAD_PORT wins. Otherwise a
# single /dev/serial/by-id/usb-* entry is used; with none, a single
# /dev/ttyACM*. With more than one candidate the script refuses to guess.
#
# Normally the script first runs firmware/tools/bootloader.py to enter the ROM
# bootloader before compiling (works when the -debug firmware is running: its
# CDC port reboots into download mode on a 1200 baud touch). If the device is
# missing or cannot enter bootloader mode, the script exits before building.
#
# --first-flash skips that: it builds, then flashes with esptool directly
# using the build directory's flasher_args.json and esptool's default reset.
# Use it when the board does not run KMJ yet (stock firmware, or the HID-only
# production env with no serial port; for the latter hold BOOT, tap RESET,
# release BOOT first so the ROM bootloader port appears).
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if command -v pio >/dev/null 2>&1; then
  pio=(pio)
elif command -v platformio >/dev/null 2>&1; then
  pio=(platformio)
else
  echo "pio is not on PATH. Install PlatformIO (pip install platformio)." >&2
  exit 1
fi

first_flash=false
args=()
for arg in "$@"; do
  if [[ "$arg" == "--first-flash" ]]; then
    first_flash=true
  else
    args+=("$arg")
  fi
done

env="kmj-debug"
if [[ ${#args[@]} -gt 0 && "${args[0]}" != -* ]]; then
  env="${args[0]}"
  args=("${args[@]:1}")
fi

case "$env" in
  kmj|kmj-debug|kmj-ws185b|kmj-ws185b-debug) ;;
  *)
    echo "Unknown environment '$env'. Use kmj, kmj-debug, kmj-ws185b or kmj-ws185b-debug." >&2
    exit 1
    ;;
esac

# Python with pyserial: PlatformIO's own interpreter has it.
find_python() {
  local pio_bin pio_py candidate
  pio_bin="$(command -v "${pio[0]}")"
  pio_py="$(dirname "$pio_bin")/python"
  for candidate in "$HOME/.platformio/penv/bin/python" "$pio_py" python3; do
    if command -v "$candidate" >/dev/null 2>&1 \
        && "$candidate" -c "import serial" >/dev/null 2>&1; then
      echo "$candidate"
      return 0
    fi
  done
  return 1
}

if ! py="$(find_python)"; then
  echo "Python with pyserial not found; cannot run firmware/tools/bootloader.py." >&2
  exit 1
fi

# Resolve upload port: --upload-port, PLATFORMIO_UPLOAD_PORT, or autodetect.
upload_port=""
for ((i = 0; i < ${#args[@]}; i++)); do
  case "${args[i]}" in
    --upload-port)
      if [[ $((i + 1)) -ge ${#args[@]} || -z "${args[i + 1]}" ]]; then
        echo "Missing value for --upload-port." >&2
        exit 1
      fi
      upload_port="${args[i + 1]}"
      i=$((i + 1))
      ;;
    --upload-port=*)
      upload_port="${args[i]#--upload-port=}"
      if [[ -z "$upload_port" ]]; then
        echo "Missing value for --upload-port=." >&2
        exit 1
      fi
      ;;
  esac
done

if [[ -z "$upload_port" && -n "${PLATFORMIO_UPLOAD_PORT:-}" ]]; then
  upload_port="$PLATFORMIO_UPLOAD_PORT"
fi

if [[ -z "$upload_port" ]]; then
  mapfile -t candidates < <(
    found=0
    for p in /dev/serial/by-id/usb-*; do
      if [[ -e $p ]]; then
        echo "$p"
        found=1
      fi
    done
    if [[ $found == 0 ]]; then
      for p in /dev/ttyACM*; do
        [[ -c $p ]] && echo "$p"
      done
    fi
  )
  if [[ ${#candidates[@]} == 1 ]]; then
    upload_port="${candidates[0]}"
  elif [[ ${#candidates[@]} -gt 1 ]]; then
    echo "Multiple serial ports found; pass the right one explicitly:" >&2
    printf '  %s\n' "${candidates[@]}" >&2
    echo "Use --upload-port, PLATFORMIO_UPLOAD_PORT, or the per-device wrappers." >&2
    exit 1
  else
    echo "No serial device found; aborting before build." >&2
    echo "Connect the board (or hold BOOT, tap RESET, release BOOT if running HID-only kmj) and try again," >&2
    echo "or use --first-flash / --upload-port." >&2
    exit 1
  fi
fi

if [[ ! -e "$upload_port" ]]; then
  echo "Serial device '$upload_port' not found; aborting before build." >&2
  echo "Connect the board (or hold BOOT, tap RESET, release BOOT if running HID-only kmj) and try again." >&2
  exit 1
fi

if [[ $first_flash == true ]]; then
  echo "Building $env..."
  "${pio[@]}" run -e "$env" -d "$root/firmware"

  build_dir="$root/firmware/.pio/build/$env"
  if [[ -f "$build_dir/flasher_args.json" ]]; then
    mapfile -t flash_args < <("$py" - "$build_dir" <<'PY'
import json
import sys
from pathlib import Path

build = Path(sys.argv[1])
args = json.loads((build / "flasher_args.json").read_text(encoding="utf-8"))
files = args.get("flash_files") or {}
if not files:
    sys.exit("flasher_args.json has no flash_files")

def resolve(name):
    path = Path(name)
    for candidate in (path, build / name, build.parent / name):
        if candidate.is_file():
            return str(candidate)
    sys.exit(f"flash file not found: {name}")

for offset, name in sorted(files.items(), key=lambda item: int(item[0], 16)):
    print(offset)
    print(resolve(name))
PY
)
  elif [[ -f "$build_dir/firmware.factory.bin" ]]; then
    # The pioarduino platform combines bootloader/partitions/boot_app0/app
    # into a merged image at offset 0x0.
    flash_args=("0x0" "$build_dir/firmware.factory.bin")
  else
    echo "No flasher_args.json or firmware.factory.bin in $build_dir; build failed?" >&2
    exit 1
  fi
  echo "Flashing $env over $upload_port (esptool default reset)..."
  # esptool ships inside PlatformIO's tool package and needs the penv python
  # (pyserial); calling it directly avoids pio CLI argument quirks.
  esptool_py="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
  pio_py="$HOME/.platformio/penv/bin/python"
  if [[ ! -f "$esptool_py" || ! -x "$pio_py" ]]; then
    echo "esptool or the PlatformIO python was not found (~/.platformio); cannot flash." >&2
    exit 1
  fi
  exec "$pio_py" "$esptool_py" --chip esp32s3 --port "$upload_port" --baud 460800 \
    --before default-reset write_flash "${flash_args[@]}"
fi

echo "Rebooting the board into the bootloader on $upload_port..."
if ! "$py" "$root/firmware/tools/bootloader.py" "$upload_port"; then
  echo "Bootloader reboot failed on '$upload_port'; aborting before build." >&2
  exit 1
fi

for _ in 1 2 3 4 5 6 7 8 9 10; do
  [[ -e "$upload_port" ]] && break
  sleep 0.2
done

if [[ ! -e "$upload_port" ]]; then
  echo "Serial device '$upload_port' did not reappear after bootloader reboot; aborting before build." >&2
  exit 1
fi

exec "${pio[@]}" run -e "$env" -d "$root/firmware" -t upload "${args[@]}"
