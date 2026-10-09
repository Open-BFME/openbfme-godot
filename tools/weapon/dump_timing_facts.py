#!/usr/bin/env python3
"""WEAPON-1 research: binary facts of the retail Weapon timing state machine and range checks.

Reads the RotWK game.dat named by the RW_GAME_DAT environment variable AT RUN TIME (nothing retail is
stored in git) and asserts, by disassembly, every binary fact the weapon timing / range port relies on.
All addresses are VAs in the community-patched RotWK image (docs/STOPS.md S-001 applies to every fact here).

    RW_GAME_DAT=<path to game.dat> python3 tools/weapon/dump_timing_facts.py            # run every check
    RW_GAME_DAT=<path to game.dat> python3 tools/weapon/dump_timing_facts.py --list     # print the fact table
    python3 tools/weapon/dump_timing_facts.py --golden                                   # print the golden timing sequences (no game.dat)

Exit codes: 0 all checks passed, 1 a check failed, 77 skipped because RW_GAME_DAT is unset (prints SKIP loudly,
the same convention run_tests.bat uses). A test wrapper turns this into a 'binary-fact' test that skips loudly.

The second half of the file is a REFERENCE MODEL of the decoded state machine (Python, exact integer/PC24
arithmetic). It is NOT retail code and not an oracle: it is the decoded logic written down so golden sequences can
be derived statically and cross-checked by --golden / self_test(). The C++ port must reproduce them.
"""
from __future__ import annotations

import math
import os
import re
import struct
import sys
from fractions import Fraction
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / "tools" / "rw_object_model"))

LOGIC_FRAME_PTR = 0xDE412C      # TheGameLogic*; the logic frame counter is [TheGameLogic + 0x40]
GLOBAL_DATA_PTR = 0xDE4364      # TheGlobalData*; +0xAD0 WeaponBonusSet*, +0x1224/+0x11C4 floats, +0x1228 fixed seed
RNG_INT, RNG_REAL = 0x6D328E, 0x6D332C
WEAPON_CPP = 0xC18320           # "E:\Builds\BFME2X\...\GameLogic\Object\Weapon.cpp"
WEAPONSET_CPP = 0xC16C08

# --------------------------------------------------------------------------------------------------
# disassembly helpers
# --------------------------------------------------------------------------------------------------


class Fact:
    def __init__(self, topic, name, fn):
        self.topic, self.name, self.fn = topic, name, fn


FACTS: list[Fact] = []


def fact(topic):
    def deco(fn):
        FACTS.append(Fact(topic, fn.__name__, fn))
        return fn
    return deco


class Asm:
    """Control-flow reachable instructions of one function, as 'mnemonic op_str' text in address order."""

    def __init__(self, img, va, limit=0x2000):
        seen, calls, tails = img.reach(va, limit)
        self.va = va
        self.addrs = sorted(seen)
        self.lines = [f"{seen[a].mnemonic} {seen[a].op_str}".strip() for a in self.addrs]
        self.calls = calls

    def seq(self, *patterns, msg=""):
        """patterns appear in this order (not necessarily adjacent)."""
        i = 0
        for p in patterns:
            rx = re.compile(p)
            while i < len(self.lines) and not rx.search(self.lines[i]):
                i += 1
            assert i < len(self.lines), f"{msg} pattern not found in order: {p!r} in {self.va:#x}"
            i += 1

    def has(self, pattern, msg=""):
        rx = re.compile(pattern)
        assert any(rx.search(l) for l in self.lines), f"{msg} {pattern!r} not in {self.va:#x}"

    def count(self, pattern):
        rx = re.compile(pattern)
        return sum(1 for l in self.lines if rx.search(l))


def f32(img, va):
    return struct.unpack("<f", struct.pack("<I", img.u32(va)))[0]


def table(img, va, stride=16):
    rows = {}
    for i in range(512):
        a = va + i * stride
        tok, parse, user, off = (img.u32(a + 4 * k) for k in range(4))
        if tok == 0 and parse == 0 and user == 0 and off == 0:
            break
        rows[img.cstr(tok)] = (parse, user, off)
    return rows


def name_list(img, va, n=700):
    out = []
    for i in range(n):
        p = img.u32(va + 4 * i)
        if p == 0:
            break
        out.append(img.cstr(p))
    return out


# --------------------------------------------------------------------------------------------------
# 1. INI side: field table, min/max duration parsers, enum lists, template defaults
# --------------------------------------------------------------------------------------------------

WEAPON_TEMPLATE_FIELDS = 0xC16DD8
PARSE_DURATION_UINT = 0x73A429      # parseDurationUnsignedInt: fild, fmul 0.005f (PC24), fstp qword, ceil
PARSE_INT, PARSE_BOOL, PARSE_REAL, PARSE_ENUM = 0x42EC5E, 0x42E558, 0x42ED00, 0x42E956

EXPECT_FIELDS = {
    # name: (parser, offset)
    "AttackRange": (PARSE_REAL, 0x14), "MinimumAttackRange": (PARSE_REAL, 0x18),
    "RangeBonusMinHeight": (PARSE_REAL, 0x1C), "RangeBonus": (PARSE_REAL, 0x20), "RangeBonusPerFoot": (PARSE_REAL, 0x24),
    "RequestAssistRange": (PARSE_REAL, 0x28), "AcceptableAimDelta": (0x42EE15, 0x2C),
    "ScatterRadius": (PARSE_REAL, 0x34), "ScatterTargetScalar": (PARSE_REAL, 0x38),
    "DisableScatterForTargetsOnWall": (PARSE_BOOL, 0x3D), "CanBeDodged": (PARSE_BOOL, 0x75),
    "IdleAfterFiringDelay": (PARSE_DURATION_UINT, 0x78), "HoldAfterFiringDelay": (PARSE_DURATION_UINT, 0x7C),
    "HoldDuringReload": (PARSE_BOOL, 0x80), "MinTargetPitch": (0x42EE15, 0x88), "MaxTargetPitch": (0x42EE15, 0x8C),
    "ClipSize": (PARSE_INT, 0xE4), "ContinuousFireOne": (PARSE_INT, 0xF8), "ContinuousFireTwo": (PARSE_INT, 0xFC),
    "ContinuousFireCoast": (PARSE_DURATION_UINT, 0x100), "AutoReloadWhenIdle": (PARSE_DURATION_UINT, 0x104),
    "ShotsPerBarrel": (PARSE_INT, 0x108), "ProjectileFilterInContainer": (0x76392F, 0x120),
    "MeleeWeapon": (PARSE_BOOL, 0x125), "AutoReloadsClip": (PARSE_ENUM, 0x128), "PreAttackType": (PARSE_ENUM, 0x12C),
    "LeechRangeWeapon": (PARSE_BOOL, 0x130), "PreAttackDelay": (PARSE_DURATION_UINT, 0x138),
    "PreAttackRandomAmount": (PARSE_DURATION_UINT, 0x13C), "FiringDuration": (PARSE_DURATION_UINT, 0x144),
    "ContinueAttackRange": (PARSE_REAL, 0x148), "ScatterRadiusVsInfantry": (PARSE_REAL, 0x14C),
    "SuspendFXDelay": (PARSE_DURATION_UINT, 0x150), "HitPercentage": (0x42EEFA, 0x158),
    "HitPassengerPercentage": (0x42EEFA, 0x15C), "FinishAttackOnceStarted": (PARSE_BOOL, 0x161),
    "RestrictedHeightRange": (PARSE_REAL, 0x164), "RequireFollowThru": (PARSE_BOOL, 0x169),
    "ShareTimers": (PARSE_BOOL, 0x16A), "InstantLoadClipOnActivate": (PARSE_BOOL, 0x16E),
    "LockWhenUsing": (PARSE_BOOL, 0x16F), "FXTrigger": (PARSE_ENUM, 0x10),
    "DelayBetweenShots": (0x6C9D99, 0x0), "ClipReloadTime": (0x6C9E7A, 0x0),
}


@fact("INI")
def field_table_rows(img):
    rows = table(img, WEAPON_TEMPLATE_FIELDS)
    assert len(rows) == 124, len(rows)
    for name, (parse, off) in EXPECT_FIELDS.items():
        assert name in rows, name
        assert rows[name][0] == parse and rows[name][2] == off, (name, [hex(x) for x in rows[name]])
    assert rows["AutoReloadsClip"][1] == 0xDA16BC and rows["PreAttackType"][1] == 0xDA16CC


