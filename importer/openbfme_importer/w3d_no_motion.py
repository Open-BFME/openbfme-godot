"""Remove source-proven W3D animation containers that contain no motion.

Some retail BFME2 model W3Ds embed an animation header but no animation
channels.  The pinned converter treats those containers as timeline metadata:
they create no action and move no model state.  This module supports only that
narrow case.  It removes complete *top-level* raw or compressed animation
containers after proving that each container contains exactly one 44-byte
header and no channel, bit-channel, compressed channel, motion channel, or
other child.

The rewrite is deliberately just top-level concatenation.  Every retained
chunk remains byte-for-byte identical and no owner size is repaired.  Explicit
expectations seal the animation identifier, hierarchy binding, timing,
compression mode, and (when embedded beside a model) model binding.  Any
structural or semantic ambiguity raises before transformed bytes are returned.
"""

from __future__ import annotations

from dataclasses import dataclass, field, replace
import hashlib
import json
import math
import struct
from typing import Iterable

from .w3d_metadata import W3DMetadata, scan_w3d_metadata
from .w3d_string_leaves import is_w3d_string_leaf


NO_MOTION_SCHEMA = "openbfme.w3d-no-motion-proof"
NO_MOTION_SCHEMA_VERSION = 0

MAX_W3D_BYTES = 512 * 1024 * 1024
MAX_W3D_CHUNKS = 1_000_000
MAX_W3D_DEPTH = 64

W3D_CHUNK_ANIMATION = 0x00000200
W3D_CHUNK_ANIMATION_HEADER = 0x00000201
W3D_CHUNK_ANIMATION_CHANNEL = 0x00000202
W3D_CHUNK_ANIMATION_BIT_CHANNEL = 0x00000203
W3D_CHUNK_COMPRESSED_ANIMATION = 0x00000280
W3D_CHUNK_COMPRESSED_ANIMATION_HEADER = 0x00000281
W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL = 0x00000282
W3D_CHUNK_COMPRESSED_BIT_CHANNEL = 0x00000283
W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL = 0x00000284
W3D_CHUNK_MORPH_ANIMATION = 0x000002C0

_CHUNK_HEADER_SIZE = 8
_ANIMATION_HEADER_SIZE = 44
_CONTAINER_FLAG = 0x80000000
_SIZE_MASK = 0x7FFFFFFF

_ANIMATION_CONTAINERS = frozenset({W3D_CHUNK_ANIMATION, W3D_CHUNK_COMPRESSED_ANIMATION})
_ANIMATION_RELATED = frozenset(
    {
        W3D_CHUNK_ANIMATION,
        W3D_CHUNK_ANIMATION_HEADER,
        W3D_CHUNK_ANIMATION_CHANNEL,
        W3D_CHUNK_ANIMATION_BIT_CHANNEL,
        W3D_CHUNK_COMPRESSED_ANIMATION,
        W3D_CHUNK_COMPRESSED_ANIMATION_HEADER,
        W3D_CHUNK_COMPRESSED_ANIMATION_CHANNEL,
        W3D_CHUNK_COMPRESSED_BIT_CHANNEL,
        W3D_CHUNK_COMPRESSED_ANIMATION_MOTION_CHANNEL,
        W3D_CHUNK_MORPH_ANIMATION,
    }
)

# These are the W3D compound chunks interpreted by the repository scanner.
# Treating a known compound chunk as a container even when its flag is absent
# lets the transformer detect an animation illegally nested inside it instead
# of overlooking the nested bytes.  Animation containers themselves are still
# required to carry the container flag before they may be removed.
_KNOWN_CONTAINERS = frozenset(
    {
        0x00000000,
        0x00000023,
        0x00000024,
        0x00000025,
        0x00000026,
        0x0000002A,
        0x0000002B,
        0x00000030,
        0x00000031,
        0x00000038,
        0x00000048,
        0x00000050,
        0x00000051,
        0x00000090,
        0x00000100,
        W3D_CHUNK_ANIMATION,
        W3D_CHUNK_COMPRESSED_ANIMATION,
        0x00000700,
        0x00000702,
        0x00000705,
        0x00000706,
        0x00000900,
    }
)

