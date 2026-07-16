from __future__ import annotations

import hashlib
import json
import struct
import unittest
from unittest import mock

from openbfme_importer.w3d_metadata import (
    W3DMetadataLimitError,
    scan_w3d_metadata,
)


def _fixed(value: str, size: int) -> bytes:
    encoded = value.encode("cp1252")
    if len(encoded) > size:
        raise ValueError("synthetic fixture string is too long")
    return encoded + b"\0" * (size - len(encoded))


def _chunk(
    chunk_id: int,
    payload: bytes,
    *,
    children: bool = False,
    declared_size: int | None = None,
) -> bytes:
    size = len(payload) if declared_size is None else declared_size
    raw_size = size | (0x80000000 if children else 0)
    return struct.pack("<II", chunk_id, raw_size) + payload


def _mesh_header(mesh: str, container: str) -> bytes:
    return struct.pack(
        "<II16s16s9I10f",
        0x00040002,
        0x0002A000,
        _fixed(mesh, 16),
        _fixed(container, 16),
        12,
        8,
        2,
        3,
        4,
        5,
        6,
        0x13,
        1,
        *[float(value) for value in range(10)],
    )


def _shader_string_property(name: str, value: str) -> bytes:
    raw_name = name.encode("cp1252") + b"\0"
    raw_value = value.encode("cp1252") + b"\0"
    return (
        struct.pack("<ii", 1, len(raw_name))
        + raw_name
        + struct.pack("<i", len(raw_value))
        + raw_value
    )


def _comprehensive_fixture() -> bytes:
    vertex_material = _chunk(
        0x2B,
        _chunk(0x2C, b"StoneMaterial\0")
        + _chunk(
            0x2D,
            struct.pack(
                "<i4s4s4s4sfff",
                0x00010001,
                bytes((1, 2, 3, 4)),
                bytes((5, 6, 7, 8)),
                bytes((9, 10, 11, 12)),
                bytes((13, 14, 15, 16)),
                17.5,
                0.75,
                0.25,
            ),
        )
        + _chunk(0x2E, b"UPerSec=0.5\0"),
        children=True,
    )
    texture = _chunk(
        0x31,
        _chunk(0x32, b"GUHero_D.tga\0")
        + _chunk(0x33, struct.pack("<HHIf", 3, 2, 4, 12.5)),
        children=True,
    )
    shader_material = _chunk(
        0x51,
        _chunk(0x52, struct.pack("<B32si", 1, _fixed("Terrain.fx", 32), 2))
        + _chunk(0x53, _shader_string_property("DiffuseTexture", "detail.dds")),
        children=True,
    )
    material_pass = _chunk(
        0x38,
        _chunk(0x39, struct.pack("<2I", 0, 1))
        + _chunk(0x3A, struct.pack("<I", 1))
        + _chunk(0x3F, struct.pack("<I", 0))
        + _chunk(
            0x48,
            _chunk(0x49, struct.pack("<2i", 0, -1)),
            children=True,
        ),
        children=True,
    )
    mesh = _chunk(
        0x00000000,
        _chunk(0x1F, _mesh_header("BODY", "GUHero"))
        + _chunk(0x28, struct.pack("<4I", 1, 1, 2, 1))
        + _chunk(0x29, bytes(range(32)))
        + _chunk(0x2A, vertex_material, children=True)
        + _chunk(0x30, texture, children=True)
        + _chunk(0x50, shader_material, children=True)
        + material_pass,
        children=True,
    )

    pivot = struct.pack(
        "<16si10f", _fixed("ROOTTRANSFORM", 16), -1, *([0.0] * 10)
    )
    hierarchy = _chunk(
        0x100,
        _chunk(
            0x101,
            struct.pack("<I16sI3f", 0x00040001, _fixed("GUHero_SKL", 16), 1, 0, 0, 0),
        )
        + _chunk(0x102, pivot),
        # Several retail writers omit the subchunk flag on this known container.
        children=False,
    )
    animation = _chunk(
        0x200,
        _chunk(
            0x201,
            struct.pack(
                "<I16s16sII",
                0x00040001,
                _fixed("GUHero_IDLE", 16),
                _fixed("GUHero_SKL", 16),
                30,
                15,
            ),
        ),
        children=True,
    )
    compressed_animation = _chunk(
        0x280,
        _chunk(
            0x281,
            struct.pack(
                "<I16s16sIHH",
                0x00000001,
                _fixed("GUHero_RUN", 16),
                _fixed("GUHero_SKL", 16),
                20,
                20,
                1,
            ),
        ),
        children=True,
    )
    lod_array = _chunk(
        0x702,
        _chunk(0x703, struct.pack("<If", 1, 1000.0))
        + _chunk(0x704, struct.pack("<I32s", 3, _fixed("GUHero.BODY", 32))),
        children=True,
    )
    hlod = _chunk(
        0x700,
        _chunk(
            0x701,
            struct.pack(
                "<II16s16s",
                0x00010000,
                1,
                _fixed("GUHero", 16),
                _fixed("GUHero_SKL", 16),
            ),
        )
        + lod_array,
        children=False,
    )
    collision_box = _chunk(
        0x740,
        struct.pack(
            "<II32s4B6f",
            0x00040002,
            0x50,
            _fixed("GUHero.COLLISION", 32),
            1,
            2,
            3,
            4,
            *([0.0] * 6),
        ),
    )
    return mesh + hierarchy + animation + compressed_animation + hlod + collision_box


