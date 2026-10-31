#!/usr/bin/env python3
"""Lane MODS-2: lay out one All In One BFME Launcher package the way OpenBFME mounts it (a developer tool for the mod corpus).

    python3 tools/mod/aio_package.py <package guid> <corpus dir> [--dry-run]

The All In One launcher (github.com/MarcellVokk/BfmeFoundationProject, WorkshopKit BfmeWorkshopSyncManager.Sync) builds a game folder from
layers: L1 the enabled enhancements (Type 2), L2 the package itself, L3 the patches it depends on (Type 0), L4 the base game; a file with
the same destination as one of an earlier layer is skipped (AddFiles). Each entry's destination is its type's folder
(BfmeWorkshopEntry.GetDestinationDirectory): Type 0 / 2 / 4 the game folder, Type 1 its own "BFME Workshop\\Mods\\<name>-<guid>" folder
(the game then starts with -mod <that folder>, BfmeLaunchManager), Type 3 the user data Maps\\Workshop folder.

OpenBFME keeps the verified retail install as L4. Every package of the stack gets its own folder <corpus>/<name>/ holding only its own
files, so a patch shared by several mods (Patch 2.02, Edain) is downloaded once:
    <corpus>/<name>/install/   its game-folder files (Type 0 / 2 / 4: mounted with --mod-install)
    <corpus>/<name>/mod/       a Type 1 package's files (mounted with -mod)
    <corpus>/<name>/maps/      a Type 3 package's files (not mounted yet: user maps are ROADMAP MOD-5)
    <corpus>/<name>/openbfme-aio.json   the package and its stack: the folder names in priority order (L1 the enhancements, L2 the package,
                              L3 the patches), the engine-edit files found in the stack (game.dat / lotrbfme2ep1.exe / *.dll: not loaded, they
                              need engine features) and the files left out. Mount the stack's install/ folders in that order (--mod-install
                              repeated) and its Type 1 mod/ folder (-mod).
Every file is downloaded from the service's content-addressed store, resumed with Range, and checked against its MD5. Nothing here is
committed: the corpus lives on the developer machines (PLAN rule 6).
"""
import hashlib
import json
import os
import sys
import urllib.request

API = "https://bfmeladder.com/api/workshop/download?guid="
HEADERS = {"AuthAccountUuid": "unauthenticated", "AuthAccountPassword": "", "User-Agent": "OpenBFME-launcher (mod corpus, dev)"}
LANGUAGE = "EN"
# WorkshopKit ConfigUtils.IgnoredGameFiles (the launcher never installs these) and files a port never reads
IGNORED = {"_zzlotr.big", "cs01.vp6", "nlc_logo.vp6", "te_logo.vp6", "ealogo.vp6", "newlinelogo.vp6", "mod.txt"}
# the game program (what RotWK runs: lotrbfme2ep1.exe starts game.dat) and code injected into it: an engine edit, never loaded by the port
ENGINE_EDIT_EXT = (".dll", ".asi")
ENGINE_EDIT_NAMES = {"game.dat", "game2.dat", "file.dat", "lotrbfme2ep1.exe"}
# other programs a package carries (its own launcher, Worldbuilder, installers): tools, not part of the game
TOOL_EXT = (".exe", ".msi")
SKIP_EXT = (".pdf", ".wps")


def fetch(guid):
    req = urllib.request.Request(API + guid, headers=HEADERS)
    return json.load(urllib.request.urlopen(req, timeout=120))


def dependency_guid(dep):
    return dep.split(":")[0]


def layers(entry):
    """the entries of L1, L2, L3 in priority order (L4, the base game, is the verified retail install)"""
    deps, todo, seen = [], list(entry.get("Dependencies") or []), {entry["Guid"]}
    while todo:
        g = dependency_guid(todo.pop(0))
        if g in seen or g.startswith("original-"):
            continue
        seen.add(g)
        d = fetch(g)
        deps.append(d)
        todo.extend(d.get("Dependencies") or [])
    enhancements = [d for d in deps if d["Type"] == 2]
    patches = [d for d in deps if d["Type"] == 0]
    others = [d for d in deps if d["Type"] not in (0, 2)]
    if entry["Type"] == 2:
        # an enhancement alone: it sits over the package it extends (the launcher enables it with that package)
        return [entry] + enhancements, others + patches
    return enhancements + [entry], patches + others


def destination(entry):
    return {1: "mod", 3: "maps"}.get(entry["Type"], "install")


def folder_name(entry):
    return entry["Name"].strip().replace(" ", "_").replace("/", "_")


