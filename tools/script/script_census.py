#!/usr/bin/env python3
"""Independent census of every map script of the pure RotWK 2.01 + BFME2 1.06 mount (lane SCRIPT-1).

    ROTWK_INSTALL=<dir> BFME2_INSTALL=<dir> python3 tools/script/script_census.py [--check]

Shares no code with the C++ engine: the mount is tools/fx/bigfs.py (pinned archive policy order, first archive wins), the RefPack envelope,
the table of contents and the top-level chunk walk are tools/maps/oracle/census.py (the spec author's reference reader), and the script subtree
(script_records: census.py's generic walker guesses container prefixes and mis-reads some Script headers) and its records are decoded below from the layouts of ZH Scripts.cpp as BFME2 writes them (Open-BFME-2 ConditionWriteDataChunk.cpp / ScriptActionWriteAction.cpp:
Condition v4+ = type, NameKey (int: Dict type 3 in the low byte, the table-of-contents id above it), parameters, v5 two ints; ScriptAction v2+ = type, NameKey, parameters, v3 one int;
Parameter = type, then three floats for type 16 (COORD3D) else int, float, ascii string).

Every condition / action is resolved against the binary's registry (engine/src/GameLogic/ScriptEngine/ScriptTemplateTables.inc, extracted
by tools/script/extract_script_templates.py) with the RotWK parsers' rules (resolve() below); the census counts the RESOLVED types and lists
every record whose resolved ordinal differs from the stored one (a renumbering, or a record retail turns into CONDITION_FALSE / NO_OP).

Writes engine/tests/data/script-census.json (names and counts only, no retail bytes); --check fails when the committed file differs.
"""
from __future__ import annotations

import collections
import json
import os
import re
import struct
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent.parent
OUT = ROOT / "engine/tests/data/script-census.json"
TABLES = ROOT / "engine/src/GameLogic/ScriptEngine/ScriptTemplateTables.inc"
sys.path.insert(0, str(ROOT / "tools" / "fx"))
sys.path.insert(0, str(ROOT / "tools" / "maps" / "oracle"))


def load_registry():
    reg = {"condition": {}, "action": {}}
    for line in TABLES.read_text().splitlines():
        m = re.match(r'OPENBFME_SCRIPT_(CONDITION|ACTION)_TEMPLATE\((\d+), "([^"]*)", (\d+), (\d+), (\d+), \{([^}]*)\}\)', line)
        if m:
            reg[m.group(1).lower()][int(m.group(2))] = (m.group(3), int(m.group(6)), int(m.group(5)))
    return reg


class Reader:
    def __init__(self, body, p, end, names):
        self.b, self.p, self.end, self.names = body, p, end, names

    def i32(self):
        v = struct.unpack_from("<i", self.b, self.p)[0]
        self.p += 4
        return v

    def f32(self):
        v = struct.unpack_from("<f", self.b, self.p)[0]
        self.p += 4
        return v

    def u8(self):
        v = self.b[self.p]
        self.p += 1
        return v

    def astr(self):
        n = struct.unpack_from("<H", self.b, self.p)[0]
        self.p += 2
        s = self.b[self.p:self.p + n].decode("latin-1")
        self.p += n
        return s

    def namekey(self):
        v = struct.unpack_from("<I", self.b, self.p)[0]
        self.p += 4
        if v & 0xFF != 3:  # the Dict type byte: 3 = ascii string
            raise ValueError("NameKey of type %d" % (v & 0xFF))
        k = v >> 8
        if k not in self.names:
            raise ValueError("NameKey %d not in the table of contents" % k)
        return self.names[k]


