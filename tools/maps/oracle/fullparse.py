"""Reference parser: parse every chunk of every pure-retail RotWK map with the layouts in
specs/maps-and-terrain.md and require 0 leftover bytes per chunk. Read-only."""
import struct, sys, json, collections, os, glob, math
import census

HERE = os.path.dirname(os.path.abspath(__file__))

# Precedence: loose files first, then RotWK BIGs, then BFME2 BIGs (see spec section 1.1). Loose files are
# contamination (plan rule 7) and never read here (map_survey.py empties LOOSE_ROOTS).
PURE_RW = ("_patch201maps.big", "_patch201.big", "data2.big", "maps.big", "libraries.big")
PURE_B2 = ("_patch103.big", "_patch101.big", "maps.big", "libraries.big", "bases.big")


def resolve_pure_bigs(rotwk, bfme2):
    """The pure archives in precedence order (first wins), names matched case-insensitively; a missing one raises."""
    return census.resolve_archives(rotwk, PURE_RW) + census.resolve_archives(bfme2, PURE_B2)


PURE_BIGS = None  # set by the caller (map_survey.py), or resolved from the install variables on first use


def pure_bigs():
    global PURE_BIGS
    if PURE_BIGS is None:
        PURE_BIGS = resolve_pure_bigs(census._RW, census._B2)
    return PURE_BIGS


LOOSE_ROOTS = []


class R:
    def __init__(self, b, p, end):
        self.b = b; self.p = p; self.end = end
    def need(self, n):
        if self.p + n > self.end:
            raise ValueError("read past end of chunk (%d > %d)" % (self.p + n, self.end))
    def u8(self):
        self.need(1); v = self.b[self.p]; self.p += 1; return v
    def u16(self):
        self.need(2); v = struct.unpack_from("<H", self.b, self.p)[0]; self.p += 2; return v
    def i32(self):
        self.need(4); v = struct.unpack_from("<i", self.b, self.p)[0]; self.p += 4; return v
    def u32(self):
        self.need(4); v = struct.unpack_from("<I", self.b, self.p)[0]; self.p += 4; return v
    def f32(self):
        self.need(4); v = struct.unpack_from("<f", self.b, self.p)[0]; self.p += 4; return v
    def raw(self, n):
        self.need(n); v = self.b[self.p:self.p+n]; self.p += n; return v
    def astr(self):
        n = self.u16(); return self.raw(n).decode("latin-1")
    def ustr(self):
        n = self.u16(); return self.raw(2*n).decode("utf-16-le", "replace")
    def left(self):
        return self.end - self.p


class Ctx:
    def __init__(self, names):
        self.names = names
        self.stats = collections.defaultdict(collections.Counter)
        self.samples = collections.defaultdict(list)
        self.w = self.h = None
    def key(self, kt):
        return self.names.get(kt >> 8, "?%d" % (kt >> 8)), kt & 0xFF


def read_dict(r, ctx, tag):
    n = r.u16()
    d = {}
    for _ in range(n):
        name, t = ctx.key(r.i32())
        if t == 0: v = r.u8()
        elif t == 1: v = r.i32()
        elif t == 2: v = r.f32()
        elif t == 3: v = r.astr()
        elif t == 4: v = r.ustr()
        else: raise ValueError("bad dict type %d" % t)
        d[name] = v
        ctx.stats[tag + ".keys"][(name, t)] += 1
    return d


