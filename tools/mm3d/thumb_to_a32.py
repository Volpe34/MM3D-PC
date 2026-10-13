"""Offline Thumb-1 -> A32 conversion for MM3D's Thumb functions.

TriAevum's whole-AOT front end and C++ backend handle A32 only. MM3D contains
150 small Thumb functions (C runtime / SDK helpers, ~6.4k instructions). Each
one is re-encoded here as equivalent A32 code in a synthetic code region that
no game data occupies, so the unchanged A32 pipeline translates it:

* every Thumb instruction becomes one or more A32 instructions with identical
  architectural effect (Thumb-1 ALU forms that set flags become S forms);
* PC-relative values (literal loads, ADR, ``add rX, pc``) become constants built
  with MOV/ORR or added with ADD immediates, so the synthetic code never reads
  its own (unmapped) bytes; literal pools live in read-only .text;
* B/Bcc/BL to Thumb code are retargeted to the converted copies, BLX to A32
  code becomes BL, BX/BLX register and POP {pc} are unchanged (interworking is
  resolved by dispatcher aliases ``T|1 -> V``);
* A32 ``BLX imm`` call sites that target a converted Thumb function are patched
  to ``BL V`` in the image handed to the translator (game memory is unchanged).

ARMv6K has no Thumb-2 beyond the BL/BLX pair, so the instruction set is closed.
Anything unrecognised aborts the conversion instead of guessing.
"""

from __future__ import annotations

import argparse
import json
import struct
from dataclasses import dataclass, field
from pathlib import Path

BASE = 0x00100000
# Above .bss (ends 0x7CF000; 0x8F9000 with Project Restoration: MM3D_SYNTH_BASE),
# below the heap.
SYNTH_BASE = int(__import__("os").environ.get("MM3D_SYNTH_BASE", "0x00800000"), 0)
AL = 0xE


class ConversionError(ValueError):
    pass


# --------------------------------------------------------------------- A32 encoders
def _rot_imm(value: int) -> int | None:
    """Encode value as an A32 rotated 8-bit immediate (rot<<8 | imm8)."""
    value &= 0xFFFFFFFF
    for rot in range(16):
        imm = ((value << (2 * rot)) | (value >> (32 - 2 * rot))) & 0xFFFFFFFF if rot else value
        if imm <= 0xFF:
            return (rot << 8) | imm
    return None


def _chunks(value: int) -> list[int]:
    """Split value into rotated-immediate chunks (each an encodable imm)."""
    value &= 0xFFFFFFFF
    parts = []
    while value:
        lowest = (value & -value).bit_length() - 1
        bit = min(lowest & ~1, 24)          # even start, window never wraps
        piece = value & (0xFF << bit)
        parts.append(piece)
        value &= ~piece & 0xFFFFFFFF
    return parts


def dp(op: int, s: int, rn: int, rd: int, op2: int, imm: bool, cond: int = AL) -> int:
    return (cond << 28) | (int(imm) << 25) | (op << 21) | (s << 20) | (rn << 16) | (rd << 12) | op2


def dp_imm(op: int, s: int, rn: int, rd: int, value: int) -> int:
    enc = _rot_imm(value)
    if enc is None:
        raise ConversionError(f"immediate 0x{value:X} not encodable")
    return dp(op, s, rn, rd, enc, True)


def shift_reg_imm(rm: int, kind: int, amount: int) -> int:
    return ((amount & 31) << 7) | (kind << 5) | rm


def shift_reg_reg(rm: int, kind: int, rs: int) -> int:
    return (rs << 8) | (kind << 5) | 0x10 | rm


AND, EOR, SUB, RSB, ADD, ADC, SBC, RSC, TST, TEQ, CMP, CMN, ORR, MOV, BIC, MVN = range(16)


def load_constant(rd: int, value: int) -> list[int]:
    """rd = value without touching flags (MOV + ORRs, or MVN)."""
    value &= 0xFFFFFFFF
    if _rot_imm(value) is not None:
        return [dp_imm(MOV, 0, 0, rd, value)]
    if _rot_imm(~value & 0xFFFFFFFF) is not None:
        return [dp_imm(MVN, 0, 0, rd, ~value & 0xFFFFFFFF)]
    parts = _chunks(value)
    out = [dp_imm(MOV, 0, 0, rd, parts[0])]
    out += [dp_imm(ORR, 0, rd, rd, p) for p in parts[1:]]
    return out


