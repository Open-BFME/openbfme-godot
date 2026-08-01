"""Fail-closed planning from W3D containers to owner-bound stream evidence.

The lower-level geometry, animation, and auxiliary stream modules deliberately
accept isolated payloads and caller-supplied cardinalities.  This module
supplies the missing, equally bounded bridge: it proves chunk containment from
:mod:`w3d_metadata`, binds a stream to its exact owner container, derives
cardinalities only from decoded owner headers, and then invokes those payload
decoders.

The returned plan is an attestation, not a converter result.  It contains no
payload bytes, filesystem paths, or authored W3D identifiers.  Primary and
secondary geometry streams remain separate, duplicate owner headers are never
resolved by preference, and no result claims GLB, render, skeleton, or
animation-playback completion.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass, field
import hashlib
import json
import struct
from typing import Literal, TypeAlias

from .paths import safe_relative_parts
from .w3d_animation_streams import (
    PrimaryImportDisposition,
    SUPPORTED_ANIMATION_STREAM_CHUNK_IDS,
    W3DAnimationChannelAttestation,
    W3DAnimationStreamDecodeError,
    W3DAnimationStreamUnsupportedError,
    W3D_CHUNK_ANIMATION_BIT_CHANNEL,
    W3D_CHUNK_ANIMATION_CHANNEL,
    W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
    W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL,
    W3D_CHUNK_COMPRESSED_BIT_CHANNEL,
    decode_animation_stream,
)
from .w3d_auxiliary_streams import (
    SUPPORTED_AUXILIARY_STREAM_CHUNK_IDS,
    W3DAabTreeHeaderRecord,
    W3DAuxiliaryStreamAttestation,
    W3DAuxiliaryStreamDecodeError,
    W3DAuxiliaryStreamUnsupportedError,
    W3D_CHUNK_AABTREE_HEADER,
    W3D_CHUNK_AABTREE_NODES,
    W3D_CHUNK_AABTREE_POLYINDICES,
    W3D_CHUNK_DIFFUSE_COLOR_VALUES,
    W3D_CHUNK_PIVOT_FIXUPS,
    decode_auxiliary_stream,
)
from .w3d_emitter_streams import (
    SUPPORTED_EMITTER_CHILD_CHUNK_IDS,
    UNSUPPORTED_EMITTER_CHILD_CHUNK_IDS,
    W3DEmitterChildAttestation,
    W3DEmitterDecodeError,
    W3DEmitterUnsupportedError,
    W3D_CHUNK_EMITTER,
    W3D_CHUNK_EMITTER_HEADER,
    decode_emitter_container,
)
from .w3d_geometry_streams import (
    SUPPORTED_GEOMETRY_STREAM_CHUNK_IDS,
    W3DGeometryStreamAttestation,
    W3DGeometryStreamDecodeError,
    W3DGeometryStreamUnsupportedError,
    decode_geometry_stream,
)
from .w3d_metadata import (
    W3DAnimationHeader,
    W3DChunkMetadata,
    W3DHierarchyHeader,
    W3DMeshHeader,
    W3DMetadata,
    W3DMetadataWarning,
    scan_w3d_metadata,
)


W3D_DECODE_PLAN_SCHEMA = "openbfme.w3d-decode-plan"
W3D_DECODE_PLAN_SCHEMA_VERSION = 2
DEFAULT_VIRTUAL_LABEL = "caller-owned.w3d"

_CHUNK_HEADER_SIZE = 8
_SUBCHUNK_FLAG = 0x80000000
_SIZE_MASK = 0x7FFFFFFF
_MESH_CONTAINER = 0x00000000
_MESH_HEADER = 0x0000001F
_MESH_HEADER_SIZE = 116
_PRELIT_CONTAINERS = frozenset({0x00000023, 0x00000024, 0x00000025, 0x00000026})
_MATERIAL_PASS_CONTAINER = 0x00000038
_AABB_TREE_CONTAINER = 0x00000090
_HIERARCHY_CONTAINER = 0x00000100
_HIERARCHY_HEADER = 0x00000101
_HIERARCHY_PIVOTS = 0x00000102
_HIERARCHY_HEADER_SIZE = 36
_HIERARCHY_PIVOT_RECORD_SIZE = 60
_RAW_ANIMATION_CONTAINER = 0x00000200
_RAW_ANIMATION_HEADER = 0x00000201
_COMPRESSED_ANIMATION_CONTAINER = 0x00000280
_COMPRESSED_ANIMATION_HEADER = 0x00000281
_ANIMATION_HEADER_SIZE = 44
_ANIMATION_CONTAINERS = frozenset(
    {_RAW_ANIMATION_CONTAINER, _COMPRESSED_ANIMATION_CONTAINER}
)
_RAW_ANIMATION_STREAMS = frozenset(
    {W3D_CHUNK_ANIMATION_CHANNEL, W3D_CHUNK_ANIMATION_BIT_CHANNEL}
)
_COMPRESSED_ANIMATION_STREAMS = frozenset(
    {
        W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
        W3D_CHUNK_COMPRESSED_BIT_CHANNEL,
        W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL,
    }
)
_NON_BLOCKING_STATES = frozenset({"observed"})
_IGNORED_CLASSIFICATION_WARNING_CODES = frozenset(
    {"unsupported-chunk", "unknown-chunk"}
)

StreamDomain: TypeAlias = Literal["geometry", "animation", "auxiliary", "emitter"]
TerminalState: TypeAlias = Literal["observed", "unresolved", "unsupported", "damaged"]
ExternalHierarchyState: TypeAlias = Literal[
    "resolved", "ambiguous", "damaged", "missing"
]
ExternalHierarchyResolutionMode: TypeAlias = Literal[
    "sibling-path", "identifier-prefix-path", "global-no-context-compatibility"
]

# The engine's hierarchy pathing rule: OpenSAGE ``Bfme2W3dPathResolver``
# (PathResolvers.Bfme2W3d) maps a hierarchy name to
# ``art/w3d/<name[:2]>/<name>.w3d`` from the asset root.  The sibling rule
# only coincides with it when the referencing file lives in the same
# two-letter shard directory as the skeleton.
_IDENTIFIER_PREFIX_PATH_ROOT = ("art", "w3d")
StreamAttestation: TypeAlias = (
    W3DGeometryStreamAttestation
    | W3DAnimationChannelAttestation
    | W3DAuxiliaryStreamAttestation
    | W3DEmitterChildAttestation
)


class W3DDecodePlanError(ValueError):
    """Raised for an invalid caller contract, not malformed W3D contents."""


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and value == value.casefold()
        and all(character in "0123456789abcdef" for character in value)
    )


def _hierarchy_lookup_sha256(identifier: str) -> str:
    return hashlib.sha256(identifier.casefold().encode("utf-8")).hexdigest()


def _normalized_virtual_path(value: str) -> str:
    return "/".join(safe_relative_parts(value))


def _virtual_path_lookup_sha256(virtual_path: str) -> str:
    normalized = _normalized_virtual_path(virtual_path)
    return hashlib.sha256(normalized.casefold().encode("utf-8")).hexdigest()


def _virtual_path_context_sha256(virtual_path: str) -> str:
    parts = safe_relative_parts(virtual_path)
    parent = "/".join(parts[:-1])
    return hashlib.sha256(parent.casefold().encode("utf-8")).hexdigest()


def _sibling_candidate_lookup_sha256(
    virtual_path: str,
    hierarchy_identifier: str,
) -> str:
    """Hash the pinned importer's case-insensitive sibling candidate path."""

    parts = safe_relative_parts(virtual_path)
    try:
        identifier_parts = safe_relative_parts(hierarchy_identifier)
    except ValueError:
        identifier_parts = ()
    if len(identifier_parts) != 1:
        # Unsafe authored references remain payload-free missing lookups.  The
        # impossible marker cannot collide with any safe manifest path hash.
        marker = "\0invalid-sibling-reference\0" + hierarchy_identifier.casefold()
        return hashlib.sha256(marker.encode("utf-8")).hexdigest()
    candidate = "/".join((*parts[:-1], f"{identifier_parts[0]}.w3d"))
    return hashlib.sha256(candidate.casefold().encode("utf-8")).hexdigest()


def _identifier_prefix_candidate_lookup_sha256(hierarchy_identifier: str) -> str:
    """Hash the engine's identifier-prefix candidate path exactly.

    The candidate is ``art/w3d/<identifier[:2]>/<identifier>.w3d``, the rule
    OpenSAGE's ``Bfme2W3dPathResolver`` uses.  An unsafe or sub-two-character
    identifier cannot form the engine path and remains a payload-free missing
    lookup; the impossible marker cannot collide with any safe manifest path
    hash.
    """

    try:
        identifier_parts = safe_relative_parts(hierarchy_identifier)
    except ValueError:
        identifier_parts = ()
    if len(identifier_parts) != 1 or len(identifier_parts[0]) < 2:
        marker = "\0invalid-prefix-reference\0" + hierarchy_identifier.casefold()
        return hashlib.sha256(marker.encode("utf-8")).hexdigest()
    name = identifier_parts[0]
    candidate = "/".join(
        (*_IDENTIFIER_PREFIX_PATH_ROOT, name[:2], f"{name}.w3d")
    )
    return hashlib.sha256(candidate.casefold().encode("utf-8")).hexdigest()


@dataclass(frozen=True, slots=True)
class _W3DExternalHierarchyObservation:
    """One source-bound hierarchy header/pivot observation.

    Authored identifiers and payload bytes are discarded before this value is
    created.  The exact header and pivot-record bytes remain bound by hashes.
    """

    lookup_sha256: str
    source_sha256: str
    source_ordinal: int
    valid: bool
    pivot_count: int | None
    version_major: int | None
    version_minor: int | None
    header_payload_sha256: str | None
    pivot_records_sha256: str | None
    pivot_records_byte_length: int | None
    failure_code: str | None
    evidence_sha256: str

    def identity(self) -> dict[str, object]:
        return {
            "lookupSha256": self.lookup_sha256,
            "sourceSha256": self.source_sha256,
            "sourceOrdinal": self.source_ordinal,
            "valid": self.valid,
            "pivotCount": self.pivot_count,
            "version": [self.version_major, self.version_minor],
            "headerPayloadSha256": self.header_payload_sha256,
            "pivotRecordsSha256": self.pivot_records_sha256,
            "pivotRecordsByteLength": self.pivot_records_byte_length,
            "failureCode": self.failure_code,
        }

    @property
    def variant_sha256(self) -> str | None:
        if not self.valid:
            return None
        return _canonical_sha256(
            {
                "schema": "openbfme.w3d-external-hierarchy-variant",
                "schemaVersion": 0,
                "pivotCount": self.pivot_count,
                "version": [self.version_major, self.version_minor],
                "headerPayloadSha256": self.header_payload_sha256,
                "pivotRecordsSha256": self.pivot_records_sha256,
                "pivotRecordsByteLength": self.pivot_records_byte_length,
            }
        )


@dataclass(frozen=True, slots=True)
class _W3DExternalHierarchyOccurrence:
    observation: _W3DExternalHierarchyObservation
    manifest_entry_count: int
    occurrence_sha256: str

    def identity(self) -> dict[str, object]:
        return {
            "observation": self.observation.identity(),
            "observationEvidenceSha256": self.observation.evidence_sha256,
            "manifestEntryCount": self.manifest_entry_count,
        }


@dataclass(frozen=True, slots=True)
class _W3DExternalHierarchySourceObservations:
    source_sha256: str
    manifest_entry_count: int
    observations: tuple[_W3DExternalHierarchyObservation, ...]
    sibling_path_lookup_sha256s: tuple[str, ...] = ()
    collection_failure_code: str | None = None


@dataclass(frozen=True, slots=True)
class _W3DExternalHierarchySiblingObservation:
    """Hierarchy structure evidence with authored identifiers discarded."""

    source_sha256: str
    source_ordinal: int
    valid: bool
    pivot_count: int | None
    version_major: int | None
    version_minor: int | None
    header_payload_sha256: str | None
    pivot_records_sha256: str | None
    pivot_records_byte_length: int | None
    failure_code: str | None
    evidence_sha256: str

    def identity(self) -> dict[str, object]:
        return {
            "sourceSha256": self.source_sha256,
            "sourceOrdinal": self.source_ordinal,
            "valid": self.valid,
            "pivotCount": self.pivot_count,
            "version": [self.version_major, self.version_minor],
            "headerPayloadSha256": self.header_payload_sha256,
            "pivotRecordsSha256": self.pivot_records_sha256,
            "pivotRecordsByteLength": self.pivot_records_byte_length,
            "failureCode": self.failure_code,
        }


