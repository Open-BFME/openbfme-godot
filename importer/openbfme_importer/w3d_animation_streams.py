"""Bounded decoders for evidence-backed W3D animation channel payloads.

The layouts in this module are limited to the repository's pinned OpenSAGE
BlenderPlugin reader.  Callers provide a single chunk *payload* together with
cardinalities already validated from its owning animation and hierarchy.

Decoding one channel does not prove animation, skeleton, GLB, or render
completeness.  In particular, the adaptive-delta expansion mirrors the pinned
reader's numeric contract; it does not silently normalize reconstructed
quaternions or establish downstream interpolation semantics.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import math
import struct
from typing import Iterable, Literal, TypeAlias


ANIMATION_STREAM_SCHEMA = "openbfme.w3d-animation-channel"
ANIMATION_STREAM_SCHEMA_VERSION = 2
MAX_ANIMATION_STREAM_BYTES = 512 * 1024 * 1024
MAX_ANIMATION_FRAMES = 10_000_000
MAX_ANIMATION_PIVOTS = 65_536
QUATERNION_NORM_SQUARED_TOLERANCE = 0.02

W3D_CHUNK_ANIMATION_CHANNEL = 0x00000202
W3D_CHUNK_ANIMATION_BIT_CHANNEL = 0x00000203
W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL = 0x00000282
W3D_CHUNK_COMPRESSED_BIT_CHANNEL = 0x00000283
W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL = 0x00000284

W3D_COMPRESSED_FLAVOR_TIME_CODED = 0
W3D_COMPRESSED_FLAVOR_ADAPTIVE_DELTA = 1

W3D_CHANNEL_X = 0
W3D_CHANNEL_Y = 1
W3D_CHANNEL_Z = 2
W3D_CHANNEL_QUATERNION = 6
W3D_CHANNEL_VISIBILITY = 15

SUPPORTED_ANIMATION_STREAM_CHUNK_IDS = frozenset(
    {
        W3D_CHUNK_ANIMATION_CHANNEL,
        W3D_CHUNK_ANIMATION_BIT_CHANNEL,
        W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
        W3D_CHUNK_COMPRESSED_BIT_CHANNEL,
        W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL,
    }
)

Quaternion: TypeAlias = tuple[float, float, float, float]
ChannelValue: TypeAlias = float | Quaternion
RangeValue: TypeAlias = int | float
PrimaryImportDisposition: TypeAlias = Literal["bound", "skipped-invalid-pivot"]


class W3DAnimationStreamDecodeError(ValueError):
    """Raised when a known channel payload violates its exact contract."""


class W3DAnimationStreamUnsupportedError(W3DAnimationStreamDecodeError):
    """Raised when no evidence-backed decoder exists for a layout variant."""


@dataclass(frozen=True, slots=True)
class W3DAnimationKey:
    """One decoded animation sample or sparse time-coded key."""

    frame: int
    value: ChannelValue
    interpolated: bool | None = None


@dataclass(frozen=True, slots=True)
class W3DAnimationBitKey:
    """One decoded visibility/bit-channel sample or sparse key."""

    frame: int
    value: bool


AnimationKey: TypeAlias = W3DAnimationKey | W3DAnimationBitKey


@dataclass(frozen=True, slots=True)
class W3DAnimationDeltaBlock:
    """One pinned adaptive-delta block after signed-byte unpacking."""

    vector_index: int
    block_index: int
    packed_deltas: tuple[int, ...]


@dataclass(frozen=True, slots=True)
class W3DAnimationChannel:
    """Immutable decoded channel with no source path, name, or payload bytes."""

    chunk_id: int
    stream_kind: str
    channel_type: int
    channel_type_name: str
    vector_width: int
    pivot_index: int
    owner_frame_count: int
    owner_pivot_count: int
    first_frame: int | None
    last_frame: int | None
    default_value: float | int | None
    scale: float | None
    delta_bits: int | None
    padding_length: int
    keys: tuple[AnimationKey, ...]
    delta_blocks: tuple[W3DAnimationDeltaBlock, ...]
    opaque_header_value: int | None = None
    padding_sha256: str | None = None
    unused_bit_count: int | None = None
    unused_bit_mask: int | None = None
    unused_bit_value: int | None = None

    @property
    def pivot_in_range(self) -> bool:
        """Whether the primary importer can bind this channel to its owner."""

        return 0 <= self.pivot_index < self.owner_pivot_count

    @property
    def primary_import_disposition(self) -> PrimaryImportDisposition:
        """Mirror the pinned importer's invalid-pivot channel disposition."""

        return "bound" if self.pivot_in_range else "skipped-invalid-pivot"


