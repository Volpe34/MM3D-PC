"""Differential test: every converted Thumb instruction vs its A32 replacement.

For each Thumb instruction of the MM3D inventory, run the original in Thumb
mode and the converted A32 sequence in ARM mode under Unicorn from identical
random states, then compare r0-r14, NZCV and a scratch memory window.
Branches are checked by destination (Thumb target -> converted address).
"""

from __future__ import annotations

import json
import random
import struct
import sys
from pathlib import Path

import capstone
from unicorn import Uc, UC_ARCH_ARM, UC_MODE_ARM, UcError, UC_HOOK_MEM_WRITE
from unicorn.arm_const import (UC_ARM_REG_CPSR, UC_ARM_REG_PC, UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2,
                               UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6, UC_ARM_REG_R7,
                               UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12,
                               UC_ARM_REG_SP, UC_ARM_REG_LR)

sys.path.insert(0, str(Path(__file__).resolve().parent))
import thumb_to_a32 as T  # noqa: E402

REGS = [UC_ARM_REG_R0, UC_ARM_REG_R1, UC_ARM_REG_R2, UC_ARM_REG_R3, UC_ARM_REG_R4, UC_ARM_REG_R5, UC_ARM_REG_R6,
        UC_ARM_REG_R7, UC_ARM_REG_R8, UC_ARM_REG_R9, UC_ARM_REG_R10, UC_ARM_REG_R11, UC_ARM_REG_R12, UC_ARM_REG_SP,
        UC_ARM_REG_LR]
SCRATCH, SCRATCH_SIZE = 0x00C00000, 0x01200000


def machine(image: bytes) -> Uc:
    uc = Uc(UC_ARCH_ARM, UC_MODE_ARM)
    size = (len(image) + 0xFFFF) & ~0xFFFF
    uc.mem_map(T.BASE, size)
    uc.mem_write(T.BASE, image)
    uc.mem_map(SCRATCH, SCRATCH_SIZE)
    uc.writes = []
    uc.hook_add(UC_HOOK_MEM_WRITE, lambda u, access, addr, size, value, data: u.writes.append((addr, size, value & ((1 << (8 * size)) - 1))))
    return uc


def setup(uc: Uc, regs: list[int], nzcv: int, thumb: bool, mem: bytes) -> None:
    uc.reg_write(UC_ARM_REG_CPSR, (nzcv << 28) | 0x10 | (0x20 if thumb else 0))   # mode first: SP/LR are banked
    for r, v in zip(REGS, regs):
        uc.reg_write(r, v)
    uc.mem_write(SCRATCH, mem)
    uc.writes = []


def state(uc: Uc) -> tuple:
    return (tuple(uc.reg_read(r) for r in REGS), uc.reg_read(UC_ARM_REG_CPSR) >> 28, tuple(uc.writes))


