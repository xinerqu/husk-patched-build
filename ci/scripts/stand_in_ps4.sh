#!/usr/bin/env bash
# Put a stand-in libhuskps4.dylib where the app expects the PS4 layer.
#
# The real one is built by scripts/build_ps4_ios.sh from two private trees (Bachata-S4 on branch `husk`, husk-fex on `husk-fex`) that
# are not public, so a CI runner cannot build it. The app links and embeds it, and refuses to package without it . (package_ipa.sh checks for it.) In place of it:
# a dylib exporting the one symbol the app looks up, husk_console_module_v1, whose entry point says plainly that this build has no
# PS4 layer. Everything this test build is about (the Android translation layer, Godot included) is unaffected; PS4 games are not
# in it.
#
#   ci/scripts/stand_in_ps4.sh <husk checkout>
set -euo pipefail
ROOT="${1:?husk checkout}"
OUT="$ROOT/build/ios-arm64/lib"
mkdir -p "$OUT"
if [ -f "$OUT/libhuskps4.real" ]; then cp "$OUT/libhuskps4.real" "$OUT/libhuskps4.dylib"; exit 0; fi
if [ -f "$OUT/libhuskps4.dylib" ] && [ -f "$OUT/libhuskps4.revision" ]; then echo "==> libhuskps4.dylib is already there"; exit 0; fi

WORK="$(mktemp -d)"
cat > "$WORK/standin.c" <<'C'
/* The console module interface (src/translation-layer-next/husk-console-host.h) and nothing else. */
typedef struct { int version; int (*unused)(void); } husk_console_module;
static int no_ps4(void) { return 0; }
__attribute__((visibility("default"))) husk_console_module *husk_console_module_v1(void)
{
    static husk_console_module m = { 1, no_ps4 };
    return &m;   /* a build of Husk without its PS4 layer */
}
C
SDK="$(xcrun --sdk iphoneos --show-sdk-path)"
xcrun --sdk iphoneos clang -target arm64-apple-ios17.4 -isysroot "$SDK" -O2 -dynamiclib \
    -fvisibility=hidden -install_name @rpath/libhuskps4.dylib -o "$OUT/libhuskps4.dylib" "$WORK/standin.c"
rm -f "$OUT/libhuskps4.revision"
echo "stand-in (no PS4 layer in this build)" > "$OUT/libhuskps4.revision"
nm -gU "$OUT/libhuskps4.dylib" | awk '{print "  export:", $3}'
echo "==> stand-in libhuskps4.dylib at $OUT"
