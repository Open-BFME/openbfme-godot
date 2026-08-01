from __future__ import annotations

from dataclasses import FrozenInstanceError
import hashlib
import json
import math
import struct
import unittest

from openbfme_importer.w3d_auxiliary_streams import (
    SUPPORTED_AUXILIARY_STREAM_CHUNK_IDS,
    W3DAabTreeHeaderRecord,
    W3DAabTreeNodeRecord,
    W3DAuxiliaryStreamDecodeError,
    W3DAuxiliaryStreamUnsupportedError,
    W3DUserTextStructure,
    decode_auxiliary_stream,
)


class W3DAuxiliaryMeshStreamTests(unittest.TestCase):
    def test_supported_set_is_exactly_the_nine_source_proven_layouts(self) -> None:
        self.assertEqual(
            SUPPORTED_AUXILIARY_STREAM_CHUNK_IDS,
            {0x0C, 0x22, 0x3B, 0x60, 0x61, 0x91, 0x92, 0x93, 0x103},
        )

    def test_vertex_shade_indices_are_owner_bound_uint32_records(self) -> None:
        payload = struct.pack("<2I", 3, 7)
        decoded = decode_auxiliary_stream(
            0x22,
            payload,
            expected_vertex_count=2,
            expected_triangle_count=1,
        )

        self.assertEqual(decoded.records(), (3, 7))
        self.assertEqual(decoded.record_layout, "<I")
        self.assertEqual(decoded.expected_record_count, 2)
        self.assertEqual(decoded.owner_vertex_count, 2)
        self.assertEqual(decoded.payload_sha256, hashlib.sha256(payload).hexdigest())
        self.assertEqual(decoded.payload_sha256, decoded.canonical_record_sha256)
        self.assertEqual(decoded.bounds[0].minimum, 3)
        self.assertEqual(decoded.bounds[0].maximum, 7)

        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "owner count"):
            decode_auxiliary_stream(
                0x22,
                payload,
                expected_vertex_count=1,
                expected_triangle_count=1,
            )
        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "4-byte"):
            decode_auxiliary_stream(
                0x22,
                payload + b"x",
                expected_vertex_count=2,
                expected_triangle_count=1,
            )

    def test_tangents_and_bitangents_are_finite_vec3_records(self) -> None:
        payload = struct.pack("<6f", 1.0, 2.0, 3.0, -4.0, 5.0, 6.0)
        tangents = decode_auxiliary_stream(
            0x60,
            payload,
            expected_vertex_count=2,
            expected_triangle_count=4,
        )
        bitangents = decode_auxiliary_stream(
            0x61,
            payload,
            expected_vertex_count=2,
            expected_triangle_count=4,
        )

        self.assertEqual(tangents.stream_kind, "tangents")
        self.assertEqual(bitangents.stream_kind, "bitangents")
        self.assertEqual(tangents.records()[0], (1.0, 2.0, 3.0))
        self.assertEqual(tangents.records()[1], (-4.0, 5.0, 6.0))

        nonfinite = struct.pack("<3f", math.inf, 0.0, 0.0)
        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "non-finite"):
            decode_auxiliary_stream(
                0x60,
                nonfinite,
                expected_vertex_count=1,
                expected_triangle_count=0,
            )

    def test_diffuse_colors_are_rgba_bytes_per_owner_vertex(self) -> None:
        payload = bytes((1, 2, 3, 4, 250, 251, 252, 253))
        decoded = decode_auxiliary_stream(
            0x3B,
            payload,
            expected_vertex_count=2,
            expected_triangle_count=1,
        )

        self.assertEqual(decoded.records(), ((1, 2, 3, 4), (250, 251, 252, 253)))
        self.assertEqual(decoded.record_layout, "<4B")
        self.assertEqual(decoded.bounds[0].minimum, 1)
        self.assertEqual(decoded.bounds[3].maximum, 253)

    def test_mesh_user_text_is_structural_hash_evidence_without_text(self) -> None:
        payload = b"private authored comment\0\0\0"
        decoded = decode_auxiliary_stream(
            0x0C,
            payload,
            expected_vertex_count=2,
            expected_triangle_count=1,
        )

        self.assertEqual(
            decoded.records(),
            (W3DUserTextStructure(len(b"private authored comment"), 2, 0),),
        )
        encoded = json.dumps(decoded.neutral(), sort_keys=True)
        self.assertNotIn("private", encoded.casefold())
        self.assertNotIn("authored", encoded.casefold())
        self.assertNotIn("comment", encoded.casefold())
        self.assertEqual(decoded.payload_sha256, hashlib.sha256(payload).hexdigest())
        self.assertEqual(decoded.payload_byte_length, len(payload))

        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "NUL"):
            decode_auxiliary_stream(
                0x0C,
                b"unterminated",
                expected_vertex_count=0,
                expected_triangle_count=0,
            )


