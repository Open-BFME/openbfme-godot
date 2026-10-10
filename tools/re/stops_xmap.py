"""Cross docs/STOPS.md with the RotWK -> BFME2 -> decomp map (tools/re/rw2decomp.py data).

For every open stop, each cited RotWK address (default reading of a bare 0x... value; values cited
after "BFME2" are mapped back to RotWK) is resolved to its function, tier and decomp status. A
stop is a "read the source" candidate when a cited site lies in a tier-A function whose BFME2
counterpart has byte-matched C++ in Open-BFME-2 (tier B with byte-matched C++ is listed second:
the source applies after checking the RotWK differences). Candidates are ranked by skirmish
impact (keywords of the stop's Area, see IMPACT), then by coverage (share of the stop's cited
code sites the source describes: tier A counts 1, tier B 1/2), then by the number of A sites.

  python tools/re/stops_xmap.py [--stops docs/STOPS.md] [--map rw_map.csv] [--out stops_xmap.md]
"""
import argparse
import bisect
import csv
import os
import re
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rw2decomp  # noqa: E402

BASE = rw2decomp.BASE
TEXT_END = 0xBD0000            # RotWK .text ends at VA 0xBD0000 (both maps cover .text only)
CPP = ("authored", "vendored", "generated")
HEXRE = re.compile(r"0x[0-9A-Fa-f]{5,8}")
MARK = re.compile(r"\b(RW|RotWK|BFME2|BFME1|BFME 2|BFME 1|ZH|lotrbfme|Open-BFME-[12])\b")

# skirmish impact: 3 = simulation outcome in every skirmish, 2 = visible every game / AI quality,
# 1 = shell, online, campaign, tooling
IMPACT = [
    (3, re.compile(r"\b(combat|damage|weapons?|armou?r|attacks?|death|special powers?|abilit(y|ies)|horde\w*|"
                   r"locomotors?|movement|move|pathfind\w*|formations?|collisions?|physics\w*|RNG|random|"
                   r"economy|resources?|command points?|build\w*|construct\w*|upgrades?|experience|"
                   r"veterancy|stealth|invisib\w*|garrison\w*|contain\w*|production|spawn\w*|heroe?s?|"
                   r"leadership|fear|emotions?|projectiles?|crush\w*|trample|skirmish AI|AI|victory|siege|"
                   r"transports?|tunnels?|stances?|turrets?|partition|shockwaves?|melee|exit|slow death|"
                   r"behaviou?r\w*|\w*body|object (creation|modifiers?)|modules?)\b", re.I)),
    (2, re.compile(r"\b(draw\w*|anim\w*|model conditions?|W3D|render\w*|terrain|particles?|FX|audio|sounds?|"
                   r"selection|control bar|command buttons?|input|camera|radar|tooltips?|INI|scripts?|"
                   r"lighting|look|look-ahead|fade)\b", re.I)),
]


def impact(area, text):
    for score, rx in IMPACT:
        if rx.search(area):
            return score
    return 1


def parse_stops(path):
    rows = []
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        if not re.match(r"^\|\s*S-\d+\s*\|", line):
            continue
        cells = [c.strip() for c in line.strip().strip("|").split("|")]
        sid, kind, area = cells[0], cells[1], cells[2]
        gap = cells[3] if len(cells) > 3 else ""
        closed = bool(re.match(r"\s*(RESOLVED|CLOSED)\b", gap)) or "(closed" in line.lower()
        rows.append({"id": sid, "kind": kind, "area": area, "text": line, "closed": closed})
    return rows


