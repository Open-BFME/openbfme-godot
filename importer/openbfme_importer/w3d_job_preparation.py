"""Transactional, source-bound preparation of planned W3D model inputs.

The job planner can mark a model as needing a narrowly proved rewrite before
the pinned Blender adapter sees it.  This module is the only bridge between
that declaration and an on-disk replacement.  It validates the complete plan
and job tree first, prepares every replacement in an owned sibling tree, and
then commits the set with rollback-capable backups.

No authored path or authored identifier enters the preparation seals or error
messages.  Read-only preflight evidence uses only opaque source IDs.  The
returned plan retains the exact adapter manifests and job IDs; only preparation
attestations and the plan/evidence seals change.
"""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass, field, replace
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import stat as stat_module
import tempfile
from typing import Literal

from .w3d_job_planner import (
    HIERARCHY_RESOLUTION_IDENTIFIER_PREFIX_PATH,
    HIERARCHY_RESOLUTION_SAME_SOURCE,
    HIERARCHY_RESOLUTION_SIBLING_PATH,
    MAX_BATCH_JOBS,
    MAX_BATCH_MANIFEST_BYTES,
    SECONDARY_SKIN_PREPARATION,
    W3DJobBatch,
    W3DJobPlan,
    W3DPlannedJob,
    W3DTerminal,
    w3d_job_is_exact_embedded_model_animation,
    w3d_job_resolution_contract_is_valid,
)
from .w3d_secondary_skin import strip_proven_redundant_secondary_skin_streams
from .w3d_skin_safety import (
    HIERARCHY_PIVOT_FIXUP_UNSUPPORTED,
    SKIN_ROOT_PIVOT_INFLUENCE_UNSUPPORTED,
    SKIN_SAFETY_PROOF_REJECTED,
    W3DSkinSafetyError,
    W3DSkinSafetyProof,
    prove_w3d_skin_safety,
    validate_w3d_skin_safety_proof,
)


W3D_JOB_PREPARATION_SCHEMA = "openbfme.w3d-job-preparation-attestation"
W3D_JOB_PREPARATION_VERSION = 0
W3D_JOB_PREPARATION_PREFLIGHT_SCHEMA = "openbfme.w3d-job-preparation-preflight-evidence"
W3D_JOB_PREPARATION_PREFLIGHT_VERSION = 0
W3D_JOB_PREPARATION_FORCED_TERMINAL_SCHEMA = (
    "openbfme.w3d-job-preparation-forced-terminal-evidence"
)
W3D_JOB_PREPARATION_FORCED_TERMINAL_VERSION = 0
W3D_JOB_PREPARATION_FIXED_POINT_SCHEMA = (
    "openbfme.w3d-job-preparation-fixed-point-evidence"
)
W3D_JOB_PREPARATION_FIXED_POINT_VERSION = 0
W3D_JOB_SKIN_SAFETY_SCHEMA = "openbfme.w3d-job-skin-safety-evidence"
W3D_JOB_SKIN_SAFETY_VERSION = 0
MODEL_PREPARATION_PROOF_REJECTED = "model-preparation-proof-rejected"

_SHA256_CHARACTERS = frozenset("0123456789abcdef")
_SOURCE_ID = re.compile(r"^src-[0-9a-f]{32}$")
_JOB_ID = re.compile(r"^w3d-[0-9a-f]{40}$")
_REASON_CODE = re.compile(r"^[a-z0-9]+(?:-[a-z0-9]+)*$")
_ASSET_KINDS = frozenset({"static", "hierarchical", "animated"})
_STAGING_PREFIX = ".openbfme-w3d-preparation-staging-"
_BACKUP_PREFIX = ".openbfme-w3d-preparation-backup-"


class W3DJobPreparationError(ValueError):
    """Raised without retail paths or identifiers when attestation fails."""


@dataclass(frozen=True, slots=True)
class W3DJobPreparationForcedTerminal:
    """Path-free forced-terminal evidence for one rejected raw model."""

    source_id: str
    source_sha256: str
    reason_codes: tuple[str, ...] = (MODEL_PREPARATION_PROOF_REJECTED,)

    def neutral(self) -> dict[str, object]:
        return {
            "sourceId": self.source_id,
            "sourceSha256": self.source_sha256,
            "reasonCodes": list(self.reason_codes),
        }


@dataclass(frozen=True, slots=True)
class W3DJobSkinSafetyProof:
    job_id: str
    model_source_id: str
    model_source_sha256: str
    hierarchy_source_id: str | None
    hierarchy_source_sha256: str | None
    proof: W3DSkinSafetyProof

    def neutral(self) -> dict[str, object]:
        return {
            "jobId": self.job_id,
            "modelSourceId": self.model_source_id,
            "modelSourceSha256": self.model_source_sha256,
            "hierarchySourceId": self.hierarchy_source_id,
            "hierarchySourceSha256": self.hierarchy_source_sha256,
            "proof": self.proof.neutral(),
        }


@dataclass(frozen=True, slots=True)
class W3DJobSkinSafetyRejection:
    job_id: str
    owner_source_id: str
    owner_source_sha256: str
    reason_code: str
    active_primary_root_count: int
    active_secondary_root_count: int
    pivot_fixup_chunk_count: int

    def neutral(self) -> dict[str, object]:
        return {
            "jobId": self.job_id,
            "ownerSourceId": self.owner_source_id,
            "ownerSourceSha256": self.owner_source_sha256,
            "reasonCode": self.reason_code,
            "activePrimaryRootCount": self.active_primary_root_count,
            "activeSecondaryRootCount": self.active_secondary_root_count,
            "pivotFixupChunkCount": self.pivot_fixup_chunk_count,
        }


@dataclass(frozen=True, slots=True)
class W3DJobSkinSafetyReport:
    input_private_plan_sha256: str
    input_plan_evidence_sha256: str
    checked_job_count: int
    safe_job_count: int
    rejected_job_count: int
    skin_mesh_count: int
    influence_record_count: int
    hierarchy_pivot_count: int
    pivot_fixup_chunk_count: int
    active_primary_root_count: int
    active_secondary_root_count: int
    proofs: tuple[W3DJobSkinSafetyProof, ...]
    rejections: tuple[W3DJobSkinSafetyRejection, ...]
    forced_terminal_rows: tuple[W3DJobPreparationForcedTerminal, ...]
    evidence_sha256: str
    schema: str = field(init=False, default=W3D_JOB_SKIN_SAFETY_SCHEMA)
    schema_version: int = field(init=False, default=W3D_JOB_SKIN_SAFETY_VERSION)

    def _neutral(self, *, include_evidence_sha256: bool) -> dict[str, object]:
        hashes = {
            "inputPrivatePlanSha256": self.input_private_plan_sha256,
            "inputPlanEvidenceSha256": self.input_plan_evidence_sha256,
        }
        if include_evidence_sha256:
            hashes["evidenceSha256"] = self.evidence_sha256
        return {
            "schema": self.schema,
            "schemaVersion": self.schema_version,
            "hashes": hashes,
            "summary": {
                "checkedJobCount": self.checked_job_count,
                "safeJobCount": self.safe_job_count,
                "rejectedJobCount": self.rejected_job_count,
                "skinMeshCount": self.skin_mesh_count,
                "influenceRecordCount": self.influence_record_count,
                "hierarchyPivotCount": self.hierarchy_pivot_count,
                "pivotFixupChunkCount": self.pivot_fixup_chunk_count,
                "activePrimaryRootCount": self.active_primary_root_count,
                "activeSecondaryRootCount": self.active_secondary_root_count,
            },
            "proofs": [item.neutral() for item in self.proofs],
            "rejections": [item.neutral() for item in self.rejections],
            "forcedTerminals": [item.neutral() for item in self.forced_terminal_rows],
        }

    def evidence_hash_basis(self) -> dict[str, object]:
        return self._neutral(include_evidence_sha256=False)

    def neutral(self) -> dict[str, object]:
        return self._neutral(include_evidence_sha256=True)


@dataclass(frozen=True, slots=True)
class W3DJobPreparationPreflightReport:
    """Canonical read-only result for every raw preparation declaration."""

    input_private_plan_sha256: str
    input_plan_evidence_sha256: str
    catalog_input_sha256: str
    catalog_metadata_sha256: str
    source_count: int
    upstream_forced_terminal_rows: tuple[tuple[str, tuple[str, ...]], ...] | None
    upstream_forced_terminal_evidence_sha256: str | None
    declared_preparation_count: int
    provable_preparation_count: int
    rejected_preparation_count: int
    forced_terminal_rows: tuple[W3DJobPreparationForcedTerminal, ...]
    skin_safety_report: W3DJobSkinSafetyReport | None
    evidence_sha256: str
    schema: str = field(init=False, default=W3D_JOB_PREPARATION_PREFLIGHT_SCHEMA)
    schema_version: int = field(
        init=False,
        default=W3D_JOB_PREPARATION_PREFLIGHT_VERSION,
    )

    @property
    def planner_forced_terminal_rows(self) -> tuple[tuple[str, tuple[str, ...]], ...]:
        """Return the planner's path-free forced-terminal mapping contract."""

        return tuple(
            (row.source_id, row.reason_codes) for row in self.forced_terminal_rows
        )

    def merged_forced_terminals(
        self,
        upstream_forced_terminal_reasons: (Mapping[str, tuple[str, ...]] | None) = None,
        upstream_forced_terminal_evidence_sha256: str | None = None,
    ) -> tuple[dict[str, tuple[str, ...]], str]:
        """Merge rejected proofs into the exact bound upstream bridge."""

        return merge_w3d_preparation_forced_terminals(
            self,
            upstream_forced_terminal_reasons=upstream_forced_terminal_reasons,
            upstream_forced_terminal_evidence_sha256=(
                upstream_forced_terminal_evidence_sha256
            ),
        )

    def _neutral(self, *, include_evidence_sha256: bool) -> dict[str, object]:
        hashes = {
            "catalogInputSha256": self.catalog_input_sha256,
            "catalogMetadataSha256": self.catalog_metadata_sha256,
            "inputPrivatePlanSha256": self.input_private_plan_sha256,
            "inputPlanEvidenceSha256": self.input_plan_evidence_sha256,
        }
        if include_evidence_sha256:
            hashes["evidenceSha256"] = self.evidence_sha256
        return {
            "schema": self.schema,
            "schemaVersion": self.schema_version,
            "hashes": hashes,
            "summary": {
                "sourceCount": self.source_count,
                "declaredPreparationCount": self.declared_preparation_count,
                "provablePreparationCount": self.provable_preparation_count,
                "rejectedPreparationCount": self.rejected_preparation_count,
            },
            "forcedTerminals": [row.neutral() for row in self.forced_terminal_rows],
            "skinSafety": (
                None
                if self.skin_safety_report is None
                else self.skin_safety_report.neutral()
            ),
            "upstreamForcedTerminals": {
                "present": self.upstream_forced_terminal_rows is not None,
                "evidenceSha256": (self.upstream_forced_terminal_evidence_sha256),
                "rows": [
                    {"sourceId": source_id, "reasonCodes": list(reason_codes)}
                    for source_id, reason_codes in (
                        self.upstream_forced_terminal_rows or ()
                    )
                ],
            },
        }

    def evidence_hash_basis(self) -> dict[str, object]:
        return self._neutral(include_evidence_sha256=False)

    def neutral(self) -> dict[str, object]:
        return self._neutral(include_evidence_sha256=True)


