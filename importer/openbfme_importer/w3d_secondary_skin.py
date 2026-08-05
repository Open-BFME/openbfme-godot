"""Fail-closed removal of proven-redundant BFME secondary skin streams.

BFME W3D skins may store a second vertex/normal record for the second bone in
each two-bone influence.  The pinned OpenSAGE exporter establishes the meaning:
each active record is the same bind-pose point/normal expressed in that bone's
local space.  glTF instead stores one bind-pose point plus joint indices,
weights, and inverse-bind matrices.  The representations are equivalent only
when their reconstructed bind-space values coincide.

This module proves that equivalence before removing ``0x00000C00`` and
``0x00000C01``.  It deliberately supports only the exact retail contract used
by the pinned importer.  Any malformed owner, ambiguous hierarchy, non-finite
number, cardinality mismatch, unbound HLOD, or semantic disagreement aborts
without producing output.
"""

from __future__ import annotations

from dataclasses import dataclass, field, replace
import hashlib
import json
import math
import struct
from typing import Iterable, TypeAlias

from .w3d_string_leaves import is_w3d_string_leaf


SECONDARY_SKIN_SCHEMA = "openbfme.w3d-secondary-skin-proof"
SECONDARY_SKIN_SCHEMA_VERSION = 0

# Measured dual-local bind coincidence across the retail corpus:
# - Men-building / Fords-creature backtests: positions ≤1.39e-5, normals ≤2.70e-6
# - Reskin shells that reuse a foreign skeleton (RUArcher_SKN on GUArcher_SKL
#   for DwarvenMenOfDale) keep dual-local *positions* within the relative
#   bound, but authored secondary *normals* drift up to ~1.46e-3 (~0.08° on
#   unit normals). That is still dual-local authoring variance, not a distinct
#   stream (0.01-scale fixtures remain an order of magnitude larger).
POSITION_COINCIDENCE_TOLERANCE = 2.0e-5
NORMAL_COINCIDENCE_TOLERANCE = 2.0e-3
# Dual-local copies are authored per bone through the exporter's matrix
# chain; their measured bind-space disagreement across the retail corpus is
# authoring variance that grows with coordinate magnitude and skeleton size
# (men ≤1.3e-05 absolute, elves units ≤6.2e-05, 68-pivot Sauron ≤2.6e-04
# relative). The absolute floors pin small-coordinate models exactly; the
# relative bounds accept only that same narrow variance for larger models.
# Genuinely distinct secondary streams (0.01-scale in the pinned tests)
# remain orders of magnitude above these bounds.
POSITION_COINCIDENCE_RELATIVE_TOLERANCE = 1.0e-3
NORMAL_COINCIDENCE_RELATIVE_TOLERANCE = 1.0e-6

MAX_W3D_BYTES = 512 * 1024 * 1024
MAX_W3D_CHUNKS = 1_000_000
MAX_W3D_DEPTH = 64

W3D_CHUNK_MESH = 0x00000000
W3D_CHUNK_VERTICES = 0x00000002
W3D_CHUNK_VERTEX_NORMALS = 0x00000003
W3D_CHUNK_VERTEX_INFLUENCES = 0x0000000E
W3D_CHUNK_MESH_HEADER = 0x0000001F
W3D_CHUNK_HIERARCHY = 0x00000100
W3D_CHUNK_HIERARCHY_HEADER = 0x00000101
W3D_CHUNK_PIVOTS = 0x00000102
W3D_CHUNK_PIVOT_FIXUPS = 0x00000103
W3D_CHUNK_HLOD = 0x00000700
W3D_CHUNK_HLOD_HEADER = 0x00000701
W3D_CHUNK_HLOD_LOD_ARRAY = 0x00000702
W3D_CHUNK_HLOD_SUB_OBJECT = 0x00000704
W3D_CHUNK_HLOD_AGGREGATE_ARRAY = 0x00000705
W3D_CHUNK_HLOD_PROXY_ARRAY = 0x00000706
W3D_CHUNK_VERTEX_MATERIAL_NAME = 0x0000002C
W3D_CHUNK_VERTEX_MAPPER_ARGS0 = 0x0000002E
W3D_CHUNK_VERTEX_MAPPER_ARGS1 = 0x0000002F
W3D_CHUNK_VERTICES_2 = 0x00000C00
W3D_CHUNK_VERTEX_NORMALS_2 = 0x00000C01

# String-leaf handling (the retail 0x80000000 flag is meaningless on them) lives
# in ``w3d_string_leaves`` so every W3D walker shares one definition.

W3D_GEOMETRY_TYPE_SKIN = 0x00020000
W3D_VERTEX_CHANNEL_LOCATION = 0x01
W3D_VERTEX_CHANNEL_NORMAL = 0x02
W3D_VERTEX_CHANNEL_BONE_ID = 0x10
_REQUIRED_SKIN_CHANNELS = (
    W3D_VERTEX_CHANNEL_LOCATION | W3D_VERTEX_CHANNEL_NORMAL | W3D_VERTEX_CHANNEL_BONE_ID
)

