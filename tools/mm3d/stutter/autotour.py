#!/usr/bin/env python3
"""autotour.py: an unattended, repeatable stutter run for comparing builds.

Boots the game on an empty temporary save (like the shader tour), warps Link
through a fixed list of areas with the tour's walk/attack/roll input timeline,
and records an MM3D_HITCH_LOG. Nothing of the player's is written: settings,
saves, shader inventories and (with --cold-cache) the renderer caches all live
in the output folder.

usage: autotour.py --install <game folder> --out <folder> [--label L]
                   [--config asis|nowait|nohd|preload] [--seconds S]
                   [--cold-cache] [--no-shader-pack] [-- extra TriAevum arguments]
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
# Main areas (the shader tour's form scenes): field, Clock Town, swamp, mountain,
# coast, canyon, woods.
ENTRANCES = (0x5400, 0xD800, 0xD600, 0xD000, 0xD400, 0x8400, 0x8600, 0x5000,
             0x9A00, 0x9400, 0xB200, 0x6800, 0x4600, 0x2000, 0x6400)
BOOT_SECONDS = 40


def timeline(path: Path) -> None:
    segments = []
    for k in range(400):
        base = 900 + k * 140
        segments += [
            {"start_frame": base, "end_frame_exclusive": base + 30, "circle_y": 1.0},
            {"start_frame": base + 30, "end_frame_exclusive": base + 36, "buttons": ["b"]},
            {"start_frame": base + 42, "end_frame_exclusive": base + 48, "buttons": ["a"], "circle_y": 1.0},
            {"start_frame": base + 56, "end_frame_exclusive": base + 120, "buttons": ["b"]},
            {"start_frame": base + 124, "end_frame_exclusive": base + 138, "circle_x": 1.0 if k % 2 else -1.0},
        ]
    path.write_text(json.dumps({"schema": "oot3d.native_game.input_timeline.v1", "frame_origin": "guest",
                                "segments": segments}))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--label", default="run")
    parser.add_argument("--config", default="asis", choices=("asis", "nowait", "nohd", "preload"))
    parser.add_argument("--seconds", type=float, default=10.0, help="seconds in each area")
    parser.add_argument("--entrances", default="", help="comma-separated entrance list (default: 15 main areas)")
    parser.add_argument("--cold-cache", action="store_true", help="empty SPIR-V/pipeline cache directory")
    parser.add_argument("--no-shader-pack", action="store_true", help="run without the collected shader pack")
    parser.add_argument("extra", nargs="*", help="after --: more TriAevum arguments")
    args = parser.parse_args()
    install = args.install.resolve()
    out = args.out.resolve() / args.label
    if out.exists():
        shutil.rmtree(out)
    (out / "save").mkdir(parents=True)
    windows = sys.platform == "win32"
    python = sys.executable
    subprocess.run([python, str(HERE / "stutter_config.py"), args.config, str(install / "data/config/TriAevum.json"),
                    str(out / "config.json")], check=True)
    # Sibling files the game reads next to its settings (controls, language).
    for name in ("oot3d_controls.json", "game_language.json", "thanks_shown"):
        if (install / "data/config" / name).is_file():
            shutil.copy(install / "data/config" / name, out / name)
    timeline(out / "timeline.json")
    entrances = [int(value, 0) for value in args.entrances.split(",") if value] or list(ENTRANCES)
    env = dict(os.environ, MM3D_HITCH_LOG="1", MM3D_HITCH_LOG_FILE=str(out / "hitch.log"),
               TRIAEVUM_WARP_ENTRANCES=",".join(hex(entrance) for entrance in entrances),
               TRIAEVUM_WARP_SECONDS=str(args.seconds), TRIAEVUM_PLAYER_FORM="4",
               TRIAEVUM_CRASH_DIR=str(out), TRIAEVUM_BACKGROUND_WINDOW="1")
    if args.cold_cache:
        (out / "renderer_cache").mkdir()
        env["TRIAEVUM_RENDERER_CACHE_DIR"] = str(out / "renderer_cache")
    total = BOOT_SECONDS + args.seconds * len(entrances)
    corpus = install / "data/shader_corpus"
    command = [str(install / ("TriAevum.exe" if windows else "TriAevum")),
               "--title-plugin", str(install / "private" / ("mm3d_title_aot.dll" if windows else "mm3d_title_aot.so")),
               "--a32-process-manifest", "process-manifest.json", "--resource-root", "resources",
               "--renderer", "nri", "--ui-profile", "oot3d", "--config", str(out / "config.json"),
               "--gameplay-timing", "native30_interpolated", "--presentation-rate", "60",
               "--save-data", str(out / "save"), "--width", "1280", "--height", "720",
               "--input-timeline", str(out / "timeline.json"), "--max-seconds", str(int(total)),
               "--pica-effective-shader-inventory", str(out / "shaders.json"),
               "--pica-pipeline-inventory", str(out / "pipelines.json")]
    if not args.no_shader_pack and (corpus / "mm3d.o3ps").is_file() and (corpus / "all_pipelines.json").is_file():
        command += ["--pica-aot-shader-pack", str(corpus / "mm3d.o3ps"),
                    "--pica-pipeline-manifest", str(corpus / "all_pipelines.json"), "--pica-pipeline-prewarm"]
    command += args.extra
    (out / "command.json").write_text(json.dumps({"command": command, "env": {
        key: env[key] for key in env if key.startswith(("MM3D_", "TRIAEVUM_", "OOT3D_"))}}, indent=1))
    started = time.monotonic()
    with open(out / "run.log", "w") as log:
        try:
            status = subprocess.run(command, cwd=install, env=env, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=total + 180).returncode
        except subprocess.TimeoutExpired:
            status = "timeout"
    print(f"{args.label}: exit {status} after {time.monotonic() - started:.0f} s -> {out}")
    subprocess.run([python, str(HERE / "analyze_hitch_log.py"), str(out / "hitch.log")])
    return 0 if status == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
