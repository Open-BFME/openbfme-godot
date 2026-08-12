"""Lane L2a oracle: castle map-object admission + fixtures from pure retail bytes.

Oracle: ``.private/retail-work/editions/rotwk/cache/effective-assets`` (PURE
RotWK 2.01) only, plus the retail map archives via
``.private/retail-work/catalog/rotwk.json``.  Every pinned number below is
authored retail data, read from the INI/map bytes — never derived from this
lane's own output:

- ``EreborGateDoors`` (``object/civilian/ereborbuildings.ini:6242-6461``):
  ActiveBody MaxHealth 20000.0, ArmorSet ``DefaultWallArmor`` (NOT the design
  doc's ``EreborGateArmour`` — that is ``EreborMainGate``'s set), three named
  BOX geometries Closed/OpenLeft/OpenRight, ``GateOpenAndCloseBehavior`` with
  OpenByDefault=Yes, ResetTimeInMilliseconds=5000, PercentOpenForPathing=50.
- ``EBGarrisonableTower`` (:5098-5329): StructureBody MaxHealth =
  MEN_DORMITORYEXPANSION_HEALTH = 1500 (``gamedata.ini:2124``), ArmorSet
  ``StructureArmor``, ``HordeGarrisonContain`` ContainMax=3.
- Erebor placements (map archive): one gate owned by
  ``Player_1/teamPlayer_1``, six towers owned by
  ``PlyrCivilian/teamPlyrCivilian``.  ``Erebortower2`` carries the GARRISON
  KindOf but no contain module (retail defect, replicated): it admits as
  ``structure``, never ``garrison``.
"""

from __future__ import annotations