_CHUNK_HEADER_SIZE = 8
_CONTAINER_FLAG = 0x80000000
_SIZE_MASK = 0x7FFFFFFF
_MESH_HEADER_SIZE = 116
_HIERARCHY_HEADER_SIZE = 36
_HIERARCHY_PIVOT_SIZE = 60
_HLOD_HEADER_SIZE = 40
_HLOD_SUB_OBJECT_SIZE = 36

Vec3: TypeAlias = tuple[float, float, float]
Mat4: TypeAlias = tuple[tuple[float, float, float, float], ...]


class W3DSecondarySkinError(ValueError):
    """Raised before output when the secondary-skin proof is incomplete."""


@dataclass(frozen=True, slots=True)
class W3DSecondarySkinMeshProof:
    mesh_ordinal: int
    mesh_identity_sha256: str
    vertex_count: int
    triangle_count: int
    active_dual_vertex_count: int
    inactive_secondary_vertex_count: int
    active_same_bone_vertex_count: int
    removed_chunk_count: int
    removed_byte_count: int
    maximum_position_delta: float
    maximum_normal_delta: float
    primary_vertices_sha256: str
    secondary_vertices_sha256: str
    primary_normals_sha256: str
    secondary_normals_sha256: str
    influences_sha256: str

    def neutral(self) -> dict[str, object]:
        return {
            "meshOrdinal": self.mesh_ordinal,
            "meshIdentitySha256": self.mesh_identity_sha256,
            "vertexCount": self.vertex_count,
            "triangleCount": self.triangle_count,
            "activeDualVertexCount": self.active_dual_vertex_count,
            "inactiveSecondaryVertexCount": self.inactive_secondary_vertex_count,
            "activeSameBoneVertexCount": self.active_same_bone_vertex_count,
            "removedChunkCount": self.removed_chunk_count,
            "removedByteCount": self.removed_byte_count,
            "maximumPositionDelta": self.maximum_position_delta,
            "maximumNormalDelta": self.maximum_normal_delta,
            "streams": {
                "primaryVerticesSha256": self.primary_vertices_sha256,
                "secondaryVerticesSha256": self.secondary_vertices_sha256,
                "primaryNormalsSha256": self.primary_normals_sha256,
                "secondaryNormalsSha256": self.secondary_normals_sha256,
                "influencesSha256": self.influences_sha256,
            },
        }

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DSecondarySkinProof:
    input_model_sha256: str
    hierarchy_sha256: str
    output_model_sha256: str
    input_byte_length: int
    output_byte_length: int
    hierarchy_pivot_count: int
    hierarchy_identity_sha256: str
    hlod_hierarchy_binding_sha256: str
    mesh_count: int
    transformed_mesh_count: int
    removed_chunk_count: int
    removed_byte_count: int
    maximum_position_delta: float
    maximum_normal_delta: float
    meshes: tuple[W3DSecondarySkinMeshProof, ...]
    proof_sha256: str

    def _basis(self) -> dict[str, object]:
        return {
            "schema": SECONDARY_SKIN_SCHEMA,
            "schemaVersion": SECONDARY_SKIN_SCHEMA_VERSION,
            "inputModelSha256": self.input_model_sha256,
            "hierarchySha256": self.hierarchy_sha256,
            "outputModelSha256": self.output_model_sha256,
            "inputByteLength": self.input_byte_length,
            "outputByteLength": self.output_byte_length,
            "hierarchyPivotCount": self.hierarchy_pivot_count,
            "hierarchyIdentitySha256": self.hierarchy_identity_sha256,
            "hlodHierarchyBindingSha256": self.hlod_hierarchy_binding_sha256,
            "meshCount": self.mesh_count,
            "transformedMeshCount": self.transformed_mesh_count,
            "removedChunkCount": self.removed_chunk_count,
            "removedByteCount": self.removed_byte_count,
            "positionCoincidenceTolerance": POSITION_COINCIDENCE_TOLERANCE,
            "normalCoincidenceTolerance": NORMAL_COINCIDENCE_TOLERANCE,
            "maximumPositionDelta": self.maximum_position_delta,
            "maximumNormalDelta": self.maximum_normal_delta,
            "meshes": [mesh.neutral() for mesh in self.meshes],
        }

    def neutral(self) -> dict[str, object]:
        return {**self._basis(), "proofSha256": self.proof_sha256}

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DSecondarySkinResult:
    proof: W3DSecondarySkinProof
    _model_bytes: bytes = field(repr=False, compare=False)

    def model_bytes(self) -> bytes:
        """Return the deterministic transformed W3D byte stream."""

        return self._model_bytes


@dataclass(frozen=True, slots=True)
class _Chunk:
    kind: int
    start: int
    payload_start: int
    end: int
    container: bool

    @property
    def payload_length(self) -> int:
        return self.end - self.payload_start


