#!/usr/bin/env python3
"""make_recipe.py <mm3d work dir>: refresh recipe/ from a converged translation.

The recipe holds addresses and structure only: function entries and sizes,
Thumb entries, runtime-discovered entries, init_array addresses and the direct
call sites (address pairs). No instruction bytes, hashes or data of the game.
"""
import json
import lzma
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FILES = ("thumb_inventory.json", "runtime_misses.txt", "dynamic_entries.txt",
         "ghidra_arm_inventory.csv", "init_array.txt", "empty_boundary_audit.csv",
         "empty_selection.json")


def main() -> int:
    work = Path(sys.argv[1])
    recipe = HERE / "recipe"
    recipe.mkdir(exist_ok=True)
    for name in FILES:
        shutil.copy2(work / name, recipe / name)
    program = json.loads((work / "aot_program2.json").read_text())
    calls = sorted({(c["site"], c["target"]) for f in program["functions"]
                    for c in f.get("direct_calls", []) if c.get("target") is not None})
    with lzma.open(recipe / "call_sites.json.xz", "wt", preset=9) as target:
        json.dump({"format": "mm3d-call-sites-v1", "functions": [
            {"direct_calls": [{"site": s, "target": t} for s, t in calls]}]}, target, separators=(",", ":"))
    print(f"recipe: {len(calls)} call sites")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
