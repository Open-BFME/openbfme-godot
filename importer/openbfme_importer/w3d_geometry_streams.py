"""Strict, payload-free attestations for owner-bound W3D geometry streams.

This module decodes only layouts established by the repository's pinned
OpenSAGE BlenderPlugin contract.  It accepts chunk *payloads*, not complete W3D
files, and requires the owning mesh cardinalities from a separately validated
mesh header.  Primary and secondary vertex/normal streams remain independent;
no stream is selected, merged, or silently discarded.

Returning an attestation proves only that one payload conforms to its known
record layout and the supplied owner bounds.  It does not prove render
semantics, material correctness, successful conversion, or backtest coverage.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import math
import struct
from typing import Iterable, TypeAlias


GEOMETRY_STREAM_SCHEMA = "openbfme.w3d-geometry-stream"
GEOMETRY_STREAM_SCHEMA_VERSION = 0
MAX_GEOMETRY_STREAM_BYTES = 512 * 1024 * 1024

W3D_CHUNK_VERTICES = 0x00000002
W3D_CHUNK_VERTEX_NORMALS = 0x00000003
W3D_CHUNK_VERTEX_INFLUENCES = 0x0000000E
W3D_CHUNK_TRIANGLES = 0x00000020
W3D_CHUNK_STAGE_TEXCOORDS = 0x0000004A
W3D_CHUNK_VERTICES_2 = 0x00000C00
W3D_CHUNK_NORMALS_2 = 0x00000C01

SUPPORTED_GEOMETRY_STREAM_CHUNK_IDS = frozenset(
    {
        W3D_CHUNK_VERTICES,
        W3D_CHUNK_VERTEX_NORMALS,
        W3D_CHUNK_VERTEX_INFLUENCES,
        W3D_CHUNK_TRIANGLES,
        W3D_CHUNK_STAGE_TEXCOORDS,
        W3D_CHUNK_VERTICES_2,
        W3D_CHUNK_NORMALS_2,
    }
)

Vector2: TypeAlias = tuple[float, float]
Vector3: TypeAlias = tuple[float, float, float]


class W3DGeometryStreamDecodeError(ValueError):
    """Raised when a known geometry payload violates its exact contract."""


class W3DGeometryStreamUnsupportedError(W3DGeometryStreamDecodeError):
    """Raised when no evidence-backed decoder exists for a chunk ID."""


@dataclass(frozen=True, slots=True)
class W3DTriangleRecord:
    """One established 32-byte W3D triangle record."""

    vertex_indices: tuple[int, int, int]
    surface_type: int
    normal: Vector3
    distance: float


@dataclass(frozen=True, slots=True)
class W3DVertexInfluenceRecord:
    """One established 8-byte two-pivot influence record.

    The pinned reader divides each encoded weight by 100.  The raw integer is
    retained so canonical repacking and exact downstream decisions do not
    depend on floating-point formatting.
    """

    primary_pivot_index: int
    secondary_pivot_index: int
    primary_weight_raw: int
    secondary_weight_raw: int

    @property
    def primary_weight(self) -> float:
        return self.primary_weight_raw / 100.0

    @property
    def secondary_weight(self) -> float:
        return self.secondary_weight_raw / 100.0


GeometryNumericRecord: TypeAlias = (
    Vector2 | Vector3 | W3DTriangleRecord | W3DVertexInfluenceRecord
)
BoundValue: TypeAlias = int | float


@dataclass(frozen=True, slots=True)
class W3DGeometryStreamBound:
    """An immutable inclusive numeric range used by an attestation."""

    name: str
    minimum: BoundValue | None
    maximum: BoundValue | None

    def neutral(self) -> dict[str, object]:
        return {"minimum": self.minimum, "maximum": self.maximum}

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DGeometryStreamAttestation:
    """Immutable evidence for one independently decoded chunk payload."""

    chunk_id: int
    stream_kind: str
    stream_slot: str
    record_layout: str
    record_size: int
    record_count: int
    expected_record_count: int
    owner_vertex_count: int
    owner_triangle_count: int | None
    pivot_count: int | None
    payload_byte_length: int
    payload_sha256: str
    canonical_record_sha256: str
    bounds: tuple[W3DGeometryStreamBound, ...]
    _numeric_records: tuple[GeometryNumericRecord, ...] = field(
        repr=False,
        compare=False,
    )

    def numeric_records(self) -> tuple[GeometryNumericRecord, ...]:
        """Return immutable decoded numbers, never the caller's payload bytes."""

        return self._numeric_records

    def neutral(self) -> dict[str, object]:
        """Return a deterministic, payload-free, JSON-serializable object."""

        result: dict[str, object] = {
            "schema": GEOMETRY_STREAM_SCHEMA,
            "schemaVersion": GEOMETRY_STREAM_SCHEMA_VERSION,
            "chunkId": self.chunk_id,
            "chunkIdHex": f"0x{self.chunk_id:08X}",
            "streamKind": self.stream_kind,
            "streamSlot": self.stream_slot,
            "recordLayout": self.record_layout,
            "recordSize": self.record_size,
            "recordCount": self.record_count,
            "expectedRecordCount": self.expected_record_count,
            "ownerVertexCount": self.owner_vertex_count,
            "payloadByteLength": self.payload_byte_length,
            "payloadSha256": self.payload_sha256,
            "canonicalRecordSha256": self.canonical_record_sha256,
            "bounds": {bound.name: bound.neutral() for bound in self.bounds},
        }
        if self.owner_triangle_count is not None:
            result["ownerTriangleCount"] = self.owner_triangle_count
        if self.pivot_count is not None:
            result["pivotCount"] = self.pivot_count
        return result

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class _StreamSpec:
    kind: str
    slot: str
    layout: str
    record_size: int


