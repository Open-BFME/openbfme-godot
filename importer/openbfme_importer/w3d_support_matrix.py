"""Payload-free W3D conversion capability accounting.

The W3D metadata/catalog scanners prove that chunks were boundedly identified
and, for a subset of records, that useful header metadata was decoded.  That is
not evidence that a runtime converter decoded the corresponding render or
animation payload.  This module preserves that distinction.

Every encountered chunk ID is assigned exactly one terminal capability state:

``decoded-and-backtested``
    An explicit attestation is bound to this catalog evidence, exact chunk ID,
    and exact aggregate occurrence count.
``metadata-only``
    The current scanners recognize the chunk, but no conversion backtest was
    supplied.
``explicit-unsupported``
    Current metadata evidence explicitly reports the chunk or property type as
    unsupported.
``unknown``
    The scanner could not classify the chunk ID.
``source-invalid``
    Structural damage or malformed metadata makes conversion evidence unsafe.

The report intentionally contains only aggregate chunk/diagnostic counts and
hashes.  It never copies source bytes, virtual paths, or authored asset IDs.
``W3DCatalogReport``, ``W3DCorpusReport``, and their JSON-ready exported forms
are accepted; compact corpus exports remain conservative when a diagnostic no
longer carries a per-chunk ID.
"""

from __future__ import annotations

from collections import Counter, defaultdict
from collections.abc import Iterable, Mapping
from dataclasses import dataclass, replace
import hashlib
import json


W3D_SUPPORT_MATRIX_SCHEMA = "openbfme.w3d-support-matrix"
W3D_SUPPORT_MATRIX_SCHEMA_VERSION = 0

CAPABILITY_STATES = (
    "decoded-and-backtested",
    "metadata-only",
    "explicit-unsupported",
    "unknown",
    "source-invalid",
)
CAPABILITY_DOMAINS = (
    "geometry",
    "material",
    "hierarchy",
    "animation",
    "particles",
    "metadata",
    "damage",
)

_SCANNER_CLASSIFICATIONS = frozenset(
    {"container", "known-data", "metadata", "unsupported", "unknown"}
)
_RELEVANT_DOMAINS = frozenset(
    {"geometry", "material", "hierarchy", "animation", "particles", "damage"}
)
_SCAN_FAILURE_CODES = frozenset({"metadata-limit", "invalid-virtual-path"})


class W3DSupportMatrixError(ValueError):
    """Raised when aggregate evidence or an attestation is not trustworthy."""


@dataclass(frozen=True, slots=True)
class W3DBacktestAttestation:
    """Exact proof that every occurrence of one chunk ID was converted.

    ``source_evidence_sha256`` must equal the support report's source evidence
    hash.  ``chunk_count`` must equal the complete aggregate count for the ID.
    The separate evidence hash binds the claim to an external, payload-free
    converter/backtest report.  A failed or partial run is never promotable.
    """

    chunk_id: int
    chunk_count: int
    source_evidence_sha256: str
    backtest_evidence_sha256: str
    decoded: bool = True
    backtested: bool = True

    def neutral(self) -> dict[str, object]:
        return {
            "chunkId": self.chunk_id,
            "chunkIdHex": f"0x{self.chunk_id:08X}",
            "chunkCount": self.chunk_count,
            "sourceEvidenceSha256": self.source_evidence_sha256,
            "backtestEvidenceSha256": self.backtest_evidence_sha256,
            "decoded": self.decoded,
            "backtested": self.backtested,
        }


@dataclass(frozen=True, slots=True)
class W3DSupportEntry:
    """Terminal capability state for one aggregate W3D chunk ID."""

    chunk_id: int
    chunk_name: str | None
    chunk_count: int
    scanner_classifications: tuple[str, ...]
    domain: str
    conversion_relevant: bool
    capability_state: str
    backtest_evidence_sha256: str | None = None

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "chunkId": self.chunk_id,
            "chunkIdHex": f"0x{self.chunk_id:08X}",
            "chunkCount": self.chunk_count,
            "scannerClassifications": list(self.scanner_classifications),
            "domain": self.domain,
            "conversionRelevant": self.conversion_relevant,
            "capabilityState": self.capability_state,
        }
        if self.chunk_name is not None:
            result["chunkName"] = self.chunk_name
        if self.backtest_evidence_sha256 is not None:
            result["backtestEvidenceSha256"] = self.backtest_evidence_sha256
        return result