@dataclass(frozen=True, slots=True)
class _W3DExternalHierarchySiblingCandidate:
    """One manifest file addressable by a case-folded virtual-path digest."""

    sibling_path_lookup_sha256: str
    source_sha256: str
    observations: tuple[_W3DExternalHierarchySiblingObservation, ...]
    collection_failure_code: str | None
    evidence_sha256: str

    def identity(self) -> dict[str, object]:
        return {
            "siblingPathLookupSha256": self.sibling_path_lookup_sha256,
            "sourceSha256": self.source_sha256,
            "observations": [item.identity() for item in self.observations],
            "observationEvidenceSha256s": [
                item.evidence_sha256 for item in self.observations
            ],
            "collectionFailureCode": self.collection_failure_code,
        }


@dataclass(frozen=True, slots=True)
class _W3DExternalHierarchyMatch:
    state: ExternalHierarchyState
    pivot_count: int | None
    lookup_sha256: str
    resolution_mode: ExternalHierarchyResolutionMode
    evidence_sha256: str


@dataclass(frozen=True, slots=True)
class _W3DExternalHierarchyResolverSeal:
    manifest_sha256: str
    manifest_aggregate_sha256: str
    inventory_sha256: str
    occurrences: tuple[_W3DExternalHierarchyOccurrence, ...]
    sibling_candidates: tuple[_W3DExternalHierarchySiblingCandidate, ...]
    resolver_sha256: str


@dataclass(frozen=True, slots=True)
class W3DExternalHierarchyResolver:
    """Immutable, hash-sealed hierarchy evidence for one verified corpus.

    Verified corpora use the pinned importer's case-insensitive sibling-file
    rule and store only virtual-path digests.  The identifier-global mode is a
    deliberately isolated compatibility fallback for direct API callers that
    build a resolver without manifest path evidence.
    """

    manifest_sha256: str
    manifest_aggregate_sha256: str
    inventory_sha256: str
    occurrences: tuple[_W3DExternalHierarchyOccurrence, ...]
    sibling_candidates: tuple[_W3DExternalHierarchySiblingCandidate, ...]
    resolver_sha256: str
    _seal: _W3DExternalHierarchyResolverSeal | None = field(
        default=None,
        init=False,
        repr=False,
        compare=False,
    )

    def _identity(self) -> dict[str, object]:
        return {
            "schema": "openbfme.w3d-external-hierarchy-resolver",
            "schemaVersion": 1,
            "resolutionMode": self.resolution_mode,
            "manifestSha256": self.manifest_sha256,
            "manifestAggregateSha256": self.manifest_aggregate_sha256,
            "inventorySha256": self.inventory_sha256,
            "occurrences": [item.identity() for item in self.occurrences],
            "siblingCandidates": [item.identity() for item in self.sibling_candidates],
        }

    @property
    def resolution_mode(self) -> ExternalHierarchyResolutionMode:
        if self.sibling_candidates:
            return "sibling-path"
        return "global-no-context-compatibility"

    def _matching_occurrences(
        self, lookup_sha256: str
    ) -> tuple[_W3DExternalHierarchyOccurrence, ...]:
        return tuple(
            item
            for item in self.occurrences
            if item.observation.lookup_sha256 == lookup_sha256
        )

    def resolve(self, identifier: str) -> _W3DExternalHierarchyMatch:
        """Resolve without path context for existing direct API callers only."""

        return self.resolve_global_no_context(identifier)

    def resolve_global_no_context(
        self,
        identifier: str,
    ) -> _W3DExternalHierarchyMatch:
        lookup_sha256 = _hierarchy_lookup_sha256(identifier)
        matches = self._matching_occurrences(lookup_sha256)
        if not identifier or not matches:
            state: ExternalHierarchyState = "missing"
            pivot_count = None
        elif any(not item.observation.valid for item in matches):
            state = "damaged"
            pivot_count = None
        else:
            variants = {
                item.observation.variant_sha256: item.observation.pivot_count
                for item in matches
            }
            if len(variants) != 1:
                state = "ambiguous"
                pivot_count = None
            else:
                state = "resolved"
                pivot_count = next(iter(variants.values()))
        evidence_sha256 = _canonical_sha256(
            {
                "schema": "openbfme.w3d-external-hierarchy-match",
                "schemaVersion": 1,
                "resolverSha256": self.resolver_sha256,
                "resolutionMode": "global-no-context-compatibility",
                "lookupSha256": lookup_sha256,
                "state": state,
                "pivotCount": pivot_count,
                "occurrenceSha256s": [item.occurrence_sha256 for item in matches],
            }
        )
        return _W3DExternalHierarchyMatch(
            state=state,
            pivot_count=pivot_count,
            lookup_sha256=lookup_sha256,
            resolution_mode="global-no-context-compatibility",
            evidence_sha256=evidence_sha256,
        )

    def resolve_sibling(
        self,
        identifier: str,
        virtual_path: str,
    ) -> _W3DExternalHierarchyMatch:
        """Resolve ``<parent(virtual_path)>/<identifier>.w3d`` exactly."""

        return self._resolve_candidate_path(
            identifier,
            lookup_sha256=_sibling_candidate_lookup_sha256(virtual_path, identifier),
            resolution_mode="sibling-path",
        )

    def resolve_identifier_prefix(
        self,
        identifier: str,
    ) -> _W3DExternalHierarchyMatch:
        """Resolve ``art/w3d/<identifier[:2]>/<identifier>.w3d`` exactly.

        This is the engine's own hierarchy pathing rule (OpenSAGE
        ``Bfme2W3dPathResolver``).  It consults only the same sealed
        candidate table as :meth:`resolve_sibling`; a candidate authored
        nowhere in the corpus stays a missing lookup and is never
        approximated by a similarly named file.
        """

        return self._resolve_candidate_path(
            identifier,
            lookup_sha256=_identifier_prefix_candidate_lookup_sha256(identifier),
            resolution_mode="identifier-prefix-path",
        )

    def _resolve_candidate_path(
        self,
        identifier: str,
        *,
        lookup_sha256: str,
        resolution_mode: ExternalHierarchyResolutionMode,
    ) -> _W3DExternalHierarchyMatch:
        matches = tuple(
            item
            for item in self.sibling_candidates
            if item.sibling_path_lookup_sha256 == lookup_sha256
        )
        if not identifier or not matches:
            state: ExternalHierarchyState = "missing"
            pivot_count = None
        elif len(matches) != 1:
            state = "ambiguous"
            pivot_count = None
        else:
            candidate = matches[0]
            observations = candidate.observations
            if candidate.collection_failure_code is not None or not observations:
                state = "damaged"
                pivot_count = None
            elif len(observations) != 1:
                state = "ambiguous"
                pivot_count = None
            elif not observations[0].valid:
                state = "damaged"
                pivot_count = None
            else:
                state = "resolved"
                pivot_count = observations[0].pivot_count
        evidence_sha256 = _canonical_sha256(
            {
                "schema": "openbfme.w3d-external-hierarchy-match",
                "schemaVersion": 1,
                "resolverSha256": self.resolver_sha256,
                "resolutionMode": resolution_mode,
                "lookupSha256": lookup_sha256,
                "state": state,
                "pivotCount": pivot_count,
                "candidateEvidenceSha256s": [item.evidence_sha256 for item in matches],
            }
        )
        return _W3DExternalHierarchyMatch(
            state=state,
            pivot_count=pivot_count,
            lookup_sha256=lookup_sha256,
            resolution_mode=resolution_mode,
            evidence_sha256=evidence_sha256,
        )

    def neutral(self) -> dict[str, object]:
        states = {"resolved": 0, "ambiguous": 0, "damaged": 0}
        if self.resolution_mode == "sibling-path":
            for candidate in self.sibling_candidates:
                if (
                    candidate.collection_failure_code is not None
                    or not candidate.observations
                ):
                    states["damaged"] += 1
                elif len(candidate.observations) != 1:
                    states["ambiguous"] += 1
                elif candidate.observations[0].valid:
                    states["resolved"] += 1
                else:
                    states["damaged"] += 1
            entry_count = len(self.sibling_candidates)
            occurrence_count = sum(
                len(item.observations) for item in self.sibling_candidates
            )
        else:
            lookup_sha256s = sorted(
                {item.observation.lookup_sha256 for item in self.occurrences}
            )
            for lookup_sha256 in lookup_sha256s:
                occurrences = self._matching_occurrences(lookup_sha256)
                if any(not item.observation.valid for item in occurrences):
                    states["damaged"] += 1
                elif (
                    len({item.observation.variant_sha256 for item in occurrences}) == 1
                ):
                    states["resolved"] += 1
                else:
                    states["ambiguous"] += 1
            entry_count = len(lookup_sha256s)
            occurrence_count = sum(
                item.manifest_entry_count for item in self.occurrences
            )
        return {
            "schema": "openbfme.w3d-external-hierarchy-resolver",
            "schemaVersion": 1,
            "resolutionMode": self.resolution_mode,
            "entryCount": entry_count,
            "occurrenceCount": occurrence_count,
            "states": states,
            "hashes": {
                "manifestSha256": self.manifest_sha256,
                "manifestAggregateSha256": self.manifest_aggregate_sha256,
                "inventorySha256": self.inventory_sha256,
                "resolverSha256": self.resolver_sha256,
            },
        }

    json_ready = neutral


