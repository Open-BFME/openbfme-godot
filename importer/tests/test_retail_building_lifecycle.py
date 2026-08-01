from __future__ import annotations

import copy
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest import mock

from openbfme_importer import retail_building_lifecycle as lifecycle
from openbfme_importer.retail_visual_closure import build_retail_visual_closure


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("cp1252")
    if len(encoded) > size:
        raise ValueError("fixture identifier is too long")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(chunk_id: int, payload: bytes, *, children: bool = False) -> bytes:
    size = len(payload) | (0x80000000 if children else 0)
    return struct.pack("<II", chunk_id, size) + payload


def _animation_w3d(identifier: str, hierarchy: str) -> bytes:
    hierarchy_header = struct.pack(
        "<I16sI3f",
        0x00040001,
        _fixed(hierarchy, 16),
        0,
        0.0,
        0.0,
        0.0,
    )
    animation_header = struct.pack(
        "<I16s16sII",
        0x00040001,
        _fixed(identifier, 16),
        _fixed(hierarchy, 16),
        30,
        15,
    )
    return _chunk(0x100, _chunk(0x101, hierarchy_header), children=True) + _chunk(
        0x200, _chunk(0x201, animation_header), children=True
    )


def _textured_w3d(identifier: str) -> bytes:
    texture = _chunk(
        0x31,
        _chunk(0x32, identifier.encode("cp1252") + b"\0"),
        children=True,
    )
    return _chunk(
        0x00000000,
        _chunk(0x30, texture, children=True),
        children=True,
    )


def _write(root: Path, virtual_path: str, source: bytes) -> None:
    path = root.joinpath(*virtual_path.split("/"))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(source)


def _dotnet_string(value: str) -> bytes:
    encoded = value.encode("utf-8")
    length = len(encoded)
    prefix = bytearray()
    while length >= 0x80:
        prefix.append((length & 0x7F) | 0x80)
        length >>= 7
    prefix.append(length)
    return bytes(prefix) + encoded


def _u16_string(value: str) -> bytes:
    encoded = value.encode("cp1252")
    return struct.pack("<H", len(encoded)) + encoded


def _map_record(index: int, version: int, payload: bytes) -> bytes:
    return struct.pack("<IHI", index, version, len(payload)) + payload


def _fortress_base_template() -> bytes:
    names = [
        "HeightMapData",
        "ObjectsList",
        "Object",
        "CastleTemplates",
        "Fortress_Men",
    ]
    indexes = {name: index + 1 for index, name in enumerate(names)}
    name_table = struct.pack("<I", len(names))
    for index in range(len(names), 0, -1):
        name_table += _dotnet_string(names[index - 1]) + struct.pack("<I", index)

    height = struct.pack("<IIIII4H", 2, 2, 0, 0, 4, 100, 100, 100, 100)
    object_payload = struct.pack("<ffffI", 0.0, 0.0, 0.0, 0.0, 0)
    object_payload += _u16_string("MenFortressCitadel") + struct.pack("<H", 0)
    objects = _map_record(indexes["Object"], 3, object_payload)

    castle = bytes([3]) + indexes["Fortress_Men"].to_bytes(3, "little")
    castle += struct.pack("<I", 1)
    castle += _u16_string("") + _u16_string("MenFortressCitadel")
    castle += struct.pack("<ffffII", 0.0, 0.0, 0.0, 0.0, 40, 1)
    castle += struct.pack("<I", 0)
    top = b"".join(
        [
            _map_record(indexes["HeightMapData"], 5, height),
            _map_record(indexes["ObjectsList"], 3, objects),
            _map_record(indexes["CastleTemplates"], 5, castle),
        ]
    )
    return b"CkMp" + name_table + top


