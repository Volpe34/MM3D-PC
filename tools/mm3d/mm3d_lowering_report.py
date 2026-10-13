"""List C++ lowering rejections for MM3D functions (which instruction each one trips on)."""
from __future__ import annotations

import collections
import json
import re
import sys
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1] / "oot3d/native_a32_runtime"
sys.path.insert(0, str(NATIVE))
sys.path.insert(0, str(NATIVE / "upstream/src"))
from whole_aot_cpp import LoweringError, _load_functions, _render_function, _symbol, SELECTION_FORMAT  # noqa: E402


def main() -> int:
    program = json.loads(Path(sys.argv[1]).read_text())
    code = Path(sys.argv[2]).read_bytes()
    entries = sorted(int(f["entry"]) for f in program["functions"] if f.get("closed_static_cfg"))
    targets = {int(c["target"]) for f in program["functions"] for c in f.get("direct_calls", [])}
    selection = {"format": SELECTION_FORMAT, "functions": [{"entry": e} for e in entries],
                 "external_functions": [{"entry": t} for t in sorted(targets - set(entries))]}
    functions, external = _load_functions(program, selection, code)
    symbols = {f.entry: _symbol(f.name, f.entry) for f in functions}
    kinds = collections.Counter()
    rows = []
    for f in functions:
        try:
            _render_function(f, symbols, external)
        except LoweringError as error:
            text = str(error)
            raw = re.search(r"0x([0-9A-F]{8}) at", text)
            kinds[re.sub(r"0x[0-9A-F]+", "X", text)] += 1
            rows.append((f.entry, f.name, text))
    for kind, count in kinds.most_common():
        print(f"{count:4d}  {kind}")
    print()
    for entry, name, text in rows:
        print(f"0x{entry:08X} {name}: {text}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
