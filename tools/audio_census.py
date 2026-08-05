"""Per-unit audio census: what retail authors, what the packs ship, what plays.

WHY THIS EXISTS. The owner reported that "many troops and heroes have generic
attack sounds".  That is one sentence covering at least three different
failures, and they need different fixes, so this script measures them apart
instead of guessing:

  N  AUTHORED  - how many retail Object blocks declare each per-unit audio field
                 (VoiceSelect / VoiceAttack / SoundImpact / ...), read straight
                 out of the extracted retail INI tree.
  M  SHIPPED   - how many playable-unit documents in the installed content packs
                 carry that field forward, and how many bind it to real samples.
  K  REACHABLE - which of the shipped fields the runtime can actually route,
                 given the kind aliases in
                 game/src/retail_slice/playable_unit_runtime_adapter.gd.

It also counts the WEAPON audio chain separately, because that chain is not on
the Object at all: retail authors melee/ranged hit sound as
``Weapon <name> / FireFX = <FXList>`` in ``weapon.ini`` and
``FXList <name> / Sound / Name = <AudioEvent>`` in ``fxlist.ini``.  No converted
pack carries it, which is why every non-siege, non-monster unit falls back to
one of two hardcoded class sounds in ``retail_slice_audio.gd``.

Read-only.  Reads the extracted retail INI tree and the installed content packs;
writes nothing but its report to stdout.

Usage:
    python tools/audio_census.py [--repo .] [--markdown]
"""

from __future__ import annotations

import argparse
import json
import re
from collections import Counter
from pathlib import Path

# Object-block audio fields, grouped the way the runtime thinks about them.
VOICE_FIELDS = [
    "VoiceSelect",
    "VoiceSelectBattle",
    "VoiceMove",
    "VoiceAttack",
    "VoiceAttackCharge",
    "VoiceAttackStructure",
    "VoiceAttackMachine",
    "VoiceGuard",
    "VoiceFear",
    "VoiceCreated",
    "VoiceFullyCreated",
    "VoiceRetreatToCastle",
    "VoiceGarrison",
]
SFX_FIELDS = ["SoundImpact", "SoundDie", "SoundMoveStart", "AnimationSound"]

# kind -> the document fields retail_slice_audio can route it from.  Mirrors
# playable_unit_runtime_adapter.gd `audio_event_ids`; kept here so a drift
# between the two shows up as a census number rather than as silence in game.
RUNTIME_KIND_ALIASES = {
    "select": ["voiceselect", "voiceselectbattle"],
    "move": ["voicemove"],
    "attack": ["voiceattack", "voiceattackmachine", "voiceattackstructure"],
    "attack_structure": ["voiceattackstructure", "voiceattackmachine"],
    "build": ["voicebuildresponse"],
    "death": ["voicedie", "sound", "sounddeath", "sounddeath1", "sounddeath2"],
}

_FIELD_RE = re.compile(r"^\s*([A-Za-z_][A-Za-z0-9_]*)\s*=\s*(\S+)", re.MULTILINE)
_OBJECT_RE = re.compile(r"^\s*Object\s+(\S+)", re.MULTILINE | re.IGNORECASE)
_BLOCK_OPENERS = (
    "weapon",
    "fxlist",
    "sound",
    "particlesystem",
    "fxparticlesystem",
    "lightpulse",
    "tracer",
    "viewshake",
    "terrainscorch",
    "raytracer",
)


def _top_level_blocks(text: str, keyword: str) -> list[tuple[str, list[str]]]:
    """Yield (name, body_lines) for every top-level `<keyword> <name> ... End`.

    These INI files nest (an FXList holds `Sound ... End` sub-blocks), so a
    non-greedy regex stops at the FIRST inner `End` and silently reports zero
    matches - which is exactly the kind of confidently-wrong number this census
    exists to avoid. Depth is counted instead.
    """
    blocks: list[tuple[str, list[str]]] = []
    lines = text.splitlines()
    index = 0
    keyword_lower = keyword.lower()
    while index < len(lines):
        stripped = lines[index].strip()
        head = stripped.split(";", 1)[0].strip()
        parts = head.split()
        if len(parts) >= 2 and parts[0].lower() == keyword_lower:
            name = parts[1]
            body: list[str] = []
            depth = 1
            index += 1
            while index < len(lines) and depth > 0:
                inner = lines[index].strip().split(";", 1)[0].strip()
                token = inner.split()[0].lower() if inner.split() else ""
                if token == "end":
                    depth -= 1
                    if depth == 0:
                        break
                elif token in _BLOCK_OPENERS and not inner.count("="):
                    depth += 1
                body.append(lines[index])
                index += 1
            blocks.append((name, body))
        index += 1
    return blocks


def _read(path: Path) -> str:
    return path.read_text(encoding="latin-1", errors="replace")


# --------------------------------------------------------------------------
# N - what retail authors
# --------------------------------------------------------------------------