@dataclass(frozen=True, slots=True)
class W3DDomainSupport:
    """Deterministic aggregate capability counts for one domain."""

    domain: str
    chunk_id_count: int
    chunk_count: int
    state_chunk_id_counts: tuple[tuple[str, int], ...]
    state_chunk_counts: tuple[tuple[str, int], ...]

    def neutral(self) -> dict[str, object]:
        return {
            "domain": self.domain,
            "chunkIdCount": self.chunk_id_count,
            "chunkCount": self.chunk_count,
            "stateChunkIdCounts": dict(self.state_chunk_id_counts),
            "stateChunkCounts": dict(self.state_chunk_counts),
        }


@dataclass(frozen=True, slots=True)
class W3DSupportDiagnostics:
    """Aggregate warnings, source failures, and duplicate authored IDs."""

    warning_code_counts: tuple[tuple[str, int], ...]
    source_failure_phase_counts: tuple[tuple[str, int], ...]
    source_failure_code_counts: tuple[tuple[str, int], ...]
    header_index_failure_code_counts: tuple[tuple[str, int], ...]
    duplicate_logical_id_kind_counts: tuple[tuple[str, int], ...]
    source_invalid_warning_count: int
    unbound_source_invalid_warning_count: int

    @property
    def source_failure_count(self) -> int:
        return sum(value for _, value in self.source_failure_code_counts)

    @property
    def header_index_failure_count(self) -> int:
        return sum(value for _, value in self.header_index_failure_code_counts)

    @property
    def duplicate_logical_id_count(self) -> int:
        return sum(value for _, value in self.duplicate_logical_id_kind_counts)

    def neutral(self) -> dict[str, object]:
        return {
            "warningCodeCounts": dict(self.warning_code_counts),
            "sourceInvalidWarningCount": self.source_invalid_warning_count,
            "unboundSourceInvalidWarningCount": (
                self.unbound_source_invalid_warning_count
            ),
            "sourceFailures": {
                "count": self.source_failure_count,
                "phaseCounts": dict(self.source_failure_phase_counts),
                "codeCounts": dict(self.source_failure_code_counts),
            },
            "headerIndexFailures": {
                "count": self.header_index_failure_count,
                "codeCounts": dict(self.header_index_failure_code_counts),
            },
            "duplicateLogicalIds": {
                "count": self.duplicate_logical_id_count,
                "kindCounts": dict(self.duplicate_logical_id_kind_counts),
            },
        }


@dataclass(frozen=True, slots=True)
class W3DSupportMatrixReport:
    """Immutable, JSON-ready aggregate W3D capability evidence."""

    entries: tuple[W3DSupportEntry, ...]
    domains: tuple[W3DDomainSupport, ...]
    diagnostics: W3DSupportDiagnostics
    attestations: tuple[W3DBacktestAttestation, ...]
    source_evidence_sha256: str
    attestation_set_sha256: str
    conversion_complete: bool
    matrix_sha256: str

    @property
    def encountered_chunk_count(self) -> int:
        return sum(item.chunk_count for item in self.entries)

    def entry_for(self, chunk_id: int) -> W3DSupportEntry:
        """Return one entry, failing explicitly when the ID was not encountered."""

        for entry in self.entries:
            if entry.chunk_id == chunk_id:
                return entry
        raise KeyError(chunk_id)

    def matrix_hash_basis(self) -> dict[str, object]:
        """Return the canonical JSON value hashed as ``matrix_sha256``."""

        return self._neutral(include_matrix_hash=False)

    def neutral(self) -> dict[str, object]:
        return self._neutral(include_matrix_hash=True)

    def _neutral(self, *, include_matrix_hash: bool) -> dict[str, object]:
        hashes = {
            "sourceEvidenceSha256": self.source_evidence_sha256,
            "attestationSetSha256": self.attestation_set_sha256,
        }
        if include_matrix_hash:
            hashes["matrixSha256"] = self.matrix_sha256
        state_id_counts = Counter(item.capability_state for item in self.entries)
        state_counts = Counter()
        for item in self.entries:
            state_counts[item.capability_state] += item.chunk_count
        return {
            "schema": W3D_SUPPORT_MATRIX_SCHEMA,
            "schemaVersion": W3D_SUPPORT_MATRIX_SCHEMA_VERSION,
            "summary": {
                "encounteredChunkIdCount": len(self.entries),
                "encounteredChunkCount": self.encountered_chunk_count,
                "attestedChunkIdCount": len(self.attestations),
                "conversionComplete": self.conversion_complete,
                "stateChunkIdCounts": {
                    state: state_id_counts.get(state, 0) for state in CAPABILITY_STATES
                },
                "stateChunkCounts": {
                    state: state_counts.get(state, 0) for state in CAPABILITY_STATES
                },
            },
            "hashes": hashes,
            "entries": [item.neutral() for item in self.entries],
            "domains": [item.neutral() for item in self.domains],
            "diagnostics": self.diagnostics.neutral(),
            "attestations": [item.neutral() for item in self.attestations],
        }

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class _ChunkRow:
    chunk_id: int
    chunk_name: str | None
    classification: str
    count: int

    def neutral(self) -> dict[str, object]:
        result: dict[str, object] = {
            "chunkId": self.chunk_id,
            "classification": self.classification,
            "count": self.count,
        }
        if self.chunk_name is not None:
            result["chunkName"] = self.chunk_name
        return result