def _canonical_sha256(value: object) -> str:
    encoded = json.dumps(
        value,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


@dataclass(frozen=True, slots=True)
class W3DDecodedStream:
    """One structurally decoded stream and its payload-free attestation.

    Animation streams additionally carry the pinned primary importer's binding
    disposition.  A skipped invalid-pivot channel remains structurally decoded
    but is never represented as owner-bound.
    """

    domain: StreamDomain
    chunk_id: int
    chunk_header_offset: int
    owner_container_header_offset: int
    owner_header_offset: int
    payload_byte_length: int
    payload_sha256: str
    attestation_sha256: str
    external_hierarchy_proof_sha256: str | None
    primary_import_disposition: PrimaryImportDisposition | None
    attestation: StreamAttestation

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "domain": self.domain,
            "chunkId": self.chunk_id,
            "chunkIdHex": f"0x{self.chunk_id:08X}",
            "chunkHeaderOffset": self.chunk_header_offset,
            "ownerContainerHeaderOffset": self.owner_container_header_offset,
            "ownerHeaderOffset": self.owner_header_offset,
            "payloadByteLength": self.payload_byte_length,
            "payloadSha256": self.payload_sha256,
            "attestationSha256": self.attestation_sha256,
            "attestation": self.attestation.neutral(),
        }
        if self.external_hierarchy_proof_sha256 is not None:
            result["externalHierarchyProofSha256"] = (
                self.external_hierarchy_proof_sha256
            )
        if self.primary_import_disposition is not None:
            result["primaryImportDisposition"] = self.primary_import_disposition
        return result

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DChunkTerminal:
    """Explicit terminal for a chunk not represented by a decoded stream."""

    state: TerminalState
    code: str
    chunk_id: int | None
    chunk_header_offset: int | None
    owner_container_header_offset: int | None
    declared_payload_size: int | None
    available_payload_size: int | None
    payload_sha256: str | None
    evidence_sha256: str

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "state": self.state,
            "code": self.code,
            "evidenceSha256": self.evidence_sha256,
        }
        if self.chunk_id is not None:
            result["chunkId"] = self.chunk_id
            result["chunkIdHex"] = f"0x{self.chunk_id:08X}"
        if self.chunk_header_offset is not None:
            result["chunkHeaderOffset"] = self.chunk_header_offset
        if self.owner_container_header_offset is not None:
            result["ownerContainerHeaderOffset"] = self.owner_container_header_offset
        if self.declared_payload_size is not None:
            result["declaredPayloadSize"] = self.declared_payload_size
        if self.available_payload_size is not None:
            result["availablePayloadSize"] = self.available_payload_size
        if self.payload_sha256 is not None:
            result["payloadSha256"] = self.payload_sha256
        return result

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DStreamDecodePlan:
    """Immutable, deterministic container-to-stream planning evidence.

    ``stream_decode_complete`` covers two provably different situations that
    ``stream_decode_vacuously_complete`` keeps distinguishable: every target
    stream decoded, or there was nothing to stream-decode and nothing
    blocking (a pure skeleton or channel-less animation container).  The
    vacuous case is additionally marked in the sealed neutral form, so a
    reader of stored evidence can always tell the two apart.
    """

    source_byte_length: int
    source_sha256: str
    encountered_chunk_count: int
    target_stream_chunk_count: int
    decoded_streams: tuple[W3DDecodedStream, ...]
    terminals: tuple[W3DChunkTerminal, ...]
    stream_decode_complete: bool
    stream_decode_vacuously_complete: bool
    path_context_sha256: str | None
    external_hierarchy_resolver_sha256: str | None
    plan_sha256: str

    @property
    def geometry_streams(self) -> tuple[W3DDecodedStream, ...]:
        return tuple(item for item in self.decoded_streams if item.domain == "geometry")

    @property
    def animation_streams(self) -> tuple[W3DDecodedStream, ...]:
        return tuple(
            item for item in self.decoded_streams if item.domain == "animation"
        )

    @property
    def primary_bound_animation_streams(self) -> tuple[W3DDecodedStream, ...]:
        return tuple(
            item
            for item in self.animation_streams
            if item.primary_import_disposition == "bound"
        )

    @property
    def primary_skipped_animation_streams(self) -> tuple[W3DDecodedStream, ...]:
        return tuple(
            item
            for item in self.animation_streams
            if item.primary_import_disposition == "skipped-invalid-pivot"
        )

    @property
    def auxiliary_streams(self) -> tuple[W3DDecodedStream, ...]:
        return tuple(
            item for item in self.decoded_streams if item.domain == "auxiliary"
        )

    @property
    def emitter_streams(self) -> tuple[W3DDecodedStream, ...]:
        return tuple(item for item in self.decoded_streams if item.domain == "emitter")

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "schema": W3D_DECODE_PLAN_SCHEMA,
            "schemaVersion": W3D_DECODE_PLAN_SCHEMA_VERSION,
            "sourceByteLength": self.source_byte_length,
            "sourceSha256": self.source_sha256,
            "encounteredChunkCount": self.encountered_chunk_count,
            "targetStreamChunkCount": self.target_stream_chunk_count,
            "decodedStreamCount": len(self.decoded_streams),
            "geometryStreamCount": len(self.geometry_streams),
            "animationStreamCount": len(self.animation_streams),
            "primaryBoundAnimationStreamCount": len(
                self.primary_bound_animation_streams
            ),
            "primarySkippedAnimationStreamCount": len(
                self.primary_skipped_animation_streams
            ),
            "auxiliaryStreamCount": len(self.auxiliary_streams),
            "emitterStreamCount": len(self.emitter_streams),
            "decodedStreams": [item.neutral() for item in self.decoded_streams],
            "terminals": [item.neutral() for item in self.terminals],
            "streamDecodeComplete": self.stream_decode_complete,
            "planSha256": self.plan_sha256,
        }
        if self.stream_decode_vacuously_complete:
            result["streamDecodeCompleteVacuous"] = True
        if self.external_hierarchy_resolver_sha256 is not None:
            result["externalHierarchyResolverSha256"] = (
                self.external_hierarchy_resolver_sha256
            )
        if self.path_context_sha256 is not None:
            result["pathContextSha256"] = self.path_context_sha256
        return result

    json_ready = neutral


def _terminal(
    *,
    source: bytes,
    state: TerminalState,
    code: str,
    chunk: W3DChunkMetadata | None = None,
    owner_container_header_offset: int | None = None,
    evidence_offset: int | None = None,
) -> W3DChunkTerminal:
    payload_sha256: str | None = None
    if chunk is not None:
        start = chunk.payload_offset
        end = start + chunk.available_payload_size
        if 0 <= start <= end <= len(source):
            payload_sha256 = hashlib.sha256(source[start:end]).hexdigest()
    evidence = {
        "state": state,
        "code": code,
        "chunkId": None if chunk is None else chunk.chunk_id,
        "chunkHeaderOffset": None if chunk is None else chunk.header_offset,
        "ownerContainerHeaderOffset": owner_container_header_offset,
        "declaredPayloadSize": (None if chunk is None else chunk.declared_payload_size),
        "availablePayloadSize": (
            None if chunk is None else chunk.available_payload_size
        ),
        "payloadSha256": payload_sha256,
        "evidenceOffset": evidence_offset,
    }
    return W3DChunkTerminal(
        state=state,
        code=code,
        chunk_id=None if chunk is None else chunk.chunk_id,
        chunk_header_offset=None if chunk is None else chunk.header_offset,
        owner_container_header_offset=owner_container_header_offset,
        declared_payload_size=(None if chunk is None else chunk.declared_payload_size),
        available_payload_size=(
            None if chunk is None else chunk.available_payload_size
        ),
        payload_sha256=payload_sha256,
        evidence_sha256=_canonical_sha256(evidence),
    )


def _final_plan(
    *,
    source: bytes,
    encountered_chunk_count: int,
    target_stream_chunk_count: int,
    decoded_streams: tuple[W3DDecodedStream, ...],
    terminals: tuple[W3DChunkTerminal, ...],
    path_context_sha256: str | None = None,
    external_hierarchy_resolver_sha256: str | None = None,
) -> W3DStreamDecodePlan:
    blocker = any(item.state not in _NON_BLOCKING_STATES for item in terminals)
    # Vacuous completeness: nothing to stream-decode and nothing blocking.
    # The resolver still consumes such files (pure skeletons, channel-less
    # animation containers) as evidence; classifying them incomplete would
    # claim a defect that does not exist.  The case stays distinguishable
    # from "every target decoded": the sealed core carries an explicit
    # marker, emitted only when the completeness is vacuous so every
    # non-vacuous plan hash is unchanged by this schema addition.
    vacuous = (
        target_stream_chunk_count == 0 and not decoded_streams and not blocker
    )
    complete = vacuous or (
        target_stream_chunk_count > 0
        and len(decoded_streams) == target_stream_chunk_count
        and not blocker
    )
    core = {
        "schema": W3D_DECODE_PLAN_SCHEMA,
        "schemaVersion": W3D_DECODE_PLAN_SCHEMA_VERSION,
        "sourceByteLength": len(source),
        "sourceSha256": hashlib.sha256(source).hexdigest(),
        "encounteredChunkCount": encountered_chunk_count,
        "targetStreamChunkCount": target_stream_chunk_count,
        "decodedStreams": [item.neutral() for item in decoded_streams],
        "terminals": [item.neutral() for item in terminals],
        "streamDecodeComplete": complete,
    }
    if vacuous:
        core["streamDecodeCompleteVacuous"] = True
    if external_hierarchy_resolver_sha256 is not None:
        core["externalHierarchyResolverSha256"] = external_hierarchy_resolver_sha256
    if path_context_sha256 is not None:
        if not _is_sha256(path_context_sha256):
            raise W3DDecodePlanError("W3D decode-plan path context hash is invalid")
        core["pathContextSha256"] = path_context_sha256
    return W3DStreamDecodePlan(
        source_byte_length=len(source),
        source_sha256=core["sourceSha256"],  # type: ignore[arg-type]
        encountered_chunk_count=encountered_chunk_count,
        target_stream_chunk_count=target_stream_chunk_count,
        decoded_streams=decoded_streams,
        terminals=terminals,
        stream_decode_complete=complete,
        stream_decode_vacuously_complete=vacuous,
        path_context_sha256=path_context_sha256,
        external_hierarchy_resolver_sha256=(external_hierarchy_resolver_sha256),
        plan_sha256=_canonical_sha256(core),
    )


def _provenance_issue_codes(
    source: bytes,
    metadata: W3DMetadata,
) -> tuple[tuple[str, int | None], ...]:
    """Validate offsets independently before slicing any stream payload."""

    issues: list[tuple[str, int | None]] = []
    chunks = metadata.chunks
    offsets = [item.header_offset for item in chunks]
    if offsets != sorted(offsets):
        issues.append(("non-monotonic-chunk-provenance", None))
    if len(set(offsets)) != len(offsets):
        issues.append(("duplicate-chunk-header-offset", None))

    by_offset = {item.header_offset: item for item in chunks}
    siblings: dict[int | None, list[W3DChunkMetadata]] = defaultdict(list)
    for chunk in chunks:
        header = chunk.header_offset
        payload = chunk.payload_offset
        end = payload + chunk.available_payload_size
        if header < 0 or header + _CHUNK_HEADER_SIZE > len(source):
            issues.append(("out-of-bounds-chunk-header-provenance", header))
            continue
        if payload != header + _CHUNK_HEADER_SIZE or payload > len(source):
            issues.append(("invalid-chunk-payload-offset-provenance", header))
            continue
        if end < payload or end > len(source):
            issues.append(("out-of-bounds-chunk-payload-provenance", header))
            continue
        raw_chunk_id, raw_size = struct.unpack_from("<II", source, header)
        if raw_chunk_id != chunk.chunk_id:
            issues.append(("chunk-id-provenance-mismatch", header))
        if (raw_size & _SIZE_MASK) != chunk.declared_payload_size:
            issues.append(("chunk-size-provenance-mismatch", header))
        if bool(raw_size & _SUBCHUNK_FLAG) != chunk.has_subchunks_flag:
            issues.append(("chunk-container-flag-provenance-mismatch", header))

        parent_offset = chunk.parent_header_offset
        parent = None if parent_offset is None else by_offset.get(parent_offset)
        if parent_offset is not None and parent is None:
            issues.append(("missing-parent-chunk-provenance", header))
            continue
        if parent is None:
            if chunk.depth != 0:
                issues.append(("invalid-root-chunk-depth-provenance", header))
        else:
            parent_end = parent.payload_offset + parent.available_payload_size
            if chunk.depth != parent.depth + 1:
                issues.append(("invalid-child-chunk-depth-provenance", header))
            if header < parent.payload_offset or end > parent_end:
                issues.append(("out-of-bounds-child-provenance", header))
        siblings[parent_offset].append(chunk)

    for values in siblings.values():
        ordered = sorted(values, key=lambda item: item.header_offset)
        previous_end: int | None = None
        for chunk in ordered:
            end = chunk.payload_offset + chunk.available_payload_size
            if previous_end is not None and chunk.header_offset < previous_end:
                issues.append(
                    ("overlapping-sibling-chunk-provenance", chunk.header_offset)
                )
            previous_end = max(previous_end or end, end)
    return tuple(sorted(set(issues), key=lambda item: (item[1] or -1, item[0])))


def _warning_chunk(
    warning: W3DMetadataWarning,
    chunks: tuple[W3DChunkMetadata, ...],
) -> W3DChunkMetadata | None:
    candidates = [
        chunk
        for chunk in chunks
        if chunk.header_offset
        <= warning.offset
        <= chunk.payload_offset + chunk.available_payload_size
    ]
    if not candidates:
        return None
    return max(candidates, key=lambda item: (item.depth, item.header_offset))


def _structural_warning_terminals(
    source: bytes,
    metadata: W3DMetadata,
) -> tuple[tuple[W3DChunkTerminal, ...], frozenset[int]]:
    terminals: list[W3DChunkTerminal] = []
    damaged_offsets: set[int] = set()
    for warning in metadata.warnings:
        if warning.code in _IGNORED_CLASSIFICATION_WARNING_CODES:
            continue
        chunk = _warning_chunk(warning, metadata.chunks)
        if chunk is not None:
            damaged_offsets.add(chunk.header_offset)
        terminals.append(
            _terminal(
                source=source,
                state="damaged",
                code=f"metadata-{warning.code}",
                chunk=chunk,
                evidence_offset=warning.offset,
            )
        )
    for chunk in metadata.chunks:
        if chunk.truncated and chunk.header_offset not in damaged_offsets:
            damaged_offsets.add(chunk.header_offset)
            terminals.append(
                _terminal(
                    source=source,
                    state="damaged",
                    code="truncated-chunk-payload",
                    chunk=chunk,
                )
            )
    return tuple(terminals), frozenset(damaged_offsets)


def _ancestor(
    chunk: W3DChunkMetadata,
    by_offset: dict[int, W3DChunkMetadata],
    accepted_ids: frozenset[int],
) -> W3DChunkMetadata | None:
    parent_offset = chunk.parent_header_offset
    seen: set[int] = set()
    while parent_offset is not None:
        if parent_offset in seen:
            return None
        seen.add(parent_offset)
        parent = by_offset.get(parent_offset)
        if parent is None:
            return None
        if parent.chunk_id in accepted_ids:
            return parent
        parent_offset = parent.parent_header_offset
    return None