def script_records(body, names, rec):
    """The PlayerScriptsList subtree with the exact chunk layouts (the generic walker of census.py guesses container prefixes and can take a
    Script header for children): yields (path, version, size, payload position) like census.walk, every chunk of the subtree."""
    top = [r for r in rec if r[0] == "/PlayerScriptsList"]
    out = []

    def chunks(p, end, path):
        while p < end:
            cid, ver, size = struct.unpack_from("<IHi", body, p)
            label = names.get(cid, "?%d" % cid)
            full = path + "/" + label
            pos = p + 10
            out.append((full, ver, size, pos))
            r = Reader(body, pos, pos + size, names)
            if label in ("PlayerScriptsList", "ScriptList", "OrCondition"):
                chunks(pos, pos + size, full)
            elif label == "ScriptGroup":
                r.astr(); r.u8()
                if ver >= 2:
                    r.u8()
                chunks(r.p, pos + size, full)
            elif label == "Script":
                for _ in range(4):
                    r.astr()
                for _ in range(6):
                    r.u8()
                if ver >= 2:
                    r.i32()
                if ver >= 3:
                    r.u8(); r.u8(); r.i32(); r.u8(); r.astr()
                if ver >= 4:
                    r.astr()
                chunks(r.p, pos + size, full)
            elif label not in ("Condition", "ScriptAction", "ScriptActionFalse"):
                raise ValueError("unexpected chunk %s in the script tree" % full)
            p = pos + size
        if p != end:
            raise ValueError("script tree chunk overrun at %s" % path)

    for full, ver, size, pos in top:
        chunks(pos - 10, pos + size, "")
    return out


def params(r, values=None):
    n = r.i32()
    out = []
    for _ in range(n):
        t = r.i32()
        if t == 16:
            v = (r.f32(), r.f32(), r.f32())
        else:
            v = (r.i32(), r.f32(), r.astr())
        out.append(t)
        if values is not None:
            values.append((t, v))
    return out


def dump(path):
    """--dump <archive path>: prints the script tree of one file (diagnostics, not part of the census)"""
    import bigfs
    import census

    mount = bigfs.Mount()
    body, _env = census.decode(mount.read(path))
    names, p = census.parse_toc(body)
    rec = []
    census.walk(body, p, len(body), names, "", rec)
    for full, ver, size, pos in script_records(body, names, rec):
        depth = full.count("/") - 1
        leaf = full.rsplit("/", 1)[-1]
        r = Reader(body, pos, pos + size, names)
        if leaf in ("Script", "ScriptGroup"):
            nm = r.astr()
            if leaf == "Script":
                r.astr(); r.astr(); r.astr()
                flags = [r.u8() for _ in range(6)]
                delay = r.i32() if ver >= 2 else 0
                seq = ""
                if ver >= 3:
                    fire, loop, count, ttype, tname = r.u8(), r.u8(), r.i32(), r.u8(), r.astr()
                    if fire:
                        seq = " seq=%s:%r loop=%d/%d" % ("unit" if ttype else "team", tname, loop, count)
                print("  " * depth + "%s %r active=%d oneShot=%d e/n/h=%d%d%d sub=%d delay=%d%s" % (leaf, nm, flags[0], flags[1], flags[2], flags[3], flags[4], flags[5], delay, seq))
            else:
                print("  " * depth + "%s %r active=%d" % (leaf, nm, r.u8()))
        elif leaf in ("Condition", "ScriptAction", "ScriptActionFalse"):
            r.i32()
            name = r.namekey()
            vals = []
            params(r, vals)
            shown = []
            for t, v in vals:
                shown.append("%d:%s" % (t, ("%.1f,%.1f,%.1f" % v) if t == 16 else (v[2] if v[2] else ("%d/%g" % (v[0], v[1])))))
            prefix = {"Condition": "IF", "ScriptAction": "DO", "ScriptActionFalse": "ELSE"}[leaf]
            if leaf == "Condition" and ver >= 5:
                # the v5 flags (Condition + 0x4C enabled, + 0x4D inverted: RW 0x7ED72C inverts an ordinal >= 5's answer; lane CAMP-1)
                enabled, inverted = r.i32(), r.i32()
                prefix += (" NOT" if inverted else "") + ("" if enabled else " (disabled)")
            print("  " * depth + "%s %s(%s)" % (prefix, name, ", ".join(shown)))
        elif leaf == "ScriptList":
            print("  " * depth + "ScriptList")
        elif leaf == "OrCondition":
            print("  " * depth + "OR")


