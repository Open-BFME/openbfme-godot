from __future__ import annotations

import copy
from pathlib import Path
import struct
import tempfile
import unittest

from openbfme_importer.pipeline import _prepare_w3d_no_motion_animations
from openbfme_importer.profile import ImportProfile
from openbfme_importer.util import write_json_atomic
from openbfme_importer.w3d_metadata import scan_w3d_metadata


CONTAINER = 0x80000000


def _fixed(value: str, size: int = 16) -> bytes:
    encoded = value.encode("ascii")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(kind: int, payload: bytes, *, container: bool = False) -> bytes:
    return (
        struct.pack("<II", kind, len(payload) | (CONTAINER if container else 0))
        + payload
    )


def _model() -> bytes:
    hierarchy_header = struct.pack(
        "<I16sI3f", 0x00040001, _fixed("MODEL"), 1, 0.0, 0.0, 0.0
    )
    pivot = struct.pack("<16si10f", _fixed("ROOTTRANSFORM"), -1, *([0.0] * 9), 1.0)
    hierarchy = _chunk(
        0x00000100,
        _chunk(0x00000101, hierarchy_header) + _chunk(0x00000102, pivot),
        container=True,
    )
    animation_header = struct.pack(
        "<I16s16sII", 0x00040001, _fixed("MODEL"), _fixed("MODEL"), 101, 30
    )
    animation = _chunk(
        0x00000200,
        _chunk(0x00000201, animation_header),
        container=True,
    )
    mesh_header = struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        0,
        _fixed("MESH"),
        _fixed("MODEL"),
        0,
        0,
        0,
        0,
        0,
        0,
        0,
        3,
        1,
        *([0.0] * 10),
    )
    mesh = _chunk(0x00000000, _chunk(0x0000001F, mesh_header), container=True)
    hlod_header = struct.pack(
        "<II16s16s", 0x00010000, 1, _fixed("MODEL"), _fixed("MODEL")
    )
    lod = _chunk(0x00000703, struct.pack("<If", 1, 1.0)) + _chunk(
        0x00000704, struct.pack("<I32s", 0, _fixed("MODEL.MESH", 32))
    )
    hlod = _chunk(
        0x00000700,
        _chunk(0x00000701, hlod_header) + _chunk(0x00000702, lod, container=True),
        container=True,
    )
    return hierarchy + animation + mesh + hlod


def _profile() -> dict:
    return {
        "format": 1,
        "id": "no-motion-test",
        "pack": {"id": "no-motion-test-pack"},
        "resources": [
            {
                "id": "no-motion-model",
                "kind": "model",
                "patterns": ["art/w3d/model.w3d"],
                "converter": "w3d-hierarchical",
                "output": "assets/model.glb",
                "limit": 1,
                "expected_count": 1,
                "options": {
                    "model": "model.w3d",
                    "animations": [],
                    "required_equipment": [],
                    "provenRootRigidBake": True,
                    "provenNoMotionAnimations": [
                        {
                            "identifier": "MODEL",
                            "hierarchyIdentifier": "MODEL",
                            "frameCount": 101,
                            "frameRate": 30,
                            "compressed": False,
                            "modelIdentifier": "MODEL",
                        }
                    ],
                },
            }
        ],
    }


class W3DNoMotionPipelineTests(unittest.TestCase):
    def test_profile_accepts_only_exact_hierarchical_no_motion_contract(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            valid = root / "valid.json"
            write_json_atomic(valid, _profile())
            rule = ImportProfile.load(valid).resources[0]
            self.assertEqual(
                rule.options["provenNoMotionAnimations"][0]["frameCount"], 101
            )

            cases: list[tuple[str, dict, str]] = []
            wrong_converter = copy.deepcopy(_profile())
            wrong_converter["resources"][0]["converter"] = "w3d-bundle"
            cases.append(("converter", wrong_converter, "without w3d-hierarchical"))
            missing_field = copy.deepcopy(_profile())
            del missing_field["resources"][0]["options"]["provenNoMotionAnimations"][0][
                "frameRate"
            ]
            cases.append(("missing", missing_field, "unsupported fields"))
            bad_timing = copy.deepcopy(_profile())
            bad_timing["resources"][0]["options"]["provenNoMotionAnimations"][0][
                "frameCount"
            ] = 0
            cases.append(("timing", bad_timing, "positive integer"))
            raw_flavor = copy.deepcopy(_profile())
            raw_flavor["resources"][0]["options"]["provenNoMotionAnimations"][0][
                "flavor"
            ] = 0
            cases.append(("raw-flavor", raw_flavor, "valid only for compressed"))

            for name, payload, message in cases:
                with self.subTest(name=name):
                    path = root / f"{name}.json"
                    write_json_atomic(path, payload)
                    with self.assertRaisesRegex(ValueError, message):
                        ImportProfile.load(path)

    def test_job_local_preprocessor_changes_only_the_declared_model(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            model = root / "model.w3d"
            dependency = root / "texture.dds"
            model.write_bytes(_model())
            dependency.write_bytes(b"unchanged")
            declaration = _profile()["resources"][0]["options"][
                "provenNoMotionAnimations"
            ]

            proof = _prepare_w3d_no_motion_animations(
                {"model.w3d": model, "texture.dds": dependency},
                model,
                declaration,
            )

            self.assertIsNotNone(proof)
            assert proof is not None
            self.assertEqual(proof["removedContainerCount"], 1)
            self.assertEqual(proof["removedByteCount"], 60)
            self.assertEqual(dependency.read_bytes(), b"unchanged")
            self.assertEqual(
                scan_w3d_metadata(model.read_bytes(), model.name).animation_headers, ()
            )


if __name__ == "__main__":
    unittest.main()
