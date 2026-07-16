"""Payload-free backtests for runtime-native importer outputs.

The validators in this module deliberately do not trust converter success or file
extensions.  Each function returns deterministic, JSON-serializable evidence and
never includes source/output bytes, decoded pixels, tags, node names, or absolute
paths.  Validation failures are reported rather than raised, and diagnostics are
bounded so malformed retail-owned inputs cannot turn a report into a data dump.
"""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path, PurePosixPath
import re
import struct
from typing import Any, BinaryIO
import zlib


EVIDENCE_SCHEMA = "openbfme.native-output-backtest"
EVIDENCE_SCHEMA_VERSION = 0

MAX_ERRORS = 32
MAX_ERROR_CHARS = 240
MAX_JSON_BYTES = 64 * 1024 * 1024
MAX_PNG_BYTES = 256 * 1024 * 1024
MAX_PNG_DIMENSION = 32_768
MAX_PNG_DECODED_BYTES = 512 * 1024 * 1024
MAX_GLTF_COLLECTION_ITEMS = 1_000_000
MAX_GLTF_CHUNKS = 64
MAX_WAV_CHUNKS = 4_096
MAX_MP3_SCAN_BYTES = 1024 * 1024
MAX_ID3_BYTES = 64 * 1024 * 1024
HASH_BLOCK_BYTES = 1024 * 1024

_PNG_SIGNATURE = b"\x89PNG\r\n\x1a\n"
_GLB_MAGIC = b"glTF"
_GLB_JSON = 0x4E4F534A
_GLB_BIN = 0x004E4942
_PCM_SUBFORMAT_GUID = bytes.fromhex("0100000000001000800000aa00389b71")
_SHA256_HEX = frozenset("0123456789abcdef")

_MAP_JSON_SCHEMAS = {
    "map.json": "openbfme.map",
    "terrain.json": "openbfme.sage-terrain",
    "water.json": "openbfme.sage-water",
    "triggers.json": "openbfme.sage-triggers",
    "objects.json": "openbfme.sage-map-objects",
    "roads.json": "openbfme.sage-roads",
    "object-bindings.json": "openbfme.sage-object-bindings",
    "waypoints.json": "openbfme.sage-waypoints",
    "setup.json": "openbfme.sage-multiplayer-setup",
    "chunks.json": "openbfme.sage-map-inventory",
}


def _map_grid_binaries(blend_cell_word_bits: int) -> dict[str, tuple[str, int]]:
    blend_cell_size = blend_cell_word_bits // 8
    suffix = f"u{blend_cell_word_bits}"
    return {
        "tileIndices": ("terrain-tile-indices.u16", 2),
        "blendCells": (f"terrain-blend-cells.{suffix}", blend_cell_size),
        "threeWayBlendCells": (
            f"terrain-three-way-blend-cells.{suffix}",
            blend_cell_size,
        ),
        "cliffCells": (f"terrain-cliff-cells.{suffix}", blend_cell_size),
    }


_MAP_GRID_BINARIES = _map_grid_binaries(32)
_MAP_TABLE_BINARIES = {
    "blendDescriptions": ("terrain-blend-descriptions.bin", 18, "rawBlendCount"),
    "cliffMappings": ("terrain-cliff-mappings.bin", 38, "rawCliffCount"),
}
_MAP_PROFILE_FIELDS = ("mapKind", "profileVersion", "runnable")
_MAP_PROFILE_VERSION = 1
_MAP_PROFILE_RUNNABLE = {
    "multiplayer": True,
    "scenario": True,
    "library": False,
    "placeholder": False,
}
_BLEND_VERSIONED_LAYER_ORDER = (
    "impassability",
    "impassabilityToPlayers",
    "passageWidths",
    "taintability",
    "extraPassability",
    "flammability",
    "visibility",
)
_BLEND_VERSIONED_LAYER_MIN_VERSION = {
    "impassability": 7,
    "impassabilityToPlayers": 10,
    "passageWidths": 11,
    "taintability": 14,
    "extraPassability": 15,
    "flammability": 16,
    "visibility": 17,
}
_BLEND_VERSIONED_LAYER_PATHS = {
    "impassability": "impassability.bit",
    "impassabilityToPlayers": "terrain-impassability-to-players.bit",
    "passageWidths": "terrain-passage-widths.bit",
    "taintability": "terrain-taintability.bit",
    "extraPassability": "terrain-extra-passability.bit",
    "flammability": "terrain-flammability.u8",
    "visibility": "terrain-visibility.bit",
}
_BLEND_CELL_WORD_BITS_BY_VERSION = {
    8: 16,
    9: 16,
    11: 16,
    14: 32,
    15: 32,
    16: 32,
    17: 32,
    18: 32,
}
_SUPPORTED_BLEND_VERSIONS = frozenset(_BLEND_CELL_WORD_BITS_BY_VERSION)
_LOSSLESS_LEGACY_BLEND_VERSIONS = frozenset({8, 9, 11, 14, 15, 16})
_BLEND_ABSENCE_REASON = "not-present-in-source-version"
_BLEND_STRUCTURAL_CONVERSION = "lossless-source-layer-preservation"
_BLEND_RUNTIME_DEFAULT_PARITY = "unproven"

_ROAD_COORDINATE_TRANSFORM = "godot=(sage.x,sage.z,-sage.y)"
_ROAD_PAIRING_POLICY = "source-order-exact-wire-2-then-4-same-road-id"
_ROAD_CURVE_RECONSTRUCTION = "not-attempted"


class _Errors:
    def __init__(self) -> None:
        self.items: list[str] = []
        self.total = 0

    def add(self, message: object) -> None:
        self.total += 1
        if len(self.items) >= MAX_ERRORS:
            return
        cleaned = " ".join(str(message).split())
        if not cleaned:
            cleaned = "validation failed"
        self.items.append(cleaned[:MAX_ERROR_CHARS])


class _DuplicateJsonKey(ValueError):
    pass


def _base_evidence(family: str) -> dict[str, Any]:
    return {
        "schema": EVIDENCE_SCHEMA,
        "schemaVersion": EVIDENCE_SCHEMA_VERSION,
        "family": family,
        "valid": False,
        "size": None,
        "sha256": None,
        "facts": {},
        "errors": [],
        "errorCount": 0,
        "errorsTruncated": False,
    }


def _finish(evidence: dict[str, Any], errors: _Errors) -> dict[str, Any]:
    evidence["errors"] = errors.items
    evidence["errorCount"] = errors.total
    evidence["errorsTruncated"] = errors.total > len(errors.items)
    evidence["valid"] = errors.total == 0
    return evidence


def _is_plain_int(value: Any, *, minimum: int = 0, maximum: int | None = None) -> bool:
    if not isinstance(value, int) or isinstance(value, bool) or value < minimum:
        return False
    return maximum is None or value <= maximum


def _is_sha256(value: Any) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and value == value.lower()
        and all(character in _SHA256_HEX for character in value)
    )