@dataclass(frozen=True, slots=True)
class _Warning:
    code: str
    chunk_id: int | None
    count: int


@dataclass(frozen=True, slots=True)
class _Failure:
    phase: str
    code: str
    count: int


@dataclass(frozen=True, slots=True)
class _Evidence:
    rows: tuple[_ChunkRow, ...]
    warnings: tuple[_Warning, ...]
    failures: tuple[_Failure, ...]
    duplicate_kinds: tuple[tuple[str, int], ...]
    source_evidence_sha256: str


# Domain membership is deliberately based on stable W3D chunk IDs, not on a
# scanner's broad ``known-data`` label.  Unknown IDs fall into ``metadata`` for
# display but remain capability state ``unknown`` and therefore block closure.
_GEOMETRY_IDS = frozenset(
    {
        0x00000000,
        0x00000002,
        0x00000003,
        0x0000001F,
        0x00000020,
        0x00000060,
        0x00000061,
        0x00000440,
        0x00000C00,
        0x00000C01,
    }
)
_MATERIAL_IDS = frozenset(
    {
        0x00000022,
        0x00000023,
        0x00000024,
        0x00000025,
        0x00000026,
        0x00000028,
        0x00000029,
        0x0000002A,
        0x0000002B,
        0x0000002C,
        0x0000002D,
        0x0000002E,
        0x0000002F,
        0x00000030,
        0x00000031,
        0x00000032,
        0x00000033,
        0x00000038,
        0x00000039,
        0x0000003A,
        0x0000003B,
        0x0000003C,
        0x0000003E,
        0x0000003F,
        0x00000048,
        0x00000049,
        0x0000004A,
        0x0000004B,
        0x00000050,
        0x00000051,
        0x00000052,
        0x00000053,
        0x00000080,
    }
)
_HIERARCHY_IDS = frozenset(
    {
        0x0000000E,
        0x00000100,
        0x00000101,
        0x00000102,
        0x00000103,
        0x00000300,
        0x00000400,
        0x00000420,
        0x00000600,
        0x00000700,
        0x00000701,
        0x00000702,
        0x00000703,
        0x00000704,
        0x00000705,
        0x00000706,
        0x00000750,
    }
)
_ANIMATION_IDS = frozenset(
    {
        0x00000058,
        0x00000200,
        0x00000201,
        0x00000202,
        0x00000203,
        0x00000280,
        0x00000281,
        0x00000282,
        0x00000283,
        0x00000284,
        0x000002C0,
    }
)
_PARTICLE_IDS = frozenset(
    {
        0x00000460,
        0x00000500,
        0x00000800,
        0x00000900,
        0x00000901,
        0x00000902,
        0x00000A00,
    }
)
_DAMAGE_IDS = frozenset(
    {
        0x00000090,
        0x00000091,
        0x00000092,
        0x00000093,
        0x00000740,
    }
)


def _canonical_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _is_uint32(value: object) -> bool:
    return (
        not isinstance(value, bool)
        and isinstance(value, int)
        and 0 <= value <= 0xFFFFFFFF
    )


def _is_positive_int(value: object) -> bool:
    return not isinstance(value, bool) and isinstance(value, int) and value >= 1


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and value == value.casefold()
        and all(character in "0123456789abcdef" for character in value)
    )


