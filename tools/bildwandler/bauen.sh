#!/bin/sh
# bildwandler bauen: macOS (nativ) und Linux ARMv7 (MiSTer, statisch).
# Aufruf: ./bauen.sh            beide
#         ./bauen.sh mac|arm    nur eins
# Ergebnisse in bau/: bildwandler-macos, bildwandler-linux-armv7
set -e
cd "$(dirname "$0")"
mkdir -p bau
QUELLEN="bildwandler.c"
WARN="-Wall -Wextra -Wno-unused-function -Wno-sign-compare"
ZIG=${ZIG:-/opt/homebrew/bin/zig}
ziel=${1:-alle}

if [ "$ziel" = alle ] || [ "$ziel" = mac ]; then
    clang -O2 $WARN -o bau/bildwandler-macos $QUELLEN -lm
    echo "bau/bildwandler-macos"
fi
if [ "$ziel" = alle ] || [ "$ziel" = arm ]; then
    "$ZIG" cc -target arm-linux-musleabihf -mcpu=cortex_a9 -O2 -static -s $WARN -o bau/bildwandler-linux-armv7 $QUELLEN -lm
    echo "bau/bildwandler-linux-armv7"
fi
rm -f bau/*.o