def add_constant(rd: int, rn: int, value: int) -> list[int]:
    """rd = rn + value without touching flags."""
    value &= 0xFFFFFFFF
    if value == 0:
        return [dp(MOV, 0, 0, rd, rn, False)]
    parts = _chunks(value)
    out = [dp_imm(ADD, 0, rn, rd, parts[0])]
    out += [dp_imm(ADD, 0, rd, rd, p) for p in parts[1:]]
    return out


def mem_imm(load: bool, byte: bool, rn: int, rd: int, offset: int) -> int:
    u = 1 if offset >= 0 else 0
    return (AL << 28) | (0b01 << 26) | (1 << 24) | (u << 23) | (int(byte) << 22) | (int(load) << 20) | (rn << 16) | (rd << 12) | (abs(offset) & 0xFFF)


def mem_reg(load: bool, byte: bool, rn: int, rd: int, rm: int) -> int:
    return (AL << 28) | (0b011 << 25) | (1 << 24) | (1 << 23) | (int(byte) << 22) | (int(load) << 20) | (rn << 16) | (rd << 12) | rm


def mem_half_imm(load: bool, sh: int, rn: int, rd: int, offset: int) -> int:
    # sh: 1 = H (unsigned half), 2 = SB, 3 = SH
    return (AL << 28) | (1 << 24) | (1 << 23) | (1 << 22) | (int(load) << 20) | (rn << 16) | (rd << 12) | (((offset >> 4) & 0xF) << 8) | (1 << 7) | (sh << 5) | (1 << 4) | (offset & 0xF)


def mem_half_reg(load: bool, sh: int, rn: int, rd: int, rm: int) -> int:
    return (AL << 28) | (1 << 24) | (1 << 23) | (int(load) << 20) | (rn << 16) | (rd << 12) | (1 << 7) | (sh << 5) | (1 << 4) | rm


def block(load: bool, rn: int, regs: int, writeback: bool, before: bool, increment: bool) -> int:
    return (AL << 28) | (0b100 << 25) | (int(before) << 24) | (int(increment) << 23) | (int(writeback) << 21) | (int(load) << 20) | (rn << 16) | regs


def branch(cond: int, link: bool, pc: int, target: int) -> int:
    off = (target - (pc + 8)) >> 2
    if not -(1 << 23) <= off < (1 << 23):
        raise ConversionError(f"branch 0x{pc:08X}->0x{target:08X} out of range")
    return (cond << 28) | (0b101 << 25) | (int(link) << 24) | (off & 0xFFFFFF)


# Odd code addresses loaded as literals by converted Thumb code (function
# pointers into Thumb code); filled by convert_insn, consumed by discovery.
THUMB_LITERAL_POINTERS: set[int] = set()


# --------------------------------------------------------------------- Thumb decode
@dataclass
class Insn:
    pc: int          # original Thumb address
    size: int
    half: int
    second: int = 0
    out: list = field(default_factory=list)   # list of (word | ("branch", cond, link, target_thumb_or_arm, is_thumb))


