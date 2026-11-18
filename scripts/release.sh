#!/usr/bin/env bash
# Publish a GitHub release from HEAD.
#
# Version comes from desktop/Cargo.toml, desktop/app/src-tauri/tauri.conf.json,
# and firmware/include/pins.hpp. All three must be the same MAJOR.MINOR.PATCH.
# Notes come from release-notes/RELEASE_NOTES_<version>.md.
#
# If tag v<version> or a GitHub release for it already exists, it is removed
# and the tag is recreated at the current HEAD, then pushed.
#
# Publishing the release triggers .github/workflows/ci.yml. This script does
# not build firmware or the desktop app.
#
# Usage:
#   ./scripts/release.sh
#   ./scripts/release.sh --dry-run
#   ./scripts/release.sh --verify-clean=false
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

VERIFY_CLEAN=true
DRY_RUN=false

for arg in "$@"; do
  case "$arg" in
    --verify-clean=false) VERIFY_CLEAN=false ;;
    --verify-clean=true) VERIFY_CLEAN=true ;;
    --dry-run) DRY_RUN=true ;;
    -h|--help)
      cat <<'EOF'
Usage: ./scripts/release.sh [--dry-run] [--verify-clean=true|false]

Options:
  --dry-run            Validate and print planned steps; no deletes, tag, push, or release.
  --verify-clean=true  Require a clean git working tree (default).
  --verify-clean=false Skip the clean-tree check.

Reads the version from desktop/Cargo.toml, tauri.conf.json, and
firmware/include/pins.hpp. Requires release-notes/RELEASE_NOTES_<version>.md.
If tag v<version> or a GitHub release for it already exists, they are removed
and the tag is recreated at HEAD, then pushed to origin. Publishing the release
triggers .github/workflows/ci.yml.
EOF
      exit 0
      ;;
    *)
      echo "Unknown argument: $arg" >&2
      exit 1
      ;;
  esac
done

require_cmd() {
  command -v "$1" >/dev/null 2>&1 || {
    echo "Missing required command: $1" >&2
    exit 1
  }
}

fail() {
  echo "Error: $*" >&2
  exit 1
}

require_cmd gh
require_cmd git
require_cmd python3

git rev-parse --is-inside-work-tree >/dev/null 2>&1 || fail "Not inside a git repository."
gh auth status >/dev/null 2>&1 || fail "GitHub CLI is not authenticated. Run: gh auth login"

VERSION="$(python3 - "$ROOT" <<'PY'
import json
import re
import sys
from pathlib import Path

root = Path(sys.argv[1])
sources = {
    "desktop/Cargo.toml": root / "desktop/Cargo.toml",
    "desktop/app/src-tauri/tauri.conf.json": root / "desktop/app/src-tauri/tauri.conf.json",
    "firmware/include/pins.hpp": root / "firmware/include/pins.hpp",
}
missing = [name for name, path in sources.items() if not path.is_file()]
if missing:
    sys.exit("Version file not found: " + ", ".join(missing))

cargo = re.search(r'(?m)^version = "([^"]+)"', sources["desktop/Cargo.toml"].read_text(encoding="utf-8"))
tauri = json.loads(sources["desktop/app/src-tauri/tauri.conf.json"].read_text(encoding="utf-8")).get("version")
firmware = re.search(
    r'kFirmwareVersion\[\] = "([^"]+)"',
    sources["firmware/include/pins.hpp"].read_text(encoding="utf-8"),
)
found = {
    "desktop/Cargo.toml": cargo.group(1) if cargo else "",
    "desktop/app/src-tauri/tauri.conf.json": tauri or "",
    "firmware/include/pins.hpp": firmware.group(1) if firmware else "",
}
blank = [name for name, value in found.items() if not value]
if blank:
    sys.exit("Could not read a version from: " + ", ".join(blank))
values = set(found.values())
if len(values) != 1:
    detail = ", ".join(f"{name}={value}" for name, value in found.items())
    sys.exit("Version mismatch: " + detail)
version = values.pop()
if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+", version):
    sys.exit(f"Version must be MAJOR.MINOR.PATCH, got: {version}")
print(version)
PY
)"

TAG="v${VERSION}"
NOTES_FILE="release-notes/RELEASE_NOTES_${VERSION}.md"

[[ -f "$NOTES_FILE" ]] || fail "Release notes file not found: $NOTES_FILE"

if [[ "$VERIFY_CLEAN" == "true" ]] && [[ -n "$(git status --porcelain)" ]]; then
  fail "Working tree is not clean. Commit/stash changes or run with --verify-clean=false"
fi

git remote get-url origin >/dev/null 2>&1 || fail "Remote 'origin' not configured."

repo_web_url() {
  local url
  url="$(git remote get-url origin)"
  url="${url%.git}"
  case "$url" in
    git@github.com:*) printf 'https://github.com/%s\n' "${url#git@github.com:}" ;;
    ssh://git@github.com/*) printf 'https://github.com/%s\n' "${url#ssh://git@github.com/}" ;;
    *) printf '%s\n' "$url" ;;
  esac
}

REPO_URL="$(repo_web_url)"
HEAD_COMMIT="$(git rev-parse HEAD)"

remote_tag_exists() {
  [[ -n "$(git ls-remote origin "refs/tags/${TAG}")" ]]
}

local_tag_exists() {
  git rev-parse -q --verify "refs/tags/${TAG}" >/dev/null 2>&1
}

release_exists() {
  gh release view "$TAG" >/dev/null 2>&1
}

recreate_tag_at_head() {
  if [[ "$DRY_RUN" == "true" ]]; then
    echo "[dry-run] HEAD commit: ${HEAD_COMMIT}"
    if release_exists; then
      echo "[dry-run] Would delete GitHub release: ${TAG}"
    fi
    if remote_tag_exists; then
      echo "[dry-run] Would delete remote tag: origin ${TAG}"
    fi
    if local_tag_exists; then
      echo "[dry-run] Would delete local tag: ${TAG}"
    fi
    echo "[dry-run] Would create annotated tag ${TAG} at HEAD and push to origin."
    return 0
  fi

  if release_exists; then
    echo "Deleting existing GitHub release ${TAG} (and its tag on the remote)..."
    gh release delete "$TAG" --yes --cleanup-tag
  elif remote_tag_exists; then
    echo "Deleting remote tag ${TAG}..."
    git push origin ":refs/tags/${TAG}"
  fi

  if local_tag_exists; then
    echo "Deleting local tag ${TAG}..."
    git tag -d "$TAG"
  fi

  echo "Creating annotated tag ${TAG} at HEAD (${HEAD_COMMIT})..."
  git tag -a "$TAG" -m "Release ${TAG}" HEAD

  echo "Pushing tag ${TAG} to origin..."
  git push origin "refs/tags/${TAG}"
}

recreate_tag_at_head

CMD=(gh release create "$TAG" --title "$TAG" --notes-file "$NOTES_FILE")

echo "Release inputs:"
echo "  Tag:        ${TAG}"
echo "  Title:      ${TAG}"
echo "  Notes file: ${NOTES_FILE}"

if [[ "$DRY_RUN" == "true" ]]; then
  echo "[dry-run] Would run:"
  printf '  %q' "${CMD[@]}"
  echo
  exit 0
fi

"${CMD[@]}"
echo "Release created successfully: ${TAG}"
echo ""
echo "Publishing ${TAG} triggers .github/workflows/ci.yml."
echo "See progress at ${REPO_URL}/actions"
echo ""
