"""The ModuleFactory registry of the RotWK binary: every addModule call, with its interface mask.

TARGET FACTS (RW addresses, caveat S-001):
  0x6570FE  ModuleFactory::addModule(createProc, createDataProc, extraProc, type, name, mask)
            __thiscall, ret 0x18. The call sites push right to left, so a site reads
            push mask / push &name / push type / push extra / push createData / push create.
            The key is makeDecoratedNameKey(name, type) = NAMEKEY('0' + type + name) (RW 0x655A0C).
  0x4374E0, 0x40611E  AsciiString::AsciiString(const char *) (how the registered name is built)
  330 call sites register 329 classes (WeaponBonusUpgrade twice, with identical arguments).
The sites are straight-line code in a few huge registration functions, so each site is evaluated
with a small register/stack simulation. An argument the simulation cannot resolve is an error.

Predicates read from the ModuleData vtable of each class (RW 0x73F496 and 0x73F4A9 call slots 4
(+0x10) and 7 (+0x1C) of the data object parseModuleName has just built): each slot resolves to a
function that is exactly `xor al,al; ret` or `mov al,1; ret`, which is read as false / true.
"""
from __future__ import annotations

import struct

from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG

from rwimage import ImageError
from rwtables import Tables

ADD_MODULE = 0x6570FE
STRING_CTORS = (0x4374E0, 0x40611E)
NEW_OPERATOR = 0x42F6E0
SEH_PROLOG = 0xA3CEF0
EXPECTED_SITES = 330
EXPECTED_CLASSES = 329


def _callers(img, target):
    out = []
    blob_start, blob_end = img.text
    o = img.off(blob_start)
    blob = img.data[o:o + (blob_end - blob_start)]
    i = 0
    while True:
        i = blob.find(b"\xe8", i)
        if i < 0 or i + 5 > len(blob):
            break
        rel = struct.unpack_from("<i", blob, i + 1)[0]
        va = blob_start + i
        if (va + 5 + rel) & 0xFFFFFFFF == target:
            out.append(va)
        i += 1
    return out


def _cluster(sites):
    sites = sorted(sites)
    out = [[sites[0]]]
    for s in sites[1:]:
        if s - out[-1][-1] > 0x600:
            out.append([s])
        else:
            out[-1].append(s)
    return out


def _decode_region(img, start, end):
    o = img.off(start)
    return list(img.md.disasm(img.data[o:o + (end - start)], start))


def _simulate(img, instrs):
    regs: dict[str, int | None] = {}
    stack: list[int | None] = []
    name = None
    results = []
    for i in instrs:
        m = i.mnemonic
        ops = i.operands
        if m == "push":
            o = ops[0]
            if o.type == X86_OP_IMM:
                stack.append(o.imm & 0xFFFFFFFF)
            elif o.type == X86_OP_REG:
                stack.append(regs.get(i.reg_name(o.reg)))
            else:
                stack.append(None)
        elif m == "pop" and ops[0].type == X86_OP_REG:
            regs[i.reg_name(ops[0].reg)] = stack.pop() if stack else None
        elif m == "mov" and len(ops) == 2 and ops[0].type == X86_OP_REG:
            r = i.reg_name(ops[0].reg)
            if ops[1].type == X86_OP_IMM:
                regs[r] = ops[1].imm & 0xFFFFFFFF
            elif ops[1].type == X86_OP_REG:
                regs[r] = regs.get(i.reg_name(ops[1].reg))
            else:
                regs[r] = None
        elif m == "xor" and len(ops) == 2 and ops[0].type == X86_OP_REG and ops[1].type == X86_OP_REG and ops[0].reg == ops[1].reg:
            regs[i.reg_name(ops[0].reg)] = 0
        elif m == "inc" and ops[0].type == X86_OP_REG:
            r = i.reg_name(ops[0].reg)
            regs[r] = (regs[r] + 1) & 0xFFFFFFFF if isinstance(regs.get(r), int) else None
        elif m == "call":
            tgt = ops[0].imm if ops[0].type == X86_OP_IMM else None
            if tgt in STRING_CTORS:
                # code before the first registration in the decoded window may build other strings;
                # a registration site must see a fresh name (checked below), so a ctor without an
                # immediate string just forgets the name
                name = img.cstr(stack[-1]) if stack and stack[-1] is not None and img.in_data(stack[-1]) else None
                stack = []
            elif tgt == ADD_MODULE:
                last = stack[-6:]
                if len(last) != 6 or any(v is None for k, v in enumerate(last) if k != 1):
                    raise ImageError(f"addModule site {i.address:#x}: unresolved arguments {last}")
                if name is None:
                    raise ImageError(f"addModule site {i.address:#x}: no class name")
                mask, _nameptr, mtype, extra, create_data, create = last
                results.append({"site": i.address, "name": name, "mask": mask, "type": mtype, "extra": extra, "createData": create_data, "create": create})
                stack = []
                name = None
            else:
                stack = []
                for r in ("eax", "ecx", "edx"):
                    regs.pop(r, None)
        elif ops and ops[0].type == X86_OP_REG and m not in ("cmp", "test"):
            regs.pop(i.reg_name(ops[0].reg), None)
    return results


