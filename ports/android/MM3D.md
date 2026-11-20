# Building MM3D for Android

The Android build has two native parts. The runtime uses SDL2 and NRI/Vulkan. The title module is compiled from code translated from your decrypted Majora's Mask 3D USA v1.1 dump. Gradle packages both into a debug APK.

Run the build commands below **on a Linux x86-64 PC**. On Bazzite, run them inside an Ubuntu Distrobox. Install SDK Platform 35, NDK r29, JDK 17, Gradle 8.13, CMake 3.30 or newer, Ninja, Python 3, and ADB first. Adjust the SDK and NDK paths to match your installation.

## 1. Prepare the private inputs

Run the [upstream PC Setup](https://github.com/Volpe34/MM3D-PC) with your own decrypted USA v1.1 dump. Use the resulting installation directory, which contains `work/`, `game/`, `resources/`, and `process-manifest.json`. The Android build uses `aot_program.json`, `mm3d_functions.json`, and `code_thumbfree.bin` from `work/`.

In the root of the Android fork, set these paths in the same terminal you will use for the other commands:

```bash
export ANDROID_HOME="$HOME/Android/Sdk"
export ANDROID_NDK_HOME="$HOME/triaevum-android-tools/android-ndk-r29"
export MM3D_INSTALL="/absolute/path/to/your/prepared-mm3d-installation"
export MM3D_WORK="$MM3D_INSTALL/work"
export MM3D_BUILD="$HOME/mm3d-android-build"
```

## 2. Compile the title and the runtime

```bash
TRIAEVUM_BUILD_DIR="$MM3D_BUILD/title" bash scripts/build-android-mm3d.sh

cmake -S ports/android/runtime -B "$MM3D_BUILD/runtime" -G Ninja \
  -DCMAKE_TOOLCHAIN_FILE="$ANDROID_NDK_HOME/build/cmake/android.toolchain.cmake" \
  -DANDROID_ABI=arm64-v8a -DANDROID_PLATFORM=android-29 \
  -DANDROID_STL=c++_shared -DCMAKE_BUILD_TYPE=Release
cmake --build "$MM3D_BUILD/runtime" --target oot3d_native_game --parallel 2
```

The runtime target name comes from the shared TriAevum code. Use separate build directories for the title and the runtime.

## 3. Package the APK

```bash
python3 tools/android/stage_native_app.py \
  --runtime-build "$MM3D_BUILD/runtime" \
  --title-module "$MM3D_BUILD/title/libtriaevum_title_aot.so" \
  --ndk "$ANDROID_NDK_HOME" --output "$MM3D_BUILD/apk-libs"

gradle -p ports/android :app:assembleDebug \
  -PtriaevumSdlSource="$MM3D_BUILD/runtime/_deps/sdl2-src" \
  -PtriaevumNativeStage="$MM3D_BUILD/apk-libs" \
  -PtriaevumApplicationIdSuffix=.mm3d --no-daemon --max-workers=2
```

The SDL path must point to the same SDL2 source used by the native build. The APK is written to `ports/android/app/build/outputs/apk/debug/app-debug.apk`.

## 4. Install and launch

Enable USB debugging, connect and unlock the phone, and accept the ADB prompt. Run these commands on the PC:

```bash
adb devices -l
adb install -r ports/android/app/build/outputs/apk/debug/app-debug.apk

python3 tools/android/prepare_mm3d_data.py \
  --installation "$MM3D_INSTALL" --output "$MM3D_BUILD/mm3d-staged"
```

The staging directory must be new. On a fresh install, open the app once to create its data directory. It may close because the launch profile is not there yet. Then copy the staged files and reopen:

```bash
MM3D_APP_DATA=/sdcard/Android/data/org.triaevum.android.mm3d/files
MM3D_ACTIVITY=org.triaevum.android.mm3d/org.triaevum.android.TriAevumActivity

adb shell am start -W -n "$MM3D_ACTIVITY"
adb shell am force-stop org.triaevum.android.mm3d
adb shell mkdir -p "$MM3D_APP_DATA/resources"
adb push "$MM3D_BUILD/mm3d-staged/." "$MM3D_APP_DATA/"
adb shell am start -W -n "$MM3D_ACTIVITY"
```

Use the data transfer on a fresh profile. For APK updates, reinstall with `adb install -r` and keep the existing data. The separate package ID keeps the MM3D data apart from OoT3D Recomp.

## Diagnostics

From the PC, read the native logs with ADB:

```bash
adb shell cat "$MM3D_APP_DATA/logs/native-stdout.log"
adb shell cat "$MM3D_APP_DATA/logs/native-stderr.log"
```

For a GPU test without a ROM, see [Device Verification](README.md#device-verification). The standalone NRI device probe checks device and queue creation. The `triaevum_android_nri_present_probe` NativeActivity library also exercises swapchain creation, drawing with SPIR-V shaders, and presentation when packaged in a probe APK.

Keep the ROM, generated title sources and libraries, debug APK containing the title, and staged game data out of the public repository.
