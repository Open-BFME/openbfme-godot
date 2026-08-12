"""Per-unit ``*Created`` EVA bindings: object -> announcer event, from retail bytes.

Retail keys hero/unit creation announcements per OBJECT, not through one
generic "HeroCreated" event. Two authored hooks feed the same map:

- ``VoiceCreated = EVA:<event>`` / ``VoiceFullyCreated = EVA:<event>``
  (mordorblackrider.ini:686-687 -> ``NazgulCreated``). SAGE ``ChildObject``
  inherits the parent's fields when it does not override them
  (``ChildObject MordorSauron_RingHero MordorSauron`` keeps
  ``VoiceCreated = EVA:SauronCreated`` from sauron.ini:306-309 / 519-539).
- Spawn-FX ``EvaEventOwner`` on the object's ``InitialSpawnFX`` list, which
  is the create hook for fortress heroes whose ``VoiceCreated`` line is
  commented out as "rehooked to spawn FX" (lurtz.ini:670-671 +
  fxlist.ini:12021-12027 ``EvaEventOwner = LurtzCreated``).

The compiler projects both into ``createdEvents``; these tests pin that
projection against an independent from-bytes census of the pure RotWK 2.01
oracle tree.
"""

from __future__ import annotations

import re
from pathlib import Path

import pytest


ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = ROOT / ".private" / "retail-work"
if not (PRIVATE_ROOT / "editions/rotwk/cache/effective-assets").is_dir() and ROOT.parent.name == "worktrees":
    PRIVATE_ROOT = ROOT.parents[2] / ".private" / "retail-work"
OBJECT_INI_ROOT = PRIVATE_ROOT / "editions/rotwk/cache/effective-assets/data/ini/object"
EVA_INI = PRIVATE_ROOT / "editions/rotwk/cache/effective-assets/data/ini/eva.ini"
FXLIST_INI = PRIVATE_ROOT / "editions/rotwk/cache/effective-assets/data/ini/fxlist.ini"

_OBJECT_HEADER = re.compile(
    r"^(Object|ChildObject|ObjectReskin)\s+(\S+)(?:\s+(\S+))?\s*$", re.IGNORECASE
)
_EVA_VOICE = re.compile(r"^EVA:([A-Za-z0-9_+.-]+)", re.IGNORECASE)
_EVA_BLOCK = re.compile(r"^(?:NewEvaEvent|PredefinedEvaEvent)\s+([A-Za-z0-9_+.-]+)\s*$", re.IGNORECASE)
_FX_HEADER = re.compile(r"^FXList\s+([A-Za-z0-9_+.-]+)\s*$", re.IGNORECASE)


def _oracle_eva_events() -> dict[str, str]:
    if not EVA_INI.is_file():
        pytest.fail("pure RotWK 2.01 eva.ini oracle is not present")
    authored: dict[str, str] = {}
    for raw in EVA_INI.read_bytes().splitlines():
        line = raw.split(b";", 1)[0].decode("latin-1").strip()
        header = _EVA_BLOCK.match(line)
        if header is not None:
            authored[header.group(1).casefold()] = header.group(1)
    return authored


def _oracle_fx_eva_owners() -> dict[str, list[str]]:
    """Independent FXList -> EvaEvent.EvaEventOwner census (not the compiler)."""

    if not FXLIST_INI.is_file():
        pytest.fail("pure RotWK 2.01 fxlist.ini oracle is not present")
    owners: dict[str, list[str]] = {}
    current: str | None = None
    stack: list[str] = []
    for raw in FXLIST_INI.read_bytes().splitlines():
        line = raw.split(b";", 1)[0].decode("latin-1").strip()
        if not line:
            continue
        header = _FX_HEADER.match(line)
        if header is not None and current is None:
            current = header.group(1)
            stack = []
            owners.setdefault(current.casefold(), [])
            continue
        if current is None:
            continue
        if line.casefold() == "end":
            if stack:
                stack.pop()
            else:
                current = None
            continue
        if "=" not in line:
            stack.append(line.split()[0].casefold())
            continue
        key, _, value = line.partition("=")
        if key.strip().casefold() == "evaeventowner" and stack and stack[-1] == "evaevent":
            token = value.strip()
            if token:
                owners[current.casefold()].append(token)
    return owners


def _eva_from_values(values: list[str]) -> str | None:
    event: str | None = None
    for value in values:
        match = _EVA_VOICE.match(value.strip())
        if match is None:
            continue
        candidate = match.group(1)
        if event is not None and event.casefold() != candidate.casefold():
            pytest.fail(f"oracle EVA conflict: {event} vs {candidate}")
        event = candidate
    return event


