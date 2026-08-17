"""Verified, deterministic private staging for every effective-assets W3D.

This module is a byte-preserving boundary between a sealed effective-assets
tree and the W3D job planner.  It does not parse W3D, resolve textures, choose
models, or claim that any staged file is convertible.  Every manifest-declared
source byte is verified, every case-insensitive ``.w3d`` winner is copied, and
the original canonical POSIX path is retained so model/animation/hierarchy
neighbourhoods remain intact.

The emitted manifest contains retail virtual paths and source digests.  A
caller handling retail data must therefore place the destination in the
repository's gitignored ``workspace`` workspace.
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

W3D_INPUT_STAGE_SCHEMA = "openbfme.w3d-input-stage"
W3D_INPUT_STAGE_SCHEMA_VERSION = 0
W3D_INPUT_STAGE_MANIFEST = ".openbfme/w3d-input-stage.json"

MAX_MANIFEST_BYTES = 64 * 1024 * 1024
MAX_EFFECTIVE_ASSET_FILES = 50_000
MAX_EFFECTIVE_ASSET_BYTES = 8 * 1024 * 1024 * 1024
MAX_W3D_STAGE_FILES = 50_000
MAX_W3D_STAGE_BYTES = 8 * 1024 * 1024 * 1024
HASH_BLOCK_BYTES = 1024 * 1024

_METADATA_DIRECTORY = ".openbfme"
_SHA256_CHARACTERS = frozenset("0123456789abcdef")


class W3DInputStageError(ValueError):
    """Base class for rejected or failed W3D input staging operations."""


class W3DInputStageLimitError(W3DInputStageError):
    """Raised before publication when a conservative bound is exceeded."""


class W3DInputStageReuseError(W3DInputStageError):
    """Raised when an existing destination cannot be exactly reused."""


@dataclass(frozen=True, slots=True)
class W3DInputStageFile:
    """One byte-identical W3D path mapping in the private stage."""

    source_path: str
    staged_path: str
    source_bytes: int
    source_sha256: str

    def neutral(self) -> dict[str, object]:
        return {
            "sourceBytes": self.source_bytes,
            "sourcePath": self.source_path,
            "sourceSha256": self.source_sha256,
            "stagedPath": self.staged_path,
        }


@dataclass(frozen=True, slots=True)
class W3DInputStageReport:
    """Verified source, selection, and published-tree evidence."""

    source_root: Path
    output_root: Path
    manifest_path: Path
    source_manifest_sha256: str
    source_manifest_aggregate_sha256: str
    source_manifest_file_count: int
    source_manifest_total_bytes: int
    files: tuple[W3DInputStageFile, ...]
    selected_inventory_sha256: str
    selected_root_sha256: str
    output_tree_sha256: str
    request_sha256: str
    identity_sha256: str
    manifest_sha256: str
    max_files: int
    max_total_bytes: int
    reused: bool

    @property
    def file_count(self) -> int:
        return len(self.files)

    @property
    def total_bytes(self) -> int:
        return sum(item.source_bytes for item in self.files)

    @property
    def staged_inputs(self) -> dict[str, str]:
        """Return the exact mapping accepted by ``plan_w3d_jobs``."""

        return {item.source_path: item.staged_path for item in self.files}

    def neutral(self) -> dict[str, object]:
        """Return deterministic JSON-ready evidence without host paths."""

        return {
            "schema": "openbfme.w3d-input-stage-report",
            "schemaVersion": 0,
            "source": {
                "manifestAggregateSha256": self.source_manifest_aggregate_sha256,
                "manifestFileCount": self.source_manifest_file_count,
                "manifestSha256": self.source_manifest_sha256,
                "manifestTotalBytes": self.source_manifest_total_bytes,
            },
            "selection": {
                "files": self.file_count,
                "bytes": self.total_bytes,
                "inventorySha256": self.selected_inventory_sha256,
                "rootSha256": self.selected_root_sha256,
            },
            "outputTreeSha256": self.output_tree_sha256,
            "requestSha256": self.request_sha256,
            "identitySha256": self.identity_sha256,
            "manifestSha256": self.manifest_sha256,
            "limits": {
                "maxFiles": self.max_files,
                "maxTotalBytes": self.max_total_bytes,
            },
            "files": [item.neutral() for item in self.files],
            "reused": self.reused,
        }

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class _ManifestFile:
    path: str
    size: int
    sha256: str

    @property
    def is_w3d(self) -> bool:
        return PurePosixPath(self.path).suffix.casefold() == ".w3d"


@dataclass(frozen=True, slots=True)
class _Manifest:
    path: Path
    raw: bytes
    sha256: str
    aggregate_sha256: str
    files: tuple[_ManifestFile, ...]
    total_bytes: int


@dataclass(frozen=True, slots=True)
class _TreeFile:
    relative_path: str
    path: Path
    size: int
    identity: tuple[int, int, int, int, int, int]


@dataclass(frozen=True, slots=True)
class _StageSpec:
    files: tuple[W3DInputStageFile, ...]
    selected_inventory_sha256: str
    selected_root_sha256: str
    output_tree_sha256: str
    request_sha256: str
    identity_sha256: str
    document: Mapping[str, object]
    canonical_manifest: bytes
    max_files: int
    max_total_bytes: int


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


def _domain_inventory_sha256(domain: str, rows: Iterable[Mapping[str, object]]) -> str:
    digest = hashlib.sha256()
    digest.update(domain.encode("ascii"))
    digest.update(b"\n")
    for row in rows:
        path = str(row["path"])
        size = int(row["size"])
        source_sha256 = str(row["sha256"])
        digest.update(path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(size).encode("ascii"))
        digest.update(b"\0")
        digest.update(source_sha256.encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _manifest_aggregate(files: Iterable[_ManifestFile]) -> str:
    return _domainless_inventory_sha256(
        {"path": item.path, "size": item.size, "sha256": item.sha256} for item in files
    )


def _domainless_inventory_sha256(rows: Iterable[Mapping[str, object]]) -> str:
    digest = hashlib.sha256()
    for row in rows:
        digest.update(str(row["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(row["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(str(row["sha256"]).encode("ascii"))
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


def _is_link_like(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(is_junction and is_junction())


def _stat_identity(value: os.stat_result) -> tuple[int, int, int, int, int, int]:
    return (
        int(value.st_dev),
        int(value.st_ino),
        int(value.st_mode),
        int(value.st_nlink),
        int(value.st_size),
        int(value.st_mtime_ns),
    )


def _absolute_unresolved(path: Path) -> Path:
    expanded = path.expanduser()
    return expanded if expanded.is_absolute() else Path.cwd() / expanded


def _refuse_link_chain(path: Path, *, context: str) -> None:
    absolute = _absolute_unresolved(path)
    current = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        current /= part
        try:
            linked = os.path.lexists(current) and _is_link_like(current)
        except OSError as exc:
            raise W3DInputStageError(
                f"{context} link state cannot be inspected"
            ) from exc
        if linked:
            raise W3DInputStageError(f"{context} is linked: {current}")


def _resolve_source_root(value: Path | str) -> Path:
    try:
        candidate = Path(value)
    except TypeError as exc:
        raise TypeError("effective-assets root must be a filesystem path") from exc
    _refuse_link_chain(candidate, context="effective-assets root")
    try:
        root = candidate.expanduser().resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise W3DInputStageError(
            f"effective-assets root is unavailable: {candidate}"
        ) from exc
    if not root.is_dir() or _is_link_like(root):
        raise W3DInputStageError("effective-assets root is not an unlinked directory")
    return root


def _paths_overlap(first: Path, second: Path) -> bool:
    try:
        common = os.path.commonpath(
            [os.path.normcase(str(first)), os.path.normcase(str(second))]
        )
    except ValueError:
        return False
    return common in {os.path.normcase(str(first)), os.path.normcase(str(second))}


def _resolve_output_root(value: Path | str, source_root: Path) -> Path:
    try:
        candidate = Path(value).expanduser()
    except TypeError as exc:
        raise TypeError("W3D input stage root must be a filesystem path") from exc
    absolute = Path(os.path.abspath(candidate))
    if not absolute.name:
        raise W3DInputStageError("W3D input stage root cannot be a filesystem anchor")
    if _paths_overlap(source_root, absolute):
        raise W3DInputStageError(
            "effective-assets root and W3D input stage root must not overlap"
        )
    try:
        ensure_external_to_repo(absolute, repo_root_from_module())
    except ValueError as exc:
        raise W3DInputStageError(str(exc)) from exc
    _refuse_link_chain(absolute.parent, context="W3D input stage parent")
    try:
        absolute.parent.mkdir(parents=True, exist_ok=True)
        _refuse_link_chain(absolute.parent, context="W3D input stage parent")
        parent = absolute.parent.resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise W3DInputStageError("W3D input stage parent is unavailable") from exc
    if not parent.is_dir() or _is_link_like(parent):
        raise W3DInputStageError("W3D input stage parent is not an unlinked directory")
    output = parent / absolute.name
    if _paths_overlap(source_root, output):
        raise W3DInputStageError(
            "effective-assets root and W3D input stage root must not overlap"
        )
    if os.path.lexists(output):
        if _is_link_like(output):
            raise W3DInputStageError("W3D input stage root must not be linked")
        if not output.is_dir():
            raise W3DInputStageError("W3D input stage root is not a directory")
    return output


def _read_strict_json(path: Path, *, label: str) -> tuple[dict[str, Any], bytes]:
    _refuse_link_chain(path, context=label)
    if not path.is_file() or _is_link_like(path):
        raise W3DInputStageError(f"{label} is missing, linked, or not a file")
    try:
        before = path.stat()
        if before.st_nlink != 1 or not stat.S_ISREG(before.st_mode):
            raise W3DInputStageError(f"{label} is not an independent ordinary file")
        if not 1 <= before.st_size <= MAX_MANIFEST_BYTES:
            raise W3DInputStageLimitError(f"{label} exceeds its safety bound")
        raw = path.read_bytes()
        after = path.stat()
    except W3DInputStageError:
        raise
    except OSError as exc:
        raise W3DInputStageError(f"{label} cannot be read") from exc
    if len(raw) != before.st_size or _stat_identity(before) != _stat_identity(after):
        raise W3DInputStageError(f"{label} changed during read")
    try:
        document = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_object_without_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise W3DInputStageError(f"{label} is invalid JSON") from exc
    if not isinstance(document, dict):
        raise W3DInputStageError(f"{label} root is not an object")
    if raw != _canonical_json_bytes(document):
        raise W3DInputStageError(f"{label} encoding is not canonical")
    return document, raw


def _safe_manifest_path(value: object, *, label: str) -> str:
    if not isinstance(value, str):
        raise W3DInputStageError(f"{label} is not a path string")
    try:
        parts = safe_relative_parts(value)
    except (TypeError, ValueError) as exc:
        raise W3DInputStageError(f"{label} is unsafe") from exc
    canonical = "/".join(parts)
    if canonical != value:
        raise W3DInputStageError(f"{label} is not canonical")
    return canonical


def _validate_manifest_file(raw: object, index: int) -> _ManifestFile:
    expected = {"archive", "offset", "path", "precedence", "sha256", "size"}
    if not isinstance(raw, dict) or set(raw) != expected:
        raise W3DInputStageError(
            f"effective-assets manifest file entry {index} has an invalid shape"
        )
    archive = _safe_manifest_path(
        raw.get("archive"), label=f"effective-assets archive path {index}"
    )
    if not archive:
        raise W3DInputStageError(
            f"effective-assets manifest file entry {index} has an invalid archive"
        )
    path = _safe_manifest_path(
        raw.get("path"), label=f"effective-assets file path {index}"
    )
    if path.split("/", 1)[0].casefold() == _METADATA_DIRECTORY.casefold():
        raise W3DInputStageError(
            f"effective-assets manifest file entry {index} uses reserved metadata space"
        )
    size = raw.get("size")
    digest = raw.get("sha256")
    if (
        not _is_int(raw.get("offset"))
        or not _is_int(raw.get("precedence"))
        or not _is_int(size)
        or not _is_sha256(digest)
    ):
        raise W3DInputStageError(
            f"effective-assets manifest file entry {index} has invalid evidence"
        )
    return _ManifestFile(path=path, size=size, sha256=digest)


def _validate_manifest_paths(files: tuple[_ManifestFile, ...]) -> None:
    seen: dict[str, str] = {}
    for item in files:
        key = item.path.casefold()
        if key in seen:
            raise W3DInputStageError(
                "effective-assets manifest contains case-colliding paths: "
                f"{seen[key]!r} and {item.path!r}"
            )
        seen[key] = item.path
    paths = [item.path for item in files]
    if paths != sorted(paths, key=lambda value: (value.casefold(), value)):
        raise W3DInputStageError("effective-assets manifest inventory is not canonical")
    folded = set(seen)
    for key in folded:
        parts = key.split("/")
        for index in range(1, len(parts)):
            if "/".join(parts[:index]) in folded:
                raise W3DInputStageError(
                    "effective-assets manifest has a file/directory path collision"
                )


def _load_manifest(root: Path) -> _Manifest:
    path = root.joinpath(*PurePosixPath(EFFECTIVE_ASSET_MANIFEST_RELATIVE).parts)
    document, raw = _read_strict_json(path, label="effective-assets manifest")
    if set(document) != {
        "aggregate_sha256",
        "catalog",
        "files",
        "install",
        "schema",
        "schema_version",
        "totals",
    }:
        raise W3DInputStageError("effective-assets manifest top-level shape is invalid")
    if document.get("schema") != EFFECTIVE_ASSET_MANIFEST_SCHEMA:
        raise W3DInputStageError("effective-assets manifest schema is unsupported")
    if document.get("schema_version") != EFFECTIVE_ASSET_MANIFEST_VERSION or isinstance(
        document.get("schema_version"), bool
    ):
        raise W3DInputStageError(
            "effective-assets manifest schema version is unsupported"
        )
    catalog = document.get("catalog")
    if not isinstance(catalog, dict) or set(catalog) != {
        "archive_count",
        "entry_count",
        "format",
        "identity_sha256",
    }:
        raise W3DInputStageError(
            "effective-assets manifest catalog identity is invalid"
        )
    if (
        not _is_int(catalog.get("archive_count"))
        or not _is_int(catalog.get("entry_count"))
        or not _is_int(catalog.get("format"), minimum=1)
        or not _is_sha256(catalog.get("identity_sha256"))
    ):
        raise W3DInputStageError(
            "effective-assets manifest catalog identity is invalid"
        )
    install = document.get("install")
    if not isinstance(install, dict) or set(install) != {"identity_sha256", "root"}:
        raise W3DInputStageError(
            "effective-assets manifest install identity is invalid"
        )
    if (
        not _is_sha256(install.get("identity_sha256"))
        or not isinstance(install.get("root"), str)
        or not install.get("root")
    ):
        raise W3DInputStageError(
            "effective-assets manifest install identity is invalid"
        )
    raw_files = document.get("files")
    if not isinstance(raw_files, list):
        raise W3DInputStageError("effective-assets manifest file inventory is invalid")
    if len(raw_files) > MAX_EFFECTIVE_ASSET_FILES:
        raise W3DInputStageLimitError(
            "effective-assets manifest file count exceeds its hard bound"
        )
    files = tuple(
        _validate_manifest_file(item, index) for index, item in enumerate(raw_files)
    )
    _validate_manifest_paths(files)
    if catalog["entry_count"] < len(files):
        raise W3DInputStageError(
            "effective-assets manifest catalog entry count is invalid"
        )
    totals = document.get("totals")
    if not isinstance(totals, dict) or set(totals) != {"bytes", "files"}:
        raise W3DInputStageError("effective-assets manifest totals are invalid")
    total_files = totals.get("files")
    total_bytes = totals.get("bytes")
    if not _is_int(total_files) or not _is_int(total_bytes):
        raise W3DInputStageError("effective-assets manifest totals are invalid")
    calculated_bytes = sum(item.size for item in files)
    if total_files != len(files) or total_bytes != calculated_bytes:
        raise W3DInputStageError(
            "effective-assets manifest totals do not match its inventory"
        )
    if total_bytes > MAX_EFFECTIVE_ASSET_BYTES:
        raise W3DInputStageLimitError(
            "effective-assets manifest byte total exceeds its hard bound"
        )
    aggregate = document.get("aggregate_sha256")
    if not _is_sha256(aggregate) or aggregate != _manifest_aggregate(files):
        raise W3DInputStageError(
            "effective-assets manifest aggregate SHA-256 is invalid"
        )
    return _Manifest(
        path=path,
        raw=raw,
        sha256=hashlib.sha256(raw).hexdigest(),
        aggregate_sha256=aggregate,
        files=files,
        total_bytes=total_bytes,
    )


def _tree_collision(
    values: Mapping[str, _TreeFile | str], key: str, relative: str, *, label: str
) -> None:
    prior = values.get(key)
    if prior is None:
        return
    prior_path = prior.relative_path if isinstance(prior, _TreeFile) else prior
    raise W3DInputStageError(
        f"{label} contains case-colliding paths: {prior_path!r} and {relative!r}"
    )


def _scan_tree(
    root: Path, *, label: str
) -> tuple[dict[str, _TreeFile], dict[str, str]]:
    if _is_link_like(root) or not root.is_dir():
        raise W3DInputStageError(f"{label} is missing, linked, or not a directory")
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
            raise W3DInputStageError(f"{label} cannot be enumerated") from exc
        for entry in entries:
            path = Path(entry.path)
            relative = path.relative_to(root).as_posix()
            if entry.is_symlink() or _is_link_like(path):
                raise W3DInputStageError(f"{label} contains a link: {relative!r}")
            try:
                # On Windows ``DirEntry.stat(follow_symlinks=False)`` can
                # report ``st_nlink == 0`` for an ordinary file.  ``lstat``
                # supplies the real link count while preserving no-follow
                # semantics, which is required for the hard-link gate.
                metadata = path.lstat()
            except OSError as exc:
                raise W3DInputStageError(
                    f"{label} entry cannot be inspected: {relative!r}"
                ) from exc
            key = relative.casefold()
            if stat.S_ISREG(metadata.st_mode):
                if metadata.st_nlink != 1:
                    raise W3DInputStageError(
                        f"{label} contains a hard-linked file: {relative!r}"
                    )
                _tree_collision(files, key, relative, label=label)
                _tree_collision(directories, key, relative, label=label)
                files[key] = _TreeFile(
                    relative_path=relative,
                    path=path,
                    size=metadata.st_size,
                    identity=_stat_identity(metadata),
                )
            elif stat.S_ISDIR(metadata.st_mode):
                _tree_collision(directories, key, relative, label=label)
                _tree_collision(files, key, relative, label=label)
                directories[key] = relative
                pending.append(path)
            else:
                raise W3DInputStageError(
                    f"{label} contains a non-ordinary entry: {relative!r}"
                )
    return files, directories


def _expected_directories(paths: Iterable[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for path in paths:
        parts = PurePosixPath(path).parts
        for index in range(1, len(parts)):
            relative = "/".join(parts[:index])
            key = relative.casefold()
            prior = result.get(key)
            if prior is None or relative < prior:
                result[key] = relative
    return result


def _validate_source_tree(root: Path, manifest: _Manifest) -> dict[str, _TreeFile]:
    files, directories = _scan_tree(root, label="effective-assets tree")
    declared_paths = [EFFECTIVE_ASSET_MANIFEST_RELATIVE]
    declared_paths.extend(item.path for item in manifest.files)
    expected_files = {path.casefold(): path for path in declared_paths}
    expected_directories = _expected_directories(declared_paths)
    if set(files) != set(expected_files):
        missing = sorted(set(expected_files) - set(files))
        extra = sorted(set(files) - set(expected_files))
        if missing:
            raise W3DInputStageError(
                "effective-assets tree is missing declared files: "
                + ", ".join(expected_files[key] for key in missing[:5])
            )
        raise W3DInputStageError(
            "effective-assets tree contains undeclared files: "
            + ", ".join(files[key].relative_path for key in extra[:5])
        )
    if set(directories) != set(expected_directories):
        missing = sorted(set(expected_directories) - set(directories))
        extra = sorted(set(directories) - set(expected_directories))
        if missing:
            raise W3DInputStageError(
                "effective-assets tree is missing declared directories: "
                + ", ".join(expected_directories[key] for key in missing[:5])
            )
        raise W3DInputStageError(
            "effective-assets tree contains undeclared directories: "
            + ", ".join(directories[key] for key in extra[:5])
        )
    for item in manifest.files:
        actual = files[item.path.casefold()]
        if PurePosixPath(actual.relative_path).name != PurePosixPath(item.path).name:
            raise W3DInputStageError(
                "effective-assets filename casing disagrees with its manifest: "
                f"{item.path!r}"
            )
        if actual.size != item.size:
            raise W3DInputStageError(
                f"effective-assets file size does not match its manifest: {item.path!r}"
            )
    return files


def _selected_limit(value: int | None, hard_limit: int, *, label: str) -> int:
    selected = hard_limit if value is None else value
    if isinstance(selected, bool) or not isinstance(selected, int):
        raise TypeError(f"W3D input stage {label} limit must be an integer")
    if not 1 <= selected <= hard_limit:
        raise ValueError(f"W3D input stage {label} limit must be 1..{hard_limit}")
    return selected


def _canonical_stage_directories(files: Iterable[_ManifestFile]) -> dict[str, str]:
    """Choose one deterministic casing for each logical source directory.

    SAGE archives are case-insensitive and retail manifests can spell the same
    directory with different casing across otherwise distinct file paths.  A
    Windows effective-assets tree necessarily materializes one such spelling.
    Treating those aliases as separate directories would either reject the
    established corpus or split hierarchy neighbours on a case-sensitive host.
    The lexicographically first manifest spelling gives every logical
    directory one stable, host-independent output path.  Full file collisions
    remain forbidden by :func:`_validate_manifest_paths`.
    """

    candidates: dict[str, set[str]] = {}
    for item in files:
        parts = PurePosixPath(item.path).parts
        for index in range(1, len(parts)):
            relative = "/".join(parts[:index])
            candidates.setdefault(relative.casefold(), set()).add(relative)
    return {key: min(values) for key, values in candidates.items()}


def _canonical_staged_path(source_path: str, directories: Mapping[str, str]) -> str:
    parts = PurePosixPath(source_path).parts
    if len(parts) == 1:
        return source_path
    parent_key = "/".join(parts[:-1]).casefold()
    parent = directories[parent_key]
    return f"{parent}/{parts[-1]}"


def _make_spec(
    manifest: _Manifest, *, max_files: int, max_total_bytes: int
) -> _StageSpec:
    selected = tuple(item for item in manifest.files if item.is_w3d)
    if not selected:
        raise W3DInputStageError(
            "effective-assets manifest declares no W3D files to stage"
        )
    total_bytes = sum(item.size for item in selected)
    if len(selected) > max_files:
        raise W3DInputStageLimitError(
            f"W3D input stage selects {len(selected)} files; limit is {max_files}"
        )
    if total_bytes > max_total_bytes:
        raise W3DInputStageLimitError(
            f"W3D input stage selects {total_bytes} bytes; limit is {max_total_bytes}"
        )
    stage_directories = _canonical_stage_directories(manifest.files)
    files = tuple(
        W3DInputStageFile(
            source_path=item.path,
            staged_path=_canonical_staged_path(item.path, stage_directories),
            source_bytes=item.size,
            source_sha256=item.sha256,
        )
        for item in selected
    )
    mappings = [item.neutral() for item in files]
    inventory_sha256 = _canonical_sha256(
        {
            "schema": "openbfme.w3d-input-stage-inventory",
            "schemaVersion": 0,
            "files": mappings,
        }
    )
    source_rows = [
        {
            "path": item.source_path,
            "size": item.source_bytes,
            "sha256": item.source_sha256,
        }
        for item in files
    ]
    output_rows = [
        {
            "path": item.staged_path,
            "size": item.source_bytes,
            "sha256": item.source_sha256,
        }
        for item in files
    ]
    selected_root_sha256 = _domain_inventory_sha256(
        "openbfme.w3d-input-stage-selected-root-v0", source_rows
    )
    output_tree_sha256 = _domain_inventory_sha256(
        "openbfme.w3d-input-stage-output-tree-v0", output_rows
    )
    limits = {
        "hardMaxFiles": MAX_W3D_STAGE_FILES,
        "hardMaxTotalBytes": MAX_W3D_STAGE_BYTES,
        "maxFiles": max_files,
        "maxTotalBytes": max_total_bytes,
    }
    source = {
        "manifestAggregateSha256": manifest.aggregate_sha256,
        "manifestFileCount": len(manifest.files),
        "manifestSha256": manifest.sha256,
        "manifestTotalBytes": manifest.total_bytes,
    }
    selection = {
        "caseInsensitiveSuffix": ".w3d",
        "inventorySha256": inventory_sha256,
        "rootSha256": selected_root_sha256,
    }
    summary = {"bytes": total_bytes, "files": len(files)}
    request_basis = {
        "schema": "openbfme.w3d-input-stage-request",
        "schemaVersion": 0,
        "source": source,
        "selection": selection,
        "summary": summary,
        "limits": limits,
    }
    request_sha256 = _canonical_sha256(request_basis)
    basis: dict[str, object] = {
        "schema": W3D_INPUT_STAGE_SCHEMA,
        "schemaVersion": W3D_INPUT_STAGE_SCHEMA_VERSION,
        "source": source,
        "selection": selection,
        "limits": limits,
        "summary": summary,
        "files": mappings,
        "outputTreeSha256": output_tree_sha256,
        "requestSha256": request_sha256,
    }
    identity_sha256 = _canonical_sha256(basis)
    document = {**basis, "identitySha256": identity_sha256}
    return _StageSpec(
        files=files,
        selected_inventory_sha256=inventory_sha256,
        selected_root_sha256=selected_root_sha256,
        output_tree_sha256=output_tree_sha256,
        request_sha256=request_sha256,
        identity_sha256=identity_sha256,
        document=document,
        canonical_manifest=_canonical_json_bytes(document),
        max_files=max_files,
        max_total_bytes=max_total_bytes,
    )


def _copy_or_hash_verified(
    actual: _TreeFile,
    expected: _ManifestFile | W3DInputStageFile,
    *,
    target: Path | None,
    label: str,
) -> None:
    expected_path = (
        expected.path if isinstance(expected, _ManifestFile) else expected.source_path
    )
    expected_size = (
        expected.size if isinstance(expected, _ManifestFile) else expected.source_bytes
    )
    expected_sha256 = (
        expected.sha256
        if isinstance(expected, _ManifestFile)
        else expected.source_sha256
    )
    output = None
    copied = 0
    digest = hashlib.sha256()
    try:
        if _is_link_like(actual.path):
            raise W3DInputStageError(
                f"{label} became linked before read: {expected_path!r}"
            )
        before = actual.path.stat()
        if before.st_nlink != 1 or not stat.S_ISREG(before.st_mode):
            raise W3DInputStageError(
                f"{label} is not an independent ordinary file: {expected_path!r}"
            )
        if _stat_identity(before) != actual.identity:
            raise W3DInputStageError(f"{label} changed before read: {expected_path!r}")
        if target is not None:
            target.parent.mkdir(parents=True, exist_ok=True)
            output = target.open("xb")
        with actual.path.open("rb") as source:
            opened = os.fstat(source.fileno())
            if _stat_identity(opened) != _stat_identity(before):
                raise W3DInputStageError(
                    f"{label} changed while opening: {expected_path!r}"
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
    except W3DInputStageError:
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
        raise W3DInputStageError(f"{label} cannot be read: {expected_path!r}") from exc
    if (
        copied != expected_size
        or _stat_identity(before) != _stat_identity(after)
        or digest.hexdigest() != expected_sha256
    ):
        if target is not None:
            target.unlink(missing_ok=True)
        raise W3DInputStageError(
            f"{label} size or SHA-256 does not match its evidence: {expected_path!r}"
        )
    if target is not None:
        try:
            target_stat = target.stat()
        except OSError as exc:
            raise W3DInputStageError(
                f"staged W3D cannot be inspected: {expected_path!r}"
            ) from exc
        if (
            target_stat.st_nlink != 1
            or not stat.S_ISREG(target_stat.st_mode)
            or target_stat.st_size != expected_size
        ):
            raise W3DInputStageError(
                f"staged W3D is not an independent ordinary file: {expected_path!r}"
            )


def _verify_and_optionally_copy_sources(
    manifest: _Manifest,
    tree: Mapping[str, _TreeFile],
    spec: _StageSpec,
    *,
    stage: Path | None,
) -> None:
    selected = {item.source_path: item for item in spec.files}
    for item in manifest.files:
        target = None
        if stage is not None and item.is_w3d:
            target = stage.joinpath(
                *PurePosixPath(selected[item.path].staged_path).parts
            )
        _copy_or_hash_verified(
            tree[item.path.casefold()],
            item,
            target=target,
            label="effective-assets file",
        )


def _revalidate_source(
    root: Path, manifest: _Manifest, snapshot: Mapping[str, _TreeFile]
) -> None:
    current_manifest = _load_manifest(root)
    if current_manifest.raw != manifest.raw:
        raise W3DInputStageError(
            "effective-assets manifest changed during W3D input staging"
        )
    current = _validate_source_tree(root, current_manifest)
    if set(current) != set(snapshot):
        raise W3DInputStageError(
            "effective-assets tree changed during W3D input staging"
        )
    for key, prior in snapshot.items():
        if current[key].identity != prior.identity:
            raise W3DInputStageError(
                "effective-assets file changed during W3D input staging: "
                f"{prior.relative_path!r}"
            )


def _verify_output(
    root: Path,
    manifest: _Manifest,
    spec: _StageSpec,
    *,
    reused: bool,
) -> W3DInputStageReport:
    manifest_path = root.joinpath(*PurePosixPath(W3D_INPUT_STAGE_MANIFEST).parts)
    document, raw = _read_strict_json(manifest_path, label="W3D input stage manifest")
    if raw != spec.canonical_manifest or document != spec.document:
        raise W3DInputStageError(
            "W3D input stage manifest does not match the verified request"
        )
    files, directories = _scan_tree(root, label="W3D input stage tree")
    declared = [W3D_INPUT_STAGE_MANIFEST, *(item.staged_path for item in spec.files)]
    expected_files = {path.casefold(): path for path in declared}
    expected_directories = _expected_directories(declared)
    if set(files) != set(expected_files):
        raise W3DInputStageError(
            "W3D input stage tree does not contain the exact declared files"
        )
    if set(directories) != set(expected_directories):
        raise W3DInputStageError(
            "W3D input stage tree does not contain the exact declared directories"
        )
    output_rows: list[dict[str, object]] = []
    for item in spec.files:
        actual = files[item.staged_path.casefold()]
        if actual.relative_path != item.staged_path:
            raise W3DInputStageError(f"staged W3D casing changed: {item.staged_path!r}")
        _copy_or_hash_verified(
            actual,
            item,
            target=None,
            label="staged W3D",
        )
        output_rows.append(
            {
                "path": actual.relative_path,
                "size": actual.size,
                "sha256": item.source_sha256,
            }
        )
    output_tree_sha256 = _domain_inventory_sha256(
        "openbfme.w3d-input-stage-output-tree-v0", output_rows
    )
    if output_tree_sha256 != spec.output_tree_sha256:
        raise W3DInputStageError("W3D input stage output-tree SHA-256 changed")
    return W3DInputStageReport(
        source_root=manifest.path.parent.parent,
        output_root=root,
        manifest_path=manifest_path,
        source_manifest_sha256=manifest.sha256,
        source_manifest_aggregate_sha256=manifest.aggregate_sha256,
        source_manifest_file_count=len(manifest.files),
        source_manifest_total_bytes=manifest.total_bytes,
        files=spec.files,
        selected_inventory_sha256=spec.selected_inventory_sha256,
        selected_root_sha256=spec.selected_root_sha256,
        output_tree_sha256=spec.output_tree_sha256,
        request_sha256=spec.request_sha256,
        identity_sha256=spec.identity_sha256,
        manifest_sha256=hashlib.sha256(raw).hexdigest(),
        max_files=spec.max_files,
        max_total_bytes=spec.max_total_bytes,
        reused=reused,
    )


def _remove_owned_tree(path: Path, parent: Path, prefix: str) -> None:
    if not os.path.lexists(path):
        return
    if path.parent != parent or not path.name.startswith(prefix) or _is_link_like(path):
        raise W3DInputStageError(
            "refused to remove an unowned W3D input-stage transaction path"
        )
    _scan_tree(path, label="W3D input-stage transaction tree")
    shutil.rmtree(path)


def _publish_stage(
    stage: Path,
    destination: Path,
    backup: Path,
    manifest: _Manifest,
    spec: _StageSpec,
) -> W3DInputStageReport:
    parent = destination.parent
    had_destination = os.path.lexists(destination)
    if had_destination:
        _scan_tree(destination, label="existing W3D input stage tree")
        try:
            os.replace(destination, backup)
        except OSError as exc:
            raise W3DInputStageError(
                "existing W3D input stage could not enter the transaction"
            ) from exc
    try:
        os.replace(stage, destination)
        report = _verify_output(destination, manifest, spec, reused=False)
    except Exception as publish_error:
        rollback_error: Exception | None = None
        try:
            if os.path.lexists(destination):
                os.replace(destination, stage)
            if had_destination and os.path.lexists(backup):
                os.replace(backup, destination)
        except Exception as exc:  # pragma: no cover - catastrophic filesystem fault
            rollback_error = exc
        if rollback_error is not None:
            raise W3DInputStageError(
                "W3D input-stage publish failed and rollback could not restore the prior output"
            ) from rollback_error
        raise W3DInputStageError(
            "W3D input-stage publish failed; prior output was preserved"
        ) from publish_error
    if had_destination and os.path.lexists(backup):
        try:
            _remove_owned_tree(backup, parent, f".{destination.name}.backup-")
        except (OSError, W3DInputStageError):
            # Publication is complete and verified.  A uniquely named old
            # backup may remain for manual cleanup without weakening output.
            pass
    return report


def build_w3d_input_stage(
    effective_assets_root: Path | str,
    output_root: Path | str,
    *,
    max_files: int | None = None,
    max_total_bytes: int | None = None,
    force: bool = False,
) -> W3DInputStageReport:
    """Verify and transactionally stage every manifest-declared W3D winner.

    The complete effective-assets tree is checked against its canonical
    manifest before any output is published; non-W3D payloads are hashed but
    not copied.  Selection is case-insensitive by suffix and is never
    truncated.  Configurable limits may only lower the hard bounds.

    A matching existing output is fully backtested and returned unchanged with
    ``reused=True``.  ``force=True`` rebuilds through a sibling staging tree;
    an existing ordinary output remains installed until the replacement has
    been fully staged and verified, and is restored on publication failure.

    This byte stage proves neither W3D semantics nor GLB conversion.  It also
    excludes textures; texture discovery/material resolution needs a separate
    verified input rooted alongside the downstream importer workflow.
    """

    if not isinstance(force, bool):
        raise TypeError("W3D input stage force flag must be a boolean")
    selected_max_files = _selected_limit(
        max_files, MAX_W3D_STAGE_FILES, label="file count"
    )
    selected_max_bytes = _selected_limit(
        max_total_bytes, MAX_W3D_STAGE_BYTES, label="total byte"
    )
    source = _resolve_source_root(effective_assets_root)
    destination = _resolve_output_root(output_root, source)
    manifest = _load_manifest(source)
    tree = _validate_source_tree(source, manifest)
    spec = _make_spec(
        manifest,
        max_files=selected_max_files,
        max_total_bytes=selected_max_bytes,
    )

    if destination.is_dir() and not force:
        try:
            # Reuse still verifies every manifest-declared byte, including
            # non-W3D payloads that are not present in the stage.
            _verify_and_optionally_copy_sources(manifest, tree, spec, stage=None)
            _revalidate_source(source, manifest, tree)
            report = _verify_output(destination, manifest, spec, reused=True)
            _revalidate_source(source, manifest, tree)
            return report
        except W3DInputStageError as exc:
            raise W3DInputStageReuseError(
                f"existing W3D input stage failed verification: {exc}"
            ) from exc

    parent = destination.parent
    token = uuid.uuid4().hex
    stage = parent / f".{destination.name}.staging-{token}"
    backup = parent / f".{destination.name}.backup-{token}"
    try:
        stage.mkdir()
    except OSError as exc:
        raise W3DInputStageError(
            "W3D input-stage staging directory could not be created"
        ) from exc
    try:
        _verify_and_optionally_copy_sources(manifest, tree, spec, stage=stage)
        metadata = stage / _METADATA_DIRECTORY
        metadata.mkdir()
        manifest_path = stage.joinpath(*PurePosixPath(W3D_INPUT_STAGE_MANIFEST).parts)
        try:
            with manifest_path.open("xb") as stream:
                stream.write(spec.canonical_manifest)
        except OSError as exc:
            raise W3DInputStageError(
                "W3D input stage manifest could not be written"
            ) from exc
        _revalidate_source(source, manifest, tree)
        _verify_output(stage, manifest, spec, reused=False)
        report = _publish_stage(stage, destination, backup, manifest, spec)
        _revalidate_source(source, manifest, tree)
        return report
    finally:
        if os.path.lexists(stage):
            _remove_owned_tree(stage, parent, f".{destination.name}.staging-")
        if os.path.lexists(backup) and not os.path.lexists(destination):
            try:
                os.replace(backup, destination)
            except OSError as exc:
                raise W3DInputStageError(
                    "W3D input-stage cleanup could not restore the prior output"
                ) from exc


# Concise pipeline-style alias.
build_input_stage = build_w3d_input_stage


__all__ = [
    "EFFECTIVE_ASSET_MANIFEST_RELATIVE",
    "EFFECTIVE_ASSET_MANIFEST_SCHEMA",
    "EFFECTIVE_ASSET_MANIFEST_VERSION",
    "MAX_EFFECTIVE_ASSET_BYTES",
    "MAX_EFFECTIVE_ASSET_FILES",
    "MAX_MANIFEST_BYTES",
    "MAX_W3D_STAGE_BYTES",
    "MAX_W3D_STAGE_FILES",
    "W3D_INPUT_STAGE_MANIFEST",
    "W3D_INPUT_STAGE_SCHEMA",
    "W3D_INPUT_STAGE_SCHEMA_VERSION",
    "W3DInputStageError",
    "W3DInputStageFile",
    "W3DInputStageLimitError",
    "W3DInputStageReport",
    "W3DInputStageReuseError",
    "build_input_stage",
    "build_w3d_input_stage",
]
