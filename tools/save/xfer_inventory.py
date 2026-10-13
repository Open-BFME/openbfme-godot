#!/usr/bin/env python3
"""Inventory of every RotWK 2.01 save-game transfer (Snapshot::xfer) read from the binary.

    python3 tools/save/xfer_inventory.py [--bin game.dat] [--map rw_map.csv] [--csv out.csv] [--md docs/SAVES-RETAIL.md]

Lane SAVE-A0. For every RotWK function that transfers a version (Xfer::operator==(Version&),
RW 0xA1F5B9, Xfer vtable slot 0x28, or one of the tiny "Version1"-style helpers that call it),
and for every function the RotWK -> BFME2 -> decomp map (tools/re, rw_map.csv) names `::xfer`,
it reads from the code:

  - the (earliest, current) version window: the two bytes stored into the Version local whose
    address is handed to slot 0x28 (XferVersionTransfer.cpp in the Open-BFME-2 decomp: data[0]
    earliest, data[1] current; the loader throws below earliest or above current);
  - the transfer sequence in code order: every Xfer virtual called on the function's Xfer
    argument (slot table below, from the RotWK Xfer vtable at RW 0xBD1940 and the decomp's
    mangled operator== names), every direct call to another xfer (a base class or member
    snapshot), and every other direct call that receives the Xfer (a helper such as an
    ObjectID or list transfer).

The scan is linear (both arms of a branch appear, loops once); it is a field ORDER, not a
control-flow decompile: `if (version >= 2)` gates are not shown. Functions whose window could not
be read are listed with the reason, never guessed. Stdlib + pefile + capstone; writes only the
requested outputs. The rw_map.csv input lives in the ignored workspace (retail-derived names and
addresses); the generated table carries addresses, versions and names only, never code bytes.
"""
from __future__ import annotations

import argparse
import csv
import os
import re
import struct
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

BASE = 0x400000

# Xfer vtable slots (byte offsets), RotWK Xfer base vtable RW 0xBD1940 (XferLoad RW 0xC939E8,
# XferSave RW 0xC93AE0, XferCRC RW 0xBFD2A0). Names: the decomp's mangled ??8Xfer operator==
# parameter types at each slot (rw_map.csv), ZH Xfer.h for the block calls.
SLOT_NAMES = {
    0x04: "?isLoading", 0x08: "?isStoring", 0x0C: "?isCRC", 0x10: "?isLightCRC",
    0x14: "BLOK", 0x18: "EBLK", 0x1C: "skipBLOK", 0x20: "skipBadBlock",
    0x24: "raw", 0x28: "ver", 0x2C: "label", 0x30: "snapshot",
    0x34: "rgbaInt", 0x38: "rgbaReal", 0x3C: "rgb", 0x40: "realRange",
    0x44: "iregion2d", 0x48: "region2d", 0x4C: "icoord2d", 0x50: "coord2d",
    0x54: "iregion3d", 0x58: "region3d", 0x5C: "icoord3d", 0x60: "coord3d",
    0x64: "pstr", 0x68: "ustr", 0x6C: "astr", 0x70: "real", 0x74: "i64", 0x78: "u32",
    0x7C: "i32", 0x80: "u16", 0x84: "i16", 0x88: "u8", 0x8C: "i8", 0x90: "bool",
    0x94: "enum", 0x98: "data",
}
MODE_QUERIES = {0x04, 0x08, 0x0C, 0x10}
VERSION_SLOT = 0x28
XFER_VERSION_VA = 0xA1F5B9          # Xfer::operator==(Version&), tier A
XFER_BASE_VTABLE_VA = 0xBD1940      # 39 slots
EH_PROLOG_VA = 0xA3CEF0             # MSVC __EH_prolog: leaves ebp = entry esp - 4 and 3 pushes below it
SNAPSHOT_XFER_SLOT = 0x0C          # Snapshot vtable: dtor, crc, loadPostProcess, xfer (decomp Xfer.cpp)