def _oracle_created_bindings() -> dict[str, str]:
    """Independent census of both retail create hooks, with ChildObject inherit.

    Deliberately NOT the compiler's parser. A line scanner attributes each
    field to the most recent Object-family header, then walks ``ChildObject``
    / ``ObjectReskin`` / resolvable ``Object`` parents so a child that does
    not override ``VoiceCreated`` keeps the parent's EVA binding. Spawn-FX
    ``EvaEventOwner`` on ``InitialSpawnFX`` fills objects whose VoiceCreated
    line was commented out and rehooked. ``RespawnAsTemplate`` is a last
    resort: the foot Witch-King authors no spawn FX of its own and converts
    into ``MordorWitchKingOnFellBeast``, which does.
    """

    if not OBJECT_INI_ROOT.is_dir():
        pytest.fail("pure RotWK 2.01 object INI oracle is not present")
    records: dict[str, dict[str, object]] = {}
    for path in sorted(OBJECT_INI_ROOT.rglob("*.ini")):
        current: dict[str, object] | None = None
        for raw in path.read_bytes().splitlines():
            line = raw.split(b";", 1)[0].decode("latin-1").strip()
            if not line:
                continue
            header = _OBJECT_HEADER.match(line)
            if header is not None:
                kind, name, parent = header.group(1), header.group(2), header.group(3)
                current = {
                    "kind": kind,
                    "name": name,
                    "parent": parent,
                    "voice_created": [],
                    "voice_fully": [],
                    "initial_spawn": [],
                    "respawn_as": [],
                }
                records[name.casefold()] = current
                continue
            if current is None or "=" not in line:
                continue
            key, _, value = line.partition("=")
            folded = key.strip().casefold()
            token = value.strip()
            if not token:
                continue
            if folded == "voicecreated":
                current["voice_created"].append(token)  # type: ignore[attr-defined]
            elif folded == "voicefullycreated":
                current["voice_fully"].append(token)  # type: ignore[attr-defined]
            elif folded == "initialspawnfx":
                current["initial_spawn"].append(token)  # type: ignore[attr-defined]
            elif folded == "respawnastemplate":
                current["respawn_as"].append(token)  # type: ignore[attr-defined]

    def _ancestry(start: dict[str, object]) -> list[dict[str, object]]:
        chain = [start]
        seen = {str(start["name"]).casefold()}
        current = start
        while current.get("parent"):
            parent_name = str(current["parent"])
            parent = records.get(parent_name.casefold())
            if parent is None:
                if str(current["kind"]).casefold() == "object":
                    break
                pytest.fail(
                    f"oracle ChildObject {current['name']} has unresolved parent {parent_name}"
                )
            key = str(parent["name"]).casefold()
            if key in seen:
                pytest.fail(f"oracle inheritance cycle at {start['name']}")
            seen.add(key)
            chain.append(parent)
            current = parent
        return chain

    def _effective(start: dict[str, object], field: str) -> list[str]:
        selected: list[str] = []
        for item in reversed(_ancestry(start)):
            values = list(item[field])  # type: ignore[arg-type]
            if values:
                selected = values
        return selected

    authored = _oracle_eva_events()
    fx_owners = _oracle_fx_eva_owners()
    bindings: dict[str, str] = {}
    for rec in records.values():
        voice = _eva_from_values(_effective(rec, "voice_created")) or _eva_from_values(
            _effective(rec, "voice_fully")
        )
        if voice is not None:
            bindings[str(rec["name"])] = voice
            continue
        spawn = _effective(rec, "initial_spawn")
        if not spawn:
            continue
        owners = fx_owners.get(spawn[-1].casefold(), [])
        chosen: str | None = None
        for owner in owners:
            authored_name = authored.get(owner.casefold())
            if authored_name is None:
                continue
            if chosen is not None and chosen.casefold() != authored_name.casefold():
                pytest.fail(
                    f"oracle spawn-FX EVA conflict on {rec['name']}: {chosen} vs {authored_name}"
                )
            chosen = authored_name
        if chosen is not None:
            bindings[str(rec["name"])] = chosen

    for rec in records.values():
        name = str(rec["name"])
        if name in bindings:
            continue
        templates = _effective(rec, "respawn_as")
        if not templates:
            continue
        template = templates[-1]
        event = bindings.get(template)
        if event is None:
            folded = template.casefold()
            event = next(
                (value for key, value in bindings.items() if key.casefold() == folded),
                None,
            )
        if event is not None:
            bindings[name] = event
    return bindings


