# Majora's Mask 3D on Android (work in progress)

Status: **untested scaffolding.** The Android host, NRI/Vulkan renderer and APK
come from the OoT3D port (see [README](README.md) and [INTRO](INTRO.md)), which
itself only reaches title-intro bring-up. Nothing here has been built or run on a
device yet. Expect to fix errors at each step below.

Target device: Android 17, Vulkan 1.3, arm64-v8a, **Mali GPU**. Build host: Linux
x64 with NDK r29, CMake >= 3.30, Ninja, JDK 17, Gradle 8.13 and SDK 35.

The OoT3D port was only measured on an Adreno 830. Whether the renderer's
capability negotiation, shader compilation and render-pass behaviour work on Mali
is unknown, so check the GPU before spending hours on the title build.

## Step 0: GPU check (minutes, no ROM needed)

1. Enable USB debugging on the phone and confirm `adb devices -l` lists it.
2. Cross-build only the probes (they do not need the translated title):
   `ANDROID_NDK_HOME=... TRIAEVUM_BUILD_DIR=$HOME/mm3d-android-build/probe scripts/build-android-native.sh`
3. Copy `triaevum_android_nri_probe` (from that build directory; if it is not at
   the top level, look for it with `find`) and the NDK's arm64
   `libc++_shared.so` (under `toolchains/llvm/prebuilt/*/sysroot/usr/lib/aarch64-linux-android/`)
   to a private folder:
   `adb push <files> /data/local/tmp/mm3d-probe/`
4. Run it, never on the Linux host:
   `adb shell 'cd /data/local/tmp/mm3d-probe && chmod +x triaevum_android_nri_probe && LD_LIBRARY_PATH=. ./triaevum_android_nri_probe'`

Success only means NRI created a Vulkan device and graphics queue (README,
"Device Verification"). It does not test rendering, shaders or speed. If it fails,
keep the output: it names what the Mali driver lacks. Also useful: the Vulkan
extension list from an app such as Vulkan Caps Viewer, and the phone model/chip.

## Flow

1. **On the PC**, run the normal MM3D Setup with your own USA v1.1 ROM. That
   translates the code and leaves a work directory (`aot_program.json`,
   `mm3d_functions.json`, `code_thumbfree.bin`) plus the install folder
   (`process-manifest.json`, `game/`, `resources/`). The translation is host
   independent; only the final compile differs.
2. **Compile the title for ARM64** (the long step, tens of minutes):
   `ANDROID_NDK_HOME=... MM3D_WORK=<work dir> scripts/build-android-mm3d.sh`.
   It emits the generated sources with `tools/mm3d/mm3d_emit_title_sources.py`
   (same generator parameters as the desktop build) and cross-compiles them with
   `ports/android/native`.
3. **Runtime and APK**: the runtime target, `tools/android/stage_native_app.py`
   and `gradle :app:assembleDebug` commands are the ones in
   [INTRO.md, Build Boundaries](INTRO.md#build-boundaries). The root runtime
   target already contains the MM3D sources.
4. **Private data**:
   `python3 tools/android/prepare_mm3d_data.py --installation <PC install> --output <staging>`
   then copy it to `/sdcard/Android/data/org.triaevum.android/files` as described
   in INTRO.md ("Private Inputs And Verification").

## Known gaps

- No touch overlay wired in: the controls library is not connected to the live
  game, so a Bluetooth/USB gamepad is needed for now.
- No ROM import on the device; the game data is staged with adb.
- MM3D-specific F1 panels, cheats, save states (about 450 MB per slot) and the
  shader tour have not been looked at for Android.
- The setup's shader corpus is built on the PC GPU. Pipelines compile on first
  use on the phone, so expect stutter until the NRI pipeline cache warms up.
- Performance on this title is unknown. Even OoT3D on Adreno 830 runs near
  real time only in places.

Do not publish or commit ROMs, generated sources, `libtriaevum_title_aot.so` or
the staged data directory.
