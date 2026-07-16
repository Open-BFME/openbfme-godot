"""Deterministic expansion-wins overlay for sealed effective-assets trees.

The overlay is intentionally a private-workspace bridge, not a retail archive
reader.  Both inputs must already be complete ``effective-assets`` trees with
the canonical manifest emitted by :mod:`openbfme_importer.pipeline`.  Every
declared byte is checked before a regular-file-only combined tree is published.

The output uses the established effective-assets manifest schema so existing
map, W3D, and native-corpus readers can consume it unchanged.  Its catalog and
install identities bind both source manifest/root aggregates, the winner
provenance, the expansion-wins policy, and the overlay statistics.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
from typing import Any, Iterable, Mapping
import uuid

from .paths import ensure_external_to_repo, repo_root_from_module, safe_relative_parts


EFFECTIVE_ASSET_MANIFEST_SCHEMA = "openbfme.effective-assets-manifest"
EFFECTIVE_ASSET_MANIFEST_VERSION = 0
EFFECTIVE_ASSET_MANIFEST_RELATIVE = ".openbfme/manifest.json"
EFFECTIVE_ASSET_METADATA_DIRECTORY = ".openbfme"

OVERLAY_POLICY = "expansion-wins"
OVERLAY_IDENTITY_SCHEMA = "openbfme.edition-overlay-identity"
OVERLAY_IDENTITY_VERSION = 0
MAX_MANIFEST_BYTES = 64 * 1024 * 1024
MAX_OVERLAY_FILES = 50_000
MAX_OVERLAY_BYTES = 8 * 1024 * 1024 * 1024
HASH_BLOCK_BYTES = 1024 * 1024
_SHA256_CHARACTERS = frozenset("0123456789abcdef")


class EditionOverlayError(ValueError):
    """Base class for rejected or failed edition-overlay operations."""


class EditionOverlayLimitError(EditionOverlayError):
    """Raised before payload copying when a conservative limit is exceeded."""


class EditionOverlayReuseError(EditionOverlayError):
    """Raised when an existing destination cannot be safely reused."""


@dataclass(frozen=True, slots=True)
class EditionOverlayStats:
    """Exact expansion/base partition of the combined virtual path set."""

    base_only_files: int
    expansion_only_files: int
    overlap_identical_files: int
    overlap_overridden_files: int
    shadowed_base_bytes: int

    @property
    def overlap_files(self) -> int:
        return self.overlap_identical_files + self.overlap_overridden_files

    @property
    def output_files(self) -> int:
        return self.base_only_files + self.expansion_only_files + self.overlap_files

    def neutral(self) -> dict[str, int]:
        return {
            "baseOnlyFiles": self.base_only_files,
            "expansionOnlyFiles": self.expansion_only_files,
            "overlapIdenticalFiles": self.overlap_identical_files,
            "overlapOverriddenFiles": self.overlap_overridden_files,
            "shadowedBaseBytes": self.shadowed_base_bytes,
        }


@dataclass(frozen=True, slots=True)
class EditionOverlayReport:
    """Verified combined-tree result and its source seals."""

    base_root: Path
    expansion_root: Path
    output_root: Path
    manifest_path: Path
    base_manifest_sha256: str
    expansion_manifest_sha256: str
    base_root_aggregate_sha256: str
    expansion_root_aggregate_sha256: str
    aggregate_sha256: str
    identity_sha256: str
    manifest_sha256: str
    file_count: int
    total_bytes: int
    stats: EditionOverlayStats
    reused: bool

    def neutral(self) -> dict[str, object]:
        return {
            "schema": "openbfme.edition-overlay-report",
            "schemaVersion": 0,
            "policy": OVERLAY_POLICY,
            "sources": {
                "base": {
                    "manifestSha256": self.base_manifest_sha256,
                    "rootAggregateSha256": self.base_root_aggregate_sha256,
                },
                "expansion": {
                    "manifestSha256": self.expansion_manifest_sha256,
                    "rootAggregateSha256": self.expansion_root_aggregate_sha256,
                },
            },
            "totals": {"files": self.file_count, "bytes": self.total_bytes},
            "stats": self.stats.neutral(),
            "aggregateSha256": self.aggregate_sha256,
            "identitySha256": self.identity_sha256,
            "manifestSha256": self.manifest_sha256,
            "reused": self.reused,
        }


@dataclass(frozen=True, slots=True)
class _ManifestFile:
    path: str
    archive: str
    offset: int
    precedence: int
    size: int
    sha256: str

    def output_row(self, layer: str) -> dict[str, object]:
        return {
            "archive": f"layer-{layer}/{self.archive}",
            "offset": self.offset,
            "path": self.path,
            "precedence": self.precedence,
            "sha256": self.sha256,
            "size": self.size,
        }


@dataclass(frozen=True, slots=True)
class _Manifest:
    path: Path
    raw: bytes
    sha256: str
    aggregate_sha256: str
    files: tuple[_ManifestFile, ...]
    total_bytes: int
    catalog: Mapping[str, object]
    install: Mapping[str, object]
    document: Mapping[str, object]


@dataclass(frozen=True, slots=True)
class _TreeFile:
    relative_path: str
    path: Path
    size: int
    identity: tuple[int, int, int, int, int]


@dataclass(frozen=True, slots=True)
class _Winner:
    layer: str
    source: _ManifestFile


@dataclass(frozen=True, slots=True)
class _Overlay:
    winners: tuple[_Winner, ...]
    stats: EditionOverlayStats
    total_bytes: int
    document: Mapping[str, object]
    canonical_manifest: bytes
    aggregate_sha256: str
    identity_sha256: str


class _DuplicateJsonKey(ValueError):
    pass


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and value == value.casefold()
        and all(character in _SHA256_CHARACTERS for character in value)
    )


def _is_int(value: object, *, minimum: int = 0) -> bool:
    return not isinstance(value, bool) and isinstance(value, int) and value >= minimum


def _is_link_like(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(is_junction and is_junction())


def _stat_identity(value: os.stat_result) -> tuple[int, int, int, int, int]:
    return (
        value.st_dev,
        value.st_ino,
        value.st_size,
        value.st_mtime_ns,
        value.st_ctime_ns,
    )


def _opened_identity(value: os.stat_result) -> tuple[int, int, int, int]:
    """Identity fields represented consistently by path-stat and fstat.

    On Windows, CPython exposes a different ``st_ctime_ns`` interpretation for
    an open handle than for a path lookup.  Device, file ID, size, and write
    time remain stable and are sufficient for the open-race check; the stricter
    path snapshots still include ctime before and after the complete read.
    """

    return value.st_dev, value.st_ino, value.st_size, value.st_mtime_ns


def _canonical_json_bytes(value: object) -> bytes:
    return (
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")


def _canonical_sha256(value: object) -> str:
    raw = json.dumps(
        value,
        sort_keys=True,
        ensure_ascii=False,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(raw).hexdigest()


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


def _row_aggregate(files: Iterable[Mapping[str, object]]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(str(item["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(str(item["sha256"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _object_without_duplicate_keys(
    pairs: Iterable[tuple[str, object]],
) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateJsonKey(key)
        result[key] = value
    return result


def _reject_json_constant(value: str) -> object:
    raise ValueError(f"non-finite JSON value is not allowed: {value}")


def _absolute_unresolved(path: Path) -> Path:
    expanded = path.expanduser()
    return expanded if expanded.is_absolute() else Path.cwd() / expanded


def _refuse_link_chain(path: Path, *, context: str) -> None:
    absolute = _absolute_unresolved(path)
    current = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        current /= part
        if os.path.lexists(current) and _is_link_like(current):
            raise EditionOverlayError(f"{context} is linked: {current}")


def _resolve_source_root(value: Path | str, *, label: str) -> Path:
    try:
        candidate = Path(value)
    except TypeError as exc:
        raise TypeError(
            f"{label} effective-assets root must be a filesystem path"
        ) from exc
    _refuse_link_chain(candidate, context=f"{label} effective-assets root")
    try:
        root = candidate.expanduser().resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise EditionOverlayError(
            f"{label} effective-assets root is unavailable: {candidate}"
        ) from exc
    if not root.is_dir() or _is_link_like(root):
        raise EditionOverlayError(
            f"{label} effective-assets root is not an unlinked directory"
        )
    return root


def _paths_overlap(first: Path, second: Path) -> bool:
    try:
        common = os.path.commonpath(
            [os.path.normcase(str(first)), os.path.normcase(str(second))]
        )
    except ValueError:
        return False
    return common in {os.path.normcase(str(first)), os.path.normcase(str(second))}


def _resolve_output_root(
    value: Path | str, base_root: Path, expansion_root: Path
) -> Path:
    try:
        candidate = Path(value).expanduser()
    except TypeError as exc:
        raise TypeError(
            "edition overlay output root must be a filesystem path"
        ) from exc
    absolute = Path(os.path.abspath(candidate))
    if not absolute.name:
        raise EditionOverlayError(
            "edition overlay output root cannot be a filesystem anchor"
        )
    try:
        ensure_external_to_repo(absolute, repo_root_from_module())
    except ValueError as exc:
        raise EditionOverlayError(str(exc)) from exc
    for source in (base_root, expansion_root):
        if _paths_overlap(source, absolute):
            raise EditionOverlayError(
                "effective-assets inputs and edition overlay output must not overlap"
            )
    _refuse_link_chain(absolute.parent, context="edition overlay output parent")
    try:
        absolute.parent.mkdir(parents=True, exist_ok=True)
        _refuse_link_chain(absolute.parent, context="edition overlay output parent")
        parent = absolute.parent.resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise EditionOverlayError(
            "edition overlay output parent is unavailable"
        ) from exc
    if not parent.is_dir() or _is_link_like(parent):
        raise EditionOverlayError(
            "edition overlay output parent is not an unlinked directory"
        )
    output = parent / absolute.name
    for source in (base_root, expansion_root):
        if _paths_overlap(source, output):
            raise EditionOverlayError(
                "effective-assets inputs and edition overlay output must not overlap"
            )
    if os.path.lexists(output):
        if _is_link_like(output):
            raise EditionOverlayError("edition overlay output root must not be linked")
        if not output.is_dir():
            raise EditionOverlayError("edition overlay output root is not a directory")
    return output


def _read_strict_manifest(path: Path, *, label: str) -> tuple[dict[str, Any], bytes]:
    if _is_link_like(path) or not path.is_file():
        raise EditionOverlayError(f"{label} manifest is missing, linked, or not a file")
    try:
        before = path.stat()
        if before.st_nlink != 1:
            raise EditionOverlayError(f"{label} manifest must not be hard-linked")
        if not 1 <= before.st_size <= MAX_MANIFEST_BYTES:
            raise EditionOverlayLimitError(f"{label} manifest exceeds its safety bound")
        raw = path.read_bytes()
        after = path.stat()
    except EditionOverlayError:
        raise
    except OSError as exc:
        raise EditionOverlayError(f"{label} manifest cannot be read") from exc
    if len(raw) != before.st_size or _stat_identity(before) != _stat_identity(after):
        raise EditionOverlayError(f"{label} manifest changed during read")
    try:
        value = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_object_without_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise EditionOverlayError(f"{label} manifest is invalid JSON") from exc
    if not isinstance(value, dict):
        raise EditionOverlayError(f"{label} manifest root is not an object")
    if raw != _canonical_json_bytes(value):
        raise EditionOverlayError(f"{label} manifest encoding is not canonical")
    return value, raw


def _validate_manifest_file(raw: object, index: int, *, label: str) -> _ManifestFile:
    keys = {"archive", "offset", "path", "precedence", "sha256", "size"}
    if not isinstance(raw, dict) or set(raw) != keys:
        raise EditionOverlayError(
            f"{label} manifest file entry {index} has an invalid shape"
        )
    archive = raw.get("archive")
    path_value = raw.get("path")
    if not isinstance(archive, str) or not archive or not isinstance(path_value, str):
        raise EditionOverlayError(
            f"{label} manifest file entry {index} has invalid paths"
        )
    try:
        archive_parts = safe_relative_parts(archive)
        path_parts = safe_relative_parts(path_value)
    except (TypeError, ValueError) as exc:
        raise EditionOverlayError(
            f"{label} manifest file entry {index} has an unsafe path"
        ) from exc
    if archive != "/".join(archive_parts) or path_value != "/".join(path_parts):
        raise EditionOverlayError(
            f"{label} manifest file entry {index} has a non-canonical path"
        )
    if path_parts[0].casefold() == EFFECTIVE_ASSET_METADATA_DIRECTORY.casefold():
        raise EditionOverlayError(
            f"{label} manifest file entry {index} uses reserved metadata space"
        )
    offset = raw.get("offset")
    precedence = raw.get("precedence")
    size = raw.get("size")
    digest = raw.get("sha256")
    if (
        not _is_int(offset)
        or not _is_int(precedence)
        or not _is_int(size)
        or not _is_sha256(digest)
    ):
        raise EditionOverlayError(
            f"{label} manifest file entry {index} has invalid evidence"
        )
    return _ManifestFile(path_value, archive, offset, precedence, size, digest)


def _validate_inventory_paths(files: tuple[_ManifestFile, ...], *, label: str) -> None:
    seen: dict[str, str] = {}
    for item in files:
        key = item.path.casefold()
        if key in seen:
            raise EditionOverlayError(
                f"{label} manifest contains case-colliding paths: "
                f"{seen[key]!r} and {item.path!r}"
            )
        seen[key] = item.path
    paths = [item.path for item in files]
    if paths != sorted(paths, key=lambda value: value.casefold()):
        raise EditionOverlayError(f"{label} manifest inventory is not canonical")
    folded = set(seen)
    for key in folded:
        parts = key.split("/")
        for index in range(1, len(parts)):
            if "/".join(parts[:index]) in folded:
                raise EditionOverlayError(
                    f"{label} manifest has a file/directory path collision"
                )


def _load_manifest(root: Path, *, label: str) -> _Manifest:
    path = root.joinpath(*PurePosixPath(EFFECTIVE_ASSET_MANIFEST_RELATIVE).parts)
    _refuse_link_chain(path, context=f"{label} effective-assets manifest")
    document, raw = _read_strict_manifest(path, label=label)
    if set(document) != {
        "aggregate_sha256",
        "catalog",
        "files",
        "install",
        "schema",
        "schema_version",
        "totals",
    }:
        raise EditionOverlayError(f"{label} manifest top-level shape is invalid")
    if document.get("schema") != EFFECTIVE_ASSET_MANIFEST_SCHEMA:
        raise EditionOverlayError(f"{label} manifest schema is unsupported")
    if document.get("schema_version") != EFFECTIVE_ASSET_MANIFEST_VERSION or isinstance(
        document.get("schema_version"), bool
    ):
        raise EditionOverlayError(f"{label} manifest schema version is unsupported")

    catalog = document.get("catalog")
    if not isinstance(catalog, dict) or set(catalog) != {
        "archive_count",
        "entry_count",
        "format",
        "identity_sha256",
    }:
        raise EditionOverlayError(f"{label} manifest catalog identity is invalid")
    if (
        not _is_int(catalog.get("archive_count"))
        or not _is_int(catalog.get("entry_count"))
        or not _is_int(catalog.get("format"), minimum=1)
        or not _is_sha256(catalog.get("identity_sha256"))
    ):
        raise EditionOverlayError(f"{label} manifest catalog identity is invalid")
    install = document.get("install")
    if not isinstance(install, dict) or set(install) != {"identity_sha256", "root"}:
        raise EditionOverlayError(f"{label} manifest install identity is invalid")
    if (
        not _is_sha256(install.get("identity_sha256"))
        or not isinstance(install.get("root"), str)
        or not install.get("root")
    ):
        raise EditionOverlayError(f"{label} manifest install identity is invalid")

    raw_files = document.get("files")
    if not isinstance(raw_files, list):
        raise EditionOverlayError(f"{label} manifest file inventory is invalid")
    if len(raw_files) > MAX_OVERLAY_FILES:
        raise EditionOverlayLimitError(f"{label} manifest file count exceeds its bound")
    files = tuple(
        _validate_manifest_file(item, index, label=label)
        for index, item in enumerate(raw_files)
    )
    _validate_inventory_paths(files, label=label)
    if catalog["entry_count"] < len(files):
        raise EditionOverlayError(f"{label} manifest catalog entry count is invalid")

    totals = document.get("totals")
    if not isinstance(totals, dict) or set(totals) != {"bytes", "files"}:
        raise EditionOverlayError(f"{label} manifest totals are invalid")
    total_files = totals.get("files")
    total_bytes = totals.get("bytes")
    if not _is_int(total_files) or not _is_int(total_bytes):
        raise EditionOverlayError(f"{label} manifest totals are invalid")
    calculated_bytes = sum(item.size for item in files)
    if total_files != len(files) or total_bytes != calculated_bytes:
        raise EditionOverlayError(f"{label} manifest totals do not match its inventory")
    if total_bytes > MAX_OVERLAY_BYTES:
        raise EditionOverlayLimitError(f"{label} manifest byte total exceeds its bound")
    aggregate = document.get("aggregate_sha256")
    if not _is_sha256(aggregate) or aggregate != _manifest_aggregate(files):
        raise EditionOverlayError(f"{label} manifest aggregate SHA-256 is invalid")
    return _Manifest(
        path=path,
        raw=raw,
        sha256=hashlib.sha256(raw).hexdigest(),
        aggregate_sha256=aggregate,
        files=files,
        total_bytes=total_bytes,
        catalog=catalog,
        install=install,
        document=document,
    )


def _tree_collision(
    values: Mapping[str, _TreeFile | str], key: str, relative: str, *, label: str
) -> None:
    previous = values.get(key)
    if previous is None:
        return
    previous_path = (
        previous.relative_path if isinstance(previous, _TreeFile) else previous
    )
    raise EditionOverlayError(
        f"{label} contains case-colliding paths: {previous_path!r} and {relative!r}"
    )


def _scan_tree(
    root: Path, *, label: str
) -> tuple[dict[str, _TreeFile], dict[str, str]]:
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
            raise EditionOverlayError(f"{label} cannot be enumerated") from exc
        for entry in entries:
            path = Path(entry.path)
            relative = path.relative_to(root).as_posix()
            if entry.is_symlink() or _is_link_like(path):
                raise EditionOverlayError(f"{label} contains a link: {relative!r}")
            key = relative.casefold()
            try:
                # CPython's Windows ``DirEntry.stat(follow_symlinks=False)``
                # reports ``st_nlink == 0`` for ordinary files.  ``lstat``
                # returns the actual link count and still refuses to follow a
                # symlink introduced between enumeration and inspection.
                metadata = os.lstat(path)
                if stat.S_ISLNK(metadata.st_mode) or _is_link_like(path):
                    raise EditionOverlayError(f"{label} contains a link: {relative!r}")
                if stat.S_ISREG(metadata.st_mode):
                    if metadata.st_nlink != 1:
                        raise EditionOverlayError(
                            f"{label} contains a hard-linked file: {relative!r}"
                        )
                    _tree_collision(files, key, relative, label=label)
                    _tree_collision(directories, key, relative, label=label)
                    files[key] = _TreeFile(
                        relative,
                        path,
                        metadata.st_size,
                        _stat_identity(metadata),
                    )
                elif stat.S_ISDIR(metadata.st_mode):
                    _tree_collision(directories, key, relative, label=label)
                    _tree_collision(files, key, relative, label=label)
                    directories[key] = relative
                    pending.append(path)
                else:
                    raise EditionOverlayError(
                        f"{label} contains an unsupported filesystem entry: {relative!r}"
                    )
            except EditionOverlayError:
                raise
            except OSError as exc:
                raise EditionOverlayError(
                    f"{label} entry cannot be inspected: {relative!r}"
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


def _validated_tree(
    root: Path, manifest: _Manifest, *, label: str
) -> dict[str, _TreeFile]:
    actual_files, actual_directories = _scan_tree(root, label=label)
    declared = [item.path for item in manifest.files]
    declared.append(EFFECTIVE_ASSET_MANIFEST_RELATIVE)
    expected_files = {item.casefold(): item for item in declared}
    expected_directories = _expected_directories(declared)
    missing_files = sorted(set(expected_files) - set(actual_files))
    extra_files = sorted(set(actual_files) - set(expected_files))
    missing_directories = sorted(set(expected_directories) - set(actual_directories))
    extra_directories = sorted(set(actual_directories) - set(expected_directories))
    if missing_files:
        raise EditionOverlayError(
            f"{label} is missing declared files: "
            + ", ".join(expected_files[key] for key in missing_files[:5])
        )
    if extra_files:
        raise EditionOverlayError(
            f"{label} contains undeclared files: "
            + ", ".join(actual_files[key].relative_path for key in extra_files[:5])
        )
    if missing_directories:
        raise EditionOverlayError(
            f"{label} is missing declared directories: "
            + ", ".join(expected_directories[key] for key in missing_directories[:5])
        )
    if extra_directories:
        raise EditionOverlayError(
            f"{label} contains undeclared directories: "
            + ", ".join(actual_directories[key] for key in extra_directories[:5])
        )
    for item in manifest.files:
        if actual_files[item.path.casefold()].size != item.size:
            raise EditionOverlayError(
                f"{label} file size does not match its manifest: {item.path!r}"
            )
    return actual_files


def _select_limit(value: int | None, maximum: int, *, label: str) -> int:
    selected = maximum if value is None else value
    if isinstance(selected, bool) or not isinstance(selected, int):
        raise TypeError(f"edition overlay {label} limit must be an integer")
    if not 1 <= selected <= maximum:
        raise ValueError(f"edition overlay {label} limit must be in 1..{maximum}")
    return selected


def _source_identity(manifest: _Manifest) -> dict[str, object]:
    return {
        "manifestSha256": manifest.sha256,
        "rootAggregateSha256": manifest.aggregate_sha256,
        "catalogIdentitySha256": manifest.catalog["identity_sha256"],
        "installIdentitySha256": manifest.install["identity_sha256"],
    }


def _validate_combined_paths(winners: tuple[_Winner, ...]) -> None:
    folded = {item.source.path.casefold(): item.source.path for item in winners}
    for key in folded:
        parts = key.split("/")
        for index in range(1, len(parts)):
            if "/".join(parts[:index]) in folded:
                raise EditionOverlayError(
                    "layered effective-assets paths have a file/directory collision"
                )


def _make_overlay(
    base: _Manifest,
    expansion: _Manifest,
    *,
    max_files: int,
    max_total_bytes: int,
) -> _Overlay:
    if base.catalog["format"] != expansion.catalog["format"]:
        raise EditionOverlayError("base and expansion effective-assets formats differ")
    base_by_key = {item.path.casefold(): item for item in base.files}
    expansion_by_key = {item.path.casefold(): item for item in expansion.files}

    shared = set(base_by_key) & set(expansion_by_key)
    identical = sum(
        base_by_key[key].size == expansion_by_key[key].size
        and base_by_key[key].sha256 == expansion_by_key[key].sha256
        for key in shared
    )
    stats = EditionOverlayStats(
        base_only_files=len(set(base_by_key) - set(expansion_by_key)),
        expansion_only_files=len(set(expansion_by_key) - set(base_by_key)),
        overlap_identical_files=identical,
        overlap_overridden_files=len(shared) - identical,
        shadowed_base_bytes=sum(base_by_key[key].size for key in shared),
    )
    winner_map: dict[str, _Winner] = {
        key: _Winner("base", item) for key, item in base_by_key.items()
    }
    winner_map.update(
        {key: _Winner("expansion", item) for key, item in expansion_by_key.items()}
    )
    winners = tuple(
        winner_map[key]
        for key in sorted(
            winner_map, key=lambda value: (value, winner_map[value].source.path)
        )
    )
    _validate_combined_paths(winners)
    total_bytes = sum(item.source.size for item in winners)
    if len(winners) > max_files:
        raise EditionOverlayLimitError(
            f"edition overlay selects {len(winners)} files; limit is {max_files}"
        )
    if total_bytes > max_total_bytes:
        raise EditionOverlayLimitError(
            f"edition overlay selects {total_bytes} bytes; limit is {max_total_bytes}"
        )

    rows = [item.source.output_row(item.layer) for item in winners]
    aggregate = _row_aggregate(rows)
    sources = {"base": _source_identity(base), "expansion": _source_identity(expansion)}
    identity_basis = {
        "schema": OVERLAY_IDENTITY_SCHEMA,
        "schemaVersion": OVERLAY_IDENTITY_VERSION,
        "policy": OVERLAY_POLICY,
        "sources": sources,
        "stats": stats.neutral(),
        "totals": {"bytes": total_bytes, "files": len(winners)},
        "aggregateSha256": aggregate,
        "winners": [
            {
                "layer": item.layer,
                "archive": item.source.archive,
                "offset": item.source.offset,
                "path": item.source.path,
                "precedence": item.source.precedence,
                "sha256": item.source.sha256,
                "size": item.source.size,
            }
            for item in winners
        ],
    }
    identity = _canonical_sha256(identity_basis)
    install_identity = _canonical_sha256(
        {
            "schema": "openbfme.edition-overlay-install-identity",
            "schemaVersion": 0,
            "policy": OVERLAY_POLICY,
            "sources": sources,
            "stats": stats.neutral(),
        }
    )
    document: dict[str, object] = {
        "schema": EFFECTIVE_ASSET_MANIFEST_SCHEMA,
        "schema_version": EFFECTIVE_ASSET_MANIFEST_VERSION,
        "catalog": {
            "archive_count": int(base.catalog["archive_count"])
            + int(expansion.catalog["archive_count"]),
            "entry_count": int(base.catalog["entry_count"])
            + int(expansion.catalog["entry_count"]),
            "format": base.catalog["format"],
            "identity_sha256": identity,
        },
        "install": {
            "identity_sha256": install_identity,
            "root": "base+expansion/expansion-wins",
        },
        "totals": {"bytes": total_bytes, "files": len(winners)},
        "aggregate_sha256": aggregate,
        "files": rows,
    }
    return _Overlay(
        winners=winners,
        stats=stats,
        total_bytes=total_bytes,
        document=document,
        canonical_manifest=_canonical_json_bytes(document),
        aggregate_sha256=aggregate,
        identity_sha256=identity,
    )


def _copy_or_hash_file(
    actual: _TreeFile,
    expected: _ManifestFile,
    *,
    target: Path | None,
    label: str,
) -> None:
    if _is_link_like(actual.path):
        raise EditionOverlayError(
            f"{label} became linked before read: {expected.path!r}"
        )
    digest = hashlib.sha256()
    copied = 0
    output = None
    try:
        before = actual.path.stat()
        if before.st_nlink != 1 or not stat.S_ISREG(before.st_mode):
            raise EditionOverlayError(
                f"{label} is not an unlinked regular file: {expected.path!r}"
            )
        if target is not None:
            target.parent.mkdir(parents=True, exist_ok=True)
            output = target.open("xb")
        with actual.path.open("rb") as source:
            opened = os.fstat(source.fileno())
            if _opened_identity(opened) != _opened_identity(before):
                raise EditionOverlayError(
                    f"{label} changed while opening: {expected.path!r}"
                )
            while True:
                block = source.read(HASH_BLOCK_BYTES)
                if not block:
                    break
                digest.update(block)
                copied += len(block)
                if output is not None:
                    output.write(block)
        if output is not None:
            output.close()
            output = None
        after = actual.path.stat()
    except EditionOverlayError:
        if output is not None:
            output.close()
        if target is not None:
            target.unlink(missing_ok=True)
        raise
    except OSError as exc:
        if output is not None:
            output.close()
        if target is not None:
            target.unlink(missing_ok=True)
        raise EditionOverlayError(
            f"{label} could not be read: {expected.path!r}"
        ) from exc
    if (
        copied != expected.size
        or _stat_identity(before) != _stat_identity(after)
        or digest.hexdigest() != expected.sha256
    ):
        if target is not None:
            target.unlink(missing_ok=True)
        raise EditionOverlayError(
            f"{label} size or SHA-256 does not match its manifest: {expected.path!r}"
        )
    if target is not None:
        try:
            target_metadata = target.stat()
        except OSError as exc:
            raise EditionOverlayError(
                f"staged overlay file cannot be inspected: {expected.path!r}"
            ) from exc
        if (
            target_metadata.st_nlink != 1
            or not stat.S_ISREG(target_metadata.st_mode)
            or target_metadata.st_size != expected.size
        ):
            raise EditionOverlayError(
                f"staged overlay file is not an independent regular file: {expected.path!r}"
            )


def _verify_source_payloads(
    manifest: _Manifest,
    tree: Mapping[str, _TreeFile],
    *,
    label: str,
    targets: Mapping[str, Path] | None = None,
) -> None:
    for item in manifest.files:
        target = None if targets is None else targets.get(item.path.casefold())
        _copy_or_hash_file(
            tree[item.path.casefold()],
            item,
            target=target,
            label=f"{label} effective-assets file",
        )


def _revalidate_source(
    root: Path,
    manifest: _Manifest,
    snapshot: Mapping[str, _TreeFile],
    *,
    label: str,
) -> None:
    current_manifest = _load_manifest(root, label=label)
    if (
        current_manifest.raw != manifest.raw
        or current_manifest.sha256 != manifest.sha256
    ):
        raise EditionOverlayError(
            f"{label} manifest changed during overlay verification"
        )
    current = _validated_tree(
        root, current_manifest, label=f"{label} effective-assets tree"
    )
    if set(current) != set(snapshot):
        raise EditionOverlayError(
            f"{label} effective-assets tree changed during overlay verification"
        )
    for key, prior in snapshot.items():
        if current[key].identity != prior.identity:
            raise EditionOverlayError(
                f"{label} effective-assets file changed during overlay verification: "
                f"{prior.relative_path!r}"
            )


def _verify_output(
    root: Path,
    overlay: _Overlay,
    *,
    reused: bool,
    base_root: Path,
    expansion_root: Path,
    base_manifest: _Manifest,
    expansion_manifest: _Manifest,
) -> EditionOverlayReport:
    if _is_link_like(root) or not root.is_dir():
        raise EditionOverlayError(
            "edition overlay output is missing, linked, or not a directory"
        )
    manifest = _load_manifest(root, label="edition overlay output")
    if (
        manifest.raw != overlay.canonical_manifest
        or manifest.document != overlay.document
    ):
        raise EditionOverlayError(
            "edition overlay output manifest identity does not match its sources"
        )
    tree = _validated_tree(root, manifest, label="edition overlay output tree")
    _verify_source_payloads(manifest, tree, label="edition overlay output")
    if manifest.aggregate_sha256 != overlay.aggregate_sha256:
        raise EditionOverlayError("edition overlay output aggregate changed")
    current_manifest = _load_manifest(root, label="edition overlay output")
    if current_manifest.raw != manifest.raw:
        raise EditionOverlayError(
            "edition overlay output manifest changed during backtest"
        )
    current_tree = _validated_tree(
        root,
        current_manifest,
        label="edition overlay output tree",
    )
    if set(current_tree) != set(tree) or any(
        current_tree[key].identity != item.identity for key, item in tree.items()
    ):
        raise EditionOverlayError("edition overlay output tree changed during backtest")
    return EditionOverlayReport(
        base_root=base_root,
        expansion_root=expansion_root,
        output_root=root,
        manifest_path=manifest.path,
        base_manifest_sha256=base_manifest.sha256,
        expansion_manifest_sha256=expansion_manifest.sha256,
        base_root_aggregate_sha256=base_manifest.aggregate_sha256,
        expansion_root_aggregate_sha256=expansion_manifest.aggregate_sha256,
        aggregate_sha256=manifest.aggregate_sha256,
        identity_sha256=overlay.identity_sha256,
        manifest_sha256=manifest.sha256,
        file_count=len(manifest.files),
        total_bytes=manifest.total_bytes,
        stats=overlay.stats,
        reused=reused,
    )


def _remove_owned_tree(path: Path, parent: Path, prefix: str) -> None:
    if not os.path.lexists(path):
        return
    if path.parent != parent or not path.name.startswith(prefix) or _is_link_like(path):
        raise EditionOverlayError(
            "refused to remove an unowned overlay transaction path"
        )
    _scan_tree(path, label="edition overlay transaction tree")
    shutil.rmtree(path)


def _publish(stage: Path, destination: Path, backup: Path) -> bool:
    had_destination = os.path.lexists(destination)
    if had_destination:
        _scan_tree(destination, label="existing edition overlay tree")
        try:
            os.replace(destination, backup)
        except OSError as exc:
            raise EditionOverlayError(
                "existing edition overlay could not enter the publish transaction"
            ) from exc
    try:
        os.replace(stage, destination)
    except Exception as publish_error:
        if had_destination and os.path.lexists(backup):
            try:
                os.replace(backup, destination)
            except Exception as rollback_error:
                raise EditionOverlayError(
                    "edition overlay publish failed and rollback could not restore the prior output"
                ) from rollback_error
        raise EditionOverlayError(
            "edition overlay publish failed; prior output was preserved"
        ) from publish_error
    return had_destination


def _rollback_publication(
    stage: Path,
    destination: Path,
    backup: Path,
    *,
    had_destination: bool,
) -> None:
    try:
        if os.path.lexists(destination):
            if os.path.lexists(stage):
                raise EditionOverlayError(
                    "edition overlay rollback staging path is unexpectedly occupied"
                )
            os.replace(destination, stage)
        if had_destination:
            if not os.path.lexists(backup):
                raise EditionOverlayError(
                    "edition overlay rollback backup is unexpectedly missing"
                )
            os.replace(backup, destination)
    except Exception as exc:
        if isinstance(exc, EditionOverlayError):
            raise
        raise EditionOverlayError(
            "published edition overlay failed verification and rollback could not restore the prior output"
        ) from exc


def _discard_backup(backup: Path, destination: Path) -> None:
    if os.path.lexists(backup):
        try:
            _remove_owned_tree(
                backup,
                destination.parent,
                f".{destination.name}.backup-",
            )
        except (EditionOverlayError, OSError):
            # Publication is already complete.  A verified, uniquely owned old
            # backup may safely remain for manual cleanup; this is not a failed
            # conversion and does not weaken the new destination's backtest.
            pass


def build_edition_overlay(
    base_effective_assets_root: Path | str,
    expansion_effective_assets_root: Path | str,
    output_root: Path | str,
    *,
    max_files: int | None = None,
    max_total_bytes: int | None = None,
    force: bool = False,
) -> EditionOverlayReport:
    """Verify and materialize a deterministic BFME2 + RotWK winner tree.

    Virtual paths are compared case-insensitively, matching SAGE lookup rules;
    the expansion path and its spelling win an overlap.  Case collisions within
    either sealed input, and file/directory collisions among surviving winners,
    are rejected.  Both complete input trees are checked against their canonical
    manifests, including shadowed base payloads.  A matching existing output is
    fully backtested and returned with ``reused=True`` without rewriting any
    byte.

    ``force=True`` permits transactional replacement of an existing regular
    directory.  The prior tree remains in place until a fully staged output has
    passed the same manifest/tree/hash validation used for reuse.
    """

    if not isinstance(force, bool):
        raise TypeError("edition overlay force flag must be a boolean")
    selected_max_files = _select_limit(max_files, MAX_OVERLAY_FILES, label="file count")
    selected_max_bytes = _select_limit(
        max_total_bytes, MAX_OVERLAY_BYTES, label="total byte"
    )
    base_root = _resolve_source_root(base_effective_assets_root, label="base")
    expansion_root = _resolve_source_root(
        expansion_effective_assets_root, label="expansion"
    )
    if _paths_overlap(base_root, expansion_root):
        raise EditionOverlayError(
            "base and expansion effective-assets roots must not overlap"
        )
    destination = _resolve_output_root(output_root, base_root, expansion_root)

    base_manifest = _load_manifest(base_root, label="base")
    expansion_manifest = _load_manifest(expansion_root, label="expansion")
    base_tree = _validated_tree(
        base_root, base_manifest, label="base effective-assets tree"
    )
    expansion_tree = _validated_tree(
        expansion_root,
        expansion_manifest,
        label="expansion effective-assets tree",
    )
    overlay = _make_overlay(
        base_manifest,
        expansion_manifest,
        max_files=selected_max_files,
        max_total_bytes=selected_max_bytes,
    )

    if destination.is_dir() and not force:
        # Reuse has no staging-copy pass, so verify every source byte here,
        # including base entries shadowed by expansion.  A build performs the
        # same complete check while copying winners into its private stage.
        _verify_source_payloads(base_manifest, base_tree, label="base")
        _verify_source_payloads(expansion_manifest, expansion_tree, label="expansion")
        _revalidate_source(base_root, base_manifest, base_tree, label="base")
        _revalidate_source(
            expansion_root,
            expansion_manifest,
            expansion_tree,
            label="expansion",
        )
        try:
            return _verify_output(
                destination,
                overlay,
                reused=True,
                base_root=base_root,
                expansion_root=expansion_root,
                base_manifest=base_manifest,
                expansion_manifest=expansion_manifest,
            )
        except EditionOverlayError as exc:
            raise EditionOverlayReuseError(
                f"existing edition overlay failed verification: {exc}"
            ) from exc

    parent = destination.parent
    token = uuid.uuid4().hex
    stage = parent / f".{destination.name}.staging-{token}"
    backup = parent / f".{destination.name}.backup-{token}"
    try:
        stage.mkdir()
    except OSError as exc:
        raise EditionOverlayError(
            "edition overlay staging directory could not be created"
        ) from exc
    try:
        selected_base = {
            winner.source.path.casefold(): stage.joinpath(
                *PurePosixPath(winner.source.path).parts
            )
            for winner in overlay.winners
            if winner.layer == "base"
        }
        selected_expansion = {
            winner.source.path.casefold(): stage.joinpath(
                *PurePosixPath(winner.source.path).parts
            )
            for winner in overlay.winners
            if winner.layer == "expansion"
        }
        _verify_source_payloads(
            base_manifest,
            base_tree,
            label="base",
            targets=selected_base,
        )
        _verify_source_payloads(
            expansion_manifest,
            expansion_tree,
            label="expansion",
            targets=selected_expansion,
        )
        metadata = stage / EFFECTIVE_ASSET_METADATA_DIRECTORY
        metadata.mkdir()
        (metadata / "manifest.json").write_bytes(overlay.canonical_manifest)

        _revalidate_source(base_root, base_manifest, base_tree, label="base")
        _revalidate_source(
            expansion_root,
            expansion_manifest,
            expansion_tree,
            label="expansion",
        )
        staged_report = _verify_output(
            stage,
            overlay,
            reused=False,
            base_root=base_root,
            expansion_root=expansion_root,
            base_manifest=base_manifest,
            expansion_manifest=expansion_manifest,
        )
        had_destination = _publish(stage, destination, backup)
        try:
            published = _verify_output(
                destination,
                overlay,
                reused=False,
                base_root=base_root,
                expansion_root=expansion_root,
                base_manifest=base_manifest,
                expansion_manifest=expansion_manifest,
            )
            if published.identity_sha256 != staged_report.identity_sha256:
                raise EditionOverlayError(
                    "edition overlay identity changed during publication"
                )
        except Exception as verification_error:
            try:
                _rollback_publication(
                    stage,
                    destination,
                    backup,
                    had_destination=had_destination,
                )
            except EditionOverlayError as rollback_error:
                raise EditionOverlayError(
                    "published edition overlay failed verification and rollback could not restore the prior output"
                ) from rollback_error
            raise EditionOverlayError(
                "published edition overlay failed verification; prior output was preserved"
            ) from verification_error
        _discard_backup(backup, destination)
        return published
    finally:
        if os.path.lexists(stage):
            _remove_owned_tree(stage, parent, f".{destination.name}.staging-")
        if os.path.lexists(backup) and not os.path.lexists(destination):
            try:
                os.replace(backup, destination)
            except OSError as exc:
                raise EditionOverlayError(
                    "edition overlay cleanup could not restore the prior output"
                ) from exc
