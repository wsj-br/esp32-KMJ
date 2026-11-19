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
profile=debug
if [[ "${1:-}" == "--release" ]]; then
  cargo_args+=(--release)
  profile=release
  shift
fi

# GNOME (Wayland) uses the GTK application id, not the window pixmap. Match
# tauri.conf.json identifier so the dash shows the KMJ icon instead of a gear.
install_linux_launcher() {
  [[ "$(uname -s)" == Linux ]] || return 0
  local data="${XDG_DATA_HOME:-$HOME/.local/share}"
  local apps="$data/applications"
  local hicolor="$data/icons/hicolor"
  local src="$root/desktop/app/src-tauri/icons"
  local id=com.kmj.desktop
  mkdir -p "$apps" \
    "$hicolor/32x32/apps" \
    "$hicolor/128x128/apps" \
    "$hicolor/256x256/apps" \
    "$hicolor/512x512/apps" \
    "$hicolor/scalable/apps"
  cp "$src/32x32.png" "$hicolor/32x32/apps/$id.png"
  cp "$src/128x128.png" "$hicolor/128x128/apps/$id.png"
  cp "$src/128x128@2x.png" "$hicolor/256x256/apps/$id.png"
  cp "$src/icon.png" "$hicolor/512x512/apps/$id.png"
  cp "$root/brand/app-icon.svg" "$hicolor/scalable/apps/$id.svg"
  cat > "$apps/$id.desktop" <<EOF
[Desktop Entry]
Type=Application
Name=KMJ
Comment=USB keyboard and mouse capture client
Exec=$root/desktop/target/$profile/kmj-app
Icon=$src/icon.png
Terminal=false
Categories=Utility;
StartupNotify=true
StartupWMClass=$id
EOF
  if command -v update-desktop-database >/dev/null 2>&1; then
    update-desktop-database "$apps" >/dev/null 2>&1 || true
  fi
  if command -v gtk-update-icon-cache >/dev/null 2>&1; then
    gtk-update-icon-cache -f "$hicolor" >/dev/null 2>&1 || true
  fi
}

install_linux_launcher

# Bash 3.2, the macOS /bin/bash, treats an empty "${array[@]}" as unbound under set -u.
exec cargo run --manifest-path "$root/desktop/Cargo.toml" -p kmj-app ${cargo_args[@]+"${cargo_args[@]}"} "$@"