CALLER_SAVED = ("eax", "ecx", "edx")
REG32 = ("eax", "ebx", "ecx", "edx", "esi", "edi", "ebp", "esp")
REG_ALIAS = {
    "al": "eax", "ah": "eax", "ax": "eax", "bl": "ebx", "bh": "ebx", "bx": "ebx",
    "cl": "ecx", "ch": "ecx", "cx": "ecx", "dl": "edx", "dh": "edx", "dx": "edx",
    "si": "esi", "di": "edi", "bp": "ebp", "sp": "esp",
}


def full_reg(name):
    return REG_ALIAS.get(name, name)


@dataclass
class Call:
    """One transfer in a function's sequence."""
    kind: str            # slot | ver | xfer | helper | vcall
    text: str
    va: int


@dataclass
class FunctionReport:
    va: int
    size: int
    versions: list = field(default_factory=list)   # [(earliest, current) or None, ...] in code order
    calls: list = field(default_factory=list)      # [Call]
    problems: list = field(default_factory=list)

    @property
    def window(self):
        """The class's own version window: the first version transfer of the function."""
        return self.versions[0] if self.versions else None


class Context:
    """What the scan of one function needs to know about the rest of the binary."""

    def __init__(self, version_helpers=None, xfer_names=None, func_names=None, slot_targets=None):
        self.version_helpers = dict(version_helpers or {})   # va -> (earliest, current)
        self.xfer_names = dict(xfer_names or {})             # va -> "Class::xfer" (known xfers)
        self.func_names = dict(func_names or {})             # va -> any name (helpers)
        self.slot_targets = dict(slot_targets or {})         # va -> slot offset (direct calls to Xfer methods)


def _disasm(code, va):
    import capstone
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    return list(md.disasm(code, va))


def _mem_key(op, insn, state):
    """A stack or register-relative memory operand as a hashable location, entry-relative for esp/ebp."""
    import capstone.x86 as x86
    if op.type != x86.X86_OP_MEM:
        return None
    m = op.mem
    base = insn.reg_name(m.base) if m.base else None
    if m.index:
        return None
    if base == "esp":
        return ("E", m.disp - state.delta)
    if base == "ebp" and state.ebp_entry is not None:
        return ("E", state.ebp_entry + m.disp)
    if base is None:
        return ("abs", m.disp)
    return (base, m.disp)


class _State:
    def __init__(self):
        self.delta = 0                 # bytes pushed since entry (esp = entry_esp - delta)
        self.ebp_entry = None          # ebp = entry_esp + ebp_entry once `mov ebp, esp` ran
        self.xfer_regs = set()
        self.xfer_mem = {("E", 4)}     # thiscall: the Xfer* is the first stack argument
        self.vt_regs = set()           # registers holding the Xfer's vtable pointer
        self.consts = {}               # reg32 -> known small constant
        self.pending_pushes = 0        # pushes since the last call (callee-cleaned arguments)
        self.xfer_pushed = False       # the Xfer was pushed as an argument since the last call
        self.last_push_imm = None
        self.push_imms = []            # immediates pushed since the last call, in order
        self.stores = {}               # location -> (byte value) for version locals
        self.leas = {}                 # reg -> location loaded by lea


def _read_window(state, loc):
    """(earliest, current) from the byte stores at loc and loc+1, or None."""
    lo = state.stores.get(loc)
    hi = state.stores.get((loc[0], loc[1] + 1))
    if lo is None or hi is None:
        return None
    return (lo, hi)


