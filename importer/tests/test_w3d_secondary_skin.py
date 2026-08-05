from __future__ import annotations

from dataclasses import FrozenInstanceError
import hashlib
import json
import math
import struct
import unittest

from openbfme_importer.w3d_secondary_skin import (
    NORMAL_COINCIDENCE_TOLERANCE,
    POSITION_COINCIDENCE_TOLERANCE,
    W3DSecondarySkinError,
    strip_proven_redundant_secondary_skin_streams,
)


CONTAINER = 0x80000000
MESH = 0x00000000
VERTICES = 0x00000002
NORMALS = 0x00000003
INFLUENCES = 0x0000000E
MESH_HEADER = 0x0000001F
HIERARCHY = 0x00000100
HIERARCHY_HEADER = 0x00000101
PIVOTS = 0x00000102
PIVOT_FIXUPS = 0x00000103
HLOD = 0x00000700
HLOD_HEADER = 0x00000701
HLOD_LOD_ARRAY = 0x00000702
HLOD_LOD_ARRAY_HEADER = 0x00000703
HLOD_SUB_OBJECT = 0x00000704
VERTICES_2 = 0x00000C00
NORMALS_2 = 0x00000C01
VERTEX_MATERIALS = 0x0000002A
VERTEX_MATERIAL = 0x0000002B
VERTEX_MATERIAL_NAME = 0x0000002C
VERTEX_MATERIAL_INFO = 0x0000002D
VERTEX_MAPPER_ARGS0 = 0x0000002E
VERTEX_MAPPER_ARGS1 = 0x0000002F

# Byte-for-byte from retail RotWK art/w3d/gu/guarcher_skn.w3d (the model behind
# GondorArcherHorde). Its VERTEX_MAPPER_ARGS0 header is raw size 0x800000AF:
# a 175-byte NUL-terminated argument string that nonetheless carries the
# 0x80000000 "has sub-chunks" bit. Its ARGS1 sibling holds the same shape of
# payload without the bit. Descending into the string reads "FPS=" as chunk id
# 0x3D535046 and the parse aborts.
RETAIL_MAPPER_ARGS0_PAYLOAD = (
    b"FPS=15.0;Theframespersecond\r\n"
    b"Log2Width=2;So0=width1\r\n1=width2\r\n2=width4."
    b"Thedefaultmeansanimateusingatexturedividedupintoquarters.\r\n"
    b"Last=;GridWidth*GridWidth;Thelastframetouse\x00"
)
RETAIL_MAPPER_ARGS1_PAYLOAD = (
    b"FPS=30.0;Theframespersecond\r\n"
    b"Log2Width=2;So0=width1\r\n1=width2\r\n2=width4."
    b"Thedefaultmeansanimateusingatexturedividedupintoquarters.\r\n"
    b"Last=GridWidth*GridWidth;Thelastframetouse\x00"
)


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("ascii")
    if len(encoded) > size:
        raise ValueError(value)
    return encoded + b"\x00" * (size - len(encoded))


def _chunk(kind: int, payload: bytes, *, container: bool = False) -> bytes:
    raw_size = len(payload) | (CONTAINER if container else 0)
    return struct.pack("<II", kind, raw_size) + payload


def _pivot(
    name: str,
    parent: int,
    translation: tuple[float, float, float],
) -> bytes:
    return struct.pack(
        "<16si10f",
        _fixed(name, 16),
        parent,
        *translation,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        0.0,
        1.0,
    )


def _hierarchy(
    *,
    identity: str = "TEST_SKL",
    bad_parent: bool = False,
    include_pivot_fixups: bool = False,
    pivot_fixup_values: tuple[float, ...] | None = None,
) -> bytes:
    pivots = b"".join(
        (
            _pivot("ROOTTRANSFORM", -1, (0.0, 0.0, 0.0)),
            _pivot("BONE1", 0, (1.0, 0.0, 0.0)),
            _pivot("BONE2", 2 if bad_parent else 0, (0.0, 2.0, 0.0)),
        )
    )
    header = struct.pack(
        "<I16sI3f",
        0x00040001,
        _fixed(identity, 16),
        3,
        0.0,
        0.0,
        0.0,
    )
    children = _chunk(HIERARCHY_HEADER, header) + _chunk(PIVOTS, pivots)
    if include_pivot_fixups:
        values = pivot_fixup_values or tuple([0.0] * 36)
        children += _chunk(PIVOT_FIXUPS, struct.pack(f"<{len(values)}f", *values))
    return _chunk(HIERARCHY, children, container=True)