@dataclass(frozen=True, slots=True)
class W3DJobPreparationFixedPointReport:
    """Ordered rejection proofs followed by one final zero-rejection proof."""

    reports: tuple[W3DJobPreparationPreflightReport, ...]
    catalog_input_sha256: str
    catalog_metadata_sha256: str
    source_count: int
    rejecting_iteration_count: int
    accumulated_rejection_count: int
    final_declared_preparation_count: int
    final_private_plan_sha256: str
    final_plan_evidence_sha256: str
    evidence_sha256: str
    schema: str = field(init=False, default=W3D_JOB_PREPARATION_FIXED_POINT_SCHEMA)
    schema_version: int = field(
        init=False,
        default=W3D_JOB_PREPARATION_FIXED_POINT_VERSION,
    )

    def _neutral(self, *, include_evidence_sha256: bool) -> dict[str, object]:
        hashes = {
            "catalogInputSha256": self.catalog_input_sha256,
            "catalogMetadataSha256": self.catalog_metadata_sha256,
            "finalPrivatePlanSha256": self.final_private_plan_sha256,
            "finalPlanEvidenceSha256": self.final_plan_evidence_sha256,
        }
        if include_evidence_sha256:
            hashes["evidenceSha256"] = self.evidence_sha256
        return {
            "schema": self.schema,
            "schemaVersion": self.schema_version,
            "hashes": hashes,
            "summary": {
                "sourceCount": self.source_count,
                "iterationCount": len(self.reports),
                "rejectingIterationCount": self.rejecting_iteration_count,
                "accumulatedRejectionCount": self.accumulated_rejection_count,
                "finalDeclaredPreparationCount": (
                    self.final_declared_preparation_count
                ),
            },
            "iterations": [item.neutral() for item in self.reports],
        }

    def evidence_hash_basis(self) -> dict[str, object]:
        return self._neutral(include_evidence_sha256=False)

    def neutral(self) -> dict[str, object]:
        return self._neutral(include_evidence_sha256=True)


@dataclass(frozen=True, slots=True)
class _FileSeal:
    byte_length: int
    sha256: str
    device: int
    inode: int
    modified_ns: int


@dataclass(frozen=True, slots=True)
class _PlanState:
    mode: Literal["none", "raw", "attested"]
    preparation_indices: tuple[int, ...]
    original_manifest_bytes: tuple[bytes, ...]


@dataclass(frozen=True, slots=True)
class _Replacement:
    job_index: int
    relative_path: str
    prepared_model_sha256: str
    evidence_sha256: str
    byte_length: int


@dataclass(frozen=True, slots=True)
class _PreparationProof:
    payload: bytes
    prepared_model_sha256: str
    evidence_sha256: str


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


def _sha256(value: bytes) -> str:
    return hashlib.sha256(value).hexdigest()


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str) and len(value) == 64 and set(value) <= _SHA256_CHARACTERS
    )


def _validate_forced_terminal_rows(
    rows: tuple[tuple[str, tuple[str, ...]], ...] | None,
    evidence_sha256: str | None,
) -> tuple[tuple[str, tuple[str, ...]], ...] | None:
    if rows is None:
        if evidence_sha256 is not None:
            raise W3DJobPreparationError(
                "upstream forced-terminal evidence is inconsistent"
            )
        return None
    if type(rows) is not tuple or not _is_sha256(evidence_sha256):
        raise W3DJobPreparationError(
            "upstream forced-terminal evidence is inconsistent"
        )
    validated: list[tuple[str, tuple[str, ...]]] = []
    for row in rows:
        if type(row) is not tuple or len(row) != 2:
            raise W3DJobPreparationError(
                "upstream forced-terminal evidence is malformed"
            )
        source_id, reason_codes = row
        if (
            not isinstance(source_id, str)
            or _SOURCE_ID.fullmatch(source_id) is None
            or type(reason_codes) is not tuple
            or not reason_codes
            or any(
                not isinstance(reason, str) or _REASON_CODE.fullmatch(reason) is None
                for reason in reason_codes
            )
            or reason_codes != tuple(sorted(set(reason_codes)))
        ):
            raise W3DJobPreparationError(
                "upstream forced-terminal evidence is malformed"
            )
        validated.append((source_id, reason_codes))
    if tuple(validated) != tuple(sorted(validated)) or len(
        {source_id for source_id, _ in validated}
    ) != len(validated):
        raise W3DJobPreparationError(
            "upstream forced-terminal evidence is not canonical"
        )
    return tuple(validated)


def _canonical_forced_terminal_mapping(
    mapping: Mapping[str, tuple[str, ...]],
    evidence_sha256: str | None,
) -> tuple[tuple[str, tuple[str, ...]], ...]:
    if not isinstance(mapping, Mapping) or not _is_sha256(evidence_sha256):
        raise W3DJobPreparationError(
            "supplied upstream forced-terminal evidence is invalid"
        )
    try:
        rows = tuple(sorted(mapping.items()))
    except (AttributeError, TypeError, ValueError):
        raise W3DJobPreparationError(
            "supplied upstream forced-terminal evidence is malformed"
        ) from None
    validated = _validate_forced_terminal_rows(rows, evidence_sha256)
    assert validated is not None
    return validated


def _validate_skin_safety_report(report: W3DJobSkinSafetyReport) -> None:
    if type(report) is not W3DJobSkinSafetyReport:
        raise TypeError("skin safety report must be a W3DJobSkinSafetyReport")
    if any(
        not _is_sha256(value)
        for value in (
            report.input_private_plan_sha256,
            report.input_plan_evidence_sha256,
            report.evidence_sha256,
        )
    ):
        raise W3DJobPreparationError("W3D job skin-safety report seal is invalid")
    counts = (
        report.checked_job_count,
        report.safe_job_count,
        report.rejected_job_count,
        report.skin_mesh_count,
        report.influence_record_count,
        report.hierarchy_pivot_count,
        report.pivot_fixup_chunk_count,
        report.active_primary_root_count,
        report.active_secondary_root_count,
    )
    if (
        any(type(value) is not int or value < 0 for value in counts)
        or type(report.proofs) is not tuple
        or type(report.rejections) is not tuple
        or type(report.forced_terminal_rows) is not tuple
        or report.checked_job_count != report.safe_job_count + report.rejected_job_count
        or report.safe_job_count != len(report.proofs)
        or report.rejected_job_count != len(report.rejections)
    ):
        raise W3DJobPreparationError("W3D job skin-safety report counts are invalid")

    proof_order: list[str] = []
    skin_mesh_count = 0
    influence_record_count = 0
    hierarchy_pivot_count = 0
    for row in report.proofs:
        if (
            type(row) is not W3DJobSkinSafetyProof
            or _JOB_ID.fullmatch(row.job_id) is None
            or not isinstance(row.model_source_id, str)
            or not row.model_source_id
            or not _is_sha256(row.model_source_sha256)
            or (row.hierarchy_source_id is None)
            != (row.hierarchy_source_sha256 is None)
            or (
                row.hierarchy_source_id is not None
                and (
                    not isinstance(row.hierarchy_source_id, str)
                    or not row.hierarchy_source_id
                )
            )
            or (
                row.hierarchy_source_sha256 is not None
                and not _is_sha256(row.hierarchy_source_sha256)
            )
        ):
            raise W3DJobPreparationError("W3D job skin-safety proof binding is invalid")
        try:
            validate_w3d_skin_safety_proof(row.proof)
        except (TypeError, W3DSkinSafetyError) as exc:
            raise W3DJobPreparationError(
                "W3D job skin-safety proof is invalid"
            ) from exc
        if (
            row.proof.model_sha256 != row.model_source_sha256
            or row.proof.hierarchy_sha256 != row.hierarchy_source_sha256
        ):
            raise W3DJobPreparationError(
                "W3D job skin-safety proof source binding is stale"
            )
        proof_order.append(row.job_id)
        skin_mesh_count += row.proof.skin_mesh_count
        influence_record_count += row.proof.influence_record_count
        hierarchy_pivot_count += row.proof.hierarchy_pivot_count
    if proof_order != sorted(proof_order) or len(set(proof_order)) != len(proof_order):
        raise W3DJobPreparationError(
            "W3D job skin-safety proof inventory is not canonical"
        )

    rejection_order: list[tuple[str, str, str]] = []
    forced: dict[str, tuple[str, set[str]]] = {}
    active_primary_root_count = 0
    active_secondary_root_count = 0
    pivot_fixup_chunk_count = 0
    allowed_reasons = {
        HIERARCHY_PIVOT_FIXUP_UNSUPPORTED,
        SKIN_ROOT_PIVOT_INFLUENCE_UNSUPPORTED,
        SKIN_SAFETY_PROOF_REJECTED,
    }
    for row in report.rejections:
        rejection_counts = (
            row.active_primary_root_count,
            row.active_secondary_root_count,
            row.pivot_fixup_chunk_count,
        )
        if (
            type(row) is not W3DJobSkinSafetyRejection
            or _JOB_ID.fullmatch(row.job_id) is None
            or _SOURCE_ID.fullmatch(row.owner_source_id) is None
            or not _is_sha256(row.owner_source_sha256)
            or row.reason_code not in allowed_reasons
            or any(type(value) is not int or value < 0 for value in rejection_counts)
            or (
                row.reason_code == SKIN_ROOT_PIVOT_INFLUENCE_UNSUPPORTED
                and not (
                    row.active_primary_root_count or row.active_secondary_root_count
                )
            )
            or (
                row.reason_code == HIERARCHY_PIVOT_FIXUP_UNSUPPORTED
                and row.pivot_fixup_chunk_count < 1
            )
        ):
            raise W3DJobPreparationError(
                "W3D job skin-safety rejection evidence is invalid"
            )
        rejection_order.append((row.job_id, row.owner_source_id, row.reason_code))
        existing = forced.get(row.owner_source_id)
        if existing is None:
            forced[row.owner_source_id] = (
                row.owner_source_sha256,
                {row.reason_code},
            )
        else:
            source_sha256, reasons = existing
            if source_sha256 != row.owner_source_sha256:
                raise W3DJobPreparationError(
                    "W3D job skin-safety rejection source SHA-256 conflicts"
                )
            reasons.add(row.reason_code)
        active_primary_root_count += row.active_primary_root_count
        active_secondary_root_count += row.active_secondary_root_count
        pivot_fixup_chunk_count += row.pivot_fixup_chunk_count
    if rejection_order != sorted(rejection_order) or len(
        {job_id for job_id, _, _ in rejection_order}
    ) != len(rejection_order):
        raise W3DJobPreparationError(
            "W3D job skin-safety rejection inventory is not canonical"
        )
    expected_forced_rows = tuple(
        W3DJobPreparationForcedTerminal(
            source_id=source_id,
            source_sha256=source_sha256,
            reason_codes=tuple(sorted(reasons)),
        )
        for source_id, (source_sha256, reasons) in sorted(forced.items())
    )
    if report.forced_terminal_rows != expected_forced_rows:
        raise W3DJobPreparationError(
            "W3D job skin-safety forced-terminal evidence is stale"
        )
    if (
        report.skin_mesh_count != skin_mesh_count
        or report.influence_record_count != influence_record_count
        or report.hierarchy_pivot_count != hierarchy_pivot_count
        or report.active_primary_root_count != active_primary_root_count
        or report.active_secondary_root_count != active_secondary_root_count
        or report.pivot_fixup_chunk_count != pivot_fixup_chunk_count
    ):
        raise W3DJobPreparationError("W3D job skin-safety aggregate evidence is stale")
    if _canonical_sha256(report.evidence_hash_basis()) != report.evidence_sha256:
        raise W3DJobPreparationError("W3D job skin-safety evidence seal is invalid")