@fact("INI")
def enum_lists_and_defaults(img):
    assert name_list(img, 0xDA16CC) == ["PER_SHOT", "PER_ATTACK", "PER_CLIP", "PER_POSITION"]
    assert name_list(img, 0xDA16BC) == ["YES", "NO", "RETURN_TO_BASE"]
    bonus_fields = name_list(img, 0xDA179C)
    assert bonus_fields == ["DAMAGE", "RADIUS", "RANGE", "RATE_OF_FIRE", "PRE_ATTACK", "FIRING"], bonus_fields
    conds = name_list(img, 0xDA1740)
    assert len(conds) == 22 and conds[2] == "CONTINUOUS_FIRE_MEAN" and conds[3] == "CONTINUOUS_FIRE_FAST", conds
    disabled = name_list(img, 0xDAD904)
    assert disabled[0] == "DEFAULT" and disabled[3] == "DISABLED_HELD" and disabled[8] == "DISABLED_TEMPORARILY_BUSY", disabled
    attrs = name_list(img, 0xD8AF48)
    assert attrs[7] == "RANGE" and attrs[0x15] == "RATE_OF_FIRE", attrs
    status = name_list(img, 0xD8AFF0)
    for idx, nm in ((0x0D, "IS_FIRING_WEAPON"), (0x16, "IS_ATTACKING"), (0x19, "IS_AIMING_WEAPON"), (0x25, "CONTESTING_BUILDING"),
                    (0x26, "HORDE_MEMBER"), (0x3A, "INSIDE_GARRISON"), (0x4B, "RUNNING_DOWN_FROM_BEHIND")):
        assert status[idx] == nm, (idx, status[idx])
    kind = name_list(img, 0xDA0E68)
    assert kind[7] == "STRUCTURE" and kind[8] == "INFANTRY" and kind[2] == "IMMOBILE"
    # constants
    assert abs(f32(img, 0xD9F610) - 0.005) < 1e-9 and img.u32(0xD9F610) == 0x3BA3D70A
    assert f32(img, 0xBE5AE8) == 2.5 and f32(img, 0xBD83D8) == 10.0 and f32(img, 0xBDD388) > 3.14159 and f32(img, 0xBDD390) < -3.14159
    assert f32(img, 0xBDD3A4) == 999999.0 and img.u32(0xD9F608) == 5


@fact("INI")
def min_max_duration_parsers(img):
    # 0x6C9D99 DelayBetweenShots -> min +0xF0, max +0xF4 ; 0x6C9E7A ClipReloadTime -> min +0xE8, max +0xEC
    for fn, lo, hi, mn, mx in ((0x6C9D99, 0xF0, 0xF4, 0xDA1850, 0xDA184C), (0x6C9E7A, 0xE8, 0xEC, 0xDA1858, 0xDA1854)):
        a = Asm(img, fn, 0x200)
        a.has(rf"add edi, {lo:#x}")
        a.has(rf"add ebx, {hi:#x}")
        a.has(rf"push dword ptr \[{mn:#x}\]")          # "Min"
        a.has(rf"push dword ptr \[{mx:#x}\]")          # "Max"
        a.has(r"mov ebx, dword ptr \[0xbd051c\]"); a.has(r"call ebx")          # _strcmpi
        a.has(r"call 0x42e9d7")                        # INI::scanInt
        a.seq(r"fild dword ptr \[edi\]", r"fmul dword ptr \[0xd9f610\]", r"fstp qword ptr \[esp\]", r"call dword ptr \[0xbd0588\]",
              r"call 0xa3cfa4", r"fild dword ptr \[ebx\]", r"fmul dword ptr \[0xd9f610\]", r"call dword ptr \[0xbd0588\]", r"call 0xa3cfa4")
        assert img.cstr(img.u32(mn)) == "Min" and img.cstr(img.u32(mx)) == "Max"
        # form 'N' (no Min): min = max = N ; form 'Min:a Max:b'; a second token that is not Max makes max = min
        a.has(r"mov dword ptr \[edi\], eax")
    # no null-token guard before the second _strcmpi: 'Min:N' alone passes NULL to _strcmpi (retail crash)
    a = Asm(img, 0x6C9D99, 0x200)
    a.seq(r"call 0x42dbf5", r"call ebx", r"call 0x42dc9f", r"call 0x42e9d7", r"call 0x42dbf5",
          r"call ebx")


@fact("INI")
def weapon_template_ctor_defaults(img):
    a = Asm(img, 0x6CDE89, 0x600)
    for pat in (r"or dword ptr \[esi \+ 0x78\], 0xffffffff",            # IdleAfterFiringDelay = -1
                r"mov dword ptr \[esi \+ 0xf8\], eax", r"mov dword ptr \[esi \+ 0xfc\], eax",   # ContinuousFireOne/Two = 0x7fffffff
                r"mov dword ptr \[esi \+ 0x108\], 1",                    # ShotsPerBarrel
                r"mov dword ptr \[esi \+ 0x10c\], 2",
                r"mov dword ptr \[esi \+ 0xe4\], ebx",                   # ClipSize 0 (infinite)
                r"mov dword ptr \[esi \+ 0x128\], ebx", r"mov dword ptr \[esi \+ 0x12c\], ebx",  # AutoReloadsClip YES, PreAttackType PER_SHOT
                r"mov byte ptr \[esi \+ 0x161\], 1", r"mov byte ptr \[esi \+ 0x16d\], 1",
                r"movss dword ptr \[esi \+ 0x158\], xmm1",               # HitPercentage 1.0
                r"mov dword ptr \[esi \+ 0xe8\], ebx", r"mov dword ptr \[esi \+ 0xf0\], ebx"):
        a.has(pat, "template default")
    a.has(r"mov eax, 0x7fffffff")


# --------------------------------------------------------------------------------------------------
# 2. Weapon instance
# --------------------------------------------------------------------------------------------------


@fact("INSTANCE")
def weapon_ctor_layout(img):
    a = Asm(img, 0x6CCF5D, 0x200)
    for pat in (r"mov dword ptr \[esi\], 0xc18404",                     # vtable
                r"mov dword ptr \[esi \+ 0xc\], edx",                    # slot
                r"mov dword ptr \[esi \+ 0x58\], eax", r"mov dword ptr \[esi \+ 4\], ecx", r"mov dword ptr \[esi \+ 8\], eax",
                r"mov dword ptr \[esi \+ 0x10\], edx",                   # status = 1
                r"mov dword ptr \[esi \+ 0x14\], eax", r"mov dword ptr \[esi \+ 0x18\], eax", r"mov dword ptr \[esi \+ 0x1c\], eax",
                r"mov dword ptr \[esi \+ 0x20\], eax", r"mov dword ptr \[esi \+ 0x24\], eax", r"mov dword ptr \[esi \+ 0x28\], eax",
                r"mov dword ptr \[esi \+ 0x50\], eax", r"mov dword ptr \[esi \+ 0x54\], eax",
                r"mov byte ptr \[esi \+ 0x4c\], bl", r"mov dword ptr \[esi \+ 0x34\], 0x7fffffff",
                r"mov dword ptr \[esi \+ 0x38\], eax", r"mov dword ptr \[esi \+ 0x3c\], edx", r"mov dword ptr \[esi \+ 0x2c\], eax",
                r"mov dword ptr \[esi \+ 0x5c\], eax", r"mov dword ptr \[esi \+ 0x30\], edx"):
        a.has(pat, "Weapon ctor")
    a.seq(r"xor edx, edx", r"inc edx")                                  # edx = 1 (status OUT_OF_AMMO and default ShotsPerBarrel)
    a.has(r"mov edx, dword ptr \[ecx \+ 0x108\]")                       # ShotsPerBarrel
    a.has(r"add edx, dword ptr \[ecx \+ 0x150\]")                       # frame + SuspendFXDelay
    a.has(r"mov edx, dword ptr \[edx \+ 0x40\]")                        # frame = [TheGameLogic + 0x40]
    a.has(rf"mov edx, dword ptr \[{LOGIC_FRAME_PTR:#x}\]")
    a.has(r"comiss xmm0, dword ptr \[0xbdd390\]")                       # MinTargetPitch > -pi  => pitch limited
    a.has(r"movss xmm0, dword ptr \[0xbdd388\]")                       # pi > MaxTargetPitch  => pitch limited
    # WeaponStore::allocateNewWeapon: operator new(0x60) then the ctor: sizeof(Weapon) == 0x60
    s = Asm(img, 0x68B150, 0x60)
    s.seq(r"push 0x60", r"call 0x42f6e0", r"call 0x6ccf5d")


@fact("INSTANCE")
def weapon_copy_ctor_and_assign(img):
    c = Asm(img, 0x6CD01C, 0x120)
    for pat in (r"mov dword ptr \[esi \+ 0x10\], edx", r"mov dword ptr \[esi \+ 0x34\], 0x7fffffff",
                r"mov ecx, dword ptr \[ecx \+ 0x30\]", r"mov dword ptr \[esi \+ 0x30\], ecx"):   # +0x30 copied from source
        c.has(pat)
    n = Asm(img, 0x6CA19F, 0x100)
    n.has(r"mov dword ptr \[eax \+ 0x2c\], edi")
    n.has(r"mov ecx, dword ptr \[ecx \+ 0x30\]")