def _function_start(img, first_site):
    """Start of the registration function holding first_site: the nearest preceding SEH prolog
    `mov eax, imm32 ; call 0xA3CEF0` (RW 0xA3CEF0), found by scanning back byte by byte."""
    for back in range(0x10, 0x2000):
        a = first_site - back
        i = img.decode(a, 10)
        if len(i) >= 2 and i[0].mnemonic == "mov" and i[0].op_str.startswith("eax, 0x") and i[0].size == 5 and i[1].mnemonic == "call" \
                and i[1].operands[0].type == X86_OP_IMM and i[1].operands[0].imm == SEH_PROLOG:
            return a
    raise ImageError(f"no function start before {first_site:#x}")


def extract_sites(img):
    """(results sorted by address, execution sequence of site addresses).

    The sites sit in a few straight-line registration functions. One of them calls another before its own
    sites (RW 0x464AD2: `call 0x6579C9` ahead of the draw registrations), so execution order is NOT address
    order, and it decides the NAMEKEY ids ModuleFactory hands out. The sequence replays each function in
    code order, expanding a call to another registration function at the point of the call."""
    sites = _callers(img, ADD_MODULE)
    results = []
    clusters = []
    for cluster in _cluster(sites):
        first, last = cluster[0], cluster[-1]
        found = None
        for back in range(0x40, 0x800):
            s = first - back
            ins = _decode_region(img, s, last + 0x10)
            addrs = {i.address for i in ins}
            if all(c in addrs for c in cluster):
                found = ins
                break
        if found is None:
            raise ImageError(f"cannot align the addModule sites at {first:#x}")
        res = _simulate(img, found)
        got = {r["site"] for r in res}
        missing = [hex(c) for c in cluster if c not in got]
        if missing:
            raise ImageError(f"addModule sites not recovered: {missing}")
        results.extend(res)
        clusters.append({"start": _function_start(img, first), "ins": found, "sites": set(cluster)})
    results.sort(key=lambda r: r["site"])
    if len(results) != EXPECTED_SITES:
        raise ImageError(f"expected {EXPECTED_SITES} addModule sites, found {len(results)}")

    by_start = {c["start"]: c for c in clusters}
    called = set()
    for c in clusters:
        for i in c["ins"]:
            if i.mnemonic == "call" and i.operands[0].type == X86_OP_IMM and i.operands[0].imm in by_start and i.operands[0].imm != c["start"]:
                called.add(i.operands[0].imm)
    roots = [c for c in clusters if c["start"] not in called]
    if len(roots) != 1:
        raise ImageError(f"expected one root registration function, found {[hex(c['start']) for c in roots]}")

    sequence = []

    def replay(c):
        for i in c["ins"]:
            if i.address < c["start"]:
                continue  # the decode window may begin inside the previous function
            if i.mnemonic == "call" and i.operands[0].type == X86_OP_IMM:
                t = i.operands[0].imm
                if t in by_start and t != c["start"]:
                    replay(by_start[t])
                elif t == ADD_MODULE and i.address in c["sites"]:
                    sequence.append(i.address)

    replay(roots[0])
    if sorted(sequence) != [r["site"] for r in results]:
        raise ImageError("the registration sequence does not cover every addModule site exactly once")
    return results, sequence