def _mapping_value(
    value: Mapping[str, object], *keys: str, required: bool = True
) -> object:
    selected = [value[key] for key in keys if key in value]
    if not selected:
        if required:
            raise W3DSupportMatrixError(f"missing required field {keys[0]!r}")
        return None
    if any(item != selected[0] for item in selected[1:]):
        raise W3DSupportMatrixError(f"conflicting aliases for field {keys[0]!r}")
    return selected[0]


def _catalog_value(report_or_catalog: object) -> object:
    if isinstance(report_or_catalog, Mapping):
        schema = report_or_catalog.get("schema")
        if schema == "openbfme.w3d-support-matrix":
            raise W3DSupportMatrixError("a support matrix is not catalog evidence")
        if schema == "openbfme.w3d-corpus" and "catalog" in report_or_catalog:
            return report_or_catalog["catalog"]
        return report_or_catalog
    catalog = getattr(report_or_catalog, "catalog", None)
    if catalog is not None:
        return catalog
    return report_or_catalog


def _row_from_object(value: object) -> _ChunkRow:
    if isinstance(value, Mapping):
        chunk_id = _mapping_value(value, "chunkId", "chunk_id")
        chunk_name = _mapping_value(value, "chunkName", "chunk_name", required=False)
        classification = _mapping_value(value, "classification")
        count = _mapping_value(value, "count", "chunkCount", "chunk_count")
    else:
        try:
            chunk_id = getattr(value, "chunk_id")
            chunk_name = getattr(value, "chunk_name")
            classification = getattr(value, "classification")
            count = getattr(value, "count")
        except AttributeError as exc:
            raise W3DSupportMatrixError("invalid W3D chunk-count row") from exc
    if not _is_uint32(chunk_id):
        raise W3DSupportMatrixError("chunk ID must be an unsigned 32-bit integer")
    if chunk_name is not None and not isinstance(chunk_name, str):
        raise W3DSupportMatrixError("chunk name must be a string or null")
    if classification not in _SCANNER_CLASSIFICATIONS:
        raise W3DSupportMatrixError(
            f"unsupported scanner classification {classification!r}"
        )
    if not _is_positive_int(count):
        raise W3DSupportMatrixError("chunk count must be a positive integer")
    return _ChunkRow(chunk_id, chunk_name, classification, count)


def _chunk_rows(catalog: object) -> tuple[_ChunkRow, ...]:
    if isinstance(catalog, Mapping):
        raw_rows = catalog.get("chunkCounts")
    else:
        raw_rows = getattr(catalog, "chunk_counts", None)
    if not isinstance(raw_rows, (list, tuple)):
        raise W3DSupportMatrixError("catalog evidence lacks chunkCounts")
    rows = tuple(_row_from_object(item) for item in raw_rows)
    return tuple(
        sorted(
            rows,
            key=lambda item: (
                item.chunk_id,
                item.chunk_name or "",
                item.classification,
                item.count,
            ),
        )
    )


def _warning_from_mapping(value: Mapping[str, object]) -> _Warning:
    diagnostic = value.get("diagnostic", value)
    if not isinstance(diagnostic, Mapping):
        raise W3DSupportMatrixError("invalid W3D warning diagnostic")
    code = _mapping_value(diagnostic, "code")
    chunk_id = _mapping_value(diagnostic, "chunkId", "chunk_id", required=False)
    if not isinstance(code, str) or not code:
        raise W3DSupportMatrixError("warning code must be a non-empty string")
    if chunk_id is not None and not _is_uint32(chunk_id):
        raise W3DSupportMatrixError("warning chunk ID must be unsigned 32-bit")
    return _Warning(code, chunk_id, 1)


def _warning_from_object(value: object) -> _Warning:
    diagnostic = getattr(value, "diagnostic", value)
    try:
        code = diagnostic.code
        chunk_id = diagnostic.chunk_id
    except AttributeError as exc:
        raise W3DSupportMatrixError("invalid W3D warning diagnostic") from exc
    if not isinstance(code, str) or not code:
        raise W3DSupportMatrixError("warning code must be a non-empty string")
    if chunk_id is not None and not _is_uint32(chunk_id):
        raise W3DSupportMatrixError("warning chunk ID must be unsigned 32-bit")
    return _Warning(code, chunk_id, 1)


