from __future__ import annotations

import copy
import json
from pathlib import Path
import tempfile
import unittest

from importer.openbfme_importer.catalog import CatalogEntry, InstallCatalog
from importer.openbfme_importer.pipeline import (
    ImportPipeline,
    _stage_w3d_sources,
    _validated_w3d_metadata,
    _w3d_conversion_cache_key,
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
        "report_version": 2,
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
        "animation_action_shapes": [],
        "action_shape_animation_count": 0,
        "action_shape_action_count": 0,
        "action_shape_nla_track_count": 0,
        "action_shape_exported_animation_count": 0,
        "action_shape_exported_channel_count": 0,
        "action_shape_exported_sampler_count": 0,
        "action_shape_exported_skin_count": 0,
        "action_shape_exported_skeletal_mesh_count": 0,
        "duplicated_logical_animation_count": 0,
        "preserved_visibility_channel_count": 0,
        "preserved_visibility_key_count": 0,
        "visibility_only_sidecar_animation_count": 0,
        "bones": 0,
        "skeletons": 0,
        "vertices": 12,
        "triangles": 8,
        "skinned_meshes": 0,
        "materials": 1,
        "images": 1,
        "generated_images": 0,
        "shader_material_compatibility": {
            "mapped_materials": [],
            "mapped_material_count": 0,
            "mapped_property_count": 0,
            "alpha_blending_enable_count": 0,
            "fog_enable_count": 0,
            "source_flags_preserved": True,
        },
        "root_rigid_bake": {
            "requested": False,
            "applied": False,
            "removed_carriers": 0,
            "baked_meshes": 0,
            "world_transforms_preserved": False,
            "deform_ambiguity_absent": False,
        },
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
    report["mesh_inventory"][0].update({"attachment": "skeletal", "skinned": True})
    return report


def root_rigid_report() -> dict:
    report = hierarchical_report()
    report.update(
        {
            "bones": 0,
            "skeletons": 0,
            "skinned_meshes": 0,
            "equipment_attachments_canonicalized_restored_and_revalidated": False,
            "root_rigid_bake": {
                "requested": True,
                "applied": True,
                "removed_carriers": 1,
                "baked_meshes": 1,
                "world_transforms_preserved": True,
                "deform_ambiguity_absent": True,
            },
        }
    )
    report["mesh_inventory"][0].update({"attachment": "scene", "skinned": False})
    return report


def embedded_animated_report() -> dict:
    report = hierarchical_report()
    report.update(
        {
            "asset_kind": "animated",
            "animations": 1,
            "animation_curves": 3,
            "animation_keys": 3,
            "animation_action_shapes": [
                {
                    "name": "embedded",
                    "shape": "transform-and-visibility",
                    "action_count": 2,
                    "object_action_count": 1,
                    "armature_action_count": 1,
                    "transform_curve_count": 2,
                    "visibility_curve_count": 1,
                    "material_curve_count": 0,
                    "unsupported_curve_count": 0,
                }
            ],
            "action_shape_animation_count": 1,
            "action_shape_action_count": 2,
            "action_shape_nla_track_count": 1,
            "action_shape_exported_animation_count": 1,
            "action_shape_exported_channel_count": 6,
            "action_shape_exported_sampler_count": 6,
            "action_shape_exported_skin_count": 1,
            "action_shape_exported_skeletal_mesh_count": 1,
            "duplicated_logical_animation_count": 0,
            "preserved_visibility_channel_count": 1,
            "preserved_visibility_key_count": 1,
            "visibility_only_sidecar_animation_count": 0,
            "embedded_model_animation": True,
            "embedded_model_action_count": 2,
            "embedded_exported_animation_count": 1,
            "embedded_exported_channel_count": 6,
            "embedded_exported_sampler_count": 6,
            "embedded_exported_skin_count": 1,
            "embedded_exported_skeletal_mesh_count": 1,
            "split_action_animation_count": 1,
            "split_action_count": 2,
            "split_exported_animation_count": 1,
            "split_exported_channel_count": 6,
            "split_exported_sampler_count": 6,
            "split_exported_skin_count": 1,
            "split_exported_skeletal_mesh_count": 1,
            "skinned_meshes": 0,
        }
    )
    report["mesh_inventory"][0]["skinned"] = False
    return report


def embedded_visibility_only_report() -> dict:
    """Exact GBFDoor_DRC-style same-container visibility-only proof."""

    report = embedded_animated_report()
    report["animation_action_shapes"][0].update(
        {
            "shape": "visibility-only",
            "transform_curve_count": 0,
            "visibility_curve_count": 3,
        }
    )
    report.update(
        {
            "action_shape_nla_track_count": 0,
            "action_shape_exported_animation_count": 0,
            "action_shape_exported_channel_count": 0,
            "action_shape_exported_sampler_count": 0,
            "preserved_visibility_channel_count": 3,
            "preserved_visibility_key_count": 3,
            "visibility_only_sidecar_animation_count": 1,
            "embedded_exported_animation_count": 0,
            "embedded_exported_channel_count": 0,
            "embedded_exported_sampler_count": 0,
            "split_exported_animation_count": 0,
            "split_exported_channel_count": 0,
            "split_exported_sampler_count": 0,
        }
    )
    return report


def mixed_capture_flag_report() -> dict:
    """Exact 3-transform plus 1 visibility-only CaptureFlag action shape."""

    report = embedded_animated_report()
    action_shapes = []
    for name in ("capflag_dn", "capflag_sdn", "capflag_sup", "capflag_up"):
        visibility_only = name == "capflag_sdn"
        action_shapes.append(
            {
                "name": name,
                "shape": (
                    "visibility-only" if visibility_only else "transform-and-visibility"
                ),
                "action_count": 2,
                "object_action_count": 1,
                "armature_action_count": 1,
                "transform_curve_count": 0 if visibility_only else 7,
                "visibility_curve_count": 9,
                "material_curve_count": 0,
                "unsupported_curve_count": 0,
            }
        )
    report.update(
        {
            "animations": 4,
            "animation_curves": 57,
            "animation_keys": 360,
            "animation_action_shapes": action_shapes,
            "action_shape_animation_count": 4,
            "action_shape_action_count": 8,
            "action_shape_nla_track_count": 3,
            "action_shape_exported_animation_count": 3,
            "action_shape_exported_channel_count": 81,
            "action_shape_exported_sampler_count": 81,
            "action_shape_exported_skeletal_mesh_count": 8,
            "preserved_visibility_channel_count": 36,
            "preserved_visibility_key_count": 36,
            "visibility_only_sidecar_animation_count": 1,
            "embedded_model_animation": False,
            "embedded_model_action_count": 0,
            "embedded_exported_animation_count": 0,
            "embedded_exported_channel_count": 0,
            "embedded_exported_sampler_count": 0,
            "embedded_exported_skin_count": 0,
            "embedded_exported_skeletal_mesh_count": 0,
            "split_action_animation_count": 4,
            "split_action_count": 8,
            "split_exported_animation_count": 3,
            "split_exported_channel_count": 81,
            "split_exported_sampler_count": 81,
            "split_exported_skeletal_mesh_count": 8,
        }
    )
    return report


def transform_only_animated_report(*, embedded: bool = False) -> dict:
    report = embedded_animated_report()
    report.update(
        {
            "embedded_model_animation": embedded,
            "embedded_model_action_count": 1 if embedded else 0,
            "embedded_exported_animation_count": 1 if embedded else 0,
            "embedded_exported_channel_count": 6 if embedded else 0,
            "embedded_exported_sampler_count": 6 if embedded else 0,
            "embedded_exported_skin_count": 1 if embedded else 0,
            "embedded_exported_skeletal_mesh_count": 1 if embedded else 0,
            "split_action_animation_count": 0,
            "split_action_count": 0,
            "split_exported_animation_count": 0,
            "split_exported_channel_count": 0,
            "split_exported_sampler_count": 0,
            "split_exported_skin_count": 0,
            "split_exported_skeletal_mesh_count": 0,
            "action_shape_action_count": 1,
            "preserved_visibility_channel_count": 0,
            "preserved_visibility_key_count": 0,
        }
    )
    report["animation_action_shapes"] = [
        {
            "name": "rigid-motion",
            "shape": "transform-only",
            "action_count": 1,
            "object_action_count": 1,
            "armature_action_count": 0,
            "transform_curve_count": 3,
            "visibility_curve_count": 0,
            "material_curve_count": 0,
            "unsupported_curve_count": 0,
        }
    ]
    return report


def shader_compatibility_report() -> dict:
    report = static_report()
    report["materials"] = 2
    report["shader_material_compatibility"] = {
        "mapped_materials": [
            {
                "material": "Cloud",
                "properties": {"FogEnable": False},
            },
            {
                "material": "water",
                "properties": {
                    "AlphaBlendingEnable": True,
                    "FogEnable": True,
                },
            },
        ],
        "mapped_material_count": 2,
        "mapped_property_count": 3,
        "alpha_blending_enable_count": 1,
        "fog_enable_count": 2,
        "source_flags_preserved": True,
    }
    return report


class W3dStaticPipelineTests(unittest.TestCase):
    def test_transform_only_rigid_animation_does_not_require_skin_modifiers(
        self,
    ) -> None:
        for embedded in (False, True):
            with self.subTest(embedded=embedded):
                metadata = _validated_w3d_metadata(
                    transform_only_animated_report(embedded=embedded),
                    [],
                    expected_animation_count=1,
                    asset_kind="animated",
                    expected_embedded_model_animation=embedded,
                )
                self.assertEqual(metadata["metrics"]["skinnedMeshCount"], 0)
                self.assertEqual(
                    metadata["metrics"]["actionShapeExportedSkeletalMeshCount"],
                    1,
                )
                self.assertEqual(
                    metadata["animationActionShapes"][0]["shape"],
                    "transform-only",
                )

    def test_visibility_only_animation_is_preserved_as_typed_sidecar(
        self,
    ) -> None:
        report = transform_only_animated_report()
        report["animation_action_shapes"][0].update(
            {
                "shape": "visibility-only",
                "transform_curve_count": 0,
                "visibility_curve_count": 3,
            }
        )
        report["preserved_visibility_channel_count"] = 3
        report["preserved_visibility_key_count"] = 3
        report["visibility_only_sidecar_animation_count"] = 1
        report["action_shape_nla_track_count"] = 0
        report["action_shape_exported_animation_count"] = 0
        report["action_shape_exported_channel_count"] = 0
        report["action_shape_exported_sampler_count"] = 0
        metadata = _validated_w3d_metadata(
            report,
            [],
            expected_animation_count=1,
            asset_kind="animated",
        )
        self.assertEqual(
            metadata["metrics"]["visibilityOnlySidecarAnimationCount"], 1
        )
        self.assertTrue(
            metadata["capabilities"][
                "sourceVisibilityChannelsPreservedInGlbExtras"
            ]
        )

    def test_embedded_model_animation_proof_is_exact_and_canonicalized(self) -> None:
        metadata = _validated_w3d_metadata(
            embedded_animated_report(),
            [],
            expected_animation_count=1,
            asset_kind="animated",
            expected_embedded_model_animation=True,
        )

        self.assertTrue(metadata["capabilities"]["embeddedModelAnimationImportedOnce"])
        self.assertEqual(
            metadata["embeddedModelAnimation"],
            {
                "importedOnce": True,
                "actionCount": 2,
                "exportedAnimationCount": 1,
                "exportedChannelCount": 6,
                "exportedSamplerCount": 6,
                "exportedSkinCount": 1,
                "exportedSkeletalMeshCount": 1,
            },
        )
        self.assertEqual(
            metadata["splitActionAnimations"],
            {
                "animationCount": 1,
                "actionCount": 2,
                "exportedAnimationCount": 1,
                "exportedChannelCount": 6,
                "exportedSamplerCount": 6,
                "exportedSkinCount": 1,
                "exportedSkeletalMeshCount": 1,
            },
        )

        external_split = embedded_animated_report()
        external_split["embedded_model_animation"] = False
        for key in (
            "embedded_model_action_count",
            "embedded_exported_animation_count",
            "embedded_exported_channel_count",
            "embedded_exported_sampler_count",
            "embedded_exported_skin_count",
            "embedded_exported_skeletal_mesh_count",
        ):
            external_split[key] = 0
        external_metadata = _validated_w3d_metadata(
            external_split,
            [],
            expected_animation_count=1,
            asset_kind="animated",
        )
        self.assertTrue(
            external_metadata["capabilities"]["splitActionAnimationsMergedAndValidated"]
        )

        cases = []
        mismatched = embedded_animated_report()
        mismatched["embedded_model_animation"] = False
        cases.append(("does not match", mismatched))
        one_action = embedded_animated_report()
        one_action["embedded_model_action_count"] = 1
        cases.append(("incomplete", one_action))
        non_boolean = embedded_animated_report()
        non_boolean["embedded_model_animation"] = 1
        cases.append(("invalid", non_boolean))
        for message, report in cases:
            with self.subTest(message=message):
                with self.assertRaisesRegex(RuntimeError, message):
                    _validated_w3d_metadata(
                        report,
                        [],
                        expected_animation_count=1,
                        asset_kind="animated",
                        expected_embedded_model_animation=True,
                    )

    def test_embedded_split_visibility_only_proof_keeps_zero_core_channels(
        self,
    ) -> None:
        metadata = _validated_w3d_metadata(
            embedded_visibility_only_report(),
            [],
            expected_animation_count=1,
            asset_kind="animated",
            expected_embedded_model_animation=True,
        )

        self.assertEqual(metadata["metrics"]["actionShapeExportedAnimationCount"], 0)
        self.assertEqual(metadata["metrics"]["visibilityOnlySidecarAnimationCount"], 1)
        self.assertEqual(
            metadata["embeddedModelAnimation"],
            {
                "importedOnce": True,
                "actionCount": 2,
                "exportedAnimationCount": 0,
                "exportedChannelCount": 0,
                "exportedSamplerCount": 0,
                "exportedSkinCount": 1,
                "exportedSkeletalMeshCount": 1,
            },
        )
        self.assertEqual(metadata["splitActionAnimations"]["exportedAnimationCount"], 0)
        self.assertEqual(metadata["splitActionAnimations"]["exportedChannelCount"], 0)
        self.assertEqual(metadata["splitActionAnimations"]["exportedSamplerCount"], 0)
        self.assertEqual(metadata["splitActionAnimations"]["exportedSkinCount"], 1)
        self.assertEqual(
            metadata["splitActionAnimations"]["exportedSkeletalMeshCount"], 1
        )

    def test_embedded_split_transform_export_proof_remains_fail_closed(self) -> None:
        cases: list[tuple[str, dict]] = []

        missing_embedded_transform = embedded_animated_report()
        missing_embedded_transform["embedded_exported_animation_count"] = 0
        cases.append(("embedded-missing-transform", missing_embedded_transform))

        missing_embedded_channels = embedded_animated_report()
        missing_embedded_channels["embedded_exported_channel_count"] = 0
        cases.append(("embedded-missing-channels", missing_embedded_channels))

        missing_split_samplers = embedded_animated_report()
        missing_split_samplers["split_exported_sampler_count"] = 0
        cases.append(("split-missing-samplers", missing_split_samplers))

        unexpected_visibility_channels = embedded_visibility_only_report()
        unexpected_visibility_channels["embedded_exported_channel_count"] = 1
        cases.append(
            ("visibility-only-unexpected-embedded-channel", unexpected_visibility_channels)
        )

        unexpected_core_channel = embedded_visibility_only_report()
        unexpected_core_channel["action_shape_exported_channel_count"] = 1
        cases.append(("visibility-only-unexpected-core-channel", unexpected_core_channel))

        unexpected_split_sampler = embedded_visibility_only_report()
        unexpected_split_sampler["split_exported_sampler_count"] = 1
        cases.append(
            ("visibility-only-unexpected-split-sampler", unexpected_split_sampler)
        )

        for name, report in cases:
            with self.subTest(name=name):
                with self.assertRaisesRegex(RuntimeError, "proof is incomplete"):
                    _validated_w3d_metadata(
                        report,
                        [],
                        expected_animation_count=1,
                        asset_kind="animated",
                        expected_embedded_model_animation=True,
                    )

    def test_mixed_capture_flag_requires_three_core_clips_and_one_sidecar(
        self,
    ) -> None:
        metadata = _validated_w3d_metadata(
            mixed_capture_flag_report(),
            [],
            expected_animation_count=4,
            asset_kind="animated",
        )

        self.assertEqual(metadata["metrics"]["actionShapeExportedAnimationCount"], 3)
        self.assertEqual(metadata["metrics"]["visibilityOnlySidecarAnimationCount"], 1)
        self.assertEqual(metadata["splitActionAnimations"]["exportedAnimationCount"], 3)
        self.assertEqual(metadata["splitActionAnimations"]["exportedChannelCount"], 81)

        missing_transform = mixed_capture_flag_report()
        missing_transform["split_exported_animation_count"] = 2
        with self.assertRaisesRegex(RuntimeError, "proof is incomplete"):
            _validated_w3d_metadata(
                missing_transform,
                [],
                expected_animation_count=4,
                asset_kind="animated",
            )

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
                        "provenRootRigidBake": True,
                    },
                }
            ],
        }
        with tempfile.TemporaryDirectory() as raw:
            path = Path(raw) / "profile.json"
            path.write_text(json.dumps(payload), encoding="utf-8")
            profile = ImportProfile.load(path)

        self.assertEqual(profile.resources[0].converter, "w3d-hierarchical")
        self.assertTrue(profile.resources[0].options["provenRootRigidBake"])

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

        non_boolean_bake = copy.deepcopy(base)
        non_boolean_bake["resources"][0]["options"]["provenRootRigidBake"] = 1
        cases.append(("non-boolean-bake", non_boolean_bake, "must be a boolean"))

        wrong_converter = copy.deepcopy(base)
        wrong_converter["resources"][0]["converter"] = "w3d-static"
        wrong_converter["resources"][0]["options"]["provenRootRigidBake"] = True
        cases.append(
            (
                "wrong-converter-bake",
                wrong_converter,
                "without w3d-hierarchical",
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
                },
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
        non_w3d["resources"][0]["options"] = {"inputResourceIds": ["unit-model"]}
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
        duplicate_exclusion["resources"][1]["options"]["excludedOptionalMeshes"] = [
            "upgrade_banner",
            "upgrade_banner",
        ]
        cases.append(
            ("duplicate-exclusion", duplicate_exclusion, "contains duplicates")
        )

        unsafe_exclusion = copy.deepcopy(base)
        unsafe_exclusion["resources"][1]["options"]["excludedOptionalMeshes"] = [
            "upgrade_*"
        ]
        cases.append(
            ("unsafe-exclusion", unsafe_exclusion, "invalid clean mesh identifier")
        )

        too_many_exclusions = copy.deepcopy(base)
        too_many_exclusions["resources"][1]["options"]["excludedOptionalMeshes"] = [
            f"upgrade_{index}" for index in range(65)
        ]
        cases.append(("bounded-exclusions", too_many_exclusions, "array of at most 64"))

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

    def test_explicit_input_closure_isolates_duplicate_cross_unit_basenames(
        self,
    ) -> None:
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

    def test_shader_compatibility_report_is_validated_and_canonicalized(
        self,
    ) -> None:
        metadata = _validated_w3d_metadata(
            shader_compatibility_report(),
            [],
            expected_animation_count=0,
            asset_kind="static",
        )

        self.assertTrue(
            metadata["capabilities"]["sourceShaderBooleanSemanticsPreserved"]
        )
        self.assertEqual(
            metadata["shaderMaterialCompatibility"],
            {
                "mappedMaterials": [
                    {
                        "material": "Cloud",
                        "properties": {"FogEnable": False},
                    },
                    {
                        "material": "water",
                        "properties": {
                            "AlphaBlendingEnable": True,
                            "FogEnable": True,
                        },
                    },
                ],
                "mappedMaterialCount": 2,
                "mappedPropertyCount": 3,
                "alphaBlendingEnableCount": 1,
                "fogEnableCount": 2,
                "sourceFlagsPreserved": True,
            },
        )
        self.assertEqual(
            metadata["metrics"]["shaderCompatibilityMappedMaterialCount"],
            2,
        )
        self.assertEqual(
            metadata["metrics"]["shaderCompatibilityMappedPropertyCount"],
            3,
        )
        self.assertEqual(
            metadata["metrics"]["shaderCompatibilityAlphaBlendingEnableCount"],
            1,
        )
        self.assertEqual(
            metadata["metrics"]["shaderCompatibilityFogEnableCount"],
            2,
        )

    def test_shader_compatibility_report_fails_closed_when_malformed(
        self,
    ) -> None:
        cases: list[tuple[str, dict]] = []

        extra_schema_key = shader_compatibility_report()
        extra_schema_key["shader_material_compatibility"]["unexpected"] = 0
        cases.append(("extra-schema-key", extra_schema_key))

        wrong_row_schema = shader_compatibility_report()
        wrong_row_schema["shader_material_compatibility"]["mapped_materials"][0][
            "unexpected"
        ] = False
        cases.append(("wrong-row-schema", wrong_row_schema))

        unsupported_property = shader_compatibility_report()
        unsupported_property["shader_material_compatibility"]["mapped_materials"][0][
            "properties"
        ] = {"AlphaTestEnable": True}
        cases.append(("unsupported-property", unsupported_property))

        non_boolean_property = shader_compatibility_report()
        non_boolean_property["shader_material_compatibility"]["mapped_materials"][0][
            "properties"
        ]["FogEnable"] = 0
        cases.append(("non-boolean-property", non_boolean_property))

        non_canonical_order = shader_compatibility_report()
        non_canonical_order["shader_material_compatibility"][
            "mapped_materials"
        ].reverse()
        cases.append(("non-canonical-order", non_canonical_order))

        ambiguous_names = shader_compatibility_report()
        ambiguous_names["shader_material_compatibility"]["mapped_materials"][1][
            "material"
        ] = "CLOUD"
        cases.append(("ambiguous-names", ambiguous_names))

        unpreserved = shader_compatibility_report()
        unpreserved["shader_material_compatibility"]["source_flags_preserved"] = False
        cases.append(("source-flags-not-preserved", unpreserved))

        non_boolean_proof = shader_compatibility_report()
        non_boolean_proof["shader_material_compatibility"]["source_flags_preserved"] = 1
        cases.append(("non-boolean-proof", non_boolean_proof))

        exceeds_material_count = shader_compatibility_report()
        exceeds_material_count["materials"] = 1
        cases.append(("exceeds-material-count", exceeds_material_count))

        for count_name in (
            "mapped_material_count",
            "mapped_property_count",
            "alpha_blending_enable_count",
            "fog_enable_count",
        ):
            wrong_count = shader_compatibility_report()
            wrong_count["shader_material_compatibility"][count_name] += 1
            cases.append((f"wrong-{count_name}", wrong_count))

        for name, report in cases:
            with self.subTest(name=name):
                with self.assertRaisesRegex(RuntimeError, "shader compatibility"):
                    _validated_w3d_metadata(
                        report,
                        [],
                        expected_animation_count=0,
                        asset_kind="static",
                    )

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
            metadata["capabilities"]["declaredOptionalRenderSubobjectsExcluded"]
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

    def test_proven_root_rigid_report_accepts_no_exported_skeleton(self) -> None:
        metadata = _validated_w3d_metadata(
            root_rigid_report(),
            [],
            expected_animation_count=0,
            asset_kind="hierarchical",
            expected_proven_root_rigid_bake=True,
        )

        self.assertFalse(metadata["capabilities"]["skeletal"])
        self.assertTrue(metadata["capabilities"]["provenRootRigidBake"])
        self.assertEqual(metadata["metrics"]["boneCount"], 0)
        self.assertEqual(metadata["metrics"]["skeletonCount"], 0)
        self.assertEqual(metadata["rootRigidBake"]["bakedMeshCount"], 1)
        self.assertTrue(metadata["rootRigidBake"]["worldTransformsPreserved"])

    def test_root_rigid_report_requires_exact_profile_opt_in_and_proof(self) -> None:
        with self.assertRaisesRegex(RuntimeError, "does not match the profile"):
            _validated_w3d_metadata(root_rigid_report(), [], asset_kind="hierarchical")

        not_applied = root_rigid_report()
        not_applied["root_rigid_bake"].update(
            {
                "requested": False,
                "applied": False,
                "removed_carriers": 0,
                "baked_meshes": 0,
                "world_transforms_preserved": False,
                "deform_ambiguity_absent": False,
            }
        )
        with self.assertRaisesRegex(RuntimeError, "invalid bones"):
            _validated_w3d_metadata(not_applied, [], asset_kind="hierarchical")

        malformed = root_rigid_report()
        malformed["root_rigid_bake"]["world_transforms_preserved"] = False
        with self.assertRaisesRegex(RuntimeError, "proof is incomplete"):
            _validated_w3d_metadata(
                malformed,
                [],
                asset_kind="hierarchical",
                expected_proven_root_rigid_bake=True,
            )

        skeletal = root_rigid_report()
        skeletal["bones"] = 1
        with self.assertRaisesRegex(RuntimeError, "skeletal data"):
            _validated_w3d_metadata(
                skeletal,
                [],
                asset_kind="hierarchical",
                expected_proven_root_rigid_bake=True,
            )

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
            _validated_w3d_metadata(missing_rig, [], asset_kind="hierarchical")

        multiple_rigs = hierarchical_report()
        multiple_rigs["skeletons"] = 2
        with self.assertRaisesRegex(RuntimeError, "skeleton count"):
            _validated_w3d_metadata(multiple_rigs, [], asset_kind="hierarchical")

        empty_hierarchy = hierarchical_report()
        empty_hierarchy["bones"] = 0
        with self.assertRaisesRegex(RuntimeError, "invalid bones"):
            _validated_w3d_metadata(empty_hierarchy, [], asset_kind="hierarchical")

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

    def test_conversion_cache_key_covers_all_declared_inputs(self) -> None:
        inputs = {
            "source_hashes": {"model.w3d": "a" * 64, "idle.w3d": "b" * 64},
            "adapter_sha256": "c" * 64,
            "plugin_attestation_sha256": "d" * 64,
            "blender_tree_sha256": "e" * 64,
            "argument_vector": ["blender", "--asset-kind", "animated"],
        }
        baseline = _w3d_conversion_cache_key(**inputs)
        reordered = {**inputs, "source_hashes": dict(reversed(inputs["source_hashes"].items()))}
        self.assertEqual(baseline, _w3d_conversion_cache_key(**reordered))

        mutations = (
            {"source_hashes": {"model.w3d": "f" * 64, "idle.w3d": "b" * 64}},
            {"adapter_sha256": "f" * 64},
            {"plugin_attestation_sha256": "f" * 64},
            {"blender_tree_sha256": "f" * 64},
            {"argument_vector": ["blender", "--asset-kind", "static"]},
        )
        for mutation in mutations:
            with self.subTest(mutation=mutation):
                self.assertNotEqual(
                    baseline, _w3d_conversion_cache_key(**{**inputs, **mutation})
                )

    def test_conversion_cache_miss_populate_and_verified_hit(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            install = root / "install"
            install.mkdir()
            pipeline = ImportPipeline(InstallCatalog(install, (), ()), root / "state")
            key = "1" * 64
            target = root / "pack" / "model.glb"
            target.parent.mkdir()

            self.assertIsNone(pipeline._copy_w3d_cache_hit(key, target))
            target.write_bytes(b"retail-glb" * 256)
            log = 'OPENBFME_W3D_OK {"asset_kind":"static"}\n'
            pipeline._populate_w3d_cache(key, target, log)
            expected = target.read_bytes()
            target.unlink()

            self.assertEqual(pipeline._copy_w3d_cache_hit(key, target), log)
            self.assertEqual(target.read_bytes(), expected)
            self.assertEqual(
                pipeline.conversion_cache_stats,
                {"enabled": True, "jobs": pipeline.conversion_jobs, "hits": 1, "misses": 1, "populated": 1},
            )

            (pipeline.converted_cache_root / key / "output.glb").write_bytes(b"corrupt")
            target.unlink()
            self.assertIsNone(pipeline._copy_w3d_cache_hit(key, target))
            self.assertFalse(target.exists())


if __name__ == "__main__":
    unittest.main()
