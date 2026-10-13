"""Compile the MM3D whole-AOT program into a TriAevum title module.

usage: mm3d_build_title.py <work> <support library> <nlohmann include> [jobs]
Environment: MM3D_TARGET=linux (default) or mingw (Windows DLL, llvm-mingw);
MM3D_CXX / MM3D_AR select the compiler and archiver.
"""
from __future__ import annotations

import json
import os
import sys
from pathlib import Path

RELEASE = Path(__file__).resolve().parents[1] / "triaevum_release"
sys.path.insert(0, str(RELEASE))
import release_platform  # noqa: E402
from whole_aot_plugin_backend import WholeAotPluginToolchain, build_whole_aot_plugin  # noqa: E402


def main() -> int:
    work = Path(sys.argv[1])
    support = Path(sys.argv[2])
    nlohmann = Path(sys.argv[3])
    jobs = int(sys.argv[4]) if len(sys.argv) > 4 else 8
    target = os.environ.get("MM3D_TARGET", "linux")
    platform = release_platform.MINGW if target == "mingw" else release_platform.LINUX
    default_cxx, default_ar = ("/usr/bin/clang++", "/usr/bin/llvm-ar")
    result = build_whole_aot_plugin(
        program_path=work / "aot_program.json",
        selection_path=work / "mm3d_functions.json",
        code_path=work / "code_thumbfree.bin",
        cache_root=work / "aot-cache",
        toolchain=WholeAotPluginToolchain(
            compiler=Path(os.environ.get("MM3D_CXX", default_cxx)),
            archiver=Path(os.environ.get("MM3D_AR", default_ar)),
            support_library=support,
            nlohmann_include=nlohmann,
            target_triple=platform.target,
            profile=platform.profile,
        ),
        shard_count=256,
        jobs=jobs,
    )
    print(json.dumps({k: v for k, v in result.items() if k in ("status", "plugin", "objects_compiled", "elapsed_seconds")}, indent=1))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
