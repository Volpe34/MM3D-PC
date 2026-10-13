#!/usr/bin/env python3
"""audit_package.py <package> <code.bin> [romfs.bin]: fail if game data is in the package.

- No game-file types (ROMs, extracted images, shader packs, savestates, saves,
  texture packs).
- No 32-byte run of code.bin (aligned 8-byte words, 4 consecutive matches) and
  no sampled 32-byte block of romfs.bin in any file.
"""
import os
import random
import sys
from pathlib import Path

import numpy as np

FORBIDDEN_SUFFIXES = {".3ds", ".cci", ".cia", ".cxi", ".bin", ".o3ps", ".oot3dsav", ".sav", ".bclim",
                      ".ctxb", ".gar", ".zar", ".cmb", ".csab", ".bcstm", ".bcsar"}
# Standard constants found in both the game and common libraries.
JPEG_ZIGZAG = bytes([0, 1, 8, 16, 9, 2, 3, 10, 17, 24, 32, 25, 18, 11, 4, 5, 12, 19, 26, 33, 40, 48, 41, 34,
                     27, 20, 13, 6, 7, 14, 21, 28, 35, 42, 49, 56, 57, 50, 43, 36, 29, 22, 15, 23, 30, 37,
                     44, 51, 58, 59, 52, 45, 38, 31, 39, 46, 53, 60, 61, 54, 47, 55, 62, 63])


def standard_constant(block: bytes) -> bool:
    if block in JPEG_ZIGZAG:
        return True
    halves = [int.from_bytes(block[i:i + 2], "little") for i in range(0, len(block), 2)]
    steps = {(halves[i + 1] - halves[i]) & 0xFFFF for i in range(len(halves) - 1)}
    return len(steps) == 1  # a counting table


FORBIDDEN_PARTS = {"savedata", "shader_corpus", "button_styles", "private", "game", "work"}


def words(data: bytes) -> np.ndarray:
    usable = len(data) // 8 * 8
    return np.frombuffer(data[:usable], dtype="<u8")


def main() -> int:
    package, code = Path(sys.argv[1]), Path(sys.argv[2]).read_bytes()
    romfs = Path(sys.argv[3]) if len(sys.argv) > 3 else None
    problems = []
    code_words = words(code)
    # Only high-entropy words (5+ distinct bytes): instructions and data, not
    # the small integers and padding any binary shares.
    distinct = np.unique(code_words)
    as_bytes = distinct.view(np.uint8).reshape(-1, 8)
    def meaningful(row):
        if len(set(row)) < 5 or all(32 <= b < 127 for b in row):
            return False  # padding, small integers, text
        steps = {(row[i + 1] - row[i]) & 0xFF for i in range(7)}
        return len(steps) > 2  # not a counting table (0 1 2 3..., 8D 00 8E 00...)
    varied = np.array([meaningful(row) for row in as_bytes.tolist()])
    code_set = distinct[varied]
    samples = []
    if romfs:
        size = romfs.stat().st_size
        with open(romfs, "rb") as source:
            for _ in range(400):
                source.seek(random.randrange(0, size - 64) // 8 * 8)
                block = source.read(32)
                if len(set(block)) > 8:
                    samples.append(block)
    for path in sorted(package.rglob("*")):
        if not path.is_file():
            continue
        relative = path.relative_to(package)
        if path.suffix.lower() in FORBIDDEN_SUFFIXES or FORBIDDEN_PARTS & set(relative.parts[:-1]):
            problems.append(f"forbidden file: {relative}")
            continue
        data = path.read_bytes()
        for shift in range(0, 8, 4):
            w = words(data[shift:])
            if len(w) < 4:
                continue
            hit = np.isin(w, code_set)
            run = hit[:-3] & hit[1:-2] & hit[2:-1] & hit[3:]
            # four different words (not one repeated constant)
            run &= (w[:-3] != w[1:-2]) & (w[1:-2] != w[2:-1]) & (w[2:-1] != w[3:]) & (w[:-3] != w[2:-1])
            found = None
            for index in np.flatnonzero(run)[:2000]:
                start = shift + int(index) * 8
                block = data[start:start + 32]
                if block in code and not standard_constant(block):  # the same 32 bytes, contiguous
                    found = start
                    break
            if found is not None:
                problems.append(f"code.bin bytes in {relative} at {found:#x}")
                break
        for block in samples:
            if block in data:
                problems.append(f"romfs bytes in {relative}")
                break
    for problem in problems:
        print("FAIL", problem)
    print("audit:", "FAILED" if problems else "clean", f"({package})")
    return 1 if problems else 0


if __name__ == "__main__":
    raise SystemExit(main())
