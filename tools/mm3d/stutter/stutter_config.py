#!/usr/bin/env python3
"""stutter_config.py <config> <TriAevum.json> <output.json>: writes the settings
copy a StutterTest.bat run uses. The player's own settings file is only read.

  asis      unchanged
  nowait    F1 > Textures > "Wait for custom textures" off
  nohd      HD pack not loaded (LoadDirectory points to an empty folder; the
            button-style overlay still loads)
  preload   "Preload custom textures" on with Texture memory 8192 MB
  asyncall  unchanged settings (StutterTest.bat sets TRIAEVUM_ASYNC_ALL_SHADERS=1)
"""
import json
import sys
from pathlib import Path

CONFIGS = ("asis", "nowait", "nohd", "preload", "asyncall")


def main() -> int:
    if len(sys.argv) != 4 or sys.argv[1] not in CONFIGS:
        print(__doc__)
        return 2
    config, source, output = sys.argv[1], Path(sys.argv[2]), Path(sys.argv[3])
    settings = json.loads(source.read_text(encoding="utf-8"))
    azahar = settings.setdefault("Graphics", {}).setdefault("TexturePacks", {}).setdefault("Azahar", {})
    if config == "nowait":
        azahar["WaitForCustomTextures"] = False
    elif config == "nohd":
        azahar["LoadDirectory"] = "data/textures_off"
    elif config == "preload":
        azahar["PreloadCustomTextures"] = True
        azahar["TextureMemoryMB"] = max(8192, int(azahar.get("TextureMemoryMB", 0)))
    output.write_text(json.dumps(settings, indent=4) + "\n", encoding="utf-8")
    print(f"{config}: Load={azahar.get('LoadCustomTextures')} Wait={azahar.get('WaitForCustomTextures')} "
          f"Preload={azahar.get('PreloadCustomTextures')} TextureMemoryMB={azahar.get('TextureMemoryMB')} "
          f"LoadDirectory={azahar.get('LoadDirectory')}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