@dataclass(frozen=True, slots=True)
class W3DAnimationRange:
    """Inclusive numeric range used by a payload-free attestation."""

    name: str
    minimum: RangeValue | None
    maximum: RangeValue | None

    def neutral(self) -> dict[str, object]:
        return {"minimum": self.minimum, "maximum": self.maximum}

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DAnimationChannelAttestation:
    """Deterministic structural evidence for one decoded channel payload."""

    chunk_id: int
    stream_kind: str
    channel_type: int
    channel_type_name: str
    vector_width: int
    pivot_index: int
    owner_frame_count: int
    owner_pivot_count: int
    pivot_in_range: bool
    primary_import_disposition: PrimaryImportDisposition
    record_count: int
    key_count: int
    delta_block_count: int
    interpolation_key_count: int
    first_frame: int | None
    last_frame: int | None
    delta_bits: int | None
    padding_length: int
    payload_byte_length: int
    payload_sha256: str
    canonical_record_sha256: str
    ranges: tuple[W3DAnimationRange, ...]
    opaque_header_value: int | None = None
    padding_sha256: str | None = None
    unused_bit_count: int | None = None
    unused_bit_mask: int | None = None
    unused_bit_value: int | None = None
    out_of_owner_frame_count: int = 0
    out_of_owner_frame_minimum: int | None = None
    out_of_owner_frame_maximum: int | None = None

    def neutral(self) -> dict[str, object]:
        """Return a payload-free, JSON-serializable attestation."""

        result: dict[str, object] = {
            "schema": ANIMATION_STREAM_SCHEMA,
            "schemaVersion": ANIMATION_STREAM_SCHEMA_VERSION,
            "chunkId": self.chunk_id,
            "chunkIdHex": f"0x{self.chunk_id:08X}",
            "streamKind": self.stream_kind,
            "channelType": self.channel_type,
            "channelTypeName": self.channel_type_name,
            "vectorWidth": self.vector_width,
            "pivotIndex": self.pivot_index,
            "ownerFrameCount": self.owner_frame_count,
            "ownerPivotCount": self.owner_pivot_count,
            "pivotInRange": self.pivot_in_range,
            "primaryImportDisposition": self.primary_import_disposition,
            "recordCount": self.record_count,
            "keyCount": self.key_count,
            "deltaBlockCount": self.delta_block_count,
            "interpolationKeyCount": self.interpolation_key_count,
            "firstFrame": self.first_frame,
            "lastFrame": self.last_frame,
            "paddingLength": self.padding_length,
            "payloadByteLength": self.payload_byte_length,
            "payloadSha256": self.payload_sha256,
            "canonicalRecordSha256": self.canonical_record_sha256,
            "outOfOwnerFrameCount": self.out_of_owner_frame_count,
            "outOfOwnerFrameRange": {
                "minimum": self.out_of_owner_frame_minimum,
                "maximum": self.out_of_owner_frame_maximum,
            },
            "ranges": {item.name: item.neutral() for item in self.ranges},
        }
        if self.delta_bits is not None:
            result["deltaBits"] = self.delta_bits
        if self.opaque_header_value is not None:
            result["opaqueHeaderValue"] = self.opaque_header_value
        if self.padding_sha256 is not None:
            result["paddingSha256"] = self.padding_sha256
        if self.unused_bit_count is not None:
            result["unusedBitCount"] = self.unused_bit_count
            result["unusedBitMask"] = self.unused_bit_mask
            result["unusedBitValue"] = self.unused_bit_value
        return result

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DAnimationChannelDecode:
    """Immutable decoded representation paired with its public attestation."""

    channel: W3DAnimationChannel
    attestation: W3DAnimationChannelAttestation


_CHANNEL_LAYOUTS = {
    W3D_CHANNEL_X: ("x-translation", 1),
    W3D_CHANNEL_Y: ("y-translation", 1),
    W3D_CHANNEL_Z: ("z-translation", 1),
    W3D_CHANNEL_QUATERNION: ("quaternion-rotation", 4),
    W3D_CHANNEL_VISIBILITY: ("scalar-visibility", 1),
}


def _plain_count(
    value: object,
    label: str,
    *,
    maximum: int,
    allow_zero: bool = False,
) -> int:
    minimum = 0 if allow_zero else 1
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < minimum
        or value > maximum
    ):
        raise W3DAnimationStreamDecodeError(
            f"{label} must be an integer in {minimum}..{maximum}"
        )
    return value


def _chunk_id(value: object) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < 0
        or value > 0xFFFFFFFF
    ):
        raise W3DAnimationStreamDecodeError(
            "chunk ID must be an unsigned 32-bit integer"
        )
    return value


def _payload_bytes(payload: object) -> bytes:
    if not isinstance(payload, bytes):
        raise TypeError("W3D animation channel payload must be bytes")
    if len(payload) > MAX_ANIMATION_STREAM_BYTES:
        raise W3DAnimationStreamDecodeError(
            f"animation channel exceeds {MAX_ANIMATION_STREAM_BYTES} byte limit"
        )
    return payload


def _owners(
    animation_frame_count: object,
    pivot_count: object,
) -> tuple[int, int]:
    return (
        _plain_count(
            animation_frame_count,
            "animation frame count",
            maximum=MAX_ANIMATION_FRAMES,
        ),
        _plain_count(
            pivot_count,
            "pivot count",
            maximum=MAX_ANIMATION_PIVOTS,
        ),
    )