@fact("INSTANCE")
def get_status(img):
    a = Asm(img, 0x6CD142, 0x120)
    a.seq(rf"mov eax, dword ptr \[{LOGIC_FRAME_PTR:#x}\]", r"mov edi, dword ptr \[eax \+ 0x40\]",
          r"cmp edi, dword ptr \[esi \+ 0x1c\]", r"push 4", r"cmp edi, dword ptr \[esi \+ 0x20\]", r"push 5",
          r"cmp dword ptr \[eax \+ 0x78\], 0", r"cmp edi, dword ptr \[esi \+ 0x18\]",
          r"call 0x762977", r"mov eax, dword ptr \[esi \+ 0x10\]")
    a.has(r"mov byte ptr \[eax\], 0")                                   # *valid = false for status 4 and 5
    assert a.count(r"mov byte ptr \[eax\], 0") == 2
    a.has(r"cmp ecx, eax")                                              # 'xor ecx,ecx; cmp ecx,eax; sbb eax,eax; inc eax' = (ammo>0)?0:1
    a.has(r"sbb eax, eax")
    # write-back wrapper: getStatus(&valid=true); if (valid) setStatus(st)
    w = Asm(img, 0x6CDCE7, 0x40)
    w.seq(r"mov byte ptr \[ebp - 1\], 1", r"call 0x6cd142", r"cmp byte ptr \[ebp - 1\], 0", r"call 0x6ca232")
    s = Asm(img, 0x6CA232, 0x20)
    s.seq(r"cmp dword ptr \[ecx \+ 0x10\], eax", r"mov dword ptr \[ecx \+ 0x10\], eax")


@fact("INSTANCE")
def get_remaining_ammo(img):
    a = Asm(img, 0x6CD298, 0x80)
    a.seq(r"lea ecx, \[eax \+ 0x120\]", r"call 0x762977", rf"mov ecx, dword ptr \[{LOGIC_FRAME_PTR:#x}\]", r"call 0x449681",
          r"mov ecx, dword ptr \[eax \+ 0x258\]", r"call dword ptr \[eax \+ 0x114\]")
    a.seq(r"cmp byte ptr \[esp \+ 8\], 0", r"call 0x6cd142", r"cmp eax, 3", r"mov eax, dword ptr \[esi \+ 0x14\]")


@fact("INSTANCE")
def get_percent_ready_to_fire(img):
    a = Asm(img, 0x6CDB73, 0x90)
    a.seq(r"call 0x6cd142", r"cmp eax, 3", r"cmp eax, 4", r"cmp eax, 5",
          rf"mov eax, dword ptr \[{LOGIC_FRAME_PTR:#x}\]", r"mov edx, dword ptr \[esi \+ 0x18\]", r"sub ecx, dword ptr \[esi \+ 0x28\]",
          r"fdivp st\(1\)")
    a.has(r"fld dword ptr \[0xc1b594\]")                                # 0.0 for OUT_OF_AMMO / PRE_ATTACK
    a.has(r"fld dword ptr \[0xbd1908\]")                                # 1.0


@fact("INSTANCE")
def delay_between_shots_and_clip_reload(img):
    d = Asm(img, 0x6CA066, 0x80)
    d.seq(r"mov eax, dword ptr \[ecx \+ 0xf0\]", r"mov ecx, dword ptr \[ecx \+ 0xf4\]", r"cmp eax, ecx", r"push 0x3dc",
          rf"push {WEAPON_CPP:#x}", rf"call {RNG_INT:#x}", r"fld dword ptr \[eax \+ 0xc\]", r"fild dword ptr \[ebp - 4\]",
          r"fmul dword ptr \[ebp \+ 0xc\]", r"fdivp st\(1\)", r"call dword ptr \[0xbd0580\]", r"fistp dword ptr \[ebp \+ 0xc\]")
    c = Asm(img, 0x6CA0C0, 0x80)
    c.seq(r"mov eax, dword ptr \[ecx \+ 0xe8\]", r"mov ecx, dword ptr \[ecx \+ 0xec\]", r"cmp eax, ecx", r"push 0x3ee",
          rf"push {WEAPON_CPP:#x}", rf"call {RNG_INT:#x}", r"fld dword ptr \[eax \+ 0xc\]", r"push 3", r"idiv esi",
          r"sub ecx, edx", r"fild dword ptr \[ebp \+ 8\]", r"fdiv st\(1\)", r"call dword ptr \[0xbd0580\]", r"fistp dword ptr \[ebp - 4\]")
    # floor is msvcr71 floor (import 0xbd0580), ceil is 0xbd0588
    import pefile
    pe = pefile.PE(img.path)
    names = {i.address: i.name for e in pe.DIRECTORY_ENTRY_IMPORT for i in e.imports}
    assert names[0xBD0580] == b"floor" and names[0xBD0588] == b"ceil" and names[0xBD06A0] == b"sqrt"
    assert names[0xBD06A8] == b"fabs" and names[0xBD06A4] == b"sin" and names[0xBD06AC] == b"cos"
    # getPreAttackDelay(bonus) template inline: (int)((float)PreAttackDelay * bonus[4]), SSE, truncating
    p = Asm(img, 0x6CA123, 0x20)
    p.seq(r"cvtsi2ss xmm0, dword ptr \[ecx \+ 0x138\]", r"mulss xmm0, dword ptr \[eax \+ 0x10\]", r"cvttss2si eax, xmm0")


@fact("INSTANCE")
def reload_with_bonus_and_loaders(img):
    r = Asm(img, 0x6CE8B9, 0x120)
    r.seq(r"mov eax, dword ptr \[eax \+ 0xe4\]", r"mov dword ptr \[esi \+ 0x14\], eax", r"call 0x6cd298",
          r"mov dword ptr \[esi \+ 0x14\], 0x7fffffff", r"mov dword ptr \[esi \+ 0x10\], eax",   # status = 3
          r"call 0x6ca0c0", r"cvtsi2ss xmm0, eax", rf"mov eax, dword ptr \[{LOGIC_FRAME_PTR:#x}\]",
          r"fild dword ptr \[ebp \+ 0xc\]", r"mov dword ptr \[esi \+ 0x28\], eax", r"fadd dword ptr \[ebp \+ 0x10\]",
          r"call 0xa3cfa4", r"mov dword ptr \[esi \+ 0x18\], eax", r"cmp byte ptr \[ecx \+ 0x80\], 0", r"push 8",
          r"call 0x6907f1", r"call 0x6ce873")
    r.has(r"push 3")
    # loadAmmoNow / reloadAmmo: bonus = computeBonus(source, 0), then reloadWithBonus(source, bonus, true|false)
    for fn, flag in ((0x6CEE0F, 1), (0x6CEE4C, 0)):
        a = Asm(img, fn, 0x60)
        a.seq(r"call 0x6ca7ac", rf"push {flag}", r"call 0x6ce8b9")
    s = Asm(img, 0x6CE873, 0x60)
    s.seq(r"call 0x5ff9d3", r"mov esi, dword ptr \[eax \+ 0x44\]", r"sar esi, 3", r"call 0x90be00")   # indices 0..n-1 of the template ScatterTarget (8-byte) vector
    # setPossibleNextShotFrame-style helpers and the 20-byte timer records
    save = Asm(img, 0x6CA178, 0x40)
    save.seq(r"mov edx, dword ptr \[edx \+ 0xc\]", r"mov edx, dword ptr \[eax \+ 0x14\]", r"mov edx, dword ptr \[eax \+ 0x10\]",
             r"mov edx, dword ptr \[eax \+ 0x28\]", r"mov eax, dword ptr \[eax \+ 0x18\]")
    rest = Asm(img, 0x6CEE89, 0x80)
    rest.seq(r"cmp dword ptr \[edx\], eax", r"add edx, 0x14", r"cmp esi, 6", r"cmp byte ptr \[eax \+ 0x16e\], 0",
             r"call 0x6cee0f", r"mov eax, dword ptr \[edx \+ 4\]", r"mov dword ptr \[ecx \+ 0x14\], eax", r"call 0x6ca232",
             r"mov dword ptr \[ecx \+ 0x28\], eax", r"mov dword ptr \[ecx \+ 0x18\], eax")


@fact("INSTANCE")
def set_clip_percent_full(img):
    a = Asm(img, 0x6CEEE3, 0x90)
    a.seq(r"mov eax, dword ptr \[eax \+ 0xe4\]", r"fild dword ptr \[ebp - 4\]", r"fmul dword ptr \[ebp \+ 8\]", r"call dword ptr \[0xbd0580\]",
          r"fistp dword ptr \[ebp - 4\]", r"call 0x6cd298", r"cmp edi, eax", r"ja ", r"mov dword ptr \[esi \+ 0x14\], edi",
          r"neg eax", r"sbb eax, eax", r"neg eax", r"call 0x6ca232", r"mov dword ptr \[esi \+ 0x28\], eax",
          r"mov dword ptr \[esi \+ 0x18\], eax", r"call 0x6ce873")