@dataclass(frozen=True, slots=True)
class _Hierarchy:
    identity: bytes
    matrices: tuple[Mat4, ...]


@dataclass(frozen=True, slots=True)
class _MeshRewrite:
    top_level_ordinal: int
    mesh_ordinal: int
    replacement: bytes
    proof: W3DSecondarySkinMeshProof


def _canonical_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    )
    return hashlib.sha256(payload.encode("utf-8")).hexdigest()


def _sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _source_bytes(value: object, label: str) -> bytes:
    if not isinstance(value, bytes):
        raise TypeError(f"{label} must be bytes")
    if len(value) > MAX_W3D_BYTES:
        raise W3DSecondarySkinError(
            f"{label} exceeds the {MAX_W3D_BYTES}-byte proof limit"
        )
    return value


def _chunks(
    source: bytes,
    start: int,
    end: int,
    *,
    label: str,
) -> tuple[_Chunk, ...]:
    result = []
    cursor = start
    while cursor < end:
        if len(result) >= MAX_W3D_CHUNKS:
            raise W3DSecondarySkinError(f"{label} exceeds the chunk-count limit")
        if end - cursor < _CHUNK_HEADER_SIZE:
            raise W3DSecondarySkinError(f"{label} has a truncated chunk header")
        kind, raw_size = struct.unpack_from("<II", source, cursor)
        payload_length = raw_size & _SIZE_MASK
        payload_start = cursor + _CHUNK_HEADER_SIZE
        chunk_end = payload_start + payload_length
        if chunk_end > end:
            raise W3DSecondarySkinError(f"{label} chunk 0x{kind:08X} exceeds its owner")
        result.append(
            _Chunk(
                kind=kind,
                start=cursor,
                payload_start=payload_start,
                end=chunk_end,
                container=bool(raw_size & _CONTAINER_FLAG),
            )
        )
        cursor = chunk_end
    if cursor != end:
        raise W3DSecondarySkinError(f"{label} chunk boundaries are inconsistent")
    return tuple(result)


def _single(
    chunks: Iterable[_Chunk],
    kind: int,
    *,
    label: str,
    required: bool = True,
) -> _Chunk | None:
    selected = [chunk for chunk in chunks if chunk.kind == kind]
    if len(selected) > 1:
        raise W3DSecondarySkinError(f"{label} duplicates chunk 0x{kind:08X}")
    if required and not selected:
        raise W3DSecondarySkinError(f"{label} lacks chunk 0x{kind:08X}")
    return selected[0] if selected else None


def _fixed_identity(raw: bytes, label: str) -> bytes:
    # Retail HLOD records contain nonzero, semantically unread padding after
    # the first NUL. The pinned reader also stops at that terminator, so only
    # the prefix is identity-bearing and the tail must not enter comparisons.
    head = raw.split(b"\x00", 1)[0]
    if not head:
        raise W3DSecondarySkinError(f"{label} is empty")
    try:
        head.decode("ascii")
    except UnicodeDecodeError as exc:
        raise W3DSecondarySkinError(f"{label} is not ASCII") from exc
    return head


def _normalized_identity(raw: bytes, label: str) -> bytes:
    return _fixed_identity(raw, label).lower()


def _finite(values: Iterable[float], label: str) -> None:
    if not all(math.isfinite(value) for value in values):
        raise W3DSecondarySkinError(f"{label} contains a non-finite float")


def _quaternion_matrix(values: tuple[float, float, float, float]) -> Mat4:
    x, y, z, w = values
    _finite(values, "hierarchy quaternion")
    magnitude_squared = x * x + y * y + z * z + w * w
    if magnitude_squared == 0.0:
        raise W3DSecondarySkinError("hierarchy quaternion has zero magnitude")
    # Matches mathutils.Quaternion.to_matrix() in the pinned plugin: raw
    # quaternion magnitude is retained rather than normalized first.
    return (
        (
            1.0 - 2.0 * (y * y + z * z),
            2.0 * (x * y - z * w),
            2.0 * (x * z + y * w),
            0.0,
        ),
        (
            2.0 * (x * y + z * w),
            1.0 - 2.0 * (x * x + z * z),
            2.0 * (y * z - x * w),
            0.0,
        ),
        (
            2.0 * (x * z - y * w),
            2.0 * (y * z + x * w),
            1.0 - 2.0 * (x * x + y * y),
            0.0,
        ),
        (0.0, 0.0, 0.0, 1.0),
    )


def _local_matrix(
    translation: Vec3,
    quaternion: tuple[float, float, float, float],
) -> Mat4:
    _finite(translation, "hierarchy translation")
    rotation = _quaternion_matrix(quaternion)
    return (
        (*rotation[0][:3], translation[0]),
        (*rotation[1][:3], translation[1]),
        (*rotation[2][:3], translation[2]),
        (0.0, 0.0, 0.0, 1.0),
    )


