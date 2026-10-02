#!/bin/sh
# KyneMail's OS 3.x programs with the os32 stove (bebbo's m68k-amigaos-gcc 6.5,
# NDK 3.2) and the AmiSSL 5 SDK's headers. Soft float: an A1200 has no FPU.
#   build-os3.sh AMISSL_INCLUDE [OUT_DIR]       (default build/os3)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
AMISSL=${1:?give the include folder of the AmiSSL SDK}
OUT=${2:-$HERE/build/os3}
mkdir -p "$OUT"
"$CC" -noixemul -m68020 -std=gnu99 -Wall -O2 -I"$HERE/engine" -I"$HERE/platform/amiga" -I"$AMISSL" \
    "$HERE"/engine/*.c "$HERE"/platform/amiga/*.c "$HERE/tools/kynemailcheck.c" -o "$OUT/KyneMailCheck"
echo "$OUT/KyneMailCheck ($(wc -c < "$OUT/KyneMailCheck") bytes)"