def _require_length(payload: bytes, expected: int, kind: str) -> None:
    if len(payload) != expected:
        raise W3DAnimationStreamDecodeError(
            f"{kind} payload length {len(payload)} does not match exact "
            f"{expected}-byte layout"
        )


def _require_header(payload: bytes, minimum: int, kind: str) -> None:
    if len(payload) < minimum:
        raise W3DAnimationStreamDecodeError(
            f"{kind} payload is truncated before its {minimum}-byte header"
        )


def _validate_frame(frame: int, frame_count: int, kind: str) -> None:
    if frame < 0 or frame >= frame_count:
        raise W3DAnimationStreamDecodeError(
            f"{kind} frame {frame} is outside animation frame count {frame_count}"
        )


def _validate_frame_span(
    first_frame: int,
    last_frame: int,
    frame_count: int,
    kind: str,
) -> int:
    if last_frame < first_frame:
        raise W3DAnimationStreamDecodeError(
            f"{kind} last frame {last_frame} precedes first frame {first_frame}"
        )
    _validate_frame(first_frame, frame_count, kind)
    _validate_frame(last_frame, frame_count, kind)
    return last_frame - first_frame + 1


def _layout(channel_type: int, vector_width: int) -> tuple[str, int]:
    try:
        name, expected_width = _CHANNEL_LAYOUTS[channel_type]
    except KeyError as error:
        raise W3DAnimationStreamUnsupportedError(
            f"channel type {channel_type} has no evidence-backed value semantics"
        ) from error
    if vector_width != expected_width:
        raise W3DAnimationStreamDecodeError(
            f"channel type {channel_type} requires vector width "
            f"{expected_width}, got {vector_width}"
        )
    return name, expected_width


def _bit_layout(channel_type: int) -> tuple[str, int]:
    if channel_type != 0:
        raise W3DAnimationStreamUnsupportedError(
            f"bit-channel type {channel_type} has no evidence-backed semantics"
        )
    return "bit-visibility", 1


def _finite(values: Iterable[float], kind: str, record_index: int) -> None:
    if not all(math.isfinite(value) for value in values):
        raise W3DAnimationStreamDecodeError(
            f"{kind} record {record_index} contains a non-finite float"
        )


def _validate_quaternion(value: Quaternion, kind: str, record_index: int) -> None:
    _finite(value, kind, record_index)
    norm_squared = sum(component * component for component in value)
    if abs(norm_squared - 1.0) > QUATERNION_NORM_SQUARED_TOLERANCE:
        raise W3DAnimationStreamDecodeError(
            f"{kind} record {record_index} quaternion is not normalized "
            f"(norm squared {norm_squared:.9g})"
        )


def _read_value(
    payload: bytes,
    offset: int,
    channel_type: int,
    *,
    kind: str,
    record_index: int,
    validate_rotation: bool = True,
) -> tuple[ChannelValue, int]:
    if channel_type == W3D_CHANNEL_QUATERNION:
        value: ChannelValue = struct.unpack_from("<4f", payload, offset)
        if validate_rotation:
            _validate_quaternion(value, kind, record_index)
        else:
            _finite(value, kind, record_index)
        return value, offset + 16
    value = struct.unpack_from("<f", payload, offset)[0]
    _finite((value,), kind, record_index)
    return value, offset + 4


def _strict_times(
    frames: Iterable[int],
    kind: str,
) -> tuple[int, ...]:
    """Validate encoded key ordering without imposing owner-frame semantics."""

    result = tuple(frames)
    previous: int | None = None
    for index, frame in enumerate(result):
        if previous is not None and frame <= previous:
            raise W3DAnimationStreamDecodeError(
                f"{kind} time code {frame} at record {index} is not strictly increasing"
            )
        previous = frame
    return result


def _range(values: Iterable[RangeValue]) -> tuple[RangeValue | None, RangeValue | None]:
    iterator = iter(values)
    try:
        first = next(iterator)
    except StopIteration:
        return None, None
    minimum = maximum = first
    for value in iterator:
        minimum = min(minimum, value)
        maximum = max(maximum, value)
    return minimum, maximum


def _ranges(
    keys: tuple[AnimationKey, ...],
    blocks: tuple[W3DAnimationDeltaBlock, ...],
    vector_width: int,
    scale: float | None,
) -> tuple[W3DAnimationRange, ...]:
    columns: list[tuple[str, Iterable[RangeValue]]] = [
        ("frame", (key.frame for key in keys)),
    ]
    if vector_width == 4:
        names = ("valueX", "valueY", "valueZ", "valueW")
        for component, name in enumerate(names):
            columns.append(
                (
                    name,
                    (
                        key.value[component]
                        for key in keys
                        if isinstance(key, W3DAnimationKey)
                        and isinstance(key.value, tuple)
                    ),
                )
            )
    else:
        columns.append(
            (
                "value",
                (
                    int(key.value) if isinstance(key, W3DAnimationBitKey) else key.value
                    for key in keys
                ),
            )
        )
    if blocks:
        columns.extend(
            (
                ("deltaBlockIndex", (block.block_index for block in blocks)),
                (
                    "packedDelta",
                    (value for block in blocks for value in block.packed_deltas),
                ),
            )
        )
    if scale is not None:
        columns.append(("scale", (scale,)))
    result: list[W3DAnimationRange] = []
    for name, values in columns:
        minimum, maximum = _range(values)
        result.append(W3DAnimationRange(name, minimum, maximum))
    return tuple(result)