def convert_insn(code: bytes, pc: int, h: int, h2: int) -> tuple[int, list]:
    """Return (size, items). Items are A32 words or deferred branch tuples."""
    def lit(addr: int) -> int:
        return struct.unpack_from("<I", code, addr - BASE)[0]

    top5 = h >> 11
    # F19: BL / BLX prefix + suffix
    if top5 == 0b11110:
        if (h2 >> 11) not in (0b11111, 0b11101):
            raise ConversionError(f"unpaired BL prefix at 0x{pc:08X}")
        off = ((h & 0x7FF) << 12) | ((h2 & 0x7FF) << 1)
        if off & (1 << 22):
            off -= 1 << 23
        target = pc + 4 + off
        if (h2 >> 11) == 0b11101:     # BLX: to A32, target word-aligned
            return 4, [("branch", AL, True, target & ~3, False)]
        return 4, [("branch", AL, True, target, True)]
    if top5 in (0b11111, 0b11101):
        raise ConversionError(f"BL suffix without prefix at 0x{pc:08X}")

    # F1: shift by immediate
    if (h >> 13) == 0 and ((h >> 11) & 3) != 3:
        op, imm, rs, rd = (h >> 11) & 3, (h >> 6) & 31, (h >> 3) & 7, h & 7
        return 2, [dp(MOV, 1, 0, rd, shift_reg_imm(rs, op, imm), False)]
    # F2: add/sub register / imm3
    if (h >> 11) == 0b00011:
        i, sub, rn_imm, rs, rd = (h >> 10) & 1, (h >> 9) & 1, (h >> 6) & 7, (h >> 3) & 7, h & 7
        op = SUB if sub else ADD
        if i:
            return 2, [dp(op, 1, rs, rd, rn_imm, True)]
        return 2, [dp(op, 1, rs, rd, rn_imm, False)]
    # F3: mov/cmp/add/sub imm8
    if (h >> 13) == 0b001:
        op, rd, imm = (h >> 11) & 3, (h >> 8) & 7, h & 0xFF
        a32op = (MOV, CMP, ADD, SUB)[op]
        rn = 0 if op == 0 else rd
        rdd = 0 if op == 1 else rd
        return 2, [dp(a32op, 1, rn, rdd, imm, True)]
    # F4: ALU
    if (h >> 10) == 0b010000:
        op, rs, rd = (h >> 6) & 15, (h >> 3) & 7, h & 7
        if op == 0x0: return 2, [dp(AND, 1, rd, rd, rs, False)]
        if op == 0x1: return 2, [dp(EOR, 1, rd, rd, rs, False)]
        if op == 0x2: return 2, [dp(MOV, 1, 0, rd, shift_reg_reg(rd, 0, rs), False)]
        if op == 0x3: return 2, [dp(MOV, 1, 0, rd, shift_reg_reg(rd, 1, rs), False)]
        if op == 0x4: return 2, [dp(MOV, 1, 0, rd, shift_reg_reg(rd, 2, rs), False)]
        if op == 0x5: return 2, [dp(ADC, 1, rd, rd, rs, False)]
        if op == 0x6: return 2, [dp(SBC, 1, rd, rd, rs, False)]
        if op == 0x7: return 2, [dp(MOV, 1, 0, rd, shift_reg_reg(rd, 3, rs), False)]
        if op == 0x8: return 2, [dp(TST, 1, rd, 0, rs, False)]
        if op == 0x9: return 2, [dp(RSB, 1, rs, rd, 0, True)]
        if op == 0xA: return 2, [dp(CMP, 1, rd, 0, rs, False)]
        if op == 0xB: return 2, [dp(CMN, 1, rd, 0, rs, False)]
        if op == 0xC: return 2, [dp(ORR, 1, rd, rd, rs, False)]
        if op == 0xD:   # MULS rd, rs, rd  (A32 MUL: Rd, Rm, Rs at bits 19..16, 3..0, 11..8)
            return 2, [(AL << 28) | (1 << 20) | (rd << 16) | (rd << 8) | 0x90 | rs]
        if op == 0xE: return 2, [dp(BIC, 1, rd, rd, rs, False)]
        if op == 0xF: return 2, [dp(MVN, 1, 0, rd, rs, False)]
    # F5: hi-register ops / BX / BLX
    if (h >> 10) == 0b010001:
        op = (h >> 8) & 3
        rd = (h & 7) | ((h >> 4) & 8)
        rs = (h >> 3) & 15
        if op == 3:
            link = (h >> 7) & 1
            return 2, [(AL << 28) | 0x012FFF10 | (link << 5) | rs]
        if rd == 15:
            raise ConversionError(f"computed jump (hi-reg op to pc) at 0x{pc:08X}")
        if rs == 15:
            pcv = pc + 4
            if op == 0: return 2, add_constant(rd, rd, pcv)
            if op == 2: return 2, load_constant(rd, pcv)
            raise ConversionError(f"cmp with pc at 0x{pc:08X}")
        if op == 0: return 2, [dp(ADD, 0, rd, rd, rs, False)]
        if op == 1: return 2, [dp(CMP, 1, rd, 0, rs, False)]
        if op == 2: return 2, [dp(MOV, 0, 0, rd, rs, False)]
    # F6: PC-relative load (literal in read-only .text)
    if (h >> 11) == 0b01001:
        rd, imm = (h >> 8) & 7, (h & 0xFF) * 4
        addr = ((pc + 4) & ~3) + imm
        value = lit(addr)
        if value & 1 and BASE <= value < BASE + len(code):
            THUMB_LITERAL_POINTERS.add(value & ~1)
        return 2, load_constant(rd, value)
    # F7: load/store register offset
    if (h >> 12) == 0b0101 and not (h >> 9) & 1:
        l, b, ro, rb, rd = (h >> 11) & 1, (h >> 10) & 1, (h >> 6) & 7, (h >> 3) & 7, h & 7
        return 2, [mem_reg(bool(l), bool(b), rb, rd, ro)]
    # F8: sign-extended / halfword register offset
    if (h >> 12) == 0b0101 and (h >> 9) & 1:
        hs, ro, rb, rd = (h >> 10) & 3, (h >> 6) & 7, (h >> 3) & 7, h & 7
        if hs == 0: return 2, [mem_half_reg(False, 1, rb, rd, ro)]   # STRH
        if hs == 1: return 2, [mem_half_reg(True, 2, rb, rd, ro)]    # LDRSB
        if hs == 2: return 2, [mem_half_reg(True, 1, rb, rd, ro)]    # LDRH
        return 2, [mem_half_reg(True, 3, rb, rd, ro)]                # LDRSH
    # F9: load/store immediate offset
    if (h >> 13) == 0b011:
        b, l, imm, rb, rd = (h >> 12) & 1, (h >> 11) & 1, (h >> 6) & 31, (h >> 3) & 7, h & 7
        return 2, [mem_imm(bool(l), bool(b), rb, rd, imm if b else imm * 4)]
    # F10: halfword immediate
    if (h >> 12) == 0b1000:
        l, imm, rb, rd = (h >> 11) & 1, ((h >> 6) & 31) * 2, (h >> 3) & 7, h & 7
        return 2, [mem_half_imm(bool(l), 1, rb, rd, imm)]
    # F11: SP-relative load/store
    if (h >> 12) == 0b1001:
        l, rd, imm = (h >> 11) & 1, (h >> 8) & 7, (h & 0xFF) * 4
        return 2, [mem_imm(bool(l), False, 13, rd, imm)]
    # F12: ADR / add rd, sp, #imm
    if (h >> 12) == 0b1010:
        sp, rd, imm = (h >> 11) & 1, (h >> 8) & 7, (h & 0xFF) * 4
        if sp:
            return 2, [dp_imm(ADD, 0, 13, rd, imm)] if imm else [dp(MOV, 0, 0, rd, 13, False)]
        return 2, load_constant(rd, ((pc + 4) & ~3) + imm)
    # F13: adjust SP
    if (h >> 8) == 0b10110000:
        imm = (h & 0x7F) * 4
        return 2, [dp_imm(SUB if (h >> 7) & 1 else ADD, 0, 13, 13, imm)]
    # ARMv6 extend: SXTH/SXTB/UXTH/UXTB
    if (h >> 8) == 0b10110010:
        op, rm, rd = (h >> 6) & 3, (h >> 3) & 7, h & 7
        base = {0: 0x06BF0070, 1: 0x06AF0070, 2: 0x06FF0070, 3: 0x06EF0070}[op]
        return 2, [(AL << 28) | base | (rd << 12) | rm]
    # ARMv6 REV/REV16/REVSH
    if (h >> 8) == 0b10111010:
        op, rm, rd = (h >> 6) & 3, (h >> 3) & 7, h & 7
        base = {0: 0x06BF0F30, 1: 0x06BF0FB0, 3: 0x06FF0FB0}.get(op)
        if base is None:
            raise ConversionError(f"undefined REV form at 0x{pc:08X}")
        return 2, [(AL << 28) | base | (rd << 12) | rm]
    # F14: push / pop
    if (h >> 12) == 0b1011 and ((h >> 9) & 3) == 0b10:
        l, r, rlist = (h >> 11) & 1, (h >> 8) & 1, h & 0xFF
        if l:
            return 2, [block(True, 13, rlist | (r << 15), True, False, True)]    # LDMIA sp!
        return 2, [block(False, 13, rlist | (r << 14), True, True, False)]      # STMDB sp!
    # BKPT
    if (h >> 8) == 0b10111110:
        imm = h & 0xFF
        return 2, [(AL << 28) | 0x01200070 | ((imm >> 4) << 8) | (imm & 0xF)]
    # F15: LDMIA / STMIA
    if (h >> 12) == 0b1100:
        l, rb, rlist = (h >> 11) & 1, (h >> 8) & 7, h & 0xFF
        wb = not (l and (rlist >> rb) & 1)
        return 2, [block(bool(l), rb, rlist, wb, False, True)]
    # F16/F17: conditional branch / SWI
    if (h >> 12) == 0b1101:
        cond = (h >> 8) & 15
        if cond == 0xF:
            return 2, [(AL << 28) | 0x0F000000 | (h & 0xFF)]
        if cond == 0xE:
            raise ConversionError(f"undefined Thumb instruction at 0x{pc:08X}")
        off = h & 0xFF
        if off & 0x80:
            off -= 0x100
        return 2, [("branch", cond, False, pc + 4 + off * 2, True)]
    # F18: unconditional branch
    if (h >> 11) == 0b11100:
        off = h & 0x7FF
        if off & 0x400:
            off -= 0x800
        return 2, [("branch", AL, False, pc + 4 + off * 2, True)]
    raise ConversionError(f"unsupported Thumb instruction 0x{h:04X} at 0x{pc:08X}")