@fact("INSTANCE")
def pre_fire(img):
    j = Asm(img, 0x6CA2DB, 0x40)
    j.seq(r"mov eax, dword ptr \[eax \+ 0x13c\]", r"test eax, eax", r"push 0xe3c", rf"push {WEAPON_CPP:#x}", r"push eax", r"push 0",
          rf"call {RNG_INT:#x}", r"mov dword ptr \[esi \+ 0x58\], eax")
    a = Asm(img, 0x6CE95D, 0x120)
    a.seq(r"call 0x6ca2db", r"call 0x6cdd10", r"test eax, eax", r"jle ", r"push 4", r"mov dword ptr \[ebx \+ 0x10\], eax", r"call 0x6ca7ac",
          rf"mov eax, dword ptr \[{LOGIC_FRAME_PTR:#x}\]", r"add eax, dword ptr \[ebp - 4\]", r"mov dword ptr \[ebx \+ 0x1c\], eax",
          r"mov eax, dword ptr \[esi \+ 0x144\]", r"test eax, eax", r"jle ", r"mov dword ptr \[ebx \+ 0x24\], ecx",
          r"cmp byte ptr \[esi \+ 0x130\], 0", r"fild dword ptr \[esi \+ 0x138\]", r"fmul dword ptr \[ebp - 0xc\]", r"call 0xa3cfa4",
          r"add eax, dword ptr \[ecx \+ 0x40\]", r"add eax, dword ptr \[esi \+ 0x144\]", r"mov dword ptr \[ebx \+ 0x50\], eax",
          r"call 0x6cb690", r"mov eax, dword ptr \[esi \+ 0x68\]", r"mov ebx, dword ptr \[esi \+ 0xb8\]", r"call 0x494615")
    # Object::preFireCurrentWeapon: only when frame+1 >= whenWeCanFireAgain
    o = Asm(img, 0x69213E, 0x120)
    o.seq(r"mov eax, dword ptr \[ebp - 4\]", rf"mov ecx, dword ptr \[{LOGIC_FRAME_PTR:#x}\]", r"inc ecx", r"cmp ecx, dword ptr \[eax \+ 0x18\]",
          r"jb ", r"call 0x6ce95d")
    assert 0x69213E in Asm(img, 0x74C258, 0x300).calls, "AIAttackFireWeaponState.onEnter calls preFireCurrentWeapon"


@fact("INSTANCE")
def get_pre_attack_delay(img):
    a = Asm(img, 0x6CDD10, 0xB0)
    a.seq(r"mov ecx, dword ptr \[eax \+ 0x12c\]", r"cmp ecx, 2", r"mov edi, dword ptr \[eax \+ 0xe4\]", r"test edi, edi", r"jle ",
          r"call 0x6cd298", r"cmp eax, edi", r"jb ")
    a.seq(r"call 0x6ca7ac", r"cvtsi2ss xmm0, dword ptr \[eax \+ 0x138\]", r"mulss xmm0, dword ptr \[ebp - 8\]", r"cvttss2si eax, xmm0",
          r"add eax, dword ptr \[esi \+ 0x58\]")
    a.seq(r"cmp ecx, 1", r"call 0x68c54e", r"test eax, eax", r"jle ")
    a.seq(r"cmp ecx, 3", r"call 0x68c562", r"call 0x403270")
    # FiringTracker::getShotsAtTarget (0x8E2E12): same-victim shot counter used by PER_ATTACK; +0x48 = owner pos at last shot (PER_POSITION)
    t = Asm(img, 0x8E2E12, 0x60)
    t.seq(r"cmp byte ptr \[esi \+ 0x34\], cl", r"mov eax, dword ptr \[eax \+ 0x74\]", r"cmp eax, dword ptr \[esi \+ 0x24\]")
    t.has(r"mov eax, dword ptr \[esi \+ 0x20\]")
    p = Asm(img, 0x68AF0C, 0x30)
    p.seq(r"fld dword ptr \[ecx \+ 0x48\]", r"mov edx, dword ptr \[ecx \+ 0x4c\]", r"mov ecx, dword ptr \[ecx \+ 0x50\]")


@fact("INSTANCE")
def private_fire_weapon(img):
    a = Asm(img, 0x6CEF6D, 0x500)
    a.seq(r"call 0x6ca7ac", r"cmp dword ptr \[ebp \+ 0x10\], edi", r"call 0x6cab32",                   # RequestAssistRange
          r"cmp byte ptr \[eax \+ 0x130\], 0", r"mov eax, dword ptr \[eax \+ 0x144\]", r"cmp eax, ecx", r"mov dword ptr \[ebx \+ 0x50\], ecx",   # leech
          r"call 0x6cdce7", r"test eax, eax", r"call 0x6cd1fd",
          rf"mov eax, dword ptr \[{LOGIC_FRAME_PTR:#x}\]", r"call 0x68c82d", r"call 0x6ca066", r"call 0x6cd298", r"test eax, eax", r"jbe ",
          r"call 0x674673", r"cmp dword ptr \[ebx \+ 0x38\], eax", r"mov dword ptr \[ebx \+ 0x3c\], eax")
    a.seq(r"push 0xcbd", rf"push {WEAPON_CPP:#x}", rf"call {RNG_INT:#x}", r"call 0x6cc915")                 # scatter pick (3261)
    a.seq(r"dec dword ptr \[ebx \+ 0x14\]", r"dec dword ptr \[ebx \+ 0x34\]", r"dec dword ptr \[ebx \+ 0x3c\]", r"mov dword ptr \[ebx \+ 0x2c\], esi",
          r"inc dword ptr \[ebx \+ 0x38\]", r"call 0x6cd298", r"test eax, eax", r"ja ",
          r"cmp dword ptr \[eax \+ 0x128\], edi", r"call 0x6cee4c",                                       # AutoReloadsClip == YES (0) -> reloadAmmo
          r"mov dword ptr \[ebx \+ 0x10\], ecx", r"call 0x762977", r"mov dword ptr \[ebx \+ 0x18\], 0x7fffffff",
          r"mov dword ptr \[ebx \+ 0x10\], eax", r"mov dword ptr \[ebx \+ 0x18\], eax", r"mov dword ptr \[ebx \+ 0x28\], esi",
          r"mov eax, dword ptr \[eax \+ 0x144\]", r"mov dword ptr \[ebx \+ 0x20\], ecx")
    assert a.count(r"push 0x3dc") == 0           # the 988 draw is inside 0x6ca066, called once from here
    # linear target loop: LinearTarget vector at template +0x4c, 12-byte entries, cursor +0x54, IgnoreLinearFirstTarget +0x154
    a.has(r"cmp byte ptr \[ecx \+ 0x154\], 0")
    a.has(r"cmp dword ptr \[ebx \+ 0x54\], 0")
    a.has(r"mov dword ptr \[ebx \+ 0x54\], edx")
    # wrappers fix the argument shape: 9 stack args ('ret 0x24')
    assert Asm(img, 0x6CEF6D, 0x500).lines[-1].startswith("ret 0x24") or any(l.startswith("ret 0x24") for l in a.lines)
    for w in (0x6CF328, 0x6CF34B, 0x6CF3AE, 0x6CF3D2):
        assert 0x6CEF6D in Asm(img, w, 0x60).calls


@fact("INSTANCE")
def compute_bonus(img):
    a = Asm(img, 0x6CA7AC, 0x60)
    a.seq(r"mov eax, 0x3f800000", r"rep stosd", r"mov esi, dword ptr \[eax \+ 0x39c\]", rf"mov eax, dword ptr \[{GLOBAL_DATA_PTR:#x}\]",
          r"mov ecx, dword ptr \[eax \+ 0xad0\]", r"or esi, dword ptr \[ebp \+ 0xc\]", r"call 0x6ca4b5",
          r"mov ecx, dword ptr \[eax \+ 0xe0\]", r"call 0x6ca4b5")
    s = Asm(img, 0x6CA4B5, 0x40)
    s.seq(r"shl eax, cl", r"call 0x6ca434", r"add edi, 0x18", r"cmp esi, 0x16")                  # 22 conditions, 6 floats each
    e = Asm(img, 0x6CA434, 0x30)
    e.seq(r"push 6", r"movss xmm0, dword ptr \[ecx \+ eax\]", r"subss xmm0, dword ptr \[0xbd1908\]", r"addss xmm0, dword ptr \[eax\]",
          r"movss dword ptr \[eax\], xmm0")


@fact("INSTANCE")
def leech_and_misc_accessors(img):
    s = Asm(img, 0x6CA31B, 0x20)
    s.seq(rf"mov eax, dword ptr \[{LOGIC_FRAME_PTR:#x}\]", r"add eax, dword ptr \[esp \+ 4\]", r"mov dword ptr \[ecx \+ 0x50\], eax")
    g = Asm(img, 0x6CA32D, 0x20)
    g.seq(r"mov eax, dword ptr \[ecx \+ 0x50\]", r"cmp dword ptr \[ecx \+ 0x40\], eax", r"sbb eax, eax", r"neg eax")
    # +0x24..+0x1c follow-through window used by Object::updateWeaponStatusConditions
    w = Asm(img, 0x6CA2C3, 0x20)
    w.seq(r"cmp eax, dword ptr \[ecx \+ 0x1c\]", r"cmp eax, dword ptr \[ecx \+ 0x24\]")
    t = Asm(img, 0x6CA241, 0x10)
    t.seq(r"mov eax, dword ptr \[ecx \+ 0x18\]", r"sub eax, dword ptr \[ecx \+ 0x28\]")


# --------------------------------------------------------------------------------------------------
# 3. Ranges
# --------------------------------------------------------------------------------------------------


