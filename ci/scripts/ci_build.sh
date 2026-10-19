#!/usr/bin/env bash
# Husk, built on a GitHub macOS runner.
#
#   ci/scripts/ci_build.sh deps        everything Husk links that is not the app: QEMU, ANGLE, the GPU stack, MoltenVK, rppairing
#   ci/scripts/ci_build.sh package     the app and the IPA, with what "deps" built (build/Husk.ipa)
#   ci/scripts/ci_build.sh all [out]   both, from a clean checkout
#
# Same stages as Husk's own script (fetch_sources, build_ios, ANGLE, the GPU stack, QEMU + integrate_husk, the guest kernel,
# MoltenVK, rppairing, package_ipa.sh), with three differences that only a CI run needs:
#
#   - the toolchain the scripts expect (meson, ninja, pkg-config, xcodegen, qemu-img, a Rust iOS target) is installed first;
#   - build/android-system (the apps' /system fonts and CA certificates) and build/java-runtime (the Dalvik runtime) are
#     extracted from the last official Husk release, because scripts/build_android_system.sh needs a 2 GB Lineage image and a
#     mountable ext4 partition, which a runner cannot make;
#   - every stage logs to a file and the failing stage's log tail is printed, so one failed run says what broke instead of the
#     step it happened to be in.
#
# Stages that already have their output skip themselves (Husk's own scripts do that), so re-running against a warm
# third_party/ + build/ only redoes what changed.
set -uo pipefail

# The Husk checkout: $HUSK_ROOT from the caller, or else where this script sits (<checkout>/ci/scripts).
HUSK_ROOT="${HUSK_ROOT:-$(cd "$(dirname "$0")/../.." && pwd)}"
[ -d "$HUSK_ROOT/src/app" ] || { echo "not a Husk checkout: $HUSK_ROOT" >&2; exit 1; }
cd "$HUSK_ROOT"
WHAT="${1:-all}"
OUT="${2:-$HUSK_ROOT/build/Husk.ipa}"
LOGS="$HUSK_ROOT/build/logs"
mkdir -p "$LOGS" "$(dirname "$OUT")"

export HOMEBREW_NO_AUTO_UPDATE=1 HOMEBREW_NO_INSTALL_CLEANUP=1
export PATH="/opt/homebrew/bin:/usr/local/bin:$HOME/.cargo/bin:$PATH"

step() { printf '\n\033[1;34m##### %s\033[0m\n' "$*"; }
die()  { echo "##### FAILED: $*" >&2; exit 1; }

# Run a stage, its output to build/logs/<name>.log and a tail shown as it goes. Returns its status.
stage() {
    local name="$1"; shift
    local log="$LOGS/${name}.log"
    step "$name"
    "$@" > "$log" 2>&1 &
    local pid=$! last=0
    while kill -0 "$pid" 2>/dev/null; do
        sleep 20
        local n; n=$(wc -l < "$log" 2>/dev/null || echo 0)
        if [ "$n" -gt "$last" ]; then
            tail -n $(( n - last > 40 ? 40 : n - last )) "$log"
            last=$n
        fi
    done
    wait "$pid"; local rc=$?
    if [ $rc -ne 0 ]; then
        echo "---- last 80 lines of the $name log ----" >&2
        tail -80 "$log" >&2
        die "$name (exit $rc)"
    fi
}

# ---------------------------------------------------------------- toolchain

