"""Fail-closed host coordinator for bounded W3D Blender batches.

The runner accepts only an already sealed :class:`W3DJobPlan` and caller-
supplied tool pins.  Private paths are used solely for process execution; the
published manifest is canonical, content-addressed evidence containing no
physical paths, W3D paths, or authored header identifiers.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat as stat_module
import struct
import subprocess
import threading
import time
from typing import Any, Callable, Mapping, Sequence
import uuid

from .w3d_job_planner import (
    MAX_BATCH_JOBS,
    MAX_BATCH_MANIFEST_BYTES,
    SECONDARY_SKIN_PREPARATION,
    W3DJobBatch,
    W3DJobPlan,
    W3DPlannedJob,
    W3DTerminal,
    w3d_job_resolution_contract_is_valid,
)
from .w3d_prepared_root import (
    W3DPreparedRootBinding,
    W3DPreparedRootError,
    W3DPreparedRootReport,
    validate_w3d_prepared_root,
)
from .w3d_glb_validation import (
    W3DGLBValidationError,
    validate_w3d_glb_semantics,
)


W3D_BATCH_CORPUS_SCHEMA = "openbfme.w3d-batch-corpus"
W3D_BATCH_CORPUS_VERSION = 0
W3D_BATCH_ACCOUNTED_CORPUS_VERSION = 2
W3D_BATCH_CORPUS_MANIFEST = "manifest.json"

MAX_CORPUS_MANIFEST_BYTES = 64 * 1024 * 1024
MAX_GLB_JSON_BYTES = 64 * 1024 * 1024
MAX_GLB_BYTES = 4 * 1024 * 1024 * 1024
DEFAULT_BATCH_TIMEOUT_SECONDS = 60 * 60
DEFAULT_STDOUT_LIMIT_BYTES = 4 * 1024 * 1024
DEFAULT_STDERR_LIMIT_BYTES = 4 * 1024 * 1024
MAX_CAPTURE_LIMIT_BYTES = 64 * 1024 * 1024
HASH_BLOCK_BYTES = 1024 * 1024
W3D_CONVERTER_MODULE_NAME = "w3d_to_glb.py"

_ADAPTER_STAGING_DIRECTORY = ".openbfme-w3d-batch-staging"
_JOB_MARKER = "OPENBFME_W3D_BATCH_JOB"
_DONE_MARKER = "OPENBFME_W3D_BATCH_DONE"
_MARKER_STEM = "OPENBFME_W3D_BATCH_"
_SHA256_CHARACTERS = frozenset("0123456789abcdef")
_ASSET_KINDS = frozenset({"animated", "hierarchical", "static"})
_FAILURE_PHASES = frozenset(
    {
        "input-validation",
        "converter-initialization",
        "converter-execution",
        "scene-reset",
        "model-import",
        "embedded-model-import",
        "model-file-read",
        "model-chunk-header-read",
        "model-mesh-read",
        "model-hierarchy-read",
        "model-hlod-read",
        "model-animation-read",
        "model-compressed-animation-read",
        "model-box-read",
        "model-dazzle-read",
        "model-hierarchy-dependency-validation",
        "model-scene-collection",
        "model-scene-mesh-create",
        "model-scene-rig-create",
        "model-scene-mesh-bind",
        "model-scene-animation-create",
        "model-animation-setup",
        "model-animation-channel-processing",
        "model-animation-bone-resolution",
        "model-animation-channel-decode",
        "model-animation-keyframe-write",
        "model-animation-action-finalization",
        "model-animation-frame-reset",
        "model-load-complete",
        "model-direct-load-dispatch",
        "model-direct-load-result",
        "model-operator-dispatch",
        "model-operator-result",
        "animation-output-capture-setup",
        "animation-output-capture-restore",
        "animation-output-capture-accounting",
        "model-import-validation",
        "request-validation",
        "rig-resolution",
        "rig-validation",
        "action-validation",
        "geometry-validation",
        "material-validation",
        "presentation-validation",
        "skin-validation",
        "attachment-validation",
        "attachment-canonicalization",
        "mesh-object-type-validation",
        "mesh-helper-filter-validation",
        "mesh-box-ambiguity-validation",
        "mesh-equipment-classification",
        "required-equipment-validation",
        "additive-material-discovery",
        "additive-material-graph-validation",
        "additive-material-pixel-read",
        "additive-material-alpha-derivation",
        "additive-material-image-duplication",
        "additive-material-pixel-write",
        "additive-material-round-trip",
        "additive-material-alpha-link",
        "generated-image-validation",
        "shader-material-validation",
        "render-proof",
        "scene-validation",
        "animation-import",
        "post-animation-validation",
        "animation-sidecar-mesh-strip",
        "attachment-restoration",
        "render-revalidation",
        "animation-export-preparation",
        "export",
        "glb-validation",
        "report-validation",
        "publication",
    }
)
_FAILURE_KINDS = frozenset(
    {
        "assertion",
        "memory",
        "timeout",
        "os",
        "key",
        "type",
        "value",
        "runtime",
        "application",
        "control-flow",
    }
)
_COUNT_FIELDS = (
    "meshes",
    "animations",
    "bones",
    "skeletons",
    "vertices",
    "triangles",
    "skinned_meshes",
    "materials",
    "images",
)
_SEMANTIC_COUNT_FIELDS = (
    "animations",
    "bones",
    "skeletons",
    "triangles",
    "skinned_meshes",
)
_JOB_RESULT_FIELDS = frozenset(
    {
        "job_id",
        "status",
        "output_sha256",
        "report_schema",
        "report_version",
        "asset_kind",
        "adapter_report_sha256",
        *_COUNT_FIELDS,
    }
)
_FAILED_JOB_RESULT_FIELDS = frozenset(
    {
        "job_id",
        "status",
        "failure_code",
        "failure_phase",
        "failure_kind",
    }
)
_DONE_FIELDS = frozenset(
    {
        "report_schema",
        "report_version",
        "manifest_sha256",
        "jobs",
        "succeeded",
        "failed",
        "complete",
    }
)


class W3DBatchRunnerError(RuntimeError):
    """A plan, pin, process, artifact, or transaction failed closed."""


class W3DBatchReuseError(W3DBatchRunnerError):
    """A pre-existing destination failed complete reuse verification."""


@dataclass(frozen=True, slots=True)
class W3DBatchConversionError(W3DBatchRunnerError):
    """Sanitized aggregate for one completely reported failed batch."""

    failed_job_count: int
    job_count: int
    failure_counts: tuple[tuple[str, str, int], ...]

    def __init__(
        self,
        *,
        failed_job_count: int,
        job_count: int,
        failure_counts: tuple[tuple[str, str, int], ...],
    ) -> None:
        W3DBatchRunnerError.__init__(self, "W3D batch conversion failed")
        object.__setattr__(self, "failed_job_count", failed_job_count)
        object.__setattr__(self, "job_count", job_count)
        object.__setattr__(self, "failure_counts", failure_counts)


@dataclass(frozen=True, slots=True)
class W3DProcessResult:
    """Bounded subprocess outcome accepted from the injectable runner."""

    returncode: int
    stdout: bytes = b""
    stderr: bytes = b""
    timed_out: bool = False
    stdout_overflow: bool = False
    stderr_overflow: bool = False


SubprocessRunner = Callable[..., W3DProcessResult]


@dataclass(frozen=True, slots=True)
class W3DTreeIdentity:
    """Payload-free identity of one fully scanned ordinary-file tree."""

    sha256: str
    file_count: int
    byte_length: int

    def neutral(self) -> dict[str, object]:
        return {
            "sha256": self.sha256,
            "fileCount": self.file_count,
            "bytes": self.byte_length,
        }


@dataclass(frozen=True, slots=True)
class W3DOutputEvidence:
    """Sanitized adapter and native-container evidence for one planned job."""

    output_id: str
    asset_kind: str
    byte_length: int
    sha256: str
    adapter_report_sha256: str
    counts: Mapping[str, int]

    def neutral(self) -> dict[str, object]:
        return {
            "outputId": self.output_id,
            "assetKind": self.asset_kind,
            "bytes": self.byte_length,
            "sha256": self.sha256,
            "adapterReportSha256": self.adapter_report_sha256,
            "counts": {key: self.counts[key] for key in _COUNT_FIELDS},
        }


@dataclass(frozen=True, slots=True)
class W3DBatchCorpusReport:
    """Local handles plus canonical path-free conversion evidence."""

    output_root: Path
    manifest_path: Path
    manifest_sha256: str
    request_sha256: str
    identity_sha256: str
    blender_executable_sha256: str
    adapter_sha256: str
    blender_runtime_tree: W3DTreeIdentity | None
    adapter_bundle_tree: W3DTreeIdentity | None
    plugin_tree: W3DTreeIdentity
    job_tree: W3DTreeIdentity
    plan: W3DJobPlan
    outputs: tuple[W3DOutputEvidence, ...]
    execute_accounted_jobs: bool
    prepared_root_binding: W3DPreparedRootBinding | None
    published: bool
    reused: bool

    @property
    def job_conversion_complete(self) -> bool:
        """Whether every explicitly planned job emitted a verified GLB."""

        return self.published and len(self.outputs) == len(self.plan.jobs)

    @property
    def conversion_complete(self) -> bool:
        """Whether the terminal-free source corpus was fully converted."""

        return (
            self.job_conversion_complete
            and not self.plan.terminals
            and self.plan.source_accounting_complete
        )

    @property
    def corpus_complete(self) -> bool:
        """Whether this report proves complete corpus conversion."""

        return self.conversion_complete

    def neutral(self) -> dict[str, object]:
        basis = _report_basis(
            plan=self.plan,
            blender_executable_sha256=self.blender_executable_sha256,
            adapter_sha256=self.adapter_sha256,
            blender_runtime_tree=self.blender_runtime_tree,
            adapter_bundle_tree=self.adapter_bundle_tree,
            plugin_tree=self.plugin_tree,
            job_tree=self.job_tree,
            request_sha256=self.request_sha256,
            outputs=self.outputs,
            execute_accounted_jobs=self.execute_accounted_jobs,
            prepared_root_binding=self.prepared_root_binding,
            published=self.published,
        )
        return {**basis, "identitySha256": self.identity_sha256}


@dataclass(frozen=True, slots=True)
class _TreeFile:
    relative_path: str
    byte_length: int
    sha256: str


def _canonical_json_bytes(value: object) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))
        + "\n"
    ).encode("utf-8")


def _canonical_sha256(value: object) -> str:
    return hashlib.sha256(_canonical_json_bytes(value)).hexdigest()


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str) and len(value) == 64 and set(value) <= _SHA256_CHARACTERS
    )


def _is_int(value: object) -> bool:
    return type(value) is int and value >= 0


def _is_link_like(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    if path.is_symlink() or bool(is_junction and is_junction()):
        return True
    try:
        attributes = getattr(path.lstat(), "st_file_attributes", 0)
    except OSError:
        return False
    return bool(attributes & getattr(stat_module, "FILE_ATTRIBUTE_REPARSE_POINT", 0))


def _assert_single_link_file(path: Path, label: str) -> None:
    try:
        metadata = path.stat(follow_symlinks=False)
    except OSError as exc:
        raise W3DBatchRunnerError(f"{label} could not be inspected") from exc
    if getattr(metadata, "st_nlink", 1) != 1:
        raise W3DBatchRunnerError(f"{label} must not be a hard link")


def _assert_no_link_chain(path: Path) -> None:
    absolute = path.expanduser().absolute()
    for candidate in reversed((absolute, *absolute.parents)):
        if os.path.lexists(candidate) and _is_link_like(candidate):
            raise W3DBatchRunnerError("declared filesystem path contains a link")


def _ordinary_file(path: Path | str, label: str) -> Path:
    candidate = Path(path).expanduser()
    _assert_no_link_chain(candidate)
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as exc:
        raise W3DBatchRunnerError(f"{label} is unavailable") from exc
    if not resolved.is_file() or _is_link_like(resolved):
        raise W3DBatchRunnerError(f"{label} must be an ordinary file")
    return resolved


def _ordinary_directory(path: Path | str, label: str) -> Path:
    candidate = Path(path).expanduser()
    _assert_no_link_chain(candidate)
    try:
        resolved = candidate.resolve(strict=True)
    except OSError as exc:
        raise W3DBatchRunnerError(f"{label} is unavailable") from exc
    if not resolved.is_dir() or _is_link_like(resolved):
        raise W3DBatchRunnerError(f"{label} must be an ordinary directory")
    return resolved


def _hash_file(path: Path, label: str) -> tuple[int, str]:
    _assert_no_link_chain(path)
    if not path.is_file() or _is_link_like(path):
        raise W3DBatchRunnerError(f"{label} must be an ordinary file")
    digest = hashlib.sha256()
    size = 0
    try:
        with path.open("rb") as handle:
            while True:
                block = handle.read(HASH_BLOCK_BYTES)
                if not block:
                    break
                size += len(block)
                digest.update(block)
    except OSError as exc:
        raise W3DBatchRunnerError(f"{label} could not be hashed") from exc
    try:
        stat = path.stat()
    except OSError as exc:
        raise W3DBatchRunnerError(f"{label} could not be restated") from exc
    if stat.st_size != size or not path.is_file() or _is_link_like(path):
        raise W3DBatchRunnerError(f"{label} changed while hashing")
    return size, digest.hexdigest()


def _scan_tree(root: Path, label: str) -> tuple[tuple[_TreeFile, ...], set[str]]:
    _assert_no_link_chain(root)
    if not root.is_dir() or _is_link_like(root):
        raise W3DBatchRunnerError(f"{label} must be an ordinary directory")
    pending = [root]
    files: dict[str, _TreeFile] = {}
    directories: set[str] = set()
    while pending:
        current = pending.pop()
        try:
            entries = list(os.scandir(current))
        except OSError as exc:
            raise W3DBatchRunnerError(f"{label} could not be scanned") from exc
        for entry in entries:
            path = Path(entry.path)
            relative = path.relative_to(root).as_posix()
            folded = relative.casefold()
            if entry.is_symlink() or _is_link_like(path):
                raise W3DBatchRunnerError(f"{label} contains a link")
            if folded in files or folded in directories:
                raise W3DBatchRunnerError(f"{label} contains case-colliding paths")
            try:
                if entry.is_file(follow_symlinks=False):
                    size, digest = _hash_file(path, f"{label} file")
                    files[folded] = _TreeFile(relative, size, digest)
                elif entry.is_dir(follow_symlinks=False):
                    directories.add(folded)
                    pending.append(path)
                else:
                    raise W3DBatchRunnerError(
                        f"{label} contains an unsupported filesystem entry"
                    )
            except OSError as exc:
                raise W3DBatchRunnerError(f"{label} entry could not be read") from exc
    ordered = tuple(
        sorted(
            files.values(),
            key=lambda item: (item.relative_path.casefold(), item.relative_path),
        )
    )
    return ordered, directories


def _tree_identity(files: Sequence[_TreeFile]) -> W3DTreeIdentity:
    payload = {
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
    return W3DTreeIdentity(
        sha256=_canonical_sha256(payload),
        file_count=len(files),
        byte_length=sum(item.byte_length for item in files),
    )


def hash_w3d_tool_tree(path: Path | str) -> W3DTreeIdentity:
    """Fully scan an ordinary tree and return its caller-pin identity."""

    root = _ordinary_directory(path, "pinned tree")
    files, _ = _scan_tree(root, "pinned tree")
    if not files:
        raise W3DBatchRunnerError("pinned tree contains no ordinary files")
    return _tree_identity(files)


def _snapshot_adapter_bundle(adapter: Path) -> W3DTreeIdentity:
    """Seal the exact host adapter plus the converter module it imports."""

    converter = _ordinary_file(
        adapter.with_name(W3D_CONVERTER_MODULE_NAME),
        "W3D converter module",
    )
    files: list[_TreeFile] = []
    for role, path in (("adapter.py", adapter), ("converter.py", converter)):
        size, digest = _hash_file(path, f"W3D {role[:-3]}")
        files.append(_TreeFile(role, size, digest))
    return _tree_identity(tuple(files))


def hash_w3d_adapter_bundle(adapter_path: Path | str) -> W3DTreeIdentity:
    """Return the caller-pin identity of the exact two-file adapter closure."""

    adapter = _ordinary_file(adapter_path, "Blender adapter")
    return _snapshot_adapter_bundle(adapter)


def _paths_overlap(left: Path, right: Path) -> bool:
    try:
        common = Path(os.path.commonpath((left, right)))
    except ValueError:
        return False
    return common == left or common == right


def _safe_relative(value: object, suffix: str) -> str:
    if (
        not isinstance(value, str)
        or not value
        or len(value) > 512
        or "\\" in value
        or "\x00" in value
        or ":" in value
    ):
        raise W3DBatchRunnerError("plan contains an unsafe relative path")
    relative = PurePosixPath(value)
    if (
        relative.is_absolute()
        or relative.as_posix() != value
        or any(part in {"", ".", ".."} for part in relative.parts)
        or relative.suffix.casefold() != suffix
    ):
        raise W3DBatchRunnerError("plan contains an unsafe relative path")
    return value


def _safe_child(root: Path, relative: str) -> Path:
    candidate = root.joinpath(*PurePosixPath(relative).parts)
    resolved_parent = candidate.parent.resolve(strict=False)
    if root != resolved_parent and root not in resolved_parent.parents:
        raise W3DBatchRunnerError("relative path escapes its declared root")
    return candidate


def _validate_plan(plan: W3DJobPlan, *, execute_accounted_jobs: bool) -> None:
    if not isinstance(plan, W3DJobPlan):
        raise TypeError("W3D batch runner plan must be a W3DJobPlan")
    for digest in (
        plan.catalog_input_sha256,
        plan.catalog_metadata_sha256,
        plan.private_plan_sha256,
        plan.evidence_sha256,
    ):
        if not _is_sha256(digest):
            raise W3DBatchRunnerError("W3D job plan contains an invalid hash")
    try:
        expected_evidence_sha256 = _canonical_sha256(plan.evidence_hash_basis())
    except (AttributeError, TypeError, ValueError) as exc:
        raise W3DBatchRunnerError("W3D job plan evidence is malformed") from exc
    if plan.evidence_sha256 != expected_evidence_sha256:
        raise W3DBatchRunnerError("W3D job plan evidence hash is incoherent")
    if (
        not _is_int(plan.source_count)
        or not _is_int(plan.consumed_source_count)
        or plan.consumed_source_count > plan.source_count
    ):
        raise W3DBatchRunnerError("W3D job plan source accounting is invalid")
    if not all(
        type(value) is tuple for value in (plan.jobs, plan.batches, plan.terminals)
    ):
        raise W3DBatchRunnerError("W3D job plan is not immutable")
    if (not plan.jobs) != (not plan.batches):
        raise W3DBatchRunnerError("W3D job plan batch coverage is invalid")

    job_ids: set[str] = set()
    output_paths: set[str] = set()
    consumed_source_ids: set[str] = set()
    for job in plan.jobs:
        if not isinstance(job, W3DPlannedJob):
            raise W3DBatchRunnerError("W3D job plan contains an invalid job")
        if (
            not isinstance(job.job_id, str)
            or not isinstance(job.asset_kind, str)
            or job.asset_kind not in _ASSET_KINDS
            or not isinstance(job.model_source_id, str)
            or not job.model_source_id
            or not _is_sha256(job.model_source_sha256)
            or not _is_sha256(job.definition_sha256)
            or job.job_id != f"w3d-{job.definition_sha256[:40]}"
            or type(job.animations) is not tuple
            or type(job.animation_source_ids) is not tuple
            or type(job.animation_source_sha256s) is not tuple
            or len(job.animation_source_ids) != len(job.animations)
            or len(job.animation_source_sha256s) != len(job.animations)
            or any(not isinstance(item, str) for item in job.animations)
            or any(
                not isinstance(item, str) or not item
                for item in job.animation_source_ids
            )
            or any(not _is_sha256(item) for item in job.animation_source_sha256s)
            or len(set(job.animations)) != len(job.animations)
        ):
            raise W3DBatchRunnerError("W3D job plan contains invalid job metadata")
        consumed_source_ids.add(job.model_source_id)
        consumed_source_ids.update(job.animation_source_ids)
        _safe_relative(job.model, ".w3d")
        for animation in job.animations:
            _safe_relative(animation, ".w3d")
        hierarchy_fields = (
            job.hierarchy,
            job.hierarchy_source_id,
            job.hierarchy_source_sha256,
        )
        if any(value is None for value in hierarchy_fields) != all(
            value is None for value in hierarchy_fields
        ):
            raise W3DBatchRunnerError("W3D job plan hierarchy evidence is incomplete")
        if job.hierarchy is not None:
            hierarchy = _safe_relative(job.hierarchy, ".w3d")
            if (
                not isinstance(job.hierarchy_source_id, str)
                or not job.hierarchy_source_id
                or not _is_sha256(job.hierarchy_source_sha256)
            ):
                raise W3DBatchRunnerError("W3D job plan hierarchy evidence is invalid")
            consumed_source_ids.add(job.hierarchy_source_id)
            if hierarchy == job.model and (
                job.hierarchy_source_id != job.model_source_id
                or job.hierarchy_source_sha256 != job.model_source_sha256
            ):
                raise W3DBatchRunnerError(
                    "W3D embedded hierarchy evidence is incoherent"
                )
        if not w3d_job_resolution_contract_is_valid(job):
            raise W3DBatchRunnerError("W3D job resolution contract is invalid")
        preparation_pair = (
            job.prepared_model_sha256,
            job.model_preparation_evidence_sha256,
        )
        if (preparation_pair[0] is None) != (preparation_pair[1] is None):
            raise W3DBatchRunnerError("W3D model preparation attestation is partial")
        if job.model_preparation is None:
            if preparation_pair != (None, None):
                raise W3DBatchRunnerError(
                    "W3D model preparation attestation is incoherent"
                )
        else:
            if job.model_preparation != SECONDARY_SKIN_PREPARATION:
                raise W3DBatchRunnerError("W3D model preparation kind is unsupported")
            if job.hierarchy is None:
                raise W3DBatchRunnerError("W3D model preparation hierarchy is missing")
            if (
                not _is_sha256(preparation_pair[0])
                or not _is_sha256(preparation_pair[1])
                or preparation_pair[0] == job.model_source_sha256
            ):
                raise W3DBatchRunnerError("W3D model preparation is not fully attested")
        output = _safe_relative(job.output, ".glb")
        if output != f"glb/{job.job_id}.glb":
            raise W3DBatchRunnerError("W3D job plan output identity is invalid")
        if job.job_id in job_ids or output.casefold() in output_paths:
            raise W3DBatchRunnerError("W3D job plan contains duplicate jobs")
        job_ids.add(job.job_id)
        output_paths.add(output.casefold())

    flattened: list[W3DPlannedJob] = []
    batch_ids: set[str] = set()
    for batch in plan.batches:
        if not isinstance(batch, W3DJobBatch):
            raise W3DBatchRunnerError("W3D job plan contains an invalid batch")
        if type(batch.jobs) is not tuple:
            raise W3DBatchRunnerError("W3D job plan batch is not immutable")
        try:
            payload = batch.manifest_bytes()
        except (AttributeError, TypeError, ValueError) as exc:
            raise W3DBatchRunnerError("W3D job plan batch is malformed") from exc
        digest = hashlib.sha256(payload).hexdigest()
        if (
            not batch.jobs
            or len(batch.jobs) > MAX_BATCH_JOBS
            or len(payload) > MAX_BATCH_MANIFEST_BYTES
            or digest != batch.manifest_sha256
            or batch.batch_id != f"batch-{digest[:32]}"
            or batch.batch_id in batch_ids
        ):
            raise W3DBatchRunnerError("W3D job plan batch seal is invalid")
        batch_ids.add(batch.batch_id)
        flattened.extend(batch.jobs)
    if tuple(flattened) != plan.jobs:
        raise W3DBatchRunnerError("W3D job plan batches do not exactly cover jobs")

    terminal_source_ids: set[str] = set()
    terminal_order: list[tuple[str, str]] = []
    for terminal in plan.terminals:
        if (
            not isinstance(terminal, W3DTerminal)
            or not isinstance(terminal.source_id, str)
            or not terminal.source_id
            or not _is_sha256(terminal.source_sha256)
            or type(terminal.reason_codes) is not tuple
            or not terminal.reason_codes
            or any(
                not isinstance(reason, str) or not reason
                for reason in terminal.reason_codes
            )
            or terminal.reason_codes != tuple(sorted(set(terminal.reason_codes)))
        ):
            raise W3DBatchRunnerError("W3D job plan terminal evidence is invalid")
        if terminal.source_id in terminal_source_ids:
            raise W3DBatchRunnerError(
                "W3D job plan contains duplicate terminal source IDs"
            )
        terminal_source_ids.add(terminal.source_id)
        terminal_order.append((terminal.source_id, terminal.source_sha256))
    if terminal_order != sorted(terminal_order):
        raise W3DBatchRunnerError("W3D job plan terminal inventory is not canonical")

    if not execute_accounted_jobs:
        if plan.terminals or not plan.source_accounting_complete:
            raise W3DBatchRunnerError(
                "W3D job plan has terminals or incomplete source accounting"
            )
        return

    if not plan.jobs:
        raise W3DBatchRunnerError(
            "accounted-job execution requires at least one planned job"
        )
    if len(consumed_source_ids) != plan.consumed_source_count:
        raise W3DBatchRunnerError(
            "accounted-job execution consumed source IDs are not exact"
        )
    if consumed_source_ids & terminal_source_ids:
        raise W3DBatchRunnerError(
            "accounted-job execution sources overlap the terminal inventory"
        )
    if plan.consumed_source_count + len(plan.terminals) != plan.source_count:
        raise W3DBatchRunnerError(
            "accounted-job execution source accounting is not exact"
        )


def _snapshot_job_tree(root: Path, plan: W3DJobPlan) -> W3DTreeIdentity:
    files, _ = _scan_tree(root, "job root")
    if plan.jobs and not files:
        raise W3DBatchRunnerError("job root contains no ordinary files")
    by_path = {item.relative_path: item for item in files}
    for job in plan.jobs:
        model = by_path.get(job.model)
        expected_model_sha256 = (
            job.prepared_model_sha256
            if job.model_preparation is not None
            else job.model_source_sha256
        )
        if model is None or model.sha256 != expected_model_sha256:
            raise W3DBatchRunnerError("planned model is missing or has changed")
        for animation, expected_sha256 in zip(
            job.animations, job.animation_source_sha256s, strict=True
        ):
            staged_animation = by_path.get(animation)
            if staged_animation is None or staged_animation.sha256 != expected_sha256:
                raise W3DBatchRunnerError("planned animation is missing or has changed")
        if job.hierarchy is None:
            continue
        assert job.hierarchy_source_sha256 is not None
        if job.hierarchy == job.model:
            if (
                job.hierarchy_source_id != job.model_source_id
                or job.hierarchy_source_sha256 != job.model_source_sha256
            ):
                raise W3DBatchRunnerError(
                    "planned embedded hierarchy evidence is incoherent"
                )
            if (
                job.model_preparation is None
                and model.sha256 != job.hierarchy_source_sha256
            ):
                raise W3DBatchRunnerError("planned hierarchy is missing or has changed")
        else:
            hierarchy = by_path.get(job.hierarchy)
            if hierarchy is None or hierarchy.sha256 != job.hierarchy_source_sha256:
                raise W3DBatchRunnerError("planned hierarchy is missing or has changed")
    return _tree_identity(files)


def _validate_pin(expected: object, actual: str, label: str) -> None:
    if not _is_sha256(expected):
        raise W3DBatchRunnerError(f"{label} pin must be a lowercase SHA-256")
    if expected != actual:
        raise W3DBatchRunnerError(f"{label} does not match its supplied pin")


def _validate_limits(
    timeout_seconds: float,
    stdout_limit_bytes: int,
    stderr_limit_bytes: int,
) -> None:
    if (
        isinstance(timeout_seconds, bool)
        or not isinstance(timeout_seconds, (int, float))
        or not 0 < timeout_seconds <= 24 * 60 * 60
    ):
        raise ValueError("batch timeout must be between 0 and 86400 seconds")
    for value, label in (
        (stdout_limit_bytes, "stdout limit"),
        (stderr_limit_bytes, "stderr limit"),
    ):
        if (
            isinstance(value, bool)
            or not isinstance(value, int)
            or not 1 <= value <= MAX_CAPTURE_LIMIT_BYTES
        ):
            raise ValueError(f"{label} must be a bounded positive integer")


def _default_subprocess_runner(
    command: Sequence[str],
    *,
    timeout_seconds: float,
    max_stdout_bytes: int,
    max_stderr_bytes: int,
) -> W3DProcessResult:
    """Run without a shell while killing on time or capture overflow."""

    environment = os.environ.copy()
    environment["PYTHONDONTWRITEBYTECODE"] = "1"
    environment["PYTHONNOUSERSITE"] = "1"
    try:
        process = subprocess.Popen(
            list(command),
            stdin=subprocess.DEVNULL,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            shell=False,
            env=environment,
        )
    except OSError as exc:
        raise W3DBatchRunnerError("Blender process could not be started") from exc
    assert process.stdout is not None
    assert process.stderr is not None
    buffers = {"stdout": bytearray(), "stderr": bytearray()}
    overflow = {"stdout": False, "stderr": False}
    lock = threading.Lock()

    def drain(name: str, stream: Any, limit: int) -> None:
        try:
            while True:
                block = stream.read(65536)
                if not block:
                    return
                with lock:
                    remaining = limit - len(buffers[name])
                    if remaining > 0:
                        buffers[name].extend(block[:remaining])
                    if len(block) > remaining:
                        overflow[name] = True
        finally:
            stream.close()

    threads = (
        threading.Thread(
            target=drain,
            args=("stdout", process.stdout, max_stdout_bytes),
            daemon=True,
        ),
        threading.Thread(
            target=drain,
            args=("stderr", process.stderr, max_stderr_bytes),
            daemon=True,
        ),
    )
    for thread in threads:
        thread.start()
    deadline = time.monotonic() + float(timeout_seconds)
    timed_out = False
    try:
        while process.poll() is None:
            with lock:
                too_large = overflow["stdout"] or overflow["stderr"]
            if too_large:
                process.kill()
                break
            if time.monotonic() >= deadline:
                timed_out = True
                process.kill()
                break
            time.sleep(0.01)
        try:
            process.wait(timeout=5)
        except subprocess.TimeoutExpired:
            timed_out = True
            process.kill()
    finally:
        if process.poll() is None:
            process.kill()
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                timed_out = True
        join_deadline = time.monotonic() + 1
        for thread in threads:
            thread.join(max(0.0, join_deadline - time.monotonic()))
        if any(thread.is_alive() for thread in threads):
            timed_out = True
    return W3DProcessResult(
        returncode=process.returncode if process.returncode is not None else -9,
        stdout=bytes(buffers["stdout"]),
        stderr=bytes(buffers["stderr"]),
        timed_out=timed_out,
        stdout_overflow=overflow["stdout"],
        stderr_overflow=overflow["stderr"],
    )


def _coerce_bytes(value: object, label: str) -> bytes:
    if value is None:
        return b""
    if isinstance(value, bytes):
        return value
    if isinstance(value, str):
        try:
            return value.encode("utf-8")
        except UnicodeEncodeError as exc:
            raise W3DBatchRunnerError(f"process {label} is not valid UTF-8") from exc
    raise W3DBatchRunnerError(f"process {label} has an invalid type")


def _run_process(
    runner: SubprocessRunner,
    command: tuple[str, ...],
    *,
    timeout_seconds: float,
    stdout_limit_bytes: int,
    stderr_limit_bytes: int,
) -> W3DProcessResult:
    try:
        raw = runner(
            command,
            timeout_seconds=timeout_seconds,
            max_stdout_bytes=stdout_limit_bytes,
            max_stderr_bytes=stderr_limit_bytes,
        )
    except (subprocess.TimeoutExpired, TimeoutError) as exc:
        raise W3DBatchRunnerError("Blender batch exceeded its wall-time bound") from exc
    except W3DBatchRunnerError:
        raise
    except Exception as exc:
        raise W3DBatchRunnerError("Blender batch runner failed") from exc
    try:
        returncode = raw.returncode
        stdout = _coerce_bytes(raw.stdout, "stdout")
        stderr = _coerce_bytes(raw.stderr, "stderr")
        timed_out = bool(getattr(raw, "timed_out", False))
        stdout_overflow = bool(getattr(raw, "stdout_overflow", False))
        stderr_overflow = bool(getattr(raw, "stderr_overflow", False))
    except AttributeError as exc:
        raise W3DBatchRunnerError("Blender batch runner returned no result") from exc
    if type(returncode) is not int:
        raise W3DBatchRunnerError("Blender batch returned an invalid exit code")
    if timed_out:
        raise W3DBatchRunnerError("Blender batch exceeded its wall-time bound")
    if stdout_overflow or len(stdout) > stdout_limit_bytes:
        raise W3DBatchRunnerError("Blender batch exceeded its stdout bound")
    if stderr_overflow or len(stderr) > stderr_limit_bytes:
        raise W3DBatchRunnerError("Blender batch exceeded its stderr bound")
    if returncode not in {0, 1}:
        raise W3DBatchRunnerError("Blender batch process failed")
    return W3DProcessResult(returncode, stdout, stderr)


def _strict_json(text: str) -> object:
    def pairs_hook(pairs: list[tuple[str, object]]) -> dict[str, object]:
        result: dict[str, object] = {}
        for key, value in pairs:
            if key in result:
                raise ValueError("duplicate JSON member")
            result[key] = value
        return result

    try:
        value = json.loads(text, object_pairs_hook=pairs_hook)
    except (json.JSONDecodeError, ValueError) as exc:
        raise W3DBatchRunnerError("Blender emitted malformed batch evidence") from exc
    if _canonical_json_bytes(value).decode("utf-8").rstrip("\n") != text:
        raise W3DBatchRunnerError("Blender emitted non-canonical batch evidence")
    return value


def _parse_markers(
    result: W3DProcessResult,
    batch: W3DJobBatch,
) -> tuple[dict[str, object], ...]:
    try:
        stdout = result.stdout.decode("utf-8")
        stderr = result.stderr.decode("utf-8")
    except UnicodeDecodeError as exc:
        raise W3DBatchRunnerError("Blender output is not valid UTF-8") from exc
    if _MARKER_STEM in stderr:
        raise W3DBatchRunnerError("Blender emitted batch evidence on stderr")
    job_rows: list[dict[str, object]] = []
    done: dict[str, object] | None = None
    for line in stdout.splitlines():
        if line.startswith(f"{_JOB_MARKER} "):
            raw = _strict_json(line[len(_JOB_MARKER) + 1 :])
            if not isinstance(raw, dict) or frozenset(raw) not in {
                _JOB_RESULT_FIELDS,
                _FAILED_JOB_RESULT_FIELDS,
            }:
                raise W3DBatchRunnerError("Blender emitted unsanitized job evidence")
            job_rows.append(raw)
        elif line.startswith(f"{_DONE_MARKER} "):
            if done is not None:
                raise W3DBatchRunnerError("Blender emitted duplicate batch evidence")
            raw = _strict_json(line[len(_DONE_MARKER) + 1 :])
            if not isinstance(raw, dict) or set(raw) != _DONE_FIELDS:
                raise W3DBatchRunnerError("Blender emitted unsanitized batch evidence")
            done = raw
        elif _MARKER_STEM in line:
            raise W3DBatchRunnerError("Blender emitted an unknown batch marker")

    if done is None or len(job_rows) != len(batch.jobs):
        raise W3DBatchRunnerError("Blender batch evidence has invalid cardinality")
    expected_ids = [job.job_id for job in batch.jobs]
    actual_ids = [row.get("job_id") for row in job_rows]
    if (
        any(not isinstance(item, str) for item in actual_ids)
        or actual_ids != expected_ids
        or len(set(actual_ids)) != len(actual_ids)
    ):
        raise W3DBatchRunnerError("Blender job evidence IDs do not match the batch")
    failed_rows: list[dict[str, object]] = []
    succeeded_count = 0
    for row, job in zip(job_rows, batch.jobs, strict=True):
        if frozenset(row) == _JOB_RESULT_FIELDS:
            if (
                row.get("status") != "succeeded"
                or row.get("report_schema") != "openbfme.w3d-adapter-report"
                or type(row.get("report_version")) is not int
                or row.get("report_version") != 2
                or row.get("asset_kind") != job.asset_kind
                or not _is_sha256(row.get("output_sha256"))
                or not _is_sha256(row.get("adapter_report_sha256"))
                or any(not _is_int(row.get(field)) for field in _COUNT_FIELDS)
                or any(
                    row.get(field) == 0 for field in ("meshes", "vertices", "triangles")
                )
                or row.get("animations") != len(job.animations)
            ):
                raise W3DBatchRunnerError("Blender job evidence did not prove success")
            succeeded_count += 1
            continue
        if (
            frozenset(row) != _FAILED_JOB_RESULT_FIELDS
            or row.get("status") != "failed"
            or row.get("failure_code") != "conversion-error"
            or not isinstance(row.get("failure_phase"), str)
            or row.get("failure_phase") not in _FAILURE_PHASES
            or not isinstance(row.get("failure_kind"), str)
            or row.get("failure_kind") not in _FAILURE_KINDS
        ):
            raise W3DBatchRunnerError("Blender job failure evidence is invalid")
        failed_rows.append(row)
    failed_count = len(failed_rows)
    if (
        done.get("report_schema") != "openbfme.w3d-batch-report"
        or type(done.get("report_version")) is not int
        or done.get("report_version") != 1
        or done.get("manifest_sha256") != batch.manifest_sha256
        or any(
            not _is_int(done.get(field)) for field in ("jobs", "succeeded", "failed")
        )
        or done.get("jobs") != len(batch.jobs)
        or done.get("succeeded") != succeeded_count
        or done.get("failed") != failed_count
        or done.get("complete") is not (failed_count == 0)
    ):
        raise W3DBatchRunnerError("Blender batch completion evidence is invalid")
    if type(result.returncode) is not int or result.returncode != (
        1 if failed_rows else 0
    ):
        raise W3DBatchRunnerError("Blender batch exit code mismatches its evidence")
    if failed_rows:
        counts: dict[tuple[str, str], int] = {}
        for row in failed_rows:
            phase = row["failure_phase"]
            kind = row["failure_kind"]
            assert isinstance(phase, str)
            assert isinstance(kind, str)
            key = (phase, kind)
            counts[key] = counts.get(key, 0) + 1
        raise W3DBatchConversionError(
            failed_job_count=failed_count,
            job_count=len(batch.jobs),
            failure_counts=tuple(
                (phase, kind, count) for (phase, kind), count in sorted(counts.items())
            ),
        )
    return tuple(job_rows)


def _validate_glb(
    path: Path,
    expected_counts: Mapping[str, int],
) -> tuple[int, str]:
    _assert_single_link_file(path, "GLB output")
    size, digest = _hash_file(path, "GLB output")
    if not 20 <= size <= MAX_GLB_BYTES:
        raise W3DBatchRunnerError("GLB output length is invalid")
    try:
        with path.open("rb") as handle:
            header = handle.read(12)
            if len(header) != 12:
                raise W3DBatchRunnerError("GLB header is truncated")
            magic, version, declared_length = struct.unpack("<4sII", header)
            if magic != b"glTF" or version != 2 or declared_length != size:
                raise W3DBatchRunnerError("GLB v2 header is invalid")
            chunks: list[tuple[int, int]] = []
            document: object | None = None
            while handle.tell() < size:
                chunk_header = handle.read(8)
                if len(chunk_header) != 8:
                    raise W3DBatchRunnerError("GLB chunk header is truncated")
                chunk_length, chunk_type = struct.unpack("<II", chunk_header)
                if (
                    chunk_length == 0
                    or chunk_length % 4
                    or handle.tell() + chunk_length > size
                ):
                    raise W3DBatchRunnerError("GLB chunk length is invalid")
                if not chunks:
                    if chunk_type != 0x4E4F534A or chunk_length > MAX_GLB_JSON_BYTES:
                        raise W3DBatchRunnerError("GLB JSON chunk is invalid")
                    raw_json = handle.read(chunk_length)
                    try:
                        document = json.loads(raw_json.decode("utf-8"))
                    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                        raise W3DBatchRunnerError(
                            "GLB JSON document is invalid"
                        ) from exc
                else:
                    if len(chunks) != 1 or chunk_type != 0x004E4942:
                        raise W3DBatchRunnerError("GLB contains invalid chunks")
                    handle.seek(chunk_length, os.SEEK_CUR)
                chunks.append((chunk_length, chunk_type))
            if (
                handle.tell() != size
                or not 1 <= len(chunks) <= 2
                or not isinstance(document, dict)
                or not isinstance(document.get("asset"), dict)
                or document["asset"].get("version") != "2.0"
            ):
                raise W3DBatchRunnerError("GLB container is incomplete")
    except OSError as exc:
        raise W3DBatchRunnerError("GLB output could not be read") from exc
    try:
        semantic = validate_w3d_glb_semantics(
            path,
            {field: expected_counts[field] for field in _SEMANTIC_COUNT_FIELDS},
        )
    except (KeyError, TypeError, W3DGLBValidationError) as exc:
        raise W3DBatchRunnerError("GLB semantic evidence is invalid") from exc
    if (
        semantic.mesh_count < 1
        or semantic.primitive_count < 1
        or semantic.vertex_count < 1
        or semantic.triangle_count < 1
    ):
        raise W3DBatchRunnerError("GLB contains no renderable triangle geometry")
    final_size, final_digest = _hash_file(path, "GLB output")
    if (final_size, final_digest) != (size, digest):
        raise W3DBatchRunnerError("GLB output changed during validation")
    return size, digest


def _output_id(job: W3DPlannedJob) -> str:
    return f"out-{job.definition_sha256[:40]}"


def _expected_directories(paths: Sequence[str]) -> set[str]:
    result: set[str] = set()
    for value in paths:
        parts = PurePosixPath(value).parts
        for index in range(1, len(parts)):
            result.add("/".join(parts[:index]).casefold())
    return result


def _verify_exact_payload_tree(root: Path, plan: W3DJobPlan, *, manifest: bool) -> None:
    files, directories = _scan_tree(root, "W3D corpus tree")
    expected = [job.output for job in plan.jobs]
    if manifest:
        expected.append(W3D_BATCH_CORPUS_MANIFEST)
    actual = [item.relative_path for item in files]
    if sorted(actual, key=str.casefold) != sorted(expected, key=str.casefold):
        raise W3DBatchRunnerError(
            "W3D corpus does not contain the exact expected files"
        )
    if directories != _expected_directories(expected):
        raise W3DBatchRunnerError(
            "W3D corpus does not contain the exact expected directories"
        )


def _remove_empty_adapter_staging(root: Path) -> None:
    path = root / _ADAPTER_STAGING_DIRECTORY
    if not os.path.lexists(path):
        return
    files, _ = _scan_tree(path, "adapter staging tree")
    if files:
        raise W3DBatchRunnerError("adapter staging tree contains undeclared files")
    shutil.rmtree(path)


def _materialize_adapter_bundle(
    adapter: Path,
    destination: Path,
    expected: W3DTreeIdentity,
) -> Path:
    if adapter.name.casefold() == W3D_CONVERTER_MODULE_NAME.casefold():
        raise W3DBatchRunnerError("Blender adapter and converter module collide")
    try:
        destination.mkdir()
        staged_adapter = destination / adapter.name
        shutil.copyfile(adapter, staged_adapter)
        shutil.copyfile(
            adapter.with_name(W3D_CONVERTER_MODULE_NAME),
            destination / W3D_CONVERTER_MODULE_NAME,
        )
    except OSError as exc:
        raise W3DBatchRunnerError("W3D adapter bundle could not be staged") from exc
    if _snapshot_adapter_bundle(staged_adapter) != expected:
        raise W3DBatchRunnerError("staged W3D adapter bundle changed while copying")
    return staged_adapter


def _remove_execution_manifests(
    work: Path,
    batches: Sequence[W3DJobBatch],
    *,
    adapter_bundle_tree: W3DTreeIdentity | None,
    adapter_name: str | None,
) -> None:
    files, directories = _scan_tree(work, "batch manifest staging tree")
    expected_files = {f"manifests/{index:08d}.json" for index in range(len(batches))}
    expected_directories = {"manifests"}
    if adapter_bundle_tree is not None:
        if not isinstance(adapter_name, str) or not adapter_name:
            raise W3DBatchRunnerError("staged W3D adapter name is missing")
        expected_files.update(
            {
                f"adapter-bundle/{adapter_name}",
                f"adapter-bundle/{W3D_CONVERTER_MODULE_NAME}",
            }
        )
        expected_directories.add("adapter-bundle")
        if (
            _snapshot_adapter_bundle(work / "adapter-bundle" / adapter_name)
            != adapter_bundle_tree
        ):
            raise W3DBatchRunnerError(
                "staged W3D adapter bundle changed during execution"
            )
    elif adapter_name is not None:
        raise W3DBatchRunnerError("strict execution has a staged adapter name")
    if {
        item.relative_path for item in files
    } != expected_files or directories != expected_directories:
        raise W3DBatchRunnerError("batch manifest staging tree was modified")
    for index, batch in enumerate(batches):
        path = work / "manifests" / f"{index:08d}.json"
        try:
            actual = path.read_bytes()
        except OSError as exc:
            raise W3DBatchRunnerError(
                "batch manifest staging tree could not be read"
            ) from exc
        if actual != batch.manifest_bytes():
            raise W3DBatchRunnerError("batch manifest changed during execution")
    shutil.rmtree(work)


def _batch_rows(plan: W3DJobPlan) -> list[dict[str, object]]:
    return [
        {
            "jobCount": len(batch.jobs),
            "manifestSha256": batch.manifest_sha256,
        }
        for batch in plan.batches
    ]


def _execution_evidence(
    plan: W3DJobPlan,
    *,
    execute_accounted_jobs: bool,
    prepared_root_binding: W3DPreparedRootBinding | None,
) -> dict[str, object]:
    """Return the policy-bound, complete neutral parent-plan evidence."""

    if not execute_accounted_jobs:
        if prepared_root_binding is not None:
            raise W3DBatchRunnerError(
                "strict W3D execution cannot bind a prepared-root attestation"
            )
        return {}
    if prepared_root_binding is None:
        raise W3DBatchRunnerError(
            "accounted W3D execution requires a prepared-root attestation"
        )
    return {
        "executionPolicy": {"executeAccountedJobs": execute_accounted_jobs},
        "parentPlanEvidence": plan.neutral(),
        "terminals": [terminal.neutral() for terminal in plan.terminals],
        "preparedRootAttestation": prepared_root_binding.neutral(),
    }


def _toolchain_evidence(
    *,
    execute_accounted_jobs: bool,
    blender_runtime_tree: W3DTreeIdentity | None,
    adapter_bundle_tree: W3DTreeIdentity | None,
) -> dict[str, object]:
    """Return full runtime provenance without changing strict-v0 evidence."""

    values = (blender_runtime_tree, adapter_bundle_tree)
    if not execute_accounted_jobs:
        if values != (None, None):
            raise W3DBatchRunnerError(
                "strict W3D execution cannot bind accounted toolchain evidence"
            )
        return {}
    if not all(isinstance(value, W3DTreeIdentity) for value in values):
        raise W3DBatchRunnerError(
            "accounted W3D execution requires complete toolchain evidence"
        )
    assert blender_runtime_tree is not None
    assert adapter_bundle_tree is not None
    return {
        "toolchainAttestation": {
            "blenderRuntimeTree": blender_runtime_tree.neutral(),
            "adapterBundleTree": adapter_bundle_tree.neutral(),
        }
    }


def _request_sha256(
    plan: W3DJobPlan,
    blender_executable_sha256: str,
    adapter_sha256: str,
    blender_runtime_tree: W3DTreeIdentity | None,
    adapter_bundle_tree: W3DTreeIdentity | None,
    plugin_tree: W3DTreeIdentity,
    job_tree: W3DTreeIdentity,
    *,
    execute_accounted_jobs: bool,
    prepared_root_binding: W3DPreparedRootBinding | None,
) -> str:
    execution_evidence = _execution_evidence(
        plan,
        execute_accounted_jobs=execute_accounted_jobs,
        prepared_root_binding=prepared_root_binding,
    )
    toolchain_evidence = _toolchain_evidence(
        execute_accounted_jobs=execute_accounted_jobs,
        blender_runtime_tree=blender_runtime_tree,
        adapter_bundle_tree=adapter_bundle_tree,
    )
    return _canonical_sha256(
        {
            "schema": "openbfme.w3d-batch-request",
            "schemaVersion": 2 if execute_accounted_jobs else 0,
            "planHashes": {
                "catalogInputSha256": plan.catalog_input_sha256,
                "catalogMetadataSha256": plan.catalog_metadata_sha256,
                "privatePlanSha256": plan.private_plan_sha256,
                "evidenceSha256": plan.evidence_sha256,
            },
            "batchManifests": _batch_rows(plan),
            "blenderExecutableSha256": blender_executable_sha256,
            "adapterSha256": adapter_sha256,
            "pluginTree": plugin_tree.neutral(),
            "jobTree": job_tree.neutral(),
            **execution_evidence,
            **toolchain_evidence,
        }
    )


def _report_basis(
    *,
    plan: W3DJobPlan,
    blender_executable_sha256: str,
    adapter_sha256: str,
    blender_runtime_tree: W3DTreeIdentity | None,
    adapter_bundle_tree: W3DTreeIdentity | None,
    plugin_tree: W3DTreeIdentity,
    job_tree: W3DTreeIdentity,
    request_sha256: str,
    outputs: Sequence[W3DOutputEvidence],
    execute_accounted_jobs: bool,
    prepared_root_binding: W3DPreparedRootBinding | None,
    published: bool,
) -> dict[str, object]:
    job_complete = published and len(outputs) == len(plan.jobs)
    complete = job_complete and not plan.terminals and plan.source_accounting_complete
    execution_evidence = _execution_evidence(
        plan,
        execute_accounted_jobs=execute_accounted_jobs,
        prepared_root_binding=prepared_root_binding,
    )
    toolchain_evidence = _toolchain_evidence(
        execute_accounted_jobs=execute_accounted_jobs,
        blender_runtime_tree=blender_runtime_tree,
        adapter_bundle_tree=adapter_bundle_tree,
    )
    return {
        "schema": W3D_BATCH_CORPUS_SCHEMA,
        "schemaVersion": (
            W3D_BATCH_ACCOUNTED_CORPUS_VERSION
            if execute_accounted_jobs
            else W3D_BATCH_CORPUS_VERSION
        ),
        "summary": {
            "sourceCount": plan.source_count,
            "consumedSourceCount": plan.consumed_source_count,
            "jobCount": len(plan.jobs),
            "batchCount": len(plan.batches),
            "terminalCount": len(plan.terminals),
            "outputCount": len(outputs),
            "sourceAccountingComplete": plan.source_accounting_complete,
            "jobConversionComplete": job_complete,
            "conversionComplete": complete,
            "corpusComplete": complete,
            "published": published,
        },
        "hashes": {
            "catalogInputSha256": plan.catalog_input_sha256,
            "catalogMetadataSha256": plan.catalog_metadata_sha256,
            "privatePlanSha256": plan.private_plan_sha256,
            "planEvidenceSha256": plan.evidence_sha256,
            "requestSha256": request_sha256,
            "blenderExecutableSha256": blender_executable_sha256,
            "adapterSha256": adapter_sha256,
            "pluginTreeSha256": plugin_tree.sha256,
            "jobTreeSha256": job_tree.sha256,
        },
        "tools": {
            "pluginTreeFileCount": plugin_tree.file_count,
            "pluginTreeBytes": plugin_tree.byte_length,
            "jobTreeFileCount": job_tree.file_count,
            "jobTreeBytes": job_tree.byte_length,
        },
        "batches": _batch_rows(plan),
        "outputs": [item.neutral() for item in outputs],
        **execution_evidence,
        **toolchain_evidence,
    }


def _make_report(
    root: Path,
    *,
    plan: W3DJobPlan,
    blender_executable_sha256: str,
    adapter_sha256: str,
    blender_runtime_tree: W3DTreeIdentity | None,
    adapter_bundle_tree: W3DTreeIdentity | None,
    plugin_tree: W3DTreeIdentity,
    job_tree: W3DTreeIdentity,
    request_sha256: str,
    outputs: tuple[W3DOutputEvidence, ...],
    execute_accounted_jobs: bool,
    prepared_root_binding: W3DPreparedRootBinding | None,
    reused: bool,
) -> W3DBatchCorpusReport:
    basis = _report_basis(
        plan=plan,
        blender_executable_sha256=blender_executable_sha256,
        adapter_sha256=adapter_sha256,
        blender_runtime_tree=blender_runtime_tree,
        adapter_bundle_tree=adapter_bundle_tree,
        plugin_tree=plugin_tree,
        job_tree=job_tree,
        request_sha256=request_sha256,
        outputs=outputs,
        execute_accounted_jobs=execute_accounted_jobs,
        prepared_root_binding=prepared_root_binding,
        published=True,
    )
    identity = _canonical_sha256(basis)
    manifest_path = root / W3D_BATCH_CORPUS_MANIFEST
    manifest_sha256 = hashlib.sha256(
        _canonical_json_bytes({**basis, "identitySha256": identity})
    ).hexdigest()
    return W3DBatchCorpusReport(
        output_root=root,
        manifest_path=manifest_path,
        manifest_sha256=manifest_sha256,
        request_sha256=request_sha256,
        identity_sha256=identity,
        blender_executable_sha256=blender_executable_sha256,
        adapter_sha256=adapter_sha256,
        blender_runtime_tree=blender_runtime_tree,
        adapter_bundle_tree=adapter_bundle_tree,
        plugin_tree=plugin_tree,
        job_tree=job_tree,
        plan=plan,
        outputs=outputs,
        execute_accounted_jobs=execute_accounted_jobs,
        prepared_root_binding=prepared_root_binding,
        published=True,
        reused=reused,
    )


def _parse_output(raw: object, job: W3DPlannedJob, path: Path) -> W3DOutputEvidence:
    if not isinstance(raw, dict) or set(raw) != {
        "outputId",
        "assetKind",
        "bytes",
        "sha256",
        "adapterReportSha256",
        "counts",
    }:
        raise W3DBatchRunnerError("W3D corpus output evidence is malformed")
    counts = raw.get("counts")
    if (
        raw.get("outputId") != _output_id(job)
        or raw.get("assetKind") != job.asset_kind
        or not _is_int(raw.get("bytes"))
        or not _is_sha256(raw.get("sha256"))
        or not _is_sha256(raw.get("adapterReportSha256"))
        or not isinstance(counts, dict)
        or set(counts) != set(_COUNT_FIELDS)
        or any(not _is_int(counts.get(field)) for field in _COUNT_FIELDS)
        or counts.get("animations") != len(job.animations)
    ):
        raise W3DBatchRunnerError("W3D corpus output evidence is invalid")
    size, digest = _validate_glb(path, counts)
    if size != raw["bytes"] or digest != raw["sha256"]:
        raise W3DBatchRunnerError("W3D corpus GLB identity changed")
    return W3DOutputEvidence(
        output_id=raw["outputId"],
        asset_kind=job.asset_kind,
        byte_length=size,
        sha256=digest,
        adapter_report_sha256=raw["adapterReportSha256"],
        counts={field: counts[field] for field in _COUNT_FIELDS},
    )


def _read_canonical_manifest(path: Path) -> tuple[dict[str, object], bytes]:
    _assert_single_link_file(path, "W3D corpus manifest")
    size, digest = _hash_file(path, "W3D corpus manifest")
    if size > MAX_CORPUS_MANIFEST_BYTES:
        raise W3DBatchRunnerError("W3D corpus manifest exceeds its size bound")
    try:
        raw = path.read_bytes()
        text = raw.decode("utf-8")
        document = _strict_json(text.rstrip("\n"))
    except OSError as exc:
        raise W3DBatchRunnerError("W3D corpus manifest could not be read") from exc
    if not raw.endswith(b"\n") or not isinstance(document, dict):
        raise W3DBatchRunnerError("W3D corpus manifest is not canonical")
    if hashlib.sha256(raw).hexdigest() != digest:
        raise W3DBatchRunnerError("W3D corpus manifest changed while reading")
    if raw != _canonical_json_bytes(document):
        raise W3DBatchRunnerError("W3D corpus manifest is not canonical")
    return document, raw


def _verify_corpus(
    root: Path,
    *,
    plan: W3DJobPlan,
    blender_executable_sha256: str,
    adapter_sha256: str,
    blender_runtime_tree: W3DTreeIdentity | None,
    adapter_bundle_tree: W3DTreeIdentity | None,
    plugin_tree: W3DTreeIdentity,
    job_tree: W3DTreeIdentity,
    request_sha256: str,
    execute_accounted_jobs: bool,
    prepared_root_binding: W3DPreparedRootBinding | None,
    reused: bool,
) -> W3DBatchCorpusReport:
    _verify_exact_payload_tree(root, plan, manifest=True)
    document, raw_bytes = _read_canonical_manifest(root / W3D_BATCH_CORPUS_MANIFEST)
    raw_outputs = document.get("outputs")
    if not isinstance(raw_outputs, list) or len(raw_outputs) != len(plan.jobs):
        raise W3DBatchRunnerError("W3D corpus output cardinality is invalid")
    outputs = tuple(
        _parse_output(raw, job, _safe_child(root, job.output))
        for raw, job in zip(raw_outputs, plan.jobs, strict=True)
    )
    report = _make_report(
        root,
        plan=plan,
        blender_executable_sha256=blender_executable_sha256,
        adapter_sha256=adapter_sha256,
        blender_runtime_tree=blender_runtime_tree,
        adapter_bundle_tree=adapter_bundle_tree,
        plugin_tree=plugin_tree,
        job_tree=job_tree,
        request_sha256=request_sha256,
        outputs=outputs,
        execute_accounted_jobs=execute_accounted_jobs,
        prepared_root_binding=prepared_root_binding,
        reused=reused,
    )
    if (
        document != report.neutral()
        or hashlib.sha256(raw_bytes).hexdigest() != report.manifest_sha256
    ):
        raise W3DBatchRunnerError("W3D corpus manifest does not match its request")
    return report


def _remove_owned_tree(path: Path, parent: Path, prefix: str) -> None:
    if path.parent != parent or not path.name.startswith(prefix):
        raise W3DBatchRunnerError("refused to remove an unowned transaction path")
    _scan_tree(path, "W3D transaction tree")
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


def run_w3d_job_plan(
    plan: W3DJobPlan,
    blender_executable: Path | str,
    plugin_root: Path | str,
    job_root: Path | str,
    adapter_path: Path | str,
    destination: Path | str,
    *,
    blender_executable_sha256: str,
    adapter_sha256: str,
    plugin_tree_sha256: str,
    job_tree_sha256: str | None = None,
    blender_runtime_tree_sha256: str | None = None,
    adapter_bundle_tree_sha256: str | None = None,
    execute_accounted_jobs: bool = False,
    prepared_root_report: W3DPreparedRootReport | None = None,
    force: bool = False,
    timeout_seconds: float = DEFAULT_BATCH_TIMEOUT_SECONDS,
    stdout_limit_bytes: int = DEFAULT_STDOUT_LIMIT_BYTES,
    stderr_limit_bytes: int = DEFAULT_STDERR_LIMIT_BYTES,
    subprocess_runner: SubprocessRunner | None = None,
) -> W3DBatchCorpusReport:
    """Execute every bounded batch and atomically publish verified GLBs.

    ``subprocess_runner`` receives the exact command tuple plus the three bound
    keywords and must return :class:`W3DProcessResult` (a compatible
    ``subprocess.CompletedProcess`` is also accepted for tests).  By default, a
    plan with any terminal is rejected before a process can be launched.  The
    explicit ``execute_accounted_jobs`` policy permits only jobs from an
    internally exact consumed-plus-terminal parent plan and preserves those
    terminals without claiming complete corpus conversion.
    """

    if not isinstance(force, bool):
        raise TypeError("W3D batch force flag must be a boolean")
    if not isinstance(execute_accounted_jobs, bool):
        raise TypeError("W3D accounted-job execution flag must be a boolean")
    _validate_limits(timeout_seconds, stdout_limit_bytes, stderr_limit_bytes)
    _validate_plan(plan, execute_accounted_jobs=execute_accounted_jobs)
    if execute_accounted_jobs and prepared_root_report is None:
        raise W3DBatchRunnerError(
            "accounted W3D execution requires a prepared-root attestation"
        )
    if not execute_accounted_jobs and prepared_root_report is not None:
        raise W3DBatchRunnerError(
            "strict W3D execution rejects a prepared-root attestation"
        )
    accounted_tool_pins = (
        blender_runtime_tree_sha256,
        adapter_bundle_tree_sha256,
    )
    if execute_accounted_jobs and any(value is None for value in accounted_tool_pins):
        raise W3DBatchRunnerError(
            "accounted W3D execution requires complete runtime-tree pins"
        )
    if not execute_accounted_jobs and accounted_tool_pins != (None, None):
        raise W3DBatchRunnerError(
            "strict W3D execution rejects accounted runtime-tree pins"
        )

    blender = _ordinary_file(blender_executable, "Blender executable")
    adapter = _ordinary_file(adapter_path, "Blender adapter")
    plugin = _ordinary_directory(plugin_root, "plugin root")
    jobs = _ordinary_directory(job_root, "job root")
    prepared_root_binding: W3DPreparedRootBinding | None = None
    if prepared_root_report is not None:
        try:
            validated_prepared_root = validate_w3d_prepared_root(
                prepared_root_report,
                plan,
                jobs,
                execute_accounted_jobs=execute_accounted_jobs,
            )
        except (TypeError, W3DPreparedRootError) as exc:
            raise W3DBatchRunnerError(
                "prepared W3D execution root failed attestation"
            ) from exc
        prepared_root_binding = validated_prepared_root.runner_binding()
    blender_size, blender_digest = _hash_file(blender, "Blender executable")
    adapter_size, adapter_digest = _hash_file(adapter, "Blender adapter")
    del blender_size, adapter_size
    plugin_files, _ = _scan_tree(plugin, "plugin tree")
    if not plugin_files:
        raise W3DBatchRunnerError("plugin tree contains no ordinary files")
    plugin_tree = _tree_identity(plugin_files)
    job_tree = _snapshot_job_tree(jobs, plan)
    blender_runtime_tree: W3DTreeIdentity | None = None
    adapter_bundle_tree: W3DTreeIdentity | None = None
    if execute_accounted_jobs:
        blender_runtime_files, _ = _scan_tree(
            blender.parent,
            "Blender runtime tree",
        )
        if not blender_runtime_files:
            raise W3DBatchRunnerError("Blender runtime tree contains no ordinary files")
        blender_runtime_tree = _tree_identity(blender_runtime_files)
        adapter_bundle_tree = _snapshot_adapter_bundle(adapter)
    if (
        prepared_root_binding is not None
        and job_tree.sha256 != prepared_root_binding.runner_tree_sha256
    ):
        raise W3DBatchRunnerError(
            "prepared W3D execution root tree mismatches its attestation"
        )
    _validate_pin(blender_executable_sha256, blender_digest, "Blender executable")
    _validate_pin(adapter_sha256, adapter_digest, "Blender adapter")
    _validate_pin(plugin_tree_sha256, plugin_tree.sha256, "plugin tree")
    if job_tree_sha256 is not None:
        _validate_pin(job_tree_sha256, job_tree.sha256, "job tree")
    if blender_runtime_tree is not None and adapter_bundle_tree is not None:
        assert blender_runtime_tree_sha256 is not None
        assert adapter_bundle_tree_sha256 is not None
        _validate_pin(
            blender_runtime_tree_sha256,
            blender_runtime_tree.sha256,
            "Blender runtime tree",
        )
        _validate_pin(
            adapter_bundle_tree_sha256,
            adapter_bundle_tree.sha256,
            "W3D adapter bundle tree",
        )

    destination_path = Path(destination).expanduser().absolute()
    _assert_no_link_chain(destination_path.parent)
    try:
        destination_path.parent.mkdir(parents=True, exist_ok=True)
        parent = destination_path.parent.resolve(strict=True)
    except OSError as exc:
        raise W3DBatchRunnerError("W3D corpus parent could not be prepared") from exc
    destination_path = parent / destination_path.name
    if not destination_path.name or destination_path.name in {".", ".."}:
        raise W3DBatchRunnerError("W3D corpus destination is invalid")
    for protected in (plugin, jobs, blender.parent, adapter.parent):
        if _paths_overlap(destination_path, protected):
            raise W3DBatchRunnerError("W3D corpus destination overlaps an input")
    if _paths_overlap(plugin, jobs):
        raise W3DBatchRunnerError("plugin and job roots overlap")
    if blender == adapter:
        raise W3DBatchRunnerError("Blender executable and adapter must be distinct")
    for file_path in (blender, adapter):
        if plugin == file_path or plugin in file_path.parents:
            raise W3DBatchRunnerError("pinned tool file overlaps the plugin tree")
        if jobs == file_path or jobs in file_path.parents:
            raise W3DBatchRunnerError("pinned tool file overlaps the job tree")

    request_sha256 = _request_sha256(
        plan,
        blender_digest,
        adapter_digest,
        blender_runtime_tree,
        adapter_bundle_tree,
        plugin_tree,
        job_tree,
        execute_accounted_jobs=execute_accounted_jobs,
        prepared_root_binding=prepared_root_binding,
    )
    if os.path.lexists(destination_path):
        if _is_link_like(destination_path) or not destination_path.is_dir():
            raise W3DBatchReuseError("existing W3D corpus is not an ordinary directory")
        if force:
            _scan_tree(destination_path, "existing W3D corpus")
        if not force:
            try:
                return _verify_corpus(
                    destination_path,
                    plan=plan,
                    blender_executable_sha256=blender_digest,
                    adapter_sha256=adapter_digest,
                    blender_runtime_tree=blender_runtime_tree,
                    adapter_bundle_tree=adapter_bundle_tree,
                    plugin_tree=plugin_tree,
                    job_tree=job_tree,
                    request_sha256=request_sha256,
                    execute_accounted_jobs=execute_accounted_jobs,
                    prepared_root_binding=prepared_root_binding,
                    reused=True,
                )
            except W3DBatchRunnerError as exc:
                raise W3DBatchReuseError(
                    "existing W3D corpus failed complete verification"
                ) from exc

    runner = subprocess_runner or _default_subprocess_runner
    token = uuid.uuid4().hex
    stage = parent / f".{destination_path.name}.staging-{token}"
    backup = parent / f".{destination_path.name}.backup-{token}"
    work = stage / ".work"
    manifests = work / "manifests"
    try:
        manifests.mkdir(parents=True)
    except OSError as exc:
        raise W3DBatchRunnerError("W3D batch staging could not be created") from exc
    execution_adapter = adapter
    staged_adapter_name: str | None = None

    marker_rows: dict[str, dict[str, object]] = {}
    published = False
    try:
        if adapter_bundle_tree is not None:
            execution_adapter = _materialize_adapter_bundle(
                adapter,
                work / "adapter-bundle",
                adapter_bundle_tree,
            )
            staged_adapter_name = adapter.name
        for index, batch in enumerate(plan.batches):
            manifest_path = manifests / f"{index:08d}.json"
            manifest_bytes = batch.manifest_bytes()
            if hashlib.sha256(manifest_bytes).hexdigest() != batch.manifest_sha256:
                raise W3DBatchRunnerError("W3D batch manifest changed before launch")
            manifest_path.write_bytes(manifest_bytes)
            if manifest_path.read_bytes() != manifest_bytes:
                raise W3DBatchRunnerError("W3D batch manifest write was not exact")
            command = (
                str(blender),
                "--background",
                "--factory-startup",
                "--python",
                str(execution_adapter),
                "--",
                "--manifest",
                str(manifest_path.resolve(strict=True)),
                "--plugin-root",
                str(plugin),
                "--job-root",
                str(jobs),
                "--output-root",
                str(stage.resolve(strict=True)),
            )
            result = _run_process(
                runner,
                command,
                timeout_seconds=timeout_seconds,
                stdout_limit_bytes=stdout_limit_bytes,
                stderr_limit_bytes=stderr_limit_bytes,
            )
            for row in _parse_markers(result, batch):
                job_id = row["job_id"]
                if not isinstance(job_id, str) or job_id in marker_rows:
                    raise W3DBatchRunnerError("duplicate job evidence across batches")
                marker_rows[job_id] = row

        _remove_empty_adapter_staging(stage)
        _remove_execution_manifests(
            work,
            plan.batches,
            adapter_bundle_tree=adapter_bundle_tree,
            adapter_name=staged_adapter_name,
        )
        if _snapshot_job_tree(jobs, plan) != job_tree:
            raise W3DBatchRunnerError("job root changed during conversion")
        if prepared_root_report is not None:
            try:
                revalidated_prepared_root = validate_w3d_prepared_root(
                    prepared_root_report,
                    plan,
                    jobs,
                    execute_accounted_jobs=execute_accounted_jobs,
                )
            except (TypeError, W3DPreparedRootError) as exc:
                raise W3DBatchRunnerError(
                    "prepared W3D execution root changed during conversion"
                ) from exc
            if revalidated_prepared_root.runner_binding() != prepared_root_binding:
                raise W3DBatchRunnerError(
                    "prepared W3D execution root attestation changed during conversion"
                )
        if _tree_identity(_scan_tree(plugin, "plugin tree")[0]) != plugin_tree:
            raise W3DBatchRunnerError("plugin tree changed during conversion")
        if _hash_file(blender, "Blender executable")[1] != blender_digest:
            raise W3DBatchRunnerError("Blender executable changed during conversion")
        if _hash_file(adapter, "Blender adapter")[1] != adapter_digest:
            raise W3DBatchRunnerError("Blender adapter changed during conversion")
        if blender_runtime_tree is not None:
            current_blender_files, _ = _scan_tree(
                blender.parent,
                "Blender runtime tree",
            )
            if _tree_identity(current_blender_files) != blender_runtime_tree:
                raise W3DBatchRunnerError(
                    "Blender runtime tree changed during conversion"
                )
        if (
            adapter_bundle_tree is not None
            and _snapshot_adapter_bundle(adapter) != adapter_bundle_tree
        ):
            raise W3DBatchRunnerError("W3D adapter bundle changed during conversion")

        _verify_exact_payload_tree(stage, plan, manifest=False)
        outputs: list[W3DOutputEvidence] = []
        for job in plan.jobs:
            row = marker_rows.get(job.job_id)
            if row is None:
                raise W3DBatchRunnerError("planned job has no success evidence")
            size, digest = _validate_glb(
                _safe_child(stage, job.output),
                row,
            )
            if row["output_sha256"] != digest:
                raise W3DBatchRunnerError("GLB hash does not match adapter evidence")
            outputs.append(
                W3DOutputEvidence(
                    output_id=_output_id(job),
                    asset_kind=job.asset_kind,
                    byte_length=size,
                    sha256=digest,
                    adapter_report_sha256=row["adapter_report_sha256"],
                    counts={field: row[field] for field in _COUNT_FIELDS},
                )
            )
        if len(marker_rows) != len(plan.jobs):
            raise W3DBatchRunnerError("conversion returned unknown job evidence")

        staged_report = _make_report(
            stage,
            plan=plan,
            blender_executable_sha256=blender_digest,
            adapter_sha256=adapter_digest,
            blender_runtime_tree=blender_runtime_tree,
            adapter_bundle_tree=adapter_bundle_tree,
            plugin_tree=plugin_tree,
            job_tree=job_tree,
            request_sha256=request_sha256,
            outputs=tuple(outputs),
            execute_accounted_jobs=execute_accounted_jobs,
            prepared_root_binding=prepared_root_binding,
            reused=False,
        )
        staged_report.manifest_path.write_bytes(
            _canonical_json_bytes(staged_report.neutral())
        )
        _verify_corpus(
            stage,
            plan=plan,
            blender_executable_sha256=blender_digest,
            adapter_sha256=adapter_digest,
            blender_runtime_tree=blender_runtime_tree,
            adapter_bundle_tree=adapter_bundle_tree,
            plugin_tree=plugin_tree,
            job_tree=job_tree,
            request_sha256=request_sha256,
            execute_accounted_jobs=execute_accounted_jobs,
            prepared_root_binding=prepared_root_binding,
            reused=False,
        )
        _publish(stage, destination_path, backup)
        try:
            report = _verify_corpus(
                destination_path,
                plan=plan,
                blender_executable_sha256=blender_digest,
                adapter_sha256=adapter_digest,
                blender_runtime_tree=blender_runtime_tree,
                adapter_bundle_tree=adapter_bundle_tree,
                plugin_tree=plugin_tree,
                job_tree=job_tree,
                request_sha256=request_sha256,
                execute_accounted_jobs=execute_accounted_jobs,
                prepared_root_binding=prepared_root_binding,
                reused=False,
            )
        except Exception:
            if os.path.lexists(destination_path):
                os.replace(destination_path, stage)
            if os.path.lexists(backup):
                os.replace(backup, destination_path)
            raise
        published = True
        if os.path.lexists(backup):
            # The new corpus has already survived complete post-publish
            # verification.  Backup removal is therefore cleanup, not part of
            # the commit: surfacing its failure would report a failed request
            # even though the new corpus is canonical at the destination.
            try:
                _remove_owned_tree(
                    backup,
                    parent,
                    f".{destination_path.name}.backup-",
                )
            except (OSError, W3DBatchRunnerError):
                pass
        return report
    finally:
        if os.path.lexists(stage):
            _remove_owned_tree(stage, parent, f".{destination_path.name}.staging-")
        if published and os.path.lexists(backup):
            try:
                _remove_owned_tree(
                    backup,
                    parent,
                    f".{destination_path.name}.backup-",
                )
            except (OSError, W3DBatchRunnerError):
                pass


build_w3d_batch_corpus = run_w3d_job_plan
run_batches = run_w3d_job_plan


__all__ = [
    "DEFAULT_BATCH_TIMEOUT_SECONDS",
    "DEFAULT_STDERR_LIMIT_BYTES",
    "DEFAULT_STDOUT_LIMIT_BYTES",
    "W3D_BATCH_CORPUS_MANIFEST",
    "W3D_BATCH_CORPUS_SCHEMA",
    "W3D_BATCH_CORPUS_VERSION",
    "W3D_BATCH_ACCOUNTED_CORPUS_VERSION",
    "W3DBatchConversionError",
    "W3DBatchCorpusReport",
    "W3DBatchReuseError",
    "W3DBatchRunnerError",
    "W3DOutputEvidence",
    "W3DProcessResult",
    "W3DTreeIdentity",
    "build_w3d_batch_corpus",
    "hash_w3d_adapter_bundle",
    "hash_w3d_tool_tree",
    "run_batches",
    "run_w3d_job_plan",
]