# The ordinals ScriptEngine evaluates / executes itself, never reaching the ScriptConditions / ScriptActions switch (RW 0x6092A9: conditions 0 .. 4;
# RW 0x60C1C9: actions 1, 2, 5, 6, 8, 9, 10, 15, 16, 20, 103, 124 .. 127, 132 .. 134, 150 .. 155, 373 .. 375, 415, 416, 439, 440, 508).
ENGINE_OWN = {
    "condition": {0, 1, 2, 3, 4},
    "action": {1, 2, 5, 6, 8, 9, 10, 15, 16, 20, 103, 124, 125, 126, 127, 132, 133, 134, 150, 151, 152, 153, 154, 155, 373, 374, 375, 415, 416,
               439, 440, 508},
    "falseAction": set(),
}
ENGINE_OWN["falseAction"] = ENGINE_OWN["action"]


def resolve(table, kind, stored, name, nparams, version):
    """The RotWK parsers' ordinal: RW 0x7B776D (conditions) / RW 0x7B68F9 (actions). Conditions: version >= 4 re-matches by NameKey (the
    stored ordinal's template first, then ordinals 0 .. 202); no match (or version < 4) -> CONDITION_FALSE. Actions: version >= 2 re-matches the
    same way over 0 .. 599 (ordinal 385 keeps its slot when its template's name is one of the BUILD_BASE_BUILDING_PER_TACTICAL... spellings); no match
    -> NO_OP (5). A parameter count that differs from the template's (after the heals, which no retail record needs) -> FALSE / NO_OP."""
    by_name = {}
    for i in sorted(table):
        by_name.setdefault(table[i][0], i)
    if kind == "condition":
        if version < 4:
            return 0
        t = stored if stored in table and table[stored][0] == name else by_name.get(name, -1)
        if t < 0:
            return 0
        return t if table[t][1] == nparams else 0
    t = stored
    if version >= 2 and not (stored in table and table[stored][0] == name):
        t = by_name.get(name, -1)
        # RW 0x7B6992 / 0x7B69AE compare the TEMPLATE's name at ordinal 385 with the marker spellings, not the stored name
        if stored == 385 and stored in table and table[stored][0] in ("BUILD_BASE_BUILDING_PER_TACTICAL_MARKER",
                                                                       "BUILD_BASE_BUILDING_WITH_TACTICAL_MARKER",
                                                                       "BUILD_BASE_BUILDING_PER_TACTIC_MARKER"):
            t = 385
        if t < 0:
            return 5
    if t not in table or table[t][1] != nparams:
        return 5
    return t


def category(path):
    p = path.lower()
    if p.startswith("libraries\\"):
        return "library"
    base = p.split("\\")[-1]
    if base.startswith("map wor "):
        return "war_of_the_ring"
    if base.startswith(("map ang ", "map good ", "map evil ", "map beginner", "map advanced")):
        return "campaign"
    if base.startswith("map mp ") or "\\map mp " in p:
        return "skirmish"
    return "other"