# String-leaf handling (the retail 0x80000000 flag is meaningless on them) lives
# in ``w3d_string_leaves`` so every W3D walker shares one definition.


class W3DNoMotionError(ValueError):
    """Raised before output when a no-motion proof is incomplete."""


@dataclass(frozen=True, slots=True)
class W3DNoMotionExpectation:
    """Exact caller-owned contract for one removable animation container."""

    identifier: str
    hierarchy_identifier: str
    frame_count: int | float
    frame_rate: int | float
    compressed: bool
    model_identifier: str | None = None
    flavor: int | float | None = None


@dataclass(frozen=True, slots=True)
class W3DNoMotionHeaderProof:
    identifier: str
    hierarchy_identifier: str
    model_identifier: str | None
    version_major: int
    version_minor: int
    frame_count: int
    frame_rate: int
    compressed: bool
    flavor: int | None
    top_level_ordinal: int
    container_chunk_id: int
    container_byte_length: int
    container_sha256: str

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "identifier": self.identifier,
            "hierarchyIdentifier": self.hierarchy_identifier,
            "version": [self.version_major, self.version_minor],
            "frameCount": self.frame_count,
            "frameRate": self.frame_rate,
            "compressed": self.compressed,
            "topLevelOrdinal": self.top_level_ordinal,
            "containerChunkId": self.container_chunk_id,
            "containerChunkIdHex": f"0x{self.container_chunk_id:08X}",
            "containerByteLength": self.container_byte_length,
            "containerSha256": self.container_sha256,
        }
        if self.model_identifier is not None:
            value["modelIdentifier"] = self.model_identifier
        if self.flavor is not None:
            value["flavor"] = self.flavor
        return value

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DNoMotionProof:
    input_sha256: str
    output_sha256: str
    input_byte_length: int
    output_byte_length: int
    removed_container_count: int
    removed_byte_count: int
    retained_top_level_chunk_count: int
    model_header_count: int
    model_reference_count: int
    model_identity_sha256: str
    hierarchy_header_count: int
    hierarchy_pivot_count: int
    hierarchy_identity_sha256: str
    mesh_header_count: int
    mesh_identity_sha256: str
    headers: tuple[W3DNoMotionHeaderProof, ...]
    proof_sha256: str

    def _basis(self) -> dict[str, object]:
        return {
            "schema": NO_MOTION_SCHEMA,
            "schemaVersion": NO_MOTION_SCHEMA_VERSION,
            "inputSha256": self.input_sha256,
            "outputSha256": self.output_sha256,
            "inputByteLength": self.input_byte_length,
            "outputByteLength": self.output_byte_length,
            "removedContainerCount": self.removed_container_count,
            "removedByteCount": self.removed_byte_count,
            "retainedTopLevelChunkCount": self.retained_top_level_chunk_count,
            "identities": {
                "modelHeaderCount": self.model_header_count,
                "modelReferenceCount": self.model_reference_count,
                "modelIdentitySha256": self.model_identity_sha256,
                "hierarchyHeaderCount": self.hierarchy_header_count,
                "hierarchyPivotCount": self.hierarchy_pivot_count,
                "hierarchyIdentitySha256": self.hierarchy_identity_sha256,
                "meshHeaderCount": self.mesh_header_count,
                "meshIdentitySha256": self.mesh_identity_sha256,
            },
            "headers": [header.neutral() for header in self.headers],
        }

    def neutral(self) -> dict[str, object]:
        return {**self._basis(), "proofSha256": self.proof_sha256}

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DNoMotionResult:
    proof: W3DNoMotionProof
    _output_bytes: bytes = field(repr=False, compare=False)

    def output_bytes(self) -> bytes:
        """Return the deterministic top-level concatenation."""

        return self._output_bytes