@fact("RANGE")
def template_attack_range(img):
    a = Asm(img, 0x6C9F5B, 0x120)
    a.seq(r"mulss xmm0, dword ptr \[esi \+ 0x14\]", r"subss xmm0, dword ptr \[0xbe5ae8\]",                   # bonus.RANGE * AttackRange - 2.5
          r"subss xmm2, dword ptr \[ebp \+ 0xc\]", r"comiss xmm2, dword ptr \[esi \+ 0x1c\]",                  # (0 - dz) >= RangeBonusMinHeight
          r"addss xmm2, dword ptr \[ebp \+ 0xc\]", r"mulss xmm2, dword ptr \[esi \+ 0x24\]",                  # (MinHeight + dz) * PerFoot
          r"movss xmm3, dword ptr \[esi \+ 0x20\]", r"subss xmm3, xmm2", r"addss xmm3, xmm0",                  # RangeBonus - that + base
          r"movss xmm2, dword ptr \[esi \+ 0x164\]", r"call 0xa3cf8a", r"fld dword ptr \[esi \+ 0x164\]", r"fcompi st\(1\)")
    m = Asm(img, 0x6CA03E, 0x30)
    m.seq(r"movss xmm0, dword ptr \[ecx \+ 0x18\]", r"subss xmm0, dword ptr \[0xbe5ae8\]", r"comiss xmm1, xmm0")
    # height wrappers: dz = target.z - source.z
    h = Asm(img, 0x6CA900, 0x20)
    h.seq(r"movss xmm0, dword ptr \[eax \+ 8\]", r"subss xmm0, dword ptr \[eax \+ 0x40\]", r"call 0x6ca8bd")
    g = Asm(img, 0x6CA8BD, 0x60)
    g.seq(r"call 0x6ca7ac", r"call 0x6ca69a")
    s = Asm(img, 0x6CA69A, 0x80)
    s.seq(r"call 0x6c9f5b", r"call 0x6ca61f", r"call 0x44ddec", r"push 3", r"call 0x6ff412", r"fmul dword ptr \[ebp \+ 0xc\]")
    s.has(r"push 0x3a")                                                                       # INSIDE_GARRISON
    u = Asm(img, 0x6CA61F, 0x80)
    u.seq(r"push 7", r"call 0x68c818", r"addss xmm0, dword ptr \[0xbd1908\]", rf"mov esi, dword ptr \[{GLOBAL_DATA_PTR:#x}\]",
          r"add esi, 0x1224", r"push 0x3a", r"call 0x44ddec", r"mulss xmm0, dword ptr \[esi\]")
    d = Asm(img, 0x6CA701, 0x60)
    d.seq(r"subss xmm0, dword ptr \[eax \+ 8\]", r"call 0x6ca69a", r"call 0x6ca61f", r"fmul dword ptr \[ebp \+ 0x10\]")   # Weapon::getAttackRange(source): scale applied twice
    p = Asm(img, 0x6CA935, 0x60)
    p.seq(r"call 0x6ca7ac", r"call 0x6ca701")
    dist = Asm(img, 0x6CA974, 0x60)
    dist.seq(r"call 0x6ca921", r"fadd dword ptr \[edi \+ 0xbc\]", r"fadd dword ptr \[esi \+ 0xbc\]")
    ar = Asm(img, 0x68C818, 0x20)
    ar.has(r"jmp 0x804f39")
    assert 0x804FFF in Asm(img, 0x68C82D, 0x20).calls or Asm(img, 0x68C82D, 0x20).count(r"jmp 0x804fff") == 1


@fact("RANGE")
def distance_and_too_close(img):
    d = Asm(img, 0x6634BF, 0x90)
    d.seq(r"fld dword ptr \[eax\]", r"fsub dword ptr \[edx\]", r"mov eax, dword ptr \[ecx \+ 0xb8\]", r"fsub dword ptr \[edx \+ 4\]",
          r"mov eax, dword ptr \[eax \+ 0xb8\]", r"fmul st\(1\)", r"faddp st\(1\)", r"fstp qword ptr \[esp\]", r"call 0xa3cf96",
          r"fsub dword ptr \[ebp \+ 8\]", r"fsub dword ptr \[ebp \+ 0x10\]", r"fst dword ptr \[ebp \+ 8\]", r"fldz", r"fcompi st\(1\)",
          r"mulss xmm0, xmm0")
    p = Asm(img, 0x6CA525, 0x70)
    p.seq(r"fld dword ptr \[eax\]", r"fsub dword ptr \[edx\]", r"mov eax, dword ptr \[ecx \+ 0xb8\]", r"call 0xa3cf96", r"fsub dword ptr \[ebp \+ 8\]",
          r"fst dword ptr \[ebp \+ 8\]", r"fldz", r"fcompi st\(1\)", r"mulss xmm0, xmm0")
    o = Asm(img, 0x66352C, 0x20)
    o.seq(r"lea edx, \[eax \+ 0x38\]", r"lea eax, \[ecx \+ 0x38\]", r"call 0x6634bf")
    t1 = Asm(img, 0x6CA83B, 0x60)           # isTooClose(source, victim)
    t1.seq(r"call 0x6ca03e", r"fstp dword ptr \[ebp - 4\]", r"call 0x66352c", r"fmul dword ptr \[ebp - 4\]", r"fcompi st\(1\)", r"jbe ")
    t2 = Asm(img, 0x6CA87A, 0x60)           # isTooClose(source, pos)
    t2.seq(r"call 0x6ca03e", r"call 0x6ca525", r"fmul dword ptr \[ebp - 4\]", r"fcompi st\(1\)", r"jbe ")
    g = Asm(img, 0x6CBFF1, 0x90)            # isSourceObjectWithGoalPositionWithinAttackRange
    g.seq(r"call 0x6634bf", r"call 0x6ca8bd", r"call 0x6ca03e", r"fmul st\(1\)", r"fcompi st\(1\)", r"ja ", r"mulss xmm0, xmm0", r"comiss xmm0, dword ptr \[ebp \+ 0x10\]", r"jb ")


@fact("RANGE")
def is_within_attack_range(img):
    a = Asm(img, 0x6CC07C, 0x260)
    a.seq(r"cmp byte ptr \[eax \+ 0x125\], 0",                                                 # MeleeWeapon
          r"push 0x25", r"call 0x44ddec", r"push 0x25", r"call 0x44ddec",                      # both CONTESTING_BUILDING
          r"test byte ptr \[eax \+ 0x108\], 0x80", r"push 0x4b", r"call 0x44ddec", r"call 0x66352c", r"fmul dword ptr \[ebp \+ 0x1c\]",
          r"call 0x6ed843",
          r"call 0xad22c0", r"call 0xad22c0", r"call 0x68f430", r"call 0x6634bf",                # BOX geometry distance vs circle distance
          r"call 0x6ca8bd", r"fmul st\(1\)", r"mulss xmm0, xmm0", r"addss xmm0, dword ptr \[ebp \+ 0x10\]",
          r"call 0x6ca03e", r"push 0x25", r"call 0x44ddec", r"cmp byte ptr \[ebp \+ 0x1c\], 0", r"comiss xmm0, dword ptr \[ebp \+ 0x10\]", r"ja ",
          r"comiss xmm0, dword ptr \[ebp \+ 0x10\]", r"jb ")
    a.has(r"fsub dword ptr \[0xbe5ae8\]")                                                       # position target: range - 2.5
    w1 = Asm(img, 0x6CC653, 0x40)
    w1.seq(r"lea esi, \[eax \+ 0x38\]", r"lea eax, \[edx \+ 0x38\]", r"call 0x6cc07c")
    w2 = Asm(img, 0x6CC622, 0x40)
    w2.seq(r"lea edx, \[eax \+ 0x38\]", r"push 0", r"call 0x6cc07c")
    g = Asm(img, 0xAD22C0, 0x30)
    g.seq(r"mov eax, 0x38e38e39", r"cmp eax, 1", r"cmp edx, 2", r"sete al")                     # GeometryInfo: exactly one shape and it is a BOX (type 2)
    b = Asm(img, 0x68F430, 0x300)
    b.seq(r"call 0xad1ae0", r"call 0xad14f0", r"call 0xa3cf8a", r"call 0xa3cf8a", r"call 0x441c56", r"call 0xa3cf8a", r"call 0xa3cf8a",
          r"call 0xa3cf96")            # fabs(local x), fabs(local y), [corner case: invsqrt, 2x fabs support radius], sqrt


@fact("RANGE")
def pitch_check(img):
    a = Asm(img, 0x6CA9BF, 0x120)
    a.seq(r"cmp byte ptr \[ecx \+ 0x4c\], 0", r"fsub dword ptr \[edi \+ 8\]", r"call 0xa3cf8a", r"fld dword ptr \[0xbd83d8\]", r"call 0xad2620",
          r"comiss xmm1, dword ptr \[eax \+ 0x88\]", r"movss xmm0, dword ptr \[eax \+ 0x8c\]", r"comiss xmm0, xmm1")


# --------------------------------------------------------------------------------------------------
# 4. Consumers outside Weapon: Object wrappers, FiringTracker, AI states, status -> model condition
# --------------------------------------------------------------------------------------------------