def _header_is_usable(
    header_offset: int,
    damaged_offsets: frozenset[int],
) -> bool:
    return header_offset not in damaged_offsets


def _header_has_exact_layout(
    *,
    by_offset: dict[int, W3DChunkMetadata],
    header_offset: int,
    accepted_chunk_ids: frozenset[int],
    expected_size: int,
) -> bool:
    chunk = by_offset.get(header_offset)
    return bool(
        chunk is not None
        and chunk.chunk_id in accepted_chunk_ids
        and not chunk.truncated
        and chunk.declared_payload_size == expected_size
        and chunk.available_payload_size == expected_size
    )


def _observation(
    *,
    lookup_sha256: str,
    source_sha256: str,
    source_ordinal: int,
    valid: bool,
    pivot_count: int | None,
    version_major: int | None,
    version_minor: int | None,
    header_payload_sha256: str | None,
    pivot_records_sha256: str | None,
    pivot_records_byte_length: int | None,
    failure_code: str | None,
) -> _W3DExternalHierarchyObservation:
    core = {
        "lookupSha256": lookup_sha256,
        "sourceSha256": source_sha256,
        "sourceOrdinal": source_ordinal,
        "valid": valid,
        "pivotCount": pivot_count,
        "version": [version_major, version_minor],
        "headerPayloadSha256": header_payload_sha256,
        "pivotRecordsSha256": pivot_records_sha256,
        "pivotRecordsByteLength": pivot_records_byte_length,
        "failureCode": failure_code,
    }
    return _W3DExternalHierarchyObservation(
        lookup_sha256=lookup_sha256,
        source_sha256=source_sha256,
        source_ordinal=source_ordinal,
        valid=valid,
        pivot_count=pivot_count,
        version_major=version_major,
        version_minor=version_minor,
        header_payload_sha256=header_payload_sha256,
        pivot_records_sha256=pivot_records_sha256,
        pivot_records_byte_length=pivot_records_byte_length,
        failure_code=failure_code,
        evidence_sha256=_canonical_sha256(core),
    )


def _observation_is_valid(
    observation: _W3DExternalHierarchyObservation,
) -> bool:
    if type(observation) is not _W3DExternalHierarchyObservation:
        return False
    if (
        not _is_sha256(observation.lookup_sha256)
        or not _is_sha256(observation.source_sha256)
        or isinstance(observation.source_ordinal, bool)
        or not isinstance(observation.source_ordinal, int)
        or observation.source_ordinal < 0
        or type(observation.valid) is not bool
        or not _is_sha256(observation.evidence_sha256)
    ):
        return False
    optional_hashes = (
        observation.header_payload_sha256,
        observation.pivot_records_sha256,
    )
    if any(value is not None and not _is_sha256(value) for value in optional_hashes):
        return False
    optional_ints = (
        observation.pivot_count,
        observation.version_major,
        observation.version_minor,
        observation.pivot_records_byte_length,
    )
    if any(
        value is not None
        and (isinstance(value, bool) or not isinstance(value, int) or value < 0)
        for value in optional_ints
    ):
        return False
    if observation.valid:
        if (
            observation.failure_code is not None
            or observation.pivot_count is None
            or observation.pivot_count <= 0
            or observation.version_major is None
            or observation.version_minor is None
            or observation.header_payload_sha256 is None
            or observation.pivot_records_sha256 is None
            or observation.pivot_records_byte_length
            != observation.pivot_count * _HIERARCHY_PIVOT_RECORD_SIZE
        ):
            return False
    elif not isinstance(observation.failure_code, str) or not observation.failure_code:
        return False
    return observation.evidence_sha256 == _canonical_sha256(observation.identity())


def _occurrence(
    observation: _W3DExternalHierarchyObservation,
    manifest_entry_count: int,
) -> _W3DExternalHierarchyOccurrence:
    core = {
        "observation": observation.identity(),
        "observationEvidenceSha256": observation.evidence_sha256,
        "manifestEntryCount": manifest_entry_count,
    }
    return _W3DExternalHierarchyOccurrence(
        observation=observation,
        manifest_entry_count=manifest_entry_count,
        occurrence_sha256=_canonical_sha256(core),
    )


def _sibling_observation(
    observation: _W3DExternalHierarchyObservation,
) -> _W3DExternalHierarchySiblingObservation:
    identifier_only_failure = observation.failure_code in {
        "empty-hierarchy-identifier",
        "ambiguous-source-hierarchy-identifier",
    }
    valid = observation.valid or identifier_only_failure
    failure_code = None if identifier_only_failure else observation.failure_code
    core = {
        "sourceSha256": observation.source_sha256,
        "sourceOrdinal": observation.source_ordinal,
        "valid": valid,
        "pivotCount": observation.pivot_count,
        "version": [observation.version_major, observation.version_minor],
        "headerPayloadSha256": observation.header_payload_sha256,
        "pivotRecordsSha256": observation.pivot_records_sha256,
        "pivotRecordsByteLength": observation.pivot_records_byte_length,
        "failureCode": failure_code,
    }
    return _W3DExternalHierarchySiblingObservation(
        source_sha256=observation.source_sha256,
        source_ordinal=observation.source_ordinal,
        valid=valid,
        pivot_count=observation.pivot_count,
        version_major=observation.version_major,
        version_minor=observation.version_minor,
        header_payload_sha256=observation.header_payload_sha256,
        pivot_records_sha256=observation.pivot_records_sha256,
        pivot_records_byte_length=observation.pivot_records_byte_length,
        failure_code=failure_code,
        evidence_sha256=_canonical_sha256(core),
    )


def _sibling_observation_is_valid(
    observation: _W3DExternalHierarchySiblingObservation,
) -> bool:
    if type(observation) is not _W3DExternalHierarchySiblingObservation:
        return False
    if (
        not _is_sha256(observation.source_sha256)
        or isinstance(observation.source_ordinal, bool)
        or not isinstance(observation.source_ordinal, int)
        or observation.source_ordinal < 0
        or type(observation.valid) is not bool
        or not _is_sha256(observation.evidence_sha256)
    ):
        return False
    optional_hashes = (
        observation.header_payload_sha256,
        observation.pivot_records_sha256,
    )
    if any(value is not None and not _is_sha256(value) for value in optional_hashes):
        return False
    optional_ints = (
        observation.pivot_count,
        observation.version_major,
        observation.version_minor,
        observation.pivot_records_byte_length,
    )
    if any(
        value is not None
        and (isinstance(value, bool) or not isinstance(value, int) or value < 0)
        for value in optional_ints
    ):
        return False
    if observation.valid:
        if (
            observation.failure_code is not None
            or observation.pivot_count is None
            or observation.pivot_count <= 0
            or observation.version_major is None
            or observation.version_minor is None
            or observation.header_payload_sha256 is None
            or observation.pivot_records_sha256 is None
            or observation.pivot_records_byte_length
            != observation.pivot_count * _HIERARCHY_PIVOT_RECORD_SIZE
        ):
            return False
    elif not isinstance(observation.failure_code, str) or not observation.failure_code:
        return False
    return observation.evidence_sha256 == _canonical_sha256(observation.identity())


def _sibling_candidate(
    *,
    sibling_path_lookup_sha256: str,
    source_sha256: str,
    observations: tuple[_W3DExternalHierarchySiblingObservation, ...],
    collection_failure_code: str | None,
) -> _W3DExternalHierarchySiblingCandidate:
    core = {
        "siblingPathLookupSha256": sibling_path_lookup_sha256,
        "sourceSha256": source_sha256,
        "observations": [item.identity() for item in observations],
        "observationEvidenceSha256s": [item.evidence_sha256 for item in observations],
        "collectionFailureCode": collection_failure_code,
    }
    return _W3DExternalHierarchySiblingCandidate(
        sibling_path_lookup_sha256=sibling_path_lookup_sha256,
        source_sha256=source_sha256,
        observations=observations,
        collection_failure_code=collection_failure_code,
        evidence_sha256=_canonical_sha256(core),
    )


def _sibling_candidate_is_valid(
    candidate: _W3DExternalHierarchySiblingCandidate,
) -> bool:
    if type(candidate) is not _W3DExternalHierarchySiblingCandidate:
        return False
    if (
        not _is_sha256(candidate.sibling_path_lookup_sha256)
        or not _is_sha256(candidate.source_sha256)
        or type(candidate.observations) is not tuple
        or not _is_sha256(candidate.evidence_sha256)
        or candidate.collection_failure_code is not None
        and (
            not isinstance(candidate.collection_failure_code, str)
            or not candidate.collection_failure_code
        )
    ):
        return False
    if candidate.collection_failure_code is not None and candidate.observations:
        return False
    if any(
        not _sibling_observation_is_valid(item)
        or item.source_sha256 != candidate.source_sha256
        for item in candidate.observations
    ):
        return False
    ordinals = [item.source_ordinal for item in candidate.observations]
    if ordinals != sorted(ordinals) or len(ordinals) != len(set(ordinals)):
        return False
    return candidate.evidence_sha256 == _canonical_sha256(candidate.identity())


def _validate_external_hierarchy_resolver(
    resolver: W3DExternalHierarchyResolver,
    *,
    allow_unsealed: bool = False,
) -> None:
    if type(resolver) is not W3DExternalHierarchyResolver:
        raise W3DDecodePlanError(
            "external hierarchy resolver must use the immutable typed contract"
        )
    seal = resolver._seal
    if seal is not None:
        if (
            type(seal) is _W3DExternalHierarchyResolverSeal
            and resolver.manifest_sha256 == seal.manifest_sha256
            and resolver.manifest_aggregate_sha256 == seal.manifest_aggregate_sha256
            and resolver.inventory_sha256 == seal.inventory_sha256
            and resolver.occurrences is seal.occurrences
            and resolver.sibling_candidates is seal.sibling_candidates
            and resolver.resolver_sha256 == seal.resolver_sha256
        ):
            return
        raise W3DDecodePlanError(
            "external hierarchy resolver integrity seal is inconsistent"
        )
    if not allow_unsealed:
        raise W3DDecodePlanError(
            "external hierarchy resolver integrity seal is missing"
        )
    if not all(
        _is_sha256(value)
        for value in (
            resolver.manifest_sha256,
            resolver.manifest_aggregate_sha256,
            resolver.inventory_sha256,
            resolver.resolver_sha256,
        )
    ):
        raise W3DDecodePlanError("external hierarchy resolver hashes are invalid")
    if type(resolver.occurrences) is not tuple:
        raise W3DDecodePlanError("external hierarchy resolver occurrences are invalid")
    if type(resolver.sibling_candidates) is not tuple:
        raise W3DDecodePlanError(
            "external hierarchy resolver sibling candidates are invalid"
        )
    if resolver.occurrences and resolver.sibling_candidates:
        raise W3DDecodePlanError(
            "external hierarchy resolver mixes sibling and global modes"
        )
    sort_keys: list[tuple[str, str, int, str]] = []
    for occurrence in resolver.occurrences:
        if type(occurrence) is not _W3DExternalHierarchyOccurrence:
            raise W3DDecodePlanError(
                "external hierarchy resolver occurrences are invalid"
            )
        observation = occurrence.observation
        if not _observation_is_valid(observation):
            raise W3DDecodePlanError(
                "external hierarchy resolver observation integrity failed"
            )
        if (
            isinstance(occurrence.manifest_entry_count, bool)
            or not isinstance(occurrence.manifest_entry_count, int)
            or occurrence.manifest_entry_count <= 0
            or not _is_sha256(occurrence.occurrence_sha256)
            or occurrence.occurrence_sha256 != _canonical_sha256(occurrence.identity())
        ):
            raise W3DDecodePlanError(
                "external hierarchy resolver occurrence integrity failed"
            )
        sort_keys.append(
            (
                observation.lookup_sha256,
                observation.source_sha256,
                observation.source_ordinal,
                observation.evidence_sha256,
            )
        )
    if sort_keys != sorted(sort_keys) or len(sort_keys) != len(set(sort_keys)):
        raise W3DDecodePlanError(
            "external hierarchy resolver occurrence ordering is invalid"
        )
    candidate_sort_keys: list[tuple[str, str, str]] = []
    for candidate in resolver.sibling_candidates:
        if not _sibling_candidate_is_valid(candidate):
            raise W3DDecodePlanError(
                "external hierarchy resolver sibling candidate integrity failed"
            )
        candidate_sort_keys.append(
            (
                candidate.sibling_path_lookup_sha256,
                candidate.source_sha256,
                candidate.evidence_sha256,
            )
        )
    if candidate_sort_keys != sorted(candidate_sort_keys) or len(
        {item[0] for item in candidate_sort_keys}
    ) != len(candidate_sort_keys):
        raise W3DDecodePlanError(
            "external hierarchy resolver sibling candidate ordering is invalid"
        )
    if resolver.resolver_sha256 != _canonical_sha256(resolver._identity()):
        raise W3DDecodePlanError("external hierarchy resolver integrity check failed")


