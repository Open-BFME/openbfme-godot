from __future__ import annotations

import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
import wave
import zlib

from openbfme_importer.native_backtest import (
    validate_cooked_sage_map,
    validate_glb,
    validate_mp3,
    validate_native_output,
    validate_png,
    validate_wav,
)


def _png_chunk(kind: bytes, payload: bytes) -> bytes:
    crc = zlib.crc32(kind)
    crc = zlib.crc32(payload, crc) & 0xFFFFFFFF
    return struct.pack(">I", len(payload)) + kind + payload + struct.pack(">I", crc)


def _rgba_png(width: int = 2, height: int = 1) -> bytes:
    rows = []
    for y in range(height):
        pixels = b"".join(bytes((x * 40, y * 40, 120, 255)) for x in range(width))
        rows.append(b"\x00" + pixels)
    ihdr = struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0)
    return (
        b"\x89PNG\r\n\x1a\n"
        + _png_chunk(b"IHDR", ihdr)
        + _png_chunk(b"IDAT", zlib.compress(b"".join(rows), 9))
        + _png_chunk(b"IEND", b"")
    )


def _mp3_frames(count: int = 2, *, with_id3: bool = True) -> bytes:
    # MPEG-1 Layer III, 128 kbit/s, 44.1 kHz, stereo: 417-byte frames.
    header = bytes.fromhex("fffb9000")
    frame = header + bytes(417 - len(header))
    prefix = b"ID3\x04\x00\x00\x00\x00\x00\x00" if with_id3 else b""
    return prefix + frame * count


def _glb(document: dict[str, object], binary: bytes | None = None) -> bytes:
    encoded = json.dumps(document, sort_keys=True, separators=(",", ":")).encode(
        "utf-8"
    )
    encoded += b" " * ((-len(encoded)) % 4)
    chunks = [struct.pack("<II", len(encoded), 0x4E4F534A) + encoded]
    if binary is not None:
        binary += b"\x00" * ((-len(binary)) % 4)
        chunks.append(struct.pack("<II", len(binary), 0x004E4942) + binary)
    body = b"".join(chunks)
    return struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body


