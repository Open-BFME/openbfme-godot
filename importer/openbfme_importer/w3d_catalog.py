"""Deterministic, bounded catalog scanning for caller-owned W3D bytes.

This module composes :func:`w3d_metadata.scan_w3d_metadata` with
:func:`w3d_index.build_w3d_index`.  It never opens a path, searches for a
replacement asset, or mutates caller data.  Every result is derived from the
supplied ``(virtual_path, bytes)`` records.

The default mode returns the strongest usable partial report: malformed paths,
per-file scanner limits, path collisions, and index-rejected header records are
retained as structured failures; structural W3D damage remains a structured
warning from the single-file scanner.  ``strict=True`` builds that same report
and then fails closed if any warning, failure, or duplicate logical ID exists.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
import hashlib
import json
from typing import Iterable, Mapping

from .paths import safe_relative_parts
from .w3d_index import W3DFileHeaders, W3DIndex, build_w3d_index
from .w3d_metadata import (
    MAX_W3D_METADATA_PATH_LENGTH,
    W3DMetadata,
    W3DMetadataLimitError,
    W3DMetadataWarning,
    W3DSourceProvenance,
    scan_w3d_metadata,
)


MAX_W3D_CATALOG_FILES = 250_000
MAX_W3D_CATALOG_TOTAL_BYTES = 64 * 1024 * 1024 * 1024
_SHA256_CHARACTERS = frozenset("0123456789abcdef")


class W3DCatalogLimitError(ValueError):
    """Raised when the aggregate caller input exceeds a hard catalog bound."""


@dataclass(frozen=True, slots=True)
class W3DCatalogSource:
    """Exact byte provenance for one caller-supplied source record."""

    supplied_virtual_path: str
    canonical_virtual_path: str | None
    byte_length: int
    source_sha256: str

    def input_neutral(self) -> dict[str, object]:
        """Return the exact source fields used by the aggregate input hash."""

        return {
            "suppliedVirtualPath": self.supplied_virtual_path,
            "byteLength": self.byte_length,
            "sourceSha256": self.source_sha256,
        }

    def neutral(self) -> dict[str, object]:
        value = self.input_neutral()
        if self.canonical_virtual_path is not None:
            value["canonicalVirtualPath"] = self.canonical_virtual_path
        return value


@dataclass(frozen=True, slots=True)
class W3DCatalogWarning:
    """One single-file metadata warning bound to exact source bytes."""

    source: W3DCatalogSource
    diagnostic: W3DMetadataWarning

    def neutral(self) -> dict[str, object]:
        return {
            "source": self.source.neutral(),
            "diagnostic": self.diagnostic.neutral(),
        }


@dataclass(frozen=True, slots=True)
class W3DCatalogFailure:
    """A source that could not safely participate in part of the catalog."""

    source: W3DCatalogSource
    phase: str
    code: str
    message: str

    def neutral(self) -> dict[str, object]:
        return {
            "source": self.source.neutral(),
            "phase": self.phase,
            "code": self.code,
            "message": self.message,
        }


@dataclass(frozen=True, slots=True)
class W3DChunkCount:
    """Aggregate count for one exact W3D chunk ID/classification."""

    chunk_id: int
    chunk_name: str | None
    classification: str
    count: int

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "chunkId": self.chunk_id,
            "chunkIdHex": f"0x{self.chunk_id:08X}",
            "classification": self.classification,
            "count": self.count,
        }
        if self.chunk_name is not None:
            value["chunkName"] = self.chunk_name
        return value


@dataclass(frozen=True, slots=True)
class W3DLogicalIdOccurrence:
    """One authored logical header ID with byte-offset provenance."""

    identifier: str
    source_sha256: str
    provenance: W3DSourceProvenance

    def neutral(self) -> dict[str, object]:
        return {
            "identifier": self.identifier,
            "sourceSha256": self.source_sha256,
            "provenance": self.provenance.neutral(),
        }


@dataclass(frozen=True, slots=True)
class W3DLogicalIdDuplicate:
    """A case-insensitive authored ID occurring more than once."""

    kind: str
    identifier: str
    occurrences: tuple[W3DLogicalIdOccurrence, ...]

    @property
    def scope(self) -> str:
        physical_sources = {
            (
                item.provenance.virtual_path,
                item.source_sha256,
            )
            for item in self.occurrences
        }
        return "within-file" if len(physical_sources) == 1 else "across-files"

    def neutral(self) -> dict[str, object]:
        return {
            "kind": self.kind,
            "identifier": self.identifier,
            "scope": self.scope,
            "occurrences": [item.neutral() for item in self.occurrences],
        }


@dataclass(frozen=True, slots=True)
class W3DCatalogReport:
    """Immutable catalog evidence and the exact index built from clean files."""

    sources: tuple[W3DCatalogSource, ...]
    files: tuple[W3DMetadata, ...]
    file_headers: tuple[W3DFileHeaders, ...]
    index: W3DIndex | None
    chunk_counts: tuple[W3DChunkCount, ...]
    asset_family_counts: tuple[tuple[str, int], ...]
    warnings: tuple[W3DCatalogWarning, ...]
    failures: tuple[W3DCatalogFailure, ...]
    duplicate_logical_ids: tuple[W3DLogicalIdDuplicate, ...]
    max_files: int
    max_total_bytes: int
    total_input_bytes: int
    input_sha256: str
    metadata_sha256: str

    @property
    def complete(self) -> bool:
        """Whether this report is suitable for strict downstream conversion."""

        return (
            self.index is not None
            and not self.warnings
            and not self.failures
            and not self.duplicate_logical_ids
            and len(self.index.virtual_paths) == len(self.sources)
        )

    @property
    def indexed_file_count(self) -> int:
        return len(self.index.virtual_paths) if self.index is not None else 0

    def metadata_hash_basis(self) -> dict[str, object]:
        """Return the canonical neutral value hashed as ``metadata_sha256``."""

        return self._neutral(include_metadata_hash=False)

    def neutral(self) -> dict[str, object]:
        return self._neutral(include_metadata_hash=True)

    def _neutral(self, *, include_metadata_hash: bool) -> dict[str, object]:
        hashes = {"inputSha256": self.input_sha256}
        if include_metadata_hash:
            hashes["metadataSha256"] = self.metadata_sha256
        return {
            "schema": "openbfme.w3d-catalog",
            "schemaVersion": 0,
            "bounds": {
                "maxFiles": self.max_files,
                "maxTotalBytes": self.max_total_bytes,
            },
            "summary": {
                "inputFileCount": len(self.sources),
                "totalInputBytes": self.total_input_bytes,
                "scannedFileCount": len(self.files),
                "indexedFileCount": self.indexed_file_count,
                "chunkCount": sum(item.count for item in self.chunk_counts),
                "warningCount": len(self.warnings),
                "failureCount": len(self.failures),
                "duplicateLogicalIdCount": len(self.duplicate_logical_ids),
                "complete": self.complete,
            },
            "hashes": hashes,
            "sources": [item.neutral() for item in self.sources],
            "files": [item.neutral() for item in self.files],
            "fileHeaders": [item.neutral() for item in self.file_headers],
            "index": self.index.neutral() if self.index is not None else None,
            "chunkCounts": [item.neutral() for item in self.chunk_counts],
            "assetFamilyCounts": dict(self.asset_family_counts),
            "warnings": [item.neutral() for item in self.warnings],
            "failures": [item.neutral() for item in self.failures],
            "duplicateLogicalIds": [
                item.neutral() for item in self.duplicate_logical_ids
            ],
        }


class W3DCatalogStrictError(ValueError):
    """Raised after producing a non-clean strict catalog report."""

    def __init__(self, report: W3DCatalogReport):
        self.report = report
        super().__init__(
            "strict W3D catalog rejected "
            f"{len(report.warnings)} warning(s), "
            f"{len(report.failures)} failure(s), and "
            f"{len(report.duplicate_logical_ids)} duplicate logical ID(s)"
        )


@dataclass(frozen=True, slots=True)
class _InputSource:
    supplied_virtual_path: str
    source: bytes
    source_sha256: str

    @property
    def byte_length(self) -> int:
        return len(self.source)

    def provenance(self, canonical_virtual_path: str | None) -> W3DCatalogSource:
        return W3DCatalogSource(
            supplied_virtual_path=self.supplied_virtual_path,
            canonical_virtual_path=canonical_virtual_path,
            byte_length=self.byte_length,
            source_sha256=self.source_sha256,
        )


@dataclass(frozen=True, slots=True)
class _ScannedSource:
    source: W3DCatalogSource
    metadata: W3DMetadata


_ASSET_FAMILY_FIELDS = (
    ("animations", "animation_headers"),
    ("collisionBoxes", "collision_boxes"),
    ("hierarchies", "hierarchy_headers"),
    ("hierarchyPivots", "hierarchy_pivots"),
    ("indexReferences", "index_references"),
    ("materialInfos", "material_infos"),
    ("materialStrings", "material_strings"),
    ("meshes", "mesh_headers"),
    ("modelHeaders", "model_headers"),
    ("modelReferences", "model_references"),
    ("secondaryGeometryStreams", "secondary_geometry_streams"),
    ("shaderMaterialHeaders", "shader_material_headers"),
    ("shaderMaterialProperties", "shader_material_properties"),
    ("shaders", "shaders"),
    ("textureInfos", "texture_infos"),
    ("textures", "texture_references"),
    ("vertexMaterialInfos", "vertex_material_infos"),
)


def _sort_text(value: str) -> tuple[str, str]:
    return (value.casefold(), value)


def _source_sort_key(source: W3DCatalogSource) -> tuple[object, ...]:
    return (
        source.supplied_virtual_path.casefold(),
        source.supplied_virtual_path,
        source.source_sha256,
        source.byte_length,
        source.canonical_virtual_path or "",
    )


def _metadata_sort_key(metadata: W3DMetadata) -> tuple[object, ...]:
    return (
        metadata.virtual_path.casefold(),
        metadata.virtual_path,
        metadata.source_sha256,
        metadata.byte_length,
    )


def _canonical_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def catalog_source_id(source: W3DCatalogSource) -> str:
    """Return the canonical opaque ID for one physical catalog source.

    The UTF-8 JSON form is deliberately defined here, beside the source
    contract, so every downstream evidence domain hashes non-ASCII paths in
    exactly the same way.  The field names and trailing newline preserve the
    legacy planner/texture IDs for all canonical ASCII paths.
    """

    if not isinstance(source, W3DCatalogSource):
        raise TypeError("catalog source ID input must be a W3DCatalogSource")
    path = source.canonical_virtual_path or source.supplied_virtual_path
    if not isinstance(path, str) or not path:
        raise ValueError("catalog source ID path is invalid")
    if len(path) > MAX_W3D_METADATA_PATH_LENGTH:
        raise ValueError("catalog source ID path exceeds its length bound")
    try:
        canonical_path = "/".join(safe_relative_parts(path))
    except (TypeError, ValueError) as exc:
        raise ValueError("catalog source ID path is unsafe") from exc
    if canonical_path != path or "\\" in path:
        raise ValueError("catalog source ID path is not canonical")
    if not canonical_path.casefold().endswith(".w3d"):
        raise ValueError("catalog source ID path is not a W3D path")
    if (
        isinstance(source.byte_length, bool)
        or not isinstance(source.byte_length, int)
        or source.byte_length < 0
    ):
        raise ValueError("catalog source ID byte length is invalid")
    digest = source.source_sha256
    if (
        not isinstance(digest, str)
        or len(digest) != 64
        or digest != digest.casefold()
        or any(character not in _SHA256_CHARACTERS for character in digest)
    ):
        raise ValueError("catalog source ID SHA-256 is not canonical lowercase")
    try:
        payload = (
            json.dumps(
                {
                    "physicalVirtualPath": canonical_path,
                    "sourceSha256": digest,
                    "byteLength": source.byte_length,
                },
                ensure_ascii=False,
                sort_keys=True,
                separators=(",", ":"),
            )
            + "\n"
        ).encode("utf-8")
    except UnicodeEncodeError as exc:
        raise ValueError("catalog source ID path is not canonical UTF-8") from exc
    return f"src-{hashlib.sha256(payload).hexdigest()[:32]}"


def _selected_limit(value: int | None, hard_limit: int, label: str) -> int:
    selected = hard_limit if value is None else value
    if isinstance(selected, bool) or not isinstance(selected, int):
        raise TypeError(f"W3D catalog {label} limit must be an integer")
    if not 1 <= selected <= hard_limit:
        raise ValueError(f"W3D catalog {label} limit must be 1..{hard_limit}")
    return selected


def _materialize_inputs(
    values: Mapping[str, bytes] | Iterable[tuple[str, bytes]],
    *,
    max_files: int,
    max_total_bytes: int,
) -> tuple[tuple[_InputSource, ...], int]:
    if isinstance(values, Mapping):
        selected_values: Iterable[object] = values.items()
    else:
        if isinstance(values, (str, bytes, bytearray)):
            raise TypeError("W3D catalog input must be a mapping or iterable of pairs")
        try:
            selected_values = iter(values)
        except TypeError as exc:
            raise TypeError(
                "W3D catalog input must be a mapping or iterable of pairs"
            ) from exc

    result: list[_InputSource] = []
    total_bytes = 0
    for raw_item in selected_values:
        if not isinstance(raw_item, tuple) or len(raw_item) != 2:
            raise TypeError("W3D catalog entries must be (virtual_path, bytes) pairs")
        raw_path, source = raw_item
        if not isinstance(raw_path, str):
            raise TypeError("W3D catalog virtual path must be a string")
        if not isinstance(source, bytes):
            raise TypeError("W3D catalog source must be bytes")
        if len(result) >= max_files:
            raise W3DCatalogLimitError(f"W3D catalog file count exceeds {max_files}")
        total_bytes += len(source)
        if total_bytes > max_total_bytes:
            raise W3DCatalogLimitError(
                f"W3D catalog total bytes exceed {max_total_bytes}"
            )
        result.append(
            _InputSource(
                supplied_virtual_path=raw_path,
                source=source,
                source_sha256=hashlib.sha256(source).hexdigest(),
            )
        )
    if not result:
        raise W3DCatalogLimitError(f"W3D catalog file count must be 1..{max_files}")
    result.sort(
        key=lambda item: (
            item.supplied_virtual_path.casefold(),
            item.supplied_virtual_path,
            item.source_sha256,
            item.byte_length,
        )
    )
    return tuple(result), total_bytes


def _warning_sort_key(item: W3DCatalogWarning) -> tuple[object, ...]:
    diagnostic = item.diagnostic
    return (
        *_source_sort_key(item.source),
        diagnostic.offset,
        diagnostic.code,
        diagnostic.chunk_id if diagnostic.chunk_id is not None else -1,
        diagnostic.parent_chunk_header_offset
        if diagnostic.parent_chunk_header_offset is not None
        else -1,
        diagnostic.message,
    )


def _failure_sort_key(item: W3DCatalogFailure) -> tuple[object, ...]:
    return (
        *_source_sort_key(item.source),
        item.phase,
        item.code,
        item.message,
    )


def _header_occurrences(
    scanned: Iterable[_ScannedSource],
) -> dict[tuple[str, str], list[W3DLogicalIdOccurrence]]:
    grouped: dict[tuple[str, str], list[W3DLogicalIdOccurrence]] = {}
    for item in scanned:
        metadata = item.metadata
        records = (
            ("model", tuple(metadata.model_headers) + tuple(metadata.mesh_headers)),
            ("animation", metadata.animation_headers),
            ("hierarchy", metadata.hierarchy_headers),
        )
        for kind, headers in records:
            for header in headers:
                identifier = header.identifier
                if not identifier:
                    continue
                grouped.setdefault((kind, identifier.casefold()), []).append(
                    W3DLogicalIdOccurrence(
                        identifier=identifier,
                        source_sha256=metadata.source_sha256,
                        provenance=header.provenance,
                    )
                )
    return grouped


def _occurrence_sort_key(
    item: W3DLogicalIdOccurrence,
) -> tuple[object, ...]:
    provenance = item.provenance
    return (
        provenance.virtual_path.casefold(),
        provenance.virtual_path,
        item.source_sha256,
        provenance.value_offset,
        provenance.chunk_header_offset,
        item.identifier.casefold(),
        item.identifier,
    )


def _logical_duplicates(
    scanned: Iterable[_ScannedSource],
) -> tuple[W3DLogicalIdDuplicate, ...]:
    result: list[W3DLogicalIdDuplicate] = []
    for (kind, _), raw_occurrences in _header_occurrences(scanned).items():
        if len(raw_occurrences) < 2:
            continue
        occurrences = tuple(sorted(raw_occurrences, key=_occurrence_sort_key))
        result.append(
            W3DLogicalIdDuplicate(
                kind=kind,
                identifier=min(
                    (item.identifier for item in occurrences), key=_sort_text
                ),
                occurrences=occurrences,
            )
        )
    result.sort(
        key=lambda item: (
            item.kind,
            item.identifier.casefold(),
            item.identifier,
            item.scope,
        )
    )
    return tuple(result)


def _local_duplicate_keys(metadata: W3DMetadata) -> set[tuple[str, str]]:
    result: set[tuple[str, str]] = set()
    for key, occurrences in _header_occurrences(
        (
            _ScannedSource(
                W3DCatalogSource(
                    metadata.virtual_path,
                    metadata.virtual_path,
                    metadata.byte_length,
                    metadata.source_sha256,
                ),
                metadata,
            ),
        )
    ).items():
        if len(occurrences) > 1:
            result.add(key)
    return result


def _chunk_counts(files: Iterable[W3DMetadata]) -> tuple[W3DChunkCount, ...]:
    grouped: dict[tuple[int, str | None, str], int] = {}
    for metadata in files:
        for chunk in metadata.chunks:
            key = (chunk.chunk_id, chunk.chunk_name, chunk.classification)
            grouped[key] = grouped.get(key, 0) + 1
    return tuple(
        W3DChunkCount(chunk_id, chunk_name, classification, count)
        for (chunk_id, chunk_name, classification), count in sorted(
            grouped.items(),
            key=lambda item: (
                item[0][0],
                item[0][1] or "",
                item[0][2],
            ),
        )
    )


def _asset_family_counts(
    files: Iterable[W3DMetadata],
) -> tuple[tuple[str, int], ...]:
    selected = tuple(files)
    return tuple(
        (
            family,
            sum(len(getattr(metadata, attribute)) for metadata in selected),
        )
        for family, attribute in _ASSET_FAMILY_FIELDS
    )


def scan_w3d_catalog(
    values: Mapping[str, bytes] | Iterable[tuple[str, bytes]],
    *,
    strict: bool = False,
    max_files: int | None = None,
    max_total_bytes: int | None = None,
) -> W3DCatalogReport:
    """Scan and index caller-supplied W3D sources without filesystem access.

    ``max_files`` and ``max_total_bytes`` may lower, but never raise, the hard
    module bounds.  Aggregate bound and input-shape violations raise before any
    scan.  Per-file damage is retained in the returned report unless ``strict``
    requests fail-closed behavior.
    """

    if not isinstance(strict, bool):
        raise TypeError("W3D catalog strict flag must be a boolean")
    selected_max_files = _selected_limit(max_files, MAX_W3D_CATALOG_FILES, "file count")
    selected_max_bytes = _selected_limit(
        max_total_bytes, MAX_W3D_CATALOG_TOTAL_BYTES, "total byte"
    )
    inputs, total_input_bytes = _materialize_inputs(
        values,
        max_files=selected_max_files,
        max_total_bytes=selected_max_bytes,
    )

    raw_input_sources = tuple(item.provenance(None) for item in inputs)
    input_sha256 = _canonical_sha256(
        {
            "schema": "openbfme.w3d-catalog-input",
            "schemaVersion": 0,
            "sources": [item.input_neutral() for item in raw_input_sources],
        }
    )

    scanned: list[_ScannedSource] = []
    sources: list[W3DCatalogSource] = []
    failures: list[W3DCatalogFailure] = []
    for item in inputs:
        try:
            metadata = scan_w3d_metadata(item.source, item.supplied_virtual_path)
        except W3DMetadataLimitError as exc:
            source = item.provenance(None)
            sources.append(source)
            failures.append(
                W3DCatalogFailure(
                    source=source,
                    phase="scan",
                    code="metadata-limit",
                    message=str(exc),
                )
            )
            continue
        except ValueError as exc:
            source = item.provenance(None)
            sources.append(source)
            failures.append(
                W3DCatalogFailure(
                    source=source,
                    phase="scan",
                    code="invalid-virtual-path",
                    message=str(exc),
                )
            )
            continue
        source = item.provenance(metadata.virtual_path)
        sources.append(source)
        scanned.append(_ScannedSource(source, metadata))

    path_groups: dict[str, list[int]] = {}
    for index, item in enumerate(scanned):
        path_groups.setdefault(item.metadata.virtual_path.casefold(), []).append(index)
    path_rejected: set[int] = set()
    for indices in path_groups.values():
        if len(indices) < 2:
            continue
        canonical_paths = {scanned[index].metadata.virtual_path for index in indices}
        code = (
            "duplicate-virtual-path"
            if len(canonical_paths) == 1
            else "case-colliding-virtual-path"
        )
        rendered_paths = ", ".join(
            repr(scanned[index].source.supplied_virtual_path) for index in indices
        )
        message = f"W3D catalog {code.replace('-', ' ')}: {rendered_paths}"
        for index in indices:
            path_rejected.add(index)
            failures.append(
                W3DCatalogFailure(
                    source=scanned[index].source,
                    phase="index",
                    code=code,
                    message=message,
                )
            )

    indexable: list[_ScannedSource] = []
    for position, item in enumerate(scanned):
        if position in path_rejected:
            continue
        headers = item.metadata.file_headers()
        local_duplicates = _local_duplicate_keys(item.metadata)
        try:
            # Validate the exact W3DFileHeaders bridge per file so partial mode
            # can identify the source that the aggregate index must exclude.
            build_w3d_index((item.metadata.virtual_path,), (headers,))
        except ValueError as exc:
            failures.append(
                W3DCatalogFailure(
                    source=item.source,
                    phase="index",
                    code=(
                        "duplicate-logical-id-within-file"
                        if local_duplicates
                        else "index-rejected-header-metadata"
                    ),
                    message=str(exc),
                )
            )
            continue
        indexable.append(item)

    if indexable:
        index = build_w3d_index(
            (item.metadata.virtual_path for item in indexable),
            (item.metadata.file_headers() for item in indexable),
        )
        file_headers = index.file_headers
    else:
        index = None
        file_headers = ()

    sources_tuple = tuple(sorted(sources, key=_source_sort_key))
    files_tuple = tuple(
        sorted((item.metadata for item in scanned), key=_metadata_sort_key)
    )
    warnings = tuple(
        sorted(
            (
                W3DCatalogWarning(item.source, warning)
                for item in scanned
                for warning in item.metadata.warnings
            ),
            key=_warning_sort_key,
        )
    )
    failures_tuple = tuple(sorted(failures, key=_failure_sort_key))
    duplicate_logical_ids = _logical_duplicates(scanned)
    chunk_counts = _chunk_counts(files_tuple)
    asset_family_counts = _asset_family_counts(files_tuple)

    provisional = W3DCatalogReport(
        sources=sources_tuple,
        files=files_tuple,
        file_headers=file_headers,
        index=index,
        chunk_counts=chunk_counts,
        asset_family_counts=asset_family_counts,
        warnings=warnings,
        failures=failures_tuple,
        duplicate_logical_ids=duplicate_logical_ids,
        max_files=selected_max_files,
        max_total_bytes=selected_max_bytes,
        total_input_bytes=total_input_bytes,
        input_sha256=input_sha256,
        metadata_sha256="",
    )
    report = replace(
        provisional,
        metadata_sha256=_canonical_sha256(provisional.metadata_hash_basis()),
    )
    if strict and not report.complete:
        raise W3DCatalogStrictError(report)
    return report


# Concise alias for callers already operating in a W3D-specific importer path.
scan_catalog = scan_w3d_catalog
