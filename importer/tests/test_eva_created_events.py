"""Per-unit ``*Created`` EVA bindings: object -> announcer event, from retail bytes.

Retail keys hero/unit creation announcements per OBJECT, not through one
generic "HeroCreated" event: an Object block's ``VoiceCreated = EVA:<event>``
(or ``VoiceFullyCreated = EVA:<event>``) names the eva.ini event the announcer
fires when that object is created (e.g. mordorblackrider.ini:686-687 ->
``NazgulCreated``). The compiler projects that binding into the eva document's
``createdEvents`` section; these tests pin the projection against an
independent from-bytes census of the pure RotWK 2.01 oracle tree.
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

_OBJECT_HEADER = re.compile(r"^(?:Object|ChildObject|ObjectReskin)\s+(\S+)", re.IGNORECASE)
_EVA_VOICE = re.compile(r"^EVA:([A-Za-z0-9_+.-]+)", re.IGNORECASE)


def _oracle_created_bindings() -> dict[str, str]:
    """Independent census: every Object-level ``Voice(Created|FullyCreated) =
    EVA:<event>`` in the oracle object corpus.

    Deliberately NOT the compiler's parser: this scanner attributes every
    ``VoiceCreated``/``VoiceFullyCreated`` line to the most recent Object-family
    header, which is sufficient because retail authors these fields only at
    object level. ``VoiceCreated`` (the created-moment voice) wins over
    ``VoiceFullyCreated``; retail never authors two different EVA targets on
    one object (a conflict here is a census failure, not a tie-break).
    """

    if not OBJECT_INI_ROOT.is_dir():
        pytest.fail("pure RotWK 2.01 object INI oracle is not present")
    created: dict[str, str] = {}
    fully: dict[str, str] = {}
    for path in sorted(OBJECT_INI_ROOT.rglob("*.ini")):
        current: str | None = None
        for raw in path.read_bytes().splitlines():
            line = raw.split(b";", 1)[0].decode("latin-1").strip()
            if not line:
                continue
            header = _OBJECT_HEADER.match(line)
            if header is not None:
                current = header.group(1)
                continue
            if current is None or "=" not in line:
                continue
            key, _, value = line.partition("=")
            match = _EVA_VOICE.match(value.strip())
            if match is None:
                continue
            folded = key.strip().casefold()
            table = created if folded == "voicecreated" else fully if folded == "voicefullycreated" else None
            if table is None:
                continue
            event = match.group(1)
            if current in table and table[current].casefold() != event.casefold():
                pytest.fail(
                    f"oracle conflict: {current} authors both {table[current]} and {event}"
                )
            table[current] = event
    conflicts = {
        name for name in created.keys() & fully.keys() if created[name].casefold() != fully[name].casefold()
    }
    if conflicts:
        pytest.fail(f"oracle VoiceCreated/VoiceFullyCreated EVA conflict: {sorted(conflicts)}")
    return {name: created.get(name, fully.get(name, "")) for name in created.keys() | fully.keys()}


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
    assert len(oracle) == 142
    assert _compiled_bindings() == oracle


def test_named_unit_created_bindings_resolve_from_the_oracle() -> None:
    bindings = _compiled_bindings()
    assert bindings["MordorBlackRider"] == "NazgulCreated"
    assert bindings["EvilMenBlackRider"] == "NazgulCreated"
    assert bindings["MordorMountainTroll"] == "MountainTrollCreated"
    assert bindings["IsengardFighter"] == "UrukCreated"


def test_commented_out_witch_king_binding_stays_unmapped() -> None:
    # Retail's own WitchKing binding is commented out (witchking.ini:313-314);
    # the Witch-King gets no creation announcement in pure 2.01, so the map
    # must not invent one.
    bindings = _compiled_bindings()
    assert "MordorWitchKing" not in bindings
    assert "MordorWitchKing_Mounted" not in bindings


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
    assert len(created) == 142
    # The side map itself is unchanged: createdEvents is schema-additive.
    assert document["schema"] == "openbfme.eva-events"
    assert document["schemaVersion"] == 1
