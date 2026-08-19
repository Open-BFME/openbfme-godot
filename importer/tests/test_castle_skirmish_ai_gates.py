from __future__ import annotations

import json
from pathlib import Path

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer import map_profile
from openbfme_importer.map_profile import MapTarget, build_map_profile
from openbfme_importer.profile import ImportProfile, is_canonical_multiplayer_map_virtual_path
from openbfme_importer.sage_map import parse_sage_map_bytes

from importer.tests.test_big import make_big
from importer.tests.test_sage_map import _synthetic_map


CASTLE_MAP_PATH = "maps/map wor erebor/map wor erebor.map"
NON_CASTLE_WOTR_MAP_PATH = "maps/map wor rivendell/map wor rivendell.map"
EXPECTED_AI_LIBRARIES = [
    "libraries/multiplayer_start_teams/multiplayer_start_teams.map",
    "libraries/ai_initialize/ai_initialize.map",
    "libraries/ai_mp_inherit_management/ai_mp_inherit_management.map",
    "libraries/multiplayer_human/multiplayer_human.map",
]


def _profile_for(tmp_path: Path, target: MapTarget) -> dict:
    source, _ = _synthetic_map()
    entries = {
        "data/ini/terrain.ini": b"Terrain TestGrass\n Texture = testgrass.tga\nEnd\n",
        "art/terrain/testgrass.tga": b"synthetic-tga",
        target.virtual_path: source,
        **{path: source for path in EXPECTED_AI_LIBRARIES},
    }
    make_big(tmp_path / "maps.big", entries)
    return build_map_profile(
        InstallCatalog.build(tmp_path),
        (target,),
        profile_id="castle-skirmish-ai-gate-test",
        title="Castle skirmish AI gate test",
        pack_id="castle-skirmish-ai-gate-test",
        pack_version="test",
        terrain_output="assets/terrain/castle-skirmish-ai-gate-test",
    )


def test_castle_map_scripts_composition(tmp_path: Path) -> None:
    profile = _profile_for(
        tmp_path,
        MapTarget(
            "wor-erebor",
            "Erebor",
            CASTLE_MAP_PATH,
            registry_player_count=1,
            category=map_profile.SKIRMISH_CATEGORY,
        ),
    )

    resource = next(
        row
        for row in profile["resources"]
        if row["id"] == "map-wor-erebor-scripts"
    )
    assert resource["options"]["libraryVirtualPaths"] == EXPECTED_AI_LIBRARIES
    assert resource["patterns"] == [CASTLE_MAP_PATH, *EXPECTED_AI_LIBRARIES]
    assert resource["limit"] == 5
    assert resource["expected_count"] == 5
    profile_path = tmp_path / "profile.json"
    profile_path.write_text(json.dumps(profile), encoding="utf-8")
    # Exercises the separate profile-side library allowlist and castle source
    # path/output coupling, not only the generator's emitted dictionary.
    ImportProfile.load(profile_path)


def test_non_castle_wotr_map_excluded(tmp_path: Path) -> None:
    # Retail does ship non-castle map-wor strategic maps. Rivendell is one of
    # them, so authored player starts alone must not bypass the category gate.
    profile = _profile_for(
        tmp_path,
        MapTarget(
            "wor-rivendell",
            "Rivendell",
            NON_CASTLE_WOTR_MAP_PATH,
            registry_player_count=1,
            category=map_profile.WOTR_BATTLE_CATEGORY,
        ),
    )

    assert not any(
        row["converter"] == "sage-script-composite"
        for row in profile["resources"]
    )


def test_content_gate_replaces_path_grammar() -> None:
    source, _ = _synthetic_map()
    parsed = parse_sage_map_bytes(source, profile="multiplayer")

    assert parsed.player_starts
    assert map_profile._map_has_player_starts(parsed)
    assert not is_canonical_multiplayer_map_virtual_path(CASTLE_MAP_PATH)
