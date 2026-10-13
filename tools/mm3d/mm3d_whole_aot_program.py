"""MM3D front-end wrapper around TriAevum's structural whole-AOT program builder.

MM3D keeps float literal pools inside function bodies; some of those words decode
as conditional A32 instructions, so the pinned OoT3D literal/CFG fixed point can
oscillate between two states. Here a word, once proven to be loaded as literal
data by reachable code, stays literal data (monotonic), which guarantees
convergence. The OoT3D path is not affected.
"""

from __future__ import annotations

import sys
from pathlib import Path

NATIVE = Path(__file__).resolve().parents[1] / "oot3d/native_a32_runtime"
sys.path.insert(0, str(NATIVE))
sys.path.insert(0, str(NATIVE / "upstream/src"))

from oot3d_pack import a32_cpp_aot as pinned  # noqa: E402
import whole_aot_program  # noqa: E402


def _monotonic_fixed_point(decoder, slots, entries, follow_explicit_lr_calls=True,
                           classify_embedded_data=True):
    stable_entries = tuple(sorted(set(entries)))
    literal_data: set[int] = set()
    dynamic_entries: set[int] = set()
    sticky_proven: set[int] = set()
    last_state = None
    for iteration in range(1, 129):
        flow = pinned._walk_cfg(decoder, slots, (*stable_entries, *sorted(dynamic_entries)),
                                literal_data, follow_explicit_lr_calls)
        targets, sources, jump_targets = pinned._literal_targets(
            decoder, flow.decoded_reachable, slots, classify_embedded_data)
        new_dynamic_entries = dynamic_entries | jump_targets
        current = targets - flow.control_targets - new_dynamic_entries
        # Words proven by a real load (PC-relative LDR/VLDR, switch tables)
        # stay data once proven. Heuristic classifications (embedded strings,
        # relative tables: the word is its own source) are re-evaluated every
        # round, as in the pinned algorithm, because they only apply to words
        # not yet reached as code.
        proven = {word for word in current if any(source != word for source in sources.get(word, ()))}
        sticky = sticky_proven | proven
        sticky_proven.clear(); sticky_proven.update(sticky)
        new_literal_data = (sticky - flow.control_targets - new_dynamic_entries) | current
        state = (frozenset(new_literal_data), frozenset(flow.decoded_reachable),
                 frozenset(new_dynamic_entries))
        if state == last_state:
            filtered = {t: s for t, s in sources.items() if t in new_literal_data}
            return flow, new_literal_data, filtered, iteration
        literal_data, dynamic_entries, last_state = new_literal_data, new_dynamic_entries, state
    raise RuntimeError("MM3D literal/CFG fixed point did not converge in 128 iterations")


pinned._address_aware_flow_fixed_point = _monotonic_fixed_point

# Thumb bodies (from the Ghidra inventory) are never decoded as A32; their
# A32 conversions (thumb_to_a32.py) live in a synthetic executable segment.
_THUMB_SLOTS: set[int] = set()
_SYNTHETIC_SLOTS: set[int] = set()
_pinned_address_aware_flow = pinned._address_aware_flow


def _address_aware_flow_without_thumb(decoder, slots, entries, pointer_root_domain=None, *args, **kwargs):
    slots = (set(slots) - _THUMB_SLOTS) | _SYNTHETIC_SLOTS
    if pointer_root_domain is not None:
        pointer_root_domain = set(pointer_root_domain) - _THUMB_SLOTS
    entries = [entry for entry in entries if entry not in _THUMB_SLOTS]
    return _pinned_address_aware_flow(decoder, slots, entries, pointer_root_domain, *args, **kwargs)


pinned._address_aware_flow = _address_aware_flow_without_thumb


def _load_thumb_slots(path: Path) -> None:
    import json
    for start, end in json.loads(path.read_text())["thumb_ranges"]:
        _THUMB_SLOTS.update(range(start & ~3, end, 4))
    extra = Path(__import__("os").environ.get(
        "MM3D_THUMB_EXTRA", Path(__file__).resolve().parent / "data/thumb_extra_ranges.json"))
    for item in json.loads(extra.read_text())["ranges"]:
        _THUMB_SLOTS.update(range(int(item["start"], 0) & ~3, int(item["end"], 0), 4))


# Code that falls through into a word proves the word is code: the embedded
# string heuristic must not claim it (it only sees "not yet reachable").
_pinned_embedded_ascii = pinned._embedded_ascii_words


def _embedded_ascii_not_fallthrough(decoder, reachable, slots):
    words = _pinned_embedded_ascii(decoder, reachable, slots)
    keep = set()
    for word in words:
        previous = word - 4
        if previous in reachable:
            item = decoder.get(previous)
            terminal = (item.kind in {"branch", "indirect_branch"} and pinned._is_always(item)) or (
                pinned._writes_pc(item) and pinned._is_always(item))
            if not terminal:
                continue
        keep.add(word)
    return keep


pinned._embedded_ascii_words = _embedded_ascii_not_fallthrough


def _load_synthetic_segment(path: Path) -> None:
    import json
    data = json.loads(path.read_text())
    base, size = int(data["synthetic_base"]), int(data["synthetic_size"])
    _SYNTHETIC_SLOTS.update(range(base, base + size, 4))


def _load_extra_executable() -> None:
    # Code outside .text (a code patch's code after .data): "start-end,...".
    import os
    for item in filter(None, os.environ.get("MM3D_EXTRA_EXEC", "").split(",")):
        start, end = (int(x, 0) for x in item.split("-"))
        _SYNTHETIC_SLOTS.update(range(start, end, 4))


if __name__ == "__main__":
    _load_extra_executable()
    if "--thumb-inventory" in sys.argv:
        index = sys.argv.index("--thumb-inventory")
        _load_thumb_slots(Path(sys.argv[index + 1]))
        del sys.argv[index:index + 2]
    if "--thumb-map" in sys.argv:
        index = sys.argv.index("--thumb-map")
        _load_synthetic_segment(Path(sys.argv[index + 1]))
        del sys.argv[index:index + 2]
    status = whole_aot_program.main()
    if status == 0 and "--output" in sys.argv and __import__("os").environ.get(
            "MM3D_ALLOW_NORETURN_RESUMES"):
        # Calls to abort/throw helpers followed by literal data (GCC code in a
        # code patch) have no return point: those functions are still closed.
        import json
        output = Path(sys.argv[sys.argv.index("--output") + 1])
        program = json.loads(output.read_text())
        changed = 0
        for function in program["functions"]:
            edges = function["unresolved_static_edges"]
            if edges and all(edge["kind"] == "resume" for edge in edges):
                function["unresolved_static_edges"] = []
                function["closed_static_cfg"] = True
                changed += 1
        if changed:
            counts = program["counts"]
            counts["unresolved_static_edges"] = sum(
                len(f["unresolved_static_edges"]) for f in program["functions"])
            counts["closed_static_cfg_functions"] = sum(
                1 for f in program["functions"] if f["closed_static_cfg"])
            output.write_text(json.dumps(program))
            print(f"{changed} functions closed over no-return calls")
    raise SystemExit(status)