def main() -> int:
    root = Path(__import__("os").environ.get("MM3D_WORK", Path.home() / "mm3d_pc/work/mm3d"))
    image = (root / "code_thumbfree.bin").read_bytes()
    original = (Path(__import__("os").environ.get("MM3D_INPUT", Path.home() / "mm3d_pc/input/mm3d_usa"))
                / "code.bin").read_bytes()
    tmap = json.loads((root / "thumb_map.json").read_text())
    vmap = {int(k, 16): v for k, v in tmap["instruction_map"].items()}
    md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_THUMB)
    # Thumb runs from the original bytes (the patched image only differs in A32 BLX sites)
    full = bytearray(image)
    full[:len(original)] = original
    uc_t, uc_a = machine(bytes(full)), machine(image)
    rng = random.Random(1234)
    # words that look like code addresses (Thumb or ARM), so returns land in mapped code
    unit = b"".join(struct.pack("<I", (0x00100000 + rng.randrange(0, 0x4F0000)) & ~2) for _ in range(0x40000 // 4))
    mem = unit * (SCRATCH_SIZE // len(unit))
    pcs = sorted(vmap)
    failures, checked, branches = [], 0, 0
    order = {pc: i for i, pc in enumerate(pcs)}
    for pc in pcs:
        v = vmap[pc]
        insn = next(md.disasm(original[pc - T.BASE:pc - T.BASE + 4], pc, 1))
        nxt_v = vmap[pcs[order[pc] + 1]] if order[pc] + 1 < len(pcs) else None
        a32_len = (nxt_v - v) if nxt_v is not None else 4
        is_branch = insn.mnemonic.startswith("b") and insn.mnemonic not in ("bic", "bics", "bkpt") or insn.mnemonic in ("pop",) and "pc" in insn.op_str
        if insn.mnemonic in ("svc", "bkpt", "udf"):
            continue
        for trial in range(6):
            memory_like = any(k in insn.mnemonic for k in ("ldr", "str", "ldm", "stm", "push", "pop"))
            if memory_like:
                regs = [(SCRATCH + rng.randrange(0, 0x200000)) & ~3 for _ in REGS]
            elif is_branch:
                regs = [(0x00100000 + rng.randrange(0, 0x4F0000)) | rng.getrandbits(1) for _ in REGS]
                regs[13] = (SCRATCH + rng.randrange(0, 0x200000)) & ~3
            else:
                regs = [rng.getrandbits(32) if trial % 2 else rng.choice([0, 1, 31, 32, 33, 0x7FFFFFFF, 0x80000000, 0xFFFFFFFF, rng.getrandbits(8)]) for _ in REGS]
            nzcv = rng.getrandbits(4)
            setup(uc_t, regs, nzcv, True, mem)
            setup(uc_a, regs, nzcv, False, mem)
            try:
                uc_t.emu_start(pc | 1, 0, count=1)
                t_pc = uc_t.reg_read(UC_ARM_REG_PC)
                uc_a.emu_start(v, 0, count=1 if is_branch else max(1, a32_len // 4))
                a_pc = uc_a.reg_read(UC_ARM_REG_PC)
            except UcError as error:
                failures.append((pc, insn.mnemonic, insn.op_str, f"emulation error {error}"))
                break
            st_t, st_a = state(uc_t), state(uc_a)
            if is_branch:
                branches += 1
                expect = vmap.get(t_pc & ~1, t_pc & ~1)
                if t_pc & ~1 == pc + insn.size:          # not taken
                    expect = vmap.get(pc + insn.size)
                if a_pc != expect and not (insn.mnemonic in ("bx", "blx", "pop") or insn.mnemonic == "bl" and t_pc & 1 == 0):
                    failures.append((pc, insn.mnemonic, insn.op_str, f"branch thumb->{t_pc:08X} a32->{a_pc:08X} expect {expect}"))
                    break
                # link register: Thumb sets LR = next|1, A32 sets V-relative return; only compare the rest
                if st_t[0][:14] != st_a[0][:14] or st_t[1] != st_a[1] or st_t[2] != st_a[2]:
                    failures.append((pc, insn.mnemonic, insn.op_str, "state differs after branch"))
                    break
                continue
            checked += 1
            if st_t[0] != st_a[0] or st_t[1] != st_a[1] or st_t[2] != st_a[2]:
                diff = [f"r{i}:{a:08X}/{b:08X}" for i, (a, b) in enumerate(zip(st_t[0], st_a[0])) if a != b]
                flags = f"nzcv {st_t[1]:X}/{st_a[1]:X}" if st_t[1] != st_a[1] else ""
                memd = "memory" if st_t[2] != st_a[2] else ""
                failures.append((pc, insn.mnemonic, insn.op_str, " ".join(diff + [flags, memd]).strip()))
                break
    print(f"checked {checked} executions, {branches} branch executions over {len(pcs)} instructions; failures {len(failures)}")
    for f in failures[:40]:
        print(f"  0x{f[0]:08X} {f[1]} {f[2]}: {f[3]}")
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
