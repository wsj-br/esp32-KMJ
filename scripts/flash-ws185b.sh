#!/usr/bin/env bash
# Flash the KMJ firmware on the Waveshare ESP32-S3-Touch-LCD-1.85B.
# Usage: scripts/flash-ws185b.sh [--first-flash] [kmj-ws185b|kmj-ws185b-debug] [extra args]
#
# Defaults to kmj-ws185b-debug because its CDC port allows the upload to
# auto-reset once KMJ is installed. Use --first-flash while the board still
# runs the stock firmware (the 1200 baud bootloader reboot does nothing
# there); esptool then flashes over the USB-Serial/JTAG port directly.
# Port selection and the --first-flash path are handled by
# scripts/flash-firmware.sh; see its header for details.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

env="kmj-ws185b-debug"
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

exec "$root/scripts/flash-firmware.sh" "$env" "$@"
