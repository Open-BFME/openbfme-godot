"""Deterministic, payload-free indexing of physical W3D leaves.

This module deliberately does not parse W3D bytes.  Callers provide the
catalogue's virtual paths and, when available, the identifiers already read
from each file's headers.  Resolution is exact and case-insensitive, retains
the evidence used for every answer, and never chooses between multiple
physical candidates.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePosixPath
from types import MappingProxyType
from typing import Iterable, Mapping, Sequence

from .paths import safe_relative_parts


MAX_W3D_PATHS = 250_000
MAX_W3D_HEADER_FILES = 250_000
MAX_W3D_HEADER_IDS_PER_FILE = 16_384
MAX_W3D_TOTAL_HEADER_IDS = 1_000_000
MAX_W3D_REQUESTS = 100_000
MAX_W3D_PATH_LENGTH = 512
MAX_W3D_IDENTIFIER_LENGTH = 255

W3D_REFERENCE_KINDS = frozenset({"model", "animation", "hierarchy", "file"})
W3D_HEADER_KINDS = ("model", "animation", "hierarchy")
_KIND_ORDER = {"model": 0, "animation": 1, "hierarchy": 2, "file": 3}
_FORBIDDEN_IDENTIFIER_CHARACTERS = frozenset('/\\:*?"<>|')


@dataclass(frozen=True, slots=True)
class W3DFileHeaders:
    """Caller-supplied logical header identifiers for one physical file."""

    virtual_path: str
    model_ids: tuple[str, ...] = ()
    animation_ids: tuple[str, ...] = ()
    hierarchy_ids: tuple[str, ...] = ()

    def neutral(self) -> dict[str, object]:
        return {
            "virtualPath": self.virtual_path,
            "modelIds": list(self.model_ids),
            "animationIds": list(self.animation_ids),
            "hierarchyIds": list(self.hierarchy_ids),
        }


@dataclass(frozen=True, slots=True)
class W3DHeaderDuplicate:
    """One case-insensitive header ID authored by more than one file."""

    kind: str
    identifier: str
    physical_virtual_paths: tuple[str, ...]

    def neutral(self) -> dict[str, object]:
        return {
            "kind": self.kind,
            "identifier": self.identifier,
            "physicalVirtualPaths": list(self.physical_virtual_paths),
        }


@dataclass(frozen=True, slots=True)
class W3DReferenceRequest:
    """A typed logical reference from SAGE data.

    ``semantic`` is intentionally per request.  Tokens such as ``None`` and
    ``MODEL`` have no implicit meaning in this module; a caller that has
    source-level proof that a token is non-physical must say so explicitly.
    """

    kind: str
    identifier: str
    semantic: bool = False


@dataclass(frozen=True, slots=True)
class W3DMatchEvidence:
    """One exact rule that connected a logical reference to a candidate."""

    rule: str
    matched_value: str
    header_kind: str | None = None

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "rule": self.rule,
            "matchedValue": self.matched_value,
        }
        if self.header_kind is not None:
            result["headerKind"] = self.header_kind
        return result


@dataclass(frozen=True, slots=True)
class W3DResolutionCandidate:
    """A physical leaf and all exact evidence supporting that candidate."""

    physical_virtual_path: str
    logical_id: str
    container_id: str | None
    subobject_id: str | None
    header_id: str | None
    evidence: tuple[W3DMatchEvidence, ...]

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "physicalVirtualPath": self.physical_virtual_path,
            "logicalId": self.logical_id,
            "evidence": [item.neutral() for item in self.evidence],
        }
        if self.container_id is not None:
            result["containerId"] = self.container_id
        if self.subobject_id is not None:
            result["subobjectId"] = self.subobject_id
        if self.header_id is not None:
            result["headerId"] = self.header_id
        return result


@dataclass(frozen=True, slots=True)
class W3DResolvedReference:
    """A resolved physical leaf, or an explicitly classified semantic token."""

    kind: str
    requested_id: str
    physical_virtual_path: str | None
    logical_id: str
    container_id: str | None
    subobject_id: str | None
    header_id: str | None
    evidence: tuple[W3DMatchEvidence, ...]
    semantic: bool = False

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "kind": self.kind,
            "requestedId": self.requested_id,
            "logicalId": self.logical_id,
            "semantic": self.semantic,
            "evidence": [item.neutral() for item in self.evidence],
        }
        if self.physical_virtual_path is not None:
            result["physicalVirtualPath"] = self.physical_virtual_path
        if self.container_id is not None:
            result["containerId"] = self.container_id
        if self.subobject_id is not None:
            result["subobjectId"] = self.subobject_id
        if self.header_id is not None:
            result["headerId"] = self.header_id
        return result


@dataclass(frozen=True, slots=True)
class W3DUnresolvedReference:
    """A missing or ambiguous typed reference with retained candidates."""

    kind: str
    requested_id: str
    reason: str
    candidates: tuple[W3DResolutionCandidate, ...] = ()

    def neutral(self) -> dict[str, object]:
        return {
            "kind": self.kind,
            "requestedId": self.requested_id,
            "reason": self.reason,
            "candidates": [item.neutral() for item in self.candidates],
        }


@dataclass(frozen=True, slots=True)
class W3DBatchResolution:
    """Canonical batch result that keeps all unresolved diagnostics."""

    resolved: tuple[W3DResolvedReference, ...]
    missing: tuple[W3DUnresolvedReference, ...]
    ambiguous: tuple[W3DUnresolvedReference, ...]

    @property
    def complete(self) -> bool:
        return not self.missing and not self.ambiguous

    @property
    def semantic(self) -> tuple[W3DResolvedReference, ...]:
        return tuple(item for item in self.resolved if item.semantic)

    @property
    def physical(self) -> tuple[W3DResolvedReference, ...]:
        return tuple(item for item in self.resolved if not item.semantic)

    def neutral(self) -> dict[str, object]:
        return {
            "resolved": [item.neutral() for item in self.resolved],
            "missing": [item.neutral() for item in self.missing],
            "ambiguous": [item.neutral() for item in self.ambiguous],
        }


class W3DResolutionError(ValueError):
    """Raised by exact resolution while preserving the failed diagnostic."""

    def __init__(self, diagnostic: W3DUnresolvedReference):
        self.diagnostic = diagnostic
        super().__init__(
            f"{diagnostic.reason} W3D {diagnostic.kind} reference: "
            f"{diagnostic.requested_id!r}"
        )


def _sort_text(value: str) -> tuple[str, str]:
    return value.casefold(), value


def _sort_request(request: W3DReferenceRequest) -> tuple[int, str, str]:
    return (
        _KIND_ORDER[request.kind],
        request.identifier.casefold(),
        request.identifier,
    )


def _safe_w3d_path(value: str, label: str) -> str:
    if not isinstance(value, str):
        raise TypeError(f"{label} must be a string")
    if len(value) > MAX_W3D_PATH_LENGTH:
        raise ValueError(f"{label} exceeds {MAX_W3D_PATH_LENGTH} character limit")
    try:
        normalized = "/".join(safe_relative_parts(value))
    except ValueError as exc:
        raise ValueError(f"unsafe {label}: {value!r}") from exc
    if not normalized.casefold().endswith(".w3d"):
        raise ValueError(f"{label} must end in .w3d: {value!r}")
    return normalized


def _safe_identifier(value: str, label: str) -> str:
    if not isinstance(value, str):
        raise TypeError(f"{label} must be a string")
    if (
        not value
        or value != value.strip()
        or len(value) > MAX_W3D_IDENTIFIER_LENGTH
        or value in {".", ".."}
        or value.startswith(".")
        or value.endswith(".")
        or ".." in value
        or any(character in _FORBIDDEN_IDENTIFIER_CHARACTERS for character in value)
        or any(ord(character) < 32 or ord(character) > 126 for character in value)
    ):
        raise ValueError(f"unsafe {label}: {value!r}")
    return value


def _materialize_ids(
    values: Sequence[str], *, kind: str, virtual_path: str
) -> tuple[str, ...]:
    if isinstance(values, (str, bytes)) or not isinstance(values, Sequence):
        raise TypeError(f"{kind} header IDs for {virtual_path!r} must be a sequence")
    if len(values) > MAX_W3D_HEADER_IDS_PER_FILE:
        raise ValueError(
            f"{kind} header ID count for {virtual_path!r} exceeds "
            f"{MAX_W3D_HEADER_IDS_PER_FILE}"
        )
    result: list[str] = []
    seen: dict[str, str] = {}
    for raw_identifier in values:
        identifier = _safe_identifier(raw_identifier, f"{kind} header ID")
        key = identifier.casefold()
        if key in seen:
            raise ValueError(
                f"duplicate {kind} header ID in {virtual_path!r}: {identifier!r}"
            )
        seen[key] = identifier
        result.append(identifier)
    result.sort(key=_sort_text)
    return tuple(result)


class W3DIndex:
    """Immutable logical-to-physical index built by :func:`build_w3d_index`."""

    __slots__ = (
        "virtual_paths",
        "file_headers",
        "duplicate_header_ids",
        "_by_path",
        "_by_basename",
        "_by_stem",
        "_by_header",
    )

    def __init__(
        self,
        *,
        virtual_paths: tuple[str, ...],
        file_headers: tuple[W3DFileHeaders, ...],
        duplicate_header_ids: tuple[W3DHeaderDuplicate, ...],
        by_path: Mapping[str, str],
        by_basename: Mapping[str, tuple[str, ...]],
        by_stem: Mapping[str, tuple[str, ...]],
        by_header: Mapping[str, Mapping[str, tuple[tuple[str, str], ...]]],
    ):
        self.virtual_paths = virtual_paths
        self.file_headers = file_headers
        self.duplicate_header_ids = duplicate_header_ids
        self._by_path = MappingProxyType(dict(by_path))
        self._by_basename = MappingProxyType(dict(by_basename))
        self._by_stem = MappingProxyType(dict(by_stem))
        self._by_header = MappingProxyType(
            {
                kind: MappingProxyType(dict(values))
                for kind, values in by_header.items()
            }
        )

    def neutral(self) -> dict[str, object]:
        """Return a deterministic, source-byte-free snapshot of the index."""

        return {
            "schema": "openbfme.w3d-physical-index",
            "schemaVersion": 0,
            "virtualPaths": list(self.virtual_paths),
            "fileHeaders": [item.neutral() for item in self.file_headers],
            "duplicateHeaderIds": [
                item.neutral() for item in self.duplicate_header_ids
            ],
        }

    def resolve_partial(
        self, requests: Iterable[W3DReferenceRequest]
    ) -> W3DBatchResolution:
        return resolve_w3d_references_partial(self, requests)

    def resolve_exact(
        self, kind: str, identifier: str, *, semantic: bool = False
    ) -> W3DResolvedReference:
        return resolve_w3d_reference(
            self, kind, identifier, semantic=semantic
        )


def build_w3d_index(
    virtual_paths: Iterable[str],
    file_headers: Iterable[W3DFileHeaders] | None = None,
    *,
    reject_duplicate_header_ids: bool = False,
) -> W3DIndex:
    """Build a bounded physical index from caller-owned neutral metadata.

    A duplicate header ID inside one file is malformed and always rejected.
    The same ID in different files is retained as an ambiguity, exposed via
    ``duplicate_header_ids`` and rejected by exact resolution.  Callers that
    need a globally unique header namespace can request strict rejection at
    construction time.
    """

    selected_paths = list(virtual_paths)
    if not 1 <= len(selected_paths) <= MAX_W3D_PATHS:
        raise ValueError(f"W3D path count must be 1..{MAX_W3D_PATHS}")

    by_path: dict[str, str] = {}
    paths: list[str] = []
    for raw_path in selected_paths:
        path = _safe_w3d_path(raw_path, "W3D virtual path")
        key = path.casefold()
        previous = by_path.get(key)
        if previous is not None:
            if previous == path:
                raise ValueError(f"duplicate W3D virtual path: {path!r}")
            raise ValueError(
                f"case-ambiguous W3D virtual paths: {previous!r}, {path!r}"
            )
        by_path[key] = path
        paths.append(path)
    paths.sort(key=_sort_text)

    basename_lists: dict[str, list[str]] = {}
    stem_lists: dict[str, list[str]] = {}
    for path in paths:
        basename = PurePosixPath(path).name
        stem = PurePosixPath(path).stem
        basename_lists.setdefault(basename.casefold(), []).append(path)
        stem_lists.setdefault(stem.casefold(), []).append(path)
    by_basename = {
        key: tuple(sorted(values, key=_sort_text))
        for key, values in basename_lists.items()
    }
    by_stem = {
        key: tuple(sorted(values, key=_sort_text))
        for key, values in stem_lists.items()
    }

    selected_headers = list(file_headers or ())
    if len(selected_headers) > MAX_W3D_HEADER_FILES:
        raise ValueError(
            f"W3D header file count exceeds {MAX_W3D_HEADER_FILES}"
        )
    headers_by_path: dict[str, W3DFileHeaders] = {}
    header_lists: dict[str, dict[str, list[tuple[str, str]]]] = {
        kind: {} for kind in W3D_HEADER_KINDS
    }
    total_ids = 0
    for raw_headers in selected_headers:
        if not isinstance(raw_headers, W3DFileHeaders):
            raise TypeError("W3D file headers must be W3DFileHeaders records")
        requested_path = _safe_w3d_path(
            raw_headers.virtual_path, "W3D header virtual path"
        )
        path = by_path.get(requested_path.casefold())
        if path is None:
            raise ValueError(
                f"W3D header metadata has no physical virtual path: {requested_path!r}"
            )
        path_key = path.casefold()
        if path_key in headers_by_path:
            raise ValueError(f"duplicate W3D header metadata for {path!r}")

        normalized_ids: dict[str, tuple[str, ...]] = {}
        for kind, raw_ids in (
            ("model", raw_headers.model_ids),
            ("animation", raw_headers.animation_ids),
            ("hierarchy", raw_headers.hierarchy_ids),
        ):
            ids = _materialize_ids(raw_ids, kind=kind, virtual_path=path)
            total_ids += len(ids)
            if total_ids > MAX_W3D_TOTAL_HEADER_IDS:
                raise ValueError(
                    f"W3D total header ID count exceeds {MAX_W3D_TOTAL_HEADER_IDS}"
                )
            normalized_ids[kind] = ids
            for identifier in ids:
                header_lists[kind].setdefault(identifier.casefold(), []).append(
                    (path, identifier)
                )

        normalized = W3DFileHeaders(
            virtual_path=path,
            model_ids=normalized_ids["model"],
            animation_ids=normalized_ids["animation"],
            hierarchy_ids=normalized_ids["hierarchy"],
        )
        headers_by_path[path_key] = normalized

    headers = tuple(
        sorted(
            headers_by_path.values(),
            key=lambda item: _sort_text(item.virtual_path),
        )
    )
    by_header: dict[str, dict[str, tuple[tuple[str, str], ...]]] = {}
    duplicates: list[W3DHeaderDuplicate] = []
    for kind in W3D_HEADER_KINDS:
        by_header[kind] = {}
        for key, raw_candidates in header_lists[kind].items():
            candidates = tuple(
                sorted(
                    raw_candidates,
                    key=lambda item: (_sort_text(item[0]), _sort_text(item[1])),
                )
            )
            by_header[kind][key] = candidates
            unique_paths = tuple(
                sorted({path for path, _ in candidates}, key=_sort_text)
            )
            if len(unique_paths) > 1:
                duplicates.append(
                    W3DHeaderDuplicate(
                        kind=kind,
                        identifier=min(
                            (identifier for _, identifier in candidates),
                            key=_sort_text,
                        ),
                        physical_virtual_paths=unique_paths,
                    )
                )
    duplicates.sort(
        key=lambda item: (
            _KIND_ORDER[item.kind],
            item.identifier.casefold(),
            item.identifier,
        )
    )
    if reject_duplicate_header_ids and duplicates:
        first = duplicates[0]
        raise ValueError(
            f"duplicate {first.kind} header ID across W3D files: "
            f"{first.identifier!r}"
        )

    return W3DIndex(
        virtual_paths=tuple(paths),
        file_headers=headers,
        duplicate_header_ids=tuple(duplicates),
        by_path=by_path,
        by_basename=by_basename,
        by_stem=by_stem,
        by_header=by_header,
    )


def _validated_request(request: W3DReferenceRequest) -> W3DReferenceRequest:
    if not isinstance(request, W3DReferenceRequest):
        raise TypeError("W3D requests must be W3DReferenceRequest records")
    if request.kind not in W3D_REFERENCE_KINDS:
        raise ValueError(f"unsupported W3D reference kind: {request.kind!r}")
    if not isinstance(request.semantic, bool):
        raise TypeError("W3D request semantic flag must be boolean")

    identifier = request.identifier
    if not isinstance(identifier, str):
        raise TypeError("W3D request identifier must be a string")
    if "/" in identifier or "\\" in identifier:
        identifier = _safe_w3d_path(identifier, "W3D request physical path")
    else:
        identifier = _safe_identifier(identifier, "W3D request identifier")
        if request.kind == "file" and not identifier.casefold().endswith(".w3d"):
            raise ValueError("W3D file reference must end in .w3d")
    return W3DReferenceRequest(request.kind, identifier, request.semantic)


def _candidate(
    *,
    path: str,
    logical_id: str,
    evidence: Iterable[W3DMatchEvidence],
    container_id: str | None = None,
    subobject_id: str | None = None,
) -> W3DResolutionCandidate:
    ordered_evidence = tuple(
        sorted(
            set(evidence),
            key=lambda item: (
                item.rule,
                item.matched_value.casefold(),
                item.matched_value,
                item.header_kind or "",
            ),
        )
    )
    header_ids = [
        item.matched_value for item in ordered_evidence if item.rule == "header-id"
    ]
    return W3DResolutionCandidate(
        physical_virtual_path=path,
        logical_id=logical_id,
        container_id=container_id,
        subobject_id=subobject_id,
        header_id=header_ids[0] if header_ids else None,
        evidence=ordered_evidence,
    )


def _resolve_one(
    index: W3DIndex, request: W3DReferenceRequest
) -> W3DResolvedReference | W3DUnresolvedReference:
    if request.semantic:
        evidence = (
            W3DMatchEvidence("caller-semantic", request.identifier, request.kind),
        )
        return W3DResolvedReference(
            kind=request.kind,
            requested_id=request.identifier,
            physical_virtual_path=None,
            logical_id=request.identifier,
            container_id=None,
            subobject_id=None,
            header_id=None,
            evidence=evidence,
            semantic=True,
        )

    evidence_by_path: dict[str, list[W3DMatchEvidence]] = {}
    container_by_path: dict[str, tuple[str, str]] = {}

    def add(path: str, evidence: W3DMatchEvidence) -> None:
        evidence_by_path.setdefault(path, []).append(evidence)

    identifier = request.identifier
    has_directory = "/" in identifier or "\\" in identifier
    if has_directory:
        normalized = _safe_w3d_path(identifier, "W3D request physical path")
        path = index._by_path.get(normalized.casefold())
        if path is not None:
            add(path, W3DMatchEvidence("exact-virtual-path", path))
    else:
        lower_identifier = identifier.casefold()
        physical_basename = lower_identifier.endswith(".w3d")
        if physical_basename:
            for path in index._by_basename.get(lower_identifier, ()):
                add(
                    path,
                    W3DMatchEvidence(
                        "exact-basename", PurePosixPath(path).name
                    ),
                )
        elif request.kind != "file":
            for path, header_id in index._by_header[request.kind].get(
                lower_identifier, ()
            ):
                add(
                    path,
                    W3DMatchEvidence("header-id", header_id, request.kind),
                )

            if request.kind == "model" and "." in identifier:
                raw_container, subobject = identifier.split(".", 1)
                container = _safe_identifier(
                    raw_container, "W3D container identifier"
                )
                subobject = _safe_identifier(
                    subobject, "W3D subobject identifier"
                )
                for path in index._by_stem.get(container.casefold(), ()):
                    canonical_container = PurePosixPath(path).stem
                    add(
                        path,
                        W3DMatchEvidence(
                            "container-subobject",
                            f"{canonical_container}.{subobject}",
                            "model",
                        ),
                    )
                    container_by_path[path] = (canonical_container, subobject)
            else:
                for path in index._by_stem.get(lower_identifier, ()):
                    add(
                        path,
                        W3DMatchEvidence(
                            "exact-stem", PurePosixPath(path).stem, request.kind
                        ),
                    )

    candidates: list[W3DResolutionCandidate] = []
    for path in sorted(evidence_by_path, key=_sort_text):
        container_id: str | None = None
        subobject_id: str | None = None
        if path in container_by_path:
            container_id, subobject_id = container_by_path[path]
        candidates.append(
            _candidate(
                path=path,
                logical_id=request.identifier,
                evidence=evidence_by_path[path],
                container_id=container_id,
                subobject_id=subobject_id,
            )
        )

    if not candidates:
        return W3DUnresolvedReference(
            request.kind, request.identifier, "missing"
        )
    if len(candidates) > 1:
        return W3DUnresolvedReference(
            request.kind,
            request.identifier,
            "ambiguous",
            tuple(candidates),
        )
    candidate = candidates[0]
    return W3DResolvedReference(
        kind=request.kind,
        requested_id=request.identifier,
        physical_virtual_path=candidate.physical_virtual_path,
        logical_id=candidate.logical_id,
        container_id=candidate.container_id,
        subobject_id=candidate.subobject_id,
        header_id=candidate.header_id,
        evidence=candidate.evidence,
        semantic=False,
    )


def resolve_w3d_references_partial(
    index: W3DIndex, requests: Iterable[W3DReferenceRequest]
) -> W3DBatchResolution:
    """Resolve a bounded batch while retaining missing/ambiguous diagnostics."""

    if not isinstance(index, W3DIndex):
        raise TypeError("index must be a W3DIndex")
    selected = list(requests)
    if not 1 <= len(selected) <= MAX_W3D_REQUESTS:
        raise ValueError(f"W3D request count must be 1..{MAX_W3D_REQUESTS}")

    normalized: list[W3DReferenceRequest] = []
    seen: set[tuple[str, str]] = set()
    for raw_request in selected:
        request = _validated_request(raw_request)
        key = (request.kind, request.identifier.casefold())
        if key in seen:
            raise ValueError(
                f"duplicate W3D {request.kind} request: {request.identifier!r}"
            )
        seen.add(key)
        normalized.append(request)
    normalized.sort(key=_sort_request)

    resolved: list[W3DResolvedReference] = []
    missing: list[W3DUnresolvedReference] = []
    ambiguous: list[W3DUnresolvedReference] = []
    for request in normalized:
        result = _resolve_one(index, request)
        if isinstance(result, W3DResolvedReference):
            resolved.append(result)
        elif result.reason == "missing":
            missing.append(result)
        else:
            ambiguous.append(result)
    return W3DBatchResolution(
        tuple(resolved), tuple(missing), tuple(ambiguous)
    )


def resolve_w3d_references(
    index: W3DIndex, requests: Iterable[W3DReferenceRequest]
) -> tuple[W3DResolvedReference, ...]:
    """Resolve a batch exactly, failing closed on its first canonical gap."""

    result = resolve_w3d_references_partial(index, requests)
    if result.missing:
        raise W3DResolutionError(result.missing[0])
    if result.ambiguous:
        raise W3DResolutionError(result.ambiguous[0])
    return result.resolved


def resolve_w3d_reference(
    index: W3DIndex,
    kind: str,
    identifier: str,
    *,
    semantic: bool = False,
) -> W3DResolvedReference:
    """Resolve one typed reference exactly and return its complete evidence."""

    return resolve_w3d_references(
        index, [W3DReferenceRequest(kind, identifier, semantic)]
    )[0]
