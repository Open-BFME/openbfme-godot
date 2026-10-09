"""Independent scan of the retail FX INI files (FXParticleSystem.ini, FXList.ini).

Shares no code with the engine: a line scanner counts blocks, module classes, nugget types and field uses so the
engine's parsers can be checked against it (engine/tests/test_fx_particle_ini.cpp pins the numbers in
engine/tests/data/fx/fx_ini_counts.json; test_fx_ini_scan.py regenerates them from the archives).

    ROTWK_INSTALL=... BFME2_INSTALL=... python3 tools/fx/fx_ini_scan.py [--write]
"""
from __future__ import annotations

import json
import re
import sys
from collections import Counter, defaultdict
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from bigfs import Mount  # noqa: E402

GOLDEN = HERE.parent.parent / "engine" / "tests" / "data" / "fx" / "fx_ini_counts.json"

# nesting of an FXParticleSystem block: `System` opens a sub-block without '='; the nine categories open one with '='
PARTICLE_CATEGORIES = ("Color", "Alpha", "Update", "Physics", "EmissionVelocity", "EmissionVolume", "Draw", "Wind", "Event")
# FXList: keywords that open an End-terminated nugget block (the other FXList-level keywords are one line)
NUGGET_BLOCKS = ("Sound", "EvaEvent", "RayEffect", "LightPulse", "CameraShakerVolume", "ViewShake", "AttachedModel", "TerrainScorch",
                 "ParticleSystem", "FXListAtBonePos", "CursorParticleSystem", "DynamicDecal", "Laser", "TintDrawable", "BuffNugget")
FXLIST_LINES = ("ParticleSysBone", "CullingInfo", "PlayEvenIfShrouded")


def logical_lines(text: str):
    for raw in text.splitlines():
        line = raw.split(";", 1)[0]
        line = line.split("//", 1)[0].strip()
        if line:
            yield line


def split_tokens(line: str):
    return [t for t in re.split(r"[ \t=]+", line) if t]


def scan_particle_systems(text: str) -> dict:
    blocks = []
    cur = None
    depth = 0
    module = None
    for line in logical_lines(text):
        toks = split_tokens(line)
        head = toks[0]
        if depth == 0:
            if head == "FXParticleSystem":
                cur = {"name": toks[1], "classes": [], "fields": Counter(), "systemFields": Counter()}
                blocks.append(cur)
                depth = 1
            else:
                raise ValueError(f"top-level token {head!r} outside a block")
            continue
        if head.lower() == "end":
            depth -= 1
            module = None
            continue
        if depth == 1:
            if head == "System":
                module = "System"
            elif head in PARTICLE_CATEGORIES:
                module = toks[1]
                cur["classes"].append((head, toks[1]))
            else:
                raise ValueError(f"{cur['name']}: unknown template line {head!r}")
            depth = 2
        else:
            key = "System" if module == "System" else module
            cur["fields"][f"{key}.{head}"] += 1
    return {"blocks": blocks, "tail_depth": depth}


def scan_fxlists(text: str) -> dict:
    blocks = []
    cur = None
    depth = 0
    nugget = None
    for line in logical_lines(text):
        toks = split_tokens(line)
        head = toks[0]
        if depth == 0:
            if head == "FXList":
                cur = {"name": toks[1], "nuggets": [], "fields": Counter(), "listFields": Counter()}
                blocks.append(cur)
                depth = 1
            else:
                raise ValueError(f"top-level token {head!r} outside a block")
            continue
        if head.lower() == "end":
            depth -= 1
            nugget = None
            continue
        if depth == 1:
            if head in NUGGET_BLOCKS:
                nugget = head
                cur["nuggets"].append(head)
                depth = 2
            elif head in FXLIST_LINES:
                cur["listFields"][head] += 1
            else:
                raise ValueError(f"{cur['name']}: unknown FXList line {head!r}")
        else:
            cur["fields"][f"{nugget}.{head}"] += 1
    return {"blocks": blocks, "tail_depth": depth}


def summarize(mount: Mount) -> dict:
    ps = scan_particle_systems(mount.read("data\\ini\\fxparticlesystem.ini").decode("latin-1"))
    fl = scan_fxlists(mount.read("data\\ini\\fxlist.ini").decode("latin-1"))
    assert ps["tail_depth"] == 0 and fl["tail_depth"] == 0
    classes = Counter()
    pfields = Counter()
    for b in ps["blocks"]:
        for cat, cls in b["classes"]:
            classes[f"{cat}={cls}"] += 1
        pfields.update(b["fields"])
    nuggets = Counter()
    nfields = Counter()
    listfields = Counter()
    for b in fl["blocks"]:
        nuggets.update(b["nuggets"])
        nfields.update(b["fields"])
        listfields.update(b["listFields"])
    return {
        "fxParticleSystem": {
            "blocks": len(ps["blocks"]),
            "uniqueNames": len({b["name"] for b in ps["blocks"]}),
            "classes": dict(sorted(classes.items())),
            "moduleFieldUses": dict(sorted(pfields.items())),
        },
        "fxList": {
            "blocks": len(fl["blocks"]),
            "uniqueNames": len({b["name"] for b in fl["blocks"]}),
            "nuggets": dict(sorted(nuggets.items())),
            "nuggetFieldUses": dict(sorted(nfields.items())),
            "listFieldUses": dict(sorted(listfields.items())),
        },
    }


def main(argv) -> int:
    summary = summarize(Mount())
    text = json.dumps(summary, indent=1, sort_keys=True) + "\n"
    if "--write" in argv:
        GOLDEN.parent.mkdir(parents=True, exist_ok=True)
        GOLDEN.write_text(text)
        print(f"wrote {GOLDEN}")
    else:
        print(text)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
