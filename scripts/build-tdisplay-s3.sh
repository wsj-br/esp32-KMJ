#!/usr/bin/env bash
# Build the KMJ firmware for the LilyGO T-Display-S3 Pro.
# Usage: scripts/build-tdisplay-s3.sh [kmj|kmj-debug] [extra pio run args]
# kmj is the HID-only production image; kmj-debug adds a USB serial port.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

env="kmj"
if [[ $# -gt 0 && "$1" != -* ]]; then
  env="$1"
  shift
fi

case "$env" in
  kmj|kmj-debug) ;;
  *)
    echo "Unknown environment '$env' for the LilyGO T-Display-S3 Pro. Use kmj or kmj-debug." >&2
    exit 1
    ;;
esac

exec "$root/scripts/build-firmware.sh" "$env" "$@"
