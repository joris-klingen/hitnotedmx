#!/bin/zsh
# HitNoteDmx installer — double-click to install the plugin + standalone on
# THIS machine. Copies the bundles from the folder this script sits in
# (falling back to the Dropbox release folder), then strips the quarantine
# flag so Gatekeeper doesn't block the ad-hoc-signed binaries.
#
# Release procedure (dev machine): see CLAUDE.md — build + ctest, then copy
# HitNoteDmx.vst3, HitNoteDmx.app and this script into the Dropbox folder.
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
DROPBOX="$HOME/Library/CloudStorage/Dropbox/Music/Hitmix/9_tech/hitnotedmx_installer"

SRC="$HERE"
if [ ! -d "$SRC/HitNoteDmx.vst3" ]; then
  SRC="$DROPBOX"
fi
if [ ! -d "$SRC/HitNoteDmx.vst3" ]; then
  echo "ERROR: no HitNoteDmx.vst3 found next to this script or in $DROPBOX" >&2
  exit 1
fi

VST3_DEST="$HOME/Library/Audio/Plug-Ins/VST3"
APP_DEST="/Applications"
[ -w "$APP_DEST" ] || APP_DEST="$HOME/Applications"
mkdir -p "$VST3_DEST" "$APP_DEST"

echo "Installing HitNoteDmx from: $SRC"

rm -rf "$VST3_DEST/HitNoteDmx.vst3"
cp -R "$SRC/HitNoteDmx.vst3" "$VST3_DEST/"
xattr -dr com.apple.quarantine "$VST3_DEST/HitNoteDmx.vst3" 2>/dev/null || true
echo "  VST3       -> $VST3_DEST/HitNoteDmx.vst3"

if [ -d "$SRC/HitNoteDmx.app" ]; then
  rm -rf "$APP_DEST/HitNoteDmx.app"
  cp -R "$SRC/HitNoteDmx.app" "$APP_DEST/"
  xattr -dr com.apple.quarantine "$APP_DEST/HitNoteDmx.app" 2>/dev/null || true
  echo "  Standalone -> $APP_DEST/HitNoteDmx.app"
fi

echo "Done. Rescan plug-ins in Live (Preferences > Plug-Ins) if it was running."
