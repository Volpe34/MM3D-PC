# Majora's Mask 3D Recomp for Android

An experimental Android port of [MM3D-PC](https://github.com/Volpe34/MM3D-PC), based on the [TriAevum runtime](https://github.com/coccofresco/TriAevum). The build translates the game's 3DS ARM code ahead of time into a native ARM64 module. On Android, SDL2 manages the window and input, and the renderer uses NRI and Vulkan.

The build requires your own decrypted **Majora's Mask 3D USA v1.1** dump (`.3ds` or `.cci`). The source repository does not include the ROM, the generated game module, or game assets.

## Current status

- Tested on a **Motorola Edge 60 with Android 17 and a Mali-G615 MC2**. The debug APK ran through the scene that had frozen at the Happy Mask Salesman and continued rendering the intro in Clock Town.
- The phone's driver supports Vulkan 1.3. The game runtime used Vulkan 1.2 through NRI on this device. Compatibility on other devices is still unverified.
- The current build runs on Android and is cross-compiled on a Linux PC. Building the entire project on the phone has not been validated.
- The touch controls are not wired into the game. Use a Bluetooth or USB gamepad for input testing. Full gameplay, saving, and performance across scenes still need testing.

## Build and install

Build on Linux x86-64. On Bazzite, use an Ubuntu Distrobox for the build tools. You need Android SDK Platform 35, NDK r29, JDK 17, Gradle 8.13, CMake 3.30 or newer, Ninja, Python 3, and ADB.

1. Prepare your own dump with the upstream [MM3D-PC Setup](https://github.com/Volpe34/MM3D-PC). Keep the installation directory and its `work/` subdirectory.
2. Follow the [Android build guide](ports/android/MM3D.md) to compile the title and runtime for ARM64, package the debug APK, and copy the private game data to the phone.
3. Launch the app. The guide uses the package ID `org.triaevum.android.mm3d` so the MM3D build can be installed alongside OoT3D Recomp.

The build produces `ports/android/app/build/outputs/apk/debug/app-debug.apk`. Keep your dump, generated title module, APK containing that module, and staged data private.

## Reporting problems

Include your phone model, Android version, GPU, the scene where the issue occurred, and what you were doing. The app writes `logs/native-stdout.log` and `logs/native-stderr.log` under its data directory. See the [Android build guide](ports/android/MM3D.md) for the ADB command to read them.

The upstream [MM3D-PC project](https://github.com/Volpe34/MM3D-PC) documents the desktop port and credits. See [LICENSE_SCOPE.md](LICENSE_SCOPE.md) and [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) for the licensing details in this fork.
