#!/usr/bin/env python3
"""Lane AI-2: the logic random draw sites each RotWK offensive AI tactic can reach (read from the user's game.dat at run time; nothing retail is stored).

For every offensive prototype of RW 0x90BE31 the vtable's setup (vslot 3), launch (vslot 6) and started update (vslot 7) are walked through their direct calls
(depth 4, callees inside the SkirmishAI code RW 0x8E0000..0x9F0000 and AIGroup RW 0x76F000..0x77A000) and every call of GetGameLogicRandomValue
(RW 0x6D328E) / GetGameLogicRandomValueReal (RW 0x6D332C) is listed with its function. Function bodies end at the next call target or vtable entry.

    python tools/skirmish_ai/tactic_draws.py [game.dat]      (default: $RW_GAME_DAT)
"""
from __future__ import annotations

import bisect
import json
import os
import re
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "retail_oracle"))
from disasm import read_va, sections  # noqa: E402

RANDOM_INT = 0x6D328E
RANDOM_REAL = 0x6D332C
TACTICS = {  # name -> vtable (RW, the ctor strings of RW 0x90BE31's prototypes)
    "SimpleAttack": 0xC89848,
    "FormationAttack": 0xC89800,
    "FlankAttack": 0xC89720,
    "PincerAttack": 0xC89634,
    "FeintAttack": 0xC8954C,
    "AIBasePenetrationTroopsTactic": 0xC894F4,
    "SimpleSiege": 0xC894B0,
    "SiegeGates": 0xC89460,
}
SLOTS = {"setup": 3, "launch": 6, "update": 7}


def disassemble(data: bytes):
    import capstone

    base, secs = sections(data)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.skipdata = True
    out = []
    for name, _vsize, sva, rsize, rptr in secs:
        if not name.startswith(b".text"):
            continue
        for a, _s, m, o in md.disasm_lite(data[rptr:rptr + rsize], base + sva):
            out.append((a, m, o))
    return out


def draw_table(data: bytes) -> dict:
    ins = disassemble(data)
    addrs = [a for a, _m, _o in ins]
    starts = set()
    for _a, m, o in ins:
        if m == "call" and re.fullmatch(r"0x[0-9a-f]+", o):
            starts.add(int(o, 16))
    vt = {}
    for name, table in TACTICS.items():
        vt[name] = [struct.unpack_from("<I", read_va(data, table + 4 * i, 4))[0] for i in range(14)]
        starts.update(vt[name])
    ordered = sorted(starts)

    def body(f):
        j = bisect.bisect_right(ordered, f)
        end = ordered[j] if j < len(ordered) else f + 0x1000
        return ins[bisect.bisect_left(addrs, f):bisect.bisect_left(addrs, end)]

    def draws(f):
        seen, stack, found = set(), [(f, 0)], []
        while stack:
            g, depth = stack.pop()
            if g in seen:
                continue
            seen.add(g)
            for a, m, o in body(g):
                if m != "call" or not re.fullmatch(r"0x[0-9a-f]+", o):
                    continue
                t = int(o, 16)
                if t in (RANDOM_INT, RANDOM_REAL):
                    found.append(("int" if t == RANDOM_INT else "real", "0x%X" % a))
                elif depth < 4 and (0x8E0000 <= t < 0x9F0000 or 0x76F000 <= t < 0x77A000):
                    stack.append((t, depth + 1))
        return sorted(set(found), key=lambda x: x[1])

    return {name: {slot: [list(x) for x in draws(vt[name][i])] for slot, i in SLOTS.items()} for name in TACTICS}


def main() -> int:
    path = sys.argv[1] if len(sys.argv) > 1 else os.environ.get("RW_GAME_DAT")
    if not path:
        print("usage: tactic_draws.py <game.dat> (or set RW_GAME_DAT)")
        return 2
    print(json.dumps(draw_table(Path(path).read_bytes()), indent=1, sort_keys=True))
    return 0


if __name__ == "__main__":
    sys.exit(main())
