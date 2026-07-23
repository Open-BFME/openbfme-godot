"""Exact, bounded resolution of logical SAGE visual references.

The W3D/INI parsers discover logical visual identifiers, while the retail
catalog supplies canonical virtual paths.  This module is the deliberately
small bridge between those two facts: it performs no filesystem or catalog
I/O, no fuzzy lookup, and no representation fallback.

Successful resolutions retain the catalog's canonical spelling.  A caller can
use :func:`diagnose_visual_leaves` to collect deterministic missing/ambiguous
diagnostics, then use the strict APIs as the conversion boundary.
"""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import Iterable, Literal

from .paths import safe_relative_parts


MAX_VISUAL_CATALOG_PATHS = 250_000
MAX_VISUAL_REQUESTS = 32_768
MAX_VISUAL_PATH_LENGTH = 1_024
MAX_VISUAL_IDENTIFIER_LENGTH = 512
MAX_VISUAL_DIAGNOSTIC_CANDIDATES = 64

VISUAL_KIND_TEXTURE = "texture"
VISUAL_KIND_HOUSE_COLOR = "house-color"
VISUAL_KIND_PARTICLE = "particle"
VISUAL_KIND_SHADOW = "shadow"
VISUAL_KIND_ATTACHED_MODEL = "attached-model"

PARTICLE_REPRESENTATION_TEXTURE = "texture"
PARTICLE_REPRESENTATION_W3D = "w3d"

_SUPPORTED_EXTENSIONS = frozenset({".dds", ".tga", ".jpg", ".png", ".w3d"})
_TEXTURE_EXTENSIONS = frozenset({".dds", ".tga", ".jpg", ".png"})
_HOUSE_COLOR_EXTENSIONS = frozenset({".tga", ".jpg", ".png"})
_MODEL_EXTENSIONS = frozenset({".w3d"})
_KINDS = frozenset(
    {
        VISUAL_KIND_TEXTURE,
        VISUAL_KIND_HOUSE_COLOR,
        VISUAL_KIND_PARTICLE,
        VISUAL_KIND_SHADOW,
        VISUAL_KIND_ATTACHED_MODEL,
    }
)
_UNSAFE_WILDCARD_CHARACTERS = frozenset("*?[]")

ResolutionStatus = Literal["missing", "ambiguous", "invalid"]


@dataclass(frozen=True, slots=True)
class VisualLeafRequest:
    """One source-proven logical reference and its semantic interpretation."""

    identifier: str
    kind: str
    representation: str | None = None

    def neutral(self) -> dict[str, object]:
        value: dict[str, object] = {
            "identifier": self.identifier,
            "kind": self.kind,
        }
        if self.representation is not None:
            value["representation"] = self.representation
        return value


@dataclass(frozen=True, slots=True)
class VisualLeaf:
    """One physical source leaf with its conversion role and exact evidence."""

    virtual_path: str
    role: str
    evidence: str

    def neutral(self) -> dict[str, str]:
        return {
            "virtualPath": self.virtual_path,
            "role": self.role,
            "evidence": self.evidence,
        }


@dataclass(frozen=True, slots=True)
class VisualLeafResolution:
    request: VisualLeafRequest
    representation: str
    leaves: tuple[VisualLeaf, ...]

    def neutral(self) -> dict[str, object]:
        return {
            "request": self.request.neutral(),
            "representation": self.representation,
            "leaves": [leaf.neutral() for leaf in self.leaves],
        }


@dataclass(frozen=True, slots=True)
class VisualLeafDiagnostic:
    request_index: int
    request: VisualLeafRequest
    status: ResolutionStatus
    message: str
    candidate_count: int
    candidates: tuple[str, ...]

    def neutral(self) -> dict[str, object]:
        return {
            "requestIndex": self.request_index,
            "request": self.request.neutral(),
            "status": self.status,
            "message": self.message,
            "candidateCount": self.candidate_count,
            "candidates": list(self.candidates),
        }