def _building_source(name: str, prefix: str) -> bytes:
    return f"""
Object {name}
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = {prefix}_SKN
    End
    IdleAnimationState
      Animation = Idle
        AnimationName = {prefix}_SKL.{prefix}_IDLA
        AnimationMode = ONCE
      End
    End
    ModelConditionState = AWAITING_CONSTRUCTION
      Model = {prefix}_A
    End
    AnimationState = AWAITING_CONSTRUCTION
      Animation = Build
        AnimationName = {prefix}_ASKL.{prefix}_ABLD
        AnimationMode = MANUAL
      End
      Flags = START_FRAME_FIRST
      BeginScript
        CurDrawableHideSubObject("WINDOW")
      EndScript
    End
    ModelConditionState = DAMAGED
      Model = {prefix}_D1
      Texture = Damage.tga Damage_D.tga
      ParticleSysBone = Smoke01 FireBuildingMedium
    End
    AnimationState = DAMAGED
      EnteringStateFX = FX_BuildingDamaged
    End
    ModelConditionState = REALLYDAMAGED
      Model = {prefix}_D2
    End
    ModelConditionState = RUBBLE
      Model = {prefix}_D3
    End
    ModelConditionState = POST_RUBBLE
      Model = None
    End
  End
  Draw = W3DFloorDraw ModuleTag_Floor
    ModelName = {prefix}_BIB
    HideIfModelConditions = AWAITING_CONSTRUCTION
  End
  Side = Men
  PlacementViewAngle = 135
  BuildCost = TEST_COST
  BuildTime = TEST_TIME
  Geometry = BOX
  GeometryMajorRadius = 40
  GeometryMinorRadius = 35
  GeometryHeight = 30
  KindOf = STRUCTURE SELECTABLE IMMOBILE
  Body = StructureBody ModuleTag_Body
    MaxHealth = TEST_HEALTH
  End
  Behavior = GettingBuiltBehavior ModuleTag_Build
    WorkerName = GondorWorker
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse
    DestroyObjectWhenDone = Yes
  End
End
""".encode("cp1252")


