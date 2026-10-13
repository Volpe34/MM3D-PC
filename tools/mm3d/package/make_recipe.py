#!/usr/bin/env python3
"""make_recipe.py <mm3d work dir> [<setup work dir>]: refresh recipe/ from a
converged translation (and the function selection of a finished setup).

The recipe holds addresses and structure only: function entries and sizes,
Thumb entries, runtime-discovered entries, init_array addresses and the direct
call sites (address pairs), and the function selection (entries, generic names,
aliases) with the fingerprint of the program it belongs to, so setup can skip
the slow selection when its translation gives that program. No instruction
bytes or data of the game.
"""
import hashlib
import json
import lzma
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
FILES = ("thumb_inventory.json", "runtime_misses.txt", "dynamic_entries.txt",
         "ghidra_arm_inventory.csv", "init_array.txt", "empty_boundary_audit.csv",
         "empty_selection.json")


def program_fingerprint(path: Path) -> str:
    """SHA-256 of the program in canonical JSON (the same on every platform)."""
    program = json.loads(path.read_text(encoding="utf-8"))
    return hashlib.sha256(json.dumps(program, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


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
    if len(sys.argv) > 2:
        setup_work = Path(sys.argv[2])
        selection = json.loads((setup_work / "mm3d_functions.json").read_text())
        selection.pop("dispatch_aliases", None)  # setup adds them from its thumb map
        selection["program_fingerprint"] = program_fingerprint(setup_work / "aot_program.json")
        with lzma.open(recipe / "selection.json.xz", "wt", preset=9) as target:
            json.dump(selection, target, separators=(",", ":"))
        print(f"recipe: selection of {len(selection['functions'])} functions")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