def _validate_preflight_report(
    report: W3DJobPreparationPreflightReport,
) -> None:
    if type(report) is not W3DJobPreparationPreflightReport:
        raise TypeError("report must be a W3DJobPreparationPreflightReport")
    if any(
        not _is_sha256(value)
        for value in (
            report.input_private_plan_sha256,
            report.input_plan_evidence_sha256,
            report.catalog_input_sha256,
            report.catalog_metadata_sha256,
            report.evidence_sha256,
        )
    ):
        raise W3DJobPreparationError(
            "W3D preparation preflight report has an invalid seal"
        )
    counts = (
        report.source_count,
        report.declared_preparation_count,
        report.provable_preparation_count,
        report.rejected_preparation_count,
    )
    if (
        any(type(value) is not int or value < 0 for value in counts)
        or report.declared_preparation_count
        != report.provable_preparation_count + report.rejected_preparation_count
        or type(report.forced_terminal_rows) is not tuple
    ):
        raise W3DJobPreparationError(
            "W3D preparation preflight report counts are invalid"
        )
    row_order: list[tuple[str, str]] = []
    for row in report.forced_terminal_rows:
        if (
            type(row) is not W3DJobPreparationForcedTerminal
            or not isinstance(row.source_id, str)
            or _SOURCE_ID.fullmatch(row.source_id) is None
            or not _is_sha256(row.source_sha256)
            or type(row.reason_codes) is not tuple
            or not row.reason_codes
            or any(
                not isinstance(reason, str) or _REASON_CODE.fullmatch(reason) is None
                for reason in row.reason_codes
            )
            or row.reason_codes != tuple(sorted(set(row.reason_codes)))
        ):
            raise W3DJobPreparationError(
                "W3D preparation preflight terminal evidence is invalid"
            )
        row_order.append((row.source_id, row.source_sha256))
    if row_order != sorted(row_order) or len(
        {source_id for source_id, _ in row_order}
    ) != len(row_order):
        raise W3DJobPreparationError(
            "W3D preparation preflight terminal evidence is not canonical"
        )
    if report.rejected_preparation_count != sum(
        MODEL_PREPARATION_PROOF_REJECTED in row.reason_codes
        for row in report.forced_terminal_rows
    ):
        raise W3DJobPreparationError(
            "W3D preparation preflight rejection count is stale"
        )
    if report.skin_safety_report is not None:
        _validate_skin_safety_report(report.skin_safety_report)
        if (
            report.skin_safety_report.input_private_plan_sha256
            != report.input_private_plan_sha256
            or report.skin_safety_report.input_plan_evidence_sha256
            != report.input_plan_evidence_sha256
        ):
            raise W3DJobPreparationError(
                "W3D preparation preflight skin-safety binding is stale"
            )
        by_source_id = {row.source_id: row for row in report.forced_terminal_rows}
        for safety_row in report.skin_safety_report.forced_terminal_rows:
            combined = by_source_id.get(safety_row.source_id)
            if (
                combined is None
                or combined.source_sha256 != safety_row.source_sha256
                or not set(safety_row.reason_codes).issubset(combined.reason_codes)
            ):
                raise W3DJobPreparationError(
                    "W3D preparation preflight omits a skin-safety terminal"
                )
    elif len(report.forced_terminal_rows) != report.rejected_preparation_count:
        raise W3DJobPreparationError(
            "legacy W3D preparation preflight terminal count is invalid"
        )
    _validate_forced_terminal_rows(
        report.upstream_forced_terminal_rows,
        report.upstream_forced_terminal_evidence_sha256,
    )
    if _canonical_sha256(report.evidence_hash_basis()) != report.evidence_sha256:
        raise W3DJobPreparationError(
            "W3D preparation preflight evidence seal is invalid"
        )


def merge_w3d_preparation_forced_terminals(
    report: W3DJobPreparationPreflightReport,
    *,
    upstream_forced_terminal_reasons: (Mapping[str, tuple[str, ...]] | None) = None,
    upstream_forced_terminal_evidence_sha256: str | None = None,
) -> tuple[dict[str, tuple[str, ...]], str]:
    """Return an exact upstream-plus-preflight mapping and evidence seal."""

    _validate_preflight_report(report)
    bound_rows = report.upstream_forced_terminal_rows
    bound_seal = report.upstream_forced_terminal_evidence_sha256
    if (
        upstream_forced_terminal_reasons is None
        and upstream_forced_terminal_evidence_sha256 is None
    ):
        supplied_rows = bound_rows
        supplied_seal = bound_seal
    else:
        if upstream_forced_terminal_reasons is None:
            raise W3DJobPreparationError(
                "supplied upstream forced-terminal evidence is incomplete"
            )
        supplied_rows = _canonical_forced_terminal_mapping(
            upstream_forced_terminal_reasons,
            upstream_forced_terminal_evidence_sha256,
        )
        supplied_seal = upstream_forced_terminal_evidence_sha256
    if supplied_rows != bound_rows or supplied_seal != bound_seal:
        raise W3DJobPreparationError(
            "supplied upstream forced-terminal evidence mismatches preflight"
        )

    merged: dict[str, set[str]] = {
        source_id: set(reason_codes) for source_id, reason_codes in (bound_rows or ())
    }
    for row in report.forced_terminal_rows:
        merged.setdefault(row.source_id, set()).update(row.reason_codes)
    merged_rows = tuple(
        (source_id, tuple(sorted(reason_codes)))
        for source_id, reason_codes in sorted(merged.items())
    )
    mapping = {source_id: reason_codes for source_id, reason_codes in merged_rows}
    basis = {
        "schema": W3D_JOB_PREPARATION_FORCED_TERMINAL_SCHEMA,
        "schemaVersion": W3D_JOB_PREPARATION_FORCED_TERMINAL_VERSION,
        "upstream": {
            "evidenceSha256": bound_seal,
            "forcedTerminalReasons": [
                {"sourceId": source_id, "reasonCodes": list(reason_codes)}
                for source_id, reason_codes in (bound_rows or ())
            ],
        },
        "preparationPreflight": report.neutral(),
        "mergedForcedTerminalReasons": [
            {"sourceId": source_id, "reasonCodes": list(reason_codes)}
            for source_id, reason_codes in merged_rows
        ],
    }
    return mapping, _canonical_sha256(basis)


def _safe_w3d_path(value: object) -> str:
    if (
        not isinstance(value, str)
        or not value
        or len(value) > 512
        or "\\" in value
        or "\x00" in value
        or ":" in value
    ):
        raise W3DJobPreparationError("planned W3D path is unsafe")
    candidate = PurePosixPath(value)
    if (
        candidate.is_absolute()
        or candidate.as_posix() != value
        or any(part in {"", ".", ".."} for part in candidate.parts)
        or candidate.suffix.casefold() != ".w3d"
    ):
        raise W3DJobPreparationError("planned W3D path is unsafe")
    return value


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
            raise W3DJobPreparationError("declared filesystem path contains a link")


