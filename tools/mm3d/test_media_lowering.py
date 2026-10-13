"""Differential test of TriAevum's C++ lowering for ARMv6 media instructions vs Unicorn.

Covers parallel add/subtract, SSAT16/USAT16, SXTAB16/UXTAB16 and the dual 16-bit
multiplies: every distinct such word found in MM3D's code plus random encodings of
each family. The emitted C++ is compiled into a harness with a minimal state
(registers, NZCV, preserved CPSR bits) and compared register-for-register, plus the
Q and GE flags, against Unicorn running the original A32 instruction.
"""

from __future__ import annotations

import random
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM
from unicorn.arm_const import (UC_ARM_REG_CPSR, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3,
                               UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7, UC_ARM_REG_R8,
                               UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_SP,
                               UC_ARM_REG_LR)

GPRS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
        UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_SP,
        UC_ARM_REG_LR]

NATIVE = Path(__file__).resolve().parents[1] / "oot3d/native_a32_runtime"
sys.path.insert(0, str(NATIVE))
sys.path.insert(0, str(NATIVE / "upstream/src"))
import whole_aot_cpp as W  # noqa: E402

HARNESS = r"""
#include <cstdint>
#include <cstdio>
struct Flags { bool n, z, c, v; bool N() const { return n; } bool Z() const { return z; }
               bool C() const { return c; } bool V() const { return v; } };
struct State {
    uint32_t R[15]; Flags Flags; uint32_t preserved;
    void SetPreservedCpsrBits(uint32_t m, uint32_t v) { preserved = (preserved & ~m) | (v & m); }
    void SetSaturationFlag(bool s) { if (s) preserved |= 1U << 27U; }
};
static inline uint32_t Oot3dAotRor(uint32_t v, uint32_t a) { a &= 31U; return a ? (v >> a) | (v << (32U - a)) : v; }
"""


def unicorn_run(raw: int, regs: list[int], cpsr: int) -> tuple[list[int], int]:
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    uc.mem_map(0x1000, 0x1000)
    uc.mem_write(0x1000, struct.pack("<I", raw))
    uc.reg_write(UC_ARM_REG_CPSR, cpsr)      # mode first: SP/LR are banked
    for i, v in enumerate(regs):
        uc.reg_write(GPRS[i], v)
    uc.emu_start(0x1000, 0x1004, count=1)
    return [uc.reg_read(GPRS[i]) for i in range(15)], uc.reg_read(UC_ARM_REG_CPSR)


def random_encoding(rng: random.Random, family: str) -> int:
    regs = [r for r in range(13)]
    rd, rn, rm, ra = (rng.choice(regs) for _ in range(4))
    cond = 0xE
    if family == "parallel":
        op1 = rng.choice([1, 2, 3, 5, 6, 7])
        op2 = rng.choice([0, 1, 2, 3, 4, 7])
        return (cond << 28) | (0b01100 << 23) | (op1 << 20) | (rn << 16) | (rd << 12) | (0xF << 8) | (op2 << 5) | 0x10 | rm
    if family == "sat16":
        signed = rng.random() < 0.5
        sat = rng.randrange(16)
        return (cond << 28) | ((0x6A if signed else 0x6E) << 20) | (sat << 16) | (rd << 12) | 0xF30 | rn
    if family == "xtab16":
        signed = rng.random() < 0.5
        rot = rng.randrange(4)
        return (cond << 28) | ((0x68 if signed else 0x6C) << 20) | (rn << 16) | (rd << 12) | (rot << 10) | 0x70 | rm
    # dual multiply
    sub = rng.randrange(2)
    x = rng.randrange(2)
    acc = ra if rng.random() < 0.6 else 15
    return (cond << 28) | (0x70 << 20) | (rd << 16) | (acc << 12) | (rm << 8) | (sub << 6) | (x << 5) | 0x10 | rn


def family_of(raw: int) -> str | None:
    if (raw & 0x0F800F10) == 0x06000F10 and ((raw >> 20) & 7) in {1, 2, 3, 5, 6, 7} and ((raw >> 5) & 7) in {0, 1, 2, 3, 4, 7}:
        return "parallel"
    if (raw & 0x0FF00FF0) in {0x06A00F30, 0x06E00F30}:
        return "sat16"
    if (raw & 0x0FF000F0) in {0x06800070, 0x06C00070} and (raw >> 16) & 0xF != 15:
        return "xtab16"
    if (raw & 0x0FF00090) == 0x07000010:
        return "dual"
    return None


