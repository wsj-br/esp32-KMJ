#!/usr/bin/env bash
# Build the KMJ firmware for the Waveshare ESP32-S3-Touch-LCD-1.85B.
# Usage: scripts/build-ws185b.sh [kmj-ws185b|kmj-ws185b-debug] [extra pio run args]
# kmj-ws185b is the HID-only production image; kmj-ws185b-debug adds a USB
# serial port.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

env="kmj-ws185b"
if [[ $# -gt 0 && "$1" != -* ]]; then
  env="$1"
  shift
fi

case "$env" in
  kmj-ws185b|kmj-ws185b-debug) ;;
  *)
    echo "Unknown environment '$env' for the Waveshare 1.85B. Use kmj-ws185b or kmj-ws185b-debug." >&2
    exit 1
    ;;
esac

exec "$root/scripts/build-firmware.sh" "$env" "$@"