def _build_external_hierarchy_resolver(
    *,
    manifest_sha256: str,
    manifest_aggregate_sha256: str,
    inventory_sha256: str,
    sources: tuple[_W3DExternalHierarchySourceObservations, ...],
) -> W3DExternalHierarchyResolver:
    for value in (manifest_sha256, manifest_aggregate_sha256, inventory_sha256):
        if not _is_sha256(value):
            raise W3DDecodePlanError(
                "external hierarchy resolver source identity is invalid"
            )
    occurrences: list[_W3DExternalHierarchyOccurrence] = []
    sibling_candidates: list[_W3DExternalHierarchySiblingCandidate] = []
    source_keys: list[str] = []
    path_modes: set[bool] = set()
    for item in sources:
        if type(item) is not _W3DExternalHierarchySourceObservations:
            raise W3DDecodePlanError("external hierarchy resolver sources are invalid")
        if (
            not _is_sha256(item.source_sha256)
            or isinstance(item.manifest_entry_count, bool)
            or not isinstance(item.manifest_entry_count, int)
            or item.manifest_entry_count <= 0
            or type(item.observations) is not tuple
            or type(item.sibling_path_lookup_sha256s) is not tuple
        ):
            raise W3DDecodePlanError("external hierarchy resolver sources are invalid")
        source_keys.append(item.source_sha256)
        has_sibling_paths = bool(item.sibling_path_lookup_sha256s)
        path_modes.add(has_sibling_paths)
        if has_sibling_paths:
            if (
                len(item.sibling_path_lookup_sha256s) != item.manifest_entry_count
                or list(item.sibling_path_lookup_sha256s)
                != sorted(item.sibling_path_lookup_sha256s)
                or len(item.sibling_path_lookup_sha256s)
                != len(set(item.sibling_path_lookup_sha256s))
                or not all(
                    _is_sha256(value) for value in item.sibling_path_lookup_sha256s
                )
            ):
                raise W3DDecodePlanError(
                    "external hierarchy resolver sibling paths are invalid"
                )
        elif item.collection_failure_code is not None:
            raise W3DDecodePlanError(
                "global compatibility resolver cannot bind collection failures"
            )
        if item.collection_failure_code is not None and (
            not isinstance(item.collection_failure_code, str)
            or not item.collection_failure_code
            or item.observations
        ):
            raise W3DDecodePlanError(
                "external hierarchy resolver collection failure is invalid"
            )
        for observation in item.observations:
            if (
                not _observation_is_valid(observation)
                or observation.source_sha256 != item.source_sha256
            ):
                raise W3DDecodePlanError(
                    "external hierarchy resolver source evidence is invalid"
                )
        if has_sibling_paths:
            sibling_observations = tuple(
                _sibling_observation(observation) for observation in item.observations
            )
            for path_lookup_sha256 in item.sibling_path_lookup_sha256s:
                sibling_candidates.append(
                    _sibling_candidate(
                        sibling_path_lookup_sha256=path_lookup_sha256,
                        source_sha256=item.source_sha256,
                        observations=sibling_observations,
                        collection_failure_code=item.collection_failure_code,
                    )
                )
        else:
            occurrences.extend(
                _occurrence(observation, item.manifest_entry_count)
                for observation in item.observations
            )
    if len(path_modes) > 1:
        raise W3DDecodePlanError(
            "external hierarchy resolver sources mix sibling and global modes"
        )
    if source_keys != sorted(source_keys) or len(source_keys) != len(set(source_keys)):
        raise W3DDecodePlanError(
            "external hierarchy resolver sources are not canonical"
        )
    ordered = tuple(
        sorted(
            occurrences,
            key=lambda item: (
                item.observation.lookup_sha256,
                item.observation.source_sha256,
                item.observation.source_ordinal,
                item.observation.evidence_sha256,
            ),
        )
    )
    ordered_sibling_candidates = tuple(
        sorted(
            sibling_candidates,
            key=lambda item: (
                item.sibling_path_lookup_sha256,
                item.source_sha256,
                item.evidence_sha256,
            ),
        )
    )
    if len(
        {item.sibling_path_lookup_sha256 for item in ordered_sibling_candidates}
    ) != len(ordered_sibling_candidates):
        raise W3DDecodePlanError("external hierarchy resolver sibling paths collide")
    provisional = W3DExternalHierarchyResolver(
        manifest_sha256=manifest_sha256,
        manifest_aggregate_sha256=manifest_aggregate_sha256,
        inventory_sha256=inventory_sha256,
        occurrences=ordered,
        sibling_candidates=ordered_sibling_candidates,
        resolver_sha256="0" * 64,
    )
    resolver = W3DExternalHierarchyResolver(
        manifest_sha256=manifest_sha256,
        manifest_aggregate_sha256=manifest_aggregate_sha256,
        inventory_sha256=inventory_sha256,
        occurrences=ordered,
        sibling_candidates=ordered_sibling_candidates,
        resolver_sha256=_canonical_sha256(provisional._identity()),
    )
    _validate_external_hierarchy_resolver(resolver, allow_unsealed=True)
    object.__setattr__(
        resolver,
        "_seal",
        _W3DExternalHierarchyResolverSeal(
            manifest_sha256=resolver.manifest_sha256,
            manifest_aggregate_sha256=resolver.manifest_aggregate_sha256,
            inventory_sha256=resolver.inventory_sha256,
            occurrences=resolver.occurrences,
            sibling_candidates=resolver.sibling_candidates,
            resolver_sha256=resolver.resolver_sha256,
        ),
    )
    return resolver


def _chunk_payload_sha256(
    source: bytes,
    chunk: W3DChunkMetadata | None,
) -> str | None:
    if chunk is None:
        return None
    start = chunk.payload_offset
    end = start + chunk.available_payload_size
    if not 0 <= start <= end <= len(source):
        return None
    return hashlib.sha256(source[start:end]).hexdigest()


def _collect_external_hierarchy_observations(
    source: bytes,
) -> tuple[_W3DExternalHierarchyObservation, ...]:
    """Collect exact, identifier-free hierarchy evidence from one W3D."""

    if not isinstance(source, bytes):
        raise TypeError("external hierarchy evidence source must be bytes")
    metadata = scan_w3d_metadata(source, DEFAULT_VIRTUAL_LABEL)
    provenance_damaged = bool(_provenance_issue_codes(source, metadata))
    _, damaged_offsets = _structural_warning_terminals(source, metadata)
    by_offset = {chunk.header_offset: chunk for chunk in metadata.chunks}
    headers_by_owner: dict[int, list[W3DHierarchyHeader]] = defaultdict(list)
    headers_by_lookup: dict[str, list[W3DHierarchyHeader]] = defaultdict(list)
    pivots_by_owner: dict[int, list[W3DChunkMetadata]] = defaultdict(list)
    for header in metadata.hierarchy_headers:
        owner_offset = header.provenance.parent_chunk_header_offset
        if owner_offset is not None:
            headers_by_owner[owner_offset].append(header)
        headers_by_lookup[_hierarchy_lookup_sha256(header.identifier)].append(header)
    for chunk in metadata.chunks:
        if (
            chunk.chunk_id == _HIERARCHY_PIVOTS
            and chunk.parent_header_offset is not None
        ):
            pivots_by_owner[chunk.parent_header_offset].append(chunk)

    observations: list[_W3DExternalHierarchyObservation] = []
    ordered_headers = sorted(
        metadata.hierarchy_headers,
        key=lambda item: item.provenance.chunk_header_offset,
    )
    for source_ordinal, header in enumerate(ordered_headers):
        lookup_sha256 = _hierarchy_lookup_sha256(header.identifier)
        header_offset = header.provenance.chunk_header_offset
        header_chunk = by_offset.get(header_offset)
        owner_offset = header.provenance.parent_chunk_header_offset
        owner = None if owner_offset is None else by_offset.get(owner_offset)
        pivot_chunks = tuple(
            sorted(
                pivots_by_owner.get(
                    owner_offset if owner_offset is not None else -1,
                    (),
                ),
                key=lambda item: item.header_offset,
            )
        )
        pivot_digest = hashlib.sha256()
        pivot_length = 0
        pivot_hash_available = bool(pivot_chunks)
        for pivot_chunk in pivot_chunks:
            start = pivot_chunk.payload_offset
            end = start + pivot_chunk.available_payload_size
            if not 0 <= start <= end <= len(source):
                pivot_hash_available = False
                break
            payload = source[start:end]
            pivot_digest.update(payload)
            pivot_length += len(payload)

        failure_code: str | None = None
        if provenance_damaged:
            failure_code = "source-provenance-invalid"
        elif owner is None or owner.chunk_id != _HIERARCHY_CONTAINER:
            failure_code = "invalid-hierarchy-owner-provenance"
        elif len(headers_by_owner.get(owner.header_offset, ())) != 1:
            failure_code = "ambiguous-hierarchy-owner-header"
        elif header_offset in damaged_offsets or owner.header_offset in damaged_offsets:
            failure_code = "damaged-hierarchy-owner-evidence"
        elif not _header_has_exact_layout(
            by_offset=by_offset,
            header_offset=header_offset,
            accepted_chunk_ids=frozenset({_HIERARCHY_HEADER}),
            expected_size=_HIERARCHY_HEADER_SIZE,
        ):
            failure_code = "hierarchy-owner-header-layout-unknown"
        elif header.pivot_count <= 0:
            failure_code = "missing-hierarchy-pivot-cardinality"
        elif len(pivot_chunks) != 1:
            failure_code = "ambiguous-hierarchy-pivot-record-stream"
        elif any(
            item.truncated or item.header_offset in damaged_offsets
            for item in pivot_chunks
        ):
            failure_code = "damaged-hierarchy-pivot-record-stream"
        elif not pivot_hash_available or pivot_length != (
            header.pivot_count * _HIERARCHY_PIVOT_RECORD_SIZE
        ):
            failure_code = "hierarchy-pivot-record-cardinality-mismatch"
        elif not header.identifier:
            failure_code = "empty-hierarchy-identifier"
        elif len(headers_by_lookup[lookup_sha256]) != 1:
            failure_code = "ambiguous-source-hierarchy-identifier"

        valid = failure_code is None
        observations.append(
            _observation(
                lookup_sha256=lookup_sha256,
                source_sha256=metadata.source_sha256,
                source_ordinal=source_ordinal,
                valid=valid,
                pivot_count=header.pivot_count,
                version_major=header.version_major,
                version_minor=header.version_minor,
                header_payload_sha256=_chunk_payload_sha256(source, header_chunk),
                pivot_records_sha256=(
                    pivot_digest.hexdigest() if pivot_hash_available else None
                ),
                pivot_records_byte_length=(
                    pivot_length if pivot_hash_available else None
                ),
                failure_code=failure_code,
            )
        )
    header_owner_offsets = {
        header.provenance.parent_chunk_header_offset
        for header in metadata.hierarchy_headers
    }
    headerless_containers = sorted(
        (
            chunk
            for chunk in metadata.chunks
            if chunk.chunk_id == _HIERARCHY_CONTAINER
            and chunk.header_offset not in header_owner_offsets
        ),
        key=lambda item: item.header_offset,
    )
    for container in headerless_containers:
        observations.append(
            _observation(
                lookup_sha256=_hierarchy_lookup_sha256(""),
                source_sha256=metadata.source_sha256,
                source_ordinal=len(observations),
                valid=False,
                pivot_count=None,
                version_major=None,
                version_minor=None,
                header_payload_sha256=None,
                pivot_records_sha256=None,
                pivot_records_byte_length=None,
                failure_code=(
                    "damaged-hierarchy-container"
                    if container.truncated or container.header_offset in damaged_offsets
                    else "missing-hierarchy-owner-header"
                ),
            )
        )
    return tuple(observations)


