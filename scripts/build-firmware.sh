#!/usr/bin/env bash
# Build the KMJ firmware. Default environment is kmj (HID only).
# Usage: scripts/build-firmware.sh [kmj|kmj-debug] [extra pio run args]
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

exec "${pio[@]}" run -e "$env" -d "$root/firmware" "$@"
