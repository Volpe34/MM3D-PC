#!/usr/bin/env python3
"""Prepare Majora's Mask 3D for the TriAevum runtime from the player's own ROM.

Nothing of the game is shipped: this extracts the player's decrypted ROM,
translates its code into a native title module with the bundled recipe
(addresses and structure only) and the compiler on this machine, and writes
the files the runtime needs.

usage: mm3d_setup.py --install <folder> [--rom <file.3ds|.cci>]
                     --cxx <clang++> --ar <llvm-ar> [--jobs N]
"""
from __future__ import annotations

import argparse
import hashlib
import json
import lzma
import os
import shutil
import ssl
import subprocess
import sys
import tarfile
import tempfile
import time
import urllib.request
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]               # the TriAevum tree (tools/mm3d/package/..)
MM3D = REPO / "tools/mm3d"
RECIPE = HERE / "recipe"
SUPPORTED = {
    # code.bin SHA-256 -> version
    "6eec27b1827eab2c87744ac38066d569ffe6fe75790b1eb3300bd91f5099038d": "USA v1.1",
}
TITLE = {"id": "mm3d", "program_id": "0004000000125500", "region": 1, "language": 1}


def step(text: str) -> None:
    print(f"\n== {text}", flush=True)


def fail(text: str) -> None:
    print(f"\nERROR: {text}", flush=True)
    raise SystemExit(1)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with open(path, "rb") as source:
        for chunk in iter(lambda: source.read(1 << 24), b""):
            digest.update(chunk)
    return digest.hexdigest()


class Tee:
    """Console and logs/setup.log at once."""

    def __init__(self, stream, log):
        self.stream, self.log = stream, log

    def write(self, text):
        self.stream.write(text)
        self.log.write(text)
        self.log.flush()

    def flush(self):
        self.stream.flush()


def run(arguments: list[str], **kwargs) -> None:
    print("  >", " ".join(str(a) for a in arguments[:4]), "..." if len(arguments) > 4 else "", flush=True)
    kwargs.setdefault("cwd", REPO)
    # The tools' output goes to the console and the log too.
    process = subprocess.Popen([str(a) for a in arguments], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                               text=True, errors="replace", **kwargs)
    for line in process.stdout:
        print(line, end="", flush=True)
    completed = process
    completed.wait()
    if completed.returncode != 0:
        fail(f"{Path(str(arguments[1])).name if len(arguments) > 1 else arguments[0]} failed "
             f"(exit {completed.returncode}); see the messages above.")


# Omni UI for Project Restoration by HardDanX (CC BY-NC-ND 4.0), button prompt
# textures. Downloaded from its GameBanana page, never redistributed here.
OMNI_UI = ("https://gamebanana.com/dl/1679608",
           "c512206ed5edbc00585b5477f588e63733b1deee167578f931bc2a50dcb20a21")
OMNI_STYLES = {"Xbox": "Omni Xbox UI for PR", "DualShock": "Omni Dualshock UI for PR",
               "Nintendo": "Omni Nintendo UI for PR", "Nintendo (SNES colors)": "Omni Nintendo Colored UI for PR"}
OMNI_SKIP = {"tex1_512x512_F5B5C27302313096_13_mip0.png"}  # item icons with Project Restoration badges
# 7-Zip 26.03 (github.com/ip7z/7zip) to read the RAR5 archive.
SEVEN_ZIP = "https://github.com/ip7z/7zip/releases/download/26.03/"
SEVEN_ZIP_FILES = {"7z2603-linux-x64.tar.xz": "dc99eff5008f1ab79bd7084c68513701547a808a89502bf4133683535ab3c695",
                   "7zr.exe": "ad4c82fadcbdf93c03b4fc440f300509c7d60c5c2f4d183e35d9d70d6957037d",
                   "7z2603-x64.exe": "0859c524b8a63551848f0c246abddcb1d0b7b656b0fbfe879f8d85e61a9e6edd"}


