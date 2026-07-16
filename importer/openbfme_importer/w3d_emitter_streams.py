"""Strict, string-safe attestations for Westwood W3D emitter containers.

The layouts decoded here come from the Westwood-derived W3D contract pinned at
``ecd8302b6cfd0578ab249cb95c8b70636c4609bc``, the official EA Generals / Zero
Hour source pinned at ``0a05454d8574207440a5fb15241b98ad0b435590``, and the
OpenSAGE reader pinned at ``588ac477367a0022adf29f20a084e8873014e6ce``:

https://github.com/mikolalysenko/w3d2ply/blob/ecd8302b6cfd0578ab249cb95c8b70636c4609bc/w3d_file.h
https://github.com/electronicarts/CnC_Generals_Zero_Hour/blob/0a05454d8574207440a5fb15241b98ad0b435590/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/w3d_file.h
https://github.com/electronicarts/CnC_Generals_Zero_Hour/blob/0a05454d8574207440a5fb15241b98ad0b435590/GeneralsMD/Code/Libraries/Source/WWVegas/WW3D2/part_ldr.cpp
https://github.com/OpenSAGE/OpenSAGE/tree/588ac477367a0022adf29f20a084e8873014e6ce/src/OpenSage.FileFormats.W3d

The start-key plus ``KeyframeCount`` convention and randomizer class IDs are
also implemented by the pinned public Thyme loader:

https://github.com/TheAssemblyArmada/Thyme/blob/ccef1e11c1355c6db577a057c06e7d790f1a0333/src/w3d/renderer/part_ldr.cpp

The official EA source closes chunk ``0x50D``: its exact record is one float32
``FutureStartTime`` in seconds followed by nine uint32 padding words.  EA reads
exactly that structure, zero-initializes it when saving, and converts the time
to an unsigned millisecond count in the particle buffer.  This decoder requires
the padding to remain zero, so a later format extension fails closed instead of
being silently discarded.  OpenSAGE's opaque reader and Thyme's ``unk1`` label
are retained as reasons for that conservative padding rule, not as competing
field semantics.

Authored emitter names, texture references, and user strings never leave this
module.  Attestations contain their byte lengths, terminator structure, and
SHA-256 digests only.  Numeric records are immutable and payload provenance is
retained, but successful decoding proves neither particle rendering nor visual
parity in Godot.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import math
import struct
from typing import Iterable, TypeAlias


EMITTER_STREAM_SCHEMA = "openbfme.w3d-emitter-stream"
EMITTER_STREAM_SCHEMA_VERSION = 0
MAX_EMITTER_SOURCE_BYTES = 512 * 1024 * 1024
MAX_EMITTER_CHILDREN = 64
MAX_EMITTER_KEYFRAMES = 1_000_000

W3D_CHUNK_EMITTER = 0x00000500
W3D_CHUNK_EMITTER_HEADER = 0x00000501
W3D_CHUNK_EMITTER_USER_DATA = 0x00000502
W3D_CHUNK_EMITTER_INFO = 0x00000503
W3D_CHUNK_EMITTER_INFO_V2 = 0x00000504
W3D_CHUNK_EMITTER_PROPERTIES = 0x00000505
W3D_CHUNK_EMITTER_LINE_PROPERTIES = 0x00000509
W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES = 0x0000050A
W3D_CHUNK_EMITTER_FRAME_KEYFRAMES = 0x0000050B
W3D_CHUNK_EMITTER_BLUR_TIME_KEYFRAMES = 0x0000050C
W3D_CHUNK_EMITTER_EXTRA_INFO = 0x0000050D

SUPPORTED_EMITTER_CHILD_CHUNK_IDS = frozenset(
    {
        W3D_CHUNK_EMITTER_HEADER,
        W3D_CHUNK_EMITTER_USER_DATA,
        W3D_CHUNK_EMITTER_INFO,
        W3D_CHUNK_EMITTER_INFO_V2,
        W3D_CHUNK_EMITTER_PROPERTIES,
        W3D_CHUNK_EMITTER_LINE_PROPERTIES,
        W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES,
        W3D_CHUNK_EMITTER_FRAME_KEYFRAMES,
        W3D_CHUNK_EMITTER_BLUR_TIME_KEYFRAMES,
        W3D_CHUNK_EMITTER_EXTRA_INFO,
    }
)
UNSUPPORTED_EMITTER_CHILD_CHUNK_IDS: frozenset[int] = frozenset()

_CHUNK_NAMES = {
    W3D_CHUNK_EMITTER_HEADER: "emitter-header",
    W3D_CHUNK_EMITTER_USER_DATA: "emitter-user-data",
    W3D_CHUNK_EMITTER_INFO: "emitter-info",
    W3D_CHUNK_EMITTER_INFO_V2: "emitter-info-v2",
    W3D_CHUNK_EMITTER_PROPERTIES: "emitter-properties",
    W3D_CHUNK_EMITTER_LINE_PROPERTIES: "emitter-line-properties",
    W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES: "emitter-rotation-keyframes",
    W3D_CHUNK_EMITTER_FRAME_KEYFRAMES: "emitter-frame-keyframes",
    W3D_CHUNK_EMITTER_BLUR_TIME_KEYFRAMES: "emitter-blur-time-keyframes",
    W3D_CHUNK_EMITTER_EXTRA_INFO: "emitter-extra-info",
}
_MANDATORY_PREFIX = (
    W3D_CHUNK_EMITTER_HEADER,
    W3D_CHUNK_EMITTER_USER_DATA,
    W3D_CHUNK_EMITTER_INFO,
    W3D_CHUNK_EMITTER_INFO_V2,
    W3D_CHUNK_EMITTER_PROPERTIES,
)
_KNOWN_CHILD_IDS = SUPPORTED_EMITTER_CHILD_CHUNK_IDS | (
    UNSUPPORTED_EMITTER_CHILD_CHUNK_IDS
)
_SIZE_MASK = 0x7FFFFFFF
_SUBCHUNK_FLAG = 0x80000000
_CURRENT_EMITTER_VERSION = 0x00020000
_MAX_FUTURE_START_TIME_SECONDS = 0xFFFFFFFF / 1000.0

Vector3: TypeAlias = tuple[float, float, float]
Rgba: TypeAlias = tuple[int, int, int, int]
BoundValue: TypeAlias = int | float


class W3DEmitterDecodeError(ValueError):
    """Raised when an emitter payload violates an evidence-backed contract."""


class W3DEmitterUnsupportedError(W3DEmitterDecodeError):
    """Raised for an emitter chunk without a proven decoder."""


@dataclass(frozen=True, slots=True)
class W3DHashedStringEvidence:
    """Structure and hashes for an authored byte string, never its content."""

    field_byte_length: int
    content_byte_length: int
    terminator_offset: int | None
    trailing_byte_count: int
    trailing_nonzero_byte_count: int
    content_sha256: str
    field_sha256: str

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "fieldByteLength": self.field_byte_length,
            "contentByteLength": self.content_byte_length,
            "trailingByteCount": self.trailing_byte_count,
            "trailingNonzeroByteCount": self.trailing_nonzero_byte_count,
            "contentSha256": self.content_sha256,
            "fieldSha256": self.field_sha256,
        }
        if self.terminator_offset is not None:
            value["terminatorOffset"] = self.terminator_offset
        return value

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DEmitterBound:
    """An immutable inclusive range used by a decoded-child attestation."""

    name: str
    minimum: BoundValue | None
    maximum: BoundValue | None

    def neutral(self) -> dict[str, object]:
        return {"minimum": self.minimum, "maximum": self.maximum}

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DEmitterHeaderRecord:
    version: int
    name: W3DHashedStringEvidence


@dataclass(frozen=True, slots=True)
class W3DEmitterUserDataRecord:
    user_type: int
    declared_string_byte_length: int
    struct_padding_sha256: str
    struct_padding_nonzero_byte_count: int
    value: W3DHashedStringEvidence


@dataclass(frozen=True, slots=True)
class W3DEmitterInfoRecord:
    texture_reference: W3DHashedStringEvidence
    start_size: float
    end_size: float
    lifetime: float
    emission_rate: float
    max_emissions: float
    velocity_random: float
    position_random: float
    fade_time: float
    gravity: float
    elasticity: float
    velocity: Vector3
    acceleration: Vector3
    start_color: Rgba
    end_color: Rgba


@dataclass(frozen=True, slots=True)
class W3DVolumeRandomizerRecord:
    class_id: int
    values: Vector3
    reserved: tuple[int, int, int, int]


@dataclass(frozen=True, slots=True)
class W3DShaderRecord:
    fields: tuple[
        int,
        int,
        int,
        int,
        int,
        int,
        int,
        int,
        int,
        int,
        int,
        int,
        int,
        int,
        int,
        int,
    ]


@dataclass(frozen=True, slots=True)
class W3DEmitterInfoV2Record:
    burst_size: int
    creation_volume: W3DVolumeRandomizerRecord
    velocity_randomizer: W3DVolumeRandomizerRecord
    outward_velocity: float
    velocity_inherit: float
    shader: W3DShaderRecord
    render_mode: int
    frame_mode: int
    reserved: tuple[int, int, int, int, int, int]


@dataclass(frozen=True, slots=True)
class W3DEmitterColorKeyframe:
    time: float
    color: Rgba


@dataclass(frozen=True, slots=True)
class W3DEmitterScalarKeyframe:
    time: float
    value: float


@dataclass(frozen=True, slots=True)
class W3DEmitterPropertiesRecord:
    color_keyframe_count: int
    opacity_keyframe_count: int
    size_keyframe_count: int
    color_random: Rgba
    opacity_random: float
    size_random: float
    reserved: tuple[int, int, int, int]
    color_keyframes: tuple[W3DEmitterColorKeyframe, ...]
    opacity_keyframes: tuple[W3DEmitterScalarKeyframe, ...]
    size_keyframes: tuple[W3DEmitterScalarKeyframe, ...]


@dataclass(frozen=True, slots=True)
class W3DEmitterLinePropertiesRecord:
    flags: int
    subdivision_level: int
    noise_amplitude: float
    merge_abort_factor: float
    texture_tile_factor: float
    u_per_second: float
    v_per_second: float
    reserved: tuple[int, int, int, int, int, int, int, int, int]

    @property
    def texture_map_mode(self) -> int:
        return (self.flags >> 24) & 0xFF


@dataclass(frozen=True, slots=True)
class W3DEmitterKeyframeSetRecord:
    declared_keyframe_count: int
    random: float
    orientation_random: float | None
    reserved: tuple[int, ...]
    keyframes: tuple[W3DEmitterScalarKeyframe, ...]


@dataclass(frozen=True, slots=True)
class W3DEmitterExtraInfoRecord:
    """Official EA emitter delay record, expressed in source-file units."""

    future_start_time_seconds: float
    padding: tuple[int, int, int, int, int, int, int, int, int]


EmitterRecord: TypeAlias = (
    W3DEmitterHeaderRecord
    | W3DEmitterUserDataRecord
    | W3DEmitterInfoRecord
    | W3DEmitterInfoV2Record
    | W3DEmitterPropertiesRecord
    | W3DEmitterLinePropertiesRecord
    | W3DEmitterKeyframeSetRecord
    | W3DEmitterExtraInfoRecord
)


@dataclass(frozen=True, slots=True)
class W3DEmitterChildAttestation:
    """Payload-free evidence for one decoded or explicitly terminal child."""

    chunk_id: int
    chunk_name: str
    status: str
    record_layout: str
    record_count: int
    declared_keyframe_count: int | None
    payload_byte_length: int
    payload_sha256: str
    canonical_record_sha256: str | None
    bounds: tuple[W3DEmitterBound, ...]
    string_evidence: tuple[W3DHashedStringEvidence, ...]
    unsupported_reason: str | None
    _record: EmitterRecord | None = field(repr=False, compare=False)

    def record(self) -> EmitterRecord | None:
        """Return immutable decoded numbers; authored strings remain hashed."""

        return self._record

    @property
    def decoded(self) -> bool:
        return self.status == "decoded"

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "schema": EMITTER_STREAM_SCHEMA,
            "schemaVersion": EMITTER_STREAM_SCHEMA_VERSION,
            "chunkId": self.chunk_id,
            "chunkIdHex": f"0x{self.chunk_id:08X}",
            "chunkName": self.chunk_name,
            "status": self.status,
            "recordLayout": self.record_layout,
            "recordCount": self.record_count,
            "payloadByteLength": self.payload_byte_length,
            "payloadSha256": self.payload_sha256,
            "bounds": {item.name: item.neutral() for item in self.bounds},
        }
        if self.declared_keyframe_count is not None:
            value["declaredKeyframeCount"] = self.declared_keyframe_count
        if self.canonical_record_sha256 is not None:
            value["canonicalRecordSha256"] = self.canonical_record_sha256
        if self.string_evidence:
            value["stringEvidence"] = [
                item.neutral() for item in self.string_evidence
            ]
        if self.unsupported_reason is not None:
            value["unsupportedReason"] = self.unsupported_reason
        return value

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DEmitterContainerAttestation:
    """One structurally partitioned emitter root and all child outcomes."""

    payload_byte_length: int
    payload_sha256: str
    child_layout_sha256: str
    children: tuple[W3DEmitterChildAttestation, ...]

    @property
    def decoded_child_count(self) -> int:
        return sum(item.decoded for item in self.children)

    @property
    def unsupported_terminal_count(self) -> int:
        return len(self.children) - self.decoded_child_count

    @property
    def conversion_ready(self) -> bool:
        return self.unsupported_terminal_count == 0

    def neutral(self) -> dict[str, object]:
        return {
            "schema": EMITTER_STREAM_SCHEMA,
            "schemaVersion": EMITTER_STREAM_SCHEMA_VERSION,
            "kind": "emitter-container",
            "payloadByteLength": self.payload_byte_length,
            "payloadSha256": self.payload_sha256,
            "childLayoutSha256": self.child_layout_sha256,
            "childCount": len(self.children),
            "decodedChildCount": self.decoded_child_count,
            "unsupportedTerminalCount": self.unsupported_terminal_count,
            "conversionReady": self.conversion_ready,
            "children": [item.neutral() for item in self.children],
        }

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DEmitterFileAttestation:
    """Source provenance plus every emitter root in one complete W3D file."""

    source_byte_length: int
    source_sha256: str
    top_level_chunk_count: int
    emitters: tuple[W3DEmitterContainerAttestation, ...]

    def neutral(self) -> dict[str, object]:
        return {
            "schema": EMITTER_STREAM_SCHEMA,
            "schemaVersion": EMITTER_STREAM_SCHEMA_VERSION,
            "kind": "emitter-file",
            "sourceByteLength": self.source_byte_length,
            "sourceSha256": self.source_sha256,
            "topLevelChunkCount": self.top_level_chunk_count,
            "emitterCount": len(self.emitters),
            "emitters": [item.neutral() for item in self.emitters],
        }

    json_ready = neutral


def _source_bytes(value: object, *, label: str) -> bytes:
    if not isinstance(value, bytes):
        raise TypeError(f"W3D {label} must be bytes")
    if len(value) > MAX_EMITTER_SOURCE_BYTES:
        raise W3DEmitterDecodeError(
            f"{label} exceeds {MAX_EMITTER_SOURCE_BYTES} byte limit"
        )
    return value


def _chunk_id(value: object) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < 0
        or value > 0xFFFFFFFF
    ):
        raise W3DEmitterDecodeError(
            "chunk ID must be an unsigned 32-bit integer"
        )
    return value


def _exact_size(payload: bytes, expected: int, kind: str) -> None:
    if len(payload) != expected:
        raise W3DEmitterDecodeError(
            f"{kind} payload must be exactly {expected} bytes; got {len(payload)}"
        )


def _finite(values: Iterable[float], kind: str) -> None:
    if not all(math.isfinite(value) for value in values):
        raise W3DEmitterDecodeError(f"{kind} contains a non-finite float")


def _range(values: Iterable[BoundValue]) -> tuple[BoundValue | None, BoundValue | None]:
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


def _bounds(
    columns: Iterable[tuple[str, Iterable[BoundValue]]],
) -> tuple[W3DEmitterBound, ...]:
    result = []
    for name, values in columns:
        minimum, maximum = _range(values)
        result.append(W3DEmitterBound(name, minimum, maximum))
    return tuple(result)


def _fixed_string_evidence(raw: bytes, kind: str) -> W3DHashedStringEvidence:
    terminator = raw.find(b"\0")
    if terminator < 0:
        raise W3DEmitterDecodeError(f"{kind} has no NUL terminator")
    content = raw[:terminator]
    trailing = raw[terminator + 1 :]
    return W3DHashedStringEvidence(
        field_byte_length=len(raw),
        content_byte_length=len(content),
        terminator_offset=terminator,
        trailing_byte_count=len(trailing),
        trailing_nonzero_byte_count=sum(value != 0 for value in trailing),
        content_sha256=hashlib.sha256(content).hexdigest(),
        field_sha256=hashlib.sha256(raw).hexdigest(),
    )


def _variable_string_evidence(raw: bytes, kind: str) -> W3DHashedStringEvidence:
    if not raw:
        content = b""
        terminator = None
        trailing = b""
    else:
        if raw[-1] != 0:
            raise W3DEmitterDecodeError(
                f"{kind} declared string has no final NUL terminator"
            )
        content = raw[:-1]
        terminator = len(content)
        trailing = b""
    return W3DHashedStringEvidence(
        field_byte_length=len(raw),
        content_byte_length=len(content),
        terminator_offset=terminator,
        trailing_byte_count=len(trailing),
        trailing_nonzero_byte_count=0,
        content_sha256=hashlib.sha256(content).hexdigest(),
        field_sha256=hashlib.sha256(raw).hexdigest(),
    )


def _decoded_attestation(
    *,
    chunk_id: int,
    payload: bytes,
    layout: str,
    record_count: int,
    record: EmitterRecord,
    bounds: tuple[W3DEmitterBound, ...],
    string_evidence: tuple[W3DHashedStringEvidence, ...] = (),
    declared_keyframe_count: int | None = None,
) -> W3DEmitterChildAttestation:
    payload_sha256 = hashlib.sha256(payload).hexdigest()
    return W3DEmitterChildAttestation(
        chunk_id=chunk_id,
        chunk_name=_CHUNK_NAMES[chunk_id],
        status="decoded",
        record_layout=layout,
        record_count=record_count,
        declared_keyframe_count=declared_keyframe_count,
        payload_byte_length=len(payload),
        payload_sha256=payload_sha256,
        canonical_record_sha256=payload_sha256,
        bounds=bounds,
        string_evidence=string_evidence,
        unsupported_reason=None,
        _record=record,
    )


def _decode_header(payload: bytes) -> W3DEmitterChildAttestation:
    kind = _CHUNK_NAMES[W3D_CHUNK_EMITTER_HEADER]
    _exact_size(payload, 20, kind)
    version = struct.unpack_from("<I", payload)[0]
    if version != _CURRENT_EMITTER_VERSION:
        raise W3DEmitterDecodeError(
            f"{kind} version 0x{version:08X} is unsupported"
        )
    name = _fixed_string_evidence(payload[4:20], "emitter name")
    record = W3DEmitterHeaderRecord(version, name)
    return _decoded_attestation(
        chunk_id=W3D_CHUNK_EMITTER_HEADER,
        payload=payload,
        layout="<I16s",
        record_count=1,
        record=record,
        bounds=_bounds((("version", (version,)),)),
        string_evidence=(name,),
    )


def _decode_user_data(payload: bytes) -> W3DEmitterChildAttestation:
    kind = _CHUNK_NAMES[W3D_CHUNK_EMITTER_USER_DATA]
    if len(payload) < 12:
        raise W3DEmitterDecodeError(
            f"{kind} payload must contain its 12-byte fixed record"
        )
    user_type, string_size = struct.unpack_from("<II", payload)
    if user_type != 0:
        raise W3DEmitterDecodeError(
            f"{kind} type {user_type} is outside the source-proven enum"
        )
    if string_size > MAX_EMITTER_SOURCE_BYTES - 12:
        raise W3DEmitterDecodeError(f"{kind} declared string is too large")
    expected = 12 + string_size
    _exact_size(payload, expected, kind)
    struct_padding = payload[8:12]
    string_raw = payload[12:]
    string_evidence = _variable_string_evidence(string_raw, "emitter user data")
    record = W3DEmitterUserDataRecord(
        user_type=user_type,
        declared_string_byte_length=string_size,
        struct_padding_sha256=hashlib.sha256(struct_padding).hexdigest(),
        struct_padding_nonzero_byte_count=sum(
            value != 0 for value in struct_padding
        ),
        value=string_evidence,
    )
    return _decoded_attestation(
        chunk_id=W3D_CHUNK_EMITTER_USER_DATA,
        payload=payload,
        layout="<II4x + declared byte string",
        record_count=1,
        record=record,
        bounds=_bounds(
            (
                ("userType", (user_type,)),
                ("declaredStringByteLength", (string_size,)),
                (
                    "structPaddingNonzeroByteCount",
                    (record.struct_padding_nonzero_byte_count,),
                ),
            )
        ),
        string_evidence=(string_evidence,),
    )


def _decode_info(payload: bytes) -> W3DEmitterChildAttestation:
    kind = _CHUNK_NAMES[W3D_CHUNK_EMITTER_INFO]
    _exact_size(payload, 332, kind)
    texture = _fixed_string_evidence(payload[:260], "emitter texture reference")
    values = struct.unpack_from("<16f8B", payload, 260)
    floats = values[:16]
    _finite(floats, kind)
    record = W3DEmitterInfoRecord(
        texture_reference=texture,
        start_size=floats[0],
        end_size=floats[1],
        lifetime=floats[2],
        emission_rate=floats[3],
        max_emissions=floats[4],
        velocity_random=floats[5],
        position_random=floats[6],
        fade_time=floats[7],
        gravity=floats[8],
        elasticity=floats[9],
        velocity=(floats[10], floats[11], floats[12]),
        acceleration=(floats[13], floats[14], floats[15]),
        start_color=(values[16], values[17], values[18], values[19]),
        end_color=(values[20], values[21], values[22], values[23]),
    )
    scalar_names = (
        "startSize",
        "endSize",
        "lifetime",
        "emissionRate",
        "maxEmissions",
        "velocityRandom",
        "positionRandom",
        "fadeTime",
        "gravity",
        "elasticity",
    )
    return _decoded_attestation(
        chunk_id=W3D_CHUNK_EMITTER_INFO,
        payload=payload,
        layout="<260s16f8B",
        record_count=1,
        record=record,
        bounds=_bounds(
            (
                *((name, (value,)) for name, value in zip(scalar_names, floats[:10])),
                ("velocity", floats[10:13]),
                ("acceleration", floats[13:16]),
                ("colorChannel", values[16:24]),
            )
        ),
        string_evidence=(texture,),
    )


def _decode_randomizer(payload: bytes, offset: int, kind: str) -> W3DVolumeRandomizerRecord:
    values = struct.unpack_from("<I3f4I", payload, offset)
    class_id = values[0]
    if class_id > 3:
        raise W3DEmitterDecodeError(
            f"{kind} randomizer class {class_id} is outside 0..3"
        )
    numeric: Vector3 = (values[1], values[2], values[3])
    _finite(numeric, kind)
    return W3DVolumeRandomizerRecord(
        class_id=class_id,
        values=numeric,
        reserved=(values[4], values[5], values[6], values[7]),
    )


def _decode_shader(raw: bytes) -> W3DShaderRecord:
    _exact_size(raw, 16, "emitter shader")
    fields = tuple(raw)
    enum_values = {
        0: set(range(8)),
        1: {0, 1},
        3: set(range(7)),
        5: {0, 1, 2, 3, 5},
        6: {0, 1},
        7: set(range(4)),
        8: {0, 1},
        9: set(range(13)),
        10: set(range(4)),
        12: {0, 1},
        13: set(range(13)),
        14: set(range(4)),
    }
    for index, allowed in enum_values.items():
        if fields[index] not in allowed:
            raise W3DEmitterDecodeError(
                f"emitter shader field {index} value {fields[index]} is unsupported"
            )
    if fields[15] != 0:
        raise W3DEmitterDecodeError("emitter shader padding byte must be zero")
    return W3DShaderRecord(fields)  # type: ignore[arg-type]


def _decode_info_v2(payload: bytes) -> W3DEmitterChildAttestation:
    kind = _CHUNK_NAMES[W3D_CHUNK_EMITTER_INFO_V2]
    _exact_size(payload, 124, kind)
    burst_size = struct.unpack_from("<I", payload)[0]
    creation = _decode_randomizer(payload, 4, "creation-volume")
    velocity = _decode_randomizer(payload, 36, "velocity")
    outward_velocity, velocity_inherit = struct.unpack_from("<2f", payload, 68)
    _finite((outward_velocity, velocity_inherit), kind)
    shader = _decode_shader(payload[76:92])
    render_mode, frame_mode = struct.unpack_from("<2I", payload, 92)
    if render_mode > 4:
        raise W3DEmitterDecodeError(
            f"{kind} render mode {render_mode} is outside 0..4"
        )
    if frame_mode > 4:
        raise W3DEmitterDecodeError(
            f"{kind} frame mode {frame_mode} is outside 0..4"
        )
    reserved = struct.unpack_from("<6I", payload, 100)
    record = W3DEmitterInfoV2Record(
        burst_size=burst_size,
        creation_volume=creation,
        velocity_randomizer=velocity,
        outward_velocity=outward_velocity,
        velocity_inherit=velocity_inherit,
        shader=shader,
        render_mode=render_mode,
        frame_mode=frame_mode,
        reserved=reserved,
    )
    return _decoded_attestation(
        chunk_id=W3D_CHUNK_EMITTER_INFO_V2,
        payload=payload,
        layout="<I 2*(I3f4I) 2f 16B 2I 6I",
        record_count=1,
        record=record,
        bounds=_bounds(
            (
                ("burstSize", (burst_size,)),
                ("randomizerClassId", (creation.class_id, velocity.class_id)),
                ("randomizerValue", (*creation.values, *velocity.values)),
                ("outwardVelocity", (outward_velocity,)),
                ("velocityInherit", (velocity_inherit,)),
                ("shaderField", shader.fields),
                ("renderMode", (render_mode,)),
                ("frameMode", (frame_mode,)),
                ("reserved", (*creation.reserved, *velocity.reserved, *reserved)),
            )
        ),
    )


def _validate_key_times(
    keyframes: tuple[W3DEmitterColorKeyframe | W3DEmitterScalarKeyframe, ...],
    kind: str,
) -> None:
    if not keyframes:
        raise W3DEmitterDecodeError(f"{kind} must contain its start key")
    times = tuple(item.time for item in keyframes)
    _finite(times, kind)
    if times[0] != 0.0:
        raise W3DEmitterDecodeError(f"{kind} start key time must be zero")
    if any(current <= previous for previous, current in zip(times, times[1:])):
        raise W3DEmitterDecodeError(
            f"{kind} key times after the start key must be strictly increasing"
        )


def _decode_properties(payload: bytes) -> W3DEmitterChildAttestation:
    kind = _CHUNK_NAMES[W3D_CHUNK_EMITTER_PROPERTIES]
    if len(payload) < 40:
        raise W3DEmitterDecodeError(
            f"{kind} payload must contain its 40-byte fixed record"
        )
    color_count, opacity_count, size_count = struct.unpack_from("<3I", payload)
    counts = (color_count, opacity_count, size_count)
    if any(count < 1 or count > MAX_EMITTER_KEYFRAMES for count in counts):
        raise W3DEmitterDecodeError(
            f"{kind} key counts must be within 1..{MAX_EMITTER_KEYFRAMES}"
        )
    expected = 40 + 8 * sum(counts)
    _exact_size(payload, expected, kind)
    color_random: Rgba = tuple(payload[12:16])  # type: ignore[assignment]
    opacity_random, size_random = struct.unpack_from("<2f", payload, 16)
    _finite((opacity_random, size_random), kind)
    reserved = struct.unpack_from("<4I", payload, 24)
    offset = 40
    colors = []
    for _ in range(color_count):
        time = struct.unpack_from("<f", payload, offset)[0]
        color: Rgba = tuple(payload[offset + 4 : offset + 8])  # type: ignore[assignment]
        colors.append(W3DEmitterColorKeyframe(time, color))
        offset += 8
    opacities = []
    for _ in range(opacity_count):
        time, value = struct.unpack_from("<2f", payload, offset)
        opacities.append(W3DEmitterScalarKeyframe(time, value))
        offset += 8
    sizes = []
    for _ in range(size_count):
        time, value = struct.unpack_from("<2f", payload, offset)
        sizes.append(W3DEmitterScalarKeyframe(time, value))
        offset += 8
    color_keys = tuple(colors)
    opacity_keys = tuple(opacities)
    size_keys = tuple(sizes)
    _validate_key_times(color_keys, "emitter color keys")
    _validate_key_times(opacity_keys, "emitter opacity keys")
    _validate_key_times(size_keys, "emitter size keys")
    opacity_values = tuple(item.value for item in opacity_keys)
    size_values = tuple(item.value for item in size_keys)
    _finite((*opacity_values, *size_values), kind)
    record = W3DEmitterPropertiesRecord(
        color_keyframe_count=color_count,
        opacity_keyframe_count=opacity_count,
        size_keyframe_count=size_count,
        color_random=color_random,
        opacity_random=opacity_random,
        size_random=size_random,
        reserved=reserved,
        color_keyframes=color_keys,
        opacity_keyframes=opacity_keys,
        size_keyframes=size_keys,
    )
    all_times = tuple(
        item.time for item in (*color_keys, *opacity_keys, *size_keys)
    )
    return _decoded_attestation(
        chunk_id=W3D_CHUNK_EMITTER_PROPERTIES,
        payload=payload,
        layout="<3I4B2f4I + color/opacity/size key arrays",
        record_count=sum(counts),
        declared_keyframe_count=sum(counts),
        record=record,
        bounds=_bounds(
            (
                ("keyframeCount", counts),
                ("keyTime", all_times),
                ("colorChannel", (*color_random, *(c for key in color_keys for c in key.color))),
                ("opacity", (*opacity_values, opacity_random)),
                ("size", (*size_values, size_random)),
                ("reserved", reserved),
            )
        ),
    )


def _decode_line_properties(payload: bytes) -> W3DEmitterChildAttestation:
    kind = _CHUNK_NAMES[W3D_CHUNK_EMITTER_LINE_PROPERTIES]
    _exact_size(payload, 64, kind)
    flags, subdivision = struct.unpack_from("<2I", payload)
    if flags & 0x00FFFFF0:
        raise W3DEmitterDecodeError(f"{kind} contains unknown flag bits")
    texture_map_mode = (flags >> 24) & 0xFF
    if texture_map_mode > 2:
        raise W3DEmitterDecodeError(
            f"{kind} texture-map mode {texture_map_mode} is outside 0..2"
        )
    values = struct.unpack_from("<5f", payload, 8)
    _finite(values, kind)
    reserved = struct.unpack_from("<9I", payload, 28)
    record = W3DEmitterLinePropertiesRecord(
        flags=flags,
        subdivision_level=subdivision,
        noise_amplitude=values[0],
        merge_abort_factor=values[1],
        texture_tile_factor=values[2],
        u_per_second=values[3],
        v_per_second=values[4],
        reserved=reserved,
    )
    return _decoded_attestation(
        chunk_id=W3D_CHUNK_EMITTER_LINE_PROPERTIES,
        payload=payload,
        layout="<2I5f9I",
        record_count=1,
        record=record,
        bounds=_bounds(
            (
                ("flags", (flags,)),
                ("textureMapMode", (texture_map_mode,)),
                ("subdivisionLevel", (subdivision,)),
                ("lineScalar", values),
                ("reserved", reserved),
            )
        ),
    )


def _decode_scalar_keyframes(
    chunk_id: int,
    payload: bytes,
) -> W3DEmitterChildAttestation:
    kind = _CHUNK_NAMES[chunk_id]
    if chunk_id == W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES:
        header_size = 16
        if len(payload) < header_size:
            raise W3DEmitterDecodeError(f"{kind} header is truncated")
        count, random, orientation, reserved0 = struct.unpack_from(
            "<I2fI", payload
        )
        reserved = (reserved0,)
    elif chunk_id == W3D_CHUNK_EMITTER_FRAME_KEYFRAMES:
        header_size = 16
        if len(payload) < header_size:
            raise W3DEmitterDecodeError(f"{kind} header is truncated")
        count, random, reserved0, reserved1 = struct.unpack_from(
            "<If2I", payload
        )
        orientation = None
        reserved = (reserved0, reserved1)
    else:
        header_size = 12
        if len(payload) < header_size:
            raise W3DEmitterDecodeError(f"{kind} header is truncated")
        count, random, reserved0 = struct.unpack_from("<IfI", payload)
        orientation = None
        reserved = (reserved0,)
    if count > MAX_EMITTER_KEYFRAMES:
        raise W3DEmitterDecodeError(
            f"{kind} key count exceeds {MAX_EMITTER_KEYFRAMES}"
        )
    expected = header_size + (count + 1) * 8
    _exact_size(payload, expected, kind)
    finite_header = (random,) if orientation is None else (random, orientation)
    _finite(finite_header, kind)
    keys = tuple(
        W3DEmitterScalarKeyframe(*values)
        for values in struct.iter_unpack("<2f", payload[header_size:])
    )
    _validate_key_times(keys, kind)
    values = tuple(item.value for item in keys)
    _finite(values, kind)
    record = W3DEmitterKeyframeSetRecord(
        declared_keyframe_count=count,
        random=random,
        orientation_random=orientation,
        reserved=reserved,
        keyframes=keys,
    )
    return _decoded_attestation(
        chunk_id=chunk_id,
        payload=payload,
        layout={
            W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES: "<I2fI + (count+1)*2f",
            W3D_CHUNK_EMITTER_FRAME_KEYFRAMES: "<If2I + (count+1)*2f",
            W3D_CHUNK_EMITTER_BLUR_TIME_KEYFRAMES: "<IfI + (count+1)*2f",
        }[chunk_id],
        record_count=len(keys),
        declared_keyframe_count=count,
        record=record,
        bounds=_bounds(
            (
                ("declaredKeyframeCount", (count,)),
                ("keyTime", (item.time for item in keys)),
                ("keyValue", values),
                ("random", finite_header),
                ("reserved", reserved),
            )
        ),
    )


def _decode_extra_info(payload: bytes) -> W3DEmitterChildAttestation:
    kind = _CHUNK_NAMES[W3D_CHUNK_EMITTER_EXTRA_INFO]
    _exact_size(payload, 40, kind)
    values = struct.unpack("<f9I", payload)
    future_start_time = values[0]
    padding = values[1:]
    _finite((future_start_time,), kind)
    if future_start_time < 0.0:
        raise W3DEmitterDecodeError(
            f"{kind} future start time cannot be negative"
        )
    if future_start_time > _MAX_FUTURE_START_TIME_SECONDS:
        raise W3DEmitterDecodeError(
            f"{kind} future start time exceeds the source runtime's "
            "unsigned millisecond range"
        )
    if any(padding):
        raise W3DEmitterDecodeError(
            f"{kind} padding must be zero; nonzero data may be a format extension"
        )
    record = W3DEmitterExtraInfoRecord(
        future_start_time_seconds=future_start_time,
        padding=padding,
    )
    return _decoded_attestation(
        chunk_id=W3D_CHUNK_EMITTER_EXTRA_INFO,
        payload=payload,
        layout="<f9I",
        record_count=1,
        record=record,
        bounds=_bounds(
            (
                ("futureStartTimeSeconds", (future_start_time,)),
                ("padding", padding),
            )
        ),
    )


def decode_emitter_child(
    chunk_id: int,
    payload: bytes,
) -> W3DEmitterChildAttestation:
    """Decode one source-proven emitter child, failing closed otherwise."""

    normalized_id = _chunk_id(chunk_id)
    source = _source_bytes(payload, label="emitter child payload")
    decoders = {
        W3D_CHUNK_EMITTER_HEADER: _decode_header,
        W3D_CHUNK_EMITTER_USER_DATA: _decode_user_data,
        W3D_CHUNK_EMITTER_INFO: _decode_info,
        W3D_CHUNK_EMITTER_INFO_V2: _decode_info_v2,
        W3D_CHUNK_EMITTER_PROPERTIES: _decode_properties,
        W3D_CHUNK_EMITTER_LINE_PROPERTIES: _decode_line_properties,
        W3D_CHUNK_EMITTER_EXTRA_INFO: _decode_extra_info,
    }
    if normalized_id in {
        W3D_CHUNK_EMITTER_ROTATION_KEYFRAMES,
        W3D_CHUNK_EMITTER_FRAME_KEYFRAMES,
        W3D_CHUNK_EMITTER_BLUR_TIME_KEYFRAMES,
    }:
        return _decode_scalar_keyframes(normalized_id, source)
    try:
        decoder = decoders[normalized_id]
    except KeyError as error:
        raise W3DEmitterUnsupportedError(
            f"chunk 0x{normalized_id:08X} is not a proven emitter child"
        ) from error
    return decoder(source)


def _cross_validate_children(
    children: tuple[W3DEmitterChildAttestation, ...],
) -> None:
    by_id = {item.chunk_id: item for item in children}
    info_v2 = by_id[W3D_CHUNK_EMITTER_INFO_V2].record()
    if not isinstance(info_v2, W3DEmitterInfoV2Record):
        raise W3DEmitterDecodeError("emitter info-v2 record is unavailable")
    frame_child = by_id.get(W3D_CHUNK_EMITTER_FRAME_KEYFRAMES)
    if frame_child is None:
        return
    frame_record = frame_child.record()
    if not isinstance(frame_record, W3DEmitterKeyframeSetRecord):
        raise W3DEmitterDecodeError("emitter frame-key record is unavailable")
    frame_cells = (1, 4, 16, 64, 256)[info_v2.frame_mode]
    for key in frame_record.keyframes:
        if key.value >= frame_cells:
            raise W3DEmitterDecodeError(
                "emitter frame key is outside the info-v2 texture grid"
            )


def decode_emitter_container(payload: bytes) -> W3DEmitterContainerAttestation:
    """Decode one ``0x500`` payload and validate child containment/order."""

    source = _source_bytes(payload, label="emitter container payload")
    children = []
    child_layout = hashlib.sha256()
    seen: set[int] = set()
    position = 0
    while position < len(source):
        if len(children) >= MAX_EMITTER_CHILDREN:
            raise W3DEmitterDecodeError(
                f"emitter child count exceeds {MAX_EMITTER_CHILDREN}"
            )
        remaining = len(source) - position
        if remaining < 8:
            raise W3DEmitterDecodeError(
                "emitter container ends with a truncated child header"
            )
        chunk_id, raw_size = struct.unpack_from("<II", source, position)
        payload_size = raw_size & _SIZE_MASK
        if raw_size & _SUBCHUNK_FLAG:
            raise W3DEmitterDecodeError(
                f"emitter child 0x{chunk_id:08X} cannot contain subchunks"
            )
        child_end = position + 8 + payload_size
        if child_end > len(source):
            raise W3DEmitterDecodeError(
                f"emitter child 0x{chunk_id:08X} exceeds its parent boundary"
            )
        if chunk_id not in _KNOWN_CHILD_IDS:
            raise W3DEmitterUnsupportedError(
                f"chunk 0x{chunk_id:08X} is not a proven emitter child"
            )
        if chunk_id in seen:
            raise W3DEmitterDecodeError(
                f"emitter child 0x{chunk_id:08X} is duplicated"
            )
        child_payload = source[position + 8 : child_end]
        child = decode_emitter_child(chunk_id, child_payload)
        children.append(child)
        seen.add(chunk_id)
        child_layout.update(struct.pack("<II", chunk_id, payload_size))
        child_layout.update(bytes.fromhex(child.payload_sha256))
        position = child_end
    ids = tuple(item.chunk_id for item in children)
    if ids[: len(_MANDATORY_PREFIX)] != _MANDATORY_PREFIX:
        raise W3DEmitterDecodeError(
            "emitter mandatory children must begin with header, user-data, "
            "info, info-v2, and properties"
        )
    typed_children = tuple(children)
    _cross_validate_children(typed_children)
    return W3DEmitterContainerAttestation(
        payload_byte_length=len(source),
        payload_sha256=hashlib.sha256(source).hexdigest(),
        child_layout_sha256=child_layout.hexdigest(),
        children=typed_children,
    )


def decode_emitter_file(source: bytes) -> W3DEmitterFileAttestation:
    """Find and decode every emitter root in one complete W3D byte string."""

    payload = _source_bytes(source, label="emitter file source")
    if not payload:
        raise W3DEmitterDecodeError("emitter file source is empty")
    emitters = []
    top_level_count = 0
    position = 0
    while position < len(payload):
        if len(payload) - position < 8:
            raise W3DEmitterDecodeError(
                "W3D file ends with a truncated top-level chunk header"
            )
        chunk_id, raw_size = struct.unpack_from("<II", payload, position)
        chunk_size = raw_size & _SIZE_MASK
        chunk_end = position + 8 + chunk_size
        if chunk_end > len(payload):
            raise W3DEmitterDecodeError(
                f"top-level chunk 0x{chunk_id:08X} exceeds the source boundary"
            )
        top_level_count += 1
        if chunk_id == W3D_CHUNK_EMITTER:
            if not raw_size & _SUBCHUNK_FLAG:
                raise W3DEmitterDecodeError(
                    "emitter root must carry the W3D subchunk flag"
                )
            emitters.append(
                decode_emitter_container(payload[position + 8 : chunk_end])
            )
        position = chunk_end
    if not emitters:
        raise W3DEmitterDecodeError("W3D file contains no emitter root")
    return W3DEmitterFileAttestation(
        source_byte_length=len(payload),
        source_sha256=hashlib.sha256(payload).hexdigest(),
        top_level_chunk_count=top_level_count,
        emitters=tuple(emitters),
    )
