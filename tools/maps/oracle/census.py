"""Independent reference reader of the retail map files, kept in the repo so engine/tests/data/map-survey.json
regenerates from a configured install (tools/maps/map_survey.py). The spec author's census script: a BIG reader,
a RefPack decoder written from the format alone, and a chunk-tree walker. It shares no code with the C++ engine.

Needs ROTWK_INSTALL and BFME2_INSTALL (the install folders). Read-only: nothing is written to the installs.
"""
import struct, sys, json, collections, os

_RW = os.environ.get("ROTWK_INSTALL")
_B2 = os.environ.get("BFME2_INSTALL")
if not _RW or not _B2:
    sys.exit("set ROTWK_INSTALL and BFME2_INSTALL (the install folders)")


def resolve_archive(directory, name):
    """Path of the archive `name` inside `directory`, matched case-insensitively (Windows file names are; the
    Linux install has `Maps.big` where the lists below say `maps.big`). A missing directory or archive is an
    error, never a skip; two names differing only in case are ambiguous and also an error."""
    try:
        entries = os.listdir(directory)
    except OSError as e:
        raise FileNotFoundError("required archive %s: cannot list %s (%s)" % (name, directory, e))
    hits = [e for e in entries if e.lower() == name.lower()]
    if not hits:
        raise FileNotFoundError("required archive %s not found in %s (matched case-insensitively)" % (name, directory))
    if len(hits) > 1:
        raise ValueError("required archive %s is ambiguous in %s: %s" % (name, directory, sorted(hits)))
    return os.path.join(directory, hits[0])


def resolve_archives(directory, names):
    """resolve_archive for each name, keeping the order asked for (it is the precedence order)."""
    return [resolve_archive(directory, n) for n in names]


def bigs():
    """The archives this script reads, in its order; every one is required."""
    return resolve_archives(_B2, ["maps.big", "_patch101.big", "_patch103.big"]) + \
        resolve_archives(_RW, ["maps.big", "_patch201.big", "_patch201maps.big"])


def big_entries(path):
    with open(path, "rb") as f:
        h = f.read(16)
        assert h[:4] in (b"BIGF", b"BIG4"), path
        n = struct.unpack(">I", h[8:12])[0]
        hs = struct.unpack(">I", h[12:16])[0]
        f.seek(16)
        hdr = f.read(hs - 16)
    out = []
    p = 0
    for _ in range(n):
        off, size = struct.unpack(">II", hdr[p:p+8]); p += 8
        e = hdr.index(b"\0", p)
        name = hdr[p:e].decode("latin-1").replace("\\", "/"); p = e + 1
        out.append((name, off, size))
    return out

def read_entry(path, off, size):
    with open(path, "rb") as f:
        f.seek(off)
        return f.read(size)

def refpack(src):
    p = 0
    flags = src[p]; marker = src[p+1]; p += 2
    assert marker == 0xFB and (flags & 0x3E) == 0x10, (hex(flags), hex(marker))
    w = 4 if flags & 0x80 else 3
    if flags & 0x01: p += w
    outlen = int.from_bytes(src[p:p+w], "big"); p += w
    out = bytearray()
    while True:
        c = src[p]; p += 1
        if c < 0x80:
            c2 = src[p]; p += 1
            lit = c & 3; ml = ((c & 0x1C) >> 2) + 3; d = ((c & 0x60) << 3) + c2 + 1
        elif c < 0xC0:
            c2, c3 = src[p], src[p+1]; p += 2
            lit = (c2 & 0xC0) >> 6; ml = (c & 0x3F) + 4; d = ((c2 & 0x3F) << 8) + c3 + 1
        elif c < 0xE0:
            c2, c3, c4 = src[p], src[p+1], src[p+2]; p += 3
            lit = c & 3; ml = ((c & 0x0C) << 6) + c4 + 5; d = ((c & 0x10) << 12) + (c2 << 8) + c3 + 1
        elif c < 0xFC:
            lit = ((c & 0x1F) + 1) * 4; ml = 0; d = 0
        else:
            lit = c & 3; out += src[p:p+lit]; p += lit; break
        out += src[p:p+lit]; p += lit
        for _ in range(ml):
            out.append(out[-d])
    assert len(out) == outlen, (len(out), outlen)
    return bytes(out), p, len(src)

