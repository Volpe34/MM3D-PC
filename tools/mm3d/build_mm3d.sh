#!/usr/bin/env bash
# MM3D whole-AOT pipeline: Thumb conversion -> inventories -> front end -> selection -> C++ -> title .so
set -euo pipefail
# MM3D_WORK / MM3D_INPUT / MM3D_PLUGIN_OUT select another game variant (e.g.
# work/mm3d_rst, input/mm3d_rst for Project Restoration); MM3D_SYNTH_BASE and
# MM3D_EXTRA_EXEC are passed to the converter and the front end.
T=~/mm3d_pc/TriAevum; PY=~/mm3d_pc/.venv/bin/python
W=${MM3D_WORK:-~/mm3d_pc/work/mm3d}; I=${MM3D_INPUT:-~/mm3d_pc/input/mm3d_usa}
OUT=${MM3D_PLUGIN_OUT:-~/mm3d_pc/play/mm3d_tri/private/mm3d_title_aot.so}
export MM3D_WORK=$W
cd "$T"
# Dynamic entries: indirect-transfer targets recorded by the bring-up runtime
# (R3DS_ENTRY_LOG), without return sites; merged with recorded misses.
if [ -f $W/entry_log.txt ] && [ -z "${MM3D_SKIP_ENTRY_LOG:-}" ]; then
  $PY tools/mm3d/filter_entry_log.py $I/code.bin 0x51cfb4 $W/entry_log.txt $W/dynamic_entries.txt
fi
cat $W/runtime_misses.txt $W/dynamic_entries.txt 2>/dev/null | sort -u > $W/discovered_entries.txt
# Conversion and front end iterate: newly reached A32 code can contain new
# BLX call sites into Thumb code, which the next conversion pass retargets.
[ -f $W/aot_program.json ] || cp $W/aot_program2.json $W/aot_program.json
for pass in 1 2 3 4; do
  echo "== thumb conversion (pass $pass)"
  cp $W/aot_program.json $W/aot_program_callsites.json
  $PY tools/mm3d/thumb_to_a32.py --code $I/code.bin --exheader $I/exheader.bin --thumb-inventory $W/thumb_inventory.json \
    --call-sites $W/aot_program_callsites.json --extra-thumb-entries $W/discovered_entries.txt \
    --output-image $W/code_thumbfree.bin --output-map $W/thumb_map.json
  $PY tools/mm3d/mm3d_inventories.py
  echo "== front end (pass $pass)"
  $PY tools/mm3d/mm3d_whole_aot_program.py --thumb-inventory $W/thumb_inventory2.json --thumb-map $W/thumb_map.json \
    --code $W/code_thumbfree.bin --inventory $W/mm3d_inventory.csv --boundary-audit $W/empty_boundary_audit.csv \
    --executable-size 0x51d000 --output $W/aot_program.json --force
  if $PY - <<PY
import json, os; m=json.load(open("$W/aot_program.json")); c=m["counts"]
# MM3D_ALLOW_NORETURN_RESUMES: a call's return point that is not code (calls
# to abort/throw helpers followed by literal data) is not an error.
unresolved = [e for f in m["functions"] for e in f["unresolved_static_edges"]
              if not (os.environ.get("MM3D_ALLOW_NORETURN_RESUMES") and e["kind"] == "resume")]
print("unresolved", c["unresolved_static_edges"], "counted", len(unresolved), "unclaimed", c["unclaimed_blocks"])
raise SystemExit(0 if not unresolved and c["unclaimed_blocks"] == 0 else 1)
PY
  then break; fi
done
$PY - <<PY
import json, os; m=json.load(open("$W/aot_program.json")); c=m["counts"]
unresolved = [e for f in m["functions"] for e in f["unresolved_static_edges"]
              if not (os.environ.get("MM3D_ALLOW_NORETURN_RESUMES") and e["kind"] == "resume")]
assert not unresolved and c["unclaimed_blocks"] == 0, (c, unresolved[:5])
PY
echo "== differential test"
$PY tools/mm3d/test_thumb_to_a32.py | tail -1
echo "== selection"
$PY tools/mm3d/mm3d_select.py $W $W/code_thumbfree.bin aot_program.json | tail -1
$PY - <<PY
import json
s=json.load(open("$W/mm3d_functions.json")); t=json.load(open("$W/thumb_map.json"))
assert not s["external_functions"], s["external_functions"]
s["dispatch_aliases"]=t["dispatch_aliases"]; json.dump(s,open("$W/mm3d_functions.json","w"))
print("aliases", len(s["dispatch_aliases"]))
PY
echo "== compile"
$PY tools/mm3d/mm3d_build_title.py $W ~/mm3d_pc/work/forge_support/libtriaevum_title_whole_aot_support.a ~/mm3d_pc/deps/prefix/include ${JOBS:-10}
P=$(ls -t $W/aot-cache/plugins/*/triaevum_title_aot.so | head -1)
cp "$P" "$OUT"
echo "installed $P"