def _decoded_stream(
    *,
    domain: StreamDomain,
    chunk: W3DChunkMetadata,
    owner: W3DChunkMetadata,
    owner_header_offset: int,
    payload: bytes,
    attestation: StreamAttestation,
    external_hierarchy_proof_sha256: str | None = None,
    primary_import_disposition: PrimaryImportDisposition | None = None,
) -> W3DDecodedStream:
    neutral = attestation.neutral()
    return W3DDecodedStream(
        domain=domain,
        chunk_id=chunk.chunk_id,
        chunk_header_offset=chunk.header_offset,
        owner_container_header_offset=owner.header_offset,
        owner_header_offset=owner_header_offset,
        payload_byte_length=len(payload),
        payload_sha256=hashlib.sha256(payload).hexdigest(),
        attestation_sha256=_canonical_sha256(neutral),
        external_hierarchy_proof_sha256=external_hierarchy_proof_sha256,
        primary_import_disposition=primary_import_disposition,
        attestation=attestation,
    )


def _direct_parent(
    chunk: W3DChunkMetadata,
    by_offset: dict[int, W3DChunkMetadata],
) -> W3DChunkMetadata | None:
    if chunk.parent_header_offset is None:
        return None
    return by_offset.get(chunk.parent_header_offset)


def _path_has_damage(
    chunk: W3DChunkMetadata,
    ancestor: W3DChunkMetadata,
    by_offset: dict[int, W3DChunkMetadata],
    damaged_offsets: frozenset[int],
) -> bool:
    """Check one already-proven owner path, including both endpoints."""

    current: W3DChunkMetadata | None = chunk
    seen: set[int] = set()
    while current is not None:
        if current.header_offset in damaged_offsets:
            return True
        if current.header_offset == ancestor.header_offset:
            return False
        if current.header_offset in seen:
            return True
        seen.add(current.header_offset)
        current = _direct_parent(current, by_offset)
    return True


def _validated_mesh_header(
    *,
    source: bytes,
    chunk: W3DChunkMetadata,
    reported_owner: W3DChunkMetadata,
    mesh_owner: W3DChunkMetadata,
    by_offset: dict[int, W3DChunkMetadata],
    mesh_headers_by_owner: dict[int, list[W3DMeshHeader]],
    damaged_offsets: frozenset[int],
) -> W3DMeshHeader | W3DChunkTerminal:
    headers = mesh_headers_by_owner.get(mesh_owner.header_offset, [])
    if not headers:
        return _terminal(
            source=source,
            state="unresolved",
            code="missing-mesh-owner-header-cardinality",
            chunk=chunk,
            owner_container_header_offset=reported_owner.header_offset,
        )
    if len(headers) != 1:
        return _terminal(
            source=source,
            state="unresolved",
            code="ambiguous-mesh-owner-header",
            chunk=chunk,
            owner_container_header_offset=reported_owner.header_offset,
        )
    header = headers[0]
    header_offset = header.provenance.chunk_header_offset
    if not _header_is_usable(header_offset, damaged_offsets):
        return _terminal(
            source=source,
            state="damaged",
            code="damaged-mesh-owner-header",
            chunk=chunk,
            owner_container_header_offset=reported_owner.header_offset,
        )
    if not _header_has_exact_layout(
        by_offset=by_offset,
        header_offset=header_offset,
        accepted_chunk_ids=frozenset({_MESH_HEADER}),
        expected_size=_MESH_HEADER_SIZE,
    ):
        return _terminal(
            source=source,
            state="unsupported",
            code="mesh-owner-header-layout-unknown",
            chunk=chunk,
            owner_container_header_offset=reported_owner.header_offset,
        )
    return header


def _mesh_auxiliary_owners(
    chunk: W3DChunkMetadata,
    by_offset: dict[int, W3DChunkMetadata],
) -> tuple[W3DChunkMetadata, W3DChunkMetadata] | None:
    """Return ``(direct semantic owner, mesh owner)`` for proven paths."""

    direct = _direct_parent(chunk, by_offset)
    if chunk.chunk_id != W3D_CHUNK_DIFFUSE_COLOR_VALUES:
        if direct is None or direct.chunk_id != _MESH_CONTAINER:
            return None
        return direct, direct

    if direct is None or direct.chunk_id != _MATERIAL_PASS_CONTAINER:
        return None
    outer = _direct_parent(direct, by_offset)
    if outer is None:
        return None
    if outer.chunk_id == _MESH_CONTAINER:
        return direct, outer
    if outer.chunk_id not in _PRELIT_CONTAINERS:
        return None
    mesh = _direct_parent(outer, by_offset)
    if mesh is None or mesh.chunk_id != _MESH_CONTAINER:
        return None
    return direct, mesh