def _ordinary_root(value: Path | str) -> Path:
    candidate = Path(value).expanduser()
    _assert_no_link_chain(candidate)
    try:
        root = candidate.resolve(strict=True)
    except OSError:
        raise W3DJobPreparationError("W3D job root is unavailable") from None
    if not root.is_dir() or _is_link_like(root) or not root.name:
        raise W3DJobPreparationError("W3D job root must be an ordinary directory")
    _assert_no_link_chain(root.parent)
    return root


def _stat_identity(metadata: os.stat_result) -> tuple[int, int, int, int]:
    return (
        int(metadata.st_dev),
        int(metadata.st_ino),
        int(metadata.st_size),
        int(metadata.st_mtime_ns),
    )


def _read_ordinary_file(path: Path) -> tuple[bytes, _FileSeal]:
    _assert_no_link_chain(path)
    if _is_link_like(path):
        raise W3DJobPreparationError("W3D job tree contains a linked entry")
    try:
        before = path.stat(follow_symlinks=False)
        if (
            not stat_module.S_ISREG(before.st_mode)
            or getattr(before, "st_nlink", 1) != 1
        ):
            raise W3DJobPreparationError("W3D job tree contains a non-ordinary file")
        with path.open("rb") as stream:
            opened = os.fstat(stream.fileno())
            if (
                not stat_module.S_ISREG(opened.st_mode)
                or getattr(opened, "st_nlink", 1) != 1
                or _stat_identity(opened) != _stat_identity(before)
            ):
                raise W3DJobPreparationError("W3D job tree changed during inspection")
            payload = stream.read()
            after_open = os.fstat(stream.fileno())
        after = path.stat(follow_symlinks=False)
    except W3DJobPreparationError:
        raise
    except OSError:
        raise W3DJobPreparationError("W3D job file could not be inspected") from None
    if (
        _stat_identity(before) != _stat_identity(after_open)
        or _stat_identity(before) != _stat_identity(after)
        or len(payload) != before.st_size
        or _is_link_like(path)
    ):
        raise W3DJobPreparationError("W3D job tree changed during inspection")
    return payload, _FileSeal(
        byte_length=len(payload),
        sha256=_sha256(payload),
        device=int(before.st_dev),
        inode=int(before.st_ino),
        modified_ns=int(before.st_mtime_ns),
    )


def _scan_job_tree(root: Path) -> dict[str, _FileSeal]:
    result: dict[str, _FileSeal] = {}
    folded: set[str] = set()

    def visit(directory: Path) -> None:
        if _is_link_like(directory) or not directory.is_dir():
            raise W3DJobPreparationError(
                "W3D job tree contains a linked or non-directory entry"
            )
        try:
            with os.scandir(directory) as iterator:
                entries = sorted(
                    iterator,
                    key=lambda item: (item.name.casefold(), item.name),
                )
        except OSError:
            raise W3DJobPreparationError(
                "W3D job tree could not be inspected"
            ) from None
        for entry in entries:
            path = Path(entry.path)
            if _is_link_like(path) or entry.is_symlink():
                raise W3DJobPreparationError("W3D job tree contains a linked entry")
            try:
                metadata = entry.stat(follow_symlinks=False)
            except OSError:
                raise W3DJobPreparationError(
                    "W3D job tree could not be inspected"
                ) from None
            if stat_module.S_ISDIR(metadata.st_mode):
                visit(path)
                continue
            if not stat_module.S_ISREG(metadata.st_mode):
                raise W3DJobPreparationError(
                    "W3D job tree contains a non-ordinary entry"
                )
            relative = path.relative_to(root).as_posix()
            key = relative.casefold()
            if key in folded:
                raise W3DJobPreparationError(
                    "W3D job tree contains ambiguous file names"
                )
            _, seal = _read_ordinary_file(path)
            result[relative] = seal
            folded.add(key)

    visit(root)
    return result


