#!/usr/bin/env python3
"""Shader tour: Link visits every scene of Majora's Mask 3D, then Deku, Goron,
Zora and Fierce Deity Link visit the main areas, walking, rolling and
attacking, so the shaders and pipelines they need are recorded. Play builds
them before the game opens, so the first visit to an area does not stutter.

The game windows open and close by themselves. Each session boots the game
fresh (no save is needed or touched) and the runtime's test warp moves Link
from scene to scene. A session that fails only loses its own scenes.

usage: shader_tour.py --install <folder> [--jobs N] [--seconds S] [--again]
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

# Entrance groups 0x00..0x6F (entrance = group << 9, spawn 0), without the ones
# that do not load a scene.
BROKEN = {0x0B, 0x0C, 0x0D, 0x0F, 0x2E, 0x37}
SCENES = [group for group in range(0x00, 0x70) if group not in BROKEN]
# Areas for the other forms: field, towns, swamp, mountain, coast, canyon, woods.
FORM_SCENES = [entrance >> 9 for entrance in (0x5400, 0xD800, 0xD600, 0xD000, 0xD400, 0x8400, 0x8600, 0x5000,
                                              0x9A00, 0x9400, 0xB200, 0x6800, 0x4600, 0x2000, 0x6400)]
FORMS = {4: "human", 3: "deku", 1: "goron", 2: "zora", 0: "fierce"}
BOOT_SECONDS = 40   # boot and first scene, before the warps' own time
BATCH = 8           # scenes per session


def timeline(path: Path) -> None:
    """Walk, attack, jump/roll, charge, turn: repeated for the whole session."""
    segments = []
    for k in range(400):
        base = 900 + k * 140  # after the first warp: inputs on the title screen open the file select
        segments += [
            {"start_frame": base, "end_frame_exclusive": base + 30, "circle_y": 1.0},
            {"start_frame": base + 30, "end_frame_exclusive": base + 36, "buttons": ["b"]},
            {"start_frame": base + 42, "end_frame_exclusive": base + 48, "buttons": ["a"], "circle_y": 1.0},
            {"start_frame": base + 56, "end_frame_exclusive": base + 120, "buttons": ["b"]},
            {"start_frame": base + 124, "end_frame_exclusive": base + 138, "circle_x": 1.0 if k % 2 else -1.0},
        ]
    path.write_text(json.dumps({"schema": "oot3d.native_game.input_timeline.v1", "frame_origin": "guest",
                                "segments": segments}))


def tour_config(install: Path, path: Path) -> None:
    source = install / "data/config/TriAevum.json"
    config = json.loads(source.read_text(encoding="utf-8")) if source.is_file() else {}
    graphics = config.setdefault("Graphics", {})
    graphics.setdefault("Window", {})["Mode"] = "Windowed"
    # Textures do not change the shaders; skip loading packs.
    packs = graphics.setdefault("TexturePacks", {}).setdefault("Azahar", {})
    packs.update({"LoadCustomTextures": False, "ButtonStyleDirectory": "", "DumpTextures": False})
    path.write_text(json.dumps(config))


def runtime_environment(install: Path) -> dict:
    env = dict(os.environ)
    if sys.platform != "win32":
        libs = [str(install / "lib")]
        compat = install / "lib/compat/libstdc++.so.6"
        if compat.is_file() and newer_libstdcxx(compat):
            libs.append(str(compat.parent))
        if (install / "lib/wayland/VERSION").is_file() and use_bundled_wayland(install / "lib/wayland"):
            libs.append(str(install / "lib/wayland"))
        env["LD_LIBRARY_PATH"] = os.pathsep.join(libs + [env.get("LD_LIBRARY_PATH", "")]).rstrip(os.pathsep)
    return env


def ldconfig_listing() -> str:
    # ldconfig lives in /sbin on some distributions, outside a user's PATH.
    for tool in ("ldconfig", "/sbin/ldconfig", "/usr/sbin/ldconfig"):
        try:
            return subprocess.run([tool, "-p"], capture_output=True, text=True).stdout
        except OSError:
            continue
    return ""


def use_bundled_wayland(folder: Path) -> bool:
    """As Play.sh: the bundled libwayland only if the system's is missing or older."""
    found = re.search(r"libwayland-client\.so\.0 \(libc6,x86-64\) => (\S+)", ldconfig_listing())
    if not found or not Path(found.group(1)).exists():
        return True
    version = lambda text: tuple(int(part) for part in re.findall(r"\d+", text))
    theirs = Path(found.group(1)).resolve().name.removeprefix("libwayland-client.so.")
    return version((folder / "VERSION").read_text()) > version(theirs)


def newer_libstdcxx(bundled: Path) -> bool:
    """As Play.sh: use the bundled libstdc++ only if it is newer than the system's."""
    def newest(path: Path) -> tuple[int, ...]:
        try:
            versions = re.findall(rb"GLIBCXX_3\.4\.(\d+)", path.read_bytes())
        except OSError:
            return ()
        return (max(int(v) for v in versions),) if versions else ()
    system = ()
    found = re.search(r"libstdc\+\+\.so\.6 \(libc6,x86-64\) => (\S+)", ldconfig_listing())
    system = newest(Path(found.group(1))) if found else ()
    return not system or newest(bundled) > system


