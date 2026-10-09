#!/usr/bin/env python3
"""Independent oracle for the C++ map reader: surveys every pure RotWK 2.01 + BFME2 1.06 map and
writes engine/tests/data/map-survey.json (names and numbers only, no retail bytes).

It reuses the spec author's reference scripts, committed next to it in tools/maps/oracle/ (census.py: BIG
reader + RefPack decoder, strict about trailing bytes; fullparse.py: the reference chunk parser that decodes
181/181 maps with 0 leftover bytes; btdstats.py: per-map blend/plane statistics), adds per-map array
checksums, and also records the mapcache.ini entries. The C++ corpus test (engine/tests/test_map_corpus.cpp)
compares the engine's own parse against this file; nothing in the file comes from the C++ code, and the file
regenerates from a configured install:

  set ROTWK_INSTALL=<RotWK dir>  &  set BFME2_INSTALL=<BFME2 dir>
  python tools/maps/map_survey.py            (rewrites engine/tests/data/map-survey.json)
"""
import collections
import json
import os
import re
import struct
import sys
import zlib

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "engine", "tests", "data", "map-survey.json")

rotwk = os.environ.get("ROTWK_INSTALL")
bfme2 = os.environ.get("BFME2_INSTALL")
if not rotwk or not bfme2:
    sys.exit("set ROTWK_INSTALL and BFME2_INSTALL")
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "oracle"))
import btdstats    # noqa: E402
import census      # noqa: E402
import fullparse   # noqa: E402

# Pure 2.01 precedence exactly as the spec's fullparse.py (loose files never read; patch202 and HD never opened).
fullparse.PURE_BIGS = fullparse.resolve_pure_bigs(rotwk, bfme2)  # case-insensitive names; a missing archive raises
fullparse.LOOSE_ROOTS = []  # archives only: loose files are out of scope (plan rule 7)


def label(path):
    p = os.path.normpath(path)
    for tag, root in (("rotwk", rotwk), ("bfme2", bfme2)):
        if os.path.normcase(p).startswith(os.path.normcase(os.path.normpath(root))):
            return tag + ":" + os.path.basename(p).lower()
    return "?:" + os.path.basename(p).lower()


class Capture:
    """Wraps fullparse's height/blend parsers to checksum the raw arrays."""
    def __init__(self):
        self.hm = None
        self.btd = None


cap = Capture()
orig_hm = fullparse.LEAF["HeightMapData"]
orig_btd = fullparse.LEAF["BlendTileData"]


def wrap_hm(r, v, ctx):
    start = r.p
    orig_hm(r, v, ctx)
    n = ctx.w * ctx.h
    data = r.b[r.p - 2 * n:r.p] if v >= 5 else None
    hs = struct.unpack("<%dH" % n, data)
    cap.hm = {"crc": zlib.crc32(data), "min": min(hs), "max": max(hs), "sum": sum(hs)}


