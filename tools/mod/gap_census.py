#!/usr/bin/env python3
"""Lane MODCOMPAT-1: the mod gap census. Plays one headless skirmish per All In One package of a mod corpus (the layout of
tools/mod/aio_package.py; the corpus test's mount, engine/tests/test_mods1_retail.cpp) and ranks what the games ran into that is not ported:

  * unported_module <class> <objects>      live objects carrying an UnportedModule of that class at the end (stop S-140)
  * unported_action <name> <count>         script actions that did nothing (S-1180)
  * unported_condition <name> <count>      script conditions that evaluated false because they are not ported (S-1180)
  * unported_template_module <class> <n>   the static side: object templates whose behavior modules name that unported class, reached in
                                           the game or not (sizes the module ports)

(openbfme_peer's report lines). The ranking: by how many packages hit an item, then by the total count. The output is markdown
(logs/mod-gap-census.md): the list the porting lanes take top-down, retail-first, from the decomp.

    gap_census.py --peer <openbfme_peer> --corpus <dir of package folders> --out <md> [--frames N] [--only NAME ...] [--jobs N]

ROTWK_INSTALL and BFME2_INSTALL must be set (the peer mounts the player's 2.01). A package whose game does not start is listed with the peer's
error: that is a gap too (a faction or map the mod renamed, an unknown game program, a data error that stops the start).
"""
from __future__ import annotations

import argparse
import concurrent.futures
import json
import os
import subprocess
import sys
import tempfile
from collections import defaultdict
from pathlib import Path

# an 8-player retail map: the local side idle, three hard computer players of the mod's first playable factions (the peer names them when it
# is given an unknown one). Fall Back 8p is a 2.01 map; a mod that ships its own map cache without it (the Patch 2.02 family, BOTTA) offers
# no such game in retail either, so it plays the next map its cache lists
MAPS = ["maps/map mp fall back 8p/map mp fall back 8p.map", "maps/map mp evendim/map mp evendim.map"]
PROBE_FACTION = "FactionNone_Census"


def mount_args(package: Path) -> tuple[list[str], list[str]]:
    """the peer's mount arguments for an All In One package folder, as the corpus test mounts it; and notes"""
    record = json.loads((package / "openbfme-aio.json").read_text(encoding="utf-8"))
    args: list[str] = []
    notes: list[str] = []
    mod_part = None
    for member in record.get("stack", []):
        folder = package.parent / member["folder"]
        if (folder / "install").is_dir():
            args += ["--mod-install", str(folder / "install")]  # stack order = priority order, as MountInstallOverlay takes it
        if mod_part is None and (folder / "mod").is_dir():
            mod_part = folder / "mod"
    if mod_part is not None:
        args += ["--mod", str(mod_part)]
    for edit in record.get("engine_edits", []):
        at = edit.rfind(" md5 ")
        if at >= 0:
            args += ["--mod-engine", edit[at + 5:]]
        notes.append("engine edit: " + edit)
    return args, notes


def peer_cmd(peer: str, mount: list[str], frames: int, port: int, factions: list[str], report: Path, map_path: str = MAPS[0]) -> list[str]:
    slots = ["--slot", f"human,{factions[0]},0,0"]
    for i in range(3):
        slots += ["--slot", f"hard,{factions[(i + 1) % len(factions)]},{i + 1},{i + 1}"]
    return [peer, "--frames", str(frames), "--host", str(port), "--bind", "127.0.0.1", "--map", map_path, "--seed", "7", "--lobby-timeout", "60",
            "--report", str(report)] + slots + mount


