#!/usr/bin/env bash
# Cross-build the Windows x64 runtime from Linux with llvm-mingw (clang, libc++,
# UCRT) and MSYS2 CLANG64 libraries, then assemble the MM3D Windows package.
#   DEPS=<dir> BUILD=<dir> scripts/build-windows-mingw.sh [package-output]
set -euo pipefail
root="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
deps="${DEPS:-$root/../deps}"; build="${BUILD:-$root/../build/windows}"
llvm="$deps/llvm-mingw"; msys="$deps/msys2"
if [ ! -x "$llvm/bin/x86_64-w64-mingw32-clang++" ]; then
  mkdir -p "$llvm"
  curl -L https://github.com/mstorsjo/llvm-mingw/releases/download/20261006/llvm-mingw-20261006-ucrt-ubuntu-22.04-x86_64.tar.xz \
    | tar -xJ -C "$llvm" --strip-components=1
fi
mkdir -p "$msys"
if [ ! -f "$msys/index.json" ]; then
  curl -L -o "$msys/clang64.db" https://repo.msys2.org/mingw/clang64/clang64.db
  python3 - "$msys" <<'PY'
import io, json, subprocess, sys, tarfile
d = sys.argv[1]
data = subprocess.run(["zstd", "-dc", f"{d}/clang64.db"], capture_output=True, check=True).stdout
pkgs = {}
for m in tarfile.open(fileobj=io.BytesIO(data)).getmembers():
    if m.name.endswith("/desc"):
        fields, key = {}, None
        for line in tarfile.open(fileobj=io.BytesIO(data)).extractfile(m.name).read().decode().splitlines():
            if line.startswith("%") and line.endswith("%"): key = line.strip("%"); fields[key] = []
            elif line and key: fields[key].append(line)
        pkgs[fields["NAME"][0]] = {"file": fields["FILENAME"][0], "deps": fields.get("DEPENDS", []),
                                   "provides": fields.get("PROVIDES", [])}
json.dump(pkgs, open(f"{d}/index.json", "w"))
PY
fi
cp "$root/tools/mm3d/package/msys2_deps.py" "$msys/add.py"
(cd "$msys" && python3 add.py SDL2 shaderc glslang spirv-tools spdlog fmt libzip tinyxml2 vulkan-loader \
  vulkan-headers zlib bzip2 xz zstd glew libpng nlohmann-json)
# MSYS2's DLLs that use C++ were linked against MSYS2's libc++.dll, which
# re-exports libunwind (_Unwind_Resume); llvm-mingw's does not. spdlog is the
# only such DLL: rebuild the same version with llvm-mingw (check_pe_imports.py
# verifies every import of the package).
overrides="$deps/win-overrides"
if [ ! -f "$overrides/libspdlog-1.17.dll" ]; then
  src="$deps/spdlog_src"; mkdir -p "$src" "$overrides"
  [ -d "$src/spdlog-1.17.0" ] || curl -fL https://github.com/gabime/spdlog/archive/refs/tags/v1.17.0.tar.gz | tar xz -C "$src"
  cmake -S "$src/spdlog-1.17.0" -B "$src/build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_TOOLCHAIN_FILE="$root/CMake/toolchain-llvm-mingw.cmake" \
    -DLLVM_MINGW_ROOT="$llvm" -DMINGW_SYSROOT="$msys/clang64" \
    -DSPDLOG_BUILD_SHARED=ON -DSPDLOG_FMT_EXTERNAL_HO=ON -DSPDLOG_BUILD_EXAMPLE=OFF -DSPDLOG_BUILD_TESTS=OFF
  ninja -C "$src/build"
  cp "$src/build/libspdlog.dll" "$overrides/libspdlog-1.17.dll"
fi
# The sources include Windows headers in CamelCase; MinGW's are lower case.
inc="$llvm/x86_64-w64-mingw32/include"
for h in D3D12.h d3dCommon.h D3Dcompiler.h D3DCompiler.h DbgHelp.h DirectXMath.h DXGIDebug.h DXGIFormat.h \
         SDKDDKVer.h Shellapi.h Shlwapi.h Windows.h WTypesbase.h Xinput.h; do
  l=$(echo "$h" | tr A-Z a-z); [ -e "$inc/$h" ] || [ ! -e "$inc/$l" ] || ln -s "$l" "$inc/$h"
done
dxc="${DXC:-$root/../build/runtime/_deps/dxc-src/bin/dxc}"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE="$root/CMake/toolchain-llvm-mingw.cmake" \
  -DLLVM_MINGW_ROOT="$llvm" -DMINGW_SYSROOT="$msys/clang64" -DCMAKE_PREFIX_PATH="$msys/clang64" \
  -DCMAKE_CXX_SCAN_FOR_MODULES=OFF -DOOT3D_WHOLE_AOT_PRODUCT_MODE=ON -DOOT3D_DIRECT_AOT_PLUGIN=ON \
  -DOOT3D_ENABLE_VULKAN_RENDERER=ON -DTHREE_DS_RECOMP_ENABLE_SSSR=OFF \
  -DTHREE_DS_RECOMP_ENABLE_NRI_UPSCALER_SDKS=OFF -DTHREE_DS_RECOMP_ENABLE_D3D12_NGX_PROVIDER=OFF \
  -DNRI_ENABLE_D3D12_SUPPORT=OFF -DNRI_ENABLE_NVAPI=OFF -DNRI_ENABLE_AMDAGS=OFF \
  -DSHADERMAKE_DXC_VK_PATH="$dxc" -DSHADERMAKE_DXC_PATH="$dxc" -DSHADERMAKE_FXC_PATH=/bin/false
ninja -C "$build" TriAevum.exe triaevum_title_aot.dll oot3d_native_pica_aot_compiler.exe \
  triaevum_title_whole_aot_support
if [ $# -gt 0 ]; then
  python3 "$root/tools/mm3d/package/build_package.py" --platform windows --runtime "$build" --out "$1"
fi