def _mesh_header(
    name: str,
    vertex_count: int,
    *,
    attributes: int = 0x00020000,
) -> bytes:
    return struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        attributes,
        _fixed(name, 16),
        _fixed("TEST_SKIN", 16),
        0,
        vertex_count,
        0,
        0,
        0,
        0,
        0,
        0x13,
        1,
        *([0.0] * 10),
    )


def _target_mesh(
    *,
    name: str = "DUAL",
    primary_vertices: tuple[tuple[float, float, float], ...] = (
        (1.0, 3.0, 4.0),
        (2.0, 5.0, 7.0),
    ),
    secondary_vertices: tuple[tuple[float, float, float], ...] = (
        (1.0, 3.0, 4.0),
        (3.0, 3.0, 7.0),
    ),
    primary_normals: tuple[tuple[float, float, float], ...] = (
        (0.0, 0.0, 1.0),
        (0.0, 1.0, 0.0),
    ),
    secondary_normals: tuple[tuple[float, float, float], ...] = (
        (0.0, 0.0, 1.0),
        (0.0, 1.0, 0.0),
    ),
    influences: tuple[tuple[int, int, int, int], ...] = (
        (1, 0, 100, 0),
        (1, 2, 60, 40),
    ),
    include_vertices_2: bool = True,
    include_normals_2: bool = True,
    duplicate_vertices_2: bool = False,
    attributes: int = 0x00020000,
    extra_children: bytes = b"",
) -> bytes:
    vertex_count = len(primary_vertices)

    def vec3(values: tuple[tuple[float, float, float], ...]) -> bytes:
        return b"".join(struct.pack("<3f", *value) for value in values)

    children = [
        _chunk(MESH_HEADER, _mesh_header(name, vertex_count, attributes=attributes)),
        _chunk(VERTICES, vec3(primary_vertices)),
    ]
    if include_vertices_2:
        children.append(_chunk(VERTICES_2, vec3(secondary_vertices)))
        if duplicate_vertices_2:
            children.append(_chunk(VERTICES_2, vec3(secondary_vertices)))
    children.append(_chunk(NORMALS, vec3(primary_normals)))
    if include_normals_2:
        children.append(_chunk(NORMALS_2, vec3(secondary_normals)))
    children.extend(
        (
            _chunk(0x0000000C, b"fixture\x00"),
            _chunk(
                INFLUENCES,
                b"".join(struct.pack("<4H", *value) for value in influences),
            ),
            _chunk(0x00000022, b"\x00" * (vertex_count * 4)),
        )
    )
    return _chunk(MESH, b"".join(children) + extra_children, container=True)


def _retail_vertex_materials() -> bytes:
    """Reproduce the retail VERTEX_MATERIALS subtree of guarcher_skn.w3d."""

    material = _chunk(
        VERTEX_MATERIAL,
        b"".join(
            (
                _chunk(VERTEX_MATERIAL_NAME, b"Material #25\x00"),
                _chunk(VERTEX_MATERIAL_INFO, b"\x00" * 32),
                # Retail sets the container bit on this string leaf.
                _chunk(VERTEX_MAPPER_ARGS0, RETAIL_MAPPER_ARGS0_PAYLOAD, container=True),
                _chunk(VERTEX_MAPPER_ARGS1, RETAIL_MAPPER_ARGS1_PAYLOAD),
            )
        ),
        container=True,
    )
    return _chunk(VERTEX_MATERIALS, material, container=True)


def _plain_mesh() -> bytes:
    return _chunk(
        MESH,
        _chunk(MESH_HEADER, _mesh_header("PLAIN", 0))
        + _chunk(VERTICES, b"")
        + _chunk(NORMALS, b""),
        container=True,
    )


