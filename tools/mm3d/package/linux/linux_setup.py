#!/usr/bin/env python3
"""One-time Linux setup: downloads the zig toolchain (a self-contained clang)
and capstone into tools/ (each checked against a pinned SHA-256), then builds
the game module from your ROM. Nothing of the game is downloaded.

usage: python3 setup/linux_setup.py [rom.3ds]
"""
from __future__ import annotations

import hashlib
import io
import os
import shutil
import ssl
import subprocess
import sys
import tarfile
import time
import urllib.request
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
TOOLS = ROOT / "tools"
ZIG = ("https://ziglang.org/download/0.17.0/zig-x86_64-linux-0.17.0.tar.xz",
       "1cbe9df9f27e6b78d14ccbca43b6703a404ef79ef1c463de901d7f088d4e2026")
CAPSTONE = ("https://files.pythonhosted.org/packages/d1/39/17747862222bb062e86b501f1f148d5ff589b77908b080d30f7f085cbfb7/"
            "capstone-5.0.9-py3-none-manylinux_2_17_x86_64.manylinux2014_x86_64.whl",
            "273fd8d747d2e35c88f91450be51a603ecfaafb00d96d9f315dcb8689c86193e")


def download(url: str, sha256: str) -> bytes:
    context = None
    for attempt in range(4):
        try:
            with urllib.request.urlopen(url, timeout=60, context=context) as response:
                data = response.read()
            if hashlib.sha256(data).hexdigest() == sha256:
                return data
            print("  checksum mismatch, retrying", flush=True)
        except OSError as error:
            print(f"  download failed: {error}", flush=True)
            if "CERTIFICATE_VERIFY_FAILED" in str(error) and context is None:
                # No CA certificates on this system: the pinned SHA-256 still
                # guarantees the file, so fetch it without TLS verification.
                print("  (no usable CA certificates; the download is verified by its SHA-256)", flush=True)
                context = ssl._create_unverified_context()
                continue
        time.sleep(3)
    raise SystemExit(f"\nSETUP FAILED: could not download {url} (check the Internet connection).")


def main() -> int:
    print(f"Majora's Mask 3D PC setup in {ROOT}")
    if sys.version_info < (3, 9):
        raise SystemExit("SETUP FAILED: Python 3.9 or newer is needed.")
    if shutil.disk_usage(ROOT).free < 4 * 2**30:
        raise SystemExit("SETUP FAILED: setup needs about 4 GB of free disk space.")
    TOOLS.mkdir(exist_ok=True)
    zig_dir = TOOLS / "zig"
    if not (zig_dir / "zig").is_file():
        print("== Downloading the zig toolchain (about 55 MB, ziglang.org)", flush=True)
        data = download(*ZIG)
        unpack = TOOLS / "zig-unpack"
        shutil.rmtree(unpack, ignore_errors=True)
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:xz") as archive:
            archive.extractall(unpack, filter="data") if hasattr(tarfile, "data_filter") else archive.extractall(unpack)
        shutil.rmtree(zig_dir, ignore_errors=True)
        next(unpack.iterdir()).rename(zig_dir)
        unpack.rmdir()
    for name in ("zig-cxx", "zig-ar", "ld.lld"):
        shutil.copy2(Path(__file__).with_name(name), zig_dir / name)
        os.chmod(zig_dir / name, 0o755)
    pylib = TOOLS / "pylib"
    if not (pylib / "capstone").is_dir():
        print("== Downloading capstone 5.0.9 (disassembler, PyPI)", flush=True)
        with zipfile.ZipFile(io.BytesIO(download(*CAPSTONE))) as wheel:
            wheel.extractall(pylib)
    os.environ["PYTHONPATH"] = str(pylib) + os.pathsep + os.environ.get("PYTHONPATH", "")
    os.environ["TRIAEVUM_CXX_KIT"] = str(ROOT / "setup/cxx")
    os.environ["ZIG_GLOBAL_CACHE_DIR"] = str(ROOT / "work/zig-cache")
    os.environ["ZIG_LOCAL_CACHE_DIR"] = str(ROOT / "work/zig-cache")
    arguments = [sys.executable, str(ROOT / "setup/TriAevum/tools/mm3d/package/mm3d_setup.py"),
                 "--install", str(ROOT), "--target", "linux",
                 "--cxx", str(zig_dir / "zig-cxx"), "--ar", str(zig_dir / "zig-ar")]
    if len(sys.argv) > 1:
        arguments += ["--rom", sys.argv[1]]
    if subprocess.run(arguments).returncode != 0:
        print("\nSETUP FAILED. Send logs/setup.log to get help.")
        return 1
    print("\nSetup complete. Start the game with ./Play.sh")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
