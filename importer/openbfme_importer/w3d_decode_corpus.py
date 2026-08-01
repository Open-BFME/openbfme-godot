"""Streaming, payload-free W3D decode-plan corpus evidence.

This module is the full-effective-tree boundary for
:func:`w3d_decode_plan.plan_w3d_stream_decode`.  It validates the established
effective-assets manifest and exact tree.  A first streaming pass gathers
exact, identifier-free hierarchy header/pivot evidence; a second streaming
pass plans each source against the sealed corpus resolver.  A final verified
read rejects mutation during planning.  Identical source bytes are scanned
once and planned once per source SHA-256 plus parent-path context, while every
manifest entry is still opened, hash-verified, and included in all totals.

The report proves decode-plan coverage only.  It deliberately contains no host
paths, W3D payloads, authored identifiers, GLB claims, render claims, or
playback-completeness claims.

The stored report document (``bfme2-w3d-decode-corpus.json``) is written by
:mod:`.w3d_decode_corpus_report`, which stamps measurement provenance and
provides the regeneration CLI::

    PYTHONPATH=importer python -m openbfme_importer.w3d_decode_corpus_report \
        <effective-assets-root> <output-json>
"""

from __future__ import annotations

from collections import Counter, defaultdict
from dataclasses import dataclass
import hashlib
import json
from pathlib import Path

from .w3d_catalog import (
    MAX_W3D_CATALOG_FILES,
    MAX_W3D_CATALOG_TOTAL_BYTES,
)
from .w3d_corpus import (
    W3DCorpusError,
    W3DCorpusLimitError,
    _load_manifest,
    _read_verified_w3d,
    _resolve_root,
    _selected_limit,
    _validate_tree,
)
from .w3d_decode_plan import (
    W3DExternalHierarchyResolver,
    W3DDecodePlanError,
    W3DStreamDecodePlan,
    _W3DExternalHierarchyObservation,
    _W3DExternalHierarchySourceObservations,
    _build_external_hierarchy_resolver,
    _collect_external_hierarchy_observations,
    _virtual_path_context_sha256,
    _virtual_path_lookup_sha256,
    plan_w3d_stream_decode,
)
from .w3d_metadata import W3DMetadataLimitError


W3D_DECODE_CORPUS_SCHEMA = "openbfme.w3d-decode-corpus"
W3D_DECODE_CORPUS_SCHEMA_VERSION = 2

MAX_W3D_DECODE_CORPUS_FILES = MAX_W3D_CATALOG_FILES
MAX_W3D_DECODE_CORPUS_TOTAL_BYTES = MAX_W3D_CATALOG_TOTAL_BYTES


class W3DDecodeCorpusError(ValueError):
    """Raised when a W3D decode corpus cannot be trusted."""


class W3DDecodeCorpusLimitError(W3DDecodeCorpusError):
    """Raised before W3D reads when selected corpus bounds are exceeded."""