def analyze_function(code, va, ctx, xfer_in_ecx=False):
    """Scan one function's code (bytes at VA va) for its version window and transfer sequence.

    A Snapshot::xfer receives the Xfer as its stack argument (thiscall); a member of Xfer itself
    (a version helper) has it in ecx: xfer_in_ecx."""
    import capstone.x86 as x86
    rep = FunctionReport(va=va, size=len(code))
    st = _State()
    if xfer_in_ecx:
        st.xfer_regs.add("ecx")
        st.xfer_mem.clear()
    insns = _disasm(code, va)
    if not insns:
        rep.problems.append("no instructions decoded")
        return rep

    def is_xfer_operand(op, insn):
        if op.type == x86.X86_OP_REG:
            return insn.reg_name(op.reg) in st.xfer_regs
        if op.type == x86.X86_OP_MEM:
            return _mem_key(op, insn, st) in st.xfer_mem
        return False

    def clear_reg(r):
        st.xfer_regs.discard(r)
        st.vt_regs.discard(r)
        st.consts.pop(r, None)
        st.leas.pop(r, None)

    def version_at_call(arg_loc):
        if arg_loc is None:
            return None
        return _read_window(st, arg_loc)

    last_arg_loc = None     # location of the Version local handed to the next call (lea + push)
    for i, insn in enumerate(insns):
        mn = insn.mnemonic
        ops = insn.operands
        # ---- stack bookkeeping and argument tracking
        if mn == "push":
            op = ops[0]
            pushes_xfer = is_xfer_operand(op, insn)   # the operand is read before esp moves
            st.delta += 4
            st.pending_pushes += 1
            if op.type == x86.X86_OP_IMM:
                st.push_imms.append(op.imm & 0xFFFFFFFF)
            else:
                st.push_imms.append(None)
            if pushes_xfer:
                st.xfer_pushed = True
            if op.type == x86.X86_OP_REG:
                r = insn.reg_name(op.reg)
                if r in st.leas:
                    last_arg_loc = st.leas[r]
            continue
        if mn == "pop":
            st.delta -= 4
            if ops and ops[0].type == x86.X86_OP_REG:
                r = insn.reg_name(ops[0].reg)
                # push imm; pop reg is MSVC's short constant load
                imm = st.push_imms[-1] if st.push_imms else None
                clear_reg(r)
                if imm is not None:
                    st.consts[r] = imm
            if st.pending_pushes:
                st.pending_pushes -= 1
                if st.push_imms:
                    st.push_imms.pop()
            continue
        if mn in ("sub", "add") and len(ops) == 2 and ops[0].type == x86.X86_OP_REG \
                and insn.reg_name(ops[0].reg) == "esp" and ops[1].type == x86.X86_OP_IMM:
            st.delta += ops[1].imm if mn == "sub" else -ops[1].imm
            if mn == "add":
                st.pending_pushes = 0
                st.push_imms = []
            continue
        if mn == "mov" and len(ops) == 2 and ops[0].type == x86.X86_OP_REG \
                and insn.reg_name(ops[0].reg) == "ebp" and ops[1].type == x86.X86_OP_REG \
                and insn.reg_name(ops[1].reg) == "esp":
            st.ebp_entry = -st.delta
            continue
        if mn in ("call", "jmp") and ops and ops[0].type in (x86.X86_OP_IMM, x86.X86_OP_MEM, x86.X86_OP_REG):
            if mn == "jmp" and ops[0].type == x86.X86_OP_IMM:
                continue  # ordinary branch inside the function (linear scan)
            op = ops[0]
            if op.type == x86.X86_OP_IMM and (op.imm & 0xFFFFFFFF) == EH_PROLOG_VA:
                # push -1; push handler; push fs:[0]; saved ebp over the return address; ebp -> it
                st.ebp_entry = -4
                st.delta += 12
                st.pending_pushes = 0
                st.push_imms = []
                clear_reg("eax")
                continue
            ecx_is_xfer = "ecx" in st.xfer_regs
            if op.type == x86.X86_OP_MEM and op.mem.base and not op.mem.index:
                base = insn.reg_name(op.mem.base)
                disp = op.mem.disp
                if base in st.vt_regs and disp in SLOT_NAMES:
                    if disp == VERSION_SLOT:
                        w = version_at_call(last_arg_loc)
                        rep.versions.append(w)
                        rep.calls.append(Call("ver", f"ver{w}" if w else "ver(?)", insn.address))
                        if w is None:
                            rep.problems.append(f"version window not read at {insn.address:#x}")
                    elif disp not in MODE_QUERIES:
                        text = SLOT_NAMES[disp]
                        if disp in (0x94, 0x24) and st.push_imms and st.push_imms[-1] is not None and disp == 0x24:
                            text += f"[{st.push_imms[-1]}]"
                        if disp == 0x94 and st.push_imms and st.push_imms[0] is not None:
                            # XferEnum(name, data, size): size is pushed first (cdecl right-to-left)
                            text += f"{st.push_imms[0]}"
                        rep.calls.append(Call("slot", text, insn.address))
                elif st.xfer_pushed and disp == SNAPSHOT_XFER_SLOT:
                    rep.calls.append(Call("vcall", "->virtual xfer", insn.address))
                elif st.xfer_pushed and disp not in (0, 4, 8):
                    rep.calls.append(Call("vcall", f"virtual+{disp:#x}", insn.address))
            elif op.type == x86.X86_OP_IMM:
                target = op.imm & 0xFFFFFFFF
                if target == XFER_VERSION_VA or (target in ctx.slot_targets and ctx.slot_targets[target] == VERSION_SLOT):
                    w = version_at_call(last_arg_loc)
                    rep.versions.append(w)
                    rep.calls.append(Call("ver", f"ver{w}" if w else "ver(?)", insn.address))
                    if w is None:
                        rep.problems.append(f"version window not read at {insn.address:#x}")
                elif target in ctx.version_helpers and ecx_is_xfer:
                    w = ctx.version_helpers[target]
                    rep.versions.append(w)
                    rep.calls.append(Call("ver", f"ver{w}", insn.address))
                elif target in ctx.slot_targets and ecx_is_xfer:
                    slot = ctx.slot_targets[target]
                    if slot not in MODE_QUERIES:
                        rep.calls.append(Call("slot", SLOT_NAMES[slot], insn.address))
                elif target in ctx.xfer_names and (st.xfer_pushed or ecx_is_xfer):
                    rep.calls.append(Call("xfer", "->" + ctx.xfer_names[target], insn.address))
                elif st.xfer_pushed or ecx_is_xfer:
                    name = ctx.func_names.get(target, f"FUN_{target:08x}")
                    rep.calls.append(Call("helper", "call:" + name, insn.address))
            if mn == "jmp":
                continue
            # after a call: caller-saved registers are dead, callee cleaned its stack arguments
            # unless the next instruction is `add esp, N` (cdecl; handled above)
            nxt = insns[i + 1] if i + 1 < len(insns) else None
            cdecl = nxt is not None and nxt.mnemonic == "add" and nxt.op_str.startswith("esp,")
            if not cdecl:
                st.delta -= 4 * st.pending_pushes
            st.pending_pushes = 0
            st.push_imms = []
            st.xfer_pushed = False
            last_arg_loc = None
            for r in CALLER_SAVED:
                clear_reg(r)
            continue
        if mn in ("ret", "retn"):
            continue
        # ---- data flow for the xfer pointer, its vtable, constants and version stores
        if mn == "mov" and len(ops) == 2:
            dst, src = ops
            if dst.type == x86.X86_OP_REG:
                r = full_reg(insn.reg_name(dst.reg))
                was_partial = insn.reg_name(dst.reg) != r
                if src.type == x86.X86_OP_IMM:
                    clear_reg(r)
                    if not was_partial:
                        st.consts[r] = src.imm & 0xFFFFFFFF
                    else:
                        st.consts[r] = src.imm & 0xFF
                    continue
                xfer_src = is_xfer_operand(src, insn)
                vt_src = (src.type == x86.X86_OP_MEM and src.mem.base and not src.mem.index
                          and src.mem.disp == 0 and insn.reg_name(src.mem.base) in st.xfer_regs)
                const_src = (st.consts.get(full_reg(insn.reg_name(src.reg)))
                             if src.type == x86.X86_OP_REG else None)
                clear_reg(r)
                if xfer_src:
                    st.xfer_regs.add(r)
                elif vt_src:
                    st.vt_regs.add(r)
                elif const_src is not None:
                    st.consts[r] = const_src
                continue
            if dst.type == x86.X86_OP_MEM:
                loc = _mem_key(dst, insn, st)
                if loc is None:
                    continue
                if src.type == x86.X86_OP_REG:
                    rname = insn.reg_name(src.reg)
                    r = full_reg(rname)
                    if r in st.xfer_regs and rname == r:
                        st.xfer_mem.add(loc)
                    elif loc in st.xfer_mem:
                        st.xfer_mem.discard(loc)
                    c = st.consts.get(r)
                    if c is not None:
                        _store(st, loc, c, dst.size)
                elif src.type == x86.X86_OP_IMM:
                    st.xfer_mem.discard(loc)
                    _store(st, loc, src.imm & 0xFFFFFFFF, dst.size)
                continue
        if mn == "lea" and len(ops) == 2 and ops[0].type == x86.X86_OP_REG:
            r = insn.reg_name(ops[0].reg)
            clear_reg(r)
            loc = _mem_key(ops[1], insn, st)
            if loc is not None:
                st.leas[r] = loc
            continue
        if mn == "xor" and len(ops) == 2 and ops[0].type == x86.X86_OP_REG and ops[1].type == x86.X86_OP_REG \
                and ops[0].reg == ops[1].reg:
            r = full_reg(insn.reg_name(ops[0].reg))
            clear_reg(r)
            st.consts[r] = 0
            continue
        if mn in ("inc", "dec") and ops and ops[0].type == x86.X86_OP_REG:
            r = full_reg(insn.reg_name(ops[0].reg))
            c = st.consts.get(r)
            clear_reg(r)
            if c is not None:
                st.consts[r] = (c + (1 if mn == "inc" else -1)) & 0xFFFFFFFF
            continue
        # any other write to a register forgets what we knew about it
        if ops and ops[0].type == x86.X86_OP_REG and mn not in ("cmp", "test", "push"):
            clear_reg(full_reg(insn.reg_name(ops[0].reg)))
    return rep


