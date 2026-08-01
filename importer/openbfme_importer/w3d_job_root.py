"""Transactional materialization of sealed W3D Blender job roots.

This boundary joins a verified :mod:`w3d_input_stage` with a complete
:mod:`w3d_texture_closure` plan.  It copies every staged W3D and the exact
native PNG closure into one ordinary-file tree for the pinned Blender runner.
It proves byte identity and native PNG validity only; it does not claim that
Blender can convert a W3D, that a GLB is correct, or that a render is faithful.
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

from .native_backtest import validate_native_output
from .paths import ensure_external_to_repo, repo_root_from_module, safe_relative_parts
from .w3d_catalog import W3DCatalogSource, catalog_source_id
from .w3d_input_stage import (
    EFFECTIVE_ASSET_MANIFEST_RELATIVE,
    MAX_W3D_STAGE_BYTES,
    MAX_W3D_STAGE_FILES,
    W3D_INPUT_STAGE_MANIFEST,
    W3D_INPUT_STAGE_SCHEMA,
    W3D_INPUT_STAGE_SCHEMA_VERSION,
    W3DInputStageFile,
    W3DInputStageReport,
)
from .w3d_job_planner import (
    MAX_BATCH_JOBS,
    MAX_BATCH_MANIFEST_BYTES,
    PRESENTATION_POLICY_PRESERVE_ALL,
    PRESENTATION_POLICY_STRICT,
    SECONDARY_SKIN_PREPARATION,
    W3DJobBatch,
    W3DJobPlan,
    W3DPlannedJob,
    W3DTerminal,
    w3d_job_is_exact_embedded_model_animation,
    w3d_job_resolution_contract_is_valid,
)
from .w3d_job_preparation import (
    W3DJobPreparationError,
    W3DJobPreparationFixedPointReport,
    W3DJobPreparationPreflightReport,
    merge_w3d_preparation_forced_terminals,
    validate_w3d_job_preparation_fixed_point,
)
from .w3d_texture_closure import (
    NATIVE_TEXTURE_MANIFEST_SCHEMA,
    NATIVE_TEXTURE_MANIFEST_VERSION,
    W3DModelTextureClosure,
    W3DTextureClosurePlan,
    W3DTextureCopyInstruction,
    texture_closure_forced_terminals,
)


W3D_JOB_ROOT_SCHEMA = "openbfme.w3d-job-root"
W3D_JOB_ROOT_SCHEMA_VERSION = 0
W3D_JOB_ROOT_MANIFEST = ".openbfme/w3d-job-root.json"

MAX_DOCUMENT_BYTES = 64 * 1024 * 1024
MAX_W3D_JOB_ROOT_FILES = 100_000
MAX_W3D_JOB_ROOT_BYTES = 16 * 1024 * 1024 * 1024
HASH_BLOCK_BYTES = 1024 * 1024

_METADATA_DIRECTORY = ".openbfme"
_SHA256_CHARACTERS = frozenset("0123456789abcdef")
_WINDOWS_FORBIDDEN_FILENAME_CHARACTERS = frozenset('<>"|?*')
_TEXTURE_EXTENSIONS = frozenset(
    {".bmp", ".dds", ".jpeg", ".jpg", ".pcx", ".png", ".tga"}
)
_ASSET_KINDS = frozenset({"animated", "hierarchical", "static"})
_PRESENTATION_POLICIES = frozenset(
    {PRESENTATION_POLICY_STRICT, PRESENTATION_POLICY_PRESERVE_ALL}
)
_ACCOUNTED_MATERIALIZATION_POLICY = "accounted-planned-jobs-v1"


class W3DJobRootError(ValueError):
    """Base class for an incoherent source, plan, or transaction."""


class W3DJobRootLimitError(W3DJobRootError):
    """Raised before publication when a conservative bound is exceeded."""


class W3DJobRootReuseError(W3DJobRootError):
    """Raised when an existing destination does not exactly match the request."""


@dataclass(frozen=True, slots=True)
class W3DJobRootFile:
    """One exact payload copy in a Blender job root."""

    kind: str
    source_path: str
    destination_path: str
    byte_length: int
    sha256: str
    instruction_id: str | None = None

    def private(self) -> dict[str, object]:
        result: dict[str, object] = {
            "kind": self.kind,
            "sourcePath": self.source_path,
            "destinationPath": self.destination_path,
            "bytes": self.byte_length,
            "sha256": self.sha256,
        }
        if self.instruction_id is not None:
            result["instructionId"] = self.instruction_id
        return result


@dataclass(frozen=True, slots=True)
class W3DJobRootReport:
    """Verified job-root publication evidence."""

    input_stage_root: Path
    native_texture_corpus_root: Path
    output_root: Path
    manifest_path: Path
    files: tuple[W3DJobRootFile, ...]
    stage_identity_sha256: str
    stage_manifest_sha256: str
    texture_closure_private_plan_sha256: str
    texture_closure_evidence_sha256: str
    native_manifest_sha256: str
    native_texture_identity_sha256: str
    inventory_sha256: str
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
        return sum(item.byte_length for item in self.files)

    @property
    def w3d_file_count(self) -> int:
        return sum(item.kind == "w3d" for item in self.files)

    @property
    def texture_file_count(self) -> int:
        return sum(item.kind == "texture-png" for item in self.files)

    def neutral(self) -> dict[str, object]:
        return {
            "schema": "openbfme.w3d-job-root-report",
            "schemaVersion": 0,
            "summary": {
                "fileCount": self.file_count,
                "totalBytes": self.total_bytes,
                "w3dFileCount": self.w3d_file_count,
                "textureFileCount": self.texture_file_count,
                "glbConversionComplete": False,
                "renderParityProven": False,
            },
            "hashes": {
                "stageIdentitySha256": self.stage_identity_sha256,
                "stageManifestSha256": self.stage_manifest_sha256,
                "textureClosurePrivatePlanSha256": (
                    self.texture_closure_private_plan_sha256
                ),
                "textureClosureEvidenceSha256": (self.texture_closure_evidence_sha256),
                "nativeManifestSha256": self.native_manifest_sha256,
                "nativeTextureIdentitySha256": self.native_texture_identity_sha256,
                "inventorySha256": self.inventory_sha256,
                "outputTreeSha256": self.output_tree_sha256,
                "requestSha256": self.request_sha256,
                "identitySha256": self.identity_sha256,
                "manifestSha256": self.manifest_sha256,
            },
            "limits": {
                "maxFiles": self.max_files,
                "maxTotalBytes": self.max_total_bytes,
            },
            "reused": self.reused,
        }

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class _TreeFile:
    relative_path: str
    path: Path
    size: int
    identity: tuple[int, int, int, int, int, int]


@dataclass(frozen=True, slots=True)
class _EffectiveFile:
    path: str
    archive: str
    size: int
    sha256: str


@dataclass(frozen=True, slots=True)
class _EffectiveManifest:
    root: Path
    path: Path
    raw: bytes
    identity: tuple[int, int, int, int, int, int]
    sha256: str
    aggregate_sha256: str
    files: tuple[_EffectiveFile, ...]


@dataclass(frozen=True, slots=True)
class _NativeOutput:
    path: str
    size: int
    sha256: str
    evidence: Mapping[str, object]


@dataclass(frozen=True, slots=True)
class _NativeManifest:
    path: Path
    raw: bytes
    identity: tuple[int, int, int, int, int, int]
    sha256: str
    corpus_identity_sha256: str
    outputs: Mapping[str, _NativeOutput]


@dataclass(frozen=True, slots=True)
class _SourceSnapshot:
    effective_files: Mapping[str, _TreeFile]
    effective_directories: Mapping[str, str]
    stage_files: Mapping[str, _TreeFile]
    stage_directories: Mapping[str, str]
    native_files: Mapping[str, _TreeFile]
    native_directories: Mapping[str, str]
    effective_manifest: _EffectiveManifest
    native_manifest: _NativeManifest


@dataclass(frozen=True, slots=True)
class _JobSpec:
    files: tuple[W3DJobRootFile, ...]
    inventory_sha256: str
    output_tree_sha256: str
    request_sha256: str
    identity_sha256: str
    document: Mapping[str, object]
    canonical_manifest: bytes
    max_files: int
    max_total_bytes: int


@dataclass(frozen=True, slots=True)
class _PayloadSelection:
    stage_files: tuple[W3DInputStageFile, ...]
    copy_instructions: tuple[W3DTextureCopyInstruction, ...]
    job_plan: W3DJobPlan | None
    materialize_accounted_jobs: bool
    preparation_fixed_point_evidence_sha256: str | None
    preparation_fixed_point_iteration_count: int | None


class _DuplicateJsonKey(ValueError):
    pass


def _canonical_json_bytes(
    value: object,
    *,
    pretty: bool = False,
    ensure_ascii: bool = True,
    newline: bool = True,
) -> bytes:
    options: dict[str, object] = {
        "allow_nan": False,
        "ensure_ascii": ensure_ascii,
        "sort_keys": True,
    }
    if pretty:
        options["indent"] = 2
    else:
        options["separators"] = (",", ":")
    suffix = "\n" if newline else ""
    return (json.dumps(value, **options) + suffix).encode("utf-8")


def _canonical_sha256(value: object, *, newline: bool = True) -> str:
    return hashlib.sha256(_canonical_json_bytes(value, newline=newline)).hexdigest()


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and value == value.casefold()
        and all(character in _SHA256_CHARACTERS for character in value)
    )


def _is_int(value: object, *, minimum: int = 0) -> bool:
    return not isinstance(value, bool) and isinstance(value, int) and value >= minimum


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
            raise W3DJobRootError(f"{context} link state cannot be inspected") from exc
        if linked:
            raise W3DJobRootError(f"{context} is linked: {current}")


def _paths_overlap(first: Path, second: Path) -> bool:
    try:
        common = os.path.commonpath(
            [os.path.normcase(str(first)), os.path.normcase(str(second))]
        )
    except ValueError:
        return False
    return common in {os.path.normcase(str(first)), os.path.normcase(str(second))}


def _resolve_source_root(value: Path | str, *, label: str) -> Path:
    try:
        candidate = Path(value)
    except TypeError as exc:
        raise TypeError(f"{label} must be a filesystem path") from exc
    _refuse_link_chain(candidate, context=label)
    try:
        root = candidate.expanduser().resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise W3DJobRootError(f"{label} is unavailable") from exc
    if not root.is_dir() or _is_link_like(root):
        raise W3DJobRootError(f"{label} is not an unlinked directory")
    return root


def _resolve_output_root(value: Path | str, sources: Iterable[Path]) -> Path:
    try:
        candidate = Path(value).expanduser()
    except TypeError as exc:
        raise TypeError("W3D job root must be a filesystem path") from exc
    absolute = Path(os.path.abspath(candidate))
    if not absolute.name:
        raise W3DJobRootError("W3D job root cannot be a filesystem anchor")
    try:
        ensure_external_to_repo(absolute, repo_root_from_module())
    except ValueError as exc:
        raise W3DJobRootError(str(exc)) from exc
    for source in sources:
        if _paths_overlap(source, absolute):
            raise W3DJobRootError("W3D job root must not overlap a source root")
    _refuse_link_chain(absolute.parent, context="W3D job-root parent")
    try:
        absolute.parent.mkdir(parents=True, exist_ok=True)
        _refuse_link_chain(absolute.parent, context="W3D job-root parent")
        parent = absolute.parent.resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise W3DJobRootError("W3D job-root parent is unavailable") from exc
    output = parent / absolute.name
    for source in sources:
        if _paths_overlap(source, output):
            raise W3DJobRootError("W3D job root must not overlap a source root")
    if os.path.lexists(output):
        if _is_link_like(output) or not output.is_dir():
            raise W3DJobRootError("W3D job root is linked or not a directory")
    return output


def _safe_path(value: object, *, label: str, suffix: str | None = None) -> str:
    if not isinstance(value, str) or len(value) > 512 or "\\" in value:
        raise W3DJobRootError(f"{label} path is invalid")
    try:
        parts = safe_relative_parts(value)
    except (TypeError, ValueError) as exc:
        raise W3DJobRootError(f"{label} path is unsafe") from exc
    if "/".join(parts) != value or any(
        character in _WINDOWS_FORBIDDEN_FILENAME_CHARACTERS
        for part in parts
        for character in part
    ):
        raise W3DJobRootError(f"{label} path is not canonical")
    if suffix is not None and PurePosixPath(value).suffix.casefold() != suffix:
        raise W3DJobRootError(f"{label} path has the wrong suffix")
    return value


def _validate_path_inventory(paths: Iterable[str], *, label: str) -> None:
    selected = list(paths)
    if len({path.casefold() for path in selected}) != len(selected):
        raise W3DJobRootError(f"{label} paths case-collide")
    files = {path.casefold() for path in selected}
    directories: dict[str, str] = {}
    for path in selected:
        parts = path.split("/")
        for index in range(1, len(parts)):
            directory = "/".join(parts[:index])
            key = directory.casefold()
            prior = directories.get(key)
            if prior is not None and prior != directory:
                raise W3DJobRootError(f"{label} directory casing collides")
            directories[key] = directory
            if key in files:
                raise W3DJobRootError(f"{label} has a file/directory collision")


def _validate_source_path_inventory(
    paths: Iterable[str], *, label: str = "effective-assets"
) -> None:
    """Validate sealed source paths while tolerating directory-only aliases.

    Retail archives are case-insensitive and can author two distinct files
    beneath differently-cased spellings of the same logical directory.  This
    exception belongs only to source inventories that are joined exactly back
    to the sealed effective-assets manifest.  Job-root inputs and outputs
    continue to use :func:`_validate_path_inventory`.
    """

    selected = list(paths)
    files = {path.casefold() for path in selected}
    if len(files) != len(selected):
        raise W3DJobRootError(f"{label} paths case-collide")
    for path in selected:
        parts = path.split("/")
        for index in range(1, len(parts)):
            if "/".join(parts[:index]).casefold() in files:
                raise W3DJobRootError(f"{label} has a file/directory collision")


def _selected_limit(value: int | None, hard_limit: int, *, label: str) -> int:
    selected = hard_limit if value is None else value
    if isinstance(selected, bool) or not isinstance(selected, int):
        raise TypeError(f"W3D job-root {label} limit must be an integer")
    if not 1 <= selected <= hard_limit:
        raise ValueError(f"W3D job-root {label} limit must be 1..{hard_limit}")
    return selected


def _read_strict_json(
    path: Path, *, label: str, ensure_ascii: bool
) -> tuple[dict[str, Any], bytes, tuple[int, int, int, int, int, int]]:
    _refuse_link_chain(path, context=label)
    if not path.is_file() or _is_link_like(path):
        raise W3DJobRootError(f"{label} is missing, linked, or not a file")
    try:
        before = path.stat()
        if before.st_nlink != 1 or not stat.S_ISREG(before.st_mode):
            raise W3DJobRootError(f"{label} is not an independent ordinary file")
        if not 1 <= before.st_size <= MAX_DOCUMENT_BYTES:
            raise W3DJobRootLimitError(f"{label} exceeds its safety bound")
        raw = path.read_bytes()
        after = path.stat()
    except W3DJobRootError:
        raise
    except OSError as exc:
        raise W3DJobRootError(f"{label} cannot be read") from exc
    if len(raw) != before.st_size or _stat_identity(before) != _stat_identity(after):
        raise W3DJobRootError(f"{label} changed during read")
    try:
        document = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_object_without_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise W3DJobRootError(f"{label} is invalid JSON") from exc
    if not isinstance(document, dict):
        raise W3DJobRootError(f"{label} root is not an object")
    if raw != _canonical_json_bytes(document, pretty=True, ensure_ascii=ensure_ascii):
        raise W3DJobRootError(f"{label} encoding is not canonical")
    return document, raw, _stat_identity(after)


def _inventory_sha256(domain: str, rows: Iterable[Mapping[str, object]]) -> str:
    digest = hashlib.sha256()
    digest.update(domain.encode("ascii"))
    digest.update(b"\n")
    for row in rows:
        digest.update(str(row["path"]).encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(row["size"]).encode("ascii"))
        digest.update(b"\0")
        digest.update(str(row["sha256"]).encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _effective_aggregate(files: Iterable[_EffectiveFile]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(item.path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item.size).encode("ascii"))
        digest.update(b"\0")
        digest.update(item.sha256.encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _load_effective_manifest(report: W3DInputStageReport) -> _EffectiveManifest:
    source = _resolve_source_root(report.source_root, label="effective-assets root")
    path = source.joinpath(*PurePosixPath(EFFECTIVE_ASSET_MANIFEST_RELATIVE).parts)
    document, raw, identity = _read_strict_json(
        path, label="effective-assets manifest", ensure_ascii=False
    )
    if hashlib.sha256(raw).hexdigest() != report.source_manifest_sha256:
        raise W3DJobRootError(
            "effective-assets manifest seal mismatches the stage report"
        )
    if (
        set(document)
        != {
            "aggregate_sha256",
            "catalog",
            "files",
            "install",
            "schema",
            "schema_version",
            "totals",
        }
        or document.get("schema") != "openbfme.effective-assets-manifest"
    ):
        raise W3DJobRootError("effective-assets manifest shape is invalid")
    if document.get("schema_version") != 0 or isinstance(
        document.get("schema_version"), bool
    ):
        raise W3DJobRootError("effective-assets manifest version is unsupported")
    raw_files = document.get("files")
    if not isinstance(raw_files, list) or len(raw_files) > 250_000:
        raise W3DJobRootError("effective-assets inventory is invalid")
    files: list[_EffectiveFile] = []
    for index, raw_file in enumerate(raw_files):
        if not isinstance(raw_file, dict) or set(raw_file) != {
            "archive",
            "offset",
            "path",
            "precedence",
            "sha256",
            "size",
        }:
            raise W3DJobRootError(f"effective-assets file {index} shape is invalid")
        path_value = _safe_path(raw_file.get("path"), label="effective-assets file")
        archive = _safe_path(raw_file.get("archive"), label="effective-assets archive")
        size = raw_file.get("size")
        digest = raw_file.get("sha256")
        if (
            path_value.split("/", 1)[0].casefold() == _METADATA_DIRECTORY.casefold()
            or not _is_int(raw_file.get("offset"))
            or not _is_int(raw_file.get("precedence"))
            or not _is_int(size)
            or not _is_sha256(digest)
        ):
            raise W3DJobRootError(f"effective-assets file {index} metadata is invalid")
        files.append(_EffectiveFile(path_value, archive, int(size), str(digest)))
    paths = [item.path for item in files]
    if paths != sorted(paths, key=lambda value: (value.casefold(), value)):
        raise W3DJobRootError("effective-assets inventory is not canonical")
    _validate_source_path_inventory(paths)
    totals = document.get("totals")
    total_bytes = sum(item.size for item in files)
    if totals != {"bytes": total_bytes, "files": len(files)}:
        raise W3DJobRootError("effective-assets totals are invalid")
    aggregate = _effective_aggregate(files)
    if (
        document.get("aggregate_sha256") != aggregate
        or aggregate != report.source_manifest_aggregate_sha256
        or len(files) != report.source_manifest_file_count
        or total_bytes != report.source_manifest_total_bytes
    ):
        raise W3DJobRootError("effective-assets evidence mismatches the stage report")
    return _EffectiveManifest(
        source,
        path,
        raw,
        identity,
        hashlib.sha256(raw).hexdigest(),
        aggregate,
        tuple(files),
    )


def _canonical_stage_directories(files: Iterable[_EffectiveFile]) -> dict[str, str]:
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
    return f"{directories['/'.join(parts[:-1]).casefold()]}/{parts[-1]}"


def _validate_stage_report(
    report: W3DInputStageReport,
    stage_root: Path,
    effective: _EffectiveManifest,
) -> None:
    if not isinstance(report, W3DInputStageReport):
        raise TypeError("W3D job root requires a W3DInputStageReport")
    if type(report.files) is not tuple or not isinstance(report.reused, bool):
        raise W3DJobRootError("W3D input-stage report is not immutable")
    try:
        reported_root = Path(report.output_root).resolve(strict=True)
        reported_manifest = Path(report.manifest_path).resolve(strict=True)
    except (OSError, RuntimeError, TypeError) as exc:
        raise W3DJobRootError("W3D input-stage report paths are unavailable") from exc
    expected_manifest = stage_root.joinpath(
        *PurePosixPath(W3D_INPUT_STAGE_MANIFEST).parts
    )
    if reported_root != stage_root or reported_manifest != expected_manifest:
        raise W3DJobRootError("W3D input-stage report root binding mismatches")
    for digest in (
        report.source_manifest_sha256,
        report.source_manifest_aggregate_sha256,
        report.selected_inventory_sha256,
        report.selected_root_sha256,
        report.output_tree_sha256,
        report.request_sha256,
        report.identity_sha256,
        report.manifest_sha256,
    ):
        if not _is_sha256(digest):
            raise W3DJobRootError("W3D input-stage report contains an invalid hash")
    if (
        not _is_int(report.max_files, minimum=1)
        or report.max_files > MAX_W3D_STAGE_FILES
        or not _is_int(report.max_total_bytes, minimum=1)
        or report.max_total_bytes > MAX_W3D_STAGE_BYTES
    ):
        raise W3DJobRootError("W3D input-stage report limits are invalid")
    selected = tuple(
        item
        for item in effective.files
        if PurePosixPath(item.path).suffix.casefold() == ".w3d"
    )
    directories = _canonical_stage_directories(effective.files)
    expected_files = tuple(
        W3DInputStageFile(
            source_path=item.path,
            staged_path=_canonical_staged_path(item.path, directories),
            source_bytes=item.size,
            source_sha256=item.sha256,
        )
        for item in selected
    )
    if report.files != expected_files or not expected_files:
        raise W3DJobRootError("W3D input-stage report inventory mismatches its source")
    if (
        len(expected_files) > report.max_files
        or sum(item.source_bytes for item in expected_files) > report.max_total_bytes
    ):
        raise W3DJobRootError("W3D input-stage report violates its own limits")
    mappings = [item.neutral() for item in expected_files]
    inventory = _canonical_sha256(
        {
            "schema": "openbfme.w3d-input-stage-inventory",
            "schemaVersion": 0,
            "files": mappings,
        },
        newline=False,
    )
    source_rows = [
        {
            "path": item.source_path,
            "size": item.source_bytes,
            "sha256": item.source_sha256,
        }
        for item in expected_files
    ]
    output_rows = [
        {
            "path": item.staged_path,
            "size": item.source_bytes,
            "sha256": item.source_sha256,
        }
        for item in expected_files
    ]
    selected_root = _inventory_sha256(
        "openbfme.w3d-input-stage-selected-root-v0", source_rows
    )
    output_tree = _inventory_sha256(
        "openbfme.w3d-input-stage-output-tree-v0", output_rows
    )
    limits = {
        "hardMaxFiles": MAX_W3D_STAGE_FILES,
        "hardMaxTotalBytes": MAX_W3D_STAGE_BYTES,
        "maxFiles": report.max_files,
        "maxTotalBytes": report.max_total_bytes,
    }
    source = {
        "manifestAggregateSha256": effective.aggregate_sha256,
        "manifestFileCount": len(effective.files),
        "manifestSha256": effective.sha256,
        "manifestTotalBytes": sum(item.size for item in effective.files),
    }
    selection = {
        "caseInsensitiveSuffix": ".w3d",
        "inventorySha256": inventory,
        "rootSha256": selected_root,
    }
    summary = {
        "bytes": sum(item.source_bytes for item in expected_files),
        "files": len(expected_files),
    }
    request = _canonical_sha256(
        {
            "schema": "openbfme.w3d-input-stage-request",
            "schemaVersion": 0,
            "source": source,
            "selection": selection,
            "summary": summary,
            "limits": limits,
        },
        newline=False,
    )
    basis: dict[str, object] = {
        "schema": W3D_INPUT_STAGE_SCHEMA,
        "schemaVersion": W3D_INPUT_STAGE_SCHEMA_VERSION,
        "source": source,
        "selection": selection,
        "limits": limits,
        "summary": summary,
        "files": mappings,
        "outputTreeSha256": output_tree,
        "requestSha256": request,
    }
    identity = _canonical_sha256(basis, newline=False)
    document = {**basis, "identitySha256": identity}
    raw_document, raw, _ = _read_strict_json(
        expected_manifest, label="W3D input-stage manifest", ensure_ascii=False
    )
    if raw_document != document or raw != _canonical_json_bytes(
        document, pretty=True, ensure_ascii=False
    ):
        raise W3DJobRootError("W3D input-stage manifest is incoherent")
    if (
        report.selected_inventory_sha256 != inventory
        or report.selected_root_sha256 != selected_root
        or report.output_tree_sha256 != output_tree
        or report.request_sha256 != request
        or report.identity_sha256 != identity
        or report.manifest_sha256 != hashlib.sha256(raw).hexdigest()
    ):
        raise W3DJobRootError("W3D input-stage report seal is incoherent")


def _scan_tree(
    root: Path, *, label: str
) -> tuple[dict[str, _TreeFile], dict[str, str]]:
    files: dict[str, _TreeFile] = {}
    directories: dict[str, str] = {}
    try:
        for current, directory_names, file_names in os.walk(root, followlinks=False):
            current_path = Path(current)
            relative_parent = current_path.relative_to(root)
            for name in directory_names:
                path = current_path / name
                if _is_link_like(path):
                    raise W3DJobRootError(f"{label} contains a linked directory")
                relative = (relative_parent / name).as_posix()
                key = relative.casefold()
                if key in directories or key in files:
                    raise W3DJobRootError(f"{label} contains a case collision")
                directories[key] = relative
            for name in file_names:
                path = current_path / name
                if _is_link_like(path):
                    raise W3DJobRootError(f"{label} contains a linked file")
                before = path.stat()
                if before.st_nlink != 1 or not stat.S_ISREG(before.st_mode):
                    raise W3DJobRootError(f"{label} contains a non-independent file")
                relative = (relative_parent / name).as_posix()
                _safe_path(relative, label=label)
                key = relative.casefold()
                if key in files or key in directories:
                    raise W3DJobRootError(f"{label} contains a case collision")
                files[key] = _TreeFile(
                    relative, path, int(before.st_size), _stat_identity(before)
                )
    except W3DJobRootError:
        raise
    except OSError as exc:
        raise W3DJobRootError(f"{label} cannot be scanned") from exc
    return files, directories


def _validate_effective_source_tree(
    manifest: _EffectiveManifest,
    *,
    verify_payload_hashes: bool,
) -> tuple[dict[str, _TreeFile], dict[str, str]]:
    """Join the authored manifest to one unambiguous physical source tree."""

    files, directories = _scan_tree(manifest.root, label="effective-assets source tree")
    declared_paths = [
        EFFECTIVE_ASSET_MANIFEST_RELATIVE,
        *(item.path for item in manifest.files),
    ]
    expected_files = {path.casefold(): path for path in declared_paths}
    expected_directories = {
        "/".join(parts[:index]).casefold()
        for path in declared_paths
        for parts in (PurePosixPath(path).parts,)
        for index in range(1, len(parts))
    }
    if set(files) != set(expected_files) or set(directories) != expected_directories:
        raise W3DJobRootError(
            "effective-assets source tree does not join one-to-one with its manifest"
        )
    physical_manifest = files[EFFECTIVE_ASSET_MANIFEST_RELATIVE.casefold()]
    if (
        physical_manifest.relative_path != EFFECTIVE_ASSET_MANIFEST_RELATIVE
        or physical_manifest.identity != manifest.identity
    ):
        raise W3DJobRootError(
            "effective-assets source manifest changed during physical join"
        )
    for item in manifest.files:
        actual = files[item.path.casefold()]
        if (
            PurePosixPath(actual.relative_path).name != PurePosixPath(item.path).name
            or actual.size != item.size
        ):
            raise W3DJobRootError(
                "effective-assets physical file path or size mismatches its manifest"
            )
        if verify_payload_hashes:
            _copy_verified(
                actual,
                expected_size=item.size,
                expected_sha256=item.sha256,
                target=None,
                label="effective-assets physical file",
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
            if prior is not None and prior != relative:
                raise W3DJobRootError("declared directory casing collides")
            result[key] = relative
    return result


def _validate_exact_tree(
    files: Mapping[str, _TreeFile],
    directories: Mapping[str, str],
    declared: Iterable[str],
    *,
    label: str,
) -> None:
    paths = list(declared)
    expected_files = {path.casefold(): path for path in paths}
    expected_directories = _expected_directories(paths)
    if set(files) != set(expected_files) or set(directories) != set(
        expected_directories
    ):
        raise W3DJobRootError(f"{label} does not contain the exact declared tree")
    for key, expected in expected_files.items():
        if files[key].relative_path != expected:
            raise W3DJobRootError(f"{label} path casing changed")
    for key, expected in expected_directories.items():
        if directories[key] != expected:
            raise W3DJobRootError(f"{label} directory casing changed")


def _native_request_sha256(
    source_manifest_sha256: str,
    source_aggregate_sha256: str,
    conversion: Mapping[str, object],
    sources: list[dict[str, object]],
) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.native-corpus-request",
            "schemaVersion": 0,
            "families": ["texture"],
            "sourceManifestSha256": source_manifest_sha256,
            "sourceManifestAggregateSha256": source_aggregate_sha256,
            "conversion": conversion,
            "sources": sources,
        }
    )


def _load_native_manifest(
    root: Path, effective: _EffectiveManifest, plan: W3DTextureClosurePlan
) -> _NativeManifest:
    path = root / "manifest.json"
    document, raw, identity = _read_strict_json(
        path, label="native texture corpus manifest", ensure_ascii=True
    )
    seal = hashlib.sha256(raw).hexdigest()
    if seal != plan.native_manifest_sha256:
        raise W3DJobRootError("native texture manifest seal mismatches the closure")
    if set(document) != {
        "schema",
        "schemaVersion",
        "selection",
        "totals",
        "entries",
        "reclassified",
        "outputs",
        "identitySha256",
    } or (
        document.get("schema") != NATIVE_TEXTURE_MANIFEST_SCHEMA
        or document.get("schemaVersion") != NATIVE_TEXTURE_MANIFEST_VERSION
        or isinstance(document.get("schemaVersion"), bool)
    ):
        raise W3DJobRootError("native texture manifest shape is invalid")
    selection = document.get("selection")
    if not isinstance(selection, dict) or set(selection) != {
        "families",
        "sourceManifestSha256",
        "sourceManifestAggregateSha256",
        "requestSha256",
        "conversion",
    }:
        raise W3DJobRootError("native texture selection is invalid")
    if (
        selection.get("families") != ["texture"]
        or selection.get("conversion") != {}
        or selection.get("sourceManifestSha256") != effective.sha256
        or selection.get("sourceManifestAggregateSha256") != effective.aggregate_sha256
        or not _is_sha256(selection.get("requestSha256"))
    ):
        raise W3DJobRootError("native texture source binding is invalid")
    raw_outputs = document.get("outputs")
    raw_entries = document.get("entries")
    raw_reclassified = document.get("reclassified")
    if not all(
        isinstance(value, list)
        for value in (raw_outputs, raw_entries, raw_reclassified)
    ):
        raise W3DJobRootError("native texture inventories are invalid")
    assert isinstance(raw_outputs, list)
    assert isinstance(raw_entries, list)
    assert isinstance(raw_reclassified, list)
    if len(raw_outputs) > 250_000 or len(raw_entries) + len(raw_reclassified) > 250_000:
        raise W3DJobRootLimitError("native texture inventory exceeds its hard bound")
    outputs: dict[str, _NativeOutput] = {}
    output_paths: list[str] = []
    for index, item in enumerate(raw_outputs):
        if not isinstance(item, dict) or set(item) != {
            "path",
            "bytes",
            "sha256",
            "nativeFamily",
            "evidence",
        }:
            raise W3DJobRootError(f"native texture output {index} shape is invalid")
        output_path = _safe_path(
            item.get("path"), label="native texture output", suffix=".png"
        )
        size = item.get("bytes")
        digest = item.get("sha256")
        if (
            not _is_int(size)
            or not _is_sha256(digest)
            or item.get("nativeFamily") != "png"
            or not isinstance(item.get("evidence"), dict)
            or output_path != f"objects/sha256/{str(digest)[:2]}/{digest}.png"
        ):
            raise W3DJobRootError(f"native texture output {index} metadata is invalid")
        output_paths.append(output_path)
        outputs[output_path.casefold()] = _NativeOutput(
            output_path, int(size), str(digest), item["evidence"]
        )
    if output_paths != sorted(
        output_paths, key=lambda value: (value.casefold(), value)
    ):
        raise W3DJobRootError("native texture outputs are not canonical")
    _validate_path_inventory(output_paths, label="native texture output")
    effective_by_path = {item.path.casefold(): item for item in effective.files}
    entry_paths: list[str] = []
    reclassified_paths: list[str] = []
    request_sources: list[dict[str, object]] = []
    referenced_outputs: set[str] = set()
    converted_source_bytes = 0
    reclassified_source_bytes = 0
    for index, item in enumerate(raw_entries):
        if not isinstance(item, dict) or set(item) != {
            "sourcePath",
            "sourceArchive",
            "sourceExtension",
            "sourceBytes",
            "sourceSha256",
            "family",
            "outputPath",
            "outputBytes",
            "outputSha256",
            "nativeFamily",
            "evidence",
        }:
            raise W3DJobRootError(f"native texture entry {index} shape is invalid")
        source_path = _safe_path(item.get("sourcePath"), label="native texture source")
        source_archive = _safe_path(
            item.get("sourceArchive"), label="native texture archive"
        )
        output_path = _safe_path(
            item.get("outputPath"),
            label="native texture output reference",
            suffix=".png",
        )
        extension = item.get("sourceExtension")
        source = effective_by_path.get(source_path.casefold())
        output = outputs.get(output_path.casefold())
        if (
            source is None
            or source.path != source_path
            or source.archive != source_archive
            or source.size != item.get("sourceBytes")
            or source.sha256 != item.get("sourceSha256")
            or not isinstance(extension, str)
            or extension != PurePosixPath(source_path).suffix.casefold()
            or extension not in _TEXTURE_EXTENSIONS
            or item.get("family") != "texture"
            or item.get("nativeFamily") != "png"
            or output is None
            or output.path != output_path
            or output.size != item.get("outputBytes")
            or output.sha256 != item.get("outputSha256")
            or output.evidence != item.get("evidence")
        ):
            raise W3DJobRootError(f"native texture entry {index} binding is invalid")
        entry_paths.append(source_path)
        referenced_outputs.add(output_path.casefold())
        converted_source_bytes += source.size
        request_sources.append(
            {
                "path": source_path,
                "bytes": source.size,
                "sha256": source.sha256,
                "family": "texture",
                "extension": extension,
                "disposition": "media-conversion",
            }
        )
    for index, item in enumerate(raw_reclassified):
        if not isinstance(item, dict) or set(item) != {
            "sourcePath",
            "sourceBytes",
            "sourceSha256",
            "originalFamily",
            "originalExtension",
            "classification",
            "detectedKind",
            "evidenceSha256",
        }:
            raise W3DJobRootError(
                f"native texture reclassification {index} shape is invalid"
            )
        source_path = _safe_path(
            item.get("sourcePath"), label="native texture reclassification"
        )
        source = effective_by_path.get(source_path.casefold())
        extension = item.get("originalExtension")
        if (
            source is None
            or source.path != source_path
            or source.size != item.get("sourceBytes")
            or source.sha256 != item.get("sourceSha256")
            or item.get("originalFamily") != "texture"
            or not isinstance(extension, str)
            or extension != PurePosixPath(source_path).suffix.casefold()
            or extension not in _TEXTURE_EXTENSIONS
            or item.get("classification") != "map-payload"
            or item.get("detectedKind") not in {"uncompressed", "ear-refpack"}
            or not _is_sha256(item.get("evidenceSha256"))
        ):
            raise W3DJobRootError(f"native texture reclassification {index} is invalid")
        reclassified_paths.append(source_path)
        reclassified_source_bytes += source.size
        request_sources.append(
            {
                "path": source_path,
                "bytes": source.size,
                "sha256": source.sha256,
                "family": "texture",
                "extension": extension,
                "disposition": "map-payload",
                "detectedKind": item["detectedKind"],
                "evidenceSha256": item["evidenceSha256"],
            }
        )
    for paths, label in (
        (entry_paths, "native texture source"),
        (reclassified_paths, "native texture reclassification"),
    ):
        if paths != sorted(paths, key=lambda value: (value.casefold(), value)):
            raise W3DJobRootError(f"{label} inventory is not canonical")
        _validate_source_path_inventory(paths, label=label)
    if {path.casefold() for path in entry_paths} & {
        path.casefold() for path in reclassified_paths
    }:
        raise W3DJobRootError("native texture source has two dispositions")
    if referenced_outputs != set(outputs):
        raise W3DJobRootError("native texture corpus contains unreferenced outputs")
    request_sources.sort(
        key=lambda item: (str(item["path"]).casefold(), str(item["path"]))
    )
    if selection["requestSha256"] != _native_request_sha256(
        effective.sha256, effective.aggregate_sha256, {}, request_sources
    ):
        raise W3DJobRootError("native texture request SHA-256 is invalid")
    expected_totals = {
        "candidateFileCount": len(raw_entries) + len(raw_reclassified),
        "candidateBytes": converted_source_bytes + reclassified_source_bytes,
        "convertedFileCount": len(raw_entries),
        "convertedBytes": converted_source_bytes,
        "reclassifiedFileCount": len(raw_reclassified),
        "reclassifiedBytes": reclassified_source_bytes,
        "outputFileCount": len(outputs),
        "outputBytes": sum(item.size for item in outputs.values()),
    }
    if document.get("totals") != expected_totals:
        raise W3DJobRootError("native texture totals are invalid")
    basis = {key: value for key, value in document.items() if key != "identitySha256"}
    corpus_identity = document.get("identitySha256")
    if (
        not _is_sha256(corpus_identity)
        or corpus_identity != _canonical_sha256(basis)
        or corpus_identity != plan.native_texture_identity_sha256
    ):
        raise W3DJobRootError("native texture identity is incoherent")
    tree_files, tree_directories = _scan_tree(root, label="native texture corpus tree")
    _validate_exact_tree(
        tree_files,
        tree_directories,
        ["manifest.json", *output_paths],
        label="native texture corpus tree",
    )
    return _NativeManifest(path, raw, identity, seal, str(corpus_identity), outputs)


def _closure_sha256(value: object) -> str:
    return _canonical_sha256(value)


def _job_plan_sha256(value: object) -> str:
    return hashlib.sha256(_canonical_json_bytes(value, ensure_ascii=False)).hexdigest()


def _stage_source_id(item: W3DInputStageFile) -> str:
    return catalog_source_id(
        W3DCatalogSource(
            supplied_virtual_path=item.source_path,
            canonical_virtual_path=item.source_path,
            byte_length=item.source_bytes,
            source_sha256=item.source_sha256,
        )
    )


def _validate_closure_plan(
    plan: W3DTextureClosurePlan,
    report: W3DInputStageReport,
    native: _NativeManifest,
    *,
    allow_terminal_models: bool,
) -> None:
    if not isinstance(plan, W3DTextureClosurePlan):
        raise TypeError("W3D job root requires a W3DTextureClosurePlan")
    if type(plan.models) is not tuple or type(plan.copy_instructions) is not tuple:
        raise W3DJobRootError("W3D texture closure is not immutable")
    for digest in (
        plan.catalog_input_sha256,
        plan.catalog_metadata_sha256,
        plan.effective_manifest_sha256,
        plan.effective_manifest_aggregate_sha256,
        plan.native_manifest_sha256,
        plan.native_texture_identity_sha256,
        plan.private_plan_sha256,
        plan.evidence_sha256,
    ):
        if not _is_sha256(digest):
            raise W3DJobRootError("W3D texture closure contains an invalid hash")
    if plan.edition_seal is not None and not _is_sha256(plan.edition_seal):
        raise W3DJobRootError("W3D texture closure edition seal is invalid")
    expected_catalog_input = _canonical_sha256(
        {
            "schema": "openbfme.w3d-catalog-input",
            "schemaVersion": 0,
            "sources": [
                {
                    "suppliedVirtualPath": item.source_path,
                    "byteLength": item.source_bytes,
                    "sourceSha256": item.source_sha256,
                }
                for item in report.files
            ],
        },
        newline=False,
    )
    if (
        plan.catalog_input_sha256 != expected_catalog_input
        or plan.source_seal != report.identity_sha256
        or plan.effective_manifest_sha256 != report.source_manifest_sha256
        or plan.effective_manifest_aggregate_sha256
        != report.source_manifest_aggregate_sha256
        or plan.native_manifest_sha256 != native.sha256
        or plan.native_texture_identity_sha256 != native.corpus_identity_sha256
    ):
        raise W3DJobRootError("W3D texture closure source bindings mismatch")
    try:
        private_hash = _closure_sha256(plan.private_hash_basis())
        evidence_hash = _closure_sha256(plan.evidence_hash_basis())
    except (AttributeError, TypeError, ValueError) as exc:
        raise W3DJobRootError(
            "W3D texture closure seal cannot be reconstructed"
        ) from exc
    if (
        plan.private_plan_sha256 != private_hash
        or plan.evidence_sha256 != evidence_hash
    ):
        raise W3DJobRootError("W3D texture closure seal is incoherent")
    staged_by_path = {item.staged_path.casefold(): item for item in report.files}
    model_by_id: dict[str, W3DModelTextureClosure] = {}
    expected_instruction_ids: dict[str, set[str]] = {}
    if any(not isinstance(item, W3DModelTextureClosure) for item in plan.models):
        raise W3DJobRootError("W3D texture closure model has the wrong type")
    if not allow_terminal_models and (not plan.complete or plan.terminal_model_count):
        raise W3DJobRootError("W3D texture closure is incomplete")
    model_ids = [item.model_source_id for item in plan.models]
    if (
        any(not isinstance(value, str) or not value for value in model_ids)
        or model_ids != sorted(model_ids)
        or len(set(model_ids)) != len(model_ids)
    ):
        raise W3DJobRootError("W3D texture closure model inventory is not canonical")
    for model in plan.models:
        assert isinstance(model, W3DModelTextureClosure)
        staged_path = _safe_path(
            model.staged_model_path, label="closure model", suffix=".w3d"
        )
        staged = staged_by_path.get(staged_path.casefold())
        if staged is None or staged.staged_path != staged_path:
            raise W3DJobRootError("W3D texture closure model is absent from the stage")
        expected_model_id = _stage_source_id(staged)
        if (
            model.model_source_id != expected_model_id
            or model.model_source_sha256 != staged.source_sha256
            or not _is_int(model.texture_reference_count)
            or not _is_int(model.resolved_reference_count)
            or model.resolved_reference_count > model.texture_reference_count
            or type(model.instruction_ids) is not tuple
            or any(
                not isinstance(value, str) or not value
                for value in model.instruction_ids
            )
            or tuple(sorted(set(model.instruction_ids))) != model.instruction_ids
            or type(model.reference_evidence_sha256s) is not tuple
            or any(not _is_sha256(value) for value in model.reference_evidence_sha256s)
            or tuple(sorted(set(model.reference_evidence_sha256s)))
            != model.reference_evidence_sha256s
            or type(model.terminal_reasons) is not tuple
            or any(
                not isinstance(reason, str) or not reason
                for reason in model.terminal_reasons
            )
            or model.terminal_reasons != tuple(sorted(set(model.terminal_reasons)))
            or (
                not model.terminal_reasons
                and model.resolved_reference_count != model.texture_reference_count
            )
            or (not allow_terminal_models and model.terminal_reasons != ())
        ):
            raise W3DJobRootError("W3D texture closure model evidence is invalid")
        model_by_id[model.model_source_id] = model
        expected_instruction_ids[model.model_source_id] = set()
    instruction_ids: list[str] = []
    destinations: list[str] = []
    for instruction in plan.copy_instructions:
        if not isinstance(instruction, W3DTextureCopyInstruction):
            raise W3DJobRootError("W3D texture copy instruction has the wrong type")
        source_path = _safe_path(
            instruction.source_output_path,
            label="texture copy source",
            suffix=".png",
        )
        destination = _safe_path(
            instruction.destination_path,
            label="texture copy destination",
            suffix=".png",
        )
        native_output = native.outputs.get(source_path.casefold())
        if (
            native_output is None
            or native_output.path != source_path
            or native_output.size != instruction.source_output_bytes
            or native_output.sha256 != instruction.source_output_sha256
            or not _is_int(instruction.source_output_bytes, minimum=1)
            or type(instruction.model_source_ids) is not tuple
            or any(
                not isinstance(model_id, str) or not model_id
                for model_id in instruction.model_source_ids
            )
            or tuple(sorted(set(instruction.model_source_ids)))
            != instruction.model_source_ids
            or not instruction.model_source_ids
            or any(
                model_id not in model_by_id for model_id in instruction.model_source_ids
            )
            or type(instruction.reference_evidence_sha256s) is not tuple
            or any(
                not _is_sha256(value)
                for value in instruction.reference_evidence_sha256s
            )
            or tuple(sorted(set(instruction.reference_evidence_sha256s)))
            != instruction.reference_evidence_sha256s
        ):
            raise W3DJobRootError("W3D texture copy instruction binding is invalid")
        basis = {
            "sourceOutputPath": source_path,
            "sourceOutputSha256": instruction.source_output_sha256,
            "sourceOutputBytes": instruction.source_output_bytes,
            "destinationPath": destination,
            "modelSourceIds": list(instruction.model_source_ids),
            "referenceEvidenceSha256s": list(instruction.reference_evidence_sha256s),
        }
        definition = _closure_sha256(basis)
        if (
            instruction.definition_sha256 != definition
            or instruction.instruction_id != f"texcopy-{definition[:40]}"
        ):
            raise W3DJobRootError("W3D texture copy instruction seal is invalid")
        instruction_ids.append(instruction.instruction_id)
        destinations.append(destination)
        for model_id in instruction.model_source_ids:
            expected_instruction_ids[model_id].add(instruction.instruction_id)
    if instruction_ids != sorted(instruction_ids) or len(set(instruction_ids)) != len(
        instruction_ids
    ):
        raise W3DJobRootError("W3D texture copy instructions are not canonical")
    _validate_path_inventory(destinations, label="texture copy destination")
    for model_id, expected in expected_instruction_ids.items():
        if tuple(sorted(expected)) != model_by_id[model_id].instruction_ids:
            raise W3DJobRootError("W3D model/copy-instruction accounting mismatches")


def _accounted_payload_selection(
    job_plan: W3DJobPlan,
    texture_plan: W3DTextureClosurePlan,
    report: W3DInputStageReport,
    preparation_preflight_report: W3DJobPreparationPreflightReport | None = None,
    preparation_fixed_point_report: W3DJobPreparationFixedPointReport | None = None,
) -> _PayloadSelection:
    if not isinstance(job_plan, W3DJobPlan):
        raise TypeError("accounted W3D job root requires a W3DJobPlan")
    if not all(
        type(value) is tuple
        for value in (job_plan.jobs, job_plan.batches, job_plan.terminals)
    ):
        raise W3DJobRootError("W3D job plan is not immutable")
    if not job_plan.jobs:
        raise W3DJobRootError(
            "accounted W3D job-root materialization requires at least one job"
        )
    if (
        type(job_plan.source_count) is not int
        or type(job_plan.consumed_source_count) is not int
        or job_plan.source_count < 0
        or job_plan.consumed_source_count < 0
        or job_plan.consumed_source_count > job_plan.source_count
    ):
        raise W3DJobRootError("W3D job plan source accounting is invalid")
    if (
        not isinstance(job_plan.presentation_policy, str)
        or job_plan.presentation_policy not in _PRESENTATION_POLICIES
    ):
        raise W3DJobRootError("W3D job plan presentation policy is invalid")
    for digest in (
        job_plan.catalog_input_sha256,
        job_plan.catalog_metadata_sha256,
        job_plan.private_plan_sha256,
        job_plan.evidence_sha256,
    ):
        if not _is_sha256(digest):
            raise W3DJobRootError("W3D job plan contains an invalid hash")
    if job_plan.forced_terminal_evidence_sha256 is not None and not _is_sha256(
        job_plan.forced_terminal_evidence_sha256
    ):
        raise W3DJobRootError("W3D job plan forced-terminal seal is invalid")
    if job_plan.forced_terminal_rows is not None:
        if type(job_plan.forced_terminal_rows) is not tuple:
            raise W3DJobRootError("W3D job plan forced-terminal rows are not immutable")
        validated_forced_rows: list[tuple[str, tuple[str, ...]]] = []
        for row in job_plan.forced_terminal_rows:
            if type(row) is not tuple or len(row) != 2:
                raise W3DJobRootError("W3D job plan forced-terminal rows are invalid")
            source_id, reasons = row
            if (
                not isinstance(source_id, str)
                or not source_id
                or type(reasons) is not tuple
                or not reasons
                or any(not isinstance(reason, str) or not reason for reason in reasons)
                or reasons != tuple(sorted(set(reasons)))
            ):
                raise W3DJobRootError("W3D job plan forced-terminal rows are invalid")
            validated_forced_rows.append((source_id, reasons))
        if tuple(validated_forced_rows) != tuple(sorted(validated_forced_rows)):
            raise W3DJobRootError("W3D job plan forced-terminal rows are not canonical")
    try:
        expected_job_evidence = _job_plan_sha256(job_plan.evidence_hash_basis())
    except (AttributeError, TypeError, ValueError) as exc:
        raise W3DJobRootError("W3D job plan evidence is malformed") from exc
    if expected_job_evidence != job_plan.evidence_sha256:
        raise W3DJobRootError("W3D job plan evidence seal is incoherent")
    if (
        job_plan.catalog_input_sha256 != texture_plan.catalog_input_sha256
        or job_plan.catalog_metadata_sha256 != texture_plan.catalog_metadata_sha256
    ):
        raise W3DJobRootError("W3D job and texture-plan catalog seals mismatch")

    staged_by_path = {item.staged_path.casefold(): item for item in report.files}
    staged_by_source_id = {_stage_source_id(item): item for item in report.files}
    if len(staged_by_source_id) != len(report.files):
        raise W3DJobRootError("W3D input stage source identities collide")
    if job_plan.source_count != len(staged_by_source_id):
        raise W3DJobRootError("W3D job plan source count mismatches the input stage")

    def staged_role(
        path_value: object,
        source_id: object,
        *,
        label: str,
        source_sha256: object | None = None,
    ) -> W3DInputStageFile:
        path = _safe_path(path_value, label=label, suffix=".w3d")
        staged = staged_by_path.get(path.casefold())
        if (
            staged is None
            or staged.staged_path != path
            or not isinstance(source_id, str)
            or _stage_source_id(staged) != source_id
            or (source_sha256 is not None and staged.source_sha256 != source_sha256)
        ):
            raise W3DJobRootError(f"{label} binding mismatches the input stage")
        return staged

    job_ids: set[str] = set()
    output_paths: set[str] = set()
    model_source_ids: set[str] = set()
    consumed_source_ids: set[str] = set()
    selected_stage_paths: set[str] = set()
    ordered_job_ids: list[str] = []
    for job in job_plan.jobs:
        if not isinstance(job, W3DPlannedJob):
            raise W3DJobRootError("W3D job plan contains an invalid job")
        if (
            not isinstance(job.job_id, str)
            or not isinstance(job.asset_kind, str)
            or job.asset_kind not in _ASSET_KINDS
            or not _is_sha256(job.model_source_sha256)
            or not _is_sha256(job.definition_sha256)
            or type(job.animations) is not tuple
            or type(job.animation_source_ids) is not tuple
            or type(job.animation_source_sha256s) is not tuple
            or type(job.proven_root_rigid_bake) is not bool
            or len(job.animations) != len(job.animation_source_ids)
            or len(job.animations) != len(job.animation_source_sha256s)
            or any(not isinstance(value, str) for value in job.animations)
            or any(
                not isinstance(value, str) or not value
                for value in job.animation_source_ids
            )
            or any(not _is_sha256(value) for value in job.animation_source_sha256s)
            or len(set(job.animations)) != len(job.animations)
            or len(set(job.animation_source_ids)) != len(job.animation_source_ids)
            or job.presentation_policy != job_plan.presentation_policy
            or job.prepared_model_sha256 is not None
            or job.model_preparation_evidence_sha256 is not None
        ):
            raise W3DJobRootError("W3D job plan contains invalid job metadata")
        if job.model_preparation is not None and (
            not isinstance(job.model_preparation, str)
            or job.model_preparation != SECONDARY_SKIN_PREPARATION
        ):
            raise W3DJobRootError("W3D job plan has an unsupported model preparation")
        if job.presentation_policy == PRESENTATION_POLICY_STRICT:
            if job.authored_presentation_mesh_count is not None:
                raise W3DJobRootError("strict W3D presentation evidence is invalid")
        elif not _is_int(job.authored_presentation_mesh_count, minimum=1):
            raise W3DJobRootError("preserve-all W3D presentation evidence is invalid")
        if not w3d_job_resolution_contract_is_valid(job):
            raise W3DJobRootError("W3D job resolution contract is invalid")

        model = staged_role(
            job.model,
            job.model_source_id,
            label="planned W3D model",
            source_sha256=job.model_source_sha256,
        )
        hierarchy_fields = (
            job.hierarchy,
            job.hierarchy_source_id,
            job.hierarchy_source_sha256,
        )
        if any(value is None for value in hierarchy_fields) != all(
            value is None for value in hierarchy_fields
        ):
            raise W3DJobRootError("planned W3D hierarchy evidence is incomplete")
        hierarchy: W3DInputStageFile | None = None
        if job.hierarchy is not None:
            hierarchy = staged_role(
                job.hierarchy,
                job.hierarchy_source_id,
                label="planned W3D hierarchy",
                source_sha256=job.hierarchy_source_sha256,
            )
        animations: list[W3DInputStageFile] = []
        for animation_path, animation_source_id, animation_source_sha256 in zip(
            job.animations,
            job.animation_source_ids,
            job.animation_source_sha256s,
            strict=True,
        ):
            animations.append(
                staged_role(
                    animation_path,
                    animation_source_id,
                    label="planned W3D animation",
                    source_sha256=animation_source_sha256,
                )
            )
        if job.asset_kind == "static" and (hierarchy is not None or animations):
            raise W3DJobRootError("static W3D job has unexpected dependencies")
        if job.asset_kind == "hierarchical" and (hierarchy is None or animations):
            raise W3DJobRootError("hierarchical W3D job dependencies are invalid")
        if job.asset_kind == "animated" and (hierarchy is None or not animations):
            raise W3DJobRootError("animated W3D job dependencies are invalid")
        if job.model_preparation is not None and hierarchy is None:
            raise W3DJobRootError("prepared W3D model has no hierarchy")

        role_ids = [job.model_source_id]
        if job.hierarchy_source_id is not None:
            role_ids.append(job.hierarchy_source_id)
        role_ids.extend(job.animation_source_ids)
        same_source_hierarchy_only = (
            not job.animations
            and hierarchy is not None
            and hierarchy.staged_path == model.staged_path
            and job.hierarchy_source_id == job.model_source_id
        )
        allowed_duplicate_count = (
            2
            if w3d_job_is_exact_embedded_model_animation(job)
            else int(same_source_hierarchy_only)
        )
        if len(role_ids) != len(set(role_ids)) + allowed_duplicate_count:
            raise W3DJobRootError("W3D job dependency accounting contains duplicates")

        definition_basis: dict[str, object] = {
            "assetKind": job.asset_kind,
            "model": {
                "sourceId": job.model_source_id,
                "sourceSha256": job.model_source_sha256,
            },
            "hierarchySourceId": job.hierarchy_source_id,
            "modelPreparation": job.model_preparation,
            "animations": [
                {
                    "sourceId": source_id,
                    "sourceSha256": source_sha256,
                }
                for source_id, source_sha256 in zip(
                    job.animation_source_ids,
                    job.animation_source_sha256s,
                    strict=True,
                )
            ],
        }
        if job.proven_root_rigid_bake:
            definition_basis["provenRootRigidBake"] = True
        if job.presentation_policy == PRESENTATION_POLICY_PRESERVE_ALL:
            definition_basis["presentation"] = {
                "policy": PRESENTATION_POLICY_PRESERVE_ALL,
                "authoredPresentationMeshCount": (job.authored_presentation_mesh_count),
                "allAuthoredPresentationMeshesRetained": True,
                "equipmentExclusionsApplied": False,
                "conditionStateSelectionApplied": False,
                "runtimePresentationResolved": False,
                "runtimeFidelityClaimed": False,
            }
        definition = _job_plan_sha256(definition_basis)
        output = _safe_path(job.output, label="planned W3D output", suffix=".glb")
        if (
            job.definition_sha256 != definition
            or job.job_id != f"w3d-{definition[:40]}"
            or output != f"glb/{job.job_id}.glb"
            or job.job_id in job_ids
            or output.casefold() in output_paths
            or job.model_source_id in model_source_ids
        ):
            raise W3DJobRootError("W3D job identity or uniqueness is invalid")
        job_ids.add(job.job_id)
        ordered_job_ids.append(job.job_id)
        output_paths.add(output.casefold())
        model_source_ids.add(job.model_source_id)
        consumed_source_ids.update(role_ids)
        selected_stage_paths.add(model.staged_path.casefold())
        if hierarchy is not None:
            selected_stage_paths.add(hierarchy.staged_path.casefold())
        selected_stage_paths.update(item.staged_path.casefold() for item in animations)
    if ordered_job_ids != sorted(ordered_job_ids):
        raise W3DJobRootError("W3D job inventory is not canonical")

    flattened_jobs: list[W3DPlannedJob] = []
    batch_ids: set[str] = set()
    for batch in job_plan.batches:
        if not isinstance(batch, W3DJobBatch) or type(batch.jobs) is not tuple:
            raise W3DJobRootError("W3D job plan contains an invalid batch")
        try:
            payload = batch.manifest_bytes()
        except (AttributeError, TypeError, ValueError) as exc:
            raise W3DJobRootError("W3D job batch is malformed") from exc
        digest = hashlib.sha256(payload).hexdigest()
        if (
            not batch.jobs
            or len(batch.jobs) > MAX_BATCH_JOBS
            or len(payload) > MAX_BATCH_MANIFEST_BYTES
            or batch.manifest_sha256 != digest
            or batch.batch_id != f"batch-{digest[:32]}"
            or batch.batch_id in batch_ids
        ):
            raise W3DJobRootError("W3D job batch seal is invalid")
        batch_ids.add(batch.batch_id)
        flattened_jobs.extend(batch.jobs)
    if tuple(flattened_jobs) != job_plan.jobs:
        raise W3DJobRootError("W3D job batches do not exactly cover jobs")

    terminal_source_ids: set[str] = set()
    terminal_by_source_id: dict[str, W3DTerminal] = {}
    terminal_order: list[tuple[str, str]] = []
    for terminal in job_plan.terminals:
        if (
            not isinstance(terminal, W3DTerminal)
            or not isinstance(terminal.source_id, str)
            or not _is_sha256(terminal.source_sha256)
            or type(terminal.reason_codes) is not tuple
            or not terminal.reason_codes
            or any(
                not isinstance(reason, str) or not reason
                for reason in terminal.reason_codes
            )
            or terminal.reason_codes != tuple(sorted(set(terminal.reason_codes)))
        ):
            raise W3DJobRootError("W3D job terminal evidence is invalid")
        staged = staged_by_source_id.get(terminal.source_id)
        if staged is None or staged.source_sha256 != terminal.source_sha256:
            raise W3DJobRootError("W3D job terminal mismatches the input stage")
        if terminal.source_id in terminal_source_ids:
            raise W3DJobRootError("W3D job plan repeats a terminal source")
        terminal_source_ids.add(terminal.source_id)
        terminal_by_source_id[terminal.source_id] = terminal
        terminal_order.append((terminal.source_id, terminal.source_sha256))
    if terminal_order != sorted(terminal_order):
        raise W3DJobRootError("W3D job terminal inventory is not canonical")
    if len(consumed_source_ids) != job_plan.consumed_source_count:
        raise W3DJobRootError("W3D job consumed source cardinality is not exact")
    if consumed_source_ids & terminal_source_ids:
        raise W3DJobRootError("W3D job and terminal source inventories overlap")
    if (
        job_plan.consumed_source_count + len(job_plan.terminals)
        != job_plan.source_count
    ):
        raise W3DJobRootError("W3D job source accounting contains a gap")
    if consumed_source_ids | terminal_source_ids != set(staged_by_source_id):
        raise W3DJobRootError("W3D job accounting mismatches the input stage")

    try:
        forced_texture_terminals, forced_texture_seal = (
            texture_closure_forced_terminals(texture_plan)
        )
    except (AttributeError, TypeError, ValueError) as exc:
        raise W3DJobRootError(
            "W3D texture-terminal bridge evidence is invalid"
        ) from exc
    if (
        preparation_preflight_report is not None
        and preparation_fixed_point_report is not None
    ):
        raise W3DJobRootError(
            "W3D preparation bridge and fixed-point evidence are mutually exclusive"
        )
    preflight_rows = ()
    if preparation_fixed_point_report is not None:
        try:
            validate_w3d_job_preparation_fixed_point(
                preparation_fixed_point_report,
                job_plan,
            )
        except (TypeError, W3DJobPreparationError) as exc:
            raise W3DJobRootError(
                "W3D preparation fixed-point evidence is invalid"
            ) from exc
        reports = preparation_fixed_point_report.reports
        first_report = reports[0]
        if (
            first_report.upstream_forced_terminal_rows
            != tuple(sorted(forced_texture_terminals.items()))
            or first_report.upstream_forced_terminal_evidence_sha256
            != forced_texture_seal
        ):
            raise W3DJobRootError(
                "W3D preparation fixed point mismatches the texture bridge"
            )
        merged_forced_terminals = dict(forced_texture_terminals)
        merged_forced_seal = forced_texture_seal
        historical_rows = []
        try:
            for iteration in reports[:-1]:
                historical_rows.extend(iteration.forced_terminal_rows)
                merged_forced_terminals, merged_forced_seal = (
                    merge_w3d_preparation_forced_terminals(
                        iteration,
                        upstream_forced_terminal_reasons=merged_forced_terminals,
                        upstream_forced_terminal_evidence_sha256=merged_forced_seal,
                    )
                )
        except (TypeError, W3DJobPreparationError) as exc:
            raise W3DJobRootError("W3D preparation fixed-point replay failed") from exc
        expected_forced_rows = tuple(sorted(merged_forced_terminals.items()))
        expected_forced_seal = merged_forced_seal
        preflight_rows = tuple(historical_rows)
    elif preparation_preflight_report is None:
        expected_forced_rows = tuple(sorted(forced_texture_terminals.items()))
        expected_forced_seal = forced_texture_seal
    else:
        try:
            merged_forced_terminals, merged_forced_seal = (
                merge_w3d_preparation_forced_terminals(
                    preparation_preflight_report,
                    upstream_forced_terminal_reasons=forced_texture_terminals,
                    upstream_forced_terminal_evidence_sha256=forced_texture_seal,
                )
            )
        except (TypeError, W3DJobPreparationError) as exc:
            raise W3DJobRootError(
                "W3D preparation-preflight bridge evidence is invalid"
            ) from exc
        if (
            preparation_preflight_report.catalog_input_sha256
            != job_plan.catalog_input_sha256
            or preparation_preflight_report.catalog_metadata_sha256
            != job_plan.catalog_metadata_sha256
            or preparation_preflight_report.source_count != job_plan.source_count
        ):
            raise W3DJobRootError("W3D preparation-preflight catalog evidence is stale")
        expected_forced_rows = tuple(sorted(merged_forced_terminals.items()))
        expected_forced_seal = merged_forced_seal
        preflight_rows = preparation_preflight_report.forced_terminal_rows
    actual_forced_rows = job_plan.forced_terminal_rows or ()
    if actual_forced_rows != expected_forced_rows:
        if (
            preparation_preflight_report is None
            and preparation_fixed_point_report is None
        ):
            raise W3DJobRootError(
                "W3D job plan forced-terminal rows mismatch the texture bridge"
            )
        raise W3DJobRootError(
            "W3D job plan forced-terminal rows mismatch the preparation bridge"
        )
    if (
        preparation_preflight_report is not None
        or preparation_fixed_point_report is not None
    ):
        if job_plan.forced_terminal_evidence_sha256 != expected_forced_seal:
            raise W3DJobRootError(
                "W3D job plan does not bind the preparation-preflight bridge"
            )
    elif forced_texture_terminals:
        if job_plan.forced_terminal_evidence_sha256 != expected_forced_seal:
            raise W3DJobRootError(
                "W3D job plan does not bind the texture-terminal bridge"
            )
    elif job_plan.forced_terminal_evidence_sha256 not in {
        None,
        expected_forced_seal,
    }:
        raise W3DJobRootError(
            "W3D job plan forced-terminal seal mismatches the empty texture bridge"
        )
    for source_id, reasons in forced_texture_terminals.items():
        terminal = terminal_by_source_id.get(source_id)
        if (
            terminal is None
            or source_id in consumed_source_ids
            or not set(reasons).issubset(terminal.reason_codes)
        ):
            raise W3DJobRootError(
                "texture-terminal bridge is not covered by planner terminals"
            )
    for preflight_row in preflight_rows:
        staged = staged_by_source_id.get(preflight_row.source_id)
        terminal = terminal_by_source_id.get(preflight_row.source_id)
        if (
            staged is None
            or staged.source_sha256 != preflight_row.source_sha256
            or preflight_row.source_id in consumed_source_ids
            or terminal is None
            or not set(preflight_row.reason_codes).issubset(terminal.reason_codes)
        ):
            raise W3DJobRootError(
                "preparation-preflight rejection is not covered by planner terminals"
            )

    closure_by_model_id = {
        model.model_source_id: model for model in texture_plan.models
    }
    texture_terminal_ids = {
        model.model_source_id for model in texture_plan.models if model.terminal_reasons
    }
    if texture_terminal_ids - terminal_source_ids:
        raise W3DJobRootError("texture-terminal W3D models are not planner terminals")
    if texture_terminal_ids & consumed_source_ids:
        raise W3DJobRootError("planned or consumed W3D model has a texture terminal")
    selected_instruction_ids: set[str] = set()
    for job in job_plan.jobs:
        model = closure_by_model_id.get(job.model_source_id)
        if (
            model is None
            or model.terminal_reasons
            or model.model_source_sha256 != job.model_source_sha256
            or model.staged_model_path != job.model
        ):
            raise W3DJobRootError(
                "planned W3D model lacks a complete bound texture closure"
            )
        selected_instruction_ids.update(model.instruction_ids)
    instructions_by_id = {
        instruction.instruction_id: instruction
        for instruction in texture_plan.copy_instructions
    }
    if selected_instruction_ids - set(instructions_by_id):
        raise W3DJobRootError("planned texture instruction is absent from the closure")
    selected_instructions = tuple(
        instruction
        for instruction in texture_plan.copy_instructions
        if instruction.instruction_id in selected_instruction_ids
    )
    if any(
        not (set(instruction.model_source_ids) & model_source_ids)
        for instruction in selected_instructions
    ):
        raise W3DJobRootError("selected texture instruction has no planned model")
    selected_stage_files = tuple(
        item
        for item in report.files
        if item.staged_path.casefold() in selected_stage_paths
    )
    if len(selected_stage_files) != len(selected_stage_paths):
        raise W3DJobRootError("planned W3D dependency selection is not exact")
    return _PayloadSelection(
        selected_stage_files,
        selected_instructions,
        job_plan,
        True,
        (
            preparation_fixed_point_report.evidence_sha256
            if preparation_fixed_point_report is not None
            else None
        ),
        (
            len(preparation_fixed_point_report.reports)
            if preparation_fixed_point_report is not None
            else None
        ),
    )


def _verify_native_evidence(
    actual: _TreeFile, expected: _NativeOutput, *, label: str
) -> None:
    if actual.relative_path != expected.path or actual.size != expected.size:
        raise W3DJobRootError(f"{label} path or size mismatches its manifest")
    try:
        before = actual.path.stat()
        evidence = validate_native_output("png", actual.path)
        after = actual.path.stat()
    except OSError as exc:
        raise W3DJobRootError(f"{label} cannot be backtested") from exc
    if (
        _stat_identity(before) != actual.identity
        or _stat_identity(after) != actual.identity
        or evidence.get("valid") is not True
        or evidence.get("size") != expected.size
        or evidence.get("sha256") != expected.sha256
        or evidence != expected.evidence
    ):
        raise W3DJobRootError(f"{label} failed independent native PNG evidence")


def _copy_verified(
    actual: _TreeFile,
    *,
    expected_size: int,
    expected_sha256: str,
    target: Path | None,
    label: str,
) -> None:
    output = None
    copied = 0
    digest = hashlib.sha256()
    try:
        before = actual.path.stat()
        if (
            _is_link_like(actual.path)
            or before.st_nlink != 1
            or not stat.S_ISREG(before.st_mode)
            or _stat_identity(before) != actual.identity
        ):
            raise W3DJobRootError(f"{label} changed or became linked before read")
        if target is not None:
            target.parent.mkdir(parents=True, exist_ok=True)
            output = target.open("xb")
        with actual.path.open("rb") as source:
            if _stat_identity(os.fstat(source.fileno())) != actual.identity:
                raise W3DJobRootError(f"{label} changed while opening")
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
    except W3DJobRootError:
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
        raise W3DJobRootError(f"{label} cannot be copied") from exc
    if (
        copied != expected_size
        or digest.hexdigest() != expected_sha256
        or _stat_identity(before) != _stat_identity(after)
    ):
        if target is not None:
            target.unlink(missing_ok=True)
        raise W3DJobRootError(f"{label} size or SHA-256 mismatches its evidence")
    if target is not None:
        target_stat = target.stat()
        if (
            target_stat.st_nlink != 1
            or not stat.S_ISREG(target_stat.st_mode)
            or target_stat.st_size != expected_size
        ):
            raise W3DJobRootError(f"{label} copy is not an independent ordinary file")


def _make_spec(
    report: W3DInputStageReport,
    plan: W3DTextureClosurePlan,
    selection: _PayloadSelection,
    *,
    max_files: int,
    max_total_bytes: int,
) -> _JobSpec:
    files = [
        W3DJobRootFile(
            "w3d",
            item.staged_path,
            item.staged_path,
            item.source_bytes,
            item.source_sha256,
        )
        for item in selection.stage_files
    ]
    files.extend(
        W3DJobRootFile(
            "texture-png",
            item.source_output_path,
            item.destination_path,
            item.source_output_bytes,
            item.source_output_sha256,
            item.instruction_id,
        )
        for item in selection.copy_instructions
    )
    files.sort(
        key=lambda item: (item.destination_path.casefold(), item.destination_path)
    )
    destinations = [item.destination_path for item in files]
    if any(
        path.split("/", 1)[0].casefold() == _METADATA_DIRECTORY.casefold()
        for path in destinations
    ):
        raise W3DJobRootError("W3D job payload uses reserved metadata space")
    _validate_path_inventory(destinations, label="W3D job payload")
    total_bytes = sum(item.byte_length for item in files)
    if len(files) > max_files:
        raise W3DJobRootLimitError(
            f"W3D job root selects {len(files)} files; limit is {max_files}"
        )
    if total_bytes > max_total_bytes:
        raise W3DJobRootLimitError(
            f"W3D job root selects {total_bytes} bytes; limit is {max_total_bytes}"
        )
    rows = [item.private() for item in files]
    inventory = _canonical_sha256(
        {
            "schema": "openbfme.w3d-job-root-inventory",
            "schemaVersion": 0,
            "files": rows,
        },
        newline=False,
    )
    tree_rows = [
        {"path": item.destination_path, "size": item.byte_length, "sha256": item.sha256}
        for item in files
    ]
    tree_sha256 = _inventory_sha256("openbfme.w3d-job-root-output-tree-v0", tree_rows)
    bindings: dict[str, object] = {
        "catalogInputSha256": plan.catalog_input_sha256,
        "catalogMetadataSha256": plan.catalog_metadata_sha256,
        "effectiveManifestSha256": plan.effective_manifest_sha256,
        "effectiveManifestAggregateSha256": plan.effective_manifest_aggregate_sha256,
        "stageIdentitySha256": report.identity_sha256,
        "stageManifestSha256": report.manifest_sha256,
        "stageOutputTreeSha256": report.output_tree_sha256,
        "textureClosurePrivatePlanSha256": plan.private_plan_sha256,
        "textureClosureEvidenceSha256": plan.evidence_sha256,
        "nativeManifestSha256": plan.native_manifest_sha256,
        "nativeTextureIdentitySha256": plan.native_texture_identity_sha256,
    }
    if plan.edition_seal is not None:
        bindings["editionSeal"] = plan.edition_seal
    limits = {
        "hardMaxFiles": MAX_W3D_JOB_ROOT_FILES,
        "hardMaxTotalBytes": MAX_W3D_JOB_ROOT_BYTES,
        "maxFiles": max_files,
        "maxTotalBytes": max_total_bytes,
    }
    summary = {
        "fileCount": len(files),
        "totalBytes": total_bytes,
        "w3dFileCount": sum(item.kind == "w3d" for item in files),
        "textureFileCount": sum(item.kind == "texture-png" for item in files),
        "glbConversionComplete": False,
        "renderParityProven": False,
    }
    request_basis: dict[str, object] = {
        "schema": "openbfme.w3d-job-root-request",
        "schemaVersion": 0,
        "bindings": bindings,
        "inventorySha256": inventory,
        "outputTreeSha256": tree_sha256,
        "summary": summary,
        "limits": limits,
    }
    manifest_version = W3D_JOB_ROOT_SCHEMA_VERSION
    accounting: dict[str, object] | None = None
    if selection.materialize_accounted_jobs:
        if selection.job_plan is None:
            raise W3DJobRootError("accounted W3D job-root selection has no job plan")
        accounting = {
            "materializationPolicy": _ACCOUNTED_MATERIALIZATION_POLICY,
            "jobPlan": selection.job_plan.neutral(),
            "textureClosurePlan": plan.neutral(),
            "selectedInstructionIds": [
                item.instruction_id for item in selection.copy_instructions
            ],
            "selectedW3DSourceIds": [
                _stage_source_id(item) for item in selection.stage_files
            ],
        }
        accounted_schema_version = 1
        if selection.preparation_fixed_point_evidence_sha256 is not None:
            if selection.preparation_fixed_point_iteration_count is None:
                raise W3DJobRootError(
                    "accounted W3D job-root fixed-point count is missing"
                )
            accounting["preparationFixedPoint"] = {
                "evidenceSha256": (selection.preparation_fixed_point_evidence_sha256),
                "iterationCount": selection.preparation_fixed_point_iteration_count,
            }
            accounted_schema_version = 2
        request_basis = {
            **request_basis,
            "schema": "openbfme.w3d-job-root-accounted-request",
            "schemaVersion": accounted_schema_version,
            "accounting": accounting,
        }
        manifest_version = accounted_schema_version
    request = _canonical_sha256(request_basis, newline=False)
    basis: dict[str, object] = {
        "schema": W3D_JOB_ROOT_SCHEMA,
        "schemaVersion": manifest_version,
        "bindings": bindings,
        "limits": limits,
        "summary": summary,
        "files": rows,
        "inventorySha256": inventory,
        "outputTreeSha256": tree_sha256,
        "requestSha256": request,
    }
    if accounting is not None:
        basis["accounting"] = accounting
    identity = _canonical_sha256(basis, newline=False)
    document = {**basis, "identitySha256": identity}
    return _JobSpec(
        tuple(files),
        inventory,
        tree_sha256,
        request,
        identity,
        document,
        _canonical_json_bytes(document, pretty=True, ensure_ascii=False),
        max_files,
        max_total_bytes,
    )


def _snapshot_sources(
    stage_root: Path,
    native_root: Path,
    report: W3DInputStageReport,
    plan: W3DTextureClosurePlan,
    *,
    allow_terminal_models: bool,
    verify_effective_payload_hashes: bool = True,
) -> _SourceSnapshot:
    effective = _load_effective_manifest(report)
    effective_files, effective_directories = _validate_effective_source_tree(
        effective,
        verify_payload_hashes=verify_effective_payload_hashes,
    )
    _validate_stage_report(report, stage_root, effective)
    native = _load_native_manifest(native_root, effective, plan)
    _validate_closure_plan(
        plan,
        report,
        native,
        allow_terminal_models=allow_terminal_models,
    )
    stage_files, stage_directories = _scan_tree(
        stage_root, label="W3D input-stage tree"
    )
    stage_paths = [
        W3D_INPUT_STAGE_MANIFEST,
        *(item.staged_path for item in report.files),
    ]
    _validate_exact_tree(
        stage_files, stage_directories, stage_paths, label="W3D input-stage tree"
    )
    native_files, native_directories = _scan_tree(
        native_root, label="native texture corpus tree"
    )
    native_paths = ["manifest.json", *(item.path for item in native.outputs.values())]
    native_paths.sort(key=lambda value: (value.casefold(), value))
    _validate_exact_tree(
        native_files,
        native_directories,
        native_paths,
        label="native texture corpus tree",
    )
    return _SourceSnapshot(
        effective_files,
        effective_directories,
        stage_files,
        stage_directories,
        native_files,
        native_directories,
        effective,
        native,
    )


def _revalidate_sources(
    snapshot: _SourceSnapshot,
    stage_root: Path,
    native_root: Path,
    report: W3DInputStageReport,
    plan: W3DTextureClosurePlan,
    *,
    allow_terminal_models: bool,
) -> None:
    current = _snapshot_sources(
        stage_root,
        native_root,
        report,
        plan,
        allow_terminal_models=allow_terminal_models,
        verify_effective_payload_hashes=False,
    )
    if (
        current.effective_manifest.raw != snapshot.effective_manifest.raw
        or current.effective_manifest.identity != snapshot.effective_manifest.identity
        or current.native_manifest.raw != snapshot.native_manifest.raw
        or current.native_manifest.identity != snapshot.native_manifest.identity
        or set(current.stage_files) != set(snapshot.stage_files)
        or set(current.native_files) != set(snapshot.native_files)
        or set(current.effective_files) != set(snapshot.effective_files)
        or current.effective_directories != snapshot.effective_directories
        or current.stage_directories != snapshot.stage_directories
        or current.native_directories != snapshot.native_directories
    ):
        raise W3DJobRootError("W3D job-root sources changed during materialization")
    for before, after in (
        (snapshot.effective_files, current.effective_files),
        (snapshot.stage_files, current.stage_files),
        (snapshot.native_files, current.native_files),
    ):
        for key, item in before.items():
            if (
                after[key].relative_path != item.relative_path
                or after[key].identity != item.identity
            ):
                raise W3DJobRootError(
                    "W3D job-root source changed during materialization"
                )


def _copy_sources(
    snapshot: _SourceSnapshot,
    report: W3DInputStageReport,
    plan: W3DTextureClosurePlan,
    selection: _PayloadSelection,
    *,
    target_root: Path | None,
) -> None:
    del report
    for item in selection.stage_files:
        actual = snapshot.stage_files[item.staged_path.casefold()]
        target = None
        if target_root is not None:
            target = target_root.joinpath(*PurePosixPath(item.staged_path).parts)
        _copy_verified(
            actual,
            expected_size=item.source_bytes,
            expected_sha256=item.source_sha256,
            target=target,
            label="staged W3D",
        )
    checked_native: set[str] = set()
    del plan
    for instruction in selection.copy_instructions:
        key = instruction.source_output_path.casefold()
        actual = snapshot.native_files[key]
        native_output = snapshot.native_manifest.outputs[key]
        if key not in checked_native:
            _verify_native_evidence(
                actual, native_output, label="native texture source"
            )
            checked_native.add(key)
        target = None
        if target_root is not None:
            target = target_root.joinpath(
                *PurePosixPath(instruction.destination_path).parts
            )
        _copy_verified(
            actual,
            expected_size=instruction.source_output_bytes,
            expected_sha256=instruction.source_output_sha256,
            target=target,
            label="native texture source",
        )


def _verify_output(
    root: Path,
    spec: _JobSpec,
    report: W3DInputStageReport,
    plan: W3DTextureClosurePlan,
    native_root: Path,
    *,
    reused: bool,
) -> W3DJobRootReport:
    manifest_path = root.joinpath(*PurePosixPath(W3D_JOB_ROOT_MANIFEST).parts)
    document, raw, _ = _read_strict_json(
        manifest_path, label="W3D job-root manifest", ensure_ascii=False
    )
    if raw != spec.canonical_manifest or document != spec.document:
        raise W3DJobRootError("W3D job-root manifest mismatches the verified request")
    files, directories = _scan_tree(root, label="W3D job-root tree")
    declared = [W3D_JOB_ROOT_MANIFEST, *(item.destination_path for item in spec.files)]
    _validate_exact_tree(files, directories, declared, label="W3D job-root tree")
    rows: list[dict[str, object]] = []
    for item in spec.files:
        actual = files[item.destination_path.casefold()]
        _copy_verified(
            actual,
            expected_size=item.byte_length,
            expected_sha256=item.sha256,
            target=None,
            label="W3D job-root payload",
        )
        if item.kind == "texture-png":
            evidence = validate_native_output("png", actual.path)
            if (
                evidence.get("valid") is not True
                or evidence.get("size") != item.byte_length
                or evidence.get("sha256") != item.sha256
            ):
                raise W3DJobRootError("W3D job-root PNG failed independent backtest")
        rows.append(
            {
                "path": item.destination_path,
                "size": item.byte_length,
                "sha256": item.sha256,
            }
        )
    tree_sha256 = _inventory_sha256("openbfme.w3d-job-root-output-tree-v0", rows)
    if tree_sha256 != spec.output_tree_sha256:
        raise W3DJobRootError("W3D job-root tree SHA-256 is incoherent")
    return W3DJobRootReport(
        input_stage_root=Path(report.output_root),
        native_texture_corpus_root=native_root,
        output_root=root,
        manifest_path=manifest_path,
        files=spec.files,
        stage_identity_sha256=report.identity_sha256,
        stage_manifest_sha256=report.manifest_sha256,
        texture_closure_private_plan_sha256=plan.private_plan_sha256,
        texture_closure_evidence_sha256=plan.evidence_sha256,
        native_manifest_sha256=plan.native_manifest_sha256,
        native_texture_identity_sha256=plan.native_texture_identity_sha256,
        inventory_sha256=spec.inventory_sha256,
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
        raise W3DJobRootError("refused to remove an unowned W3D job-root path")
    _scan_tree(path, label="W3D job-root transaction tree")
    shutil.rmtree(path)


def _publish_stage(
    stage: Path,
    destination: Path,
    backup: Path,
    spec: _JobSpec,
    report: W3DInputStageReport,
    plan: W3DTextureClosurePlan,
    snapshot: _SourceSnapshot,
    stage_root: Path,
    native_root: Path,
    selection: _PayloadSelection,
) -> W3DJobRootReport:
    parent = destination.parent
    had_destination = os.path.lexists(destination)
    if had_destination:
        _scan_tree(destination, label="existing W3D job-root tree")
        try:
            os.replace(destination, backup)
        except OSError as exc:
            raise W3DJobRootError(
                "existing W3D job root could not enter transaction"
            ) from exc
    try:
        os.replace(stage, destination)
        result = _verify_output(
            destination, spec, report, plan, native_root, reused=False
        )
        # Keep the prior destination in its sibling backup until the inputs
        # have also survived a post-publication revalidation.  A source race
        # therefore rolls the publication back instead of merely reporting a
        # failure after the old verified output has already been discarded.
        _revalidate_sources(
            snapshot,
            stage_root,
            native_root,
            report,
            plan,
            allow_terminal_models=selection.materialize_accounted_jobs,
        )
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
            raise W3DJobRootError(
                "W3D job-root publish failed and rollback could not restore the prior output"
            ) from rollback_error
        raise W3DJobRootError(
            "W3D job-root publish failed; prior output was preserved"
        ) from publish_error
    if had_destination and os.path.lexists(backup):
        try:
            _remove_owned_tree(backup, parent, f".{destination.name}.backup-")
        except (OSError, W3DJobRootError):
            pass
    return result


def materialize_w3d_job_root(
    input_stage_report: W3DInputStageReport,
    texture_closure_plan: W3DTextureClosurePlan,
    input_stage_root: Path | str,
    native_texture_corpus_root: Path | str,
    output_root: Path | str,
    *,
    job_plan: W3DJobPlan | None = None,
    materialize_accounted_jobs: bool = False,
    preparation_preflight_report: W3DJobPreparationPreflightReport | None = None,
    preparation_fixed_point_report: W3DJobPreparationFixedPointReport | None = None,
    max_files: int | None = None,
    max_total_bytes: int | None = None,
    force: bool = False,
) -> W3DJobRootReport:
    """Build an exact ordinary-file W3D+PNG tree transactionally.

    Every W3D byte is checked against the independently reconstructed input
    stage seal.  Every selected PNG is checked against the native corpus
    manifest and rerun through the native PNG validator before copying.  A
    complete, source-bound texture closure is mandatory by default; nothing is
    truncated.  The explicit accounted mode instead materializes only proven
    planned-job dependencies while retaining every excluded source as sealed
    terminal evidence.
    """

    if not isinstance(force, bool):
        raise TypeError("W3D job-root force flag must be a boolean")
    if not isinstance(materialize_accounted_jobs, bool):
        raise TypeError("accounted W3D job-root flag must be a boolean")
    if not isinstance(input_stage_report, W3DInputStageReport):
        raise TypeError("W3D job root requires a W3DInputStageReport")
    if not isinstance(texture_closure_plan, W3DTextureClosurePlan):
        raise TypeError("W3D job root requires a W3DTextureClosurePlan")
    if materialize_accounted_jobs:
        if not isinstance(job_plan, W3DJobPlan):
            raise TypeError("accounted W3D job root requires a W3DJobPlan")
    else:
        if job_plan is not None:
            raise W3DJobRootError(
                "W3D job plan requires materialize_accounted_jobs=True"
            )
        if (
            preparation_preflight_report is not None
            or preparation_fixed_point_report is not None
        ):
            raise W3DJobRootError(
                "W3D preparation preflight requires materialize_accounted_jobs=True"
            )
    selected_max_files = _selected_limit(
        max_files, MAX_W3D_JOB_ROOT_FILES, label="file count"
    )
    selected_max_bytes = _selected_limit(
        max_total_bytes, MAX_W3D_JOB_ROOT_BYTES, label="total byte"
    )
    stage_root = _resolve_source_root(input_stage_root, label="W3D input-stage root")
    native_root = _resolve_source_root(
        native_texture_corpus_root, label="native texture corpus root"
    )
    effective_root = _resolve_source_root(
        input_stage_report.source_root, label="effective-assets root"
    )
    source_roots = (stage_root, native_root, effective_root)
    for index, first in enumerate(source_roots):
        if any(_paths_overlap(first, second) for second in source_roots[index + 1 :]):
            raise W3DJobRootError("W3D job-root source roots must not overlap")
    destination = _resolve_output_root(output_root, source_roots)
    snapshot = _snapshot_sources(
        stage_root,
        native_root,
        input_stage_report,
        texture_closure_plan,
        allow_terminal_models=materialize_accounted_jobs,
    )
    selection = (
        _accounted_payload_selection(
            job_plan,
            texture_closure_plan,
            input_stage_report,
            preparation_preflight_report,
            preparation_fixed_point_report,
        )
        if materialize_accounted_jobs and job_plan is not None
        else _PayloadSelection(
            input_stage_report.files,
            texture_closure_plan.copy_instructions,
            None,
            False,
            None,
            None,
        )
    )
    spec = _make_spec(
        input_stage_report,
        texture_closure_plan,
        selection,
        max_files=selected_max_files,
        max_total_bytes=selected_max_bytes,
    )

    if destination.is_dir() and not force:
        try:
            _copy_sources(
                snapshot,
                input_stage_report,
                texture_closure_plan,
                selection,
                target_root=None,
            )
            _revalidate_sources(
                snapshot,
                stage_root,
                native_root,
                input_stage_report,
                texture_closure_plan,
                allow_terminal_models=materialize_accounted_jobs,
            )
            result = _verify_output(
                destination,
                spec,
                input_stage_report,
                texture_closure_plan,
                native_root,
                reused=True,
            )
            _revalidate_sources(
                snapshot,
                stage_root,
                native_root,
                input_stage_report,
                texture_closure_plan,
                allow_terminal_models=materialize_accounted_jobs,
            )
            return result
        except W3DJobRootError as exc:
            raise W3DJobRootReuseError(
                f"existing W3D job root failed verification: {exc}"
            ) from exc

    parent = destination.parent
    token = uuid.uuid4().hex
    stage = parent / f".{destination.name}.staging-{token}"
    backup = parent / f".{destination.name}.backup-{token}"
    try:
        stage.mkdir()
    except OSError as exc:
        raise W3DJobRootError(
            "W3D job-root staging directory cannot be created"
        ) from exc
    try:
        _copy_sources(
            snapshot,
            input_stage_report,
            texture_closure_plan,
            selection,
            target_root=stage,
        )
        metadata = stage / _METADATA_DIRECTORY
        metadata.mkdir()
        manifest_path = stage.joinpath(*PurePosixPath(W3D_JOB_ROOT_MANIFEST).parts)
        with manifest_path.open("xb") as stream:
            stream.write(spec.canonical_manifest)
        _revalidate_sources(
            snapshot,
            stage_root,
            native_root,
            input_stage_report,
            texture_closure_plan,
            allow_terminal_models=materialize_accounted_jobs,
        )
        _verify_output(
            stage,
            spec,
            input_stage_report,
            texture_closure_plan,
            native_root,
            reused=False,
        )
        result = _publish_stage(
            stage,
            destination,
            backup,
            spec,
            input_stage_report,
            texture_closure_plan,
            snapshot,
            stage_root,
            native_root,
            selection,
        )
        return result
    finally:
        if os.path.lexists(stage):
            _remove_owned_tree(stage, parent, f".{destination.name}.staging-")
        if os.path.lexists(backup) and not os.path.lexists(destination):
            try:
                os.replace(backup, destination)
            except OSError as exc:
                raise W3DJobRootError(
                    "W3D job-root cleanup could not restore the prior output"
                ) from exc


build_w3d_job_root = materialize_w3d_job_root


__all__ = [
    "MAX_DOCUMENT_BYTES",
    "MAX_W3D_JOB_ROOT_BYTES",
    "MAX_W3D_JOB_ROOT_FILES",
    "W3D_JOB_ROOT_MANIFEST",
    "W3D_JOB_ROOT_SCHEMA",
    "W3D_JOB_ROOT_SCHEMA_VERSION",
    "W3DJobRootError",
    "W3DJobRootFile",
    "W3DJobRootLimitError",
    "W3DJobRootReport",
    "W3DJobRootReuseError",
    "build_w3d_job_root",
    "materialize_w3d_job_root",
]
