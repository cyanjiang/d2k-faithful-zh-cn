#!/usr/bin/env bash
# Incremental MSVC build without a Developer Command Prompt.
# Usage: bash tools/rebuild.sh [target]   (default target: d2k)
set -e
cd "$(dirname "$0")/.."
VS="C:/Program Files (x86)/Microsoft Visual Studio/18/BuildTools"
MSVC="$VS/VC/Tools/MSVC/14.51.36231"
SDK="C:/Program Files (x86)/Windows Kits/10"
SDKV="10.0.26100.0"
export INCLUDE="$MSVC/include;$SDK/Include/$SDKV/ucrt;$SDK/Include/$SDKV/um;$SDK/Include/$SDKV/shared;$SDK/Include/$SDKV/winrt"
export LIB="$MSVC/lib/x64;$SDK/Lib/$SDKV/ucrt/x64;$SDK/Lib/$SDKV/um/x64"
export PATH="$MSVC/bin/Hostx64/x64:$SDK/bin/$SDKV/x64:$PATH"
CMAKE="$VS/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe"
TARGET="${1:-d2k}"
"$CMAKE" --build build --target "$TARGET"
