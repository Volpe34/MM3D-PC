# Majora's Mask 3D — native PC port (TriAevum runtime)

A native Windows/Linux port of **The Legend of Zelda: Majora's Mask 3D**,
built on [TriAevum](https://github.com/coccofresco/TriAevum), the
recompilation runtime and Vulkan renderer made for Ocarina of Time 3D.
The game's ARM code is translated to native code **on your computer, from
your own ROM**. The 3DS GPU command stream is rendered with Vulkan. On top of
that you get higher resolutions, widescreen, frame interpolation above 30 fps
and a single-screen layout, so no touch screen is needed.

> **This repository and its releases contain no Nintendo code or assets.**
> You need your own decrypted dump of Majora's Mask 3D (USA, v1.1). Setup
> extracts it, translates the code and builds the game module on your PC.
> Nothing of the game is downloaded or distributed.

## Made with AI

This whole project was made by AI (Claude) under human review: I directed it,
tested it and decided what goes in. Love it or hate it, i could not care less if you dont want to play it because it's vibecoded.

## Status, please read

This was mostly a personal project that I decided to share. **Performance
may be bad right now** (stutter, frame drops, rough edges on some PCs), and
**I don't know if I will keep working on it.** I'm publishing it mostly so
that other people can pick it up, learn from it or continue it. Forks and
pull requests are welcome.

## Features

- **Native code.** The game logic runs exactly as on the 3DS (30 Hz). It is
  translated ahead of time instead of emulated.
- **Vulkan renderer.** Resolution scaling, anti-aliasing, ambient occlusion,
  shadows, toon shading and the other TriAevum options, all in the F1 menu.
- **Frame interpolation** from the 30 Hz game to 60 fps or higher. It covers
  the camera, the world, characters and shadows; the interface is shown
  exactly as the game draws it.
- **Single screen.** The touch-screen HUD and menus are rearranged onto one
  screen and usable with a controller: items, masks, map, Bombers' Notebook,
  ocarina songs, bank, giving items.
- **Input:** controller, keyboard and mouse. Button prompts come in Xbox,
  DualShock, Nintendo, SNES and original 3DS styles.

## Requirements

- Windows 10/11 x64, or Linux x64 with glibc 2.38 or newer (Ubuntu 24.04+,
  Debian 13, Fedora 39+, Arch/SteamOS and similar).
- A GPU with Vulkan 1.2 and current drivers.
- Your decrypted Majora's Mask 3D **USA v1.1** dump, as `.3ds` or `.cci`.
- About 4 GB of disk space and 8 GB of RAM.
- An Internet connection the first time. Setup downloads a compiler from its
  official site and checks every download against a pinned SHA-256:
  - Windows: Python (embeddable) and llvm-mingw.
  - Linux: zig.

## Install on Windows

1. Download `MM3D-PC-Windows.zip` from Releases and extract it to a short,
   writable folder such as `C:\Games\MM3D`. Do not use *Program Files*.
2. Put your ROM in that folder (or drag it onto `Setup.bat`), then run
   `Setup.bat`.
   - It runs once and takes 10–40 minutes; the log is `logs\setup.log`.
   - Windows may warn about an unsigned script. Choose *More info → Run anyway*.
3. Play with `Play.bat`. **F1** opens the settings, **F11** toggles
   fullscreen.

## Install on Linux

1. Download `MM3D-PC-Linux.tar.xz` from Releases and extract it.
2. Put your ROM in that folder and run `./Setup.sh`, or run
   `./Setup.sh /path/to/rom.3ds`. It needs only `python3`, which every
   desktop distribution has. It takes 10–40 minutes once.
3. Play with `./Play.sh`.

## Updating

Extract the new release into a new folder and run Setup again with the same
ROM: it rebuilds the game module for that version, so copying the new files
over an old folder does not work. Then copy from the old folder
`data/savedata` (your saves), `data/textures` (an HD pack) and, to keep your
settings, `data/config`.

## Shader tour (no stutter on first visits)

At the end of setup, Link visits every scene once, then Deku, Goron, Zora and
Fierce Deity Link visit the main areas while walking, rolling and attacking.
This records every shader the game needs, and `Play` builds them before the
game opens. Game windows open and close by themselves (15–40 minutes). You can
close it and finish later with `BuildShaders.bat` / `./BuildShaders.sh`; the game
works either way, it may just stutter briefly the first time it shows something
new.

## Button prompts and texture packs

- **Button prompts**: setup downloads HardDanX's Omni UI (Xbox, DualShock,
  Nintendo, SNES colors). Choose one in **F1 → Button style**.