# --------------------------------------------------------------------- discovery
def discover_thumb_function(code: bytes, entry: int, known: set[int]) -> tuple[str, set[int]]:
    """Recursive descent over Thumb-1 from entry; returns (ranges, new BL targets)."""
    seen: set[int] = set()
    calls: set[int] = set()
    work = [entry]
    while work:
        pc = work.pop()
        while pc not in seen and pc not in known:
            if not BASE <= pc < BASE + 0x51C000:   # .text only
                raise ConversionError(f"Thumb flow leaves .text at 0x{pc:08X}")
            h = struct.unpack_from("<H", code, pc - BASE)[0]
            h2 = struct.unpack_from("<H", code, pc + 2 - BASE)[0]
            size, items = convert_insn(code, pc, h, h2)
            seen.add(pc)
            if size == 4:
                seen.add(pc + 2)
            stop = False
            for item in items:
                if isinstance(item, tuple) and item[0] == "branch":
                    _, cond, link, target, to_thumb = item
                    if link:
                        if to_thumb:
                            calls.add(target)
                    elif cond == AL:
                        work.append(target)
                        stop = True
                    else:
                        work.append(target)
            top = h >> 8
            if (h >> 7) == 0b010001110 or (h >> 7) == 0b010001111:      # BX/BLX reg
                stop = (h >> 7) == 0b010001110
            if top == 0xBD:                                             # POP {.., pc}
                stop = True
            if stop:
                break
            pc += size
    starts = sorted(a for a in seen if a - 2 not in seen or struct.unpack_from("<H", code, a - 2 - BASE)[0] >> 11 != 0b11110)
    insns = sorted(seen)
    ranges, run_start, prev = [], None, None
    for a in insns:
        if run_start is None:
            run_start = prev = a
            continue
        if a != prev + 2:
            ranges.append((run_start, prev + 2))
            run_start = a
        prev = a
    if run_start is not None:
        ranges.append((run_start, prev + 2))
    # a 32-bit BL occupies two halfwords; ranges end on whole instructions
    return ";".join(f"{a:x}-{b:x}" for a, b in ranges), calls