def _count_mapping(value: object, label: str) -> Counter[str]:
    if value is None:
        return Counter()
    if not isinstance(value, Mapping):
        raise W3DSupportMatrixError(f"{label} must be an object")
    result: Counter[str] = Counter()
    for key, count in value.items():
        if not isinstance(key, str) or not key:
            raise W3DSupportMatrixError(f"{label} keys must be non-empty strings")
        if not _is_positive_int(count):
            raise W3DSupportMatrixError(f"{label} counts must be positive integers")
        result[key] += count
    return result


def _warnings(catalog: object) -> tuple[_Warning, ...]:
    if isinstance(catalog, Mapping):
        raw = catalog.get("warnings")
        if raw is not None:
            if not isinstance(raw, (list, tuple)):
                raise W3DSupportMatrixError("catalog warnings must be an array")
            return tuple(_warning_from_mapping(item) for item in raw)
        diagnostics = catalog.get("diagnostics", {})
        if not isinstance(diagnostics, Mapping):
            raise W3DSupportMatrixError("corpus diagnostics must be an object")
        counts = _count_mapping(diagnostics.get("warningCodes"), "warningCodes")
        return tuple(
            _Warning(code, None, count) for code, count in sorted(counts.items())
        )
    raw = getattr(catalog, "warnings", None)
    if raw is None:
        raise W3DSupportMatrixError("catalog evidence lacks warnings")
    return tuple(_warning_from_object(item) for item in raw)


def _failure_from_mapping(value: Mapping[str, object]) -> _Failure:
    phase = _mapping_value(value, "phase")
    code = _mapping_value(value, "code")
    if not isinstance(phase, str) or not phase:
        raise W3DSupportMatrixError("failure phase must be a non-empty string")
    if not isinstance(code, str) or not code:
        raise W3DSupportMatrixError("failure code must be a non-empty string")
    return _Failure(phase, code, 1)


def _failure_from_object(value: object) -> _Failure:
    try:
        phase = value.phase
        code = value.code
    except AttributeError as exc:
        raise W3DSupportMatrixError("invalid W3D catalog failure") from exc
    if not isinstance(phase, str) or not phase:
        raise W3DSupportMatrixError("failure phase must be a non-empty string")
    if not isinstance(code, str) or not code:
        raise W3DSupportMatrixError("failure code must be a non-empty string")
    return _Failure(phase, code, 1)


def _failures(catalog: object) -> tuple[_Failure, ...]:
    if isinstance(catalog, Mapping):
        raw = catalog.get("failures")
        if raw is not None:
            if not isinstance(raw, (list, tuple)):
                raise W3DSupportMatrixError("catalog failures must be an array")
            return tuple(_failure_from_mapping(item) for item in raw)
        diagnostics = catalog.get("diagnostics", {})
        if not isinstance(diagnostics, Mapping):
            raise W3DSupportMatrixError("corpus diagnostics must be an object")
        counts = _count_mapping(diagnostics.get("failureCodes"), "failureCodes")
        result = []
        for code, count in sorted(counts.items()):
            phase = "scan" if code in _SCAN_FAILURE_CODES else "index"
            result.append(_Failure(phase, code, count))
        return tuple(result)
    raw = getattr(catalog, "failures", None)
    if raw is None:
        raise W3DSupportMatrixError("catalog evidence lacks failures")
    return tuple(_failure_from_object(item) for item in raw)


def _duplicate_kinds(catalog: object) -> tuple[tuple[str, int], ...]:
    if isinstance(catalog, Mapping):
        raw = catalog.get("duplicateLogicalIds")
        if raw is not None:
            if not isinstance(raw, (list, tuple)):
                raise W3DSupportMatrixError(
                    "catalog duplicateLogicalIds must be an array"
                )
            result: Counter[str] = Counter()
            for duplicate in raw:
                if not isinstance(duplicate, Mapping):
                    raise W3DSupportMatrixError("invalid duplicate logical ID")
                kind = _mapping_value(duplicate, "kind")
                if not isinstance(kind, str) or not kind:
                    raise W3DSupportMatrixError(
                        "duplicate logical ID kind must be non-empty"
                    )
                result[kind] += 1
            return tuple(sorted(result.items()))
        diagnostics = catalog.get("diagnostics", {})
        if not isinstance(diagnostics, Mapping):
            raise W3DSupportMatrixError("corpus diagnostics must be an object")
        counts = _count_mapping(
            diagnostics.get("duplicateLogicalIdKinds"),
            "duplicateLogicalIdKinds",
        )
        return tuple(sorted(counts.items()))
    raw = getattr(catalog, "duplicate_logical_ids", None)
    if raw is None:
        raise W3DSupportMatrixError("catalog evidence lacks duplicate logical IDs")
    result = Counter()
    for duplicate in raw:
        kind = getattr(duplicate, "kind", None)
        if not isinstance(kind, str) or not kind:
            raise W3DSupportMatrixError("invalid duplicate logical ID kind")
        result[kind] += 1
    return tuple(sorted(result.items()))