@dataclass(frozen=True, slots=True)
class _Chunk:
    kind: int
    start: int
    payload_start: int
    end: int
    depth: int
    flagged_container: bool
    children: tuple[_Chunk, ...]

    @property
    def byte_length(self) -> int:
        return self.end - self.start

    @property
    def payload_length(self) -> int:
        return self.end - self.payload_start


@dataclass(frozen=True, slots=True)
class _Header:
    identifier: str
    hierarchy_identifier: str
    version_major: int
    version_minor: int
    frame_count: int
    frame_rate: int
    compressed: bool
    flavor: int | None


@dataclass(frozen=True, slots=True)
class _ValidatedExpectation:
    identifier: str
    hierarchy_identifier: str
    frame_count: int
    frame_rate: int
    compressed: bool
    model_identifier: str | None
    flavor: int | None


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


def _source_bytes(value: object) -> bytes:
    if not isinstance(value, bytes):
        raise TypeError("W3D no-motion source must be bytes")
    if len(value) > MAX_W3D_BYTES:
        raise W3DNoMotionError(
            f"W3D source exceeds the {MAX_W3D_BYTES}-byte no-motion limit"
        )
    if not value:
        raise W3DNoMotionError("W3D source is empty")
    return value


def _identity(value: object, label: str) -> str:
    if not isinstance(value, str):
        raise TypeError(f"{label} must be a string")
    if not value:
        raise W3DNoMotionError(f"{label} is empty")
    if "\x00" in value:
        raise W3DNoMotionError(f"{label} contains NUL")
    try:
        encoded = value.encode("cp1252")
    except UnicodeEncodeError as exc:
        raise W3DNoMotionError(f"{label} is not CP1252") from exc
    if len(encoded) > 16:
        raise W3DNoMotionError(f"{label} exceeds the 16-byte W3D field")
    return value