def _canonical_sha256(value: object) -> str:
    encoded = json.dumps(
        value,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("ascii")
    return hashlib.sha256(encoded).hexdigest()


@dataclass(frozen=True, slots=True)
class W3DDecodeSourceEvidence:
    """Compact evidence for one source payload and sibling-path context.

    ``manifest_entry_count`` is the number of individually verified manifest
    entries represented by this source hash.  No source path or authored W3D
    identifier is retained.  The context is represented only by the SHA-256 of
    the case-folded virtual parent directory.
    """

    source_sha256: str
    path_context_sha256: str
    byte_length: int
    manifest_entry_count: int
    planner_accepted: bool
    plan_sha256: str | None
    decode_evidence_sha256: str
    stream_complete: bool
    stream_complete_vacuous: bool
    incomplete: bool
    damaged: bool
    unresolved: bool
    unsupported: bool
    primary_bound_animation_stream_count: int
    primary_skipped_animation_stream_count: int

    def neutral(self) -> dict[str, object]:
        status: dict[str, object] = {
            "plannerAccepted": self.planner_accepted,
            "streamComplete": self.stream_complete,
            "incomplete": self.incomplete,
            "damaged": self.damaged,
            "unresolved": self.unresolved,
            "unsupported": self.unsupported,
        }
        if self.stream_complete_vacuous:
            # Complete because there was nothing to stream-decode, not
            # because targets decoded.  Emitted only when true so every
            # non-vacuous source row stays byte-identical.
            status["streamCompleteVacuous"] = True
        result: dict[str, object] = {
            "sourceSha256": self.source_sha256,
            "pathContextSha256": self.path_context_sha256,
            "byteLength": self.byte_length,
            "manifestEntryCount": self.manifest_entry_count,
            "decodeEvidenceSha256": self.decode_evidence_sha256,
            "status": status,
            "primaryImport": {
                "boundAnimationStreamCount": (
                    self.primary_bound_animation_stream_count
                ),
                "skippedAnimationStreamCount": (
                    self.primary_skipped_animation_stream_count
                ),
            },
        }
        if self.plan_sha256 is not None:
            result["planSha256"] = self.plan_sha256
        return result

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DDecodeChunkAggregate:
    """Accounted decoded-stream and terminal evidence for one chunk ID."""

    chunk_id: int
    decoded_stream_count: int
    terminal_count: int
    primary_bound_animation_stream_count: int
    primary_skipped_animation_stream_count: int

    def neutral(self) -> dict[str, object]:
        return {
            "chunkId": self.chunk_id,
            "chunkIdHex": f"0x{self.chunk_id:08X}",
            "decodedStreamCount": self.decoded_stream_count,
            "terminalCount": self.terminal_count,
            "primaryBoundAnimationStreamCount": (
                self.primary_bound_animation_stream_count
            ),
            "primarySkippedAnimationStreamCount": (
                self.primary_skipped_animation_stream_count
            ),
        }

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DDecodeTerminalAggregate:
    """Accounted terminal count by stable state and code."""

    state: str
    code: str
    count: int

    def neutral(self) -> dict[str, object]:
        return {"state": self.state, "code": self.code, "count": self.count}

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class W3DDecodeDomainAggregate:
    """Accounted successfully decoded stream count for one domain."""

    domain: str
    decoded_stream_count: int
    primary_bound_stream_count: int
    primary_skipped_stream_count: int

    def neutral(self) -> dict[str, object]:
        return {
            "domain": self.domain,
            "decodedStreamCount": self.decoded_stream_count,
            "primaryBoundStreamCount": self.primary_bound_stream_count,
            "primarySkippedStreamCount": self.primary_skipped_stream_count,
        }

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class _PlanSummary:
    source_sha256: str
    path_context_sha256: str
    byte_length: int
    planner_accepted: bool
    plan_sha256: str | None
    decode_evidence_sha256: str
    stream_complete: bool
    stream_complete_vacuous: bool
    incomplete: bool
    damaged: bool
    unresolved: bool
    unsupported: bool
    encountered_chunk_count: int
    target_stream_chunk_count: int
    decoded_stream_count: int
    primary_bound_animation_stream_count: int
    primary_skipped_animation_stream_count: int
    terminal_counts: tuple[tuple[str, str, int], ...]
    chunk_counts: tuple[tuple[int, int, int, int, int], ...]
    domain_counts: tuple[tuple[str, int, int, int], ...]

    def source_evidence(self, entry_count: int) -> W3DDecodeSourceEvidence:
        return W3DDecodeSourceEvidence(
            source_sha256=self.source_sha256,
            path_context_sha256=self.path_context_sha256,
            byte_length=self.byte_length,
            manifest_entry_count=entry_count,
            planner_accepted=self.planner_accepted,
            plan_sha256=self.plan_sha256,
            decode_evidence_sha256=self.decode_evidence_sha256,
            stream_complete=self.stream_complete,
            stream_complete_vacuous=self.stream_complete_vacuous,
            incomplete=self.incomplete,
            damaged=self.damaged,
            unresolved=self.unresolved,
            unsupported=self.unsupported,
            primary_bound_animation_stream_count=(
                self.primary_bound_animation_stream_count
            ),
            primary_skipped_animation_stream_count=(
                self.primary_skipped_animation_stream_count
            ),
        )

    def identity(self) -> dict[str, object]:
        result = {
            "sourceSha256": self.source_sha256,
            "pathContextSha256": self.path_context_sha256,
            "byteLength": self.byte_length,
            "plannerAccepted": self.planner_accepted,
            "planSha256": self.plan_sha256,
            "decodeEvidenceSha256": self.decode_evidence_sha256,
            "streamComplete": self.stream_complete,
            "incomplete": self.incomplete,
            "damaged": self.damaged,
            "unresolved": self.unresolved,
            "unsupported": self.unsupported,
            "encounteredChunkCount": self.encountered_chunk_count,
            "targetStreamChunkCount": self.target_stream_chunk_count,
            "decodedStreamCount": self.decoded_stream_count,
            "primaryBoundAnimationStreamCount": (
                self.primary_bound_animation_stream_count
            ),
            "primarySkippedAnimationStreamCount": (
                self.primary_skipped_animation_stream_count
            ),
            "terminalCounts": [
                {"state": state, "code": code, "count": count}
                for state, code, count in self.terminal_counts
            ],
            "chunkCounts": [
                {
                    "chunkId": chunk_id,
                    "decodedStreamCount": decoded,
                    "terminalCount": terminals,
                    "primaryBoundAnimationStreamCount": primary_bound,
                    "primarySkippedAnimationStreamCount": primary_skipped,
                }
                for (
                    chunk_id,
                    decoded,
                    terminals,
                    primary_bound,
                    primary_skipped,
                ) in self.chunk_counts
            ],
            "domainCounts": [
                {
                    "domain": domain,
                    "decodedStreamCount": count,
                    "primaryBoundStreamCount": primary_bound,
                    "primarySkippedStreamCount": primary_skipped,
                }
                for domain, count, primary_bound, primary_skipped in self.domain_counts
            ],
        }
        if self.stream_complete_vacuous:
            result["streamCompleteVacuous"] = True
        return result


@dataclass(frozen=True, slots=True)
class W3DDecodeCorpusReport:
    """Immutable, deterministic, identifier-free decode-corpus evidence."""

    manifest_sha256: str
    manifest_aggregate_sha256: str
    manifest_file_count: int
    manifest_total_bytes: int
    selected_file_count: int
    selected_total_bytes: int
    scanned_file_count: int
    unique_source_count: int
    unique_source_total_bytes: int
    plan_context_count: int
    stream_complete_file_count: int
    vacuously_complete_file_count: int
    incomplete_file_count: int
    damaged_file_count: int
    unresolved_file_count: int
    unsupported_file_count: int
    encountered_chunk_count: int
    target_stream_chunk_count: int
    decoded_stream_count: int
    primary_bound_animation_stream_count: int
    primary_skipped_animation_stream_count: int
    terminal_count: int
    sources: tuple[W3DDecodeSourceEvidence, ...]
    chunks: tuple[W3DDecodeChunkAggregate, ...]
    terminals: tuple[W3DDecodeTerminalAggregate, ...]
    domains: tuple[W3DDecodeDomainAggregate, ...]
    inventory_sha256: str
    hierarchy_resolver_sha256: str
    plan_set_sha256: str
    corpus_sha256: str

    @property
    def complete(self) -> bool:
        return (
            self.selected_file_count > 0
            and self.scanned_file_count == self.selected_file_count
            and self.stream_complete_file_count == self.selected_file_count
            and self.incomplete_file_count == 0
        )

    def neutral(self) -> dict[str, object]:
        """Return JSON-ready evidence without paths, payloads, or identifiers."""

        return {
            "schema": W3D_DECODE_CORPUS_SCHEMA,
            "schemaVersion": W3D_DECODE_CORPUS_SCHEMA_VERSION,
            "summary": {
                "manifestFileCount": self.manifest_file_count,
                "manifestTotalBytes": self.manifest_total_bytes,
                "selectedFileCount": self.selected_file_count,
                "selectedTotalBytes": self.selected_total_bytes,
                "scannedFileCount": self.scanned_file_count,
                "uniqueSourceCount": self.unique_source_count,
                "uniqueSourceTotalBytes": self.unique_source_total_bytes,
                "planContextCount": self.plan_context_count,
                "streamCompleteFileCount": self.stream_complete_file_count,
                "vacuouslyCompleteFileCount": (
                    self.vacuously_complete_file_count
                ),
                "incompleteFileCount": self.incomplete_file_count,
                "damagedFileCount": self.damaged_file_count,
                "unresolvedFileCount": self.unresolved_file_count,
                "unsupportedFileCount": self.unsupported_file_count,
                "encounteredChunkCount": self.encountered_chunk_count,
                "targetStreamChunkCount": self.target_stream_chunk_count,
                "decodedStreamCount": self.decoded_stream_count,
                "primaryBoundAnimationStreamCount": (
                    self.primary_bound_animation_stream_count
                ),
                "primarySkippedAnimationStreamCount": (
                    self.primary_skipped_animation_stream_count
                ),
                "terminalCount": self.terminal_count,
                "complete": self.complete,
            },
            "hashes": {
                "manifestSha256": self.manifest_sha256,
                "manifestAggregateSha256": self.manifest_aggregate_sha256,
                "inventorySha256": self.inventory_sha256,
                "hierarchyResolverSha256": self.hierarchy_resolver_sha256,
                "planSetSha256": self.plan_set_sha256,
                "corpusSha256": self.corpus_sha256,
            },
            "sources": [item.neutral() for item in self.sources],
            "chunks": [item.neutral() for item in self.chunks],
            "terminals": [item.neutral() for item in self.terminals],
            "domains": [item.neutral() for item in self.domains],
        }

    json_ready = neutral


class W3DDecodeCorpusStrictError(W3DDecodeCorpusError):
    """Raised after a complete scan finds non-complete decode plans."""

    def __init__(self, report: W3DDecodeCorpusReport):
        self.report = report
        super().__init__(
            "strict W3D decode corpus rejected "
            f"{report.incomplete_file_count} incomplete file(s): "
            f"{report.damaged_file_count} damaged, "
            f"{report.unresolved_file_count} unresolved, and "
            f"{report.unsupported_file_count} unsupported"
        )


def _plan_summary(
    source: bytes,
    expected_sha256: str,
    virtual_label: str,
    path_context_sha256: str,
    hierarchy_resolver: W3DExternalHierarchyResolver,
) -> _PlanSummary:
    try:
        plan = plan_w3d_stream_decode(
            source,
            virtual_label=virtual_label,
            external_hierarchy_resolver=hierarchy_resolver,
        )
    except W3DMetadataLimitError:
        return _rejected_plan(
            source,
            expected_sha256,
            path_context_sha256,
            "planner-resource-limit",
        )
    except W3DDecodePlanError:
        return _rejected_plan(
            source,
            expected_sha256,
            path_context_sha256,
            "planner-contract-rejected",
        )
    except ValueError:
        return _rejected_plan(
            source,
            expected_sha256,
            path_context_sha256,
            "planner-content-rejected",
        )
    return _accepted_plan(
        source,
        expected_sha256,
        path_context_sha256,
        hierarchy_resolver,
        plan,
    )


def _accepted_plan(
    source: bytes,
    expected_sha256: str,
    path_context_sha256: str,
    hierarchy_resolver: W3DExternalHierarchyResolver,
    plan: W3DStreamDecodePlan,
) -> _PlanSummary:
    if plan.source_sha256 != expected_sha256:
        raise W3DDecodeCorpusError("decode plan source SHA-256 is inconsistent")
    if plan.source_byte_length != len(source):
        raise W3DDecodeCorpusError("decode plan source byte length is inconsistent")
    if plan.external_hierarchy_resolver_sha256 != hierarchy_resolver.resolver_sha256:
        raise W3DDecodeCorpusError(
            "decode plan hierarchy resolver identity is inconsistent"
        )
    if plan.path_context_sha256 != path_context_sha256:
        raise W3DDecodeCorpusError("decode plan path context is inconsistent")

    states = {item.state for item in plan.terminals}
    terminal_counter = Counter((item.state, item.code) for item in plan.terminals)
    decoded_chunk_counter = Counter(item.chunk_id for item in plan.decoded_streams)
    primary_bound_chunk_counter = Counter(
        item.chunk_id for item in plan.primary_bound_animation_streams
    )
    primary_skipped_chunk_counter = Counter(
        item.chunk_id for item in plan.primary_skipped_animation_streams
    )
    terminal_chunk_counter = Counter(
        item.chunk_id for item in plan.terminals if item.chunk_id is not None
    )
    chunk_ids = sorted(set(decoded_chunk_counter) | set(terminal_chunk_counter))
    domain_counter = Counter(item.domain for item in plan.decoded_streams)
    primary_bound_domain_counter = Counter(
        item.domain for item in plan.primary_bound_animation_streams
    )
    primary_skipped_domain_counter = Counter(
        item.domain for item in plan.primary_skipped_animation_streams
    )
    primary_bound_count = len(plan.primary_bound_animation_streams)
    primary_skipped_count = len(plan.primary_skipped_animation_streams)
    core = {
        "sourceSha256": expected_sha256,
        "pathContextSha256": path_context_sha256,
        "byteLength": len(source),
        "plannerAccepted": True,
        "planSha256": plan.plan_sha256,
        "streamComplete": plan.stream_decode_complete,
        "incomplete": not plan.stream_decode_complete,
        **(
            {"streamCompleteVacuous": True}
            if plan.stream_decode_vacuously_complete
            else {}
        ),
        "damaged": "damaged" in states,
        "unresolved": "unresolved" in states,
        "unsupported": "unsupported" in states,
        "encounteredChunkCount": plan.encountered_chunk_count,
        "targetStreamChunkCount": plan.target_stream_chunk_count,
        "decodedStreamCount": len(plan.decoded_streams),
        "primaryBoundAnimationStreamCount": primary_bound_count,
        "primarySkippedAnimationStreamCount": primary_skipped_count,
        "terminalCounts": [
            {"state": state, "code": code, "count": count}
            for (state, code), count in sorted(terminal_counter.items())
        ],
        "chunkCounts": [
            {
                "chunkId": chunk_id,
                "decodedStreamCount": decoded_chunk_counter[chunk_id],
                "terminalCount": terminal_chunk_counter[chunk_id],
                "primaryBoundAnimationStreamCount": (
                    primary_bound_chunk_counter[chunk_id]
                ),
                "primarySkippedAnimationStreamCount": (
                    primary_skipped_chunk_counter[chunk_id]
                ),
            }
            for chunk_id in chunk_ids
        ],
        "domainCounts": [
            {
                "domain": domain,
                "decodedStreamCount": count,
                "primaryBoundStreamCount": primary_bound_domain_counter[domain],
                "primarySkippedStreamCount": primary_skipped_domain_counter[domain],
            }
            for domain, count in sorted(domain_counter.items())
        ],
    }
    return _PlanSummary(
        source_sha256=expected_sha256,
        path_context_sha256=path_context_sha256,
        byte_length=len(source),
        planner_accepted=True,
        plan_sha256=plan.plan_sha256,
        decode_evidence_sha256=_canonical_sha256(core),
        stream_complete=plan.stream_decode_complete,
        stream_complete_vacuous=plan.stream_decode_vacuously_complete,
        incomplete=not plan.stream_decode_complete,
        damaged="damaged" in states,
        unresolved="unresolved" in states,
        unsupported="unsupported" in states,
        encountered_chunk_count=plan.encountered_chunk_count,
        target_stream_chunk_count=plan.target_stream_chunk_count,
        decoded_stream_count=len(plan.decoded_streams),
        primary_bound_animation_stream_count=primary_bound_count,
        primary_skipped_animation_stream_count=primary_skipped_count,
        terminal_counts=tuple(
            (state, code, count)
            for (state, code), count in sorted(terminal_counter.items())
        ),
        chunk_counts=tuple(
            (
                chunk_id,
                decoded_chunk_counter[chunk_id],
                terminal_chunk_counter[chunk_id],
                primary_bound_chunk_counter[chunk_id],
                primary_skipped_chunk_counter[chunk_id],
            )
            for chunk_id in chunk_ids
        ),
        domain_counts=tuple(
            (
                domain,
                count,
                primary_bound_domain_counter[domain],
                primary_skipped_domain_counter[domain],
            )
            for domain, count in sorted(domain_counter.items())
        ),
    )


def _rejected_plan(
    source: bytes,
    expected_sha256: str,
    path_context_sha256: str,
    code: str,
) -> _PlanSummary:
    core = {
        "sourceSha256": expected_sha256,
        "pathContextSha256": path_context_sha256,
        "byteLength": len(source),
        "plannerAccepted": False,
        "planSha256": None,
        "streamComplete": False,
        "incomplete": True,
        "damaged": True,
        "unresolved": False,
        "unsupported": False,
        "encounteredChunkCount": 0,
        "targetStreamChunkCount": 0,
        "decodedStreamCount": 0,
        "primaryBoundAnimationStreamCount": 0,
        "primarySkippedAnimationStreamCount": 0,
        "terminalCounts": [{"state": "damaged", "code": code, "count": 1}],
        "chunkCounts": [],
        "domainCounts": [],
    }
    return _PlanSummary(
        source_sha256=expected_sha256,
        path_context_sha256=path_context_sha256,
        byte_length=len(source),
        planner_accepted=False,
        plan_sha256=None,
        decode_evidence_sha256=_canonical_sha256(core),
        stream_complete=False,
        stream_complete_vacuous=False,
        incomplete=True,
        damaged=True,
        unresolved=False,
        unsupported=False,
        encountered_chunk_count=0,
        target_stream_chunk_count=0,
        decoded_stream_count=0,
        primary_bound_animation_stream_count=0,
        primary_skipped_animation_stream_count=0,
        terminal_counts=(("damaged", code, 1),),
        chunk_counts=(),
        domain_counts=(),
    )


def _inventory_sha256(
    source_sizes: dict[str, int],
    entry_counts: Counter[str],
) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.w3d-decode-corpus-inventory",
            "schemaVersion": 0,
            "sources": [
                {
                    "sourceSha256": source_sha256,
                    "byteLength": source_sizes[source_sha256],
                    "manifestEntryCount": entry_counts[source_sha256],
                }
                for source_sha256 in sorted(source_sizes)
            ],
        }
    )