@dataclass(frozen=True, slots=True)
class VisualLeafBatch:
    """Request-order results plus deterministic diagnostics for failed entries."""

    resolutions: tuple[VisualLeafResolution | None, ...]
    diagnostics: tuple[VisualLeafDiagnostic, ...]

    def neutral(self) -> dict[str, object]:
        return {
            "resolutions": [
                resolution.neutral() if resolution is not None else None
                for resolution in self.resolutions
            ],
            "diagnostics": [item.neutral() for item in self.diagnostics],
        }


class VisualLeafResolutionError(ValueError):
    """A classified exact-resolution failure."""

    def __init__(
        self,
        status: ResolutionStatus,
        message: str,
        candidates: Iterable[str] = (),
    ) -> None:
        ordered = tuple(sorted(set(candidates), key=lambda item: (item.casefold(), item)))
        self.status = status
        self.candidate_count = len(ordered)
        self.candidates = ordered[:MAX_VISUAL_DIAGNOSTIC_CANDIDATES]
        super().__init__(message)


class VisualLeafBatchError(ValueError):
    """Raised by the strict batch API when any request does not resolve."""

    def __init__(self, diagnostics: tuple[VisualLeafDiagnostic, ...]) -> None:
        self.diagnostics = diagnostics
        summary = ", ".join(
            f"#{item.request_index} {item.status}: {item.request.identifier!r}"
            for item in diagnostics
        )
        super().__init__(f"visual leaf batch did not resolve exactly: {summary}")


@dataclass(frozen=True, slots=True)
class _CatalogLeaf:
    virtual_path: str
    suffix: str
    basename_key: str
    basename_stem_key: str
    path_stem_key: str


def _bounded_list(values: Iterable[object], maximum: int, label: str) -> list[object]:
    result: list[object] = []
    for value in values:
        result.append(value)
        if len(result) > maximum:
            raise ValueError(f"{label} count exceeds limit of {maximum}")
    return result


def _safe_normalized_path(value: object, *, label: str, maximum: int) -> str:
    if not isinstance(value, str):
        raise ValueError(f"{label} must be a string")
    if len(value) > maximum:
        raise ValueError(f"{label} exceeds {maximum} character limit")
    if any(character in value for character in _UNSAFE_WILDCARD_CHARACTERS):
        raise ValueError(f"unsafe wildcard in {label}: {value!r}")
    try:
        parts = safe_relative_parts(value)
    except ValueError as exc:
        raise ValueError(f"unsafe {label}: {value!r}: {exc}") from exc
    normalized = "/".join(parts)
    if len(normalized) > maximum:
        raise ValueError(f"{label} exceeds {maximum} character limit")
    return normalized


def _catalog(catalog_virtual_paths: Iterable[str]) -> tuple[_CatalogLeaf, ...]:
    raw_paths = _bounded_list(
        catalog_virtual_paths, MAX_VISUAL_CATALOG_PATHS, "visual catalog path"
    )
    by_key: dict[str, str] = {}
    leaves: list[_CatalogLeaf] = []
    for raw_path in raw_paths:
        path = _safe_normalized_path(
            raw_path, label="visual catalog path", maximum=MAX_VISUAL_PATH_LENGTH
        )
        key = path.casefold()
        previous = by_key.get(key)
        if previous is not None:
            if previous == path:
                raise ValueError(f"duplicate visual catalog path: {path!r}")
            raise ValueError(
                "case-ambiguous visual catalog path: "
                f"{previous!r} and {path!r}"
            )
        by_key[key] = path
        pure = PurePosixPath(path)
        suffix = pure.suffix.casefold()
        leaves.append(
            _CatalogLeaf(
                virtual_path=path,
                suffix=suffix,
                basename_key=pure.name.casefold(),
                basename_stem_key=pure.stem.casefold(),
                path_stem_key=pure.with_suffix("").as_posix().casefold(),
            )
        )
    leaves.sort(key=lambda item: (item.virtual_path.casefold(), item.virtual_path))
    return tuple(leaves)


