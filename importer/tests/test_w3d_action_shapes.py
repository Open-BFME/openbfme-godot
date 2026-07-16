from __future__ import annotations

import importlib.util
import json
from pathlib import Path
import struct
import sys
import tempfile
import types
import unittest


def load_adapter_module():
    previous = sys.modules.get("bpy")
    sys.modules["bpy"] = types.SimpleNamespace()
    try:
        path = Path(__file__).parents[1] / "blender" / "w3d_to_glb.py"
        spec = importlib.util.spec_from_file_location("w3d_action_shapes", path)
        if spec is None or spec.loader is None:
            raise RuntimeError("could not load W3D adapter")
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
    finally:
        if previous is None:
            sys.modules.pop("bpy", None)
        else:
            sys.modules["bpy"] = previous
    return module


ADAPTER = load_adapter_module()


class Point:
    def __init__(self, frame: float, value: float):
        self.co = (frame, value)
        self.interpolation = "LINEAR"


class Curve:
    def __init__(self, path: str, values: list[float], array_index: int = 0):
        self.data_path = path
        self.array_index = array_index
        self.keyframe_points = [
            Point(index, value) for index, value in enumerate(values)
        ]


class Action:
    def __init__(self, name: str, curves: list[Curve]):
        self.name = name
        self.fcurves = curves
        self.use_fake_user = False


def rig_with_actions(object_action=None, armature_action=None):
    return types.SimpleNamespace(
        animation_data=types.SimpleNamespace(action=object_action),
        data=types.SimpleNamespace(
            animation_data=types.SimpleNamespace(action=armature_action)
        ),
    )


def visibility_channel() -> dict:
    return {
        "owner": "armature",
        "data_path": 'bones["FLAG"].visibility',
        "array_index": 0,
        "keys": [{"frame": 0.0, "value": 1.0, "interpolation": "LINEAR"}],
    }


def glb_payload(document: dict) -> bytes:
    encoded = json.dumps(document, separators=(",", ":")).encode("utf-8")
    encoded += b" " * ((-len(encoded)) % 4)
    body = struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
    return struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body


def glb_document(payload: bytes) -> dict:
    json_length = struct.unpack_from("<I", payload, 12)[0]
    return json.loads(payload[20 : 20 + json_length])


def visibility_only_shape(name: str = "closed") -> dict:
    return {
        "public": {
            "name": name,
            "shape": "visibility-only",
            "action_count": 2,
            "object_action_count": 1,
            "armature_action_count": 1,
            "transform_curve_count": 0,
            "visibility_curve_count": 1,
            "material_curve_count": 0,
            "unsupported_curve_count": 0,
        },
        "semantic_fingerprint": "visibility-only-source-proof",
        "visibility_channels": [visibility_channel()],
        "object_action": None,
    }