def download(url: str, sha256_hex: str, target: Path) -> None:
    """Download url to target, checked against its pinned SHA-256."""
    context = None
    for attempt in range(4):
        try:
            request = urllib.request.Request(url, headers={"User-Agent": "mm3d-pc-setup"})
            with urllib.request.urlopen(request, timeout=60, context=context) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() == sha256_hex:
                target.write_bytes(data)
                return
            print("  checksum mismatch, retrying", flush=True)
        except OSError as error:
            print(f"  download failed: {error}", flush=True)
            if "CERTIFICATE_VERIFY_FAILED" in str(error) and context is None:
                # No CA certificates: the pinned SHA-256 still guarantees the file.
                context = ssl._create_unverified_context()
                continue
        time.sleep(3)
    raise OSError(f"could not download {url}")


def seven_zip(tools: Path) -> Path:
    """A 7-Zip console program in tools/7zip (downloaded once)."""
    folder = tools / "7zip"
    program = folder / ("7z.exe" if sys.platform == "win32" else "7zzs")
    if program.is_file():
        return program
    folder.mkdir(parents=True, exist_ok=True)
    if sys.platform == "win32":
        for name in ("7zr.exe", "7z2603-x64.exe"):
            download(SEVEN_ZIP + name, SEVEN_ZIP_FILES[name], folder / name)
        # 7zr reads only .7z: unpack the full 7-Zip (7z.exe + 7z.dll) from its installer.
        subprocess.run([str(folder / "7zr.exe"), "x", "-y", f"-o{folder}", str(folder / "7z2603-x64.exe"),
                        "7z.exe", "7z.dll"], check=True, stdout=subprocess.DEVNULL)
    else:
        archive = folder / "7z.tar.xz"
        download(SEVEN_ZIP + "7z2603-linux-x64.tar.xz", SEVEN_ZIP_FILES["7z2603-linux-x64.tar.xz"], archive)
        with tarfile.open(archive) as source:
            member = source.getmember("7zzs")
            member.name = "7zzs"
            source.extract(member, folder)
        archive.unlink()
        os.chmod(program, 0o755)
    return program


def install_button_styles(install: Path) -> None:
    """Omni UI's Xbox, DualShock and Nintendo prompts in resources/button_styles."""
    styles = install / "resources/button_styles"
    if all((styles / name).is_dir() for name in OMNI_STYLES):
        print("  already installed")
        return
    try:
        tools = install / "tools"
        sevenzip = seven_zip(tools)
        with tempfile.TemporaryDirectory(dir=tools) as temporary:
            temporary = Path(temporary)
            archive = temporary / "omni.rar"
            print("  downloading Omni UI for Project Restoration 1.3 by HardDanX (gamebanana.com/mods/569317)")
            download(*OMNI_UI, archive)
            subprocess.run([str(sevenzip), "x", "-y", f"-o{temporary / 'x'}", str(archive)], check=True,
                           stdout=subprocess.DEVNULL)
            for style, source in OMNI_STYLES.items():
                target = styles / style
                shutil.rmtree(target, ignore_errors=True)
                for path in (temporary / "x" / source).rglob("*.png"):
                    if path.name in OMNI_SKIP:
                        continue
                    destination = target / path.relative_to(temporary / "x" / source)
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    shutil.copy2(path, destination)
            readme = temporary / "x" / "ReadmeENG.txt"
            if readme.is_file():
                shutil.copy2(readme, styles / "Omni UI readme.txt")
        (styles / "CREDITS.txt").write_text(
            "Omni UI for Project Restoration 1.3 by HardDanX\n"
            "https://gamebanana.com/mods/569317 - CC BY-NC-ND 4.0\n"
            "Downloaded by setup from GameBanana; not redistributed with this port.\n", encoding="utf-8")
        print(f"  installed: {', '.join(OMNI_STYLES)} (choose one in F1 > Button style)")
    except (OSError, subprocess.CalledProcessError, KeyError, tarfile.TarError) as error:
        # Optional: the game keeps the 3DS prompts.
        print(f"  skipped ({error}); the game uses the 3DS button prompts")


