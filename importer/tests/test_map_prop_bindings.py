from __future__ import annotations

import unittest

from openbfme_importer.map_prop_bindings import (
    NO_AUTHORED_MODEL_CLASSIFICATION,
    _default_lifecycle_visual_binding,
    _merge_stage_resources,
    partition_placement_types,
    pseudo_type_classification,
)


class _Parsed:
    def __init__(self, objects: list[dict[str, object]]) -> None:
        self.objects = objects


def _object(type_name: str, *, road: int = 0) -> dict[str, object]:
    return {"index": 0, "typeName": type_name, "roadType": road}


class PseudoTypeTests(unittest.TestCase):
    def test_logical_namespaces_derive_their_classification(self) -> None:
        self.assertEqual(
            pseudo_type_classification("*Waypoints/Waypoint"), "waypoint"
        )
        self.assertEqual(
            pseudo_type_classification("*GenericAIObjects/GenericAIObject"),
            "generic-ai-object",
        )

    def test_unclassifiable_logical_type_fails_closed(self) -> None:
        with self.assertRaises(ValueError):
            pseudo_type_classification("*//")


class PartitionTests(unittest.TestCase):
    def test_placements_split_into_visual_logical_and_unsafe(self) -> None:
        parsed = _Parsed(
            [
                _object("PTGrass15"),
                _object("PTGrass15"),
                _object("*Waypoints/Waypoint"),
                _object("Object With Spaces"),
                _object("RoadControlPoint", road=3),
            ]
        )
        targets, pseudo, unsafe = partition_placement_types(parsed)
        # Road control points are placements of a road, never props.
        self.assertEqual(targets, ["PTGrass15"])
        self.assertEqual(pseudo, ["*Waypoints/Waypoint"])
        self.assertEqual(unsafe, ["Object With Spaces"])


class StageMergeTests(unittest.TestCase):
    def test_a_texture_keeps_one_owner_and_later_stages_are_rewritten(self) -> None:
        owners: dict[str, str] = {}
        accumulated: list[dict[str, object]] = []
        static_stage = [
            {
                "id": "static-prop-texture-shadowi",
                "kind": "texture",
                "patterns": ["art/compiledtextures/sh/shadowi.tga"],
                "output": "assets/textures/props/shadowi.png",
            },
            {
                "id": "static-prop-model-rock",
                "kind": "model",
                "patterns": ["art/w3d/rock.w3d"],
                "output": "assets/models/props/rock.glb",
                "options": {"inputResourceIds": ["static-prop-texture-shadowi"]},
            },
        ]
        animated_stage = [
            {
                "id": "animated-prop-texture-shadowi",
                "kind": "texture",
                "patterns": ["art/compiledtextures/sh/shadowi.tga"],
                "output": "assets/textures/props-animated/shadowi.png",
            },
            {
                "id": "animated-prop-model-wolf",
                "kind": "model",
                "patterns": ["art/w3d/wolf.w3d"],
                "output": "assets/models/props-animated/wolf.glb",
                "options": {"inputResourceIds": ["animated-prop-texture-shadowi"]},
            },
        ]
        _merge_stage_resources(accumulated, owners, static_stage)
        _merge_stage_resources(accumulated, owners, animated_stage)

        ids = [str(item["id"]) for item in accumulated]
        self.assertEqual(
            ids,
            [
                "static-prop-texture-shadowi",
                "static-prop-model-rock",
                "animated-prop-model-wolf",
            ],
        )
        wolf = accumulated[-1]
        self.assertEqual(
            wolf["options"]["inputResourceIds"], ["static-prop-texture-shadowi"]
        )

    def test_owners_shared_across_maps_keep_one_definition_per_resource(self) -> None:
        # Two maps reach the same shared texture through different stages. With
        # one shared owner map the dependent model resource is byte-identical in
        # both, which is what lets a profile declare it once.
        owners: dict[str, str] = {}
        first: list[dict[str, object]] = []
        second: list[dict[str, object]] = []
        texture = {
            "id": "static-prop-texture-shadowi",
            "kind": "texture",
            "patterns": ["art/compiledtextures/sh/shadowi.tga"],
            "output": "assets/textures/props/shadowi.png",
        }
        animated_texture = {
            "id": "animated-prop-texture-shadowi",
            "kind": "texture",
            "patterns": ["art/compiledtextures/sh/shadowi.tga"],
            "output": "assets/textures/props-animated/shadowi.png",
        }
        wolf = {
            "id": "animated-prop-model-wolf",
            "kind": "model",
            "patterns": ["art/w3d/wolf.w3d"],
            "output": "assets/models/props-animated/wolf.glb",
            "options": {"inputResourceIds": ["animated-prop-texture-shadowi"]},
        }
        _merge_stage_resources(first, owners, [texture])
        _merge_stage_resources(first, owners, [animated_texture, dict(wolf)])
        _merge_stage_resources(second, owners, [animated_texture, dict(wolf)])

        first_wolf = next(
            item for item in first if item["id"] == "animated-prop-model-wolf"
        )
        second_wolf = next(
            item for item in second if item["id"] == "animated-prop-model-wolf"
        )
        self.assertEqual(first_wolf, second_wolf)
        self.assertEqual(
            second_wolf["options"]["inputResourceIds"],
            ["static-prop-texture-shadowi"],
        )


class ClassificationConstantTests(unittest.TestCase):
    def test_derived_non_renderable_classification_is_binding_safe(self) -> None:
        import re

        self.assertIsNotNone(
            re.fullmatch(
                r"[A-Za-z0-9][A-Za-z0-9._-]{0,127}",
                NO_AUTHORED_MODEL_CLASSIFICATION,
            )
        )


class LifecycleVisualBindingTests(unittest.TestCase):
    def test_unique_default_intact_model_becomes_visible_binding(self) -> None:
        row = _default_lifecycle_visual_binding(
            "WargLair",
            {
                "lifecycleStates": [
                    {
                        "phases": ["intact"],
                        "sourceConditionSets": [[]],
                        "sourceW3d": "art/w3d/nbwargpit_skn.w3d",
                        "output": "assets/models/structures/warglair/intact.glb",
                    },
                    {
                        "phases": ["rubble"],
                        "sourceConditionSets": [["RUBBLE"]],
                        "sourceW3d": "art/w3d/nbwargpit_d.w3d",
                        "output": "assets/models/structures/warglair/rubble.glb",
                    },
                ]
            },
        )
        self.assertEqual(row["typeName"], "WargLair")
        self.assertEqual(row["matchMethod"], "exact-type-name")

    def test_ambiguous_default_stays_fail_closed(self) -> None:
        state = {
            "phases": ["intact"],
            "sourceConditionSets": [[]],
            "sourceW3d": "art/w3d/a.w3d",
            "output": "assets/models/a.glb",
        }
        with self.assertRaisesRegex(Exception, "expected one default intact"):
            _default_lifecycle_visual_binding(
                "Ambiguous", {"lifecycleStates": [state, dict(state)]}
            )


if __name__ == "__main__":
    unittest.main()