def interworking_veneer_targets(code: bytes, text_size: int) -> dict[int, int]:
    """ADD Rx, PC, #1 ; BX Rx  ->  Thumb code at veneer + 8 (veneer site -> Thumb target)."""
    found = {}
    for off in range(0, text_size - 4, 4):
        a, b = struct.unpack_from("<II", code, off)
        if (a & 0xFFFF0FFF) == 0xE28F0001 and (b & 0xFFFFFFF0) == 0xE12FFF10 and ((a >> 12) & 15) == (b & 15):
            found[BASE + off] = BASE + off + 8
    return found


# --------------------------------------------------------------------- inline-argument helpers
def inline_arg_helpers(code: bytes, text_size: int) -> dict[int, int]:
    """Functions starting with LDR Rx, [LR], #4: they read an inline word after the call."""
    found = {}
    for off in range(0, text_size, 4):
        w = struct.unpack_from("<I", code, off)[0]
        if (w & 0xFFFF0FFF) == 0xE49E0004:
            found[BASE + off] = (w >> 12) & 15
    return found


# --------------------------------------------------------------------- driver
def convert(code: bytes, thumb_functions: list[dict]) -> dict:
    """Return the synthetic segment and the address maps."""
    insns: list[tuple[int, int, list]] = []          # (thumb_pc, size, items)
    for function in thumb_functions:
        for rg in function["ranges"].split(";"):
            start, end = (int(x, 16) for x in rg.split("-"))
            pc = start
            while pc < end:
                h = struct.unpack_from("<H", code, pc - BASE)[0]
                h2 = struct.unpack_from("<H", code, pc + 2 - BASE)[0] if pc + 2 < BASE + len(code) else 0
                size, items = convert_insn(code, pc, h, h2)
                insns.append((pc, size, items))
                pc += size
    insns.sort()
    seen = set()
    unique = []
    for item in insns:                      # shared ranges between functions
        if item[0] not in seen:
            seen.add(item[0])
            unique.append(item)
    # Layout: a Thumb fallthrough continues at the next converted instruction
    # only if it is contiguous; otherwise an explicit branch keeps the flow.
    vmap: dict[int, int] = {}
    layout: list[tuple[int, int, list]] = []
    v = SYNTH_BASE
    for index, (pc, size, items) in enumerate(unique):
        vmap[pc] = v
        words = list(items)
        nxt = unique[index + 1][0] if index + 1 < len(unique) else None
        if nxt != pc + size:
            words.append(("fallthrough", pc + size))
        layout.append((pc, v, words))
        v += 4 * len(words)
    out = bytearray()
    for pc, v, words in layout:
        for k, w in enumerate(words):
            at = v + 4 * k
            if isinstance(w, int):
                out += struct.pack("<I", w)
                continue
            if w[0] == "fallthrough":
                target = w[1]
                if target not in vmap:
                    # Falls out of the converted code: only after a terminal or a
                    # call that never returns. Unreachable; a self-branch keeps
                    # the CFG closed and lowerable.
                    out += struct.pack("<I", branch(AL, False, at, at))
                    continue
                out += struct.pack("<I", branch(AL, False, at, vmap[target]))
                continue
            _, cond, link, target, to_thumb = w
            if to_thumb:
                if target not in vmap:
                    raise ConversionError(f"Thumb branch 0x{pc:08X} -> 0x{target:08X} leaves converted code")
                target = vmap[target]
            out += struct.pack("<I", branch(cond, link, at, target))
    entries = {f["entry"]: vmap[f["entry"]] for f in thumb_functions}
    return {"segment": bytes(out), "vmap": vmap, "entries": entries}


