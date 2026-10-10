"""Shared helpers for binary-to-binary function matching (RotWK vs BFME2 vs BFME1).

- Image: PE image access by RVA (memory-mapped layout), section lookup, C/UTF-16 strings.
- rtti_classes(): MSVC RTTI walk (vtable -> CompleteObjectLocator -> TypeDescriptor name),
  giving {function_rva: {class names whose vtable holds it}}.
- extract_features(): per-function disassembly features used by the matcher:
    mhash   sha1 of the body with every absolute address (imm32/disp32 inside the image) and
            every rel32 branch leaving the function masked to zero; a referenced C string is
            folded in by content, so `push offset "foo"` still distinguishes functions (__FILE__
            paths by basename only: the build machine's path differs between the two games)
    shape   list of interned "mnemonic optype,optype" strings (register choice ignored)
    callees external call / tail-jump targets in order (RVA)
    strs    referenced string literals in order
    ext     sections outside the original image that the body branches to / references
Relocation tables are NOT used: BFME2 1.06's .reloc data directory is gone and the table is
partially overwritten by an import directory, so masking works from operand values instead.
"""
import csv
import hashlib
import os
import pickle
import re
import struct
import sys

import capstone
import pefile

GRP_JUMP, GRP_CALL, GRP_BRANCH_REL = 1, 2, 7
FEATURE_VERSION = 4
SRC_PATH = re.compile(r"^[A-Za-z]:\\.*\\([^\\]+\.(?:cpp|c|h|inl))$", re.I)


def salt_string(s):
    """__FILE__ literals differ only by build machine path between BFME2 and RotWK: hash the basename."""
    m = SRC_PATH.match(s)
    return ("__FILE__:" + m.group(1).lower()) if m else s


class Image:
    def __init__(self, path):
        self.path = path
        pe = pefile.PE(path, fast_load=True)
        self.base = pe.OPTIONAL_HEADER.ImageBase
        self.size = pe.OPTIONAL_HEADER.SizeOfImage
        self.mem = pe.get_memory_mapped_image()
        self.sections = []
        for s in pe.sections:
            name = s.Name.split(b"\0")[0].decode("latin1").strip() or "(unnamed)"
            self.sections.append((name, s.VirtualAddress,
                                  s.VirtualAddress + max(s.Misc_VirtualSize, s.SizeOfRawData),
                                  s.Characteristics))
        with open(path, "rb") as fh:
            self.md5 = hashlib.md5(fh.read()).hexdigest()
        text = self.section_named(".text")
        self.text = (text[1], text[2])

    def section_named(self, name):
        return next(s for s in self.sections if s[0] == name)

    def section_of(self, rva):
        for s in self.sections:
            if s[1] <= rva < s[2]:
                return s[0]
        return None

    def u32(self, rva):
        return struct.unpack_from("<I", self.mem, rva)[0]

    def valid_rva(self, rva):
        return 0 <= rva < len(self.mem)

    def cstr(self, rva, minlen=3, maxlen=1024):
        """Printable ASCII string terminated by NUL at rva, else None."""
        mem = self.mem
        if not self.valid_rva(rva):
            return None
        end = rva
        lim = min(len(mem), rva + maxlen)
        while end < lim and (0x20 <= mem[end] < 0x7F or mem[end] in (9, 10, 13)):
            end += 1
        if end >= lim or mem[end] != 0 or end - rva < minlen:
            return None
        return mem[rva:end].decode("ascii")

    def wstr(self, rva, minlen=3, maxlen=512):
        mem = self.mem
        out = []
        pos = rva
        while pos + 1 < len(mem) and len(out) < maxlen:
            lo, hi = mem[pos], mem[pos + 1]
            if hi != 0:
                return None
            if lo == 0:
                break
            if not (0x20 <= lo < 0x7F or lo in (9, 10, 13)):
                return None
            out.append(chr(lo))
            pos += 2
        return "".join(out) if len(out) >= minlen else None

    def string_at(self, rva):
        if self.section_of(rva) not in (".rdata", ".data"):
            return None
        s = self.cstr(rva)
        if s is not None:
            return s
        w = self.wstr(rva)
        return ("L:" + w) if w is not None else None


def load_inventory(path):
    """Read an export_inventory.java functions CSV (or a plain rva,size,name CSV)."""
    out = []
    with open(path, newline="", encoding="utf-8") as fh:
        for row in csv.DictReader(fh):
            rva = int(row["rva"], 16)
            size = int(row["size"])
            blk = int(row.get("start_block_size") or size)
            out.append((rva, size, blk, row.get("name", "")))
    out.sort()
    return out