def main() -> int:
    code = (Path.home() / "mm3d_pc/work/mm3d/code_thumbfree.bin").read_bytes()
    found = set()
    for off in range(0, 0x51d000, 4):
        raw = struct.unpack_from("<I", code, off)[0]
        if raw >> 28 == 0xE and family_of(raw):
            found.add(raw)
    rng = random.Random(77)
    words = sorted(found)
    for family in ("parallel", "sat16", "xtab16", "dual"):
        words += [random_encoding(rng, family) for _ in range(120)]
    words = list(dict.fromkeys(words))
    cases = []
    funcs = []
    for index, raw in enumerate(words):
        item = W.Instruction(pc=0x1000, raw=raw)
        lines = W._emit_non_control(item)
        funcs.append(f"static void t{index}(State& state) {{\n" + "\n".join(lines) + "\n}")
        for trial in range(16):
            regs = [rng.getrandbits(32) if trial % 3 else rng.choice([0, 1, 0x7FFF, 0x8000, 0xFFFF, 0x7FFF7FFF, 0x80008000,
                                                                      0xFFFFFFFF, 0x80000000, 0x00FF00FF, 0x7F7F7F7F])
                    for _ in range(15)]
            nzcv = rng.getrandbits(4)
            cpsr = (nzcv << 28) | 0x10
            out, out_cpsr = unicorn_run(raw, regs, cpsr)
            cases.append((index, regs, cpsr, out, out_cpsr))
    src = [HARNESS, *funcs, "typedef void (*Fn)(State&);", f"static Fn fns[] = {{{', '.join(f't{i}' for i in range(len(words)))}}};",
           "struct Case { int f; uint32_t in[15]; uint32_t cpsr; uint32_t out[15]; uint32_t ocpsr; };",
           "static const Case cases[] = {"]
    for f, regs, cpsr, out, ocpsr in cases:
        src.append("{%d,{%s},0x%XU,{%s},0x%XU}," % (f, ",".join(f"0x{v:X}U" for v in regs), cpsr,
                                                      ",".join(f"0x{v:X}U" for v in out), ocpsr))
    src.append("};")
    src.append(r"""
int main() {
    int failures = 0;
    for (const Case& c : cases) {
        State s{}; for (int i = 0; i < 15; ++i) s.R[i] = c.in[i];
        s.Flags = {(c.cpsr >> 31) & 1U ? true : false, (c.cpsr >> 30) & 1U ? true : false,
                   (c.cpsr >> 29) & 1U ? true : false, (c.cpsr >> 28) & 1U ? true : false};
        s.preserved = c.cpsr & 0x0FFFFFFFU;
        fns[c.f](s);
        bool ok = true;
        for (int i = 0; i < 15; ++i) ok = ok && s.R[i] == c.out[i];
        const uint32_t mask = (1U << 27U) | 0x000F0000U;   // Q and GE
        ok = ok && (s.preserved & mask) == (c.ocpsr & mask);
        if (!ok && failures++ < 30) {
            std::printf("FAIL fn %d:", c.f);
            for (int i = 0; i < 15; ++i) if (s.R[i] != c.out[i]) std::printf(" r%d %08X/%08X", i, s.R[i], c.out[i]);
            std::printf(" qge %08X/%08X\n", s.preserved & mask, c.ocpsr & mask);
        }
    }
    std::printf("%zu cases, %d failures\n", sizeof(cases) / sizeof(cases[0]), failures);
    return failures != 0;
}
""")
    with tempfile.TemporaryDirectory() as tmp:
        cpp = Path(tmp) / "t.cpp"
        exe = Path(tmp) / "t"
        cpp.write_text("\n".join(src))
        subprocess.run(["clang++", "-std=c++20", "-O1", "-o", str(exe), str(cpp)], check=True)
        result = subprocess.run([str(exe)], capture_output=True, text=True)
        print(f"{len(words)} encodings ({len(found)} from MM3D)")
        print(result.stdout.strip())
        if result.returncode:
            for index, raw in enumerate(words):
                if f"fn {index}:" in result.stdout:
                    print(f"  fn {index} = 0x{raw:08X}")
        return result.returncode


if __name__ == "__main__":
    raise SystemExit(main())
