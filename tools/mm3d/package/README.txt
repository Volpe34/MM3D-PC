Majora's Mask 3D - native PC port (TriAevum runtime)
=====================================================

This package contains NO game data. You need your own decrypted dump of
The Legend of Zelda: Majora's Mask 3D (USA, v1.1) as a .3ds or .cci file.
Setup translates the game code from YOUR dump on YOUR computer; nothing of the
game is downloaded or included.

Made with AI
  This whole project was made by AI (Claude) under human review.

Requirements
  - Windows 10/11 x64, or Linux x64 with glibc 2.38+ (Ubuntu 24.04+, Debian 13,
    Fedora 39+, Arch, SteamOS...)
  - A GPU with Vulkan 1.2 and current drivers, 8 GB of RAM
  - About 4 GB of free disk space and an Internet connection for the first
    setup (it downloads a compiler from its official site: Python and
    llvm-mingw on Windows, zig on Linux; every download is checksum-verified)

Setup (once)
  1. Extract this folder to a short, writable path (for example C:\Games\MM3D;
     not Program Files).
  2. Copy your decrypted ROM (.3ds/.cci) into this folder,
     or drag it onto Setup.bat (Linux: ./Setup.sh /path/to/rom.3ds).
  3. Run Setup.bat (Linux: ./Setup.sh) and wait: it takes 10-40 minutes.
     The log is written to logs\setup.log.

Shader tour
  At the end of Setup, game windows open and close by themselves for 15-40
  minutes: Link (and his other forms) visits every area so it never stutters
  the first time. You can close it and run BuildShaders.bat (Linux:
  ./BuildShaders.sh) later.

Play
  Run Play.bat (Linux: ./Play.sh). F1 = settings (graphics, controls, button
  prompts), F11 = fullscreen. Saves are in data\savedata.
  The first minutes may stutter briefly while shaders are compiled for your
  GPU; they are kept for the next sessions.

HD texture packs
  Copy an Azahar/Citra texture pack (the contents of its title folder) into
  data\textures. With enough RAM, turn on F1 > Textures > Preload custom
  textures and raise Texture memory (a 4K pack needs about 6.5 GB).

Controls (controller, Xbox names; rebind in F1 > Controls)
  A/B = A/B, Y = 3DS X, X = 3DS Y, LT = target (L), RT = shield (R),
  LB/RB = item slots, right stick = camera, R3 = first-person view,
  D-pad up = ocarina, D-pad down = masks, L3 = minimap,
  Back = items, Start = Bombers' Notebook, Start+A = settings, Start+B closes them.
  In the Notebook: X = schedule/events, Y = set the alarm, Back = the event's map.
  Camera speed (right stick) and the F1 menu size: F1 > Controls.

Updating to a new version
  Extract the new release into a new folder and run Setup again with your ROM
  (copying new files over an old folder does not work). Then copy data\savedata,
  data\textures and, to keep your settings, data\config from the old folder.

If something goes wrong
  Send logs\setup.log and logs\setup_download.log (setup problems) or
  logs\last_run.log and any report .txt in logs\ (game problems). Logs contain
  your user name in file paths; nothing else personal.

Italiano (breve)
  Serve il TUO dump decriptato di Majora's Mask 3D (USA v1.1, .3ds/.cci):
  estrai la cartella in un percorso corto (es. C:\Games\MM3D), mettici la ROM
  (o trascinala su Setup.bat), avvia Setup.bat una volta (10-40 minuti), poi
  gioca con Play.bat. In caso di problemi manda la cartella logs.
