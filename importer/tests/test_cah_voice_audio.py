"""Create-a-Hero class voices must be cooked into the HOST faction registry.

A created hero references its subclass voice events by name from the
``cah.system`` table (``HeroWestMaleVoiceAttack``...). Before this lane the
faction audio registry only carried events a CONVERTED unit referenced, so
every custom hero was mute (``unvoiced_created_hero`` in the owner's v0.2.8
run.log). Live-catalog test: fails loudly when the catalog is absent.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = ROOT / "workspace" / "retail-work"


def _live_men_extension(include_census_registry: bool):
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.faction_profile import build_faction_audio_extension

    catalog_path = PRIVATE_ROOT / "catalog" / "rotwk.json"
    census_path = PRIVATE_ROOT / "editions/rotwk/reports/men-faction-leaf-census.json"
    if not catalog_path.is_file():
        pytest.fail(f"RotWK catalog is not present: {catalog_path}")
    if not census_path.is_file():
        pytest.fail(f"RotWK Men leaf census is not present: {census_path}")
    report = json.loads(census_path.read_text(encoding="utf-8"))
    return build_faction_audio_extension(
        InstallCatalog.load(catalog_path),
        report,
        "Men",
        include_census_registry=include_census_registry,
    )


def _cah_voice_roots() -> set[str]:
    from openbfme_importer.cah_system_compiler import AUDIO_PATH, _cah_voice_bindings
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.faction_profile import _read_document

    catalog = InstallCatalog.load(PRIVATE_ROOT / "catalog" / "rotwk.json")
    source = _read_document(catalog, AUDIO_PATH).source
    roots: set[str] = set()
    for fields in _cah_voice_bindings({AUDIO_PATH: source}).values():
        for event_ids in fields.values():
            roots.update(event_ids)
    return roots


def test_host_registry_carries_every_cah_subclass_voice_event() -> None:
    extension = _live_men_extension(include_census_registry=True)
    manifest = extension["runtime_data"]["data/audio_events.json"]
    roots = _cah_voice_roots()
    assert len(roots) >= 40, "createaheroaudio.inc authors dozens of voice events"
    defined = {key.casefold() for key in manifest["events"]} | {
        key.casefold() for key in manifest["multisounds"]
    }
    root_ids = {key.casefold() for key in manifest["rootIds"]}
    missing_definitions = sorted(r for r in roots if r.casefold() not in defined)
    missing_roots = sorted(r for r in roots if r.casefold() not in root_ids)
    assert missing_definitions == [], missing_definitions
    assert missing_roots == [], missing_roots
    # Every cooked CAH event resolves to at least one sample the pack ships.
    sample_keys = {key.casefold() for key in manifest["samples"]}
    for event_id in roots:
        row = manifest["events"].get(event_id) or manifest["multisounds"].get(event_id)
        assert row is not None, event_id
    diagnostics = extension["cahVoiceDiagnostics"]
    assert diagnostics["rootCount"] == len(roots)
    assert diagnostics["droppedDefinitions"] == [], diagnostics
    assert sample_keys, "registry ships samples"


def test_cah_voice_samples_are_converter_resources() -> None:
    extension = _live_men_extension(include_census_registry=True)
    cah_rows = [row for row in extension["resources"] if "-cah-audio-leaves-" in row["id"]]
    assert cah_rows, "CAH voice samples must be cooked by a converter row"
    for row in cah_rows:
        assert row["converter"] == "audio"
        assert row["required"] is True
        assert row["expected_count"] == len(row["patterns"])


def test_overlay_packs_carry_the_cah_voices_too() -> None:
    # The RotWK selection mounts no host registry for its factions; the EVA
    # overlays are the registry surface, so they must carry the class voices.
    extension = _live_men_extension(include_census_registry=False)
    manifest = extension["runtime_data"]["data/audio_events.json"]
    defined = {key.casefold() for key in manifest["events"]} | {
        key.casefold() for key in manifest["multisounds"]
    }
    roots = _cah_voice_roots()
    missing = sorted(r for r in roots if r.casefold() not in defined)
    assert missing == [], missing
    assert [row for row in extension["resources"] if "-cah-audio-leaves-" in row["id"]]
