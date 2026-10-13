"""Run TriAevum's whole-AOT function selection on MM3D inputs (all lowerable functions)."""
from __future__ import annotations

import json
import sys
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1] / "oot3d/native_a32_runtime"
sys.path.insert(0, str(NATIVE))
sys.path.insert(0, str(NATIVE / "upstream/src"))
import select_profile_whole_aot as selector  # noqa: E402


def main() -> int:
    work = Path(sys.argv[1])
    code = Path(sys.argv[2])
    selector.operational_path = lambda entry_id: code
    empty_profile = Path(__file__).resolve().parent / "data/empty_profile.json"
    previous = work / "empty_selection.json"
    previous.write_text(json.dumps({"format": selector.SELECTION_FORMAT, "functions": [], "external_functions": []}))
    return selector.main([
        "--program", str(work / sys.argv[3]),
        "--profile", str(empty_profile),
        "--selection", str(previous),
        "--all-lowerable", "--ignore-performance-exclusions",
        "--output", str(work / "mm3d_functions.json"),
    ])


if __name__ == "__main__":
    raise SystemExit(main())