def patch_arm_blx(code: bytearray, text_size: int, entries: dict[int, int], sites: set[int]) -> list[dict]:
    """Retarget A32 BLX-imm calls into converted Thumb functions (BLX -> BL V)."""
    patched = []
    for site in sorted(sites):
        off = site - BASE
        w = struct.unpack_from("<I", code, off)[0]
        if w >> 25 != 0b1111101:
            continue
        imm = w & 0xFFFFFF
        if imm & 0x800000:
            imm -= 1 << 24
        target = site + 8 + (imm << 2) + ((w >> 24) & 1) * 2
        if target not in entries:
            raise ConversionError(f"BLX at 0x{site:08X} targets unconverted Thumb 0x{target:08X}")
        struct.pack_into("<I", code, off, branch(AL, True, site, entries[target]))
        patched.append({"site": site, "thumb_target": target, "a32_target": entries[target]})
    return patched


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--code", type=Path, required=True)
    ap.add_argument("--exheader", type=Path, required=True)
    ap.add_argument("--thumb-inventory", type=Path, required=True)
    ap.add_argument("--call-sites", type=Path, required=True,
                    help="whole-AOT program whose unresolved direct calls are the decoded A32 BLX sites")
    ap.add_argument("--extra-thumb-entries", type=Path,
                    help="file of runtime-discovered Thumb entry addresses (bit 0 set), one per line")
    ap.add_argument("--output-image", type=Path, required=True)
    ap.add_argument("--output-map", type=Path, required=True)
    args = ap.parse_args()
    code = bytearray(args.code.read_bytes())
    exh = args.exheader.read_bytes()
    text_size = struct.unpack_from("<I", exh, 0x18)[0]
    thumb = json.loads(args.thumb_inventory.read_text())["thumb_functions"]
    program = json.loads(args.call_sites.read_text())
    call_sites = {(int(c["site"]), int(c["target"])) for f in program["functions"]
                  for c in f.get("direct_calls", []) if c.get("target") is not None}
    # A32 BLX imm sites (decoded code) and their Thumb targets
    blx_sites = {}
    for site, _ in call_sites:
        if BASE <= site < BASE + text_size:
            w = struct.unpack_from("<I", code, site - BASE)[0]
            if w >> 25 == 0b1111101:
                imm = w & 0xFFFFFF
                imm = imm - (1 << 24) if imm & 0x800000 else imm
                blx_sites[site] = site + 8 + (imm << 2) + ((w >> 24) & 1) * 2
    known_entries = {int(f["entry"]) for f in thumb}
    known_insns = set()
    for f in thumb:
        for rg in f["ranges"].split(";"):
            a, b = (int(x, 16) for x in rg.split("-"))
            known_insns.update(range(a, b, 2))
    veneers = interworking_veneer_targets(bytes(code), text_size)
    # A32 PC-relative literal loads in decoded code that load odd .text values
    arm_literal_thumb = set()
    original_bytes = bytes(code)
    for block in program.get("blocks", []):
        pc = int(block["pc"])
        while pc < int(block["end_pc"]):
            if BASE <= pc < BASE + text_size:
                w = struct.unpack_from("<I", original_bytes, pc - BASE)[0]
                if (w & 0x0F7F0000) == 0x051F0000:
                    imm = w & 0xFFF
                    addr = pc + 8 + (imm if w & (1 << 23) else -imm)
                    if BASE <= addr < BASE + len(original_bytes) - 4:
                        value = struct.unpack_from("<I", original_bytes, addr - BASE)[0]
                        if value & 1 and BASE <= value < BASE + text_size:
                            arm_literal_thumb.add(value & ~1)
            pc += 4
    runtime_thumb = set()
    if args.extra_thumb_entries is not None:
        for line in args.extra_thumb_entries.read_text().split():
            value = int(line, 16)
            if value & 1:
                runtime_thumb.add(value & ~1)
    # Literal-derived candidates must not land inside decoded A32 code.
    decoded_a32 = set()
    for block in program.get("blocks", []):
        decoded_a32.update(range(int(block["pc"]), int(block["end_pc"]), 2))
    arm_literal_thumb -= decoded_a32
    pending = sorted((set(blx_sites.values()) | set(veneers.values()) | runtime_thumb
                      | arm_literal_thumb) - known_entries)
    # veneer targets inside an already-known Thumb body become extra entries of it
    for target in sorted((set(veneers.values()) | runtime_thumb) - known_entries):
        if target in known_insns:
            owner = next(f for f in thumb if any(int(r.split("-")[0], 16) <= target < int(r.split("-")[1], 16)
                                                  for r in f["ranges"].split(";")))
            thumb.append({"entry": target, "name": f"thumb_{target:08X}", "ranges": owner["ranges"]})
            known_entries.add(target)
            pending.remove(target)
    discovered = []
    tentative = set(arm_literal_thumb) - set(blx_sites.values()) - set(veneers.values()) - runtime_thumb
    while pending:
        entry = pending.pop()
        if entry in known_entries:
            continue
        try:
            ranges, calls = discover_thumb_function(bytes(code), entry, known_insns)
        except ConversionError as error:
            if entry in tentative:
                known_entries.add(entry)
                continue
            raise
        if entry in tentative:
            claimed = set()
            for a_b in ranges.split(";"):
                a, b = (int(x, 16) for x in a_b.split("-"))
                claimed.update(range(a, b, 2))
            if claimed & decoded_a32:
                known_entries.add(entry)
                continue
        thumb.append({"entry": entry, "name": f"thumb_{entry:08X}", "ranges": ranges or f"{entry:x}-{entry:x}"})
        known_entries.add(entry)
        discovered.append(entry)
        for a_b in ranges.split(";"):
            if a_b:
                a, b = (int(x, 16) for x in a_b.split("-"))
                known_insns.update(range(a, b, 2))
        pending += [c for c in calls if c not in known_entries]
    result = convert(bytes(code), thumb)
    # Thumb literal pointers found while converting: discover and reconvert
    for _ in range(8):
        extra = sorted(t for t in THUMB_LITERAL_POINTERS - known_entries
                       if t not in decoded_a32)
        if not extra:
            break
        work = list(extra)
        while work:
            entry = work.pop()
            if entry in known_entries:
                continue
            if entry in known_insns:
                owner = next(f for f in thumb if any(int(r.split("-")[0], 16) <= entry < int(r.split("-")[1], 16)
                                                      for r in f["ranges"].split(";")))
                thumb.append({"entry": entry, "name": f"thumb_{entry:08X}", "ranges": owner["ranges"]})
            else:
                try:
                    ranges, calls = discover_thumb_function(bytes(code), entry, known_insns)
                except ConversionError:
                    known_entries.add(entry)
                    continue
                claimed = set()
                for a_b in ranges.split(";"):
                    a, b = (int(x, 16) for x in a_b.split("-"))
                    claimed.update(range(a, b, 2))
                if claimed & decoded_a32:
                    known_entries.add(entry)
                    continue
                thumb.append({"entry": entry, "name": f"thumb_{entry:08X}", "ranges": ranges})
                for a_b in ranges.split(";"):
                    a, b = (int(x, 16) for x in a_b.split("-"))
                    known_insns.update(range(a, b, 2))
                work += [c for c in calls if c not in known_entries]   # Thumb callees are Thumb code
            known_entries.add(entry)
            discovered.append(entry)
        result = convert(bytes(code), thumb)
    patched = patch_arm_blx(code, text_size, result["entries"], set(blx_sites))
    # Inline-argument helpers: stub = constant into the helper's register, then
    # the helper's second instruction; the call returns to the data word, which
    # becomes a branch over it in this image.
    helpers = inline_arg_helpers(bytes(code), text_size)
    segment = bytearray(result["segment"])
    inline_rewrites = []
    original = args.code.read_bytes()
    for site in sorted({site for site, _ in call_sites}):
        if not BASE <= site < BASE + text_size:
            continue
        # decode the original instruction: the program may come from an image
        # where this call was already retargeted to its stub
        w = struct.unpack_from("<I", original, site - BASE)[0]
        if (w & 0x0F000000) != 0x0B000000:
            continue
        imm = w & 0xFFFFFF
        imm = imm - (1 << 24) if imm & 0x800000 else imm
        target = site + 8 + (imm << 2)
        if target not in helpers:
            continue
        if (w & 0x0F000000) != 0x0B000000 or (w >> 28) != AL:
            raise ConversionError(f"inline-argument call at 0x{site:08X} is not an unconditional BL")
        value = struct.unpack_from("<I", original, site + 4 - BASE)[0]
        stub = SYNTH_BASE + len(segment)
        words = load_constant(helpers[target], value)
        words.append(branch(AL, False, stub + 4 * len(words), target + 4))
        for word in words:
            segment += struct.pack("<I", word)
        struct.pack_into("<I", code, site - BASE, branch(AL, True, site, stub))
        struct.pack_into("<I", code, site + 4 - BASE, branch(AL, False, site + 4, site + 8))
        inline_rewrites.append({"site": site, "helper": target, "inline_word": value, "stub": stub})
    result["segment"] = bytes(segment)
    image = bytes(code) + bytes(SYNTH_BASE - BASE - len(code)) + result["segment"]
    args.output_image.write_bytes(image)
    aliases = [{"alias": t | 1, "target": v} for t, v in sorted(result["entries"].items())]
    args.output_map.write_text(json.dumps({
        "format": "mm3d_thumb_to_a32_v1",
        "synthetic_base": SYNTH_BASE,
        "synthetic_size": len(result["segment"]),
        "functions": [{"thumb_entry": t, "a32_entry": v, "name": f"thumb_{t:08X}"} for t, v in sorted(result["entries"].items())],
        "dispatch_aliases": aliases,
        "patched_blx_sites": patched,
        "discovered_thumb_functions": discovered,
        "inline_argument_rewrites": inline_rewrites,
        "thumb_functions": [{"entry": f["entry"], "name": f["name"], "ranges": f["ranges"]} for f in thumb],
        "instruction_map": {f"0x{k:08X}": v for k, v in sorted(result["vmap"].items())},
    }, indent=1))
    print(f"converted {len(result['vmap'])} Thumb instructions into {len(result['segment'])//4} A32 words; "
          f"{len(result['entries'])} functions ({len(discovered)} discovered), {len(patched)} BLX sites patched, "
          f"{len(inline_rewrites)} inline-argument calls rewritten; image {len(image)} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
