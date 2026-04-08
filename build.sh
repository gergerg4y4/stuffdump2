#!/bin/bash
# build.sh - builds libstuffdump.so for ARM64 Android (Quest 3)
# Requires: Android NDK r25+ installed, ANDROID_NDK_HOME set

set -e

NDK="${ANDROID_NDK_HOME:-$HOME/android-ndk}"

if [ ! -d "$NDK" ]; then
    echo "ERROR: NDK not found at $NDK"
    echo "Set ANDROID_NDK_HOME or install NDK to ~/android-ndk"
    exit 1
fi

BUILD_DIR="build_arm64"
mkdir -p "$BUILD_DIR"

cmake \
    -S . \
    -B "$BUILD_DIR" \
    -DCMAKE_TOOLCHAIN_FILE="$NDK/build/cmake/android.toolchain.cmake" \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-29 \
    -DCMAKE_BUILD_TYPE=Release \
    -DANDROID_STL=c++_static

cmake --build "$BUILD_DIR" --config Release -j$(nproc)

echo ""
echo "Built: $BUILD_DIR/libstuffdump.so"
echo ""
echo "Place libstuffdump.so in your native mods folder alongside libil2cpp.so"
echo "After launch, wait ~10 seconds in a room, then:"
echo "  adb pull /sdcard/stuffdump.txt ."
echo "  cat stuffdump.txt"
