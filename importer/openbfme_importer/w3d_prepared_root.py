"""Distinct, source-bound execution roots for prepared W3D jobs.

The retail job-root publication is immutable evidence.  Model preparation is
therefore applied only to an independently copied sibling tree.  This module
reconstructs the source publication seals, proves the copy and every declared
rewrite, and publishes a second canonical manifest for the exact tree accepted
by the Blender runner.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat as stat_module
from typing import Iterable, Mapping, Sequence
import uuid

from .w3d_job_planner import (
    SECONDARY_SKIN_PREPARATION,
    W3DJobBatch,
    W3DJobPlan,
    w3d_job_resolution_contract_is_valid,
)
from .w3d_job_preparation import (
    W3D_JOB_PREPARATION_SCHEMA,
    W3D_JOB_PREPARATION_VERSION,
    W3DJobPreparationError,
    attest_w3d_job_preparations,
)
from .w3d_job_root import (
    W3D_JOB_ROOT_MANIFEST,
    W3D_JOB_ROOT_SCHEMA,
    W3DJobRootFile,
    W3DJobRootReport,
)


W3D_PREPARED_ROOT_SCHEMA = "openbfme.w3d-prepared-root"
W3D_PREPARED_ROOT_SCHEMA_VERSION = 0
W3D_PREPARED_ROOT_MANIFEST = ".openbfme/w3d-prepared-root.json"

MAX_PREPARED_MANIFEST_BYTES = 64 * 1024 * 1024
HASH_BLOCK_BYTES = 1024 * 1024

_SHA256_CHARACTERS = frozenset("0123456789abcdef")
_SOURCE_MANIFEST_SCHEMA_VERSIONS = frozenset({0, 1, 2})
_ACCOUNTED_MATERIALIZATION_POLICY = "accounted-planned-jobs-v1"


class W3DPreparedRootError(ValueError):
    """A source publication, plan, prepared tree, or transaction failed closed."""


class W3DPreparedRootReuseError(W3DPreparedRootError):
    """An existing prepared root did not exactly match the current request."""


@dataclass(frozen=True, slots=True)
class W3DPreparedFile:
    """Private exact-file evidence retained only in the private manifest."""

    kind: str
    relative_path: str
    source_bytes: int
    source_sha256: str
    prepared_bytes: int
    prepared_sha256: str
    changed: bool

    def private(self) -> dict[str, object]:
        return {
            "kind": self.kind,
            "path": self.relative_path,
            "sourceBytes": self.source_bytes,
            "sourceSha256": self.source_sha256,
            "preparedBytes": self.prepared_bytes,
            "preparedSha256": self.prepared_sha256,
            "changed": self.changed,
        }


@dataclass(frozen=True, slots=True)
class W3DPreparationEvidence:
    """Path-free proof summary for one declared model preparation."""

    ordinal: int
    job_id: str
    kind: str
    model_source_sha256: str
    hierarchy_source_sha256: str
    prepared_model_sha256: str
    evidence_sha256: str
    source_bytes: int
    prepared_bytes: int

    def neutral(self) -> dict[str, object]:
        return {
            "ordinal": self.ordinal,
            "jobId": self.job_id,
            "kind": self.kind,
            "modelSourceSha256": self.model_source_sha256,
            "hierarchySourceSha256": self.hierarchy_source_sha256,
            "preparedModelSha256": self.prepared_model_sha256,
            "evidenceSha256": self.evidence_sha256,
            "sourceBytes": self.source_bytes,
            "preparedBytes": self.prepared_bytes,
        }


@dataclass(frozen=True, slots=True)
class W3DPreparedRootBinding:
    """Minimal path-free evidence embedded by the batch runner."""

    execute_accounted_jobs: bool
    source_identity_sha256: str
    source_manifest_sha256: str
    source_request_sha256: str
    source_tree_sha256: str
    input_private_plan_sha256: str
    input_plan_evidence_sha256: str
    prepared_private_plan_sha256: str
    prepared_plan_evidence_sha256: str
    inventory_sha256: str
    payload_tree_sha256: str
    runner_tree_sha256: str
    request_sha256: str
    identity_sha256: str
    manifest_sha256: str

    def neutral(self) -> dict[str, object]:
        return {
            "schema": "openbfme.w3d-prepared-root-runner-binding",
            "schemaVersion": 0,
            "policy": {"executeAccountedJobs": self.execute_accounted_jobs},
            "sourceJobRoot": {
                "identitySha256": self.source_identity_sha256,
                "manifestSha256": self.source_manifest_sha256,
                "requestSha256": self.source_request_sha256,
                "outputTreeSha256": self.source_tree_sha256,
            },
            "plans": {
                "inputPrivatePlanSha256": self.input_private_plan_sha256,
                "inputEvidenceSha256": self.input_plan_evidence_sha256,
                "preparedPrivatePlanSha256": self.prepared_private_plan_sha256,
                "preparedEvidenceSha256": self.prepared_plan_evidence_sha256,
            },
            "hashes": {
                "inventorySha256": self.inventory_sha256,
                "payloadTreeSha256": self.payload_tree_sha256,
                "runnerTreeSha256": self.runner_tree_sha256,
                "requestSha256": self.request_sha256,
                "identitySha256": self.identity_sha256,
                "manifestSha256": self.manifest_sha256,
            },
        }


@dataclass(frozen=True, slots=True)
class W3DPreparedRootReport:
    """Local handles and canonical evidence for a prepared execution tree."""

    source_job_root_report: W3DJobRootReport
    input_plan: W3DJobPlan
    prepared_plan: W3DJobPlan
    output_root: Path
    manifest_path: Path
    files: tuple[W3DPreparedFile, ...]
    preparations: tuple[W3DPreparationEvidence, ...]
    execute_accounted_jobs: bool
    inventory_sha256: str
    payload_tree_sha256: str
    runner_tree_sha256: str
    request_sha256: str
    identity_sha256: str
    manifest_sha256: str
    reused: bool

    @property
    def file_count(self) -> int:
        return len(self.files)

    @property
    def changed_model_count(self) -> int:
        return len(self.preparations)

    def runner_binding(self) -> W3DPreparedRootBinding:
        source = self.source_job_root_report
        return W3DPreparedRootBinding(
            execute_accounted_jobs=self.execute_accounted_jobs,
            source_identity_sha256=source.identity_sha256,
            source_manifest_sha256=source.manifest_sha256,
            source_request_sha256=source.request_sha256,
            source_tree_sha256=source.output_tree_sha256,
            input_private_plan_sha256=self.input_plan.private_plan_sha256,
            input_plan_evidence_sha256=self.input_plan.evidence_sha256,
            prepared_private_plan_sha256=self.prepared_plan.private_plan_sha256,
            prepared_plan_evidence_sha256=self.prepared_plan.evidence_sha256,
            inventory_sha256=self.inventory_sha256,
            payload_tree_sha256=self.payload_tree_sha256,
            runner_tree_sha256=self.runner_tree_sha256,
            request_sha256=self.request_sha256,
            identity_sha256=self.identity_sha256,
            manifest_sha256=self.manifest_sha256,
        )

    def neutral(self) -> dict[str, object]:
        return {
            "schema": "openbfme.w3d-prepared-root-report",
            "schemaVersion": 0,
            "summary": {
                "fileCount": self.file_count,
                "preparationCount": len(self.preparations),
                "changedModelCount": self.changed_model_count,
                "rawSourceUnchanged": True,
                "texturesUnchanged": True,
                "animationsUnchanged": True,
                "nonPreparedW3DsUnchanged": True,
                "undeclaredFilesUnchanged": True,
                "declaredPreparedModelsExact": True,
                "glbConversionComplete": False,
                "renderParityProven": False,
            },
            "binding": self.runner_binding().neutral(),
            "preparations": [item.neutral() for item in self.preparations],
            "reused": self.reused,
        }

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class _FileSnapshot:
    relative_path: str
    byte_length: int
    sha256: str


@dataclass(frozen=True, slots=True)
class _ValidatedSource:
    root: Path
    document: Mapping[str, object]
    manifest_bytes: bytes
    files: tuple[_FileSnapshot, ...]
    directories: frozenset[str]


@dataclass(frozen=True, slots=True)
class _PreparedSpec:
    files: tuple[W3DPreparedFile, ...]
    preparations: tuple[W3DPreparationEvidence, ...]
    inventory_sha256: str
    payload_tree_sha256: str
    request_sha256: str
    identity_sha256: str
    document: Mapping[str, object]
    manifest_bytes: bytes


class _DuplicateJsonKey(ValueError):
    pass


def _canonical_json_bytes(
    value: object, *, pretty: bool = False, newline: bool = True
) -> bytes:
    options: dict[str, object] = {
        "allow_nan": False,
        "ensure_ascii": False,
        "sort_keys": True,
    }
    if pretty:
        options["indent"] = 2
    else:
        options["separators"] = (",", ":")
    return (json.dumps(value, **options) + ("\n" if newline else "")).encode("utf-8")


def _canonical_sha256(value: object, *, newline: bool = True) -> str:
    return hashlib.sha256(_canonical_json_bytes(value, newline=newline)).hexdigest()


def _source_canonical_sha256(value: object, *, newline: bool = True) -> str:
    suffix = "\n" if newline else ""
    payload = (
        json.dumps(
            value,
            allow_nan=False,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )
        + suffix
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str) and len(value) == 64 and set(value) <= _SHA256_CHARACTERS
    )


def _is_int(value: object, *, minimum: int = 0) -> bool:
    return type(value) is int and value >= minimum


def _object_without_duplicate_keys(
    pairs: Sequence[tuple[str, object]],
) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateJsonKey(key)
        result[key] = value
    return result


def _reject_json_constant(value: str) -> object:
    raise ValueError(value)


def _is_link_like(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    if path.is_symlink() or bool(is_junction and is_junction()):
        return True
    try:
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
    except OSError:
        return False
    return bool(attributes & getattr(stat_module, "FILE_ATTRIBUTE_REPARSE_POINT", 0))


def _assert_no_link_chain(path: Path) -> None:
    absolute = path.expanduser().absolute()
    for candidate in reversed((absolute, *absolute.parents)):
        if os.path.lexists(candidate) and _is_link_like(candidate):
            raise W3DPreparedRootError("declared filesystem path contains a link")


def _ordinary_root(value: Path | str, *, label: str) -> Path:
    candidate = Path(value).expanduser()
    _assert_no_link_chain(candidate)
    try:
        result = candidate.resolve(strict=True)
    except OSError:
        raise W3DPreparedRootError(f"{label} is unavailable") from None
    if not result.is_dir() or _is_link_like(result) or not result.name:
        raise W3DPreparedRootError(f"{label} must be an ordinary directory")
    return result


def _paths_overlap(first: Path, second: Path) -> bool:
    try:
        common = Path(os.path.commonpath((first, second)))
    except ValueError:
        return False
    return common == first or common == second


def _safe_relative(value: object) -> str:
    if (
        not isinstance(value, str)
        or not value
        or len(value) > 512
        or "\\" in value
        or "\x00" in value
        or ":" in value
    ):
        raise W3DPreparedRootError("manifest contains an unsafe relative path")
    relative = PurePosixPath(value)
    if (
        relative.is_absolute()
        or relative.as_posix() != value
        or any(part in {"", ".", ".."} for part in relative.parts)
    ):
        raise W3DPreparedRootError("manifest contains an unsafe relative path")
    return value


def _read_file(
    path: Path, *, label: str, capture: bool = True
) -> tuple[bytes, _FileSnapshot]:
    _assert_no_link_chain(path)
    if _is_link_like(path):
        raise W3DPreparedRootError(f"{label} is linked")
    try:
        before = path.stat(follow_symlinks=False)
        if (
            not stat_module.S_ISREG(before.st_mode)
            or getattr(before, "st_nlink", 1) != 1
        ):
            raise W3DPreparedRootError(f"{label} is not an independent file")
        digest = hashlib.sha256()
        payload = bytearray() if capture else None
        with path.open("rb") as stream:
            opened = os.fstat(stream.fileno())
            if (
                opened.st_dev != before.st_dev
                or opened.st_ino != before.st_ino
                or opened.st_size != before.st_size
            ):
                raise W3DPreparedRootError(f"{label} changed while opening")
            while True:
                block = stream.read(HASH_BLOCK_BYTES)
                if not block:
                    break
                if payload is not None:
                    payload.extend(block)
                digest.update(block)
            after_open = os.fstat(stream.fileno())
        after = path.stat(follow_symlinks=False)
    except W3DPreparedRootError:
        raise
    except OSError:
        raise W3DPreparedRootError(f"{label} could not be read") from None
    identities = {
        (item.st_dev, item.st_ino, item.st_size, item.st_mtime_ns)
        for item in (before, after_open, after)
    }
    if (
        len(identities) != 1
        or (payload is not None and len(payload) != before.st_size)
        or _is_link_like(path)
    ):
        raise W3DPreparedRootError(f"{label} changed while reading")
    return (
        bytes(payload or b""),
        _FileSnapshot("", int(before.st_size), digest.hexdigest()),
    )


def _scan_tree(
    root: Path, *, label: str
) -> tuple[tuple[_FileSnapshot, ...], frozenset[str]]:
    _assert_no_link_chain(root)
    pending = [root]
    files: dict[str, _FileSnapshot] = {}
    directories: dict[str, str] = {}
    while pending:
        current = pending.pop()
        try:
            entries = list(os.scandir(current))
        except OSError:
            raise W3DPreparedRootError(f"{label} could not be scanned") from None
        for entry in entries:
            path = Path(entry.path)
            relative = path.relative_to(root).as_posix()
            folded = relative.casefold()
            if entry.is_symlink() or _is_link_like(path):
                raise W3DPreparedRootError(f"{label} contains a link")
            if folded in files or folded in directories:
                raise W3DPreparedRootError(f"{label} contains case-colliding paths")
            try:
                if entry.is_dir(follow_symlinks=False):
                    directories[folded] = relative
                    pending.append(path)
                elif entry.is_file(follow_symlinks=False):
                    _, seal = _read_file(path, label=f"{label} file", capture=False)
                    files[folded] = replace(seal, relative_path=relative)
                else:
                    raise W3DPreparedRootError(
                        f"{label} contains an unsupported filesystem entry"
                    )
            except OSError:
                raise W3DPreparedRootError(f"{label} entry could not be read") from None
    ordered = tuple(
        sorted(
            files.values(),
            key=lambda item: (item.relative_path.casefold(), item.relative_path),
        )
    )
    return ordered, frozenset(directories.values())


def _expected_directories(paths: Iterable[str]) -> frozenset[str]:
    result: set[str] = set()
    for value in paths:
        parts = PurePosixPath(value).parts
        for index in range(1, len(parts)):
            result.add(PurePosixPath(*parts[:index]).as_posix())
    return frozenset(result)


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


def _runner_tree_sha256(files: Sequence[_FileSnapshot]) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.w3d-pinned-tree",
            "schemaVersion": 0,
            "files": [
                {
                    "path": item.relative_path,
                    "bytes": item.byte_length,
                    "sha256": item.sha256,
                }
                for item in files
            ],
        }
    )


def _read_canonical_manifest(
    path: Path, *, source: bool
) -> tuple[dict[str, object], bytes]:
    payload, seal = _read_file(path, label="W3D root manifest")
    if seal.byte_length > MAX_PREPARED_MANIFEST_BYTES:
        raise W3DPreparedRootError("W3D root manifest exceeds its size bound")
    try:
        document = json.loads(
            payload.decode("utf-8"),
            object_pairs_hook=_object_without_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError):
        raise W3DPreparedRootError("W3D root manifest is invalid JSON") from None
    if not isinstance(document, dict):
        raise W3DPreparedRootError("W3D root manifest is not an object")
    expected = _canonical_json_bytes(document, pretty=True)
    if payload != expected:
        raise W3DPreparedRootError("W3D root manifest is not canonical")
    if source and document.get("schema") != W3D_JOB_ROOT_SCHEMA:
        raise W3DPreparedRootError("source W3D job-root schema is invalid")
    if not source and document.get("schema") != W3D_PREPARED_ROOT_SCHEMA:
        raise W3DPreparedRootError("prepared W3D root schema is invalid")
    return document, payload


def _source_file_rows(report: W3DJobRootReport) -> tuple[dict[str, object], ...]:
    if type(report.files) is not tuple or any(
        type(item) is not W3DJobRootFile for item in report.files
    ):
        raise W3DPreparedRootError("source W3D job-root file report is invalid")
    for item in report.files:
        if (
            item.kind not in {"w3d", "texture-png"}
            or not _is_int(item.byte_length)
            or not _is_sha256(item.sha256)
            or (item.kind == "w3d" and item.instruction_id is not None)
            or (
                item.kind == "texture-png"
                and (
                    not isinstance(item.instruction_id, str) or not item.instruction_id
                )
            )
        ):
            raise W3DPreparedRootError("source W3D job-root file evidence is invalid")
        _safe_relative(item.source_path)
        _safe_relative(item.destination_path)
    rows = tuple(item.private() for item in report.files)
    paths = [_safe_relative(row.get("destinationPath")) for row in rows]
    if paths != sorted(paths, key=lambda value: (value.casefold(), value)):
        raise W3DPreparedRootError("source W3D job-root files are not canonical")
    if len({value.casefold() for value in paths}) != len(paths):
        raise W3DPreparedRootError("source W3D job-root files collide")
    return rows


def _validate_source_report(report: W3DJobRootReport) -> _ValidatedSource:
    if type(report) is not W3DJobRootReport:
        raise TypeError("source_job_root_report must be a W3DJobRootReport")
    root = _ordinary_root(report.output_root, label="source W3D job root")
    manifest_path = root.joinpath(*PurePosixPath(W3D_JOB_ROOT_MANIFEST).parts)
    try:
        reported_manifest = Path(report.manifest_path).resolve(strict=True)
    except OSError:
        raise W3DPreparedRootError(
            "source W3D job-root manifest is unavailable"
        ) from None
    if reported_manifest != manifest_path.resolve(strict=True):
        raise W3DPreparedRootError("source W3D job-root manifest path is incoherent")
    document, raw = _read_canonical_manifest(manifest_path, source=True)
    version = document.get("schemaVersion")
    if version not in _SOURCE_MANIFEST_SCHEMA_VERSIONS:
        raise W3DPreparedRootError("source W3D job-root version is unsupported")
    expected_keys = {
        "schema",
        "schemaVersion",
        "bindings",
        "limits",
        "summary",
        "files",
        "inventorySha256",
        "outputTreeSha256",
        "requestSha256",
        "identitySha256",
    }
    if version in {1, 2}:
        expected_keys.add("accounting")
    if set(document) != expected_keys:
        raise W3DPreparedRootError("source W3D job-root manifest fields are invalid")
    bindings = document.get("bindings")
    limits = document.get("limits")
    summary = document.get("summary")
    if not all(isinstance(value, dict) for value in (bindings, limits, summary)):
        raise W3DPreparedRootError("source W3D job-root manifest evidence is invalid")
    assert isinstance(bindings, dict)
    assert isinstance(limits, dict)
    assert isinstance(summary, dict)
    rows = _source_file_rows(report)
    if document.get("files") != list(rows):
        raise W3DPreparedRootError("source W3D job-root file evidence mismatches")
    required_bindings = {
        "stageIdentitySha256": report.stage_identity_sha256,
        "stageManifestSha256": report.stage_manifest_sha256,
        "textureClosurePrivatePlanSha256": report.texture_closure_private_plan_sha256,
        "textureClosureEvidenceSha256": report.texture_closure_evidence_sha256,
        "nativeManifestSha256": report.native_manifest_sha256,
        "nativeTextureIdentitySha256": report.native_texture_identity_sha256,
    }
    if any(bindings.get(key) != value for key, value in required_bindings.items()):
        raise W3DPreparedRootError("source W3D job-root report bindings mismatch")
    if any(
        not _is_sha256(value) for value in bindings.values() if isinstance(value, str)
    ):
        raise W3DPreparedRootError("source W3D job-root binding hash is invalid")
    expected_limits = {
        "hardMaxFiles": limits.get("hardMaxFiles"),
        "hardMaxTotalBytes": limits.get("hardMaxTotalBytes"),
        "maxFiles": report.max_files,
        "maxTotalBytes": report.max_total_bytes,
    }
    if limits != expected_limits or any(
        not _is_int(value) for value in expected_limits.values()
    ):
        raise W3DPreparedRootError("source W3D job-root limits mismatch")
    expected_summary = {
        "fileCount": len(report.files),
        "totalBytes": sum(item.byte_length for item in report.files),
        "w3dFileCount": sum(item.kind == "w3d" for item in report.files),
        "textureFileCount": sum(item.kind == "texture-png" for item in report.files),
        "glbConversionComplete": False,
        "renderParityProven": False,
    }
    if summary != expected_summary:
        raise W3DPreparedRootError("source W3D job-root summary mismatch")
    if not isinstance(report.reused, bool):
        raise W3DPreparedRootError("source W3D job-root reuse evidence is invalid")
    inventory = _source_canonical_sha256(
        {
            "schema": "openbfme.w3d-job-root-inventory",
            "schemaVersion": 0,
            "files": list(rows),
        },
        newline=False,
    )
    tree_rows = [
        {
            "path": item.destination_path,
            "size": item.byte_length,
            "sha256": item.sha256,
        }
        for item in report.files
    ]
    tree = _inventory_sha256("openbfme.w3d-job-root-output-tree-v0", tree_rows)
    request_basis: dict[str, object] = {
        "schema": "openbfme.w3d-job-root-request",
        "schemaVersion": 0,
        "bindings": bindings,
        "inventorySha256": inventory,
        "outputTreeSha256": tree,
        "summary": summary,
        "limits": limits,
    }
    if version in {1, 2}:
        request_basis = {
            **request_basis,
            "schema": "openbfme.w3d-job-root-accounted-request",
            "schemaVersion": version,
            "accounting": document.get("accounting"),
        }
    request = _source_canonical_sha256(request_basis, newline=False)
    identity_basis = dict(document)
    identity = identity_basis.pop("identitySha256", None)
    expected_identity = _source_canonical_sha256(identity_basis, newline=False)
    manifest_sha256 = hashlib.sha256(raw).hexdigest()
    expected_hashes = (
        (document.get("inventorySha256"), inventory, report.inventory_sha256),
        (document.get("outputTreeSha256"), tree, report.output_tree_sha256),
        (document.get("requestSha256"), request, report.request_sha256),
        (identity, expected_identity, report.identity_sha256),
        (manifest_sha256, report.manifest_sha256, report.manifest_sha256),
    )
    if any(
        first != second or second != third for first, second, third in expected_hashes
    ):
        raise W3DPreparedRootError("source W3D job-root hash evidence mismatch")
    files, directories = _scan_tree(root, label="source W3D job-root tree")
    declared = [
        W3D_JOB_ROOT_MANIFEST,
        *(item.destination_path for item in report.files),
    ]
    if [item.relative_path for item in files] != sorted(
        declared, key=lambda value: (value.casefold(), value)
    ) or directories != _expected_directories(declared):
        raise W3DPreparedRootError("source W3D job-root tree is not exact")
    by_path = {item.relative_path: item for item in files}
    manifest_seal = by_path[W3D_JOB_ROOT_MANIFEST]
    if manifest_seal.sha256 != manifest_sha256 or manifest_seal.byte_length != len(raw):
        raise W3DPreparedRootError("source W3D job-root manifest changed")
    for item in report.files:
        actual = by_path.get(item.destination_path)
        if (
            actual is None
            or actual.byte_length != item.byte_length
            or actual.sha256 != item.sha256
        ):
            raise W3DPreparedRootError("source W3D job-root payload mismatch")
    return _ValidatedSource(root, document, raw, files, directories)


def _validate_input_plan(
    plan: W3DJobPlan,
    source: _ValidatedSource,
    report: W3DJobRootReport,
    *,
    execute_accounted_jobs: bool,
) -> None:
    if type(plan) is not W3DJobPlan:
        raise TypeError("plan must be a W3DJobPlan")
    if not all(
        type(value) is tuple for value in (plan.jobs, plan.batches, plan.terminals)
    ):
        raise W3DPreparedRootError("input W3D job plan is not immutable")
    if any(not w3d_job_resolution_contract_is_valid(job) for job in plan.jobs):
        raise W3DPreparedRootError("input W3D job resolution contract is invalid")
    try:
        evidence = _canonical_sha256(plan.evidence_hash_basis())
    except (AttributeError, TypeError, ValueError):
        raise W3DPreparedRootError("input W3D job plan evidence is malformed") from None
    if evidence != plan.evidence_sha256 or not _is_sha256(plan.private_plan_sha256):
        raise W3DPreparedRootError("input W3D job plan seal is incoherent")
    if any(
        job.prepared_model_sha256 is not None
        or job.model_preparation_evidence_sha256 is not None
        for job in plan.jobs
    ):
        raise W3DPreparedRootError("input W3D job plan is already prepared")
    bindings = source.document["bindings"]
    assert isinstance(bindings, dict)
    if (
        bindings.get("catalogInputSha256") != plan.catalog_input_sha256
        or bindings.get("catalogMetadataSha256") != plan.catalog_metadata_sha256
    ):
        raise W3DPreparedRootError("input W3D plan and job-root catalog seals mismatch")
    version = source.document["schemaVersion"]
    if execute_accounted_jobs:
        accounting = source.document.get("accounting")
        if version not in {1, 2} or not isinstance(accounting, dict):
            raise W3DPreparedRootError(
                "accounted preparation requires an accounted job root"
            )
        expected_accounting_keys = {
            "materializationPolicy",
            "jobPlan",
            "textureClosurePlan",
            "selectedInstructionIds",
            "selectedW3DSourceIds",
        }
        if version == 2:
            expected_accounting_keys.add("preparationFixedPoint")
        if set(accounting) != expected_accounting_keys:
            raise W3DPreparedRootError("source accounted job-root evidence is invalid")
        fixed_point = accounting.get("preparationFixedPoint")
        if version == 2 and (
            not isinstance(fixed_point, dict)
            or set(fixed_point) != {"evidenceSha256", "iterationCount"}
            or not _is_sha256(fixed_point.get("evidenceSha256"))
            or not _is_int(fixed_point.get("iterationCount"), minimum=1)
        ):
            raise W3DPreparedRootError(
                "source accounted job-root fixed-point evidence is invalid"
            )
        if (
            accounting.get("materializationPolicy") != _ACCOUNTED_MATERIALIZATION_POLICY
            or accounting.get("jobPlan") != plan.neutral()
            or not isinstance(accounting.get("textureClosurePlan"), dict)
            or not isinstance(accounting.get("selectedInstructionIds"), list)
            or not isinstance(accounting.get("selectedW3DSourceIds"), list)
        ):
            raise W3DPreparedRootError(
                "source accounted job-root plan binding mismatch"
            )
    elif version != 0:
        raise W3DPreparedRootError("strict preparation requires a strict job root")
    by_path = {
        item.destination_path: item for item in report.files if item.kind == "w3d"
    }
    for job in plan.jobs:
        if (
            type(job.animations) is not tuple
            or type(job.animation_source_sha256s) is not tuple
            or len(job.animations) != len(job.animation_source_sha256s)
        ):
            raise W3DPreparedRootError("planned W3D animation evidence is invalid")
        roles = [(job.model, job.model_source_sha256)]
        roles.extend(zip(job.animations, job.animation_source_sha256s, strict=True))
        if job.hierarchy is not None and job.hierarchy != job.model:
            roles.append((job.hierarchy, job.hierarchy_source_sha256))
        for path, expected_sha256 in roles:
            item = by_path.get(path)
            if item is None or item.sha256 != expected_sha256:
                raise W3DPreparedRootError(
                    "planned W3D input mismatches the raw job root"
                )


def _preparation_rows(
    input_plan: W3DJobPlan,
    prepared_plan: W3DJobPlan,
    source_by_path: Mapping[str, _FileSnapshot],
    prepared_by_path: Mapping[str, _FileSnapshot],
) -> tuple[W3DPreparationEvidence, ...]:
    if len(input_plan.jobs) != len(prepared_plan.jobs):
        raise W3DPreparedRootError("prepared W3D plan job cardinality changed")
    rows: list[W3DPreparationEvidence] = []
    for ordinal, (before, after) in enumerate(
        zip(input_plan.jobs, prepared_plan.jobs, strict=True)
    ):
        if before.model_preparation is None:
            if after != before:
                raise W3DPreparedRootError("preparation changed an undeclared W3D job")
            continue
        if before.model_preparation != SECONDARY_SKIN_PREPARATION:
            raise W3DPreparedRootError("input W3D preparation kind is unsupported")
        if (
            after.model_preparation != before.model_preparation
            or not _is_sha256(after.prepared_model_sha256)
            or not _is_sha256(after.model_preparation_evidence_sha256)
            or after.prepared_model_sha256 == before.model_source_sha256
        ):
            raise W3DPreparedRootError("prepared W3D job attestation is incomplete")
        source_file = source_by_path.get(before.model)
        prepared_file = prepared_by_path.get(before.model)
        if (
            source_file is None
            or prepared_file is None
            or source_file.sha256 != before.model_source_sha256
            or prepared_file.sha256 != after.prepared_model_sha256
        ):
            raise W3DPreparedRootError("prepared W3D model bytes mismatch their seal")
        assert before.hierarchy_source_sha256 is not None
        rows.append(
            W3DPreparationEvidence(
                ordinal=ordinal,
                job_id=before.job_id,
                kind=before.model_preparation,
                model_source_sha256=before.model_source_sha256,
                hierarchy_source_sha256=before.hierarchy_source_sha256,
                prepared_model_sha256=after.prepared_model_sha256,
                evidence_sha256=after.model_preparation_evidence_sha256,
                source_bytes=source_file.byte_length,
                prepared_bytes=prepared_file.byte_length,
            )
        )
    return tuple(rows)


def _request_sha256(
    plan: W3DJobPlan,
    report: W3DJobRootReport,
    *,
    execute_accounted_jobs: bool,
) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.w3d-prepared-root-request",
            "schemaVersion": 0,
            "policy": {"executeAccountedJobs": execute_accounted_jobs},
            "sourceJobRoot": {
                "identitySha256": report.identity_sha256,
                "manifestSha256": report.manifest_sha256,
                "requestSha256": report.request_sha256,
                "outputTreeSha256": report.output_tree_sha256,
            },
            "inputPlan": {
                "privatePlanSha256": plan.private_plan_sha256,
                "evidenceSha256": plan.evidence_sha256,
            },
        }
    )


def _make_spec(
    input_plan: W3DJobPlan,
    prepared_plan: W3DJobPlan,
    source_report: W3DJobRootReport,
    source_files: Sequence[_FileSnapshot],
    prepared_files: Sequence[_FileSnapshot],
    *,
    execute_accounted_jobs: bool,
) -> _PreparedSpec:
    source_by_path = {item.relative_path: item for item in source_files}
    prepared_by_path = {item.relative_path: item for item in prepared_files}
    if source_by_path.keys() != prepared_by_path.keys():
        raise W3DPreparedRootError("prepared root changed the source file inventory")
    preparations = _preparation_rows(
        input_plan, prepared_plan, source_by_path, prepared_by_path
    )
    changed_paths = {input_plan.jobs[item.ordinal].model for item in preparations}
    source_kinds = {item.destination_path: item.kind for item in source_report.files}
    source_kinds[W3D_JOB_ROOT_MANIFEST] = "source-job-root-manifest"
    files: list[W3DPreparedFile] = []
    for path in sorted(source_by_path, key=lambda value: (value.casefold(), value)):
        source = source_by_path[path]
        prepared = prepared_by_path[path]
        changed = source != prepared
        if changed != (path in changed_paths):
            raise W3DPreparedRootError("prepared root changed an undeclared file")
        files.append(
            W3DPreparedFile(
                kind=source_kinds[path],
                relative_path=path,
                source_bytes=source.byte_length,
                source_sha256=source.sha256,
                prepared_bytes=prepared.byte_length,
                prepared_sha256=prepared.sha256,
                changed=changed,
            )
        )
    private_rows = [item.private() for item in files]
    inventory = _canonical_sha256(
        {
            "schema": "openbfme.w3d-prepared-root-private-inventory",
            "schemaVersion": 0,
            "files": private_rows,
        },
        newline=False,
    )
    tree_rows = [
        {
            "path": item.relative_path,
            "size": item.prepared_bytes,
            "sha256": item.prepared_sha256,
        }
        for item in files
    ]
    payload_tree = _inventory_sha256(
        "openbfme.w3d-prepared-root-payload-tree-v0", tree_rows
    )
    request = _request_sha256(
        input_plan,
        source_report,
        execute_accounted_jobs=execute_accounted_jobs,
    )
    public_basis: dict[str, object] = {
        "schema": W3D_PREPARED_ROOT_SCHEMA,
        "schemaVersion": W3D_PREPARED_ROOT_SCHEMA_VERSION,
        "policy": {"executeAccountedJobs": execute_accounted_jobs},
        "sourceJobRoot": {
            "identitySha256": source_report.identity_sha256,
            "manifestSha256": source_report.manifest_sha256,
            "requestSha256": source_report.request_sha256,
            "outputTreeSha256": source_report.output_tree_sha256,
        },
        "plans": {
            "inputPrivatePlanSha256": input_plan.private_plan_sha256,
            "inputEvidenceSha256": input_plan.evidence_sha256,
            "preparedPrivatePlanSha256": prepared_plan.private_plan_sha256,
            "preparedEvidenceSha256": prepared_plan.evidence_sha256,
        },
        "summary": {
            "fileCount": len(files),
            "preparationCount": len(preparations),
            "changedModelCount": len(preparations),
            "rawSourceUnchanged": True,
            "texturesUnchanged": True,
            "animationsUnchanged": True,
            "nonPreparedW3DsUnchanged": True,
            "undeclaredFilesUnchanged": True,
            "declaredPreparedModelsExact": True,
            "glbConversionComplete": False,
            "renderParityProven": False,
        },
        "preparations": [item.neutral() for item in preparations],
        "hashes": {
            "inventorySha256": inventory,
            "payloadTreeSha256": payload_tree,
            "requestSha256": request,
        },
        "private": {"files": private_rows},
    }
    identity = _canonical_sha256(public_basis, newline=False)
    document = {**public_basis, "identitySha256": identity}
    return _PreparedSpec(
        tuple(files),
        preparations,
        inventory,
        payload_tree,
        request,
        identity,
        document,
        _canonical_json_bytes(document, pretty=True),
    )


def _reseal_from_evidence(
    plan: W3DJobPlan, rows: Sequence[W3DPreparationEvidence]
) -> W3DJobPlan:
    if not rows:
        if any(job.model_preparation is not None for job in plan.jobs):
            raise W3DPreparedRootError(
                "prepared plan is missing declared preparation evidence"
            )
        return plan
    by_ordinal = {item.ordinal: item for item in rows}
    jobs = tuple(
        replace(
            job,
            prepared_model_sha256=by_ordinal[index].prepared_model_sha256,
            model_preparation_evidence_sha256=by_ordinal[index].evidence_sha256,
        )
        if index in by_ordinal
        else job
        for index, job in enumerate(plan.jobs)
    )
    batches: list[W3DJobBatch] = []
    cursor = 0
    for batch in plan.batches:
        selected = jobs[cursor : cursor + len(batch.jobs)]
        cursor += len(batch.jobs)
        replacement = replace(batch, jobs=selected)
        if replacement.manifest_bytes() != batch.manifest_bytes():
            raise W3DPreparedRootError("prepared plan changed an adapter manifest")
        batches.append(replacement)
    preparation_basis = {
        "schema": W3D_JOB_PREPARATION_SCHEMA,
        "schemaVersion": W3D_JOB_PREPARATION_VERSION,
        "inputPrivatePlanSha256": plan.private_plan_sha256,
        "preparations": [
            {
                "ordinal": item.ordinal,
                "kind": item.kind,
                "modelSourceSha256": item.model_source_sha256,
                "hierarchySourceSha256": item.hierarchy_source_sha256,
                "preparedModelSha256": item.prepared_model_sha256,
                "evidenceSha256": item.evidence_sha256,
            }
            for item in rows
        ],
    }
    preparation_seal = _canonical_sha256(preparation_basis)
    private_plan = _canonical_sha256(
        {
            "schema": f"{W3D_JOB_PREPARATION_SCHEMA}.plan-seal",
            "schemaVersion": W3D_JOB_PREPARATION_VERSION,
            "inputPrivatePlanSha256": plan.private_plan_sha256,
            "preparationSealSha256": preparation_seal,
        }
    )
    provisional = replace(
        plan,
        jobs=jobs,
        batches=tuple(batches),
        private_plan_sha256=private_plan,
        evidence_sha256="",
    )
    return replace(
        provisional,
        evidence_sha256=_canonical_sha256(provisional.evidence_hash_basis()),
    )


def _parse_preparation_evidence(
    value: object, plan: W3DJobPlan
) -> tuple[W3DPreparationEvidence, ...]:
    if not isinstance(value, list):
        raise W3DPreparedRootError("prepared root evidence is not a list")
    rows: list[W3DPreparationEvidence] = []
    expected_ordinals = [
        index
        for index, job in enumerate(plan.jobs)
        if job.model_preparation is not None
    ]
    for raw in value:
        if not isinstance(raw, dict) or set(raw) != {
            "ordinal",
            "jobId",
            "kind",
            "modelSourceSha256",
            "hierarchySourceSha256",
            "preparedModelSha256",
            "evidenceSha256",
            "sourceBytes",
            "preparedBytes",
        }:
            raise W3DPreparedRootError("prepared root evidence fields are invalid")
        try:
            row = W3DPreparationEvidence(
                ordinal=raw["ordinal"],
                job_id=raw["jobId"],
                kind=raw["kind"],
                model_source_sha256=raw["modelSourceSha256"],
                hierarchy_source_sha256=raw["hierarchySourceSha256"],
                prepared_model_sha256=raw["preparedModelSha256"],
                evidence_sha256=raw["evidenceSha256"],
                source_bytes=raw["sourceBytes"],
                prepared_bytes=raw["preparedBytes"],
            )
        except TypeError:
            raise W3DPreparedRootError(
                "prepared root evidence values are invalid"
            ) from None
        if (
            not _is_int(row.ordinal)
            or row.ordinal >= len(plan.jobs)
            or not all(
                _is_sha256(item)
                for item in (
                    row.model_source_sha256,
                    row.hierarchy_source_sha256,
                    row.prepared_model_sha256,
                    row.evidence_sha256,
                )
            )
            or not _is_int(row.source_bytes)
            or not _is_int(row.prepared_bytes)
        ):
            raise W3DPreparedRootError("prepared root evidence values are invalid")
        job = plan.jobs[row.ordinal]
        if (
            row.job_id != job.job_id
            or row.kind != job.model_preparation
            or row.model_source_sha256 != job.model_source_sha256
            or row.hierarchy_source_sha256 != job.hierarchy_source_sha256
        ):
            raise W3DPreparedRootError(
                "prepared root evidence mismatches the input plan"
            )
        rows.append(row)
    if [item.ordinal for item in rows] != expected_ordinals:
        raise W3DPreparedRootError(
            "prepared root evidence misses a declared preparation"
        )
    return tuple(rows)


def _verify_prepared_output(
    root: Path,
    source: _ValidatedSource,
    source_report: W3DJobRootReport,
    input_plan: W3DJobPlan,
    *,
    execute_accounted_jobs: bool,
    reused: bool,
) -> W3DPreparedRootReport:
    manifest_path = root.joinpath(*PurePosixPath(W3D_PREPARED_ROOT_MANIFEST).parts)
    document, raw = _read_canonical_manifest(manifest_path, source=False)
    if document.get("schemaVersion") != W3D_PREPARED_ROOT_SCHEMA_VERSION:
        raise W3DPreparedRootError("prepared W3D root version is unsupported")
    rows = _parse_preparation_evidence(document.get("preparations"), input_plan)
    prepared_plan = _reseal_from_evidence(input_plan, rows)
    try:
        returned = attest_w3d_job_preparations(
            prepared_plan,
            root,
            execute_accounted_jobs=execute_accounted_jobs,
        )
    except (TypeError, W3DJobPreparationError) as exc:
        raise W3DPreparedRootError(
            "prepared W3D root plan/tree attestation failed"
        ) from exc
    if returned != prepared_plan:
        raise W3DPreparedRootError(
            "prepared W3D root unexpectedly changed on validation"
        )
    tree_files, directories = _scan_tree(root, label="prepared W3D root tree")
    prepared_payload = tuple(
        item for item in tree_files if item.relative_path != W3D_PREPARED_ROOT_MANIFEST
    )
    expected_paths = [
        *(item.relative_path for item in source.files),
        W3D_PREPARED_ROOT_MANIFEST,
    ]
    if [item.relative_path for item in tree_files] != sorted(
        expected_paths, key=lambda value: (value.casefold(), value)
    ) or directories != _expected_directories(expected_paths):
        raise W3DPreparedRootError("prepared W3D root tree is not exact")
    spec = _make_spec(
        input_plan,
        prepared_plan,
        source_report,
        source.files,
        prepared_payload,
        execute_accounted_jobs=execute_accounted_jobs,
    )
    if document != spec.document or raw != spec.manifest_bytes:
        raise W3DPreparedRootError("prepared W3D root manifest mismatches its request")
    manifest_sha256 = hashlib.sha256(raw).hexdigest()
    manifest_snapshot = next(
        item for item in tree_files if item.relative_path == W3D_PREPARED_ROOT_MANIFEST
    )
    if manifest_snapshot.sha256 != manifest_sha256:
        raise W3DPreparedRootError("prepared W3D root manifest changed")
    return W3DPreparedRootReport(
        source_job_root_report=source_report,
        input_plan=input_plan,
        prepared_plan=prepared_plan,
        output_root=root,
        manifest_path=manifest_path,
        files=spec.files,
        preparations=spec.preparations,
        execute_accounted_jobs=execute_accounted_jobs,
        inventory_sha256=spec.inventory_sha256,
        payload_tree_sha256=spec.payload_tree_sha256,
        runner_tree_sha256=_runner_tree_sha256(tree_files),
        request_sha256=spec.request_sha256,
        identity_sha256=spec.identity_sha256,
        manifest_sha256=manifest_sha256,
        reused=reused,
    )


def _copy_source(source: _ValidatedSource, destination: Path) -> None:
    for item in source.files:
        target = destination.joinpath(*PurePosixPath(item.relative_path).parts)
        target.parent.mkdir(parents=True, exist_ok=True)
        source_path = source.root.joinpath(*PurePosixPath(item.relative_path).parts)
        try:
            with source_path.open("rb") as reader, target.open("xb") as writer:
                shutil.copyfileobj(reader, writer, HASH_BLOCK_BYTES)
                writer.flush()
                os.fsync(writer.fileno())
        except OSError:
            raise W3DPreparedRootError(
                "source W3D job root could not be cloned"
            ) from None
        _, copied = _read_file(target, label="cloned W3D job-root file", capture=False)
        if copied.byte_length != item.byte_length or copied.sha256 != item.sha256:
            raise W3DPreparedRootError("source W3D job-root clone is not exact")


def _same_snapshot(first: _ValidatedSource, second: _ValidatedSource) -> bool:
    return (
        first.root == second.root
        and first.document == second.document
        and first.manifest_bytes == second.manifest_bytes
        and first.files == second.files
        and first.directories == second.directories
    )


def _resolve_destination(output_root: Path | str, source_root: Path) -> Path:
    candidate = Path(output_root).expanduser().absolute()
    _assert_no_link_chain(candidate.parent)
    try:
        candidate.parent.mkdir(parents=True, exist_ok=True)
        parent = candidate.parent.resolve(strict=True)
    except OSError:
        raise W3DPreparedRootError(
            "prepared W3D root parent could not be created"
        ) from None
    destination = parent / candidate.name
    if not destination.name or destination.name in {".", ".."}:
        raise W3DPreparedRootError("prepared W3D root destination is invalid")
    if _paths_overlap(destination, source_root):
        raise W3DPreparedRootError("prepared and source W3D roots overlap")
    return destination


def _remove_owned_tree(path: Path, parent: Path, prefix: str) -> None:
    if path.parent != parent or not path.name.startswith(prefix) or _is_link_like(path):
        raise W3DPreparedRootError("refused to remove an unowned prepared-root path")
    _scan_tree(path, label="prepared-root transaction tree")
    shutil.rmtree(path)


def _publish(stage: Path, destination: Path, backup: Path) -> None:
    had_destination = os.path.lexists(destination)
    if had_destination:
        os.replace(destination, backup)
    try:
        os.replace(stage, destination)
    except Exception:
        if (
            had_destination
            and os.path.lexists(backup)
            and not os.path.lexists(destination)
        ):
            os.replace(backup, destination)
        raise


def prepare_w3d_execution_root(
    plan: W3DJobPlan,
    source_job_root_report: W3DJobRootReport,
    output_root: Path | str,
    *,
    execute_accounted_jobs: bool = False,
    force: bool = False,
) -> W3DPreparedRootReport:
    """Clone, prepare, seal, and publish a distinct W3D execution root."""

    if not isinstance(execute_accounted_jobs, bool):
        raise TypeError("W3D accounted-job preparation flag must be a boolean")
    if not isinstance(force, bool):
        raise TypeError("W3D prepared-root force flag must be a boolean")
    source = _validate_source_report(source_job_root_report)
    _validate_input_plan(
        plan,
        source,
        source_job_root_report,
        execute_accounted_jobs=execute_accounted_jobs,
    )
    destination = _resolve_destination(output_root, source.root)
    if os.path.lexists(destination) and not force:
        if _is_link_like(destination) or not destination.is_dir():
            raise W3DPreparedRootReuseError(
                "existing prepared W3D root is not an ordinary directory"
            )
        try:
            result = _verify_prepared_output(
                destination,
                source,
                source_job_root_report,
                plan,
                execute_accounted_jobs=execute_accounted_jobs,
                reused=True,
            )
            after = _validate_source_report(source_job_root_report)
            if not _same_snapshot(source, after):
                raise W3DPreparedRootError(
                    "raw source W3D job root changed during reuse"
                )
            return result
        except W3DPreparedRootError as exc:
            raise W3DPreparedRootReuseError(
                "existing prepared W3D root failed complete verification"
            ) from exc

    parent = destination.parent
    token = uuid.uuid4().hex
    stage = parent / f".{destination.name}.staging-{token}"
    backup = parent / f".{destination.name}.backup-{token}"
    published = False
    try:
        stage.mkdir()
        _copy_source(source, stage)
        try:
            prepared_plan = attest_w3d_job_preparations(
                plan,
                stage,
                execute_accounted_jobs=execute_accounted_jobs,
            )
        except (TypeError, W3DJobPreparationError) as exc:
            raise W3DPreparedRootError("W3D model preparation failed") from exc
        prepared_payload, directories = _scan_tree(
            stage, label="staged prepared W3D root"
        )
        if directories != source.directories:
            raise W3DPreparedRootError("W3D preparation changed source directories")
        spec = _make_spec(
            plan,
            prepared_plan,
            source_job_root_report,
            source.files,
            prepared_payload,
            execute_accounted_jobs=execute_accounted_jobs,
        )
        manifest_path = stage.joinpath(*PurePosixPath(W3D_PREPARED_ROOT_MANIFEST).parts)
        manifest_path.parent.mkdir(parents=True, exist_ok=True)
        with manifest_path.open("xb") as stream:
            stream.write(spec.manifest_bytes)
            stream.flush()
            os.fsync(stream.fileno())
        staged_report = _verify_prepared_output(
            stage,
            source,
            source_job_root_report,
            plan,
            execute_accounted_jobs=execute_accounted_jobs,
            reused=False,
        )
        after = _validate_source_report(source_job_root_report)
        if not _same_snapshot(source, after):
            raise W3DPreparedRootError(
                "raw source W3D job root changed during preparation"
            )
        if os.path.lexists(destination):
            if _is_link_like(destination) or not destination.is_dir():
                raise W3DPreparedRootError(
                    "existing prepared W3D root is not an ordinary directory"
                )
            _scan_tree(destination, label="existing prepared W3D root")
        _publish(stage, destination, backup)
        try:
            result = _verify_prepared_output(
                destination,
                source,
                source_job_root_report,
                plan,
                execute_accounted_jobs=execute_accounted_jobs,
                reused=False,
            )
            after_publish = _validate_source_report(source_job_root_report)
            if not _same_snapshot(source, after_publish):
                raise W3DPreparedRootError(
                    "raw source W3D job root changed during publication"
                )
        except Exception:
            if os.path.lexists(destination):
                os.replace(destination, stage)
            if os.path.lexists(backup):
                os.replace(backup, destination)
            raise
        published = True
        if os.path.lexists(backup):
            # Publication is already committed and fully revalidated.  A
            # best-effort cleanup failure must not turn that successful
            # commit into an exception while leaving the new destination in
            # place; callers could otherwise retry under the false premise
            # that the prior output remained canonical.
            try:
                _remove_owned_tree(backup, parent, f".{destination.name}.backup-")
            except (OSError, W3DPreparedRootError):
                pass
        del staged_report
        return result
    except OSError as exc:
        raise W3DPreparedRootError("prepared W3D root transaction failed") from exc
    finally:
        if os.path.lexists(stage):
            _remove_owned_tree(stage, parent, f".{destination.name}.staging-")
        if published and os.path.lexists(backup):
            try:
                _remove_owned_tree(backup, parent, f".{destination.name}.backup-")
            except (OSError, W3DPreparedRootError):
                pass


def validate_w3d_prepared_root(
    report: W3DPreparedRootReport,
    plan: W3DJobPlan,
    job_root: Path | str,
    *,
    execute_accounted_jobs: bool,
) -> W3DPreparedRootReport:
    """Fully revalidate a prepared report against runner inputs and raw source."""

    if type(report) is not W3DPreparedRootReport:
        raise TypeError("prepared_root_report must be a W3DPreparedRootReport")
    if not isinstance(execute_accounted_jobs, bool):
        raise TypeError("W3D accounted-job validation flag must be a boolean")
    if report.execute_accounted_jobs != execute_accounted_jobs:
        raise W3DPreparedRootError("prepared-root execution policy is stale")
    if report.prepared_plan != plan:
        raise W3DPreparedRootError("prepared-root plan does not match runner plan")
    source = _validate_source_report(report.source_job_root_report)
    _validate_input_plan(
        report.input_plan,
        source,
        report.source_job_root_report,
        execute_accounted_jobs=execute_accounted_jobs,
    )
    root = _ordinary_root(job_root, label="runner W3D job root")
    try:
        reported_root = Path(report.output_root).resolve(strict=True)
        reported_manifest = Path(report.manifest_path).resolve(strict=True)
    except OSError:
        raise W3DPreparedRootError(
            "prepared-root report paths are unavailable"
        ) from None
    expected_manifest = root.joinpath(*PurePosixPath(W3D_PREPARED_ROOT_MANIFEST).parts)
    if root != reported_root or reported_manifest != expected_manifest.resolve(
        strict=True
    ):
        raise W3DPreparedRootError("prepared-root report does not match runner root")
    verified = _verify_prepared_output(
        root,
        source,
        report.source_job_root_report,
        report.input_plan,
        execute_accounted_jobs=execute_accounted_jobs,
        reused=report.reused,
    )
    comparable = (
        "prepared_plan",
        "files",
        "preparations",
        "inventory_sha256",
        "payload_tree_sha256",
        "runner_tree_sha256",
        "request_sha256",
        "identity_sha256",
        "manifest_sha256",
    )
    if any(getattr(verified, field) != getattr(report, field) for field in comparable):
        raise W3DPreparedRootError("prepared-root report evidence is stale")
    after = _validate_source_report(report.source_job_root_report)
    if not _same_snapshot(source, after):
        raise W3DPreparedRootError("raw source W3D job root changed during validation")
    return verified


build_w3d_prepared_root = prepare_w3d_execution_root


__all__ = [
    "MAX_PREPARED_MANIFEST_BYTES",
    "W3D_PREPARED_ROOT_MANIFEST",
    "W3D_PREPARED_ROOT_SCHEMA",
    "W3D_PREPARED_ROOT_SCHEMA_VERSION",
    "W3DPreparationEvidence",
    "W3DPreparedFile",
    "W3DPreparedRootBinding",
    "W3DPreparedRootError",
    "W3DPreparedRootReport",
    "W3DPreparedRootReuseError",
    "build_w3d_prepared_root",
    "prepare_w3d_execution_root",
    "validate_w3d_prepared_root",
]
