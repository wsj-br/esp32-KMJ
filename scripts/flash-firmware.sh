#!/usr/bin/env bash
# Build if needed, then flash the KMJ firmware.
# Usage: scripts/flash-firmware.sh [kmj|kmj-debug] [extra pio run args]
#
# kmj has no serial port while it is running. Hold BOOT, tap RESET, release
# BOOT, then run this script. kmj-debug keeps a CDC port so upload can reset
# the board. Pass --upload-port /dev/ttyACM0 (or another port) to override
# firmware/platformio.ini.
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

env="kmj"
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

if [[ "$env" == "kmj" ]]; then
  echo "kmj has no serial port while it is running."
  echo "Hold BOOT, tap RESET, release BOOT, then leave the cable connected."
fi

exec "${pio[@]}" run -e "$env" -d "$root/firmware" -t upload "$@"
