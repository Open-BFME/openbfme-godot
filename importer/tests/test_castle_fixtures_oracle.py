"""Lane L2a oracle: castle map-object admission + fixtures from pure retail bytes.

Oracle: ``workspace/retail-work/editions/rotwk/cache/effective-assets`` (PURE
RotWK 2.01) only, plus the retail map archives via
``workspace/retail-work/catalog/rotwk.json``.  Every pinned number below is
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
    _compile_lineage,
    _gate_block,
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

EREBOR = "maps/map wor erebor/map wor erebor.map"
GREY_HAVENS = "maps/map wor grey havens/map wor grey havens.map"
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
def test_erebor_and_helms_gate_blocks_pin_authored_runtime_fields(raw, defines) -> None:
    _, erebor_lineage = _compile_lineage("EreborGateDoors", raw)
    erebor = _gate_block(erebor_lineage, defines, "EreborGateDoors")
    assert erebor["commandSet"] == "CastleGateCommandSet"
    assert erebor["openByDefault"] is True
    assert erebor["resetMilliseconds"] == 5000
    assert erebor["percentOpenForPathing"] == 50
    assert set(erebor["geometries"]) == {"Closed", "OpenLeft", "OpenRight"}
    # ereborbuildings.ini:6406 authors no AI/portal modules.
    assert "aiGateUpdate" not in erebor
    assert "fakePathfindPortal" not in erebor

    _, helms_lineage = _compile_lineage("RBHelmsDeepGateDoorBig", raw)
    block = _gate_block(helms_lineage, defines, "RBHelmsDeepGateDoorBig")
    # Pure effective-assets helmsdeepbuildings.ini:6227,6274,6286-6293. The older
    # retail-extract mirror says 300x150; the effective RotWK oracle is 450x225.
    assert block["commandSet"] == "CastleGateCommandSet_NoSell"
    assert block["openByDefault"] is True
    assert block["resetMilliseconds"] == 12200
    assert block["percentOpenForPathing"] == 50
    assert set(block["geometries"]) == {"Closed", "OpenLeft", "OpenRight"}
    assert block["aiGateUpdate"] == {"triggerWidthX": 450.0, "triggerWidthY": 225.0}
    assert block["fakePathfindPortal"] == {
        "allowEnemies": False,
        "allowNonSkirmishAIUnits": False,
    }


@oracle_present
def test_erebor_emits_six_garrison_fixtures_with_contain_max_3(fixtures_by_map) -> None:
    document = fixtures_by_map[EREBOR]
    garrisons = [row for row in document["fixtures"] if row["role"] == "garrison"]
    assert len(garrisons) == 6
    for row in garrisons:
        assert row["typeName"] == "EBGarrisonableTower"
        assert row["garrison"]["containMax"] == 3
        assert row["garrison"]["objectStatusOfContained"] == [
            "UNSELECTABLE", "CAN_ATTACK", "ENCLOSED"
        ]
        assert row["garrison"]["damagePercentToUnits"] == 0.0
        assert row["garrison"]["allowNeutralInside"] is True
        assert row["garrison"]["killPassengersOnDeath"] is False
        assert row["garrison"]["passengerFilter"] == [
            "ANY", "+INFANTRY", "+BANNER", "-CAVALRY", "-SUMMONED",
            "-WildSpiderling", "-WildSpiderlingHorde", "-COMBO_HORDE",
            "-IsengardSharku", "-AngmarThrallMaster",
        ]
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


@oracle_present
def test_grey_havens_garrison_tower_pins_retail_contract(fixtures_by_map) -> None:
    document = fixtures_by_map[GREY_HAVENS]
    towers = [
        row for row in document["fixtures"]
        if row["typeName"] == "GHGarrisonableTower"
    ]
    assert towers
    for tower in towers:
        block = tower["garrison"]
        assert block["containMax"] == 1
        assert block["allowNeutralInside"] is True
        assert block["killPassengersOnDeath"] is False
        assert block["damagePercentToUnits"] == 0.0


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


# --- lane L2b: seed disposition + lifecycle-structure reclassification --------

from openbfme_importer.castle_fixtures import (  # noqa: E402
    castle_fixture_seed_disposition,
    map_fixture_object_id,
    rebind_castle_fixture_structures,
)

CARN_DUM = "maps/map wor ang carn dum/map wor ang carn dum.map"

#: Pinned against the pure RotWK oracle (probe over all ten cooked maps,
#: 2026-08-12): per map (seeded rows, seeded distinct types, deferred rows by
#: reason).  The seed rule is three clauses — creep-lair types defer to the
#: creep lane, INERT KindOf is indestructible scenery, CAPTURABLE/CAPTUREFLAG
#: belongs to the capture lane — and the loader's GDScript mirror must produce
#: these same numbers.
EXPECTED_SEED_DISPOSITIONS = {
    "maps/map wor minas tirith/map wor minas tirith.map": (207, 61, {"capturable-flag": 5, "creep-lair-owned": 4, "inert-scenery": 1}),
    "maps/map wor helms deep/map wor helms deep.map": (45, 37, {"capturable-flag": 3, "inert-scenery": 32}),
    EREBOR: (587, 39, {"capturable-flag": 4, "creep-lair-owned": 2, "inert-scenery": 16}),
    "maps/map wor isengard/map wor isengard.map": (23, 7, {"capturable-flag": 2, "creep-lair-owned": 4, "inert-scenery": 27}),
    "maps/map wor black gate/map wor black gate.map": (9, 9, {"creep-lair-owned": 4, "inert-scenery": 72}),
    "maps/map wor dol guldur/map wor dol guldur.map": (319, 10, {"capturable-flag": 5, "creep-lair-owned": 8, "inert-scenery": 2}),
    "maps/map wor grey havens/map wor grey havens.map": (20, 4, {"capturable-flag": 10, "creep-lair-owned": 2, "inert-scenery": 8}),
    "maps/map wor minas morgul/map wor minas morgul.map": (98, 27, {"capturable-flag": 4, "creep-lair-owned": 2, "inert-scenery": 52}),
    CARN_DUM: (260, 22, {"capturable-flag": 3, "inert-scenery": 140}),
    "maps/map wor ang fornost/map wor ang fornost.map": (234, 29, {"capturable-flag": 4, "inert-scenery": 15}),
}


@oracle_present
@pytest.mark.parametrize("virtual_path", sorted(EXPECTED_SEED_DISPOSITIONS))
def test_fixture_seed_disposition_matches_oracle(virtual_path, fixtures_by_map) -> None:
    rows = fixtures_by_map[virtual_path]["fixtures"]
    deferred: dict[str, int] = {}
    seed_types: set[str] = set()
    seed_rows = 0
    for row in rows:
        disposition = castle_fixture_seed_disposition(row)
        if disposition == "seed":
            seed_rows += 1
            seed_types.add(str(row["typeName"]))
            # Every seeded retail type name must form a loader-safe object id.
            assert map_fixture_object_id(str(row["typeName"])).startswith(
                "bfme2.object.map-fixture."
            )
        else:
            deferred[disposition] = deferred.get(disposition, 0) + 1
    expected_rows, expected_types, expected_deferred = EXPECTED_SEED_DISPOSITIONS[
        virtual_path
    ]
    assert seed_rows == expected_rows
    assert len(seed_types) == expected_types
    assert deferred == expected_deferred


def _synthetic_model_bindings(type_names) -> dict[str, object]:
    return {
        "logical": [],
        "models": [
            {
                "typeName": name,
                "sourceVirtualModel": f"art/w3d/eb/{name.casefold()}.w3d",
                "glb": f"assets/models/props/{name.casefold()}.glb",
                "matchMethod": "exact-type-name",
            }
            for name in type_names
        ],
    }


@oracle_present
def test_cooked_erebor_bindings_classify_seeded_types_as_lifecycle_structures(
    fixtures_by_map, catalog, tmp_path
) -> None:
    fixtures = fixtures_by_map[EREBOR]
    type_names = sorted({str(row["typeName"]) for row in fixtures["fixtures"]})
    rebound, evidence = rebind_castle_fixture_structures(
        _synthetic_model_bindings(type_names), fixtures
    )
    assert len(evidence["movedTypeNames"]) == 39
    assert evidence["unboundFixtureTypeNames"] == []
    assert evidence["deferredPlacements"] == {
        "capturable-flag": 4,
        "creep-lair-owned": 2,
        "inert-scenery": 16,
    }

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
        object_bindings=rebound,
        fixtures=fixtures,
    )
    cooked = json.loads((output / "object-bindings.json").read_text(encoding="utf-8"))
    records = {str(row["typeName"]): row for row in cooked["records"]}

    gate = records["EreborGateDoors"]
    assert gate["status"] == "bound"
    assert gate["classification"] == "lifecycle-structure"
    assert gate["objectId"] == "bfme2.object.map-fixture.ereborgatedoors"
    assert gate["glb"] == "assets/models/props/ereborgatedoors.glb"
    tower = records["EBGarrisonableTower"]
    assert tower["classification"] == "lifecycle-structure"
    assert tower["objectId"] == "bfme2.object.map-fixture.ebgarrisonabletower"
    # The underscore name folds to the loader's alphabet.
    throne = records["WOR_EreborThrone"]
    assert throne["objectId"] == "bfme2.object.map-fixture.wor-ereborthrone"

    # Deferred types stay exactly what the visual binder made them.
    assert records["FireDrakeLair"]["classification"] == "renderable"
    assert "objectId" not in records["FireDrakeLair"]
    assert records["CaptureFlag"]["classification"] == "renderable"
    # Omitted (Body-less) scenery was never in the models set: unresolved.
    assert records["EBMineCartD"]["status"] == "unresolved"

    # The cooked summary still closes over its own record table.
    summary = cooked["summary"]
    assert summary["typeCount"] == len(cooked["records"])
    lifecycle = [
        row
        for row in cooked["records"]
        if row["classification"] == "lifecycle-structure"
    ]
    assert len(lifecycle) == 39
    assert summary["boundTypeCount"] >= 39


@oracle_present
def test_map_profile_rebinds_fixture_types_when_fixtures_and_bindings_meet(
    catalog,
) -> None:
    targets, _ = discover_registry_map_targets(
        catalog, categories=(SKIRMISH_CATEGORY,)
    )
    selected = tuple(
        target for target in targets if target.virtual_path == EREBOR
    )
    assert len(selected) == 1

    fixture_row = {
        "typeName": "EreborGateDoors",
        "role": "gate",
        "index": 812,
        "position": [3585.2, 3686.6, 0.0],
        "angle": 1.57,
        "kindOf": ["STRUCTURE", "IMMOBILE", "SELECTABLE", "BLOCKING_GATE", "WALL_GATE"],
        "maxHealth": 20000.0,
        "armor": "DefaultWallArmor",
        "originalOwner": "Player_1/teamPlayer_1",
    }

    def stub_fixtures(target, parsed) -> dict[str, object]:
        return {"schema": "openbfme.sage-map-fixtures", "fixtures": [fixture_row]}

    def fake_binder(target, parsed):
        return (
            [],
            _synthetic_model_bindings(["EreborGateDoors", "DaleHouse"]),
            {"stub": True},
        )

    profile = build_map_profile(
        catalog,
        selected,
        profile_id="fixtures-rebind-test",
        title="fixtures rebind test",
        pack_id="fixtures-rebind-test",
        pack_version="v0",
        terrain_output="assets/terrain/fixtures-rebind-test",
        binder=fake_binder,
        fixtures_builder=stub_fixtures,
    )
    resources = {str(resource["id"]): resource for resource in profile["resources"]}
    erebor = resources[f"map-{selected[0].slug}-binary"]
    bindings = erebor["options"]["objectBindings"]
    assert [row["typeName"] for row in bindings["models"]] == ["DaleHouse"]
    structures = bindings["structures"]
    assert len(structures) == 1
    assert structures[0]["typeName"] == "EreborGateDoors"
    assert structures[0]["objectId"] == "bfme2.object.map-fixture.ereborgatedoors"
    assert structures[0]["glb"] == "assets/models/props/ereborgatedoors.glb"


@oracle_present
def test_map_profile_fixtures_rejection_fails_closed_even_non_strict(catalog) -> None:
    # L2a follow-up F3: the registry-catalog path fails closed when a castle
    # map's fixtures cannot build, but build_map_profile's default non-strict
    # mode used to record the rejection and cook the map with NO fixtures —
    # indistinguishable from a pre-L2a pack downstream. The modes now agree.
    from openbfme_importer.sage_map import SageMapError

    targets, _ = discover_registry_map_targets(
        catalog, categories=(SKIRMISH_CATEGORY,)
    )
    selected = tuple(
        target for target in targets if target.virtual_path == EREBOR
    )
    assert len(selected) == 1

    def raising_builder(target, parsed) -> dict[str, object]:
        raise SageMapError("fixtures boom")

    with pytest.raises(SageMapError, match="fixtures boom"):
        build_map_profile(
            catalog,
            selected,
            profile_id="fixtures-strict-test",
            title="fixtures strict test",
            pack_id="fixtures-strict-test",
            pack_version="v0",
            terrain_output="assets/terrain/fixtures-strict-test",
            strict=False,
            fixtures_builder=raising_builder,
        )
