#!/bin/sh
# OpenMail's OS 3.x programs with the os32 stove (bebbo's m68k-amigaos-gcc 6.5,
# NDK 3.2) and the AmiSSL 5 SDK's headers. Soft float: an A1200 has no FPU.
#   build-os3.sh AMISSL_INCLUDE [OUT_DIR]       (default build/os3)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
AMISSL=${1:?give the include folder of the AmiSSL SDK}
OUT=${2:-$HERE/build/os3}
mkdir -p "$OUT"
"$CC" -noixemul -m68020 -std=gnu99 -Wall -Werror -O2 -I"$HERE/engine" -I"$HERE/platform/amiga" -I"$AMISSL" \
    "$HERE"/engine/*.c "$HERE"/platform/amiga/*.c "$HERE/tools/openmailcheck.c" -o "$OUT/OpenMailCheck"
echo "$OUT/OpenMailCheck ($(wc -c < "$OUT/OpenMailCheck") bytes)"
# OpenMail itself: the GadTools window (app/) on the same engine.
"$CC" -noixemul -m68020 -std=gnu99 -Wall -Werror -Wno-pointer-sign -O2 -fno-common -I"$HERE/engine" -I"$HERE/platform/amiga" -I"$HERE/app" -I"$AMISSL" \
    "$HERE"/engine/*.c "$HERE"/platform/amiga/*.c "$HERE"/app/*.c -lamiga -o "$OUT/OpenMail"
echo "$OUT/OpenMail ($(wc -c < "$OUT/OpenMail") bytes)"