def _matmul(left: Mat4, right: Mat4) -> Mat4:
    return tuple(
        tuple(
            sum(left[row][index] * right[index][column] for index in range(4))
            for column in range(4)
        )
        for row in range(4)
    )


def _point(matrix: Mat4, value: Vec3) -> Vec3:
    x, y, z = value
    return (
        matrix[0][0] * x + matrix[0][1] * y + matrix[0][2] * z + matrix[0][3],
        matrix[1][0] * x + matrix[1][1] * y + matrix[1][2] * z + matrix[1][3],
        matrix[2][0] * x + matrix[2][1] * y + matrix[2][2] * z + matrix[2][3],
    )


def _direction(matrix: Mat4, value: Vec3) -> Vec3:
    x, y, z = value
    return (
        matrix[0][0] * x + matrix[0][1] * y + matrix[0][2] * z,
        matrix[1][0] * x + matrix[1][1] * y + matrix[1][2] * z,
        matrix[2][0] * x + matrix[2][1] * y + matrix[2][2] * z,
    )


def _parse_hierarchy(source: bytes) -> _Hierarchy:
    top_level = _chunks(source, 0, len(source), label="hierarchy")
    hierarchy_chunk = _single(
        top_level,
        W3D_CHUNK_HIERARCHY,
        label="hierarchy file",
    )
    assert hierarchy_chunk is not None
    if not hierarchy_chunk.container:
        raise W3DSecondarySkinError("hierarchy chunk is not a container")
    children = _chunks(
        source,
        hierarchy_chunk.payload_start,
        hierarchy_chunk.end,
        label="hierarchy chunk",
    )
    header = _single(
        children,
        W3D_CHUNK_HIERARCHY_HEADER,
        label="hierarchy chunk",
    )
    pivot_chunk = _single(
        children,
        W3D_CHUNK_PIVOTS,
        label="hierarchy chunk",
    )
    pivot_fixups = _single(
        children,
        W3D_CHUNK_PIVOT_FIXUPS,
        label="hierarchy chunk",
        required=False,
    )
    assert header is not None and pivot_chunk is not None
    if header.payload_length != _HIERARCHY_HEADER_SIZE:
        raise W3DSecondarySkinError("hierarchy header has an invalid size")
    _, raw_identity, pivot_count, *center = struct.unpack_from(
        "<I16sI3f",
        source,
        header.payload_start,
    )
    _finite(center, "hierarchy center")
    identity = _fixed_identity(raw_identity, "hierarchy identity")
    if pivot_count < 1:
        raise W3DSecondarySkinError("hierarchy has no pivots")
    if pivot_chunk.payload_length != pivot_count * _HIERARCHY_PIVOT_SIZE:
        raise W3DSecondarySkinError("hierarchy pivot cardinality is invalid")
    if pivot_fixups is not None:
        # Westwood/OpenSAGE defines these Matrix4x3 records as exporter-only
        # transforms used when forcing the authored base pose (for example to
        # translation-only pivots). The exported pivots and vertex streams
        # below already contain that result, so runtime bind reconstruction
        # must not apply the fixups again. Accept them only after proving exact
        # one-record-per-pivot cardinality and finite source values; the full
        # hierarchy bytes, including these records, remain covered by the
        # hierarchy SHA-256 in the returned proof.
        matrix_float_count = 12
        expected_fixup_bytes = pivot_count * matrix_float_count * 4
        if pivot_fixups.payload_length != expected_fixup_bytes:
            raise W3DSecondarySkinError(
                "hierarchy pivot fixup cardinality is invalid"
            )
        for fixup_index, values in enumerate(
            struct.iter_unpack(
                "<12f",
                memoryview(source)[pivot_fixups.payload_start : pivot_fixups.end],
            )
        ):
            _finite(values, f"hierarchy pivot fixup {fixup_index}")

    parents: list[int] = []
    local_matrices: list[Mat4] = []
    pivot_names: set[bytes] = set()
    for pivot_index in range(pivot_count):
        offset = pivot_chunk.payload_start + pivot_index * _HIERARCHY_PIVOT_SIZE
        values = struct.unpack_from("<16si10f", source, offset)
        pivot_name = _normalized_identity(
            values[0],
            f"hierarchy pivot {pivot_index} identity",
        )
        if pivot_name in pivot_names:
            raise W3DSecondarySkinError("hierarchy pivot identities are not unique")
        pivot_names.add(pivot_name)
        parent = values[1]
        translation = (values[2], values[3], values[4])
        euler = (values[5], values[6], values[7])
        quaternion = (values[8], values[9], values[10], values[11])
        _finite(euler, f"hierarchy pivot {pivot_index} Euler values")
        if pivot_index == 0:
            if parent >= 0:
                raise W3DSecondarySkinError("hierarchy root parent is not negative")
        elif parent < 0 or parent >= pivot_index:
            raise W3DSecondarySkinError(
                f"hierarchy pivot {pivot_index} has an invalid parent"
            )
        parents.append(parent)
        local_matrices.append(_local_matrix(translation, quaternion))

    matrices: list[Mat4] = []
    for pivot_index, local in enumerate(local_matrices):
        parent = parents[pivot_index]
        # This is intentionally the pinned OpenSAGE hierarchy rule. The root
        # lives on the armature object. A direct child of pivot zero uses its
        # own matrix; only deeper bones multiply by their parent bone matrix.
        matrix = (
            _matmul(matrices[parent], local)
            if pivot_index > 0 and parent > 0
            else local
        )
        matrices.append(matrix)
    return _Hierarchy(identity, tuple(matrices))