@fact("CONSUMERS")
def object_fire_wrappers(img):
    a = Asm(img, 0x69200A, 0xA8)
    a.seq(r"mov edi, dword ptr \[esi \+ eax\*4 \+ 0x354\]", r"cmp byte ptr \[eax \+ 0x16f\], bl", r"call 0x69121a", r"call 0x6cdce7",
          r"push 0x25", r"call 0x6cf3ae", r"call 0x6cf328", r"mov ecx, dword ptr \[esi \+ 0x248\]", r"call 0x8e326d",
          r"cmp dword ptr \[eax \+ 0x78\], -1", r"call 0x68df11", r"and byte ptr \[esi \+ 0x458\], 0xfd")
    b = Asm(img, 0x6920AD, 0x90)
    b.seq(r"push 0x25", r"call 0x44ddec", r"call 0x6cdce7", r"call 0x6cf34b", r"mov ecx, dword ptr \[esi \+ 0x248\]", r"call 0x8e326d",
          r"call 0x68df11")
    g = Asm(img, 0x68B58C, 0x30)
    g.seq(r"cmp dword ptr \[ecx \+ 0x374\], 0", r"mov edx, dword ptr \[ecx \+ 0x36c\]", r"mov eax, dword ptr \[ecx \+ eax\*4 \+ 0x354\]")
    r = Asm(img, 0x68B491, 0x20)
    r.seq(r"add ecx, 0x34c", r"call 0x6c80e5")


@fact("CONSUMERS")
def firing_tracker(img):
    s = Asm(img, 0x8E326D, 0x300)
    s.seq(r"call 0x449681", r"push 0x26", r"mov dword ptr \[ebx \+ 0x44\], ecx", r"call 0x8e302a", r"call 0x403270",
          r"inc dword ptr \[ebx \+ 0x20\]", r"mov dword ptr \[ebx \+ 0x20\], 1",
          r"mov eax, dword ptr \[eax \+ 0x104\]", r"mov dword ptr \[ebx \+ 0x40\], eax",                       # AutoReloadWhenIdle deadline
          r"mov ecx, dword ptr \[eax \+ 0x100\]", r"mov eax, dword ptr \[esi \+ 0x18\]", r"mov dword ptr \[ebx \+ 0x3c\], eax",   # coast end = whenWeCanFireAgain + Coast
          r"mov edi, dword ptr \[eax \+ 0xf8\]", r"mov eax, dword ptr \[eax \+ 0xfc\]", r"call 0x8e302a", r"call 0x8e3174")
    s.has(r"mov edi, dword ptr \[eax \+ 0xcc\]")                                                          # FireSoundLoopTime
    assert Asm(img, 0x8E3174, 0x40).count(r"shr ecx, 3") >= 1
    u = Asm(img, 0x8E30DD, 0x120)
    u.seq(rf"mov eax, dword ptr \[{LOGIC_FRAME_PTR:#x}\]", r"call 0x403270", r"call 0x8e302a", r"mov eax, dword ptr \[esi \+ 0x30\]",
          r"call 0x68b491", r"and dword ptr \[esi \+ 0x30\], 0", r"mov eax, dword ptr \[0xd9f608\]", r"call 0x8e302a")
    c = Asm(img, 0x8E302A, 0x100)
    assert c.count(r"0x39c\]") >= 4                                                                       # Object weapon-bonus-condition bits 2 (MEAN) / 3 (FAST)
    # ctor vtable/name
    assert img.cstr(0xC78224) == "FiringTracker"


@fact("CONSUMERS")
def ai_wait_until_finished_firing(img):
    names = {0xC277C4: "AIWaitUntilFinishedFiringState", 0xC2789C: "AIAttackFireWeaponState", 0xC2770C: "AIAttackPositionAimAtTargetState",
             0xC276AC: "AIAttackAimAtTargetState"}
    for va, nm in names.items():
        assert img.cstr(va) == nm, (hex(va), img.cstr(va))
    assert [img.u32(0xC27780 + 4 * k) for k in (2, 4, 5, 6)] == [0x740E10, 0x7479B7, 0x742D42, 0x742CEF]
    # AIAttackFireWeaponState vtable 0xC27858 (ctor 0x740E51): slot 4 = onEnter (preFireCurrentWeapon), slot 6 = update (fireCurrentWeapon)
    assert [img.u32(0xC27858 + 4 * k) for k in (4, 6)] == [0x74C258, 0x74C4A7]
    assert 0x69213E in Asm(img, 0x74C258, 0x300).calls and 0x69200A in Asm(img, 0x74C4A7, 0x400).calls
    fs = Asm(img, 0x74C4A7, 0x400)
    fs.seq(r"call 0x6cdce7", r"cmp eax, 4", r"xor eax, eax", r"cmp eax, edi", r"jne ", r"push -2")
    on = Asm(img, 0x74C258, 0x300)
    on.seq(r"call 0x6cc653", r"call 0x6cdce7", r"test eax, eax", r"push 0x26", r"call 0x44ddec", r"push 0xd", r"call 0x62684d", r"call 0x69213e")
    e = Asm(img, 0x7479B7, 0x90)       # onEnter
    e.seq(r"call 0x68b58c", r"call 0x6cdce7", r"test eax, eax", r"push 0x26", r"call 0x44ddec", r"cmp dword ptr \[eax \+ 0x78\], 0",
          r"call 0x69121a", r"mov eax, dword ptr \[eax \+ 0x7c\]", r"test eax, eax", r"jle ", r"push 8", r"call 0x6907f1")
    u = Asm(img, 0x742CEF, 0x60)       # update
    u.seq(r"call 0x68b58c", r"mov eax, dword ptr \[eax \+ 0x78\]", r"test eax, eax", r"jl ", r"mov ecx, dword ptr \[esi \+ 0x2c\]",
          r"add ecx, eax", rf"mov eax, dword ptr \[{LOGIC_FRAME_PTR:#x}\]", r"cmp ecx, dword ptr \[eax \+ 0x40\]", r"call 0x6cdce7",
          r"cmp eax, 5", r"cmp dword ptr \[eax \+ 0x78\], ecx", r"setl cl", r"dec ecx", r"dec ecx")
    x = Asm(img, 0x742D42, 0x20)
    x.seq(r"push 1", r"call 0x68df11")


@fact("CONSUMERS")
def status_to_model_condition(img):
    assert [img.u32(0xC11F74 + 4 * k) for k in range(6)] == [0, 0, 2, 3, 4, 1]       # weapon status -> WeaponSetCondition
    mc = name_list(img, 0xD9FAD8)
    lookup = [img.u32(0xDA14CC + 4 * c) for c in range(6)]
    first = [mc[img.u32(p)] for p in lookup[1:5]]
    assert first == ["FIRING_A", "BETWEEN_FIRING_SHOTS_A", "RELOADING_A", "PREATTACK_A"], first
    assert mc[img.u32(0xC16BF0)] == "USING_WEAPON_A" and mc[img.u32(0xC16BD8)] == "FIRING_OR_PREATTACK_A" and mc[img.u32(0xC16BC0)] == "FIRING_OR_RELOADING_A"
    u = Asm(img, 0x68E197, 0x200)
    u.seq(r"push 0x16", r"call 0x44ddec", r"push 0x19", r"call 0x44ddec", r"push 0xd", r"call 0x44ddec",
          r"mov ecx, dword ptr \[esi \+ 0x2c\]", r"mov eax, dword ptr \[eax \+ 0x78\]", r"cmp byte ptr \[eax \+ 0x169\], 0",
          r"cmp ebx, dword ptr \[esi \+ 0x24\]", r"cmp ebx, dword ptr \[esi \+ 0x1c\]", r"push 5", r"call 0x6ca2c3", r"call 0x6cdce7",
          r"mov ebx, dword ptr \[eax\*4 \+ 0xc11f74\]", r"push 2", r"call 0x6c83fd", r"call 0x6c84c9", r"call 0x68d607")
    assert 0x68E197 in Asm(img, 0x690053, 0x10).calls          # ObjectWeaponStatusHelper per-frame update


@fact("CONSUMERS")
def weapon_set_timer_carry(img):
    f = table(img, 0xC16D30)
    assert f["ShareWeaponReloadTime"][2] == 0x35C and f["WeaponLockSharedAcrossSets"][2] == 0x35D and f["ReadyStatusSharedWithinSet"][2] == 0x364
    u = Asm(img, 0x6C99E2, 0x1A0)
    u.seq(r"cmp byte ptr \[eax \+ 0x35c\], bl", r"cmp byte ptr \[edi \+ 0x35c\], bl", r"call 0x6ca178", r"call 0x68b150", r"call 0x6cee89")
    s = Asm(img, 0x69720E, 0x90)       # ShareTimers on a manual weapon switch
    s.seq(r"call 0x68b58c", r"call 0x68df11", r"call 0x69121a", r"cmp byte ptr \[eax \+ 0x16a\], bl", r"call 0x6cd142", r"call 0x68b58c",
          r"mov ecx, dword ptr \[edi \+ 0x18\]", r"mov dword ptr \[eax \+ 0x18\], ecx", r"call 0x6ca232")


