from __future__ import annotations

import importlib.util
import json
from pathlib import Path

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.map_profile import (
    CASTLE_SIEGE_MAPS,
    SKIRMISH_CATEGORY,
    discover_registry_map_targets,
)
from openbfme_importer.sage_map import MAX_SOURCE_BYTES, SageMapError, convert_sage_map
from importer.tests.test_sage_map import _synthetic_map


def _retail_catalog_path() -> Path | None:
    for parent in Path(__file__).resolve().parents:
        candidate = parent / ".private" / "retail-work" / "catalog" / "rotwk.json"
        if candidate.is_file():
            return candidate
    return None


RETAIL_CATALOG = _retail_catalog_path()


def test_multimap_registry_catalog_uses_the_same_finite_castle_admission() -> None:
    root = Path(__file__).resolve().parents[2]
    tool_path = root / "tools" / "rotwk_multimap_skirmish.py"
    spec = importlib.util.spec_from_file_location("castle_multimap_tool", tool_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    for virtual_path in CASTLE_SIEGE_MAPS:
        assert module._registry_category(virtual_path) == SKIRMISH_CATEGORY
    assert (
        module._registry_category("maps/map wor rivendell/map wor rivendell.map")
        == "wotr-battle"
    )


def test_castle_document_refuses_an_incomplete_gap_inventory(tmp_path: Path) -> None:
    source, _ = _synthetic_map()
    source_path = tmp_path / "castle.map"
    source_path.write_bytes(source)

    with pytest.raises(SageMapError, match="castleSiege contract is invalid"):
        convert_sage_map(
            source_path,
            tmp_path / "output",
            metadata={
                "castleSiege": {
                    "family": "retail-castle-siege-skirmish",
                    "gameplayStatus": "blocked-named-gaps",
                    "blockers": ["walkable-walls"],
                    "admissionPolicy": (
                        "document-loadable-lobby-visible-gameplay-fails-closed"
                    ),
                }
            },
        )


@pytest.mark.skipif(RETAIL_CATALOG is None, reason="RotWK retail oracle is not present")
def test_real_rotwk_castle_family_is_admitted_to_skirmish_without_rejections() -> None:
    catalog = InstallCatalog.load(RETAIL_CATALOG)
    targets, rejections = discover_registry_map_targets(
        catalog, categories=(SKIRMISH_CATEGORY,)
    )
    by_path = {target.virtual_path.casefold(): target for target in targets}

    assert not [
        row
        for row in rejections
        if str(row.get("virtualPath", "")).casefold() in CASTLE_SIEGE_MAPS
    ]
    assert set(CASTLE_SIEGE_MAPS) <= set(by_path)
    for virtual_path, evidence in CASTLE_SIEGE_MAPS.items():
        target = by_path[virtual_path]
        assert target.category == SKIRMISH_CATEGORY
        assert target.registry_player_count == evidence["playerCount"]


@pytest.mark.skipif(RETAIL_CATALOG is None, reason="RotWK retail oracle is not present")
def test_real_erebor_compiles_a_castle_document_with_wall_gate_and_garrison(
    tmp_path: Path,
) -> None:
    catalog = InstallCatalog.load(RETAIL_CATALOG)
    virtual_path = "maps/map wor erebor/map wor erebor.map"
    entry = catalog.resolve_exact(virtual_path)
    assert entry is not None
    archive = catalog.open_archive_for(entry)
    source = archive.read_entry(catalog.as_entry(entry), max_bytes=MAX_SOURCE_BYTES)
    source_path = tmp_path / "erebor.map"
    source_path.write_bytes(source)
    output = tmp_path / "erebor"
    evidence = CASTLE_SIEGE_MAPS[virtual_path]

    convert_sage_map(
        source_path,
        output,
        metadata={
            "id": "rotwk.map.wor-erebor",
            "displayName": "Erebor",
            "terrainMaterials": "assets/terrain/skirmish-maps/terrain-materials.json",
            "castleSiege": evidence["runtimeContract"],
        },
    )

    map_document = json.loads((output / "map.json").read_text(encoding="utf-8"))
    objects_document = json.loads(
        (output / "objects.json").read_text(encoding="utf-8")
    )
    type_names = {str(row["typeName"]) for row in objects_document["objects"]}
    assert map_document["playerCount"] == 2
    assert map_document["castleSiege"]["gameplayStatus"] == "blocked-named-gaps"
    assert "EreborWall01" in type_names
    assert "EreborMainGate" in type_names
    assert "EBGarrisonableTower" in type_names
