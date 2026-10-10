"""The update phase (the scheduler vector) of every registered update module of the RotWK binary (lane IDLE-1 r2).

TARGET FACTS (RW addresses, caveat S-001):
  0x62BD6A  registerObject's module loop: for each behavior b, U = (b + 0xC)->vslot 0x24 (getUpdate); the UpdateModule base is U - 0x10;
            a module that does not sleep forever goes to updates[base->vslot 0x30()] (GameLogic + 0xBC + 12 * phase).
  0x62E982  the scheduler: updates[0] in phases 3 / 4, updates[1] then updates[2] in phase 5, updates[3] in phase 6; per module the
            UpdateModuleInterface at base + 0x10 is called (slot 0: update).
  The vslot 0x30 bodies are constants: RW 0x851E97 `xor eax, eax; ret` (0), RW 0x490AC4 `xor eax, eax; inc eax; ret` (1), the default
  `push 2; pop eax; ret` (2) and RW 0x8311B1 `push 3; pop eax; ret` (3).
Method: each class's constructor (the call after the operator-new wrapper in its createProc, module_registry) stores its vtables into
`this`; the vtable stored at the UpdateModule base (the offset whose + 0x10 also receives a vtable) answers vslot 0x30. Classes whose
base cannot be found that way are listed as unresolved, never guessed.
"""
from __future__ import annotations

import sys
from pathlib import Path

from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG

sys.path.insert(0, str(Path(__file__).resolve().parent))

import module_registry as mr  # noqa: E402

PHASE_BODIES = (("xor eax, eax;inc eax;ret", 1), ("xor eax, eax;ret", 0), ("push 1;pop eax;ret", 1), ("push 2;pop eax;ret", 2),
                ("push 3;pop eax;ret", 3))
# the helpers the Object constructor RW 0x69990F makes (not in the ModuleFactory registry): their constructors
HELPERS = (("SMCHelper", 0x69300C), ("RecoveryHelper", 0x68D04E), ("RepulsorHelper", 0x68D109), ("DefectionHelper", 0x68CFAA),
           ("GuardingHelper", 0x8E37C2), ("WeaponStatusHelper", 0x68D19A), ("FiringTrackerHelper", 0x8E2EB2))


def phase_body(img, f):
    t = ";".join(f"{i.mnemonic} {i.op_str}" for i in img.decode(f, 8)[:3])
    for prefix, phase in PHASE_BODIES:
        if t.startswith(prefix):
            return phase
    return None


def constructor(img, create):
    seen, _, _ = img.reach(create)
    saw_new = False
    for i in (seen[a] for a in sorted(seen)):
        if i.mnemonic == "call" and i.operands[0].type == X86_OP_IMM:
            t = i.operands[0].imm
            if t == mr.NEW_OPERATOR:
                saw_new = True
            elif saw_new and t != mr.SEH_PROLOG:
                return t
    return None


def vtable_stores(img, f):
    """{offset: vtable} of the `mov dword ptr [r + d], vtable` stores of a constructor where r holds `this` + an offset (a plain copy of
    ecx, or `lea r, [this + k]`); the last store per offset wins"""
    seen, _, _ = img.reach(f)
    this = {"ecx": 0}  # register -> offset from `this`
    out = {}
    for i in (seen[a] for a in sorted(seen)):
        ops = i.operands
        if i.mnemonic == "call":
            if not (ops[0].type == X86_OP_IMM and ops[0].imm == mr.SEH_PROLOG):  # the SEH prolog keeps ecx
                for r in ("eax", "ecx", "edx"):
                    this.pop(r, None)
            continue
        if not ops or ops[0].type != X86_OP_REG:
            if i.mnemonic == "mov" and len(ops) == 2 and ops[0].type == X86_OP_MEM and ops[1].type == X86_OP_IMM:
                m = ops[0].mem
                v = ops[1].imm & 0xFFFFFFFF
                if m.index == 0 and m.base and i.reg_name(m.base) in this and img.in_data(v) and img.in_text(img.u32(v)):
                    out[this[i.reg_name(m.base)] + m.disp] = v
            continue
        dst = i.reg_name(ops[0].reg)
        if i.mnemonic == "mov" and len(ops) == 2 and ops[1].type == X86_OP_REG and i.reg_name(ops[1].reg) in this:
            this[dst] = this[i.reg_name(ops[1].reg)]
        elif i.mnemonic == "lea" and ops[1].type == X86_OP_MEM and ops[1].mem.index == 0 and ops[1].mem.base and i.reg_name(ops[1].mem.base) in this:
            this[dst] = this[i.reg_name(ops[1].mem.base)] + ops[1].mem.disp
        elif i.mnemonic not in ("cmp", "test", "push"):
            this.pop(dst, None)
    return out


def phase_of_constructor(img, ctor):
    stores = vtable_stores(img, ctor)
    found = set()
    for off, vt in stores.items():
        if off + 0x10 in stores:
            p = phase_body(img, img.u32(vt + 0x30))
            if p is not None:
                found.add((off, p))
    # the base at the lowest offset with an interface at + 0x10 is the UpdateModule (the later ones are the interface vtables themselves)
    return min(found)[1] if found else None


def update_phases(img):
    """{class name: phase or None (unresolved)} for every registered class with the update interface (mask bit 0) and the helpers"""
    classes, _, _ = mr.build_registry(img)
    out = {}
    for c in classes:
        if not (c["mask"] & 1):
            continue
        ctor = constructor(img, c["create"])
        out[c["name"]] = phase_of_constructor(img, ctor) if ctor else None
    for name, ctor in HELPERS:
        out[name] = phase_of_constructor(img, ctor)
    return out


if __name__ == "__main__":
    from rwimage import Image

    phases = update_phases(Image())
    for name, p in sorted(phases.items(), key=lambda kv: (str(kv[1]), kv[0])):
        print(p, name)
