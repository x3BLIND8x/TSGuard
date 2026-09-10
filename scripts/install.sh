#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
  echo "Usage: $0 /path/to/tsguard.so" >&2
  exit 2
fi

SRC="$1"
DEST="${TS3_PLUGIN_DIR:-$HOME/.ts3client/plugins}"
LEGACY="$DEST/blondguard.so"
TARGET="$DEST/tsguard.so"

mkdir -p "$DEST"
install -m 0755 "$SRC" "$TARGET"

# v0.2.2 renamed the plugin binary. Remove the previous filename so TeamSpeak
# cannot accidentally load both the legacy copy and TSGuard at the same time.
if [[ -f "$LEGACY" ]]; then
  rm -f "$LEGACY"
  printf 'Removed legacy plugin binary: %s\n' "$LEGACY"
fi

printf 'Installed TSGuard to: %s\nRestart TeamSpeak 3, then enable it under Tools -> Options -> Addons/Plugins.\n' "$TARGET"