def _provided_source_hash(catalog: object) -> str | None:
    if isinstance(catalog, Mapping):
        hashes = catalog.get("hashes", {})
        if not isinstance(hashes, Mapping):
            raise W3DSupportMatrixError("catalog hashes must be an object")
        value = hashes.get("metadataSha256")
        if value is None:
            value = hashes.get("catalogMetadataSha256")
    else:
        value = getattr(catalog, "metadata_sha256", None)
    if value is None:
        return None
    if not _is_sha256(value):
        raise W3DSupportMatrixError("catalog metadata SHA-256 is invalid")
    return value


def _evidence(report_or_catalog: object) -> _Evidence:
    catalog = _catalog_value(report_or_catalog)
    rows = _chunk_rows(catalog)
    warnings = _warnings(catalog)
    failures = _failures(catalog)
    duplicates = _duplicate_kinds(catalog)
    source_hash = _provided_source_hash(catalog)
    if source_hash is None:
        source_hash = _canonical_sha256(
            {
                "schema": "openbfme.w3d-support-source-evidence",
                "schemaVersion": 0,
                "chunkCounts": [item.neutral() for item in rows],
                "warnings": [
                    {"code": item.code, "chunkId": item.chunk_id, "count": item.count}
                    for item in sorted(
                        warnings,
                        key=lambda item: (item.code, item.chunk_id or -1, item.count),
                    )
                ],
                "failures": [
                    {"phase": item.phase, "code": item.code, "count": item.count}
                    for item in sorted(
                        failures, key=lambda item: (item.phase, item.code, item.count)
                    )
                ],
                "duplicateLogicalIdKinds": dict(duplicates),
            }
        )
    return _Evidence(rows, warnings, failures, duplicates, source_hash)


def _domain(chunk_id: int) -> str:
    if chunk_id in _GEOMETRY_IDS:
        return "geometry"
    if chunk_id in _MATERIAL_IDS:
        return "material"
    if chunk_id in _HIERARCHY_IDS:
        return "hierarchy"
    if chunk_id in _ANIMATION_IDS:
        return "animation"
    if chunk_id in _PARTICLE_IDS:
        return "particles"
    if chunk_id in _DAMAGE_IDS:
        return "damage"
    return "metadata"


def _is_unsupported_warning(code: str) -> bool:
    return code.startswith("unsupported-")


def _base_entries(
    evidence: _Evidence,
) -> tuple[tuple[W3DSupportEntry, ...], W3DSupportDiagnostics]:
    grouped: dict[int, list[_ChunkRow]] = defaultdict(list)
    for row in evidence.rows:
        grouped[row.chunk_id].append(row)

    warning_counts: Counter[str] = Counter()
    invalid_ids: set[int] = set()
    unsupported_ids: set[int] = set()
    unknown_ids: set[int] = set()
    source_invalid_warning_count = 0
    unbound_invalid_warning_count = 0
    encountered_ids = set(grouped)
    for warning in evidence.warnings:
        warning_counts[warning.code] += warning.count
        if warning.code == "unknown-chunk":
            if warning.chunk_id is not None:
                unknown_ids.add(warning.chunk_id)
            continue
        if _is_unsupported_warning(warning.code):
            if warning.chunk_id is not None:
                unsupported_ids.add(warning.chunk_id)
            continue
        source_invalid_warning_count += warning.count
        if warning.chunk_id is None or warning.chunk_id not in encountered_ids:
            unbound_invalid_warning_count += warning.count
        else:
            invalid_ids.add(warning.chunk_id)

    entries = []
    for chunk_id, rows in sorted(grouped.items()):
        classifications = tuple(sorted({item.classification for item in rows}))
        names = {item.chunk_name for item in rows if item.chunk_name is not None}
        name = next(iter(names)) if len(names) == 1 else None
        count = sum(item.count for item in rows)
        if chunk_id in invalid_ids:
            state = "source-invalid"
        elif "unknown" in classifications or chunk_id in unknown_ids:
            state = "unknown"
        elif "unsupported" in classifications or chunk_id in unsupported_ids:
            state = "explicit-unsupported"
        else:
            state = "metadata-only"
        domain = _domain(chunk_id)
        entries.append(
            W3DSupportEntry(
                chunk_id=chunk_id,
                chunk_name=name,
                chunk_count=count,
                scanner_classifications=classifications,
                domain=domain,
                conversion_relevant=domain in _RELEVANT_DOMAINS,
                capability_state=state,
            )
        )

    failure_phases: Counter[str] = Counter()
    failure_codes: Counter[str] = Counter()
    index_codes: Counter[str] = Counter()
    for failure in evidence.failures:
        failure_phases[failure.phase] += failure.count
        failure_codes[failure.code] += failure.count
        if failure.phase == "index":
            index_codes[failure.code] += failure.count
    diagnostics = W3DSupportDiagnostics(
        warning_code_counts=tuple(sorted(warning_counts.items())),
        source_failure_phase_counts=tuple(sorted(failure_phases.items())),
        source_failure_code_counts=tuple(sorted(failure_codes.items())),
        header_index_failure_code_counts=tuple(sorted(index_codes.items())),
        duplicate_logical_id_kind_counts=evidence.duplicate_kinds,
        source_invalid_warning_count=source_invalid_warning_count,
        unbound_source_invalid_warning_count=unbound_invalid_warning_count,
    )
    return tuple(entries), diagnostics


