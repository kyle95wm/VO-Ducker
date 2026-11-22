#!/usr/bin/env bash
# VODucker installer for macOS and Linux.
#
# Run it from an extracted release zip:      ./install.sh
# Or install the latest release in one go:   curl -fsSL https://raw.githubusercontent.com/kyle95wm/VO-Ducker/main/scripts/install.sh | bash
#
# Installs for the current user only (no sudo needed):
#   macOS: ~/Library/Audio/Plug-Ins/VST3 and ~/Library/Audio/Plug-Ins/Components (AU)
#   Linux: ~/.vst3
set -euo pipefail

REPO="kyle95wm/VO-Ducker"
OS="$(uname -s)"

case "$OS" in
  Darwin) PLATFORM="macOS"; VST3_DIR="$HOME/Library/Audio/Plug-Ins/VST3"; AU_DIR="$HOME/Library/Audio/Plug-Ins/Components" ;;
  Linux)  PLATFORM="Linux"; VST3_DIR="$HOME/.vst3"; AU_DIR="" ;;
  *) echo "Unsupported OS: $OS (on Windows use install.ps1)"; exit 1 ;;
esac

SRC_DIR=""
SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]:-$0}")" 2>/dev/null && pwd || true)"
if [[ -n "$SCRIPT_DIR" && -d "$SCRIPT_DIR/VODucker.vst3" ]]; then
  SRC_DIR="$SCRIPT_DIR"
else
  echo "Fetching the latest VODucker release for $PLATFORM..."
  TMP="$(mktemp -d)"
  trap 'rm -rf "$TMP"' EXIT
  URL="$(curl -fsSL "https://api.github.com/repos/$REPO/releases/latest" \
        | grep -o "\"browser_download_url\": *\"[^\"]*-$PLATFORM\.zip\"" \
        | head -1 | sed 's/.*"\(https[^"]*\)"/\1/')"
  if [[ -z "$URL" ]]; then
    echo "Couldn't find a $PLATFORM download in the latest release of $REPO."; exit 1
  fi
  curl -fL --progress-bar "$URL" -o "$TMP/VODucker.zip"
  unzip -q "$TMP/VODucker.zip" -d "$TMP"
  SRC_DIR="$TMP/VODucker"
fi

install_bundle() {
  local src="$1" dest_dir="$2"
  mkdir -p "$dest_dir"
  rm -rf "$dest_dir/$(basename "$src")"
  cp -R "$src" "$dest_dir/"
  if [[ "$OS" == "Darwin" ]]; then
    # Builds are ad-hoc signed, not notarised: clear the download quarantine flag.
    xattr -dr com.apple.quarantine "$dest_dir/$(basename "$src")" 2>/dev/null || true
  fi
  echo "Installed $(basename "$src") -> $dest_dir"
}

install_bundle "$SRC_DIR/VODucker.vst3" "$VST3_DIR"
if [[ -n "$AU_DIR" && -d "$SRC_DIR/VODucker.component" ]]; then
  install_bundle "$SRC_DIR/VODucker.component" "$AU_DIR"
  killall -9 AudioComponentRegistrar 2>/dev/null || true   # make Logic etc. rescan AUs
fi

echo "Done. Rescan plugins in your DAW if VODucker doesn't show up."
