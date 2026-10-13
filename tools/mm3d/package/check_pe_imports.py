#!/usr/bin/env python3
"""check_pe_imports.py <package dir> [llvm-objdump]: every function a packaged
EXE/DLL imports from another packaged DLL must be exported by it. (Wine binds a
missing import to a stub and runs anyway; Windows refuses to start.)"""
import re
import subprocess
import sys
from pathlib import Path


def tables(objdump: str, path: Path):
    text = subprocess.run([objdump, "-p", str(path)], capture_output=True, text=True, check=True).stdout
    imports, exports, dll, section = {}, set(), None, None
    for line in text.splitlines():
        if line.startswith("Export Table:"):
            section = "export"
        elif line.startswith("The Import Tables:"):
            section = "import"
        elif section == "import" and (m := re.match(r"\s+DLL Name: (\S+)", line)):
            dll = m.group(1).lower()
            imports.setdefault(dll, set())
        elif section == "import" and dll and (m := re.match(r"\s+\d+\s+(\S+)$", line)):
            imports[dll].add(m.group(1))
        elif section == "export" and (m := re.match(r"\s+\d+\s+0x[0-9a-f]+\s+(\S+)", line)):
            exports.add(m.group(1))
    return imports, exports


def main() -> int:
    package = Path(sys.argv[1])
    objdump = sys.argv[2] if len(sys.argv) > 2 else "llvm-objdump"
    modules = {p.name.lower(): p for p in list(package.glob("*.dll")) + list(package.glob("*.exe"))
               + list(package.glob("private/*.dll"))}
    parsed = {name: tables(objdump, path) for name, path in modules.items()}
    problems = 0
    for name, (imports, _) in sorted(parsed.items()):
        for dll, symbols in sorted(imports.items()):
            if dll not in parsed:
                continue  # a Windows system DLL
            missing = sorted(symbols - parsed[dll][1])
            for symbol in missing:
                print(f"MISSING {name} imports {symbol} from {dll}")
                problems += 1
    print("imports:", "FAILED" if problems else "ok", f"({len(parsed)} modules)")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
