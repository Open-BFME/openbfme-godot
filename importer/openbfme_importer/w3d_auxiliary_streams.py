"""Strict, payload-free attestations for bounded W3D auxiliary streams.

The nine layouts below are limited to records parsed by OpenSAGE at pinned
commit ``588ac477367a0022adf29f20a084e8873014e6ce``.  The relevant primary
sources are ``W3dMesh.cs``, ``W3dMaterialPass.cs``,
``W3dMeshAabTree*.cs``, and ``W3dHierarchyDef.cs`` in:

https://github.com/OpenSAGE/OpenSAGE/tree/588ac477367a0022adf29f20a084e8873014e6ce/src/OpenSage.FileFormats.W3d

The AABB ownership, cardinality, and leaf-bit contract is also stated in the
Westwood-derived header used by that pinned parser:

https://github.com/mikolalysenko/w3d2ply/blob/master/w3d_file.h

Callers supply cardinalities from separately validated owner headers.  The
decoders preserve exact source length and SHA-256 evidence, expose immutable
numeric records only, and never return mesh user text.  A decoded auxiliary
stream does not by itself prove material, collision, skeleton, GLB, or render
correctness.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import math
import struct
from typing import Iterable, TypeAlias


AUXILIARY_STREAM_SCHEMA = "openbfme.w3d-auxiliary-stream"
AUXILIARY_STREAM_SCHEMA_VERSION = 0
MAX_AUXILIARY_STREAM_BYTES = 512 * 1024 * 1024

W3D_CHUNK_MESH_USER_TEXT = 0x0000000C
W3D_CHUNK_VERTEX_SHADE_INDICES = 0x00000022
W3D_CHUNK_DIFFUSE_COLOR_VALUES = 0x0000003B
W3D_CHUNK_TANGENTS = 0x00000060
W3D_CHUNK_BITANGENTS = 0x00000061
W3D_CHUNK_AABTREE_HEADER = 0x00000091
W3D_CHUNK_AABTREE_POLYINDICES = 0x00000092
W3D_CHUNK_AABTREE_NODES = 0x00000093
W3D_CHUNK_PIVOT_FIXUPS = 0x00000103

SUPPORTED_AUXILIARY_STREAM_CHUNK_IDS = frozenset(
    {
        W3D_CHUNK_MESH_USER_TEXT,
        W3D_CHUNK_VERTEX_SHADE_INDICES,
        W3D_CHUNK_DIFFUSE_COLOR_VALUES,
        W3D_CHUNK_TANGENTS,
        W3D_CHUNK_BITANGENTS,
        W3D_CHUNK_AABTREE_HEADER,
        W3D_CHUNK_AABTREE_POLYINDICES,
        W3D_CHUNK_AABTREE_NODES,
        W3D_CHUNK_PIVOT_FIXUPS,
    }
)

Vector3: TypeAlias = tuple[float, float, float]
Matrix4x3Values: TypeAlias = tuple[
    float,
    float,
    float,
    float,
    float,
    float,
    float,
    float,
    float,
    float,
    float,
    float,
]
Rgba: TypeAlias = tuple[int, int, int, int]
BoundValue: TypeAlias = int | float


class W3DAuxiliaryStreamDecodeError(ValueError):
    """Raised when a proven auxiliary payload violates its exact contract."""


class W3DAuxiliaryStreamUnsupportedError(W3DAuxiliaryStreamDecodeError):
    """Raised when no evidence-backed auxiliary decoder exists for a chunk."""


@dataclass(frozen=True, slots=True)
class W3DAabTreeHeaderRecord:
    """The exact 32-byte W3dMeshAABTreeHeader record."""

    node_count: int
    polygon_count: int
    padding: tuple[int, int, int, int, int, int]


@dataclass(frozen=True, slots=True)
class W3DAabTreeNodeRecord:
    """The exact 32-byte W3dMeshAABTreeNode record."""

    minimum: Vector3
    maximum: Vector3
    front_or_polygon0: int
    back_or_polygon_count: int

    @property
    def is_leaf(self) -> bool:
        return bool(self.front_or_polygon0 & 0x80000000)

    @property
    def front_child_or_polygon0(self) -> int:
        return self.front_or_polygon0 & 0x7FFFFFFF


@dataclass(frozen=True, slots=True)
class W3DUserTextStructure:
    """Structural string evidence that deliberately contains no authored text."""

    terminator_offset: int
    trailing_byte_count: int
    trailing_nonzero_byte_count: int


AuxiliaryRecord: TypeAlias = (
    int
    | Vector3
    | Matrix4x3Values
    | Rgba
    | W3DAabTreeHeaderRecord
    | W3DAabTreeNodeRecord
    | W3DUserTextStructure
)


@dataclass(frozen=True, slots=True)
class W3DAuxiliaryStreamBound:
    """An immutable inclusive numeric range used by an attestation."""

    name: str
    minimum: BoundValue | None
    maximum: BoundValue | None

    def neutral(self) -> dict[str, object]:
        return {"minimum": self.minimum, "maximum": self.maximum}

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DAuxiliaryStreamAttestation:
    """Deterministic evidence for one owner-bound auxiliary payload."""

    chunk_id: int
    stream_kind: str
    record_layout: str
    record_size: int | None
    record_count: int
    expected_record_count: int
    owner_vertex_count: int | None
    owner_triangle_count: int | None
    owner_pivot_count: int | None
    owner_aabb_node_count: int | None
    owner_aabb_polygon_count: int | None
    payload_byte_length: int
    payload_sha256: str
    canonical_record_sha256: str
    bounds: tuple[W3DAuxiliaryStreamBound, ...]
    _records: tuple[AuxiliaryRecord, ...] = field(repr=False, compare=False)

    def records(self) -> tuple[AuxiliaryRecord, ...]:
        """Return immutable numeric/structural records, never payload text."""

        return self._records

    def neutral(self) -> dict[str, object]:
        """Return deterministic JSON-ready evidence with no authored text."""

        result: dict[str, object] = {
            "schema": AUXILIARY_STREAM_SCHEMA,
            "schemaVersion": AUXILIARY_STREAM_SCHEMA_VERSION,
            "chunkId": self.chunk_id,
            "chunkIdHex": f"0x{self.chunk_id:08X}",
            "streamKind": self.stream_kind,
            "recordLayout": self.record_layout,
            "recordCount": self.record_count,
            "expectedRecordCount": self.expected_record_count,
            "payloadByteLength": self.payload_byte_length,
            "payloadSha256": self.payload_sha256,
            "canonicalRecordSha256": self.canonical_record_sha256,
            "bounds": {bound.name: bound.neutral() for bound in self.bounds},
        }
        if self.record_size is not None:
            result["recordSize"] = self.record_size
        if self.owner_vertex_count is not None:
            result["ownerVertexCount"] = self.owner_vertex_count
        if self.owner_triangle_count is not None:
            result["ownerTriangleCount"] = self.owner_triangle_count
        if self.owner_pivot_count is not None:
            result["ownerPivotCount"] = self.owner_pivot_count
        if self.owner_aabb_node_count is not None:
            result["ownerAabbNodeCount"] = self.owner_aabb_node_count
        if self.owner_aabb_polygon_count is not None:
            result["ownerAabbPolygonCount"] = self.owner_aabb_polygon_count
        return result

    json_ready = neutral


def _chunk_id(value: object) -> int:
    if (
        not isinstance(value, int)
        or isinstance(value, bool)
        or value < 0
        or value > 0xFFFFFFFF
    ):
        raise W3DAuxiliaryStreamDecodeError(
            "chunk ID must be an unsigned 32-bit integer"
        )
    return value


def _payload_bytes(payload: object) -> bytes:
    if not isinstance(payload, bytes):
        raise TypeError("W3D auxiliary stream payload must be bytes")
    if len(payload) > MAX_AUXILIARY_STREAM_BYTES:
        raise W3DAuxiliaryStreamDecodeError(
            f"auxiliary stream exceeds {MAX_AUXILIARY_STREAM_BYTES} byte limit"
        )
    return payload


def _required_count(value: object, label: str) -> int:
    if not isinstance(value, int) or isinstance(value, bool) or value < 0:
        raise W3DAuxiliaryStreamDecodeError(
            f"{label} must be an explicit non-negative integer"
        )
    return value


def _record_count(
    payload: bytes,
    *,
    kind: str,
    record_size: int,
    expected: int,
) -> int:
    count, remainder = divmod(len(payload), record_size)
    if remainder:
        raise W3DAuxiliaryStreamDecodeError(
            f"{kind} payload length is not a multiple of its "
            f"{record_size}-byte record"
        )
    if count != expected:
        raise W3DAuxiliaryStreamDecodeError(
            f"{kind} record count {count} does not match owner count {expected}"
        )
    return count


def _finite(values: Iterable[float], kind: str, record_index: int) -> None:
    if not all(math.isfinite(value) for value in values):
        raise W3DAuxiliaryStreamDecodeError(
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
) -> tuple[W3DAuxiliaryStreamBound, ...]:
    result = []
    for name, values in columns:
        minimum, maximum = _range(values)
        result.append(W3DAuxiliaryStreamBound(name, minimum, maximum))
    return tuple(result)


def _attestation(
    *,
    chunk_id: int,
    kind: str,
    layout: str,
    record_size: int | None,
    expected_record_count: int,
    payload: bytes,
    records: tuple[AuxiliaryRecord, ...],
    canonical_sha256: str,
    bounds: tuple[W3DAuxiliaryStreamBound, ...],
    owner_vertex_count: int | None = None,
    owner_triangle_count: int | None = None,
    owner_pivot_count: int | None = None,
    owner_aabb_node_count: int | None = None,
    owner_aabb_polygon_count: int | None = None,
) -> W3DAuxiliaryStreamAttestation:
    return W3DAuxiliaryStreamAttestation(
        chunk_id=chunk_id,
        stream_kind=kind,
        record_layout=layout,
        record_size=record_size,
        record_count=len(records),
        expected_record_count=expected_record_count,
        owner_vertex_count=owner_vertex_count,
        owner_triangle_count=owner_triangle_count,
        owner_pivot_count=owner_pivot_count,
        owner_aabb_node_count=owner_aabb_node_count,
        owner_aabb_polygon_count=owner_aabb_polygon_count,
        payload_byte_length=len(payload),
        payload_sha256=hashlib.sha256(payload).hexdigest(),
        canonical_record_sha256=canonical_sha256,
        bounds=bounds,
        _records=records,
    )


def _decode_mesh_user_text(
    payload: bytes,
    *,
    vertex_count: int,
    triangle_count: int,
) -> W3DAuxiliaryStreamAttestation:
    terminator = payload.find(b"\0")
    if terminator < 0:
        raise W3DAuxiliaryStreamDecodeError(
            "mesh-user-text payload has no NUL terminator"
        )
    trailing = payload[terminator + 1 :]
    structure = W3DUserTextStructure(
        terminator_offset=terminator,
        trailing_byte_count=len(trailing),
        trailing_nonzero_byte_count=sum(value != 0 for value in trailing),
    )
    return _attestation(
        chunk_id=W3D_CHUNK_MESH_USER_TEXT,
        kind="mesh-user-text",
        layout="nul-terminated-byte-string",
        record_size=None,
        expected_record_count=1,
        payload=payload,
        records=(structure,),
        canonical_sha256=hashlib.sha256(payload).hexdigest(),
        bounds=_bounds(
            (
                ("terminatorOffset", (structure.terminator_offset,)),
                ("trailingByteCount", (structure.trailing_byte_count,)),
                (
                    "trailingNonzeroByteCount",
                    (structure.trailing_nonzero_byte_count,),
                ),
            )
        ),
        owner_vertex_count=vertex_count,
        owner_triangle_count=triangle_count,
    )


def _decode_uint32_per_vertex(
    payload: bytes,
    *,
    vertex_count: int,
    triangle_count: int,
) -> W3DAuxiliaryStreamAttestation:
    kind = "vertex-shade-indices"
    _record_count(payload, kind=kind, record_size=4, expected=vertex_count)
    records: tuple[AuxiliaryRecord, ...] = tuple(
        value for (value,) in struct.iter_unpack("<I", payload)
    )
    canonical = hashlib.sha256()
    for value in records:
        canonical.update(struct.pack("<I", value))
    return _attestation(
        chunk_id=W3D_CHUNK_VERTEX_SHADE_INDICES,
        kind=kind,
        layout="<I",
        record_size=4,
        expected_record_count=vertex_count,
        payload=payload,
        records=records,
        canonical_sha256=canonical.hexdigest(),
        bounds=_bounds((("shadeIndex", (int(value) for value in records)),)),
        owner_vertex_count=vertex_count,
        owner_triangle_count=triangle_count,
    )


def _decode_vec3_per_vertex(
    chunk_id: int,
    payload: bytes,
    *,
    vertex_count: int,
    triangle_count: int,
) -> W3DAuxiliaryStreamAttestation:
    kind = {
        W3D_CHUNK_TANGENTS: "tangents",
        W3D_CHUNK_BITANGENTS: "bitangents",
    }[chunk_id]
    _record_count(payload, kind=kind, record_size=12, expected=vertex_count)
    vectors: list[Vector3] = []
    canonical = hashlib.sha256()
    for index, values in enumerate(struct.iter_unpack("<3f", payload)):
        vector: Vector3 = (values[0], values[1], values[2])
        _finite(vector, kind, index)
        vectors.append(vector)
        canonical.update(struct.pack("<3f", *vector))
    records: tuple[AuxiliaryRecord, ...] = tuple(vectors)
    return _attestation(
        chunk_id=chunk_id,
        kind=kind,
        layout="<3f",
        record_size=12,
        expected_record_count=vertex_count,
        payload=payload,
        records=records,
        canonical_sha256=canonical.hexdigest(),
        bounds=_bounds(
            (
                ("x", (vector[0] for vector in vectors)),
                ("y", (vector[1] for vector in vectors)),
                ("z", (vector[2] for vector in vectors)),
            )
        ),
        owner_vertex_count=vertex_count,
        owner_triangle_count=triangle_count,
    )


def _decode_diffuse_colors(
    payload: bytes,
    *,
    vertex_count: int,
    triangle_count: int,
) -> W3DAuxiliaryStreamAttestation:
    kind = "diffuse-color-values"
    _record_count(payload, kind=kind, record_size=4, expected=vertex_count)
    colors: tuple[Rgba, ...] = tuple(
        (values[0], values[1], values[2], values[3])
        for values in struct.iter_unpack("<4B", payload)
    )
    records: tuple[AuxiliaryRecord, ...] = colors
    canonical = hashlib.sha256()
    for color in colors:
        canonical.update(struct.pack("<4B", *color))
    return _attestation(
        chunk_id=W3D_CHUNK_DIFFUSE_COLOR_VALUES,
        kind=kind,
        layout="<4B",
        record_size=4,
        expected_record_count=vertex_count,
        payload=payload,
        records=records,
        canonical_sha256=canonical.hexdigest(),
        bounds=_bounds(
            (
                ("red", (color[0] for color in colors)),
                ("green", (color[1] for color in colors)),
                ("blue", (color[2] for color in colors)),
                ("alpha", (color[3] for color in colors)),
            )
        ),
        owner_vertex_count=vertex_count,
        owner_triangle_count=triangle_count,
    )


def _decode_aabb_header(
    payload: bytes,
    *,
    triangle_count: int,
) -> W3DAuxiliaryStreamAttestation:
    if len(payload) != 32:
        raise W3DAuxiliaryStreamDecodeError(
            "aabb-tree-header payload must be exactly 32 bytes"
        )
    values = struct.unpack("<8I", payload)
    record = W3DAabTreeHeaderRecord(
        node_count=values[0],
        polygon_count=values[1],
        padding=(values[2], values[3], values[4], values[5], values[6], values[7]),
    )
    if record.polygon_count != triangle_count:
        raise W3DAuxiliaryStreamDecodeError(
            "aabb-tree-header polygon count does not match owner triangle count"
        )
    records: tuple[AuxiliaryRecord, ...] = (record,)
    return _attestation(
        chunk_id=W3D_CHUNK_AABTREE_HEADER,
        kind="aabb-tree-header",
        layout="<8I",
        record_size=32,
        expected_record_count=1,
        payload=payload,
        records=records,
        canonical_sha256=hashlib.sha256(struct.pack("<8I", *values)).hexdigest(),
        bounds=_bounds(
            (
                ("nodeCount", (record.node_count,)),
                ("polygonCount", (record.polygon_count,)),
                ("paddingValue", record.padding),
            )
        ),
        owner_triangle_count=triangle_count,
        owner_aabb_node_count=record.node_count,
        owner_aabb_polygon_count=record.polygon_count,
    )


def _decode_aabb_poly_indices(
    payload: bytes,
    *,
    triangle_count: int,
    polygon_count: int,
) -> W3DAuxiliaryStreamAttestation:
    if polygon_count != triangle_count:
        raise W3DAuxiliaryStreamDecodeError(
            "AABB polygon count does not match owner triangle count"
        )
    kind = "aabb-tree-poly-indices"
    _record_count(payload, kind=kind, record_size=4, expected=polygon_count)
    indices = tuple(value for (value,) in struct.iter_unpack("<I", payload))
    for record_index, value in enumerate(indices):
        if value >= triangle_count:
            raise W3DAuxiliaryStreamDecodeError(
                f"{kind} record {record_index} index {value} is outside "
                f"owner triangle count {triangle_count}"
            )
    records: tuple[AuxiliaryRecord, ...] = indices
    canonical = hashlib.sha256()
    for value in indices:
        canonical.update(struct.pack("<I", value))
    return _attestation(
        chunk_id=W3D_CHUNK_AABTREE_POLYINDICES,
        kind=kind,
        layout="<I",
        record_size=4,
        expected_record_count=polygon_count,
        payload=payload,
        records=records,
        canonical_sha256=canonical.hexdigest(),
        bounds=_bounds((("polygonIndex", indices),)),
        owner_triangle_count=triangle_count,
        owner_aabb_polygon_count=polygon_count,
    )


def _decode_aabb_nodes(
    payload: bytes,
    *,
    triangle_count: int,
    node_count: int,
    polygon_count: int,
) -> W3DAuxiliaryStreamAttestation:
    if polygon_count != triangle_count:
        raise W3DAuxiliaryStreamDecodeError(
            "AABB polygon count does not match owner triangle count"
        )
    kind = "aabb-tree-nodes"
    _record_count(payload, kind=kind, record_size=32, expected=node_count)
    nodes: list[W3DAabTreeNodeRecord] = []
    canonical = hashlib.sha256()
    for record_index, values in enumerate(struct.iter_unpack("<6f2I", payload)):
        minimum: Vector3 = (values[0], values[1], values[2])
        maximum: Vector3 = (values[3], values[4], values[5])
        _finite((*minimum, *maximum), kind, record_index)
        if any(lower > upper for lower, upper in zip(minimum, maximum, strict=True)):
            raise W3DAuxiliaryStreamDecodeError(
                f"{kind} record {record_index} has an inverted bound"
            )
        node = W3DAabTreeNodeRecord(minimum, maximum, values[6], values[7])
        index_or_polygon0 = node.front_child_or_polygon0
        if node.is_leaf:
            if index_or_polygon0 + node.back_or_polygon_count > polygon_count:
                raise W3DAuxiliaryStreamDecodeError(
                    f"{kind} leaf {record_index} polygon range exceeds "
                    f"AABB polygon count {polygon_count}"
                )
        elif (
            index_or_polygon0 >= node_count
            or node.back_or_polygon_count >= node_count
        ):
            raise W3DAuxiliaryStreamDecodeError(
                f"{kind} branch {record_index} child index exceeds "
                f"AABB node count {node_count}"
            )
        nodes.append(node)
        canonical.update(
            struct.pack(
                "<6f2I",
                *node.minimum,
                *node.maximum,
                node.front_or_polygon0,
                node.back_or_polygon_count,
            )
        )
    records: tuple[AuxiliaryRecord, ...] = tuple(nodes)
    return _attestation(
        chunk_id=W3D_CHUNK_AABTREE_NODES,
        kind=kind,
        layout="<6f2I",
        record_size=32,
        expected_record_count=node_count,
        payload=payload,
        records=records,
        canonical_sha256=canonical.hexdigest(),
        bounds=_bounds(
            (
                ("minimumX", (node.minimum[0] for node in nodes)),
                ("minimumY", (node.minimum[1] for node in nodes)),
                ("minimumZ", (node.minimum[2] for node in nodes)),
                ("maximumX", (node.maximum[0] for node in nodes)),
                ("maximumY", (node.maximum[1] for node in nodes)),
                ("maximumZ", (node.maximum[2] for node in nodes)),
                (
                    "frontOrPolygon0",
                    (node.front_or_polygon0 for node in nodes),
                ),
                (
                    "backOrPolygonCount",
                    (node.back_or_polygon_count for node in nodes),
                ),
            )
        ),
        owner_triangle_count=triangle_count,
        owner_aabb_node_count=node_count,
        owner_aabb_polygon_count=polygon_count,
    )


def _decode_pivot_fixups(
    payload: bytes,
    *,
    pivot_count: int,
) -> W3DAuxiliaryStreamAttestation:
    kind = "pivot-fixups"
    _record_count(payload, kind=kind, record_size=48, expected=pivot_count)
    matrices: list[Matrix4x3Values] = []
    canonical = hashlib.sha256()
    for record_index, values in enumerate(struct.iter_unpack("<12f", payload)):
        _finite(values, kind, record_index)
        matrix: Matrix4x3Values = (
            values[0],
            values[1],
            values[2],
            values[3],
            values[4],
            values[5],
            values[6],
            values[7],
            values[8],
            values[9],
            values[10],
            values[11],
        )
        matrices.append(matrix)
        canonical.update(struct.pack("<12f", *matrix))
    records: tuple[AuxiliaryRecord, ...] = tuple(matrices)
    return _attestation(
        chunk_id=W3D_CHUNK_PIVOT_FIXUPS,
        kind=kind,
        layout="<12f",
        record_size=48,
        expected_record_count=pivot_count,
        payload=payload,
        records=records,
        canonical_sha256=canonical.hexdigest(),
        bounds=_bounds(
            (
                (f"matrixValue{index}", (matrix[index] for matrix in matrices))
                for index in range(12)
            )
        ),
        owner_pivot_count=pivot_count,
    )


def decode_auxiliary_stream(
    chunk_id: int,
    payload: bytes,
    *,
    expected_vertex_count: int | None = None,
    expected_triangle_count: int | None = None,
    expected_pivot_count: int | None = None,
    expected_aabb_node_count: int | None = None,
    expected_aabb_polygon_count: int | None = None,
) -> W3DAuxiliaryStreamAttestation:
    """Decode one of the nine proven auxiliary layouts with owner bounds."""

    normalized_chunk_id = _chunk_id(chunk_id)
    source = _payload_bytes(payload)
    if normalized_chunk_id not in SUPPORTED_AUXILIARY_STREAM_CHUNK_IDS:
        raise W3DAuxiliaryStreamUnsupportedError(
            f"chunk 0x{normalized_chunk_id:08X} is not a supported auxiliary stream"
        )

    if normalized_chunk_id in {
        W3D_CHUNK_MESH_USER_TEXT,
        W3D_CHUNK_VERTEX_SHADE_INDICES,
        W3D_CHUNK_DIFFUSE_COLOR_VALUES,
        W3D_CHUNK_TANGENTS,
        W3D_CHUNK_BITANGENTS,
    }:
        vertex_count = _required_count(
            expected_vertex_count,
            "expected vertex count",
        )
        triangle_count = _required_count(
            expected_triangle_count,
            "expected triangle count",
        )
        if normalized_chunk_id == W3D_CHUNK_MESH_USER_TEXT:
            return _decode_mesh_user_text(
                source,
                vertex_count=vertex_count,
                triangle_count=triangle_count,
            )
        if normalized_chunk_id == W3D_CHUNK_VERTEX_SHADE_INDICES:
            return _decode_uint32_per_vertex(
                source,
                vertex_count=vertex_count,
                triangle_count=triangle_count,
            )
        if normalized_chunk_id == W3D_CHUNK_DIFFUSE_COLOR_VALUES:
            return _decode_diffuse_colors(
                source,
                vertex_count=vertex_count,
                triangle_count=triangle_count,
            )
        return _decode_vec3_per_vertex(
            normalized_chunk_id,
            source,
            vertex_count=vertex_count,
            triangle_count=triangle_count,
        )

    if normalized_chunk_id == W3D_CHUNK_PIVOT_FIXUPS:
        return _decode_pivot_fixups(
            source,
            pivot_count=_required_count(
                expected_pivot_count,
                "expected pivot count",
            ),
        )

    triangle_count = _required_count(
        expected_triangle_count,
        "expected triangle count",
    )
    if normalized_chunk_id == W3D_CHUNK_AABTREE_HEADER:
        return _decode_aabb_header(source, triangle_count=triangle_count)
    polygon_count = _required_count(
        expected_aabb_polygon_count,
        "expected AABB polygon count",
    )
    if normalized_chunk_id == W3D_CHUNK_AABTREE_POLYINDICES:
        return _decode_aabb_poly_indices(
            source,
            triangle_count=triangle_count,
            polygon_count=polygon_count,
        )
    return _decode_aabb_nodes(
        source,
        triangle_count=triangle_count,
        node_count=_required_count(
            expected_aabb_node_count,
            "expected AABB node count",
        ),
        polygon_count=polygon_count,
    )