import importlib.util
import json
from pathlib import Path

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.castle_fixtures import (
    build_map_fixtures,
    compile_map_object_descriptor,
    validate_map_fixtures,
)
from openbfme_importer.map_profile import (
    CASTLE_SIEGE_MAPS,
    SKIRMISH_CATEGORY,
    build_map_profile,
    discover_registry_map_targets,
)
from openbfme_importer.playable_unit_compiler import _numeric_defines, _object_index
from openbfme_importer.castle_capabilities import build_retail_object_index
from openbfme_importer.sage_map import (
    MAX_SOURCE_BYTES,
    convert_sage_map,
    parse_sage_map_bytes,
)


ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = ROOT / ".private" / "retail-work"
if (
    not (PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets").is_dir()
    and ROOT.parent.name == "worktrees"
):
    PRIVATE_ROOT = ROOT.parents[2] / ".private" / "retail-work"
EFFECTIVE_ASSETS = PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets"
CATALOG_PATH = PRIVATE_ROOT / "catalog" / "rotwk.json"

oracle_present = pytest.mark.skipif(
    not (EFFECTIVE_ASSETS / "data" / "ini" / "object").is_dir()
    or not CATALOG_PATH.is_file(),
    reason="pure RotWK effective-assets oracle or catalog is not present",
)

EREBOR = "maps/map wor erebor/map wor erebor.map"
AMON_SUL = "maps/map mp amon sul fortress/map mp amon sul fortress.map"

#: (operable gates, static gates, real garrisons) per castle map.  Static
#: gates are the KindOf-only props measured against the pure oracle:
#: ``MBMMGateC`` x2 (Minas Morgul) and ``AngmarWallPosternGateCarnDum`` x2
#: (Carn Dum) — their command sets and gate modules are commented out in the
#: INI.  Gate + static-gate sums equal the L1 matrix's gate column exactly.
EXPECTED_ROLE_COUNTS = {
    "maps/map wor minas tirith/map wor minas tirith.map": (1, 0, 0),
    "maps/map wor helms deep/map wor helms deep.map": (2, 0, 0),
    EREBOR: (1, 0, 6),
    "maps/map wor isengard/map wor isengard.map": (1, 0, 0),
    "maps/map wor black gate/map wor black gate.map": (0, 0, 0),
    "maps/map wor dol guldur/map wor dol guldur.map": (5, 0, 0),
    "maps/map wor grey havens/map wor grey havens.map": (0, 0, 9),
    "maps/map wor minas morgul/map wor minas morgul.map": (3, 2, 0),
    "maps/map wor ang carn dum/map wor ang carn dum.map": (3, 2, 0),
    "maps/map wor ang fornost/map wor ang fornost.map": (5, 0, 1),
}


def _corpus_documents() -> dict[str, bytes]:
    ini_root = EFFECTIVE_ASSETS / "data" / "ini"
    documents: dict[str, bytes] = {}
    for path in sorted(ini_root.rglob("*")):
        if path.suffix.casefold() not in {".ini", ".inc"} or not path.is_file():
            continue
        virtual = path.relative_to(EFFECTIVE_ASSETS).as_posix()
        documents[virtual] = path.read_bytes()
    return documents


def _parse_map(catalog: InstallCatalog, virtual_path: str):
    entry = catalog.resolve_exact(virtual_path)
    assert entry is not None, f"map missing from retail catalog: {virtual_path}"
    archive = catalog.open_archive_for(entry)
    source = archive.read_entry(catalog.as_entry(entry), max_bytes=MAX_SOURCE_BYTES)
    return parse_sage_map_bytes(source)


@pytest.fixture(scope="module")
def documents() -> dict[str, bytes]:
    return _corpus_documents()


@pytest.fixture(scope="module")
def raw(documents):
    return _object_index(documents)


@pytest.fixture(scope="module")
def index(documents):
    return build_retail_object_index(documents)


@pytest.fixture(scope="module")
def defines(documents):
    return _numeric_defines(documents)


@pytest.fixture(scope="module")
def catalog() -> InstallCatalog:
    return InstallCatalog.load(CATALOG_PATH)


@pytest.fixture(scope="module")
def fixtures_by_map(documents, raw, index, defines, catalog):
    return {
        virtual_path: build_map_fixtures(
            documents,
            _parse_map(catalog, virtual_path).objects,
            raw=raw,
            index=index,
            defines=defines,
            game="rotwk",
        )
        for virtual_path in EXPECTED_ROLE_COUNTS
    }


# --- admission descriptors pinned to authored INI ---------------------------


@oracle_present
def test_erebor_gate_doors_descriptor_matches_authored_ini(
    documents, raw, defines
) -> None:
    descriptor = compile_map_object_descriptor(
        "EreborGateDoors", documents, raw=raw, defines=defines, game="rotwk"
    )
    assert descriptor["schema"] == "openbfme.map-object-descriptor"
    assert {"STRUCTURE", "IMMOBILE", "SELECTABLE", "BLOCKING_GATE", "WALL_GATE"} <= set(
        descriptor["kindOf"]
    )
    assert descriptor["health"]["primary"]["maxHealth"]["value"] == 20000.0
    # The design doc's "EreborGateArmour" is wrong: that set belongs to
    # EreborMainGate. The doors author DefaultWallArmor.
    assert descriptor["armor"]["setId"] == "DefaultWallArmor"
    pieces = {piece.get("name"): piece for piece in descriptor["geometry"]["pieces"]}
    assert set(pieces) == {"Closed", "OpenLeft", "OpenRight"}
    assert pieces["Closed"]["majorRadius"]["value"] == 130.0
    assert pieces["Closed"]["minorRadius"]["value"] == 7.5
    assert pieces["Closed"]["height"]["value"] == 140
    assert pieces["OpenLeft"]["offset"] == {"x": -115.0, "y": 68.0, "z": 0.0}
    assert pieces["OpenRight"]["offset"] == {"x": 115.0, "y": 68.0, "z": 0.0}
    modules = {
        row["module"]: row
        for row in descriptor["moduleContracts"]
        if isinstance(row, dict)
    }
    gate = modules["GateOpenAndCloseBehavior"]["fields"]
    assert gate["ResetTimeInMilliseconds"]["authored"].strip() == "5000"
    assert gate["OpenByDefault"]["authored"].strip().casefold() == "yes"
    assert gate["PercentOpenForPathing"]["authored"].strip() == "50"
    assert "KeepObjectDie" in modules


@oracle_present
def test_eb_garrisonable_tower_descriptor_matches_authored_ini(
    documents, raw, defines
) -> None:
    descriptor = compile_map_object_descriptor(
        "EBGarrisonableTower", documents, raw=raw, defines=defines, game="rotwk"
    )
    # StructureBody MaxHealth = MEN_DORMITORYEXPANSION_HEALTH, resolved through
    # gamedata.ini: 1500 (damaged 1000, really damaged 500).
    primary = descriptor["health"]["primary"]
    assert primary["maxHealth"]["value"] == 1500
    assert primary["maxHealthDamaged"]["value"] == 1000
    assert primary["maxHealthReallyDamaged"]["value"] == 500
    assert descriptor["armor"]["setId"] == "StructureArmor"
    modules = {
        row["module"]: row
        for row in descriptor["moduleContracts"]
        if isinstance(row, dict)
    }
    contain = modules["HordeGarrisonContain"]["fields"]
    assert contain["ContainMax"]["authored"].strip() == "3"
    pieces = descriptor["geometry"]["pieces"]
    assert len(pieces) == 1
    assert pieces[0]["shape"] == "BOX"
    assert pieces[0]["majorRadius"]["value"] == 14.0
    assert pieces[0]["height"]["value"] == 105.0


# --- the Erebor DoD ----------------------------------------------------------


@oracle_present
def test_erebor_emits_one_gate_fixture_with_authored_contract(fixtures_by_map) -> None:
    document = fixtures_by_map[EREBOR]
    validate_map_fixtures(document)
    assert document["capabilities"] == [
        "walkable-walls",
        "defendable-gates",
        "wall-garrisons",
        "skirmish-ai-libraries",
    ]
    gates = [row for row in document["fixtures"] if row["role"] == "gate"]
    assert len(gates) == 1
    gate = gates[0]
    assert gate["typeName"] == "EreborGateDoors"
    assert gate["originalOwner"] == "Player_1/teamPlayer_1"
    assert gate["maxHealth"] == 20000.0
    assert gate["armor"] == "DefaultWallArmor"
    assert set(gate["gate"]["geometries"]) == {"Closed", "OpenLeft", "OpenRight"}
    assert gate["gate"]["openByDefault"] is True
    assert gate["gate"]["resetMilliseconds"] == 5000
    assert gate["gate"]["percentOpenForPathing"] == 50
    assert gate["deathRule"] == "keep-object"


@oracle_present
def test_erebor_emits_six_garrison_fixtures_with_contain_max_3(fixtures_by_map) -> None:
    document = fixtures_by_map[EREBOR]
    garrisons = [row for row in document["fixtures"] if row["role"] == "garrison"]
    assert len(garrisons) == 6
    for row in garrisons:
        assert row["typeName"] == "EBGarrisonableTower"
        assert row["garrison"]["containMax"] == 3
        assert row["originalOwner"] == "PlyrCivilian/teamPlyrCivilian"
        assert row["maxHealth"] == 1500
        assert row["armor"] == "StructureArmor"
    # The retail defect, replicated: Erebortower2 has the GARRISON KindOf but
    # no contain module, so it admits as a plain structure, never a garrison.
    towers2 = [
        row for row in document["fixtures"] if row["typeName"] == "Erebortower2"
    ]
    assert towers2, "Erebortower2 is a STRUCTURE placement and must be admitted"
    for row in towers2:
        assert row["role"] == "structure"
        assert "garrison" not in row
        assert "gate" not in row


# --- every castle map --------------------------------------------------------


@oracle_present
@pytest.mark.parametrize("virtual_path", sorted(EXPECTED_ROLE_COUNTS))
def test_every_castle_map_fixtures_validate_and_match_the_matrix(
    virtual_path, fixtures_by_map
) -> None:
    document = fixtures_by_map[virtual_path]
    validate_map_fixtures(document)
    gates, static_gates, garrisons = EXPECTED_ROLE_COUNTS[virtual_path]
    roles = [str(row["role"]) for row in document["fixtures"]]
    assert roles.count("gate") == gates
    assert roles.count("static-gate") == static_gates
    assert roles.count("garrison") == garrisons
    assert document["capabilities"] == list(
        CASTLE_SIEGE_MAPS[virtual_path]["runtimeContract"]["required"]
    )


# --- compiled objects.json agreement ----------------------------------------


@oracle_present
def test_compiled_erebor_fixtures_agree_with_parsed_fixtures(
    documents, raw, index, defines, catalog, tmp_path
) -> None:
    entry = catalog.resolve_exact(EREBOR)
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
            "castleSiege": CASTLE_SIEGE_MAPS[EREBOR]["runtimeContract"],
        },
    )
    objects_document = json.loads((output / "objects.json").read_text(encoding="utf-8"))
    compiled = build_map_fixtures(
        documents,
        objects_document["objects"],
        raw=raw,
        index=index,
        defines=defines,
        game="rotwk",
    )
    entry_catalog = InstallCatalog.load(CATALOG_PATH)
    parsed = build_map_fixtures(
        documents,
        _parse_map(entry_catalog, EREBOR).objects,
        raw=raw,
        index=index,
        defines=defines,
        game="rotwk",
    )
    assert compiled == parsed


