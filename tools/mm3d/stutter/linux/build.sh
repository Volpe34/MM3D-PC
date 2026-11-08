#!/usr/bin/env bash
# Linux compile check of the TriAevum runtime (runs inside the container).
# From the repository root, with Docker:
#   docker build -t mm3d-linux-build tools/mm3d/stutter/linux
#   docker run --rm -v "$PWD:/src:ro" -v "$PWD/tools/mm3d/stutter/linux:/scripts:ro" #     -v mm3d-linux-build-dir:/build mm3d-linux-build bash /scripts/build.sh
# (The working tree needs libultraship's debug/ and log/ sources; see
# docs/MM3D_STUTTER_INVESTIGATION.md, section 7.)
set -euo pipefail
rm -rf /work/src && mkdir -p /work && cp -a /src /work/src
cd /work/src
echo "== source $(git log --oneline -1)"
dxc=/opt/dxc/bin/dxc
cmake -S . -B /build -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_C_COMPILER=clang -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_POSITION_INDEPENDENT_CODE=ON \
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF -DOOT3D_WHOLE_AOT_PRODUCT_MODE=ON -DOOT3D_DIRECT_AOT_PLUGIN=ON \
  -DOOT3D_ENABLE_VULKAN_RENDERER=ON -DTHREE_DS_RECOMP_ENABLE_SSSR=OFF \
  -DTHREE_DS_RECOMP_ENABLE_NRI_UPSCALER_SDKS=OFF -DTHREE_DS_RECOMP_ENABLE_D3D12_NGX_PROVIDER=OFF \
  -DSHADERMAKE_DXC_VK_PATH=$dxc -DSHADERMAKE_DXC_PATH=$dxc "$@"
time cmake --build /build --target "${TARGETS:-oot3d_native_game}" --parallel "${JOBS:-8}"
ls -la /build/TriAevum* 2>/dev/null || find /build -maxdepth 2 -name 'TriAevum*' | head
