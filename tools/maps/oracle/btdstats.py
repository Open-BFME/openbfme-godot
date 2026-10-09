"""Per-map blend / plane statistics of the BlendTileData chunk, computed straight from the map bytes (independent of
the C++ engine). compute() returns one row per map; map_survey.py embeds them as each map's "btdstats"."""
import census, struct, fullparse, collections, json


def compute(maps=None):
    maps = maps if maps is not None else fullparse.pure_maps()
    rows = []
    for key in sorted(maps):
        body, env = census.decode(fullparse.load(maps[key]))
        names, p = census.parse_toc(body)
        w = h = None
        while p < len(body):
            cid, ver, size = struct.unpack_from("<IHi", body, p)
            nm = names[cid]; b0 = p + 10
            if nm == "HeightMapData":
                w, h, border, nb = struct.unpack_from("<4i", body, b0)
                bounds = [struct.unpack_from("<2i", body, b0 + 16 + 8*i) for i in range(nb)]
                hs = struct.unpack_from("<%dH" % (w*h), body, b0 + 16 + 8*nb + 4)
            if nm == "BlendTileData":
                q = b0 + 4; n = w*h
                tiles = struct.unpack_from("<%dh" % n, body, q); q += 2*n
                isz = 4 if 14 <= ver < 24 else 2; fmt = "<%di" % n if isz == 4 else "<%dh" % n
                blend = struct.unpack_from(fmt, body, q); q += isz*n
                extra = struct.unpack_from(fmt, body, q); q += isz*n
                cliff = struct.unpack_from(fmt, body, q); q += isz*n
                rb = (w+7)//8
                planes = []
                nplanes = 1 + (ver>=10) + (ver>=11) + (14<=ver<25) + (ver>=15)
                for k in range(nplanes):
                    planes.append(body[q:q+rb*h]); q += rb*h
                flam = None
                if 16 <= ver < 25: flam = body[q:q+n]; q += n
                if ver >= 17: planes.append(body[q:q+rb*h]); q += rb*h
                nbt, nbl, ncl, ntc = struct.unpack_from("<4i", body, q)
                def popcount(bb): return sum(bin(x).count("1") for x in bb)
                rows.append(dict(map=key, ver=ver, w=w, h=h, border=border, bounds=bounds,
                    minH=min(hs), maxH=max(hs), numBitmapTiles=nbt, numBlended=nbl, numCliff=ncl, numClasses=ntc,
                    maxTile=max(tiles), minTile=min(tiles), maxBlend=max(blend), maxExtra=max(extra), maxCliffIdx=max(cliff),
                    cellsBlended=sum(1 for x in blend if x), cellsExtra=sum(1 for x in extra if x), cellsCliffUV=sum(1 for x in cliff if x),
                    planeBits=[popcount(pl) for pl in planes], flam=(collections.Counter(flam).most_common(5) if flam else None)))
            p += 10 + size
    return rows


if __name__ == "__main__":
    json.dump(compute(), open("btdstats.json", "w"), indent=0)
