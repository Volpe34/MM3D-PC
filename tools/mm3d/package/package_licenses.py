"""License texts for the libraries bundled with the MM3D packages.

Windows: each DLL maps to the license folders of the MSYS2 CLANG64 packages it
comes from (or llvm-mingw / the spdlog source it was built from). Linux: each
shared library maps to the Debian copyright file of its Steam Runtime 4 SDK
package. A bundled library without a known license fails the package build.
"""
from __future__ import annotations

import shutil
from pathlib import Path

WINDOWS_DLLS = {
    "SDL2.dll": ["msys2:SDL2"],
    "glew32.dll": ["msys2:glew"],
    "libbz2-1.dll": ["msys2:bzip2"],
    "liblzma-5.dll": ["msys2:xz"],
    "libzstd.dll": ["msys2:zstd"],
    "zlib1.dll": ["msys2:zlib"],
    "libzip.dll": ["msys2:libzip"],
    "libshaderc_shared.dll": ["msys2:shaderc", "msys2:glslang", "msys2:spirv-tools"],
    "libspdlog-1.17.dll": ["spdlog", "msys2:fmt"],
    "libc++.dll": ["llvm-mingw"],
    "libunwind.dll": ["llvm-mingw"],
}


def _copy_tree(source: Path, target: Path) -> None:
    if source.is_dir():
        shutil.copytree(source, target, dirs_exist_ok=True)
    else:
        target.mkdir(parents=True, exist_ok=True)
        shutil.copy2(source, target / source.name)


def windows(out: Path, deps: Path) -> None:
    folder = out / "LICENSES/bundled"
    sources = {
        "llvm-mingw": deps / "llvm-mingw/LICENSE.TXT",
        "spdlog": deps / "spdlog_src/spdlog-1.17.0/LICENSE",
    }
    for dll in sorted(p.name for p in out.glob("*.dll")):
        if dll == "triaevum_title_aot.dll":
            continue  # TriAevum's own (GPL-3.0-or-later)
        if dll not in WINDOWS_DLLS:
            raise SystemExit(f"no license known for bundled {dll}: add it to package_licenses.py")
        for entry in WINDOWS_DLLS[dll]:
            kind, _, name = entry.partition(":")
            source = deps / "msys2/clang64/share/licenses" / name if kind == "msys2" else sources[kind]
            if not source.exists():
                raise SystemExit(f"license source missing for {dll}: {source}")
            _copy_tree(source, folder / (name or kind))
    (folder / "README.txt").write_text(
        "Licenses of the libraries bundled with the Windows package:\n"
        "MSYS2 CLANG64 packages (sources: https://packages.msys2.org), libc++ and\n"
        "libunwind from llvm-mingw (https://github.com/mstorsjo/llvm-mingw), spdlog 1.17.0\n"
        "built from https://github.com/gabime/spdlog with fmt.\n")


def _debian_package(sdk: Path, library: str) -> str | None:
    wanted = "/usr/lib/x86_64-linux-gnu/" + library
    for listing in sorted((sdk / "var/lib/dpkg/info").glob("*.list")):
        try:
            lines = listing.read_text(errors="replace").splitlines()
        except OSError:
            continue
        if wanted in lines:
            return listing.name.removesuffix(".list").split(":")[0]
    # Packages unpacked into the SDK without dpkg (TriAevum's SDK dependencies):
    # their documentation folder starts with the library's name.
    stem = library.split(".so")[0]
    for doc in sorted((sdk / "usr/share/doc").glob(stem + "*")):
        if (doc / "copyright").is_file() and not doc.name.endswith("-dev"):
            return doc.name
    return None


def linux(out: Path, sdk: Path) -> None:
    folder = out / "LICENSES/bundled"
    folder.mkdir(parents=True, exist_ok=True)
    for library in sorted(p for p in (out / "lib").rglob("*.so*") if p.is_file()):
        if library.name == "triaevum_title_aot.so":
            continue  # TriAevum's own (GPL-3.0-or-later)
        package = _debian_package(sdk, library.name)
        copyright = sdk / "usr/share/doc" / (package or "") / "copyright"
        if package is None or not copyright.is_file():
            raise SystemExit(f"no license known for bundled {library.name}")
        shutil.copy2(copyright, folder / f"{package}.copyright.txt")
    # The texts the Debian copyright files refer to (GPL, LGPL, Apache, ...).
    shutil.copytree(sdk / "usr/share/common-licenses", folder / "common-licenses", dirs_exist_ok=True)
    (folder / "README.txt").write_text(
        "Licenses of the libraries bundled in lib/ (and the libstdc++ kit in setup/cxx), as\n"
        "Debian copyright files of the Steam Runtime 4 SDK packages they come from.\n"
        "Their sources: https://repo.steampowered.com/steamrt4/ (source packages) and\n"
        "https://sources.debian.org (Debian 13 'trixie').\n")


def common(out: Path, repo: Path, platform: str) -> None:
    for name in ("LICENSE", "THIRD_PARTY_NOTICES.md", "LICENSE_SCOPE.md"):
        shutil.copy2(repo / name, out / name)
    shutil.copytree(repo / "LICENSES", out / "LICENSES", dirs_exist_ok=True)
    (out / "SOURCE.txt").write_text(
        "Majora's Mask 3D PC port on the TriAevum runtime - source code\n\n"
        "The programs in this package are free software (GPL-3.0-or-later, see LICENSE,\n"
        "LICENSE_SCOPE.md and THIRD_PARTY_NOTICES.md). Their complete corresponding source\n"
        "is the repository this release comes from:\n"
        "  https://github.com/Volpe34/MM3D-PC (the release's tag)\n"
        "Bundled third-party libraries and their licenses: LICENSES/bundled.\n"
        "No Nintendo code or data is included; setup builds the game module from your ROM.\n",
        encoding="utf-8")
