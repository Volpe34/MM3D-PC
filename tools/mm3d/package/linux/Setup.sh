#!/usr/bin/env bash
# Majora's Mask 3D (TriAevum PC port): one-time setup from your own ROM.
# Put your decrypted ROM (.3ds/.cci) in this folder, or: ./Setup.sh /path/to/rom.3ds
cd -- "$(dirname -- "$(readlink -f -- "$0")")"
if ! command -v python3 >/dev/null; then
  echo "Python 3 is needed (install the python3 package of your distribution)."; exit 1
fi
mkdir -p logs
python3 setup/linux_setup.py "$@" 2>&1 | tee logs/setup_console.log
exit "${PIPESTATUS[0]}"
