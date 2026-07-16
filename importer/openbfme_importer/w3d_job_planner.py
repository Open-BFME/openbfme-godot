"""Conservative W3D catalog to Blender-batch job planning.

The planner consumes only an already-scanned :class:`W3DCatalogReport`, its
exact index, and a caller-owned mapping to staged private inputs.  It does not
open files or infer presentation choices.  Every catalog source is accounted
for as a conversion input or an explicit terminal.

Batch manifests intentionally retain private staged paths because Blender
needs them.  :meth:`W3DJobPlan.neutral` is a separate payload-free evidence
surface: it contains hashes, opaque source IDs, counts, and reason codes but no
physical paths or authored header names.
"""

from __future__ import annotations

from dataclasses import dataclass, field
import hashlib
import json
from pathlib import PurePosixPath
import re
from typing import Mapping

from .w3d_catalog import W3DCatalogReport, W3DCatalogSource, catalog_source_id
from .w3d_index import W3DIndex
from .w3d_metadata import (
    W3DAnimationHeader,
    W3DHierarchyHeader,
    W3DMetadata,
    W3DModelHeader,
)


BATCH_MANIFEST_SCHEMA = "openbfme.w3d-batch-jobs"
BATCH_MANIFEST_VERSION = 1
MAX_BATCH_JOBS = 256
MAX_BATCH_MANIFEST_BYTES = 1024 * 1024
MAX_ANIMATIONS_PER_JOB = 256
MAX_RELATIVE_PATH_LENGTH = 512

SECONDARY_SKIN_PREPARATION = "secondary-skin-equivalence-v0"
_SECONDARY_SKIN_CHUNK_IDS = frozenset({0x00000C00, 0x00000C01})
_W3D_GEOMETRY_TYPE_SKIN = 0x00020000
_ROOT_RIGID_REFERENCE_ROLES = frozenset({"lod"})

PRESENTATION_POLICY_STRICT = "strict"
PRESENTATION_POLICY_PRESERVE_ALL = "preserve-all"
_PRESENTATION_POLICIES = frozenset(
    {PRESENTATION_POLICY_STRICT, PRESENTATION_POLICY_PRESERVE_ALL}
)

_JOB_ID = re.compile(r"^[a-z0-9](?:[a-z0-9_-]{0,62}[a-z0-9])?$")
_SOURCE_ID = re.compile(r"^src-[0-9a-f]{32}$")
_REASON_CODE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
_SHA256 = re.compile(r"^[0-9a-f]{64}$")
_FORBIDDEN_IDENTIFIER_CHARACTERS = frozenset('/\\:*?"<>|')

HIERARCHY_RESOLUTION_SAME_SOURCE = "same-source"
HIERARCHY_RESOLUTION_SIBLING_PATH = "sibling-path"
_HIERARCHY_RESOLUTION_MODES = frozenset(
    {HIERARCHY_RESOLUTION_SAME_SOURCE, HIERARCHY_RESOLUTION_SIBLING_PATH}
)


class W3DJobPlannerError(ValueError):
    """The catalog, index, or staged-input mapping is not a coherent input."""


def _canonical_json_bytes(value: object) -> bytes:
    return (
        json.dumps(
            value,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    ).encode("utf-8")


def _canonical_sha256(value: object) -> str:
    return hashlib.sha256(_canonical_json_bytes(value)).hexdigest()


def _safe_identifier(value: str) -> bool:
    return (
        isinstance(value, str)
        and bool(value)
        and value == value.strip()
        and len(value) <= 255
        and value not in {".", ".."}
        and not value.startswith(".")
        and not value.endswith(".")
        and ".." not in value
        and not any(char in _FORBIDDEN_IDENTIFIER_CHARACTERS for char in value)
        and all(32 <= ord(char) <= 126 for char in value)
    )


def _animation_export_name(value: str) -> str:
    """Mirror the adapter's public action-name normalization."""

    return re.sub(r"[^a-z0-9_]+", "_", value.casefold()).strip("_")


def _safe_staged_w3d_path(value: object) -> str | None:
    if (
        not isinstance(value, str)
        or not value
        or len(value) > MAX_RELATIVE_PATH_LENGTH
        or "\\" in value
        or "\x00" in value
        or ":" in value
    ):
        return None
    candidate = PurePosixPath(value)
    if (
        candidate.is_absolute()
        or candidate.as_posix() != value
        or any(part in {"", ".", ".."} for part in candidate.parts)
        or candidate.suffix.casefold() != ".w3d"
    ):
        return None
    return value


@dataclass(frozen=True, slots=True)
class W3DPlannedJob:
    """One exact adapter-compatible conversion job."""

    job_id: str
    asset_kind: str
    model: str
    animations: tuple[str, ...]
    output: str
    model_source_id: str
    model_source_sha256: str
    hierarchy_source_id: str | None
    animation_source_ids: tuple[str, ...]
    definition_sha256: str
    animation_source_sha256s: tuple[str, ...] = ()
    hierarchy: str | None = None
    hierarchy_source_sha256: str | None = None
    model_preparation: str | None = None
    prepared_model_sha256: str | None = None
    model_preparation_evidence_sha256: str | None = None
    proven_root_rigid_bake: bool = False
    presentation_policy: str = PRESENTATION_POLICY_STRICT
    authored_presentation_mesh_count: int | None = None
    hierarchy_resolution_mode: str | None = None
    animation_hierarchy_resolution_modes: tuple[str, ...] = ()

    def manifest_row(self) -> dict[str, object]:
        """Return the exact ``w3d_batch_to_glb.py`` job contract."""

        return {
            "job_id": self.job_id,
            "model": self.model,
            "asset_kind": self.asset_kind,
            "animations": list(self.animations),
            "required_equipment": [],
            "excluded_optional_meshes": [],
            "proven_root_rigid_bake": self.proven_root_rigid_bake,
            "output": self.output,
        }

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "jobId": self.job_id,
            "assetKind": self.asset_kind,
            "modelSourceId": self.model_source_id,
            "modelSourceSha256": self.model_source_sha256,
            "animationSourceIds": list(self.animation_source_ids),
            "animationSourceCount": len(self.animation_source_ids),
            "definitionSha256": self.definition_sha256,
        }
        if self.animation_source_sha256s:
            result["animationSourceSha256s"] = list(self.animation_source_sha256s)
        if self.hierarchy_source_id is not None:
            result["hierarchySourceId"] = self.hierarchy_source_id
            result["hierarchySourceSha256"] = self.hierarchy_source_sha256
        if self.hierarchy_resolution_mode is not None:
            result["hierarchyResolutionMode"] = self.hierarchy_resolution_mode
        if self.animation_hierarchy_resolution_modes:
            result["animationHierarchyResolutionModes"] = list(
                self.animation_hierarchy_resolution_modes
            )
        if self.model_preparation is not None:
            preparation: dict[str, object] = {
                "kind": self.model_preparation,
                "complete": self.prepared_model_sha256 is not None,
            }
            if self.prepared_model_sha256 is not None:
                preparation["preparedModelSha256"] = self.prepared_model_sha256
                preparation["evidenceSha256"] = self.model_preparation_evidence_sha256
            result["modelPreparation"] = preparation
        if self.proven_root_rigid_bake:
            result["provenRootRigidBake"] = True
        if self.presentation_policy == PRESENTATION_POLICY_PRESERVE_ALL:
            result["presentation"] = {
                "policy": PRESENTATION_POLICY_PRESERVE_ALL,
                "authoredPresentationMeshCount": (
                    self.authored_presentation_mesh_count
                ),
                "allAuthoredPresentationMeshesRetained": True,
                "equipmentExclusionsApplied": False,
                "conditionStateSelectionApplied": False,
                "runtimePresentationResolved": False,
                "runtimeFidelityClaimed": False,
            }
        return result