class W3DAuxiliaryAabbTests(unittest.TestCase):
    def test_aabb_header_is_exact_and_bound_to_mesh_polygon_count(self) -> None:
        payload = struct.pack("<8I", 3, 2, 9, 8, 7, 6, 5, 4)
        decoded = decode_auxiliary_stream(
            0x91,
            payload,
            expected_triangle_count=2,
        )

        self.assertEqual(
            decoded.records(),
            (W3DAabTreeHeaderRecord(3, 2, (9, 8, 7, 6, 5, 4)),),
        )
        self.assertEqual(decoded.owner_aabb_node_count, 3)
        self.assertEqual(decoded.owner_aabb_polygon_count, 2)

        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "exactly 32"):
            decode_auxiliary_stream(
                0x91,
                payload[:-1],
                expected_triangle_count=2,
            )
        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "owner triangle"):
            decode_auxiliary_stream(
                0x91,
                payload,
                expected_triangle_count=3,
            )

    def test_aabb_polygon_indices_match_header_and_mesh_cardinality(self) -> None:
        payload = struct.pack("<3I", 2, 0, 1)
        decoded = decode_auxiliary_stream(
            0x92,
            payload,
            expected_triangle_count=3,
            expected_aabb_polygon_count=3,
        )

        self.assertEqual(decoded.records(), (2, 0, 1))
        self.assertEqual(decoded.expected_record_count, 3)

        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "AABB polygon count"):
            decode_auxiliary_stream(
                0x92,
                payload,
                expected_triangle_count=3,
                expected_aabb_polygon_count=2,
            )
        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "outside"):
            decode_auxiliary_stream(
                0x92,
                struct.pack("<I", 1),
                expected_triangle_count=1,
                expected_aabb_polygon_count=1,
            )

    def test_aabb_nodes_validate_branch_and_leaf_ranges(self) -> None:
        branch = struct.pack("<6f2I", 0, 0, 0, 4, 4, 4, 1, 2)
        leaf0 = struct.pack("<6f2I", 0, 0, 0, 2, 2, 2, 0x80000000, 1)
        leaf1 = struct.pack("<6f2I", 2, 2, 2, 4, 4, 4, 0x80000001, 1)
        decoded = decode_auxiliary_stream(
            0x93,
            branch + leaf0 + leaf1,
            expected_triangle_count=2,
            expected_aabb_node_count=3,
            expected_aabb_polygon_count=2,
        )

        self.assertEqual(decoded.record_count, 3)
        root, first_leaf, second_leaf = decoded.records()
        self.assertIsInstance(root, W3DAabTreeNodeRecord)
        self.assertFalse(root.is_leaf)  # type: ignore[union-attr]
        self.assertTrue(first_leaf.is_leaf)  # type: ignore[union-attr]
        self.assertEqual(
            second_leaf.front_child_or_polygon0,  # type: ignore[union-attr]
            1,
        )

        invalid_child = struct.pack("<6f2I", 0, 0, 0, 1, 1, 1, 3, 0)
        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "child index"):
            decode_auxiliary_stream(
                0x93,
                invalid_child,
                expected_triangle_count=1,
                expected_aabb_node_count=1,
                expected_aabb_polygon_count=1,
            )

        invalid_leaf = struct.pack(
            "<6f2I",
            0,
            0,
            0,
            1,
            1,
            1,
            0x80000001,
            1,
        )
        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "polygon range"):
            decode_auxiliary_stream(
                0x93,
                invalid_leaf,
                expected_triangle_count=1,
                expected_aabb_node_count=1,
                expected_aabb_polygon_count=1,
            )

    def test_aabb_nodes_reject_inverted_and_nonfinite_bounds(self) -> None:
        inverted = struct.pack(
            "<6f2I",
            2,
            0,
            0,
            1,
            1,
            1,
            0x80000000,
            0,
        )
        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "inverted"):
            decode_auxiliary_stream(
                0x93,
                inverted,
                expected_triangle_count=0,
                expected_aabb_node_count=1,
                expected_aabb_polygon_count=0,
            )

        nonfinite = struct.pack(
            "<6f2I",
            math.nan,
            0,
            0,
            1,
            1,
            1,
            0x80000000,
            0,
        )
        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "non-finite"):
            decode_auxiliary_stream(
                0x93,
                nonfinite,
                expected_triangle_count=0,
                expected_aabb_node_count=1,
                expected_aabb_polygon_count=0,
            )


class W3DAuxiliaryHierarchyAndContractTests(unittest.TestCase):
    def test_pivot_fixups_are_finite_matrix4x3_records_per_pivot(self) -> None:
        payload = struct.pack("<24f", *[float(index) for index in range(24)])
        decoded = decode_auxiliary_stream(
            0x103,
            payload,
            expected_pivot_count=2,
        )

        self.assertEqual(decoded.record_layout, "<12f")
        self.assertEqual(decoded.record_count, 2)
        self.assertEqual(decoded.records()[0], tuple(float(i) for i in range(12)))
        self.assertEqual(decoded.owner_pivot_count, 2)

        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "owner count"):
            decode_auxiliary_stream(
                0x103,
                payload,
                expected_pivot_count=1,
            )
        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "non-finite"):
            decode_auxiliary_stream(
                0x103,
                struct.pack("<12f", math.inf, *([0.0] * 11)),
                expected_pivot_count=1,
            )

    def test_caller_contracts_fail_closed_and_attestations_are_immutable(self) -> None:
        with self.assertRaises(W3DAuxiliaryStreamUnsupportedError):
            decode_auxiliary_stream(0xDEADBEEF, b"")
        with self.assertRaises(TypeError):
            decode_auxiliary_stream(0x22, bytearray(4))  # type: ignore[arg-type]
        with self.assertRaisesRegex(W3DAuxiliaryStreamDecodeError, "explicit"):
            decode_auxiliary_stream(
                0x22,
                struct.pack("<I", 0),
                expected_vertex_count=True,  # type: ignore[arg-type]
                expected_triangle_count=0,
            )

        payload = struct.pack("<I", 5)
        first = decode_auxiliary_stream(
            0x22,
            payload,
            expected_vertex_count=1,
            expected_triangle_count=0,
        )
        second = decode_auxiliary_stream(
            0x22,
            payload,
            expected_vertex_count=1,
            expected_triangle_count=0,
        )
        self.assertEqual(first.neutral(), second.neutral())
        with self.assertRaises(FrozenInstanceError):
            first.record_count = 0  # type: ignore[misc]


if __name__ == "__main__":
    unittest.main()