def census_retail_objects(extract_root: Path) -> dict:
    object_dir = extract_root / "data" / "ini" / "object"
    field_counts: Counter = Counter()
    objects_seen = 0
    files_seen = 0
    if not object_dir.is_dir():
        return {"available": False, "objects": 0, "fields": {}, "files": 0}
    for path in object_dir.rglob("*.ini"):
        text = _read(path)
        files_seen += 1
        objects_seen += len(_OBJECT_RE.findall(text))
        present = {name.lower() for name, _ in _FIELD_RE.findall(text)}
        for field in VOICE_FIELDS + SFX_FIELDS:
            if field.lower() in present:
                field_counts[field] += 1
    return {
        "available": True,
        "objects": objects_seen,
        "files": files_seen,
        # NOTE: per-FILE presence, not per-Object - a file is one unit family in
        # this corpus, and per-Object attribution would need a full block parser.
        "fields": dict(field_counts),
    }


def census_retail_weapon_chain(extract_root: Path) -> dict:
    """The chain the packs do NOT carry: Weapon -> FireFX -> FXList -> Sound."""
    weapon_ini = extract_root / "data" / "ini" / "weapon.ini"
    fxlist_ini = extract_root / "data" / "ini" / "fxlist.ini"
    if not weapon_ini.is_file() or not fxlist_ini.is_file():
        return {"available": False}
    weapon_text = _read(weapon_ini)
    fx_text = _read(fxlist_ini)

    fx_sounds: dict[str, list[str]] = {}
    for name, body in _top_level_blocks(fx_text, "FXList"):
        sounds = []
        inside_sound = False
        for line in body:
            head = line.strip().split(";", 1)[0].strip()
            if head.lower() == "sound":
                inside_sound = True
                continue
            if head.lower() == "end":
                inside_sound = False
                continue
            if not inside_sound:
                continue
            match = _FIELD_RE.match(line)
            if match and match.group(1).lower() == "name":
                sounds.append(match.group(2))
        if sounds:
            fx_sounds[name.lower()] = sounds

    weapons = 0
    with_firefx = 0
    resolvable = 0
    events: set[str] = set()
    for _name, body in _top_level_blocks(weapon_text, "Weapon"):
        weapons += 1
        named_fx = False
        for line in body:
            match = _FIELD_RE.match(line)
            if not match or match.group(1).lower() not in {
                "firefx", "projectiledetonationfx", "veterancefirefx"
            }:
                continue
            named_fx = True
            hits = fx_sounds.get(match.group(2).lower(), [])
            if hits:
                resolvable += 1
                events.update(hits)
                break
        if named_fx:
            with_firefx += 1
    return {
        "available": True,
        "weapons": weapons,
        "weapons_with_fx": with_firefx,
        "weapons_reaching_a_sound": resolvable,
        "distinct_audio_events": sorted(events),
    }


# --------------------------------------------------------------------------
# M / K - what the packs ship and what the runtime can route
# --------------------------------------------------------------------------


def _selected_pack_dirs(repo: Path) -> list[Path]:
    selection = repo / ".private" / "content-packs" / "selection.json"
    if not selection.is_file():
        return []
    document = json.loads(selection.read_text(encoding="utf-8"))
    entries = [document.get("activePack", "")] + list(document.get("supplementalPacks", []))
    roots = []
    for entry in entries:
        if not entry:
            continue
        candidate = repo / ".private" / "content-packs" / entry
        if candidate.is_dir():
            roots.append(candidate)
    return roots