def _store(st, loc, value, size):
    for k in range(min(size, 4)):
        st.stores[(loc[0], loc[1] + k)] = (value >> (8 * k)) & 0xFF


class Binary:
    def __init__(self, path):
        import pefile
        pe = pefile.PE(str(path), fast_load=True)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.mem = pe.get_memory_mapped_image()
        text = next(s for s in pe.sections if s.Name.startswith(b".text"))
        self.text_lo = self.base + text.VirtualAddress
        self.text_hi = self.text_lo + text.Misc_VirtualSize

    def bytes_at(self, va, size):
        o = va - self.base
        return bytes(self.mem[o:o + size])

    def u32(self, va):
        return struct.unpack_from("<I", self.mem, va - self.base)[0]


def main_checkout():
    try:
        common = subprocess.run(["git", "rev-parse", "--path-format=absolute", "--git-common-dir"],
                                cwd=Path(__file__).resolve().parent, capture_output=True, text=True,
                                check=True).stdout.strip()
    except (OSError, subprocess.CalledProcessError):
        return Path(__file__).resolve().parents[2]
    return Path(common).parent


def load_map(path):
    with open(path, newline="", encoding="utf-8") as fh:
        rows = list(csv.DictReader(fh))
    for r in rows:
        r["_va"] = int(r["rotwk_rva"], 16) + BASE
        r["_size"] = int(r["size"])
    rows.sort(key=lambda r: r["_va"])
    return rows


