from __future__ import annotations

from dataclasses import FrozenInstanceError
import hashlib
import json
import math
import struct
import unittest

from openbfme_importer.w3d_geometry_streams import (
    W3D_CHUNK_NORMALS_2,
    W3D_CHUNK_STAGE_TEXCOORDS,
    W3D_CHUNK_TRIANGLES,
    W3D_CHUNK_VERTEX_INFLUENCES,
    W3D_CHUNK_VERTEX_NORMALS,
    W3D_CHUNK_VERTICES,
    W3D_CHUNK_VERTICES_2,
    W3DGeometryStreamDecodeError,
    W3DGeometryStreamUnsupportedError,
    W3DTriangleRecord,
    W3DVertexInfluenceRecord,
    decode_geometry_stream,
    decode_geometry_streams,
    decode_triangle_stream,
    decode_uv_stream,
    decode_vec3_stream,
    decode_vertex_influence_stream,
)


def _bound(attestation, name: str) -> tuple[int | float | None, int | float | None]:
    item = next(bound for bound in attestation.bounds if bound.name == name)
    return item.minimum, item.maximum


class W3DGeometryVectorStreamTests(unittest.TestCase):
    def test_primary_vec3_is_little_endian_exact_immutable_and_json_ready(self) -> None:
        payload = struct.pack("<6f", 1.25, -2.5, 3.75, 4.0, 5.5, -6.25)

        result = decode_vec3_stream(
            W3D_CHUNK_VERTICES,
            payload,
            expected_vertex_count=2,
            owner_triangle_count=1,
        )

        self.assertEqual(
            result.numeric_records(),
            ((1.25, -2.5, 3.75), (4.0, 5.5, -6.25)),
        )
        self.assertEqual(result.record_layout, "<3f")
        self.assertEqual(result.stream_slot, "primary")
        self.assertEqual(_bound(result, "x"), (1.25, 4.0))
        self.assertEqual(_bound(result, "z"), (-6.25, 3.75))
        expected_hash = hashlib.sha256(payload).hexdigest()
        self.assertEqual(result.payload_sha256, expected_hash)
        self.assertEqual(result.canonical_record_sha256, expected_hash)
        self.assertEqual(result.neutral()["ownerTriangleCount"], 1)
        self.assertNotIn("records", result.neutral())
        json.dumps(result.json_ready(), sort_keys=True)
        with self.assertRaises(FrozenInstanceError):
            result.record_count = 3  # type: ignore[misc]

    def test_primary_and_secondary_streams_remain_distinct(self) -> None:
        primary = struct.pack("<6f", 0.0, 1.0, 2.0, 3.0, 4.0, 5.0)
        secondary = struct.pack("<6f", 10.0, 11.0, 12.0, 13.0, 14.0, 15.0)

        results = decode_geometry_streams(
            (
                (W3D_CHUNK_VERTICES, primary),
                (W3D_CHUNK_VERTICES_2, secondary),
                (W3D_CHUNK_VERTEX_NORMALS, primary),
                (W3D_CHUNK_NORMALS_2, secondary),
            ),
            expected_vertex_count=2,
            expected_triangle_count=0,
        )

        self.assertEqual([item.stream_slot for item in results], [
            "primary",
            "secondary",
            "primary",
            "secondary",
        ])
        self.assertEqual([item.stream_kind for item in results], [
            "vertices",
            "vertices",
            "normals",
            "normals",
        ])
        self.assertNotEqual(results[0].payload_sha256, results[1].payload_sha256)
        self.assertEqual(results[0].numeric_records()[0], (0.0, 1.0, 2.0))
        self.assertEqual(results[1].numeric_records()[0], (10.0, 11.0, 12.0))

    def test_vec3_rejects_malformed_length_cardinality_and_non_finite_values(self) -> None:
        with self.assertRaisesRegex(W3DGeometryStreamDecodeError, "multiple"):
            decode_vec3_stream(
                W3D_CHUNK_VERTICES,
                b"\x00" * 11,
                expected_vertex_count=1,
            )
        with self.assertRaisesRegex(W3DGeometryStreamDecodeError, "owner count"):
            decode_vec3_stream(
                W3D_CHUNK_VERTICES,
                struct.pack("<3f", 1.0, 2.0, 3.0),
                expected_vertex_count=2,
            )
        for bad in (math.nan, math.inf, -math.inf):
            with self.subTest(bad=bad):
                with self.assertRaisesRegex(
                    W3DGeometryStreamDecodeError,
                    "non-finite",
                ):
                    decode_vec3_stream(
                        W3D_CHUNK_VERTICES,
                        struct.pack("<3f", bad, 0.0, 0.0),
                        expected_vertex_count=1,
                    )


