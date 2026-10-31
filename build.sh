#!/bin/bash
# Builds build/CampFundsWithdraw.asi (and the offline signature checker build/cfw_check.exe).
set -euo pipefail
cd "$(dirname "$0")"
mkdir -p build
FLAGS="-O2 -std=c++17 -Wall -Wextra -Wno-unused-parameter -static -static-libgcc -static-libstdc++ -fno-exceptions -fno-rtti"
x86_64-w64-mingw32-g++ $FLAGS -shared -o build/CampFundsWithdraw.asi src/main.cpp -Wl,--kill-at -s
x86_64-w64-mingw32-g++ $FLAGS -DCFW_TEST_HARNESS -o build/cfw_check.exe src/main.cpp -Wl,--image-base=0x10000000
echo "built: $(ls -la build/CampFundsWithdraw.asi)"