def _validate_plan(
    plan: W3DJobPlan,
    *,
    execute_accounted_jobs: bool,
) -> _PlanState:
    if type(plan) is not W3DJobPlan:
        raise TypeError("plan must be a W3DJobPlan")
    if (
        type(plan.jobs) is not tuple
        or type(plan.batches) is not tuple
        or type(plan.terminals) is not tuple
    ):
        raise W3DJobPreparationError("W3D job plan is not immutable")
    if not execute_accounted_jobs:
        if (
            plan.terminals
            or type(plan.source_count) is not int
            or type(plan.consumed_source_count) is not int
            or plan.source_count < 0
            or plan.consumed_source_count != plan.source_count
            or not plan.source_accounting_complete
        ):
            raise W3DJobPreparationError(
                "W3D job plan has terminals or incomplete source accounting"
            )
    elif (
        type(plan.source_count) is not int
        or type(plan.consumed_source_count) is not int
        or plan.source_count < 0
        or plan.consumed_source_count < 0
        or plan.consumed_source_count > plan.source_count
    ):
        raise W3DJobPreparationError("W3D job plan source accounting is invalid")
    if not all(
        _is_sha256(value)
        for value in (
            plan.catalog_input_sha256,
            plan.catalog_metadata_sha256,
            plan.private_plan_sha256,
            plan.evidence_sha256,
        )
    ):
        raise W3DJobPreparationError("W3D job plan has an invalid seal")
    _validate_forced_terminal_rows(
        plan.forced_terminal_rows,
        plan.forced_terminal_evidence_sha256,
    )
    try:
        evidence_sha256 = _canonical_sha256(plan.evidence_hash_basis())
    except (AttributeError, TypeError, ValueError):
        raise W3DJobPreparationError("W3D job plan evidence is malformed") from None
    if evidence_sha256 != plan.evidence_sha256:
        raise W3DJobPreparationError("W3D job plan evidence seal is invalid")
    if (not plan.jobs) != (not plan.batches):
        raise W3DJobPreparationError("W3D job plan batch coverage is invalid")

    preparation_indices: list[int] = []
    preparation_states: set[str] = set()
    model_paths: dict[str, int] = {}
    job_ids: set[str] = set()
    output_paths: set[str] = set()
    consumed_source_ids: set[str] = set()
    roles: list[tuple[str, str | None, tuple[str, ...]]] = []

    for index, job in enumerate(plan.jobs):
        if type(job) is not W3DPlannedJob:
            raise W3DJobPreparationError("W3D job plan contains invalid job metadata")
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
            or type(job.animation_hierarchy_resolution_modes) is not tuple
            or len(job.animations) != len(job.animation_source_ids)
            or len(job.animations) != len(job.animation_source_sha256s)
            or any(
                not isinstance(source_id, str) or not source_id
                for source_id in job.animation_source_ids
            )
            or any(
                not _is_sha256(source_sha256)
                for source_sha256 in job.animation_source_sha256s
            )
        ):
            raise W3DJobPreparationError("W3D job plan contains invalid job metadata")
        if not w3d_job_resolution_contract_is_valid(job):
            raise W3DJobPreparationError("W3D job resolution contract is invalid")
        if len(job.animations) != len(job.animation_hierarchy_resolution_modes):
            raise W3DJobPreparationError(
                "planned animation hierarchy resolution is invalid"
            )
        if any(
            mode
            not in {
                HIERARCHY_RESOLUTION_SAME_SOURCE,
                HIERARCHY_RESOLUTION_SIBLING_PATH,
                HIERARCHY_RESOLUTION_IDENTIFIER_PREFIX_PATH,
            }
            for mode in job.animation_hierarchy_resolution_modes
        ):
            raise W3DJobPreparationError(
                "planned animation hierarchy resolution is invalid"
            )
        consumed_source_ids.add(job.model_source_id)
        consumed_source_ids.update(job.animation_source_ids)
        model = _safe_w3d_path(job.model)
        animations = tuple(_safe_w3d_path(path) for path in job.animations)
        if len({path.casefold() for path in animations}) != len(animations):
            raise W3DJobPreparationError(
                "W3D job plan contains duplicate animation inputs"
            )
        hierarchy_fields = (
            job.hierarchy,
            job.hierarchy_source_id,
            job.hierarchy_source_sha256,
        )
        if any(value is None for value in hierarchy_fields) != all(
            value is None for value in hierarchy_fields
        ):
            raise W3DJobPreparationError(
                "planned hierarchy evidence is missing or ambiguous"
            )
        hierarchy: str | None = None
        if job.hierarchy is not None:
            hierarchy = _safe_w3d_path(job.hierarchy)
            if (
                not isinstance(job.hierarchy_source_id, str)
                or not job.hierarchy_source_id
                or not _is_sha256(job.hierarchy_source_sha256)
            ):
                raise W3DJobPreparationError(
                    "planned hierarchy evidence is missing or ambiguous"
                )
            consumed_source_ids.add(job.hierarchy_source_id)
            if hierarchy == model and (
                job.hierarchy_source_id != job.model_source_id
                or job.hierarchy_source_sha256 != job.model_source_sha256
            ):
                raise W3DJobPreparationError(
                    "embedded hierarchy evidence is inconsistent"
                )
            same_source_hierarchy = (
                hierarchy == model
                and job.hierarchy_source_id == job.model_source_id
                and job.hierarchy_source_sha256 == job.model_source_sha256
            )
            sibling_hierarchy = (
                hierarchy != model and job.hierarchy_source_id != job.model_source_id
            )
            if (
                job.hierarchy_resolution_mode == HIERARCHY_RESOLUTION_SAME_SOURCE
                and not same_source_hierarchy
            ) or (
                job.hierarchy_resolution_mode
                in {
                    HIERARCHY_RESOLUTION_SIBLING_PATH,
                    HIERARCHY_RESOLUTION_IDENTIFIER_PREFIX_PATH,
                }
                and not sibling_hierarchy
            ):
                raise W3DJobPreparationError(
                    "planned hierarchy resolution evidence is inconsistent"
                )
            if job.hierarchy_resolution_mode not in {
                HIERARCHY_RESOLUTION_SAME_SOURCE,
                HIERARCHY_RESOLUTION_SIBLING_PATH,
                HIERARCHY_RESOLUTION_IDENTIFIER_PREFIX_PATH,
            }:
                raise W3DJobPreparationError(
                    "planned hierarchy resolution evidence is inconsistent"
                )
        elif job.hierarchy_resolution_mode is not None:
            raise W3DJobPreparationError(
                "planned hierarchy resolution evidence is inconsistent"
            )

        if animations and hierarchy is None:
            raise W3DJobPreparationError(
                "planned animation hierarchy resolution is inconsistent"
            )
        for animation, source_id, source_sha256, mode in zip(
            animations,
            job.animation_source_ids,
            job.animation_source_sha256s,
            job.animation_hierarchy_resolution_modes,
            strict=True,
        ):
            same_source_animation = (
                animation == hierarchy
                and source_id == job.hierarchy_source_id
                and source_sha256 == job.hierarchy_source_sha256
            )
            sibling_animation = (
                animation != hierarchy and source_id != job.hierarchy_source_id
            )
            if (
                mode == HIERARCHY_RESOLUTION_SAME_SOURCE and not same_source_animation
            ) or (
                mode
                in {
                    HIERARCHY_RESOLUTION_SIBLING_PATH,
                    HIERARCHY_RESOLUTION_IDENTIFIER_PREFIX_PATH,
                }
                and not sibling_animation
            ):
                raise W3DJobPreparationError(
                    "planned animation hierarchy resolution is inconsistent"
                )

        role_paths = [model]
        role_source_ids = [job.model_source_id]
        if hierarchy is not None:
            role_paths.append(hierarchy)
            assert job.hierarchy_source_id is not None
            role_source_ids.append(job.hierarchy_source_id)
        role_paths.extend(animations)
        role_source_ids.extend(job.animation_source_ids)
        has_role_overlap = len({path.casefold() for path in role_paths}) != len(
            role_paths
        ) or len(set(role_source_ids)) != len(role_source_ids)
        exact_embedded_animation = w3d_job_is_exact_embedded_model_animation(job)
        same_source_hierarchy_only = (
            not animations
            and hierarchy == model
            and job.hierarchy_source_id == job.model_source_id
        )
        if has_role_overlap and not (
            same_source_hierarchy_only or exact_embedded_animation
        ):
            raise W3DJobPreparationError("W3D job resolution overlap is invalid")
        pair = (
            job.prepared_model_sha256,
            job.model_preparation_evidence_sha256,
        )
        if (pair[0] is None) != (pair[1] is None):
            raise W3DJobPreparationError("model preparation attestation is partial")
        if job.model_preparation is None:
            if pair != (None, None):
                raise W3DJobPreparationError(
                    "model preparation attestation is incoherent"
                )
        else:
            if job.model_preparation != SECONDARY_SKIN_PREPARATION:
                raise W3DJobPreparationError("model preparation kind is unsupported")
            if hierarchy is None:
                raise W3DJobPreparationError(
                    "planned hierarchy evidence is missing or ambiguous"
                )
            preparation_indices.append(index)
            if pair == (None, None):
                preparation_states.add("raw")
            else:
                if (
                    not _is_sha256(pair[0])
                    or not _is_sha256(pair[1])
                    or pair[0] == job.model_source_sha256
                ):
                    raise W3DJobPreparationError(
                        "model preparation attestation is incoherent"
                    )
                preparation_states.add("attested")

        model_key = model.casefold()
        if model_key in model_paths:
            raise W3DJobPreparationError(
                "W3D job plan has duplicate or conflicting model targets"
            )
        model_paths[model_key] = index
        if job.job_id in job_ids:
            raise W3DJobPreparationError("W3D job plan contains duplicate jobs")
        job_ids.add(job.job_id)
        expected_output = f"glb/{job.job_id}.glb"
        if job.output != expected_output or job.output.casefold() in output_paths:
            raise W3DJobPreparationError("W3D job plan output identity is invalid")
        output_paths.add(job.output.casefold())
        roles.append((model, hierarchy, animations))

    if len(preparation_states) > 1:
        raise W3DJobPreparationError(
            "W3D job plan mixes raw and pre-attested preparations"
        )
    preparation_targets = {
        roles[index][0].casefold(): index for index in preparation_indices
    }
    for job_index, (model, hierarchy, animations) in enumerate(roles):
        del model
        if hierarchy is not None and hierarchy.casefold() in preparation_targets:
            owner = preparation_targets[hierarchy.casefold()]
            if owner != job_index or hierarchy != roles[owner][0]:
                raise W3DJobPreparationError(
                    "prepared model is shared by a conflicting input role"
                )
        if any(path.casefold() in preparation_targets for path in animations):
            raise W3DJobPreparationError(
                "prepared model is shared by a conflicting input role"
            )

    flattened: list[W3DPlannedJob] = []
    original_manifest_bytes: list[bytes] = []
    batch_ids: set[str] = set()
    for batch in plan.batches:
        if type(batch) is not W3DJobBatch or type(batch.jobs) is not tuple:
            raise W3DJobPreparationError("W3D job plan contains an invalid batch")
        try:
            payload = batch.manifest_bytes()
        except (AttributeError, TypeError, ValueError):
            raise W3DJobPreparationError("W3D job plan batch is malformed") from None
        digest = _sha256(payload)
        if (
            not batch.jobs
            or len(batch.jobs) > MAX_BATCH_JOBS
            or len(payload) > MAX_BATCH_MANIFEST_BYTES
            or not _is_sha256(batch.manifest_sha256)
            or digest != batch.manifest_sha256
            or batch.batch_id != f"batch-{digest[:32]}"
            or batch.batch_id in batch_ids
        ):
            raise W3DJobPreparationError("W3D job plan batch seal is invalid")
        batch_ids.add(batch.batch_id)
        flattened.extend(batch.jobs)
        original_manifest_bytes.append(payload)
    if tuple(flattened) != plan.jobs:
        raise W3DJobPreparationError("W3D job plan batches do not exactly cover jobs")

    terminal_source_ids: set[str] = set()
    terminal_order: list[tuple[str, str]] = []
    for terminal in plan.terminals:
        if (
            type(terminal) is not W3DTerminal
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
            raise W3DJobPreparationError("W3D job plan terminal evidence is invalid")
        if terminal.source_id in terminal_source_ids:
            raise W3DJobPreparationError(
                "W3D job plan contains duplicate terminal source IDs"
            )
        terminal_source_ids.add(terminal.source_id)
        terminal_order.append((terminal.source_id, terminal.source_sha256))
    if terminal_order != sorted(terminal_order):
        raise W3DJobPreparationError("W3D job plan terminal inventory is not canonical")

    if execute_accounted_jobs:
        if not plan.jobs:
            raise W3DJobPreparationError(
                "accounted-job preparation requires at least one planned job"
            )
        if len(consumed_source_ids) != plan.consumed_source_count:
            raise W3DJobPreparationError(
                "accounted-job preparation consumed source IDs are not exact"
            )
        if consumed_source_ids & terminal_source_ids:
            raise W3DJobPreparationError(
                "accounted-job preparation sources overlap the terminal inventory"
            )
        if plan.consumed_source_count + len(plan.terminals) != plan.source_count:
            raise W3DJobPreparationError(
                "accounted-job preparation source accounting is not exact"
            )

    mode: Literal["none", "raw", "attested"]
    if not preparation_indices:
        mode = "none"
    elif preparation_states == {"attested"}:
        mode = "attested"
    else:
        mode = "raw"
    return _PlanState(
        mode=mode,
        preparation_indices=tuple(preparation_indices),
        original_manifest_bytes=tuple(original_manifest_bytes),
    )


def _exact_file(snapshot: dict[str, _FileSeal], relative_path: str) -> _FileSeal:
    seal = snapshot.get(relative_path)
    if seal is None:
        raise W3DJobPreparationError(
            "planned W3D input is missing or ambiguously named"
        )
    return seal


def _validate_filesystem_bindings(
    plan: W3DJobPlan,
    state: _PlanState,
    snapshot: dict[str, _FileSeal],
) -> None:
    preparing = set(state.preparation_indices)
    for index, job in enumerate(plan.jobs):
        model = _exact_file(snapshot, job.model)
        expected_model = (
            job.prepared_model_sha256
            if state.mode == "attested" and index in preparing
            else job.model_source_sha256
        )
        if model.sha256 != expected_model:
            raise W3DJobPreparationError(
                "planned model source does not match its SHA-256"
            )
        for animation, expected_sha256 in zip(
            job.animations, job.animation_source_sha256s, strict=True
        ):
            if _exact_file(snapshot, animation).sha256 != expected_sha256:
                raise W3DJobPreparationError(
                    "planned animation source does not match its SHA-256"
                )
        if job.hierarchy is None:
            continue
        assert job.hierarchy_source_sha256 is not None
        if job.hierarchy == job.model:
            if job.hierarchy_source_sha256 != job.model_source_sha256:
                raise W3DJobPreparationError(
                    "embedded hierarchy evidence is inconsistent"
                )
            if state.mode != "attested" or index not in preparing:
                if model.sha256 != job.hierarchy_source_sha256:
                    raise W3DJobPreparationError(
                        "planned hierarchy source does not match its SHA-256"
                    )
        else:
            hierarchy = _exact_file(snapshot, job.hierarchy)
            if hierarchy.sha256 != job.hierarchy_source_sha256:
                raise W3DJobPreparationError(
                    "planned hierarchy source does not match its SHA-256"
                )


def _owned_sibling(root: Path, prefix: str) -> Path:
    try:
        candidate = Path(tempfile.mkdtemp(prefix=prefix, dir=root.parent))
    except OSError:
        raise W3DJobPreparationError(
            "W3D preparation transaction could not be created"
        ) from None
    if candidate.parent != root.parent or not candidate.name.startswith(prefix):
        raise W3DJobPreparationError("W3D preparation transaction ownership is invalid")
    return candidate


def _transaction_path(tree: Path, relative_path: str) -> Path:
    candidate = tree.joinpath(*PurePosixPath(relative_path).parts)
    if tree not in candidate.parents:
        raise W3DJobPreparationError(
            "W3D preparation transaction path escaped its owner"
        )
    return candidate


def _write_staged_file(path: Path, payload: bytes) -> _FileSeal:
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
    except OSError:
        raise W3DJobPreparationError(
            "W3D preparation output could not be staged"
        ) from None
    _, seal = _read_ordinary_file(path)
    if seal.sha256 != _sha256(payload) or seal.byte_length != len(payload):
        raise W3DJobPreparationError("W3D preparation staged output seal is invalid")
    return seal


def _remove_owned_tree(path: Path | None, parent: Path, prefix: str) -> None:
    if path is None or not os.path.lexists(path):
        return
    if path.parent != parent or not path.name.startswith(prefix):
        raise W3DJobPreparationError(
            "refused to remove an unowned preparation transaction"
        )
    if _is_link_like(path) or not path.is_dir():
        raise W3DJobPreparationError("preparation transaction ownership changed")
    try:
        shutil.rmtree(path)
    except OSError:
        raise W3DJobPreparationError(
            "preparation transaction could not be removed"
        ) from None


def _read_bound_payload(
    root: Path,
    relative_path: str,
    expected: _FileSeal,
) -> bytes:
    path = root.joinpath(*PurePosixPath(relative_path).parts)
    payload, actual = _read_ordinary_file(path)
    if actual != expected:
        raise W3DJobPreparationError("W3D job tree changed during preparation")
    return payload


def _job_skin_safety_report(
    plan: W3DJobPlan,
    root: Path,
    snapshot: dict[str, _FileSeal],
) -> W3DJobSkinSafetyReport:
    proofs: list[W3DJobSkinSafetyProof] = []
    rejections: list[W3DJobSkinSafetyRejection] = []
    for job in plan.jobs:
        model_seal = _exact_file(snapshot, job.model)
        model = _read_bound_payload(root, job.model, model_seal)
        hierarchy = None
        hierarchy_seal = None
        if job.hierarchy is not None:
            if job.hierarchy == job.model:
                hierarchy = model
                hierarchy_seal = model_seal
            else:
                hierarchy_seal = _exact_file(snapshot, job.hierarchy)
                hierarchy = _read_bound_payload(root, job.hierarchy, hierarchy_seal)
        try:
            proof = prove_w3d_skin_safety(model, hierarchy)
        except W3DSkinSafetyError as error:
            if error.owner == "hierarchy" and (
                job.hierarchy_source_id is not None and hierarchy_seal is not None
            ):
                owner_source_id = job.hierarchy_source_id
                owner_source_sha256 = hierarchy_seal.sha256
            else:
                owner_source_id = job.model_source_id
                owner_source_sha256 = model_seal.sha256
            rejections.append(
                W3DJobSkinSafetyRejection(
                    job_id=job.job_id,
                    owner_source_id=owner_source_id,
                    owner_source_sha256=owner_source_sha256,
                    reason_code=error.reason_code,
                    active_primary_root_count=(error.active_primary_root_count),
                    active_secondary_root_count=(error.active_secondary_root_count),
                    pivot_fixup_chunk_count=error.pivot_fixup_chunk_count,
                )
            )
            continue
        proofs.append(
            W3DJobSkinSafetyProof(
                job_id=job.job_id,
                model_source_id=job.model_source_id,
                model_source_sha256=model_seal.sha256,
                hierarchy_source_id=job.hierarchy_source_id,
                hierarchy_source_sha256=(
                    None if hierarchy_seal is None else hierarchy_seal.sha256
                ),
                proof=proof,
            )
        )

    proofs.sort(key=lambda item: item.job_id)
    rejections.sort(
        key=lambda item: (item.job_id, item.owner_source_id, item.reason_code)
    )
    forced: dict[str, tuple[str, set[str]]] = {}
    for rejection in rejections:
        existing = forced.get(rejection.owner_source_id)
        if existing is None:
            forced[rejection.owner_source_id] = (
                rejection.owner_source_sha256,
                {rejection.reason_code},
            )
            continue
        source_sha256, reasons = existing
        if source_sha256 != rejection.owner_source_sha256:
            raise W3DJobPreparationError("W3D job skin-safety source SHA-256 conflicts")
        reasons.add(rejection.reason_code)
    forced_rows = tuple(
        W3DJobPreparationForcedTerminal(
            source_id=source_id,
            source_sha256=source_sha256,
            reason_codes=tuple(sorted(reasons)),
        )
        for source_id, (source_sha256, reasons) in sorted(forced.items())
    )
    provisional = W3DJobSkinSafetyReport(
        input_private_plan_sha256=plan.private_plan_sha256,
        input_plan_evidence_sha256=plan.evidence_sha256,
        checked_job_count=len(plan.jobs),
        safe_job_count=len(proofs),
        rejected_job_count=len(rejections),
        skin_mesh_count=sum(item.proof.skin_mesh_count for item in proofs),
        influence_record_count=sum(
            item.proof.influence_record_count for item in proofs
        ),
        hierarchy_pivot_count=sum(item.proof.hierarchy_pivot_count for item in proofs),
        pivot_fixup_chunk_count=sum(
            item.pivot_fixup_chunk_count for item in rejections
        ),
        active_primary_root_count=sum(
            item.active_primary_root_count for item in rejections
        ),
        active_secondary_root_count=sum(
            item.active_secondary_root_count for item in rejections
        ),
        proofs=tuple(proofs),
        rejections=tuple(rejections),
        forced_terminal_rows=forced_rows,
        evidence_sha256="",
    )
    report = replace(
        provisional,
        evidence_sha256=_canonical_sha256(provisional.evidence_hash_basis()),
    )
    _validate_skin_safety_report(report)
    return report


def _prove_declared_preparation(
    job: W3DPlannedJob,
    model: bytes,
    hierarchy: bytes,
) -> _PreparationProof:
    """Run the exact proof used by both preflight and transactional mutation."""

    if (
        job.model_preparation != SECONDARY_SKIN_PREPARATION
        or job.hierarchy_source_sha256 is None
    ):
        raise W3DJobPreparationError("W3D model preparation proof is incoherent")
    try:
        result = strip_proven_redundant_secondary_skin_streams(
            model,
            hierarchy,
        )
        payload = result.model_bytes()
        proof = result.proof
        if type(payload) is not bytes:
            raise TypeError
        output_sha256 = _sha256(payload)
        coherent = (
            proof.input_model_sha256 == job.model_source_sha256
            and proof.hierarchy_sha256 == job.hierarchy_source_sha256
            and proof.output_model_sha256 == output_sha256
            and _is_sha256(proof.proof_sha256)
            and output_sha256 != job.model_source_sha256
            and proof.removed_chunk_count >= 2
            and proof.removed_byte_count > 0
        )
    except Exception:
        raise W3DJobPreparationError("W3D model preparation proof failed") from None
    if not coherent:
        raise W3DJobPreparationError("W3D model preparation proof is incoherent")
    return _PreparationProof(
        payload=payload,
        prepared_model_sha256=output_sha256,
        evidence_sha256=proof.proof_sha256,
    )


def _prepare_replacements(
    plan: W3DJobPlan,
    state: _PlanState,
    root: Path,
    snapshot: dict[str, _FileSeal],
    staging: Path,
) -> tuple[_Replacement, ...]:
    replacements: list[_Replacement] = []
    for job_index in state.preparation_indices:
        job = plan.jobs[job_index]
        assert job.hierarchy is not None
        assert job.hierarchy_source_sha256 is not None
        model = _read_bound_payload(
            root,
            job.model,
            _exact_file(snapshot, job.model),
        )
        hierarchy = (
            model
            if job.hierarchy == job.model
            else _read_bound_payload(
                root,
                job.hierarchy,
                _exact_file(snapshot, job.hierarchy),
            )
        )
        proof = _prove_declared_preparation(job, model, hierarchy)
        staged = _transaction_path(staging, job.model)
        staged_seal = _write_staged_file(staged, proof.payload)
        replacements.append(
            _Replacement(
                job_index=job_index,
                relative_path=job.model,
                prepared_model_sha256=proof.prepared_model_sha256,
                evidence_sha256=proof.evidence_sha256,
                byte_length=staged_seal.byte_length,
            )
        )
    return tuple(replacements)


def _reseal_plan(
    plan: W3DJobPlan,
    state: _PlanState,
    replacements: tuple[_Replacement, ...],
) -> W3DJobPlan:
    by_index = {item.job_index: item for item in replacements}
    jobs = tuple(
        replace(
            job,
            prepared_model_sha256=by_index[index].prepared_model_sha256,
            model_preparation_evidence_sha256=by_index[index].evidence_sha256,
        )
        if index in by_index
        else job
        for index, job in enumerate(plan.jobs)
    )
    batches: list[W3DJobBatch] = []
    cursor = 0
    for batch_index, batch in enumerate(plan.batches):
        selected = jobs[cursor : cursor + len(batch.jobs)]
        cursor += len(batch.jobs)
        replacement = W3DJobBatch(
            batch_id=batch.batch_id,
            jobs=selected,
            manifest_sha256=batch.manifest_sha256,
        )
        if (
            replacement.manifest_bytes() != state.original_manifest_bytes[batch_index]
            or replacement.manifest_sha256 != batch.manifest_sha256
            or replacement.batch_id != batch.batch_id
        ):
            raise W3DJobPreparationError(
                "model preparation changed an adapter manifest"
            )
        batches.append(replacement)
    if cursor != len(jobs):
        raise W3DJobPreparationError("prepared W3D batches do not exactly cover jobs")

    preparation_basis = {
        "schema": W3D_JOB_PREPARATION_SCHEMA,
        "schemaVersion": W3D_JOB_PREPARATION_VERSION,
        "inputPrivatePlanSha256": plan.private_plan_sha256,
        "preparations": [
            {
                "ordinal": item.job_index,
                "kind": SECONDARY_SKIN_PREPARATION,
                "modelSourceSha256": plan.jobs[item.job_index].model_source_sha256,
                "hierarchySourceSha256": plan.jobs[
                    item.job_index
                ].hierarchy_source_sha256,
                "preparedModelSha256": item.prepared_model_sha256,
                "evidenceSha256": item.evidence_sha256,
            }
            for item in replacements
        ],
    }
    preparation_seal_sha256 = _canonical_sha256(preparation_basis)
    private_plan_sha256 = _canonical_sha256(
        {
            "schema": f"{W3D_JOB_PREPARATION_SCHEMA}.plan-seal",
            "schemaVersion": W3D_JOB_PREPARATION_VERSION,
            "inputPrivatePlanSha256": plan.private_plan_sha256,
            "preparationSealSha256": preparation_seal_sha256,
        }
    )
    provisional = replace(
        plan,
        jobs=jobs,
        batches=tuple(batches),
        private_plan_sha256=private_plan_sha256,
        evidence_sha256="",
    )
    return replace(
        provisional,
        evidence_sha256=_canonical_sha256(provisional.evidence_hash_basis()),
    )


def _snapshot_unchanged(
    before: dict[str, _FileSeal], after: dict[str, _FileSeal]
) -> bool:
    return before == after


def _verify_committed_tree(
    before: dict[str, _FileSeal],
    after: dict[str, _FileSeal],
    replacements: tuple[_Replacement, ...],
) -> None:
    if before.keys() != after.keys():
        raise W3DJobPreparationError(
            "W3D preparation changed undeclared job-tree entries"
        )
    targets = {item.relative_path: item for item in replacements}
    if len(targets) != len(replacements):
        raise W3DJobPreparationError(
            "W3D preparation contains duplicate replacement targets"
        )
    for relative_path, original in before.items():
        prepared = after[relative_path]
        replacement = targets.get(relative_path)
        if replacement is None:
            if prepared != original:
                raise W3DJobPreparationError(
                    "W3D preparation changed an undeclared input"
                )
        elif (
            prepared.sha256 != replacement.prepared_model_sha256
            or prepared.byte_length != replacement.byte_length
        ):
            raise W3DJobPreparationError("W3D preparation replacement seal is invalid")


def _rollback(
    root: Path,
    backup: Path,
    replacements: tuple[_Replacement, ...],
    before: dict[str, _FileSeal],
) -> bool:
    succeeded = True
    for item in reversed(replacements):
        target = root.joinpath(*PurePosixPath(item.relative_path).parts)
        saved = _transaction_path(backup, item.relative_path)
        if not os.path.lexists(saved):
            continue
        try:
            os.replace(saved, target)
        except Exception:
            succeeded = False
    try:
        succeeded = succeeded and _snapshot_unchanged(
            before,
            _scan_job_tree(root),
        )
    except W3DJobPreparationError:
        succeeded = False
    return succeeded


def _commit_replacements(
    root: Path,
    staging: Path,
    backup: Path,
    replacements: tuple[_Replacement, ...],
    before: dict[str, _FileSeal],
) -> None:
    try:
        for item in replacements:
            target = root.joinpath(*PurePosixPath(item.relative_path).parts)
            staged = _transaction_path(staging, item.relative_path)
            saved = _transaction_path(backup, item.relative_path)
            saved.parent.mkdir(parents=True, exist_ok=True)
            current_payload, current = _read_ordinary_file(target)
            del current_payload
            if current != before[item.relative_path]:
                raise W3DJobPreparationError(
                    "W3D job tree changed before preparation commit"
                )
            os.replace(target, saved)
            os.replace(staged, target)
        _verify_committed_tree(
            before,
            _scan_job_tree(root),
            replacements,
        )
    except Exception:
        if not _rollback(root, backup, replacements, before):
            raise W3DJobPreparationError(
                "W3D preparation commit and rollback both failed"
            ) from None
        raise W3DJobPreparationError(
            "W3D preparation commit failed and was rolled back"
        ) from None


def _preflight_report(
    plan: W3DJobPlan,
    declared_count: int,
    rejected_rows: list[W3DJobPreparationForcedTerminal],
    skin_safety_report: W3DJobSkinSafetyReport,
) -> W3DJobPreparationPreflightReport:
    preparation_rows = tuple(
        sorted(
            rejected_rows,
            key=lambda row: (row.source_id, row.source_sha256),
        )
    )
    if len({row.source_id for row in preparation_rows}) != len(preparation_rows):
        raise W3DJobPreparationError(
            "W3D preparation preflight terminal inventory is invalid"
        )
    merged: dict[str, tuple[str, set[str]]] = {}
    for row in (*skin_safety_report.forced_terminal_rows, *preparation_rows):
        existing = merged.get(row.source_id)
        if existing is None:
            merged[row.source_id] = (row.source_sha256, set(row.reason_codes))
            continue
        source_sha256, reasons = existing
        if source_sha256 != row.source_sha256:
            raise W3DJobPreparationError(
                "W3D preparation preflight source SHA-256 conflicts"
            )
        reasons.update(row.reason_codes)
    rows = tuple(
        W3DJobPreparationForcedTerminal(
            source_id=source_id,
            source_sha256=source_sha256,
            reason_codes=tuple(sorted(reasons)),
        )
        for source_id, (source_sha256, reasons) in sorted(merged.items())
    )
    provisional = W3DJobPreparationPreflightReport(
        input_private_plan_sha256=plan.private_plan_sha256,
        input_plan_evidence_sha256=plan.evidence_sha256,
        catalog_input_sha256=plan.catalog_input_sha256,
        catalog_metadata_sha256=plan.catalog_metadata_sha256,
        source_count=plan.source_count,
        upstream_forced_terminal_rows=plan.forced_terminal_rows,
        upstream_forced_terminal_evidence_sha256=(plan.forced_terminal_evidence_sha256),
        declared_preparation_count=declared_count,
        provable_preparation_count=declared_count - len(preparation_rows),
        rejected_preparation_count=len(preparation_rows),
        forced_terminal_rows=rows,
        skin_safety_report=skin_safety_report,
        evidence_sha256="",
    )
    report = replace(
        provisional,
        evidence_sha256=_canonical_sha256(provisional.evidence_hash_basis()),
    )
    _validate_preflight_report(report)
    return report


def _validate_fixed_point_report(
    report: W3DJobPreparationFixedPointReport,
    final_plan: W3DJobPlan,
) -> None:
    if type(report) is not W3DJobPreparationFixedPointReport:
        raise TypeError("report must be a W3DJobPreparationFixedPointReport")
    if type(report.reports) is not tuple or not report.reports:
        raise W3DJobPreparationError(
            "W3D preparation fixed-point iteration evidence is empty"
        )
    if any(
        not _is_sha256(value)
        for value in (
            report.catalog_input_sha256,
            report.catalog_metadata_sha256,
            report.final_private_plan_sha256,
            report.final_plan_evidence_sha256,
            report.evidence_sha256,
        )
    ):
        raise W3DJobPreparationError(
            "W3D preparation fixed-point evidence has an invalid seal"
        )
    state = _validate_plan(final_plan, execute_accounted_jobs=True)
    if state.mode == "attested":
        raise W3DJobPreparationError(
            "W3D preparation fixed-point evidence requires a raw final plan"
        )
    counts = (
        report.source_count,
        report.rejecting_iteration_count,
        report.accumulated_rejection_count,
        report.final_declared_preparation_count,
    )
    if any(type(value) is not int or value < 0 for value in counts):
        raise W3DJobPreparationError("W3D preparation fixed-point counts are invalid")
    if (
        report.source_count != final_plan.source_count
        or report.catalog_input_sha256 != final_plan.catalog_input_sha256
        or report.catalog_metadata_sha256 != final_plan.catalog_metadata_sha256
        or report.final_private_plan_sha256 != final_plan.private_plan_sha256
        or report.final_plan_evidence_sha256 != final_plan.evidence_sha256
        or report.rejecting_iteration_count != len(report.reports) - 1
        or report.final_declared_preparation_count != len(state.preparation_indices)
    ):
        raise W3DJobPreparationError(
            "W3D preparation fixed-point final-plan binding is stale"
        )

    initial_forced_count = len(report.reports[0].upstream_forced_terminal_rows or ())
    if report.rejecting_iteration_count > report.source_count - initial_forced_count:
        raise W3DJobPreparationError(
            "W3D preparation fixed-point iteration bound is invalid"
        )

    rejected_source_ids: set[str] = set()
    accumulated_rejection_count = 0
    for index, item in enumerate(report.reports):
        _validate_preflight_report(item)
        if item.skin_safety_report is None:
            raise W3DJobPreparationError(
                "W3D preparation fixed-point lacks skin-safety evidence"
            )
        if (
            item.catalog_input_sha256 != report.catalog_input_sha256
            or item.catalog_metadata_sha256 != report.catalog_metadata_sha256
            or item.source_count != report.source_count
        ):
            raise W3DJobPreparationError(
                "W3D preparation fixed-point catalog evidence is stale"
            )
        final_iteration = index == len(report.reports) - 1
        if final_iteration:
            if (
                item.rejected_preparation_count != 0
                or item.forced_terminal_rows
                or item.input_private_plan_sha256 != final_plan.private_plan_sha256
                or item.input_plan_evidence_sha256 != final_plan.evidence_sha256
                or item.upstream_forced_terminal_rows != final_plan.forced_terminal_rows
                or item.upstream_forced_terminal_evidence_sha256
                != final_plan.forced_terminal_evidence_sha256
                or item.declared_preparation_count
                != report.final_declared_preparation_count
                or item.provable_preparation_count
                != report.final_declared_preparation_count
                or item.skin_safety_report.checked_job_count != len(final_plan.jobs)
            ):
                raise W3DJobPreparationError(
                    "W3D preparation fixed-point final preflight is not exact"
                )
            continue
        if not item.forced_terminal_rows:
            raise W3DJobPreparationError(
                "W3D preparation fixed-point stopped before its final iteration"
            )
        iteration_source_ids = {row.source_id for row in item.forced_terminal_rows}
        upstream_source_ids = {
            source_id for source_id, _ in (item.upstream_forced_terminal_rows or ())
        }
        if (
            iteration_source_ids & rejected_source_ids
            or iteration_source_ids & upstream_source_ids
        ):
            raise W3DJobPreparationError(
                "W3D preparation fixed-point rejection progress is not strict"
            )
        rejected_source_ids.update(iteration_source_ids)
        accumulated_rejection_count += len(iteration_source_ids)
        merged_mapping, merged_seal = merge_w3d_preparation_forced_terminals(item)
        next_item = report.reports[index + 1]
        if (
            next_item.upstream_forced_terminal_rows
            != tuple(sorted(merged_mapping.items()))
            or next_item.upstream_forced_terminal_evidence_sha256 != merged_seal
        ):
            raise W3DJobPreparationError(
                "W3D preparation fixed-point iteration chain is broken"
            )

    if report.accumulated_rejection_count != accumulated_rejection_count:
        raise W3DJobPreparationError(
            "W3D preparation fixed-point rejection count is stale"
        )
    if _canonical_sha256(report.evidence_hash_basis()) != report.evidence_sha256:
        raise W3DJobPreparationError(
            "W3D preparation fixed-point evidence seal is invalid"
        )


def seal_w3d_job_preparation_fixed_point(
    reports: tuple[W3DJobPreparationPreflightReport, ...],
    final_plan: W3DJobPlan,
) -> W3DJobPreparationFixedPointReport:
    """Seal a monotonic rejection chain ending at the exact final raw plan."""

    if type(reports) is not tuple or not reports:
        raise W3DJobPreparationError(
            "W3D preparation fixed-point iteration evidence is empty"
        )
    final = reports[-1]
    if type(final) is not W3DJobPreparationPreflightReport:
        raise TypeError("fixed-point iterations must be preflight reports")
    provisional = W3DJobPreparationFixedPointReport(
        reports=reports,
        catalog_input_sha256=final.catalog_input_sha256,
        catalog_metadata_sha256=final.catalog_metadata_sha256,
        source_count=final.source_count,
        rejecting_iteration_count=len(reports) - 1,
        accumulated_rejection_count=sum(
            len(item.forced_terminal_rows) for item in reports[:-1]
        ),
        final_declared_preparation_count=final.declared_preparation_count,
        final_private_plan_sha256=final_plan.private_plan_sha256,
        final_plan_evidence_sha256=final_plan.evidence_sha256,
        evidence_sha256="",
    )
    result = replace(
        provisional,
        evidence_sha256=_canonical_sha256(provisional.evidence_hash_basis()),
    )
    _validate_fixed_point_report(result, final_plan)
    return result


def validate_w3d_job_preparation_fixed_point(
    report: W3DJobPreparationFixedPointReport,
    final_plan: W3DJobPlan,
) -> None:
    """Independently validate an immutable fixed-point report and final plan."""

    _validate_fixed_point_report(report, final_plan)


def preflight_w3d_job_preparations(
    plan: W3DJobPlan,
    job_root: Path | str,
    *,
    execute_accounted_jobs: bool,
) -> W3DJobPreparationPreflightReport:
    """Read-only proof preflight for every declared raw model preparation.

    The caller must select the same accounted-job policy intended for the
    later transaction.  Malformed, pre-attested, or filesystem-unbound inputs
    fail closed.  A raw model whose strict proof rejects becomes one canonical
    path-free forced-terminal row while the remaining declarations are still
    attempted.  The job tree is never written.
    """

    if not isinstance(execute_accounted_jobs, bool):
        raise TypeError("W3D accounted-job preparation flag must be a boolean")
    state = _validate_plan(
        plan,
        execute_accounted_jobs=execute_accounted_jobs,
    )
    if state.mode == "attested":
        raise W3DJobPreparationError("W3D preparation preflight requires a raw plan")
    root = _ordinary_root(job_root)
    before = _scan_job_tree(root)
    _validate_filesystem_bindings(plan, state, before)
    skin_safety_report = _job_skin_safety_report(plan, root, before)

    rejected_rows: list[W3DJobPreparationForcedTerminal] = []
    for job_index in state.preparation_indices:
        job = plan.jobs[job_index]
        assert job.hierarchy is not None
        model = _read_bound_payload(
            root,
            job.model,
            _exact_file(before, job.model),
        )
        hierarchy = (
            model
            if job.hierarchy == job.model
            else _read_bound_payload(
                root,
                job.hierarchy,
                _exact_file(before, job.hierarchy),
            )
        )
        try:
            _prove_declared_preparation(job, model, hierarchy)
        except W3DJobPreparationError:
            rejected_rows.append(
                W3DJobPreparationForcedTerminal(
                    source_id=job.model_source_id,
                    source_sha256=job.model_source_sha256,
                )
            )

    if not _snapshot_unchanged(before, _scan_job_tree(root)):
        raise W3DJobPreparationError(
            "W3D job tree changed during preparation preflight"
        )
    return _preflight_report(
        plan,
        len(state.preparation_indices),
        rejected_rows,
        skin_safety_report,
    )


def attest_w3d_job_preparations(
    plan: W3DJobPlan,
    job_root: Path | str,
    *,
    execute_accounted_jobs: bool = False,
) -> W3DJobPlan:
    """Prove and transactionally apply every declared model preparation.

    A returned, already-attested plan is idempotent: the function revalidates
    its plan/evidence seals and prepared job-root bytes, then returns it without
    touching either.  Plans with no declared preparation are likewise fully
    validated and returned unchanged.  By default, terminal-bearing plans are
    rejected.  The explicit ``execute_accounted_jobs`` policy permits only the
    planned jobs from an exact consumed-plus-terminal source inventory.
    """

    if not isinstance(execute_accounted_jobs, bool):
        raise TypeError("W3D accounted-job preparation flag must be a boolean")
    state = _validate_plan(
        plan,
        execute_accounted_jobs=execute_accounted_jobs,
    )
    root = _ordinary_root(job_root)
    before = _scan_job_tree(root)
    _validate_filesystem_bindings(plan, state, before)
    skin_safety_report = _job_skin_safety_report(plan, root, before)
    if skin_safety_report.forced_terminal_rows:
        raise W3DJobPreparationError("W3D job skin-safety proof rejected")
    if state.mode in {"none", "attested"}:
        return plan

    staging: Path | None = None
    backup: Path | None = None
    committed = False
    try:
        staging = _owned_sibling(root, _STAGING_PREFIX)
        replacements = _prepare_replacements(
            plan,
            state,
            root,
            before,
            staging,
        )
        if len(replacements) != len(state.preparation_indices):
            raise W3DJobPreparationError(
                "W3D preparation did not account for every declaration"
            )
        prepared_plan = _reseal_plan(plan, state, replacements)
        if not _snapshot_unchanged(before, _scan_job_tree(root)):
            raise W3DJobPreparationError(
                "W3D job tree changed while preparations were staged"
            )
        backup = _owned_sibling(root, _BACKUP_PREFIX)
        _commit_replacements(
            root,
            staging,
            backup,
            replacements,
            before,
        )
        committed = True
        _remove_owned_tree(staging, root.parent, _STAGING_PREFIX)
        staging = None
        _remove_owned_tree(backup, root.parent, _BACKUP_PREFIX)
        backup = None
        return prepared_plan
    except W3DJobPreparationError:
        if committed and backup is not None and os.path.lexists(backup):
            # Cleanup failures happen after a verified commit. Restore while
            # the complete owned backup still exists rather than returning a
            # partially managed transaction.
            if not _rollback(root, backup, replacements, before):
                raise W3DJobPreparationError(
                    "W3D preparation cleanup and rollback both failed"
                ) from None
        raise
    finally:
        # Failure before commit leaves the job root untouched. These trees are
        # uniquely owned by this invocation and may be removed safely.
        for tree, prefix in (
            (staging, _STAGING_PREFIX),
            (backup, _BACKUP_PREFIX),
        ):
            if tree is not None and os.path.lexists(tree):
                try:
                    _remove_owned_tree(tree, root.parent, prefix)
                except W3DJobPreparationError:
                    pass


__all__ = [
    "MODEL_PREPARATION_PROOF_REJECTED",
    "W3D_JOB_PREPARATION_FORCED_TERMINAL_SCHEMA",
    "W3D_JOB_PREPARATION_FORCED_TERMINAL_VERSION",
    "W3D_JOB_PREPARATION_FIXED_POINT_SCHEMA",
    "W3D_JOB_PREPARATION_FIXED_POINT_VERSION",
    "W3D_JOB_PREPARATION_SCHEMA",
    "W3D_JOB_PREPARATION_PREFLIGHT_SCHEMA",
    "W3D_JOB_PREPARATION_PREFLIGHT_VERSION",
    "W3D_JOB_PREPARATION_VERSION",
    "W3D_JOB_SKIN_SAFETY_SCHEMA",
    "W3D_JOB_SKIN_SAFETY_VERSION",
    "W3DJobPreparationForcedTerminal",
    "W3DJobPreparationError",
    "W3DJobPreparationFixedPointReport",
    "W3DJobPreparationPreflightReport",
    "W3DJobSkinSafetyProof",
    "W3DJobSkinSafetyRejection",
    "W3DJobSkinSafetyReport",
    "attest_w3d_job_preparations",
    "merge_w3d_preparation_forced_terminals",
    "preflight_w3d_job_preparations",
    "seal_w3d_job_preparation_fixed_point",
    "validate_w3d_job_preparation_fixed_point",
]
