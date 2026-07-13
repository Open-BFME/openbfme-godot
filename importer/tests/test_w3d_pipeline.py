from __future__ import annotations

import copy
import json
from pathlib import Path
import tempfile
import unittest

from importer.openbfme_importer.catalog import CatalogEntry
from importer.openbfme_importer.pipeline import (
    _stage_w3d_sources,
    _validated_w3d_metadata,
    _w3d_report_relative_path,
    _w3d_staging_sources,
)
from importer.openbfme_importer.profile import (
    ImportProfile,
    ResolvedResource,
    ResourceRule,
)


def static_report() -> dict:
    return {
        "report_schema": "openbfme.w3d-adapter-report",
        "report_version": 1,
        "asset_kind": "static",
        "meshes": 1,
        "mesh_inventory": [
            {
                "index": 0,
                "semantic_role": "character-mesh",
                "attachment": "scene",
                "proof_methods": [],
                "vertices": 12,
                "triangles": 8,
                "material_slots": 1,
                "skinned": False,
            }
        ],
        "required_equipment": [],
        "equipment": {},
        "animations": 0,
        "animation_curves": 0,
        "animation_keys": 0,
        "bones": 0,
        "skeletons": 0,
        "vertices": 12,
        "triangles": 8,
        "skinned_meshes": 0,
        "materials": 1,
        "images": 1,
        "generated_images": 0,
        "filtered_non_render_geometry": {
            "count": 1,
            "object_types": [{"type": "COLLISION_BOX", "count": 1}],
            "reasons": [{"reason": "non-render-object-type", "count": 1}],
        },
        "remaining_non_render_geometry": 0,
        "remaining_ambiguous_box_geometry": 0,
        "equipment_attachments_canonicalized_restored_and_revalidated": False,
        "fps": 30,
    }


def hierarchical_report() -> dict:
    report = static_report()
    report.update(
        {
            "asset_kind": "hierarchical",
            "bones": 12,
            "skeletons": 1,
            "skinned_meshes": 1,
            "equipment_attachments_canonicalized_restored_and_revalidated": True,
        }
    )
    report["mesh_inventory"][0].update(
        {"attachment": "skeletal", "skinned": True}
    )
    return report


