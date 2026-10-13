#!/usr/bin/env python3
"""collect_dlls.py <exe/dll>... <out dir>: copy the non-system DLLs they need
(recursively) from the llvm-mingw and MSYS2 CLANG64 runtime directories.
Environment: LLVM_MINGW_ROOT, MINGW_SYSROOT."""
import os, shutil, subprocess, sys
from pathlib import Path
llvm = Path(os.environ["LLVM_MINGW_ROOT"]); sysroot = Path(os.environ["MINGW_SYSROOT"])
search = [llvm / "x86_64-w64-mingw32/bin", sysroot / "bin"]
objdump = llvm / "bin/llvm-objdump"
# Provided by Windows or the GPU driver.
system_prefixes = ("api-ms-win-", "ext-ms-")
system = {n.lower() for n in """kernel32.dll user32.dll gdi32.dll shell32.dll ole32.dll oleaut32.dll advapi32.dll
 dbghelp.dll opengl32.dll vulkan-1.dll ws2_32.dll winmm.dll imm32.dll version.dll setupapi.dll cfgmgr32.dll
 shlwapi.dll comdlg32.dll dwmapi.dll uxtheme.dll bcrypt.dll crypt32.dll ncrypt.dll secur32.dll iphlpapi.dll
 hid.dll dinput8.dll xinput1_4.dll ksuser.dll avrt.dll mfplat.dll ntdll.dll rpcrt4.dll dxgi.dll d3d11.dll
 d3d12.dll userenv.dll wldap32.dll normaliz.dll psapi.dll powrprof.dll winhttp.dll msvcrt.dll ucrtbase.dll""".split()}
def imports(path):
    out = subprocess.run([str(objdump), "-p", str(path)], capture_output=True, text=True).stdout
    return [l.split(":", 1)[1].strip() for l in out.splitlines() if "DLL Name:" in l]
out = Path(sys.argv[-1]); out.mkdir(parents=True, exist_ok=True)
todo = [Path(p) for p in sys.argv[1:-1]]; seen = set(); missing = set()
while todo:
    for name in imports(todo.pop()):
        key = name.lower()
        if key in seen or key in system or key.startswith(system_prefixes): continue
        seen.add(key)
        found = next((d / name for d in search if (d / name).is_file()), None)
        if found is None:
            local = [p for d in search for p in d.glob("*") if p.name.lower() == key]
            found = local[0] if local else None
        if found is None: missing.add(name); continue
        shutil.copy2(found, out / found.name); todo.append(found); print("copied", found.name)
if missing: print("MISSING", sorted(missing)); sys.exit(1)