def main():
    if "--dump" in sys.argv:
        dump(sys.argv[sys.argv.index("--dump") + 1])
        return 0
    import bigfs
    import census

    reg = load_registry()
    mount = bigfs.Mount()
    files = [n for n in mount.files if n.endswith(".map") or n.endswith(".scb")]
    res = {"files": {}, "condition": {}, "action": {}, "falseAction": {}, "mismatches": [], "categories": {}}
    uses = {"condition": collections.Counter(), "action": collections.Counter(), "falseAction": collections.Counter()}
    files_with = {k: collections.defaultdict(set) for k in uses}
    no_case = {k: collections.Counter() for k in uses}
    cats = collections.defaultdict(lambda: collections.Counter())
    for path in sorted(files):
        blob = mount.read(path)
        if not blob:
            res["files"][path] = {"empty": True}
            continue
        body, _env = census.decode(blob)
        names, p = census.parse_toc(body)
        rec = []
        census.walk(body, p, len(body), names, "", rec)
        stat = collections.Counter()
        cat = category(path)
        for full, ver, size, pos in script_records(body, names, rec):
            leaf = full.rsplit("/", 1)[-1]
            if "/PlayerScriptsList/" not in full and not full.startswith("/PlayerScriptsList"):
                continue
            if leaf == "Script":
                stat["scripts"] += 1
            elif leaf == "ScriptGroup":
                stat["groups"] += 1
            elif leaf == "ScriptList":
                stat["lists"] += 1
            elif leaf in ("Condition", "ScriptAction", "ScriptActionFalse"):
                kind = {"Condition": "condition", "ScriptAction": "action", "ScriptActionFalse": "falseAction"}[leaf]
                r = Reader(body, pos, pos + size, names)
                typ = r.i32()
                name = r.namekey() if ver >= (4 if kind == "condition" else 2) else None
                ps = params(r)
                if kind == "condition" and ver >= 5:
                    r.i32(); r.i32()
                if kind != "condition" and ver >= 3:
                    r.i32()
                if r.p != pos + size:
                    raise ValueError("%s %s: %d leftover bytes" % (path, full, pos + size - r.p))
                table = reg["condition" if kind == "condition" else "action"]
                resolved = resolve(table, kind, typ, name, len(ps), ver)
                if resolved != typ:
                    res["mismatches"].append({"file": path, "kind": kind, "stored": typ, "name": name, "params": len(ps),
                                              "resolved": resolved, "resolvedName": table[resolved][0]})
                rname = table[resolved][0]
                uses[kind][rname] += 1
                files_with[kind][rname].add(path)
                if not table[resolved][2] and resolved not in ENGINE_OWN[kind]:
                    no_case[kind][rname] += 1
                stat[kind + "s"] += 1
                cats[cat][kind + "s"] += 1
        if stat:
            res["files"][path] = dict(sorted(stat.items()))
            cats[cat]["files"] += 1
            cats[cat]["scripts"] += stat["scripts"]
    for kind in uses:
        res[kind] = {n: {"uses": c, "files": len(files_with[kind][n])} for n, c in sorted(uses[kind].items())}
    res["categories"] = {k: dict(sorted(v.items())) for k, v in sorted(cats.items())}
    # uses whose resolved ordinal has no case in the retail switch (retail: false / nothing)
    res["noRetailCase"] = {k: dict(sorted(v.items())) for k, v in no_case.items()}
    res["totals"] = {
        "filesScanned": len(files),
        "filesWithScripts": sum(1 for v in res["files"].values() if v.get("scripts")),
        "scripts": sum(v.get("scripts", 0) for v in res["files"].values()),
        "groups": sum(v.get("groups", 0) for v in res["files"].values()),
        "conditions": sum(uses["condition"].values()),
        "actions": sum(uses["action"].values()),
        "falseActions": sum(uses["falseAction"].values()),
        "distinctConditionTypes": len(uses["condition"]),
        "distinctActionTypes": len(set(uses["action"]) | set(uses["falseAction"])),
        "rematched": len(res["mismatches"]),
        "registryConditions": len(reg["condition"]),
        "registryActions": len(reg["action"]),
    }
    text = json.dumps(res, indent=1, sort_keys=True) + "\n"
    if "--check" in sys.argv:
        if OUT.read_text() != text:
            print("%s differs from the mount" % OUT, file=sys.stderr)
            return 1
        return 0
    OUT.write_text(text)
    print(json.dumps(res["totals"], indent=1))
    print("mismatches:", len(res["mismatches"]))
    return 0


if __name__ == "__main__":
    sys.exit(main())