def _mesh_auxiliary_result(
    *,
    source: bytes,
    chunk: W3DChunkMetadata,
    by_offset: dict[int, W3DChunkMetadata],
    mesh_headers_by_owner: dict[int, list[W3DMeshHeader]],
    damaged_offsets: frozenset[int],
) -> W3DDecodedStream | W3DChunkTerminal:
    owners = _mesh_auxiliary_owners(chunk, by_offset)
    if owners is None:
        code = (
            "invalid-material-pass-owner"
            if chunk.chunk_id == W3D_CHUNK_DIFFUSE_COLOR_VALUES
            else "invalid-mesh-auxiliary-owner"
        )
        return _terminal(
            source=source,
            state="unresolved",
            code=code,
            chunk=chunk,
        )
    owner, mesh_owner = owners
    if _path_has_damage(chunk, mesh_owner, by_offset, damaged_offsets):
        return _terminal(
            source=source,
            state="damaged",
            code="damaged-auxiliary-stream-or-owner",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    header_result = _validated_mesh_header(
        source=source,
        chunk=chunk,
        reported_owner=owner,
        mesh_owner=mesh_owner,
        by_offset=by_offset,
        mesh_headers_by_owner=mesh_headers_by_owner,
        damaged_offsets=damaged_offsets,
    )
    if isinstance(header_result, W3DChunkTerminal):
        return header_result
    header = header_result
    payload = source[
        chunk.payload_offset : chunk.payload_offset + chunk.available_payload_size
    ]
    try:
        attestation = decode_auxiliary_stream(
            chunk.chunk_id,
            payload,
            expected_vertex_count=header.vertex_count,
            expected_triangle_count=header.face_count,
        )
    except W3DAuxiliaryStreamUnsupportedError:
        return _terminal(
            source=source,
            state="unsupported",
            code="auxiliary-stream-layout-unsupported",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    except W3DAuxiliaryStreamDecodeError:
        return _terminal(
            source=source,
            state="damaged",
            code="auxiliary-stream-payload-invalid",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    return _decoded_stream(
        domain="auxiliary",
        chunk=chunk,
        owner=owner,
        owner_header_offset=header.provenance.chunk_header_offset,
        payload=payload,
        attestation=attestation,
    )


def _aabb_auxiliary_result(
    *,
    source: bytes,
    chunk: W3DChunkMetadata,
    by_offset: dict[int, W3DChunkMetadata],
    mesh_headers_by_owner: dict[int, list[W3DMeshHeader]],
    aabb_headers_by_owner: dict[int, list[W3DChunkMetadata]],
    damaged_offsets: frozenset[int],
) -> W3DDecodedStream | W3DChunkTerminal:
    owner = _direct_parent(chunk, by_offset)
    if owner is None or owner.chunk_id != _AABB_TREE_CONTAINER:
        return _terminal(
            source=source,
            state="unresolved",
            code="invalid-aabb-tree-owner",
            chunk=chunk,
        )
    mesh_owner = _direct_parent(owner, by_offset)
    if mesh_owner is None or mesh_owner.chunk_id != _MESH_CONTAINER:
        return _terminal(
            source=source,
            state="unresolved",
            code="invalid-aabb-tree-mesh-owner",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    if _path_has_damage(chunk, mesh_owner, by_offset, damaged_offsets):
        return _terminal(
            source=source,
            state="damaged",
            code="damaged-auxiliary-stream-or-owner",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    mesh_header_result = _validated_mesh_header(
        source=source,
        chunk=chunk,
        reported_owner=owner,
        mesh_owner=mesh_owner,
        by_offset=by_offset,
        mesh_headers_by_owner=mesh_headers_by_owner,
        damaged_offsets=damaged_offsets,
    )
    if isinstance(mesh_header_result, W3DChunkTerminal):
        return mesh_header_result
    mesh_header = mesh_header_result

    aabb_headers = aabb_headers_by_owner.get(owner.header_offset, [])
    if not aabb_headers:
        return _terminal(
            source=source,
            state="unresolved",
            code="missing-aabb-owner-header-cardinality",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    if len(aabb_headers) != 1:
        return _terminal(
            source=source,
            state="unresolved",
            code="ambiguous-aabb-owner-header",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    aabb_header_chunk = aabb_headers[0]
    if (
        aabb_header_chunk.header_offset in damaged_offsets
        or aabb_header_chunk.has_subchunks_flag
    ):
        return _terminal(
            source=source,
            state="damaged",
            code="damaged-aabb-owner-header",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    header_payload = source[
        aabb_header_chunk.payload_offset : aabb_header_chunk.payload_offset
        + aabb_header_chunk.available_payload_size
    ]
    try:
        header_attestation = decode_auxiliary_stream(
            W3D_CHUNK_AABTREE_HEADER,
            header_payload,
            expected_triangle_count=mesh_header.face_count,
        )
    except W3DAuxiliaryStreamDecodeError:
        return _terminal(
            source=source,
            state="damaged",
            code="aabb-owner-header-payload-invalid",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    header_record = header_attestation.records()[0]
    if not isinstance(header_record, W3DAabTreeHeaderRecord):
        return _terminal(
            source=source,
            state="unsupported",
            code="aabb-owner-header-layout-unsupported",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )

    payload = source[
        chunk.payload_offset : chunk.payload_offset + chunk.available_payload_size
    ]
    try:
        attestation = (
            header_attestation
            if chunk.chunk_id == W3D_CHUNK_AABTREE_HEADER
            else decode_auxiliary_stream(
                chunk.chunk_id,
                payload,
                expected_triangle_count=mesh_header.face_count,
                expected_aabb_node_count=header_record.node_count,
                expected_aabb_polygon_count=header_record.polygon_count,
            )
        )
    except W3DAuxiliaryStreamUnsupportedError:
        return _terminal(
            source=source,
            state="unsupported",
            code="auxiliary-stream-layout-unsupported",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    except W3DAuxiliaryStreamDecodeError:
        return _terminal(
            source=source,
            state="damaged",
            code="auxiliary-stream-payload-invalid",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    return _decoded_stream(
        domain="auxiliary",
        chunk=chunk,
        owner=owner,
        owner_header_offset=aabb_header_chunk.header_offset,
        payload=payload,
        attestation=attestation,
    )


def _hierarchy_auxiliary_result(
    *,
    source: bytes,
    chunk: W3DChunkMetadata,
    by_offset: dict[int, W3DChunkMetadata],
    hierarchy_headers_by_owner: dict[int, list[W3DHierarchyHeader]],
    damaged_offsets: frozenset[int],
) -> W3DDecodedStream | W3DChunkTerminal:
    owner = _direct_parent(chunk, by_offset)
    if owner is None or owner.chunk_id != _HIERARCHY_CONTAINER:
        return _terminal(
            source=source,
            state="unresolved",
            code="invalid-hierarchy-auxiliary-owner",
            chunk=chunk,
        )
    if _path_has_damage(chunk, owner, by_offset, damaged_offsets):
        return _terminal(
            source=source,
            state="damaged",
            code="damaged-auxiliary-stream-or-owner",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    headers = hierarchy_headers_by_owner.get(owner.header_offset, [])
    if not headers:
        return _terminal(
            source=source,
            state="unresolved",
            code="missing-hierarchy-owner-header-cardinality",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    if len(headers) != 1:
        return _terminal(
            source=source,
            state="unresolved",
            code="ambiguous-hierarchy-owner-header",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    header = headers[0]
    header_offset = header.provenance.chunk_header_offset
    if not _header_is_usable(header_offset, damaged_offsets):
        return _terminal(
            source=source,
            state="damaged",
            code="damaged-hierarchy-owner-header",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    if not _header_has_exact_layout(
        by_offset=by_offset,
        header_offset=header_offset,
        accepted_chunk_ids=frozenset({_HIERARCHY_HEADER}),
        expected_size=_HIERARCHY_HEADER_SIZE,
    ):
        return _terminal(
            source=source,
            state="unsupported",
            code="hierarchy-owner-header-layout-unknown",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    payload = source[
        chunk.payload_offset : chunk.payload_offset + chunk.available_payload_size
    ]
    try:
        attestation = decode_auxiliary_stream(
            chunk.chunk_id,
            payload,
            expected_pivot_count=header.pivot_count,
        )
    except W3DAuxiliaryStreamUnsupportedError:
        return _terminal(
            source=source,
            state="unsupported",
            code="auxiliary-stream-layout-unsupported",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    except W3DAuxiliaryStreamDecodeError:
        return _terminal(
            source=source,
            state="damaged",
            code="auxiliary-stream-payload-invalid",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    return _decoded_stream(
        domain="auxiliary",
        chunk=chunk,
        owner=owner,
        owner_header_offset=header_offset,
        payload=payload,
        attestation=attestation,
    )


def _auxiliary_result(
    *,
    source: bytes,
    chunk: W3DChunkMetadata,
    by_offset: dict[int, W3DChunkMetadata],
    mesh_headers_by_owner: dict[int, list[W3DMeshHeader]],
    hierarchy_headers_by_owner: dict[int, list[W3DHierarchyHeader]],
    aabb_headers_by_owner: dict[int, list[W3DChunkMetadata]],
    damaged_offsets: frozenset[int],
) -> W3DDecodedStream | W3DChunkTerminal:
    if chunk.has_subchunks_flag:
        return _terminal(
            source=source,
            state="damaged",
            code="auxiliary-leaf-container-flag-invalid",
            chunk=chunk,
        )
    if chunk.chunk_id == W3D_CHUNK_PIVOT_FIXUPS:
        return _hierarchy_auxiliary_result(
            source=source,
            chunk=chunk,
            by_offset=by_offset,
            hierarchy_headers_by_owner=hierarchy_headers_by_owner,
            damaged_offsets=damaged_offsets,
        )
    if chunk.chunk_id in {
        W3D_CHUNK_AABTREE_HEADER,
        W3D_CHUNK_AABTREE_POLYINDICES,
        W3D_CHUNK_AABTREE_NODES,
    }:
        return _aabb_auxiliary_result(
            source=source,
            chunk=chunk,
            by_offset=by_offset,
            mesh_headers_by_owner=mesh_headers_by_owner,
            aabb_headers_by_owner=aabb_headers_by_owner,
            damaged_offsets=damaged_offsets,
        )
    return _mesh_auxiliary_result(
        source=source,
        chunk=chunk,
        by_offset=by_offset,
        mesh_headers_by_owner=mesh_headers_by_owner,
        damaged_offsets=damaged_offsets,
    )


def _emitter_result(
    *,
    source: bytes,
    chunk: W3DChunkMetadata,
    by_offset: dict[int, W3DChunkMetadata],
    emitter_headers_by_owner: dict[int, list[W3DChunkMetadata]],
    damaged_offsets: frozenset[int],
) -> W3DDecodedStream | W3DChunkTerminal:
    if chunk.has_subchunks_flag:
        return _terminal(
            source=source,
            state="damaged",
            code="emitter-child-container-flag-invalid",
            chunk=chunk,
        )
    owner = _direct_parent(chunk, by_offset)
    if owner is None or owner.chunk_id != W3D_CHUNK_EMITTER:
        return _terminal(
            source=source,
            state="unresolved",
            code="invalid-emitter-stream-owner",
            chunk=chunk,
        )
    if not owner.has_subchunks_flag:
        return _terminal(
            source=source,
            state="damaged",
            code="emitter-owner-container-flag-invalid",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    if _path_has_damage(chunk, owner, by_offset, damaged_offsets):
        return _terminal(
            source=source,
            state="damaged",
            code="damaged-emitter-stream-or-owner",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    headers = emitter_headers_by_owner.get(owner.header_offset, [])
    if not headers:
        return _terminal(
            source=source,
            state="unresolved",
            code="missing-emitter-owner-header-cardinality",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    if len(headers) != 1:
        return _terminal(
            source=source,
            state="unresolved",
            code="ambiguous-emitter-owner-header",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    header = headers[0]
    if header.header_offset in damaged_offsets:
        return _terminal(
            source=source,
            state="damaged",
            code="damaged-emitter-owner-header",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )

    owner_payload = source[
        owner.payload_offset : owner.payload_offset + owner.available_payload_size
    ]
    try:
        container = decode_emitter_container(owner_payload)
    except W3DEmitterUnsupportedError:
        return _terminal(
            source=source,
            state="unsupported",
            code="emitter-container-layout-unsupported",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    except W3DEmitterDecodeError:
        return _terminal(
            source=source,
            state="damaged",
            code="emitter-container-payload-invalid",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    matches = tuple(
        item for item in container.children if item.chunk_id == chunk.chunk_id
    )
    if len(matches) != 1:
        return _terminal(
            source=source,
            state="damaged",
            code="emitter-child-attestation-cardinality-invalid",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    attestation = matches[0]
    payload = source[
        chunk.payload_offset : chunk.payload_offset + chunk.available_payload_size
    ]
    if attestation.payload_sha256 != hashlib.sha256(payload).hexdigest():
        return _terminal(
            source=source,
            state="damaged",
            code="emitter-child-attestation-source-mismatch",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    if not attestation.decoded:
        return _terminal(
            source=source,
            state="unsupported",
            code="emitter-stream-layout-unsupported",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    return _decoded_stream(
        domain="emitter",
        chunk=chunk,
        owner=owner,
        owner_header_offset=header.header_offset,
        payload=payload,
        attestation=attestation,
    )


def _geometry_result(
    *,
    source: bytes,
    chunk: W3DChunkMetadata,
    by_offset: dict[int, W3DChunkMetadata],
    mesh_headers_by_owner: dict[int, list[W3DMeshHeader]],
    damaged_offsets: frozenset[int],
) -> W3DDecodedStream | W3DChunkTerminal:
    owner = _ancestor(chunk, by_offset, frozenset({_MESH_CONTAINER}))
    if owner is None:
        return _terminal(
            source=source,
            state="unresolved",
            code="missing-mesh-owner",
            chunk=chunk,
        )
    if chunk.header_offset in damaged_offsets or owner.header_offset in damaged_offsets:
        return _terminal(
            source=source,
            state="damaged",
            code="damaged-stream-or-owner-container",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    headers = mesh_headers_by_owner.get(owner.header_offset, [])
    if not headers:
        return _terminal(
            source=source,
            state="unresolved",
            code="missing-mesh-owner-header-cardinality",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    if len(headers) != 1:
        return _terminal(
            source=source,
            state="unresolved",
            code="ambiguous-mesh-owner-header",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    header = headers[0]
    header_offset = header.provenance.chunk_header_offset
    if not _header_is_usable(header_offset, damaged_offsets):
        return _terminal(
            source=source,
            state="damaged",
            code="damaged-mesh-owner-header",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    if not _header_has_exact_layout(
        by_offset=by_offset,
        header_offset=header_offset,
        accepted_chunk_ids=frozenset({_MESH_HEADER}),
        expected_size=_MESH_HEADER_SIZE,
    ):
        return _terminal(
            source=source,
            state="unsupported",
            code="mesh-owner-header-layout-unknown",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    start = chunk.payload_offset
    payload = source[start : start + chunk.available_payload_size]
    try:
        attestation = decode_geometry_stream(
            chunk.chunk_id,
            payload,
            expected_vertex_count=header.vertex_count,
            expected_triangle_count=header.face_count,
            pivot_count=None,
        )
    except W3DGeometryStreamUnsupportedError:
        return _terminal(
            source=source,
            state="unsupported",
            code="geometry-stream-layout-unsupported",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    except W3DGeometryStreamDecodeError:
        return _terminal(
            source=source,
            state="damaged",
            code="geometry-stream-payload-invalid",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    return _decoded_stream(
        domain="geometry",
        chunk=chunk,
        owner=owner,
        owner_header_offset=header_offset,
        payload=payload,
        attestation=attestation,
    )


def _animation_result(
    *,
    source: bytes,
    chunk: W3DChunkMetadata,
    by_offset: dict[int, W3DChunkMetadata],
    animation_headers_by_owner: dict[int, list[W3DAnimationHeader]],
    hierarchy_headers: tuple[W3DHierarchyHeader, ...],
    damaged_offsets: frozenset[int],
    external_hierarchy_resolver: W3DExternalHierarchyResolver | None,
    virtual_path: str,
) -> W3DDecodedStream | W3DChunkTerminal:
    owner = _ancestor(chunk, by_offset, _ANIMATION_CONTAINERS)
    if owner is None:
        return _terminal(
            source=source,
            state="unresolved",
            code="missing-animation-owner",
            chunk=chunk,
        )
    if chunk.header_offset in damaged_offsets or owner.header_offset in damaged_offsets:
        return _terminal(
            source=source,
            state="damaged",
            code="damaged-stream-or-owner-container",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    headers = animation_headers_by_owner.get(owner.header_offset, [])
    if not headers:
        return _terminal(
            source=source,
            state="unresolved",
            code="missing-animation-owner-header-cardinality",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    if len(headers) != 1:
        return _terminal(
            source=source,
            state="unresolved",
            code="ambiguous-animation-owner-header",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    header = headers[0]
    header_offset = header.provenance.chunk_header_offset
    if not _header_is_usable(header_offset, damaged_offsets):
        return _terminal(
            source=source,
            state="damaged",
            code="damaged-animation-owner-header",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    expected_header_id = (
        _COMPRESSED_ANIMATION_HEADER if header.compressed else _RAW_ANIMATION_HEADER
    )
    if not _header_has_exact_layout(
        by_offset=by_offset,
        header_offset=header_offset,
        accepted_chunk_ids=frozenset({expected_header_id}),
        expected_size=_ANIMATION_HEADER_SIZE,
    ):
        return _terminal(
            source=source,
            state="unsupported",
            code="animation-owner-header-layout-unknown",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )

    layout_matches = (
        owner.chunk_id == _RAW_ANIMATION_CONTAINER
        and not header.compressed
        and chunk.chunk_id in _RAW_ANIMATION_STREAMS
    ) or (
        owner.chunk_id == _COMPRESSED_ANIMATION_CONTAINER
        and header.compressed
        and chunk.chunk_id in _COMPRESSED_ANIMATION_STREAMS
    )
    if not layout_matches:
        return _terminal(
            source=source,
            state="unresolved",
            code="animation-owner-layout-mismatch",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    if header.frame_count <= 0:
        return _terminal(
            source=source,
            state="unresolved",
            code="missing-animation-frame-cardinality",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    hierarchy_reference = header.hierarchy_identifier.casefold()
    authored_hierarchy_matches = tuple(
        item
        for item in hierarchy_headers
        if hierarchy_reference and item.identifier.casefold() == hierarchy_reference
    )
    if len(authored_hierarchy_matches) > 1:
        return _terminal(
            source=source,
            state="unresolved",
            code="ambiguous-hierarchy-pivot-cardinality",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    external_hierarchy_proof_sha256: str | None = None
    if authored_hierarchy_matches:
        hierarchy = authored_hierarchy_matches[0]
        hierarchy_header_offset = hierarchy.provenance.chunk_header_offset
        if not _header_is_usable(hierarchy_header_offset, damaged_offsets):
            return _terminal(
                source=source,
                state="damaged",
                code="damaged-hierarchy-cardinality-header",
                chunk=chunk,
                owner_container_header_offset=owner.header_offset,
            )
        hierarchy_owner_offset = hierarchy.provenance.parent_chunk_header_offset
        hierarchy_owner = (
            None
            if hierarchy_owner_offset is None
            else by_offset.get(hierarchy_owner_offset)
        )
        if hierarchy_owner is None or hierarchy_owner.chunk_id != _HIERARCHY_CONTAINER:
            return _terminal(
                source=source,
                state="unresolved",
                code="invalid-hierarchy-owner-provenance",
                chunk=chunk,
                owner_container_header_offset=owner.header_offset,
            )
        if hierarchy_owner.header_offset in damaged_offsets:
            return _terminal(
                source=source,
                state="damaged",
                code="damaged-hierarchy-owner-container",
                chunk=chunk,
                owner_container_header_offset=owner.header_offset,
            )
        if not _header_has_exact_layout(
            by_offset=by_offset,
            header_offset=hierarchy_header_offset,
            accepted_chunk_ids=frozenset({_HIERARCHY_HEADER}),
            expected_size=_HIERARCHY_HEADER_SIZE,
        ):
            return _terminal(
                source=source,
                state="unsupported",
                code="hierarchy-owner-header-layout-unknown",
                chunk=chunk,
                owner_container_header_offset=owner.header_offset,
            )
        if hierarchy.pivot_count <= 0:
            return _terminal(
                source=source,
                state="unresolved",
                code="missing-hierarchy-pivot-cardinality",
                chunk=chunk,
                owner_container_header_offset=owner.header_offset,
            )
        pivot_count = hierarchy.pivot_count
    else:
        if external_hierarchy_resolver is None:
            return _terminal(
                source=source,
                state="unresolved",
                code="missing-hierarchy-pivot-cardinality",
                chunk=chunk,
                owner_container_header_offset=owner.header_offset,
            )
        if external_hierarchy_resolver.resolution_mode == "sibling-path":
            match = external_hierarchy_resolver.resolve_sibling(
                header.hierarchy_identifier,
                virtual_path,
            )
            if match.state == "missing":
                # The engine's identifier-prefix rule
                # (art/w3d/<name[:2]>/<name>.w3d) is consulted only when no
                # sibling candidate file exists at all.  Corpus measurement
                # found no file where the two rules disagree: every sibling
                # resolution is also the prefix resolution.  A prefix
                # candidate that exists but is damaged or ambiguous keeps
                # its explicit terminal rather than reverting to "missing".
                prefix_match = (
                    external_hierarchy_resolver.resolve_identifier_prefix(
                        header.hierarchy_identifier
                    )
                )
                if prefix_match.state != "missing":
                    match = prefix_match
        else:
            match = external_hierarchy_resolver.resolve_global_no_context(
                header.hierarchy_identifier
            )
        if match.state == "missing":
            return _terminal(
                source=source,
                state="unresolved",
                code="missing-hierarchy-pivot-cardinality",
                chunk=chunk,
                owner_container_header_offset=owner.header_offset,
            )
        if match.state == "ambiguous":
            return _terminal(
                source=source,
                state="unresolved",
                code="ambiguous-external-hierarchy-pivot-cardinality",
                chunk=chunk,
                owner_container_header_offset=owner.header_offset,
            )
        if match.state == "damaged":
            return _terminal(
                source=source,
                state="damaged",
                code="damaged-external-hierarchy-cardinality-evidence",
                chunk=chunk,
                owner_container_header_offset=owner.header_offset,
            )
        if match.pivot_count is None or match.pivot_count <= 0:
            raise W3DDecodePlanError(
                "external hierarchy resolver returned invalid cardinality"
            )
        pivot_count = match.pivot_count
        animation_header_sha256 = _chunk_payload_sha256(
            source,
            by_offset.get(header_offset),
        )
        if animation_header_sha256 is None:
            raise W3DDecodePlanError(
                "animation header proof could not be bound to source bytes"
            )
        proof = {
            "schema": "openbfme.w3d-external-hierarchy-proof",
            "schemaVersion": 1,
            "resolverSha256": external_hierarchy_resolver.resolver_sha256,
            "manifestSha256": external_hierarchy_resolver.manifest_sha256,
            "manifestAggregateSha256": (
                external_hierarchy_resolver.manifest_aggregate_sha256
            ),
            "inventorySha256": external_hierarchy_resolver.inventory_sha256,
            "resolutionMode": match.resolution_mode,
            "targetSourceSha256": hashlib.sha256(source).hexdigest(),
            "animationHeaderPayloadSha256": animation_header_sha256,
            "lookupSha256": match.lookup_sha256,
            "matchEvidenceSha256": match.evidence_sha256,
            "pivotCount": pivot_count,
        }
        if match.resolution_mode == "sibling-path":
            proof["targetPathContextSha256"] = _virtual_path_context_sha256(
                virtual_path
            )
        external_hierarchy_proof_sha256 = _canonical_sha256(proof)

    start = chunk.payload_offset
    payload = source[start : start + chunk.available_payload_size]
    try:
        decoded = decode_animation_stream(
            chunk.chunk_id,
            payload,
            animation_frame_count=header.frame_count,
            pivot_count=pivot_count,
            compressed_flavor=(
                header.flavor
                if chunk.chunk_id == W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL
                else None
            ),
        )
    except W3DAnimationStreamUnsupportedError:
        return _terminal(
            source=source,
            state="unsupported",
            code="animation-stream-layout-unsupported",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    except W3DAnimationStreamDecodeError:
        return _terminal(
            source=source,
            state="damaged",
            code="animation-stream-payload-invalid",
            chunk=chunk,
            owner_container_header_offset=owner.header_offset,
        )
    return _decoded_stream(
        domain="animation",
        chunk=chunk,
        owner=owner,
        owner_header_offset=header_offset,
        payload=payload,
        attestation=decoded.attestation,
        external_hierarchy_proof_sha256=(external_hierarchy_proof_sha256),
        primary_import_disposition=(decoded.attestation.primary_import_disposition),
    )


def _non_stream_terminal(
    source: bytes,
    chunk: W3DChunkMetadata,
) -> W3DChunkTerminal:
    if chunk.classification in {"container", "metadata"}:
        return _terminal(
            source=source,
            state="observed",
            code=f"{chunk.classification}-structure-only",
            chunk=chunk,
        )
    if chunk.classification == "known-data":
        return _terminal(
            source=source,
            state="unsupported",
            code="known-data-layout-not-decoded",
            chunk=chunk,
        )
    if chunk.classification == "unsupported":
        return _terminal(
            source=source,
            state="unsupported",
            code="metadata-classified-unsupported",
            chunk=chunk,
        )
    return _terminal(
        source=source,
        state="unsupported",
        code="unknown-chunk-layout",
        chunk=chunk,
    )


def plan_w3d_stream_decode(
    source: bytes,
    virtual_label: str = DEFAULT_VIRTUAL_LABEL,
    *,
    external_hierarchy_resolver: W3DExternalHierarchyResolver | None = None,
) -> W3DStreamDecodePlan:
    """Plan and attest every provable stream in caller-owned W3D bytes.

    ``virtual_label`` is validated by the metadata scanner.  It is omitted in
    plain form; a sibling-mode resolver binds only its parent-context digest.
    Malformed content is retained as explicit terminals; type, safe-label, and
    scanner resource-limit failures continue to raise under the scanner's
    established contract.  The optional resolver must be the immutable, sealed
    contract built from a verified corpus; source-only behavior is unchanged
    when it is omitted.
    """

    if not isinstance(source, bytes):
        raise TypeError("W3D decode-plan source must be bytes")
    if external_hierarchy_resolver is not None:
        _validate_external_hierarchy_resolver(external_hierarchy_resolver)
    resolver_sha256 = (
        None
        if external_hierarchy_resolver is None
        else external_hierarchy_resolver.resolver_sha256
    )
    metadata = scan_w3d_metadata(source, virtual_label)
    path_context_sha256 = (
        _virtual_path_context_sha256(metadata.virtual_path)
        if external_hierarchy_resolver is not None
        and external_hierarchy_resolver.resolution_mode == "sibling-path"
        else None
    )
    target_ids = (
        SUPPORTED_GEOMETRY_STREAM_CHUNK_IDS
        | SUPPORTED_ANIMATION_STREAM_CHUNK_IDS
        | SUPPORTED_AUXILIARY_STREAM_CHUNK_IDS
        | SUPPORTED_EMITTER_CHILD_CHUNK_IDS
        | UNSUPPORTED_EMITTER_CHILD_CHUNK_IDS
    )
    targets = tuple(chunk for chunk in metadata.chunks if chunk.chunk_id in target_ids)

    provenance_issues = _provenance_issue_codes(source, metadata)
    if provenance_issues:
        terminals = tuple(
            _terminal(
                source=source,
                state="damaged",
                code=code,
                evidence_offset=offset,
            )
            for code, offset in provenance_issues
        )
        return _final_plan(
            source=source,
            encountered_chunk_count=len(metadata.chunks),
            target_stream_chunk_count=len(targets),
            decoded_streams=(),
            terminals=terminals,
            path_context_sha256=path_context_sha256,
            external_hierarchy_resolver_sha256=resolver_sha256,
        )

    warning_terminals, damaged_offsets = _structural_warning_terminals(
        source,
        metadata,
    )
    by_offset = {chunk.header_offset: chunk for chunk in metadata.chunks}
    mesh_headers_by_owner: dict[int, list[W3DMeshHeader]] = defaultdict(list)
    for header in metadata.mesh_headers:
        owner = header.provenance.parent_chunk_header_offset
        if owner is not None:
            mesh_headers_by_owner[owner].append(header)
    hierarchy_headers_by_owner: dict[int, list[W3DHierarchyHeader]] = defaultdict(list)
    for header in metadata.hierarchy_headers:
        owner = header.provenance.parent_chunk_header_offset
        if owner is not None:
            hierarchy_headers_by_owner[owner].append(header)
    aabb_headers_by_owner: dict[int, list[W3DChunkMetadata]] = defaultdict(list)
    for candidate in metadata.chunks:
        if (
            candidate.chunk_id == W3D_CHUNK_AABTREE_HEADER
            and candidate.parent_header_offset is not None
        ):
            aabb_headers_by_owner[candidate.parent_header_offset].append(candidate)
    animation_headers_by_owner: dict[int, list[W3DAnimationHeader]] = defaultdict(list)
    for header in metadata.animation_headers:
        owner = header.provenance.parent_chunk_header_offset
        if owner is not None:
            animation_headers_by_owner[owner].append(header)
    emitter_headers_by_owner: dict[int, list[W3DChunkMetadata]] = defaultdict(list)
    for candidate in metadata.chunks:
        if (
            candidate.chunk_id == W3D_CHUNK_EMITTER_HEADER
            and candidate.parent_header_offset is not None
        ):
            emitter_headers_by_owner[candidate.parent_header_offset].append(candidate)

    decoded: list[W3DDecodedStream] = []
    stream_terminals: list[W3DChunkTerminal] = []
    target_offsets = {chunk.header_offset for chunk in targets}
    for chunk in targets:
        if chunk.chunk_id in SUPPORTED_GEOMETRY_STREAM_CHUNK_IDS:
            result = _geometry_result(
                source=source,
                chunk=chunk,
                by_offset=by_offset,
                mesh_headers_by_owner=mesh_headers_by_owner,
                damaged_offsets=damaged_offsets,
            )
        elif chunk.chunk_id in (
            SUPPORTED_EMITTER_CHILD_CHUNK_IDS | UNSUPPORTED_EMITTER_CHILD_CHUNK_IDS
        ):
            result = _emitter_result(
                source=source,
                chunk=chunk,
                by_offset=by_offset,
                emitter_headers_by_owner=emitter_headers_by_owner,
                damaged_offsets=damaged_offsets,
            )
        elif chunk.chunk_id in SUPPORTED_AUXILIARY_STREAM_CHUNK_IDS:
            result = _auxiliary_result(
                source=source,
                chunk=chunk,
                by_offset=by_offset,
                mesh_headers_by_owner=mesh_headers_by_owner,
                hierarchy_headers_by_owner=hierarchy_headers_by_owner,
                aabb_headers_by_owner=aabb_headers_by_owner,
                damaged_offsets=damaged_offsets,
            )
        else:
            result = _animation_result(
                source=source,
                chunk=chunk,
                by_offset=by_offset,
                animation_headers_by_owner=animation_headers_by_owner,
                hierarchy_headers=metadata.hierarchy_headers,
                damaged_offsets=damaged_offsets,
                external_hierarchy_resolver=external_hierarchy_resolver,
                virtual_path=metadata.virtual_path,
            )
        if isinstance(result, W3DDecodedStream):
            decoded.append(result)
        else:
            stream_terminals.append(result)

    non_stream_terminals = tuple(
        _non_stream_terminal(source, chunk)
        for chunk in metadata.chunks
        if chunk.header_offset not in target_offsets
    )
    terminals = tuple(
        sorted(
            (*warning_terminals, *stream_terminals, *non_stream_terminals),
            key=lambda item: (
                item.chunk_header_offset
                if item.chunk_header_offset is not None
                else -1,
                item.state,
                item.code,
                item.evidence_sha256,
            ),
        )
    )
    return _final_plan(
        source=source,
        encountered_chunk_count=len(metadata.chunks),
        target_stream_chunk_count=len(targets),
        decoded_streams=tuple(
            sorted(decoded, key=lambda item: item.chunk_header_offset)
        ),
        terminals=terminals,
        path_context_sha256=path_context_sha256,
        external_hierarchy_resolver_sha256=resolver_sha256,
    )


# Descriptive aliases for callers that use "build" or omit "stream".
build_w3d_decode_plan = plan_w3d_stream_decode
plan_w3d_decode = plan_w3d_stream_decode
