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
find "$DIST" -type f | sort
