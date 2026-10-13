"""Regenerate MM3D front-end inputs from thumb_map.json + the Ghidra ARM inventory."""
import json, csv
from pathlib import Path
import os
W = Path(os.environ.get("MM3D_WORK", Path.home() / "mm3d_pc/work/mm3d"))
m = json.load(open(W / "thumb_map.json"))
ranges = []
for f in m["thumb_functions"]:
    for rg in f["ranges"].split(";"):
        a, b = (int(x, 16) for x in rg.split("-"))
        ranges.append([a, b])
json.dump({"thumb_functions": m["thumb_functions"], "thumb_ranges": sorted(ranges)}, open(W / "thumb_inventory2.json", "w"))
rows = list(csv.DictReader(open(W / "ghidra_arm_inventory.csv")))
thumb_slots = set()
for a, b in ranges:
    thumb_slots.update(range(a & ~3, b, 4))
rows = [r for r in rows if int(r["entry"], 16) not in thumb_slots]
ents = sorted(f["a32_entry"] for f in m["functions"])
end = m["synthetic_base"] + m["synthetic_size"]
for f in m["functions"]:
    v = f["a32_entry"]
    nxt = min([e for e in ents if e > v] + [end])
    rows.append({"entry": f"0x{v:08X}", "size": max(4, nxt - v), "name": f["name"]})
# runtime-discovered A32 roots: C++ static constructors (relative init table)
# and entries observed by the 3ds_recompiler bring-up runtime
extra = set()
for name in ("init_array.txt", "discovered_entries.txt"):
    path = W / name
    if path.exists():
        for line in path.read_text().split():
            value = int(line, 16)
            if not value & 1 and value not in thumb_slots:
                extra.add(value)
for value in sorted(extra):
    rows.append({"entry": f"0x{value:08X}", "size": 4, "name": f"runtime_entry_{value:08X}"})
for r in m["inline_argument_rewrites"]:
    rows.append({"entry": f"0x{r['stub']:08X}", "size": 8, "name": f"inline_arg_stub_{r['site']:08X}"})
seen = set(); out = []
for r in rows:
    if r["entry"] not in seen:
        seen.add(r["entry"]); out.append(r)
w = csv.DictWriter(open(W / "mm3d_inventory.csv", "w", newline=""), fieldnames=["entry", "size", "name"])
w.writeheader(); w.writerows(out)
print(len(out), "inventory entries;", len(ranges), "thumb ranges")
