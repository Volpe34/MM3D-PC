# Majora's Mask 3D on Android (work in progress)

Status: **untested scaffolding.** The Android host, NRI/Vulkan renderer and APK
come from the OoT3D port (see [README](README.md) and [INTRO](INTRO.md)), which
itself only reaches title-intro bring-up. Nothing here has been built or run on a
device yet. Expect to fix errors at each step below.

Target device: Android 17, Vulkan 1.3, arm64-v8a. Build host: Linux x64 with
NDK r29, CMake >= 3.30, Ninja, JDK 17, Gradle 8.13 and SDK 35.

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