def run_game(peer: str, package: Path, frames: int, workdir: Path, port: int) -> dict:
    mount, notes = mount_args(package)
    report = workdir / f"{package.name}.txt"
    # the mod's playable factions: a probe with an unknown faction makes the peer list them (it stops before loading a map)
    probe = subprocess.run(peer_cmd(peer, mount, 1, port, [PROBE_FACTION], report), capture_output=True, text=True, timeout=3600)
    text = probe.stderr or ""
    at = text.find("(playable: ")
    if at < 0:
        last = (text.strip().splitlines() or [f"exit {probe.returncode}"])[-1]
        return {"package": package.name, "ok": False, "error": last, "notes": notes, "lines": []}
    factions = text[at + len("(playable: "):text.find(")", at)].split()
    if not factions:
        line = next((l for l in text.splitlines() if "(playable: " in l), "")
        return {"package": package.name, "ok": False, "error": f"the mod lists no playable faction ({line.strip()})", "notes": notes, "lines": []}
    for map_path in MAPS:
        r = subprocess.run(peer_cmd(peer, mount, frames, port, factions, report, map_path), capture_output=True, text=True, timeout=3 * 3600)
        if r.returncode == 0 or "is not in the map cache" not in (r.stderr or ""):
            break
    if r.returncode == 0 and report.exists():
        used = [factions[i % len(factions)] for i in range(4)]
        on = "" if map_path == MAPS[0] else f" on {map_path.split('/')[1]}"
        return {"package": package.name, "ok": True, "sides": f"{used[0]} (idle) vs hard {', '.join(used[1:])}{on}", "notes": notes,
                "lines": report.read_text(encoding="utf-8", errors="replace").splitlines()}
    last = ((r.stderr or "").strip().splitlines() or [f"exit {r.returncode}"])[-1]
    return {"package": package.name, "ok": False, "error": last, "notes": notes, "lines": []}


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--peer", required=True)
    ap.add_argument("--corpus", type=Path, required=True)
    ap.add_argument("--out", type=Path, required=True)
    ap.add_argument("--frames", type=int, default=3000)
    ap.add_argument("--only", action="append", default=[])
    ap.add_argument("--jobs", type=int, default=2)
    ap.add_argument("--reports", type=Path, help="keep every package's peer report here (default: a temporary folder)")
    a = ap.parse_args(argv)
    for v in ("ROTWK_INSTALL", "BFME2_INSTALL"):
        if not os.environ.get(v):
            raise SystemExit(f"gap_census: {v} is not set")
    # a package whose record has no stack is a dependency laid out for another package (dependency_only: an asset layer, Patch 2.02 under
    # its add-ons); like the corpus test, it is played through the packages that stack it
    def playable(p: Path) -> bool:
        f = p / "openbfme-aio.json"
        return f.exists() and isinstance(json.loads(f.read_text(encoding="utf-8")).get("stack"), list)
    packages = sorted(p for p in a.corpus.iterdir() if playable(p) and (not a.only or p.name in a.only))
    if not packages:
        raise SystemExit(f"gap_census: no package (a folder with openbfme-aio.json) in {a.corpus}")
    with tempfile.TemporaryDirectory(prefix="gap-census-") as tmp:
        keep = a.reports if a.reports else Path(tmp)
        keep.mkdir(parents=True, exist_ok=True)
        with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:
            results = list(pool.map(lambda ip: run_game(a.peer, ip[1], a.frames, keep, 47600 + ip[0]), enumerate(packages)))
    hits: dict[tuple[str, str], dict] = {}
    for res in results:
        for line in res["lines"]:
            parts = line.split(" ")
            if parts[0] in ("unported_module", "unported_action", "unported_condition") and len(parts) >= 3:
                key = (parts[0].removeprefix("unported_"), " ".join(parts[1:-1]))
                h = hits.setdefault(key, {"packages": [], "total": 0})
                h["packages"].append(res["package"])
                h["total"] += int(parts[-1])
    ranked = sorted(hits.items(), key=lambda kv: (-len(kv[1]["packages"]), -kv[1]["total"], kv[0]))
    static: dict[str, dict] = {}
    for res in results:
        for line in res["lines"]:
            parts = line.split(" ")
            if parts[0] == "unported_template_module" and len(parts) == 3:
                h = static.setdefault(parts[1], {"packages": {}, "total": 0})
                h["packages"][res["package"]] = int(parts[2])
                h["total"] += int(parts[2])
    static_ranked = sorted(static.items(), key=lambda kv: (-len(kv[1]["packages"]), -kv[1]["total"], kv[0]))
    out = ["# Mod gap census (lane MODCOMPAT-1)", "",
           f"One headless skirmish per All In One package ({a.frames} frames, Fall Back 8p or, where the mod's map cache lacks it, Evendim; the",
           "local side idle beside three hard computer players of the mod's first playable factions), the report's unported modules (live",
           "objects at the end), script actions and conditions; a package whose game does not start is listed with the error.",
           "Ranked by how many packages hit an item, then by the total. Generated by tools/mod/gap_census.py; the porting lanes take it",
           "top-down, retail-first from the decomp.", "",
           "## Ranked gaps", "", "| # | Kind | Name | Packages | Total | Which |", "|---:|---|---|---:|---:|---|"]
    for i, ((kind, name), h) in enumerate(ranked, 1):
        which = ", ".join(sorted(set(h["packages"])))
        out.append(f"| {i} | {kind} | `{name}` | {len(set(h['packages']))} | {h['total']} | {which} |")
    out += ["", "## Unported module classes by object template (static)", "",
            "Every object template of the mod (its final override) whose behavior modules name an unported class, whether the game reached it or",
            "not: the size of each module port. A ported class that falls back to an UnportedModule for some data is not counted here.", "",
            "| # | Class | Packages | Templates | Most templates in |", "|---:|---|---:|---:|---|"]
    for i, (name, h) in enumerate(static_ranked, 1):
        top = ", ".join(f"{p} {n}" for p, n in sorted(h["packages"].items(), key=lambda pn: (-pn[1], pn[0]))[:3])
        out.append(f"| {i} | `{name}` | {len(h['packages'])} | {h['total']} | {top} |")
    out += ["", "## Packages", "", "| Package | Game | Sides / error | Notes |", "|---|---|---|---|"]
    for res in results:
        state = "played" if res["ok"] else "did not start"
        detail = res.get("sides", "") if res["ok"] else res.get("error", "").replace("|", "/")
        out.append(f"| {res['package']} | {state} | {detail} | {'; '.join(res['notes'])} |")
    a.out.parent.mkdir(parents=True, exist_ok=True)
    a.out.write_text("\n".join(out) + "\n", encoding="utf-8")
    print(f"gap_census: {len(results)} packages, {sum(r['ok'] for r in results)} played, {len(ranked)} gaps, "
          f"{len(static_ranked)} unported classes in templates -> {a.out}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
