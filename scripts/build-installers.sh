#!/usr/bin/env bash
# Build native installers for the KMJ desktop app.
# Usage: scripts/build-installers.sh [--install-cli] [--dry-run] [extra cargo tauri build args]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
manifest="$root/desktop/app/src-tauri/Cargo.toml"
bundle_dir="${CARGO_TARGET_DIR:-$root/desktop/target}/release/bundle"

INSTALL_CLI=false
DRY_RUN=false
extra=()

usage() {
  cat <<'EOF'
Usage: ./scripts/build-installers.sh [--install-cli] [--dry-run] [extra cargo tauri build args]

Build the KMJ desktop installers for the current host OS with Tauri CLI 2.

Options:
  --install-cli  Install or update tauri-cli ^2 if it is missing or not v2.
  --dry-run      Print the planned cargo tauri command; do not build.
  -h, --help     Show this help.

Remaining arguments are passed to `cargo tauri build`. Examples:

  ./scripts/build-installers.sh
  ./scripts/build-installers.sh --install-cli
  ./scripts/build-installers.sh --bundles deb

Linux needs the WebKitGTK development packages and produces .deb / AppImage
bundles. Windows produces NSIS or MSI. macOS produces a .dmg.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --install-cli) INSTALL_CLI=true; shift ;;
    --dry-run) DRY_RUN=true; shift ;;
    -h|--help)
      usage
      exit 0
      ;;
    --)
      shift
      extra+=("$@")
      break
      ;;
    *)
      extra+=("$1")
      shift
      ;;
  esac
done

if ! command -v cargo >/dev/null 2>&1; then
  echo "cargo is not on PATH. Install Rust 1.80 or newer." >&2
  exit 1
fi

tauri_major() {
  local line
  line="$(cargo tauri --version 2>/dev/null || true)"
  if [[ "$line" =~ ([0-9]+)\. ]]; then
    echo "${BASH_REMATCH[1]}"
  fi
}

ensure_tauri_cli() {
  local major
  major="$(tauri_major)"
  if [[ "$major" == "2" ]]; then
    return 0
  fi
  if [[ -z "$major" ]]; then
    echo "tauri-cli 2 is not installed." >&2
  else
    echo "tauri-cli $major is installed; this project needs version 2." >&2
  fi
  if [[ "$DRY_RUN" == true ]]; then
    if [[ "$INSTALL_CLI" == true ]]; then
      echo '[dry-run] Would run: cargo install tauri-cli --version "^2"'
    else
      echo 'Install it with: cargo install tauri-cli --version "^2"' >&2
      echo "Or rerun with --install-cli." >&2
    fi
    return 0
  fi
  if [[ "$INSTALL_CLI" != true ]]; then
    echo 'Install it with: cargo install tauri-cli --version "^2"' >&2
    echo "Or rerun with --install-cli." >&2
    exit 1
  fi
  echo "Installing tauri-cli ^2..."
  cargo install tauri-cli --version "^2"
  major="$(tauri_major)"
  if [[ "$major" != "2" ]]; then
    echo "tauri-cli 2 is still not available after install." >&2
    exit 1
  fi
}

cmd=(cargo tauri build --manifest-path "$manifest")
if [[ ${#extra[@]} -gt 0 ]]; then
  cmd+=("${extra[@]}")
fi

ensure_tauri_cli

echo "Building KMJ installers for $(uname -s)..."
if [[ "$DRY_RUN" == true ]]; then
  printf '[dry-run] Would run:'
  printf ' %q' "${cmd[@]}"
  echo
  echo "[dry-run] Bundles would be written under $bundle_dir"
  exit 0
fi

"${cmd[@]}"

if [[ ! -d "$bundle_dir" ]]; then
  echo "Build finished, but no bundle directory was found at $bundle_dir" >&2
  exit 1
fi

echo
echo "Installers in $bundle_dir:"
found=false
while IFS= read -r file; do
  [[ -z "$file" ]] && continue
  found=true
  echo "  $file"
done < <(find "$bundle_dir" \( \
  -name '*.deb' -o -name '*.rpm' -o -name '*.AppImage' \
  -o -name '*.msi' -o -name '*.exe' -o -name '*.dmg' \
  \) -type f | sort)

if [[ "$found" != true ]]; then
  echo "  (no installer files matched; inspect the bundle directory)"
fi