def _validated_request(request: VisualLeafRequest) -> tuple[VisualLeafRequest, str]:
    if not isinstance(request, VisualLeafRequest):
        raise VisualLeafResolutionError(
            "invalid", "visual leaf request must be a VisualLeafRequest"
        )
    try:
        identifier = _safe_normalized_path(
            request.identifier,
            label="visual identifier",
            maximum=MAX_VISUAL_IDENTIFIER_LENGTH,
        )
    except ValueError as exc:
        raise VisualLeafResolutionError("invalid", str(exc)) from exc
    if not isinstance(request.kind, str) or request.kind not in _KINDS:
        raise VisualLeafResolutionError(
            "invalid", f"unsupported visual kind: {request.kind!r}"
        )
    if request.kind == VISUAL_KIND_PARTICLE:
        if not (
            request.representation is None
            or isinstance(request.representation, str)
            and request.representation in {
                PARTICLE_REPRESENTATION_TEXTURE,
                PARTICLE_REPRESENTATION_W3D,
            }
        ):
            raise VisualLeafResolutionError(
                "invalid",
                f"unsupported particle representation: {request.representation!r}",
            )
    elif request.representation is not None:
        raise VisualLeafResolutionError(
            "invalid",
            f"representation is only valid for particle requests: {request.kind!r}",
        )
    suffix = PurePosixPath(identifier).suffix.casefold()
    if suffix and suffix not in _SUPPORTED_EXTENSIONS:
        raise VisualLeafResolutionError(
            "invalid", f"unsupported explicit visual extension: {suffix!r}"
        )
    return VisualLeafRequest(identifier, request.kind, request.representation), suffix


def _allowed_extensions(request: VisualLeafRequest) -> frozenset[str]:
    if request.kind in {VISUAL_KIND_TEXTURE, VISUAL_KIND_SHADOW}:
        return _TEXTURE_EXTENSIONS
    if request.kind == VISUAL_KIND_HOUSE_COLOR:
        return _HOUSE_COLOR_EXTENSIONS
    if request.kind == VISUAL_KIND_ATTACHED_MODEL:
        return _MODEL_EXTENSIONS
    if request.representation == PARTICLE_REPRESENTATION_TEXTURE:
        return _TEXTURE_EXTENSIONS
    if request.representation == PARTICLE_REPRESENTATION_W3D:
        return _MODEL_EXTENSIONS
    return _TEXTURE_EXTENSIONS | _MODEL_EXTENSIONS


def _matching_candidates(
    catalog: tuple[_CatalogLeaf, ...],
    identifier: str,
    explicit_suffix: str,
    allowed_extensions: frozenset[str],
) -> tuple[_CatalogLeaf, ...]:
    path_scoped = "/" in identifier
    identifier_key = identifier.casefold()
    result: list[_CatalogLeaf] = []
    for leaf in catalog:
        if leaf.suffix not in allowed_extensions:
            continue
        if explicit_suffix:
            candidate_key = (
                leaf.virtual_path.casefold() if path_scoped else leaf.basename_key
            )
        else:
            candidate_key = leaf.path_stem_key if path_scoped else leaf.basename_stem_key
        if candidate_key == identifier_key:
            result.append(leaf)
    return tuple(result)


def _evidence(identifier: str, explicit_suffix: str) -> str:
    scope = "path" if "/" in identifier else "basename"
    form = "explicit-extension" if explicit_suffix else "extensionless-stem"
    return f"case-insensitive-exact-{scope}-{form}"


def _failure(
    status: ResolutionStatus,
    request: VisualLeafRequest,
    detail: str,
    candidates: Iterable[_CatalogLeaf] = (),
) -> VisualLeafResolutionError:
    return VisualLeafResolutionError(
        status,
        f"{status} {request.kind} visual leaf for {request.identifier!r}: {detail}",
        (candidate.virtual_path for candidate in candidates),
    )


def _canonical_bucket_candidates(
    candidates: tuple[_CatalogLeaf, ...],
) -> tuple[_CatalogLeaf, ...]:
    """Prefer the engine's canonical compiled-texture bucket among duplicates.

    Retail archives shelve compiled textures at
    ``art/compiledtextures/<first-two-letters-of-stem>/<name>``; RotWK 2.01
    additionally ships stray off-bucket copies (``kb/dummy.dds`` beside the
    canonical ``du/dummy.dds``). When duplicates exist and exactly one sits in
    its canonical bucket, that copy is the authored lookup target; any other
    multiplicity stays ambiguous.
    """

    matched = tuple(
        leaf
        for leaf in candidates
        if leaf.virtual_path.casefold()
        == f"art/compiledtextures/{leaf.basename_stem_key[:2]}/{leaf.basename_key}"
    )
    return matched if len(matched) == 1 else candidates