def bits(r, w, h):
    return r.raw(((w + 7) // 8) * h)


def p_HeightMapData(r, v, ctx):
    w, h, border = r.i32(), r.i32(), r.i32()
    nb = r.i32()
    bounds = [(r.i32(), r.i32()) for _ in range(nb)]
    n = r.i32()
    assert n == w * h
    data = r.raw(2 * n) if v >= 5 else r.raw(n)
    ctx.w, ctx.h = w, h
    hs = struct.unpack("<%dH" % n, data) if v >= 5 else data
    ctx.stats["hm"]["v%d" % v] += 1
    ctx.samples["hm"].append((w, h, border, bounds, min(hs), max(hs)))


def p_BlendTileData(r, v, ctx):
    w, h = ctx.w, ctx.h
    n = r.i32(); assert n == w * h
    r.raw(2 * n)                               # tileNdxes (Short)
    isz = 4 if 14 <= v < 24 else 2
    r.raw(isz * n); r.raw(isz * n); r.raw(isz * n)   # blend, extraBlend(3-way), cliffInfo
    if v >= 7:
        r.raw(((w + 1) // 8) * h if v == 7 else ((w + 7) // 8) * h)  # cellCliffState / impassable
    if v >= 10: bits(r, w, h)   # impassable-to-players
    if v >= 11: bits(r, w, h)   # passage widths
    if 14 <= v < 25: bits(r, w, h)   # taintability
    if v >= 15: bits(r, w, h)   # extra passability
    if 16 <= v < 25: r.raw(w * h)    # flammability (byte per cell)
    if v >= 17: bits(r, w, h)   # visibility
    numBitmapTiles = r.i32(); numBlended = r.i32(); numCliff = r.i32() if v >= 5 else 1
    ntc = r.i32()
    classes = []
    for _ in range(ntc):
        first, num, width, legacy = r.i32(), r.i32(), r.i32(), r.i32()
        name = r.astr()
        classes.append((first, num, width, legacy, name))
        ctx.stats["texclass.name"][name] += 1
        ctx.stats["texclass.legacy"][legacy] += 1
        ctx.stats["texclass.width"][width] += 1
    if v >= 4:
        numEdgeTiles = r.i32(); nec = r.i32()
        ctx.stats["edge"][(numEdgeTiles, nec)] += 1
        for _ in range(nec):
            r.i32(); r.i32(); r.i32(); r.astr()
    for i in range(1, numBlended):
        blendNdx = r.i32()
        horiz, vert, rd, ld, inv = r.u8(), r.u8(), r.u8(), r.u8(), r.u8()
        longd = r.u8() if v >= 3 else 0
        custom = r.i32() if v >= 4 else -1
        flag = r.u32()
        assert flag == 0x7ADA0000, hex(flag)
        ctx.stats["blend.dir"][(horiz, vert, rd, ld)] += 1
        ctx.stats["blend.inv"][inv] += 1
        ctx.stats["blend.long"][longd] += 1
        ctx.stats["blend.custom"][custom] += 1
    for i in range(1, numCliff):
        r.i32(); [r.f32() for _ in range(8)]; flip = r.u8(); mutant = r.u8()
        ctx.stats["cliff.flipmutant"][(flip, mutant)] += 1
    ctx.stats["btd"]["numBitmapTiles-sum-check"] += int(numBitmapTiles == sum(c[1] for c in classes))
    ctx.stats["btd"]["maps"] += 1
    ctx.samples["btd"].append((numBitmapTiles, numBlended, numCliff, [(c[0], c[1], c[2], c[4]) for c in classes]))


def p_WorldInfo(r, v, ctx):
    d = read_dict(r, ctx, "worldinfo")
    ctx.samples["worldinfo"].append(d)


def p_MPPositionInfo(r, v, ctx):
    a, b, c = r.u8(), r.u8(), r.u8()
    team = r.i32(); n = r.i32(); names = [r.astr() for _ in range(n)]
    ctx.stats["mpinfo"][(a, b, c, team)] += 1
    for x in names: ctx.stats["mpinfo.restrict"][x] += 1


def p_SidesList(r, v, ctx):
    lead = r.u8() if v >= 6 else None
    ctx.stats["sides.lead"][(v, lead)] += 1
    n = r.i32()
    for _ in range(n):
        read_dict(r, ctx, "side")
        bl = r.i32()
        for _ in range(bl):
            read_buildlist_entry(r, ctx)


def read_buildlist_entry(r, ctx):
    bname = r.astr(); tmpl = r.astr(); x, y, z = r.f32(), r.f32(), r.f32(); ang = r.f32()
    built = r.u8(); rebuilds = r.i32(); script = r.astr(); health = r.i32()
    whiner, unsell, repair = r.u8(), r.u8(), r.u8()
    ctx.stats["buildlist.tmpl"][tmpl] += 1


def p_LibraryMaps(r, v, ctx):
    n = r.i32()
    for _ in range(n): ctx.stats["libmaps"][r.astr()] += 1


def p_Teams(r, v, ctx):
    n = r.i32()
    for _ in range(n): read_dict(r, ctx, "team")


def p_BuildLists(r, v, ctx):
    n = r.i32()
    for _ in range(n):
        name, t = ctx.key(r.i32())
        ctx.stats["buildlists.faction"][name] += 1
        c = r.i32()
        for _ in range(c): read_buildlist_entry(r, ctx)


def p_Object(r, v, ctx):
    x, y, z, ang = r.f32(), r.f32(), r.f32(), r.f32()
    flags = r.i32(); name = r.astr()
    d = read_dict(r, ctx, "object") if v >= 2 else {}
    ctx.stats["object.flags"][flags] += 1
    ctx.stats["object.name"][name] += 1
    if name == "":
        ctx.stats["object.emptyname"]["count"] += 1
    if flags != 0 and len(ctx.samples["roadobj"]) < 40:
        ctx.samples["roadobj"].append((name, hex(flags), round(x, 1), round(y, 1), round(z, 2)))


def p_TriggerAreas(r, v, ctx):
    n = r.i32()
    for _ in range(n):
        r.astr(); r.astr(); r.i32(); np_ = r.i32(); r.raw(8 * np_); tail = r.i32()
        ctx.stats["trigger.tail"][tail] += 1


def p_StandingWaterAreas(r, v, ctx):
    n = r.i32()
    for _ in range(n):
        uid = r.u32(); name = r.astr(); layer = r.astr(); uv = r.f32(); add = r.u8()
        bump = r.astr(); sky = r.astr(); np_ = r.i32(); r.raw(8 * np_); wh = r.i32()
        fx = r.astr(); depth = r.astr()
        ctx.stats["water.fx"][fx] += 1
        ctx.stats["water.depth"][depth] += 1
        ctx.stats["water.bump"][bump] += 1
        ctx.stats["water.sky"][sky] += 1
        ctx.samples["water"].append((name, uv, add, np_, wh))


def p_RiverAreas(r, v, ctx):
    n = r.i32()
    for _ in range(n):
        uid = r.u32(); name = r.astr(); layer = r.astr(); uv = r.f32(); add = r.u8()
        tex = [r.astr() for _ in range(4)]
        rgba = r.raw(4); alpha = r.f32(); wh = r.i32()
        if v >= 3: r.astr()
        lod = r.astr(); nl = r.i32(); r.raw(16 * nl)
        ctx.stats["river.lod"][lod] += 1
        ctx.stats["river.tex"][tuple(tex)] += 1
        ctx.samples["river"].append((name, uv, add, rgba.hex(), alpha, wh, nl))


def p_StandingWaveAreas(r, v, ctx):
    n = r.i32()
    for _ in range(n):
        uid = r.u32(); name = r.astr(); layer = r.astr(); uv = r.f32(); add = r.u8()
        np_ = r.i32(); r.raw(8 * np_); unk = r.i32()
        vals = [r.i32() for _ in range(9)]; tex = r.astr()
        pca = r.i32() if v == 2 else None
        ctx.stats["wave.unk"][unk] += 1
        ctx.stats["wave.tex"][tex] += 1
        ctx.samples["wave"].append((name, uv, add, np_, vals, pca))


def p_GlobalLighting(r, v, ctx):
    tod = r.i32()
    ctx.stats["light.tod"][tod] += 1
    lights = [[r.f32() for _ in range(81)] for _ in range(4)]
    rest = []
    while r.left() >= 4:
        rest.append(r.raw(4))
    ctx.stats["light.taillen"][(v, len(rest))] += 1
    if len(ctx.samples["light"]) < 6:
        ctx.samples["light"].append((v, tod, [round(x, 3) for x in lights[1][:27]],
                                     [struct.unpack("<f", x)[0] for x in rest], [x.hex() for x in rest]))


def p_PostEffectsChunk(r, v, ctx):
    n = r.u8() if v < 2 else r.i32()
    for _ in range(n):
        name = r.astr(); bf = r.f32(); img = r.astr()
        ctx.stats["post"][(name, img)] += 1


def p_EnvironmentData(r, v, ctx):
    a, b = (r.f32(), r.f32()) if v >= 3 else (0.0, 0.0); s = r.u8(); m = r.astr(); c = r.astr()
    ctx.stats["env.macro"][(m, s)] += 1
    ctx.stats["env.cloud"][c] += 1
    ctx.stats["env.ab"][(round(a, 2), round(b, 2))] += 1


def p_NamedCameras(r, v, ctx):
    n = r.i32()
    for _ in range(n):
        r.f32(); r.f32(); r.f32(); r.astr(); [r.f32() for _ in range(6)]


def p_CameraAnimationList(r, v, ctx):
    n = r.i32()
    for _ in range(n):
        typ = r.raw(4)[::-1]
        r.astr(); r.u32(); r.u32()
        if typ == b"free":
            for _ in range(r.u32()):
                r.u32(); r.raw(4); r.raw(12); r.raw(16); r.f32()
        elif typ == b"look":
            for _ in range(r.u32()):
                r.u32(); r.raw(4); r.raw(12); r.f32(); r.f32()
            for _ in range(r.u32()):
                r.u32(); r.raw(4); r.raw(12)
        else:
            raise ValueError("cam type %r" % typ)
        ctx.stats["camanim.type"][typ] += 1


def p_WaypointsList(r, v, ctx):
    n = r.i32(); r.raw(8 * n)
    ctx.stats["waypointlinks"]["total"] += n


def p_SkyboxSettings(r, v, ctx):
    r.raw(12); r.f32(); r.f32(); ctx.stats["skybox"][r.astr()] += 1


def p_PolygonTriggers(r, v, ctx):
    n = r.i32()
    for _ in range(n):
        r.astr()
        if v >= 4: r.astr()
        r.i32()
        if v >= 2: r.u8()
        if v >= 3: r.u8(); r.i32()
        if v >= 5:
            for _ in range(6): r.astr()
            r.u8(); r.raw(4); r.raw(8); r.f32()
        np_ = r.i32(); r.raw(12 * np_)


def p_Param(r, ctx):
    t = r.i32()
    if t == 16:
        r.raw(12)
    else:
        r.i32(); r.f32(); r.astr()
    ctx.stats["param.type"][t] += 1


def p_Condition(r, v, ctx):
    r.i32()
    if v >= 4: ctx.stats["cond.key"][ctx.key(r.i32())[0]] += 1
    n = r.i32()
    for _ in range(n): p_Param(r, ctx)
    if v >= 5:
        a = r.i32(); b = r.i32(); ctx.stats["cond.tail"][(a, b)] += 1


def p_Action(r, v, ctx):
    r.i32()
    if v >= 2: ctx.stats["action.key"][ctx.key(r.i32())[0]] += 1
    n = r.i32()
    for _ in range(n): p_Param(r, ctx)
    if v >= 3: ctx.stats["action.tail"][r.i32()] += 1


def p_ScriptHeader(r, v, ctx):
    r.astr(); r.astr(); r.astr(); r.astr()
    [r.u8() for _ in range(6)]
    if v >= 2: r.i32()
    if v >= 3:
        r.u8(); r.u8(); r.i32(); r.u8(); r.astr()
    if v >= 4:
        ctx.stats["script.v4str"][r.astr()] += 1


def p_ScriptGroupHeader(r, v, ctx):
    r.astr(); r.u8()
    if v >= 2: r.u8()


LEAF = {
    "HeightMapData": p_HeightMapData, "BlendTileData": p_BlendTileData, "WorldInfo": p_WorldInfo,
    "MPPositionInfo": p_MPPositionInfo, "SidesList": p_SidesList, "LibraryMaps": p_LibraryMaps,
    "Teams": p_Teams, "BuildLists": p_BuildLists, "Object": p_Object, "TriggerAreas": p_TriggerAreas,
    "StandingWaterAreas": p_StandingWaterAreas, "RiverAreas": p_RiverAreas,
    "StandingWaveAreas": p_StandingWaveAreas, "GlobalLighting": p_GlobalLighting,
    "PostEffectsChunk": p_PostEffectsChunk, "EnvironmentData": p_EnvironmentData,
    "NamedCameras": p_NamedCameras, "CameraAnimationList": p_CameraAnimationList,
    "WaypointsList": p_WaypointsList, "SkyboxSettings": p_SkyboxSettings,
    "PolygonTriggers": p_PolygonTriggers, "Condition": p_Condition,
    "ScriptAction": p_Action, "ScriptActionFalse": p_Action,
}
# Containers: optional header parser, then child chunks to end of chunk.
CONTAINER = {"ObjectsList": None, "MPPositionList": None, "LibraryMapLists": None,
             "PlayerScriptsList": None, "ScriptList": None, "OrCondition": None,
             "Script": p_ScriptHeader, "ScriptGroup": p_ScriptGroupHeader}
ALLOWED_CHILD = {
    "": {"HeightMapData", "BlendTileData", "WorldInfo", "MPPositionList", "SidesList", "LibraryMapLists",
         "Teams", "PlayerScriptsList", "BuildLists", "ObjectsList", "PolygonTriggers", "TriggerAreas",
         "StandingWaterAreas", "RiverAreas", "StandingWaveAreas", "GlobalLighting", "PostEffectsChunk",
         "EnvironmentData", "NamedCameras", "CameraAnimationList", "WaypointsList", "SkyboxSettings"},
    "ObjectsList": {"Object"}, "MPPositionList": {"MPPositionInfo"}, "LibraryMapLists": {"LibraryMaps"},
    "PlayerScriptsList": {"ScriptList"}, "ScriptList": {"Script", "ScriptGroup"},
    "ScriptGroup": {"Script", "ScriptGroup"}, "Script": {"OrCondition", "ScriptAction", "ScriptActionFalse"},
    "OrCondition": {"Condition"},
}


def parse_chunks(b, p, end, ctx, parent, versions, order):
    while p < end:
        if end - p < 10:
            raise ValueError("trailing %d bytes in %s" % (end - p, parent))
        cid, ver, size = struct.unpack_from("<IHi", b, p)
        name = ctx.names[cid]
        if name not in ALLOWED_CHILD.get(parent, set()):
            raise ValueError("unexpected chunk %s in %s" % (name, parent or "<root>"))
        body = p + 10; cend = body + size
        if cend > end:
            raise ValueError("chunk %s overruns parent" % name)
        versions[(parent, name)].add(ver)
        if parent == "": order.append(name)
        r = R(b, body, cend)
        if name in LEAF:
            LEAF[name](r, ver, ctx)
            if r.left():
                raise ValueError("%s v%d leftover %d bytes" % (name, ver, r.left()))
        elif name in CONTAINER:
            if CONTAINER[name]: CONTAINER[name](r, ver, ctx)
            parse_chunks(b, r.p, cend, ctx, name, versions, order)
        else:
            raise ValueError("no parser for %s" % name)
        p = cend


def pure_maps():
    seen = {}
    for root in LOOSE_ROOTS:
        for f in glob.glob(os.path.join(root, "**", "*.map"), recursive=True):
            rel = os.path.relpath(f, root).replace("\\", "/").lower()
            if os.path.getsize(f) == 0:
                print("IGNORING zero-byte loose file", f)
                continue
            seen.setdefault(rel, ("loose", f, 0, 0))
    for b in pure_bigs():
        for name, off, size in census.big_entries(b):
            if name.lower().endswith(".map"):
                seen.setdefault(name.lower(), (b, name, off, size))
    return seen


def load(entry):
    src, name, off, size = entry
    if src == "loose":
        return open(name, "rb").read()
    return census.read_entry(src, off, size)


def main(only=None):
    maps = pure_maps()
    versions = collections.defaultdict(set)
    allstats = collections.defaultdict(collections.Counter)
    results = {}
    orders = collections.Counter()
    samples = collections.defaultdict(list)
    envs = collections.Counter()
    for key, entry in sorted(maps.items()):
        if only and only not in key: continue
        blob = load(entry)
        try:
            body, env = census.decode(blob)
            envs[env.split("(")[0]] += 1
            names, p = census.parse_toc(body)
            ctx = Ctx(names)
            order = []
            mv = collections.defaultdict(set)
            parse_chunks(body, p, len(body), ctx, "", mv, order)
            for k2, vv in mv.items(): versions[k2] |= vv
            orders[tuple(order)] += 1
            for k, c in ctx.stats.items(): allstats[k].update(c)
            for k, s in ctx.samples.items(): samples[k].extend(s[:3])
            results[key] = {"src": entry[0], "ok": True, "w": ctx.w, "h": ctx.h, "rawsize": len(body),
                            "vers": {(a + "/" + b2): sorted(vv) for (a, b2), vv in mv.items()}}
        except Exception as ex:
            results[key] = {"src": entry[0], "ok": False, "err": repr(ex)}
    ok = sum(1 for r in results.values() if r["ok"])
    print("maps", len(results), "ok", ok, "envelopes", dict(envs))
    for k, r in results.items():
        if not r["ok"]: print("FAIL", k, r["err"])
    print("srcs", collections.Counter(r["src"] for r in results.values()))
    for (par, name), vs in sorted(versions.items()):
        print("version", par or "<root>", name, sorted(vs))
    for o, c in orders.most_common(): print("order", c, o)
    out = {"results": results,
           "stats": {k: {repr(kk): vv for kk, vv in c.most_common()} for k, c in allstats.items()},
           "samples": {k: v[:20] for k, v in samples.items()}}
    json.dump(out, open(os.path.join(HERE, "fullparse.json"), "w"), indent=1, default=str)


if __name__ == "__main__":
    main(sys.argv[1] if len(sys.argv) > 1 else None)