def _hlod(
    identifiers: tuple[str, ...],
    *,
    hierarchy_identity: str = "TEST_SKL",
) -> bytes:
    header = struct.pack(
        "<II16s16s",
        0x00010000,
        1,
        _fixed("TEST_SKIN", 16),
        _fixed(hierarchy_identity, 16),
    )

    def sub_object(identifier: str) -> bytes:
        # Retail HLOD fixed strings may contain unread nonzero bytes after the
        # first NUL terminator. Keep that source-proven padding case covered.
        raw_identifier = bytearray(_fixed(identifier, 32))
        if identifier.endswith(".DUAL"):
            raw_identifier[-4:] = b"\xab\x00\x00\x01"
        return _chunk(
            HLOD_SUB_OBJECT,
            struct.pack("<I32s", 0, bytes(raw_identifier)),
        )

    array_payload = _chunk(
        HLOD_LOD_ARRAY_HEADER,
        struct.pack("<If", len(identifiers), 1.0),
    ) + b"".join(sub_object(identifier) for identifier in identifiers)
    return _chunk(
        HLOD,
        _chunk(HLOD_HEADER, header)
        + _chunk(HLOD_LOD_ARRAY, array_payload, container=True),
        container=True,
    )


def _model(
    target: bytes | None = None,
    *,
    hierarchy_identity: str = "TEST_SKL",
) -> bytes:
    target = _target_mesh() if target is None else target
    return b"".join(
        (
            _chunk(0x00000740, b"preserved-top-level"),
            _plain_mesh(),
            target,
            _hlod(
                ("TEST_SKIN.PLAIN", "TEST_SKIN.DUAL"),
                hierarchy_identity=hierarchy_identity,
            ),
        )
    )


def _top_chunks(source: bytes) -> list[tuple[int, int, int, bool]]:
    result = []
    cursor = 0
    while cursor < len(source):
        kind, raw_size = struct.unpack_from("<II", source, cursor)
        end = cursor + 8 + (raw_size & 0x7FFFFFFF)
        result.append((kind, cursor, end, bool(raw_size & CONTAINER)))
        cursor = end
    if cursor != len(source):
        raise AssertionError("bad fixture")
    return result


def _mesh_child_bytes(source: bytes, mesh_ordinal: int) -> list[tuple[int, bytes]]:
    meshes = [item for item in _top_chunks(source) if item[0] == MESH]
    _, start, end, is_container = meshes[mesh_ordinal]
    if not is_container:
        raise AssertionError("bad fixture")
    result = []
    cursor = start + 8
    while cursor < end:
        kind, raw_size = struct.unpack_from("<II", source, cursor)
        child_end = cursor + 8 + (raw_size & 0x7FFFFFFF)
        result.append((kind, source[cursor:child_end]))
        cursor = child_end
    return result