def decode(blob):
    if blob[:4] == b"CkMp":
        return blob, "raw"
    if blob[:4] == b"EAR\0":
        n = struct.unpack("<I", blob[4:8])[0]
        body, used, total = refpack(blob[8:])
        assert len(body) == n
        # strict, like the C++ loader: a RefPack stream ends at its terminator command; bytes after it are
        # not a stream any retail tool wrote (all 181 retail maps end exactly there)
        if used != total:
            raise ValueError("RefPack stream ends at byte %d of %d: %d trailing byte(s) after the terminator" % (used, total, total - used))
        return body, "EAR+refpack"
    raise ValueError("unknown envelope %r" % blob[:8])

# Chunks known (from source) to hold child chunks.
CONTAINERS = {"ObjectsList", "SidesList", "PlayerScriptsList", "ScriptList", "ScriptGroup",
              "Script", "OrCondition", "BuildLists", "LibraryMapLists", "TeamsList",
              "CameraAnimationList", "ScriptTeams", "PostEffectsChunk", "Teams"}

def parse_toc(body):
    assert body[:4] == b"CkMp"
    n = struct.unpack("<i", body[4:8])[0]
    p = 8; names = {}
    for _ in range(n):
        l = body[p]; p += 1
        nm = body[p:p+l].decode("latin-1"); p += l
        i = struct.unpack("<I", body[p:p+4])[0]; p += 4
        names[i] = nm
    return names, p

def looks_like_children(body, start, end, names):
    p = start
    if end - start < 10: return False
    while p < end:
        if p + 10 > end: return False
        cid, ver, size = struct.unpack("<IHi", body[p:p+10])
        if cid not in names or size < 0 or p + 10 + size > end: return False
        p += 10 + size
    return p == end

def walk(body, start, end, names, path, rec, depth=0):
    p = start
    while p < end:
        cid, ver, size = struct.unpack("<IHi", body[p:p+10])
        nm = names.get(cid, "?%d" % cid)
        full = path + "/" + nm
        rec.append((full, ver, size, p + 10))
        if nm in CONTAINERS or (depth < 6 and looks_like_children(body, p + 10, p + 10 + size, names) and nm not in ("HeightMapData","BlendTileData")):
            # Containers may have a header before children: scan for the child sequence start.
            cs = p + 10
            ce = cs + size
            k = cs
            found = False
            while k <= min(ce, cs + 4096):
                if looks_like_children(body, k, ce, names):
                    rec.append((full + "#prefix", k - cs, 0, cs))
                    walk(body, k, ce, names, full, rec, depth + 1)
                    found = True
                    break
                k += 1
            if not found:
                rec.append((full + "#nochildren", ver, size, cs))
        p += 10 + size
    assert p == end, (p, end, path)

def main():
    seen = {}
    order = []
    for b in bigs():
        for name, off, size in big_entries(b):
            if name.lower().endswith(".map"):
                key = name.lower()
                seen[key] = (b, name, off, size)  # later archives override
                order.append((b, name))
    print("archives->map entries:", collections.Counter(b for b, _ in order))
    census = collections.defaultdict(lambda: collections.defaultdict(list))
    maps = {}
    for key, (b, name, off, size) in sorted(seen.items()):
        blob = read_entry(b, off, size)
        try:
            body, env = decode(blob)
            names, p = parse_toc(body)
            rec = []
            walk(body, p, len(body), names, "", rec)
        except Exception as ex:
            maps[key] = {"src": b, "error": repr(ex)}
            continue
        maps[key] = {"src": b, "env": env, "size": len(body), "toc": len(names)}
        for full, ver, sz, at in rec:
            census[full][ver].append(key)
    out = {"maps": maps,
           "census": {k: {str(v): {"count": len(ms), "examples": ms[:3]} for v, ms in vv.items()}
                      for k, vv in sorted(census.items())}}
    json.dump(out, open(os.path.join(os.path.dirname(__file__), "census.json"), "w"), indent=1)
    print("maps", len(maps), "errors", sum(1 for m in maps.values() if "error" in m))
    for k, vv in sorted(census.items()):
        print(k, {v: len(ms) for v, ms in sorted(vv.items())})

if __name__ == "__main__":
    main()