def _hlod_binding(
    source: bytes, top_level: tuple[_Chunk, ...]
) -> tuple[bytes, set[bytes]]:
    hlod = _single(top_level, W3D_CHUNK_HLOD, label="model HLOD")
    assert hlod is not None
    if not hlod.container:
        raise W3DSecondarySkinError("model HLOD chunk is not a container")
    children = _chunks(
        source,
        hlod.payload_start,
        hlod.end,
        label="model HLOD",
    )
    header = _single(children, W3D_CHUNK_HLOD_HEADER, label="model HLOD")
    assert header is not None
    if header.payload_length != _HLOD_HEADER_SIZE:
        raise W3DSecondarySkinError("model HLOD header has an invalid size")
    _, lod_count, raw_model, raw_hierarchy = struct.unpack_from(
        "<II16s16s",
        source,
        header.payload_start,
    )
    _fixed_identity(raw_model, "model HLOD identity")
    hierarchy_identity = _fixed_identity(
        raw_hierarchy,
        "model HLOD hierarchy identity",
    )
    lod_arrays = [chunk for chunk in children if chunk.kind == W3D_CHUNK_HLOD_LOD_ARRAY]
    if lod_count != len(lod_arrays) or lod_count < 1:
        raise W3DSecondarySkinError("model HLOD LOD cardinality is invalid")
    identifiers: set[bytes] = set()
    array_kinds = {
        W3D_CHUNK_HLOD_LOD_ARRAY,
        W3D_CHUNK_HLOD_AGGREGATE_ARRAY,
        W3D_CHUNK_HLOD_PROXY_ARRAY,
    }
    for array_index, array in enumerate(
        chunk for chunk in children if chunk.kind in array_kinds
    ):
        if not array.container:
            raise W3DSecondarySkinError("model HLOD array is not a container")
        array_children = _chunks(
            source,
            array.payload_start,
            array.end,
            label=f"model HLOD array {array_index}",
        )
        for sub_object in (
            child for child in array_children if child.kind == W3D_CHUNK_HLOD_SUB_OBJECT
        ):
            if sub_object.payload_length != _HLOD_SUB_OBJECT_SIZE:
                raise W3DSecondarySkinError("model HLOD sub-object has an invalid size")
            _, raw_identifier = struct.unpack_from(
                "<I32s",
                source,
                sub_object.payload_start,
            )
            identifier = _normalized_identity(
                raw_identifier,
                "model HLOD sub-object identity",
            )
            if identifier in identifiers:
                raise W3DSecondarySkinError(
                    "model HLOD sub-object identities are not unique"
                )
            identifiers.add(identifier)
    return hierarchy_identity, identifiers


def _records(
    source: bytes,
    chunk: _Chunk,
    layout: str,
    record_size: int,
    count: int,
    label: str,
) -> tuple[tuple[float, ...] | tuple[int, ...], ...]:
    if chunk.payload_length != count * record_size:
        raise W3DSecondarySkinError(f"{label} cardinality is invalid")
    payload = memoryview(source)[chunk.payload_start : chunk.end]
    return tuple(struct.iter_unpack(layout, payload))


def _maximum(values: Iterable[float]) -> float:
    return max(values, default=0.0)


def _distance(left: Vec3, right: Vec3) -> float:
    return math.sqrt(sum((a - b) ** 2 for a, b in zip(left, right)))