class W3dStaticPipelineTests(unittest.TestCase):
    def test_profile_accepts_explicit_hierarchical_converter(self) -> None:
        payload = {
            "format": 1,
            "id": "hierarchical-model-fixture",
            "pack": {"id": "hierarchical-model-pack"},
            "resources": [
                {
                    "id": "gondor-fortress-model",
                    "kind": "model",
                    "patterns": ["art/w3d/gondor-fortress.w3d"],
                    "converter": "w3d-hierarchical",
                    "output": "models/gondor-fortress.glb",
                    "options": {
                        "model": "gondor-fortress.w3d",
                        "inputResourceIds": [],
                        "excludedOptionalMeshes": ["upgrade_banner"],
                    },
                }
            ],
        }
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "profile.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            profile = ImportProfile.load(path)

        self.assertEqual(profile.resources[0].converter, "w3d-hierarchical")

    def test_profile_rejects_invalid_hierarchical_conversion_requests(self) -> None:
        base = {
            "format": 1,
            "id": "hierarchical-contract-fixture",
            "pack": {"id": "hierarchical-contract-pack"},
            "resources": [
                {
                    "id": "fortress-model",
                    "kind": "model",
                    "patterns": ["art/fortress.w3d"],
                    "converter": "w3d-hierarchical",
                    "output": "models/fortress.glb",
                    "options": {"model": "fortress.w3d"},
                }
            ],
        }
        cases: list[tuple[str, dict, str]] = []
        missing_model = copy.deepcopy(base)
        missing_model["resources"][0]["options"] = {}
        cases.append(("missing-model", missing_model, "requires options.model"))

        animated = copy.deepcopy(base)
        animated["resources"][0]["options"]["animations"] = ["idle.w3d"]
        cases.append(("animation", animated, "forbids animations"))

        equipment = copy.deepcopy(base)
        equipment["resources"][0]["options"]["required_equipment"] = [
            "right-hand-weapon"
        ]
        cases.append(("equipment", equipment, "forbids required equipment"))

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            for name, payload, message in cases:
                with self.subTest(name=name):
                    path = root / f"{name}.json"
                    path.write_text(json.dumps(payload), encoding="utf-8")
                    with self.assertRaisesRegex(ValueError, message):
                        ImportProfile.load(path)

    def test_profile_accepts_explicit_static_converter(self) -> None:
        payload = {
            "format": 1,
            "id": "static-model-fixture",
            "pack": {"id": "static-model-pack"},
            "resources": [
                {
                    "id": "gondor-barracks-textures",
                    "kind": "texture",
                    "patterns": ["art/textures/gondor-barracks.dds"],
                    "converter": "texture",
                    "output": "textures/gondor-barracks.png",
                },
                {
                    "id": "gondor-barracks-model",
                    "kind": "model",
                    "patterns": ["art/w3d/gondor-barracks.w3d"],
                    "converter": "w3d-static",
                    "output": "models/gondor-barracks.glb",
                    "options": {
                        "model": "gondor-barracks.w3d",
                        "inputResourceIds": ["gondor-barracks-textures"],
                        "excludedOptionalMeshes": [
                            "upgrade_plume",
                            "upgrade_banner",
                        ],
                    },
                }
            ],
        }
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "profile.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            profile = ImportProfile.load(path)

        self.assertEqual(profile.resources[1].converter, "w3d-static")
        self.assertEqual(
            profile.resources[1].options["inputResourceIds"],
            ["gondor-barracks-textures"],
        )
        self.assertEqual(
            profile.resources[1].options["excludedOptionalMeshes"],
            ["upgrade_banner", "upgrade_plume"],
        )

    def test_profile_rejects_invalid_w3d_input_dependency_graphs(self) -> None:
        base = {
            "format": 1,
            "id": "w3d-dependency-fixture",
            "pack": {"id": "w3d-dependency-pack"},
            "resources": [
                {
                    "id": "shared-textures",
                    "kind": "texture",
                    "patterns": ["art/shared.dds"],
                    "converter": "texture",
                    "output": "textures/shared.png",
                },
                {
                    "id": "unit-model",
                    "kind": "model",
                    "patterns": ["art/unit.w3d"],
                    "converter": "w3d-static",
                    "output": "models/unit.glb",
                    "options": {
                        "model": "unit.w3d",
                        "inputResourceIds": ["shared-textures"],
                    },
                },
            ],
        }
        cases: list[tuple[str, dict, str]] = []

        duplicate = copy.deepcopy(base)
        duplicate["resources"][1]["options"]["inputResourceIds"] = [
            "shared-textures",
            "shared-textures",
        ]
        cases.append(("duplicate", duplicate, "contains duplicates"))

        unknown = copy.deepcopy(base)
        unknown["resources"][1]["options"]["inputResourceIds"] = ["missing"]
        cases.append(("unknown", unknown, "unknown input resource"))

        self_reference = copy.deepcopy(base)
        self_reference["resources"][1]["options"]["inputResourceIds"] = ["unit-model"]
        cases.append(("self", self_reference, "cannot list itself"))

        non_w3d = copy.deepcopy(base)
        non_w3d["resources"][0]["options"] = {
            "inputResourceIds": ["unit-model"]
        }
        cases.append(("non-w3d", non_w3d, "without a W3D bundle converter"))

        cycle = copy.deepcopy(base)
        cycle["resources"][1]["options"]["inputResourceIds"] = ["second-model"]
        cycle["resources"].append(
            {
                "id": "second-model",
                "kind": "model",
                "patterns": ["art/second.w3d"],
                "converter": "w3d-static",
                "output": "models/second.glb",
                "options": {
                    "model": "second.w3d",
                    "inputResourceIds": ["unit-model"],
                },
            }
        )
        cases.append(("cycle", cycle, "dependency cycle"))

        duplicate_exclusion = copy.deepcopy(base)
        duplicate_exclusion["resources"][1]["options"][
            "excludedOptionalMeshes"
        ] = ["upgrade_banner", "upgrade_banner"]
        cases.append(
            ("duplicate-exclusion", duplicate_exclusion, "contains duplicates")
        )

        unsafe_exclusion = copy.deepcopy(base)
        unsafe_exclusion["resources"][1]["options"][
            "excludedOptionalMeshes"
        ] = ["upgrade_*"]
        cases.append(
            ("unsafe-exclusion", unsafe_exclusion, "invalid clean mesh identifier")
        )

        too_many_exclusions = copy.deepcopy(base)
        too_many_exclusions["resources"][1]["options"][
            "excludedOptionalMeshes"
        ] = [f"upgrade_{index}" for index in range(65)]
        cases.append(
            ("bounded-exclusions", too_many_exclusions, "array of at most 64")
        )

        non_w3d_exclusion = copy.deepcopy(base)
        non_w3d_exclusion["resources"][0]["options"] = {
            "excludedOptionalMeshes": ["upgrade_banner"]
        }
        cases.append(
            (
                "non-w3d-exclusion",
                non_w3d_exclusion,
                "without a W3D bundle converter",
            )
        )

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            for name, payload, message in cases:
                with self.subTest(name=name):
                    path = root / f"{name}.json"
                    path.write_text(json.dumps(payload), encoding="utf-8")
                    with self.assertRaisesRegex(ValueError, message):
                        ImportProfile.load(path)

    def test_explicit_input_closure_isolates_duplicate_cross_unit_basenames(self) -> None:
        def resolved(
            resource_id: str,
            entry: CatalogEntry,
            *,
            converter: str,
            options: dict,
        ) -> ResolvedResource:
            return ResolvedResource(
                ResourceRule(
                    id=resource_id,
                    kind="model" if converter.startswith("w3d-") else "texture",
                    patterns=(entry.name,),
                    required=True,
                    converter=converter,
                    output="models/output.glb",
                    limit=1,
                    expected_count=1,
                    options=options,
                ),
                (entry,),
                (),
                None,
            )

        current_entry = CatalogEntry("unit-a.big", "art/unit-a/shared.w3d", 1, 7, 0)
        texture_entry = CatalogEntry("textures.big", "art/shared.dds", 2, 7, 0)
        other_entry = CatalogEntry("unit-b.big", "art/unit-b/shared.w3d", 3, 7, 0)
        current = resolved(
            "unit-a-model",
            current_entry,
            converter="w3d-static",
            options={
                "model": "shared.w3d",
                "inputResourceIds": ["shared-textures"],
            },
        )
        textures = resolved(
            "shared-textures", texture_entry, converter="texture", options={}
        )
        other = resolved(
            "unit-b-model",
            other_entry,
            converter="w3d-static",
            options={"model": "shared.w3d", "inputResourceIds": []},
        )

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            current_path = root / "unit-a" / "shared.w3d"
            texture_path = root / "textures" / "shared.dds"
            other_path = root / "unit-b" / "shared.w3d"
            for path, payload in (
                (current_path, b"unit-a"),
                (texture_path, b"texture"),
                (other_path, b"unit-b"),
            ):
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(payload)
            extracted = {
                (current_entry.archive.casefold(), current_entry.name.casefold()): {
                    "source_path": current_path
                },
                (texture_entry.archive.casefold(), texture_entry.name.casefold()): {
                    "source_path": texture_path
                },
                (other_entry.archive.casefold(), other_entry.name.casefold()): {
                    "source_path": other_path
                },
            }

            selected = _w3d_staging_sources(
                current, (current, textures, other), extracted
            )
            self.assertEqual(set(selected), {current_path, texture_path})
            copied = _stage_w3d_sources(selected, root / "explicit-input")
            self.assertEqual(copied["shared.w3d"].read_bytes(), b"unit-a")

            legacy = resolved(
                "legacy-unit-model",
                current_entry,
                converter="w3d-static",
                options={"model": "shared.w3d"},
            )
            legacy_sources = _w3d_staging_sources(
                legacy, (legacy, textures, other), extracted
            )
            with self.assertRaisesRegex(RuntimeError, "staging collision"):
                _stage_w3d_sources(legacy_sources, root / "legacy-input")

    def test_static_report_produces_non_skeletal_capabilities(self) -> None:
        metadata = _validated_w3d_metadata(
            static_report(),
            [],
            expected_animation_count=0,
            asset_kind="static",
        )

        self.assertFalse(metadata["capabilities"]["animated"])
        self.assertFalse(metadata["capabilities"]["skeletal"])
        self.assertEqual(metadata["metrics"]["boneCount"], 0)
        self.assertEqual(metadata["metrics"]["animationCount"], 0)
        self.assertEqual(metadata["metrics"]["skinnedMeshCount"], 0)

    def test_optional_mesh_exclusion_report_is_exact_and_payload_free(self) -> None:
        report = static_report()
        report["excluded_optional_meshes"] = [
            {
                "identifier": "upgrade_banner",
                "geometry_sha256": "a" * 64,
                "materials_sha256": "b" * 64,
                "vertices": 12,
                "triangles": 8,
                "material_slots": 1,
            }
        ]

        metadata = _validated_w3d_metadata(
            report,
            [],
            expected_animation_count=0,
            asset_kind="static",
            expected_excluded_optional_meshes=["upgrade_banner"],
        )

        self.assertEqual(
            metadata["excludedOptionalMeshes"],
            [
                {
                    "identifier": "upgrade_banner",
                    "geometrySha256": "a" * 64,
                    "materialsSha256": "b" * 64,
                    "vertexCount": 12,
                    "triangleCount": 8,
                    "materialSlotCount": 1,
                }
            ],
        )
        self.assertEqual(metadata["metrics"]["excludedOptionalMeshCount"], 1)
        self.assertTrue(
            metadata["capabilities"][
                "declaredOptionalRenderSubobjectsExcluded"
            ]
        )

        mismatched = copy.deepcopy(report)
        mismatched["excluded_optional_meshes"][0]["identifier"] = "upgrade_plume"
        with self.assertRaisesRegex(RuntimeError, "do not match"):
            _validated_w3d_metadata(
                mismatched,
                [],
                asset_kind="static",
                expected_excluded_optional_meshes=["upgrade_banner"],
            )

        leaked = copy.deepcopy(report)
        leaked["excluded_optional_meshes"][0]["source_name"] = "private-name"
        with self.assertRaisesRegex(RuntimeError, "entry is invalid"):
            _validated_w3d_metadata(
                leaked,
                [],
                asset_kind="static",
                expected_excluded_optional_meshes=["upgrade_banner"],
            )

    def test_static_report_rejects_skeletal_or_animated_payload(self) -> None:
        for field in ("animations", "animation_curves", "animation_keys", "bones"):
            report = copy.deepcopy(static_report())
            report[field] = 1
            with self.subTest(field=field):
                with self.assertRaisesRegex(
                    RuntimeError, "animation data|skeletal data"
                ):
                    _validated_w3d_metadata(
                        report,
                        [],
                        expected_animation_count=None,
                        asset_kind="static",
                    )

        skeletal_inventory = static_report()
        skeletal_inventory["mesh_inventory"][0]["attachment"] = "skeletal"
        with self.assertRaisesRegex(RuntimeError, "mesh inventory contains skeletal"):
            _validated_w3d_metadata(
                skeletal_inventory,
                [],
                expected_animation_count=0,
                asset_kind="static",
            )

    def test_hierarchical_report_accepts_one_rig_zero_clips_and_skinning(self) -> None:
        metadata = _validated_w3d_metadata(
            hierarchical_report(),
            [],
            expected_animation_count=0,
            asset_kind="hierarchical",
        )

        self.assertFalse(metadata["capabilities"]["animated"])
        self.assertTrue(metadata["capabilities"]["skeletal"])
        self.assertEqual(metadata["metrics"]["skeletonCount"], 1)
        self.assertEqual(metadata["metrics"]["boneCount"], 12)
        self.assertEqual(metadata["metrics"]["animationCount"], 0)
        self.assertEqual(metadata["metrics"]["skinnedMeshCount"], 1)

    def test_hierarchical_report_fails_closed_on_wrong_kind_or_contract(self) -> None:
        for requested_kind in ("animated", "static"):
            with self.subTest(requested_kind=requested_kind):
                with self.assertRaisesRegex(RuntimeError, "asset kind"):
                    _validated_w3d_metadata(
                        hierarchical_report(), [], asset_kind=requested_kind
                    )

        accidental_animation = hierarchical_report()
        accidental_animation.update(
            {"animations": 1, "animation_curves": 2, "animation_keys": 3}
        )
        with self.assertRaisesRegex(RuntimeError, "contains animation data"):
            _validated_w3d_metadata(
                accidental_animation,
                [],
                expected_animation_count=None,
                asset_kind="hierarchical",
            )

        with self.assertRaisesRegex(RuntimeError, "cannot require skeletal equipment"):
            _validated_w3d_metadata(
                hierarchical_report(),
                ["right-hand-weapon"],
                asset_kind="hierarchical",
            )

        missing_rig = hierarchical_report()
        missing_rig["skeletons"] = 0
        with self.assertRaisesRegex(RuntimeError, "skeleton count"):
            _validated_w3d_metadata(
                missing_rig, [], asset_kind="hierarchical"
            )

        multiple_rigs = hierarchical_report()
        multiple_rigs["skeletons"] = 2
        with self.assertRaisesRegex(RuntimeError, "skeleton count"):
            _validated_w3d_metadata(
                multiple_rigs, [], asset_kind="hierarchical"
            )

        empty_hierarchy = hierarchical_report()
        empty_hierarchy["bones"] = 0
        with self.assertRaisesRegex(RuntimeError, "invalid bones"):
            _validated_w3d_metadata(
                empty_hierarchy, [], asset_kind="hierarchical"
            )

    def test_requested_asset_kind_must_match_adapter_report(self) -> None:
        report = static_report()
        report["asset_kind"] = "animated"
        with self.assertRaisesRegex(RuntimeError, "asset kind"):
            _validated_w3d_metadata(report, [], asset_kind="static")

    def test_report_path_uses_only_a_validated_resource_id(self) -> None:
        self.assertEqual(
            _w3d_report_relative_path("gondor-barracks-model"),
            "provenance/conversion/gondor-barracks-model.json",
        )
        for unsafe in ("Gondor-Barracks", "../fighter", "fighter/unit", ""):
            with self.subTest(asset_id=unsafe):
                with self.assertRaisesRegex(ValueError, "bounded lowercase slug"):
                    _w3d_report_relative_path(unsafe)


if __name__ == "__main__":
    unittest.main()