_VECTOR3_SPECS = {
    W3D_CHUNK_VERTICES: _StreamSpec("vertices", "primary", "<3f", 12),
    W3D_CHUNK_VERTEX_NORMALS: _StreamSpec("normals", "primary", "<3f", 12),
    W3D_CHUNK_VERTICES_2: _StreamSpec("vertices", "secondary", "<3f", 12),
    W3D_CHUNK_NORMALS_2: _StreamSpec("normals", "secondary", "<3f", 12),
}
_TRIANGLE_SPEC = _StreamSpec("triangles", "owner", "<4I4f", 32)
_INFLUENCE_SPEC = _StreamSpec("vertex-influences", "owner", "<4H", 8)
_UV_SPEC = _StreamSpec("stage-texcoords", "owner", "<2f", 8)


def _plain_count(value: object, label: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise W3DGeometryStreamDecodeError(
            f"{label} must be an explicit non-negative integer"
        )
    return value


def _chunk_id(value: object) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < 0
        or value > 0xFFFFFFFF
    ):
        raise W3DGeometryStreamDecodeError(
            "chunk ID must be an unsigned 32-bit integer"
        )
    return value


def _payload_bytes(payload: object) -> bytes:
    if not isinstance(payload, bytes):
        raise TypeError("W3D geometry stream payload must be bytes")
    if len(payload) > MAX_GEOMETRY_STREAM_BYTES:
        raise W3DGeometryStreamDecodeError(
            f"geometry stream exceeds {MAX_GEOMETRY_STREAM_BYTES} byte limit"
        )
    return payload


def _record_count(payload: bytes, spec: _StreamSpec, expected: int) -> int:
    count, remainder = divmod(len(payload), spec.record_size)
    if remainder:
        raise W3DGeometryStreamDecodeError(
            f"{spec.kind} payload length is not a multiple of its "
            f"{spec.record_size}-byte record"
        )
    if count != expected:
        raise W3DGeometryStreamDecodeError(
            f"{spec.kind} record count {count} does not match owner count {expected}"
        )
    return count


def _finite(values: Iterable[float], kind: str, record_index: int) -> None:
    if not all(math.isfinite(value) for value in values):
        raise W3DGeometryStreamDecodeError(
            f"{kind} record {record_index} contains a non-finite float"
        )


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
) -> tuple[W3DGeometryStreamBound, ...]:
    result = []
    for name, values in columns:
        minimum, maximum = _range(values)
        result.append(W3DGeometryStreamBound(name, minimum, maximum))
    return tuple(result)