def _write_json(path: Path, value: object) -> None:
    path.write_text(
        json.dumps(value, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )


def _descriptor(path: str, payload: bytes, **extra: object) -> dict[str, object]:
    return {
        "path": path,
        "byteLength": len(payload),
        "sha256": hashlib.sha256(payload).hexdigest(),
        "sourceExact": True,
        **extra,
    }


def _canonical_json_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _cooked_map(root: Path, *, dimensions: tuple[int, int] = (3, 2)) -> None:
    width, height = dimensions
    area = width * height
    binaries = {
        "heightmap.r16": struct.pack(f"<{area}H", *range(area)),
        "impassability.bit": bytes(((width + 7) // 8) * height),
        "terrain-tile-indices.u16": struct.pack(
            f"<{area}H", *(index % 2 for index in range(area))
        ),
        "terrain-blend-cells.u32": struct.pack(
            f"<{area}I", *(index + 1 for index in range(area))
        ),
        "terrain-three-way-blend-cells.u32": struct.pack(
            f"<{area}I", *(area - index for index in range(area))
        ),
        "terrain-cliff-cells.u32": struct.pack(
            f"<{area}I", *(index % 2 for index in range(area))
        ),
        "terrain-blend-descriptions.bin": bytes(range(18)),
        "terrain-cliff-mappings.bin": bytes(range(38)),
    }
    for relative, payload in binaries.items():
        (root / relative).write_bytes(payload)

    source_hash = hashlib.sha256(b"synthetic-public-map-fixture").hexdigest()
    binding_summary = {
        "resolutionStatus": "complete",
        "placementCount": 2,
        "typeCount": 2,
        "resolvedTypeCount": 2,
        "resolvedPlacementCount": 2,
        "boundTypeCount": 2,
        "boundPlacementCount": 2,
        "logicalTypeCount": 0,
        "logicalPlacementCount": 0,
        "unresolvedTypeCount": 0,
        "unresolvedPlacementCount": 0,
    }
    road_summary = {
        "status": "empty",
        "roadIdCount": 0,
        "controlPointCount": 0,
        "pairedControlPointCount": 0,
        "unresolvedControlPointCount": 0,
        "segmentCount": 0,
        "unresolvedDiagnosticCount": 0,
    }
    _write_json(
        root / "map.json",
        {
            "schema": "openbfme.map",
            "schemaVersion": 0,
            "id": "test.map.synthetic",
            "displayName": "Synthetic",
            "sourceFormat": "sage-map-binary",
            "sourceBinaryImported": True,
            "sourceBinaryPackaged": False,
            "terrain": "terrain.json",
            "water": "water.json",
            "objects": "objects.json",
            "roads": "roads.json",
            "roadSummary": road_summary,
            "objectBindings": "object-bindings.json",
            "objectResolution": binding_summary,
            "waypoints": "waypoints.json",
            "setup": "setup.json",
            "triggers": "triggers.json",
            "inventory": "chunks.json",
            "conversionStatus": {},
            "source": {"sha256": source_hash, "packaged": False},
        },
    )
    _write_json(
        root / "terrain.json",
        {
            "schema": "openbfme.sage-terrain",
            "schemaVersion": 0,
            "height": {
                "width": width,
                "height": height,
                "area": area,
                "heightmap": {
                    "path": "heightmap.r16",
                    "encoding": "uint16",
                    "endianness": "little",
                },
            },
            "passability": {
                "path": "impassability.bit",
                "rowStrideBytes": 1,
                "sourceExact": True,
            },
            "blend": {"rawBlendCount": 2, "rawCliffCount": 2},
            "sourceLayers": {
                "schema": "openbfme.sage-terrain-source-layers",
                "schemaVersion": 0,
                "gridWidth": width,
                "gridHeight": height,
                "cellCount": area,
                "layers": {
                    "tileIndices": _descriptor(
                        "terrain-tile-indices.u16",
                        binaries["terrain-tile-indices.u16"],
                        cellCount=area,
                        cellSizeBytes=2,
                    ),
                    "blendCells": _descriptor(
                        "terrain-blend-cells.u32",
                        binaries["terrain-blend-cells.u32"],
                        cellCount=area,
                        cellSizeBytes=4,
                    ),
                    "threeWayBlendCells": _descriptor(
                        "terrain-three-way-blend-cells.u32",
                        binaries["terrain-three-way-blend-cells.u32"],
                        cellCount=area,
                        cellSizeBytes=4,
                    ),
                    "cliffCells": _descriptor(
                        "terrain-cliff-cells.u32",
                        binaries["terrain-cliff-cells.u32"],
                        cellCount=area,
                        cellSizeBytes=4,
                    ),
                },
                "descriptionTables": {
                    "blendDescriptions": _descriptor(
                        "terrain-blend-descriptions.bin",
                        binaries["terrain-blend-descriptions.bin"],
                        recordCount=1,
                        recordSizeBytes=18,
                    ),
                    "cliffMappings": _descriptor(
                        "terrain-cliff-mappings.bin",
                        binaries["terrain-cliff-mappings.bin"],
                        recordCount=1,
                        recordSizeBytes=38,
                    ),
                },
            },
        },
    )
    _write_json(
        root / "water.json",
        {
            "schema": "openbfme.sage-water",
            "schemaVersion": 0,
            "standingAreas": [],
            "rivers": [],
            "standingWaves": [],
        },
    )
    _write_json(
        root / "triggers.json",
        {
            "schema": "openbfme.sage-triggers",
            "schemaVersion": 0,
            "count": 0,
            "areas": [],
        },
    )
    _write_json(
        root / "objects.json",
        {
            "schema": "openbfme.sage-map-objects",
            "schemaVersion": 0,
            "count": 2,
            "objects": [
                {"index": 0, "typeName": "SyntheticA", "roadType": 0},
                {"index": 1, "typeName": "SyntheticB", "roadType": 0},
            ],
        },
    )
    _write_json(
        root / "roads.json",
        {
            "schema": "openbfme.sage-roads",
            "schemaVersion": 0,
            "coordinateTransform": "godot=(sage.x,sage.z,-sage.y)",
            "pairingPolicy": "source-order-exact-wire-2-then-4-same-road-id",
            "curveReconstruction": "not-attempted",
            "roadIds": [],
            "summary": road_summary,
            "controlPoints": [],
            "segments": [],
            "unresolvedDiagnostics": [],
        },
    )
    _write_json(
        root / "object-bindings.json",
        {
            "schema": "openbfme.sage-object-bindings",
            "schemaVersion": 0,
            "matchPolicy": "explicit-exact-type-name-only",
            "summary": binding_summary,
            "records": [
                {
                    "typeName": "SyntheticA",
                    "placementCount": 1,
                    "status": "bound",
                },
                {
                    "typeName": "SyntheticB",
                    "placementCount": 1,
                    "status": "bound",
                },
            ],
        },
    )
    _write_json(
        root / "waypoints.json",
        {
            "schema": "openbfme.sage-waypoints",
            "schemaVersion": 0,
            "count": 1,
            "waypoints": [{"id": 1}],
            "playerStartBindings": [{"playerIndex": 1, "waypointId": 1}],
        },
    )
    _write_json(
        root / "setup.json",
        {
            "schema": "openbfme.sage-multiplayer-setup",
            "schemaVersion": 0,
            "declaredPlayerCount": 1,
        },
    )
    _write_json(
        root / "chunks.json",
        {
            "schema": "openbfme.sage-map-inventory",
            "schemaVersion": 0,
            "source": {
                "sha256": source_hash,
                "bodySha256": hashlib.sha256(b"synthetic-body").hexdigest(),
                "packaged": False,
            },
            "chunks": [],
            "summaries": {},
        },
    )


def _install_paired_roads(root: Path) -> None:
    objects_path = root / "objects.json"
    objects = json.loads(objects_path.read_text(encoding="utf-8"))
    road_objects = [
        {
            "index": 2,
            "typeName": "RoadAlpha",
            "roadType": 2,
            "sagePosition": [1.0, 2.0, 0.25],
            "godotPosition": [1.0, 10.25, -2.0],
        },
        {
            "index": 3,
            "typeName": "RoadAlpha",
            "roadType": 4,
            "sagePosition": [3.0, 4.0, 0.5],
            "godotPosition": [3.0, 11.5, -4.0],
        },
        {
            "index": 4,
            "typeName": "RoadBeta",
            "roadType": 2,
            "sagePosition": [5.0, 6.0, 0.75],
            "godotPosition": [5.0, 12.75, -6.0],
        },
        {
            "index": 5,
            "typeName": "RoadBeta",
            "roadType": 4,
            "sagePosition": [7.0, 8.0, 1.0],
            "godotPosition": [7.0, 14.0, -8.0],
        },
    ]
    objects["objects"].extend(road_objects)
    objects["count"] = len(objects["objects"])
    _write_json(objects_path, objects)

    control_points = [
        {
            "sequence": sequence,
            "sourceIndex": item["index"],
            "roadId": item["typeName"],
            "wireType": item["roadType"],
            "role": "segment-start" if item["roadType"] == 2 else "segment-end",
            "status": "paired",
            "segmentIndex": sequence // 2,
            "sagePosition": item["sagePosition"],
            "godotPosition": item["godotPosition"],
        }
        for sequence, item in enumerate(road_objects)
    ]
    segments = [
        {
            "index": segment_index,
            "roadId": start["typeName"],
            "startSourceIndex": start["index"],
            "endSourceIndex": end["index"],
            "sageStart": start["sagePosition"],
            "sageEnd": end["sagePosition"],
            "godotStart": start["godotPosition"],
            "godotEnd": end["godotPosition"],
        }
        for segment_index, (start, end) in enumerate(
            ((road_objects[0], road_objects[1]), (road_objects[2], road_objects[3]))
        )
    ]
    summary = {
        "status": "exact-paired-control-points",
        "roadIdCount": 2,
        "controlPointCount": 4,
        "pairedControlPointCount": 4,
        "unresolvedControlPointCount": 0,
        "segmentCount": 2,
        "unresolvedDiagnosticCount": 0,
    }
    _write_json(
        root / "roads.json",
        {
            "schema": "openbfme.sage-roads",
            "schemaVersion": 0,
            "coordinateTransform": "godot=(sage.x,sage.z,-sage.y)",
            "pairingPolicy": "source-order-exact-wire-2-then-4-same-road-id",
            "curveReconstruction": "not-attempted",
            "roadIds": ["RoadAlpha", "RoadBeta"],
            "summary": summary,
            "controlPoints": control_points,
            "segments": segments,
            "unresolvedDiagnostics": [],
        },
    )
    map_path = root / "map.json"
    map_data = json.loads(map_path.read_text(encoding="utf-8"))
    map_data["roadSummary"] = summary
    _write_json(map_path, map_data)


def _make_last_road_pair_unresolved(root: Path) -> None:
    objects_path = root / "objects.json"
    objects = json.loads(objects_path.read_text(encoding="utf-8"))
    objects["objects"][4]["roadType"] = 7
    _write_json(objects_path, objects)

    roads_path = root / "roads.json"
    roads = json.loads(roads_path.read_text(encoding="utf-8"))
    roads["controlPoints"][2].update(
        {
            "wireType": 7,
            "role": "unresolved",
            "status": "unresolved",
            "segmentIndex": None,
        }
    )
    roads["controlPoints"][3].update({"status": "unresolved", "segmentIndex": None})
    roads["segments"] = roads["segments"][:1]
    roads["unresolvedDiagnostics"] = [
        {
            "sourceIndex": 4,
            "roadId": "RoadBeta",
            "wireType": 7,
            "reason": "unsupported-road-control-wire-type",
        },
        {
            "sourceIndex": 5,
            "roadId": "RoadBeta",
            "wireType": 4,
            "reason": "unpaired-segment-end",
        },
    ]
    roads["summary"] = {
        "status": "unresolved-control-points",
        "roadIdCount": 2,
        "controlPointCount": 4,
        "pairedControlPointCount": 2,
        "unresolvedControlPointCount": 2,
        "segmentCount": 1,
        "unresolvedDiagnosticCount": 2,
    }
    _write_json(roads_path, roads)
    map_path = root / "map.json"
    map_data = json.loads(map_path.read_text(encoding="utf-8"))
    map_data["roadSummary"] = roads["summary"]
    _write_json(map_path, map_data)


def _attest_map_profile(
    root: Path,
    *,
    map_kind: str,
    profile_version: int = 1,
    runnable: bool,
) -> None:
    structural_status = (
        "runnable-structure" if runnable else "non-runnable-structural-map"
    )
    profile = {
        "mapKind": map_kind,
        "profileVersion": profile_version,
        "runnable": runnable,
        "structuralStatus": structural_status,
    }
    for relative in ("map.json", "setup.json"):
        target = root / relative
        document = json.loads(target.read_text(encoding="utf-8"))
        document.update(profile)
        if relative == "map.json":
            document["conversionEvidence"] = dict(profile)
        _write_json(target, document)
    chunks_path = root / "chunks.json"
    chunks = json.loads(chunks_path.read_text(encoding="utf-8"))
    chunks["conversionEvidence"] = dict(profile)
    _write_json(chunks_path, chunks)


_VERSIONED_BLEND_LAYER_PATHS = {
    "impassability": "impassability.bit",
    "impassabilityToPlayers": "terrain-impassability-to-players.bit",
    "passageWidths": "terrain-passage-widths.bit",
    "taintability": "terrain-taintability.bit",
    "extraPassability": "terrain-extra-passability.bit",
    "flammability": "terrain-flammability.u8",
    "visibility": "terrain-visibility.bit",
}


def _attest_versioned_blend_layers(root: Path, version: int) -> None:
    _attest_map_profile(root, map_kind="multiplayer", runnable=True)
    width, height = 3, 2
    area = width * height
    row_stride = (width + 7) // 8
    minimum_versions = {
        "impassability": 7,
        "impassabilityToPlayers": 10,
        "passageWidths": 11,
        "taintability": 14,
        "extraPassability": 15,
        "flammability": 16,
        "visibility": 17,
    }
    presence = {
        name: version >= minimum_version
        for name, minimum_version in minimum_versions.items()
    }
    layers: dict[str, dict[str, object]] = {}
    for index, (name, path) in enumerate(_VERSIONED_BLEND_LAYER_PATHS.items()):
        if not presence[name]:
            layers[name] = {
                "present": False,
                "absence": "not-present-in-source-version",
            }
            continue
        if name == "impassability":
            payload = (root / path).read_bytes()
        elif name == "flammability":
            payload = bytes(range(area))
            (root / path).write_bytes(payload)
        else:
            payload = bytes((index + 1, index + 2))
            (root / path).write_bytes(payload)
        if name == "flammability":
            metadata = {
                "encoding": "uint8",
                "endianness": "little",
                "order": "row-major-y-then-x",
                "cellCount": area,
                "cellSizeBytes": 1,
            }
        else:
            metadata = {
                "encoding": "packed-single-bit",
                "bitOrder": "least-significant-bit-first",
                "order": "row-major-y-then-x",
                "gridWidth": width,
                "gridHeight": height,
                "rowStrideBytes": row_stride,
                "rowPadding": True,
            }
        layers[name] = {
            "present": True,
            **_descriptor(path, payload),
            **metadata,
        }

    layout = {
        "sourceVersion": version,
        "blendCellWordBits": 16 if version < 14 else 32,
        "sourceLayerPresence": presence,
        "structuralConversion": "lossless-source-layer-preservation",
        "runtimeDefaultParity": "unproven",
    }
    terrain_path = root / "terrain.json"
    terrain = json.loads(terrain_path.read_text(encoding="utf-8"))
    blend_cell_word_bits = layout["blendCellWordBits"]
    if blend_cell_word_bits == 16:
        for name in ("blendCells", "threeWayBlendCells", "cliffCells"):
            descriptor = terrain["sourceLayers"]["layers"][name]
            old_path = str(descriptor["path"])
            values = struct.unpack(f"<{area}I", (root / old_path).read_bytes())
            payload = struct.pack(f"<{area}H", *values)
            new_path = old_path.removesuffix(".u32") + ".u16"
            (root / new_path).write_bytes(payload)
            (root / old_path).unlink()
            descriptor.update(
                {
                    "path": new_path,
                    "byteLength": len(payload),
                    "sha256": hashlib.sha256(payload).hexdigest(),
                    "cellSizeBytes": 2,
                }
            )
    grid_stats = {}
    for name, summary_name in (
        ("impassability", "impassable"),
        ("impassabilityToPlayers", "impassableToPlayers"),
        ("passageWidths", "passageWidth"),
        ("taintability", "taintable"),
        ("extraPassability", "extraPassability"),
        ("visibility", "visible"),
    ):
        if presence[name]:
            grid_stats[summary_name] = 0
    blend_evidence = {
        "version": version,
        "sourceLayerPresence": presence,
        "structuralConversion": "lossless-source-layer-preservation",
        "runtimeDefaultParity": "unproven",
        "gridStats": grid_stats,
    }
    if presence["flammability"]:
        blend_evidence["flammabilityCounts"] = {"0": area}
    terrain["blend"].update(blend_evidence)
    terrain["sourceLayers"]["versionedBlendLayers"] = {
        "schema": "openbfme.sage-blend-versioned-source-layers",
        "schemaVersion": 0,
        "sourceVersion": version,
        "blendCellWordBits": blend_cell_word_bits,
        "structuralConversion": "lossless-source-layer-preservation",
        "runtimeDefaultParity": "unproven",
        "layers": layers,
    }
    _write_json(terrain_path, terrain)

    map_path = root / "map.json"
    map_data = json.loads(map_path.read_text(encoding="utf-8"))
    map_data["conversionEvidence"]["sourceChunkLayouts"] = {"BlendTileData": layout}
    _write_json(map_path, map_data)

    setup_path = root / "setup.json"
    setup = json.loads(setup_path.read_text(encoding="utf-8"))
    setup["sourceChunkLayouts"] = {"BlendTileData": layout}
    _write_json(setup_path, setup)

    chunks_path = root / "chunks.json"
    chunks = json.loads(chunks_path.read_text(encoding="utf-8"))
    chunks["conversionEvidence"]["sourceChunkLayouts"] = {"BlendTileData": layout}
    _write_json(chunks_path, chunks)


class NativeBacktestTests(unittest.TestCase):
    def assert_payload_free_and_bounded(
        self, result: dict[str, object], root: Path
    ) -> None:
        serialized = json.dumps(result, sort_keys=True)
        self.assertNotIn(str(root), serialized)
        self.assertLessEqual(len(result["errors"]), 32)
        self.assertTrue(all(len(item) <= 240 for item in result["errors"]))

    def test_png_decodes_dimensions_mode_and_rejects_bad_crc(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            target = root / "image.png"
            target.write_bytes(_rgba_png())
            valid = validate_png(target)
            self.assertTrue(valid["valid"], valid["errors"])
            self.assertEqual(valid["facts"]["width"], 2)
            self.assertEqual(valid["facts"]["height"], 1)
            self.assertEqual(valid["facts"]["mode"], "RGBA")
            self.assertEqual(valid["facts"]["decodedPixelBytes"], 8)
            self.assertEqual(
                valid["sha256"], hashlib.sha256(target.read_bytes()).hexdigest()
            )
            self.assert_payload_free_and_bounded(valid, root)

            damaged = bytearray(target.read_bytes())
            damaged[-1] ^= 1
            target.write_bytes(damaged)
            invalid = validate_png(target)
            self.assertFalse(invalid["valid"])
            self.assertTrue(any("CRC" in item for item in invalid["errors"]))

    def test_wav_reports_pcm_header_and_fails_closed_on_inconsistent_rate(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            target = root / "sound.wav"
            with wave.open(str(target), "wb") as stream:
                stream.setnchannels(2)
                stream.setsampwidth(2)
                stream.setframerate(22_050)
                stream.writeframes(struct.pack("<8h", *range(8)))
            valid = validate_wav(target)
            self.assertTrue(valid["valid"], valid["errors"])
            self.assertEqual(valid["facts"]["channels"], 2)
            self.assertEqual(valid["facts"]["sampleRate"], 22_050)
            self.assertEqual(valid["facts"]["frames"], 4)
            self.assert_payload_free_and_bounded(valid, root)

            damaged = bytearray(target.read_bytes())
            struct.pack_into("<I", damaged, 28, 1)
            target.write_bytes(damaged)
            invalid = validate_wav(target)
            self.assertFalse(invalid["valid"])
            self.assertTrue(any("byte rate" in item for item in invalid["errors"]))

    def test_mp3_requires_consecutive_frames_and_cross_checks_injected_probe(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            target = root / "music.mp3"
            target.write_bytes(_mp3_frames())
            probe = {
                "format": {"format_name": "mp3", "duration": "0.052"},
                "streams": [
                    {
                        "codec_type": "audio",
                        "codec_name": "mp3",
                        "sample_rate": "44100",
                        "channels": 2,
                    }
                ],
            }
            valid = validate_mp3(target, probe)
            self.assertTrue(valid["valid"], valid["errors"])
            self.assertEqual(valid["facts"]["mpegVersion"], "1")
            self.assertEqual(valid["facts"]["sampleRate"], 44_100)
            self.assertTrue(valid["facts"]["ffprobe"]["valid"])
            self.assert_payload_free_and_bounded(valid, root)

            mismatched = json.loads(json.dumps(probe))
            mismatched["streams"][0]["sample_rate"] = "48000"
            invalid_probe = validate_mp3(target, mismatched)
            self.assertFalse(invalid_probe["valid"])
            self.assertFalse(invalid_probe["facts"]["ffprobe"]["valid"])

            target.write_bytes(_mp3_frames(1))
            one_frame = validate_mp3(target)
            self.assertFalse(one_frame["valid"])
            self.assertTrue(
                any("two consecutive" in item for item in one_frame["errors"])
            )

    def test_glb_validates_v2_json_chunks_and_buffer_bounds(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw)
            target = root / "model.glb"
            document = {
                "asset": {"version": "2.0", "generator": "synthetic-test"},
                "buffers": [{"byteLength": 4}],
                "bufferViews": [{"buffer": 0, "byteOffset": 0, "byteLength": 4}],
                "accessors": [
                    {
                        "bufferView": 0,
                        "componentType": 5121,
                        "count": 4,
                        "type": "SCALAR",
                    }
                ],
                "nodes": [],
                "scenes": [{"nodes": []}],
                "scene": 0,
            }
            target.write_bytes(_glb(document, b"\x00\x01\x02\x03"))
            valid = validate_glb(target)
            self.assertTrue(valid["valid"], valid["errors"])
            self.assertEqual(valid["facts"]["containerVersion"], 2)
            self.assertEqual(valid["facts"]["bufferViewsCount"], 1)
            self.assertEqual(valid["facts"]["binBytes"], 4)
            self.assert_payload_free_and_bounded(valid, root)

            broken_document = json.loads(json.dumps(document))
            broken_document["buffers"][0]["byteLength"] = 8
            target.write_bytes(_glb(broken_document, b"\x00\x01\x02\x03"))
            invalid = validate_glb(target)
            self.assertFalse(invalid["valid"])
            self.assertTrue(
                any("BIN chunk length" in item for item in invalid["errors"])
            )

    def test_cooked_map_is_deterministic_and_detects_binary_tampering(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "map"
            root.mkdir()
            _cooked_map(root)
            first = validate_cooked_sage_map(root)
            second = validate_cooked_sage_map(root)
            self.assertTrue(first["valid"], first["errors"])
            self.assertEqual(first, second)
            self.assertEqual(first["facts"]["checkedFileCount"], 18)
            self.assertEqual(first["facts"]["requiredFileCount"], 18)
            self.assertEqual(first["facts"]["width"], 3)
            self.assertEqual(len(first["inventory"]), 18)
            self.assertTrue(first["structuralValid"])
            self.assertIsNone(first["runnable"])
            self.assertFalse(first["gameplayFidelityClaimed"])
            self.assert_payload_free_and_bounded(first, root)

            target = root / "terrain-blend-cells.u32"
            target.write_bytes(target.read_bytes() + b"\x00")
            invalid = validate_cooked_sage_map(root)
            self.assertFalse(invalid["valid"])
            self.assertTrue(any("blendCells" in item for item in invalid["errors"]))
            self.assert_payload_free_and_bounded(invalid, root)

    def test_cooked_map_backtests_paired_roads_against_nonroad_bindings(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "map"
            root.mkdir()
            _cooked_map(root)
            _install_paired_roads(root)

            result = validate_cooked_sage_map(root)

            self.assertTrue(result["valid"], result["errors"])
            self.assertTrue(result["facts"]["roadPartitionAttested"])
            self.assertTrue(result["facts"]["roadInventoryAttested"])
            self.assertTrue(result["facts"]["objectBindingPartitionAttested"])
            self.assertEqual(result["facts"]["objectCount"], 6)
            self.assertEqual(result["facts"]["roadControlPointCount"], 4)
            self.assertEqual(result["facts"]["nonRoadObjectCount"], 2)
            self.assertEqual(result["facts"]["roadSegmentCount"], 2)
            self.assertEqual(result["facts"]["objectBindingPlacementCount"], 2)

    def test_cooked_map_reconstructs_unresolved_road_diagnostics(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "map"
            root.mkdir()
            _cooked_map(root)
            _install_paired_roads(root)
            _make_last_road_pair_unresolved(root)

            result = validate_cooked_sage_map(root)

            self.assertTrue(result["valid"], result["errors"])
            self.assertTrue(result["facts"]["roadInventoryAttested"])
            self.assertEqual(result["facts"]["roadSegmentCount"], 1)
            self.assertEqual(
                result["facts"]["unresolvedRoadControlPointCount"],
                2,
            )

    def test_cooked_map_rejects_road_and_partition_tampering(self) -> None:
        cases = (
            ("missing-roads", "roads.json"),
            ("road-schema", "schema contract"),
            ("reordered-controls", "control points"),
            ("invented-control", "control points"),
            ("control-position", "control points"),
            ("segment", "segments"),
            ("invented-diagnostic", "diagnostics"),
            ("summary", "summary/count/status"),
            ("map-summary", "roadSummary"),
            ("map-reference", "roads reference"),
            ("pairing-policy", "pairing policy"),
            ("object-index", "source indices"),
            ("road-nonroad-partition", "non-road objects"),
            ("binding-total", "type/placement records"),
            ("binding-type", "type/placement records"),
        )
        for case, expected_error in cases:
            with self.subTest(case=case):
                with tempfile.TemporaryDirectory() as raw:
                    root = Path(raw) / "map"
                    root.mkdir()
                    _cooked_map(root)
                    _install_paired_roads(root)

                    roads_path = root / "roads.json"
                    objects_path = root / "objects.json"
                    bindings_path = root / "object-bindings.json"
                    map_path = root / "map.json"
                    if case == "missing-roads":
                        roads_path.unlink()
                    elif case in {
                        "reordered-controls",
                        "road-schema",
                        "invented-control",
                        "control-position",
                        "segment",
                        "invented-diagnostic",
                        "summary",
                        "pairing-policy",
                    }:
                        roads = json.loads(roads_path.read_text(encoding="utf-8"))
                        if case == "road-schema":
                            roads["schema"] = "openbfme.sage-road-list"
                        elif case == "reordered-controls":
                            roads["controlPoints"][0], roads["controlPoints"][1] = (
                                roads["controlPoints"][1],
                                roads["controlPoints"][0],
                            )
                        elif case == "invented-control":
                            roads["controlPoints"].append(
                                dict(roads["controlPoints"][-1])
                            )
                        elif case == "control-position":
                            roads["controlPoints"][0]["sagePosition"][0] += 1.0
                        elif case == "segment":
                            roads["segments"][0]["endSourceIndex"] = 5
                        elif case == "invented-diagnostic":
                            roads["unresolvedDiagnostics"].append(
                                {
                                    "sourceIndex": 2,
                                    "roadId": "RoadAlpha",
                                    "wireType": 2,
                                    "reason": "unpaired-segment-start",
                                }
                            )
                        elif case == "summary":
                            roads["summary"]["segmentCount"] = 1
                        else:
                            roads["pairingPolicy"] = "adjacent-wire-pairs"
                        _write_json(roads_path, roads)
                    elif case in {"map-summary", "map-reference"}:
                        map_data = json.loads(map_path.read_text(encoding="utf-8"))
                        if case == "map-summary":
                            map_data["roadSummary"]["controlPointCount"] = 3
                        else:
                            map_data["roads"] = "road-list.json"
                        _write_json(map_path, map_data)
                    elif case in {"object-index", "road-nonroad-partition"}:
                        objects = json.loads(objects_path.read_text(encoding="utf-8"))
                        if case == "object-index":
                            objects["objects"][2]["index"] = 3
                        else:
                            objects["objects"][0].update(
                                {
                                    "roadType": 2,
                                    "sagePosition": [0.0, 0.0, 0.0],
                                    "godotPosition": [0.0, 0.0, 0.0],
                                }
                            )
                        _write_json(objects_path, objects)
                    else:
                        bindings = json.loads(bindings_path.read_text(encoding="utf-8"))
                        if case == "binding-total":
                            bindings["records"][0]["placementCount"] = 2
                        else:
                            bindings["records"][0]["typeName"] = "RoadAlpha"
                        _write_json(bindings_path, bindings)

                    result = validate_cooked_sage_map(root)

                    self.assertFalse(result["valid"], (case, result["errors"]))
                    self.assertTrue(
                        any(expected_error in item for item in result["errors"]),
                        (case, result["errors"]),
                    )

    def test_sixteen_bit_legacy_blend_contract_is_exact_and_tamper_evident(
        self,
    ) -> None:
        for version, expected_present in (
            (8, {"impassability"}),
            (9, {"impassability"}),
            (
                11,
                {"impassability", "impassabilityToPlayers", "passageWidths"},
            ),
        ):
            with self.subTest(version=version):
                with tempfile.TemporaryDirectory() as raw:
                    root = Path(raw) / "map"
                    root.mkdir()
                    _cooked_map(root)
                    _attest_versioned_blend_layers(root, version)

                    first = validate_cooked_sage_map(root)
                    second = validate_cooked_sage_map(root)
                    self.assertTrue(first["valid"], first["errors"])
                    self.assertEqual(first, second)
                    self.assertEqual(
                        first["facts"]["checkedFileCount"],
                        first["facts"]["requiredFileCount"],
                    )
                    self.assertFalse(first["gameplayFidelityClaimed"])
                    for stem in (
                        "terrain-blend-cells",
                        "terrain-three-way-blend-cells",
                        "terrain-cliff-cells",
                    ):
                        self.assertTrue((root / f"{stem}.u16").is_file())
                        self.assertFalse((root / f"{stem}.u32").exists())

                    terrain_path = root / "terrain.json"
                    original = json.loads(terrain_path.read_text(encoding="utf-8"))
                    contract = original["sourceLayers"]["versionedBlendLayers"]
                    self.assertEqual(contract["blendCellWordBits"], 16)
                    self.assertEqual(
                        {
                            name
                            for name, descriptor in contract["layers"].items()
                            if descriptor["present"] is True
                        },
                        expected_present,
                    )

                    tampered = json.loads(json.dumps(original))
                    tampered["sourceLayers"]["versionedBlendLayers"][
                        "blendCellWordBits"
                    ] = 32
                    _write_json(terrain_path, tampered)
                    wrong_word_width = validate_cooked_sage_map(root)
                    self.assertFalse(wrong_word_width["valid"])
                    self.assertTrue(
                        any(
                            "blendCellWordBits evidence is inconsistent" in item
                            for item in wrong_word_width["errors"]
                        ),
                        wrong_word_width["errors"],
                    )

                    tampered = json.loads(json.dumps(original))
                    tampered["sourceLayers"]["layers"]["blendCells"][
                        "cellSizeBytes"
                    ] = 4
                    _write_json(terrain_path, tampered)
                    wrong_cell_size = validate_cooked_sage_map(root)
                    self.assertFalse(wrong_cell_size["valid"])
                    self.assertTrue(
                        any(
                            "blendCells cell metadata is inconsistent" in item
                            for item in wrong_cell_size["errors"]
                        ),
                        wrong_cell_size["errors"],
                    )

                    _write_json(terrain_path, original)
                    conflicting_path = root / "terrain-blend-cells.u32"
                    conflicting_path.write_bytes(b"\x00" * 24)
                    conflicting_width = validate_cooked_sage_map(root)
                    self.assertFalse(conflicting_width["valid"])
                    self.assertTrue(
                        any(
                            "blendCells has a conflicting 32-bit file" in item
                            for item in conflicting_width["errors"]
                        ),
                        conflicting_width["errors"],
                    )

    def test_duplicate_side_records_require_reconstructable_runtime_semantics(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "map"
            root.mkdir()
            _cooked_map(root)
            _attest_map_profile(root, map_kind="multiplayer", runnable=True)
            setup_path = root / "setup.json"
            setup = json.loads(setup_path.read_text(encoding="utf-8"))
            player = {
                "index": 0,
                "name": "PlyrDuplicate",
                "properties": [
                    {
                        "name": "playerName",
                        "wireType": "ascii-string",
                        "wireTypeCode": 3,
                        "value": "PlyrDuplicate",
                    }
                ],
                "buildList": [],
            }
            setup.update(
                {
                    "scenarioPlayerCount": 2,
                    "scenarioPlayers": [
                        player,
                        {**json.loads(json.dumps(player)), "index": 1},
                    ],
                    "scriptListCount": 2,
                    "nonemptyScriptListCount": 0,
                }
            )
            _write_json(setup_path, setup)

            missing = validate_cooked_sage_map(root)
            self.assertFalse(missing["valid"])
            self.assertTrue(
                any(
                    "lossless side runtime-semantics evidence is missing" in item
                    for item in missing["errors"]
                ),
                missing["errors"],
            )
            self.assertFalse(missing["facts"]["sideSemanticsAttested"])

    def test_default_team_owner_repairs_are_reconstructed_and_tamper_evident(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "map"
            root.mkdir()
            _cooked_map(root)
            _attest_map_profile(root, map_kind="multiplayer", runnable=True)
            setup_path = root / "setup.json"
            setup = json.loads(setup_path.read_text(encoding="utf-8"))
            players = [
                {
                    "index": index,
                    "name": name,
                    "properties": [
                        {
                            "name": "playerName",
                            "wireType": "ascii-string",
                            "wireTypeCode": 3,
                            "value": name,
                        }
                    ],
                    "buildList": [],
                }
                for index, name in enumerate(("PlyrSynthetic", "PlyrOther"))
            ]
            teams = [
                {
                    "index": 0,
                    "name": "teamPlyrSynthetic",
                    "nameOccurrence": 1,
                    "owner": "PlyrOther",
                    "properties": [
                        {
                            "name": "teamName",
                            "wireType": "ascii-string",
                            "wireTypeCode": 3,
                            "value": "teamPlyrSynthetic",
                        },
                        {
                            "name": "teamOwner",
                            "wireType": "ascii-string",
                            "wireTypeCode": 3,
                            "value": "PlyrOther",
                        },
                    ],
                },
                {
                    "index": 1,
                    "name": "teamPlyrOther",
                    "nameOccurrence": 1,
                    "owner": "PlyrOther",
                    "properties": [
                        {
                            "name": "teamName",
                            "wireType": "ascii-string",
                            "wireTypeCode": 3,
                            "value": "teamPlyrOther",
                        },
                        {
                            "name": "teamOwner",
                            "wireType": "ascii-string",
                            "wireTypeCode": 3,
                            "value": "PlyrOther",
                        },
                    ],
                },
            ]
            repairs = [
                {
                    "playerSourceIndex": 0,
                    "teamSourceIndex": 0,
                    "teamName": "teamPlyrSynthetic",
                    "authoredOwner": "PlyrOther",
                    "runtimeOwner": "PlyrSynthetic",
                }
            ]
            setup.update(
                {
                    "scenarioPlayerCount": len(players),
                    "scenarioPlayers": players,
                    "teamCount": len(teams),
                    "teams": teams,
                    "teamRuntimeSemantics": {
                        "schema": "openbfme.sage-team-runtime-semantics",
                        "schemaVersion": 0,
                        "rawTeamPolicy": (
                            "source-order-preserved-no-synthesis-rename-or-merge"
                        ),
                        "defaultTeamLookupPolicy": (
                            "exact-case-sensitive-first-source-wins"
                        ),
                        "ownerRepairPolicy": (
                            "ea-validate-sides-default-team-owner-repair"
                        ),
                        "defaultTeamOwnerRepairs": repairs,
                        "evidence": {
                            "playerRecordCount": len(players),
                            "orderedPlayerRecordsSha256": _canonical_json_sha256(
                                players
                            ),
                            "teamRecordCount": len(teams),
                            "orderedTeamRecordsSha256": _canonical_json_sha256(teams),
                            "defaultTeamOwnerRepairCount": len(repairs),
                            "defaultTeamOwnerRepairsSha256": (
                                _canonical_json_sha256(repairs)
                            ),
                        },
                    },
                }
            )
            _write_json(setup_path, setup)

            clean = validate_cooked_sage_map(root)
            self.assertTrue(clean["valid"], clean["errors"])
            self.assertTrue(clean["facts"]["teamSemanticsAttested"])
            self.assertEqual(
                clean["facts"]["teamSemanticsEvidence"]["defaultTeamOwnerRepairCount"],
                1,
            )

            tampered = json.loads(json.dumps(setup))
            tampered["teamRuntimeSemantics"]["defaultTeamOwnerRepairs"][0][
                "runtimeOwner"
            ] = "PlyrOther"
            _write_json(setup_path, tampered)
            damaged = validate_cooked_sage_map(root)
            self.assertFalse(damaged["valid"])
            self.assertTrue(
                any(
                    "default-team owner repairs are inconsistent" in item
                    for item in damaged["errors"]
                ),
                damaged["errors"],
            )

            missing_contract = json.loads(json.dumps(setup))
            del missing_contract["teamRuntimeSemantics"]
            _write_json(setup_path, missing_contract)
            missing = validate_cooked_sage_map(root)
            self.assertFalse(missing["valid"])
            self.assertTrue(
                any(
                    "lossless team runtime-semantics evidence is missing" in item
                    for item in missing["errors"]
                ),
                missing["errors"],
            )

    def test_optional_lobby_source_absence_is_exact_and_tamper_evident(
        self,
    ) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "map"
            root.mkdir()
            _cooked_map(root)
            _attest_map_profile(root, map_kind="multiplayer", runnable=True)
            expected_layout = {
                "sourceVersion": None,
                "present": False,
                "absence": "not-present-in-source",
                "structuralConversion": "lossless-source-absence-preservation",
                "runtimeDefaultParity": (
                    "not-applicable-runtime-does-not-consult-chunk"
                ),
            }
            setup_path = root / "setup.json"
            setup = json.loads(setup_path.read_text(encoding="utf-8"))
            setup.update(
                {
                    "lobbySlotCount": 0,
                    "lobbySlots": [],
                    "lobbySourceStatus": "not-present-in-source",
                    "sourceVersions": {"SidesList": 5},
                    "sourceChunkLayouts": {
                        "MPPositionList": expected_layout,
                    },
                    "crossChecks": {
                        "startCountWithinLobbySlots": ("not-applicable-source-absent")
                    },
                }
            )
            _write_json(setup_path, setup)
            chunks_path = root / "chunks.json"
            chunks = json.loads(chunks_path.read_text(encoding="utf-8"))
            chunks["conversionEvidence"]["sourceChunkLayouts"] = {
                "MPPositionList": expected_layout,
            }
            _write_json(chunks_path, chunks)

            clean = validate_cooked_sage_map(root)
            self.assertTrue(clean["valid"], clean["errors"])
            self.assertTrue(clean["facts"]["lobbySourceAbsenceAttested"])

            tampered = json.loads(json.dumps(setup))
            tampered["lobbySlots"] = [{"index": 1}]
            _write_json(setup_path, tampered)
            synthesized = validate_cooked_sage_map(root)
            self.assertFalse(synthesized["valid"])
            self.assertTrue(
                any(
                    "synthesizes lobby slots" in item for item in synthesized["errors"]
                ),
                synthesized["errors"],
            )

            _write_json(setup_path, setup)
            inserted_chunks = json.loads(json.dumps(chunks))
            inserted_chunks["chunks"].append({"name": "MPPositionList", "version": 0})
            _write_json(chunks_path, inserted_chunks)
            inserted = validate_cooked_sage_map(root)
            self.assertFalse(inserted["valid"])
            self.assertTrue(
                any(
                    "contains absent MPPositionList" in item
                    for item in inserted["errors"]
                ),
                inserted["errors"],
            )

    def test_versioned_blend_layers_reject_removal_insertion_and_tampering(
        self,
    ) -> None:
        for version in (14, 15, 16):
            with self.subTest(version=version):
                with tempfile.TemporaryDirectory() as raw:
                    root = Path(raw) / "map"
                    root.mkdir()
                    _cooked_map(root)
                    _attest_versioned_blend_layers(root, version)

                    clean = validate_cooked_sage_map(root)
                    self.assertTrue(clean["valid"], clean["errors"])
                    self.assertEqual(
                        clean["facts"]["checkedFileCount"],
                        clean["facts"]["requiredFileCount"],
                    )
                    self.assertFalse(clean["gameplayFidelityClaimed"])

                    present_path = (
                        root / _VERSIONED_BLEND_LAYER_PATHS["impassabilityToPlayers"]
                    )
                    present_payload = present_path.read_bytes()
                    present_path.unlink()
                    removed_file = validate_cooked_sage_map(root)
                    self.assertFalse(removed_file["valid"])
                    self.assertTrue(
                        any(
                            "terrain-impassability-to-players.bit" in item
                            for item in removed_file["errors"]
                        ),
                        removed_file["errors"],
                    )
                    present_path.write_bytes(present_payload)

                    present_path.write_bytes(
                        bytes([present_payload[0] ^ 1]) + present_payload[1:]
                    )
                    tampered_file = validate_cooked_sage_map(root)
                    self.assertFalse(tampered_file["valid"])
                    self.assertTrue(
                        any(
                            "file hash disagrees" in item
                            for item in tampered_file["errors"]
                        ),
                        tampered_file["errors"],
                    )
                    present_path.write_bytes(present_payload)

                    absent_path = root / _VERSIONED_BLEND_LAYER_PATHS["visibility"]
                    absent_path.write_bytes(b"\x00\x00")
                    inserted_file = validate_cooked_sage_map(root)
                    self.assertFalse(inserted_file["valid"])
                    self.assertTrue(
                        any(
                            "exists despite source-version absence" in item
                            for item in inserted_file["errors"]
                        ),
                        inserted_file["errors"],
                    )
                    absent_path.unlink()

                    terrain_path = root / "terrain.json"
                    original_terrain = json.loads(
                        terrain_path.read_text(encoding="utf-8")
                    )
                    terrain = json.loads(json.dumps(original_terrain))
                    terrain["sourceLayers"]["versionedBlendLayers"]["layers"][
                        "inventedLayer"
                    ] = {
                        "present": False,
                        "absence": "not-present-in-source-version",
                    }
                    _write_json(terrain_path, terrain)
                    inserted_declaration = validate_cooked_sage_map(root)
                    self.assertFalse(inserted_declaration["valid"])
                    self.assertTrue(
                        any(
                            "layer declarations are not exact" in item
                            for item in inserted_declaration["errors"]
                        ),
                        inserted_declaration["errors"],
                    )

                    terrain = json.loads(json.dumps(original_terrain))
                    terrain["blend"]["gridStats"]["visible"] = 0
                    _write_json(terrain_path, terrain)
                    synthesized_absent_grid = validate_cooked_sage_map(root)
                    self.assertFalse(synthesized_absent_grid["valid"])
                    self.assertTrue(
                        any(
                            "grid statistics synthesize or omit a layer" in item
                            for item in synthesized_absent_grid["errors"]
                        ),
                        synthesized_absent_grid["errors"],
                    )

                    terrain = json.loads(json.dumps(original_terrain))
                    del terrain["sourceLayers"]["versionedBlendLayers"]["layers"][
                        "taintability"
                    ]
                    _write_json(terrain_path, terrain)
                    removed_declaration = validate_cooked_sage_map(root)
                    self.assertFalse(removed_declaration["valid"])
                    self.assertTrue(
                        any(
                            "layer declarations are not exact" in item
                            for item in removed_declaration["errors"]
                        ),
                        removed_declaration["errors"],
                    )

                    terrain = json.loads(json.dumps(original_terrain))
                    terrain["sourceLayers"]["versionedBlendLayers"]["layers"][
                        "visibility"
                    ]["present"] = True
                    _write_json(terrain_path, terrain)
                    tampered_presence = validate_cooked_sage_map(root)
                    self.assertFalse(tampered_presence["valid"])
                    self.assertTrue(
                        any(
                            "visibility absence declaration" in item
                            for item in tampered_presence["errors"]
                        ),
                        tampered_presence["errors"],
                    )

                    terrain = json.loads(json.dumps(original_terrain))
                    terrain["blend"]["version"] = 19
                    _write_json(terrain_path, terrain)
                    tampered_version = validate_cooked_sage_map(root)
                    self.assertFalse(tampered_version["valid"])
                    self.assertTrue(
                        any(
                            "source version is unsupported" in item
                            for item in tampered_version["errors"]
                        ),
                        tampered_version["errors"],
                    )

    def test_one_cell_placeholder_is_structurally_valid_but_not_runnable(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "map"
            root.mkdir()
            _cooked_map(root, dimensions=(1, 1))
            _attest_map_profile(root, map_kind="placeholder", runnable=False)

            result = validate_cooked_sage_map(root)
            self.assertTrue(result["valid"], result["errors"])
            self.assertTrue(result["structuralValid"])
            self.assertFalse(result["runnable"])
            self.assertFalse(result["gameplayFidelityClaimed"])
            self.assertEqual(result["facts"]["mapKind"], "placeholder")
            self.assertEqual(result["facts"]["profileVersion"], 1)
            self.assertTrue(result["facts"]["profileAttested"])
            self.assertEqual(result["facts"]["width"], 1)
            self.assertEqual(result["facts"]["height"], 1)

            (root / "heightmap.r16").write_bytes(b"")
            damaged = validate_cooked_sage_map(root)
            self.assertFalse(damaged["valid"])
            self.assertTrue(
                any("heightmap.r16 size" in item for item in damaged["errors"])
            )

    def test_one_cell_map_cannot_claim_runnable_or_multiplayer_profile(self) -> None:
        cases = (
            ("placeholder", True, "runnable attestation"),
            ("multiplayer", True, "height dimensions"),
        )
        for map_kind, runnable, expected_error in cases:
            with self.subTest(map_kind=map_kind, runnable=runnable):
                with tempfile.TemporaryDirectory() as raw:
                    root = Path(raw) / "map"
                    root.mkdir()
                    _cooked_map(root, dimensions=(1, 1))
                    _attest_map_profile(
                        root,
                        map_kind=map_kind,
                        runnable=runnable,
                    )
                    result = validate_cooked_sage_map(root)
                    self.assertFalse(result["valid"])
                    self.assertFalse(result["structuralValid"])
                    self.assertTrue(
                        any(expected_error in item for item in result["errors"]),
                        result["errors"],
                    )

    def test_one_cell_profile_must_agree_between_map_and_setup(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "map"
            root.mkdir()
            _cooked_map(root, dimensions=(1, 1))
            _attest_map_profile(root, map_kind="placeholder", runnable=False)
            setup_path = root / "setup.json"
            setup = json.loads(setup_path.read_text(encoding="utf-8"))
            setup["mapKind"] = "library"
            _write_json(setup_path, setup)

            result = validate_cooked_sage_map(root)
            self.assertFalse(result["valid"])
            self.assertTrue(
                any("profile evidence disagrees" in item for item in result["errors"])
            )

    def test_one_cell_missing_unknown_or_unsupported_profile_stays_strict(self) -> None:
        cases = (
            (None, None),
            ("future-map-kind", 1),
            ("placeholder", 2),
        )
        for map_kind, profile_version in cases:
            with self.subTest(map_kind=map_kind, profile_version=profile_version):
                with tempfile.TemporaryDirectory() as raw:
                    root = Path(raw) / "map"
                    root.mkdir()
                    _cooked_map(root, dimensions=(1, 1))
                    if map_kind is not None and profile_version is not None:
                        _attest_map_profile(
                            root,
                            map_kind=map_kind,
                            profile_version=profile_version,
                            runnable=False,
                        )
                    result = validate_cooked_sage_map(root)
                    self.assertFalse(result["valid"])
                    self.assertTrue(
                        any("height dimensions" in item for item in result["errors"]),
                        result["errors"],
                    )

        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "zero-map"
            root.mkdir()
            _cooked_map(root, dimensions=(1, 1))
            _attest_map_profile(root, map_kind="placeholder", runnable=False)
            terrain_path = root / "terrain.json"
            terrain = json.loads(terrain_path.read_text(encoding="utf-8"))
            terrain["height"]["width"] = 0
            terrain["height"]["area"] = 0
            _write_json(terrain_path, terrain)
            zero = validate_cooked_sage_map(root)
            self.assertFalse(zero["valid"])
            self.assertTrue(any("height dimensions" in item for item in zero["errors"]))

    def test_cooked_map_requires_all_files_and_rejects_descriptor_escape(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            root = Path(raw) / "map"
            root.mkdir()
            missing = validate_cooked_sage_map(root)
            self.assertFalse(missing["valid"])
            self.assertGreaterEqual(missing["errorCount"], 17)
            self.assert_payload_free_and_bounded(missing, root)

            _cooked_map(root)
            terrain_path = root / "terrain.json"
            terrain = json.loads(terrain_path.read_text(encoding="utf-8"))
            terrain["sourceLayers"]["layers"]["tileIndices"]["path"] = "../outside.bin"
            _write_json(terrain_path, terrain)
            escaped = validate_cooked_sage_map(root)
            self.assertFalse(escaped["valid"])
            self.assertTrue(
                any("tileIndices path" in item for item in escaped["errors"])
            )
            self.assertNotIn("outside.bin", json.dumps(escaped))

    def test_dispatcher_is_fail_closed_and_json_ready(self) -> None:
        with tempfile.TemporaryDirectory() as raw:
            target = Path(raw) / "image.png"
            target.write_bytes(_rgba_png(1, 1))
            self.assertTrue(validate_native_output("png", target)["valid"])
            unknown = validate_native_output("retail-opaque", target)
            self.assertFalse(unknown["valid"])
            self.assertEqual(unknown["family"], "unknown")
            json.dumps(unknown)


if __name__ == "__main__":
    unittest.main()