# --------------------------------------------------------------------------------------------------
# 5. RNG call sites
# --------------------------------------------------------------------------------------------------


def rng_sites(img, lo, hi):
    """(call va, kind, file ptr or None, line) for every call to the logic RNG in [lo, hi)."""
    out = []
    o = img.off(lo)
    insns = list(img.md.disasm(img.data[o:o + (hi - lo)], lo))
    for i, ins in enumerate(insns):
        if ins.mnemonic != "call" or not ins.op_str.startswith("0x"):
            continue
        t = int(ins.op_str, 16)
        if t not in (RNG_INT, RNG_REAL):
            continue
        pushes = []
        j = i - 1
        while j >= 0 and len(pushes) < 4 and i - j < 14:
            if insns[j].mnemonic == "push":
                pushes.append(insns[j].op_str)
            j -= 1
        # pushes are nearest-first: [arg0/lo, arg1/hi, file, line] for ints; [ecx, ecx, file, line] for reals
        line = int(pushes[3], 16) if pushes[3].startswith("0x") else int(pushes[3])
        out.append((ins.address, "int" if t == RNG_INT else "real", pushes[2], line))
    return out


@fact("RNG")
def weapon_runtime_rng_calls(img):
    sites = rng_sites(img, 0x6C8000, 0x6CF900)
    got = {(k, line) for _, k, _, line in sites}
    expect = {("int", 988), ("int", 1006), ("int", 3644), ("int", 3261), ("int", 1749),
              ("real", 1489), ("real", 1495), ("real", 1505), ("real", 1625), ("real", 1643),     # 1639 (0x667) shares the call at 0x6CC523 with 1643
             
              ("real", 0x5C6)}                       # 0x5C6 = WeaponSet.cpp:1478, chooseBestWeaponForTarget tiebreak
    assert got == expect, (sorted(got ^ expect))
    assert len(sites) == len(expect)
    # file strings
    assert img.cstr(WEAPON_CPP).endswith("GameLogic\\Object\\Weapon.cpp") and img.cstr(WEAPONSET_CPP).endswith("WeaponSet.cpp")
    # generators: logic arrays only (client/audio variants are 0x6D32E4/0x6D33AB/0x6D343B/0x6D3481, never called from the weapon cluster)
    for fn, arr in ((RNG_INT, 0xDA1CA4), (RNG_REAL, 0xDA1CA4)):
        assert Asm(img, fn, 0x80).count(rf"mov ecx, {arr:#x}") == 1
    for _, kind, fileptr, line in sites:
        assert fileptr in (hex(WEAPON_CPP), hex(WEAPONSET_CPP), "edi", "esi"), (fileptr, line)


@fact("RNG")
def scatter_and_aim_position(img):
    s = Asm(img, 0x6CC399, 0x260)
    s.seq(r"movss xmm0, dword ptr \[ecx \+ 0x34\]", r"call 0x6cb85a", r"lea eax, \[ecx \+ 0x14c\]",
          r"push 0x659", rf"call {RNG_REAL:#x}", r"call 0x441c56", r"call 0x441bf4", r"push 0x667",
          r"push 0x66b", rf"call {RNG_REAL:#x}", r"call 0x42f4e0", r"call 0x42f4d0")
    # fcos / fsin wrappers
    assert Asm(img, 0x42F4E0, 0x10).has(r"fcos") is None and Asm(img, 0x42F4D0, 0x10).has(r"fsin") is None
    a = Asm(img, 0x6CB85A, 0x140)
    a.seq(r"call 0x6cb81a", r"push 0x6d5", rf"push {WEAPON_CPP:#x}", r"push 0xbc614e", rf"call {RNG_INT:#x}", r"call 0x690bd2")
    assert 0xBC614E == 12345678
    h = Asm(img, 0x6CC915, 0x700)
    h.seq(r"push 0x5d1", rf"call {RNG_REAL:#x}", r"push 0x5d7", rf"call {RNG_REAL:#x}", r"push 0x5e1", rf"call {RNG_REAL:#x}", r"call 0x6cc399")
    h.has(r"cmp eax, dword ptr \[ecx \+ 0x30\]")                      # FX only when frame >= weapon.+0x30 (SuspendFX)


# --------------------------------------------------------------------------------------------------
# runner
# --------------------------------------------------------------------------------------------------


def run(img, only=None):
    failed = 0
    for f in FACTS:
        if only and f.topic not in only:
            continue
        try:
            f.fn(img)
            print(f"PASS {f.topic:9s} {f.name}")
        except Exception as e:                      # noqa: BLE001
            failed += 1
            print(f"FAIL {f.topic:9s} {f.name}: {type(e).__name__}: {e}")
    return failed


# ==================================================================================================
# REFERENCE MODEL (decoded logic; not retail code). Used for golden timing sequences.
# ==================================================================================================


def f32r(x):
    return struct.unpack("<f", struct.pack("<f", float(x)))[0]


def pc24(x):
    """Round an exact rational to a 24-bit significand, nearest-even, unbounded exponent (x87 PC24 with wide exponent)."""
    x = Fraction(x)
    if x == 0:
        return x
    s = 1
    if x < 0:
        s, x = -1, -x
    e = (x.numerator.bit_length() - x.denominator.bit_length()) - 24
    while True:
        m = x / Fraction(2) ** e
        if m < 2 ** 23:
            e -= 1
        elif m >= 2 ** 24:
            e += 1
        else:
            break
    fl = m.numerator // m.denominator
    rem = m - fl
    if rem > Fraction(1, 2) or (rem == Fraction(1, 2) and fl % 2 == 1):
        fl += 1
    return s * Fraction(fl) * Fraction(2) ** e


MS_TO_FRAMES = Fraction(f32r(0.005))


def ms_to_frames(ms):
    """parseDurationUnsignedInt RW 0x73A429: fild (int32; +2^32 if negative), fmul 0.005f (PC24), fstp qword, ceil."""
    ms &= 0xFFFFFFFF
    v = Fraction(ms if ms < 2 ** 31 else ms - 2 ** 32)
    if ms >= 2 ** 31:
        v = pc24(v + 2 ** 32)
    return math.ceil(pc24(v * MS_TO_FRAMES)) & 0xFFFFFFFF


def u32(x):
    return x & 0xFFFFFFFF


def ctrunc(x):
    return int(math.trunc(x))


def cdiv_rem(a, b):
    r = abs(a) % abs(b)
    return -r if a < 0 else r


class Tmpl:
    """Weapon template fields the timing machine reads (all durations already in frames)."""

    def __init__(self, **kw):
        self.clip_size = 0
        self.delay_min = self.delay_max = 0
        self.reload_min = self.reload_max = 0
        self.pre_delay = self.pre_rand = 0
        self.pre_type = 0                      # 0 PER_SHOT 1 PER_ATTACK 2 PER_CLIP 3 PER_POSITION
        self.firing_duration = 0
        self.auto_reloads = 0                  # 0 YES 1 NO 2 RETURN_TO_BASE
        self.idle_after = 0xFFFFFFFF
        self.shots_per_barrel = 1
        self.hold_during_reload = False
        self.leech = False
        self.__dict__.update(kw)


