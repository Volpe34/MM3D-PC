#!/usr/bin/env bash
# Run MM3D on TriAevum for N seconds, print the last SVC events, and record a missing
# dispatch entry (kind=6) into work/mm3d/runtime_misses.txt for the next build.
I=~/mm3d_pc/play/mm3d_tri; cd $I && rm -f captures/runtime.json
timeout --kill-after=5 $(( ${1:-20} + 40 )) ./run.sh $I/private/mm3d_title_aot.so --max-seconds ${1:-20} --frames 0 --output $I/captures/runtime.json ${@:2} > captures/launch.log 2>&1
echo "exit $?"
grep -v "Gtk-WARNING\|^$\|Adwaita\|Theme\|TRIAEVUM_\|INFO (\|graphics config" captures/launch.log | tail -6
miss=$(grep -oE "dispatch exited unexpectedly: kind=6 pc=0x[0-9A-Fa-f]{8}" captures/launch.log | grep -oE "0x[0-9A-Fa-f]{8}$")
if [ -n "$miss" ]; then
  printf "%08x\n" "$miss" >> ~/mm3d_pc/work/mm3d/runtime_misses.txt
  sort -u ~/mm3d_pc/work/mm3d/runtime_misses.txt -o ~/mm3d_pc/work/mm3d/runtime_misses.txt
  echo "recorded missing entry $miss"
fi
python3 - <<'PY'
import json, os
p=os.path.expanduser('~/mm3d_pc/play/mm3d_tri/captures/runtime.json')
if not os.path.exists(p): print("no runtime.json"); raise SystemExit
d=json.load(open(p))
ev=d.get('recent_svc_events',[])
print("events:",len(ev))
for e in ev[-14:]: print(' ', {k:(hex(v) if k=='pc' else v) for k,v in e.items()})
PY