def _compiled_bindings() -> dict[str, str]:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.faction_profile import _eva_created_event_bindings

    catalog_path = PRIVATE_ROOT / "catalog" / "rotwk.json"
    if not catalog_path.is_file():
        pytest.fail(f"RotWK catalog is not present: {catalog_path}")
    return _eva_created_event_bindings(InstallCatalog.load(catalog_path))


def test_created_event_bindings_match_the_from_bytes_census() -> None:
    oracle = _oracle_created_bindings()
    # The census pins itself, so a retail-byte change cannot move both sides.
    # 142 own VoiceCreated EVA: rows + ChildObject inherit + spawn-FX EvaEventOwner
    # + RespawnAsTemplate follow (foot Witch-King -> fellbeast spawn FX).
    assert len(oracle) == 237
    assert _compiled_bindings() == oracle


def test_named_unit_created_bindings_resolve_from_the_oracle() -> None:
    bindings = _compiled_bindings()
    assert bindings["MordorBlackRider"] == "NazgulCreated"
    assert bindings["EvilMenBlackRider"] == "NazgulCreated"
    assert bindings["MordorMountainTroll"] == "MountainTrollCreated"
    assert bindings["IsengardFighter"] == "UrukCreated"


def test_child_object_inherits_parent_voice_created() -> None:
    # sauron.ini:306-309 authors VoiceCreated = EVA:SauronCreated on MordorSauron.
    # ChildObject MordorSauron_RingHero MordorSauron (sauron.ini:519-539) does
    # not override it; production emits objectId = MordorSauron_RingHero.
    bindings = _compiled_bindings()
    assert bindings["MordorSauron"] == "SauronCreated"
    assert bindings["MordorSauron_RingHero"] == "SauronCreated"


def test_spawn_fx_eva_event_owner_maps_fortress_heroes() -> None:
    # VoiceCreated is commented out as "rehooked to spawn FX"; the real create
    # hook is InitialSpawnFX -> EvaEvent.EvaEventOwner (fxlist.ini).
    bindings = _compiled_bindings()
    assert bindings["IsengardLurtz"] == "LurtzCreated"
    assert bindings["IsengardSaruman"] == "SarumanCreated"
    assert bindings["IsengardSharku"] == "SharkuCreated"
    assert bindings["IsengardWormTongue"] == "WormtongueCreated"
    assert bindings["MordorMouthOfSauron"] == "MouthofSauronCreated"
    assert bindings["WildGoblinKing"] == "GoblinKingCreated"
    assert bindings["MordorWitchKingOnFellBeast"] == "WitchKingCreated"
    # Foot Witch-King authors no InitialSpawnFX; RespawnAsTemplate converts it
    # into the fellbeast object that plays FX_WitchKingInitialSpawn.
    assert bindings["MordorWitchKing"] == "WitchKingCreated"


def test_every_bound_event_is_an_authored_eva_block() -> None:
    from openbfme_importer.faction_profile import _eva_event_side_sounds

    if not EVA_INI.is_file():
        pytest.fail("pure RotWK 2.01 eva.ini oracle is not present")
    authored = {
        names[0].casefold()
        for names in _eva_event_side_sounds(EVA_INI.read_bytes()).values()
    }
    unknown = {
        event
        for event in _compiled_bindings().values()
        if event.casefold() not in authored
    }
    assert unknown == set()


def test_composed_overlay_document_carries_the_created_events_section() -> None:
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.faction_profile import build_faction_audio_extension

    catalog_path = PRIVATE_ROOT / "catalog" / "rotwk.json"
    census_path = PRIVATE_ROOT / "editions/rotwk/reports/men-faction-leaf-census.json"
    if not catalog_path.is_file():
        pytest.fail(f"RotWK catalog is not present: {catalog_path}")
    if not census_path.is_file():
        pytest.fail(f"RotWK Men leaf census is not present: {census_path}")
    import json

    report = json.loads(census_path.read_text(encoding="utf-8"))
    extension = build_faction_audio_extension(
        InstallCatalog.load(catalog_path),
        report,
        "Men",
        include_census_registry=False,
    )
    document = extension["runtime_data"]["data/eva_events.json"]
    created = document["createdEvents"]
    assert created["MordorBlackRider"] == "NazgulCreated"
    assert created["MordorSauron_RingHero"] == "SauronCreated"
    assert created["IsengardLurtz"] == "LurtzCreated"
    assert created["MordorWitchKingOnFellBeast"] == "WitchKingCreated"
    assert len(created) == 237
    # The side map itself is unchanged: createdEvents is schema-additive.
    assert document["schema"] == "openbfme.eva-events"
    assert document["schemaVersion"] == 1
