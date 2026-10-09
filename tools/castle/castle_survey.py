#!/usr/bin/env python3
"""Independent oracle for the C++ CastleTemplates reader (lane BUILD-1): surveys every `.bse` base layout of the pure RotWK 2.01 + BFME2 1.06 archives and writes
engine/tests/data/castle-survey.json (names and numbers only, no retail bytes).

The grammar is the one read from the RotWK binary at RW 0x731010 (see engine/src/GameClient/MapChunks.h, CastleTemplateEntry) and is parsed here with its own code:
the RefPack + CkMp envelope and the chunk walk come from tools/maps/oracle/census.py (the spec author's reference scripts), the CastleTemplates body is decoded below.

  set ROTWK_INSTALL=<RotWK dir>  &  set BFME2_INSTALL=<BFME2 dir>
  python tools/castle/castle_survey.py            (rewrites engine/tests/data/castle-survey.json)
"""
import collections
import json
import os
import struct
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(REPO, "engine", "tests", "data", "castle-survey.json")
sys.path.insert(0, os.path.join(REPO, "tools", "maps", "oracle"))
import census  # noqa: E402


def big_entries(path):
    with open(path, "rb") as f:
        data = f.read()
    assert data[:4] in (b"BIG4", b"BIGF")
    n = struct.unpack(">I", data[8:12])[0]
    p = 16
    out = {}
    for _ in range(n):
        off, size = struct.unpack(">II", data[p:p + 8])
        p += 8
        e = data.index(b"\0", p)
        out[data[p:e].decode("latin-1").lower()] = (off, size)
        p = e + 1
    return data, out


def parse_castle_templates(body, p, ver, size, names):
    end = p + size
    q = p

    def u32():
        nonlocal q
        v = struct.unpack("<I", body[q:q + 4])[0]
        q += 4
        return v

    def i32():
        nonlocal q
        v = struct.unpack("<i", body[q:q + 4])[0]
        q += 4
        return v

    def f32():
        nonlocal q
        v = struct.unpack("<f", body[q:q + 4])[0]
        q += 4
        return v

    def s():
        nonlocal q
        n = struct.unpack("<H", body[q:q + 2])[0]
        q += 2
        r = body[q:q + n].decode("latin-1")
        q += n
        return r

    key = names[u32() >> 8]
    entries = []
    for _ in range(i32()):
        first = s()
        tmpl = s()
        x, y, z, ang = f32(), f32(), f32(), f32()
        if ver >= 4:
            i32()
            i32()
        entries.append((first, tmpl, x, y, z, ang))
    lines = 0
    points = 0
    if ver >= 2:
        for _ in range(i32()):
            if ver >= 5:
                s()
            k = i32()
            lines += 1
            for _ in range(k):
                if ver >= 3:
                    f32()
                    f32()
                else:
                    i32()
                    i32()
                    i32()
                points += 1
    assert q == end, (key, q, end)
    return key, entries, lines, points


def main():
    rotwk = os.environ.get("ROTWK_INSTALL")
    bfme2 = os.environ.get("BFME2_INSTALL")
    if not rotwk or not bfme2:
        sys.exit("set ROTWK_INSTALL and BFME2_INSTALL")
    res = {}
    for label, root in (("rotwk", rotwk), ("bfme2", bfme2)):
        for fn in sorted(os.listdir(root)):
            if fn.lower() not in ("bases.big",):
                continue
            data, ents = big_entries(os.path.join(root, fn))
            for name, (off, size) in sorted(ents.items()):
                if not name.endswith(".bse"):
                    continue
                body, _ = census.decode(data[off:off + size]), None
                body = body[0]
                tnames, p = census.parse_toc(body)
                rec = []
                census.walk(body, p, len(body), tnames, "", rec)
                found = [r for r in rec if r[0] == "/CastleTemplates"]
                assert len(found) == 1, name
                _, ver, sz, pos = found[0]
                key, entries, lines, points = parse_castle_templates(body, pos, ver, sz, tnames)
                counts = collections.Counter(e[1] for e in entries)
                res["%s:%s" % (label, name)] = {
                    "key": key, "version": ver, "entries": len(entries), "lines": lines, "points": points,
                    "firstNames": sum(1 for e in entries if e[0]),
                    "templates": dict(sorted(counts.items())),
                }
    with open(OUT, "w") as f:
        json.dump({"bases": res}, f, indent=1, sort_keys=True)
        f.write("\n")
    print("wrote", OUT, len(res), "base files")


if __name__ == "__main__":
    main()