- **Texture packs** (Azahar/Citra format, e.g. Henriko Magnifico's
  [MM3D 4K pack](https://www.henrikomagnifico.com/zelda-majoras-mask-3d-4k)):
  copy the contents of the pack's title folder into `data/textures`. Sub-folders
  are fine, and packs with or without `pack.json` both work. The button style
  is applied on top of the pack. A new area waits a moment for its textures
  (no low-resolution pop-in). With enough RAM, turn on **F1 → Textures →
  Preload custom textures** and raise **Texture memory** (the 4K pack needs
  about 6.5 GB): the whole pack is decoded in the background and areas
  appear without any wait.

## If something goes wrong

Open an issue and attach the logs:

- Setup problems: `logs/setup.log` and `logs/setup_download.log` (Windows).
- Game problems: `logs/last_run.log` and any report in `logs/`.

The logs show folder paths, which may include your user name. Nothing else
in them is personal.

## Controls

These are the controller defaults, with Xbox names. Rebind them in
F1 → Controls.

| Action | Button |
| --- | --- |
| A / B | A / B |
| 3DS X / 3DS Y | Y / X |
| Target (L) | LT |
| Shield (R) | RT |
| Item slots | LB / RB |
| Camera | Right stick |
| First person | R3 |
| Ocarina | D-pad up |
| Masks | D-pad down |
| Minimap | L3 |
| Items | Back |
| Bombers' Notebook | Start |
| Notebook: schedule / events, alarm, event's map | X / Y / Back |
| Settings (open / close) | Start+A / Start+B |

The camera's turn speed (right stick) and the size of the F1 menu are in
F1 → Controls.

## How it works

`Setup` runs `tools/mm3d/package/mm3d_setup.py`, which does the following:

1. Extracts the ROM (`tools/mm3d/ctr_extract.py`) and checks the `code.bin`
   hash of the supported version.
2. Translates the ARM/Thumb code into a whole-program representation and
   generates C++ from it (`tools/mm3d/`, `tools/oot3d/native_a32_runtime/`).
   The bundled *recipe* in `tools/mm3d/package/recipe/` holds only
   addresses: function entries and sizes, Thumb entries, call sites and
   runtime-discovered entries. It has no instruction bytes or game data.
3. Compiles the generated C++ into the title module
   (`private/mm3d_title_aot.dll` or `.so`) with the downloaded compiler.
4. Writes the process manifest for the runtime.

The game reads every texture, model, sound and text from your ROM at runtime.

## Building from source

**Linux runtime.** Build it in the Steam Runtime 4 SDK:

```bash
bash scripts/prepare-steamrt4-sdk.sh <sdk dir> <work dir>
bash scripts/run-in-steamrt4.sh <sdk dir> <work dir> bash <build script>
```

For a local build with system packages, run `scripts/build-linux-runtime.sh`
(Clang, Vulkan, SDL2, shaderc, spdlog, libzip, tinyxml2).

**Windows runtime.** Cross-build it on Linux with llvm-mingw:
`scripts/build-windows-mingw.sh`.

**Release packages.** Assemble them with:

```bash
python3 tools/mm3d/package/build_package.py --platform windows|linux --runtime <build dir> --out <folder>
```

Then run `tools/mm3d/package/audit_package.py <folder> <code.bin> <romfs.bin>`.
It fails if any byte run of the game is found in the package.

**Recipe.** Regenerate it from a converged translation with
`tools/mm3d/package/make_recipe.py <work dir>`.

## Credits

- **TriAevum** by coccofresco and contributors: runtime, recompiler and
  renderer (GPL-3.0-or-later). See `README_TRIAEVUM.md`,
  `THIRD_PARTY_NOTICES.md` and `LICENSE_SCOPE.md`.
- **Azahar**, **Ship of Harkinian / libultraship**, **NVIDIA NRI** and the
  other projects listed in `THIRD_PARTY_NOTICES.md`.
- **[mm3d-dekui](https://github.com/tgacquin/mm3d-dekui)** by tgacquin, the
  single-screen mod for Majora's Mask 3D. Its approach to bringing the
  bottom-screen menus onto one screen was the main reference for the
  single-screen layout here....even if my own take of it was completely ASS.
- **TopScreen**, the single-screen HUD design of TriAevum.
- **[Omni UI for Project Restoration](https://gamebanana.com/mods/569317)** by
  **HardDanX**, the Xbox / DualShock / Nintendo button prompt textures
  (CC BY-NC-ND 4.0). Setup downloads them from GameBanana; they are not
  redistributed here.
- **Project Restoration** and the MM3D decompilation/randomizer projects, for
  game structure research.
- **Nintendo** and **Grezzo**, who made the game. This is a fan project, not
  affiliated with or endorsed by Nintendo.

## License

The original code is GPL-3.0-or-later (see `LICENSE` and `LICENSE_SCOPE.md`).
No license is granted for Nintendo's game, which you must own.