def _attestation(
    *,
    chunk_id: int,
    payload: bytes,
    spec: _StreamSpec,
    expected_record_count: int,
    owner_vertex_count: int,
    owner_triangle_count: int | None,
    pivot_count: int | None,
    bounds: tuple[W3DGeometryStreamBound, ...],
    records: tuple[GeometryNumericRecord, ...],
    canonical_hash: str,
) -> W3DGeometryStreamAttestation:
    return W3DGeometryStreamAttestation(
        chunk_id=chunk_id,
        stream_kind=spec.kind,
        stream_slot=spec.slot,
        record_layout=spec.layout,
        record_size=spec.record_size,
        record_count=len(records),
        expected_record_count=expected_record_count,
        owner_vertex_count=owner_vertex_count,
        owner_triangle_count=owner_triangle_count,
        pivot_count=pivot_count,
        payload_byte_length=len(payload),
        payload_sha256=hashlib.sha256(payload).hexdigest(),
        canonical_record_sha256=canonical_hash,
        bounds=bounds,
        _numeric_records=records,
    )


def decode_vec3_stream(
    chunk_id: int,
    payload: bytes,
    *,
    expected_vertex_count: int,
    owner_triangle_count: int | None = None,
) -> W3DGeometryStreamAttestation:
    """Decode one primary/secondary vertex or normal vec3 stream."""

    normalized_chunk_id = _chunk_id(chunk_id)
    try:
        spec = _VECTOR3_SPECS[normalized_chunk_id]
    except KeyError as error:
        raise W3DGeometryStreamUnsupportedError(
            f"chunk 0x{normalized_chunk_id:08X} is not a supported vec3 stream"
        ) from error
    source = _payload_bytes(payload)
    vertex_count = _plain_count(expected_vertex_count, "expected vertex count")
    triangle_count = (
        None
        if owner_triangle_count is None
        else _plain_count(owner_triangle_count, "owner triangle count")
    )
    _record_count(source, spec, vertex_count)

    records: list[GeometryNumericRecord] = []
    canonical = hashlib.sha256()
    for record_index, unpacked in enumerate(struct.iter_unpack(spec.layout, source)):
        vector: Vector3 = (unpacked[0], unpacked[1], unpacked[2])
        _finite(vector, spec.kind, record_index)
        records.append(vector)
        canonical.update(struct.pack(spec.layout, *vector))
    typed_records = tuple(records)
    vector_records = tuple(record for record in typed_records if isinstance(record, tuple))
    attest_bounds = _bounds(
        (
            ("x", (record[0] for record in vector_records)),
            ("y", (record[1] for record in vector_records)),
            ("z", (record[2] for record in vector_records)),
        )
    )
    return _attestation(
        chunk_id=normalized_chunk_id,
        payload=source,
        spec=spec,
        expected_record_count=vertex_count,
        owner_vertex_count=vertex_count,
        owner_triangle_count=triangle_count,
        pivot_count=None,
        bounds=attest_bounds,
        records=typed_records,
        canonical_hash=canonical.hexdigest(),
    )


