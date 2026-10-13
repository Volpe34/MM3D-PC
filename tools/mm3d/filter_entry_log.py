"""Turn an R3DS_ENTRY_LOG (targets of indirect transfers) into dynamic code entries.

Return sites (targets right after a call) are dropped: the whole-AOT program
already resumes there. What remains are function-pointer, vtable and computed
jump targets that static analysis may not see.
"""
import struct
import sys
from pathlib import Path

BASE = 0x00100000


def main() -> int:
    code = Path(sys.argv[1]).read_bytes()
    text_end = BASE + int(sys.argv[2], 0)
    out = set()
    for line in Path(sys.argv[3]).read_text().split():
        target = int(line, 16)
        address = target & ~1
        if not BASE + 4 <= address < text_end:
            continue
        if target & 1:   # Thumb: preceded by a BL/BLX pair or BLX register?
            h2 = struct.unpack_from("<H", code, address - 2 - BASE)[0]
            h1 = struct.unpack_from("<H", code, address - 4 - BASE)[0]
            if (h1 >> 11) == 0b11110 and (h2 >> 11) in (0b11111, 0b11101):
                continue
            if (h2 & 0xFF87) == 0x4780:   # BLX Rm
                continue
        else:
            word = struct.unpack_from("<I", code, address - 4 - BASE)[0]
            if (word & 0x0F000000) == 0x0B000000 or word >> 25 == 0b1111101:   # BL / BLX imm
                continue
            if (word & 0x0FFFFFF0) == 0x012FFF30:   # BLX Rm
                continue
        out.add(target)
    Path(sys.argv[4]).write_text("".join(f"{t:08x}\n" for t in sorted(out)))
    print(f"{len(out)} dynamic entries ({sum(t & 1 for t in out)} Thumb)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