def wrap_btd(r, v, ctx):
    start = r.p
    orig_btd(r, v, ctx)
    n = ctx.w * ctx.h
    isz = 4 if 14 <= v < 24 else 2
    p = start + 4
    out = {"version": v}
    out["tileCrc"] = zlib.crc32(r.b[p:p + 2 * n]); p += 2 * n
    out["blendCrc"] = zlib.crc32(r.b[p:p + isz * n]); p += isz * n
    out["extraCrc"] = zlib.crc32(r.b[p:p + isz * n]); p += isz * n
    out["cliffCrc"] = zlib.crc32(r.b[p:p + isz * n]); p += isz * n
    stride = (ctx.w + 7) // 8
    if v >= 7:
        ln = ((ctx.w + 1) // 8 if v == 7 else stride) * ctx.h
        out["cliffStateCrc"] = zlib.crc32(r.b[p:p + ln]); p += ln
    cap.btd = out


orig_obj = fullparse.LEAF["Object"]
orig_wp = fullparse.LEAF["WaypointsList"]


def wrap_obj(r, v, ctx):
    # re-read the object header and dict to capture name, flags and waypointID (fullparse discards them)
    r2 = fullparse.R(r.b, r.p, r.end)
    r2.f32(); r2.f32(); r2.f32(); r2.f32()
    flags = r2.i32()
    name = r2.astr()
    d = fullparse.read_dict(r2, ctx, "survey.object") if v >= 2 else {}
    cap.objects.append((name, flags, d.get("waypointID")))
    orig_obj(r, v, ctx)


def wrap_wp(r, v, ctx):
    n = struct.unpack_from("<i", r.b, r.p)[0]
    cap.links = [struct.unpack_from("<ii", r.b, r.p + 4 + 8 * i) for i in range(n)]
    orig_wp(r, v, ctx)


cap.objects = []
cap.links = []
fullparse.LEAF["HeightMapData"] = wrap_hm
fullparse.LEAF["BlendTileData"] = wrap_btd
fullparse.LEAF["Object"] = wrap_obj
fullparse.LEAF["WaypointsList"] = wrap_wp


def road_stats(objects):
    """ZH W3DRoadBuffer::addMapObjects pairing (flag 0x2 immediately followed by flag 0x4, names ignored)
    and the spec author's roadchk.py pairing (same name required)."""
    pairs_flag = pairs_same = orphans = 0
    i = 0
    while i < len(objects):
        name, fl, _ = objects[i]
        if fl & 2:
            if i + 1 < len(objects) and objects[i + 1][1] & 4:
                pairs_flag += 1
                pairs_same += objects[i + 1][0] == name
                i += 2
                continue
            orphans += 1
        elif fl & 4:
            orphans += 1
        i += 1
    return pairs_flag, pairs_same, orphans


def counter_total(c):
    return sum(c.values())


def survey_one(key, entry):
    blob = fullparse.load(entry)
    body, env = census.decode(blob)
    names, p = census.parse_toc(body)
    ctx = fullparse.Ctx(names)
    order = []
    mv = collections.defaultdict(set)
    cap.hm = cap.btd = None
    cap.objects = []
    cap.links = []
    fullparse.parse_chunks(body, p, len(body), ctx, "", mv, order)
    st = ctx.stats
    sm = ctx.samples
    rec = {
        "src": label(entry[0]),
        "stored": len(blob),
        "decoded": len(body),
        "toc": len(names),
        "order": order,
        "versions": {(a + "/" + b): sorted(vs) for (a, b), vs in sorted(mv.items())},
    }
    if sm.get("hm"):
        w, h, border, bounds, mn, mx = sm["hm"][0]
        rec["heightMap"] = {"w": w, "h": h, "border": border, "boundaries": [list(b) for b in bounds],
                            "min": mn, "max": mx, "crc": cap.hm["crc"], "sum": cap.hm["sum"]}
    if sm.get("btd"):
        nb, nbl, ncl, classes = sm["btd"][0]
        rec["blend"] = dict(cap.btd)
        rec["blend"].update({"numBitmapTiles": nb, "numBlendedTiles": nbl, "numCliffInfo": ncl,
                             "classes": [[c[0], c[1], c[2], c[3]] for c in classes]})
        rec["blend"]["blendDirCounts"] = {str(k): v for k, v in sorted(st["blend.dir"].items())}
        rec["blend"]["invertedCounts"] = {str(k): v for k, v in sorted(st["blend.inv"].items())}
        rec["blend"]["longCounts"] = {str(k): v for k, v in sorted(st["blend.long"].items())}
    if sm.get("worldinfo"):
        rec["worldInfo"] = {k: (v if not isinstance(v, float) else float(v)) for k, v in sm["worldinfo"][0].items()}
    rec["objects"] = {
        "count": counter_total(st["object.name"]),
        "flags": {str(k): v for k, v in sorted(st["object.flags"].items())},
        "emptyName": st["object.emptyname"]["count"],
    }
    # "<name>TAB<count>" lines joined by LF, sorted by name: the C++ test rebuilds the same string
    lines = chr(10).join("%s%s%d" % (k, chr(9), v) for k, v in sorted(st["object.name"].items()))
    rec["objects"]["distinctNames"] = len(st["object.name"])
    # the world builder's "*" pseudo-templates this map places (the engine's evidenced special-name list)
    rec["objects"]["starNames"] = sorted(k for k in st["object.name"] if k.startswith("*"))
    rec["objects"]["nameCountCrc"] = zlib.crc32(lines.encode("latin-1"))
    rec["waypointLinks"] = st["waypointlinks"]["total"]
    ids = set(o[2] for o in cap.objects if o[2] is not None)
    pf, ps, orph = road_stats(cap.objects)
    rec["waypoints"] = {"objects": len(ids), "links": len(cap.links),
                        "badLinks": sum(1 for a, b in cap.links if a not in ids or b not in ids)}
    rec["roads"] = {"pairsFlag": pf, "pairsSameName": ps, "orphans": orph,
                    "names": sorted(set(o[0] for o in cap.objects if o[1] & 6))}
    rec["mpPositions"] = counter_total(st["mpinfo"])
    rec["buildListEntries"] = counter_total(st["buildlist.tmpl"])
    rec["factionBuildLists"] = counter_total(st["buildlists.faction"])
    rec["libraryMapRefs"] = counter_total(st["libmaps"])
    rec["triggerAreas"] = counter_total(st["trigger.tail"])
    rec["standingWater"] = [list(x) for x in sm.get("water", [])]
    rec["rivers"] = [list(x) for x in sm.get("river", [])]
    rec["waves"] = [[x[0], x[1], x[2], x[3], x[4], x[5]] for x in sm.get("wave", [])]
    rec["scripts"] = {
        "conditions": counter_total(st["cond.key"]),
        "actions": counter_total(st["action.key"]),
        "parameters": counter_total(st["param.type"]),
        "condTail": {str(k): v for k, v in sorted(st["cond.tail"].items())},
        "actionTail": {str(k): v for k, v in sorted(st["action.tail"].items())},
    }
    rec["postEffects"] = counter_total(st["post"])
    rec["cameraAnimations"] = counter_total(st["camanim.type"])
    if st["light.taillen"]:
        rec["lightingTail"] = {str(k): v for k, v in st["light.taillen"].items()}
    rec["envMacro"] = {str(k): v for k, v in st["env.macro"].items()}
    rec["envCloud"] = {str(k): v for k, v in st["env.cloud"].items()}
    rec["texClassNames"] = sorted(str(k) for k in st["texclass.name"])
    return rec


def btd_rows(maps):
    """Independent per-map blend/plane statistics (tools/maps/oracle/btdstats.py), computed from the map bytes."""
    return {r["map"]: r for r in btdstats.compute(maps)}


def scb_survey():
    """Top-level chunk lists of the .scb script libraries (spec scbchk.py), first archive wins."""
    seen = {}
    for b in fullparse.PURE_BIGS:
        for name, off, size in census.big_entries(b):
            if name.lower().endswith(".scb"):
                seen.setdefault(name.lower(), (b, name, off, size))
    out = {}
    for key, (b, name, off, size) in sorted(seen.items()):
        blob = census.read_entry(b, off, size)
        body, env = census.decode(blob)
        names, p = census.parse_toc(body)
        top = []
        while p < len(body):
            cid, ver, sz = struct.unpack_from("<IHi", body, p)
            top.append([names[cid], ver, sz])
            p += 10 + sz
        out[key] = {"src": label(b), "stored": len(blob), "decoded": len(body), "envelope": env.split("(")[0], "top": top}
    return out


def mapcache():
    big = census.resolve_archive(rotwk, "_patch201maps.big")
    text = None
    for n, o, s in census.big_entries(big):
        if n.lower().endswith("mapcache.ini"):
            text = census.read_entry(big, o, s).decode("latin-1")
    ents = re.findall(r"MapCache\s+(\S+)(.*?)\nEND", text, re.S)
    out = {}
    for raw, body in ents:
        name = re.sub(r"_([0-9A-Fa-f]{2})", lambda m: chr(int(m.group(1), 16)), raw).replace("\\", "/").lower()
        f = {}
        for k, v in re.findall(r"^\s*(\w+)\s*=\s*(.*?)\s*$", body, re.M):
            f[k] = v
        out[name] = f
    return out


def main():
    maps = fullparse.pure_maps()
    result = {"maps": {}}
    for key, entry in sorted(maps.items()):
        result["maps"][key] = survey_one(key, entry)
    btd = btd_rows(maps)
    for key, rec in result["maps"].items():
        row = btd[key]
        rec["btdstats"] = {k: v for k, v in row.items() if k != "map"}
    result["scb"] = scb_survey()
    result["mapcache"] = mapcache()
    result["note"] = ("Generated by tools/maps/map_survey.py from the reference scripts in tools/maps/oracle/ "
                      "(independent of the C++ reader). Names and numbers only.")
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        json.dump(result, f, indent=1, sort_keys=True, ensure_ascii=True)
        f.write("\n")
    print("maps", len(result["maps"]), "mapcache", len(result["mapcache"]), "->", OUT)


if __name__ == "__main__":
    main()