class W3dActionShapeTests(unittest.TestCase):
    def test_transform_only_and_full_pair_are_distinct_typed_shapes(self) -> None:
        transform = Action(
            "MOVE",
            [Curve('pose.bones["ROOT"].location', [0.0, 2.0])],
        )
        captured, shape = ADAPTER.capture_w3d_animation_actions(
            rig_with_actions(transform), [transform], "move"
        )
        self.assertEqual(captured, [transform])
        self.assertEqual(shape["public"]["shape"], "transform-only")
        self.assertEqual(shape["public"]["action_count"], 1)

        visibility = Action(
            "VIS", [Curve('bones["ROOT"].visibility', [1.0])]
        )
        captured, shape = ADAPTER.capture_w3d_animation_actions(
            rig_with_actions(transform, visibility),
            [transform, visibility],
            "move-visible",
        )
        self.assertEqual(len(captured), 2)
        self.assertEqual(shape["public"]["shape"], "transform-and-visibility")
        self.assertEqual(shape["public"]["visibility_curve_count"], 1)
        self.assertEqual(len(shape["visibility_channels"]), 1)

    def test_semantic_fingerprint_includes_visibility_values_not_action_names(
        self,
    ) -> None:
        def evidence(name: str, visible: float):
            transform = Action(
                name,
                [Curve('pose.bones["ROOT"].location', [0.0, 2.0])],
            )
            visibility = Action(
                name + "_VIS",
                [Curve('bones["ROOT"].visibility', [visible])],
            )
            return ADAPTER.capture_w3d_animation_actions(
                rig_with_actions(transform, visibility),
                [transform, visibility],
                name,
            )[1]

        self.assertEqual(
            evidence("DOWN", 1.0)["semantic_fingerprint"],
            evidence("STAY_DOWN", 1.0)["semantic_fingerprint"],
        )
        self.assertNotEqual(
            evidence("DOWN", 1.0)["semantic_fingerprint"],
            evidence("HIDDEN", 0.0)["semantic_fingerprint"],
        )

    def test_duplicate_logical_clip_requires_full_semantic_equivalence(self) -> None:
        document = {
            "asset": {"version": "2.0"},
            "animations": [{"name": "down", "channels": [], "samplers": []}],
        }
        encoded = json.dumps(document, separators=(",", ":")).encode("utf-8")
        encoded += b" " * ((-len(encoded)) % 4)
        body = struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
        payload = struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body
        base_shape = {
            "public": {
                "name": "down",
                "transform_curve_count": 1,
                "shape": "transform-and-visibility",
            },
            "semantic_fingerprint": "same",
            "visibility_channels": [visibility_channel()],
        }
        duplicate_shape = {
            "public": {
                "name": "stay-down",
                "transform_curve_count": 1,
                "shape": "transform-and-visibility",
            },
            "semantic_fingerprint": "same",
            "visibility_channels": [visibility_channel()],
        }
        with tempfile.TemporaryDirectory() as raw:
            output = Path(raw) / "flag.glb"
            output.write_bytes(payload)
            restored = ADAPTER.restore_duplicate_logical_animations(
                output, [base_shape, duplicate_shape]
            )
            rewritten = output.read_bytes()
        json_length = struct.unpack_from("<I", rewritten, 12)[0]
        result = json.loads(rewritten[20 : 20 + json_length])
        self.assertEqual(restored["duplicated_animations"], 1)
        self.assertEqual(restored["visibility_channels"], 2)
        self.assertEqual(
            [animation["name"] for animation in result["animations"]],
            ["down", "stay-down"],
        )
        self.assertEqual(
            result["animations"][1]["extras"]["openbfme_w3d_visibility"],
            result["animations"][0]["extras"]["openbfme_w3d_visibility"],
        )

        mismatched = dict(duplicate_shape)
        mismatched["semantic_fingerprint"] = "different"
        with tempfile.TemporaryDirectory() as raw:
            output = Path(raw) / "flag.glb"
            output.write_bytes(payload)
            with self.assertRaisesRegex(RuntimeError, "exact exported duplicate"):
                ADAPTER.restore_duplicate_logical_animations(
                    output, [base_shape, mismatched]
                )

    def test_visibility_only_source_preserves_static_geometry_without_motion(
        self,
    ) -> None:
        geometry = {
            "asset": {"version": "2.0"},
            "meshes": [{"name": "door-left"}, {"name": "door-right"}],
            "nodes": [
                {"name": "door-root", "children": [1]},
                {"name": "door-mesh", "mesh": 0, "skin": 0},
            ],
            "skins": [{"joints": [0]}],
            "buffers": [{"byteLength": 16}],
            "bufferViews": [{"buffer": 0, "byteLength": 16}],
        }
        with tempfile.TemporaryDirectory() as raw:
            output = Path(raw) / "closed.glb"
            output.write_bytes(glb_payload(geometry))
            restored = ADAPTER.restore_duplicate_logical_animations(
                output, [visibility_only_shape()]
            )
            result = glb_document(output.read_bytes())
            validated = ADAPTER.validate_split_animation_glb(output, [])

        self.assertNotIn("animations", result)
        for key in ("meshes", "nodes", "skins", "buffers", "bufferViews"):
            self.assertEqual(result[key], geometry[key])
        self.assertEqual(restored["duplicated_animations"], 0)
        self.assertEqual(restored["visibility_channels"], 1)
        self.assertEqual(restored["visibility_keys"], 1)
        self.assertEqual(restored["visibility_only_animations"], 1)
        self.assertEqual(validated["animations"], 0)
        self.assertEqual(validated["channels"], 0)
        self.assertEqual(validated["samplers"], 0)
        self.assertEqual(validated["skins"], 1)
        self.assertEqual(validated["skeletal_meshes"], 1)
        self.assertEqual(validated["visibility_only_animations"], 1)

    def test_missing_required_transform_animation_still_fails_closed(self) -> None:
        transform_shape = visibility_only_shape("closing")
        transform_shape["public"] = {
            **transform_shape["public"],
            "shape": "transform-and-visibility",
            "transform_curve_count": 1,
        }
        with tempfile.TemporaryDirectory() as raw:
            output = Path(raw) / "closing.glb"
            output.write_bytes(glb_payload({"asset": {"version": "2.0"}}))
            with self.assertRaisesRegex(
                RuntimeError, "no required transform animations"
            ):
                ADAPTER.restore_duplicate_logical_animations(
                    output, [transform_shape]
                )

    def test_unsealed_empty_animation_output_is_rejected(self) -> None:
        malformed_shape = visibility_only_shape()
        malformed_shape["public"]["visibility_curve_count"] = 2
        with tempfile.TemporaryDirectory() as raw:
            output = Path(raw) / "closed.glb"
            output.write_bytes(glb_payload({"asset": {"version": "2.0"}}))
            with self.assertRaisesRegex(RuntimeError, "sealed visibility-only"):
                ADAPTER.restore_duplicate_logical_animations(
                    output, [malformed_shape]
                )

    def test_empty_action_shape_set_cannot_prove_static_output(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            output = Path(raw) / "closed.glb"
            output.write_bytes(glb_payload({"asset": {"version": "2.0"}}))
            with self.assertRaisesRegex(RuntimeError, "sealed source action proof"):
                ADAPTER.restore_duplicate_logical_animations(output, [])


if __name__ == "__main__":
    unittest.main()