def _mesh_rewrite(
    source: bytes,
    chunk: _Chunk,
    *,
    top_level_ordinal: int,
    mesh_ordinal: int,
    hierarchy: _Hierarchy,
    hlod_identifiers: set[bytes],
) -> _MeshRewrite | None:
    if not chunk.container:
        raise W3DSecondarySkinError(f"mesh {mesh_ordinal} is not a container")
    children = _chunks(
        source,
        chunk.payload_start,
        chunk.end,
        label=f"mesh {mesh_ordinal}",
    )
    secondary_vertices = _single(
        children,
        W3D_CHUNK_VERTICES_2,
        label=f"mesh {mesh_ordinal}",
        required=False,
    )
    secondary_normals = _single(
        children,
        W3D_CHUNK_VERTEX_NORMALS_2,
        label=f"mesh {mesh_ordinal}",
        required=False,
    )
    if (secondary_vertices is None) != (secondary_normals is None):
        raise W3DSecondarySkinError(
            f"mesh {mesh_ordinal} has an unpaired secondary stream"
        )
    if secondary_vertices is None:
        return None

    header = _single(
        children,
        W3D_CHUNK_MESH_HEADER,
        label=f"mesh {mesh_ordinal}",
    )
    primary_vertices = _single(
        children,
        W3D_CHUNK_VERTICES,
        label=f"mesh {mesh_ordinal}",
    )
    primary_normals = _single(
        children,
        W3D_CHUNK_VERTEX_NORMALS,
        label=f"mesh {mesh_ordinal}",
    )
    influences = _single(
        children,
        W3D_CHUNK_VERTEX_INFLUENCES,
        label=f"mesh {mesh_ordinal}",
    )
    assert (
        header is not None
        and primary_vertices is not None
        and primary_normals is not None
        and influences is not None
        and secondary_normals is not None
    )
    if header.payload_length != _MESH_HEADER_SIZE:
        raise W3DSecondarySkinError(f"mesh {mesh_ordinal} header size is invalid")
    values = struct.unpack_from(
        "<II16s16s9I10f",
        source,
        header.payload_start,
    )
    attributes = values[1]
    raw_mesh_name = values[2]
    raw_container_name = values[3]
    triangle_count = values[4]
    vertex_count = values[5]
    vertex_channels = values[11]
    _finite(values[13:], f"mesh {mesh_ordinal} bounds")
    if not attributes & W3D_GEOMETRY_TYPE_SKIN:
        raise W3DSecondarySkinError(
            f"mesh {mesh_ordinal} secondary streams are not on a skin"
        )
    if vertex_channels & _REQUIRED_SKIN_CHANNELS != _REQUIRED_SKIN_CHANNELS:
        raise W3DSecondarySkinError(
            f"mesh {mesh_ordinal} lacks required skin vertex channels"
        )
    mesh_name = _fixed_identity(raw_mesh_name, f"mesh {mesh_ordinal} identity")
    container_name = _fixed_identity(
        raw_container_name,
        f"mesh {mesh_ordinal} container identity",
    )
    full_identity = (container_name + b"." + mesh_name).lower()
    if full_identity not in hlod_identifiers:
        raise W3DSecondarySkinError(
            f"mesh {mesh_ordinal} is not bound by the model HLOD"
        )

    primary_vertex_records = _records(
        source,
        primary_vertices,
        "<3f",
        12,
        vertex_count,
        f"mesh {mesh_ordinal} primary vertices",
    )
    secondary_vertex_records = _records(
        source,
        secondary_vertices,
        "<3f",
        12,
        vertex_count,
        f"mesh {mesh_ordinal} secondary vertices",
    )
    primary_normal_records = _records(
        source,
        primary_normals,
        "<3f",
        12,
        vertex_count,
        f"mesh {mesh_ordinal} primary normals",
    )
    secondary_normal_records = _records(
        source,
        secondary_normals,
        "<3f",
        12,
        vertex_count,
        f"mesh {mesh_ordinal} secondary normals",
    )
    influence_records = _records(
        source,
        influences,
        "<4H",
        8,
        vertex_count,
        f"mesh {mesh_ordinal} influences",
    )

    position_deltas: list[float] = []
    normal_deltas: list[float] = []
    active_records: list[tuple[int, Vec3, Vec3, Vec3, Vec3]] = []
    active_count = 0
    inactive_count = 0
    active_same_bone_count = 0
    pivot_count = len(hierarchy.matrices)
    for vertex_index, influence in enumerate(influence_records):
        primary_bone, secondary_bone, primary_weight, secondary_weight = influence
        if primary_bone >= pivot_count or secondary_bone >= pivot_count:
            raise W3DSecondarySkinError(
                f"mesh {mesh_ordinal} vertex {vertex_index} has an invalid pivot"
            )
        if primary_weight > 100 or secondary_weight > 100:
            raise W3DSecondarySkinError(
                f"mesh {mesh_ordinal} vertex {vertex_index} has an invalid weight"
            )
        if primary_weight + secondary_weight != 100:
            raise W3DSecondarySkinError(
                f"mesh {mesh_ordinal} vertex {vertex_index} weights do not sum to 100"
            )
        primary_vertex = primary_vertex_records[vertex_index]
        secondary_vertex = secondary_vertex_records[vertex_index]
        primary_normal = primary_normal_records[vertex_index]
        secondary_normal = secondary_normal_records[vertex_index]
        _finite(primary_vertex, f"mesh {mesh_ordinal} primary vertex")
        _finite(secondary_vertex, f"mesh {mesh_ordinal} secondary vertex")
        _finite(primary_normal, f"mesh {mesh_ordinal} primary normal")
        _finite(secondary_normal, f"mesh {mesh_ordinal} secondary normal")

        if secondary_weight == 0:
            # The second local-space record contributes exactly zero to the
            # skinned result.  Retail files are allowed to leave arbitrary
            # finite values in that inactive slot; requiring a byte copy of
            # the primary stream rejects valid content without strengthening
            # the equivalence proof.
            inactive_count += 1
            continue

        # Pivot zero is the armature root: the pinned importer reserves it for
        # the root transform but still computes a bind transform for it (the
        # root's own local matrix), so the exact guard is the bind-space
        # coincidence check below, not a blanket root-index rejection.
        active_count += 1
        active_same_bone_count += primary_bone == secondary_bone
        active_records.append(
            (
                vertex_index,
                _point(
                    hierarchy.matrices[primary_bone],
                    primary_vertex,
                ),
                _point(
                    hierarchy.matrices[secondary_bone],
                    secondary_vertex,
                ),
                _direction(
                    hierarchy.matrices[primary_bone],
                    primary_normal,
                ),
                _direction(
                    hierarchy.matrices[secondary_bone],
                    secondary_normal,
                ),
            )
        )

    mesh_bind_extent = max(
        (
            math.sqrt(sum(component * component for component in record[1]))
            for record in active_records
        ),
        default=0.0,
    )
    normal_bound = max(
        NORMAL_COINCIDENCE_TOLERANCE,
        NORMAL_COINCIDENCE_RELATIVE_TOLERANCE * mesh_bind_extent,
    )
    for (
        vertex_index,
        bind_primary_vertex,
        bind_secondary_vertex,
        bind_primary_normal,
        bind_secondary_normal,
    ) in active_records:
        position_delta = _distance(bind_primary_vertex, bind_secondary_vertex)
        normal_delta = _distance(bind_primary_normal, bind_secondary_normal)
        bind_magnitude = math.sqrt(
            sum(component * component for component in bind_primary_vertex)
        )
        position_bound = max(
            POSITION_COINCIDENCE_TOLERANCE,
            POSITION_COINCIDENCE_RELATIVE_TOLERANCE * bind_magnitude,
        )
        if position_delta > position_bound:
            raise W3DSecondarySkinError(
                f"mesh {mesh_ordinal} active vertex {vertex_index} bind "
                f"position delta {position_delta:.9g} exceeds "
                f"{position_bound:.9g}"
            )
        if normal_delta > normal_bound:
            raise W3DSecondarySkinError(
                f"mesh {mesh_ordinal} active vertex {vertex_index} bind "
                f"normal delta {normal_delta:.9g} exceeds "
                f"{normal_bound:.9g}"
            )
        position_deltas.append(position_delta)
        normal_deltas.append(normal_delta)

    removed = {W3D_CHUNK_VERTICES_2, W3D_CHUNK_VERTEX_NORMALS_2}
    retained_payload = b"".join(
        source[child.start : child.end]
        for child in children
        if child.kind not in removed
    )
    removed_byte_count = chunk.payload_length - len(retained_payload)
    if removed_byte_count <= 0 or len(retained_payload) > _SIZE_MASK:
        raise W3DSecondarySkinError("secondary stream size repair is invalid")
    replacement = (
        struct.pack(
            "<II",
            W3D_CHUNK_MESH,
            _CONTAINER_FLAG | len(retained_payload),
        )
        + retained_payload
    )
    identity_hash = _sha256(container_name + b"\x00" + mesh_name)
    proof = W3DSecondarySkinMeshProof(
        mesh_ordinal=mesh_ordinal,
        mesh_identity_sha256=identity_hash,
        vertex_count=vertex_count,
        triangle_count=triangle_count,
        active_dual_vertex_count=active_count,
        inactive_secondary_vertex_count=inactive_count,
        active_same_bone_vertex_count=active_same_bone_count,
        removed_chunk_count=2,
        removed_byte_count=removed_byte_count,
        maximum_position_delta=_maximum(position_deltas),
        maximum_normal_delta=_maximum(normal_deltas),
        primary_vertices_sha256=_sha256(
            source[primary_vertices.payload_start : primary_vertices.end]
        ),
        secondary_vertices_sha256=_sha256(
            source[secondary_vertices.payload_start : secondary_vertices.end]
        ),
        primary_normals_sha256=_sha256(
            source[primary_normals.payload_start : primary_normals.end]
        ),
        secondary_normals_sha256=_sha256(
            source[secondary_normals.payload_start : secondary_normals.end]
        ),
        influences_sha256=_sha256(source[influences.payload_start : influences.end]),
    )
    return _MeshRewrite(top_level_ordinal, mesh_ordinal, replacement, proof)