def _canonical_json_sha256(value: Any) -> str:
    payload = json.dumps(
        value,
        ensure_ascii=False,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _hash_stream(stream: BinaryIO) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    while True:
        block = stream.read(HASH_BLOCK_BYTES)
        if not block:
            break
        digest.update(block)
        size += len(block)
    return size, digest.hexdigest()


def _identity(path: Path, errors: _Errors) -> tuple[int | None, str | None]:
    try:
        if path.is_symlink():
            errors.add("artifact must not be a symbolic link")
            return None, None
        if not path.is_file():
            errors.add("artifact is not a regular file")
            return None, None
        before = path.stat()
        with path.open("rb") as stream:
            size, digest = _hash_stream(stream)
        after = path.stat()
        if (
            size != before.st_size
            or after.st_size != before.st_size
            or after.st_mtime_ns != before.st_mtime_ns
        ):
            errors.add("artifact changed while it was being backtested")
            return size, digest
        return size, digest
    except OSError:
        errors.add("artifact could not be read")
        return None, None


def _read_bounded(
    path: Path, maximum: int, errors: _Errors, label: str
) -> bytes | None:
    try:
        size = path.stat().st_size
        if size > maximum:
            errors.add(f"{label} exceeds the {maximum}-byte validation limit")
            return None
        payload = path.read_bytes()
        if len(payload) != size:
            errors.add(f"{label} changed while it was being read")
            return None
        return payload
    except OSError:
        errors.add(f"{label} could not be read")
        return None


def _strict_json_bytes(payload: bytes, errors: _Errors, label: str) -> Any | None:
    def object_pairs(pairs: list[tuple[str, Any]]) -> dict[str, Any]:
        result: dict[str, Any] = {}
        for key, value in pairs:
            if key in result:
                raise _DuplicateJsonKey(key)
            result[key] = value
        return result

    def reject_constant(_value: str) -> None:
        raise ValueError("non-finite JSON number")

    try:
        text = payload.decode("utf-8")
    except UnicodeDecodeError:
        errors.add(f"{label} is not valid UTF-8")
        return None
    try:
        return json.loads(
            text,
            object_pairs_hook=object_pairs,
            parse_constant=reject_constant,
        )
    except _DuplicateJsonKey:
        errors.add(f"{label} contains a duplicate object key")
    except (json.JSONDecodeError, ValueError, RecursionError):
        errors.add(f"{label} is not strict JSON")
    return None


def _decode_png_rows(
    raw: bytes,
    *,
    width: int,
    height: int,
    bits_per_pixel: int,
    interlace: int,
    errors: _Errors,
) -> int:
    passes = [(0, 0, 1, 1)]
    if interlace == 1:
        passes = [
            (0, 0, 8, 8),
            (4, 0, 8, 8),
            (0, 4, 4, 8),
            (2, 0, 4, 4),
            (0, 2, 2, 4),
            (1, 0, 2, 2),
            (0, 1, 1, 2),
        ]
    offset = 0
    decoded = 0
    filter_bpp = max(1, (bits_per_pixel + 7) // 8)
    for start_x, start_y, step_x, step_y in passes:
        pass_width = 0 if width <= start_x else (width - start_x + step_x - 1) // step_x
        pass_height = (
            0 if height <= start_y else (height - start_y + step_y - 1) // step_y
        )
        if pass_width == 0 or pass_height == 0:
            continue
        row_bytes = (pass_width * bits_per_pixel + 7) // 8
        previous = bytearray(row_bytes)
        for _ in range(pass_height):
            if offset >= len(raw):
                errors.add("PNG decompressed scanlines are truncated")
                return decoded
            filter_type = raw[offset]
            offset += 1
            if filter_type > 4:
                errors.add("PNG scanline uses an invalid filter type")
                return decoded
            end = offset + row_bytes
            if end > len(raw):
                errors.add("PNG decompressed scanline is truncated")
                return decoded
            current = bytearray(raw[offset:end])
            offset = end
            for index in range(row_bytes):
                left = current[index - filter_bpp] if index >= filter_bpp else 0
                above = previous[index]
                upper_left = previous[index - filter_bpp] if index >= filter_bpp else 0
                if filter_type == 1:
                    current[index] = (current[index] + left) & 0xFF
                elif filter_type == 2:
                    current[index] = (current[index] + above) & 0xFF
                elif filter_type == 3:
                    current[index] = (current[index] + ((left + above) // 2)) & 0xFF
                elif filter_type == 4:
                    predictor = left + above - upper_left
                    pa = abs(predictor - left)
                    pb = abs(predictor - above)
                    pc = abs(predictor - upper_left)
                    selected = (
                        left
                        if pa <= pb and pa <= pc
                        else above
                        if pb <= pc
                        else upper_left
                    )
                    current[index] = (current[index] + selected) & 0xFF
            previous = current
            decoded += row_bytes
    if offset != len(raw):
        errors.add("PNG decompressed stream has trailing scanline data")
    return decoded


def validate_png(path: Path | str) -> dict[str, Any]:
    """Decode and structurally validate a PNG without returning pixel bytes."""

    source = Path(path)
    errors = _Errors()
    evidence = _base_evidence("png")
    size, digest = _identity(source, errors)
    evidence["size"], evidence["sha256"] = size, digest
    if size is None:
        return _finish(evidence, errors)
    payload = _read_bounded(source, MAX_PNG_BYTES, errors, "PNG")
    if payload is None:
        return _finish(evidence, errors)
    if not payload.startswith(_PNG_SIGNATURE):
        errors.add("PNG signature is invalid")
        return _finish(evidence, errors)

    offset = len(_PNG_SIGNATURE)
    ihdr: tuple[int, int, int, int, int] | None = None
    idat_parts: list[bytes] = []
    saw_iend = False
    saw_plte = False
    idat_ended = False
    chunk_count = 0
    palette_entries = 0
    while offset < len(payload):
        if offset + 12 > len(payload):
            errors.add("PNG chunk header is truncated")
            break
        length = struct.unpack_from(">I", payload, offset)[0]
        chunk_type = payload[offset + 4 : offset + 8]
        data_start = offset + 8
        data_end = data_start + length
        crc_end = data_end + 4
        if crc_end > len(payload):
            errors.add("PNG chunk payload is truncated")
            break
        chunk_data = payload[data_start:data_end]
        expected_crc = struct.unpack_from(">I", payload, data_end)[0]
        actual_crc = zlib.crc32(chunk_type)
        actual_crc = zlib.crc32(chunk_data, actual_crc) & 0xFFFFFFFF
        if actual_crc != expected_crc:
            errors.add("PNG chunk CRC mismatch")
        chunk_count += 1
        if chunk_count > 1_000_000:
            errors.add("PNG chunk count exceeds validation limit")
            break
        if saw_iend:
            errors.add("PNG contains data after IEND")
            break
        if chunk_type == b"IHDR":
            if ihdr is not None or chunk_count != 1 or length != 13:
                errors.add("PNG must contain one 13-byte IHDR as its first chunk")
            else:
                width, height, depth, color_type, compression, filtering, interlace = (
                    struct.unpack(">IIBBBBB", chunk_data)
                )
                if not (
                    1 <= width <= MAX_PNG_DIMENSION and 1 <= height <= MAX_PNG_DIMENSION
                ):
                    errors.add("PNG dimensions are outside validation limits")
                valid_depths = {
                    0: {1, 2, 4, 8, 16},
                    2: {8, 16},
                    3: {1, 2, 4, 8},
                    4: {8, 16},
                    6: {8, 16},
                }
                if color_type not in valid_depths or depth not in valid_depths.get(
                    color_type, set()
                ):
                    errors.add("PNG color type and bit depth are incompatible")
                if compression != 0 or filtering != 0 or interlace not in (0, 1):
                    errors.add("PNG uses an unsupported coding method")
                ihdr = (width, height, depth, color_type, interlace)
        elif chunk_type == b"PLTE":
            if ihdr is None or idat_parts or saw_plte:
                errors.add("PNG PLTE chunk ordering is invalid")
            if length == 0 or length > 768 or length % 3:
                errors.add("PNG PLTE chunk length is invalid")
            saw_plte = True
            palette_entries = length // 3
        elif chunk_type == b"IDAT":
            if ihdr is None or idat_ended:
                errors.add("PNG IDAT chunk ordering is invalid")
            idat_parts.append(chunk_data)
        elif chunk_type == b"IEND":
            if length != 0:
                errors.add("PNG IEND chunk must be empty")
            saw_iend = True
        else:
            if idat_parts:
                idat_ended = True
            if chunk_type and 65 <= chunk_type[0] <= 90:
                errors.add("PNG contains an unknown critical chunk")
        offset = crc_end

    if not saw_iend:
        errors.add("PNG is missing IEND")
    if offset != len(payload):
        errors.add("PNG container length is inconsistent")
    if ihdr is None:
        errors.add("PNG is missing a valid IHDR")
        return _finish(evidence, errors)
    width, height, depth, color_type, interlace = ihdr
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}.get(color_type)
    mode = {0: "L", 2: "RGB", 3: "P", 4: "LA", 6: "RGBA"}.get(color_type)
    if channels is None or mode is None:
        return _finish(evidence, errors)
    if color_type == 3:
        if not saw_plte:
            errors.add("indexed PNG is missing PLTE")
        elif palette_entries > (1 << depth):
            errors.add("indexed PNG palette exceeds its bit depth")
    elif color_type in (0, 4) and saw_plte:
        errors.add("grayscale PNG must not contain PLTE")
    if not idat_parts:
        errors.add("PNG is missing IDAT data")
        return _finish(evidence, errors)

    bits_per_pixel = channels * depth
    passes = (
        [(0, 0, 1, 1)]
        if interlace == 0
        else [
            (0, 0, 8, 8),
            (4, 0, 8, 8),
            (0, 4, 4, 8),
            (2, 0, 4, 4),
            (0, 2, 2, 4),
            (1, 0, 2, 2),
            (0, 1, 1, 2),
        ]
    )
    expected_inflated = 0
    for start_x, start_y, step_x, step_y in passes:
        pass_width = 0 if width <= start_x else (width - start_x + step_x - 1) // step_x
        pass_height = (
            0 if height <= start_y else (height - start_y + step_y - 1) // step_y
        )
        if pass_width and pass_height:
            expected_inflated += pass_height * (
                1 + (pass_width * bits_per_pixel + 7) // 8
            )
    if expected_inflated > MAX_PNG_DECODED_BYTES:
        errors.add("PNG decoded image exceeds validation limit")
        return _finish(evidence, errors)
    compressed = b"".join(idat_parts)
    try:
        decompressor = zlib.decompressobj()
        raw = decompressor.decompress(compressed, expected_inflated + 1)
        if decompressor.unconsumed_tail or len(raw) > expected_inflated:
            errors.add("PNG decompressed stream exceeds expected dimensions")
        else:
            raw += decompressor.flush(max(1, expected_inflated + 1 - len(raw)))
        if not decompressor.eof or decompressor.unused_data:
            errors.add("PNG IDAT zlib stream is incomplete or has trailing data")
    except zlib.error:
        errors.add("PNG IDAT zlib stream cannot be decoded")
        raw = b""
    if len(raw) != expected_inflated:
        errors.add("PNG decompressed byte count does not match its dimensions")
    decoded_bytes = _decode_png_rows(
        raw,
        width=width,
        height=height,
        bits_per_pixel=bits_per_pixel,
        interlace=interlace,
        errors=errors,
    )
    evidence["facts"] = {
        "width": width,
        "height": height,
        "mode": mode,
        "bitDepth": depth,
        "colorType": color_type,
        "interlaced": interlace == 1,
        "decodedPixelBytes": decoded_bytes,
        "idatBytes": len(compressed),
        "chunkCount": chunk_count,
    }
    return _finish(evidence, errors)


def validate_wav(path: Path | str) -> dict[str, Any]:
    """Validate a RIFF/WAVE PCM header and derive channels/rate/frame facts."""

    source = Path(path)
    errors = _Errors()
    evidence = _base_evidence("wav-pcm")
    size, digest = _identity(source, errors)
    evidence["size"], evidence["sha256"] = size, digest
    if size is None:
        return _finish(evidence, errors)
    if size < 44:
        errors.add("WAV is too small for a PCM container")
        return _finish(evidence, errors)

    fmt: tuple[int, int, int, int, int, int] | None = None
    data_size: int | None = None
    chunk_count = 0
    try:
        with source.open("rb") as stream:
            header = stream.read(12)
            if len(header) != 12 or header[:4] != b"RIFF" or header[8:] != b"WAVE":
                errors.add("WAV RIFF/WAVE header is invalid")
                return _finish(evidence, errors)
            riff_size = struct.unpack_from("<I", header, 4)[0]
            if riff_size + 8 != size:
                errors.add("WAV RIFF length does not match file size")
            container_end = min(size, riff_size + 8)
            while stream.tell() < container_end:
                chunk_header = stream.read(8)
                if len(chunk_header) != 8:
                    errors.add("WAV chunk header is truncated")
                    break
                chunk_id, chunk_size = struct.unpack("<4sI", chunk_header)
                chunk_count += 1
                if chunk_count > MAX_WAV_CHUNKS:
                    errors.add("WAV chunk count exceeds validation limit")
                    break
                chunk_start = stream.tell()
                padded_end = chunk_start + chunk_size + (chunk_size & 1)
                if padded_end > container_end:
                    errors.add("WAV chunk extends beyond the RIFF container")
                    break
                if chunk_id == b"fmt ":
                    if fmt is not None:
                        errors.add("WAV contains multiple fmt chunks")
                    if chunk_size < 16 or chunk_size > 65_536:
                        errors.add("WAV fmt chunk length is invalid")
                    else:
                        payload = stream.read(chunk_size)
                        if len(payload) != chunk_size:
                            errors.add("WAV fmt chunk is truncated")
                            break
                        audio_format, channels, rate, byte_rate, block_align, bits = (
                            struct.unpack_from("<HHIIHH", payload)
                        )
                        if audio_format == 0xFFFE:
                            if (
                                chunk_size < 40
                                or struct.unpack_from("<H", payload, 16)[0] < 22
                            ):
                                errors.add("WAV extensible PCM header is truncated")
                            elif payload[24:40] != _PCM_SUBFORMAT_GUID:
                                errors.add("WAV extensible subformat is not PCM")
                            audio_format = 1
                        fmt = (
                            audio_format,
                            channels,
                            rate,
                            byte_rate,
                            block_align,
                            bits,
                        )
                elif chunk_id == b"data":
                    if data_size is not None:
                        errors.add("WAV contains multiple data chunks")
                    data_size = chunk_size
                stream.seek(padded_end)
    except OSError:
        errors.add("WAV container could not be parsed")
        return _finish(evidence, errors)

    if fmt is None:
        errors.add("WAV is missing a fmt chunk")
        return _finish(evidence, errors)
    if data_size is None:
        errors.add("WAV is missing a data chunk")
        return _finish(evidence, errors)
    audio_format, channels, rate, byte_rate, block_align, bits = fmt
    if audio_format != 1:
        errors.add("WAV encoding is not integer PCM")
    if not 1 <= channels <= 8:
        errors.add("WAV channel count is outside runtime limits")
    if not 8_000 <= rate <= 384_000:
        errors.add("WAV sample rate is outside runtime limits")
    if bits not in (8, 16, 24, 32):
        errors.add("WAV PCM bit depth is unsupported")
    expected_align = channels * ((bits + 7) // 8)
    if block_align != expected_align:
        errors.add("WAV block alignment is inconsistent")
    if byte_rate != rate * block_align:
        errors.add("WAV byte rate is inconsistent")
    if block_align <= 0 or data_size % max(1, block_align):
        errors.add("WAV data length is not a whole number of PCM frames")
        frames = 0
    else:
        frames = data_size // block_align
    if frames == 0:
        errors.add("WAV contains no PCM frames")
    evidence["facts"] = {
        "encoding": f"pcm-{bits}",
        "channels": channels,
        "sampleRate": rate,
        "bitsPerSample": bits,
        "frames": frames,
        "dataBytes": data_size,
        "durationSeconds": frames / rate if rate > 0 else 0.0,
        "chunkCount": chunk_count,
    }
    return _finish(evidence, errors)


_MPEG1_LAYER3_BITRATES = (
    0,
    32,
    40,
    48,
    56,
    64,
    80,
    96,
    112,
    128,
    160,
    192,
    224,
    256,
    320,
    0,
)
_MPEG2_LAYER3_BITRATES = (
    0,
    8,
    16,
    24,
    32,
    40,
    48,
    56,
    64,
    80,
    96,
    112,
    128,
    144,
    160,
    0,
)


def _mp3_header(data: bytes, offset: int) -> dict[str, int | str] | None:
    if offset < 0 or offset + 4 > len(data):
        return None
    word = int.from_bytes(data[offset : offset + 4], "big")
    if word & 0xFFE00000 != 0xFFE00000:
        return None
    version_id = (word >> 19) & 0x3
    layer_id = (word >> 17) & 0x3
    bitrate_index = (word >> 12) & 0xF
    rate_index = (word >> 10) & 0x3
    padding = (word >> 9) & 0x1
    channel_mode = (word >> 6) & 0x3
    if version_id == 1 or layer_id != 1 or rate_index == 3:
        return None
    version = {0: "2.5", 2: "2", 3: "1"}[version_id]
    bitrates = _MPEG1_LAYER3_BITRATES if version_id == 3 else _MPEG2_LAYER3_BITRATES
    bitrate = bitrates[bitrate_index]
    if bitrate == 0:
        return None
    base_rate = (44_100, 48_000, 32_000)[rate_index]
    rate = (
        base_rate
        if version_id == 3
        else base_rate // 2
        if version_id == 2
        else base_rate // 4
    )
    coefficient = 144_000 if version_id == 3 else 72_000
    frame_length = coefficient * bitrate // rate + padding
    if frame_length < 24:
        return None
    return {
        "version": version,
        "versionId": version_id,
        "bitrateKbps": bitrate,
        "sampleRate": rate,
        "channels": 1 if channel_mode == 3 else 2,
        "frameLength": frame_length,
    }


def _syncsafe(value: bytes) -> int | None:
    if len(value) != 4 or any(byte & 0x80 for byte in value):
        return None
    return (value[0] << 21) | (value[1] << 14) | (value[2] << 7) | value[3]


def _positive_float(value: Any) -> float | None:
    try:
        result = float(value)
    except (TypeError, ValueError, OverflowError):
        return None
    return result if math.isfinite(result) and result > 0 else None


def _validate_ffprobe_metadata(
    metadata: Any,
    header: dict[str, int | str],
    errors: _Errors,
) -> dict[str, Any]:
    facts: dict[str, Any] = {"provided": True, "valid": False}
    if not isinstance(metadata, dict):
        errors.add("injected ffprobe metadata must be an object")
        return facts
    format_data = metadata.get("format")
    streams = metadata.get("streams")
    if (
        not isinstance(format_data, dict)
        or not isinstance(streams, list)
        or len(streams) > 64
    ):
        errors.add("injected ffprobe metadata has an invalid format or stream list")
        return facts
    format_name = format_data.get("format_name")
    if not isinstance(format_name, str) or "mp3" not in {
        item.strip().casefold() for item in format_name.split(",")
    }:
        errors.add("ffprobe container format is not MP3")
    audio_streams = [
        item
        for item in streams
        if isinstance(item, dict) and item.get("codec_type") == "audio"
    ]
    if len(audio_streams) != 1:
        errors.add("ffprobe must report exactly one audio stream")
        return facts
    stream = audio_streams[0]
    if str(stream.get("codec_name", "")).casefold() not in {"mp3", "mp3float"}:
        errors.add("ffprobe audio codec is not MP3")
    try:
        sample_rate = int(stream.get("sample_rate"))
        channels = int(stream.get("channels"))
    except (TypeError, ValueError, OverflowError):
        errors.add("ffprobe audio rate or channel count is invalid")
        return facts
    if sample_rate != header["sampleRate"]:
        errors.add("ffprobe sample rate disagrees with the MP3 frame header")
    if channels != header["channels"]:
        errors.add("ffprobe channel count disagrees with the MP3 frame header")
    duration_value = stream.get("duration", format_data.get("duration"))
    duration = None
    if duration_value is not None:
        duration = _positive_float(duration_value)
        if duration is None:
            errors.add("ffprobe MP3 duration is invalid")
    facts.update(
        {
            "format": "mp3",
            "audioStreamCount": 1,
            "codec": "mp3",
            "sampleRate": sample_rate,
            "channels": channels,
            "durationSeconds": duration,
        }
    )
    facts["valid"] = errors.total == 0
    return facts


def validate_mp3(
    path: Path | str, ffprobe_metadata: Any | None = None
) -> dict[str, Any]:
    """Validate a bounded pair of MP3 frames and optional injected probe facts.

    No external process is launched.  Callers that already ran pinned ffprobe may
    inject its parsed JSON output for a stronger runtime-candidate cross-check.
    """

    source = Path(path)
    errors = _Errors()
    evidence = _base_evidence("mp3")
    size, digest = _identity(source, errors)
    evidence["size"], evidence["sha256"] = size, digest
    if size is None:
        return _finish(evidence, errors)
    if size < 8:
        errors.add("MP3 is too small to contain consecutive frames")
        return _finish(evidence, errors)

    audio_start = 0
    audio_end = size
    id3v2 = False
    id3v1 = False
    try:
        with source.open("rb") as stream:
            prefix = stream.read(10)
            if prefix[:3] == b"ID3":
                id3v2 = True
                major = prefix[3]
                flags = prefix[5]
                tag_size = _syncsafe(prefix[6:10])
                if major not in (2, 3, 4) or tag_size is None:
                    errors.add("MP3 ID3v2 header is invalid")
                    return _finish(evidence, errors)
                audio_start = 10 + tag_size + (10 if flags & 0x10 else 0)
                if audio_start > size or audio_start > MAX_ID3_BYTES:
                    errors.add(
                        "MP3 ID3v2 tag length is invalid or exceeds validation limits"
                    )
                    return _finish(evidence, errors)
            if size >= 128:
                stream.seek(size - 128)
                if stream.read(3) == b"TAG":
                    id3v1 = True
                    audio_end -= 128
            if audio_start >= audio_end:
                errors.add("MP3 contains no audio payload after tags")
                return _finish(evidence, errors)
            stream.seek(audio_start)
            scan = stream.read(min(MAX_MP3_SCAN_BYTES, audio_end - audio_start))
    except OSError:
        errors.add("MP3 container could not be parsed")
        return _finish(evidence, errors)

    selected: dict[str, int | str] | None = None
    selected_offset = 0
    second: dict[str, int | str] | None = None
    for candidate_offset in range(max(0, len(scan) - 3)):
        first = _mp3_header(scan, candidate_offset)
        if first is None:
            continue
        next_offset = candidate_offset + int(first["frameLength"])
        candidate_second = _mp3_header(scan, next_offset)
        if candidate_second is None:
            continue
        if (
            first["versionId"] != candidate_second["versionId"]
            or first["sampleRate"] != candidate_second["sampleRate"]
            or first["channels"] != candidate_second["channels"]
        ):
            continue
        second_end = audio_start + next_offset + int(candidate_second["frameLength"])
        if second_end > audio_end:
            continue
        selected, second, selected_offset = first, candidate_second, candidate_offset
        break
    if selected is None or second is None:
        errors.add("MP3 does not contain two consecutive complete Layer III frames")
        return _finish(evidence, errors)
    if selected_offset > 65_536:
        errors.add("MP3 frame sync appears too far after the metadata tag")

    probe_facts: dict[str, Any]
    if ffprobe_metadata is None:
        probe_facts = {"provided": False, "valid": None}
    else:
        before_probe_errors = errors.total
        probe_facts = _validate_ffprobe_metadata(ffprobe_metadata, selected, errors)
        probe_facts["valid"] = errors.total == before_probe_errors
    evidence["facts"] = {
        "mpegVersion": selected["version"],
        "layer": 3,
        "sampleRate": selected["sampleRate"],
        "channels": selected["channels"],
        "firstBitrateKbps": selected["bitrateKbps"],
        "secondBitrateKbps": second["bitrateKbps"],
        "consecutiveFramesChecked": 2,
        "id3v2Present": id3v2,
        "id3v1Present": id3v1,
        "ffprobe": probe_facts,
    }
    return _finish(evidence, errors)


def _bounded_collection(root: dict[str, Any], name: str, errors: _Errors) -> list[Any]:
    value = root.get(name, [])
    if not isinstance(value, list):
        errors.add(f"GLB JSON {name} must be an array")
        return []
    if len(value) > MAX_GLTF_COLLECTION_ITEMS:
        errors.add(f"GLB JSON {name} exceeds validation limit")
        return []
    return value


def _gltf_nonnegative(value: Any) -> int | None:
    return value if _is_plain_int(value) else None


def _validate_gltf_json(
    root: Any, bin_length: int | None, errors: _Errors
) -> dict[str, Any]:
    if not isinstance(root, dict):
        errors.add("GLB JSON root must be an object")
        return {}
    asset = root.get("asset")
    if not isinstance(asset, dict) or not isinstance(asset.get("version"), str):
        errors.add("GLB JSON is missing asset.version")
    elif asset["version"].split(".", 1)[0] != "2":
        errors.add("GLB asset.version is not glTF 2")

    collection_names = (
        "accessors",
        "animations",
        "buffers",
        "bufferViews",
        "cameras",
        "images",
        "materials",
        "meshes",
        "nodes",
        "samplers",
        "scenes",
        "skins",
        "textures",
    )
    collections = {
        name: _bounded_collection(root, name, errors) for name in collection_names
    }
    buffers = collections["buffers"]
    buffer_lengths: list[int] = []
    if len(buffers) > 1:
        errors.add("runtime GLB must be self-contained in at most one buffer")
    for index, item in enumerate(buffers):
        if not isinstance(item, dict):
            errors.add(f"GLB buffer {index} must be an object")
            buffer_lengths.append(0)
            continue
        length = _gltf_nonnegative(item.get("byteLength"))
        if length is None:
            errors.add(f"GLB buffer {index} has invalid byteLength")
            length = 0
        if "uri" in item:
            errors.add(f"runtime GLB buffer {index} must not use an external URI")
        buffer_lengths.append(length)
    if bin_length is not None:
        if len(buffer_lengths) != 1:
            errors.add("GLB BIN chunk requires exactly one JSON buffer")
        elif buffer_lengths[0] > bin_length or bin_length - buffer_lengths[0] > 3:
            errors.add("GLB BIN chunk length disagrees with buffers[0].byteLength")
    elif buffer_lengths and buffer_lengths[0] != 0:
        errors.add("GLB nonempty buffer is missing its BIN chunk")

    views = collections["bufferViews"]
    normalized_views: list[tuple[int, int, int] | None] = []
    for index, item in enumerate(views):
        if not isinstance(item, dict):
            errors.add(f"GLB bufferView {index} must be an object")
            normalized_views.append(None)
            continue
        buffer_index = _gltf_nonnegative(item.get("buffer"))
        offset = _gltf_nonnegative(item.get("byteOffset", 0))
        length = _gltf_nonnegative(item.get("byteLength"))
        if (
            buffer_index is None
            or buffer_index >= len(buffer_lengths)
            or offset is None
            or length is None
        ):
            errors.add(f"GLB bufferView {index} has invalid bounds")
            normalized_views.append(None)
            continue
        if offset + length > buffer_lengths[buffer_index]:
            errors.add(f"GLB bufferView {index} exceeds its buffer")
        stride = item.get("byteStride")
        if stride is not None and (
            not _is_plain_int(stride, minimum=4, maximum=252) or stride % 4 != 0
        ):
            errors.add(f"GLB bufferView {index} has invalid byteStride")
        normalized_views.append((buffer_index, offset, length))

    component_sizes = {5120: 1, 5121: 1, 5122: 2, 5123: 2, 5125: 4, 5126: 4}
    type_components = {
        "SCALAR": 1,
        "VEC2": 2,
        "VEC3": 3,
        "VEC4": 4,
        "MAT2": 4,
        "MAT3": 9,
        "MAT4": 16,
    }
    accessors = collections["accessors"]
    for index, item in enumerate(accessors):
        if not isinstance(item, dict):
            errors.add(f"GLB accessor {index} must be an object")
            continue
        component_type = item.get("componentType")
        accessor_type = item.get("type")
        count = _gltf_nonnegative(item.get("count"))
        offset = _gltf_nonnegative(item.get("byteOffset", 0))
        if (
            component_type not in component_sizes
            or accessor_type not in type_components
            or count is None
            or offset is None
        ):
            errors.add(f"GLB accessor {index} has invalid structural fields")
            continue
        if "bufferView" in item:
            view_index = _gltf_nonnegative(item.get("bufferView"))
            if (
                view_index is None
                or view_index >= len(normalized_views)
                or normalized_views[view_index] is None
            ):
                errors.add(f"GLB accessor {index} references an invalid bufferView")
            else:
                element_size = (
                    component_sizes[component_type] * type_components[accessor_type]
                )
                if (
                    count > 0
                    and offset + element_size > normalized_views[view_index][2]
                ):
                    errors.add(f"GLB accessor {index} starts beyond its bufferView")
        elif "sparse" not in item:
            errors.add(
                f"GLB accessor {index} has neither bufferView nor sparse storage"
            )

    nodes = collections["nodes"]
    meshes = collections["meshes"]
    for index, node in enumerate(nodes):
        if not isinstance(node, dict):
            errors.add(f"GLB node {index} must be an object")
            continue
        mesh = node.get("mesh")
        if mesh is not None and (not _is_plain_int(mesh) or mesh >= len(meshes)):
            errors.add(f"GLB node {index} references an invalid mesh")
        children = node.get("children", [])
        if not isinstance(children, list) or any(
            not _is_plain_int(child) or child >= len(nodes) for child in children
        ):
            errors.add(f"GLB node {index} has invalid children")

    for index, mesh in enumerate(meshes):
        if (
            not isinstance(mesh, dict)
            or not isinstance(mesh.get("primitives"), list)
            or not mesh["primitives"]
        ):
            errors.add(f"GLB mesh {index} has no primitive array")
            continue
        for primitive in mesh["primitives"]:
            if not isinstance(primitive, dict) or not isinstance(
                primitive.get("attributes"), dict
            ):
                errors.add(f"GLB mesh {index} contains an invalid primitive")
                continue
            accessor_refs = list(primitive["attributes"].values())
            if "indices" in primitive:
                accessor_refs.append(primitive["indices"])
            if any(
                not _is_plain_int(ref) or ref >= len(accessors) for ref in accessor_refs
            ):
                errors.add(f"GLB mesh {index} primitive references an invalid accessor")

    scenes = collections["scenes"]
    scene_index = root.get("scene")
    if scene_index is not None and (
        not _is_plain_int(scene_index) or scene_index >= len(scenes)
    ):
        errors.add("GLB default scene index is invalid")
    for index, scene in enumerate(scenes):
        if not isinstance(scene, dict):
            errors.add(f"GLB scene {index} must be an object")
            continue
        scene_nodes = scene.get("nodes", [])
        if not isinstance(scene_nodes, list) or any(
            not _is_plain_int(node) or node >= len(nodes) for node in scene_nodes
        ):
            errors.add(f"GLB scene {index} has invalid node references")

    return {f"{name}Count": len(collections[name]) for name in collection_names}


def validate_glb(path: Path | str) -> dict[str, Any]:
    """Validate a glTF 2.0 binary container and its JSON/BIN structure."""

    source = Path(path)
    errors = _Errors()
    evidence = _base_evidence("glb-v2")
    size, digest = _identity(source, errors)
    evidence["size"], evidence["sha256"] = size, digest
    if size is None:
        return _finish(evidence, errors)
    if size < 20:
        errors.add("GLB is too small for a version 2 container")
        return _finish(evidence, errors)

    json_payload: bytes | None = None
    bin_length: int | None = None
    chunk_count = 0
    unknown_chunks = 0
    try:
        with source.open("rb") as stream:
            header = stream.read(12)
            if len(header) != 12:
                errors.add("GLB header is truncated")
                return _finish(evidence, errors)
            magic, version, declared_length = struct.unpack("<4sII", header)
            if magic != _GLB_MAGIC:
                errors.add("GLB magic is invalid")
            if version != 2:
                errors.add("GLB container version is not 2")
            if declared_length != size:
                errors.add("GLB declared length does not match file size")
            container_end = min(size, declared_length)
            while stream.tell() < container_end:
                chunk_header = stream.read(8)
                if len(chunk_header) != 8:
                    errors.add("GLB chunk header is truncated")
                    break
                chunk_length, chunk_type = struct.unpack("<II", chunk_header)
                chunk_count += 1
                if chunk_count > MAX_GLTF_CHUNKS:
                    errors.add("GLB chunk count exceeds validation limit")
                    break
                if chunk_length % 4:
                    errors.add("GLB chunk length is not four-byte aligned")
                chunk_start = stream.tell()
                chunk_end = chunk_start + chunk_length
                if chunk_end > container_end:
                    errors.add("GLB chunk extends beyond its container")
                    break
                if chunk_count == 1 and chunk_type != _GLB_JSON:
                    errors.add("GLB first chunk is not JSON")
                if chunk_type == _GLB_JSON:
                    if json_payload is not None:
                        errors.add("GLB contains multiple JSON chunks")
                    elif chunk_length > MAX_JSON_BYTES:
                        errors.add("GLB JSON chunk exceeds validation limit")
                    else:
                        json_payload = stream.read(chunk_length)
                elif chunk_type == _GLB_BIN:
                    if bin_length is not None:
                        errors.add("GLB contains multiple BIN chunks")
                    bin_length = chunk_length
                else:
                    unknown_chunks += 1
                stream.seek(chunk_end)
            if stream.tell() != container_end:
                errors.add("GLB chunk table does not consume its declared length")
    except OSError:
        errors.add("GLB container could not be parsed")
        return _finish(evidence, errors)

    if json_payload is None:
        errors.add("GLB is missing its JSON chunk")
        return _finish(evidence, errors)
    gltf = _strict_json_bytes(json_payload, errors, "GLB JSON chunk")
    counts = _validate_gltf_json(gltf, bin_length, errors) if gltf is not None else {}
    evidence["facts"] = {
        "containerVersion": 2,
        "chunkCount": chunk_count,
        "jsonBytes": len(json_payload),
        "binBytes": bin_length,
        "unknownChunkCount": unknown_chunks,
        **counts,
    }
    return _finish(evidence, errors)


def _safe_map_path(
    root: Path, relative: Any, errors: _Errors, label: str
) -> Path | None:
    if (
        not isinstance(relative, str)
        or not relative
        or len(relative) > 240
        or "\\" in relative
    ):
        errors.add(f"{label} is not a bounded pack-relative path")
        return None
    pure = PurePosixPath(relative)
    if pure.is_absolute() or any(part in ("", ".", "..") for part in pure.parts):
        errors.add(f"{label} is not a safe pack-relative path")
        return None
    candidate = root.joinpath(*pure.parts)
    try:
        resolved_root = root.resolve(strict=True)
        resolved = candidate.resolve(strict=False)
        resolved.relative_to(resolved_root)
    except (OSError, ValueError):
        errors.add(f"{label} escapes the cooked map directory")
        return None
    if candidate.is_symlink():
        errors.add(f"{label} must not resolve through a symbolic link")
        return None
    return candidate


def _map_file_identity(
    root: Path,
    relative: str,
    errors: _Errors,
    inventory: dict[str, dict[str, Any]],
) -> tuple[int | None, str | None]:
    target = _safe_map_path(root, relative, errors, relative)
    if target is None:
        return None, None
    local_errors = _Errors()
    size, digest = _identity(target, local_errors)
    for item in local_errors.items:
        errors.add(f"{relative}: {item}")
    if size is not None and digest is not None:
        inventory[relative] = {"size": size, "sha256": digest}
    return size, digest


def _map_json(
    root: Path,
    relative: str,
    errors: _Errors,
    inventory: dict[str, dict[str, Any]],
) -> dict[str, Any] | None:
    size, _digest = _map_file_identity(root, relative, errors, inventory)
    if size is None:
        return None
    if size > MAX_JSON_BYTES:
        errors.add(f"{relative} exceeds the JSON validation limit")
        return None
    target = root / relative
    payload = _read_bounded(target, MAX_JSON_BYTES, errors, relative)
    if payload is None:
        return None
    value = _strict_json_bytes(payload, errors, relative)
    if not isinstance(value, dict):
        if value is not None:
            errors.add(f"{relative} root must be an object")
        return None
    expected_schema = _MAP_JSON_SCHEMAS[relative]
    if value.get("schema") != expected_schema or value.get("schemaVersion") != 0:
        errors.add(f"{relative} schema contract is invalid")
    return value


def _expect_map_reference(
    map_data: dict[str, Any], field: str, expected: str, errors: _Errors
) -> None:
    if map_data.get(field) != expected:
        errors.add(f"map.json {field} reference must be {expected}")


def _validate_count_array(
    document: dict[str, Any] | None,
    count_field: str,
    array_field: str,
    label: str,
    errors: _Errors,
) -> int | None:
    if document is None:
        return None
    count = document.get(count_field)
    values = document.get(array_field)
    if not _is_plain_int(count) or not isinstance(values, list):
        errors.add(f"{label} count/array contract is invalid")
        return None
    if count != len(values):
        errors.add(f"{label} declared count disagrees with its array")
    return len(values)


def _validate_waypoint_runtime_semantics(
    document: dict[str, Any] | None,
    errors: _Errors,
) -> dict[str, Any]:
    """Rebuild waypoint indexes from ordered raw records without leaking names."""

    facts: dict[str, Any] = {"waypointSemanticsAttested": False}
    if document is None:
        return facts
    records = document.get("waypoints")
    edges = document.get("edges", [])
    contract = document.get("runtimeSemantics")
    if not isinstance(records, list):
        return facts
    if not isinstance(edges, list):
        errors.add("waypoints.json edges must be an array")
        return facts

    named_records = [
        item
        for item in records
        if isinstance(item, dict)
        and isinstance(item.get("name"), str)
        and _is_plain_int(item.get("id"), minimum=1, maximum=2_147_483_647)
    ]
    exact_names = [str(item["name"]) for item in named_records]
    requires_contract = bool(edges)
    if len(named_records) == len(records):
        requires_contract = requires_contract or any(name == "" for name in exact_names)
        requires_contract = requires_contract or len(exact_names) != len(
            set(exact_names)
        )
        requires_contract = requires_contract or len(
            {name.casefold() for name in exact_names}
        ) != len(set(exact_names))
        requires_contract = requires_contract or any(
            "authoredUniqueId" in item for item in named_records
        )
    if contract is None:
        if requires_contract:
            errors.add("waypoints.json lossless runtime-semantics evidence is missing")
        return facts

    facts["waypointSemanticsAttested"] = True
    if not isinstance(contract, dict):
        errors.add("waypoints.json runtimeSemantics must be an object")
        return facts
    expected_contract_fields = {
        "schema",
        "schemaVersion",
        "rawWaypointPolicy",
        "nameLookupPolicy",
        "unresolvedEdgePolicy",
        "nameLookup",
        "runtimeAdjacency",
        "evidence",
    }
    if set(contract) != expected_contract_fields:
        errors.add("waypoints.json runtimeSemantics fields are not exact")
    if (
        contract.get("schema") != "openbfme.sage-waypoint-runtime-semantics"
        or contract.get("schemaVersion") != 0
        or contract.get("rawWaypointPolicy")
        != "source-order-preserved-no-synthesis-rename-or-merge"
        or contract.get("nameLookupPolicy") != "exact-case-sensitive-last-source-wins"
        or contract.get("unresolvedEdgePolicy")
        != "preserved-raw-omitted-from-derived-runtime-adjacency"
    ):
        errors.add("waypoints.json runtimeSemantics policy contract is invalid")

    waypoint_ids: set[int] = set()
    names: dict[str, list[dict[str, int]]] = {}
    folded_names: dict[str, list[dict[str, Any]]] = {}
    empty_names: list[dict[str, int]] = []
    mismatches: list[dict[str, Any]] = []
    lookup_by_name: dict[str, dict[str, Any]] = {}
    player_starts: dict[str, dict[str, Any]] = {}
    records_valid = True
    for source_index, item in enumerate(records):
        if not isinstance(item, dict):
            errors.add("waypoints.json ordered waypoint record is not an object")
            records_valid = False
            continue
        waypoint_id = item.get("id")
        name = item.get("name")
        if not _is_plain_int(
            waypoint_id, minimum=1, maximum=2_147_483_647
        ) or not isinstance(name, str):
            errors.add("waypoints.json ordered waypoint identity is invalid")
            records_valid = False
            continue
        if waypoint_id in waypoint_ids:
            errors.add("waypoints.json contains a duplicate waypointID")
            records_valid = False
        waypoint_ids.add(waypoint_id)
        reference = {"sourceIndex": source_index, "waypointId": waypoint_id}
        names.setdefault(name, []).append(reference)
        folded_names.setdefault(name.casefold(), []).append({**reference, "name": name})
        if name == "":
            empty_names.append(reference)
        if "authoredUniqueId" in item:
            authored_unique_id = item.get("authoredUniqueId")
            if not isinstance(authored_unique_id, str) or authored_unique_id == name:
                errors.add(
                    "waypoints.json authored identity mismatch record is invalid"
                )
                records_valid = False
            else:
                mismatches.append(
                    {
                        **reference,
                        "waypointName": name,
                        "authoredUniqueId": authored_unique_id,
                    }
                )
        lookup_by_name[name] = {**reference, "name": name}
        player_match = re.fullmatch(r"Player_([1-9][0-9]*)_Start", name)
        if player_match is None:
            if "playerIndex" in item:
                errors.add("waypoints.json non-start record has a playerIndex")
                records_valid = False
        elif item.get("playerIndex") != int(player_match.group(1)):
            errors.add("waypoints.json player-start index is inconsistent")
            records_valid = False
        else:
            player_starts[name] = item

    if not records_valid:
        return facts

    duplicate_groups = [
        {"name": name, "records": occurrences}
        for name, occurrences in names.items()
        if len(occurrences) > 1
    ]
    casefold_collision_groups = [
        {"records": occurrences}
        for occurrences in folded_names.values()
        if len({str(item["name"]) for item in occurrences}) > 1
    ]
    name_lookup = sorted(
        lookup_by_name.values(), key=lambda item: int(item["sourceIndex"])
    )
    if contract.get("nameLookup") != name_lookup:
        errors.add("waypoints.json derived name lookup is inconsistent")

    expected_player_starts = {name: player_starts[name] for name in player_starts}
    expected_player_bindings = [
        {
            "playerIndex": int(item["playerIndex"]),
            "waypointId": int(item["id"]),
            "waypointName": str(item["name"]),
        }
        for item in sorted(
            player_starts.values(), key=lambda value: int(value["playerIndex"])
        )
    ]
    if document.get("playerStarts") != expected_player_starts:
        errors.add("waypoints.json derived player-start lookup is inconsistent")
    if document.get("playerStartBindings") != expected_player_bindings:
        errors.add("waypoints.json derived player-start bindings are inconsistent")

    raw_edges: list[dict[str, Any]] = []
    resolved_edges: list[dict[str, Any]] = []
    unresolved_edges: list[dict[str, Any]] = []
    adjacency_by_start: dict[int, list[int]] = {}
    edge_keys: set[tuple[int, int]] = set()
    edges_valid = True
    for source_index, edge in enumerate(edges):
        if not isinstance(edge, dict) or set(edge) != {
            "sourceIndex",
            "startId",
            "endId",
            "resolved",
        }:
            errors.add("waypoints.json raw edge fields are not exact")
            edges_valid = False
            continue
        start_id = edge.get("startId")
        end_id = edge.get("endId")
        if (
            edge.get("sourceIndex") != source_index
            or not isinstance(start_id, int)
            or isinstance(start_id, bool)
            or start_id < -2_147_483_648
            or start_id > 2_147_483_647
            or not isinstance(end_id, int)
            or isinstance(end_id, bool)
            or end_id < -2_147_483_648
            or end_id > 2_147_483_647
            or not isinstance(edge.get("resolved"), bool)
        ):
            errors.add("waypoints.json raw edge value is invalid")
            edges_valid = False
            continue
        edge_key = (start_id, end_id)
        if edge_key in edge_keys or start_id == end_id:
            errors.add("waypoints.json raw edge violates source edge invariants")
            edges_valid = False
        edge_keys.add(edge_key)
        resolved = start_id in waypoint_ids and end_id in waypoint_ids
        raw = {
            "sourceIndex": source_index,
            "startId": start_id,
            "endId": end_id,
            "resolved": resolved,
        }
        raw_edges.append(raw)
        if edge.get("resolved") is not resolved:
            errors.add("waypoints.json raw edge resolution is fabricated")
            edges_valid = False
        if resolved:
            resolved_edges.append(raw)
            adjacency_by_start.setdefault(start_id, []).append(end_id)
        else:
            unresolved_edges.append(raw)

    if not edges_valid:
        return facts
    runtime_adjacency = [
        {"startId": start_id, "endIds": end_ids}
        for start_id, end_ids in adjacency_by_start.items()
    ]
    if contract.get("runtimeAdjacency") != runtime_adjacency:
        errors.add("waypoints.json derived runtime adjacency is inconsistent")
    if document.get("topLevelWaypointPathCount") != len(raw_edges):
        errors.add("waypoints.json raw edge count is inconsistent")
    expected_routing_status = (
        "source-edges-preserved-unresolved-omitted-from-runtime-adjacency"
        if unresolved_edges
        else "source-edges-imported-runtime-pending"
        if raw_edges
        else "empty-no-authored-navmesh"
    )
    if document.get("routingGraphStatus") != expected_routing_status:
        errors.add("waypoints.json routing status is inconsistent")

    evidence = {
        "waypointRecordCount": len(records),
        "orderedWaypointRecordsSha256": _canonical_json_sha256(records),
        "nameLookupCount": len(name_lookup),
        "nameLookupSha256": _canonical_json_sha256(name_lookup),
        "rawEdgeCount": len(raw_edges),
        "rawEdgesSha256": _canonical_json_sha256(raw_edges),
        "resolvedEdgeCount": len(resolved_edges),
        "resolvedEdgesSha256": _canonical_json_sha256(resolved_edges),
        "unresolvedEdgeCount": len(unresolved_edges),
        "unresolvedEdgesSha256": _canonical_json_sha256(unresolved_edges),
        "runtimeAdjacencyStartCount": len(runtime_adjacency),
        "runtimeAdjacencyEdgeCount": len(resolved_edges),
        "runtimeAdjacencySha256": _canonical_json_sha256(runtime_adjacency),
        "duplicateNameGroupCount": len(duplicate_groups),
        "duplicateNameRecordCount": sum(
            len(item["records"]) for item in duplicate_groups
        ),
        "duplicateNamesSha256": _canonical_json_sha256(duplicate_groups),
        "caseFoldCollisionGroupCount": len(casefold_collision_groups),
        "caseFoldCollisionRecordCount": sum(
            len(item["records"]) for item in casefold_collision_groups
        ),
        "caseFoldCollisionsSha256": _canonical_json_sha256(casefold_collision_groups),
        "emptyNameCount": len(empty_names),
        "emptyNamesSha256": _canonical_json_sha256(empty_names),
        "authoredIdentityMismatchCount": len(mismatches),
        "authoredIdentityMismatchesSha256": _canonical_json_sha256(mismatches),
    }
    if contract.get("evidence") != evidence:
        errors.add("waypoints.json runtime-semantics evidence is inconsistent")
    facts["waypointSemanticsEvidence"] = evidence
    return facts


def _validate_side_runtime_semantics(
    setup: dict[str, Any] | None,
    errors: _Errors,
) -> dict[str, Any]:
    """Rebuild EA's ordered first-exact side lookup and script bindings."""

    facts: dict[str, Any] = {"sideSemanticsAttested": False}
    if setup is None:
        return facts
    players = setup.get("scenarioPlayers")
    contract = setup.get("sideRuntimeSemantics")
    source_script_lists = setup.get("sourceScriptLists")
    if not isinstance(players, list):
        if contract is not None or source_script_lists is not None:
            errors.add("setup.json side semantics require ordered scenario players")
        return facts

    records_valid = True
    exact_names: dict[str, list[dict[str, int]]] = {}
    folded_names: dict[str, list[dict[str, Any]]] = {}
    lookup_by_name: dict[str, dict[str, Any]] = {}
    for source_index, player in enumerate(players):
        if not isinstance(player, dict):
            errors.add("setup.json ordered scenario player is not an object")
            records_valid = False
            continue
        name = player.get("name")
        properties = player.get("properties")
        build_list = player.get("buildList")
        if (
            player.get("index") != source_index
            or not isinstance(name, str)
            or not isinstance(properties, list)
            or not isinstance(build_list, list)
        ):
            errors.add("setup.json ordered scenario player record is invalid")
            records_valid = False
            continue
        player_name_properties = [
            item
            for item in properties
            if isinstance(item, dict) and item.get("name") == "playerName"
        ]
        if (
            len(player_name_properties) != 1
            or player_name_properties[0].get("wireType") != "ascii-string"
            or player_name_properties[0].get("wireTypeCode") != 3
            or player_name_properties[0].get("value") != name
        ):
            errors.add("setup.json scenario playerName property is inconsistent")
            records_valid = False
        reference = {"sourceIndex": source_index}
        exact_names.setdefault(name, []).append(reference)
        folded_names.setdefault(name.casefold(), []).append({**reference, "name": name})
        lookup_by_name.setdefault(name, {"name": name, **reference})

    if setup.get("scenarioPlayerCount") != len(players):
        errors.add("setup.json scenario player count is inconsistent")
    if not records_valid:
        return facts

    duplicate_groups = [
        {"name": name, "records": records}
        for name, records in exact_names.items()
        if len(records) > 1
    ]
    casefold_collision_groups = [
        {"records": records}
        for records in folded_names.values()
        if len({str(item["name"]) for item in records}) > 1
    ]
    requires_contract = bool(duplicate_groups or casefold_collision_groups)
    if contract is None:
        if requires_contract:
            errors.add("setup.json lossless side runtime-semantics evidence is missing")
        if source_script_lists is not None:
            errors.add("setup.json source script lists require side semantics")
        return facts

    facts["sideSemanticsAttested"] = True
    if not isinstance(contract, dict):
        errors.add("setup.json sideRuntimeSemantics must be an object")
        return facts
    expected_contract_fields = {
        "schema",
        "schemaVersion",
        "rawPlayerPolicy",
        "nameLookupPolicy",
        "scriptBindingPolicy",
        "nameLookup",
        "scriptBindings",
        "duplicateNameGroups",
        "caseFoldCollisionGroups",
        "evidence",
    }
    if set(contract) != expected_contract_fields:
        errors.add("setup.json sideRuntimeSemantics fields are not exact")
    if (
        contract.get("schema") != "openbfme.sage-side-runtime-semantics"
        or contract.get("schemaVersion") != 0
        or contract.get("rawPlayerPolicy")
        != "source-order-preserved-no-synthesis-rename-or-merge"
        or contract.get("nameLookupPolicy") != "exact-case-sensitive-first-source-wins"
        or contract.get("scriptBindingPolicy")
        != "script-source-ordinal-equals-player-source-index"
    ):
        errors.add("setup.json sideRuntimeSemantics policy contract is invalid")

    if not isinstance(source_script_lists, list):
        errors.add("setup.json sourceScriptLists must be an array")
        return facts
    scripts_valid = True
    for source_ordinal, script_list in enumerate(source_script_lists):
        if not isinstance(script_list, dict) or set(script_list) != {
            "sourceOrdinal",
            "sourceVersion",
            "payloadByteLength",
            "payloadSha256",
            "nonempty",
        }:
            errors.add("setup.json source script-list fields are not exact")
            scripts_valid = False
            continue
        payload_length = script_list.get("payloadByteLength")
        if (
            script_list.get("sourceOrdinal") != source_ordinal
            or script_list.get("sourceVersion") != 1
            or not _is_plain_int(payload_length)
            or not _is_sha256(script_list.get("payloadSha256"))
            or script_list.get("nonempty") is not (payload_length > 0)
        ):
            errors.add("setup.json source script-list ordinal evidence is invalid")
            scripts_valid = False
    if len(source_script_lists) != len(players):
        errors.add("setup.json source script-list count disagrees with players")
        scripts_valid = False
    if setup.get("scriptListCount") != len(source_script_lists):
        errors.add("setup.json scriptListCount disagrees with source script lists")
    expected_nonempty = sum(
        isinstance(item, dict) and item.get("nonempty") is True
        for item in source_script_lists
    )
    if setup.get("nonemptyScriptListCount") != expected_nonempty:
        errors.add("setup.json nonempty script-list count is inconsistent")
    if not scripts_valid:
        return facts

    name_lookup = list(lookup_by_name.values())
    script_bindings = [
        {
            "sourceOrdinal": source_ordinal,
            "playerSourceIndex": source_ordinal,
            "playerName": str(players[source_ordinal]["name"]),
        }
        for source_ordinal in range(len(source_script_lists))
    ]
    if contract.get("nameLookup") != name_lookup:
        errors.add("setup.json derived side name lookup is inconsistent")
    if contract.get("scriptBindings") != script_bindings:
        errors.add("setup.json derived player script bindings are inconsistent")
    if contract.get("duplicateNameGroups") != duplicate_groups:
        errors.add("setup.json duplicate side-name evidence is inconsistent")
    if contract.get("caseFoldCollisionGroups") != casefold_collision_groups:
        errors.add("setup.json side case-fold collision evidence is inconsistent")

    evidence = {
        "playerRecordCount": len(players),
        "orderedPlayerRecordsSha256": _canonical_json_sha256(players),
        "nameLookupCount": len(name_lookup),
        "nameLookupSha256": _canonical_json_sha256(name_lookup),
        "sourceScriptListCount": len(source_script_lists),
        "sourceScriptListsSha256": _canonical_json_sha256(source_script_lists),
        "scriptBindingCount": len(script_bindings),
        "scriptBindingsSha256": _canonical_json_sha256(script_bindings),
        "duplicateNameGroupCount": len(duplicate_groups),
        "duplicateNameRecordCount": sum(
            len(item["records"]) for item in duplicate_groups
        ),
        "duplicateNamesSha256": _canonical_json_sha256(duplicate_groups),
        "caseFoldCollisionGroupCount": len(casefold_collision_groups),
        "caseFoldCollisionRecordCount": sum(
            len(item["records"]) for item in casefold_collision_groups
        ),
        "caseFoldCollisionsSha256": _canonical_json_sha256(casefold_collision_groups),
    }
    if contract.get("evidence") != evidence:
        errors.add("setup.json side runtime-semantics evidence is inconsistent")
    facts["sideSemanticsEvidence"] = evidence
    return facts


def _validate_team_runtime_semantics(
    setup: dict[str, Any] | None,
    errors: _Errors,
) -> dict[str, Any]:
    """Rebuild EA's exact default-team lookup and load-time owner repairs."""

    facts: dict[str, Any] = {"teamSemanticsAttested": False}
    if setup is None:
        return facts
    players = setup.get("scenarioPlayers")
    teams = setup.get("teams")
    contract = setup.get("teamRuntimeSemantics")
    if not isinstance(players, list) or not isinstance(teams, list):
        if contract is not None:
            errors.add("setup.json team semantics require ordered players and teams")
        return facts

    records_valid = True
    for source_index, player in enumerate(players):
        if (
            not isinstance(player, dict)
            or player.get("index") != source_index
            or not isinstance(player.get("name"), str)
        ):
            errors.add("setup.json team semantics player record is invalid")
            records_valid = False

    team_lookup: dict[str, dict[str, Any]] = {}
    runtime_owners: dict[int, str] = {}
    for source_index, team in enumerate(teams):
        if not isinstance(team, dict):
            errors.add("setup.json ordered team record is not an object")
            records_valid = False
            continue
        name = team.get("name")
        owner = team.get("owner")
        properties = team.get("properties")
        if (
            team.get("index") != source_index
            or not isinstance(name, str)
            or not isinstance(owner, str)
            or not isinstance(properties, list)
        ):
            errors.add("setup.json ordered team record is invalid")
            records_valid = False
            continue
        for property_name, expected_value in (
            ("teamName", name),
            ("teamOwner", owner),
        ):
            matching = [
                item
                for item in properties
                if isinstance(item, dict) and item.get("name") == property_name
            ]
            if (
                len(matching) != 1
                or matching[0].get("wireType") != "ascii-string"
                or matching[0].get("wireTypeCode") != 3
                or matching[0].get("value") != expected_value
            ):
                errors.add(f"setup.json {property_name} property is inconsistent")
                records_valid = False
        team_lookup.setdefault(name, team)
        runtime_owners[source_index] = owner

    if setup.get("teamCount") != len(teams):
        errors.add("setup.json team count is inconsistent")
    if not records_valid:
        return facts

    repairs: list[dict[str, Any]] = []
    for player_source_index, player in enumerate(players):
        player_name = str(player["name"])
        team = team_lookup.get("team" + player_name)
        if team is None:
            continue
        team_source_index = int(team["index"])
        if runtime_owners[team_source_index] == player_name:
            continue
        repairs.append(
            {
                "playerSourceIndex": player_source_index,
                "teamSourceIndex": team_source_index,
                "teamName": str(team["name"]),
                "authoredOwner": str(team["owner"]),
                "runtimeOwner": player_name,
            }
        )
        runtime_owners[team_source_index] = player_name

    if contract is None:
        if repairs:
            errors.add("setup.json lossless team runtime-semantics evidence is missing")
        return facts

    facts["teamSemanticsAttested"] = True
    if not isinstance(contract, dict):
        errors.add("setup.json teamRuntimeSemantics must be an object")
        return facts
    expected_fields = {
        "schema",
        "schemaVersion",
        "rawTeamPolicy",
        "defaultTeamLookupPolicy",
        "ownerRepairPolicy",
        "defaultTeamOwnerRepairs",
        "evidence",
    }
    if set(contract) != expected_fields:
        errors.add("setup.json teamRuntimeSemantics fields are not exact")
    if (
        contract.get("schema") != "openbfme.sage-team-runtime-semantics"
        or contract.get("schemaVersion") != 0
        or contract.get("rawTeamPolicy")
        != "source-order-preserved-no-synthesis-rename-or-merge"
        or contract.get("defaultTeamLookupPolicy")
        != "exact-case-sensitive-first-source-wins"
        or contract.get("ownerRepairPolicy")
        != "ea-validate-sides-default-team-owner-repair"
    ):
        errors.add("setup.json teamRuntimeSemantics policy contract is invalid")
    if not repairs:
        errors.add("setup.json team runtime-semantics evidence is not required")
    if contract.get("defaultTeamOwnerRepairs") != repairs:
        errors.add("setup.json default-team owner repairs are inconsistent")

    evidence = {
        "playerRecordCount": len(players),
        "orderedPlayerRecordsSha256": _canonical_json_sha256(players),
        "teamRecordCount": len(teams),
        "orderedTeamRecordsSha256": _canonical_json_sha256(teams),
        "defaultTeamOwnerRepairCount": len(repairs),
        "defaultTeamOwnerRepairsSha256": _canonical_json_sha256(repairs),
    }
    if contract.get("evidence") != evidence:
        errors.add("setup.json team runtime-semantics evidence is inconsistent")
    facts["teamSemanticsEvidence"] = evidence
    return facts


def _validate_lobby_source_absence(
    setup: dict[str, Any] | None,
    chunks: dict[str, Any] | None,
    profile: dict[str, Any],
    errors: _Errors,
) -> dict[str, Any]:
    """Attest the source-proven optional MPPositionList layout."""

    facts: dict[str, Any] = {"lobbySourceAbsenceAttested": False}
    if setup is None:
        return facts
    status = setup.get("lobbySourceStatus")
    setup_layouts = setup.get("sourceChunkLayouts")
    setup_layout = (
        setup_layouts.get("MPPositionList") if isinstance(setup_layouts, dict) else None
    )
    if status is None:
        if isinstance(setup_layout, dict) and setup_layout.get("present") is False:
            errors.add("setup.json lobby source-absence layout lacks source status")
        return facts

    facts["lobbySourceAbsenceAttested"] = True
    expected_layout = {
        "sourceVersion": None,
        "present": False,
        "absence": "not-present-in-source",
        "structuralConversion": "lossless-source-absence-preservation",
        "runtimeDefaultParity": "not-applicable-runtime-does-not-consult-chunk",
    }
    if status != "not-present-in-source":
        errors.add("setup.json lobbySourceStatus is unsupported")
    if profile.get("profileAttested") is not True:
        errors.add("setup.json lobby source absence requires an attested profile")
    if setup.get("lobbySlotCount") != 0 or setup.get("lobbySlots") != []:
        errors.add("setup.json absent lobby chunk synthesizes lobby slots")
    source_versions = setup.get("sourceVersions")
    if not isinstance(source_versions, dict) or any(
        name in source_versions for name in ("MPPositionList", "MPPositionInfo")
    ):
        errors.add("setup.json absent lobby chunk has source-version evidence")
    if setup_layout != expected_layout:
        errors.add("setup.json lobby source-absence layout is inconsistent")
    cross_checks = setup.get("crossChecks")
    if not isinstance(cross_checks, dict) or (
        cross_checks.get("startCountWithinLobbySlots") != "not-applicable-source-absent"
    ):
        errors.add("setup.json absent lobby chunk has an applicable slot cross-check")

    chunk_layout = None
    chunk_records: Any = None
    if isinstance(chunks, dict):
        conversion = chunks.get("conversionEvidence")
        layouts = (
            conversion.get("sourceChunkLayouts")
            if isinstance(conversion, dict)
            else None
        )
        chunk_layout = (
            layouts.get("MPPositionList") if isinstance(layouts, dict) else None
        )
        chunk_records = chunks.get("chunks")
    if chunk_layout != expected_layout:
        errors.add("chunks.json lobby source-absence layout is inconsistent")
    if not isinstance(chunk_records, list):
        errors.add("chunks.json source chunk inventory is invalid")
    elif any(
        isinstance(item, dict) and item.get("name") == "MPPositionList"
        for item in chunk_records
    ):
        errors.add("chunks.json contains absent MPPositionList source chunk")
    return facts


def _profile_tuple(
    document: dict[str, Any] | None,
    label: str,
    errors: _Errors,
) -> tuple[str, int, bool] | None:
    if document is None:
        return None
    present = [field for field in _MAP_PROFILE_FIELDS if field in document]
    if not present:
        return None
    if len(present) != len(_MAP_PROFILE_FIELDS):
        errors.add(f"{label} profile evidence is incomplete")
        return None
    map_kind = document.get("mapKind")
    version = document.get("profileVersion")
    runnable = document.get("runnable")
    if (
        not isinstance(map_kind, str)
        or not map_kind
        or map_kind != map_kind.strip()
        or len(map_kind) > 64
        or not _is_plain_int(version, maximum=2_147_483_647)
        or not isinstance(runnable, bool)
    ):
        errors.add(f"{label} profile evidence has invalid field types")
        return None
    expected_status = (
        "runnable-structure" if runnable else "non-runnable-structural-map"
    )
    if (
        "structuralStatus" in document
        and document.get("structuralStatus") != expected_status
    ):
        errors.add(f"{label} structuralStatus contradicts runnable")
    return map_kind, version, runnable


def _cooked_map_profile(
    map_data: dict[str, Any] | None,
    setup: dict[str, Any] | None,
    chunks: dict[str, Any] | None,
    errors: _Errors,
) -> dict[str, Any]:
    map_profile = _profile_tuple(map_data, "map.json", errors)
    setup_profile = _profile_tuple(setup, "setup.json", errors)
    selected: tuple[str, int, bool] | None = None
    if (map_profile is None) != (setup_profile is None):
        errors.add(
            "map.json and setup.json profile evidence is incomplete or inconsistent"
        )
    elif map_profile is not None and setup_profile is not None:
        if map_profile != setup_profile:
            errors.add("map.json and setup.json profile evidence disagrees")
        else:
            selected = map_profile

    for document, field, label in (
        (map_data, "conversionEvidence", "map.json conversionEvidence"),
        (chunks, "conversionEvidence", "chunks.json conversionEvidence"),
    ):
        if document is None or field not in document:
            continue
        nested = document.get(field)
        if not isinstance(nested, dict):
            errors.add(f"{label} must be an object")
            continue
        nested_profile = _profile_tuple(nested, label, errors)
        if nested_profile is None or selected is None or nested_profile != selected:
            errors.add(f"{label} disagrees with map/setup profile evidence")

    supported = False
    map_kind = version = declared_runnable = None
    if selected is not None:
        map_kind, version, declared_runnable = selected
        expected_runnable = _MAP_PROFILE_RUNNABLE.get(map_kind)
        if version == _MAP_PROFILE_VERSION and expected_runnable is not None:
            if declared_runnable != expected_runnable:
                errors.add(f"{map_kind} profile runnable attestation is contradictory")
            else:
                supported = True
    runnable = declared_runnable if supported else None
    structural_only = (
        supported and map_kind in {"library", "placeholder"} and runnable is False
    )
    return {
        "mapKind": map_kind,
        "profileVersion": version,
        "profileAttested": supported,
        "runnable": runnable,
        "allowOneCellTerrain": structural_only,
    }


def _finish_cooked_map_evidence(
    evidence: dict[str, Any], errors: _Errors
) -> dict[str, Any]:
    result = _finish(evidence, errors)
    result["structuralValid"] = result["valid"]
    facts = result.get("facts")
    if isinstance(facts, dict):
        facts["structuralValid"] = result["structuralValid"]
    return result


def _check_descriptor(
    *,
    root: Path,
    descriptor: Any,
    expected_path: str,
    expected_size: int,
    label: str,
    errors: _Errors,
    inventory: dict[str, dict[str, Any]],
) -> None:
    if not isinstance(descriptor, dict):
        errors.add(f"{label} descriptor is missing")
        return
    if descriptor.get("path") != expected_path:
        errors.add(f"{label} path does not match the cooked map contract")
        return
    declared_length = descriptor.get("byteLength")
    declared_hash = descriptor.get("sha256")
    if declared_length != expected_size:
        errors.add(f"{label} byteLength is inconsistent")
    if not _is_sha256(declared_hash):
        errors.add(f"{label} sha256 is invalid")
    actual_size, actual_hash = _map_file_identity(
        root, expected_path, errors, inventory
    )
    if actual_size is not None and actual_size != declared_length:
        errors.add(f"{label} file size disagrees with byteLength")
    if actual_hash is not None and actual_hash != declared_hash:
        errors.add(f"{label} file hash disagrees with sha256")


def _blend_source_layer_presence(version: int) -> dict[str, bool]:
    return {
        name: version >= _BLEND_VERSIONED_LAYER_MIN_VERSION[name]
        for name in _BLEND_VERSIONED_LAYER_ORDER
    }


def _legacy_blend_layout_evidence(version: int) -> dict[str, Any]:
    return {
        "sourceVersion": version,
        "blendCellWordBits": _BLEND_CELL_WORD_BITS_BY_VERSION[version],
        "sourceLayerPresence": _blend_source_layer_presence(version),
        "structuralConversion": _BLEND_STRUCTURAL_CONVERSION,
        "runtimeDefaultParity": _BLEND_RUNTIME_DEFAULT_PARITY,
    }


def _validate_legacy_blend_evidence_copy(
    document: dict[str, Any] | None,
    *,
    path: tuple[str, ...],
    expected: dict[str, Any],
    label: str,
    errors: _Errors,
) -> None:
    current: Any = document
    for key in path:
        if not isinstance(current, dict):
            current = None
            break
        current = current.get(key)
    if current != expected:
        errors.add(f"{label} BlendTileData layout evidence is inconsistent")


def _validate_versioned_blend_layers(
    *,
    root: Path,
    source_layers: dict[str, Any],
    blend: dict[str, Any],
    width: int | None,
    height: int | None,
    map_data: dict[str, Any] | None,
    setup: dict[str, Any] | None,
    chunks: dict[str, Any] | None,
    required_binaries: tuple[str, ...],
    errors: _Errors,
    inventory: dict[str, dict[str, Any]],
) -> int:
    contract = source_layers.get("versionedBlendLayers")
    version = blend.get("version")
    if version is None:
        if contract is not None:
            errors.add("versioned BlendTileData layers require source version evidence")
        return 0
    if not _is_plain_int(version):
        errors.add("terrain.json BlendTileData source version is invalid")
        return 0
    if version not in _SUPPORTED_BLEND_VERSIONS:
        errors.add("terrain.json BlendTileData source version is unsupported")
        return 0
    if version not in _LOSSLESS_LEGACY_BLEND_VERSIONS:
        if contract is not None:
            errors.add(
                "current BlendTileData versions must retain their original source-layer contract"
            )
        return 0

    presence = _blend_source_layer_presence(version)
    expected_layout = _legacy_blend_layout_evidence(version)
    if blend.get("sourceLayerPresence") != presence:
        errors.add("terrain.json BlendTileData layer-presence evidence is inconsistent")
    if blend.get("structuralConversion") != _BLEND_STRUCTURAL_CONVERSION:
        errors.add(
            "terrain.json BlendTileData structural-conversion evidence is invalid"
        )
    if blend.get("runtimeDefaultParity") != _BLEND_RUNTIME_DEFAULT_PARITY:
        errors.add("terrain.json BlendTileData runtime-default parity is not unproven")
    grid_stats = blend.get("gridStats")
    expected_grid_stats = {
        summary_name
        for name, summary_name in (
            ("impassability", "impassable"),
            ("impassabilityToPlayers", "impassableToPlayers"),
            ("passageWidths", "passageWidth"),
            ("taintability", "taintable"),
            ("extraPassability", "extraPassability"),
            ("visibility", "visible"),
        )
        if presence[name]
    }
    if not isinstance(grid_stats, dict) or set(grid_stats) != expected_grid_stats:
        errors.add(
            "terrain.json BlendTileData grid statistics synthesize or omit a layer"
        )
    if ("flammabilityCounts" in blend) != presence["flammability"]:
        errors.add(
            "terrain.json BlendTileData flammability evidence contradicts presence"
        )
    _validate_legacy_blend_evidence_copy(
        map_data,
        path=("conversionEvidence", "sourceChunkLayouts", "BlendTileData"),
        expected=expected_layout,
        label="map.json",
        errors=errors,
    )
    _validate_legacy_blend_evidence_copy(
        setup,
        path=("sourceChunkLayouts", "BlendTileData"),
        expected=expected_layout,
        label="setup.json",
        errors=errors,
    )
    _validate_legacy_blend_evidence_copy(
        chunks,
        path=("conversionEvidence", "sourceChunkLayouts", "BlendTileData"),
        expected=expected_layout,
        label="chunks.json",
        errors=errors,
    )

    expected_extra_files = sum(
        present and _BLEND_VERSIONED_LAYER_PATHS[name] not in required_binaries
        for name, present in presence.items()
    )
    if not isinstance(contract, dict):
        errors.add(
            "terrain.json versioned BlendTileData source-layer contract is missing"
        )
        return expected_extra_files
    expected_header = {
        "schema": "openbfme.sage-blend-versioned-source-layers",
        "schemaVersion": 0,
        "sourceVersion": version,
        "blendCellWordBits": _BLEND_CELL_WORD_BITS_BY_VERSION[version],
        "structuralConversion": _BLEND_STRUCTURAL_CONVERSION,
        "runtimeDefaultParity": _BLEND_RUNTIME_DEFAULT_PARITY,
    }
    if set(contract) != {*expected_header, "layers"}:
        errors.add("versioned BlendTileData contract fields are not exact")
    for key, expected_value in expected_header.items():
        if contract.get(key) != expected_value:
            errors.add(f"versioned BlendTileData {key} evidence is inconsistent")
    layers = contract.get("layers")
    if not isinstance(layers, dict):
        errors.add("versioned BlendTileData layer declarations are missing")
        layers = {}
    if set(layers) != set(_BLEND_VERSIONED_LAYER_ORDER):
        errors.add("versioned BlendTileData layer declarations are not exact")

    if not _is_plain_int(width, minimum=1) or not _is_plain_int(height, minimum=1):
        errors.add("versioned BlendTileData layers require valid terrain dimensions")
        return expected_extra_files
    row_stride = (width + 7) // 8
    packed_size = row_stride * height
    area = width * height
    for name in _BLEND_VERSIONED_LAYER_ORDER:
        descriptor = layers.get(name)
        path = _BLEND_VERSIONED_LAYER_PATHS[name]
        label = f"versioned BlendTileData layer {name}"
        if not presence[name]:
            expected_absence = {
                "present": False,
                "absence": _BLEND_ABSENCE_REASON,
            }
            if descriptor != expected_absence:
                errors.add(f"{label} absence declaration is inconsistent")
            target = _safe_map_path(root, path, errors, label)
            if target is not None and target.exists():
                errors.add(f"{label} file exists despite source-version absence")
            continue
        if not isinstance(descriptor, dict) or descriptor.get("present") is not True:
            errors.add(f"{label} presence declaration is missing")
        if name == "flammability":
            expected_size = area
            metadata = {
                "encoding": "uint8",
                "endianness": "little",
                "order": "row-major-y-then-x",
                "cellCount": area,
                "cellSizeBytes": 1,
                "sourceExact": True,
            }
        else:
            expected_size = packed_size
            metadata = {
                "encoding": "packed-single-bit",
                "bitOrder": "least-significant-bit-first",
                "order": "row-major-y-then-x",
                "gridWidth": width,
                "gridHeight": height,
                "rowStrideBytes": row_stride,
                "rowPadding": True,
                "sourceExact": True,
            }
        if isinstance(descriptor, dict):
            expected_descriptor_fields = {
                "present",
                "path",
                "byteLength",
                "sha256",
                *metadata,
            }
            if set(descriptor) != expected_descriptor_fields:
                errors.add(f"{label} descriptor fields are not exact")
            for key, expected_value in metadata.items():
                if descriptor.get(key) != expected_value:
                    errors.add(f"{label} {key} metadata is inconsistent")
        _check_descriptor(
            root=root,
            descriptor=descriptor,
            expected_path=path,
            expected_size=expected_size,
            label=label,
            errors=errors,
            inventory=inventory,
        )
    return expected_extra_files


def _is_json_position(value: Any) -> bool:
    if not isinstance(value, list) or len(value) != 3:
        return False
    for component in value:
        if isinstance(component, bool) or not isinstance(component, (int, float)):
            return False
        if isinstance(component, float) and not math.isfinite(component):
            return False
    return True


def _json_values_are_exact(left: Any, right: Any) -> bool:
    return left == right and _canonical_json_sha256(left) == _canonical_json_sha256(
        right
    )


def _expected_road_document(road_objects: list[dict[str, Any]]) -> dict[str, Any]:
    control_points: list[dict[str, Any]] = []
    for sequence, item in enumerate(road_objects):
        wire_type = int(item["roadType"])
        control_points.append(
            {
                "sequence": sequence,
                "sourceIndex": int(item["index"]),
                "roadId": str(item["typeName"]),
                "wireType": wire_type,
                "role": (
                    "segment-start"
                    if wire_type == 2
                    else "segment-end"
                    if wire_type == 4
                    else "unresolved"
                ),
                "status": "unresolved",
                "segmentIndex": None,
                "sagePosition": list(item["sagePosition"]),
                "godotPosition": list(item["godotPosition"]),
            }
        )

    segments: list[dict[str, Any]] = []
    diagnostics: list[dict[str, Any]] = []
    cursor = 0
    while cursor < len(control_points):
        start = control_points[cursor]
        following = (
            control_points[cursor + 1] if cursor + 1 < len(control_points) else None
        )
        if (
            start["wireType"] == 2
            and following is not None
            and following["wireType"] == 4
            and following["roadId"] == start["roadId"]
        ):
            segment_index = len(segments)
            start["status"] = "paired"
            start["segmentIndex"] = segment_index
            following["status"] = "paired"
            following["segmentIndex"] = segment_index
            segments.append(
                {
                    "index": segment_index,
                    "roadId": start["roadId"],
                    "startSourceIndex": start["sourceIndex"],
                    "endSourceIndex": following["sourceIndex"],
                    "sageStart": list(start["sagePosition"]),
                    "sageEnd": list(following["sagePosition"]),
                    "godotStart": list(start["godotPosition"]),
                    "godotEnd": list(following["godotPosition"]),
                }
            )
            cursor += 2
            continue

        wire_type = int(start["wireType"])
        if wire_type not in (2, 4):
            reason = "unsupported-road-control-wire-type"
        elif wire_type == 4:
            reason = "unpaired-segment-end"
        elif following is None:
            reason = "unpaired-segment-start"
        elif following["wireType"] != 4:
            reason = "segment-start-not-followed-by-wire-type-4"
        else:
            reason = "segment-road-id-mismatch"
        diagnostics.append(
            {
                "sourceIndex": start["sourceIndex"],
                "roadId": start["roadId"],
                "wireType": wire_type,
                "reason": reason,
            }
        )
        cursor += 1

    road_ids = sorted(
        {str(point["roadId"]) for point in control_points},
        key=lambda value: (value.casefold(), value),
    )
    paired_count = sum(point["status"] == "paired" for point in control_points)
    unresolved_count = len(control_points) - paired_count
    summary = {
        "status": (
            "empty"
            if not control_points
            else "exact-paired-control-points"
            if unresolved_count == 0
            else "unresolved-control-points"
        ),
        "roadIdCount": len(road_ids),
        "controlPointCount": len(control_points),
        "pairedControlPointCount": paired_count,
        "unresolvedControlPointCount": unresolved_count,
        "segmentCount": len(segments),
        "unresolvedDiagnosticCount": len(diagnostics),
    }
    return {
        "schema": "openbfme.sage-roads",
        "schemaVersion": 0,
        "coordinateTransform": _ROAD_COORDINATE_TRANSFORM,
        "pairingPolicy": _ROAD_PAIRING_POLICY,
        "curveReconstruction": _ROAD_CURVE_RECONSTRUCTION,
        "roadIds": road_ids,
        "summary": summary,
        "controlPoints": control_points,
        "segments": segments,
        "unresolvedDiagnostics": diagnostics,
    }


def _validate_road_partition(
    objects: dict[str, Any] | None,
    roads: dict[str, Any] | None,
    map_data: dict[str, Any] | None,
    errors: _Errors,
) -> tuple[dict[str, Any], dict[str, int] | None]:
    facts: dict[str, Any] = {
        "roadPartitionAttested": False,
        "roadInventoryAttested": False,
        "roadControlPointCount": None,
        "nonRoadObjectCount": None,
        "roadSegmentCount": None,
        "unresolvedRoadControlPointCount": None,
    }
    if objects is None:
        return facts, None
    records = objects.get("objects")
    if not isinstance(records, list):
        return facts, None

    road_objects: list[dict[str, Any]] = []
    nonroad_type_counts: dict[str, int] = {}
    valid = True
    road_source_indices: set[int] = set()
    nonroad_source_indices: set[int] = set()
    for source_index, item in enumerate(records):
        if not isinstance(item, dict):
            errors.add("objects.json contains a non-object placement record")
            valid = False
            continue
        if item.get("index") != source_index:
            errors.add("objects.json source indices are not exact ordered ordinals")
            valid = False
        road_type = item.get("roadType")
        type_name = item.get("typeName")
        if not _is_plain_int(road_type, maximum=0xFFFFFFFF):
            errors.add("objects.json roadType is not an unsigned 32-bit integer")
            valid = False
            continue
        if not isinstance(type_name, str):
            errors.add("objects.json typeName is not a string")
            valid = False
            continue
        if road_type == 0:
            nonroad_source_indices.add(source_index)
            nonroad_type_counts[type_name] = nonroad_type_counts.get(type_name, 0) + 1
            continue
        if not _is_json_position(item.get("sagePosition")) or not _is_json_position(
            item.get("godotPosition")
        ):
            errors.add("objects.json road control position is invalid")
            valid = False
            continue
        road_source_indices.add(source_index)
        road_objects.append(item)

    all_indices = road_source_indices | nonroad_source_indices
    partition_exact = (
        valid
        and road_source_indices.isdisjoint(nonroad_source_indices)
        and all_indices == set(range(len(records)))
        and len(road_objects) + sum(nonroad_type_counts.values()) == len(records)
    )
    if not partition_exact:
        errors.add("objects.json road/non-road partition is not exact")
        return facts, None

    expected = _expected_road_document(road_objects)
    expected_summary = expected["summary"]
    facts.update(
        {
            "roadPartitionAttested": True,
            "roadControlPointCount": len(road_objects),
            "nonRoadObjectCount": sum(nonroad_type_counts.values()),
            "roadSegmentCount": expected_summary["segmentCount"],
            "unresolvedRoadControlPointCount": expected_summary[
                "unresolvedControlPointCount"
            ],
        }
    )

    road_document_exact = roads is not None and _json_values_are_exact(roads, expected)
    if roads is not None:
        if set(roads) != set(expected):
            errors.add("roads.json fields are not exact")
        for field, message in (
            ("coordinateTransform", "coordinate transform is invalid"),
            ("pairingPolicy", "pairing policy is invalid"),
            ("curveReconstruction", "curve reconstruction policy is invalid"),
            ("roadIds", "road IDs do not match ordered road objects"),
            (
                "controlPoints",
                "control points do not exactly preserve ordered road objects",
            ),
            ("segments", "segments do not match deterministic road pairing"),
            (
                "unresolvedDiagnostics",
                "diagnostics do not match unresolved road control points",
            ),
            ("summary", "summary/count/status contract is inconsistent"),
        ):
            if not _json_values_are_exact(roads.get(field), expected[field]):
                errors.add(f"roads.json {message}")
    map_summary_exact = map_data is not None and _json_values_are_exact(
        map_data.get("roadSummary"), expected_summary
    )
    if map_data is not None and not map_summary_exact:
        errors.add("map.json roadSummary disagrees with ordered road objects")
    facts["roadInventoryAttested"] = road_document_exact and map_summary_exact
    return facts, nonroad_type_counts


def _validate_object_bindings_partition(
    bindings: dict[str, Any] | None,
    map_data: dict[str, Any] | None,
    nonroad_type_counts: dict[str, int] | None,
    errors: _Errors,
) -> dict[str, Any]:
    facts: dict[str, Any] = {
        "objectBindingPartitionAttested": False,
        "objectBindingTypeCount": None,
        "objectBindingPlacementCount": None,
    }
    if bindings is None or nonroad_type_counts is None:
        return facts
    expected_binding_fields = {
        "schema",
        "schemaVersion",
        "matchPolicy",
        "summary",
        "records",
    }
    binding_contract_exact = (
        set(bindings) == expected_binding_fields
        and bindings.get("schema") == "openbfme.sage-object-bindings"
        and bindings.get("schemaVersion") == 0
        and bindings.get("matchPolicy") == "explicit-exact-type-name-only"
    )
    if set(bindings) != expected_binding_fields:
        errors.add("object-bindings.json fields are not exact")
    if bindings.get("matchPolicy") != "explicit-exact-type-name-only":
        errors.add("object-bindings.json match policy is invalid")

    summary = bindings.get("summary")
    records = bindings.get("records")
    if not isinstance(summary, dict) or not isinstance(records, list):
        errors.add("object-bindings.json summary/records contract is invalid")
        return facts

    expected_pairs = [
        {"typeName": type_name, "placementCount": nonroad_type_counts[type_name]}
        for type_name in sorted(
            nonroad_type_counts,
            key=lambda value: (value.casefold(), value),
        )
    ]
    actual_pairs: list[dict[str, Any]] = []
    status_type_counts = {status: 0 for status in ("bound", "logical", "unresolved")}
    status_placement_counts = {
        status: 0 for status in ("bound", "logical", "unresolved")
    }
    records_valid = True
    for record in records:
        if not isinstance(record, dict):
            records_valid = False
            errors.add("object-bindings.json contains a non-object type record")
            continue
        type_name = record.get("typeName")
        placement_count = record.get("placementCount")
        status = record.get("status")
        if (
            not isinstance(type_name, str)
            or not _is_plain_int(placement_count, minimum=1)
            or status not in status_type_counts
        ):
            records_valid = False
            errors.add("object-bindings.json type record is invalid")
            continue
        actual_pairs.append({"typeName": type_name, "placementCount": placement_count})
        status_type_counts[status] += 1
        status_placement_counts[status] += placement_count

    if not records_valid:
        return facts
    if not _json_values_are_exact(actual_pairs, expected_pairs):
        errors.add(
            "object-bindings.json type/placement records disagree with non-road objects"
        )

    expected_summary = {
        "resolutionStatus": (
            "complete" if status_type_counts["unresolved"] == 0 else "partial"
        ),
        "typeCount": len(records),
        "placementCount": sum(status_placement_counts.values()),
        "resolvedTypeCount": (
            status_type_counts["bound"] + status_type_counts["logical"]
        ),
        "resolvedPlacementCount": (
            status_placement_counts["bound"] + status_placement_counts["logical"]
        ),
        "boundTypeCount": status_type_counts["bound"],
        "boundPlacementCount": status_placement_counts["bound"],
        "logicalTypeCount": status_type_counts["logical"],
        "logicalPlacementCount": status_placement_counts["logical"],
        "unresolvedTypeCount": status_type_counts["unresolved"],
        "unresolvedPlacementCount": status_placement_counts["unresolved"],
    }
    if not _json_values_are_exact(summary, expected_summary):
        errors.add("object-bindings.json summary totals are inconsistent")
    if expected_summary["placementCount"] != sum(nonroad_type_counts.values()):
        errors.add(
            "object-bindings.json placement total disagrees with non-road objects"
        )
    if expected_summary["typeCount"] != len(nonroad_type_counts):
        errors.add("object-bindings.json type total disagrees with non-road objects")
    if map_data is not None and not _json_values_are_exact(
        map_data.get("objectResolution"), expected_summary
    ):
        errors.add("map.json objectResolution disagrees with object-bindings.json")

    facts.update(
        {
            "objectBindingPartitionAttested": (
                binding_contract_exact
                and _json_values_are_exact(actual_pairs, expected_pairs)
                and _json_values_are_exact(summary, expected_summary)
            ),
            "objectBindingTypeCount": len(records),
            "objectBindingPlacementCount": expected_summary["placementCount"],
        }
    )
    return facts


def validate_cooked_sage_map(path: Path | str) -> dict[str, Any]:
    """Backtest a cooked SAGE map directory against its public JSON contract."""

    root = Path(path)
    errors = _Errors()
    evidence = _base_evidence("cooked-sage-map")
    evidence.pop("size")
    evidence.pop("sha256")
    evidence["structuralValid"] = False
    evidence["runnable"] = None
    evidence["gameplayFidelityClaimed"] = False
    if root.is_symlink() or not root.is_dir():
        errors.add("cooked map artifact is not a real directory")
        evidence["inventory"] = []
        return _finish_cooked_map_evidence(evidence, errors)
    inventory: dict[str, dict[str, Any]] = {}
    documents = {
        relative: _map_json(root, relative, errors, inventory)
        for relative in _MAP_JSON_SCHEMAS
    }
    terrain = documents["terrain.json"]
    blend_document = terrain.get("blend") if isinstance(terrain, dict) else None
    blend_version = (
        blend_document.get("version") if isinstance(blend_document, dict) else None
    )
    blend_cell_word_bits = (
        _BLEND_CELL_WORD_BITS_BY_VERSION.get(blend_version, 32)
        if _is_plain_int(blend_version)
        else 32
    )
    map_grid_binaries = _map_grid_binaries(blend_cell_word_bits)
    required_binaries = (
        "heightmap.r16",
        "impassability.bit",
        *[item[0] for item in map_grid_binaries.values()],
        *[item[0] for item in _MAP_TABLE_BINARIES.values()],
    )
    for relative in required_binaries:
        if relative not in inventory:
            _map_file_identity(root, relative, errors, inventory)

    map_data = documents["map.json"]
    objects = documents["objects.json"]
    roads = documents["roads.json"]
    bindings = documents["object-bindings.json"]
    waypoints = documents["waypoints.json"]
    setup = documents["setup.json"]
    chunks = documents["chunks.json"]
    triggers = documents["triggers.json"]
    profile = _cooked_map_profile(map_data, setup, chunks, errors)
    evidence["runnable"] = profile["runnable"]

    if map_data is not None:
        for field, relative in (
            ("terrain", "terrain.json"),
            ("water", "water.json"),
            ("objects", "objects.json"),
            ("roads", "roads.json"),
            ("objectBindings", "object-bindings.json"),
            ("waypoints", "waypoints.json"),
            ("setup", "setup.json"),
            ("triggers", "triggers.json"),
            ("inventory", "chunks.json"),
        ):
            _expect_map_reference(map_data, field, relative, errors)
        if (
            map_data.get("sourceBinaryImported") is not True
            or map_data.get("sourceBinaryPackaged") is not False
        ):
            errors.add("map.json source-binary containment flags are invalid")

    width = height = area = None
    versioned_blend_required_files = 0
    if terrain is not None:
        height_data = terrain.get("height")
        if not isinstance(height_data, dict):
            errors.add("terrain.json height contract is missing")
        else:
            width = height_data.get("width")
            height = height_data.get("height")
            area = height_data.get("area")
            minimum_dimension = 1 if profile["allowOneCellTerrain"] else 2
            if (
                not _is_plain_int(width, minimum=minimum_dimension, maximum=65_536)
                or not _is_plain_int(height, minimum=minimum_dimension, maximum=65_536)
                or not _is_plain_int(area)
                or area != width * height
            ):
                errors.add("terrain.json height dimensions and area are inconsistent")
            heightmap = height_data.get("heightmap")
            if (
                not isinstance(heightmap, dict)
                or heightmap.get("path") != "heightmap.r16"
            ):
                errors.add("terrain.json heightmap reference is invalid")
            if _is_plain_int(area):
                actual_size = inventory.get("heightmap.r16", {}).get("size")
                if actual_size != area * 2:
                    errors.add("heightmap.r16 size does not match terrain dimensions")
        passability = terrain.get("passability")
        if (
            not isinstance(passability, dict)
            or passability.get("path") != "impassability.bit"
        ):
            errors.add("terrain.json passability reference is invalid")
        elif _is_plain_int(width, minimum=1) and _is_plain_int(height, minimum=1):
            expected_stride = (width + 7) // 8
            if passability.get("rowStrideBytes") != expected_stride:
                errors.add("terrain.json passability row stride is inconsistent")
            if (
                inventory.get("impassability.bit", {}).get("size")
                != expected_stride * height
            ):
                errors.add("impassability.bit size does not match terrain dimensions")

        source_layers = terrain.get("sourceLayers")
        if not isinstance(source_layers, dict):
            errors.add("terrain.json sourceLayers contract is missing")
        elif (
            source_layers.get("schema") != "openbfme.sage-terrain-source-layers"
            or source_layers.get("schemaVersion") != 0
        ):
            errors.add("terrain.json sourceLayers schema contract is invalid")
        else:
            if (
                source_layers.get("gridWidth") != width
                or source_layers.get("gridHeight") != height
                or source_layers.get("cellCount") != area
            ):
                errors.add(
                    "terrain.json source layer grid disagrees with height dimensions"
                )
            layer_descriptors = source_layers.get("layers")
            table_descriptors = source_layers.get("descriptionTables")
            if not isinstance(layer_descriptors, dict):
                errors.add("terrain.json source layer descriptors are missing")
                layer_descriptors = {}
            if not isinstance(table_descriptors, dict):
                errors.add("terrain.json source table descriptors are missing")
                table_descriptors = {}
            if _is_plain_int(area):
                for key, (relative, cell_size) in map_grid_binaries.items():
                    descriptor = layer_descriptors.get(key)
                    if isinstance(descriptor, dict):
                        if (
                            descriptor.get("cellCount") != area
                            or descriptor.get("cellSizeBytes") != cell_size
                        ):
                            errors.add(
                                f"terrain source layer {key} cell metadata is inconsistent"
                            )
                    _check_descriptor(
                        root=root,
                        descriptor=descriptor,
                        expected_path=relative,
                        expected_size=area * cell_size,
                        label=f"terrain source layer {key}",
                        errors=errors,
                        inventory=inventory,
                    )
                alternate_word_bits = 32 if blend_cell_word_bits == 16 else 16
                alternate_grid_binaries = _map_grid_binaries(alternate_word_bits)
                for key in ("blendCells", "threeWayBlendCells", "cliffCells"):
                    alternate_path = alternate_grid_binaries[key][0]
                    target = _safe_map_path(
                        root,
                        alternate_path,
                        errors,
                        f"terrain source layer {key} alternate word width",
                    )
                    if target is not None and target.exists():
                        errors.add(
                            f"terrain source layer {key} has a conflicting "
                            f"{alternate_word_bits}-bit file"
                        )
            blend = terrain.get("blend")
            blend = blend if isinstance(blend, dict) else {}
            for key, (
                relative,
                record_size,
                source_count_field,
            ) in _MAP_TABLE_BINARIES.items():
                descriptor = table_descriptors.get(key)
                record_count = (
                    descriptor.get("recordCount")
                    if isinstance(descriptor, dict)
                    else None
                )
                if not _is_plain_int(record_count):
                    errors.add(f"terrain source table {key} recordCount is invalid")
                    expected_size = 0
                else:
                    expected_size = record_count * record_size
                    if descriptor.get("recordSizeBytes") != record_size:
                        errors.add(
                            f"terrain source table {key} record size is inconsistent"
                        )
                    source_count = blend.get(source_count_field)
                    if _is_plain_int(source_count) and record_count != max(
                        source_count - 1, 0
                    ):
                        errors.add(
                            f"terrain source table {key} count disagrees with blend metadata"
                        )
                _check_descriptor(
                    root=root,
                    descriptor=descriptor,
                    expected_path=relative,
                    expected_size=expected_size,
                    label=f"terrain source table {key}",
                    errors=errors,
                    inventory=inventory,
                )
            versioned_blend_required_files = _validate_versioned_blend_layers(
                root=root,
                source_layers=source_layers,
                blend=blend,
                width=width if _is_plain_int(width, minimum=1) else None,
                height=height if _is_plain_int(height, minimum=1) else None,
                map_data=map_data,
                setup=setup,
                chunks=chunks,
                required_binaries=required_binaries,
                errors=errors,
                inventory=inventory,
            )

    object_count = _validate_count_array(
        objects, "count", "objects", "objects.json", errors
    )
    _validate_count_array(triggers, "count", "areas", "triggers.json", errors)
    waypoint_count = _validate_count_array(
        waypoints, "count", "waypoints", "waypoints.json", errors
    )
    road_partition_facts, nonroad_type_counts = _validate_road_partition(
        objects,
        roads,
        map_data,
        errors,
    )
    object_binding_facts = _validate_object_bindings_partition(
        bindings,
        map_data,
        nonroad_type_counts,
        errors,
    )
    waypoint_semantics_facts = _validate_waypoint_runtime_semantics(
        waypoints,
        errors,
    )
    side_semantics_facts = _validate_side_runtime_semantics(setup, errors)
    team_semantics_facts = _validate_team_runtime_semantics(setup, errors)
    lobby_source_facts = _validate_lobby_source_absence(
        setup,
        chunks,
        profile,
        errors,
    )

    if setup is not None and waypoints is not None:
        bindings_list = waypoints.get("playerStartBindings")
        declared = setup.get("declaredPlayerCount")
        if not isinstance(bindings_list, list) or not _is_plain_int(declared):
            errors.add("setup/player-start count contract is invalid")
        elif len(bindings_list) != declared:
            errors.add("setup declaredPlayerCount disagrees with player-start bindings")
        if (
            waypoint_count is not None
            and isinstance(bindings_list, list)
            and len(bindings_list) > waypoint_count
        ):
            errors.add("player-start binding count exceeds waypoint count")

    if map_data is not None and chunks is not None:
        map_source = map_data.get("source")
        chunk_source = chunks.get("source")
        if not isinstance(map_source, dict) or not isinstance(chunk_source, dict):
            errors.add("map/chunks source evidence is missing")
        else:
            map_hash = map_source.get("sha256")
            chunk_hash = chunk_source.get("sha256")
            if not _is_sha256(map_hash) or map_hash != chunk_hash:
                errors.add("map.json source hash disagrees with chunks.json")
            if (
                map_source.get("packaged") is not False
                or chunk_source.get("packaged") is not False
            ):
                errors.add(
                    "cooked map source evidence incorrectly claims packaged source bytes"
                )

    inventory_rows = [
        {"path": relative, **inventory[relative]} for relative in sorted(inventory)
    ]
    digest_input = "".join(
        f"{item['path']}\0{item['size']}\0{item['sha256']}\n" for item in inventory_rows
    ).encode("utf-8")
    evidence["inventory"] = inventory_rows
    evidence["treeSha256"] = hashlib.sha256(digest_input).hexdigest()
    evidence["facts"] = {
        "checkedFileCount": len(inventory_rows),
        "requiredFileCount": (
            len(_MAP_JSON_SCHEMAS)
            + len(required_binaries)
            + versioned_blend_required_files
        ),
        "width": width if _is_plain_int(width) else None,
        "height": height if _is_plain_int(height) else None,
        "cellCount": area if _is_plain_int(area) else None,
        "objectCount": object_count,
        "waypointCount": waypoint_count,
        "mapKind": profile["mapKind"],
        "profileVersion": profile["profileVersion"],
        "profileAttested": profile["profileAttested"],
        "runnable": profile["runnable"],
        "gameplayFidelityClaimed": False,
        **road_partition_facts,
        **object_binding_facts,
        **waypoint_semantics_facts,
        **side_semantics_facts,
        **team_semantics_facts,
        **lobby_source_facts,
    }
    return _finish_cooked_map_evidence(evidence, errors)


def validate_native_output(
    family: str,
    path: Path | str,
    *,
    ffprobe_metadata: Any | None = None,
) -> dict[str, Any]:
    """Dispatch one native-output family without throwing on an unknown family."""

    normalized = str(family).strip().casefold().replace("_", "-")
    validators = {
        "png": validate_png,
        "wav": validate_wav,
        "wav-pcm": validate_wav,
        "mp3": lambda value: validate_mp3(value, ffprobe_metadata),
        "glb": validate_glb,
        "glb-v2": validate_glb,
        "sage-map": validate_cooked_sage_map,
        "cooked-sage-map": validate_cooked_sage_map,
    }
    validator = validators.get(normalized)
    if validator is not None:
        return validator(path)
    errors = _Errors()
    errors.add("unknown native-output family")
    evidence = _base_evidence("unknown")
    return _finish(evidence, errors)


__all__ = [
    "EVIDENCE_SCHEMA",
    "EVIDENCE_SCHEMA_VERSION",
    "validate_cooked_sage_map",
    "validate_glb",
    "validate_mp3",
    "validate_native_output",
    "validate_png",
    "validate_wav",
]
