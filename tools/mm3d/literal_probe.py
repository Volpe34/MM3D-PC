"""Why is an address classified as literal data? Re-run the MM3D wrapper's fixed point and report sources."""
import sys, json, csv
from pathlib import Path
sys.argv = [sys.argv[0]] + sys.argv[1:]
TA = Path.home() / "mm3d_pc/TriAevum/tools/mm3d"
sys.path.insert(0, str(TA))
import mm3d_whole_aot_program as W   # installs the MM3D patches
P = W.pinned
W._load_thumb_slots(Path.home() / "mm3d_pc/work/mm3d/thumb_inventory.json")
W._load_synthetic_segment(Path.home() / "mm3d_pc/work/mm3d/thumb_map.json")
code = (Path.home() / "mm3d_pc/work/mm3d/code_thumbfree.bin").read_bytes()
base, exe = 0x100000, 0x51d000
_, funcs, slots, _ = P._load_inputs(Path.home() / "mm3d_pc/work/mm3d/mm3d_inventory.csv",
                                    Path.home() / "mm3d_pc/work/mm3d/empty_boundary_audit.csv", len(code), base)
dec = P._Decoder(code, base)
flow_slots = (slots | set(range(base, base + exe, 4)))
want = [int(a, 16) for a in sys.argv[1:]]
orig = W._monotonic_fixed_point
def traced(decoder, slots_, entries, follow=True, classify=True):
    lit, dyn, last = set(), set(), None
    for it in range(1, 129):
        flow = P._walk_cfg(decoder, slots_, (*sorted(set(entries)), *sorted(dyn)), lit, follow)
        targets, sources, jumps = P._literal_targets(decoder, flow.decoded_reachable, slots_, classify)
        ndyn = dyn | jumps
        new = targets - flow.control_targets - ndyn
        for w in want:
            if w in new and w not in lit:
                print(f"it{it}: 0x{w:08X} becomes literal; sources {[hex(s) for s in sources.get(w, ())]}")
        nlit = lit | new
        st = (frozenset(nlit), frozenset(flow.decoded_reachable), frozenset(ndyn))
        if st == last:
            return flow, nlit, {t: s for t, s in sources.items() if t in nlit}, it
        lit, dyn, last = nlit, ndyn, st
P._address_aware_flow_fixed_point = traced
P._address_aware_flow(dec, flow_slots, [f.entry for f in funcs], flow_slots)