def census_pack(pack_dir: Path) -> dict:
    pack_json = pack_dir / "pack.json"
    if not pack_json.is_file():
        return {}
    document = json.loads(pack_json.read_text(encoding="utf-8"))
    unit_files = [
        value
        for key, value in document.get("files", {}).items()
        if key.startswith("playableUnit.")
    ]
    row = {
        "pack": pack_dir.parent.name,
        "units": 0,
        "with_any_binding": 0,
        "kinds": Counter(),
        "sound_impact": 0,
        "bodyfall": 0,
        "weapon_swing": 0,
        "bound_events": 0,
        # Units that will hit the hardcoded class swing in
        # `retail_slice_audio.gd::_route_weapon_swing` because nothing else can
        # answer: everything that is not a siege/monster with an authored
        # swing/fire event. This is the per-faction size of the "all attacks
        # sound the same" gap, and the row count the next cook has to close.
        "generic_swing_units": 0,
    }
    for relative in unit_files:
        path = pack_dir / relative
        if not path.is_file():
            continue
        unit = json.loads(path.read_text(encoding="utf-8"))
        registration = unit.get("registration", {})
        routes = registration.get("audioRoutes", {})
        bindings = registration.get("audioBindings", {})
        row["units"] += 1
        row["bound_events"] += len(bindings)
        if bindings:
            row["with_any_binding"] += 1

        fields: set[str] = set()
        for owner in routes.values():
            if isinstance(owner, dict):
                fields.update(str(name).lower() for name in owner)
        for kind, aliases in RUNTIME_KIND_ALIASES.items():
            if fields.intersection(aliases):
                row["kinds"][kind] += 1
        if "soundimpact" in fields:
            row["sound_impact"] += 1
        if any("bodyfall" in str(key).lower() for key in bindings):
            row["bodyfall"] += 1
        # A per-unit WEAPON swing/fire event. Nothing emits this today; the
        # column exists so the next cook can be measured against zero.
        if any(
            "firefx" in str(key).lower() or "weaponsound" in str(key).lower()
            for key in bindings
        ):
            row["weapon_swing"] += 1

        category = str(unit.get("category", ""))
        authored_swing = False
        if category == "monster":
            authored_swing = any(
                "swing" in str(key).lower()
                or (
                    str(key).lower().startswith("impact")
                    and ("punch" in str(key).lower() or "kick" in str(key).lower())
                )
                for key in bindings
            )
        elif category == "siege":
            authored_swing = any(
                str(entry.get("id", "")) in bindings
                for owner in routes.values()
                if isinstance(owner, dict)
                for entry in owner.get("AnimationSound", [])
                if isinstance(entry, dict)
            )
        if not authored_swing:
            row["generic_swing_units"] += 1
    return row


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=".", help="repository root")
    parser.add_argument("--markdown", action="store_true", help="emit markdown tables")
    args = parser.parse_args()
    repo = Path(args.repo).resolve()

    retail = census_retail_objects(repo / "_bfme2_extract")
    weapons = census_retail_weapon_chain(repo / "_bfme2_extract")
    packs = [census_pack(path) for path in _selected_pack_dirs(repo)]
    packs = [row for row in packs if row and row["units"]]

    bar = "|" if args.markdown else " "

    print("== N: RETAIL AUTHORED (files under data/ini/object declaring the field) ==")
    if retail["available"]:
        print("object .ini files: %d, Object blocks: %d" % (retail["files"], retail["objects"]))
        if args.markdown:
            print("| field | files declaring it |")
            print("|---|---|")
        for field in VOICE_FIELDS + SFX_FIELDS:
            count = retail["fields"].get(field, 0)
            print("%s %-24s %s %6d %s" % (bar, field, bar, count, bar if args.markdown else ""))
    else:
        print("(no _bfme2_extract/data/ini/object tree available)")

    print()
    print("== N: RETAIL WEAPON AUDIO CHAIN (Weapon -> FireFX -> FXList -> Sound) ==")
    if weapons.get("available"):
        print("Weapon blocks:                    %d" % weapons["weapons"])
        print("  ... declaring an FX list:       %d" % weapons["weapons_with_fx"])
        print("  ... whose FXList names a Sound: %d" % weapons["weapons_reaching_a_sound"])
        print("distinct weapon AudioEvents:      %d" % len(weapons["distinct_audio_events"]))
    else:
        print("(weapon.ini / fxlist.ini not available)")

    print()
    print("== M / K: INSTALLED PACKS ==")
    header = [
        "pack", "units", "any audio", "select", "move", "attack",
        "death", "SoundImpact", "bodyfall", "weapon sfx", "generic swing", "bound events",
    ]
    if args.markdown:
        print("| " + " | ".join(header) + " |")
        print("|" + "---|" * len(header))
    for row in sorted(packs, key=lambda item: item["pack"]):
        cells = [
            row["pack"], row["units"], row["with_any_binding"],
            row["kinds"]["select"], row["kinds"]["move"], row["kinds"]["attack"],
            row["kinds"]["death"], row["sound_impact"], row["bodyfall"],
            row["weapon_swing"], row["generic_swing_units"], row["bound_events"],
        ]
        if args.markdown:
            print("| " + " | ".join(str(cell) for cell in cells) + " |")
        else:
            print("%-52s %s" % (cells[0], " ".join("%6s" % cell for cell in cells[1:])))
    totals = Counter()
    for row in packs:
        totals["units"] += row["units"]
        totals["sound_impact"] += row["sound_impact"]
        totals["bodyfall"] += row["bodyfall"]
        totals["weapon_swing"] += row["weapon_swing"]
        totals["generic_swing_units"] += row["generic_swing_units"]
        for kind in RUNTIME_KIND_ALIASES:
            totals[kind] += row["kinds"][kind]
    print()
    print("TOTALS: units=%d select=%d move=%d attack=%d death=%d SoundImpact=%d bodyfall=%d weapon_sfx=%d generic_swing_units=%d"
          % (totals["units"], totals["select"], totals["move"], totals["attack"],
             totals["death"], totals["sound_impact"], totals["bodyfall"], totals["weapon_swing"], totals["generic_swing_units"]))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