class W3DSecondarySkinSuccessTests(unittest.TestCase):
    def test_exact_proof_removes_only_secondary_children_and_repairs_size(self) -> None:
        model = _model()
        hierarchy = _hierarchy()
        before_top = _top_chunks(model)
        before_children = _mesh_child_bytes(model, 1)

        result = strip_proven_redundant_secondary_skin_streams(model, hierarchy)
        output = result.model_bytes()
        proof = result.proof

        removed = [
            item for item in before_children if item[0] in {VERTICES_2, NORMALS_2}
        ]
        removed_size = sum(len(item[1]) for item in removed)
        self.assertEqual(len(model) - len(output), removed_size)
        self.assertEqual(proof.removed_byte_count, removed_size)
        self.assertEqual(proof.removed_chunk_count, 2)
        self.assertEqual(proof.transformed_mesh_count, 1)
        self.assertEqual(proof.mesh_count, 2)
        self.assertEqual(proof.hierarchy_pivot_count, 3)
        self.assertEqual(proof.output_model_sha256, hashlib.sha256(output).hexdigest())
        self.assertEqual(proof.input_model_sha256, hashlib.sha256(model).hexdigest())
        self.assertEqual(proof.hierarchy_sha256, hashlib.sha256(hierarchy).hexdigest())
        self.assertLessEqual(
            proof.maximum_position_delta,
            POSITION_COINCIDENCE_TOLERANCE,
        )
        self.assertLessEqual(
            proof.maximum_normal_delta,
            NORMAL_COINCIDENCE_TOLERANCE,
        )
        self.assertEqual(proof.meshes[0].active_dual_vertex_count, 1)
        self.assertEqual(proof.meshes[0].inactive_secondary_vertex_count, 1)

        after_top = _top_chunks(output)
        self.assertEqual(
            [item[0] for item in before_top], [item[0] for item in after_top]
        )
        self.assertEqual(
            model[before_top[0][1] : before_top[0][2]],
            output[after_top[0][1] : after_top[0][2]],
        )
        self.assertEqual(
            model[before_top[1][1] : before_top[1][2]],
            output[after_top[1][1] : after_top[1][2]],
        )
        self.assertEqual(
            model[before_top[3][1] : before_top[3][2]],
            output[after_top[3][1] : after_top[3][2]],
        )
        retained_before = [
            item for item in before_children if item[0] not in {VERTICES_2, NORMALS_2}
        ]
        self.assertEqual(_mesh_child_bytes(output, 1), retained_before)

    def test_retail_mapper_args_string_leaf_is_not_walked_as_a_container(
        self,
    ) -> None:
        # Regression: RotWK guarcher_skn.w3d (GondorArcherHorde) authors
        # VERTEX_MAPPER_ARGS0 as a NUL-terminated string with the 0x80000000
        # "has sub-chunks" bit set. Recursing into it read the literal "FPS="
        # as chunk 0x3D535046 and failed the whole secondary-skin proof, which
        # dropped the unit from the men pack.
        model = _model(_target_mesh(extra_children=_retail_vertex_materials()))
        hierarchy = _hierarchy()

        result = strip_proven_redundant_secondary_skin_streams(model, hierarchy)
        output = result.model_bytes()

        # Both mapper-arg chunks survive verbatim, header bits included.
        self.assertIn(
            _chunk(VERTEX_MAPPER_ARGS0, RETAIL_MAPPER_ARGS0_PAYLOAD, container=True),
            output,
        )
        self.assertIn(
            _chunk(VERTEX_MAPPER_ARGS1, RETAIL_MAPPER_ARGS1_PAYLOAD), output
        )
        # ...and the secondary streams were still found and removed.
        self.assertEqual(result.proof.transformed_mesh_count, 1)
        self.assertEqual(result.proof.removed_chunk_count, 2)
        kinds = [kind for kind, _ in _mesh_child_bytes(output, 1)]
        self.assertNotIn(VERTICES_2, kinds)
        self.assertNotIn(NORMALS_2, kinds)
        self.assertIn(VERTEX_MATERIALS, kinds)

    def test_output_and_payload_free_proof_are_deterministic_and_immutable(
        self,
    ) -> None:
        model = _model()
        hierarchy = _hierarchy()

        first = strip_proven_redundant_secondary_skin_streams(model, hierarchy)
        second = strip_proven_redundant_secondary_skin_streams(model, hierarchy)

        self.assertEqual(first.model_bytes(), second.model_bytes())
        self.assertEqual(first.proof.neutral(), second.proof.neutral())
        neutral = first.proof.neutral()
        json.dumps(neutral, sort_keys=True)
        serialized = json.dumps(neutral, sort_keys=True)
        self.assertNotIn("TEST_SKIN", serialized)
        self.assertNotIn("TEST_SKL", serialized)
        self.assertNotIn("modelBytes", serialized)
        self.assertRegex(first.proof.proof_sha256, r"^[0-9a-f]{64}$")
        with self.assertRaises(FrozenInstanceError):
            first.proof.removed_chunk_count = 0  # type: ignore[misc]

    def test_fixed_tolerances_accept_only_narrow_float32_roundoff(self) -> None:
        within_position = (
            (1.0, 3.0, 4.0),
            (3.0 + POSITION_COINCIDENCE_TOLERANCE * 0.25, 3.0, 7.0),
        )
        within_normal = (
            (0.0, 0.0, 1.0),
            (NORMAL_COINCIDENCE_TOLERANCE * 0.25, 1.0, 0.0),
        )
        result = strip_proven_redundant_secondary_skin_streams(
            _model(
                _target_mesh(
                    secondary_vertices=within_position,
                    secondary_normals=within_normal,
                )
            ),
            _hierarchy(),
        )

        self.assertGreater(result.proof.maximum_position_delta, 0.0)
        self.assertGreater(result.proof.maximum_normal_delta, 0.0)

    def test_reskin_authoring_normal_drift_within_tolerance_still_proves(
        self,
    ) -> None:
        """RUArcher_SKN / GUArcher_SKL dual-local normals drift ~1.46e-3.

        Positions stay dual-local; only unit-normal authoring variance grows.
        0.01-scale fixtures remain rejected as genuinely distinct streams.
        """

        # Secondary normal in bone-2 local space that reconstructs to a bind
        # normal within the measured reskin band but above the old 3e-6 floor.
        reskin_normal_delta = 1.46e-3
        result = strip_proven_redundant_secondary_skin_streams(
            _model(
                _target_mesh(
                    secondary_normals=(
                        (0.0, 0.0, 1.0),
                        (reskin_normal_delta, 1.0, 0.0),
                    ),
                )
            ),
            _hierarchy(),
        )
        self.assertGreater(result.proof.maximum_normal_delta, 3.0e-6)
        self.assertLessEqual(
            result.proof.maximum_normal_delta, NORMAL_COINCIDENCE_TOLERANCE
        )

        with self.assertRaisesRegex(W3DSecondarySkinError, "bind normal delta"):
            strip_proven_redundant_secondary_skin_streams(
                _model(
                    _target_mesh(
                        secondary_normals=((0.0, 0.0, 1.0), (0.01, 1.0, 0.0)),
                    )
                ),
                _hierarchy(),
            )

    def test_magnitude_relative_roundoff_bounds_follow_bind_coordinate_scale(
        self,
    ) -> None:
        # The active dual vertex sits at |bind| ≈ 116; the secondary local
        # copy compensates the (1,-2,0) bone offset, so bind-space copies
        # coincide up to the displacement. The float32 authoring round trip
        # may exceed the absolute floor at this scale; deltas far beyond the
        # relative bound still fail closed.
        result = strip_proven_redundant_secondary_skin_streams(
            _model(
                _target_mesh(
                    primary_vertices=((1.0, 3.0, 4.0), (100.0, 50.0, 30.0)),
                    secondary_vertices=(
                        (3.0, 3.0, 7.0),
                        (101.0 + POSITION_COINCIDENCE_TOLERANCE * 5.0, 48.0, 30.0),
                    ),
                )
            ),
            _hierarchy(),
        )
        self.assertGreater(
            result.proof.maximum_position_delta, POSITION_COINCIDENCE_TOLERANCE
        )

        with self.assertRaisesRegex(W3DSecondarySkinError, "bind position delta"):
            strip_proven_redundant_secondary_skin_streams(
                _model(
                    _target_mesh(
                        primary_vertices=((1.0, 3.0, 4.0), (100.0, 50.0, 30.0)),
                        secondary_vertices=((3.0, 3.0, 7.0), (102.0, 48.0, 30.0)),
                    )
                ),
                _hierarchy(),
            )

    def test_zero_weight_secondary_records_are_finite_but_otherwise_irrelevant(
        self,
    ) -> None:
        result = strip_proven_redundant_secondary_skin_streams(
            _model(
                _target_mesh(
                    secondary_vertices=((125.0, -9.0, 0.25), (3.0, 3.0, 7.0)),
                    secondary_normals=((-0.75, 0.5, 0.25), (0.0, 1.0, 0.0)),
                )
            ),
            _hierarchy(),
        )

        self.assertEqual(result.proof.meshes[0].inactive_secondary_vertex_count, 1)
        self.assertEqual(result.proof.meshes[0].active_dual_vertex_count, 1)