def _first_vtable_store(img, ins_list):
    """`mov dword ptr [reg], <vtable>` with the vtable in .rdata: the vtable assigned at offset 0."""
    last = None
    for i in ins_list:
        if i.mnemonic == "mov" and len(i.operands) == 2 and i.operands[0].type == X86_OP_MEM and i.operands[1].type == X86_OP_IMM:
            mm = i.operands[0].mem
            v = i.operands[1].imm & 0xFFFFFFFF
            if mm.disp == 0 and mm.index == 0 and img.in_data(v) and img.in_text(img.u32(v)):
                last = v
    return last


def data_vtable(img, create_data):
    """The vtable of the ModuleData a createData proc builds: stored inline by the proc, or by the
    constructor it calls right after the operator-new wrapper (RW 0x42F6E0)."""
    seen, calls, tails = img.reach(create_data)
    ins_list = [seen[a] for a in sorted(seen)]
    v = _first_vtable_store(img, ins_list)
    if v:
        return v
    saw_new = False
    for i in ins_list:
        if i.mnemonic == "call" and i.operands[0].type == X86_OP_IMM:
            t = i.operands[0].imm
            if t == NEW_OPERATOR:
                saw_new = True
                continue
            if saw_new and t != SEH_PROLOG:
                cseen, ccalls, ctails = img.reach(t)
                v = _first_vtable_store(img, [cseen[a] for a in sorted(cseen)])
                if v:
                    return v
                break
    raise ImageError(f"createData {create_data:#x}: ModuleData vtable not found")


def slot_predicate(img, vtable, slot):
    f = img.u32(vtable + 4 * slot)
    ins = img.decode(f, 8)
    text = ";".join(f"{i.mnemonic} {i.op_str}" for i in ins[:2])
    if text.startswith("xor al, al;ret"):
        return False, f
    if text.startswith("mov al, 1;ret"):
        return True, f
    raise ImageError(f"vtable {vtable:#x} slot {slot} ({f:#x}) is not a constant predicate: {text}")


def build_registry(img):
    sites, sequence = extract_sites(img)
    tables = Tables(img)
    classes = {}
    for s in sites:
        key = (s["name"], s["type"])
        if key in classes:
            old = classes[key]
            if any(old[k] != s[k] for k in ("mask", "extra", "createData", "create")):
                raise ImageError(f"{s['name']} registered twice with different arguments")
            old["sites"].append(s["site"])
            continue
        vt = data_vtable(img, s["createData"])
        ai, ai_fn = slot_predicate(img, vt, 4)
        slot7, slot7_fn = slot_predicate(img, vt, 7)
        classes[key] = {
            "name": s["name"], "type": s["type"], "mask": s["mask"], "extra": s["extra"],
            "createData": s["createData"], "create": s["create"], "sites": [s["site"]],
            "vtable": vt, "isAiModuleData": ai, "slot7": slot7,
            "tableRefs": tables.class_tables(s["createData"]),
        }
    names = {k[0] for k in classes}
    if len(names) != EXPECTED_CLASSES:
        raise ImageError(f"expected {EXPECTED_CLASSES} classes, found {len(names)}")
    by_site = {x["site"]: x for x in sites}
    seq = [{"site": a, "name": by_site[a]["name"], "type": by_site[a]["type"]} for a in sequence]
    return sorted(classes.values(), key=lambda c: (c["type"], c["name"])), sites, seq
