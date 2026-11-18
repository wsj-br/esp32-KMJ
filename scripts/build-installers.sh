#!/usr/bin/env bash
# Build native installers for the KMJ desktop app.
# Usage: scripts/build-installers.sh [--dry-run] [--copyright-files] [extra cargo tauri build args]
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
bundle_dir="${CARGO_TARGET_DIR:-$root/desktop/target}/release/bundle"

DRY_RUN=false
COPYRIGHT_FILES=false
extra=()

usage() {
  cat <<'EOF'
Usage: ./scripts/build-installers.sh [--dry-run] [--copyright-files] [extra cargo tauri build args]

Build the KMJ desktop installers for the current host OS with Tauri CLI 2.
Installs or updates tauri-cli to the latest stable 2.x release when the
installed copy is missing or older.

Options:
  --dry-run          Print the planned commands; do not install or build.
  --copyright-files  Keep linuxdeploy's per-library copyright scan in the
                     AppImage. The default skips that scan.
  -h, --help         Show this help.

Remaining arguments are passed to `cargo tauri build`. Examples:

  ./scripts/build-installers.sh
  ./scripts/build-installers.sh --bundles deb
  ./scripts/build-installers.sh --copyright-files

Linux needs the WebKitGTK development packages and produces .deb / AppImage
bundles. Windows produces NSIS or MSI. macOS produces a .dmg. The AppImage
build sets DISABLE_COPYRIGHT_FILES_DEPLOYMENT=1 unless --copyright-files is
given. deb and rpm do not use that scan.
EOF
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --install-cli) shift ;; # install and update are automatic
    --dry-run) DRY_RUN=true; shift ;;
    --copyright-files) COPYRIGHT_FILES=true; shift ;;
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

# True when $1 is a newer x.y.z than $2. Pre-release suffixes are not accepted.
version_gt() {
  local a_major a_minor a_patch b_major b_minor b_patch
  IFS=. read -r a_major a_minor a_patch <<< "$1"
  IFS=. read -r b_major b_minor b_patch <<< "$2"
  if (( a_major > b_major || (a_major == b_major && a_minor > b_minor) || (a_major == b_major && a_minor == b_minor && a_patch > b_patch) )); then
    return 0
  fi
  return 1
}

tauri_version() {
  local line
  line="$(cargo tauri --version 2>/dev/null || true)"
  if [[ "$line" =~ ([0-9]+\.[0-9]+\.[0-9]+) ]]; then
    echo "${BASH_REMATCH[1]}"
  fi
}

# Latest non-yanked stable 2.x on crates.io. Tauri 3 prereleases are ignored.
latest_stable_tauri_v2() {
  local index line vers best
  index="$(curl -fsSL --retry 2 --max-time 30 \
    -H 'User-Agent: kmj-build-installers' \
    'https://index.crates.io/ta/ur/tauri-cli')" || return 1
  best=""
  while IFS= read -r line; do
    [[ "$line" =~ \"vers\":\"(2\.[0-9]+\.[0-9]+)\" ]] || continue
    vers="${BASH_REMATCH[1]}"
    [[ "$line" =~ \"yanked\":true ]] && continue
    if [[ -z "$best" ]] || version_gt "$vers" "$best"; then
      best="$vers"
    fi
  done <<< "$index"
  [[ -n "$best" ]] || return 1
  printf '%s\n' "$best"
}

ensure_tauri_cli() {
  local installed latest
  installed="$(tauri_version)"
  if ! command -v curl >/dev/null 2>&1; then
    if [[ "$installed" == 2.* ]]; then
      echo "curl is not on PATH, so tauri-cli was not checked for updates. Continuing with $installed." >&2
      return 0
    fi
    echo "curl is not on PATH, so tauri-cli 2 cannot be installed automatically." >&2
    exit 1
  fi
  if ! latest="$(latest_stable_tauri_v2)"; then
    if [[ "$installed" == 2.* ]]; then
      echo "Could not check crates.io for a tauri-cli update. Continuing with $installed." >&2
      return 0
    fi
    echo "Could not reach crates.io to install tauri-cli 2." >&2
    echo 'Install it with: cargo install tauri-cli --version "^2"' >&2
    exit 1
  fi
  if [[ "$installed" == 2.* ]] && ! version_gt "$latest" "$installed"; then
    return 0
  fi
  if [[ "$DRY_RUN" == true ]]; then
    echo "[dry-run] Would run: cargo install tauri-cli --version $latest --force"
    return 0
  fi
  if [[ -z "$installed" ]]; then
    echo "Installing tauri-cli $latest..."
  elif [[ "$installed" == 2.* ]]; then
    echo "Updating tauri-cli $installed to $latest..."
  else
    echo "Replacing tauri-cli $installed with $latest..."
  fi
  cargo install tauri-cli --version "$latest" --force
  installed="$(tauri_version)"
  if [[ "$installed" != "$latest" ]]; then
    echo "tauri-cli $latest is still not available after install (found: ${installed:-none})." >&2
    exit 1
  fi
}

# Tauri CLI 2 locates tauri.conf.json from the working directory. It does not
# accept Cargo's --manifest-path. From the repo root the walk reaches
# desktop/app/src-tauri.
cmd=(cargo tauri build)
if [[ ${#extra[@]} -gt 0 ]]; then
  cmd+=("${extra[@]}")
fi

ensure_tauri_cli

# linuxdeploy looks up a copyright file for every bundled library. That scan
# is sequential and is most of the AppImage time. deb and rpm ignore the variable.
if [[ "$COPYRIGHT_FILES" == true ]]; then
  unset DISABLE_COPYRIGHT_FILES_DEPLOYMENT
else
  export DISABLE_COPYRIGHT_FILES_DEPLOYMENT=1
fi

echo "Building KMJ installers for $(uname -s)..."
if [[ "$DRY_RUN" == true ]]; then
  printf '[dry-run] Would run:'
  if [[ "$COPYRIGHT_FILES" != true ]]; then
    printf ' %q' DISABLE_COPYRIGHT_FILES_DEPLOYMENT=1
  fi
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