def cited(text):
    """[(va, game)] for every hex value; game from the nearest preceding marker in the clause."""
    out = []
    for m in HEXRE.finditer(text):
        v = int(m.group(0), 16)
        start = max(0, m.start() - 80)
        window = text[start:m.start()]
        window = re.split(r"[;.()]\s|\|", window)[-1]
        marks = MARK.findall(window)
        game = marks[-1] if marks else "RW"
        game = {"RotWK": "RW", "BFME 2": "BFME2", "BFME 1": "BFME1"}.get(game, game)
        out.append((v, game))
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    root = rw2decomp.main_checkout()
    ap.add_argument("--stops", type=Path, default=Path(__file__).resolve().parents[2] / "docs/STOPS.md")
    ap.add_argument("--map", type=Path,
                    default=Path(os.environ.get("OPENBFME_XMAP") or root / "workspace/rebuild/xmap/rw_map.csv"))
    ap.add_argument("--out", type=Path)
    args = ap.parse_args()
    rows = rw2decomp.load(args.map)
    starts = [r["_rva"] for r in rows]
    by_b2 = sorted(((r["_b2"], r) for r in rows if r["_b2"] is not None), key=lambda x: x[0])
    b2_starts = [x[0] for x in by_b2]

    def rw_fn(rva):
        k = bisect.bisect_right(starts, rva) - 1
        if k >= 0 and rva < rows[k]["_rva"] + rows[k]["_size"]:
            return rows[k]
        return None

    def b2_fn(rva):
        k = bisect.bisect_right(b2_starts, rva) - 1
        for j in range(k, max(-1, k - 8), -1):  # several RotWK functions may share one counterpart
            r = by_b2[j][1]
            if r["_b2"] <= rva < r["_b2"] + int(r["bfme2_size"] or 0):
                return r
        return None

    stops = parse_stops(args.stops)
    results = []
    for st in stops:
        if st["closed"]:
            continue
        sites = {}
        for va, game in cited(st["text"]):
            if not (BASE + 0x1000 <= va < TEXT_END):
                continue
            if game == "RW":
                r = rw_fn(va - BASE)
            elif game == "BFME2":
                r = b2_fn(va - BASE)
            else:
                continue
            if r is not None:
                sites[r["_rva"]] = (r, game, va)
        if not sites:
            continue
        a_cpp = [s for s in sites.values() if s[0]["tier"] == "A" and s[0]["decomp_status"] in CPP]
        b_cpp = [s for s in sites.values() if s[0]["tier"] == "B" and s[0]["decomp_status"] in CPP]
        results.append({"stop": st, "sites": list(sites.values()), "a": a_cpp, "b": b_cpp,
                        "cover": (len(a_cpp) + 0.5 * len(b_cpp)) / len(sites),
                        "impact": impact(st["area"], st["text"])})

    def fmt_site(s):
        r, game, va = s
        src = r["decomp_source"].split("/")[-1] if r["decomp_source"] else "-"
        name = r["decomp_qualname"].split("::")[-2:] if r["decomp_qualname"] else []
        cite = f"{game} 0x{va:X}" if game != "RW" else f"0x{va:X}"
        return f"{cite} -> RW 0x{r['_rva'] + BASE:X} {r['tier']} {r['decomp_status'] or 'rotwk-only'} {src} {'::'.join(name)}"

    out = []
    total_open = sum(1 for s in stops if not s["closed"])
    out.append(f"# STOPS x RotWK->BFME2->decomp map\n")
    out.append(f"Open stops: {total_open}; with resolvable code addresses: {len(results)}; "
               f"with a tier-A byte-matched site: {sum(1 for r in results if r['a'])}; "
               f"with only tier-B byte-matched sites: {sum(1 for r in results if not r['a'] and r['b'])}.\n")
    for title, sel in (("Tier A with byte-matched decomp source (read the source)", lambda r: r["a"]),
                       ("Tier B with byte-matched decomp source (read the source, verify the RotWK edits)",
                        lambda r: not r["a"] and r["b"])):
        out.append(f"\n## {title}\n")
        out.append("| Rank | Stop | Impact | Coverage | Area | Sites (cited -> RW function, tier, decomp status, file, name) |")
        out.append("|---|---|---|---|---|---|")
        picked = sorted((r for r in results if sel(r)),
                        key=lambda r: (-r["impact"], -r["cover"], -len(r["a"]), r["stop"]["id"]))
        for n, r in enumerate(picked, 1):
            sites = r["a"] if r["a"] else r["b"]
            others = len(r["sites"]) - len(sites)
            txt = "; ".join(fmt_site(s) for s in sites[:4]) + (f"; +{len(sites) - 4} more" if len(sites) > 4 else "")
            if others:
                txt += f" (+{others} other cited site(s) not covered)"
            cov = f"{len(r['a'])}A+{len(r['b'])}B/{len(r['sites'])}"
            out.append(f"| {n} | {r['stop']['id']} | {r['impact']} | {cov} | {r['stop']['area']} | {txt} |")
    text = "\n".join(out) + "\n"
    if args.out:
        args.out.write_text(text, encoding="utf-8")
    print(text)


if __name__ == "__main__":
    main()