XFER_NAME = re.compile(r"::(xfer|DoXfer)$")


def find_version_helpers(binary, rows):
    """Functions of at most 0x30 bytes that load two constant bytes into a local and call the version slot on ecx."""
    helpers = {}
    pat = re.compile(rb"\xff[\x50-\x53\x56\x57]\x28")
    for r in rows:
        code = first_body(binary.bytes_at(r["_va"], min(r["_size"], 0x40)), r["_va"])
        if code is None or len(code) > 0x30 or not pat.search(code):
            continue
        st_rep = analyze_function(code, r["_va"], Context(), xfer_in_ecx=True)
        if st_rep.versions and len(st_rep.versions) == 1 and st_rep.versions[0] and len(st_rep.calls) == 1:
            helpers[r["_va"]] = st_rep.versions[0]
    return helpers


def first_body(code, va):
    """The bytes up to and including the first ret (rw_map extents can span several tiny functions)."""
    for insn in _disasm(code, va):
        if insn.mnemonic in ("ret", "retn"):
            return code[:insn.address + insn.size - va]
    return None


def slot_targets(binary):
    """Xfer base vtable implementations -> slot offset (non-virtual calls into Xfer's own methods)."""
    out = {}
    for k in range(39):
        f = binary.u32(XFER_BASE_VTABLE_VA + 4 * k)
        if k * 4 in SLOT_NAMES and f not in out:
            out[f] = k * 4
    # pure-virtual stubs and the shared mode getters are not transfers
    for f, slot in list(out.items()):
        if slot in MODE_QUERIES or f in (0x43B160,):
            del out[f]
    return out