def _accounted_report(
    *,
    manifest: object,
    selected_file_count: int,
    selected_total_bytes: int,
    scanned_file_count: int,
    plans: dict[tuple[str, str], _PlanSummary],
    context_entry_counts: Counter[tuple[str, str]],
    source_sizes: dict[str, int],
    source_entry_counts: Counter[str],
    hierarchy_resolver: W3DExternalHierarchyResolver,
) -> W3DDecodeCorpusReport:
    ordered_plans = tuple(plans[key] for key in sorted(plans))
    sources = tuple(
        plan.source_evidence(
            context_entry_counts[(plan.source_sha256, plan.path_context_sha256)]
        )
        for plan in ordered_plans
    )
    if sum(item.manifest_entry_count for item in sources) != selected_file_count:
        raise W3DDecodeCorpusError("decode plan context accounting is inconsistent")

    terminal_counter: Counter[tuple[str, str]] = Counter()
    decoded_chunk_counter: Counter[int] = Counter()
    terminal_chunk_counter: Counter[int] = Counter()
    primary_bound_chunk_counter: Counter[int] = Counter()
    primary_skipped_chunk_counter: Counter[int] = Counter()
    domain_counter: Counter[str] = Counter()
    primary_bound_domain_counter: Counter[str] = Counter()
    primary_skipped_domain_counter: Counter[str] = Counter()
    encountered_chunks = 0
    target_streams = 0
    decoded_streams = 0
    primary_bound_animation_streams = 0
    primary_skipped_animation_streams = 0
    terminal_count = 0
    stream_complete_files = 0
    vacuously_complete_files = 0
    incomplete_files = 0
    damaged_files = 0
    unresolved_files = 0
    unsupported_files = 0

    for plan in ordered_plans:
        multiplier = context_entry_counts[
            (plan.source_sha256, plan.path_context_sha256)
        ]
        encountered_chunks += plan.encountered_chunk_count * multiplier
        target_streams += plan.target_stream_chunk_count * multiplier
        decoded_streams += plan.decoded_stream_count * multiplier
        primary_bound_animation_streams += (
            plan.primary_bound_animation_stream_count * multiplier
        )
        primary_skipped_animation_streams += (
            plan.primary_skipped_animation_stream_count * multiplier
        )
        stream_complete_files += int(plan.stream_complete) * multiplier
        vacuously_complete_files += int(plan.stream_complete_vacuous) * multiplier
        incomplete_files += int(plan.incomplete) * multiplier
        damaged_files += int(plan.damaged) * multiplier
        unresolved_files += int(plan.unresolved) * multiplier
        unsupported_files += int(plan.unsupported) * multiplier
        for state, code, count in plan.terminal_counts:
            accounted = count * multiplier
            terminal_counter[(state, code)] += accounted
            terminal_count += accounted
        for (
            chunk_id,
            decoded,
            terminals,
            primary_bound,
            primary_skipped,
        ) in plan.chunk_counts:
            decoded_chunk_counter[chunk_id] += decoded * multiplier
            terminal_chunk_counter[chunk_id] += terminals * multiplier
            primary_bound_chunk_counter[chunk_id] += primary_bound * multiplier
            primary_skipped_chunk_counter[chunk_id] += primary_skipped * multiplier
        for domain, count, primary_bound, primary_skipped in plan.domain_counts:
            domain_counter[domain] += count * multiplier
            primary_bound_domain_counter[domain] += primary_bound * multiplier
            primary_skipped_domain_counter[domain] += primary_skipped * multiplier

    chunks = tuple(
        W3DDecodeChunkAggregate(
            chunk_id=chunk_id,
            decoded_stream_count=decoded_chunk_counter[chunk_id],
            terminal_count=terminal_chunk_counter[chunk_id],
            primary_bound_animation_stream_count=(
                primary_bound_chunk_counter[chunk_id]
            ),
            primary_skipped_animation_stream_count=(
                primary_skipped_chunk_counter[chunk_id]
            ),
        )
        for chunk_id in sorted(set(decoded_chunk_counter) | set(terminal_chunk_counter))
    )
    terminals = tuple(
        W3DDecodeTerminalAggregate(state=state, code=code, count=count)
        for (state, code), count in sorted(terminal_counter.items())
    )
    domains = tuple(
        W3DDecodeDomainAggregate(
            domain=domain,
            decoded_stream_count=count,
            primary_bound_stream_count=primary_bound_domain_counter[domain],
            primary_skipped_stream_count=primary_skipped_domain_counter[domain],
        )
        for domain, count in sorted(domain_counter.items())
    )

    inventory_sha256 = _inventory_sha256(source_sizes, source_entry_counts)
    if inventory_sha256 != hierarchy_resolver.inventory_sha256:
        raise W3DDecodeCorpusError(
            "hierarchy resolver inventory identity is inconsistent"
        )
    plan_set_sha256 = _canonical_sha256(
        {
            "schema": "openbfme.w3d-decode-corpus-plan-set",
            "schemaVersion": 2,
            "plans": [plan.identity() for plan in ordered_plans],
        }
    )
    manifest_sha256 = manifest.raw_sha256
    manifest_aggregate_sha256 = manifest.aggregate_sha256
    identity = {
        "schema": "openbfme.w3d-decode-corpus-identity",
        "schemaVersion": 2,
        "manifestSha256": manifest_sha256,
        "manifestAggregateSha256": manifest_aggregate_sha256,
        "inventorySha256": inventory_sha256,
        "hierarchyResolverSha256": hierarchy_resolver.resolver_sha256,
        "planSetSha256": plan_set_sha256,
        "selectedFileCount": selected_file_count,
        "selectedTotalBytes": selected_total_bytes,
        "scannedFileCount": scanned_file_count,
        "uniqueSourceCount": len(source_sizes),
        "planContextCount": len(ordered_plans),
        "streamCompleteFileCount": stream_complete_files,
        "vacuouslyCompleteFileCount": vacuously_complete_files,
        "incompleteFileCount": incomplete_files,
        "damagedFileCount": damaged_files,
        "unresolvedFileCount": unresolved_files,
        "unsupportedFileCount": unsupported_files,
        "encounteredChunkCount": encountered_chunks,
        "targetStreamChunkCount": target_streams,
        "decodedStreamCount": decoded_streams,
        "primaryBoundAnimationStreamCount": primary_bound_animation_streams,
        "primarySkippedAnimationStreamCount": primary_skipped_animation_streams,
        "terminalCount": terminal_count,
    }
    return W3DDecodeCorpusReport(
        manifest_sha256=manifest_sha256,
        manifest_aggregate_sha256=manifest_aggregate_sha256,
        manifest_file_count=manifest.total_files,
        manifest_total_bytes=manifest.total_bytes,
        selected_file_count=selected_file_count,
        selected_total_bytes=selected_total_bytes,
        scanned_file_count=scanned_file_count,
        unique_source_count=len(source_sizes),
        unique_source_total_bytes=sum(source_sizes.values()),
        plan_context_count=len(ordered_plans),
        stream_complete_file_count=stream_complete_files,
        vacuously_complete_file_count=vacuously_complete_files,
        incomplete_file_count=incomplete_files,
        damaged_file_count=damaged_files,
        unresolved_file_count=unresolved_files,
        unsupported_file_count=unsupported_files,
        encountered_chunk_count=encountered_chunks,
        target_stream_chunk_count=target_streams,
        decoded_stream_count=decoded_streams,
        primary_bound_animation_stream_count=primary_bound_animation_streams,
        primary_skipped_animation_stream_count=primary_skipped_animation_streams,
        terminal_count=terminal_count,
        sources=sources,
        chunks=chunks,
        terminals=terminals,
        domains=domains,
        inventory_sha256=inventory_sha256,
        hierarchy_resolver_sha256=hierarchy_resolver.resolver_sha256,
        plan_set_sha256=plan_set_sha256,
        corpus_sha256=_canonical_sha256(identity),
    )