class W3DSecondarySkinFailureTests(unittest.TestCase):
    def test_stream_structure_and_semantics_fail_closed(self) -> None:
        nan_vertices = ((1.0, 3.0, 4.0), (math.nan, 3.0, 7.0))
        cases = (
            (
                "unpaired-vertices",
                _target_mesh(include_normals_2=False),
                "unpaired secondary stream",
            ),
            (
                "unpaired-normals",
                _target_mesh(include_vertices_2=False),
                "unpaired secondary stream",
            ),
            (
                "duplicate",
                _target_mesh(duplicate_vertices_2=True),
                "duplicates chunk 0x00000C00",
            ),
            (
                "cardinality",
                _target_mesh(secondary_vertices=((1.0, 3.0, 4.0),)),
                "secondary vertices cardinality",
            ),
            (
                "active-position",
                _target_mesh(secondary_vertices=((1.0, 3.0, 4.0), (3.01, 3.0, 7.0))),
                "bind position delta",
            ),
            (
                "active-normal",
                _target_mesh(secondary_normals=((0.0, 0.0, 1.0), (0.01, 1.0, 0.0))),
                "bind normal delta",
            ),
            (
                "bad-pivot",
                _target_mesh(influences=((1, 0, 100, 0), (1, 9, 60, 40))),
                "invalid pivot",
            ),
            (
                "bad-weight-sum",
                _target_mesh(influences=((1, 0, 100, 0), (1, 2, 50, 40))),
                "weights do not sum to 100",
            ),
            (
                "active-secondary-root",
                _target_mesh(influences=((1, 0, 100, 0), (1, 0, 60, 40))),
                "bind position delta",
            ),
            (
                "active-primary-root",
                _target_mesh(influences=((1, 0, 100, 0), (0, 2, 60, 40))),
                "bind position delta",
            ),
            (
                "non-finite",
                _target_mesh(secondary_vertices=nan_vertices),
                "non-finite float",
            ),
            (
                "not-skin",
                _target_mesh(attributes=0),
                "not on a skin",
            ),
        )
        for label, target, error in cases:
            with self.subTest(label=label):
                with self.assertRaisesRegex(W3DSecondarySkinError, error):
                    strip_proven_redundant_secondary_skin_streams(
                        _model(target),
                        _hierarchy(),
                    )

    def test_hierarchy_and_hlod_binding_fail_closed(self) -> None:
        with self.assertRaisesRegex(W3DSecondarySkinError, "identities disagree"):
            strip_proven_redundant_secondary_skin_streams(
                _model(hierarchy_identity="OTHER_SKL"),
                _hierarchy(),
            )
        with self.assertRaisesRegex(W3DSecondarySkinError, "invalid parent"):
            strip_proven_redundant_secondary_skin_streams(
                _model(),
                _hierarchy(bad_parent=True),
            )
        fixed = strip_proven_redundant_secondary_skin_streams(
            _model(),
            _hierarchy(include_pivot_fixups=True),
        )
        self.assertEqual(fixed.proof.hierarchy_pivot_count, 3)
        self.assertRegex(fixed.proof.hierarchy_sha256, r"^[0-9a-f]{64}$")
        with self.assertRaisesRegex(W3DSecondarySkinError, "not bound by"):
            model = _model().replace(b"TEST_SKIN.DUAL", b"TEST_SKIN.VOID", 1)
            strip_proven_redundant_secondary_skin_streams(model, _hierarchy())

        wrong_count = _hierarchy(
            include_pivot_fixups=True,
            pivot_fixup_values=tuple([0.0] * 12),
        )
        with self.assertRaisesRegex(
            W3DSecondarySkinError, "pivot fixup cardinality is invalid"
        ):
            strip_proven_redundant_secondary_skin_streams(_model(), wrong_count)

        non_finite_values = [0.0] * 36
        non_finite_values[0] = math.nan
        non_finite = _hierarchy(
            include_pivot_fixups=True,
            pivot_fixup_values=tuple(non_finite_values),
        )
        with self.assertRaisesRegex(W3DSecondarySkinError, "non-finite float"):
            strip_proven_redundant_secondary_skin_streams(_model(), non_finite)

    def test_malformed_noop_and_non_bytes_are_rejected(self) -> None:
        with self.assertRaisesRegex(W3DSecondarySkinError, "exceeds its owner"):
            malformed = bytearray(_model())
            struct.pack_into("<I", malformed, 4, 0x7FFFFFFF)
            strip_proven_redundant_secondary_skin_streams(
                bytes(malformed),
                _hierarchy(),
            )
        with self.assertRaisesRegex(W3DSecondarySkinError, "no secondary"):
            strip_proven_redundant_secondary_skin_streams(
                _plain_mesh() + _hlod(("TEST_SKIN.PLAIN",)),
                _hierarchy(),
            )
        with self.assertRaisesRegex(TypeError, "model bytes must be bytes"):
            strip_proven_redundant_secondary_skin_streams(  # type: ignore[arg-type]
                bytearray(_model()),
                _hierarchy(),
            )


if __name__ == "__main__":
    unittest.main()
