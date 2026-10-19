#!/bin/sh
# OpenMail's OS 3.x programs with the os32-gcc16 stove (OpenAmigaGCC
# 16.2.0-open2, libnix, OpenNDK). TLS is OpenTLS (opentls.library, its
# headers in third_party/opentls), and nothing else: no AmiSSL SDK is needed
# or used. Built for the 68040, soft float (no FPU code).
#   build-os3.sh [OUT_DIR]       (default build/os3)
#
#   STOVE   the stove (default ~/AmigaChrome/stoves/os32-gcc16; ~/AmigaChrome/stoves/os32
#           is GCC 6.5, which builds it too)
#
# Before 0.3.3 the first argument was the AmiSSL 5 SDK's include folder (or
# "-" for none). Such a first argument, followed by OUT_DIR, is still taken
# and skipped, so older callers (OpenUp's build_pack.py) build OpenMail as
# it is now rather than into the SDK's folder.
set -eu
HERE=$(cd "$(dirname "$0")" && pwd)
STOVE=${STOVE:-$HOME/AmigaChrome/stoves/os32-gcc16}
CC="$STOVE/prefix/bin/m68k-amigaos-gcc"
if [ $# -ge 1 ] && { [ "$1" = "-" ] || [ -f "$1/proto/amissl.h" ]; }; then
    [ "$1" = "-" ] || echo "build-os3.sh: OpenMail is OpenTLS only now; $1 (an AmiSSL SDK) is not used"
    shift
fi
OUT=${1:-$HERE/build/os3}
TLS="-I$HERE/third_party/opentls/include"
mkdir -p "$OUT"
# Soft float: -m68040 links libnix's soft libm020, not libm881
# (specs/m68k-soft-m68040.specs: -m68040 alone would link the FPU libm).
SOFT="-specs=$HERE/specs/m68k-soft-m68040.specs"
COMMON="-noixemul -m68040 $SOFT -std=gnu99 -Wall -Werror -O2 -fno-delete-null-pointer-checks"
# shellcheck disable=SC2086
"$CC" $COMMON -I"$HERE/engine" -I"$HERE/platform/amiga" $TLS \
    "$HERE"/engine/*.c "$HERE"/platform/amiga/*.c "$HERE/tools/openmailcheck.c" -o "$OUT/OpenMailCheck"
echo "$OUT/OpenMailCheck ($(wc -c < "$OUT/OpenMailCheck") bytes)"
# OpenMail itself: the desk (app/), drawn with OpenGadTools (third_party/), on the same engine.
# shellcheck disable=SC2086
"$CC" $COMMON -Wno-pointer-sign -fno-common -I"$HERE/engine" -I"$HERE/platform/amiga" -I"$HERE/app" -I"$HERE/third_party/opengadtools" $TLS \
    "$HERE"/engine/*.c "$HERE"/platform/amiga/*.c "$HERE"/app/*.c "$HERE"/third_party/opengadtools/*.c -lamiga -o "$OUT/OpenMail"
echo "$OUT/OpenMail ($(wc -c < "$OUT/OpenMail") bytes)"