def run_session(install: Path, work: Path, runs: Path, env: dict, form: int, groups: list[int],
                seconds: int, tag: str) -> str:
    save = Path(tempfile.mkdtemp(prefix="save_", dir=work))
    shaders, pipelines = work / f"{tag}_shaders.json", work / f"{tag}_pipelines.json"
    windows = sys.platform == "win32"
    session_env = dict(env, TRIAEVUM_WARP_ENTRANCES=",".join(hex(g << 9) for g in groups),
                       TRIAEVUM_WARP_SECONDS=str(seconds), TRIAEVUM_PLAYER_FORM=str(form),
                       TRIAEVUM_CRASH_DIR=str(work / "reports"), TRIAEVUM_BACKGROUND_WINDOW="1")
    total = BOOT_SECONDS + seconds * len(groups)
    command = [str(install / ("TriAevum.exe" if windows else "TriAevum")),
               "--title-plugin", str(install / "private" / ("mm3d_title_aot.dll" if windows else "mm3d_title_aot.so")),
               "--a32-process-manifest", "process-manifest.json", "--resource-root", "resources",
               "--renderer", "nri", "--ui-profile", "oot3d", "--config", str(work / "config.json"),
               "--gameplay-timing", "native30_interpolated", "--presentation-rate", "60",
               "--save-data", str(save), "--width", "640", "--height", "360",
               "--input-timeline", str(work / "timeline.json"), "--max-seconds", str(total),
               "--pica-effective-shader-inventory", str(shaders), "--pica-pipeline-inventory", str(pipelines)]
    started = time.monotonic()
    try:
        with open(work / "logs" / f"{tag}.log", "w") as log:
            status = subprocess.run(command, cwd=install, env=session_env, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=total + 120).returncode
    except subprocess.TimeoutExpired:
        status = "timeout"
    finally:
        shutil.rmtree(save, ignore_errors=True)
    if status == 0 and shaders.is_file() and pipelines.is_file():
        # Play merges the recordings in data/shader_corpus/runs and builds them.
        os.replace(shaders, runs / shaders.name)
        os.replace(pipelines, runs / pipelines.name)
        if time.monotonic() - started < total - 5:
            return "closed early (kept what it recorded; it runs again next time)"
        return "ok"
    return f"failed ({status})"


def default_jobs() -> int:
    """Game sessions at once: about 3 GB of memory each, at most 3."""
    memory = 0
    try:
        if sys.platform == "win32":
            import ctypes

            class Status(ctypes.Structure):
                _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong), ("total", ctypes.c_ulonglong),
                            ("available", ctypes.c_ulonglong), ("page_total", ctypes.c_ulonglong),
                            ("page_available", ctypes.c_ulonglong), ("virtual_total", ctypes.c_ulonglong),
                            ("virtual_available", ctypes.c_ulonglong), ("extended", ctypes.c_ulonglong)]
            status = Status(); status.length = ctypes.sizeof(Status)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
                memory = status.total
        else:
            memory = os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
    except (OSError, ValueError, AttributeError):
        pass
    return max(1, min(3, int(memory / (3 * 2**30)) if memory else 1, (os.cpu_count() or 2) // 2))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=0)
    parser.add_argument("--seconds", type=int, default=12, help="seconds in each scene")
    parser.add_argument("--again", action="store_true", help="also repeat the sessions already done")
    args = parser.parse_args()
    install = args.install.resolve()
    if not (install / "process-manifest.json").is_file():
        print("Run Setup first.")
        return 1
    work = install / "work/shader_tour"
    for folder in (work / "logs", work / "reports", install / "data/shader_corpus/runs"):
        folder.mkdir(parents=True, exist_ok=True)
    runs = install / "data/shader_corpus/runs"
    done_path = work / "done.json"
    done = set(json.loads(done_path.read_text())) if done_path.is_file() and not args.again else set()
    timeline(work / "timeline.json")
    tour_config(install, work / "config.json")
    env = runtime_environment(install)

    sessions = []
    for form, groups in [(4, SCENES)] + [(form, FORM_SCENES) for form in (3, 1, 2, 0)]:
        for start in range(0, len(groups), BATCH):
            batch = groups[start:start + BATCH]
            tag = f"tour_{FORMS[form]}_{batch[0]:02x}"
            if tag not in done:
                sessions.append((form, batch, tag))
    jobs = args.jobs or default_jobs()
    print(f"Shader tour: {len(sessions)} sessions, {jobs} at a time "
          f"(about {round(len(sessions) * (BOOT_SECONDS + args.seconds * BATCH) / jobs / 60)} minutes).\n"
          "  Small game windows will open and close by themselves many times: this is normal,\n"
          "  it is the setup playing through the areas to prepare the graphics. Do not close them;\n"
          "  you can keep using the computer. Closing this console stops it (the game works anyway).",
          flush=True)
    started, finished, failed = time.time(), 0, 0
    with concurrent.futures.ThreadPoolExecutor(jobs) as pool:
        futures = {pool.submit(run_session, install, work, runs, env, form, batch, args.seconds, tag): tag
                   for form, batch, tag in sessions}
        for future in concurrent.futures.as_completed(futures):
            tag = futures[future]
            status = future.result()
            finished += 1
            if status == "ok":
                done.add(tag)
                done_path.write_text(json.dumps(sorted(done)))
            else:
                failed += 1
            print(f"  [{finished}/{len(sessions)} {time.time() - started:5.0f}s] {tag}: {status}", flush=True)
    print(f"Shader tour done: {len(sessions) - failed} sessions recorded, {failed} failed. "
          "Play builds the recorded shaders when it starts.", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