class W3DMetadataTests(unittest.TestCase):
    def test_extracts_header_identity_dependencies_and_material_metadata(self) -> None:
        source = _comprehensive_fixture()
        metadata = scan_w3d_metadata(source, r"Art\W3D\GUHero.w3d")

        self.assertEqual(metadata.virtual_path, "Art/W3D/GUHero.w3d")
        self.assertEqual(metadata.source_sha256, hashlib.sha256(source).hexdigest())
        self.assertEqual(metadata.model_ids, ("GUHero.BODY", "GUHero"))
        self.assertEqual(metadata.hierarchy_ids, ("GUHero_SKL",))
        self.assertEqual(metadata.animation_ids, ("GUHero_IDLE", "GUHero_RUN"))
        self.assertEqual(
            metadata.file_headers().neutral(),
            {
                "virtualPath": "Art/W3D/GUHero.w3d",
                "modelIds": ["GUHero.BODY", "GUHero"],
                "animationIds": [
                    "GUHero_IDLE",
                    "GUHero_SKL.GUHero_IDLE",
                    "GUHero_RUN",
                    "GUHero_SKL.GUHero_RUN",
                ],
                "hierarchyIds": ["GUHero_SKL"],
            },
        )

        mesh = metadata.mesh_headers[0]
        self.assertEqual((mesh.identifier, mesh.face_count, mesh.vertex_count), ("GUHero.BODY", 12, 8))
        self.assertEqual((mesh.material_count, mesh.damage_stage_count), (2, 3))
        self.assertEqual((mesh.vertex_channel_flags, mesh.face_channel_flags), (0x13, 1))
        self.assertGreater(mesh.provenance.chunk_header_offset, 0)
        self.assertEqual(mesh.provenance.value_size, 116)

        self.assertEqual(metadata.hierarchy_pivots[0].identifier, "ROOTTRANSFORM")
        self.assertEqual(metadata.hierarchy_pivots[0].parent_index, -1)
        self.assertFalse(metadata.animation_headers[0].compressed)
        self.assertTrue(metadata.animation_headers[1].compressed)
        self.assertEqual(metadata.animation_headers[1].flavor, 1)
        self.assertEqual(metadata.model_headers[0].hierarchy_identifier, "GUHero_SKL")
        self.assertEqual(metadata.model_references[0].identifier, "GUHero.BODY")
        self.assertEqual(metadata.model_references[0].role, "lod")
        self.assertEqual(metadata.collision_boxes[0].identifier, "GUHero.COLLISION")

        self.assertEqual([item.identifier for item in metadata.texture_references], ["GUHero_D.tga"])
        self.assertEqual(metadata.texture_infos[0].frame_count, 4)
        self.assertAlmostEqual(metadata.texture_infos[0].frame_rate, 12.5)
        self.assertEqual(metadata.material_infos[0].texture_count, 1)
        self.assertEqual(
            [(item.kind, item.value) for item in metadata.material_strings],
            [("vertex-material-name", "StoneMaterial"), ("vertex-mapper-args-0", "UPerSec=0.5")],
        )
        vertex_material = metadata.vertex_material_infos[0]
        self.assertEqual(vertex_material.diffuse, (5, 6, 7, 8))
        self.assertAlmostEqual(vertex_material.opacity, 0.75)
        self.assertEqual(len(metadata.shaders), 2)
        self.assertEqual(dict(metadata.shaders[1].fields)["depthCompare"], 16)
        self.assertEqual(metadata.shader_material_headers[0].type_name, "Terrain.fx")
        shader_property = metadata.shader_material_properties[0]
        self.assertEqual(
            (shader_property.property_type_name, shader_property.name, shader_property.value),
            ("string", "DiffuseTexture", "detail.dds"),
        )
        self.assertEqual(
            [(item.kind, item.indices) for item in metadata.index_references],
            [
                ("vertex-material", (0, 1)),
                ("shader", (1,)),
                ("shader-material", (0,)),
                ("texture", (0, -1)),
            ],
        )
        self.assertFalse(metadata.warnings)

    def test_chunk_inventory_and_neutral_output_are_offset_stable(self) -> None:
        source = _comprehensive_fixture()
        first = scan_w3d_metadata(source, "fixture.w3d")
        second = scan_w3d_metadata(source, "fixture.w3d")

        self.assertEqual(first.chunks[0].chunk_name, "mesh")
        self.assertEqual(first.chunks[0].header_offset, 0)
        self.assertTrue(first.chunks[0].has_subchunks_flag)
        hierarchy_chunk = next(item for item in first.chunks if item.chunk_id == 0x100)
        self.assertFalse(hierarchy_chunk.has_subchunks_flag)
        self.assertTrue(hierarchy_chunk.scanned_as_container)
        self.assertEqual(
            json.dumps(first.neutral(), sort_keys=True, separators=(",", ":")),
            json.dumps(second.neutral(), sort_keys=True, separators=(",", ":")),
        )

    def test_reports_unknown_unsupported_truncated_and_count_mismatch(self) -> None:
        pivot = struct.pack("<16si10f", _fixed("ONLY", 16), -1, *([0.0] * 10))
        hierarchy = _chunk(
            0x100,
            _chunk(
                0x101,
                struct.pack("<I16sI3f", 0x00040001, _fixed("SKL", 16), 2, 0, 0, 0),
            )
            + _chunk(0x102, pivot),
        )
        emitter = _chunk(
            0x500,
            _chunk(0x50D, b"\0" * 40),
            children=True,
        )
        source = (
            hierarchy
            + emitter
            + _chunk(0x600, b"opaque")
            + _chunk(0xDEADBEEF, b"raw")
            + b"\x01\x02\x03"
        )
        metadata = scan_w3d_metadata(source, "warnings.w3d")
        codes = [item.code for item in metadata.warnings]

        self.assertIn("unsupported-chunk", codes)
        self.assertIn("unknown-chunk", codes)
        self.assertIn("truncated-chunk-header", codes)
        self.assertIn("count-mismatch", codes)
        unknown = next(item for item in metadata.chunks if item.chunk_id == 0xDEADBEEF)
        self.assertEqual(unknown.classification, "unknown")
        self.assertEqual(unknown.available_payload_size, 3)

        truncated = scan_w3d_metadata(
            _chunk(0x101, b"\0" * 4, declared_size=36), "truncated.w3d"
        )
        truncated_codes = [item.code for item in truncated.warnings]
        self.assertIn("truncated-chunk-payload", truncated_codes)
        self.assertIn("truncated-metadata-record", truncated_codes)
        self.assertFalse(truncated.hierarchy_headers)

    def test_emitter_is_a_real_container_with_exact_child_classifications(self) -> None:
        child_ids = (0x501, 0x502, 0x503, 0x504, 0x505, 0x509, 0x50A, 0x50B, 0x50C, 0x50D)
        source = _chunk(
            0x500,
            b"".join(_chunk(chunk_id, b"") for chunk_id in child_ids),
            children=True,
        )
        metadata = scan_w3d_metadata(source, "emitter.w3d")

        root = metadata.chunks[0]
        self.assertEqual((root.chunk_name, root.classification), ("emitter", "container"))
        self.assertTrue(root.scanned_as_container)
        children = {item.chunk_id: item for item in metadata.chunks[1:]}
        self.assertEqual(
            {chunk_id: children[chunk_id].classification for chunk_id in child_ids},
            {
                0x501: "known-data",
                0x502: "known-data",
                0x503: "known-data",
                0x504: "known-data",
                0x505: "known-data",
                0x509: "known-data",
                0x50A: "known-data",
                0x50B: "known-data",
                0x50C: "known-data",
                0x50D: "known-data",
            },
        )
        self.assertEqual(
            children[0x50D].chunk_name,
            "emitter-extra-info",
        )
        self.assertEqual(
            [item.code for item in metadata.warnings],
            [],
        )

    def test_shader_property_damage_is_retained_without_fabricated_values(self) -> None:
        bad_type = struct.pack("<ii", 99, 5) + b"Name\0" + b"unparsed"
        bad_count = struct.pack("<ii", 1, 100) + b"short"
        source = _chunk(
            0x51,
            _chunk(0x53, bad_type) + _chunk(0x53, bad_count),
            children=True,
        )
        metadata = scan_w3d_metadata(source, "properties.w3d")

        first, second = metadata.shader_material_properties
        self.assertEqual((first.property_type, first.name, first.value), (99, "Name", None))
        self.assertEqual((second.property_type, second.name, second.value), (1, None, None))
        codes = [item.code for item in metadata.warnings]
        self.assertIn("unsupported-shader-property-type", codes)
        self.assertIn("truncated-metadata-record", codes)

    def test_does_not_guess_identifiers_or_promote_shader_strings_to_textures(self) -> None:
        empty_mesh = _chunk(0x1F, _mesh_header("", ""))
        fake_names = _chunk(0x12345678, b"FakeModel\0FakeTexture.tga\0")
        property_chunk = _chunk(
            0x51,
            _chunk(0x53, _shader_string_property("Arbitrary", "NotATexture.dds")),
            children=True,
        )
        metadata = scan_w3d_metadata(empty_mesh + fake_names + property_chunk, "exact.w3d")

        self.assertEqual(metadata.model_ids, ())
        self.assertEqual(metadata.texture_references, ())
        self.assertEqual(metadata.shader_material_properties[0].value, "NotATexture.dds")
        self.assertIn("empty-identifier", [item.code for item in metadata.warnings])
        self.assertIn("unknown-chunk", [item.code for item in metadata.warnings])

    def test_preserves_duplicate_header_ids_for_the_index_to_reject(self) -> None:
        source = _chunk(0x1F, _mesh_header("BODY", "Same")) * 2
        metadata = scan_w3d_metadata(source, "duplicate.w3d")

        self.assertEqual(metadata.model_ids, ("Same.BODY", "Same.BODY"))
        self.assertEqual(
            metadata.file_headers().model_ids, ("Same.BODY", "Same.BODY")
        )

    def test_validates_types_paths_and_hard_bounds(self) -> None:
        with self.assertRaisesRegex(TypeError, "source must be bytes"):
            scan_w3d_metadata(bytearray(), "a.w3d")  # type: ignore[arg-type]
        for path in ("../escape.w3d", r"C:\retail\a.w3d", "not-a-model.bin"):
            with self.subTest(path=path), self.assertRaisesRegex(ValueError, "unsafe|must end"):
                scan_w3d_metadata(b"", path)

        with mock.patch("openbfme_importer.w3d_metadata.MAX_W3D_METADATA_BYTES", 1):
            with self.assertRaisesRegex(W3DMetadataLimitError, "byte limit"):
                scan_w3d_metadata(b"12", "large.w3d")
        with mock.patch("openbfme_importer.w3d_metadata.MAX_W3D_METADATA_CHUNKS", 0):
            with self.assertRaisesRegex(W3DMetadataLimitError, "chunk count"):
                scan_w3d_metadata(_chunk(0x29, b""), "chunks.w3d")

        empty = scan_w3d_metadata(b"", "empty.w3d")
        self.assertEqual([item.code for item in empty.warnings], ["empty-source"])
        self.assertFalse(empty.chunks)


if __name__ == "__main__":
    unittest.main()