class W3DGeometryTriangleStreamTests(unittest.TestCase):
    def test_triangle_layout_indices_floats_bounds_and_hash_are_exact(self) -> None:
        payload = b"".join(
            (
                struct.pack("<4I4f", 0, 1, 2, 13, 0.0, 0.0, 1.0, -2.0),
                struct.pack("<4I4f", 2, 3, 0, 5, -1.0, 0.5, 0.0, 4.0),
            )
        )

        result = decode_triangle_stream(
            payload,
            expected_vertex_count=4,
            expected_triangle_count=2,
        )

        first = result.numeric_records()[0]
        self.assertIsInstance(first, W3DTriangleRecord)
        self.assertEqual(first.vertex_indices, (0, 1, 2))
        self.assertEqual(first.surface_type, 13)
        self.assertEqual(first.normal, (0.0, 0.0, 1.0))
        self.assertEqual(first.distance, -2.0)
        self.assertEqual(_bound(result, "vertexIndex"), (0, 3))
        self.assertEqual(_bound(result, "surfaceType"), (5, 13))
        self.assertEqual(_bound(result, "distance"), (-2.0, 4.0))
        expected_hash = hashlib.sha256(payload).hexdigest()
        self.assertEqual(result.payload_sha256, expected_hash)
        self.assertEqual(result.canonical_record_sha256, expected_hash)

    def test_triangle_rejects_bad_length_count_index_and_float(self) -> None:
        with self.assertRaisesRegex(W3DGeometryStreamDecodeError, "multiple"):
            decode_triangle_stream(
                b"\x00" * 31,
                expected_vertex_count=3,
                expected_triangle_count=1,
            )
        valid = struct.pack("<4I4f", 0, 1, 2, 13, 0.0, 0.0, 1.0, 0.0)
        with self.assertRaisesRegex(W3DGeometryStreamDecodeError, "owner count"):
            decode_triangle_stream(
                valid,
                expected_vertex_count=3,
                expected_triangle_count=2,
            )
        out_of_bounds = struct.pack(
            "<4I4f",
            0,
            1,
            3,
            13,
            0.0,
            0.0,
            1.0,
            0.0,
        )
        with self.assertRaisesRegex(W3DGeometryStreamDecodeError, "vertex index 3"):
            decode_triangle_stream(
                out_of_bounds,
                expected_vertex_count=3,
                expected_triangle_count=1,
            )
        non_finite = struct.pack(
            "<4I4f",
            0,
            1,
            2,
            13,
            0.0,
            math.inf,
            1.0,
            0.0,
        )
        with self.assertRaisesRegex(W3DGeometryStreamDecodeError, "non-finite"):
            decode_triangle_stream(
                non_finite,
                expected_vertex_count=3,
                expected_triangle_count=1,
            )