def w3d_job_is_exact_embedded_model_animation(job: object) -> bool:
    """Return whether one job is the admitted exact embedded animation shape.

    This is the single overlap exception for model, hierarchy, and animation
    roles.  All three roles must name the same staged source exactly, and the
    source must need no model preparation.
    """

    if not isinstance(job, W3DPlannedJob):
        return False
    if (
        job.asset_kind != "animated"
        or job.hierarchy is None
        or type(job.animations) is not tuple
        or type(job.animation_source_ids) is not tuple
        or type(job.animation_source_sha256s) is not tuple
        or type(job.animation_hierarchy_resolution_modes) is not tuple
        or type(job.proven_root_rigid_bake) is not bool
        or len(job.animations) != 1
        or len(job.animation_source_ids) != 1
        or len(job.animation_source_sha256s) != 1
        or job.hierarchy_resolution_mode != HIERARCHY_RESOLUTION_SAME_SOURCE
        or job.animation_hierarchy_resolution_modes
        != (HIERARCHY_RESOLUTION_SAME_SOURCE,)
        or job.model_preparation is not None
        or job.prepared_model_sha256 is not None
        or job.model_preparation_evidence_sha256 is not None
    ):
        return False

    model_role = (
        job.model,
        job.model_source_id,
        job.model_source_sha256,
    )
    hierarchy_role = (
        job.hierarchy,
        job.hierarchy_source_id,
        job.hierarchy_source_sha256,
    )
    animation_role = (
        job.animations[0],
        job.animation_source_ids[0],
        job.animation_source_sha256s[0],
    )
    return (
        model_role == hierarchy_role == animation_role
        and _safe_staged_w3d_path(job.model) is not None
        and isinstance(job.model_source_id, str)
        and bool(job.model_source_id)
        and isinstance(job.model_source_sha256, str)
        and _SHA256.fullmatch(job.model_source_sha256) is not None
    )


def w3d_job_resolution_contract_is_valid(job: object) -> bool:
    """Return whether one job has an exact hierarchy-resolution binding.

    The predicate deliberately returns only a boolean so downstream failures
    cannot disclose authored paths or identifiers.  It validates structural
    role identity; boundaries retain their stricter current overlap policies.
    """

    if not isinstance(job, W3DPlannedJob):
        return False
    if (
        _safe_staged_w3d_path(job.model) is None
        or not isinstance(job.model_source_id, str)
        or not job.model_source_id
        or not isinstance(job.model_source_sha256, str)
        or _SHA256.fullmatch(job.model_source_sha256) is None
        or type(job.animations) is not tuple
        or type(job.animation_source_ids) is not tuple
        or type(job.animation_source_sha256s) is not tuple
        or type(job.animation_hierarchy_resolution_modes) is not tuple
    ):
        return False

    animation_count = len(job.animations)
    if job.proven_root_rigid_bake and (
        job.asset_kind != "hierarchical"
        or job.hierarchy is None
        or animation_count != 0
    ):
        return False
    if not (
        len(job.animation_source_ids)
        == len(job.animation_source_sha256s)
        == len(job.animation_hierarchy_resolution_modes)
        == animation_count
    ):
        return False

    model_role = (
        job.model,
        job.model_source_id,
        job.model_source_sha256,
    )
    raw_hierarchy_role = (
        job.hierarchy,
        job.hierarchy_source_id,
        job.hierarchy_source_sha256,
    )
    if job.hierarchy is None:
        return (
            raw_hierarchy_role == (None, None, None)
            and job.hierarchy_resolution_mode is None
            and animation_count == 0
        )
    if (
        _safe_staged_w3d_path(job.hierarchy) is None
        or not isinstance(job.hierarchy_source_id, str)
        or not job.hierarchy_source_id
        or not isinstance(job.hierarchy_source_sha256, str)
        or _SHA256.fullmatch(job.hierarchy_source_sha256) is None
        or not isinstance(job.hierarchy_resolution_mode, str)
        or job.hierarchy_resolution_mode not in _HIERARCHY_RESOLUTION_MODES
    ):
        return False

    hierarchy_role = (
        job.hierarchy,
        job.hierarchy_source_id,
        job.hierarchy_source_sha256,
    )
    hierarchy_is_same_source = hierarchy_role == model_role
    if job.hierarchy_resolution_mode == HIERARCHY_RESOLUTION_SAME_SOURCE:
        if not hierarchy_is_same_source:
            return False
    elif (
        hierarchy_is_same_source
        or job.hierarchy.casefold() == job.model.casefold()
        or job.hierarchy_source_id == job.model_source_id
    ):
        return False

    roles = [model_role, hierarchy_role]
    for path, source_id, source_sha256, mode in zip(
        job.animations,
        job.animation_source_ids,
        job.animation_source_sha256s,
        job.animation_hierarchy_resolution_modes,
        strict=True,
    ):
        if (
            _safe_staged_w3d_path(path) is None
            or not isinstance(source_id, str)
            or not source_id
            or not isinstance(source_sha256, str)
            or _SHA256.fullmatch(source_sha256) is None
            or not isinstance(mode, str)
            or mode not in _HIERARCHY_RESOLUTION_MODES
        ):
            return False
        animation_role = (path, source_id, source_sha256)
        animation_is_same_source = animation_role == hierarchy_role
        if mode == HIERARCHY_RESOLUTION_SAME_SOURCE:
            if (
                not animation_is_same_source
                or not w3d_job_is_exact_embedded_model_animation(job)
            ):
                return False
        elif (
            animation_is_same_source
            or path.casefold() == job.hierarchy.casefold()
            or source_id == job.hierarchy_source_id
        ):
            return False
        roles.append(animation_role)

    # A different spelling of the same case-insensitive physical path is never
    # a distinct role.  The exact embedded predicate above is the only admitted
    # model/hierarchy/animation overlap.
    canonical_paths: dict[str, str] = {}
    for path, _source_id, _source_sha256 in roles:
        prior = canonical_paths.setdefault(path.casefold(), path)
        if prior != path:
            return False
    return True


@dataclass(frozen=True, slots=True)
class W3DTerminal:
    """One source that cannot enter a generic conversion batch."""

    source_id: str
    source_sha256: str
    reason_codes: tuple[str, ...]

    def neutral(self) -> dict[str, object]:
        return {
            "sourceId": self.source_id,
            "sourceSha256": self.source_sha256,
            "reasonCodes": list(self.reason_codes),
        }


@dataclass(frozen=True, slots=True)
class W3DJobBatch:
    """A deterministic adapter manifest containing no more than 256 jobs."""

    batch_id: str
    jobs: tuple[W3DPlannedJob, ...]
    manifest_sha256: str

    def manifest_document(self) -> dict[str, object]:
        return {
            "manifest_schema": BATCH_MANIFEST_SCHEMA,
            "manifest_version": BATCH_MANIFEST_VERSION,
            "jobs": [job.manifest_row() for job in self.jobs],
        }

    def manifest_bytes(self) -> bytes:
        return _canonical_json_bytes(self.manifest_document())

    def neutral(self) -> dict[str, object]:
        return {
            "batchId": self.batch_id,
            "jobCount": len(self.jobs),
            "jobIds": [job.job_id for job in self.jobs],
            "manifestSha256": self.manifest_sha256,
        }


