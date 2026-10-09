#!/usr/bin/env python3
"""Extract the ModuleFactory registry and the object field grammar from the RotWK game.dat.

    RW_GAME_DAT=<path to RotWK game.dat> python tools/rw_object_model/extract.py <output dir>

Writes (deterministically, sorted keys, LF) :
    module-registry.json   every addModule registration with its interface mask and predicates
    field-tables.json      the object and audio field tables and the grammar closure below them

The outputs are the engine's embedded data (engine/data/rotwk-201/) and the goldens the tests
compare against. See rwtables.py and module_registry.py for the binary facts they rest on.
"""
from __future__ import annotations

import json
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))

from grammar_closure import closure, hx  # noqa: E402
from module_registry import build_registry  # noqa: E402
from object_fields import (CONDITIONAL, DISPATCH, NAME_LIST_SEMANTICS, NULL_GUARDED, OBJECT_FN_SEMANTICS)  # noqa: E402
from rwimage import Image  # noqa: E402
from rwtables import Tables, GrammarError  # noqa: E402

OBJECT_TABLE = 0xDA3DF8   # RW: 191 rows
AUDIO_TABLE = 0xC26720    # RW: 56 rows, applied at extra offset 0x124
AUDIO_EXTRA = 0x124
EMPTY_BUILD_PROC = 0x9F3A3C  # `ret 4`: a build proc that adds no table


def name_array(img, va):
    out = []
    while True:
        p = img.u32(va)
        if p == 0:
            return out
        out.append(img.cstr(p))
        va += 4


def build(img):
    T = Tables(img)
    classes, sites, sequence = build_registry(img)
    roots = [(OBJECT_TABLE, 0, None), (AUDIO_TABLE, AUDIO_EXTRA, None)]
    for c in classes:
        for t, e, o in c["tableRefs"]:
            roots.append((t, e, o))
    for d in DISPATCH.values():  # tables the closure cannot see (reached through vtable dispatch)
        for vt in d["vtables"]:
            proc = img.u32(vt + 8)
            if proc != EMPTY_BUILD_PROC:
                roots.extend(T.build_proc_tables(proc))
    tables, functions = closure(img, roots)

    # --- function grammar, with the reviewed annotations -------------------------------------
    fn_out = {}
    for f, info in sorted(functions.items()):
        e = {"kind": info["kind"]}
        if info["kind"] == "script":
            e["terminator"] = info["terminator"]
        elif info["kind"] == "block":
            e["tables"] = [{"table": t["table"], "extra": t["extra"]} for t in info["tables"]]
            if f in DISPATCH:
                d = DISPATCH[f]
                names = name_array(img, d["names_va"])
                per_type = []
                for vt in d["vtables"]:
                    per_type.append([{"table": _reg(tables, T, img, tt), "extra": ee} for tt, ee, _o in T.build_proc_tables(img.u32(vt + 8))] if img.u32(vt + 8) != EMPTY_BUILD_PROC else [])
                e["tables"] = []
                e["dispatch"] = {"names": names, "tables": per_type, "evidence": d["evidence"]}
            elif not info["resolved"]:
                raise GrammarError(f"block function {f:#x} has unresolved tables: {info['notes']}")
            if info.get("conditional"):
                if f in NULL_GUARDED:
                    e["nullGuarded"] = NULL_GUARDED[f]
                elif f in CONDITIONAL:
                    e["conditional"] = {k: v for k, v in CONDITIONAL[f].items()}
                else:
                    raise GrammarError(f"block function {f:#x} opens its block conditionally and has not been reviewed: {info['notes']}")
        fn_out[hx(f)] = e

    # --- tables ---------------------------------------------------------------------------
    t_out = {}
    for tid, t in sorted(tables.items()):
        rows = []
        for (nm, f, u, o) in t["rows"]:
            row = {"name": nm, "fn": hx(f), "userData": u if u is not None else 0, "offset": o if o is not None else 0}
            if u is None or o is None:
                row["runtimeValues"] = True  # a lazily initialised row whose userData / offset is an argument of the init code
            sem = OBJECT_FN_SEMANTICS.get(f)
            if sem and sem[0] in NAME_LIST_SEMANTICS and u:
                row["names"] = name_array(img, u)
            rows.append(row)
        t_out[tid] = {"rows": rows, "terminated": t["terminated"]}
        if t["catchAll"]:
            t_out[tid]["catchAll"] = hx(t["catchAll"])
        if t.get("lazy"):
            t_out[tid]["lazyInit"] = True
    # fn semantics (object table only)
    obj_rows = tables[hx(OBJECT_TABLE)]["rows"] + tables[hx(AUDIO_TABLE)]["rows"]
    sem_out = {}
    for (nm, f, u, o) in obj_rows:
        s = OBJECT_FN_SEMANTICS.get(f)
        sem_out[hx(f)] = {"semantic": s[0], "evidence": s[1]} if s else {"semantic": "raw"}
    field_tables = {
        "schema": "openbfme.rw-field-tables",
        "schemaVersion": 1,
        "binary": "RotWK 2.01 game.dat (stop S-001: community-modified image)",
        "objectTable": {"table": hx(OBJECT_TABLE), "extra": 0},
        "audioTable": {"table": hx(AUDIO_TABLE), "extra": AUDIO_EXTRA},
        "objectFunctions": sem_out,
        "functions": fn_out,
        "tables": t_out,
    }
    registry = {
        "schema": "openbfme.rw-module-registry",
        "schemaVersion": 1,
        "binary": "RotWK 2.01 game.dat (stop S-001: community-modified image)",
        "addModuleSites": len(sites),
        # the addModule calls in the order the binary executes them (RW 0x464AD2 runs the 0x6579C9 function first);
        # ModuleFactory replays it so the NAMEKEY ids match retail's
        "registrationSequence": [{"site": hx(q["site"]), "name": q["name"], "type": q["type"]} for q in sequence],
        "moduleTypes": {"0": "BEHAVIOR", "1": "DRAW", "2": "CLIENT_UPDATE", "3": "CLIENT_BEHAVIOR"},
        "classes": [
            {
                "name": c["name"], "type": c["type"], "mask": c["mask"],
                "isAiModuleData": c["isAiModuleData"], "slot7": c["slot7"],
                "createData": hx(c["createData"]), "create": hx(c["create"]), "extra": hx(c["extra"]),
                "vtable": hx(c["vtable"]), "sites": [hx(s) for s in c["sites"]],
                "tables": [{"table": hx(t), "extra": e} for t, e, _o in c["tableRefs"]],
            }
            for c in classes
        ],
    }
    return registry, field_tables


def _reg(tables, T, img, va):
    tid = hx(va)
    if tid not in tables:
        raise GrammarError(f"dispatch table {tid} is not part of the closure")
    return tid


def dump(obj, path):
    Path(path).write_text(json.dumps(obj, indent=1, sort_keys=True) + "\n", encoding="utf-8", newline="\n")


def main(argv):
    if len(argv) != 2:
        print(__doc__)
        return 2
    out = Path(argv[1])
    out.mkdir(parents=True, exist_ok=True)
    registry, field_tables = build(Image())
    dump(registry, out / "module-registry.json")
    dump(field_tables, out / "field-tables.json")
    print(f"{len(registry['classes'])} classes, {len(field_tables['tables'])} tables, {len(field_tables['functions'])} functions")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
