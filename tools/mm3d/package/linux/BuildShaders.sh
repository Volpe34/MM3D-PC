#!/usr/bin/env bash
# Shader tour: Link visits every area so the first visits never stutter.
# Game windows open and close by themselves (15-40 minutes). Safe to stop and run again.
cd -- "$(dirname -- "$(readlink -f -- "$0")")"
exec python3 setup/TriAevum/tools/mm3d/package/shader_tour.py --install . "$@"