def _scan(
    effective_assets_root: Path | str,
    *,
    strict: bool,
    max_files: int | None,
    max_total_bytes: int | None,
) -> W3DDecodeCorpusReport:
    if not isinstance(strict, bool):
        raise TypeError("W3D decode corpus strict flag must be a boolean")
    selected_max_files = _selected_limit(
        max_files,
        MAX_W3D_DECODE_CORPUS_FILES,
        "decode file count",
    )
    selected_max_bytes = _selected_limit(
        max_total_bytes,
        MAX_W3D_DECODE_CORPUS_TOTAL_BYTES,
        "decode total byte",
    )

    root = _resolve_root(effective_assets_root)
    manifest_path, manifest = _load_manifest(root)
    actual_files = _validate_tree(root, manifest)
    selected = tuple(item for item in manifest.files if item.is_w3d)
    if not selected:
        raise W3DDecodeCorpusError("effective-assets manifest declares no W3D files")
    selected_total_bytes = sum(item.size for item in selected)
    if len(selected) > selected_max_files:
        raise W3DDecodeCorpusLimitError(
            f"W3D decode corpus file count exceeds {selected_max_files}"
        )
    if selected_total_bytes > selected_max_bytes:
        raise W3DDecodeCorpusLimitError(
            f"W3D decode corpus total bytes exceed {selected_max_bytes}"
        )

    source_sizes: dict[str, int] = {}
    source_entry_counts: Counter[str] = Counter()
    evidence_context_counts: Counter[tuple[str, str]] = Counter()
    path_lookups_by_source: dict[str, list[str]] = defaultdict(list)
    observations_by_source: dict[str, tuple[_W3DExternalHierarchyObservation, ...]] = {}
    collection_failures_by_source: dict[str, str | None] = {}
    for item in selected:
        source = _read_verified_w3d(actual_files[item.path.casefold()], item)
        path_context_sha256 = _virtual_path_context_sha256(item.path)
        source_entry_counts[item.sha256] += 1
        evidence_context_counts[(item.sha256, path_context_sha256)] += 1
        path_lookups_by_source[item.sha256].append(
            _virtual_path_lookup_sha256(item.path)
        )
        previous_size = source_sizes.get(item.sha256)
        if previous_size is not None and previous_size != len(source):
            raise W3DDecodeCorpusError(
                "W3D source identity has inconsistent byte lengths"
            )
        if previous_size is None:
            source_sizes[item.sha256] = len(source)
            try:
                observations_by_source[item.sha256] = (
                    _collect_external_hierarchy_observations(source)
                )
                collection_failures_by_source[item.sha256] = None
            except ValueError:
                # The second pass retains the planner's established rejected-
                # source evidence.  A source that cannot be scanned cannot
                # contribute trusted external hierarchy cardinality.
                observations_by_source[item.sha256] = ()
                collection_failures_by_source[item.sha256] = (
                    "hierarchy-evidence-scan-failed"
                )
        del source

    middle_manifest_path, middle_manifest = _load_manifest(root)
    if (
        middle_manifest_path != manifest_path
        or middle_manifest.raw_sha256 != manifest.raw_sha256
        or middle_manifest != manifest
    ):
        raise W3DDecodeCorpusError(
            "effective-assets manifest changed during W3D decode scan"
        )
    middle_actual_files = _validate_tree(root, middle_manifest)

    inventory_sha256 = _inventory_sha256(source_sizes, source_entry_counts)
    source_observations = tuple(
        _W3DExternalHierarchySourceObservations(
            source_sha256=source_sha256,
            manifest_entry_count=source_entry_counts[source_sha256],
            observations=observations_by_source[source_sha256],
            sibling_path_lookup_sha256s=tuple(
                sorted(path_lookups_by_source[source_sha256])
            ),
            collection_failure_code=collection_failures_by_source[source_sha256],
        )
        for source_sha256 in sorted(source_sizes)
    )
    try:
        hierarchy_resolver = _build_external_hierarchy_resolver(
            manifest_sha256=manifest.raw_sha256,
            manifest_aggregate_sha256=manifest.aggregate_sha256,
            inventory_sha256=inventory_sha256,
            sources=source_observations,
        )
    except W3DDecodePlanError as exc:
        raise W3DDecodeCorpusError(
            "external hierarchy resolver evidence is invalid"
        ) from exc

    plans: dict[tuple[str, str], _PlanSummary] = {}
    planning_context_counts: Counter[tuple[str, str]] = Counter()
    scanned_file_count = 0
    for item in selected:
        source = _read_verified_w3d(
            middle_actual_files[item.path.casefold()],
            item,
        )
        scanned_file_count += 1
        path_context_sha256 = _virtual_path_context_sha256(item.path)
        plan_key = (item.sha256, path_context_sha256)
        planning_context_counts[plan_key] += 1
        if plan_key not in plans:
            plans[plan_key] = _plan_summary(
                source,
                item.sha256,
                item.path,
                path_context_sha256,
                hierarchy_resolver,
            )
        del source
    if planning_context_counts != evidence_context_counts:
        raise W3DDecodeCorpusError(
            "W3D hierarchy and planning passes selected different inventories"
        )

    final_manifest_path, final_manifest = _load_manifest(root)
    if (
        final_manifest_path != manifest_path
        or final_manifest.raw_sha256 != manifest.raw_sha256
        or final_manifest != manifest
    ):
        raise W3DDecodeCorpusError(
            "effective-assets manifest changed during W3D decode scan"
        )
    final_actual_files = _validate_tree(root, final_manifest)
    for item in selected:
        verified_source = _read_verified_w3d(
            final_actual_files[item.path.casefold()],
            item,
        )
        del verified_source

    report = _accounted_report(
        manifest=manifest,
        selected_file_count=len(selected),
        selected_total_bytes=selected_total_bytes,
        scanned_file_count=scanned_file_count,
        plans=plans,
        context_entry_counts=evidence_context_counts,
        source_sizes=source_sizes,
        source_entry_counts=source_entry_counts,
        hierarchy_resolver=hierarchy_resolver,
    )
    if strict and not report.complete:
        raise W3DDecodeCorpusStrictError(report)
    return report