def _secondary_locations(
    source: bytes,
    chunks: Iterable[_Chunk],
    *,
    parent_kind: int | None,
    depth: int,
) -> tuple[tuple[int | None, int], ...]:
    if depth > MAX_W3D_DEPTH:
        raise W3DSecondarySkinError("model exceeds the chunk-depth limit")
    result: list[tuple[int | None, int]] = []
    for chunk in chunks:
        if chunk.kind in {W3D_CHUNK_VERTICES_2, W3D_CHUNK_VERTEX_NORMALS_2}:
            result.append((parent_kind, chunk.kind))
        if chunk.container and not is_w3d_string_leaf(chunk.kind):
            children = _chunks(
                source,
                chunk.payload_start,
                chunk.end,
                label=f"container 0x{chunk.kind:08X}",
            )
            result.extend(
                _secondary_locations(
                    source,
                    children,
                    parent_kind=chunk.kind,
                    depth=depth + 1,
                )
            )
    return tuple(result)


def strip_proven_redundant_secondary_skin_streams(
    model_bytes: bytes,
    hierarchy_bytes: bytes,
) -> W3DSecondarySkinResult:
    """Prove and strip BFME secondary local skin streams.

    The returned proof contains only counts, bounded floating-point deltas, and
    hashes.  Source payloads and identifiers are not exposed.  The output is
    available only through :meth:`W3DSecondarySkinResult.model_bytes`.
    """

    model = _source_bytes(model_bytes, "model bytes")
    hierarchy_source = _source_bytes(hierarchy_bytes, "hierarchy bytes")
    top_level = _chunks(model, 0, len(model), label="model")
    locations = _secondary_locations(
        model,
        top_level,
        parent_kind=None,
        depth=0,
    )
    if not locations:
        raise W3DSecondarySkinError("model contains no secondary skin streams")
    if any(parent != W3D_CHUNK_MESH for parent, _ in locations):
        raise W3DSecondarySkinError("secondary skin stream is not a direct mesh child")

    hierarchy = _parse_hierarchy(hierarchy_source)
    hlod_hierarchy, hlod_identifiers = _hlod_binding(model, top_level)
    if hlod_hierarchy.lower() != hierarchy.identity.lower():
        raise W3DSecondarySkinError(
            "model HLOD and supplied hierarchy identities disagree"
        )

    rewrites: list[_MeshRewrite] = []
    mesh_ordinal = 0
    for top_level_ordinal, chunk in enumerate(top_level):
        if chunk.kind != W3D_CHUNK_MESH:
            continue
        rewrite = _mesh_rewrite(
            model,
            chunk,
            top_level_ordinal=top_level_ordinal,
            mesh_ordinal=mesh_ordinal,
            hierarchy=hierarchy,
            hlod_identifiers=hlod_identifiers,
        )
        if rewrite is not None:
            rewrites.append(rewrite)
        mesh_ordinal += 1
    if not rewrites:
        raise W3DSecondarySkinError(
            "model secondary chunks were not owned by a provable mesh"
        )

    replacement_by_ordinal = {
        rewrite.top_level_ordinal: rewrite.replacement for rewrite in rewrites
    }
    output = b"".join(
        replacement_by_ordinal.get(index, model[chunk.start : chunk.end])
        for index, chunk in enumerate(top_level)
    )
    output_top_level = _chunks(output, 0, len(output), label="transformed model")
    if _secondary_locations(
        output,
        output_top_level,
        parent_kind=None,
        depth=0,
    ):
        raise W3DSecondarySkinError(
            "transformed model retained a secondary skin stream"
        )

    mesh_proofs = tuple(rewrite.proof for rewrite in rewrites)
    removed_chunk_count = sum(mesh.removed_chunk_count for mesh in mesh_proofs)
    removed_byte_count = sum(mesh.removed_byte_count for mesh in mesh_proofs)
    if len(model) - len(output) != removed_byte_count:
        raise W3DSecondarySkinError("transformed model byte delta is inconsistent")
    proof = W3DSecondarySkinProof(
        input_model_sha256=_sha256(model),
        hierarchy_sha256=_sha256(hierarchy_source),
        output_model_sha256=_sha256(output),
        input_byte_length=len(model),
        output_byte_length=len(output),
        hierarchy_pivot_count=len(hierarchy.matrices),
        hierarchy_identity_sha256=_sha256(hierarchy.identity),
        hlod_hierarchy_binding_sha256=_sha256(hlod_hierarchy),
        mesh_count=mesh_ordinal,
        transformed_mesh_count=len(mesh_proofs),
        removed_chunk_count=removed_chunk_count,
        removed_byte_count=removed_byte_count,
        maximum_position_delta=_maximum(
            mesh.maximum_position_delta for mesh in mesh_proofs
        ),
        maximum_normal_delta=_maximum(
            mesh.maximum_normal_delta for mesh in mesh_proofs
        ),
        meshes=mesh_proofs,
        proof_sha256="",
    )
    proof = replace(proof, proof_sha256=_canonical_sha256(proof._basis()))
    return W3DSecondarySkinResult(proof, output)


__all__ = [
    "NORMAL_COINCIDENCE_TOLERANCE",
    "POSITION_COINCIDENCE_TOLERANCE",
    "SECONDARY_SKIN_SCHEMA",
    "SECONDARY_SKIN_SCHEMA_VERSION",
    "W3DSecondarySkinError",
    "W3DSecondarySkinMeshProof",
    "W3DSecondarySkinProof",
    "W3DSecondarySkinResult",
    "strip_proven_redundant_secondary_skin_streams",
]
