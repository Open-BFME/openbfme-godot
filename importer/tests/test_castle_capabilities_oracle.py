"""Lane L1 oracle: the per-map castle capability matrix from pure retail bytes.

Oracle: ``workspace/retail-work/editions/rotwk/cache/effective-assets`` (PURE
RotWK 2.01), never ``layered-effective-assets`` and never the stale
``workspace/retail-work/cache/effective-assets`` tree.  Map object placements
come from the retail map archives via ``workspace/retail-work/catalog/rotwk.json``.

The pinned matrix is the brief's (docs/castle-siege-design.md section 1.2).
Two traps are pinned by name:

- Carn Dum = 5 gates / 39 wall-mounted, reachable only when the ``ChildObject``
  keyword is registered as a definition site
  (``evilfaction/structures/angmar/angmarcastlewalls.ini``).
- Erebor = 6 real garrisons, NOT 8: ``Erebortower2`` carries the ``GARRISON``
  KindOf but no contain module and must be excluded.
"""

from __future__ import annotations

import json
from pathlib import Path

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.castle_capabilities import (
    build_retail_object_index,
    castle_siege_contract_v2,
    derive_map_capabilities,
)
from openbfme_importer.map_profile import CASTLE_SIEGE_MAPS
from openbfme_importer.sage_map import (
    MAX_SOURCE_BYTES,
    convert_sage_map,
    parse_sage_map_bytes,
)


ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = ROOT / "workspace" / "retail-work"
if (
    not (PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets").is_dir()
    and ROOT.parent.name == "worktrees"
):
    PRIVATE_ROOT = ROOT.parents[2] / "workspace" / "retail-work"
EFFECTIVE_ASSETS = PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets"
CATALOG_PATH = PRIVATE_ROOT / "catalog" / "rotwk.json"

oracle_present = pytest.mark.skipif(
    not (EFFECTIVE_ASSETS / "data" / "ini" / "object").is_dir()
    or not CATALOG_PATH.is_file(),
    reason="pure RotWK effective-assets oracle or catalog is not present",
)

#: The pinned matrix from the lane brief.  Columns:
#: (gates, garrison-real, walk-on-top, scaleable, wall-mounted).
EXPECTED_MATRIX = {
    "maps/map wor minas tirith/map wor minas tirith.map": (1, 0, 99, 0, 14),
    "maps/map wor helms deep/map wor helms deep.map": (2, 0, 36, 0, 0),
    "maps/map wor erebor/map wor erebor.map": (1, 6, 5, 0, 0),
    "maps/map wor isengard/map wor isengard.map": (1, 0, 19, 0, 0),
    "maps/map wor black gate/map wor black gate.map": (0, 0, 6, 0, 0),
    "maps/map wor dol guldur/map wor dol guldur.map": (5, 0, 6, 251, 16),
    "maps/map wor grey havens/map wor grey havens.map": (0, 9, 0, 0, 0),
    "maps/map wor minas morgul/map wor minas morgul.map": (5, 0, 34, 0, 0),
    "maps/map wor ang carn dum/map wor ang carn dum.map": (5, 0, 0, 0, 39),
    "maps/map wor ang fornost/map wor ang fornost.map": (5, 1, 0, 0, 0),
}

#: Unresolved map-placed typeNames, pinned per map after measurement against
#: the pure oracle.  Every name is one of three explained kinds (see the lane
#: L1 report): ``*Waypoints/Waypoint`` is the WorldBuilder waypoint marker
#: cooked from the map's waypoint list, not an INI object; the road/sidewalk/
#: footprint/rug/scorch names are texture decal props the map places directly
#: with no ThingTemplate anywhere under ``data/ini/**``; all are inert scenery.
EXPECTED_UNRESOLVED: dict[str, tuple[str, ...]] = {
    "maps/map wor minas tirith/map wor minas tirith.map": (
        "*Waypoints/Waypoint", "DirtRoadTracks3", "FtprintsDrk02",
        "Sidewalk15", "Sidewalk8", "Sidewalk9", "WagonTraveled",
    ),
    "maps/map wor helms deep/map wor helms deep.map": (
        "*Waypoints/Waypoint", "DirtRoad2", "FtprintsDrk02",
        "MinasMorgulRoad04", "Sidewalk17",
    ),
    "maps/map wor erebor/map wor erebor.map": (
        "*Waypoints/Waypoint", "BlueMountainsRoad02", "DaleRoad01",
        "EreborRoad01", "EreborRoad02", "EreborRug01", "EreborRug02",
    ),
    "maps/map wor isengard/map wor isengard.map": (
        "*Waypoints/Waypoint", "FtprintsDrk", "GravelRoad2",
        "GravelRoad3", "GravelRoad4",
    ),
    "maps/map wor black gate/map wor black gate.map": (
        "*Waypoints/Waypoint",
    ),
    "maps/map wor dol guldur/map wor dol guldur.map": (
        "*Waypoints/Waypoint", "BlueMountainsRoad03",
        "MinasMorgulRoad01", "MinasMorgulRoad04",
    ),
    "maps/map wor grey havens/map wor grey havens.map": (
        "*Waypoints/Waypoint", "Sidewalk8",
    ),
    "maps/map wor minas morgul/map wor minas morgul.map": (
        "*Waypoints/Waypoint", "MinasMorgulRoad01",
        "MinasMorgulRoad02", "MinasMorgulRoad03",
    ),
    "maps/map wor ang carn dum/map wor ang carn dum.map": (
        "*Waypoints/Waypoint", "DirtRoadTracks3", "FtPrintDrkGr01",
        "FtPrintDrkGr02", "Scorch",
    ),
    "maps/map wor ang fornost/map wor ang fornost.map": (
        "*Waypoints/Waypoint", "DaleRoad01", "DirtRoad12",
        "DirtRoadTracks3", "MinasMorgulRoad02", "Scorch",
        "Sidewalk10", "Sidewalk15", "Sidewalk17", "Sidewalk6",
    ),
}


def _object_corpus_documents() -> dict[str, bytes]:
    corpus = EFFECTIVE_ASSETS / "data" / "ini" / "object"
    documents: dict[str, bytes] = {}
    for path in sorted(corpus.rglob("*")):
        if path.suffix.casefold() not in {".ini", ".inc"} or not path.is_file():
            continue
        virtual = path.relative_to(EFFECTIVE_ASSETS).as_posix()
        documents[virtual] = path.read_bytes()
    return documents


def _map_type_names(catalog: InstallCatalog, virtual_path: str) -> list[str]:
    entry = catalog.resolve_exact(virtual_path)
    assert entry is not None, f"map missing from retail catalog: {virtual_path}"
    archive = catalog.open_archive_for(entry)
    source = archive.read_entry(catalog.as_entry(entry), max_bytes=MAX_SOURCE_BYTES)
    parsed = parse_sage_map_bytes(source)
    return [str(row["typeName"]) for row in parsed.objects]


@pytest.fixture(scope="module")
def object_index():
    return build_retail_object_index(_object_corpus_documents())


@pytest.fixture(scope="module")
def derivations(object_index):
    catalog = InstallCatalog.load(CATALOG_PATH)
    return {
        virtual_path: derive_map_capabilities(
            object_index, _map_type_names(catalog, virtual_path)
        )
        for virtual_path in EXPECTED_MATRIX
    }


@oracle_present
def test_index_covers_all_three_definition_keywords(object_index) -> None:
    definitions = [info.definition for info in object_index.values()]
    objects_and_children = sum(
        1 for kind in definitions if kind in {"Object", "ChildObject"}
    )
    child_objects = sum(1 for kind in definitions if kind == "ChildObject")
    reskins = sum(1 for kind in definitions if kind == "ObjectReskin")
    # Design lane census said ~3867 Object+ChildObject (569 ChildObject) and
    # the 759 ObjectReskin definitions its own index missed.  Measured against
    # the pure oracle: exactly 3305 Object + 569 ChildObject = 3874, and 759
    # ObjectReskin.  The "~3867" was an estimate; 3874 is the pinned truth.
    assert objects_and_children == 3874
    assert child_objects == 569
    assert reskins == 759


@oracle_present
def test_every_castle_map_is_covered_by_the_oracle_matrix() -> None:
    assert set(EXPECTED_MATRIX) == set(CASTLE_SIEGE_MAPS)


@oracle_present
@pytest.mark.parametrize("virtual_path", sorted(EXPECTED_MATRIX))
def test_capability_matrix_matches_retail(virtual_path, derivations) -> None:
    derivation = derivations[virtual_path]
    expected = EXPECTED_MATRIX[virtual_path]
    actual = (
        derivation.gates,
        derivation.garrisons,
        derivation.walk_on_top,
        derivation.scaleable,
        derivation.wall_mounted,
    )
    assert actual == expected, (
        f"{virtual_path}: (gates, garrison, walk-on-top, scaleable, "
        f"wall-mounted) {actual} != pinned {expected}; "
        f"unresolved={list(derivation.unresolved)}"
    )


@oracle_present
def test_carn_dum_childobject_trap_stays_pinned(derivations) -> None:
    derivation = derivations["maps/map wor ang carn dum/map wor ang carn dum.map"]
    assert derivation.gates == 5
    assert derivation.wall_mounted == 39


@oracle_present
def test_erebor_garrison_trap_stays_pinned(object_index, derivations) -> None:
    derivation = derivations["maps/map wor erebor/map wor erebor.map"]
    assert derivation.garrisons == 6
    fake = object_index.get("erebortower2")
    assert fake is not None
    assert "GARRISON" in fake.kind_of
    assert not any(
        behavior.casefold().endswith("contain") for behavior in fake.behaviors
    )


@oracle_present
@pytest.mark.parametrize("virtual_path", sorted(EXPECTED_MATRIX))
def test_unresolved_typenames_are_zero_or_explained(virtual_path, derivations) -> None:
    derivation = derivations[virtual_path]
    assert derivation.unresolved == EXPECTED_UNRESOLVED.get(virtual_path, ()), (
        f"{virtual_path}: unresolved typeNames {list(derivation.unresolved)}; "
        "pin them in EXPECTED_UNRESOLVED with a report explanation, or fix "
        "the index if they are real definitions"
    )


@oracle_present
@pytest.mark.parametrize("virtual_path", sorted(EXPECTED_MATRIX))
def test_map_profile_contract_is_the_v2_derivation(virtual_path, derivations) -> None:
    """The pinned cook-time contract must equal the derivation, computed.

    ``skirmish-ai-libraries`` is a family-level requirement (design 2.5:
    mapcache multiplayer registry rows and per-map ``AIBase`` bindings), so it
    is appended to every map's object-derived set; it sorts last in the
    canonical vocabulary order.
    """

    derivation = derivations[virtual_path]
    expected_required = tuple(derivation.required) + ("skirmish-ai-libraries",)
    assert CASTLE_SIEGE_MAPS[virtual_path]["runtimeContract"] == (
        castle_siege_contract_v2(expected_required)
    )


@oracle_present
def test_compiled_erebor_objects_json_agrees_with_parsed_placements(
    object_index, tmp_path
) -> None:
    """The parse-based matrix must agree with a real compiled objects.json."""

    catalog = InstallCatalog.load(CATALOG_PATH)
    virtual_path = "maps/map wor erebor/map wor erebor.map"
    entry = catalog.resolve_exact(virtual_path)
    assert entry is not None
    archive = catalog.open_archive_for(entry)
    source_path = tmp_path / "erebor.map"
    source_path.write_bytes(
        archive.read_entry(catalog.as_entry(entry), max_bytes=MAX_SOURCE_BYTES)
    )
    output = tmp_path / "erebor"
    convert_sage_map(
        source_path,
        output,
        metadata={
            "id": "rotwk.map.wor-erebor",
            "displayName": "Erebor",
            "castleSiege": CASTLE_SIEGE_MAPS[virtual_path]["runtimeContract"],
        },
    )
    objects_document = json.loads(
        (output / "objects.json").read_text(encoding="utf-8")
    )
    compiled = derive_map_capabilities(
        object_index,
        [str(row["typeName"]) for row in objects_document["objects"]],
    )
    parsed = derive_map_capabilities(
        object_index, _map_type_names(catalog, virtual_path)
    )
    assert compiled == parsed
    assert (
        compiled.gates,
        compiled.garrisons,
        compiled.walk_on_top,
        compiled.scaleable,
        compiled.wall_mounted,
    ) == EXPECTED_MATRIX[virtual_path]