def scan_w3d_decode_corpus(
    effective_assets_root: Path | str,
    *,
    strict: bool = False,
    max_files: int | None = None,
    max_total_bytes: int | None = None,
) -> W3DDecodeCorpusReport:
    """Verify and plan every manifest-declared W3D without corpus buffering.

    Bounds are fail-closed caps and never truncate the selection.  Strict mode
    performs the complete scan, builds the immutable report, and only then
    raises :class:`W3DDecodeCorpusStrictError` when any selected entry lacks a
    complete stream-decode plan.  Manifest and exact-tree identities are
    reverified between evidence and planning passes and after planning.
    """

    try:
        return _scan(
            effective_assets_root,
            strict=strict,
            max_files=max_files,
            max_total_bytes=max_total_bytes,
        )
    except W3DDecodeCorpusError:
        raise
    except W3DCorpusLimitError as exc:
        raise W3DDecodeCorpusLimitError(str(exc)) from exc
    except W3DCorpusError as exc:
        raise W3DDecodeCorpusError(str(exc)) from exc


__all__ = [
    "MAX_W3D_DECODE_CORPUS_FILES",
    "MAX_W3D_DECODE_CORPUS_TOTAL_BYTES",
    "W3D_DECODE_CORPUS_SCHEMA",
    "W3D_DECODE_CORPUS_SCHEMA_VERSION",
    "W3DDecodeChunkAggregate",
    "W3DDecodeCorpusError",
    "W3DDecodeCorpusLimitError",
    "W3DDecodeCorpusReport",
    "W3DDecodeCorpusStrictError",
    "W3DDecodeDomainAggregate",
    "W3DDecodeSourceEvidence",
    "W3DDecodeTerminalAggregate",
    "scan_w3d_decode_corpus",
]
