#!/usr/bin/env python3
"""Emit the MM3D translated C++ title sources for the Android NDK build.

usage: mm3d_emit_title_sources.py <work> <output>

<work> is the directory a PC Setup left behind (it holds aot_program.json,
mm3d_functions.json and code_thumbfree.bin). <output> receives the generated
.cpp/.h shards plus TITLE_SOURCE_MANIFEST.json, which is the input of
TRIAEVUM_TRANSLATED_TITLE_DIR in ports/android/native. The sources are derived
from the player's own ROM: keep <output> private, never publish or commit it.
"""
from __future__ import annotations

import hashlib
import json
import re
import shutil
import sys
from pathlib import Path

A32_ROOT = Path(__file__).resolve().parents[1] / "oot3d/native_a32_runtime"
NAME = re.compile(r"^[A-Za-z0-9_]+\.(cpp|h)$")  # same rule as TranslatedTitleSources.cmake


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    work, output = Path(sys.argv[1]).resolve(), Path(sys.argv[2]).resolve()
    repo = Path(__file__).resolve().parents[2]
    if output == repo or output.is_relative_to(repo):
        raise SystemExit("Generated game sources must stay outside the repository")
    for name in ("aot_program.json", "mm3d_functions.json", "code_thumbfree.bin"):
        if not (work / name).is_file():
            raise SystemExit(f"Missing {work / name}: run the PC Setup first")
    sys.path.insert(0, str(A32_ROOT))
    import whole_aot_cpp  # noqa: E402

    generated = output / ".generated"
    shutil.rmtree(generated, ignore_errors=True)
    generated.mkdir(parents=True)
    # Same parameters as the desktop build (whole_aot_plugin_backend.py).
    whole_aot_cpp.generate(work / "aot_program.json", work / "mm3d_functions.json",
                           work / "code_thumbfree.bin", generated,
                           shard_count=256, shard_strategy="affinity")
    files = {}
    for path in sorted(generated.rglob("*")):
        if path.suffix not in (".cpp", ".h"):
            continue
        if path.parent != generated or not NAME.match(path.name):
            raise SystemExit(f"Unexpected generated source layout: {path}")
        files[path.name] = hashlib.sha256(path.read_bytes()).hexdigest()
    if not any(name.endswith(".cpp") for name in files):
        raise SystemExit("The generator produced no C++ sources")
    for name in files:
        shutil.copy2(generated / name, output / name)
    shutil.rmtree(generated)
    (output / "TITLE_SOURCE_MANIFEST.json").write_text(json.dumps(
        {"format": "triaevum_translated_title_source_v1", "title": "mm3d", "files": files}, indent=1))
    print(f"{len(files)} files written to {output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
