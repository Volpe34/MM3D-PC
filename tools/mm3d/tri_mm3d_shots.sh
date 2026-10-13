#!/usr/bin/env bash
# Run MM3D on TriAevum for N seconds with an optional input timeline, capturing a
# framebuffer every INTERVAL frames into captures/ and a contact sheet captures/sheet.png.
# Usage: tri_mm3d_shots.sh <seconds> [timeline.json] [extra args]
I=~/mm3d_pc/play/mm3d_tri; cd $I && rm -f captures/fb_*.bmp* captures/runtime.json
secs=${1:-30}; tl=${2:-}; if [ $# -ge 2 ]; then shift 2; else shift $#; fi
args=(--max-seconds $secs --frames 0 --output $I/captures/runtime.json
      --screenshot $I/captures/fb.bmp --screenshot-sequence --screenshot-interval ${INTERVAL:-150} --screenshot-start-frame ${START:-60})
[ -n "$tl" ] && args+=(--input-timeline "$tl")
timeout --kill-after=5 $(( secs + 40 )) ./${RUN:-run.sh} $I/private/${PLUGIN:-mm3d_title_aot.so} "${args[@]}" "$@" > captures/launch.log 2>&1
echo "exit $?"
grep -v "Gtk-WARNING\|^$\|Adwaita\|Theme\|TRIAEVUM_\|INFO (\|graphics config" captures/launch.log | tail -6
python3 - <<'PY'
import glob, os
from PIL import Image
fs = sorted(glob.glob(os.path.expanduser('~/mm3d_pc/play/mm3d_tri/captures/fb_*.bmp')))[-16:]
if not fs: print("no frames"); raise SystemExit
ims = [Image.open(f).convert('RGB') for f in fs]
w, h = 400, int(400 * ims[0].height / ims[0].width)
cols = 4; rows = (len(ims) + cols - 1) // cols
sheet = Image.new('RGB', (cols * w, rows * h))
for i, im in enumerate(ims):
    sheet.paste(im.resize((w, h)), ((i % cols) * w, (i // cols) * h))
sheet.save(os.path.expanduser('~/mm3d_pc/play/mm3d_tri/captures/sheet.png'))
print("frames:", " ".join(os.path.basename(f)[3:9] for f in fs))
PY