def default_jobs() -> int:
    """Parallel compiles: one per core but one, and about 1.5 GB of memory each
    (MM3D_JOBS overrides it)."""
    if os.environ.get("MM3D_JOBS", "").isdigit():
        return max(1, min(16, int(os.environ["MM3D_JOBS"])))
    jobs = max(1, (os.cpu_count() or 4) - 1)
    memory = 0
    try:
        if sys.platform == "win32":
            import ctypes

            class Status(ctypes.Structure):
                _fields_ = [("length", ctypes.c_ulong), ("load", ctypes.c_ulong),
                            ("total", ctypes.c_ulonglong), ("available", ctypes.c_ulonglong),
                            ("page_total", ctypes.c_ulonglong), ("page_available", ctypes.c_ulonglong),
                            ("virtual_total", ctypes.c_ulonglong), ("virtual_available", ctypes.c_ulonglong),
                            ("extended", ctypes.c_ulonglong)]
            status = Status(); status.length = ctypes.sizeof(Status)
            if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)):
                memory = status.total
        else:
            memory = os.sysconf("SC_PAGE_SIZE") * os.sysconf("SC_PHYS_PAGES")
    except (OSError, ValueError, AttributeError):
        memory = 0
    if memory:
        jobs = min(jobs, max(1, int(memory / (1.5 * 2**30)) - 1))
    return max(1, min(16, jobs))


def find_rom(install: Path, given: str | None) -> Path:
    if given:
        rom = Path(given)
        if not rom.is_file():
            fail(f"ROM not found: {rom}")
        return rom
    candidates = sorted(p for p in install.iterdir() if p.suffix.lower() in (".3ds", ".cci"))
    if not candidates:
        fail("No ROM found. Put your decrypted Majora's Mask 3D ROM (.3ds or .cci) in "
             f"{install} or drag it onto Setup.")
    return candidates[0]


def extract(rom: Path, game: Path) -> None:
    sys.path.insert(0, str(MM3D))
    import ctr_extract  # noqa: E402
    with open(rom, "rb") as source:
        header = source.read(0x200)
    if header[0x100:0x104] != b"NCSD":
        fail(f"{rom.name} is not a .3ds/.cci (NCSD) image. CIA files are not supported yet.")
    game.mkdir(parents=True, exist_ok=True)
    ctr_extract.main(str(rom), str(game))
    if not (game / "code.bin").is_file():
        fail("The ROM is encrypted. Use a decrypted dump (for example GodMode9's "
             "'decrypted' option or a decryption tool) and run Setup again.")


def prepare_work(work: Path) -> None:
    work.mkdir(parents=True, exist_ok=True)
    for item in RECIPE.iterdir():
        if item.name.endswith(".xz"):
            target = work / item.name[:-3]
            if not target.exists():
                with lzma.open(item) as source, open(target, "wb") as out:
                    shutil.copyfileobj(source, out)
        elif not (work / item.name).exists():
            shutil.copy2(item, work / item.name)
    entries = set()
    for name in ("runtime_misses.txt", "dynamic_entries.txt"):
        path = work / name
        if path.exists():
            entries.update(line.strip() for line in path.read_text().splitlines() if line.strip())
    (work / "discovered_entries.txt").write_text("".join(f"{e}\n" for e in sorted(entries)))
    if not (work / "aot_program.json").exists():
        # Pass 1 reads only the call sites; the program itself is rebuilt from the ROM.
        shutil.copy2(work / "call_sites.json", work / "aot_program.json")


def known_selection(program_path: Path) -> dict | None:
    recipe = RECIPE / "selection.json.xz"
    if not recipe.is_file():
        return None
    with lzma.open(recipe, "rt") as source:
        selection = json.load(source)
    expected = selection.pop("program_fingerprint", None)
    program = json.loads(program_path.read_text(encoding="utf-8"))
    actual = hashlib.sha256(json.dumps(program, sort_keys=True, separators=(",", ":")).encode()).hexdigest()
    return selection if actual == expected else None


