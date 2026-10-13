#!/usr/bin/env bash
# Majora's Mask 3D (TriAevum PC port). F1 = settings, F11 = fullscreen.
cd -- "$(dirname -- "$(readlink -f -- "$0")")"
if [[ ! -f private/mm3d_title_aot.so ]]; then
  echo "The game is not prepared yet: run ./Setup.sh first."; exit 1
fi
mkdir -p logs data/shader_corpus/runs
[[ -f logs/last_run.log ]] && mv -f logs/last_run.log logs/previous_run.log
export TRIAEVUM_LOG_FILE="$PWD/logs/last_run.log" TRIAEVUM_CRASH_DIR="$PWD/logs" TRIAEVUM_REPORT_TITLE=MM3D
# Bundled libstdc++ only when it is newer than the system one (GPU drivers may need the newer).
PATH="$PATH:/sbin:/usr/sbin"  # ldconfig
newest() { grep -ao 'GLIBCXX_3\.4\.[0-9]*' "$1" 2>/dev/null | sort -uV | tail -1; }
system_cxx="$(ldconfig -p 2>/dev/null | awk '/libstdc\+\+\.so\.6 .*x86-64/ {print $NF; exit}')"
libs="$PWD/lib"
if [[ -f lib/compat/libstdc++.so.6 ]]; then
  ours="$(newest lib/compat/libstdc++.so.6)"; theirs="$(newest "$system_cxx")"
  if [[ -z "$theirs" || "$(printf '%s\n%s\n' "$ours" "$theirs" | sort -V | tail -1)" != "$theirs" ]]; then
    libs="$libs:$PWD/lib/compat"
  fi
fi
# Bundled libwayland only when the system has none or an older one (the system's
# GPU driver may need its own newer libwayland).
if [[ -f lib/wayland/VERSION ]]; then
  system_wayland="$(readlink -f "$(ldconfig -p 2>/dev/null | awk '/libwayland-client\.so\.0 .*x86-64/ {print $NF; exit}')" 2>/dev/null)"
  theirs="${system_wayland##*libwayland-client.so.}"; ours="$(cat lib/wayland/VERSION)"
  if [[ -z "$system_wayland" || ! -f "$system_wayland" || "$(printf '%s\n%s\n' "$ours" "$theirs" | sort -V | tail -1)" != "$theirs" ]]; then
    libs="$libs:$PWD/lib/wayland"
  fi
fi
export LD_LIBRARY_PATH="$libs${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
python3 setup/TriAevum/tools/mm3d/package/prepare_shaders.py "$PWD" || true
shaders=()
if [[ -f data/shader_corpus/mm3d.o3ps && -f data/shader_corpus/all_pipelines.json ]]; then
  shaders=(--pica-aot-shader-pack data/shader_corpus/mm3d.o3ps
           --pica-pipeline-manifest data/shader_corpus/all_pipelines.json --pica-pipeline-prewarm)
fi
stamp="$(date +%s)_$$"
./TriAevum --title-plugin private/mm3d_title_aot.so --a32-process-manifest process-manifest.json \
  --resource-root resources --renderer nri --ui-profile oot3d --config data/config/TriAevum.json \
  --gameplay-timing native30_interpolated --presentation-rate 60 --save-data data/savedata \
  --width 1280 --height 720 \
  --pica-effective-shader-inventory "data/shader_corpus/runs/${stamp}_shaders.json" \
  --pica-pipeline-inventory "data/shader_corpus/runs/${stamp}_pipelines.json" "${shaders[@]}" "$@" \
  > logs/last_run.log 2>&1
status=$?
if [[ $status != 0 ]]; then
  echo "The game stopped with an error ($status). Send logs/last_run.log (and any report in logs/) to get help."
fi
exit $status