@dataclass(frozen=True, slots=True)
class W3DJobPlan:
    """Immutable jobs, bounded manifests, and explicit terminal evidence."""

    jobs: tuple[W3DPlannedJob, ...]
    batches: tuple[W3DJobBatch, ...]
    terminals: tuple[W3DTerminal, ...]
    catalog_input_sha256: str
    catalog_metadata_sha256: str
    source_count: int
    consumed_source_count: int
    private_plan_sha256: str
    evidence_sha256: str
    presentation_policy: str = PRESENTATION_POLICY_STRICT
    forced_terminal_evidence_sha256: str | None = None
    forced_terminal_rows: tuple[tuple[str, tuple[str, ...]], ...] | None = None

    @property
    def source_accounting_complete(self) -> bool:
        """Whether every supplied source is consumed or terminal-free.

        This property deliberately does not claim conversion or corpus
        completeness.
        """

        return not self.terminals and self.consumed_source_count == self.source_count

    def _neutral(self, *, include_evidence_hash: bool) -> dict[str, object]:
        hashes = {
            "catalogInputSha256": self.catalog_input_sha256,
            "catalogMetadataSha256": self.catalog_metadata_sha256,
            "privatePlanSha256": self.private_plan_sha256,
        }
        if include_evidence_hash:
            hashes["evidenceSha256"] = self.evidence_sha256
        if self.forced_terminal_evidence_sha256 is not None:
            hashes["forcedTerminalEvidenceSha256"] = (
                self.forced_terminal_evidence_sha256
            )
        result: dict[str, object] = {
            "schema": "openbfme.w3d-job-plan-evidence",
            "schemaVersion": 0,
            "summary": {
                "sourceCount": self.source_count,
                "consumedSourceCount": self.consumed_source_count,
                "jobCount": len(self.jobs),
                "batchCount": len(self.batches),
                "terminalCount": len(self.terminals),
                "sourceAccountingComplete": self.source_accounting_complete,
                "conversionComplete": False,
                "corpusComplete": False,
            },
            "hashes": hashes,
            "jobs": [job.neutral() for job in self.jobs],
            "batches": [batch.neutral() for batch in self.batches],
            "terminals": [terminal.neutral() for terminal in self.terminals],
        }
        if self.forced_terminal_rows is not None:
            result["forcedTerminalReasons"] = [
                {"sourceId": source_id, "reasonCodes": list(reason_codes)}
                for source_id, reason_codes in self.forced_terminal_rows
            ]
        if self.presentation_policy == PRESENTATION_POLICY_PRESERVE_ALL:
            result["presentation"] = {
                "policy": PRESENTATION_POLICY_PRESERVE_ALL,
                "claimScope": "planned-jobs-only",
                "allAuthoredPresentationMeshesRetained": True,
                "equipmentExclusionsApplied": False,
                "conditionStateSelectionApplied": False,
                "runtimePresentationResolved": False,
                "runtimeFidelityClaimed": False,
            }
        return result

    def evidence_hash_basis(self) -> dict[str, object]:
        return self._neutral(include_evidence_hash=False)

    def neutral(self) -> dict[str, object]:
        return self._neutral(include_evidence_hash=True)


@dataclass(slots=True)
class _SourceState:
    source: W3DCatalogSource
    source_id: str
    metadata: W3DMetadata | None = None
    staged_path: str | None = None
    reasons: set[str] = field(default_factory=set)
    preparations: set[str] = field(default_factory=set)
    consumed: bool = False
    physical_path_safe: bool = True

    @property
    def path(self) -> str | None:
        return self.source.canonical_virtual_path

    @property
    def clean(self) -> bool:
        return not self.reasons and self.metadata is not None


def _state_sort_key(state: _SourceState) -> tuple[str, str]:
    return state.source_id, state.source.source_sha256


def _path_free_terminal_source_id(source: W3DCatalogSource, occurrence: int) -> str:
    """Derive an opaque ID for a source whose physical path is unusable."""

    if (
        isinstance(source.byte_length, bool)
        or not isinstance(source.byte_length, int)
        or source.byte_length < 0
    ):
        raise W3DJobPlannerError("W3D catalog source byte length is invalid")
    if (
        not isinstance(source.source_sha256, str)
        or _SHA256.fullmatch(source.source_sha256) is None
    ):
        raise W3DJobPlannerError("W3D catalog source SHA-256 is invalid")
    if (
        isinstance(occurrence, bool)
        or not isinstance(occurrence, int)
        or occurrence < 0
    ):
        raise W3DJobPlannerError("W3D catalog unsafe-source ordinal is invalid")
    digest = _canonical_sha256(
        {
            "schema": "openbfme.w3d-unsafe-physical-source",
            "schemaVersion": 0,
            "sourceSha256": source.source_sha256,
            "byteLength": source.byte_length,
            "occurrence": occurrence,
        }
    )
    return f"src-{digest[:32]}"


def _source_states(report: W3DCatalogReport) -> tuple[_SourceState, ...]:
    unsafe_occurrences: dict[tuple[str, int], int] = {}
    states: list[_SourceState] = []
    for source in report.sources:
        canonical_path = _safe_staged_w3d_path(source.canonical_virtual_path)
        path_safe = canonical_path is not None
        if path_safe:
            try:
                source_id = catalog_source_id(source)
            except ValueError:
                path_safe = False
        if not path_safe:
            signature = (source.source_sha256, source.byte_length)
            occurrence = unsafe_occurrences.get(signature, 0)
            unsafe_occurrences[signature] = occurrence + 1
            source_id = _path_free_terminal_source_id(source, occurrence)
        states.append(
            _SourceState(
                source=source,
                source_id=source_id,
                physical_path_safe=path_safe,
            )
        )
    return tuple(sorted(states, key=_state_sort_key))


def _metadata_has_mesh_evidence(metadata: W3DMetadata) -> bool:
    if not metadata.mesh_headers or any(
        header.vertex_count < 1 or header.face_count < 1
        for header in metadata.mesh_headers
    ):
        return False
    required = {"vertices", "triangles"}
    present = {
        chunk.chunk_name
        for chunk in metadata.chunks
        if chunk.available_payload_size > 0 and not chunk.truncated
    }
    return required <= present


def _hierarchy_is_complete(metadata: W3DMetadata, header: W3DHierarchyHeader) -> bool:
    return (
        header.pivot_count > 0 and len(metadata.hierarchy_pivots) >= header.pivot_count
    )


def _root_rigid_bake_is_proven(
    asset_kind: str,
    model_metadata: W3DMetadata,
    hierarchy_header: W3DHierarchyHeader | None,
) -> bool:
    """Prove the pinned importer will create only an empty root carrier."""

    if (
        asset_kind != "hierarchical"
        or hierarchy_header is None
        or hierarchy_header.pivot_count != 1
        or len(model_metadata.model_headers) != 1
        or not model_metadata.model_references
        or any(
            header.attributes & _W3D_GEOMETRY_TYPE_SKIN
            for header in model_metadata.mesh_headers
        )
    ):
        return False
    model_ids = set(model_metadata.model_ids)
    return bool(model_ids) and all(
        reference.role in _ROOT_RIGID_REFERENCE_ROLES
        and reference.bone_index == 0
        and reference.identifier in model_ids
        for reference in model_metadata.model_references
    )