def build_inventory(binary, rows):
    by_va = {r["_va"]: r for r in rows}
    names = {}
    for r in rows:
        q = r["decomp_qualname"]
        names[r["_va"]] = q if q else r["rotwk_name"]
    xfer_rows = [r for r in rows if XFER_NAME.search(r["decomp_qualname"] or "")]
    xfer_names = {r["_va"]: r["decomp_qualname"] for r in xfer_rows}
    helpers = find_version_helpers(binary, rows)
    ctx = Context(helpers, xfer_names, names, slot_targets(binary))
    # every function that transfers a version: the call pattern on the vtable, the base Version
    # operator, or a helper
    call_rel = re.compile(rb"\xe8(....)", re.S)
    vslot = re.compile(rb"\xff[\x50-\x53\x56\x57]\x28")
    candidates = {r["_va"] for r in xfer_rows}
    wanted_targets = set(helpers) | {XFER_VERSION_VA}
    for r in rows:
        if r["_size"] > 0x20000 or r["_va"] in helpers:
            continue
        code = binary.bytes_at(r["_va"], r["_size"])
        hit = bool(vslot.search(code))
        if not hit:
            for m in call_rel.finditer(code):
                tgt = (r["_va"] + m.start() + 5 + struct.unpack("<i", m.group(1))[0]) & 0xFFFFFFFF
                if tgt in wanted_targets:
                    hit = True
                    break
        if hit:
            candidates.add(r["_va"])
    out = []
    for va in sorted(candidates):
        r = by_va[va]
        code = binary.bytes_at(va, r["_size"])
        rep = analyze_function(code, va, ctx)
        named = bool(XFER_NAME.search(r["decomp_qualname"] or ""))
        if not named and not any(rep.versions):
            continue  # a vtable +0x28 call on something that is not the Xfer argument
        out.append((r, rep))
    return out, helpers


def compress(calls):
    """Run-length compress a transfer sequence for the table."""
    out = []
    for c in calls:
        if out and out[-1][0] == c.text:
            out[-1][1] += 1
        else:
            out.append([c.text, 1])
    return " ".join(t if n == 1 else f"{t}x{n}" for t, n in out)


def class_of(r):
    q = r["decomp_qualname"] or ""
    return q.rsplit("::", 1)[0] if "::" in q else ""


def write_csv(path, inv):
    with open(path, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh, lineterminator="\n")
        w.writerow(["rw_va", "size", "tier", "decomp_status", "qualname", "earliest", "current",
                    "version_calls", "sequence", "decomp_source", "subsystem", "problems"])
        for r, rep in inv:
            win = rep.window
            w.writerow([f"0x{r['_va']:06X}", r["_size"], r["tier"], r["decomp_status"],
                        r["decomp_qualname"] or r["rotwk_name"],
                        win[0] if win else "", win[1] if win else "", len(rep.versions),
                        compress(rep.calls), r["decomp_source"], r["subsystem"], "; ".join(rep.problems)])


MD_BEGIN = "<!-- BEGIN GENERATED: tools/save/xfer_inventory.py -->"
MD_END = "<!-- END GENERATED -->"


