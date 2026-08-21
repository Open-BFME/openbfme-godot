"""Lane L6: Draw-module walk-surface names compile into descriptors and fixtures.

Oracle: ``workspace/retail-work/editions/rotwk/cache/effective-assets`` (pure
RotWK 2.01).  Every pinned mesh name below is authored on a
``W3DScriptedModelDraw`` block and checked against the intact W3D mesh
headers — never against this lane's own cooked GLBs:

- ``GondorCastleWallSegment`` (campsandcastles.ini): WallBoundsMesh = P1;
  KindOf carries both WALK_ON_TOP_OF_WALL and SCALEABLE_WALL.
- ``MenWallRamp``: RampMesh1 = R1 (present in GBWallRamp) and RampMesh2 = R2
  (retail dangle — recorded as an unresolved receipt, never an error).
- ``HelmsDeepSectionC`` (helmsdeepbuildings.ini): WallBoundsMesh = P2,
  RampMesh1 = P1; KindOf is walk-only (no SCALEABLE_WALL).
- A type that authors none of the four fields emits no ``walkSurfaces`` block.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.castle_fixtures import (
    build_map_fixtures,
    compile_map_object_descriptor,
)
from openbfme_importer.playable_structure_compiler import (
    compile_playable_structure_descriptor,
)
from openbfme_importer.playable_unit_compiler import _numeric_defines, _object_index


ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = ROOT / "workspace" / "retail-work"
if (
    not (PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets").is_dir()
    and ROOT.parent.name == "worktrees"
):
    PRIVATE_ROOT = ROOT.parents[2] / "workspace" / "retail-work"
EFFECTIVE_ASSETS = PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets"

oracle_present = pytest.mark.skipif(
    not (EFFECTIVE_ASSETS / "data" / "ini" / "object").is_dir(),
    reason="pure RotWK effective-assets oracle is not present",
)

_WALK_W3D = (
    "art/w3d/gb/gbwallseg.w3d",
    "art/w3d/gb/gbwallramp.w3d",
    "art/w3d/rb/rbhddwsecc.w3d",
    "art/w3d/rb/rbhdgathsl.w3d",
    "art/w3d/rb/rbhdgathsr.w3d",
    "art/w3d/gb/gbmbridge2.w3d",
    "art/w3d/gb/gbmbridge8.w3d",
    "art/w3d/gb/gbmbridge9.w3d",
    "art/w3d/gb/gbmgate2.w3d",
    "art/w3d/gb/gbmingate1.w3d",
    "art/w3d/gb/gbmingate2.w3d",
    "art/w3d/gb/gbmingate2_d1.w3d",
    "art/w3d/gb/gbmingate2_d2.w3d",
    "art/w3d/gb/gbmingate3.w3d",
    "art/w3d/gb/gbmingate3_d1.w3d",
    "art/w3d/gb/gbmingate3_d2.w3d",
    "art/w3d/gb/gbmtop1.w3d",
    "art/w3d/gb/gbmtop1_d1.w3d",
    "art/w3d/gb/gbmwallc.w3d",
)


def _corpus_documents() -> dict[str, bytes]:
    documents: dict[str, bytes] = {}
    ini_root = EFFECTIVE_ASSETS / "data" / "ini"
    for path in sorted(ini_root.rglob("*")):
        if path.suffix.casefold() not in {".ini", ".inc"} or not path.is_file():
            continue
        documents[path.relative_to(EFFECTIVE_ASSETS).as_posix()] = path.read_bytes()
    for virtual in _WALK_W3D:
        payload = EFFECTIVE_ASSETS.joinpath(*virtual.split("/"))
        if payload.is_file():
            documents[virtual] = payload.read_bytes()
    return documents


@pytest.fixture(scope="module")
def documents() -> dict[str, bytes]:
    return _corpus_documents()


@pytest.fixture(scope="module")
def raw(documents):
    return _object_index(documents)


@pytest.fixture(scope="module")
def defines(documents):
    return _numeric_defines(documents)


def _placement(type_name: str, index: int = 0) -> dict[str, object]:
    return {
        "index": index,
        "typeName": type_name,
        "roadType": 0,
        "godotPosition": [0.0, 300.0, 0.0],
        "godotYawRadians": 0.0,
        "properties": {
            "originalOwner": "Player_1/teamPlayer_1",
            "objectInitialHealth": 100,
            "objectIndestructible": False,
            "objectEnabled": True,
            "objectTargetable": False,
        },
    }


# --- synthetic --------------------------------------------------------------


_SYNTHETIC = {
    "data/ini/gamedata.ini": b"#define TEST_WALL_HEALTH 1500\n",
    "data/ini/armor.ini": (
        b"Armor TestWallArmor\n"
        b"  Armor = DEFAULT 10%\n"
        b"End\n"
    ),
    "data/ini/object/test/walls.ini": (
        b"Object TestPlainKeep\n"
        b"  KindOf = STRUCTURE IMMOBILE SELECTABLE\n"
        b"  Body = ActiveBody ModuleTag_02\n"
        b"    MaxHealth = 5000.0\n"
        b"  End\n"
        b"  ArmorSet\n"
        b"    Conditions = None\n"
        b"    Armor = TestWallArmor\n"
        b"  End\n"
        b"End\n"
        b"\n"
        b"Object TestWalkableWall\n"
        b"  KindOf = STRUCTURE IMMOBILE WALK_ON_TOP_OF_WALL SELECTABLE\n"
        b"  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
        b"    DefaultModelConditionState\n"
        b"      Model = GBWallSeg\n"
        b"    End\n"
        b"  End\n"
        b"  Body = StructureBody ModuleTag_05\n"
        b"    MaxHealth = 110000.0\n"
        b"  End\n"
        b"  ArmorSet\n"
        b"    Conditions = None\n"
        b"    Armor = TestWallArmor\n"
        b"  End\n"
        b"  Geometry = BOX\n"
        b"  GeometryMajorRadius = 40.0\n"
        b"  GeometryMinorRadius = 114.4\n"
        b"  GeometryHeight = 54.0\n"
        b"End\n"
        b"\n"
        b"Object TestRampWall\n"
        b"  KindOf = STRUCTURE IMMOBILE WALK_ON_TOP_OF_WALL SCALEABLE_WALL SELECTABLE\n"
        b"  Draw = W3DScriptedModelDraw ModuleTag_Draw\n"
        b"    DefaultModelConditionState\n"
        b"      Model = GBWallRamp\n"
        b"    End\n"
        b"    WallBoundsMesh = P1\n"
        b"    RampMesh1 = R1\n"
        b"    RampMesh2 = R2\n"
        b"  End\n"
        b"  Body = StructureBody ModuleTag_05\n"
        b"    MaxHealth = 1500.0\n"
        b"  End\n"
        b"  ArmorSet\n"
        b"    Conditions = None\n"
        b"    Armor = TestWallArmor\n"
        b"  End\n"
        b"  Geometry = BOX\n"
        b"  GeometryMajorRadius = 25.0\n"
        b"  GeometryMinorRadius = 60.0\n"
        b"  GeometryHeight = 50\n"
        b"End\n"
    ),
}


def test_type_authoring_no_walk_meshes_emits_no_block() -> None:
    descriptor = compile_map_object_descriptor("TestWalkableWall", _SYNTHETIC)
    assert "walkSurfaces" not in descriptor
    keep = compile_map_object_descriptor("TestPlainKeep", _SYNTHETIC)
    assert "walkSurfaces" not in keep
    document = build_map_fixtures(
        _SYNTHETIC,
        [_placement("TestWalkableWall"), _placement("TestPlainKeep", 1)],
    )
    for row in document["fixtures"]:
        assert "walkSurfaces" not in row


def test_synthetic_draw_fields_compile_names_only() -> None:
    descriptor = compile_map_object_descriptor("TestRampWall", _SYNTHETIC)
    surfaces = descriptor["walkSurfaces"]
    assert surfaces["wallBoundsMesh"] == "P1"
    assert surfaces["rampMesh1"] == "R1"
    assert surfaces["rampMesh2"] == "R2"
    assert "raisedWallMesh" not in surfaces
    document = build_map_fixtures(_SYNTHETIC, [_placement("TestRampWall")])
    row = document["fixtures"][0]
    assert row["walkSurfaces"]["wallBoundsMesh"] == "P1"
    assert row["walkSurfaces"]["rampMesh1"] == "R1"
    assert row["walkSurfaces"]["rampMesh2"] == "R2"


def test_trailing_prose_after_mesh_name_reads_first_token() -> None:
    """helmsdeepbuildings.ini:3372 authors `RampMesh1 = P2 this is not in any
    way suitable for a ramp at this time.` with no comment marker. SAGE reads
    the first token; the cook must not fail on retail's own prose (the v0.2.7
    maps republish died on exactly this line)."""

    documents = dict(_SYNTHETIC)
    documents["data/ini/object/test/walls.ini"] = (
        documents["data/ini/object/test/walls.ini"].replace(
            b"    RampMesh1 = R1\n",
            b"    RampMesh1 = R1 this is not in any way suitable for a ramp at this time.\n",
        )
    )
    descriptor = compile_map_object_descriptor("TestRampWall", documents)
    assert descriptor["walkSurfaces"]["rampMesh1"] == "R1"


# --- retail oracle ----------------------------------------------------------


@oracle_present
def test_gondor_castle_wall_segment_walk_surfaces_and_kindof(
    documents, raw, defines
) -> None:
    descriptor = compile_map_object_descriptor(
        "GondorCastleWallSegment",
        documents,
        raw=raw,
        defines=defines,
        game="rotwk",
    )
    surfaces = descriptor["walkSurfaces"]
    assert surfaces["wallBoundsMesh"] == "P1"
    assert "rampMesh1" not in surfaces
    assert "unresolved" not in surfaces
    kinds = set(descriptor["kindOf"])
    assert "WALK_ON_TOP_OF_WALL" in kinds
    assert "SCALEABLE_WALL" in kinds

    structure = compile_playable_structure_descriptor(
        "GondorCastleWallSegment",
        documents,
        wall_template_roots=("GondorCastleWallSegment",),
        game="rotwk",
    )
    assert structure["walkSurfaces"]["wallBoundsMesh"] == "P1"
    assert "WALK_ON_TOP_OF_WALL" in structure["kindOf"]
    assert "SCALEABLE_WALL" in structure["kindOf"]

    fixtures = build_map_fixtures(
        documents,
        [_placement("GondorCastleWallSegment")],
        raw=raw,
        defines=defines,
        game="rotwk",
    )
    row = fixtures["fixtures"][0]
    assert row["walkSurfaces"]["wallBoundsMesh"] == "P1"
    assert "WALK_ON_TOP_OF_WALL" in row["kindOf"]
    assert "SCALEABLE_WALL" in row["kindOf"]


@oracle_present
def test_men_wall_ramp_rampmesh2_is_unresolved_receipt(
    documents, raw, defines
) -> None:
    descriptor = compile_map_object_descriptor(
        "MenWallRamp", documents, raw=raw, defines=defines, game="rotwk"
    )
    surfaces = descriptor["walkSurfaces"]
    assert surfaces["wallBoundsMesh"] == "P1"
    assert surfaces["rampMesh1"] == "R1"
    assert surfaces["rampMesh2"] == "R2"
    unresolved = surfaces["unresolved"]
    assert any(
        row["role"] == "rampMesh2" and row["meshName"] == "R2" for row in unresolved
    )
    assert all(row["meshName"] != "R1" for row in unresolved)

    structure = compile_playable_structure_descriptor(
        "MenWallRamp",
        documents,
        wall_template_roots=("MenWallRamp",),
        game="rotwk",
    )
    assert structure["walkSurfaces"]["rampMesh1"] == "R1"
    assert structure["walkSurfaces"]["rampMesh2"] == "R2"
    assert any(
        row["role"] == "rampMesh2" and row["meshName"] == "R2"
        for row in structure["walkSurfaces"]["unresolved"]
    )


@oracle_present
def test_helms_deep_section_c_walk_surfaces_walk_only(
    documents, raw, defines
) -> None:
    descriptor = compile_map_object_descriptor(
        "HelmsDeepSectionC", documents, raw=raw, defines=defines, game="rotwk"
    )
    surfaces = descriptor["walkSurfaces"]
    assert surfaces["wallBoundsMesh"] == "P2"
    assert surfaces["rampMesh1"] == "P1"
    assert "rampMesh2" not in surfaces
    kinds = set(descriptor["kindOf"])
    assert "WALK_ON_TOP_OF_WALL" in kinds
    assert "SCALEABLE_WALL" not in kinds

    fixtures = build_map_fixtures(
        documents,
        [_placement("HelmsDeepSectionC")],
        raw=raw,
        defines=defines,
        game="rotwk",
    )
    row = fixtures["fixtures"][0]
    assert row["walkSurfaces"]["wallBoundsMesh"] == "P2"
    assert row["walkSurfaces"]["rampMesh1"] == "P1"
    assert "WALK_ON_TOP_OF_WALL" in row["kindOf"]
    assert "SCALEABLE_WALL" not in row["kindOf"]


@oracle_present
def test_minas_proxy_roles_resolve_from_authored_sibling_models(
    documents, raw, defines
) -> None:
    expected = {
        "MinisTop1": {
            "wallBoundsMesh": "art/w3d/gb/gbmtop1_d1.w3d",
            "rampMesh1": "art/w3d/gb/gbmtop1_d1.w3d",
        },
        "MinisGate2": {
            "wallBoundsMesh": "art/w3d/gb/gbmgate2.w3d",
            "rampMesh1": "art/w3d/gb/gbmgate2.w3d",
        },
        "MinisGate3": {
            "wallBoundsMesh": "art/w3d/gb/gbmingate3_d2.w3d",
        },
    }
    for type_name, sources in expected.items():
        descriptor = compile_map_object_descriptor(
            type_name, documents, raw=raw, defines=defines, game="rotwk"
        )
        surfaces = descriptor["walkSurfaces"]
        assert surfaces["meshSources"] == sources
        unresolved = {
            (row["role"], row["meshName"])
            for row in surfaces.get("unresolved", [])
        }
        assert all((role, surfaces[role]) not in unresolved for role in sources)


@oracle_present
def test_minas_true_retail_absences_stay_named_receipts(
    documents, raw, defines
) -> None:
    expected = {
        "MinisBridge2": {("rampMesh2", "P3")},
        "MinisBridge8": {("rampMesh2", "P3")},
        "MinisBridge9": {("rampMesh2", "P3")},
        "MinisGate1": {
            ("raisedWallMesh", "P1"),
            ("rampMesh1", "P2"),
            ("rampMesh2", "P3"),
        },
        "MinisGate3": {("rampMesh1", "P1")},
        "MinisWallC": {("rampMesh1", "P2")},
    }
    for type_name, missing in expected.items():
        descriptor = compile_map_object_descriptor(
            type_name, documents, raw=raw, defines=defines, game="rotwk"
        )
        surfaces = descriptor["walkSurfaces"]
        assert {
            (row["role"], row["meshName"])
            for row in surfaces.get("unresolved", [])
        } == missing
