#!/usr/bin/env bash
# Build the production firmware image for every supported board.
# Usage: scripts/build-all-firmware.sh [extra pio run args]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

"$root/scripts/build-firmware.sh" kmj "$@"
"$root/scripts/build-firmware.sh" kmj-ws185b "$@"
