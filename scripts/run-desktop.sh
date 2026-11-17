#!/usr/bin/env bash
# Build if needed, then launch the KMJ desktop app (debug by default).
# Usage: scripts/run-desktop.sh [--release] [extra cargo run args]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

if ! command -v cargo >/dev/null 2>&1; then
  echo "cargo is not on PATH. Install Rust 1.80 or newer." >&2
  exit 1
fi

cargo_args=()
if [[ "${1:-}" == "--release" ]]; then
  cargo_args+=(--release)
  shift
fi

exec cargo run --manifest-path "$root/desktop/Cargo.toml" -p kmj-app "${cargo_args[@]}" "$@"
