"""Verified effective-assets bridge for deterministic W3D catalog scans.

``scan_w3d_catalog`` deliberately accepts caller-owned byte records and never
opens files.  This module is the narrow filesystem boundary in front of that
API: it validates an effective-assets manifest/tree, reads only manifest-
declared W3D files, verifies their exact size and SHA-256, and then delegates
the bytes to the existing catalog scanner.

The returned reports contain paths, counts, hashes, and parsed W3D metadata;
they never contain source payload bytes.  ``compact=True`` retains stable
aggregate evidence while omitting the per-file catalog expansion, making the
JSON-ready value practical for large retail corpora.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
from typing import Any, Iterable, Mapping

from .paths import safe_relative_parts
from .w3d_catalog import (
    MAX_W3D_CATALOG_FILES,
    MAX_W3D_CATALOG_TOTAL_BYTES,
    W3DCatalogReport,
    scan_w3d_catalog,
)


EFFECTIVE_ASSET_MANIFEST_SCHEMA = "openbfme.effective-assets-manifest"
EFFECTIVE_ASSET_MANIFEST_VERSION = 0
EFFECTIVE_ASSET_MANIFEST_RELATIVE = ".openbfme/manifest.json"
W3D_CORPUS_SCHEMA = "openbfme.w3d-corpus"
W3D_CORPUS_SCHEMA_VERSION = 0

MAX_EFFECTIVE_ASSET_MANIFEST_BYTES = 64 * 1024 * 1024
MAX_EFFECTIVE_ASSET_FILES = 50_000
MAX_EFFECTIVE_ASSET_BYTES = 8 * 1024 * 1024 * 1024


class W3DCorpusError(ValueError):
    """Raised when an effective-assets corpus cannot be trusted."""


class W3DCorpusLimitError(W3DCorpusError):
    """Raised before payload reads when selected corpus bounds are exceeded."""


@dataclass(frozen=True, slots=True)
class W3DCorpusFile:
    """One manifest-declared W3D source, represented without payload bytes."""

    virtual_path: str
    byte_length: int
    source_sha256: str

    def neutral(self) -> dict[str, object]:
        return {
            "virtualPath": self.virtual_path,
            "byteLength": self.byte_length,
            "sourceSha256": self.source_sha256,
        }


@dataclass(frozen=True, slots=True)
class W3DCorpusReport:
    """Verified corpus provenance plus the delegated W3D catalog report.

    ``asset_root`` and ``manifest_path`` are available to the local caller but
    intentionally omitted from :meth:`neutral`, so identical verified trees
    produce identical JSON-ready evidence regardless of their host location.
    """

    asset_root: Path
    manifest_path: Path
    manifest_sha256: str
    manifest_aggregate_sha256: str
    manifest_file_count: int
    manifest_total_bytes: int
    w3d_files: tuple[W3DCorpusFile, ...]
    w3d_inventory_sha256: str
    catalog: W3DCatalogReport
    compact: bool
    corpus_sha256: str

    @property
    def complete(self) -> bool:
        return self.catalog.complete

    @property
    def w3d_total_bytes(self) -> int:
        return sum(item.byte_length for item in self.w3d_files)

    def neutral(self) -> dict[str, object]:
        """Return deterministic JSON-ready evidence with no source bytes."""

        catalog = self.catalog
        result: dict[str, object] = {
            "schema": W3D_CORPUS_SCHEMA,
            "schemaVersion": W3D_CORPUS_SCHEMA_VERSION,
            "mode": "compact" if self.compact else "full",
            "summary": {
                "manifestFileCount": self.manifest_file_count,
                "manifestTotalBytes": self.manifest_total_bytes,
                "w3dFileCount": len(self.w3d_files),
                "w3dTotalBytes": self.w3d_total_bytes,
                "scannedFileCount": len(catalog.files),
                "indexedFileCount": catalog.indexed_file_count,
                "chunkCount": sum(item.count for item in catalog.chunk_counts),
                "warningCount": len(catalog.warnings),
                "failureCount": len(catalog.failures),
                "duplicateLogicalIdCount": len(catalog.duplicate_logical_ids),
                "complete": self.complete,
            },
            "hashes": {
                "manifestSha256": self.manifest_sha256,
                "manifestAggregateSha256": self.manifest_aggregate_sha256,
                "w3dInventorySha256": self.w3d_inventory_sha256,
                "catalogInputSha256": catalog.input_sha256,
                "catalogMetadataSha256": catalog.metadata_sha256,
                "corpusSha256": self.corpus_sha256,
            },
        }
        if not self.compact:
            result["w3dFiles"] = [item.neutral() for item in self.w3d_files]
            result["catalog"] = catalog.neutral()
            return result

        warning_codes = Counter(item.diagnostic.code for item in catalog.warnings)
        failure_codes = Counter(item.code for item in catalog.failures)
        duplicate_kinds = Counter(item.kind for item in catalog.duplicate_logical_ids)
        result.update(
            {
                "chunkCounts": [item.neutral() for item in catalog.chunk_counts],
                "assetFamilyCounts": dict(catalog.asset_family_counts),
                "diagnostics": {
                    "warningCodes": dict(sorted(warning_codes.items())),
                    "failureCodes": dict(sorted(failure_codes.items())),
                    "duplicateLogicalIdKinds": dict(sorted(duplicate_kinds.items())),
                },
            }
        )
        return result

    # Explicit name for callers preparing json.dumps(...) output.
    json_ready = neutral


class W3DCorpusStrictError(W3DCorpusError):
    """Raised after producing a verified but non-clean strict corpus report."""

    def __init__(self, report: W3DCorpusReport):
        self.report = report
        catalog = report.catalog
        super().__init__(
            "strict W3D corpus rejected "
            f"{len(catalog.warnings)} warning(s), "
            f"{len(catalog.failures)} failure(s), and "
            f"{len(catalog.duplicate_logical_ids)} duplicate logical ID(s)"
        )


@dataclass(frozen=True, slots=True)
class _ManifestFile:
    path: str
    size: int
    sha256: str

    @property
    def is_w3d(self) -> bool:
        return PurePosixPath(self.path).suffix.casefold() == ".w3d"

    def corpus_source(self) -> W3DCorpusFile:
        return W3DCorpusFile(self.path, self.size, self.sha256)


@dataclass(frozen=True, slots=True)
class _ValidatedManifest:
    raw_sha256: str
    aggregate_sha256: str
    total_files: int
    total_bytes: int
    files: tuple[_ManifestFile, ...]


@dataclass(frozen=True, slots=True)
class _TreeFile:
    relative_path: str
    path: Path
    size: int


class _DuplicateManifestKey(ValueError):
    pass


def _canonical_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and value == value.casefold()
        and all(character in "0123456789abcdef" for character in value)
    )


def _is_int(value: object, *, minimum: int = 0) -> bool:
    return not isinstance(value, bool) and isinstance(value, int) and value >= minimum


def _is_link_like(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(is_junction and is_junction())


def _absolute_unresolved(path: Path) -> Path:
    expanded = path.expanduser()
    if expanded.is_absolute():
        return expanded
    return Path.cwd() / expanded


def _refuse_link_chain(path: Path, *, context: str) -> None:
    """Reject links/junctions in an existing path without resolving through them."""

    absolute = _absolute_unresolved(path)
    anchor = Path(absolute.anchor)
    current = anchor
    for part in absolute.parts[1:]:
        current = current / part
        if os.path.lexists(current) and _is_link_like(current):
            raise W3DCorpusError(f"{context} is linked: {current}")


def _resolve_root(value: Path | str) -> Path:
    try:
        candidate = Path(value)
    except TypeError as exc:
        raise TypeError("effective-assets root must be a filesystem path") from exc
    _refuse_link_chain(candidate, context="effective-assets root")
    try:
        root = candidate.expanduser().resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise W3DCorpusError(f"effective-assets root is unavailable: {candidate}") from exc
    if not root.is_dir() or _is_link_like(root):
        raise W3DCorpusError("effective-assets root is not an unlinked directory")
    return root


def _object_without_duplicate_keys(
    pairs: Iterable[tuple[str, object]],
) -> dict[str, object]:
    value: dict[str, object] = {}
    for key, item in pairs:
        if key in value:
            raise _DuplicateManifestKey(f"duplicate manifest key: {key!r}")
        value[key] = item
    return value


def _reject_json_constant(value: str) -> object:
    raise ValueError(f"non-finite JSON value is not allowed: {value}")


def _manifest_aggregate(files: Iterable[_ManifestFile]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(item.path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item.size).encode("ascii"))
        digest.update(b"\0")
        digest.update(item.sha256.encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _validate_identity_sections(manifest: Mapping[str, object]) -> None:
    catalog = manifest.get("catalog")
    if not isinstance(catalog, dict) or set(catalog) != {
        "archive_count",
        "entry_count",
        "format",
        "identity_sha256",
    }:
        raise W3DCorpusError("effective-assets manifest catalog identity is invalid")
    if (
        not _is_int(catalog.get("archive_count"))
        or not _is_int(catalog.get("entry_count"))
        or not _is_int(catalog.get("format"), minimum=1)
        or not _is_sha256(catalog.get("identity_sha256"))
    ):
        raise W3DCorpusError("effective-assets manifest catalog identity is invalid")

    install = manifest.get("install")
    if not isinstance(install, dict) or set(install) != {
        "identity_sha256",
        "root",
    }:
        raise W3DCorpusError("effective-assets manifest install identity is invalid")
    if not _is_sha256(install.get("identity_sha256")) or not isinstance(
        install.get("root"), str
    ):
        raise W3DCorpusError("effective-assets manifest install identity is invalid")


def _validate_manifest_file(raw: object, index: int) -> _ManifestFile:
    expected_keys = {"archive", "offset", "path", "precedence", "sha256", "size"}
    if not isinstance(raw, dict) or set(raw) != expected_keys:
        raise W3DCorpusError(
            f"effective-assets manifest file entry {index} has an invalid shape"
        )
    archive = raw.get("archive")
    path_value = raw.get("path")
    size = raw.get("size")
    sha256 = raw.get("sha256")
    if not isinstance(archive, str) or not archive:
        raise W3DCorpusError(
            f"effective-assets manifest file entry {index} has an invalid archive"
        )
    if (
        not _is_int(raw.get("offset"))
        or not _is_int(raw.get("precedence"))
        or not _is_int(size)
        or not _is_sha256(sha256)
    ):
        raise W3DCorpusError(
            f"effective-assets manifest file entry {index} has invalid metadata"
        )
    if not isinstance(path_value, str):
        raise W3DCorpusError(
            f"effective-assets manifest file entry {index} has an invalid path"
        )
    try:
        parts = safe_relative_parts(path_value)
    except ValueError as exc:
        raise W3DCorpusError(
            f"effective-assets manifest file entry {index} has an unsafe path"
        ) from exc
    canonical_path = "/".join(parts)
    if canonical_path != path_value:
        raise W3DCorpusError(
            f"effective-assets manifest path is not canonical: {path_value!r}"
        )
    if parts[0].casefold() == ".openbfme":
        raise W3DCorpusError(
            f"effective-assets manifest path uses reserved metadata space: {path_value!r}"
        )
    return _ManifestFile(path_value, size, sha256)


def _validate_inventory_paths(files: tuple[_ManifestFile, ...]) -> None:
    seen: dict[str, str] = {}
    for item in files:
        key = item.path.casefold()
        previous = seen.get(key)
        if previous is not None:
            raise W3DCorpusError(
                "effective-assets manifest contains case-colliding paths: "
                f"{previous!r} and {item.path!r}"
            )
        seen[key] = item.path

    paths = [item.path for item in files]
    expected_order = sorted(paths, key=lambda value: value.casefold())
    if paths != expected_order:
        raise W3DCorpusError("effective-assets manifest inventory is not canonical")

    keys = {item.path.casefold() for item in files}
    for key in keys:
        parts = key.split("/")
        for index in range(1, len(parts)):
            if "/".join(parts[:index]) in keys:
                raise W3DCorpusError(
                    "effective-assets manifest has a file/directory path collision"
                )


def _load_manifest(root: Path) -> tuple[Path, _ValidatedManifest]:
    manifest_path = root.joinpath(*PurePosixPath(EFFECTIVE_ASSET_MANIFEST_RELATIVE).parts)
    _refuse_link_chain(manifest_path, context="effective-assets manifest")
    if not manifest_path.is_file() or _is_link_like(manifest_path):
        raise W3DCorpusError("effective-assets manifest is missing, linked, or not a file")
    try:
        size = manifest_path.stat().st_size
    except OSError as exc:
        raise W3DCorpusError("effective-assets manifest cannot be inspected") from exc
    if not 1 <= size <= MAX_EFFECTIVE_ASSET_MANIFEST_BYTES:
        raise W3DCorpusLimitError("effective-assets manifest exceeds its safety bound")
    try:
        raw_bytes = manifest_path.read_bytes()
    except OSError as exc:
        raise W3DCorpusError("effective-assets manifest cannot be read") from exc
    if len(raw_bytes) != size:
        raise W3DCorpusError("effective-assets manifest changed during read")
    try:
        manifest = json.loads(
            raw_bytes.decode("utf-8"),
            object_pairs_hook=_object_without_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise W3DCorpusError("effective-assets manifest is invalid JSON") from exc
    if not isinstance(manifest, dict):
        raise W3DCorpusError("effective-assets manifest root is not an object")
    if set(manifest) != {
        "aggregate_sha256",
        "catalog",
        "files",
        "install",
        "schema",
        "schema_version",
        "totals",
    }:
        raise W3DCorpusError("effective-assets manifest top-level shape is invalid")
    if manifest.get("schema") != EFFECTIVE_ASSET_MANIFEST_SCHEMA:
        raise W3DCorpusError("effective-assets manifest schema is unsupported")
    schema_version = manifest.get("schema_version")
    if (
        isinstance(schema_version, bool)
        or not isinstance(schema_version, int)
        or schema_version != EFFECTIVE_ASSET_MANIFEST_VERSION
    ):
        raise W3DCorpusError("effective-assets manifest schema version is unsupported")
    _validate_identity_sections(manifest)

    raw_files = manifest.get("files")
    if not isinstance(raw_files, list):
        raise W3DCorpusError("effective-assets manifest file inventory is invalid")
    if len(raw_files) > MAX_EFFECTIVE_ASSET_FILES:
        raise W3DCorpusLimitError("effective-assets manifest file count exceeds its bound")
    files = tuple(
        _validate_manifest_file(item, index) for index, item in enumerate(raw_files)
    )
    _validate_inventory_paths(files)

    totals = manifest.get("totals")
    if not isinstance(totals, dict) or set(totals) != {"bytes", "files"}:
        raise W3DCorpusError("effective-assets manifest totals are invalid")
    total_files = totals.get("files")
    total_bytes = totals.get("bytes")
    if not _is_int(total_files) or not _is_int(total_bytes):
        raise W3DCorpusError("effective-assets manifest totals are invalid")
    calculated_bytes = sum(item.size for item in files)
    if total_files != len(files) or total_bytes != calculated_bytes:
        raise W3DCorpusError("effective-assets manifest totals do not match its inventory")
    if total_bytes > MAX_EFFECTIVE_ASSET_BYTES:
        raise W3DCorpusLimitError("effective-assets manifest byte total exceeds its bound")

    aggregate = manifest.get("aggregate_sha256")
    if not _is_sha256(aggregate) or aggregate != _manifest_aggregate(files):
        raise W3DCorpusError("effective-assets manifest aggregate SHA-256 is invalid")
    return manifest_path, _ValidatedManifest(
        raw_sha256=hashlib.sha256(raw_bytes).hexdigest(),
        aggregate_sha256=aggregate,
        total_files=total_files,
        total_bytes=total_bytes,
        files=files,
    )


def _tree_collision(
    values: Mapping[str, _TreeFile | str], key: str, relative: str
) -> None:
    previous = values.get(key)
    if previous is None:
        return
    previous_path = (
        previous.relative_path if isinstance(previous, _TreeFile) else previous
    )
    raise W3DCorpusError(
        "effective-assets tree contains case-colliding paths: "
        f"{previous_path!r} and {relative!r}"
    )


def _scan_tree(root: Path) -> tuple[dict[str, _TreeFile], dict[str, str]]:
    files: dict[str, _TreeFile] = {}
    directories: dict[str, str] = {}
    pending = [root]
    while pending:
        directory = pending.pop()
        try:
            with os.scandir(directory) as iterator:
                entries = sorted(
                    iterator, key=lambda item: (item.name.casefold(), item.name)
                )
        except OSError as exc:
            raise W3DCorpusError("effective-assets tree cannot be enumerated") from exc
        for entry in entries:
            path = Path(entry.path)
            relative = path.relative_to(root).as_posix()
            if entry.is_symlink() or _is_link_like(path):
                raise W3DCorpusError(
                    f"effective-assets tree contains a link: {relative!r}"
                )
            key = relative.casefold()
            try:
                if entry.is_file(follow_symlinks=False):
                    _tree_collision(files, key, relative)
                    _tree_collision(directories, key, relative)
                    files[key] = _TreeFile(
                        relative_path=relative,
                        path=path,
                        size=entry.stat(follow_symlinks=False).st_size,
                    )
                elif entry.is_dir(follow_symlinks=False):
                    _tree_collision(directories, key, relative)
                    _tree_collision(files, key, relative)
                    directories[key] = relative
                    pending.append(path)
                else:
                    raise W3DCorpusError(
                        "effective-assets tree contains an unsupported filesystem entry: "
                        f"{relative!r}"
                    )
            except OSError as exc:
                raise W3DCorpusError(
                    f"effective-assets tree entry cannot be inspected: {relative!r}"
                ) from exc
    return files, directories


def _expected_directories(paths: Iterable[str]) -> dict[str, str]:
    expected: dict[str, str] = {}
    for relative in paths:
        parts = PurePosixPath(relative).parts
        for index in range(1, len(parts)):
            directory = "/".join(parts[:index])
            expected[directory.casefold()] = directory
    return expected


def _validate_tree(
    root: Path, manifest: _ValidatedManifest
) -> dict[str, _TreeFile]:
    actual_files, actual_directories = _scan_tree(root)
    expected_file_paths = [item.path for item in manifest.files]
    expected_file_paths.append(EFFECTIVE_ASSET_MANIFEST_RELATIVE)
    expected_files = {path.casefold(): path for path in expected_file_paths}
    expected_directories = _expected_directories(expected_file_paths)

    missing_files = sorted(set(expected_files) - set(actual_files))
    extra_files = sorted(set(actual_files) - set(expected_files))
    missing_directories = sorted(set(expected_directories) - set(actual_directories))
    extra_directories = sorted(set(actual_directories) - set(expected_directories))
    if missing_files:
        raise W3DCorpusError(
            "effective-assets tree is missing declared files: "
            + ", ".join(expected_files[key] for key in missing_files[:5])
        )
    if extra_files:
        raise W3DCorpusError(
            "effective-assets tree contains undeclared files: "
            + ", ".join(actual_files[key].relative_path for key in extra_files[:5])
        )
    if missing_directories:
        raise W3DCorpusError(
            "effective-assets tree is missing declared directories: "
            + ", ".join(expected_directories[key] for key in missing_directories[:5])
        )
    if extra_directories:
        raise W3DCorpusError(
            "effective-assets tree contains undeclared directories: "
            + ", ".join(actual_directories[key] for key in extra_directories[:5])
        )

    for item in manifest.files:
        actual = actual_files[item.path.casefold()]
        if actual.size != item.size:
            raise W3DCorpusError(
                f"effective-assets file size does not match the manifest: {item.path!r}"
            )
    return actual_files


def _selected_limit(value: int | None, hard_limit: int, label: str) -> int:
    selected = hard_limit if value is None else value
    if isinstance(selected, bool) or not isinstance(selected, int):
        raise TypeError(f"W3D corpus {label} limit must be an integer")
    if not 1 <= selected <= hard_limit:
        raise ValueError(f"W3D corpus {label} limit must be 1..{hard_limit}")
    return selected


def _read_verified_w3d(actual: _TreeFile, expected: _ManifestFile) -> bytes:
    if _is_link_like(actual.path):
        raise W3DCorpusError(
            f"manifest-declared W3D became linked: {expected.path!r}"
        )
    try:
        with actual.path.open("rb") as stream:
            source = stream.read(expected.size + 1)
    except OSError as exc:
        raise W3DCorpusError(
            f"manifest-declared W3D cannot be read: {expected.path!r}"
        ) from exc
    if len(source) != expected.size:
        raise W3DCorpusError(
            f"manifest-declared W3D size changed during read: {expected.path!r}"
        )
    if hashlib.sha256(source).hexdigest() != expected.sha256:
        raise W3DCorpusError(
            f"manifest-declared W3D SHA-256 does not match: {expected.path!r}"
        )
    return source


def _w3d_inventory_sha256(files: tuple[W3DCorpusFile, ...]) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.w3d-corpus-inventory",
            "schemaVersion": 0,
            "files": [item.neutral() for item in files],
        }
    )


def scan_w3d_corpus(
    effective_assets_root: Path | str,
    *,
    strict: bool = False,
    compact: bool = False,
    max_files: int | None = None,
    max_total_bytes: int | None = None,
) -> W3DCorpusReport:
    """Verify an effective-assets tree and scan its declared W3D corpus.

    Only entries whose manifest path has a case-insensitive ``.w3d`` suffix
    are opened.  All declared paths and sizes, the manifest schema/totals/
    aggregate, and the exact no-extras tree shape are validated first.

    ``max_files`` and ``max_total_bytes`` may lower, but never raise, the hard
    bounds inherited from :func:`scan_w3d_catalog`.  Strict mode raises
    :class:`W3DCorpusStrictError` with the completed report when parsed W3D
    warnings, failures, or duplicate logical IDs make the catalog incomplete.
    """

    if not isinstance(strict, bool):
        raise TypeError("W3D corpus strict flag must be a boolean")
    if not isinstance(compact, bool):
        raise TypeError("W3D corpus compact flag must be a boolean")
    selected_max_files = _selected_limit(
        max_files, MAX_W3D_CATALOG_FILES, "file count"
    )
    selected_max_bytes = _selected_limit(
        max_total_bytes, MAX_W3D_CATALOG_TOTAL_BYTES, "total byte"
    )

    root = _resolve_root(effective_assets_root)
    manifest_path, manifest = _load_manifest(root)
    actual_files = _validate_tree(root, manifest)
    selected = tuple(item for item in manifest.files if item.is_w3d)
    if not selected:
        raise W3DCorpusError("effective-assets manifest declares no W3D files")
    selected_bytes = sum(item.size for item in selected)
    if len(selected) > selected_max_files:
        raise W3DCorpusLimitError(
            f"W3D corpus file count exceeds {selected_max_files}"
        )
    if selected_bytes > selected_max_bytes:
        raise W3DCorpusLimitError(
            f"W3D corpus total bytes exceed {selected_max_bytes}"
        )

    inputs: list[tuple[str, bytes]] = []
    for item in selected:
        actual = actual_files[item.path.casefold()]
        inputs.append((item.path, _read_verified_w3d(actual, item)))
    catalog = scan_w3d_catalog(
        inputs,
        max_files=selected_max_files,
        max_total_bytes=selected_max_bytes,
    )
    w3d_files = tuple(item.corpus_source() for item in selected)
    inventory_sha256 = _w3d_inventory_sha256(w3d_files)
    identity_basis = {
        "schema": "openbfme.w3d-corpus-identity",
        "schemaVersion": 0,
        "manifestSha256": manifest.raw_sha256,
        "manifestAggregateSha256": manifest.aggregate_sha256,
        "w3dInventorySha256": inventory_sha256,
        "catalogInputSha256": catalog.input_sha256,
        "catalogMetadataSha256": catalog.metadata_sha256,
    }
    report = W3DCorpusReport(
        asset_root=root,
        manifest_path=manifest_path,
        manifest_sha256=manifest.raw_sha256,
        manifest_aggregate_sha256=manifest.aggregate_sha256,
        manifest_file_count=manifest.total_files,
        manifest_total_bytes=manifest.total_bytes,
        w3d_files=w3d_files,
        w3d_inventory_sha256=inventory_sha256,
        catalog=catalog,
        compact=compact,
        corpus_sha256=_canonical_sha256(identity_basis),
    )
    if strict and not report.complete:
        raise W3DCorpusStrictError(report)
    return report


# Concise alias for callers already operating at the corpus/import boundary.
scan_corpus = scan_w3d_corpus


__all__ = [
    "EFFECTIVE_ASSET_MANIFEST_RELATIVE",
    "EFFECTIVE_ASSET_MANIFEST_SCHEMA",
    "EFFECTIVE_ASSET_MANIFEST_VERSION",
    "W3D_CORPUS_SCHEMA",
    "W3D_CORPUS_SCHEMA_VERSION",
    "W3DCorpusError",
    "W3DCorpusFile",
    "W3DCorpusLimitError",
    "W3DCorpusReport",
    "W3DCorpusStrictError",
    "scan_corpus",
    "scan_w3d_corpus",
]
