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
# Normally the script builds first, then runs firmware/tools/bootloader.py to
# enter the ROM bootloader (works when the -debug firmware is running: its CDC
# port reboots into download mode on a 1200 baud touch). The panel can keep
# showing "Waiting for new firmware" for the whole compile if the reboot
# happens first, and a CDC port that never drops is still the application.
# Building first keeps that screen up only for the upload. If the device is
# missing or does not enter bootloader mode, the script exits without flashing.
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
port_in_args=false
for ((i = 0; i < ${#args[@]}; i++)); do
  case "${args[i]}" in
    --upload-port)
      if [[ $((i + 1)) -ge ${#args[@]} || -z "${args[i + 1]}" ]]; then
        echo "Missing value for --upload-port." >&2
        exit 1
      fi
      upload_port="${args[i + 1]}"
      port_in_args=true
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

# Resolve symlinks (e.g. /dev/serial/by-id/*) to the real tty node: after the
# 1200 baud touch the board re-enumerates in download mode under a different
# USB identity, so the by-id name changes while the tty node stays. This must
# happen after autodetection, which selects the by-id path first.
upload_port="$(readlink -f "$upload_port")"

# Forward the resolved port to pio when it was auto-detected, so pio does not
# re-resolve (and possibly pick a different board) on its own.
if [[ -n "$upload_port" && $port_in_args == false ]]; then
  args+=("--upload-port" "$upload_port")
fi

esptool_py="$HOME/.platformio/packages/tool-esptoolpy/esptool.py"
pio_py="$HOME/.platformio/penv/bin/python"
if [[ ! -f "$esptool_py" || ! -x "$pio_py" ]]; then
  echo "esptool or the PlatformIO python was not found (~/.platformio); cannot flash." >&2
  exit 1
fi

# Bootloader, partition table, otadata, and the app. NVS is not in this list:
# 0x9000 on the LilyGO table, 0x650000 on the Waveshare table. firmware.factory.bin
# is one file from 0x0 through the app with every gap filled by 0xFF, so flashing
# it erases the LilyGO NVS partition.
set_flash_args() {
  local build_dir="$1"
  local boot_app0="$HOME/.platformio/packages/framework-arduinoespressif32/tools/partitions/boot_app0.bin"
  local f
  for f in "$build_dir/bootloader.bin" "$build_dir/partitions.bin" "$build_dir/firmware.bin" "$boot_app0"; do
    if [[ ! -f "$f" ]]; then
      echo "Missing flash image: $f" >&2
      return 1
    fi
  done
  flash_args=(
    0x0 "$build_dir/bootloader.bin"
    0x8000 "$build_dir/partitions.bin"
    0xe000 "$boot_app0"
    0x10000 "$build_dir/firmware.bin"
  )
}

# The 1200 baud reboot sets RTC_CNTL_FORCE_DOWNLOAD_BOOT (ESP32-S3
# RTC_CNTL_OPTION1_REG). An RTS "hard reset" on the USB-Serial/JTAG port is
# also defined as download boot (the ROM prints boot:0x23 DOWNLOAD), so it
# cannot start the application. Clear the flag, then reset with the RTC
# watchdog, which boots the flashed image.
boot_application() {
  local port="$1"
  "$pio_py" "$esptool_py" --chip esp32s3 --port "$port" --baud 460800 \
    --before no-reset --after watchdog-reset \
    write-mem 0x6000812C 0 0x1
}

start_application() {
  local port="$1"
  local attempt p
  local -a jtags
  for attempt in 1 2 3 4 5; do
    if ! boot_application "$port"; then
      echo "Reset attempt $attempt failed; retrying..." >&2
    fi
    sleep 2
    if ! compgen -G '/dev/serial/by-id/usb-*USB_JTAG*' >/dev/null; then
      echo "Application is up."
      return 0
    fi
    jtags=()
    for p in /dev/serial/by-id/usb-*USB_JTAG*; do
      [[ -e "$p" ]] || continue
      jtags+=("$p")
    done
    if [[ ${#jtags[@]} -eq 1 ]]; then
      port="$(readlink -f "${jtags[0]}")"
    fi
    echo "Still in download mode after reset attempt $attempt." >&2
  done
  echo "Board did not leave download mode. Unplug USB for a few seconds, then plug it back in." >&2
  return 1
}

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
  elif ! set_flash_args "$build_dir"; then
    echo "No flasher_args.json or firmware images in $build_dir; build failed?" >&2
    exit 1
  fi
  echo "Flashing $env over $upload_port (esptool default reset)..."
  "$pio_py" "$esptool_py" --chip esp32s3 --port "$upload_port" --baud 460800 \
    --before default-reset --after no-reset write-flash "${flash_args[@]}"
  echo "Starting the application on $upload_port..."
  start_application "$upload_port"
  exit $?
fi

echo "Building $env..."
"${pio[@]}" run -e "$env" -d "$root/firmware"

build_dir="$root/firmware/.pio/build/$env"
if ! set_flash_args "$build_dir"; then
  echo "Firmware images not found in $build_dir; build failed?" >&2
  exit 1
fi

echo "Rebooting the board into the bootloader on $upload_port..."
if ! "$py" "$root/firmware/tools/bootloader.py" "$upload_port"; then
  echo "Bootloader reboot failed on '$upload_port'." >&2
  exit 1
fi

# bootloader.py sleeps 2 s, by which the ROM may already have reused the same
# tty node. Identity is what matters: the application CDC must be gone and a
# single "USB JTAG/serial debug unit" must be present. A node that is still
# the application means the 1200 baud handler never reset the chip.
jtag_port=""
for _ in $(seq 1 40); do
  jtags=()
  for p in /dev/serial/by-id/usb-*USB_JTAG*; do
    [[ -e "$p" ]] || continue
    jtags+=("$p")
  done
  if [[ ${#jtags[@]} -eq 1 ]]; then
    jtag_port="$(readlink -f "${jtags[0]}")"
    break
  fi
  if [[ ${#jtags[@]} -gt 1 ]]; then
    echo "Multiple USB JTAG ports; pass --upload-port of the board in download mode:" >&2
    printf '  %s\n' "${jtags[@]}" >&2
    exit 1
  fi
  sleep 0.2
done
if [[ -z "$jtag_port" || ! -c "$jtag_port" ]]; then
  if [[ -e "$upload_port" ]]; then
    echo "The board did not reset off '$upload_port'." >&2
    echo "If the panel says \"Waiting for new firmware\", the restart wedged before the USB reset." >&2
  else
    echo "ROM bootloader (USB JTAG) did not enumerate after the reset." >&2
  fi
  echo "Hold BOOT, tap RESET, release BOOT, then run this script again with --first-flash." >&2
  exit 1
fi
upload_port="$jtag_port"

# Flash with esptool directly instead of pio's uploader: after the 1200 baud
# touch the board re-enumerates under a different USB identity, and pio's own
# second reset dance can abort the write mid-stream, leaving the previous
# image on the flash. The regions come from set_flash_args; NVS is not one
# of them.
echo "Flashing $env over $upload_port..."
# Retry the write: ModemManager probes every newly enumerated serial port and
# races esptool's connection right after each board reset. Stay in the
# bootloader afterwards; an RTS hard reset would just re-enter download mode.
flash_ok=false
for attempt in 1 2 3; do
  if "$pio_py" "$esptool_py" --chip esp32s3 --port "$upload_port" --baud 460800 \
      --before no-reset --after no-reset write-flash "${flash_args[@]}"; then
    flash_ok=true
    break
  fi
  echo "Flash attempt $attempt failed; retrying..." >&2
  sleep 1
done
if [[ $flash_ok != true ]]; then
  echo "Flashing failed after 3 attempts." >&2
  exit 1
fi

echo "Starting the application on $upload_port..."
start_application "$upload_port"