def plan_entry(entry, taken, engine_edits, left_out):
    """the files of one package, skipping destinations a higher layer took (AddFiles)"""
    plan = []
    if entry.get("Game") != 2:
        # a layer for another game (a BFME2 enhancement such as "Edain assets": its asset.dat belongs in the BFME2 folder, which OpenBFME keeps
        # pristine); listed, never taken into the RotWK install part
        left_out.extend(f"{entry['Name']}: {f['Name']} (for game {entry.get('Game')}, not RotWK)" for f in entry["Files"])
        return plan
    for f in entry["Files"]:
        name = f["Name"].replace("/", "\\")
        low = os.path.basename(name).lower()
        if f.get("Language") not in ("ALL", "", None) and LANGUAGE not in f["Language"].split():
            continue
        part = destination(entry)
        key = (part, name.lower())
        if key in taken:
            continue
        taken.add(key)
        if low in IGNORED or low.endswith(SKIP_EXT):
            left_out.append(f"{entry['Name']}: {name}")
            continue
        if low.endswith(ENGINE_EDIT_EXT) or low in ENGINE_EDIT_NAMES:
            engine_edits.append(f"{entry['Name']}: {name} md5 {f['Md5'].lower()}")  # the game's ModEngineFeatures profile key (--mod-engine)
            continue
        if low.endswith(TOOL_EXT):
            left_out.append(f"{entry['Name']}: {name} (a tool)")
            continue
        plan.append((part, name, f))
    return plan


def download(plan, out):
    bad = 0
    for part, name, f in plan:
        dst = os.path.join(out, part, *name.split("\\"))
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        if os.path.exists(dst) and os.path.getsize(dst) == f["Size"] and f["Size"] > 0:
            continue  # sized match (the store is content-addressed and the file was MD5-checked when it was written)
        if os.path.exists(dst) and hashlib.md5(open(dst, "rb").read()).hexdigest() == f["Md5"]:
            continue
        tmp = dst + ".part"
        have = os.path.getsize(tmp) if os.path.exists(tmp) else 0
        req = urllib.request.Request(f["Url"], headers={**HEADERS, **({"Range": f"bytes={have}-"} if have else {})})
        with urllib.request.urlopen(req, timeout=300) as resp, open(tmp, "ab" if have and resp.status == 206 else "wb") as o:
            while True:
                block = resp.read(1 << 20)
                if not block:
                    break
                o.write(block)
        md5 = hashlib.md5()
        with open(tmp, "rb") as i:
            for block in iter(lambda: i.read(1 << 22), b""):
                md5.update(block)
        if md5.hexdigest() != f["Md5"]:
            print("MD5 MISMATCH", name, flush=True)
            os.remove(tmp)
            bad += 1
            continue
        os.replace(tmp, dst)
    return bad


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 2
    guid, corpus = argv[1], argv[2]
    dry = "--dry-run" in argv
    entry = fetch(guid)
    first, rest = layers(entry)
    stack = first + rest
    taken, engine_edits, left_out, bad, files, size = set(), [], [], 0, 0, 0
    plans = []
    for e in stack:
        plan = plan_entry(e, taken, engine_edits, left_out)
        plans.append((e, plan))
        files += len(plan)
        size += sum(f["Size"] for _, _, f in plan)
    summary = {
        "schema": "openbfme.aio-package 2",
        "guid": entry["Guid"], "name": entry["Name"], "version": entry["Version"], "type": entry["Type"],
        "stack": [{"folder": folder_name(e), "name": e["Name"], "version": e["Version"], "type": e["Type"], "guid": e["Guid"],
                   "parts": sorted({p for p, _, _ in plan})} for e, plan in plans],
        "files": files, "bytes": size, "engine_edits": engine_edits, "left_out": left_out,
    }
    print(json.dumps({k: summary[k] for k in ("name", "version", "type", "files", "bytes", "engine_edits")}, indent=1), flush=True)
    print("stack:", " > ".join(s["folder"] for s in summary["stack"]), flush=True)
    if dry:
        return 0
    for e, plan in plans:
        out = os.path.join(corpus, folder_name(e))
        os.makedirs(out, exist_ok=True)  # a package may hold nothing OpenBFME takes (only an engine-edit file: the 2.02 Delayfix Addon)
        bad += download(plan, out)
        if e is not entry and not os.path.exists(os.path.join(out, "openbfme-aio.json")):
            with open(os.path.join(out, "openbfme-aio.json"), "w") as o:  # a dependency's own record (its own stack is written when it is fetched itself)
                json.dump({"schema": "openbfme.aio-package 2", "guid": e["Guid"], "name": e["Name"], "version": e["Version"], "type": e["Type"],
                           "stack": None, "dependency_only": True}, o, indent=1)
    with open(os.path.join(corpus, folder_name(entry), "openbfme-aio.json"), "w") as o:
        json.dump(summary, o, indent=1)
    print("DONE", entry["Name"], "bad", bad, flush=True)
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
