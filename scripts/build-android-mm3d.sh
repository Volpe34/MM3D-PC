#!/usr/bin/env bash
set -euo pipefail

# Developer build of the MM3D title module for Android (arm64-v8a).
# Needs the work directory of a finished PC Setup (your own ROM, translated on
# the PC). Produces libtriaevum_title_aot.so; the runtime, staging and APK steps
# are in ports/android/MM3D.md. Generated sources are ROM-derived: keep them private.
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
: "${ANDROID_NDK_HOME:?Set ANDROID_NDK_HOME to the extracted Android NDK (r29)}"
: "${MM3D_WORK:?Set MM3D_WORK to the PC Setup work directory (holds aot_program.json)}"
sources="${MM3D_TITLE_SOURCE_DIR:-$HOME/mm3d-android-title-source}"
build="${TRIAEVUM_BUILD_DIR:-$HOME/mm3d-android-build/title}"
jobs="${CMAKE_BUILD_PARALLEL_LEVEL:-2}"

python3 "$root/tools/mm3d/mm3d_emit_title_sources.py" "$MM3D_WORK" "$sources"
cmake -S "$root/ports/android/native" -B "$build" -G Ninja \
  "-DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release \
  -DTRIAEVUM_ANDROID_BUILD_NRI=OFF \
  "-DTRIAEVUM_TRANSLATED_TITLE_DIR=$sources" \
  -DTRIAEVUM_ANDROID_TITLE_OPTIMIZATION=1 \
  -DTRIAEVUM_ANDROID_TITLE_DEBUG_INFO=OFF
cmake --build "$build" --target triaevum_android_title --parallel "$jobs"
printf 'MM3D Android title module: %s/libtriaevum_title_aot.so\n' "$build"
