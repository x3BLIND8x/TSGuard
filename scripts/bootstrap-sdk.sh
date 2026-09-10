#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
DST="$ROOT/third_party/ts3client-pluginsdk"

if [[ -f "$DST/include/ts3_functions.h" ]]; then
  echo "TeamSpeak Plugin SDK already present: $DST"
  exit 0
fi

mkdir -p "$(dirname "$DST")"
git clone --depth 1 --branch 26 https://github.com/teamspeak/ts3client-pluginsdk.git "$DST"
echo "Fetched official TeamSpeak 3 Plugin SDK tag 26."