def _fixture(root: Path, *, omit_stable: bool = False) -> None:
    fortress = b"""
Object MenFortress
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = None
    End
    ModelConditionState = PHANTOM_STRUCTURE
      Model = GBFortress
    End
    ModelConditionState = ACTIVELY_BEING_CONSTRUCTED
      Model = GBFortress_A
    End
    AnimationState = ACTIVELY_BEING_CONSTRUCTED
      Animation = Build
        AnimationName = GBFortress_ASK.GBFortress_ABL
        AnimationMode = MANUAL
      End
    End
    ModelConditionState = RUBBLE
      Model = GBFortress_D3
    End
  End
  Side = Men
  PlacementViewAngle = -45
  Behavior = CastleBehavior ModuleTag_castle
    CastleToUnpackForFaction = Men Fortress_Men
    InstantUnpack = Yes
    KeepDeathKillsEverything = Yes
  End
  Behavior = GettingBuiltBehavior ModuleTag_Build
    WorkerName = GondorWorker
  End
End

Object MenFortressCitadel
  Draw = W3DScriptedModelDraw ModuleTag_Draw
    DefaultModelConditionState
      Model = GBFortress
    End
    ModelConditionState = DAMAGED
      Model = GBFortress
      Texture = Damage.tga Damage_D.tga
    End
    ModelConditionState = REALLYDAMAGED
      Model = GBFortress_D2
    End
    ModelConditionState = RUBBLE
      Model = GBFortress_D3
    End
    ModelConditionState = POST_COLLAPSE
      Model = None
    End
  End
  Side = Men
  Body = StructureBody ModuleTag_Body
    MaxHealth = TEST_FORTRESS_HEALTH
  End
  Behavior = CastleMemberBehavior ModuleTag_Member
  End
  Behavior = StructureCollapseUpdate ModuleTag_Collapse
    DestroyObjectWhenDone = Yes
  End
End
"""
    _write(
        root,
        "data/ini/object/goodfaction/structures/men/fortress.ini",
        fortress,
    )
    _write(
        root,
        "data/ini/object/goodfaction/structures/men/barracks.ini",
        _building_source("GondorBarracks", "GBBarracks"),
    )
    _write(
        root,
        "data/ini/object/goodfaction/structures/men/archerrange.ini",
        _building_source("GondorArcherRange", "GBArcheryN"),
    )
    if not omit_stable:
        _write(
            root,
            "data/ini/object/goodfaction/structures/men/stable.ini",
            _building_source("GondorStable", "GBStable"),
        )
    _write(
        root,
        "data/ini/object/goodfaction/structures/farminterface.ini",
        _building_source("FarmInterface", "GBFarm"),
    )
    _write(
        root,
        "data/ini/object/goodfaction/structures/men/farm.ini",
        b"""
ChildObject GondorFarm FarmInterface
  Draw = W3DScriptedModelDraw ModuleTag_Banner
    DefaultModelConditionState
      Model = GBHCFarm
    End
  End
  BuildCost = GONDOR_FARM_BUILDCOST
End
""",
    )

    model_ids = {
        "GBFortress",
        "GBFortress_A",
        "GBFortress_D2",
        "GBFortress_D3",
        "GBHCFarm",
    }
    for prefix in ("GBBarracks", "GBArcheryN", "GBStable", "GBFarm"):
        model_ids.update(
            {
                f"{prefix}_SKN",
                f"{prefix}_A",
                f"{prefix}_D1",
                f"{prefix}_D2",
                f"{prefix}_D3",
                f"{prefix}_BIB",
            }
        )
        _write(
            root,
            f"art/w3d/{prefix.lower()}_idla.w3d",
            _animation_w3d(f"{prefix}_IDLA", f"{prefix}_SKL"),
        )
        _write(
            root,
            f"art/w3d/{prefix.lower()}_abld.w3d",
            _animation_w3d(f"{prefix}_ABLD", f"{prefix}_ASKL"),
        )
    for identifier in sorted(model_ids):
        payload = _textured_w3d("Shared.tga") if identifier == "GBBarracks_SKN" else b""
        _write(root, f"art/w3d/{identifier.lower()}.w3d", payload)
    _write(
        root,
        "art/w3d/gbfortress_abl.w3d",
        _animation_w3d("GBFortress_ABL", "GBFortress_ASK"),
    )
    _write(root, "art/compiledtextures/shared.dds", b"shared fixture texture")
    _write(root, "art/compiledtextures/damage.dds", b"damage fixture texture")
    _write(root, "art/compiledtextures/damage_d.dds", b"damaged fixture texture")
    _write(
        root,
        "bases/fortress_men/fortress_men.bse",
        _fortress_base_template(),
    )


def _structure(report: dict[str, object], runtime_kind: str) -> dict[str, object]:
    return next(
        item
        for item in report["structures"]  # type: ignore[union-attr]
        if item["runtimeKind"] == runtime_kind
    )


