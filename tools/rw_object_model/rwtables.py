"""Field tables, module build procs and nested-block grammar of the RotWK binary.

RotWK keeps every INI field table as rows {char *token, parseProc, userData, offset} (16 bytes,
terminated by a row whose token is NULL; spec ini-and-object-model.md 4.3). This module reads
them out of the image and works out, for every parse function reachable from a table, whether it
consumes only the rest of its line or opens a nested block, and which tables that block uses:

  line    the function never reaches INI::initFromINI*; it reads tokens from the current line only
  block   it reaches INI::initFromINI / initFromINIMulti / initFromINIMultiProc and so consumes
          lines up to a matching End (the nested table is recovered from the call site)
  script  it reaches the BeginScript reader (RW 0x42D400), which consumes lines up to ENDSCRIPT

Binary facts used (RW addresses, caveat S-001), each read from the disassembly:
  0x42B8D7  MultiIniFieldParse::add(table, extra)       ret 8; table[count]=arg1, extra[count]=arg2
  0x42DB80  INI::initFromINI(what, table)                builds a MultiIniFieldParse, add(table,0), calls 0x42D4B0
  0x42D4B0  INI::initFromINIMulti(what, multi)           the End / field loop (messages at 0xBD3E98...)
  0x42DBBD  INI::initFromINIMultiProc(what, buildProc)   builds a MultiIniFieldParse via buildProc, calls 0x42D4B0
  0x42D400  script block reader                          readLine loop until the first token equals ENDSCRIPT
"""
from __future__ import annotations

import functools
import struct

from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG

ADD = 0x42B8D7
INI_FROM = 0x42DB80
INI_MULTI = 0x42D4B0
INI_PROC = 0x42DBBD
SCRIPT_READ = 0x42D400
SINKS = {INI_FROM, INI_MULTI, INI_PROC}
# helpers that never lead to a nested block: SEH prolog/epilog, throw, AsciiString ctor/dtor, token readers
from rwimage import NORETURN_CALLS  # noqa: E402
STOP_CALLS = {0xA3CEF0, 0xA3CE04, 0x42F3C1, 0x435D50, 0x4374E0, 0x42DC9F, ADD, 0x42B710}


class GrammarError(RuntimeError):
    pass


