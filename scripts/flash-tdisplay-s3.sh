#!/usr/bin/env bash
# Flash the KMJ firmware on the LilyGO T-Display-S3 Pro.
# Usage: scripts/flash-tdisplay-s3.sh [--first-flash] [kmj|kmj-debug] [extra args]
#
# Defaults to kmj-debug because its CDC port allows the upload to auto-reset.
# The HID-only kmj env has no serial port while running: hold BOOT, tap RESET,
# release BOOT first (or pass --first-flash with the ROM bootloader port up).
# Port selection and the --first-flash path are handled by
# scripts/flash-firmware.sh; see its header for details.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

env="kmj-debug"
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

exec "$root/scripts/flash-firmware.sh" "$env" "$@"
