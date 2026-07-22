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


class FakeAnimationData:
    def __init__(self, action=None):
        self.action = action
        self.nla_tracks = FakeTrackCollection()


class FakeStripCollection(list):
    def new(self, name: str, start: int, action):
        strip = types.SimpleNamespace(name=name, start=start, action=action)
        self.append(strip)
        return strip


class FakeTrack:
    def __init__(self):
        self.name = ""
        self.strips = FakeStripCollection()


class FakeTrackCollection(list):
    def new(self):
        track = FakeTrack()
        self.append(track)
        return track


class FakeRig:
    def __init__(self, name: str):
        self.type = "ARMATURE"
        self.name = name
        self.animation_data = FakeAnimationData()
        self.data = types.SimpleNamespace(animation_data=FakeAnimationData())
        self.bones = [object()]

    def animation_data_create(self):
        if self.animation_data is None:
            self.animation_data = FakeAnimationData()


def nla_shape(name: str, owner) -> dict:
    action = Action(name, [Curve('pose.bones["ROOT"].location', [0.0, 2.0])])
    action.frame_range = (0.0, 1.0)
    return {
        "public": {
            "name": name,
            "shape": "transform-only",
            "action_count": 1,
            "object_action_count": 1,
            "armature_action_count": 0,
            "transform_curve_count": 1,
            "visibility_curve_count": 0,
            "material_curve_count": 0,
            "unsupported_curve_count": 0,
        },
        "object_action": action,
        "owner_rig": owner,
    }


class W3dAnimationOwnerRigTests(unittest.TestCase):
    def setUp(self) -> None:
        ADAPTER.bpy.data = types.SimpleNamespace(objects=[], actions=[])

    def test_find_model_rig_allows_rigless_animated_only(self) -> None:
        ADAPTER.bpy.data.objects = []
        self.assertIsNone(ADAPTER.find_model_rig("animated"))
        with self.assertRaisesRegex(RuntimeError, "found 0"):
            ADAPTER.find_model_rig("hierarchical")
        rig = FakeRig("RIG")
        ADAPTER.bpy.data.objects = [rig]
        self.assertIs(ADAPTER.find_model_rig("animated"), rig)
        ADAPTER.bpy.data.objects = [rig, FakeRig("AUX")]
        with self.assertRaisesRegex(RuntimeError, "found 2"):
            ADAPTER.find_model_rig("animated")

    def test_owner_rig_resolution_prefers_the_unique_owning_rig(self) -> None:
        model_rig = FakeRig("MODEL")
        aux_rig = FakeRig("AUX")
        ADAPTER.bpy.data.objects = [model_rig, aux_rig]
        clip = Action("CLIP", [])
        aux_rig.animation_data.action = clip

        self.assertIs(
            ADAPTER.find_animation_owner_rig(model_rig, [clip]), aux_rig
        )

        model_rig.animation_data.action = clip
        with self.assertRaisesRegex(RuntimeError, "ambiguous owner rigs"):
            ADAPTER.find_animation_owner_rig(model_rig, [clip])

        aux_rig.animation_data.action = None
        self.assertIs(
            ADAPTER.find_animation_owner_rig(model_rig, [clip]), model_rig
        )

        model_rig.animation_data.action = None
        with self.assertRaisesRegex(RuntimeError, "owned keyed action"):
            ADAPTER.find_animation_owner_rig(model_rig, [clip])
        with self.assertRaisesRegex(RuntimeError, "owned keyed action"):
            ADAPTER.find_animation_owner_rig(model_rig, [])

    def test_owner_rig_rejects_actions_outside_the_owner_set(self) -> None:
        model_rig = FakeRig("MODEL")
        aux_rig = FakeRig("AUX")
        ADAPTER.bpy.data.objects = [model_rig, aux_rig]
        owned = Action("OWNED", [])
        stray = Action("STRAY", [])
        aux_rig.animation_data.action = owned
        with self.assertRaisesRegex(RuntimeError, "outside its proven owner set"):
            ADAPTER.find_animation_owner_rig(model_rig, [owned, stray])

    def test_nla_tracks_are_created_per_owner_rig(self) -> None:
        model_rig = FakeRig("MODEL")
        aux_rig = FakeRig("AUX")
        shapes = [
            nla_shape("model_clip", model_rig),
            nla_shape("aux_clip", aux_rig),
        ]

        created = ADAPTER.prepare_w3d_animation_nla_tracks(model_rig, shapes)

        self.assertEqual(created, 2)
        self.assertEqual(len(model_rig.animation_data.nla_tracks), 1)
        self.assertEqual(model_rig.animation_data.nla_tracks[0].name, "model_clip")
        self.assertEqual(len(aux_rig.animation_data.nla_tracks), 1)
        self.assertEqual(aux_rig.animation_data.nla_tracks[0].name, "aux_clip")

    def test_nla_shapes_without_owner_stay_on_the_model_rig(self) -> None:
        model_rig = FakeRig("MODEL")
        shape = nla_shape("model_clip", model_rig)
        del shape["owner_rig"]

        created = ADAPTER.prepare_w3d_animation_nla_tracks(model_rig, [shape])

        self.assertEqual(created, 1)
        self.assertEqual(len(model_rig.animation_data.nla_tracks), 1)


class W3dSplitGlbSkeletalRelaxationTests(unittest.TestCase):
    def _rigid_animated_glb(self, *, mesh_under_armature: bool) -> dict:
        if mesh_under_armature:
            nodes = [
                {"name": "SKL", "children": [1, 2]},
                {"name": "BONE", "children": []},
                {"name": "DOOR", "mesh": 0},
            ]
        else:
            nodes = [
                {"name": "SKL", "children": [1]},
                {"name": "BONE", "children": []},
                {"name": "STATIC", "mesh": 0},
            ]
        return {
            "asset": {"version": "2.0"},
            "scene": 0,
            "scenes": [{"nodes": [0, 2]}],
            "nodes": nodes,
            "meshes": [{"primitives": []}],
            "animations": [
                {
                    "name": "clip",
                    "channels": [
                        {"sampler": 0, "target": {"node": 1, "path": "translation"}}
                    ],
                    "samplers": [{"input": 0, "output": 1}],
                }
            ],
        }

    def test_rigid_animated_glb_without_skins_validates(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            output = Path(raw) / "rigid.glb"
            output.write_bytes(
                glb_payload(self._rigid_animated_glb(mesh_under_armature=True))
            )
            with self.assertRaisesRegex(RuntimeError, "no skeletal skin"):
                ADAPTER.validate_split_animation_glb(output, ["clip"])

            result = ADAPTER.validate_split_animation_glb(
                output, ["clip"], require_skins=False
            )
            self.assertEqual(result["skins"], 0)
            self.assertEqual(result["skeletal_meshes"], 1)

    def test_detached_mesh_requires_skeletal_opt_out(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            output = Path(raw) / "rigless.glb"
            output.write_bytes(
                glb_payload(self._rigid_animated_glb(mesh_under_armature=False))
            )
            with self.assertRaisesRegex(RuntimeError, "no skinned or bone-parented"):
                ADAPTER.validate_split_animation_glb(
                    output, ["clip"], require_skins=False
                )
            result = ADAPTER.validate_split_animation_glb(
                output,
                ["clip"],
                require_skins=False,
                require_skeletal_mesh=False,
            )
            self.assertEqual(result["skeletal_meshes"], 0)


if __name__ == "__main__":
    unittest.main()
