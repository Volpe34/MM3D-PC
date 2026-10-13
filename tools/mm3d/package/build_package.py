#!/usr/bin/env python3
"""Assemble the MM3D PC package (no game data: the player's ROM is required).

usage: build_package.py --platform windows|linux --runtime <build dir> --out <folder>

The package holds the TriAevum runtime, the setup (Python pipeline, recipe of
addresses/structure, support library) and launch scripts. Setup builds the
game module and the process manifest from the player's own ROM.
"""
from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
TOOL_TREES = ("tools/mm3d", "tools/triaevum_release", "tools/oot3d/native_a32_runtime")
HEADER_SUFFIXES = {".h", ".hpp", ".inl", ".inc"}


def copy_tree(source: Path, target: Path, keep) -> None:
    for path in source.rglob("*"):
        if path.is_dir() or "__pycache__" in path.parts or not keep(path):
            continue
        destination = target / path.relative_to(source)
        destination.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(path, destination)


def build_zig_support(zig: Path, library: Path, include: Path) -> None:
    """The support library for the Linux module, with the same zig the setup downloads."""
    a32 = REPO / "tools/oot3d/native_a32_runtime"
    host = REPO / "tools/oot3d/native_game_runtime"
    pinned = a32 / "upstream"
    sources = (host / "oot3d_native_a32_memory.cpp", host / "oot3d_native_a32_memory_bus_defaults.cpp",
               pinned / "recomp/a32_vfp_binary64.cpp", a32 / "oot3d_native_a32_vfp_ops.cpp",
               pinned / "recomp/a32_vfp_transport.cpp")
    build = library.parent / "build"
    build.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, TRIAEVUM_ZIG=str(zig), TRIAEVUM_CXX_KIT=str(include.parents[0] / "../../../../cxx"))
    objects = []
    for source in sources:
        target = build / (source.stem + ".o")
        subprocess.run([HERE / "linux/zig-cxx", "--target=x86_64-linux-gnu", "-std=c++20", "-O2",
                        "-ffp-model=strict", "-fPIC", "-fvisibility=hidden", "-fvisibility-inlines-hidden",
                        *(f"-I{d}" for d in (host, pinned, pinned / "recomp", a32, a32 / "mass_cpp", include)),
                        "-c", source, "-o", target], env=env, check=True)
        objects.append(target)
    subprocess.run([HERE / "linux/zig-ar", "rcs", library, *objects], env=env, check=True)
    shutil.rmtree(build)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--platform", choices=("windows", "linux"), required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--sdk", type=Path, default=Path.home() / "mm3d_pc/deps/steamrt4-sdk")
    parser.add_argument("--zig", type=Path, default=Path.home() / "mm3d_pc/deps/zig_dl/zig-x86_64-linux-0.17.0/zig")
    parser.add_argument("--nlohmann", type=Path, default=Path.home() / "mm3d_pc/deps/prefix/include")
    args = parser.parse_args()
    out = args.out.resolve()
    if out.exists():
        shutil.rmtree(out)
    out.mkdir(parents=True)
    windows = args.platform == "windows"

    # Runtime.
    runtime = args.runtime.resolve()
    if windows:
        for name in ("TriAevum.exe", "triaevum_title_aot.dll", "oot3d_native_pica_aot_compiler.exe"):
            shutil.copy2(runtime / name, out / name)
        deps = Path.home() / "mm3d_pc/deps"
        env = dict(os.environ, LLVM_MINGW_ROOT=str(deps / "llvm-mingw"), MINGW_SYSROOT=str(deps / "msys2/clang64"),
                   WIN_DLL_OVERRIDES=str(deps / "win-overrides"))
        subprocess.run(["python3", HERE / "collect_dlls.py", out / "TriAevum.exe",
                        out / "oot3d_native_pica_aot_compiler.exe", out], env=env, check=True)
        # Wine runs with missing imports; Windows does not start. Check them all.
        subprocess.run(["python3", HERE / "check_pe_imports.py", out, deps / "llvm-mingw/bin/llvm-objdump"],
                       check=True)
        for name in ("Setup.bat", "Play.bat", "BuildShaders.bat"):
            shutil.copy2(HERE / "windows" / name, out / name)
        (out / "setup").mkdir()
        shutil.copy2(HERE / "windows_setup/setup.ps1", out / "setup/setup.ps1")
    else:
        # Built in the Steam Runtime 4 SDK (glibc 2.41 headers, older symbol versions).
        for name in ("TriAevum", "triaevum_title_aot.so", "oot3d_native_pica_aot_compiler"):
            shutil.copy2(runtime / name, out / name)
        sdk_work = runtime.parent
        subprocess.run(["python3", HERE / "collect_so.py", args.sdk, sdk_work, out,
                        *(f"/home/work/{runtime.name}/{name}" for name in
                          ("TriAevum", "triaevum_title_aot.so", "oot3d_native_pica_aot_compiler"))], check=True)
        # The SDK's SDL2 is sdl2-compat, which loads SDL3 at runtime.
        # Its less common dependencies come along, and libwayland-client (SDL3 needs
        # 1.23, Ubuntu 24.04 has 1.22; newer clients work with any compositor).
        # X11 and audio are the system's.
        for name in ("libSDL3.so.0", "libdecor-0.so.0", "libfribidi.so.0", "libXss.so.1", "libXtst.so.6",
                     "libwayland-client.so.0", "libwayland-cursor.so.0", "libwayland-egl.so.1"):
            shutil.copy2((args.sdk / "usr/lib/x86_64-linux-gnu" / name).resolve(), out / "lib" / name)
        for name in ("Setup.sh", "Play.sh", "BuildShaders.sh"):
            shutil.copy2(HERE / "linux" / name, out / name)
        (out / "setup").mkdir()
        for name in ("linux_setup.py", "zig-cxx", "zig-ar", "ld.lld"):
            shutil.copy2(HERE / "linux" / name, out / "setup" / name)
        # libstdc++ headers and link libraries of the runtime (GPL-3.0 with the
        # GCC Runtime Library Exception), so the module matches its C++ ABI.
        kit = out / "setup/cxx"
        shutil.copytree(args.sdk / "usr/include/c++/14", kit / "include/c++/14")
        shutil.copytree(args.sdk / "usr/include/x86_64-linux-gnu/c++/14", kit / "include/x86_64-linux-gnu/c++/14")
        (kit / "lib").mkdir()
        for name in ("libstdc++.so.6", "libgcc_s.so.1"):
            shutil.copy2((args.sdk / "usr/lib/x86_64-linux-gnu" / name).resolve(), kit / "lib" / name)
        copyright = args.sdk / "usr/share/doc/gcc-14-base/copyright"
        if copyright.is_file():
            shutil.copy2(copyright, kit / "copyright.txt")
        (kit / "README.txt").write_text(
            "libstdc++ 14 headers and libraries from the Steam Runtime 4 SDK (Debian 13),\n"
            "GPL-3.0 with the GCC Runtime Library Exception (see\n"
            "https://www.gnu.org/licenses/gcc-exception-3.1.html).\n")
    (out / "resources").mkdir()
    shutil.copy2(REPO / "runtime/three_ds_recomp/resources/gamecontrollerdb.txt"
                 if (REPO / "runtime/three_ds_recomp/resources/gamecontrollerdb.txt").exists()
                 else Path.home() / "mm3d_pc/play/mm3d_tri/resources/gamecontrollerdb.txt",
                 out / "resources/gamecontrollerdb.txt")

    # Setup: the TriAevum tool trees the pipeline runs from.
    tree = out / "setup/TriAevum"
    for relative in TOOL_TREES:
        copy_tree(REPO / relative, tree / relative,
                  lambda p: p.suffix not in (".pyc",) and "package" not in p.relative_to(REPO).parts[2:3])
    shutil.copy2(REPO / "tools/oot3d/operational_inputs.json", tree / "tools/oot3d/operational_inputs.json")
    copy_tree(REPO / "tools/oot3d/native_game_runtime", tree / "tools/oot3d/native_game_runtime",
              lambda p: p.suffix in HEADER_SUFFIXES or p.name == "triaevum_title_whole_aot_plugin.cpp")
    package = tree / "tools/mm3d/package"
    package.mkdir(parents=True, exist_ok=True)
    for name in ("mm3d_setup.py", "prepare_shaders.py", "merge_pipelines.py", "shader_tour.py"):
        shutil.copy2(HERE / name, package / name)
    shutil.copytree(HERE / "recipe", package / "recipe")
    (package / "lib").mkdir()
    copy_tree(args.nlohmann / "nlohmann", package / "include/nlohmann", lambda p: True)
    if windows:
        shutil.copy2(runtime / "libtriaevum_title_whole_aot_support.a", package / "lib")
    else:
        build_zig_support(args.zig, package / "lib/libtriaevum_title_whole_aot_support.a", package / "include")
    (package / "defaults").mkdir()
    for name in ("TriAevum.json", "oot3d_controls.json"):
        source = HERE / "defaults" / name
        if source.exists():
            shutil.copy2(source, package / "defaults" / name)
    shutil.copy2(HERE / "README.txt", out / "README.txt")
    (out / "package.json").write_text(json.dumps({"platform": args.platform}, indent=1))
    if windows:
        # cmd.exe mis-parses some LF-only batch files.
        for path in (out / "Setup.bat", out / "Play.bat", out / "BuildShaders.bat", out / "setup/setup.ps1", out / "README.txt"):
            text = path.read_bytes().replace(b"\r\n", b"\n")
            path.write_bytes(text.replace(b"\n", b"\r\n"))
    print("package:", out)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