step "toolchain"
need=()
command -v meson     >/dev/null || need+=(meson)
command -v ninja     >/dev/null || need+=(ninja)
command -v pkg-config >/dev/null || need+=(pkg-config)
command -v qemu-img  >/dev/null || need+=(qemu)
command -v xcodegen  >/dev/null || need+=(xcodegen)
[ -x "$HOME/.cargo/bin/rustup" ] || need+=(rustup)
if [ ${#need[@]} -gt 0 ]; then
    brew install "${need[@]}" >> "$LOGS/toolchain.log" 2>&1 || { tail -40 "$LOGS/toolchain.log" >&2; die "brew install ${need[*]}"; }
fi
# virglrenderer's mesa needs PyYAML in whichever python3 meson runs (/opt/homebrew/bin/python3 here).
python3 -c 'import yaml' 2>/dev/null || python3 -m pip install --break-system-packages -q pyyaml >> "$LOGS/toolchain.log" 2>&1 \
    || /opt/homebrew/bin/python3 -m pip install --break-system-packages -q pyyaml >> "$LOGS/toolchain.log" 2>&1
python3 -c 'import yaml; print("pyyaml", yaml.__version__)'
"$HOME/.cargo/bin/rustup" target list --installed 2>/dev/null | grep -q aarch64-apple-ios \
    || "$HOME/.cargo/bin/rustup" target add aarch64-apple-ios >> "$LOGS/toolchain.log" 2>&1
xcodebuild -version | head -2
python3 --version
df -h "$HUSK_ROOT" | tail -1

# ------------------------------------------- the parts that need an Android image

prebaked() {
    local assets="${HUSK_PREBUILT:-$HUSK_ROOT/ci/assets}"
    [ -d "$assets/java-runtime" ] || [ -d "$assets/android-system" ] || return 0
    step "prebuilt Java runtime and system fonts"
    [ -d "$assets/java-runtime" ]   && { rm -rf  build/java-runtime;   cp -R "$assets/java-runtime"   build/java-runtime; }
    [ -d "$assets/android-system" ] && { rm -rf  build/android-system; cp -R "$assets/android-system" build/android-system; }
    ls build/java-runtime/husk-framework.dex build/android-system/etc/fonts.xml
}
prebaked

# ---------------------------------------------------------------- build

if [ "$WHAT" = all ] || [ "$WHAT" = deps ]; then
stage   "fetch sources"     ./scripts/fetch_sources.sh
stage   "dependencies"      ./scripts/build_ios.sh libffi glib pixman libucontext libslirp
# build_gpu_ios.sh asks for cross-ios-darwin.meson, which no script generates; build_ios.sh writes that file as cross-darwin.meson.
cp build/ios-arm64/cross-darwin.meson build/ios-arm64/cross-ios-darwin.meson
if [ ! -f build/ios-arm64/sysroot/lib/libANGLE-shared.dylib ]; then
    stage "ANGLE" ./scripts/build_angle_ios.sh
fi
if [ ! -f build/ios-arm64/sysroot/lib/libvirglrenderer.a ]; then
    stage "GPU stack" env \
        PKG_CONFIG_LIBDIR="$HUSK_ROOT/build/ios-arm64/sysroot/lib/pkgconfig:$HUSK_ROOT/build/ios-arm64/sysroot/share/pkgconfig" \
        ./scripts/build_gpu_ios.sh
fi
stage   "QEMU"              ./scripts/build_ios.sh qemu
stage   "integrate + rebuild QEMU" bash -c './scripts/integrate_husk.sh && ./scripts/build_ios.sh qemurebuild'
stage   "guest kernel"      ./scripts/fetch_phase0_guest.sh
if [ ! -d build/ios-arm64/lib/MoltenVK.xcframework ]; then
    stage "MoltenVK" ./scripts/build_moltenvk_ios.sh
fi
stage   "pairing library"   ./scripts/build_rppairing_ios.sh
fi

if [ "$WHAT" = all ] || [ "$WHAT" = package ]; then
if [ ! -f build/ios-arm64/lib/libqemu-aarch64-softmmu.dylib ]; then
    die "the dependencies are not built yet (run ci_build.sh deps first)"
fi
stage   "app + IPA"         ./scripts/package_ipa.sh "$OUT"
# The TrollStore .tipa is a second codesign pass; IPA_ONLY=1 skips it for a faster test build.
if [ "${IPA_ONLY:-0}" = "1" ]; then
    ls -lh "$OUT"
else
    stage   "TrollStore .tipa" ./scripts/package_ipa.sh --trollstore "${OUT%.ipa}.tipa"
    ls -lh "$OUT" "${OUT%.ipa}.tipa"
fi
fi
