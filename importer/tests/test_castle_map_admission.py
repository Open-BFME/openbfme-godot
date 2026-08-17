from __future__ import annotations

import importlib.util
import json
from pathlib import Path

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.castle_capabilities import (
    CAPABILITY_VOCABULARY,
    castle_siege_contract_v2,
)
from openbfme_importer.map_profile import (
    CASTLE_SIEGE_MAPS,
    SKIRMISH_CATEGORY,
    discover_registry_map_targets,
)
from openbfme_importer.sage_map import MAX_SOURCE_BYTES, SageMapError, convert_sage_map
from importer.tests.test_sage_map import _synthetic_map


def _retail_catalog_path() -> Path | None:
    for parent in Path(__file__).resolve().parents:
        candidate = parent / "workspace" / "retail-work" / "catalog" / "rotwk.json"
        if candidate.is_file():
            return candidate
    return None


RETAIL_CATALOG = _retail_catalog_path()
AMON_SUL_VIRTUAL_PATH = (
    "maps/map mp amon sul fortress/map mp amon sul fortress.map"
)
EXPECTED_CASTLE_BLOCKERS = [
    "walkable-walls",
    "defendable-gates",
    "wall-garrisons",
    "wall-mounted-defenses",
    "skirmish-ai-libraries",
]


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


def test_amon_sul_remains_an_ordinary_skirmish_map() -> None:
    assert AMON_SUL_VIRTUAL_PATH not in CASTLE_SIEGE_MAPS


def test_castle_contract_names_the_missing_skirmish_ai_libraries() -> None:
    for evidence in CASTLE_SIEGE_MAPS.values():
        contract = evidence["runtimeContract"]
        assert contract["version"] == 2
        assert contract["family"] == "retail-castle-siege-skirmish"
        assert "skirmish-ai-libraries" in contract["required"]
        # Canonical vocabulary order, no authored blocker list: the runtime
        # computes blockers = required - implemented at load time.
        ranks = [CAPABILITY_VOCABULARY.index(name) for name in contract["required"]]
        assert ranks == sorted(ranks)
        assert "blockers" not in contract


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


def test_castle_document_accepts_a_v2_capability_contract(tmp_path: Path) -> None:
    source, _ = _synthetic_map()
    source_path = tmp_path / "castle.map"
    source_path.write_bytes(source)
    contract = castle_siege_contract_v2(["defendable-gates", "wall-garrisons"])

    convert_sage_map(
        source_path,
        tmp_path / "output",
        metadata={"castleSiege": contract},
    )

    document = json.loads(
        (tmp_path / "output" / "map.json").read_text(encoding="utf-8")
    )
    assert document["castleSiege"] == contract


def test_castle_document_still_accepts_the_legacy_v1_contract(tmp_path: Path) -> None:
    source, _ = _synthetic_map()
    source_path = tmp_path / "castle.map"
    source_path.write_bytes(source)
    contract = {
        "family": "retail-castle-siege-skirmish",
        "gameplayStatus": "blocked-named-gaps",
        "blockers": list(EXPECTED_CASTLE_BLOCKERS),
        "admissionPolicy": "document-loadable-lobby-visible-gameplay-fails-closed",
    }

    convert_sage_map(
        source_path,
        tmp_path / "output",
        metadata={"castleSiege": contract},
    )

    document = json.loads(
        (tmp_path / "output" / "map.json").read_text(encoding="utf-8")
    )
    assert document["castleSiege"] == contract


@pytest.mark.parametrize(
    "contract",
    (
        # Unknown capability name.
        castle_siege_contract_v2(["walkable-walls"])
        | {"required": ["walkable-walls", "moonbeams"]},
        # Non-canonical order.
        castle_siege_contract_v2(["defendable-gates"])
        | {"required": ["defendable-gates", "walkable-walls"]},
        # Empty requirement set contradicts the blocked status.
        castle_siege_contract_v2(["walkable-walls"]) | {"required": []},
        # An authored blocker list is the v1 hardcode the v2 schema removes.
        castle_siege_contract_v2(["walkable-walls"])
        | {"blockers": ["walkable-walls"]},
        # Wrong version number.
        castle_siege_contract_v2(["walkable-walls"]) | {"version": 3},
    ),
)
def test_castle_document_refuses_malformed_v2_contracts(
    tmp_path: Path, contract: dict
) -> None:
    source, _ = _synthetic_map()
    source_path = tmp_path / "castle.map"
    source_path.write_bytes(source)

    with pytest.raises(SageMapError, match="castleSiege contract is invalid"):
        convert_sage_map(
            source_path,
            tmp_path / "output",
            metadata={"castleSiege": contract},
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


@pytest.mark.skipif(RETAIL_CATALOG is None, reason="RotWK retail oracle is not present")
def test_real_multimap_tool_emits_castle_contract_into_resource_metadata(
    tmp_path: Path,
) -> None:
    root = Path(__file__).resolve().parents[2]
    tool_path = root / "tools" / "rotwk_multimap_skirmish.py"
    spec = importlib.util.spec_from_file_location("castle_multimap_tool_emission", tool_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    profile = module.build_registry_skirmish_catalog(
        InstallCatalog.load(RETAIL_CATALOG), game="rotwk"
    )
    erebor_resource = next(
        resource
        for resource in profile["resources"]
        if resource["id"] == "map-wor-erebor-binary"
    )
    assert set(erebor_resource["options"]) == {"metadata", "profile"}
    assert erebor_resource["output"] == "maps/wor-erebor"
    assert erebor_resource["options"]["metadata"]["id"] == "rotwk.map.wor-erebor"
    assert erebor_resource["options"]["metadata"]["displayName"] == "Erebor"
    assert erebor_resource["options"]["metadata"]["castleSiege"] == (
        CASTLE_SIEGE_MAPS["maps/map wor erebor/map wor erebor.map"]["runtimeContract"]
    )

    # Exercise the tool-produced metadata through the actual converter. This is
    # the second seal: the catalog row validator is not enough by itself.
    catalog = InstallCatalog.load(RETAIL_CATALOG)
    entry = catalog.resolve_exact("maps/map wor erebor/map wor erebor.map")
    assert entry is not None
    archive = catalog.open_archive_for(entry)
    source_path = tmp_path / "tool-erebor.map"
    source_path.write_bytes(
        archive.read_entry(catalog.as_entry(entry), max_bytes=MAX_SOURCE_BYTES)
    )
    output = tmp_path / "tool-erebor"
    convert_sage_map(
        source_path,
        output,
        metadata=erebor_resource["options"]["metadata"],
        profile=erebor_resource["options"]["profile"],
    )
    document = json.loads((output / "map.json").read_text(encoding="utf-8"))
    assert document["castleSiege"] == erebor_resource["options"]["metadata"][
        "castleSiege"
    ]