def translate(work: Path, game: Path) -> None:
    python = sys.executable
    env = dict(os.environ, MM3D_WORK=str(work))
    for attempt in range(1, 5):
        step(f"Translating the game code (pass {attempt})")
        shutil.copy2(work / "aot_program.json", work / "aot_program_callsites.json")
        run([python, MM3D / "thumb_to_a32.py", "--code", game / "code.bin", "--exheader", game / "exheader.bin",
             "--thumb-inventory", work / "thumb_inventory.json", "--call-sites", work / "aot_program_callsites.json",
             "--extra-thumb-entries", work / "discovered_entries.txt", "--output-image", work / "code_thumbfree.bin",
             "--output-map", work / "thumb_map.json"], env=env)
        run([python, MM3D / "mm3d_inventories.py"], env=env)
        run([python, MM3D / "mm3d_whole_aot_program.py", "--thumb-inventory", work / "thumb_inventory2.json",
             "--thumb-map", work / "thumb_map.json", "--code", work / "code_thumbfree.bin",
             "--inventory", work / "mm3d_inventory.csv", "--boundary-audit", work / "empty_boundary_audit.csv",
             "--executable-size", "0x51d000", "--output", work / "aot_program.json", "--force"], env=env)
        program = json.loads((work / "aot_program.json").read_text())
        unresolved = [e for f in program["functions"] for e in f["unresolved_static_edges"]]
        if not unresolved and program["counts"]["unclaimed_blocks"] == 0:
            break
    else:
        fail("The code translation did not converge (unexpected for a supported ROM).")
    step("Selecting functions")
    known = known_selection(work / "aot_program.json")
    if known is not None:
        # The translation gave exactly the program the recipe's selection was made
        # from: the selection (hours with Python on some Windows PCs) is known.
        print("  the translation matches the recipe: using its function selection")
        (work / "mm3d_functions.json").write_text(json.dumps(known))
    else:
        run([python, MM3D / "mm3d_select.py", work, work / "code_thumbfree.bin", "aot_program.json"], env=env)
    selection = json.loads((work / "mm3d_functions.json").read_text())
    thumb_map = json.loads((work / "thumb_map.json").read_text())
    if selection.get("external_functions"):
        fail("The function selection is incomplete.")
    selection["dispatch_aliases"] = thumb_map["dispatch_aliases"]
    (work / "mm3d_functions.json").write_text(json.dumps(selection))