class Tables:
    def __init__(self, img):
        self.img = img

    # -- plain tables ----------------------------------------------------------------------
    def ident(self, va: int):
        if not self.img.in_data(va):
            return None
        try:
            s = self.img.cstr(va, 80)
        except Exception:
            return None
        if not s or len(s) > 70 or not all(32 < ord(c) < 127 for c in s):
            return None
        return s

    def read_table(self, va: int, patches: dict | None = None):
        """Rows [(name, fn, userData, offset)] of the table at va, stopping at the NULL-token row.
        Some retail tables have no terminator row (the next object follows the last row, e.g. the
        W3DLightDraw table): rows stop when a token is not an identifier string or the function
        is not in .text. Returns (rows, terminator) where terminator is (fn, user, offset) of the
        NULL-token row (a non-zero fn is a catch-all row) or None when unterminated.
        `patches` maps absolute VAs to values written by lazy-initialisation code."""
        if va == 0:
            return [], (0, 0, 0)
        img = self.img
        patches = patches or {}

        def word(a):
            return patches[a] if a in patches else img.u32(a)

        rows = []
        while True:
            t, f, u, o = (word(va + 4 * k) for k in range(4))
            if t == 0:
                return rows, (f, u, o)
            nm = self.ident(t)
            if nm is None or not img.in_text(f):
                return rows, None
            rows.append((nm, f, u, o))
            va += 16

    # -- build procs -----------------------------------------------------------------------
    def getter_value(self, c: int):
        """A function of the form `mov eax, imm; ret` returns that constant (a table getter)."""
        I = self.img.decode(c, 12)
        if len(I) >= 2 and I[0].mnemonic == "mov" and I[0].op_str.startswith("eax, 0x") and I[1].mnemonic == "ret":
            return I[0].operands[1].imm & 0xFFFFFFFF
        return None

    def build_proc_tables(self, p: int, depth: int = 4):
        """[(tableVA, extraOffset, originFn)] that the build proc p adds with MultiIniFieldParse::add.
        Arguments are pushed right to left: add(table, extra) pushes extra first."""
        img = self.img
        seen, calls, tails = img.reach(p)
        out = []
        stack = []
        regs = {}

        def val(op, i):
            if op.type == X86_OP_IMM:
                return op.imm & 0xFFFFFFFF
            if op.type == X86_OP_REG:
                return regs.get(i.reg_name(op.reg))
            return None

        for a in sorted(seen):
            i = seen[a]
            m = i.mnemonic
            ops = i.operands
            if m == "push":
                stack.append(val(ops[0], i))
            elif m == "pop" and ops[0].type == X86_OP_REG:
                regs[i.reg_name(ops[0].reg)] = stack.pop() if stack else None
            elif m == "mov" and ops[0].type == X86_OP_REG:
                regs[i.reg_name(ops[0].reg)] = val(ops[1], i)
            elif m == "xor" and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_REG and ops[0].reg == ops[1].reg:
                regs[i.reg_name(ops[0].reg)] = 0
            elif m in ("call", "jmp") and ops[0].type == X86_OP_IMM:
                t = ops[0].imm
                if m == "jmp" and t in seen:
                    continue
                if t == ADD:
                    f = stack.pop() if stack else None
                    e = stack.pop() if stack else None
                    out.append((f, e, p))
                    continue
                g = self.getter_value(t)
                if g is not None:
                    regs["eax"] = g  # getters take no arguments and pop nothing: the caller's pushes stay
                    continue
                if img.in_text(t) and t not in STOP_CALLS and depth > 0:
                    out.extend(self.build_proc_tables(t, depth - 1))
                for r in ("eax", "ecx", "edx"):
                    regs.pop(r, None)
                stack.clear()  # any other call consumed (or cleaned up) the pushes before it
            elif ops and ops[0].type == X86_OP_REG and m not in ("cmp", "test"):
                regs.pop(i.reg_name(ops[0].reg), None)
        for f, e, _origin in out:
            if f is None or e is None:
                raise GrammarError(f"build proc {p:#x}: add() with an argument the walker could not resolve")
        return out

    def class_tables(self, create_data: int):
        """Tables a module class parses its body with: (tableVA, extra) list, from its createData proc.
        createData(ini) news the data and, when ini is non-NULL, calls initFromINIMultiProc(data, buildProc)
        (or initFromINI(data, table) for the base ModuleData)."""
        seen, calls, tails = self.img.reach(create_data)
        I = [seen[a] for a in sorted(seen)]
        res = []
        for k, i in enumerate(I):
            if i.mnemonic == "call" and i.operands[0].type == X86_OP_IMM and i.operands[0].imm in (INI_PROC, INI_FROM):
                t = i.operands[0].imm
                pushes = []
                j = k - 1
                while j >= 0 and len(pushes) < 2 and k - j < 8:
                    if I[j].mnemonic == "push":
                        pushes.append(I[j])
                    j -= 1
                if len(pushes) < 2 or pushes[1].operands[0].type != X86_OP_IMM:
                    raise GrammarError(f"createData {create_data:#x}: unresolved initFromINI argument")
                arg = pushes[1].operands[0].imm & 0xFFFFFFFF
                res.append([(arg, 0, create_data)] if t == INI_FROM else self.build_proc_tables(arg))
        if len(res) != 1:
            raise GrammarError(f"createData {create_data:#x}: expected one initFromINI call, found {len(res)}")
        return res[0]

    # -- parse-function classification -----------------------------------------------------
    @functools.lru_cache(maxsize=None)
    def reaches(self, f: int, depth: int = 6, want_script: bool = False):
        seen, calls, tails = self.img.reach(f)
        target = {SCRIPT_READ} if want_script else SINKS
        if any(c in target for c in calls):
            return True
        if depth > 0:
            for c in calls:
                if c in SINKS or c == SCRIPT_READ or c in STOP_CALLS or not self.img.in_text(c):
                    continue
                if self.reaches(c, depth - 1, want_script):
                    return True
        return False

    def _sites(self, f, depth, seen, out):
        if f in seen:
            return
        seen.add(f)
        img = self.img
        s, calls, tails = img.reach(f)
        addrs = sorted(s)
        for k, a in enumerate(addrs):
            i = s[a]
            if i.mnemonic == "call" and i.operands[0].type == X86_OP_IMM and i.operands[0].imm in SINKS:
                out.append((f, a, i.operands[0].imm, addrs, s))
        if depth > 0:
            for c in calls:
                if c in SINKS or c in STOP_CALLS or c == SCRIPT_READ or not img.in_text(c):
                    continue
                if self.reaches(c):
                    self._sites(c, depth - 1, seen, out)

    def _arg_pushes(self, addrs, s, a):
        """Operands of the (up to) four pushes before the call at a, nearest first."""
        k = addrs.index(a)
        out = []
        j = k - 1
        while j >= 0 and len(out) < 4 and a - addrs[j] < 0x60:
            p = s[addrs[j]]
            if p.mnemonic == "push":
                o = p.operands[0]
                out.append(("imm", o.imm & 0xFFFFFFFF) if o.type == X86_OP_IMM else ("reg", p.reg_name(o.reg)) if o.type == X86_OP_REG else ("op", p.op_str))
            j -= 1
        return out

    def _stack_table(self, fn_start, addrs, s, a, reg):
        """Rows of a table built on the stack: find `lea reg, [ebp+d]` before the call and the
        `mov dword ptr [ebp+k], imm|reg` stores into that frame region."""
        base = None
        regs = {}
        stores = {}
        for x in addrs:
            if x >= a:
                break
            i = s[x]
            m = i.mnemonic
            ops = i.operands
            if m == "lea" and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_MEM and ops[1].mem.base != 0 and i.reg_name(ops[1].mem.base) == "ebp":
                if i.reg_name(ops[0].reg) == reg:
                    base = ops[1].mem.disp
            elif m == "mov" and ops[0].type == X86_OP_REG:
                r = i.reg_name(ops[0].reg)
                if ops[1].type == X86_OP_IMM:
                    regs[r] = ops[1].imm & 0xFFFFFFFF
                elif ops[1].type == X86_OP_REG:
                    regs[r] = regs.get(i.reg_name(ops[1].reg))
                else:
                    regs.pop(r, None)
            elif m == "xor" and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_REG and ops[0].reg == ops[1].reg:
                regs[i.reg_name(ops[0].reg)] = 0
            elif m == "mov" and ops[0].type == X86_OP_MEM and ops[0].mem.base != 0 and i.reg_name(ops[0].mem.base) == "ebp" and ops[0].mem.index == 0:
                v = ops[1].imm & 0xFFFFFFFF if ops[1].type == X86_OP_IMM else regs.get(i.reg_name(ops[1].reg)) if ops[1].type == X86_OP_REG else None
                stores[ops[0].mem.disp] = v
            elif m in ("call", "lea") or (ops and ops[0].type == X86_OP_REG and m not in ("cmp", "test", "push")):
                if ops and ops[0].type == X86_OP_REG:
                    regs.pop(i.reg_name(ops[0].reg), None)
        if base is None:
            return None
        rows = []
        d = base
        while True:
            w = [stores.get(d + 4 * k) for k in range(4)]
            if w[0] is None:
                return None
            if w[0] == 0:
                return rows
            nm = self.ident(w[0])
            if nm is None or w[1] is None:
                return None
            rows.append((nm, w[1], w[2] or 0, w[3] or 0))
            d += 16

    def _patches_for(self, table_va, fn):
        """Absolute stores `mov [abs], imm|reg` in fn's reach that land inside the table (lazy init)."""
        seen, calls, tails = self.img.reach(fn)
        regs = {}
        patches = {}
        for a in sorted(seen):
            i = seen[a]
            m = i.mnemonic
            ops = i.operands
            if m == "mov" and ops[0].type == X86_OP_REG:
                r = i.reg_name(ops[0].reg)
                if ops[1].type == X86_OP_IMM:
                    regs[r] = ops[1].imm & 0xFFFFFFFF
                elif ops[1].type == X86_OP_REG:
                    regs[r] = regs.get(i.reg_name(ops[1].reg))
                else:
                    regs.pop(r, None)
            elif m == "xor" and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_REG and ops[0].reg == ops[1].reg:
                regs[i.reg_name(ops[0].reg)] = 0
            elif m == "mov" and ops[0].type == X86_OP_MEM and ops[0].mem.base == 0 and ops[0].mem.index == 0:
                d = ops[0].mem.disp & 0xFFFFFFFF
                if table_va <= d < table_va + 0x400:
                    v = ops[1].imm & 0xFFFFFFFF if ops[1].type == X86_OP_IMM else regs.get(i.reg_name(ops[1].reg)) if ops[1].type == X86_OP_REG else None
                    patches[d] = v
        return patches

    def conditional_exits(self, fn: int):
        """Return from fn without crossing a call that opens a block? Walks the function's control
        flow and stops at every call to an initFromINI* sink or to a function that reaches one. A
        `ret` still reachable means the block is opened only on some paths (e.g. RW 0x73B723, which
        opens one only when its first token does not scan as a float). Returns the list of such ret
        addresses (empty: the block is opened on every path)."""
        img = self.img
        seen, calls, tails = img.reach(fn)
        rets = []
        visited = set()
        work = [fn]
        while work:
            a = work.pop()
            while a in seen and a not in visited:
                visited.add(a)
                i = seen[a]
                m = i.mnemonic
                if m == "ret":
                    rets.append(a)
                    break
                if m == "call" and i.operands[0].type == X86_OP_IMM:
                    t = i.operands[0].imm
                    if t in NORETURN_CALLS or t in SINKS or (t not in STOP_CALLS and img.in_text(t) and self.reaches(t)):
                        break
                    a += i.size
                    continue
                if m == "jmp":
                    op = i.operands[0]
                    if op.type == X86_OP_IMM:
                        t = op.imm
                        if t in SINKS or t not in seen:
                            if t in SINKS or (img.in_text(t) and t not in STOP_CALLS and self.reaches(t)):
                                break
                            rets.append(a)  # tail jump to a function that does not open a block
                            break
                        a = t
                        continue
                    break
                if m.startswith("j"):
                    if i.operands[0].type == X86_OP_IMM:
                        work.append(i.operands[0].imm)
                    a += i.size
                    continue
                a += i.size
        return rets

    @functools.lru_cache(maxsize=None)
    def fn_info(self, fn: int):
        """{'kind': 'line'|'block'|'script', 'tables': [...], 'resolved': bool, 'notes': [...]}.
        Each entry of 'tables' is {'extra': int, 'rows': [...]} for an inline (stack or lazily
        patched) table or {'va': tableVA, 'extra': int} for a static one."""
        if self.reaches(fn, 6, True):
            return {"kind": "script", "terminator": "ENDSCRIPT"}
        if not self.reaches(fn):
            return {"kind": "line"}
        sites = []
        self._sites(fn, 4, set(), sites)
        tables = []
        notes = []
        resolved = True

        def add(entry):
            if entry not in tables:
                tables.append(entry)

        for (sf, a, tgt, addrs, s) in sites:
            pushes = self._arg_pushes(addrs, s, a)
            if tgt in (INI_FROM, INI_PROC):
                p = pushes[1] if len(pushes) > 1 else None
                if p is None:
                    resolved = False
                    notes.append(f"site {a:#x}: no table argument")
                elif p[0] == "imm":
                    if tgt == INI_FROM:
                        add({"va": p[1], "extra": 0, "origin": sf})
                    else:
                        for t, e, o in self.build_proc_tables(p[1]):
                            add({"va": t, "extra": e, "origin": o})
                elif p[0] == "reg":
                    rows = self._stack_table(sf, addrs, s, a, p[1]) if tgt == INI_FROM else None
                    if rows is None:
                        resolved = False
                        notes.append(f"site {a:#x}: table in register {p[1]} not decoded")
                    else:
                        add({"extra": 0, "rows": rows, "stack": True})
                else:
                    resolved = False
                    notes.append(f"site {a:#x}: table operand {p[1]}")
            else:  # INI_MULTI: MultiIniFieldParse assembled in the site's function
                tl = self.build_proc_tables(sf)
                if tl:
                    for t, e, o in tl:
                        add({"va": t, "extra": e, "origin": o})
                else:
                    resolved = False
                    notes.append(f"site {a:#x}: initFromINIMulti with a table set built elsewhere")
        if not tables:
            resolved = False
        info = {"kind": "block", "tables": tables, "resolved": resolved, "notes": notes}
        cond = self.conditional_exits(fn)
        if cond:
            info["conditional"] = True
            info["notes"] = notes + [f"conditional: the block is opened only on some paths (ret at {cond[0]:#x})"]
        return info