def _positive_integer(value: object, label: str, *, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise TypeError(f"{label} must be a finite positive integer")
    if isinstance(value, float) and not math.isfinite(value):
        raise W3DNoMotionError(f"{label} must be a finite positive integer")
    if value <= 0 or int(value) != value:
        raise W3DNoMotionError(f"{label} must be a finite positive integer")
    integer = int(value)
    if integer > maximum:
        raise W3DNoMotionError(f"{label} exceeds {maximum}")
    return integer


def _nonnegative_integer(value: object, label: str, *, maximum: int) -> int:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise TypeError(f"{label} must be a finite non-negative integer")
    if isinstance(value, float) and not math.isfinite(value):
        raise W3DNoMotionError(f"{label} must be a finite non-negative integer")
    if value < 0 or int(value) != value:
        raise W3DNoMotionError(f"{label} must be a finite non-negative integer")
    integer = int(value)
    if integer > maximum:
        raise W3DNoMotionError(f"{label} exceeds {maximum}")
    return integer


def _flavor(value: object, *, compressed: bool) -> int | None:
    if not compressed:
        if value is not None:
            raise W3DNoMotionError("raw animation expectation forbids flavor")
        return None
    if value is None:
        raise W3DNoMotionError(
            "compressed animation expectation requires an exact flavor"
        )
    return _nonnegative_integer(value, "animation flavor", maximum=0xFFFF)


def _validate_expectations(
    values: Iterable[W3DNoMotionExpectation],
) -> tuple[_ValidatedExpectation, ...]:
    if isinstance(values, (str, bytes)):
        raise TypeError("W3D no-motion expectations must be an iterable")
    try:
        raw = tuple(values)
    except TypeError as exc:
        raise TypeError("W3D no-motion expectations must be an iterable") from exc
    if not raw:
        raise W3DNoMotionError("at least one animation expectation is required")

    result = []
    exact_ids: set[str] = set()
    folded_ids: set[str] = set()
    for index, item in enumerate(raw):
        if not isinstance(item, W3DNoMotionExpectation):
            raise TypeError(
                f"animation expectation {index} must be W3DNoMotionExpectation"
            )
        identifier = _identity(item.identifier, f"animation expectation {index} ID")
        hierarchy = _identity(
            item.hierarchy_identifier,
            f"animation expectation {index} hierarchy ID",
        )
        if type(item.compressed) is not bool:
            raise TypeError("animation expectation compressed must be a boolean")
        model = (
            _identity(item.model_identifier, f"animation expectation {index} model ID")
            if item.model_identifier is not None
            else None
        )
        if identifier in exact_ids or identifier.casefold() in folded_ids:
            raise W3DNoMotionError(
                f"ambiguous duplicate animation expectation: {identifier!r}"
            )
        exact_ids.add(identifier)
        folded_ids.add(identifier.casefold())
        result.append(
            _ValidatedExpectation(
                identifier=identifier,
                hierarchy_identifier=hierarchy,
                frame_count=_positive_integer(
                    item.frame_count,
                    f"animation expectation {identifier!r} frame count",
                    maximum=0xFFFFFFFF,
                ),
                frame_rate=_positive_integer(
                    item.frame_rate,
                    f"animation expectation {identifier!r} frame rate",
                    maximum=0xFFFFFFFF if not item.compressed else 0xFFFF,
                ),
                compressed=item.compressed,
                model_identifier=model,
                flavor=_flavor(item.flavor, compressed=item.compressed),
            )
        )
    return tuple(result)


def _parse_region(
    source: bytes,
    start: int,
    end: int,
    *,
    depth: int,
    counter: list[int],
) -> tuple[_Chunk, ...]:
    if depth > MAX_W3D_DEPTH:
        raise W3DNoMotionError(
            f"W3D chunk nesting exceeds the {MAX_W3D_DEPTH}-level limit"
        )
    result = []
    cursor = start
    while cursor < end:
        if counter[0] >= MAX_W3D_CHUNKS:
            raise W3DNoMotionError(
                f"W3D source exceeds the {MAX_W3D_CHUNKS}-chunk limit"
            )
        if end - cursor < _CHUNK_HEADER_SIZE:
            raise W3DNoMotionError(
                f"W3D region at depth {depth} has a truncated chunk header"
            )
        kind, raw_size = struct.unpack_from("<II", source, cursor)
        payload_length = raw_size & _SIZE_MASK
        payload_start = cursor + _CHUNK_HEADER_SIZE
        chunk_end = payload_start + payload_length
        if chunk_end > end:
            raise W3DNoMotionError(f"W3D chunk 0x{kind:08X} exceeds its owner boundary")
        flagged = bool(raw_size & _CONTAINER_FLAG)
        is_container = (
            flagged or kind in _KNOWN_CONTAINERS
        ) and not is_w3d_string_leaf(kind)
        counter[0] += 1
        children = (
            _parse_region(
                source,
                payload_start,
                chunk_end,
                depth=depth + 1,
                counter=counter,
            )
            if is_container and payload_length
            else ()
        )
        result.append(
            _Chunk(
                kind=kind,
                start=cursor,
                payload_start=payload_start,
                end=chunk_end,
                depth=depth,
                flagged_container=flagged,
                children=children,
            )
        )
        cursor = chunk_end
    if cursor != end:
        raise W3DNoMotionError("W3D chunk boundaries are inconsistent")
    return tuple(result)


def _fixed_string(raw: bytes, label: str) -> str:
    terminator = raw.find(b"\x00")
    if terminator < 0:
        value = raw
    else:
        value = raw[:terminator]
        if any(raw[terminator + 1 :]):
            raise W3DNoMotionError(f"{label} has non-NUL trailing bytes")
    if not value:
        raise W3DNoMotionError(f"{label} is empty")
    try:
        return value.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise W3DNoMotionError(f"{label} is not CP1252") from exc


def _animation_header(source: bytes, container: _Chunk) -> _Header:
    compressed = container.kind == W3D_CHUNK_COMPRESSED_ANIMATION
    expected_header = (
        W3D_CHUNK_COMPRESSED_ANIMATION_HEADER
        if compressed
        else W3D_CHUNK_ANIMATION_HEADER
    )
    if not container.flagged_container:
        raise W3DNoMotionError(
            f"animation container 0x{container.kind:08X} lacks its container flag"
        )
    if len(container.children) != 1:
        raise W3DNoMotionError(
            "animation container must contain exactly one header and no other chunks"
        )
    header = container.children[0]
    if header.kind != expected_header:
        raise W3DNoMotionError(
            "animation container does not contain its exact header kind"
        )
    if header.flagged_container or header.children:
        raise W3DNoMotionError("animation header cannot be a container")
    if header.payload_length != _ANIMATION_HEADER_SIZE:
        raise W3DNoMotionError(
            f"animation header must be exactly {_ANIMATION_HEADER_SIZE} bytes"
        )
    payload = source[header.payload_start : header.end]
    if compressed:
        raw_version, raw_id, raw_hierarchy, frame_count, frame_rate, flavor = (
            struct.unpack("<I16s16sIHH", payload)
        )
    else:
        raw_version, raw_id, raw_hierarchy, frame_count, frame_rate = struct.unpack(
            "<I16s16sII", payload
        )
        flavor = None
    if frame_count <= 0 or frame_rate <= 0:
        raise W3DNoMotionError(
            "animation header frame count and frame rate must both be positive"
        )
    return _Header(
        identifier=_fixed_string(raw_id, "animation identifier"),
        hierarchy_identifier=_fixed_string(
            raw_hierarchy, "animation hierarchy identifier"
        ),
        version_major=raw_version >> 16,
        version_minor=raw_version & 0xFFFF,
        frame_count=frame_count,
        frame_rate=frame_rate,
        compressed=compressed,
        flavor=flavor,
    )


def _walk(chunks: Iterable[_Chunk]) -> Iterable[_Chunk]:
    for chunk in chunks:
        yield chunk
        yield from _walk(chunk.children)


def _reject_unowned_animation_chunks(top_level: tuple[_Chunk, ...]) -> None:
    for top in top_level:
        if top.kind in _ANIMATION_CONTAINERS:
            for descendant in _walk(top.children):
                if descendant.kind in _ANIMATION_CONTAINERS:
                    raise W3DNoMotionError(
                        "animation container contains a nested animation container"
                    )
            continue
        for chunk in _walk((top,)):
            if chunk.kind in _ANIMATION_RELATED:
                raise W3DNoMotionError(
                    f"animation-related chunk 0x{chunk.kind:08X} is not owned by "
                    "a removable top-level animation container"
                )


def _record_basis(records: Iterable[object]) -> list[dict[str, object]]:
    result = []
    for record in records:
        neutral = getattr(record, "neutral")()
        if not isinstance(neutral, dict):
            raise W3DNoMotionError("W3D metadata record is not an object")
        value = dict(neutral)
        value.pop("provenance", None)
        result.append(value)
    return result


def _identity_bases(metadata: W3DMetadata) -> dict[str, object]:
    return {
        "models": {
            "headers": _record_basis(metadata.model_headers),
            "references": _record_basis(metadata.model_references),
        },
        "hierarchies": {
            "headers": _record_basis(metadata.hierarchy_headers),
            "pivots": _record_basis(metadata.hierarchy_pivots),
        },
        "meshes": _record_basis(metadata.mesh_headers),
    }


def _validate_bindings(
    metadata: W3DMetadata,
    expectations: tuple[_ValidatedExpectation, ...],
) -> None:
    expected_hierarchies = {item.hierarchy_identifier for item in expectations}
    hierarchy_ids = [item.identifier for item in metadata.hierarchy_headers]
    if hierarchy_ids or any(item.model_identifier is not None for item in expectations):
        if len(hierarchy_ids) != len(set(hierarchy_ids)):
            raise W3DNoMotionError("W3D contains duplicate hierarchy headers")
        if set(hierarchy_ids) != expected_hierarchies:
            raise W3DNoMotionError(
                "embedded hierarchy headers do not exactly match animation bindings"
            )

    expected_models: dict[str, str] = {}
    for item in expectations:
        if item.model_identifier is None:
            continue
        prior = expected_models.setdefault(
            item.model_identifier, item.hierarchy_identifier
        )
        if prior != item.hierarchy_identifier:
            raise W3DNoMotionError(
                f"model {item.model_identifier!r} has ambiguous hierarchy bindings"
            )

    source_models: dict[str, str] = {}
    for header in metadata.model_headers:
        if header.identifier in source_models:
            raise W3DNoMotionError("W3D contains duplicate model headers")
        source_models[header.identifier] = header.hierarchy_identifier
    if source_models != expected_models:
        raise W3DNoMotionError(
            "embedded model headers do not exactly match expected model bindings"
        )


def _validate_metadata_headers(
    metadata: W3DMetadata,
    parsed: tuple[tuple[_Chunk, _Header, _ValidatedExpectation], ...],
) -> None:
    if len(metadata.animation_headers) != len(parsed):
        raise W3DNoMotionError(
            "metadata scan does not agree with animation container count"
        )
    by_parent = {
        header.provenance.parent_chunk_header_offset: header
        for header in metadata.animation_headers
    }
    if len(by_parent) != len(metadata.animation_headers):
        raise W3DNoMotionError("animation headers have ambiguous container ownership")
    for container, header, _ in parsed:
        scanned = by_parent.get(container.start)
        if scanned is None:
            raise W3DNoMotionError(
                "metadata scan did not bind an animation header to its container"
            )
        scanned_tuple = (
            scanned.identifier,
            scanned.hierarchy_identifier,
            scanned.version_major,
            scanned.version_minor,
            scanned.frame_count,
            scanned.frame_rate,
            scanned.compressed,
            scanned.flavor,
        )
        parsed_tuple = (
            header.identifier,
            header.hierarchy_identifier,
            header.version_major,
            header.version_minor,
            header.frame_count,
            header.frame_rate,
            header.compressed,
            header.flavor,
        )
        if scanned_tuple != parsed_tuple:
            raise W3DNoMotionError(
                "metadata scan disagrees with the exact animation header"
            )


def strip_proven_header_only_animations(
    source: bytes,
    *,
    virtual_path: str,
    expectations: Iterable[W3DNoMotionExpectation],
) -> W3DNoMotionResult:
    """Remove only explicitly expected top-level header-only animations.

    ``expectations`` is intentionally mandatory and exact.  The function
    returns no output when any selected container has a motion-bearing or
    unknown child, when another animation-related chunk is present, or when
    embedded hierarchy/model bindings do not match the caller-owned contract.
    """

    data = _source_bytes(source)
    expected = _validate_expectations(expectations)
    top_level = _parse_region(data, 0, len(data), depth=0, counter=[0])
    _reject_unowned_animation_chunks(top_level)
    containers = tuple(
        chunk for chunk in top_level if chunk.kind in _ANIMATION_CONTAINERS
    )
    if len(containers) != len(expected):
        raise W3DNoMotionError(
            "top-level animation container count does not match expectations"
        )

    expected_by_id = {item.identifier: item for item in expected}
    parsed = []
    seen: set[str] = set()
    for container in containers:
        header = _animation_header(data, container)
        expectation = expected_by_id.get(header.identifier)
        if expectation is None:
            raise W3DNoMotionError(
                f"unexpected animation identifier: {header.identifier!r}"
            )
        if header.identifier in seen:
            raise W3DNoMotionError(
                f"duplicate animation container: {header.identifier!r}"
            )
        seen.add(header.identifier)
        actual = (
            header.hierarchy_identifier,
            header.frame_count,
            header.frame_rate,
            header.compressed,
            header.flavor,
        )
        requested = (
            expectation.hierarchy_identifier,
            expectation.frame_count,
            expectation.frame_rate,
            expectation.compressed,
            expectation.flavor,
        )
        if actual != requested:
            raise W3DNoMotionError(
                f"animation header does not exactly match expectation: "
                f"{header.identifier!r}"
            )
        parsed.append((container, header, expectation))
    if seen != set(expected_by_id):
        raise W3DNoMotionError("not every expected animation container was found")

    input_metadata = scan_w3d_metadata(data, virtual_path)
    parsed_tuple = tuple(parsed)
    _validate_metadata_headers(input_metadata, parsed_tuple)
    _validate_bindings(input_metadata, expected)
    input_identities = _identity_bases(input_metadata)

    removed_starts = {container.start for container in containers}
    retained = tuple(chunk for chunk in top_level if chunk.start not in removed_starts)
    output = b"".join(data[chunk.start : chunk.end] for chunk in retained)
    removed_bytes = sum(container.byte_length for container in containers)
    if len(data) - len(output) != removed_bytes:
        raise W3DNoMotionError("W3D no-motion byte delta is inconsistent")

    output_top_level = _parse_region(output, 0, len(output), depth=0, counter=[0])
    if any(chunk.kind in _ANIMATION_RELATED for chunk in _walk(output_top_level)):
        raise W3DNoMotionError("transformed W3D retained animation-related chunks")
    if tuple(data[chunk.start : chunk.end] for chunk in retained) != tuple(
        output[chunk.start : chunk.end] for chunk in output_top_level
    ):
        raise W3DNoMotionError("retained top-level W3D bytes changed")

    output_metadata = scan_w3d_metadata(output, virtual_path)
    if output_metadata.animation_headers:
        raise W3DNoMotionError("transformed W3D retained animation headers")
    output_identities = _identity_bases(output_metadata)
    if output_identities != input_identities:
        raise W3DNoMotionError(
            "model, hierarchy, or mesh identity changed during concatenation"
        )

    model_basis = input_identities["models"]
    hierarchy_basis = input_identities["hierarchies"]
    mesh_basis = input_identities["meshes"]
    headers = tuple(
        W3DNoMotionHeaderProof(
            identifier=header.identifier,
            hierarchy_identifier=header.hierarchy_identifier,
            model_identifier=expectation.model_identifier,
            version_major=header.version_major,
            version_minor=header.version_minor,
            frame_count=header.frame_count,
            frame_rate=header.frame_rate,
            compressed=header.compressed,
            flavor=header.flavor,
            top_level_ordinal=top_level.index(container),
            container_chunk_id=container.kind,
            container_byte_length=container.byte_length,
            container_sha256=_sha256(data[container.start : container.end]),
        )
        for container, header, expectation in parsed_tuple
    )
    proof = W3DNoMotionProof(
        input_sha256=_sha256(data),
        output_sha256=_sha256(output),
        input_byte_length=len(data),
        output_byte_length=len(output),
        removed_container_count=len(containers),
        removed_byte_count=removed_bytes,
        retained_top_level_chunk_count=len(retained),
        model_header_count=len(input_metadata.model_headers),
        model_reference_count=len(input_metadata.model_references),
        model_identity_sha256=_canonical_sha256(model_basis),
        hierarchy_header_count=len(input_metadata.hierarchy_headers),
        hierarchy_pivot_count=len(input_metadata.hierarchy_pivots),
        hierarchy_identity_sha256=_canonical_sha256(hierarchy_basis),
        mesh_header_count=len(input_metadata.mesh_headers),
        mesh_identity_sha256=_canonical_sha256(mesh_basis),
        headers=headers,
        proof_sha256="",
    )
    proof = replace(proof, proof_sha256=_canonical_sha256(proof._basis()))
    return W3DNoMotionResult(proof, output)


__all__ = [
    "NO_MOTION_SCHEMA",
    "NO_MOTION_SCHEMA_VERSION",
    "W3DNoMotionError",
    "W3DNoMotionExpectation",
    "W3DNoMotionHeaderProof",
    "W3DNoMotionProof",
    "W3DNoMotionResult",
    "strip_proven_header_only_animations",
]