def _float_token(value: float) -> str:
    return value.hex()


def _value_token(value: ChannelValue | bool) -> object:
    if isinstance(value, tuple):
        return [_float_token(component) for component in value]
    if isinstance(value, bool):
        return value
    return _float_token(value)


def _canonical_record_hash(channel: W3DAnimationChannel) -> str:
    canonical = {
        "chunkId": channel.chunk_id,
        "streamKind": channel.stream_kind,
        "channelType": channel.channel_type,
        "vectorWidth": channel.vector_width,
        "pivotIndex": channel.pivot_index,
        "ownerFrameCount": channel.owner_frame_count,
        "ownerPivotCount": channel.owner_pivot_count,
        "pivotInRange": channel.pivot_in_range,
        "primaryImportDisposition": channel.primary_import_disposition,
        "firstFrame": channel.first_frame,
        "lastFrame": channel.last_frame,
        "defaultValue": (
            None
            if channel.default_value is None
            else (
                _float_token(channel.default_value)
                if isinstance(channel.default_value, float)
                else channel.default_value
            )
        ),
        "scale": None if channel.scale is None else _float_token(channel.scale),
        "deltaBits": channel.delta_bits,
        "paddingLength": channel.padding_length,
        "opaqueHeaderValue": channel.opaque_header_value,
        "paddingSha256": channel.padding_sha256,
        "unusedBitCount": channel.unused_bit_count,
        "unusedBitMask": channel.unused_bit_mask,
        "unusedBitValue": channel.unused_bit_value,
        "keys": [
            {
                "frame": key.frame,
                "interpolated": (
                    key.interpolated if isinstance(key, W3DAnimationKey) else None
                ),
                "value": _value_token(key.value),
            }
            for key in channel.keys
        ],
        "deltaBlocks": [
            {
                "vectorIndex": block.vector_index,
                "blockIndex": block.block_index,
                "packedDeltas": list(block.packed_deltas),
            }
            for block in channel.delta_blocks
        ],
    }
    payload = json.dumps(
        canonical,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("ascii")
    return hashlib.sha256(payload).hexdigest()


def _result(channel: W3DAnimationChannel, payload: bytes) -> W3DAnimationChannelDecode:
    adaptive = bool(channel.delta_blocks)
    record_count = 1 + len(channel.delta_blocks) if adaptive else len(channel.keys)
    out_of_owner_frames = tuple(
        key.frame
        for key in channel.keys
        if key.frame < 0 or key.frame >= channel.owner_frame_count
    )
    attestation = W3DAnimationChannelAttestation(
        chunk_id=channel.chunk_id,
        stream_kind=channel.stream_kind,
        channel_type=channel.channel_type,
        channel_type_name=channel.channel_type_name,
        vector_width=channel.vector_width,
        pivot_index=channel.pivot_index,
        owner_frame_count=channel.owner_frame_count,
        owner_pivot_count=channel.owner_pivot_count,
        pivot_in_range=channel.pivot_in_range,
        primary_import_disposition=channel.primary_import_disposition,
        record_count=record_count,
        key_count=len(channel.keys),
        delta_block_count=len(channel.delta_blocks),
        interpolation_key_count=sum(
            1
            for key in channel.keys
            if isinstance(key, W3DAnimationKey) and key.interpolated is True
        ),
        first_frame=channel.first_frame,
        last_frame=channel.last_frame,
        delta_bits=channel.delta_bits,
        padding_length=channel.padding_length,
        payload_byte_length=len(payload),
        payload_sha256=hashlib.sha256(payload).hexdigest(),
        canonical_record_sha256=_canonical_record_hash(channel),
        ranges=_ranges(
            channel.keys,
            channel.delta_blocks,
            channel.vector_width,
            channel.scale,
        ),
        opaque_header_value=channel.opaque_header_value,
        padding_sha256=channel.padding_sha256,
        unused_bit_count=channel.unused_bit_count,
        unused_bit_mask=channel.unused_bit_mask,
        unused_bit_value=channel.unused_bit_value,
        out_of_owner_frame_count=len(out_of_owner_frames),
        out_of_owner_frame_minimum=(
            min(out_of_owner_frames) if out_of_owner_frames else None
        ),
        out_of_owner_frame_maximum=(
            max(out_of_owner_frames) if out_of_owner_frames else None
        ),
    )
    return W3DAnimationChannelDecode(channel, attestation)


def decode_raw_animation_channel(
    payload: bytes,
    *,
    animation_frame_count: int,
    pivot_count: int,
) -> W3DAnimationChannelDecode:
    """Decode a 0x202 ``<6H`` header followed by direct float samples."""

    source = _payload_bytes(payload)
    frame_count, owner_pivots = _owners(animation_frame_count, pivot_count)
    kind = "raw animation channel"
    _require_header(source, 12, kind)
    first, last, vector_width, channel_type, pivot, opaque = struct.unpack_from(
        "<6H", source
    )
    channel_type_name, width = _layout(channel_type, vector_width)
    key_count = _validate_frame_span(first, last, frame_count, kind)
    data_length = key_count * width * 4
    required = 12 + data_length
    if len(source) < required:
        raise W3DAnimationStreamDecodeError(
            f"{kind} payload is truncated: {key_count} keys require at least "
            f"{required} bytes"
        )
    padding_length = len(source) - required
    padding_sha256 = hashlib.sha256(source[required:]).hexdigest()

    keys: list[AnimationKey] = []
    offset = 12
    for index in range(key_count):
        value, offset = _read_value(
            source,
            offset,
            channel_type,
            kind=kind,
            record_index=index,
        )
        keys.append(W3DAnimationKey(first + index, value))
    channel = W3DAnimationChannel(
        W3D_CHUNK_ANIMATION_CHANNEL,
        "raw-channel",
        channel_type,
        channel_type_name,
        width,
        pivot,
        frame_count,
        owner_pivots,
        first,
        last,
        None,
        None,
        None,
        padding_length,
        tuple(keys),
        (),
        opaque_header_value=opaque,
        padding_sha256=padding_sha256,
    )
    return _result(channel, source)


def decode_raw_animation_bit_channel(
    payload: bytes,
    *,
    animation_frame_count: int,
    pivot_count: int,
) -> W3DAnimationChannelDecode:
    """Decode a 0x203 9-byte header and LSB-first packed frame bits."""

    source = _payload_bytes(payload)
    frame_count, owner_pivots = _owners(animation_frame_count, pivot_count)
    kind = "raw animation bit channel"
    _require_header(source, 9, kind)
    first, last, channel_type, pivot, default_raw = struct.unpack_from("<4HB", source)
    channel_type_name, width = _bit_layout(channel_type)
    key_count = _validate_frame_span(first, last, frame_count, kind)
    packed_length = (key_count + 7) // 8
    _require_length(source, 9 + packed_length, kind)
    packed = source[9:]
    used_bits = key_count % 8
    unused_bit_count = 0 if not used_bits else 8 - used_bits
    unused_bit_mask = 0 if not used_bits else (0xFF << used_bits) & 0xFF
    unused_bit_value = 0 if not used_bits else packed[-1] & unused_bit_mask
    keys = tuple(
        W3DAnimationBitKey(
            first + index,
            bool(packed[index // 8] & (1 << (index % 8))),
        )
        for index in range(key_count)
    )
    channel = W3DAnimationChannel(
        W3D_CHUNK_ANIMATION_BIT_CHANNEL,
        "raw-bit-channel",
        channel_type,
        channel_type_name,
        width,
        pivot,
        frame_count,
        owner_pivots,
        first,
        last,
        default_raw / 255.0,
        None,
        1,
        0,
        keys,
        (),
        unused_bit_count=unused_bit_count,
        unused_bit_mask=unused_bit_mask,
        unused_bit_value=unused_bit_value,
    )
    return _result(channel, source)


def _decode_time_coded_channel(
    source: bytes,
    *,
    frame_count: int,
    owner_pivots: int,
) -> W3DAnimationChannelDecode:
    kind = "compressed time-coded animation channel"
    _require_header(source, 8, kind)
    key_count, pivot, vector_width, channel_type = struct.unpack_from("<IHBB", source)
    channel_type_name, width = _layout(channel_type, vector_width)
    _plain_count(
        key_count,
        "compressed time-coded key count",
        maximum=MAX_ANIMATION_FRAMES,
    )
    value_size = width * 4
    _require_length(source, 8 + key_count * (4 + value_size), kind)
    raw_times: list[tuple[int, bool]] = []
    values: list[ChannelValue] = []
    offset = 8
    for index in range(key_count):
        encoded_time = struct.unpack_from("<I", source, offset)[0]
        offset += 4
        raw_times.append((encoded_time & 0x7FFFFFFF, bool(encoded_time & 0x80000000)))
        value, offset = _read_value(
            source,
            offset,
            channel_type,
            kind=kind,
            record_index=index,
        )
        values.append(value)
    frames = _strict_times((frame for frame, _ in raw_times), kind)
    keys = tuple(
        W3DAnimationKey(frame, value, raw_times[index][1])
        for index, (frame, value) in enumerate(zip(frames, values, strict=True))
    )
    channel = W3DAnimationChannel(
        W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
        "compressed-time-coded-channel",
        channel_type,
        channel_type_name,
        width,
        pivot,
        frame_count,
        owner_pivots,
        frames[0],
        frames[-1],
        None,
        None,
        None,
        0,
        keys,
        (),
    )
    return _result(channel, source)


def _delta_table() -> tuple[float, ...]:
    exponents = tuple(10.0 ** (index - 8) for index in range(16))
    sinus = tuple(
        1.0 - math.sin(90.0 * (index / 240.0) * math.pi / 180.0) for index in range(240)
    )
    return exponents + sinus


_ADAPTIVE_DELTA_TABLE = _delta_table()


def _unpack_packed_deltas(values: tuple[int, ...], bits: int) -> tuple[int, ...]:
    if bits == 4:
        result: list[int] = []
        for value in values:
            lower = value & 0x0F
            if lower >= 8:
                lower -= 16
            upper = value >> 4
            result.extend((lower, upper))
        return tuple(result)
    if bits == 8:
        result = []
        for value in values:
            decoded = value + 128
            if decoded >= 128:
                decoded -= 256
            result.append(decoded)
        return tuple(result)
    raise W3DAnimationStreamUnsupportedError(
        f"adaptive-delta width {bits} has no evidence-backed decoder"
    )


def _expand_adaptive_keys(
    *,
    channel_type: int,
    key_count: int,
    scale: float,
    initial_value: ChannelValue,
    blocks: tuple[W3DAnimationDeltaBlock, ...],
    bits: int,
    kind: str,
) -> tuple[W3DAnimationKey, ...]:
    values: list[ChannelValue | None] = [None] * key_count
    values[0] = initial_value
    scale_factor = 1.0 / 16.0 if bits == 8 else 1.0
    vector_width = 4 if channel_type == W3D_CHANNEL_QUATERNION else 1
    for block_number, block in enumerate(blocks):
        delta_scale = scale * scale_factor * _ADAPTIVE_DELTA_TABLE[block.block_index]
        for delta_index, delta in enumerate(
            _unpack_packed_deltas(block.packed_deltas, bits)
        ):
            frame = (block_number // vector_width) * 16 + delta_index + 1
            if frame >= key_count:
                break
            previous = values[frame - 1]
            if previous is None:
                raise W3DAnimationStreamDecodeError(
                    f"{kind} block ordering cannot reconstruct frame {frame}"
                )
            if channel_type == W3D_CHANNEL_QUATERNION:
                if not isinstance(previous, tuple):
                    raise W3DAnimationStreamDecodeError(
                        f"{kind} quaternion state is structurally inconsistent"
                    )
                current = values[frame]
                mutable = list(previous if current is None else current)
                mutable[block.vector_index] = (
                    previous[block.vector_index] + delta_scale * delta
                )
                values[frame] = (mutable[0], mutable[1], mutable[2], mutable[3])
            else:
                if isinstance(previous, tuple):
                    raise W3DAnimationStreamDecodeError(
                        f"{kind} scalar state is structurally inconsistent"
                    )
                values[frame] = previous + delta_scale * delta
    keys: list[W3DAnimationKey] = []
    for index, value in enumerate(values):
        if value is None:
            raise W3DAnimationStreamDecodeError(
                f"{kind} did not reconstruct declared frame {index}"
            )
        if isinstance(value, tuple):
            # Compressed deltas are preserved exactly; only their directly
            # encoded initial quaternion carries a normalization guarantee.
            _finite(value, kind, index)
        else:
            _finite((value,), kind, index)
        keys.append(W3DAnimationKey(index, value))
    return tuple(keys)


def _read_adaptive_body(
    source: bytes,
    *,
    offset: int,
    key_count: int,
    channel_type: int,
    vector_width: int,
    scale: float,
    bits: int,
    trailing_padding: int,
    kind: str,
) -> tuple[
    ChannelValue,
    tuple[W3DAnimationDeltaBlock, ...],
    tuple[W3DAnimationKey, ...],
]:
    value_size = vector_width * 4
    block_count = ((key_count + 15) >> 4) * vector_width
    packed_count = bits * 2
    expected = offset + value_size + block_count * (1 + packed_count)
    expected += trailing_padding
    _require_length(source, expected, kind)
    initial, offset = _read_value(
        source,
        offset,
        channel_type,
        kind=kind,
        record_index=0,
    )
    blocks: list[W3DAnimationDeltaBlock] = []
    for block_number in range(block_count):
        block_index = source[offset]
        offset += 1
        packed = struct.unpack_from(f"<{packed_count}b", source, offset)
        offset += packed_count
        blocks.append(
            W3DAnimationDeltaBlock(
                block_number % vector_width,
                block_index,
                packed,
            )
        )
    typed_blocks = tuple(blocks)
    keys = _expand_adaptive_keys(
        channel_type=channel_type,
        key_count=key_count,
        scale=scale,
        initial_value=initial,
        blocks=typed_blocks,
        bits=bits,
        kind=kind,
    )
    return initial, typed_blocks, keys


def _decode_adaptive_delta_channel(
    source: bytes,
    *,
    frame_count: int,
    owner_pivots: int,
) -> W3DAnimationChannelDecode:
    kind = "compressed adaptive-delta animation channel"
    _require_header(source, 12, kind)
    key_count, pivot, vector_width, channel_type, scale = struct.unpack_from(
        "<IHBBf", source
    )
    channel_type_name, width = _layout(channel_type, vector_width)
    _plain_count(
        key_count,
        "compressed adaptive-delta key count",
        maximum=frame_count,
    )
    _finite((scale,), kind, 0)
    _, blocks, keys = _read_adaptive_body(
        source,
        offset=12,
        key_count=key_count,
        channel_type=channel_type,
        vector_width=width,
        scale=scale,
        bits=4,
        trailing_padding=3,
        kind=kind,
    )
    channel = W3DAnimationChannel(
        W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
        "compressed-adaptive-delta-channel",
        channel_type,
        channel_type_name,
        width,
        pivot,
        frame_count,
        owner_pivots,
        0,
        key_count - 1,
        None,
        scale,
        4,
        3,
        keys,
        blocks,
    )
    return _result(channel, source)


def decode_compressed_animation_channel(
    payload: bytes,
    *,
    animation_frame_count: int,
    pivot_count: int,
    flavor: int,
) -> W3DAnimationChannelDecode:
    """Decode 0x282 using the owning compressed-animation flavor."""

    source = _payload_bytes(payload)
    frame_count, owner_pivots = _owners(animation_frame_count, pivot_count)
    if flavor == W3D_COMPRESSED_FLAVOR_TIME_CODED:
        return _decode_time_coded_channel(
            source,
            frame_count=frame_count,
            owner_pivots=owner_pivots,
        )
    if flavor == W3D_COMPRESSED_FLAVOR_ADAPTIVE_DELTA:
        return _decode_adaptive_delta_channel(
            source,
            frame_count=frame_count,
            owner_pivots=owner_pivots,
        )
    raise W3DAnimationStreamUnsupportedError(
        f"compressed-animation flavor {flavor!r} has no evidence-backed decoder"
    )


def decode_compressed_animation_bit_channel(
    payload: bytes,
    *,
    animation_frame_count: int,
    pivot_count: int,
) -> W3DAnimationChannelDecode:
    """Decode 0x283 sparse bit keys with value stored in the high time bit."""

    source = _payload_bytes(payload)
    frame_count, owner_pivots = _owners(animation_frame_count, pivot_count)
    kind = "compressed time-coded bit channel"
    _require_header(source, 8, kind)
    key_count, pivot, channel_type, default_raw = struct.unpack_from("<IhBB", source)
    channel_type_name, width = _bit_layout(channel_type)
    _plain_count(
        key_count,
        "compressed bit-channel key count",
        maximum=MAX_ANIMATION_FRAMES,
        allow_zero=True,
    )
    _require_length(source, 8 + key_count * 4, kind)
    encoded = struct.unpack_from(f"<{key_count}I", source, 8) if key_count else ()
    frames = _strict_times((value & 0x7FFFFFFF for value in encoded), kind)
    keys = tuple(
        W3DAnimationBitKey(frame, bool(encoded[index] & 0x80000000))
        for index, frame in enumerate(frames)
    )
    channel = W3DAnimationChannel(
        W3D_CHUNK_COMPRESSED_BIT_CHANNEL,
        "compressed-time-coded-bit-channel",
        channel_type,
        channel_type_name,
        width,
        pivot,
        frame_count,
        owner_pivots,
        frames[0] if frames else None,
        frames[-1] if frames else None,
        default_raw,
        None,
        1,
        0,
        keys,
        (),
    )
    return _result(channel, source)


def _decode_motion_time_coded(
    source: bytes,
    *,
    frame_count: int,
    owner_pivots: int,
    key_count: int,
    pivot: int,
    channel_type: int,
    channel_type_name: str,
    vector_width: int,
) -> W3DAnimationChannelDecode:
    kind = "motion time-coded animation channel"
    padding_length = 2 if key_count % 2 else 0
    time_end = 8 + key_count * 2
    value_offset = time_end + padding_length
    _require_length(source, value_offset + key_count * vector_width * 4, kind)
    # Each on-disk time code is an unsigned 16-bit value whose high bit is
    # Westwood's W3D_TIMECODED_BINARY_MOVEMENT_FLAG (the 16-bit analog of the
    # 0x80000000 flag masked out of the uint32 time-coded channel times).  The
    # ordered frame value is the low 15 bits; retail motion channels set the
    # flag on step-transition keys.
    encoded = struct.unpack_from(f"<{key_count}H", source, 8)
    frames = _strict_times((value & 0x7FFF for value in encoded), kind)
    keys: list[AnimationKey] = []
    offset = value_offset
    for index, frame in enumerate(frames):
        value, offset = _read_value(
            source,
            offset,
            channel_type,
            kind=kind,
            record_index=index,
        )
        keys.append(W3DAnimationKey(frame, value, True))
    channel = W3DAnimationChannel(
        W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL,
        "motion-time-coded-channel",
        channel_type,
        channel_type_name,
        vector_width,
        pivot,
        frame_count,
        owner_pivots,
        frames[0],
        frames[-1],
        None,
        None,
        None,
        padding_length,
        tuple(keys),
        (),
    )
    return _result(channel, source)


def _decode_motion_adaptive(
    source: bytes,
    *,
    frame_count: int,
    owner_pivots: int,
    key_count: int,
    pivot: int,
    channel_type: int,
    channel_type_name: str,
    vector_width: int,
    delta_type: int,
) -> W3DAnimationChannelDecode:
    kind = "motion adaptive-delta animation channel"
    _require_header(source, 12, kind)
    scale = struct.unpack_from("<f", source, 8)[0]
    _finite((scale,), kind, 0)
    bits = delta_type * 4
    _, blocks, keys = _read_adaptive_body(
        source,
        offset=12,
        key_count=key_count,
        channel_type=channel_type,
        vector_width=vector_width,
        scale=scale,
        bits=bits,
        trailing_padding=0,
        kind=kind,
    )
    channel = W3DAnimationChannel(
        W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL,
        "motion-adaptive-delta-channel",
        channel_type,
        channel_type_name,
        vector_width,
        pivot,
        frame_count,
        owner_pivots,
        0,
        key_count - 1,
        None,
        scale,
        bits,
        0,
        keys,
        blocks,
    )
    return _result(channel, source)


def decode_motion_animation_channel(
    payload: bytes,
    *,
    animation_frame_count: int,
    pivot_count: int,
) -> W3DAnimationChannelDecode:
    """Decode 0x284 time-coded or 4/8-bit adaptive motion channels."""

    source = _payload_bytes(payload)
    frame_count, owner_pivots = _owners(animation_frame_count, pivot_count)
    kind = "motion animation channel"
    _require_header(source, 8, kind)
    reserved, delta_type, vector_width, channel_type, key_count, pivot = (
        struct.unpack_from("<4Bhh", source)
    )
    if reserved != 0:
        raise W3DAnimationStreamDecodeError(
            f"{kind} reserved header byte must be zero, got {reserved}"
        )
    channel_type_name, width = _layout(channel_type, vector_width)
    if delta_type == 0:
        _plain_count(
            key_count,
            "motion time-coded key count",
            maximum=0x7FFF,
        )
        return _decode_motion_time_coded(
            source,
            frame_count=frame_count,
            owner_pivots=owner_pivots,
            key_count=key_count,
            pivot=pivot,
            channel_type=channel_type,
            channel_type_name=channel_type_name,
            vector_width=width,
        )
    if delta_type in {1, 2}:
        _plain_count(
            key_count,
            "motion adaptive-delta key count",
            maximum=min(frame_count, 0x7FFF),
        )
        return _decode_motion_adaptive(
            source,
            frame_count=frame_count,
            owner_pivots=owner_pivots,
            key_count=key_count,
            pivot=pivot,
            channel_type=channel_type,
            channel_type_name=channel_type_name,
            vector_width=width,
            delta_type=delta_type,
        )
    raise W3DAnimationStreamUnsupportedError(
        f"motion delta type {delta_type} has no evidence-backed decoder"
    )


def decode_animation_stream(
    chunk_id: int,
    payload: bytes,
    *,
    animation_frame_count: int,
    pivot_count: int,
    compressed_flavor: int | None = None,
) -> W3DAnimationChannelDecode:
    """Dispatch one known animation-channel payload, failing closed."""

    normalized_chunk_id = _chunk_id(chunk_id)
    if normalized_chunk_id == W3D_CHUNK_ANIMATION_CHANNEL:
        if compressed_flavor is not None:
            raise W3DAnimationStreamDecodeError(
                "compressed flavor is only valid for chunk 0x00000282"
            )
        return decode_raw_animation_channel(
            payload,
            animation_frame_count=animation_frame_count,
            pivot_count=pivot_count,
        )
    if normalized_chunk_id == W3D_CHUNK_ANIMATION_BIT_CHANNEL:
        if compressed_flavor is not None:
            raise W3DAnimationStreamDecodeError(
                "compressed flavor is only valid for chunk 0x00000282"
            )
        return decode_raw_animation_bit_channel(
            payload,
            animation_frame_count=animation_frame_count,
            pivot_count=pivot_count,
        )
    if normalized_chunk_id == W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL:
        if compressed_flavor is None:
            raise W3DAnimationStreamDecodeError(
                "chunk 0x00000282 requires its owning compressed flavor"
            )
        return decode_compressed_animation_channel(
            payload,
            animation_frame_count=animation_frame_count,
            pivot_count=pivot_count,
            flavor=compressed_flavor,
        )
    if normalized_chunk_id == W3D_CHUNK_COMPRESSED_BIT_CHANNEL:
        if compressed_flavor is not None:
            raise W3DAnimationStreamDecodeError(
                "compressed flavor is only valid for chunk 0x00000282"
            )
        return decode_compressed_animation_bit_channel(
            payload,
            animation_frame_count=animation_frame_count,
            pivot_count=pivot_count,
        )
    if normalized_chunk_id == W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL:
        if compressed_flavor is not None:
            raise W3DAnimationStreamDecodeError(
                "compressed flavor is only valid for chunk 0x00000282"
            )
        return decode_motion_animation_channel(
            payload,
            animation_frame_count=animation_frame_count,
            pivot_count=pivot_count,
        )
    raise W3DAnimationStreamUnsupportedError(
        f"chunk 0x{normalized_chunk_id:08X} has no evidence-backed animation "
        "channel decoder"
    )


decode_animation_channel = decode_animation_stream