def _single_resolution(
    request: VisualLeafRequest,
    candidates: tuple[_CatalogLeaf, ...],
    evidence: str,
) -> VisualLeafResolution:
    if not candidates:
        raise _failure("missing", request, "no exact catalog candidate")
    if len(candidates) != 1 and request.kind != VISUAL_KIND_ATTACHED_MODEL:
        candidates = _canonical_bucket_candidates(candidates)
    if len(candidates) != 1:
        raise _failure(
            "ambiguous", request, "multiple exact catalog candidates", candidates
        )
    leaf = candidates[0]
    if request.kind == VISUAL_KIND_TEXTURE:
        role, representation = "texture", "texture"
    elif request.kind == VISUAL_KIND_SHADOW:
        role, representation = "shadow-texture", "texture"
    elif request.kind == VISUAL_KIND_ATTACHED_MODEL:
        role, representation = "attached-model", "w3d"
    elif request.kind == VISUAL_KIND_PARTICLE and leaf.suffix == ".w3d":
        role, representation = "particle-model", "w3d"
    elif request.kind == VISUAL_KIND_PARTICLE:
        role, representation = "particle-texture", "texture"
    else:
        role, representation = "house-color-texture", "tga"
    return VisualLeafResolution(
        request,
        representation,
        (VisualLeaf(leaf.virtual_path, role, evidence),),
    )


def _house_color_resolution(
    catalog: tuple[_CatalogLeaf, ...],
    request: VisualLeafRequest,
    explicit_suffix: str,
    candidates: tuple[_CatalogLeaf, ...],
    evidence: str,
) -> VisualLeafResolution:
    if explicit_suffix == ".tga":
        return _single_resolution(request, candidates, evidence)

    if explicit_suffix in {".jpg", ".png"}:
        if not candidates:
            raise _failure("missing", request, "no exact catalog candidate")
        if len(candidates) != 1:
            raise _failure(
                "ambiguous", request, "multiple exact catalog candidates", candidates
            )
        anchor = candidates[0]
        counterpart_extension = ".png" if explicit_suffix == ".jpg" else ".jpg"
        counterpart_key = (
            PurePosixPath(anchor.virtual_path)
            .with_suffix(counterpart_extension)
            .as_posix()
            .casefold()
        )
        counterpart = tuple(
            leaf
            for leaf in catalog
            if leaf.virtual_path.casefold() == counterpart_key
        )
        if not counterpart:
            raise _failure(
                "missing",
                request,
                f"incomplete JPG+PNG pair; missing {counterpart_extension}",
                candidates,
            )
        pair = {anchor.suffix: anchor, counterpart_extension: counterpart[0]}
        pair_evidence = f"{evidence}-complete-jpg-png-pair"
        return VisualLeafResolution(
            request,
            "jpg-png",
            (
                VisualLeaf(
                    pair[".jpg"].virtual_path,
                    "house-color-color",
                    pair_evidence,
                ),
                VisualLeaf(
                    pair[".png"].virtual_path,
                    "house-color-alpha",
                    pair_evidence,
                ),
            ),
        )

    if not candidates:
        raise _failure("missing", request, "no exact catalog candidate")
    groups: dict[str, list[_CatalogLeaf]] = {}
    for candidate in candidates:
        groups.setdefault(candidate.path_stem_key, []).append(candidate)

    if len(groups) != 1:
        raise _failure(
            "ambiguous",
            request,
            "multiple house-color path alternatives",
            candidates,
        )
    group = next(iter(groups.values()))
    by_extension = {leaf.suffix: leaf for leaf in group}
    extensions = frozenset(by_extension)
    if extensions == {".tga"}:
        return _single_resolution(request, tuple(group), evidence)
    if extensions == {".jpg", ".png"}:
        pair_evidence = f"{evidence}-complete-jpg-png-pair"
        return VisualLeafResolution(
            request,
            "jpg-png",
            (
                VisualLeaf(
                    by_extension[".jpg"].virtual_path,
                    "house-color-color",
                    pair_evidence,
                ),
                VisualLeaf(
                    by_extension[".png"].virtual_path,
                    "house-color-alpha",
                    pair_evidence,
                ),
            ),
        )
    if extensions in ({".jpg"}, {".png"}):
        missing = ".png" if extensions == {".jpg"} else ".jpg"
        raise _failure(
            "missing",
            request,
            f"incomplete JPG+PNG pair; missing {missing}",
            candidates,
        )
    raise _failure(
        "ambiguous",
        request,
        "TGA and JPG/PNG house-color alternatives coexist",
        candidates,
    )