def _attestation_from_mapping(value: Mapping[str, object]) -> W3DBacktestAttestation:
    chunk_id = _mapping_value(value, "chunkId", "chunk_id")
    chunk_count = _mapping_value(
        value, "chunkCount", "chunk_count", "observedCount", "observed_count"
    )
    source_hash = _mapping_value(
        value, "sourceEvidenceSha256", "source_evidence_sha256"
    )
    backtest_hash = _mapping_value(
        value,
        "backtestEvidenceSha256",
        "backtest_evidence_sha256",
        "evidenceSha256",
        "evidence_sha256",
    )
    decoded = _mapping_value(value, "decoded")
    backtested = _mapping_value(value, "backtested")
    return W3DBacktestAttestation(
        chunk_id=chunk_id,  # type: ignore[arg-type]
        chunk_count=chunk_count,  # type: ignore[arg-type]
        source_evidence_sha256=source_hash,  # type: ignore[arg-type]
        backtest_evidence_sha256=backtest_hash,  # type: ignore[arg-type]
        decoded=decoded,  # type: ignore[arg-type]
        backtested=backtested,  # type: ignore[arg-type]
    )


def _validated_attestations(
    raw_attestations: Iterable[W3DBacktestAttestation | Mapping[str, object]],
    entries: tuple[W3DSupportEntry, ...],
    source_evidence_sha256: str,
) -> tuple[W3DBacktestAttestation, ...]:
    if isinstance(raw_attestations, (str, bytes, bytearray, Mapping)):
        raise W3DSupportMatrixError("backtest attestations must be an iterable")
    try:
        values = tuple(raw_attestations)
    except TypeError as exc:
        raise W3DSupportMatrixError("backtest attestations must be iterable") from exc
    by_id = {item.chunk_id: item for item in entries}
    result = []
    seen: set[int] = set()
    for raw in values:
        if isinstance(raw, W3DBacktestAttestation):
            item = raw
        elif isinstance(raw, Mapping):
            item = _attestation_from_mapping(raw)
        else:
            raise W3DSupportMatrixError("invalid W3D backtest attestation")
        if not _is_uint32(item.chunk_id):
            raise W3DSupportMatrixError("attestation chunk ID must be unsigned 32-bit")
        if item.chunk_id in seen:
            raise W3DSupportMatrixError(
                f"duplicate attestation for chunk 0x{item.chunk_id:08X}"
            )
        seen.add(item.chunk_id)
        entry = by_id.get(item.chunk_id)
        if entry is None:
            raise W3DSupportMatrixError(
                f"attestation targets unencountered chunk 0x{item.chunk_id:08X}"
            )
        if (
            not _is_positive_int(item.chunk_count)
            or item.chunk_count != entry.chunk_count
        ):
            raise W3DSupportMatrixError(
                f"attestation count mismatch for chunk 0x{item.chunk_id:08X}"
            )
        if not _is_sha256(item.source_evidence_sha256):
            raise W3DSupportMatrixError(
                "attestation source evidence SHA-256 is invalid"
            )
        if item.source_evidence_sha256 != source_evidence_sha256:
            raise W3DSupportMatrixError(
                f"attestation source mismatch for chunk 0x{item.chunk_id:08X}"
            )
        if not _is_sha256(item.backtest_evidence_sha256):
            raise W3DSupportMatrixError("backtest evidence SHA-256 is invalid")
        if item.decoded is not True or item.backtested is not True:
            raise W3DSupportMatrixError(
                "attestation must explicitly prove decoded=true and backtested=true"
            )
        if entry.capability_state != "metadata-only":
            raise W3DSupportMatrixError(
                "attestation cannot override "
                f"{entry.capability_state} chunk 0x{item.chunk_id:08X}"
            )
        result.append(item)
    return tuple(sorted(result, key=lambda item: item.chunk_id))