def rtti_classes(img):
    """Return {function_rva: set(class names)} from MSVC RTTI-described vtables."""
    rdata = [s for s in img.sections if s[0] in (".rdata", ".data")]
    text0, text1 = img.text
    base = img.base
    # 1. complete object locators: sig=0, pTypeDescriptor -> ".?AV..."/".?AU..." name at +8
    cols = {}
    for _, lo, hi, _ in rdata:
        for rva in range(lo, hi - 20, 4):
            sig = img.u32(rva)
            if sig != 0:
                continue
            ptd = img.u32(rva + 12) - base
            if not (0 < ptd < img.size - 16) or not img.valid_rva(ptd + 8):
                continue
            if img.mem[ptd + 8:ptd + 11] not in (b".?A", ):
                continue
            name = img.cstr(ptd + 8, minlen=4, maxlen=512)
            if name:
                cols[rva] = name
    # 2. vtables: a dword pointing at a COL, followed by code pointers
    out = {}
    colva = {rva + base: n for rva, n in cols.items()}
    for _, lo, hi, _ in rdata:
        for rva in range(lo, hi - 8, 4):
            v = img.u32(rva)
            name = colva.get(v)
            if name is None:
                continue
            cls = demangle_type(name)
            slot = rva + 4
            while slot + 4 <= hi:
                f = img.u32(slot) - base
                if not (text0 <= f < text1):
                    break
                out.setdefault(f, set()).add(cls)
                slot += 4
                if img.u32(slot) - base in cols:
                    break
    return out


def demangle_type(name):
    """.?AVFoo@Bar@@ -> Bar::Foo (templates left mangled)."""
    body = name[4:] if name.startswith((".?AV", ".?AU")) else name
    body = body.rstrip("@")
    if body.startswith("?$"):
        return body
    parts = [p for p in body.split("@") if p]
    return "::".join(reversed(parts))


def _optype(op):
    t = op.type
    if t == 1:
        return "r%d" % op.size
    if t == 2:
        return "i"
    if t == 3:
        return "m%d" % op.size
    return "f"


def extract_features(img, funcs, added_sections=(), log_every=0):
    """funcs: list of (rva, size, blk, name). Returns list of dicts (same order)."""
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    md.skipdata = True
    lo_va, hi_va = img.base, img.base + img.size
    base = img.base
    tokens = {}  # token -> count (diagnostic only)
    added = [s for s in img.sections if s[0] in added_sections]
    feats = []
    for n, (rva, size, blk, name) in enumerate(funcs):
        length = blk if blk > 0 else size
        code = img.mem[rva:rva + length]
        masked = bytearray(code)
        shape, callees, strs, ext = [], [], [], set()
        salt = []
        end = rva + length
        for ins in md.disasm(code, rva):
            off = ins.address - rva
            if ins.id == 0:  # skipdata
                shape.append(".byte")
                continue
            ops = ins.operands
            tok = sys.intern(ins.mnemonic + " " + ",".join(_optype(o) for o in ops))
            tokens[tok] = tokens.get(tok, 0) + 1
            shape.append(tok)
            groups = ins.groups
            if GRP_BRANCH_REL in groups and ins.imm_size:
                tgt = ops[0].imm & 0xFFFFFFFF if ops else None
                if tgt is not None and not (rva <= tgt < end):
                    if ins.imm_size == 4:
                        masked[off + ins.imm_offset: off + ins.imm_offset + 4] = b"\0\0\0\0"
                    callees.append(tgt)
                    for sname, slo, shi, _ in added:
                        if slo <= tgt < shi:
                            ext.add(sname)
                continue
            for field_off, field_size in ((ins.disp_offset, ins.disp_size), (ins.imm_offset, ins.imm_size)):
                if field_size != 4:
                    continue
                p = off + field_off
                val = struct.unpack_from("<I", code, p)[0] if p + 4 <= len(code) else 0
                if lo_va <= val < hi_va:
                    masked[p:p + 4] = b"\0\0\0\0"
                    tr = val - base
                    s = img.string_at(tr)
                    if s is not None:
                        strs.append(s)
                        salt.append(salt_string(s))
                    for sname, slo, shi, _ in added:
                        if slo <= tr < shi:
                            ext.add(sname)
        h = hashlib.sha1(bytes(masked))
        h.update("\x00".join(salt).encode("utf-8", "replace"))
        feats.append({
            "rva": rva, "size": size, "blk": length, "name": name,
            "mhash": h.digest()[:12],
            "shash": hashlib.sha1("|".join(shape).encode()).digest()[:12],
            "shape": shape, "callees": callees, "strs": strs, "ext": sorted(ext),
        })
        if log_every and n % log_every == 0:
            print(f"  features {n}/{len(funcs)}", flush=True)
    return feats, tokens


def cached_features(img, funcs, cache_path, added_sections=()):
    key = (FEATURE_VERSION, img.md5, len(funcs), funcs[0][:3] if funcs else None,
           funcs[-1][:3] if funcs else None, tuple(added_sections))
    if cache_path and os.path.exists(cache_path):
        with open(cache_path, "rb") as fh:
            data = pickle.load(fh)
        if data.get("key") == key:
            return data["feats"], data["tokens"]
    feats, tokens = extract_features(img, funcs, added_sections, log_every=20000)
    if cache_path:
        with open(cache_path, "wb") as fh:
            pickle.dump({"key": key, "feats": feats, "tokens": tokens}, fh, protocol=4)
    return feats, tokens