class W3DGeometryInfluenceAndUvTests(unittest.TestCase):
    def test_influences_decode_exact_weights_and_validate_pivots(self) -> None:
        payload = struct.pack("<8H", 1, 2, 75, 25, 3, 0, 100, 0)

        result = decode_vertex_influence_stream(
            payload,
            expected_vertex_count=2,
            pivot_count=4,
        )

        first = result.numeric_records()[0]
        self.assertIsInstance(first, W3DVertexInfluenceRecord)
        self.assertEqual(first.primary_pivot_index, 1)
        self.assertEqual(first.secondary_pivot_index, 2)
        self.assertEqual(first.primary_weight, 0.75)
        self.assertEqual(first.secondary_weight, 0.25)
        self.assertEqual(_bound(result, "primaryPivotIndex"), (1, 3))
        self.assertEqual(_bound(result, "secondaryWeightRaw"), (0, 25))
        self.assertEqual(result.payload_sha256, hashlib.sha256(payload).hexdigest())

        bad_pivot = struct.pack("<4H", 4, 0, 100, 0)
        with self.assertRaisesRegex(W3DGeometryStreamDecodeError, "pivot index 4"):
            decode_vertex_influence_stream(
                bad_pivot,
                expected_vertex_count=1,
                pivot_count=4,
            )

    def test_influences_reject_bad_length_count_and_weight(self) -> None:
        with self.assertRaisesRegex(W3DGeometryStreamDecodeError, "multiple"):
            decode_vertex_influence_stream(
                b"\x00" * 7,
                expected_vertex_count=1,
            )
        one = struct.pack("<4H", 0, 0, 100, 0)
        with self.assertRaisesRegex(W3DGeometryStreamDecodeError, "owner count"):
            decode_vertex_influence_stream(one, expected_vertex_count=2)
        bad_weight = struct.pack("<4H", 0, 0, 101, 0)
        with self.assertRaisesRegex(W3DGeometryStreamDecodeError, "0..100"):
            decode_vertex_influence_stream(bad_weight, expected_vertex_count=1)

    def test_uv_is_little_endian_exact_and_rejects_non_finite(self) -> None:
        payload = struct.pack("<4f", -0.25, 0.5, 1.25, 2.0)

        result = decode_uv_stream(payload, expected_vertex_count=2)

        self.assertEqual(result.chunk_id, W3D_CHUNK_STAGE_TEXCOORDS)
        self.assertEqual(result.numeric_records(), ((-0.25, 0.5), (1.25, 2.0)))
        self.assertEqual(_bound(result, "u"), (-0.25, 1.25))
        self.assertEqual(result.canonical_record_sha256, hashlib.sha256(payload).hexdigest())
        with self.assertRaisesRegex(W3DGeometryStreamDecodeError, "non-finite"):
            decode_uv_stream(
                struct.pack("<2f", 0.0, math.nan),
                expected_vertex_count=1,
            )

    def test_empty_explicit_cardinalities_have_null_bounds(self) -> None:
        result = decode_uv_stream(b"", expected_vertex_count=0)

        self.assertEqual(result.numeric_records(), ())
        self.assertEqual(_bound(result, "u"), (None, None))
        self.assertEqual(
            result.payload_sha256,
            hashlib.sha256(b"").hexdigest(),
        )


class W3DGeometryDispatchTests(unittest.TestCase):
    def test_dispatch_requires_owner_counts_and_refuses_unknown_layouts(self) -> None:
        triangle = struct.pack("<4I4f", 0, 1, 2, 13, 0.0, 0.0, 1.0, 0.0)

        result = decode_geometry_stream(
            W3D_CHUNK_TRIANGLES,
            triangle,
            expected_vertex_count=3,
            expected_triangle_count=1,
        )

        self.assertEqual(result.chunk_id, W3D_CHUNK_TRIANGLES)
        with self.assertRaisesRegex(W3DGeometryStreamDecodeError, "non-negative"):
            decode_geometry_stream(
                W3D_CHUNK_TRIANGLES,
                triangle,
                expected_vertex_count=-1,
                expected_triangle_count=1,
            )
        with self.assertRaisesRegex(
            W3DGeometryStreamUnsupportedError,
            "no evidence-backed",
        ):
            decode_geometry_stream(
                0x60,
                struct.pack("<3f", 1.0, 0.0, 0.0),
                expected_vertex_count=1,
                expected_triangle_count=0,
            )

    def test_duplicate_uv_streams_are_preserved_in_input_order(self) -> None:
        first = struct.pack("<2f", 0.0, 0.0)
        second = struct.pack("<2f", 1.0, 1.0)

        results = decode_geometry_streams(
            (
                (W3D_CHUNK_STAGE_TEXCOORDS, first),
                (W3D_CHUNK_STAGE_TEXCOORDS, second),
                (W3D_CHUNK_VERTEX_INFLUENCES, struct.pack("<4H", 0, 0, 100, 0)),
            ),
            expected_vertex_count=1,
            expected_triangle_count=0,
            pivot_count=1,
        )

        self.assertEqual(len(results), 3)
        self.assertEqual(results[0].numeric_records(), ((0.0, 0.0),))
        self.assertEqual(results[1].numeric_records(), ((1.0, 1.0),))
        self.assertNotEqual(results[0].payload_sha256, results[1].payload_sha256)


if __name__ == "__main__":
    unittest.main()
