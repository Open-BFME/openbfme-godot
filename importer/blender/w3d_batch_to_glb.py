"""Blender-side bounded batch adapter for W3D-to-GLB conversion.

The coordinator supplies three explicit roots: a pinned plugin root, a private
job root containing copied W3D inputs, and a separate private output root.  The
manifest contains only safe relative paths.  Result markers deliberately omit
source paths, authored names, and exception text.
"""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import hashlib
import importlib.util
import json
import os
from pathlib import Path, PurePosixPath
import re
import struct
import sys
from typing import Any, Callable


BATCH_MANIFEST_SCHEMA = "openbfme.w3d-batch-jobs"
BATCH_MANIFEST_VERSION = 1
BATCH_REPORT_SCHEMA = "openbfme.w3d-batch-report"
BATCH_REPORT_VERSION = 1
ADAPTER_REPORT_SCHEMA = "openbfme.w3d-adapter-report"
ADAPTER_REPORT_VERSION = 2
MAX_MANIFEST_BYTES = 1024 * 1024
MAX_BATCH_JOBS = 256
MAX_ANIMATIONS_PER_JOB = 256
MAX_REQUIRED_EQUIPMENT = 16
MAX_OPTIONAL_MESH_EXCLUSIONS = 64
MAX_RELATIVE_PATH_LENGTH = 512
MAX_GLB_JSON_BYTES = 64 * 1024 * 1024
STAGING_DIRECTORY_NAME = ".openbfme-w3d-batch-staging"
SUPPORTED_ASSET_KINDS = {"animated", "hierarchical", "static"}
SUPPORTED_EQUIPMENT_ROLES = {"right-hand-weapon", "left-hand-shield"}
JOB_ID_PATTERN = re.compile(r"^[a-z0-9](?:[a-z0-9_-]{0,62}[a-z0-9])?$")
CLEAN_MESH_IDENTIFIER_PATTERN = re.compile(r"^[a-z0-9](?:[a-z0-9_]{0,126}[a-z0-9])?$")
JOB_FIELDS = {
    "job_id",
    "model",
    "asset_kind",
    "animations",
    "required_equipment",
    "excluded_optional_meshes",
    "proven_root_rigid_bake",
    "output",
}
COUNT_REPORT_FIELDS = (
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
FAILURE_PHASES = frozenset(
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
FAILURE_KINDS = frozenset(
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


class BatchManifestError(ValueError):
    """The bounded job manifest or its declared filesystem is invalid."""


@dataclass(frozen=True)
class BatchJob:
    job_id: str
    model: PurePosixPath
    asset_kind: str
    animations: tuple[PurePosixPath, ...]
    embedded_model_animation: bool
    required_equipment: tuple[str, ...]
    excluded_optional_meshes: tuple[str, ...]
    proven_root_rigid_bake: bool
    output: PurePosixPath


Converter = Callable[..., dict[str, Any]]
Emitter = Callable[[str], None]
ConversionPhaseErrorType = type[BaseException]


def parse_args() -> argparse.Namespace:
    argv = sys.argv[sys.argv.index("--") + 1 :] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", type=Path, required=True)
    parser.add_argument("--plugin-root", type=Path, required=True)
    parser.add_argument("--job-root", type=Path, required=True)
    parser.add_argument("--output-root", type=Path, required=True)
    return parser.parse_args(argv)


def canonical_json_bytes(value: Any) -> bytes:
    return (
        json.dumps(value, ensure_ascii=False, separators=(",", ":"), sort_keys=True)
        + "\n"
    ).encode("utf-8")


def _is_link_like(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(is_junction and is_junction())


def _assert_existing_path_chain_has_no_links(path: Path) -> None:
    absolute = path.expanduser().absolute()
    candidates = list(reversed((absolute, *absolute.parents)))
    for candidate in candidates:
        if _is_link_like(candidate):
            raise BatchManifestError("declared filesystem path contains a link")


def _validated_directory(path: Path, label: str) -> Path:
    _assert_existing_path_chain_has_no_links(path)
    resolved = path.expanduser().resolve()
    if not resolved.is_dir() or _is_link_like(resolved):
        raise BatchManifestError(f"{label} must be an existing ordinary directory")
    return resolved


def _paths_overlap(left: Path, right: Path) -> bool:
    return left == right or left in right.parents or right in left.parents


def _safe_relative_path(value: Any, label: str, suffix: str) -> PurePosixPath:
    if not isinstance(value, str) or not value or len(value) > MAX_RELATIVE_PATH_LENGTH:
        raise BatchManifestError(f"{label} must be a bounded relative path")
    if "\\" in value or "\x00" in value or ":" in value:
        raise BatchManifestError(f"{label} must use safe POSIX path syntax")
    candidate = PurePosixPath(value)
    if (
        candidate.is_absolute()
        or candidate.as_posix() != value
        or any(part in {"", ".", ".."} for part in candidate.parts)
    ):
        raise BatchManifestError(f"{label} must be a canonical relative path")
    if candidate.suffix.casefold() != suffix:
        raise BatchManifestError(f"{label} must end in {suffix}")
    return candidate


def _bounded_unique_strings(
    value: Any,
    *,
    label: str,
    maximum: int,
    allowed: set[str] | None = None,
) -> tuple[str, ...]:
    if not isinstance(value, list) or len(value) > maximum:
        raise BatchManifestError(
            f"{label} must be an array of at most {maximum} strings"
        )
    if any(not isinstance(item, str) or not item for item in value):
        raise BatchManifestError(f"{label} must contain non-empty strings")
    if len(set(value)) != len(value):
        raise BatchManifestError(f"{label} contains duplicates")
    if allowed is not None and any(item not in allowed for item in value):
        raise BatchManifestError(f"{label} contains an unsupported value")
    return tuple(value)


def parse_manifest_document(document: Any) -> tuple[BatchJob, ...]:
    if not isinstance(document, dict) or set(document) != {
        "manifest_schema",
        "manifest_version",
        "jobs",
    }:
        raise BatchManifestError("batch manifest has unexpected top-level fields")
    if document["manifest_schema"] != BATCH_MANIFEST_SCHEMA:
        raise BatchManifestError("batch manifest schema is unsupported")
    if (
        type(document["manifest_version"]) is not int
        or document["manifest_version"] != BATCH_MANIFEST_VERSION
    ):
        raise BatchManifestError("batch manifest version is unsupported")
    rows = document["jobs"]
    if not isinstance(rows, list) or not 1 <= len(rows) <= MAX_BATCH_JOBS:
        raise BatchManifestError(
            f"batch manifest must contain between 1 and {MAX_BATCH_JOBS} jobs"
        )

    jobs: list[BatchJob] = []
    seen_ids: set[str] = set()
    seen_outputs: set[str] = set()
    for index, row in enumerate(rows):
        label = f"job {index}"
        if not isinstance(row, dict) or set(row) != JOB_FIELDS:
            raise BatchManifestError(f"{label} has unexpected fields")
        job_id = row["job_id"]
        if not isinstance(job_id, str) or not JOB_ID_PATTERN.fullmatch(job_id):
            raise BatchManifestError(f"{label} has an invalid job id")
        if job_id in seen_ids:
            raise BatchManifestError("batch manifest contains duplicate job ids")
        seen_ids.add(job_id)

        model = _safe_relative_path(row["model"], f"{label} model", ".w3d")
        output = _safe_relative_path(row["output"], f"{label} output", ".glb")
        if output.parts[0].casefold() == STAGING_DIRECTORY_NAME.casefold():
            raise BatchManifestError(f"{label} output uses the reserved staging path")
        output_key = output.as_posix().casefold()
        if output_key in seen_outputs:
            raise BatchManifestError("batch manifest contains duplicate outputs")
        seen_outputs.add(output_key)
        animations_value = row["animations"]
        if (
            not isinstance(animations_value, list)
            or len(animations_value) > MAX_ANIMATIONS_PER_JOB
        ):
            raise BatchManifestError(
                f"{label} animations must contain at most {MAX_ANIMATIONS_PER_JOB} paths"
            )
        animations = tuple(
            _safe_relative_path(item, f"{label} animation", ".w3d")
            for item in animations_value
        )
        animation_keys = [item.as_posix().casefold() for item in animations]
        if len(set(animation_keys)) != len(animation_keys):
            raise BatchManifestError(f"{label} animations contain duplicates")
        required_equipment = _bounded_unique_strings(
            row["required_equipment"],
            label=f"{label} required equipment",
            maximum=MAX_REQUIRED_EQUIPMENT,
            allowed=SUPPORTED_EQUIPMENT_ROLES,
        )
        excluded_optional_meshes = _bounded_unique_strings(
            row["excluded_optional_meshes"],
            label=f"{label} excluded optional meshes",
            maximum=MAX_OPTIONAL_MESH_EXCLUSIONS,
        )
        if any(
            not CLEAN_MESH_IDENTIFIER_PATTERN.fullmatch(identifier)
            for identifier in excluded_optional_meshes
        ):
            raise BatchManifestError(
                f"{label} excluded optional meshes contain an invalid identifier"
            )
        asset_kind = row["asset_kind"]
        if not isinstance(asset_kind, str) or asset_kind not in SUPPORTED_ASSET_KINDS:
            raise BatchManifestError(f"{label} has an unsupported asset kind")
        model_key = model.as_posix().casefold()
        model_animation_overlap = model_key in set(animation_keys)
        embedded_model_animation = (
            asset_kind == "animated" and len(animations) == 1 and animations[0] == model
        )
        if model_animation_overlap != embedded_model_animation:
            raise BatchManifestError(f"{label} model and animation inputs overlap")
        proven_root_rigid_bake = row["proven_root_rigid_bake"]
        if type(proven_root_rigid_bake) is not bool:
            raise BatchManifestError(f"{label} root-rigid bake flag must be boolean")
        if asset_kind == "animated" and not animations:
            raise BatchManifestError(f"{label} animated conversion requires animations")
        if asset_kind != "animated" and animations:
            raise BatchManifestError(f"{label} non-animated conversion has animations")
        if asset_kind != "animated" and required_equipment:
            raise BatchManifestError(f"{label} non-animated conversion has equipment")
        if proven_root_rigid_bake and asset_kind != "hierarchical":
            raise BatchManifestError(
                f"{label} root-rigid bake requires hierarchical kind"
            )

        jobs.append(
            BatchJob(
                job_id=job_id,
                model=model,
                asset_kind=asset_kind,
                animations=animations,
                embedded_model_animation=embedded_model_animation,
                required_equipment=required_equipment,
                excluded_optional_meshes=excluded_optional_meshes,
                proven_root_rigid_bake=proven_root_rigid_bake,
                output=output,
            )
        )
    return tuple(jobs)


def load_canonical_manifest(path: Path) -> tuple[tuple[BatchJob, ...], str]:
    _assert_existing_path_chain_has_no_links(path)
    if not path.is_file() or _is_link_like(path):
        raise BatchManifestError("batch manifest must be an ordinary file")
    size = path.stat().st_size
    if size < 2 or size > MAX_MANIFEST_BYTES:
        raise BatchManifestError("batch manifest size is outside the accepted bound")
    payload = path.read_bytes()
    try:
        document = json.loads(payload.decode("utf-8"))
    except (UnicodeDecodeError, json.JSONDecodeError) as exc:
        raise BatchManifestError("batch manifest is not UTF-8 JSON") from exc
    if payload != canonical_json_bytes(document):
        raise BatchManifestError("batch manifest is not canonical JSON")
    return parse_manifest_document(document), hashlib.sha256(payload).hexdigest()


def _join_relative(root: Path, relative: PurePosixPath) -> Path:
    return root.joinpath(*relative.parts)


def _validated_input(root: Path, relative: PurePosixPath) -> Path:
    candidate = _join_relative(root, relative)
    _assert_existing_path_chain_has_no_links(candidate)
    if not candidate.is_file() or _is_link_like(candidate):
        raise BatchManifestError("declared W3D input is not an ordinary file")
    resolved = candidate.resolve()
    if root not in resolved.parents:
        raise BatchManifestError("declared W3D input escapes the job root")
    return resolved


def _prepare_output_parent(output_root: Path, relative: PurePosixPath) -> Path:
    candidate = _join_relative(output_root, relative)
    _assert_existing_path_chain_has_no_links(candidate.parent)
    candidate.parent.mkdir(parents=True, exist_ok=True)
    _assert_existing_path_chain_has_no_links(candidate.parent)
    resolved_parent = candidate.parent.resolve()
    if output_root != resolved_parent and output_root not in resolved_parent.parents:
        raise BatchManifestError("declared output escapes the output root")
    if _is_link_like(candidate) or (candidate.exists() and not candidate.is_file()):
        raise BatchManifestError("declared output is not an ordinary file")
    return candidate


def _sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def _validate_glb_container(path: Path) -> None:
    if not path.is_file() or _is_link_like(path):
        raise RuntimeError("converter did not create an ordinary GLB")
    size = path.stat().st_size
    if size < 20:
        raise RuntimeError("converter created a truncated GLB")
    with path.open("rb") as handle:
        header = handle.read(12)
        magic, version, declared_length = struct.unpack("<4sII", header)
        if magic != b"glTF" or version != 2 or declared_length != size:
            raise RuntimeError("converter created an invalid GLB container")

        chunk_index = 0
        while handle.tell() < size:
            chunk_header = handle.read(8)
            if len(chunk_header) != 8:
                raise RuntimeError("converter created a truncated GLB chunk")
            chunk_length, chunk_type = struct.unpack("<II", chunk_header)
            if chunk_length % 4 or handle.tell() + chunk_length > size:
                raise RuntimeError("converter created an invalid GLB chunk")
            if chunk_index == 0:
                if (
                    chunk_type != 0x4E4F534A
                    or not 1 <= chunk_length <= MAX_GLB_JSON_BYTES
                ):
                    raise RuntimeError("converter created an invalid GLB JSON chunk")
                try:
                    document = json.loads(handle.read(chunk_length).decode("utf-8"))
                except (UnicodeDecodeError, json.JSONDecodeError) as exc:
                    raise RuntimeError("converter created invalid GLB JSON") from exc
                if (
                    not isinstance(document, dict)
                    or not isinstance(document.get("asset"), dict)
                    or document["asset"].get("version") != "2.0"
                ):
                    raise RuntimeError("converter created an incompatible GLB document")
            else:
                handle.seek(chunk_length, os.SEEK_CUR)
            chunk_index += 1
        if chunk_index < 1 or handle.tell() != size:
            raise RuntimeError("converter created an incomplete GLB container")


def _validated_adapter_summary(report: Any) -> dict[str, Any]:
    if not isinstance(report, dict):
        raise RuntimeError("converter returned no adapter report")
    if report.get("report_schema") != ADAPTER_REPORT_SCHEMA or (
        type(report.get("report_version")) is not int
        or report.get("report_version") != ADAPTER_REPORT_VERSION
    ):
        raise RuntimeError("converter returned an incompatible adapter report")
    asset_kind = report.get("asset_kind")
    if asset_kind not in SUPPORTED_ASSET_KINDS:
        raise RuntimeError("converter returned an invalid asset kind")
    summary: dict[str, Any] = {
        "report_schema": ADAPTER_REPORT_SCHEMA,
        "report_version": ADAPTER_REPORT_VERSION,
        "asset_kind": asset_kind,
        "adapter_report_sha256": hashlib.sha256(
            canonical_json_bytes(report)
        ).hexdigest(),
    }
    embedded_model_animation = report.get("embedded_model_animation")
    if type(embedded_model_animation) is not bool:
        raise RuntimeError("converter returned an invalid embedded animation flag")
    summary["embedded_model_animation"] = embedded_model_animation
    for field in COUNT_REPORT_FIELDS:
        value = report.get(field)
        if type(value) is not int or value < 0:
            raise RuntimeError("converter returned an invalid count")
        summary[field] = value
    return summary


def _validated_converter_bindings(
    module: Any,
) -> tuple[Callable[[Path], None], Converter, ConversionPhaseErrorType]:
    initialize_w3d_converter = getattr(module, "initialize_w3d_converter", None)
    convert_w3d_job = getattr(module, "convert_w3d_job", None)
    phase_error_type = getattr(module, "W3DConversionPhaseError", None)
    if not callable(initialize_w3d_converter) or not callable(convert_w3d_job):
        raise BatchManifestError("pinned W3D converter module is incomplete")
    if (
        type(phase_error_type) is not type
        or not issubclass(phase_error_type, BaseException)
        or phase_error_type.__name__ != "W3DConversionPhaseError"
        or phase_error_type.__qualname__ != "W3DConversionPhaseError"
        or phase_error_type.__module__ != getattr(module, "__name__", None)
        or getattr(module, "W3DConversionPhaseError", None) is not phase_error_type
    ):
        raise BatchManifestError("pinned W3D converter failure contract changed")
    return initialize_w3d_converter, convert_w3d_job, phase_error_type


def _exec_module_without_bytecode(loader: Any, module: Any) -> None:
    """Execute one staged module without mutating its sealed source tree."""

    previous_dont_write_bytecode = sys.dont_write_bytecode
    sys.dont_write_bytecode = True
    try:
        loader.exec_module(module)
    finally:
        sys.dont_write_bytecode = previous_dont_write_bytecode


def _default_converter(
    plugin_root: Path,
) -> tuple[Converter, ConversionPhaseErrorType]:
    plugin_entry = plugin_root / "io_mesh_w3d" / "__init__.py"
    _assert_existing_path_chain_has_no_links(plugin_entry)
    if not plugin_entry.is_file() or _is_link_like(plugin_entry):
        raise BatchManifestError("pinned plugin entry must be an ordinary file")
    converter_path = Path(__file__).resolve().with_name("w3d_to_glb.py")
    _assert_existing_path_chain_has_no_links(converter_path)
    if not converter_path.is_file() or _is_link_like(converter_path):
        raise BatchManifestError(
            "pinned W3D converter module must be an ordinary sibling file"
        )
    spec = importlib.util.spec_from_file_location(
        "_openbfme_pinned_w3d_to_glb",
        converter_path,
    )
    if spec is None or spec.loader is None:
        raise BatchManifestError("pinned W3D converter module could not be loaded")
    module = importlib.util.module_from_spec(spec)
    # The adapter bundle is a sealed transaction input.  Loading its sibling
    # must not add __pycache__ entries that make the bundle differ from the
    # exact two-file tree pinned by the coordinator.
    _exec_module_without_bytecode(spec.loader, module)
    module_file = getattr(module, "__file__", None)
    if (
        not isinstance(module_file, str)
        or Path(module_file).resolve() != converter_path
    ):
        raise BatchManifestError("pinned W3D converter module origin changed")
    if (
        getattr(module, "ADAPTER_REPORT_SCHEMA", None) != ADAPTER_REPORT_SCHEMA
        or getattr(module, "ADAPTER_REPORT_VERSION", None) != ADAPTER_REPORT_VERSION
    ):
        raise BatchManifestError("pinned W3D converter report contract changed")
    initialize_w3d_converter, convert_w3d_job, phase_error_type = (
        _validated_converter_bindings(module)
    )
    initialize_w3d_converter(plugin_root)
    return convert_w3d_job, phase_error_type


def _emit_marker(emit: Emitter, marker: str, payload: dict[str, Any]) -> None:
    encoded = canonical_json_bytes(payload).decode("utf-8").rstrip("\n")
    emit(f"{marker} {encoded}")


def _safe_failure_kind(error: BaseException) -> str:
    """Return a fixed category without inspecting or rendering the exception."""

    if isinstance(error, AssertionError):
        return "assertion"
    if isinstance(error, MemoryError):
        return "memory"
    if isinstance(error, TimeoutError):
        return "timeout"
    if isinstance(error, OSError):
        return "os"
    if isinstance(error, KeyError):
        return "key"
    if isinstance(error, TypeError):
        return "type"
    if isinstance(error, ValueError):
        return "value"
    if isinstance(error, RuntimeError):
        return "runtime"
    if isinstance(error, Exception):
        return "application"
    return "control-flow"


def _failure_evidence(
    error: BaseException,
    *,
    coarse_phase: str,
    phase_error_type: ConversionPhaseErrorType | None,
) -> tuple[str, str]:
    """Return only fixed evidence, trusting one exact pinned exception type."""

    if phase_error_type is not None and type(error) is phase_error_type:
        try:
            phase = getattr(error, "failure_phase", None)
            kind = getattr(error, "failure_kind", None)
        except BaseException:
            return coarse_phase, _safe_failure_kind(error)
        if (
            type(phase) is str
            and phase in FAILURE_PHASES
            and type(kind) is str
            and kind in FAILURE_KINDS
        ):
            return phase, kind
    return coarse_phase, _safe_failure_kind(error)


def _failed_result(
    job: BatchJob,
    error: BaseException,
    *,
    coarse_phase: str,
    phase_error_type: ConversionPhaseErrorType | None,
) -> dict[str, Any]:
    phase, kind = _failure_evidence(
        error,
        coarse_phase=coarse_phase,
        phase_error_type=phase_error_type,
    )
    return {
        "job_id": job.job_id,
        "status": "failed",
        "failure_code": "conversion-error",
        "failure_phase": phase,
        "failure_kind": kind,
    }


def _complete_batch_report(
    jobs: tuple[BatchJob, ...],
    manifest_sha256: str,
    results: list[dict[str, Any]],
    emit: Emitter,
) -> dict[str, Any]:
    successes = sum(item["status"] == "succeeded" for item in results)
    aggregate = {
        "report_schema": BATCH_REPORT_SCHEMA,
        "report_version": BATCH_REPORT_VERSION,
        "manifest_sha256": manifest_sha256,
        "jobs": len(results),
        "succeeded": successes,
        "failed": len(results) - successes,
        "complete": successes == len(results),
        "results": results,
    }
    if len(results) != len(jobs):
        raise RuntimeError("batch result cardinality changed")
    marker = {key: value for key, value in aggregate.items() if key != "results"}
    _emit_marker(emit, "OPENBFME_W3D_BATCH_DONE", marker)
    return aggregate


def run_batch(
    *,
    manifest: Path,
    plugin_root: Path,
    job_root: Path,
    output_root: Path,
    converter: Converter | None = None,
    emit: Emitter = print,
) -> dict[str, Any]:
    """Run all valid jobs in one process and return sanitized aggregate evidence."""

    jobs, manifest_sha256 = load_canonical_manifest(manifest)
    plugin_root = _validated_directory(plugin_root, "plugin root")
    job_root = _validated_directory(job_root, "job root")
    output_root = _validated_directory(output_root, "output root")
    if any(
        _paths_overlap(left, right)
        for left, right in (
            (plugin_root, job_root),
            (plugin_root, output_root),
            (job_root, output_root),
        )
    ):
        raise BatchManifestError("plugin, job, and output roots must not overlap")
    phase_error_type: ConversionPhaseErrorType | None = None
    if converter is None:
        try:
            converter, phase_error_type = _default_converter(plugin_root)
        except BaseException as error:
            results = [
                _failed_result(
                    job,
                    error,
                    coarse_phase="converter-initialization",
                    phase_error_type=None,
                )
                for job in jobs
            ]
            for result in results:
                _emit_marker(emit, "OPENBFME_W3D_BATCH_JOB", result)
            return _complete_batch_report(jobs, manifest_sha256, results, emit)

    staging_root = output_root / STAGING_DIRECTORY_NAME / manifest_sha256
    _assert_existing_path_chain_has_no_links(staging_root)
    staging_root.mkdir(parents=True, exist_ok=True)
    _assert_existing_path_chain_has_no_links(staging_root)

    results: list[dict[str, Any]] = []
    for index, job in enumerate(jobs):
        staging_output = staging_root / f"{index:03d}.glb"
        failure_phase = "input-validation"
        try:
            if staging_output.exists() or _is_link_like(staging_output):
                if not staging_output.is_file() or _is_link_like(staging_output):
                    raise BatchManifestError(
                        "batch staging target is not an ordinary file"
                    )
                staging_output.unlink()
            model = _validated_input(job_root, job.model)
            animations = [_validated_input(job_root, item) for item in job.animations]
            final_output = _prepare_output_parent(output_root, job.output)
            failure_phase = "converter-execution"
            report = converter(
                model=model,
                asset_kind=job.asset_kind,
                animations=animations,
                required_equipment=list(job.required_equipment),
                excluded_optional_meshes=list(job.excluded_optional_meshes),
                proven_root_rigid_bake=job.proven_root_rigid_bake,
                output=staging_output,
            )
            failure_phase = "glb-validation"
            _validate_glb_container(staging_output)
            failure_phase = "report-validation"
            summary = _validated_adapter_summary(report)
            reported_embedded_model_animation = summary.pop("embedded_model_animation")
            if (
                summary["asset_kind"] != job.asset_kind
                or summary["animations"] != len(job.animations)
                or reported_embedded_model_animation is not job.embedded_model_animation
                or report.get("required_equipment")
                != sorted(set(job.required_equipment))
                or report.get("generated_images") != 0
                or report.get("remaining_non_render_geometry") != 0
                or report.get("remaining_ambiguous_box_geometry") != 0
            ):
                raise RuntimeError("converter report does not match the requested job")
            failure_phase = "publication"
            output_sha256 = _sha256_file(staging_output)
            result = {
                "job_id": job.job_id,
                "status": "succeeded",
                "output_sha256": output_sha256,
                **summary,
            }
            os.replace(staging_output, final_output)
        except BaseException as error:
            try:
                if staging_output.is_file() and not _is_link_like(staging_output):
                    staging_output.unlink()
            except BaseException:
                pass
            result = _failed_result(
                job,
                error,
                coarse_phase=failure_phase,
                phase_error_type=phase_error_type,
            )
        results.append(result)
        _emit_marker(emit, "OPENBFME_W3D_BATCH_JOB", result)

    return _complete_batch_report(jobs, manifest_sha256, results, emit)


def main() -> None:
    args = parse_args()
    report = run_batch(
        manifest=args.manifest,
        plugin_root=args.plugin_root,
        job_root=args.job_root,
        output_root=args.output_root,
    )
    if not report["complete"]:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