def compile_title(work: Path, args, platform: str) -> Path:
    step("Compiling the native game module (the longest step: 10-40 minutes)")
    env = dict(os.environ, MM3D_TARGET=platform, MM3D_CXX=str(args.cxx), MM3D_AR=str(args.ar),
               MM3D_WORK=str(work))
    support = HERE / "lib" / ("libtriaevum_title_whole_aot_support.a")
    run([sys.executable, MM3D / "mm3d_build_title.py", work, support, HERE / "include", str(args.jobs)], env=env)
    suffix = ".dll" if platform == "mingw" else ".so"
    plugins = sorted((work / "aot-cache/plugins").glob(f"*/triaevum_title_aot{suffix}"),
                     key=lambda p: p.stat().st_mtime)
    if not plugins:
        fail("The game module was not produced.")
    return plugins[-1]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--install", type=Path, required=True)
    parser.add_argument("--rom")
    parser.add_argument("--cxx", type=Path, required=True)
    parser.add_argument("--ar", type=Path, required=True)
    parser.add_argument("--jobs", type=int, default=default_jobs())
    parser.add_argument("--no-shader-tour", action="store_true")
    parser.add_argument("--target", choices=("mingw", "linux"),
                        default="mingw" if sys.platform == "win32" else "linux")
    args = parser.parse_args()
    install = args.install.resolve()
    (install / "logs").mkdir(parents=True, exist_ok=True)
    log = open(install / "logs/setup.log", "a", encoding="utf-8")
    sys.stdout = Tee(sys.stdout, log)
    sys.stderr = Tee(sys.stderr, log)
    print(f"\n==== setup {time.strftime('%Y-%m-%d %H:%M:%S')} python {sys.version.split()[0]} {sys.platform}")
    platform = args.target
    started = time.time()

    step("Reading the ROM")
    game = install / "game"
    if not (game / "code.bin").is_file() or not (game / "romfs.bin").is_file():
        rom = find_rom(install, args.rom)
        print(f"  {rom}")
        extract(rom, game)
    else:
        print(f"  already extracted to {game}")
    version = SUPPORTED.get(sha256(game / "code.bin"))
    if version is None:
        shutil.rmtree(game, ignore_errors=True)
        fail("This ROM version is not supported. Supported: Majora's Mask 3D " +
             ", ".join(SUPPORTED.values()) + " (decrypted .3ds/.cci).")
    print(f"  Majora's Mask 3D {version}")

    work = install / "work"
    step("Preparing the translation recipe")
    prepare_work(work)
    translate(work, game)
    plugin = compile_title(work, args, platform)
    private = install / "private"
    private.mkdir(exist_ok=True)
    target = private / ("mm3d_title_aot.dll" if platform == "mingw" else "mm3d_title_aot.so")
    shutil.copy2(plugin, target)

    step("Writing the process manifest")
    manifest = install / "process-manifest.json"
    run([sys.executable, REPO / "tools/oot3d/native_a32_runtime/build_process_manifest.py",
         "--exheader", game / "exheader.bin", "--code-bin", game / "code.bin",
         "--romfs-image", game / "romfs.bin", "--output", manifest])
    data = json.loads(manifest.read_text())
    data["title"] = TITLE

    def relative(value):
        # Paths relative to the folder (Play starts there), so it can be moved.
        if isinstance(value, dict):
            return {key: relative(item) for key, item in value.items()}
        if isinstance(value, list):
            return [relative(item) for item in value]
        if isinstance(value, str) and Path(value).is_absolute():
            try:
                return Path(value).resolve().relative_to(install).as_posix()
            except (ValueError, OSError):
                return value
        return value
    data = relative(data)
    manifest.write_text(json.dumps(data, indent=1))
    step("Button prompt textures")
    install_button_styles(install)
    for folder in ("data/config", "data/savedata", "data/textures", "logs"):
        (install / folder).mkdir(parents=True, exist_ok=True)
    guide = install / "data/textures/PUT TEXTURE PACKS HERE.txt"
    if not guide.exists():
        guide.write_text(
            "Azahar/Citra texture packs for Majora's Mask 3D go in this folder: copy the\n"
            "contents of the pack's title folder (load/textures/0004000000125500, or the\n"
            "pack's PAL/JAP folder) here. Sub-folders are fine. Packs with or without\n"
            "pack.json work. F1 > Texture packs: 'Load custom textures' must be on.\n"
            "The button prompt style (F1 > Button style) is applied on top.\n", encoding="utf-8")
    for name in ("TriAevum.json", "oot3d_controls.json"):
        default = HERE / "defaults" / name
        if default.is_file() and not (install / "data/config" / name).exists():
            shutil.copy2(default, install / "data/config" / name)
    (install / "setup_done.json").write_text(json.dumps(
        {"version": version, "module": target.name, "seconds": round(time.time() - started)}, indent=1))
    step(f"Game ready in {round((time.time() - started) / 60)} minutes.")
    if not args.no_shader_tour:
        step("Shader tour: Link visits every area once so they never stutter the first time.\n"
             "  The game opens and closes by itself many times: THIS IS NORMAL, it is not a bug.\n"
             "  Closing this window skips it (the game works anyway; run BuildShaders later to finish it).")
        subprocess.run([sys.executable, str(HERE / "shader_tour.py"), "--install", str(install)])
    step("Done. Start the game with Play.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