@dataclass(frozen=True, slots=True)
class _HierarchyResolution:
    status: str
    source: _SourceState | None = None
    header: W3DHierarchyHeader | None = None
    mode: str | None = None


def _hierarchy_path_index(
    states: tuple[_SourceState, ...],
) -> dict[str, tuple[_SourceState, ...]]:
    grouped: dict[str, list[_SourceState]] = {}
    for state in states:
        if state.path is not None and state.physical_path_safe:
            grouped.setdefault(state.path.casefold(), []).append(state)
    return {
        key: tuple(sorted(values, key=_state_sort_key))
        for key, values in grouped.items()
    }


def _resolve_hierarchy_source(
    state: _SourceState,
    hierarchy_identifier: str,
    *,
    by_folded_path: Mapping[str, tuple[_SourceState, ...]],
) -> _HierarchyResolution:
    """Resolve the exact hierarchy source used by the pinned importer.

    An exact, complete hierarchy embedded in the source wins.  Otherwise the
    only legal external candidate is the case-insensitive sibling
    ``<source parent>/<hierarchy identifier>.w3d``.  Authored hierarchy IDs in
    that sibling do not participate in selection; the physical filename does.
    """

    metadata = state.metadata
    if (
        metadata is None
        or state.path is None
        or not state.physical_path_safe
        or not _safe_identifier(hierarchy_identifier)
    ):
        return _HierarchyResolution(status="missing")

    same_source = tuple(
        header
        for header in metadata.hierarchy_headers
        if header.identifier.casefold() == hierarchy_identifier.casefold()
    )
    if len(same_source) > 1:
        return _HierarchyResolution(status="ambiguous")
    if len(same_source) == 1 and _hierarchy_is_complete(metadata, same_source[0]):
        return _HierarchyResolution(
            status="resolved",
            source=state,
            header=same_source[0],
            mode=HIERARCHY_RESOLUTION_SAME_SOURCE,
        )

    parent = PurePosixPath(state.path).parent
    candidate_name = f"{hierarchy_identifier}.w3d"
    candidate_path = (
        candidate_name
        if parent == PurePosixPath(".")
        else (parent / candidate_name).as_posix()
    )
    physical_candidates = by_folded_path.get(candidate_path.casefold(), ())
    if not physical_candidates:
        return _HierarchyResolution(status="missing")
    if len(physical_candidates) != 1:
        return _HierarchyResolution(status="ambiguous")

    candidate = physical_candidates[0]
    candidate_metadata = candidate.metadata
    if candidate_metadata is None or not candidate_metadata.hierarchy_headers:
        return _HierarchyResolution(
            status="damaged",
            source=candidate,
            mode=HIERARCHY_RESOLUTION_SIBLING_PATH,
        )
    if len(candidate_metadata.hierarchy_headers) != 1:
        return _HierarchyResolution(
            status="ambiguous",
            source=candidate,
            mode=HIERARCHY_RESOLUTION_SIBLING_PATH,
        )
    header = candidate_metadata.hierarchy_headers[0]
    if not _hierarchy_is_complete(candidate_metadata, header):
        return _HierarchyResolution(
            status="damaged",
            source=candidate,
            header=header,
            mode=HIERARCHY_RESOLUTION_SIBLING_PATH,
        )
    return _HierarchyResolution(
        status="resolved",
        source=candidate,
        header=header,
        mode=HIERARCHY_RESOLUTION_SIBLING_PATH,
    )


def _animation_is_complete(metadata: W3DMetadata, header: W3DAnimationHeader) -> bool:
    return (
        header.frame_count > 0
        and header.frame_rate > 0
        and any(
            chunk.chunk_name
            in {
                "animation-channel",
                "animation-bit-channel",
                "compressed-animation-channel",
                "compressed-bit-channel",
                "compressed-animation-motion-channel",
            }
            and chunk.available_payload_size > 0
            and not chunk.truncated
            for chunk in metadata.chunks
        )
    )


def _model_header(metadata: W3DMetadata) -> W3DModelHeader | None:
    if len(metadata.model_headers) == 1:
        return metadata.model_headers[0]
    return None


def _is_mesh_only_static(metadata: W3DMetadata) -> bool:
    return (
        not metadata.model_headers
        and len(metadata.mesh_headers) == 1
        and not metadata.hierarchy_headers
        and not metadata.animation_headers
    )


def _is_animation_only(metadata: W3DMetadata) -> bool:
    return (
        len(metadata.animation_headers) == 1
        and not metadata.model_headers
        and not metadata.mesh_headers
        and not metadata.hierarchy_headers
        and not metadata.model_references
        and not metadata.texture_references
    )


def _is_exact_embedded_model_animation_source(state: _SourceState) -> bool:
    metadata = state.metadata
    if (
        metadata is None
        or state.preparations
        or len(metadata.model_headers) != 1
        or len(metadata.hierarchy_headers) != 1
        or len(metadata.animation_headers) != 1
        or not _metadata_has_mesh_evidence(metadata)
    ):
        return False

    model_header = metadata.model_headers[0]
    hierarchy_header = metadata.hierarchy_headers[0]
    animation_header = metadata.animation_headers[0]
    hierarchy_identifiers = (
        model_header.hierarchy_identifier,
        hierarchy_header.identifier,
        animation_header.hierarchy_identifier,
    )
    return (
        all(_safe_identifier(identifier) for identifier in hierarchy_identifiers)
        and len({identifier.casefold() for identifier in hierarchy_identifiers}) == 1
        and _hierarchy_is_complete(metadata, hierarchy_header)
        and _animation_is_complete(metadata, animation_header)
    )


def _is_hierarchy_only(metadata: W3DMetadata) -> bool:
    return (
        len(metadata.hierarchy_headers) == 1
        and not metadata.model_headers
        and not metadata.mesh_headers
        and not metadata.animation_headers
        and not metadata.model_references
        and not metadata.texture_references
    )


def _validate_catalog_binding(
    report: W3DCatalogReport, index: W3DIndex | None
) -> W3DIndex | None:
    if not isinstance(report, W3DCatalogReport):
        raise TypeError("W3D job planner report must be a W3DCatalogReport")
    if index is not None and not isinstance(index, W3DIndex):
        raise TypeError("W3D job planner index must be a W3DIndex or None")
    if report.index is not None and index is not None:
        if report.index.neutral() != index.neutral():
            raise W3DJobPlannerError("W3D catalog report and index do not match")
    return report.index if index is None else index


