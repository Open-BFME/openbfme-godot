"""Fail-closed skin/hierarchy checks for the pinned W3D Blender importer.

The pinned importer cannot faithfully represent a nonzero skin weight targeting
pivot zero, because that pivot lives on the armature object rather than as a
Blender bone.  It also reads but does not apply W3D pivot-fixup matrices.  This
module proves those lossy cases are absent before a W3D job reaches Blender.

Evidence and errors are deliberately path- and name-free.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
import struct
from typing import Literal


W3D_SKIN_SAFETY_SCHEMA = "openbfme.w3d-skin-safety-proof"
W3D_SKIN_SAFETY_VERSION = 0

SKIN_ROOT_PIVOT_INFLUENCE_UNSUPPORTED = "skin-root-pivot-influence-unsupported"
HIERARCHY_PIVOT_FIXUP_UNSUPPORTED = "hierarchy-pivot-fixup-unsupported"
SKIN_SAFETY_PROOF_REJECTED = "skin-safety-proof-rejected"

MAX_W3D_BYTES = 512 * 1024 * 1024
MAX_W3D_CHUNKS = 1_000_000

W3D_CHUNK_MESH = 0x00000000
W3D_CHUNK_VERTEX_INFLUENCES = 0x0000000E
W3D_CHUNK_MESH_HEADER = 0x0000001F
W3D_CHUNK_HIERARCHY = 0x00000100
W3D_CHUNK_HIERARCHY_HEADER = 0x00000101
W3D_CHUNK_PIVOTS = 0x00000102
W3D_CHUNK_PIVOT_FIXUPS = 0x00000103

W3D_GEOMETRY_TYPE_SKIN = 0x00020000

_CHUNK_HEADER_SIZE = 8
_CONTAINER_FLAG = 0x80000000
_SIZE_MASK = 0x7FFFFFFF
_MESH_HEADER_SIZE = 116
_HIERARCHY_HEADER_SIZE = 36
_HIERARCHY_PIVOT_SIZE = 60


class W3DSkinSafetyError(ValueError):
    """Path-free rejection with a canonical owner and terminal reason."""

    def __init__(
        self,
        reason_code: str,
        owner: Literal["model", "hierarchy"],
        *,
        active_primary_root_count: int = 0,
        active_secondary_root_count: int = 0,
        pivot_fixup_chunk_count: int = 0,
    ) -> None:
        super().__init__("W3D skin safety proof rejected")
        self.reason_code = reason_code
        self.owner = owner
        self.active_primary_root_count = active_primary_root_count
        self.active_secondary_root_count = active_secondary_root_count
        self.pivot_fixup_chunk_count = pivot_fixup_chunk_count


@dataclass(frozen=True, slots=True)
class W3DSkinSafetyProof:
    model_sha256: str
    hierarchy_sha256: str | None
    skin_mesh_count: int
    influence_record_count: int
    hierarchy_pivot_count: int
    pivot_fixup_chunk_count: int
    active_primary_root_influence_count: int
    active_secondary_root_influence_count: int
    proof_sha256: str
    schema: str = field(init=False, default=W3D_SKIN_SAFETY_SCHEMA)
    schema_version: int = field(init=False, default=W3D_SKIN_SAFETY_VERSION)

    def _neutral(self, *, include_proof_sha256: bool) -> dict[str, object]:
        hashes: dict[str, object] = {
            "modelSha256": self.model_sha256,
            "hierarchySha256": self.hierarchy_sha256,
        }
        if include_proof_sha256:
            hashes["proofSha256"] = self.proof_sha256
        return {
            "schema": self.schema,
            "schemaVersion": self.schema_version,
            "hashes": hashes,
            "summary": {
                "skinMeshCount": self.skin_mesh_count,
                "influenceRecordCount": self.influence_record_count,
                "hierarchyPivotCount": self.hierarchy_pivot_count,
                "pivotFixupChunkCount": self.pivot_fixup_chunk_count,
                "activePrimaryRootInfluenceCount": (
                    self.active_primary_root_influence_count
                ),
                "activeSecondaryRootInfluenceCount": (
                    self.active_secondary_root_influence_count
                ),
            },
        }

    def proof_hash_basis(self) -> dict[str, object]:
        return self._neutral(include_proof_sha256=False)

    def neutral(self) -> dict[str, object]:
        return self._neutral(include_proof_sha256=True)


@dataclass(frozen=True, slots=True)
class _Chunk:
    kind: int
    payload_start: int
    end: int
    container: bool

    @property
    def payload_length(self) -> int:
        return self.end - self.payload_start


def _canonical_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        ensure_ascii=True,
        separators=(",", ":"),
        sort_keys=True,
    ).encode("ascii")
    return hashlib.sha256(payload).hexdigest()


def _sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _source_bytes(
    value: object,
    owner: Literal["model", "hierarchy"],
) -> bytes:
    if not isinstance(value, bytes):
        raise TypeError("W3D skin safety input must be bytes")
    if len(value) > MAX_W3D_BYTES:
        raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, owner)
    return value


def _chunks(
    source: bytes,
    start: int,
    end: int,
    *,
    owner: Literal["model", "hierarchy"],
) -> tuple[_Chunk, ...]:
    result = []
    cursor = start
    while cursor < end:
        if len(result) >= MAX_W3D_CHUNKS or end - cursor < _CHUNK_HEADER_SIZE:
            raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, owner)
        kind, raw_size = struct.unpack_from("<II", source, cursor)
        payload_start = cursor + _CHUNK_HEADER_SIZE
        chunk_end = payload_start + (raw_size & _SIZE_MASK)
        if chunk_end > end:
            raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, owner)
        result.append(
            _Chunk(
                kind=kind,
                payload_start=payload_start,
                end=chunk_end,
                container=bool(raw_size & _CONTAINER_FLAG),
            )
        )
        cursor = chunk_end
    if cursor != end:
        raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, owner)
    return tuple(result)


def _single(
    chunks: tuple[_Chunk, ...],
    kind: int,
    *,
    owner: Literal["model", "hierarchy"],
    required: bool,
) -> _Chunk | None:
    selected = [chunk for chunk in chunks if chunk.kind == kind]
    if len(selected) > 1 or (required and not selected):
        raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, owner)
    return selected[0] if selected else None


def _hierarchy_pivot_count(source: bytes) -> int:
    top_level = _chunks(source, 0, len(source), owner="hierarchy")
    hierarchy = _single(
        top_level,
        W3D_CHUNK_HIERARCHY,
        owner="hierarchy",
        required=True,
    )
    assert hierarchy is not None
    if not hierarchy.container:
        raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, "hierarchy")
    children = _chunks(
        source,
        hierarchy.payload_start,
        hierarchy.end,
        owner="hierarchy",
    )
    header = _single(
        children,
        W3D_CHUNK_HIERARCHY_HEADER,
        owner="hierarchy",
        required=True,
    )
    pivots = _single(
        children,
        W3D_CHUNK_PIVOTS,
        owner="hierarchy",
        required=True,
    )
    fixups = [chunk for chunk in children if chunk.kind == W3D_CHUNK_PIVOT_FIXUPS]
    if fixups:
        raise W3DSkinSafetyError(
            HIERARCHY_PIVOT_FIXUP_UNSUPPORTED,
            "hierarchy",
            pivot_fixup_chunk_count=len(fixups),
        )
    assert header is not None and pivots is not None
    if header.payload_length != _HIERARCHY_HEADER_SIZE:
        raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, "hierarchy")
    pivot_count = struct.unpack_from("<I", source, header.payload_start + 20)[0]
    if pivot_count < 1 or pivots.payload_length != pivot_count * _HIERARCHY_PIVOT_SIZE:
        raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, "hierarchy")
    for pivot_index in range(pivot_count):
        offset = pivots.payload_start + pivot_index * _HIERARCHY_PIVOT_SIZE
        parent = struct.unpack_from("<i", source, offset + 16)[0]
        if (pivot_index == 0 and parent >= 0) or (
            pivot_index > 0 and (parent < 0 or parent >= pivot_index)
        ):
            raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, "hierarchy")
    return pivot_count


def prove_w3d_skin_safety(
    model_bytes: bytes,
    hierarchy_bytes: bytes | None,
) -> W3DSkinSafetyProof:
    """Prove a model/hierarchy pair avoids pinned-importer skin loss."""

    model = _source_bytes(model_bytes, "model")
    hierarchy = (
        None if hierarchy_bytes is None else _source_bytes(hierarchy_bytes, "hierarchy")
    )
    hierarchy_pivot_count = 0
    if hierarchy is not None:
        try:
            hierarchy_pivot_count = _hierarchy_pivot_count(hierarchy)
        except W3DSkinSafetyError as error:
            if error.owner == "model":
                raise W3DSkinSafetyError(
                    error.reason_code,
                    "hierarchy",
                    active_primary_root_count=error.active_primary_root_count,
                    active_secondary_root_count=error.active_secondary_root_count,
                    pivot_fixup_chunk_count=error.pivot_fixup_chunk_count,
                ) from None
            raise

    top_level = _chunks(model, 0, len(model), owner="model")
    skin_mesh_count = 0
    influence_record_count = 0
    active_primary_root_count = 0
    active_secondary_root_count = 0
    for mesh in (chunk for chunk in top_level if chunk.kind == W3D_CHUNK_MESH):
        if not mesh.container:
            raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, "model")
        children = _chunks(model, mesh.payload_start, mesh.end, owner="model")
        header = _single(
            children,
            W3D_CHUNK_MESH_HEADER,
            owner="model",
            required=True,
        )
        assert header is not None
        if header.payload_length != _MESH_HEADER_SIZE:
            raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, "model")
        attributes = struct.unpack_from("<I", model, header.payload_start + 4)[0]
        if not attributes & W3D_GEOMETRY_TYPE_SKIN:
            continue
        skin_mesh_count += 1
        if hierarchy is None:
            raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, "model")
        vertex_count = struct.unpack_from("<I", model, header.payload_start + 44)[0]
        influences = _single(
            children,
            W3D_CHUNK_VERTEX_INFLUENCES,
            owner="model",
            required=True,
        )
        assert influences is not None
        if influences.payload_length != vertex_count * 8:
            raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, "model")
        for primary, secondary, primary_weight, secondary_weight in struct.iter_unpack(
            "<4H",
            memoryview(model)[influences.payload_start : influences.end],
        ):
            if (
                primary >= hierarchy_pivot_count
                or secondary >= hierarchy_pivot_count
                or primary_weight > 100
                or secondary_weight > 100
                or primary_weight + secondary_weight != 100
            ):
                raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, "model")
            active_primary_root_count += primary == 0 and primary_weight > 0
            active_secondary_root_count += secondary == 0 and secondary_weight > 0
            influence_record_count += 1

    if active_primary_root_count or active_secondary_root_count:
        raise W3DSkinSafetyError(
            SKIN_ROOT_PIVOT_INFLUENCE_UNSUPPORTED,
            "model",
            active_primary_root_count=active_primary_root_count,
            active_secondary_root_count=active_secondary_root_count,
        )
    provisional = W3DSkinSafetyProof(
        model_sha256=_sha256(model),
        hierarchy_sha256=None if hierarchy is None else _sha256(hierarchy),
        skin_mesh_count=skin_mesh_count,
        influence_record_count=influence_record_count,
        hierarchy_pivot_count=hierarchy_pivot_count,
        pivot_fixup_chunk_count=0,
        active_primary_root_influence_count=0,
        active_secondary_root_influence_count=0,
        proof_sha256="",
    )
    return W3DSkinSafetyProof(
        model_sha256=provisional.model_sha256,
        hierarchy_sha256=provisional.hierarchy_sha256,
        skin_mesh_count=provisional.skin_mesh_count,
        influence_record_count=provisional.influence_record_count,
        hierarchy_pivot_count=provisional.hierarchy_pivot_count,
        pivot_fixup_chunk_count=provisional.pivot_fixup_chunk_count,
        active_primary_root_influence_count=(
            provisional.active_primary_root_influence_count
        ),
        active_secondary_root_influence_count=(
            provisional.active_secondary_root_influence_count
        ),
        proof_sha256=_canonical_sha256(provisional.proof_hash_basis()),
    )


def validate_w3d_skin_safety_proof(proof: W3DSkinSafetyProof) -> None:
    """Validate a safe proof without rereading source payloads."""

    if type(proof) is not W3DSkinSafetyProof:
        raise TypeError("proof must be a W3DSkinSafetyProof")
    hashes = (proof.model_sha256, proof.proof_sha256)
    if proof.hierarchy_sha256 is not None:
        hashes += (proof.hierarchy_sha256,)
    counts = (
        proof.skin_mesh_count,
        proof.influence_record_count,
        proof.hierarchy_pivot_count,
        proof.pivot_fixup_chunk_count,
        proof.active_primary_root_influence_count,
        proof.active_secondary_root_influence_count,
    )
    if (
        proof.schema != W3D_SKIN_SAFETY_SCHEMA
        or proof.schema_version != W3D_SKIN_SAFETY_VERSION
        or any(
            not isinstance(value, str)
            or len(value) != 64
            or any(character not in "0123456789abcdef" for character in value)
            for value in hashes
        )
        or any(type(value) is not int or value < 0 for value in counts)
        or proof.pivot_fixup_chunk_count != 0
        or proof.active_primary_root_influence_count != 0
        or proof.active_secondary_root_influence_count != 0
        or proof.proof_sha256 != _canonical_sha256(proof.proof_hash_basis())
    ):
        raise W3DSkinSafetyError(SKIN_SAFETY_PROOF_REJECTED, "model")


__all__ = [
    "HIERARCHY_PIVOT_FIXUP_UNSUPPORTED",
    "SKIN_ROOT_PIVOT_INFLUENCE_UNSUPPORTED",
    "SKIN_SAFETY_PROOF_REJECTED",
    "W3D_SKIN_SAFETY_SCHEMA",
    "W3D_SKIN_SAFETY_VERSION",
    "W3DSkinSafetyError",
    "W3DSkinSafetyProof",
    "prove_w3d_skin_safety",
    "validate_w3d_skin_safety_proof",
]
