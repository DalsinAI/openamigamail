#!/bin/sh
# OpenMail's OS 3.x programs with the os32 stove (bebbo's m68k-amigaos-gcc 6.5,
# NDK 3.2). TLS is OpenTLS (opentls.library, its headers in third_party/opentls);
# give the AmiSSL 5 SDK's include folder to build in AmiSSL as well, used only
# when OpenTLS isn't installed. Soft float: an A1200 has no FPU.
#   build-os3.sh [AMISSL_INCLUDE|-] [OUT_DIR]       (default: no AmiSSL, build/os3)
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
AMISSL=${1:--}
OUT=${2:-$HERE/build/os3}
TLS="-I$HERE/third_party/opentls/include"
[ "$AMISSL" != "-" ] && TLS="$TLS -DOAM_AMISSL -I$AMISSL"
mkdir -p "$OUT"
"$CC" -noixemul -m68020 -std=gnu99 -Wall -Werror -O2 -fno-delete-null-pointer-checks -I"$HERE/engine" -I"$HERE/platform/amiga" $TLS \
    "$HERE"/engine/*.c "$HERE"/platform/amiga/*.c "$HERE/tools/openmailcheck.c" -o "$OUT/OpenMailCheck"
echo "$OUT/OpenMailCheck ($(wc -c < "$OUT/OpenMailCheck") bytes)"
# OpenMail itself: the desk (app/), drawn with OpenGadTools (third_party/), on the same engine.
"$CC" -noixemul -m68020 -std=gnu99 -Wall -Werror -Wno-pointer-sign -O2 -fno-delete-null-pointer-checks -fno-common -I"$HERE/engine" -I"$HERE/platform/amiga" -I"$HERE/app" -I"$HERE/third_party/opengadtools" $TLS \
    "$HERE"/engine/*.c "$HERE"/platform/amiga/*.c "$HERE"/app/*.c "$HERE"/third_party/opengadtools/*.c -lamiga -o "$OUT/OpenMail"
echo "$OUT/OpenMail ($(wc -c < "$OUT/OpenMail") bytes)"