def decode_triangle_stream(
    payload: bytes,
    *,
    expected_vertex_count: int,
    expected_triangle_count: int,
) -> W3DGeometryStreamAttestation:
    """Decode exact 32-byte triangle records and validate all vertex indices."""

    source = _payload_bytes(payload)
    vertex_count = _plain_count(expected_vertex_count, "expected vertex count")
    triangle_count = _plain_count(expected_triangle_count, "expected triangle count")
    _record_count(source, _TRIANGLE_SPEC, triangle_count)

    records: list[W3DTriangleRecord] = []
    canonical = hashlib.sha256()
    for record_index, values in enumerate(
        struct.iter_unpack(_TRIANGLE_SPEC.layout, source)
    ):
        indices = (values[0], values[1], values[2])
        for index in indices:
            if index >= vertex_count:
                raise W3DGeometryStreamDecodeError(
                    f"triangle record {record_index} vertex index {index} is outside "
                    f"owner vertex count {vertex_count}"
                )
        normal: Vector3 = (values[4], values[5], values[6])
        distance = values[7]
        _finite((*normal, distance), "triangles", record_index)
        record = W3DTriangleRecord(indices, values[3], normal, distance)
        records.append(record)
        canonical.update(
            struct.pack(
                _TRIANGLE_SPEC.layout,
                *record.vertex_indices,
                record.surface_type,
                *record.normal,
                record.distance,
            )
        )
    typed_records: tuple[GeometryNumericRecord, ...] = tuple(records)
    attest_bounds = _bounds(
        (
            (
                "vertexIndex",
                (index for record in records for index in record.vertex_indices),
            ),
            ("surfaceType", (record.surface_type for record in records)),
            ("normalX", (record.normal[0] for record in records)),
            ("normalY", (record.normal[1] for record in records)),
            ("normalZ", (record.normal[2] for record in records)),
            ("distance", (record.distance for record in records)),
        )
    )
    return _attestation(
        chunk_id=W3D_CHUNK_TRIANGLES,
        payload=source,
        spec=_TRIANGLE_SPEC,
        expected_record_count=triangle_count,
        owner_vertex_count=vertex_count,
        owner_triangle_count=triangle_count,
        pivot_count=None,
        bounds=attest_bounds,
        records=typed_records,
        canonical_hash=canonical.hexdigest(),
    )


def decode_vertex_influence_stream(
    payload: bytes,
    *,
    expected_vertex_count: int,
    pivot_count: int | None = None,
    owner_triangle_count: int | None = None,
) -> W3DGeometryStreamAttestation:
    """Decode two-pivot influence records and validate supplied pivot bounds."""

    source = _payload_bytes(payload)
    vertex_count = _plain_count(expected_vertex_count, "expected vertex count")
    normalized_pivot_count = (
        None if pivot_count is None else _plain_count(pivot_count, "pivot count")
    )
    triangle_count = (
        None
        if owner_triangle_count is None
        else _plain_count(owner_triangle_count, "owner triangle count")
    )
    _record_count(source, _INFLUENCE_SPEC, vertex_count)

    records: list[W3DVertexInfluenceRecord] = []
    canonical = hashlib.sha256()
    for record_index, values in enumerate(
        struct.iter_unpack(_INFLUENCE_SPEC.layout, source)
    ):
        primary_pivot, secondary_pivot, primary_weight, secondary_weight = values
        if primary_weight > 100 or secondary_weight > 100:
            raise W3DGeometryStreamDecodeError(
                f"vertex-influences record {record_index} has a weight outside "
                "the encoded 0..100 normalized range"
            )
        if normalized_pivot_count is not None:
            for pivot in (primary_pivot, secondary_pivot):
                if pivot >= normalized_pivot_count:
                    raise W3DGeometryStreamDecodeError(
                        f"vertex-influences record {record_index} pivot index {pivot} "
                        f"is outside pivot count {normalized_pivot_count}"
                    )
        record = W3DVertexInfluenceRecord(
            primary_pivot,
            secondary_pivot,
            primary_weight,
            secondary_weight,
        )
        records.append(record)
        canonical.update(
            struct.pack(
                _INFLUENCE_SPEC.layout,
                record.primary_pivot_index,
                record.secondary_pivot_index,
                record.primary_weight_raw,
                record.secondary_weight_raw,
            )
        )
    typed_records: tuple[GeometryNumericRecord, ...] = tuple(records)
    attest_bounds = _bounds(
        (
            ("primaryPivotIndex", (record.primary_pivot_index for record in records)),
            (
                "secondaryPivotIndex",
                (record.secondary_pivot_index for record in records),
            ),
            ("primaryWeightRaw", (record.primary_weight_raw for record in records)),
            (
                "secondaryWeightRaw",
                (record.secondary_weight_raw for record in records),
            ),
        )
    )
    return _attestation(
        chunk_id=W3D_CHUNK_VERTEX_INFLUENCES,
        payload=source,
        spec=_INFLUENCE_SPEC,
        expected_record_count=vertex_count,
        owner_vertex_count=vertex_count,
        owner_triangle_count=triangle_count,
        pivot_count=normalized_pivot_count,
        bounds=attest_bounds,
        records=typed_records,
        canonical_hash=canonical.hexdigest(),
    )