# --- profile wiring ------------------------------------------------------------


@oracle_present
def test_registry_tool_emits_fixtures_options_for_castle_maps_only() -> None:
    root = Path(__file__).resolve().parents[2]
    tool_path = root / "tools" / "rotwk_multimap_skirmish.py"
    spec = importlib.util.spec_from_file_location("fixtures_multimap_tool", tool_path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    built: dict[str, dict[str, object]] = {}

    def stub_builder(virtual_path: str, parsed) -> dict[str, object]:
        document = {"schema": "openbfme.sage-map-fixtures", "stub": virtual_path}
        built[virtual_path] = document
        return document

    profile = module.build_registry_skirmish_catalog(
        InstallCatalog.load(CATALOG_PATH),
        game="rotwk",
        fixtures_builder=stub_builder,
    )
    resources = {
        str(resource["id"]): resource for resource in profile["resources"]
    }
    erebor = resources["map-wor-erebor-binary"]
    assert erebor["options"]["fixtures"] == built[EREBOR]
    assert set(built) == set(CASTLE_SIEGE_MAPS)
    amon_sul = resources["map-amon-sul-fortress-binary"]
    assert "fixtures" not in amon_sul["options"]


@oracle_present
def test_map_profile_wires_fixtures_for_castle_maps_only(catalog) -> None:
    targets, _ = discover_registry_map_targets(
        catalog, categories=(SKIRMISH_CATEGORY,)
    )
    wanted = {EREBOR, AMON_SUL}
    selected = tuple(
        target for target in targets if target.virtual_path in wanted
    )
    assert {target.virtual_path for target in selected} == wanted

    built: dict[str, dict[str, object]] = {}

    def stub_builder(target, parsed) -> dict[str, object]:
        document = {
            "schema": "openbfme.sage-map-fixtures",
            "stub": target.virtual_path,
        }
        built[target.virtual_path] = document
        return document

    profile = build_map_profile(
        catalog,
        selected,
        profile_id="fixtures-wiring-test",
        title="fixtures wiring test",
        pack_id="fixtures-wiring-test",
        pack_version="v0",
        terrain_output="assets/terrain/fixtures-wiring-test",
        fixtures_builder=stub_builder,
    )
    resources = {
        str(resource["id"]): resource for resource in profile["resources"]
    }
    erebor_slug = next(
        target.slug for target in selected if target.virtual_path == EREBOR
    )
    amon_sul_slug = next(
        target.slug for target in selected if target.virtual_path == AMON_SUL
    )
    erebor = resources[f"map-{erebor_slug}-binary"]
    assert erebor["options"]["fixtures"] == built[EREBOR]
    assert built == {EREBOR: built[EREBOR]}
    amon_sul = resources[f"map-{amon_sul_slug}-binary"]
    assert "fixtures" not in amon_sul["options"]