def _domains(entries: tuple[W3DSupportEntry, ...]) -> tuple[W3DDomainSupport, ...]:
    result = []
    for domain in CAPABILITY_DOMAINS:
        selected = tuple(item for item in entries if item.domain == domain)
        state_ids = Counter(item.capability_state for item in selected)
        state_counts = Counter()
        for item in selected:
            state_counts[item.capability_state] += item.chunk_count
        result.append(
            W3DDomainSupport(
                domain=domain,
                chunk_id_count=len(selected),
                chunk_count=sum(item.chunk_count for item in selected),
                state_chunk_id_counts=tuple(
                    (state, state_ids.get(state, 0)) for state in CAPABILITY_STATES
                ),
                state_chunk_counts=tuple(
                    (state, state_counts.get(state, 0)) for state in CAPABILITY_STATES
                ),
            )
        )
    return tuple(result)


def build_w3d_support_matrix(
    report_or_catalog: object,
    *,
    backtest_attestations: Iterable[W3DBacktestAttestation | Mapping[str, object]] = (),
) -> W3DSupportMatrixReport:
    """Build conservative conversion capability evidence from a W3D report.

    Catalog ``complete`` and scanner ``known-data`` labels are intentionally
    ignored as converter claims.  Eligible entries are promoted only by exact,
    non-duplicated attestations bound to this report's evidence hash and full
    aggregate count.
    """

    evidence = _evidence(report_or_catalog)
    entries, diagnostics = _base_entries(evidence)
    attestations = _validated_attestations(
        backtest_attestations, entries, evidence.source_evidence_sha256
    )
    attestation_by_id = {item.chunk_id: item for item in attestations}
    promoted = tuple(
        replace(
            entry,
            capability_state="decoded-and-backtested",
            backtest_evidence_sha256=attestation_by_id[
                entry.chunk_id
            ].backtest_evidence_sha256,
        )
        if entry.chunk_id in attestation_by_id
        else entry
        for entry in entries
    )
    attestation_set_sha256 = _canonical_sha256(
        {
            "schema": "openbfme.w3d-backtest-attestation-set",
            "schemaVersion": 0,
            "attestations": [item.neutral() for item in attestations],
        }
    )
    conversion_complete = (
        all(
            not entry.conversion_relevant
            or entry.capability_state == "decoded-and-backtested"
            for entry in promoted
        )
        and all(
            entry.capability_state not in {"unknown", "source-invalid"}
            for entry in promoted
        )
        and diagnostics.source_invalid_warning_count == 0
        and diagnostics.source_failure_count == 0
        and diagnostics.duplicate_logical_id_count == 0
    )
    report = W3DSupportMatrixReport(
        entries=promoted,
        domains=_domains(promoted),
        diagnostics=diagnostics,
        attestations=attestations,
        source_evidence_sha256=evidence.source_evidence_sha256,
        attestation_set_sha256=attestation_set_sha256,
        conversion_complete=conversion_complete,
        matrix_sha256="",
    )
    return replace(report, matrix_sha256=_canonical_sha256(report.matrix_hash_basis()))


__all__ = [
    "CAPABILITY_DOMAINS",
    "CAPABILITY_STATES",
    "W3DBacktestAttestation",
    "W3DDomainSupport",
    "W3DSupportDiagnostics",
    "W3DSupportEntry",
    "W3DSupportMatrixError",
    "W3DSupportMatrixReport",
    "build_w3d_support_matrix",
]