def decode_uv_stream(
    payload: bytes,
    *,
    expected_vertex_count: int,
    owner_triangle_count: int | None = None,
) -> W3DGeometryStreamAttestation:
    """Decode one owner-bound stage-texcoord vec2 payload."""

    source = _payload_bytes(payload)
    vertex_count = _plain_count(expected_vertex_count, "expected vertex count")
    triangle_count = (
        None
        if owner_triangle_count is None
        else _plain_count(owner_triangle_count, "owner triangle count")
    )
    _record_count(source, _UV_SPEC, vertex_count)

    records: list[GeometryNumericRecord] = []
    canonical = hashlib.sha256()
    for record_index, unpacked in enumerate(struct.iter_unpack(_UV_SPEC.layout, source)):
        vector: Vector2 = (unpacked[0], unpacked[1])
        _finite(vector, _UV_SPEC.kind, record_index)
        records.append(vector)
        canonical.update(struct.pack(_UV_SPEC.layout, *vector))
    typed_records = tuple(records)
    vector_records = tuple(record for record in typed_records if isinstance(record, tuple))
    attest_bounds = _bounds(
        (
            ("u", (record[0] for record in vector_records)),
            ("v", (record[1] for record in vector_records)),
        )
    )
    return _attestation(
        chunk_id=W3D_CHUNK_STAGE_TEXCOORDS,
        payload=source,
        spec=_UV_SPEC,
        expected_record_count=vertex_count,
        owner_vertex_count=vertex_count,
        owner_triangle_count=triangle_count,
        pivot_count=None,
        bounds=attest_bounds,
        records=typed_records,
        canonical_hash=canonical.hexdigest(),
    )


def decode_geometry_stream(
    chunk_id: int,
    payload: bytes,
    *,
    expected_vertex_count: int,
    expected_triangle_count: int,
    pivot_count: int | None = None,
) -> W3DGeometryStreamAttestation:
    """Dispatch one known stream while requiring both owner cardinalities."""

    normalized_chunk_id = _chunk_id(chunk_id)
    vertex_count = _plain_count(expected_vertex_count, "expected vertex count")
    triangle_count = _plain_count(expected_triangle_count, "expected triangle count")
    if normalized_chunk_id in _VECTOR3_SPECS:
        return decode_vec3_stream(
            normalized_chunk_id,
            payload,
            expected_vertex_count=vertex_count,
            owner_triangle_count=triangle_count,
        )
    if normalized_chunk_id == W3D_CHUNK_TRIANGLES:
        return decode_triangle_stream(
            payload,
            expected_vertex_count=vertex_count,
            expected_triangle_count=triangle_count,
        )
    if normalized_chunk_id == W3D_CHUNK_VERTEX_INFLUENCES:
        return decode_vertex_influence_stream(
            payload,
            expected_vertex_count=vertex_count,
            pivot_count=pivot_count,
            owner_triangle_count=triangle_count,
        )
    if normalized_chunk_id == W3D_CHUNK_STAGE_TEXCOORDS:
        return decode_uv_stream(
            payload,
            expected_vertex_count=vertex_count,
            owner_triangle_count=triangle_count,
        )
    raise W3DGeometryStreamUnsupportedError(
        f"chunk 0x{normalized_chunk_id:08X} has no evidence-backed geometry decoder"
    )


def decode_geometry_streams(
    streams: Iterable[tuple[int, bytes]],
    *,
    expected_vertex_count: int,
    expected_triangle_count: int,
    pivot_count: int | None = None,
) -> tuple[W3DGeometryStreamAttestation, ...]:
    """Decode every supplied stream in order without collapsing duplicate IDs."""

    return tuple(
        decode_geometry_stream(
            chunk_id,
            payload,
            expected_vertex_count=expected_vertex_count,
            expected_triangle_count=expected_triangle_count,
            pivot_count=pivot_count,
        )
        for chunk_id, payload in streams
    )


# Concise aliases for callers already operating in an owner-bound mesh context.
decode_triangles = decode_triangle_stream
decode_vertex_influences = decode_vertex_influence_stream
decode_stage_texcoords = decode_uv_stream