def _prepare_states(
    report: W3DCatalogReport,
    index: W3DIndex | None,
    staged_inputs: Mapping[str, str],
    presentation_policy: str,
) -> tuple[_SourceState, ...]:
    if not isinstance(staged_inputs, Mapping):
        raise TypeError("staged W3D inputs must be a mapping")
    states = _source_states(report)
    if len({state.source_id for state in states}) != len(states):
        raise W3DJobPlannerError("W3D catalog contains duplicate physical identities")

    by_physical: dict[tuple[str, str], _SourceState] = {}
    canonical_paths: dict[str, str] = {}
    by_source = {state.source: state for state in states}
    if len(by_source) != len(states):
        raise W3DJobPlannerError("W3D catalog repeats a source record")
    for state in states:
        if state.path is None or not state.physical_path_safe:
            state.reasons.add("unsafe-physical-path")
            continue
        key = (state.path, state.source.source_sha256)
        if key in by_physical:
            raise W3DJobPlannerError("W3D catalog repeats a physical source")
        by_physical[key] = state
        canonical_paths[state.path.casefold()] = state.path

    for metadata in report.files:
        state = by_physical.get((metadata.virtual_path, metadata.source_sha256))
        if state is None:
            unsafe_matches = [
                candidate
                for candidate in states
                if not candidate.physical_path_safe
                and candidate.source.source_sha256 == metadata.source_sha256
                and candidate.source.byte_length == metadata.byte_length
            ]
            if len(unsafe_matches) == 1:
                continue
            raise W3DJobPlannerError("W3D catalog metadata/source binding is invalid")
        if state.metadata is not None:
            raise W3DJobPlannerError("W3D catalog metadata/source binding is invalid")
        if metadata.byte_length != state.source.byte_length:
            raise W3DJobPlannerError("W3D catalog metadata byte length is invalid")
        state.metadata = metadata

    raw_mapping: dict[str, object] = {}
    exact_paths = {state.path for state in states if state.path is not None}
    for key, value in staged_inputs.items():
        if not isinstance(key, str):
            raise TypeError("staged W3D input keys must be strings")
        if key in raw_mapping:
            raise W3DJobPlannerError("staged W3D input mapping contains duplicates")
        if key not in exact_paths:
            folded = canonical_paths.get(key.casefold())
            if folded is not None:
                raise W3DJobPlannerError(
                    "staged W3D input key does not match catalog path casing"
                )
            raise W3DJobPlannerError("staged W3D input key is absent from catalog")
        raw_mapping[key] = value

    staged_collisions: dict[str, list[_SourceState]] = {}
    indexed_paths = set(index.virtual_paths) if index is not None else set()
    for state in states:
        if state.metadata is None:
            state.reasons.add("catalog-source-unscanned")
        if state.path is None:
            continue
        if state.path not in raw_mapping:
            state.reasons.add("missing-staged-input")
        else:
            raw_staged = raw_mapping[state.path]
            safe = _safe_staged_w3d_path(raw_staged)
            if safe is None:
                state.reasons.add("unsafe-staged-input")
            else:
                state.staged_path = safe
                staged_collisions.setdefault(safe.casefold(), []).append(state)
        if index is None:
            state.reasons.add("catalog-index-unavailable")
        elif state.path not in indexed_paths:
            state.reasons.add("catalog-source-not-indexed")

    for collision in staged_collisions.values():
        if len(collision) > 1:
            for state in collision:
                state.reasons.add("staged-input-collision")

    for warning in report.warnings:
        state = by_source.get(warning.source)
        if state is None:
            raise W3DJobPlannerError("W3D catalog warning has no source")
        code = warning.diagnostic.code
        if code == "empty-source":
            state.reasons.add("empty-source")
        elif code == "unsupported-chunk":
            if warning.diagnostic.chunk_id in _SECONDARY_SKIN_CHUNK_IDS:
                state.preparations.add(SECONDARY_SKIN_PREPARATION)
            else:
                state.reasons.add("unsupported-source")
        elif code == "unknown-chunk":
            state.reasons.add("unknown-source")
        elif code.startswith("truncated"):
            state.reasons.add("damaged-source")
        else:
            state.reasons.add("source-warning")

    for failure in report.failures:
        state = by_source.get(failure.source)
        if state is None:
            raise W3DJobPlannerError("W3D catalog failure has no source")
        state.reasons.add("catalog-source-failure")
        if failure.code == "index-rejected-header-metadata":
            state.reasons.add("unsafe-header-id")
        if failure.code == "duplicate-logical-id-within-file":
            state.reasons.add("duplicate-logical-id")

    for duplicate in report.duplicate_logical_ids:
        for occurrence in duplicate.occurrences:
            state = by_physical.get(
                (occurrence.provenance.virtual_path, occurrence.source_sha256)
            )
            if state is None:
                raise W3DJobPlannerError("W3D duplicate occurrence has no source")
            state.reasons.add("duplicate-logical-id")

    if index is not None:
        by_path = {state.path: state for state in states if state.path is not None}
        for duplicate in index.duplicate_header_ids:
            for path in duplicate.physical_virtual_paths:
                state = by_path.get(path)
                if state is None:
                    raise W3DJobPlannerError("W3D index duplicate has no source")
                state.reasons.add("duplicate-logical-id")

    for state in states:
        metadata = state.metadata
        if metadata is None:
            continue
        if metadata.byte_length == 0 or not metadata.chunks:
            state.reasons.add("empty-source")
        if any(chunk.truncated for chunk in metadata.chunks):
            state.reasons.add("damaged-source")
        unsupported_ids = {
            chunk.chunk_id
            for chunk in metadata.chunks
            if chunk.classification == "unsupported"
        }
        if unsupported_ids & _SECONDARY_SKIN_CHUNK_IDS:
            state.preparations.add(SECONDARY_SKIN_PREPARATION)
        if unsupported_ids - _SECONDARY_SKIN_CHUNK_IDS:
            state.reasons.add("unsupported-source")
        if any(chunk.classification == "unknown" for chunk in metadata.chunks):
            state.reasons.add("unknown-source")
        identifiers = [
            *metadata.model_ids,
            *metadata.hierarchy_ids,
            *metadata.animation_ids,
            *(header.hierarchy_identifier for header in metadata.model_headers),
            *(header.hierarchy_identifier for header in metadata.animation_headers),
        ]
        if any(value and not _safe_identifier(value) for value in identifiers):
            state.reasons.add("unsafe-header-id")
        if len(metadata.model_headers) > 1:
            state.reasons.add("multiple-model-headers")
        if len(metadata.hierarchy_headers) > 1:
            state.reasons.add("multiple-hierarchy-headers")
        if len(metadata.animation_headers) > 1:
            state.reasons.add("multiple-animation-headers")
        if state.preparations and not (metadata.model_headers or metadata.mesh_headers):
            state.reasons.add("model-preparation-owner-invalid")
        if (
            metadata.animation_headers
            and (metadata.model_headers or metadata.mesh_headers)
            and not _is_exact_embedded_model_animation_source(state)
        ):
            state.reasons.add("embedded-action-ambiguity")
        if metadata.model_headers or _is_mesh_only_static(metadata):
            if not _metadata_has_mesh_evidence(metadata):
                state.reasons.add("no-complete-mesh-evidence")
            needs_presentation_profile = len(metadata.mesh_headers) > 1 or any(
                reference.role != "lod" for reference in metadata.model_references
            )
            preserve_all_eligible = (
                presentation_policy == PRESENTATION_POLICY_PRESERVE_ALL
                and len(metadata.model_headers) == 1
            )
            if needs_presentation_profile and not preserve_all_eligible:
                state.reasons.add("needs-explicit-presentation-profile")
        if _is_animation_only(metadata):
            header = metadata.animation_headers[0]
            if not _animation_is_complete(metadata, header):
                state.reasons.add("incomplete-animation-evidence")
        if _is_hierarchy_only(metadata):
            header = metadata.hierarchy_headers[0]
            if not _hierarchy_is_complete(metadata, header):
                state.reasons.add("incomplete-hierarchy-evidence")
    return states