class RetailBuildingLifecycleTests(unittest.TestCase):
    def test_report_is_deterministic_and_keeps_exact_lifecycle_bindings(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "effective"
            _fixture(root)

            first = lifecycle.build_retail_building_lifecycle_report(root)
            second = lifecycle.build_retail_building_lifecycle_report(root)
            self.assertEqual(first, second)
            self.assertEqual(first["schema"], lifecycle.SCHEMA)
            self.assertEqual(first["summary"]["runtimeStructureCount"], 5)
            self.assertEqual(first["summary"]["sourceObjectCount"], 6)
            self.assertEqual(first["summary"]["unresolvedVisualReferenceCount"], 0)

            barracks = _structure(first, "barracks")
            references = [
                reference
                for binding in barracks["visualBindings"]
                for reference in binding["references"]
            ]
            intact = {
                item["identifier"]
                for item in references
                if item["kind"] == "model" and item["lifecyclePhases"] == ["intact"]
            }
            construction = {
                item["identifier"]
                for item in references
                if item["kind"] == "model"
                and item["lifecyclePhases"] == ["construction"]
            }
            animations = {
                item["identifier"]: item["physicalVirtualPaths"]
                for item in references
                if item["kind"] == "animation"
            }
            self.assertIn("GBBarracks_SKN", intact)
            self.assertIn("GBBarracks_A", construction)
            self.assertEqual(
                animations["GBBarracks_ASKL.GBBarracks_ABLD"],
                ["art/w3d/gbbarracks_abld.w3d"],
            )
            embedded = [
                item
                for binding in barracks["visualBindings"]
                for item in binding["embeddedTextureBindings"]
            ]
            self.assertTrue(
                any(
                    item["identifier"] == "Shared.tga"
                    and item["physicalVirtualPaths"]
                    == ["art/compiledtextures/shared.dds"]
                    for item in embedded
                )
            )

            fortress = _structure(first, "fortress")
            handoff = fortress["authoredHandoff"]
            self.assertEqual(handoff["castleTemplateToken"], "Fortress_Men")
            self.assertEqual(
                handoff["status"],
                "source-proven-template-to-operational-object-link",
            )
            self.assertEqual(
                handoff["baseTemplateEvidence"]["virtualPath"],
                "bases/fortress_men/fortress_men.bse",
            )
            self.assertEqual(
                handoff["operationalTemplateEvidence"]["templateName"],
                "MenFortressCitadel",
            )
            module_kinds = {
                module["moduleKind"]
                for source in fortress["sourceObjectEvidence"]
                for module in source["runtimeModules"]
            }
            self.assertIn("GettingBuiltBehavior", module_kinds)
            self.assertIn("StructureCollapseUpdate", module_kinds)

            output_a = Path(temporary) / "a.json"
            output_b = Path(temporary) / "b.json"
            lifecycle.write_retail_building_lifecycle_report(output_a, first)
            lifecycle.write_retail_building_lifecycle_report(output_b, second)
            self.assertEqual(output_a.read_bytes(), output_b.read_bytes())
            rendered = output_a.read_text(encoding="utf-8")
            self.assertNotIn(str(root), rendered)
            self.assertEqual(
                json.loads(rendered)["aggregateSha256"], first["aggregateSha256"]
            )

    def test_report_tamper_fails_digest_verification(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "effective"
            _fixture(root)
            report = lifecycle.build_retail_building_lifecycle_report(root)
            tampered = copy.deepcopy(report)
            tampered["summary"]["runtimeStructureCount"] = 6
            with self.assertRaisesRegex(
                lifecycle.RetailBuildingLifecycleError,
                "aggregate SHA-256 mismatch",
            ):
                lifecycle.verify_retail_building_lifecycle_report(tampered)

    def test_missing_scoped_object_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "effective"
            _fixture(root, omit_stable=True)
            with self.assertRaisesRegex(
                lifecycle.RetailBuildingLifecycleError,
                "every lifecycle target must resolve",
            ):
                lifecycle.build_retail_building_lifecycle_report(root)

    def test_bound_asset_drift_after_visual_scan_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary) / "effective"
            _fixture(root)
            closure = build_retail_visual_closure(root, lifecycle.TARGET_OBJECTS)
            (root / "art/w3d/gbbarracks_skn.w3d").unlink()
            with mock.patch.object(
                lifecycle, "build_retail_visual_closure", return_value=closure
            ):
                with self.assertRaisesRegex(
                    lifecycle.RetailBuildingLifecycleError,
                    "bound asset is not a regular contained file",
                ):
                    lifecycle.build_retail_building_lifecycle_report(root)

    def test_non_directory_root_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "not-a-directory"
            path.write_bytes(b"fixture")
            with self.assertRaisesRegex(
                lifecycle.RetailBuildingLifecycleError,
                "not a regular directory",
            ):
                lifecycle.build_retail_building_lifecycle_report(path)


if __name__ == "__main__":
    unittest.main()
