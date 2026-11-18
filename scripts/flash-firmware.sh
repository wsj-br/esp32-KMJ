#!/usr/bin/env bash
# Reboot the board into the ROM bootloader, verify the upload port exists,
# build if needed, and flash the KMJ firmware.
# Usage: scripts/flash-firmware.sh [kmj|kmj-debug] [extra pio run args]
#
# First checks that the configured serial device (from --upload-port,
# PLATFORMIO_UPLOAD_PORT, or firmware/platformio.ini, defaulting to
# /dev/ttyACM0) is present, then runs firmware/tools/bootloader.py to enter the
# ROM bootloader before compiling. If the device is missing or cannot enter
# bootloader mode, the script exits before building.
#
# If the board is currently running HID-only kmj (no serial port), hold BOOT,
# tap RESET, and release BOOT first so the ROM bootloader port appears.
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

env="kmj-debug"
if [[ $# -gt 0 && "$1" != -* ]]; then
  env="$1"
  shift
fi

case "$env" in
  kmj|kmj-debug) ;;
  *)
    echo "Unknown environment '$env'. Use kmj or kmj-debug." >&2
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

# Resolve upload port: --upload-port, PLATFORMIO_UPLOAD_PORT, platformio.ini, or /dev/ttyACM0.
upload_port=""
args=("$@")
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
  upload_port="$("$py" - "$root/firmware/platformio.ini" "$env" <<'PY'
import configparser
import sys

ini_path, env_name = sys.argv[1], sys.argv[2]
cp = configparser.ConfigParser()
cp.read(ini_path)
sec = f"env:{env_name}"
seen = set()
port = ""
while sec and sec not in seen and cp.has_section(sec):
    seen.add(sec)
    if cp.has_option(sec, "upload_port"):
        port = cp.get(sec, "upload_port").strip()
        break
    parent = cp.get(sec, "extends", fallback="").strip()
    if parent and not parent.startswith("env:"):
        parent = f"env:{parent}"
    sec = parent
print(port)
PY
)"
fi

if [[ -z "$upload_port" ]]; then
  upload_port="/dev/ttyACM0"
fi

if [[ ! -c "$upload_port" ]]; then
  echo "Serial device '$upload_port' not found; aborting before build." >&2
  echo "Connect the board (or hold BOOT, tap RESET, release BOOT if running HID-only kmj) and try again." >&2
  exit 1
fi

echo "Rebooting the board into the bootloader on $upload_port..."
if ! "$py" "$root/firmware/tools/bootloader.py" "$upload_port"; then
  echo "Bootloader reboot failed on '$upload_port'; aborting before build." >&2
  exit 1
fi

for _ in 1 2 3 4 5 6 7 8 9 10; do
  [[ -c "$upload_port" ]] && break
  sleep 0.2
done

if [[ ! -c "$upload_port" ]]; then
  echo "Serial device '$upload_port' did not reappear after bootloader reboot; aborting before build." >&2
  exit 1
fi

exec "${pio[@]}" run -e "$env" -d "$root/firmware" -t upload "$@"