def _apply_forced_terminals(
    states: tuple[_SourceState, ...],
    forced_terminal_reasons: Mapping[str, tuple[str, ...]] | None,
    forced_terminal_evidence_sha256: str | None,
) -> tuple[tuple[str, tuple[str, ...]], ...] | None:
    if forced_terminal_reasons is None:
        if forced_terminal_evidence_sha256 is not None:
            raise ValueError(
                "forced_terminal_evidence_sha256 requires forced_terminal_reasons"
            )
        return None
    if not isinstance(forced_terminal_reasons, Mapping):
        raise TypeError("forced_terminal_reasons must be a mapping or None")
    if (
        not isinstance(forced_terminal_evidence_sha256, str)
        or _SHA256.fullmatch(forced_terminal_evidence_sha256) is None
    ):
        raise ValueError(
            "forced_terminal_evidence_sha256 must be a canonical lowercase SHA-256"
        )

    by_source_id = {state.source_id: state for state in states}
    validated: list[tuple[str, tuple[str, ...]]] = []
    seen_source_ids: set[str] = set()
    for source_id, reasons in forced_terminal_reasons.items():
        if not isinstance(source_id, str) or _SOURCE_ID.fullmatch(source_id) is None:
            raise ValueError("forced terminal source IDs must be canonical")
        if source_id in seen_source_ids:
            raise W3DJobPlannerError("forced terminal mapping repeats a source ID")
        seen_source_ids.add(source_id)
        if source_id not in by_source_id:
            raise W3DJobPlannerError(
                "forced terminal source ID is absent from the catalog"
            )
        if not isinstance(reasons, tuple):
            raise TypeError("forced terminal reasons must be tuples")
        if not reasons:
            raise ValueError("forced terminal reason tuples must be nonempty")
        if any(
            not isinstance(reason, str) or _REASON_CODE.fullmatch(reason) is None
            for reason in reasons
        ):
            raise ValueError("forced terminal reasons must be canonical reason codes")
        if reasons != tuple(sorted(set(reasons))):
            raise ValueError("forced terminal reason tuples must be sorted and unique")
        validated.append((source_id, reasons))

    rows = tuple(sorted(validated))
    for source_id, reasons in rows:
        by_source_id[source_id].reasons.update(reasons)
    return rows


def _job(
    *,
    asset_kind: str,
    model: _SourceState,
    hierarchy: _SourceState | None,
    hierarchy_header: W3DHierarchyHeader | None,
    hierarchy_resolution_mode: str | None,
    animations: tuple[_SourceState, ...],
    animation_hierarchy_resolution_modes: tuple[str, ...],
    presentation_policy: str,
) -> W3DPlannedJob:
    assert model.staged_path is not None
    animation_paths = tuple(item.staged_path for item in animations)
    if any(path is None for path in animation_paths):
        raise W3DJobPlannerError("planned W3D animation lacks a staged input")
    if (hierarchy is None) != (hierarchy_resolution_mode is None):
        raise W3DJobPlannerError("planned W3D hierarchy resolution is inconsistent")
    if (
        hierarchy_resolution_mode is not None
        and hierarchy_resolution_mode not in _HIERARCHY_RESOLUTION_MODES
    ):
        raise W3DJobPlannerError("planned W3D hierarchy resolution mode is invalid")
    if len(animation_hierarchy_resolution_modes) != len(animations) or any(
        mode not in _HIERARCHY_RESOLUTION_MODES
        for mode in animation_hierarchy_resolution_modes
    ):
        raise W3DJobPlannerError("planned W3D animation resolution is inconsistent")
    preparations = tuple(sorted(model.preparations))
    if len(preparations) > 1:
        raise W3DJobPlannerError("planned W3D model has ambiguous preparations")
    assert model.metadata is not None
    proven_root_rigid_bake = _root_rigid_bake_is_proven(
        asset_kind,
        model.metadata,
        hierarchy_header,
    )
    source_basis = {
        "assetKind": asset_kind,
        "model": {
            "sourceId": model.source_id,
            "sourceSha256": model.source.source_sha256,
        },
        "hierarchySourceId": hierarchy.source_id if hierarchy is not None else None,
        "modelPreparation": preparations[0] if preparations else None,
        "animations": [
            {
                "sourceId": item.source_id,
                "sourceSha256": item.source.source_sha256,
            }
            for item in animations
        ],
    }
    if proven_root_rigid_bake:
        source_basis["provenRootRigidBake"] = True
    authored_presentation_mesh_count: int | None = None
    if presentation_policy == PRESENTATION_POLICY_PRESERVE_ALL:
        assert model.metadata is not None
        authored_presentation_mesh_count = len(model.metadata.mesh_headers)
        source_basis["presentation"] = {
            "policy": PRESENTATION_POLICY_PRESERVE_ALL,
            "authoredPresentationMeshCount": authored_presentation_mesh_count,
            "allAuthoredPresentationMeshesRetained": True,
            "equipmentExclusionsApplied": False,
            "conditionStateSelectionApplied": False,
            "runtimePresentationResolved": False,
            "runtimeFidelityClaimed": False,
        }
    definition_sha256 = _canonical_sha256(source_basis)
    job_id = f"w3d-{definition_sha256[:40]}"
    if not _JOB_ID.fullmatch(job_id):
        raise W3DJobPlannerError("derived W3D job ID is invalid")
    job = W3DPlannedJob(
        job_id=job_id,
        asset_kind=asset_kind,
        model=model.staged_path,
        animations=tuple(path for path in animation_paths if path is not None),
        output=f"glb/{job_id}.glb",
        model_source_id=model.source_id,
        model_source_sha256=model.source.source_sha256,
        hierarchy_source_id=hierarchy.source_id if hierarchy is not None else None,
        animation_source_ids=tuple(item.source_id for item in animations),
        definition_sha256=definition_sha256,
        animation_source_sha256s=tuple(
            item.source.source_sha256 for item in animations
        ),
        hierarchy=hierarchy.staged_path if hierarchy is not None else None,
        hierarchy_source_sha256=(
            hierarchy.source.source_sha256 if hierarchy is not None else None
        ),
        model_preparation=preparations[0] if preparations else None,
        proven_root_rigid_bake=proven_root_rigid_bake,
        presentation_policy=presentation_policy,
        authored_presentation_mesh_count=authored_presentation_mesh_count,
        hierarchy_resolution_mode=hierarchy_resolution_mode,
        animation_hierarchy_resolution_modes=(animation_hierarchy_resolution_modes),
    )
    if not w3d_job_resolution_contract_is_valid(job):
        raise W3DJobPlannerError("planned W3D resolution contract is invalid")
    return job


def _make_batches(
    jobs: tuple[W3DPlannedJob, ...], max_jobs_per_batch: int
) -> tuple[W3DJobBatch, ...]:
    batches: list[W3DJobBatch] = []
    pending: list[W3DPlannedJob] = []

    def document(selected: list[W3DPlannedJob]) -> dict[str, object]:
        return {
            "manifest_schema": BATCH_MANIFEST_SCHEMA,
            "manifest_version": BATCH_MANIFEST_VERSION,
            "jobs": [job.manifest_row() for job in selected],
        }

    def seal(selected: list[W3DPlannedJob]) -> None:
        manifest = document(selected)
        payload = _canonical_json_bytes(manifest)
        if not selected or len(selected) > MAX_BATCH_JOBS:
            raise W3DJobPlannerError("derived W3D batch job count is invalid")
        if len(payload) > MAX_BATCH_MANIFEST_BYTES:
            raise W3DJobPlannerError("one W3D batch job exceeds the manifest bound")
        digest = hashlib.sha256(payload).hexdigest()
        batches.append(
            W3DJobBatch(
                batch_id=f"batch-{digest[:32]}",
                jobs=tuple(selected),
                manifest_sha256=digest,
            )
        )

    for job in jobs:
        candidate = [*pending, job]
        if pending and (
            len(candidate) > max_jobs_per_batch
            or len(_canonical_json_bytes(document(candidate)))
            > MAX_BATCH_MANIFEST_BYTES
        ):
            seal(pending)
            pending = [job]
        else:
            pending = candidate
    if pending:
        seal(pending)
    return tuple(batches)