def _resolve_from_catalog(
    catalog: tuple[_CatalogLeaf, ...], request: VisualLeafRequest
) -> VisualLeafResolution:
    canonical_request, explicit_suffix = _validated_request(request)
    allowed = _allowed_extensions(canonical_request)
    if explicit_suffix and explicit_suffix not in allowed:
        raise _failure(
            "invalid",
            canonical_request,
            f"extension {explicit_suffix!r} is not allowed for this semantic kind",
        )
    candidates = _matching_candidates(
        catalog,
        canonical_request.identifier,
        explicit_suffix,
        allowed,
    )
    evidence = _evidence(canonical_request.identifier, explicit_suffix)
    if canonical_request.kind == VISUAL_KIND_HOUSE_COLOR:
        return _house_color_resolution(
            catalog,
            canonical_request,
            explicit_suffix,
            candidates,
            evidence,
        )
    return _single_resolution(canonical_request, candidates, evidence)


def resolve_visual_leaf(
    catalog_virtual_paths: Iterable[str],
    identifier: str,
    kind: str,
    *,
    representation: str | None = None,
) -> VisualLeafResolution:
    """Resolve one logical identifier exactly or raise a classified error."""

    catalog = _catalog(catalog_virtual_paths)
    return _resolve_from_catalog(
        catalog, VisualLeafRequest(identifier, kind, representation)
    )


def diagnose_visual_leaves(
    catalog_virtual_paths: Iterable[str],
    requests: Iterable[VisualLeafRequest],
) -> VisualLeafBatch:
    """Resolve a bounded batch while retaining all per-request diagnostics."""

    catalog = _catalog(catalog_virtual_paths)
    bounded = _bounded_list(requests, MAX_VISUAL_REQUESTS, "visual request")
    resolutions: list[VisualLeafResolution | None] = []
    diagnostics: list[VisualLeafDiagnostic] = []
    for index, raw_request in enumerate(bounded):
        request = (
            raw_request
            if isinstance(raw_request, VisualLeafRequest)
            else VisualLeafRequest("invalid-request", "invalid")
        )
        try:
            resolution = _resolve_from_catalog(catalog, raw_request)  # type: ignore[arg-type]
        except VisualLeafResolutionError as exc:
            resolutions.append(None)
            diagnostics.append(
                VisualLeafDiagnostic(
                    index,
                    request,
                    exc.status,
                    str(exc),
                    exc.candidate_count,
                    exc.candidates,
                )
            )
        except ValueError as exc:
            resolutions.append(None)
            diagnostics.append(
                VisualLeafDiagnostic(index, request, "invalid", str(exc), 0, ())
            )
        else:
            resolutions.append(resolution)
    return VisualLeafBatch(tuple(resolutions), tuple(diagnostics))


def resolve_visual_leaves(
    catalog_virtual_paths: Iterable[str],
    requests: Iterable[VisualLeafRequest],
) -> tuple[VisualLeafResolution, ...]:
    """Strict batch boundary: every request must resolve exactly."""

    batch = diagnose_visual_leaves(catalog_virtual_paths, requests)
    if batch.diagnostics:
        raise VisualLeafBatchError(batch.diagnostics)
    return tuple(
        resolution
        for resolution in batch.resolutions
        if resolution is not None
    )