def md_table(inv, helpers):
    lines = []
    n = len(inv)
    named = sum(1 for r, _ in inv if XFER_NAME.search(r["decomp_qualname"] or ""))
    windows = sum(1 for _, rep in inv if rep.window)
    tiers = {}
    for r, _ in inv:
        tiers[r["tier"] or "-"] = tiers.get(r["tier"] or "-", 0) + 1
    lines.append(f"{n} functions transfer a version or are named `::xfer` by the map "
                 f"({named} named `::xfer` / `::DoXfer`, {n - named} found by their version transfer only); "
                 f"{windows} with a version window read from the code. Tiers: "
                 + ", ".join(f"{k} {v}" for k, v in sorted(tiers.items())) + ". "
                 f"Version helpers (whole body = one version transfer on ecx): "
                 + ", ".join(f"RW 0x{va:06X} {w}" for va, w in sorted(helpers.items())) + ".")
    lines.append("")
    lines.append("| RW | Tier | Class / function | (earliest, current) | Transfer sequence (code order, both branch arms) |")
    lines.append("|---|---|---|---|---|")
    for r, rep in sorted(inv, key=lambda x: ((x[0]["decomp_qualname"] or "~" + x[0]["rotwk_name"]).lower(), x[0]["_va"])):
        win = rep.window
        name = r["decomp_qualname"] or f"{r['rotwk_name']} (unnamed)"
        status = r["decomp_status"]
        tier = (r["tier"] or "-") + ("" if status in ("authored", "vendored", "generated") else f" ({status or 'none'})")
        seq = compress(rep.calls).replace("|", "\\|")
        if rep.problems:
            seq += " **[" + "; ".join(rep.problems) + "]**"
        win_s = f"({win[0]}, {win[1]})" if win else ("none" if not rep.versions else "not read")
        if len(rep.versions) > 1:
            win_s += f" +{len(rep.versions) - 1}"
        lines.append(f"| 0x{r['_va']:06X} | {tier} | `{name}` | {win_s} | {seq} |")
    return "\n".join(lines)


def write_md(path, inv, helpers):
    text = Path(path).read_text(encoding="utf-8")
    if MD_BEGIN not in text or MD_END not in text:
        raise SystemExit(f"{path}: no generated-section markers ({MD_BEGIN} ... {MD_END})")
    head, rest = text.split(MD_BEGIN, 1)
    _, tail = rest.split(MD_END, 1)
    Path(path).write_text(head + MD_BEGIN + "\n" + md_table(inv, helpers) + "\n" + MD_END + tail, encoding="utf-8")


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    root = main_checkout()
    ap.add_argument("--bin", default=os.environ.get("RW_GAME_DAT"), help="RotWK 2.01 game.dat ($RW_GAME_DAT)")
    ap.add_argument("--map", default=os.environ.get("OPENBFME_XMAP") or str(root / "workspace/rebuild/xmap/rw_map.csv"))
    ap.add_argument("--csv", help="write the full inventory as CSV (keep it under the ignored workspace/)")
    ap.add_argument("--md", help="regenerate the table between the markers of this markdown file")
    ap.add_argument("--show", help="print the scan of one RW address")
    args = ap.parse_args(argv)
    if not args.bin or not Path(args.bin).is_file():
        raise SystemExit("RotWK game.dat not found: pass --bin or set RW_GAME_DAT")
    if not Path(args.map).is_file():
        raise SystemExit(f"map not found: {args.map} (tools/re/README.md; or set OPENBFME_XMAP)")
    binary = Binary(args.bin)
    rows = load_map(args.map)
    inv, helpers = build_inventory(binary, rows)
    if args.show:
        va = int(args.show, 16)
        for r, rep in inv:
            if r["_va"] <= va < r["_va"] + r["_size"]:
                print(f"RW 0x{r['_va']:06X} {r['decomp_qualname'] or r['rotwk_name']} tier {r['tier']} window {rep.window}")
                for c in rep.calls:
                    print(f"  {c.va:#x} {c.kind:6} {c.text}")
                for p in rep.problems:
                    print("  problem:", p)
                return 0
        raise SystemExit(f"0x{va:X} is not in the inventory")
    if args.csv:
        write_csv(args.csv, inv)
    if args.md:
        write_md(args.md, inv, helpers)
    if not args.csv and not args.md:
        print(md_table(inv, helpers))
    return 0


if __name__ == "__main__":
    sys.exit(main())