def plan_w3d_batches(
    report: W3DCatalogReport,
    staged_inputs: Mapping[str, str],
    *,
    index: W3DIndex | None = None,
    max_jobs_per_batch: int = MAX_BATCH_JOBS,
    presentation_policy: str = PRESENTATION_POLICY_STRICT,
    forced_terminal_reasons: Mapping[str, tuple[str, ...]] | None = None,
    forced_terminal_evidence_sha256: str | None = None,
) -> W3DJobPlan:
    """Build bounded conversion batches without guessing asset relationships.

    ``staged_inputs`` keys must exactly match catalog virtual paths.  Values are
    private, canonical POSIX paths relative to the Blender job root.  Unsafe or
    missing values become source terminals; unknown mapping keys fail because
    they cannot be bound to catalog provenance.  ``preserve-all`` is an
    archival policy: it sends every authored mesh in an otherwise safe,
    single-HLOD source to the adapter without claiming that equipment,
    condition states, or runtime presentation have been resolved.
    ``forced_terminal_reasons`` is an opt-in, externally attested fail-closed
    layer keyed by canonical catalog source ID.  Its exact evidence SHA-256 is
    sealed into the plan; forced sources are excluded before dependency
    ownership is computed so dependent sources also fail closed.  Hierarchy
    ownership mirrors the pinned importer: an exact complete same-source
    hierarchy wins, otherwise one complete case-insensitive sibling physical
    source is required.  Owners and clips share only that resolved source ID.
    """

    if (
        isinstance(max_jobs_per_batch, bool)
        or not isinstance(max_jobs_per_batch, int)
        or not 1 <= max_jobs_per_batch <= MAX_BATCH_JOBS
    ):
        raise ValueError(
            f"max_jobs_per_batch must be an integer from 1 to {MAX_BATCH_JOBS}"
        )
    if (
        not isinstance(presentation_policy, str)
        or presentation_policy not in _PRESENTATION_POLICIES
    ):
        raise ValueError("presentation_policy must be 'strict' or 'preserve-all'")
    selected_index = _validate_catalog_binding(report, index)
    states = _prepare_states(
        report,
        selected_index,
        staged_inputs,
        presentation_policy,
    )
    forced_terminal_rows = _apply_forced_terminals(
        states,
        forced_terminal_reasons,
        forced_terminal_evidence_sha256,
    )

    by_folded_path = _hierarchy_path_index(states)

    model_states: list[tuple[_SourceState, W3DModelHeader | None]] = []
    for state in states:
        metadata = state.metadata
        if metadata is None:
            continue
        header = _model_header(metadata)
        if header is not None or _is_mesh_only_static(metadata):
            model_states.append((state, header))

    jobs: list[W3DPlannedJob] = []
    owner_groups: dict[str, list[tuple[_SourceState, _HierarchyResolution]]] = {}
    for model_state, model_header in model_states:
        if not model_state.clean:
            continue
        metadata = model_state.metadata
        assert metadata is not None
        if model_header is None:
            if model_state.preparations:
                model_state.reasons.add(
                    "model-preparation-hierarchy-association-missing"
                )
                continue
            jobs.append(
                _job(
                    asset_kind="static",
                    model=model_state,
                    hierarchy=None,
                    hierarchy_header=None,
                    hierarchy_resolution_mode=None,
                    animations=(),
                    animation_hierarchy_resolution_modes=(),
                    presentation_policy=presentation_policy,
                )
            )
            model_state.consumed = True
            continue

        hierarchy_id = model_header.hierarchy_identifier
        if not hierarchy_id:
            if model_state.preparations:
                model_state.reasons.add(
                    "model-preparation-hierarchy-association-missing"
                )
                continue
            if metadata.hierarchy_headers:
                model_state.reasons.add("hierarchy-association-ambiguous")
                continue
            jobs.append(
                _job(
                    asset_kind="static",
                    model=model_state,
                    hierarchy=None,
                    hierarchy_header=None,
                    hierarchy_resolution_mode=None,
                    animations=(),
                    animation_hierarchy_resolution_modes=(),
                    presentation_policy=presentation_policy,
                )
            )
            model_state.consumed = True
            continue

        resolution = _resolve_hierarchy_source(
            model_state,
            hierarchy_id,
            by_folded_path=by_folded_path,
        )
        if resolution.status == "missing":
            model_state.reasons.add("hierarchy-not-found")
            continue
        if resolution.status == "ambiguous":
            model_state.reasons.add("hierarchy-not-unique")
            continue
        if resolution.status != "resolved" or resolution.source is None:
            model_state.reasons.add("hierarchy-source-not-convertible")
            continue
        hierarchy_state = resolution.source
        if not hierarchy_state.clean:
            model_state.reasons.add("hierarchy-source-not-convertible")
            continue
        owner_groups.setdefault(hierarchy_state.source_id, []).append(
            (model_state, resolution)
        )

    animation_groups: dict[
        str,
        list[tuple[_SourceState, W3DAnimationHeader, _HierarchyResolution]],
    ] = {}
    embedded_animation_groups: dict[
        str,
        list[tuple[_SourceState, W3DAnimationHeader, _HierarchyResolution]],
    ] = {}
    for clip_state in states:
        clip_metadata = clip_state.metadata
        if clip_metadata is None or not clip_metadata.animation_headers:
            continue
        if _is_exact_embedded_model_animation_source(clip_state):
            clip_header = clip_metadata.animation_headers[0]
            clip_resolution = _resolve_hierarchy_source(
                clip_state,
                clip_header.hierarchy_identifier,
                by_folded_path=by_folded_path,
            )
            if (
                clip_resolution.status == "resolved"
                and clip_resolution.source is not None
            ):
                embedded_animation_groups.setdefault(
                    clip_resolution.source.source_id, []
                ).append((clip_state, clip_header, clip_resolution))
            continue
        if (
            clip_metadata.model_headers
            or clip_metadata.mesh_headers
            or clip_metadata.hierarchy_headers
            or clip_metadata.model_references
            or clip_metadata.texture_references
        ):
            continue
        for clip_header in clip_metadata.animation_headers:
            clip_resolution = _resolve_hierarchy_source(
                clip_state,
                clip_header.hierarchy_identifier,
                by_folded_path=by_folded_path,
            )
            if (
                clip_resolution.status == "resolved"
                and clip_resolution.source is not None
            ):
                animation_groups.setdefault(
                    clip_resolution.source.source_id, []
                ).append((clip_state, clip_header, clip_resolution))

    for hierarchy_source_id in sorted(owner_groups):
        owners = tuple(
            sorted(
                owner_groups[hierarchy_source_id],
                key=lambda item: _state_sort_key(item[0]),
            )
        )
        hierarchy_state = owners[0][1].source
        if hierarchy_state is None:
            raise W3DJobPlannerError("resolved W3D hierarchy source is absent")
        if any(item[1].source is not hierarchy_state for item in owners):
            raise W3DJobPlannerError("W3D hierarchy source identity is inconsistent")

        external_clips = tuple(
            sorted(
                animation_groups.get(hierarchy_source_id, ()),
                key=lambda item: (
                    *_state_sort_key(item[0]),
                    item[1].identifier.casefold(),
                    item[1].identifier,
                ),
            )
        )
        embedded_clips = tuple(
            sorted(
                embedded_animation_groups.get(hierarchy_source_id, ()),
                key=lambda item: (
                    *_state_sort_key(item[0]),
                    item[1].identifier.casefold(),
                    item[1].identifier,
                ),
            )
        )
        if embedded_clips:
            embedded_state, _embedded_header, embedded_resolution = embedded_clips[0]
            exact_embedded_closure = (
                len(embedded_clips) == 1
                and not external_clips
                and len(owners) == 1
                and owners[0][0] is embedded_state
                and owners[0][1].source is embedded_state
                and owners[0][1].mode == HIERARCHY_RESOLUTION_SAME_SOURCE
                and hierarchy_state is embedded_state
                and embedded_resolution.source is embedded_state
                and embedded_resolution.mode == HIERARCHY_RESOLUTION_SAME_SOURCE
            )
            if not exact_embedded_closure:
                for model_state, _resolution in owners:
                    model_state.reasons.add("animation-closure-ambiguous")
                for embedded_state, _header, _resolution in embedded_clips:
                    embedded_state.reasons.add("animation-closure-ambiguous")
                continue
            raw_clips = embedded_clips
        else:
            raw_clips = external_clips
        clip_states: list[_SourceState] = []
        clip_resolution_modes: dict[str, str] = {}
        closure_valid = True
        for clip_state, clip_header, clip_resolution in raw_clips:
            clip_metadata = clip_state.metadata
            if (
                not clip_state.clean
                or clip_metadata is None
                or not (
                    _is_animation_only(clip_metadata)
                    or _is_exact_embedded_model_animation_source(clip_state)
                )
                or not _animation_is_complete(clip_metadata, clip_header)
                or clip_resolution.mode not in _HIERARCHY_RESOLUTION_MODES
            ):
                closure_valid = False
                continue
            clip_states.append(clip_state)
            clip_resolution_modes[clip_state.source_id] = clip_resolution.mode
        unique_clips = tuple(
            sorted(
                {item.source_id: item for item in clip_states}.values(),
                key=_state_sort_key,
            )
        )
        if not closure_valid or len(unique_clips) != len(raw_clips):
            for model_state, _resolution in owners:
                model_state.reasons.add("animation-closure-ambiguous")
            continue
        if len(unique_clips) > MAX_ANIMATIONS_PER_JOB:
            for model_state, _resolution in owners:
                model_state.reasons.add("animation-count-exceeds-adapter-bound")
            continue
        export_names = tuple(
            _animation_export_name(PurePosixPath(clip.staged_path).stem)
            for clip in unique_clips
            if clip.staged_path is not None
        )
        if (
            len(export_names) != len(unique_clips)
            or any(not name for name in export_names)
            or len(set(export_names)) != len(export_names)
        ):
            for model_state, _resolution in owners:
                model_state.reasons.add("animation-closure-ambiguous")
            continue

        asset_kind = "animated" if unique_clips else "hierarchical"
        animation_resolution_modes = tuple(
            clip_resolution_modes[clip.source_id] for clip in unique_clips
        )
        for model_state, model_resolution in owners:
            jobs.append(
                _job(
                    asset_kind=asset_kind,
                    model=model_state,
                    hierarchy=hierarchy_state,
                    hierarchy_header=model_resolution.header,
                    hierarchy_resolution_mode=model_resolution.mode,
                    animations=unique_clips,
                    animation_hierarchy_resolution_modes=(animation_resolution_modes),
                    presentation_policy=presentation_policy,
                )
            )
            model_state.consumed = True
        hierarchy_state.consumed = True
        for clip_state in unique_clips:
            clip_state.consumed = True

    for state in states:
        if state.consumed:
            continue
        metadata = state.metadata
        if state.reasons:
            continue
        if metadata is None:
            state.reasons.add("catalog-source-unscanned")
        elif metadata.animation_headers:
            state.reasons.add("animation-no-unique-model-owner")
        elif metadata.hierarchy_headers:
            state.reasons.add("hierarchy-no-unique-model-owner")
        elif metadata.model_headers or metadata.mesh_headers:
            state.reasons.add("model-not-safely-plannable")
        else:
            state.reasons.add("no-model-header")

    ordered_jobs = tuple(sorted(jobs, key=lambda item: item.job_id))
    if len({job.job_id for job in ordered_jobs}) != len(ordered_jobs):
        raise W3DJobPlannerError("derived W3D job IDs collide")
    if len({job.output.casefold() for job in ordered_jobs}) != len(ordered_jobs):
        raise W3DJobPlannerError("derived W3D output paths collide")
    batches = _make_batches(ordered_jobs, max_jobs_per_batch)
    terminals = tuple(
        W3DTerminal(
            source_id=state.source_id,
            source_sha256=state.source.source_sha256,
            reason_codes=tuple(sorted(state.reasons)),
        )
        for state in sorted(states, key=_state_sort_key)
        if not state.consumed
    )
    consumed_count = sum(state.consumed for state in states)
    private_basis = {
        "catalogInputSha256": report.input_sha256,
        "catalogMetadataSha256": report.metadata_sha256,
        "jobs": [job.manifest_row() for job in ordered_jobs],
        "jobDefinitions": [job.neutral() for job in ordered_jobs],
        "terminals": [terminal.neutral() for terminal in terminals],
        "maxJobsPerBatch": max_jobs_per_batch,
    }
    if presentation_policy == PRESENTATION_POLICY_PRESERVE_ALL:
        private_basis["presentationPolicy"] = presentation_policy
    if forced_terminal_reasons is not None:
        private_basis["forcedTerminalEvidenceSha256"] = forced_terminal_evidence_sha256
        private_basis["forcedTerminalReasons"] = [
            {"sourceId": source_id, "reasonCodes": list(reason_codes)}
            for source_id, reason_codes in forced_terminal_rows or ()
        ]
    private_plan_sha256 = _canonical_sha256(private_basis)
    provisional = W3DJobPlan(
        jobs=ordered_jobs,
        batches=batches,
        terminals=terminals,
        catalog_input_sha256=report.input_sha256,
        catalog_metadata_sha256=report.metadata_sha256,
        source_count=len(states),
        consumed_source_count=consumed_count,
        private_plan_sha256=private_plan_sha256,
        evidence_sha256="",
        presentation_policy=presentation_policy,
        forced_terminal_evidence_sha256=(
            forced_terminal_evidence_sha256
            if forced_terminal_reasons is not None
            else None
        ),
        forced_terminal_rows=forced_terminal_rows,
    )
    return W3DJobPlan(
        jobs=provisional.jobs,
        batches=provisional.batches,
        terminals=provisional.terminals,
        catalog_input_sha256=provisional.catalog_input_sha256,
        catalog_metadata_sha256=provisional.catalog_metadata_sha256,
        source_count=provisional.source_count,
        consumed_source_count=provisional.consumed_source_count,
        private_plan_sha256=provisional.private_plan_sha256,
        evidence_sha256=_canonical_sha256(provisional.evidence_hash_basis()),
        presentation_policy=provisional.presentation_policy,
        forced_terminal_evidence_sha256=(provisional.forced_terminal_evidence_sha256),
        forced_terminal_rows=provisional.forced_terminal_rows,
    )


# Concise alias for callers already operating in a W3D importer context.
build_w3d_job_plan = plan_w3d_batches
