"""Diagnose the TriAevum literal/CFG fixed point on MM3D: print per-iteration deltas."""
import sys, csv
from pathlib import Path
NA = Path.home() / "mm3d_pc/TriAevum/tools/oot3d/native_a32_runtime"
sys.path.insert(0, str(NA / "upstream/src"))
from oot3d_pack import a32_cpp_aot as P

W = Path.home() / "mm3d_pc/work/mm3d"
code = (Path.home() / "mm3d_pc/input/mm3d_usa/code.bin").read_bytes()
base, exe = 0x100000, 0x51d000
_, funcs, slots, _ = P._load_inputs(W / "seed_inventory.csv", W / "empty_boundary_audit.csv", len(code), base)
dec = P._Decoder(code, base)
slots = slots | set(range(base, base + exe, 4))
entries = tuple(sorted(f.entry for f in funcs))
lit, dyn, hist = set(), set(), []
for it in range(1, int(sys.argv[1]) if len(sys.argv) > 1 else 48):
    flow = P._walk_cfg(dec, slots, (*entries, *sorted(dyn)), lit, True)
    targets, sources, jumps = P._literal_targets(dec, flow.decoded_reachable, slots, True)
    ndyn = dyn | jumps
    nlit = targets - flow.control_targets - ndyn
    reach = flow.decoded_reachable
    print(f"it{it}: reach={len(reach)} lit={len(nlit)} (+{len(nlit-lit)} -{len(lit-nlit)}) dyn={len(ndyn)} (+{len(ndyn-dyn)})", flush=True)
    key = (frozenset(nlit), frozenset(reach), frozenset(ndyn))
    if key in hist:
        print("cycle back to iteration", hist.index(key) + 1); 
        if hist[-1] == key: print("converged")
        diff = sorted(nlit ^ lit)[:40]
        print("oscillating literal addrs sample:", [hex(a) for a in diff]); break
    hist.append(key); lit, dyn = nlit, ndyn