class Weapon:
    """Weapon instance (RW layout: status +0x10, ammo +0x14, whenWeCanFireAgain +0x18, whenPreAttackFinished +0x1C,
    whenFiringEnds +0x20, followThruEnd +0x24, whenLastReloadStarted +0x28, lastFireFrame +0x2C, ...)."""

    def __init__(self, t):
        self.t = t
        self.status, self.ammo = 1, 0
        self.wcf = self.wpf = self.wfe = self.wpe2 = self.wlrs = self.last = 0
        self.max_shot, self.barrel, self.shots_barrel = 0x7FFFFFFF, 0, t.shots_per_barrel
        self.jitter = self.leech_until = 0
        self.rng_log = []
        self.disabled_until = None

    def _rng(self, rng, lo, hi, line):
        v = rng(lo, hi)
        self.rng_log.append((line, lo, hi, v))
        return v

    def clip_reload_time(self, bonus, rng):                 # RW 0x6CA0C0
        t = self.t
        v = t.reload_min if t.reload_min == t.reload_max else self._rng(rng, t.reload_min, t.reload_max, 1006)
        v -= cdiv_rem(v, 3)
        return math.floor(pc24(Fraction(v) / Fraction(f32r(bonus[3]))))

    def delay_between_shots(self, bonus, mult, rng):        # RW 0x6CA066
        t = self.t
        v = t.delay_min if t.delay_min == t.delay_max else self._rng(rng, t.delay_min, t.delay_max, 988)
        m = pc24(Fraction(f32r(bonus[3])) * Fraction(f32r(mult)))
        return math.floor(pc24(Fraction(v) / m))

    def compute(self, now):                                 # RW 0x6CD142 (ProjectileFilterInContainer not modelled)
        if now < self.wpf:
            return 4, False
        if now < self.wfe:
            return 5, False
        if now < self.wcf:
            return self.status, True
        return (0 if self.ammo > 0 else 1), True

    def get_status(self, now):                              # RW 0x6CDCE7 (write-back wrapper)
        st, valid = self.compute(now)
        if valid:
            self.status = st
        return st

    def percent_ready(self, now):                           # RW 0x6CDB73
        st = self.compute(now)[0]
        if st == 0:
            return 1.0
        if st in (1, 4) or st > 5:
            return 0.0
        if now >= self.wcf:
            return 1.0
        total = u32(self.wcf - self.wlrs)
        if total == 0:
            return 1.0
        prog = u32(total - self.wcf + now)
        return 1.0 if prog >= total else f32r(prog / total)

    def reload(self, now, bonus, rng, instant=False):       # RW 0x6CE8B9
        self.ammo = self.t.clip_size or 0x7FFFFFFF
        self.status = 3
        rt = 0 if instant else self.clip_reload_time(bonus, rng)
        self.wlrs = now
        self.wcf = u32(ctrunc(pc24(Fraction(now) + Fraction(f32r(rt)))))
        if rt > 0 and self.t.hold_during_reload:
            self.disabled_until = self.wcf                  # Object::setDisabledUntil(DISABLED_TEMPORARILY_BUSY = 8, wcf)

    def pre_attack_delay(self, bonus, ft_same_target=0, ft_same_pos=False):   # RW 0x6CDD10
        t = self.t
        if t.pre_type == 2:
            if t.clip_size > 0 and self.ammo < t.clip_size:
                return 0
        elif t.pre_type == 1:
            if ft_same_target > 0:
                return 0
        elif t.pre_type == 3:
            if ft_same_pos:
                return 0
        return ctrunc(f32r(f32r(t.pre_delay) * f32r(bonus[4]))) + self.jitter

    def pre_fire(self, now, rng, bonus=(1.0,) * 6, **kw):   # RW 0x6CE95D
        t = self.t
        self.jitter = self._rng(rng, 0, t.pre_rand, 3644) if t.pre_rand else 0
        d = self.pre_attack_delay(bonus, **kw)
        if d <= 0:
            return d
        self.status = 4
        self.wpf = u32(now + d)
        if t.firing_duration > 0:
            self.wpe2 = u32(now + t.firing_duration + d)
        if t.leech:
            self.leech_until = u32(ctrunc(pc24(Fraction(t.pre_delay) * Fraction(f32r(bonus[4])))) + now + t.firing_duration)
        return d

    def fire(self, now, rng, bonus=(1.0,) * 6, attr_rof=1.0, barrel_count=1):   # RW 0x6CEF6D, plain target, no scatter list
        if self.get_status(now) != 0:
            return False, False
        delay = self.delay_between_shots(bonus, attr_rof, rng)
        if self.ammo == 0:
            return False, False
        if self.barrel >= barrel_count:
            self.barrel, self.shots_barrel = 0, self.t.shots_per_barrel
        self.ammo -= 1
        self.max_shot -= 1
        self.last = now
        self.shots_barrel -= 1
        if self.shots_barrel <= 0:
            self.barrel += 1
            self.shots_barrel = self.t.shots_per_barrel
        if self.ammo == 0:
            if self.t.auto_reloads == 0:
                self.reload(now, bonus, rng)
                return True, True
            self.status, self.wcf = 1, 0x7FFFFFFF
            return True, False
        self.status = 2
        self.wcf, self.wlrs = u32(delay + now), now
        if self.t.firing_duration > 0:
            self.wfe = u32(now + self.t.firing_duration)
        return True, False


def _lo(lo, hi):
    return lo


def drive(t, frames, pre_fire_at=(), rng=_lo, load=True, **prekw):
    """Fire whenever READY. Returns [(frame, status_before, status_after, fired, ammo)] plus the weapon."""
    w = Weapon(t)
    if load:
        w.reload(0, (1.0,) * 6, rng, instant=True)
    rows = []
    for f in range(frames):
        if f in pre_fire_at:
            w.pre_fire(f, rng, **prekw)
        before = w.get_status(f)
        fired = False
        if before == 0:
            fired, _ = w.fire(f, rng)
        rows.append((f, before, w.get_status(f), fired, w.ammo))
    return rows, w


def golden():
    """Golden sequences derived statically from the decoded logic (see notes, section 5)."""
    out = {}
    ms = ms_to_frames
    # G1: ClipSize 3, DelayBetweenShots 1000, ClipReloadTime 5000, AutoReloadsClip YES, clip loaded instantly at frame 0.
    t = Tmpl(clip_size=3, delay_min=ms(1000), delay_max=ms(1000), reload_min=ms(5000), reload_max=ms(5000))
    assert (t.delay_min, t.reload_min) == (5, 25)
    rows, w = drive(t, 40)
    out["G1"] = [(f, a) for f, b, a, fi, am in rows]
    out["G1_fire_frames"] = [f for f, b, a, fi, am in rows if fi]
    # G2: PER_ATTACK PreAttackDelay 500, FiringDuration 400, DelayBetweenShots 1000, infinite clip; preFire at frame 0, same-target after.
    t = Tmpl(pre_type=1, pre_delay=ms(500), firing_duration=ms(400), delay_min=ms(1000), delay_max=ms(1000))
    assert (t.pre_delay, t.firing_duration, t.delay_min) == (3, 2, 5)
    rows, w = drive(t, 14, pre_fire_at=(0,), ft_same_target=0)
    out["G2"] = [(f, a) for f, b, a, fi, am in rows]
    out["G2_fire_frames"] = [f for f, b, a, fi, am in rows if fi]
    # G3: PER_SHOT PreAttackDelay 500 + PreAttackRandomAmount 200 (RNG forced to the upper bound), clip 1, reload Min:5000 Max:6000
    t = Tmpl(clip_size=1, pre_delay=ms(500), pre_rand=ms(200), reload_min=ms(5000), reload_max=ms(6000))
    assert (t.pre_rand, t.reload_min, t.reload_max) == (1, 25, 30)
    hi = lambda lo, hi_: hi_
    rows, w = drive(t, 8, pre_fire_at=(0,), rng=hi)
    out["G3"] = [(f, a) for f, b, a, fi, am in rows]
    out["G3_rng"] = list(w.rng_log)
    out["G3_wcf"] = w.wcf
    # G4: real retail values: EagleClawAttack ClipSize 2, DelayBetweenShots 250, ClipReloadTime 2000, AutoReloadsClip Yes
    t = Tmpl(clip_size=2, delay_min=ms(250), delay_max=ms(250), reload_min=ms(2000), reload_max=ms(2000))
    assert (t.delay_min, t.reload_min) == (2, 10)
    rows, w = drive(t, 20)
    out["G4"] = [(f, a) for f, b, a, fi, am in rows]
    out["G4_fire_frames"] = [f for f, b, a, fi, am in rows if fi]
    # G5: percent ready of G1's weapon after the first shot (BETWEEN, delay 5): frames 1..4
    t = Tmpl(clip_size=3, delay_min=5, delay_max=5, reload_min=25, reload_max=25)
    w = Weapon(t)
    w.reload(0, (1.0,) * 6, _lo, instant=True)
    w.fire(0, _lo)
    out["G5"] = [(f, w.percent_ready(f)) for f in range(0, 7)]
    return out


EXPECTED_G1 = [(0, 2), (1, 2), (2, 2), (3, 2), (4, 2), (5, 2), (6, 2), (7, 2), (8, 2), (9, 2), (10, 3)] + [(f, 3) for f in range(11, 34)] + [(34, 2), (35, 2), (36, 2), (37, 2), (38, 2), (39, 2)]
EXPECTED_G2 = [(0, 4), (1, 4), (2, 4), (3, 5), (4, 5), (5, 2), (6, 2), (7, 2), (8, 5), (9, 5), (10, 2), (11, 2), (12, 2), (13, 5)]


def self_test():
    g = golden()
    assert g["G1_fire_frames"] == [0, 5, 10, 34, 39], g["G1_fire_frames"]
    assert g["G1"] == EXPECTED_G1
    assert g["G2_fire_frames"] == [3, 8, 13], g["G2_fire_frames"]
    assert g["G2"] == EXPECTED_G2
    assert g["G3_rng"] == [(3644, 0, 1, 1), (1006, 25, 30, 30)] and g["G3_wcf"] == 4 + 30
    assert g["G4_fire_frames"] == [0, 2, 11, 13], g["G4_fire_frames"]
    assert ms_to_frames(52428805) == 262145 and ms_to_frames(0) == 0 and ms_to_frames(1) == 1
    return g


def main(argv):
    if "--golden" in argv:
        g = self_test()
        for k, v in g.items():
            print(k, v)
        return 0
    if not os.environ.get("RW_GAME_DAT"):
        print("SKIP: RW_GAME_DAT is not set (path of the RotWK game.dat); binary-fact checks NOT run")
        return 77
    self_test()
    from rwimage import Image
    img = Image()
    if "--list" in argv:
        for f in FACTS:
            print(f"{f.topic:9s} {f.name}")
        return 0
    failed = run(img)
    print(f"{len(FACTS) - failed}/{len(FACTS)} facts hold")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
