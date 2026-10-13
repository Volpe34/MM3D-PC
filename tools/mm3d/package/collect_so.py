#!/usr/bin/env python3
"""collect_so.py <sdk> <sdk work> <out> <binary in the sdk work>...: copy the
shared libraries the Linux binaries need from the Steam Runtime 4 SDK into
<out>/lib. Graphics drivers, the X/GL stack and glibc stay the system's;
libstdc++ goes to lib/compat and Play.sh uses it only when the system one is older.
"""
import re
import shutil
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[2]
SYSTEM = re.compile(r"^(linux-vdso|ld-linux|libc\.so|libm\.so|libdl\.so|libpthread\.so|librt\.so|libresolv\.so|"
                    r"libvulkan\.so|libGL|libEGL|libOpenGL|libX|libxcb|libdrm|libwayland|libgbm|libasound|"
                    r"libpulse|libdbus|libudev|libsystemd)")
COMPAT = re.compile(r"^(libstdc\+\+\.so|libgcc_s\.so)")


def main() -> int:
    sdk, work, out = (Path(a).resolve() for a in sys.argv[1:4])
    binaries = sys.argv[4:]
    listing = subprocess.run(["bash", str(REPO / "scripts/run-in-steamrt4.sh"), str(sdk), str(work), "ldd", *binaries],
                             capture_output=True, text=True, check=True).stdout
    (out / "lib/compat").mkdir(parents=True, exist_ok=True)
    for line in listing.splitlines():
        match = re.match(r"\s*(\S+) => (/\S+)", line)
        if not match:
            if "not found" in line:
                raise SystemExit(f"missing library: {line.strip()}")
            continue
        name, path = match.groups()
        if SYSTEM.match(name):
            continue
        source = sdk / path.lstrip("/")
        if not source.exists():
            source = work / path.removeprefix("/home/work/")
        target = out / "lib" / ("compat" if COMPAT.match(name) else "") / name
        shutil.copy2(source.resolve(), target)
        print("  ", target.relative_to(out))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
