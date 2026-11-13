#!/bin/bash
# Builds the plugin and assembles dist/: CampFundsWithdraw.asi + CampFundsWithdraw.ini (a DMM ASI mod).
# Since 0.2.0 no game file is overridden (the menu entry is added at runtime).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

"$ROOT/build.sh"
DIST="$ROOT/dist"
rm -rf -- "$DIST"
mkdir -p "$DIST"
cp "$ROOT/build/CampFundsWithdraw.asi" "$ROOT/dist-src/CampFundsWithdraw.ini" "$DIST/"
# DMM names the plugin from the modinfo.json in its folder instead of the file name.
VERSION=$(sed -n 's/^#define MOD_VERSION "\(.*\)"/\1/p' "$ROOT/src/main.cpp")
python3 - "$DIST/modinfo.json" "$VERSION" "$ROOT/nexusmods/short-description.txt" <<'PY'
import json, sys
out, version, desc = sys.argv[1:]
info = {"title": "Withdraw Camp Funds", "version": version, "author": "Marukooh",
        "description": open(desc, encoding="utf-8").read().strip()}
open(out, "w", encoding="utf-8").write(json.dumps(info, indent=2, ensure_ascii=False) + "\n")
PY
find "$DIST" -type f | sort
