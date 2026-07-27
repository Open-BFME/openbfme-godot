"""Ranked, faction-weighted census of retail W3D chunk-decode gaps.

The aggregate "N unsupported chunk warnings" number that the W3D catalog
reports is not actionable: a chunk ID occurring hundreds of times inside a
handful of unused props matters less than one occurring a few times in every
shipped unit.  This module turns per-file scan evidence into the committed
``game/data/retail_w3d_chunk_backlog.json`` work queue, ranked by **distinct
files blocked** and cross-referenced against per-faction asset closures and
the sealed decode-corpus evidence.

Privacy contract: the rendered census contains chunk IDs, chunk names, counts
and hashes only.  Retail virtual paths, authored asset identifiers, payload
bytes and geometry never appear in the output; they exist only inside the
in-memory evidence rows used during aggregation.

Regeneration (byte-identical for the same inputs)::

    PYTHONPATH=importer python -m openbfme_importer.w3d_chunk_backlog \
        <effective-assets-root> <reports-dir> <output-json>

where ``<reports-dir>`` must contain ``bfme2-w3d-decode-corpus.json`` and the
``faction-slice-*-plan.json`` closure plans.
"""

from __future__ import annotations

from collections import Counter, defaultdict
from collections.abc import Collection, Iterable, Mapping
from dataclasses import dataclass
import json
from pathlib import Path

from .w3d_index import (
    W3DFileHeaders,
    _safe_identifier,
    build_w3d_index,
    reject_ambiguous_w3d_trims,
)
from .w3d_metadata import W3DMetadata, scan_w3d_metadata


RETAIL_W3D_CHUNK_BACKLOG_SCHEMA = "openbfme.retail-w3d-chunk-backlog"
RETAIL_W3D_CHUNK_BACKLOG_SCHEMA_VERSION = 1

_FLAGGED_CLASSIFICATIONS = frozenset({"unsupported", "unknown"})
_METADATA_DAMAGE_CODES = frozenset(
    {
        "empty-source",
        "truncated-chunk-header",
        "truncated-chunk-payload",
        "truncated-metadata-record",
        "truncated-record-tail",
        "invalid-secondary-geometry",
    }
)
_STATUS_FLAGS = ("damaged", "incomplete", "unresolved", "unsupported")


class W3DChunkBacklogError(ValueError):
    """Raised when census inputs are structurally untrustworthy."""


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and value == value.casefold()
        and all(character in "0123456789abcdef" for character in value)
    )


@dataclass(frozen=True, slots=True)
class W3DBacklogFileEvidence:
    """Private per-file aggregation input.  Never serialized as-is."""

    path_key: str
    source_sha256: str
    byte_length: int
    chunk_occurrences: tuple[tuple[int, int], ...]
    flagged_chunk_occurrences: tuple[tuple[int, str, int], ...]
    chunk_names: tuple[tuple[int, str], ...]
    warning_codes: tuple[str, ...]
    index_rejection_causes: tuple[str, ...]
    logical_ids: tuple[tuple[str, str], ...]
    # The (kind, casefolded id) namespace this file would occupy in the
    # aggregate index: authored ids for raw-accepted files, trimmed ids for
    # trim-pending files, empty for files that stay rejected outright.
    header_ids: tuple[tuple[str, str], ...] = ()
    # (kind, authored, admitted) for every identifier whose padding trim is
    # the exact single-file repair; admission still requires the corpus-wide
    # uniqueness proof performed during aggregation.
    trimmed_identifiers: tuple[tuple[str, str, str], ...] = ()


def _identifier_rejection_cause(identifier: str) -> str | None:
    """Classify one identifier that the index REJECTED, or return None.

    Only identifiers that actually fail the index's own safety contract are
    classified; an identifier the index accepts never contributes a cause.
    """

    try:
        _safe_identifier(identifier, "header ID")
    except (TypeError, ValueError):
        pass
    else:
        return None
    if not identifier:
        return "empty-identifier"
    if identifier != identifier.strip():
        return "surrounding-whitespace-identifier"
    if any(ord(character) < 32 or ord(character) > 126 for character in identifier):
        return "non-printable-identifier"
    return "unsafe-identifier-other"


def collect_w3d_backlog_file_evidence(
    source: bytes, virtual_path: str
) -> W3DBacklogFileEvidence:
    """Scan one W3D source into an aggregation row.

    Index acceptance is probed with the exact single-file bridge the catalog
    uses, so the census reproduces catalog rejection accounting without
    holding every scanned file in memory at once.
    """

    metadata = scan_w3d_metadata(source, virtual_path)
    return backlog_file_evidence_from_metadata(metadata)


def backlog_file_evidence_from_metadata(
    metadata: W3DMetadata,
) -> W3DBacklogFileEvidence:
    if not isinstance(metadata, W3DMetadata):
        raise W3DChunkBacklogError("backlog evidence requires W3DMetadata")
    occurrences: Counter[int] = Counter()
    flagged: Counter[tuple[int, str]] = Counter()
    names: dict[int, str] = {}
    for chunk in metadata.chunks:
        occurrences[chunk.chunk_id] += 1
        if chunk.classification in _FLAGGED_CLASSIFICATIONS:
            flagged[(chunk.chunk_id, chunk.classification)] += 1
        if chunk.chunk_name is not None:
            names.setdefault(chunk.chunk_id, chunk.chunk_name)

    causes: list[str] = []
    admitted_headers: W3DFileHeaders | None = None
    trimmed_identifiers: tuple[tuple[str, str, str], ...] = ()
    try:
        build_w3d_index((metadata.virtual_path,), (metadata.file_headers(),))
    except ValueError:
        identifiers = (
            *metadata.model_ids,
            *metadata.hierarchy_ids,
            *metadata.animation_ids,
        )
        seen: set[str] = set()
        for identifier in identifiers:
            cause = _identifier_rejection_cause(identifier)
            if cause is not None and cause not in seen:
                seen.add(cause)
                causes.append(cause)
        if not causes:
            causes.append("index-rejected-other")
        # Trim-and-prove-unique admission, single-file half: the trimmed
        # headers must repair the rejection on their own.  The corpus-wide
        # uniqueness proof happens in :func:`build_w3d_chunk_backlog`.
        try:
            trimmed_headers, trims = metadata.trimmed_file_headers()
            if trims:
                build_w3d_index((metadata.virtual_path,), (trimmed_headers,))
                admitted_headers = trimmed_headers
                trimmed_identifiers = tuple(
                    (trim.kind, trim.authored_identifier, trim.admitted_identifier)
                    for trim in trims
                )
        except (TypeError, ValueError):
            admitted_headers = None
            trimmed_identifiers = ()
    else:
        admitted_headers = metadata.file_headers()

    header_ids: tuple[tuple[str, str], ...] = ()
    if admitted_headers is not None:
        header_ids = tuple(
            (kind, value.casefold())
            for kind, values in (
                ("model", admitted_headers.model_ids),
                ("animation", admitted_headers.animation_ids),
                ("hierarchy", admitted_headers.hierarchy_ids),
            )
            for value in values
        )

    logical_ids = [
        *(("model", value.casefold()) for value in metadata.model_ids),
        *(("hierarchy", value.casefold()) for value in metadata.hierarchy_ids),
        *(("animation", value.casefold()) for value in metadata.animation_ids),
    ]
    return W3DBacklogFileEvidence(
        path_key=metadata.virtual_path.casefold(),
        source_sha256=metadata.source_sha256,
        byte_length=metadata.byte_length,
        chunk_occurrences=tuple(sorted(occurrences.items())),
        flagged_chunk_occurrences=tuple(
            (chunk_id, classification, count)
            for (chunk_id, classification), count in sorted(flagged.items())
        ),
        chunk_names=tuple(sorted(names.items())),
        warning_codes=tuple(item.code for item in metadata.warnings),
        index_rejection_causes=tuple(causes),
        logical_ids=tuple(logical_ids),
        header_ids=header_ids,
        trimmed_identifiers=trimmed_identifiers,
    )


def w3d_paths_from_slice_plan(plan: Mapping[str, object]) -> frozenset[str]:
    """Extract every ``*.w3d`` virtual path referenced by a slice plan."""

    if not isinstance(plan, Mapping):
        raise W3DChunkBacklogError("slice plan must be a mapping")
    found: set[str] = set()

    def walk(value: object) -> None:
        if isinstance(value, Mapping):
            for child in value.values():
                walk(child)
        elif isinstance(value, (list, tuple)):
            for child in value:
                walk(child)
        elif isinstance(value, str) and value.casefold().endswith(".w3d"):
            found.add(value.casefold())

    walk(plan.get("resources"))
    return frozenset(found)


def _validated_faction_closures(
    faction_closures: Mapping[str, Collection[str]],
) -> dict[str, frozenset[str]]:
    if not isinstance(faction_closures, Mapping) or not faction_closures:
        raise W3DChunkBacklogError("faction closures must be a non-empty mapping")
    result: dict[str, frozenset[str]] = {}
    for faction, paths in faction_closures.items():
        if not isinstance(faction, str) or not faction:
            raise W3DChunkBacklogError("faction names must be non-empty strings")
        if isinstance(paths, (str, bytes)):
            raise W3DChunkBacklogError("faction closure paths must be a collection")
        validated = set()
        for path in paths:
            if not isinstance(path, str) or not path:
                raise W3DChunkBacklogError("faction closure paths must be strings")
            validated.add(path.casefold())
        result[faction] = frozenset(validated)
    return dict(sorted(result.items()))


def _validated_decode_states(
    decode_source_states: Mapping[str, Mapping[str, bool]] | None,
) -> dict[str, dict[str, bool]]:
    if decode_source_states is None:
        return {}
    if not isinstance(decode_source_states, Mapping):
        raise W3DChunkBacklogError("decode source states must be a mapping")
    result: dict[str, dict[str, bool]] = {}
    for sha, status in decode_source_states.items():
        if not _is_sha256(sha):
            raise W3DChunkBacklogError("decode source state keys must be SHA-256")
        if not isinstance(status, Mapping):
            raise W3DChunkBacklogError("decode source states must map to objects")
        row: dict[str, bool] = {}
        for flag in (*_STATUS_FLAGS, "streamComplete"):
            value = status.get(flag)
            if not isinstance(value, bool):
                raise W3DChunkBacklogError(
                    f"decode source state {flag!r} must be a boolean"
                )
            row[flag] = value
        result[sha] = row
    return result


def _validated_decode_chunk_rows(
    decode_chunk_rows: Mapping[int, Mapping[str, int]] | None,
) -> dict[int, dict[str, int]]:
    if decode_chunk_rows is None:
        return {}
    if not isinstance(decode_chunk_rows, Mapping):
        raise W3DChunkBacklogError("decode chunk rows must be a mapping")
    result: dict[int, dict[str, int]] = {}
    for chunk_id, row in decode_chunk_rows.items():
        if isinstance(chunk_id, bool) or not isinstance(chunk_id, int):
            raise W3DChunkBacklogError("decode chunk row keys must be integers")
        if not isinstance(row, Mapping):
            raise W3DChunkBacklogError("decode chunk rows must map to objects")
        decoded = row.get("decodedStreamCount")
        terminal = row.get("terminalCount")
        for value in (decoded, terminal):
            if isinstance(value, bool) or not isinstance(value, int) or value < 0:
                raise W3DChunkBacklogError(
                    "decode chunk rows require non-negative integer counts"
                )
        result[chunk_id] = {
            "decodedStreamCount": decoded,
            "terminalCount": terminal,
        }
    return result


def build_w3d_chunk_backlog(
    evidence: Iterable[W3DBacklogFileEvidence],
    *,
    faction_closures: Mapping[str, Collection[str]],
    corpus_identity: Mapping[str, object],
    decode_source_states: Mapping[str, Mapping[str, bool]] | None = None,
    decode_chunk_rows: Mapping[int, Mapping[str, int]] | None = None,
) -> dict[str, object]:
    """Aggregate per-file evidence into the JSON-ready ranked census."""

    closures = _validated_faction_closures(faction_closures)
    states = _validated_decode_states(decode_source_states)
    chunk_rows = _validated_decode_chunk_rows(decode_chunk_rows)
    if not isinstance(corpus_identity, Mapping):
        raise W3DChunkBacklogError("corpus identity must be a mapping")

    rows = list(evidence)
    if not rows:
        raise W3DChunkBacklogError("chunk backlog requires at least one evidence row")
    seen_paths: set[str] = set()
    for row in rows:
        if type(row) is not W3DBacklogFileEvidence:
            raise W3DChunkBacklogError("evidence rows must be W3DBacklogFileEvidence")
        if row.path_key in seen_paths:
            raise W3DChunkBacklogError("duplicate evidence row for one virtual path")
        seen_paths.add(row.path_key)
    rows.sort(key=lambda row: row.path_key)
    any_closure: frozenset[str] = frozenset().union(*closures.values())

    occurrences: Counter[int] = Counter()
    flagged_occurrences: Counter[int] = Counter()
    classifications: dict[int, set[str]] = defaultdict(set)
    blocked_files: dict[int, set[str]] = defaultdict(set)
    chunk_names: dict[int, str] = {}
    warning_counts: Counter[str] = Counter()
    metadata_damaged_files: set[str] = set()
    metadata_damage_counts: Counter[str] = Counter()
    rejected_files: set[str] = set()
    rejection_causes: Counter[str] = Counter()
    trim_pending: list[W3DBacklogFileEvidence] = []
    occupied_header_ids: dict[tuple[str, str], set[str]] = defaultdict(set)
    logical_groups: dict[tuple[str, str], list[str]] = defaultdict(list)
    status_files: dict[str, set[str]] = {flag: set() for flag in _STATUS_FLAGS}
    not_stream_complete: set[str] = set()
    unjoined_decode_paths = 0

    for row in rows:
        for chunk_id, count in row.chunk_occurrences:
            occurrences[chunk_id] += count
        for chunk_id, classification, count in row.flagged_chunk_occurrences:
            flagged_occurrences[chunk_id] += count
            classifications[chunk_id].add(classification)
            blocked_files[chunk_id].add(row.path_key)
        for chunk_id, name in row.chunk_names:
            chunk_names.setdefault(chunk_id, name)
        for code in row.warning_codes:
            warning_counts[code] += 1
            if code in _METADATA_DAMAGE_CODES:
                metadata_damaged_files.add(row.path_key)
                metadata_damage_counts[code] += 1
        if row.index_rejection_causes and not row.trimmed_identifiers:
            rejected_files.add(row.path_key)
            for cause in row.index_rejection_causes:
                rejection_causes[cause] += 1
        if row.trimmed_identifiers:
            trim_pending.append(row)
        for key in row.header_ids:
            occupied_header_ids[key].add(row.path_key)
        for kind, identifier in row.logical_ids:
            logical_groups[(kind, identifier)].append(row.source_sha256)
        if states:
            status = states.get(row.source_sha256)
            if status is None:
                unjoined_decode_paths += 1
            else:
                for flag in _STATUS_FLAGS:
                    if status[flag]:
                        status_files[flag].add(row.path_key)
                if not status["streamComplete"]:
                    not_stream_complete.add(row.path_key)

    # Trim-and-prove-unique admission, corpus-wide half.  A trim-pending file
    # is admitted only when every trimmed identifier is claimed by no other
    # file in the post-trim namespace; any collision keeps the established
    # fail-closed index rejection.
    changed = {
        row.path_key: frozenset(
            (kind, admitted.casefold())
            for kind, _authored, admitted in row.trimmed_identifiers
        )
        for row in trim_pending
    }
    collided = reject_ambiguous_w3d_trims(occupied_header_ids, changed)
    trim_admitted_files: set[str] = set()
    trim_admitted_identifier_count = 0
    trim_admitted_kind_counts: Counter[str] = Counter()
    for row in trim_pending:
        if row.path_key in collided:
            rejected_files.add(row.path_key)
            for cause in row.index_rejection_causes:
                rejection_causes[cause] += 1
            rejection_causes["trimmed-identifier-collision"] += 1
        else:
            trim_admitted_files.add(row.path_key)
            trim_admitted_identifier_count += len(row.trimmed_identifiers)
            for kind, _authored, _admitted in row.trimmed_identifiers:
                trim_admitted_kind_counts[kind] += 1

    def reachable(paths: set[str]) -> int:
        return sum(1 for path in paths if path in any_closure)

    backlog_entries: list[dict[str, object]] = []
    for chunk_id in sorted(flagged_occurrences):
        files = blocked_files[chunk_id]
        entry_classifications = sorted(classifications[chunk_id])
        entry: dict[str, object] = {
            "chunkId": chunk_id,
            "chunkIdHex": f"0x{chunk_id:08X}",
            "chunkName": chunk_names.get(chunk_id),
            "scannerClassifications": entry_classifications,
            "occurrences": flagged_occurrences[chunk_id],
            "filesBlocked": len(files),
            "factionReachableFiles": reachable(files),
            "factionFiles": {
                faction: sum(1 for path in files if path in paths)
                for faction, paths in closures.items()
            },
            "decodeLayer": chunk_rows.get(chunk_id),
        }
        backlog_entries.append(entry)
    backlog_entries.sort(
        key=lambda entry: (
            -int(entry["filesBlocked"]),
            -int(entry["occurrences"]),
            int(entry["chunkId"]),
        )
    )

    duplicate_kind_counts: Counter[str] = Counter()
    identical_groups = 0
    distinct_groups = 0
    for (kind, _identifier), shas in logical_groups.items():
        if len(shas) < 2:
            continue
        duplicate_kind_counts[kind] += 1
        if len(set(shas)) == 1:
            identical_groups += 1
        else:
            distinct_groups += 1

    all_blocked = set().union(*blocked_files.values()) if blocked_files else set()
    census: dict[str, object] = {
        "$comment": (
            "Ranked retail W3D chunk-decode backlog. Regenerate with "
            "`PYTHONPATH=importer python -m openbfme_importer.w3d_chunk_backlog "
            "<effective-assets-root> <reports-dir> <output-json>`; do not "
            "hand-edit. Ranking is by DISTINCT FILES BLOCKED at the metadata-"
            "scanner level, not raw occurrences. `decodeLayer` quotes the "
            "sealed decode-corpus evidence for the same chunk ID: a non-null "
            "row with terminalCount 0 means the stream decoder already "
            "decodes every occurrence and only census accounting lags. "
            "anomalies.trimAdmitted counts files whose only index defect "
            "was retail fixed-width whitespace padding in an authored "
            "header identifier and whose trimmed ids are provably unique "
            "corpus-wide; a collision keeps the fail-closed rejection and "
            "is counted under anomalies.indexRejected instead. "
            "Aggregate counts and hashes only - no retail paths, authored "
            "identifiers, payload bytes, or geometry."
        ),
        "schema": RETAIL_W3D_CHUNK_BACKLOG_SCHEMA,
        "schemaVersion": RETAIL_W3D_CHUNK_BACKLOG_SCHEMA_VERSION,
        "corpus": dict(corpus_identity),
        "summary": {
            "scannedFileCount": len(rows),
            "warningCodeCounts": dict(sorted(warning_counts.items())),
            "flaggedChunkIdCount": len(backlog_entries),
            "blockedFileCount": len(all_blocked),
            "factionReachableBlockedFileCount": reachable(all_blocked),
            "factionClosureW3dFileCounts": {
                faction: len(paths) for faction, paths in closures.items()
            },
        },
        "backlog": backlog_entries,
        "anomalies": {
            "indexRejected": {
                "fileCount": len(rejected_files),
                "causeCounts": dict(sorted(rejection_causes.items())),
                "factionReachableFileCount": reachable(rejected_files),
            },
            "trimAdmitted": {
                "fileCount": len(trim_admitted_files),
                "identifierCount": trim_admitted_identifier_count,
                "kindCounts": dict(sorted(trim_admitted_kind_counts.items())),
                "factionReachableFileCount": reachable(trim_admitted_files),
            },
            "metadataDamaged": {
                "fileCount": len(metadata_damaged_files),
                "warningCodeCounts": dict(sorted(metadata_damage_counts.items())),
                "factionReachableFileCount": reachable(metadata_damaged_files),
            },
            "decodeCorpus": {
                "joinedBySourceSha256": bool(states),
                "unjoinedFileCount": unjoined_decode_paths,
                "notStreamCompleteFileCount": len(not_stream_complete),
                "notStreamCompleteFactionReachableFileCount": reachable(
                    not_stream_complete
                ),
                **{
                    f"{flag}FileCount": len(status_files[flag])
                    for flag in _STATUS_FLAGS
                },
                **{
                    f"{flag}FactionReachableFileCount": reachable(status_files[flag])
                    for flag in _STATUS_FLAGS
                },
            },
            "duplicateLogicalIds": {
                "groupCount": sum(duplicate_kind_counts.values()),
                "kindCounts": dict(sorted(duplicate_kind_counts.items())),
                "identicalByteGroupCount": identical_groups,
                "distinctByteGroupCount": distinct_groups,
            },
        },
    }
    return census


def render_w3d_chunk_backlog(census: Mapping[str, object]) -> str:
    """Render the census as canonical LF-terminated, tab-indented JSON."""

    if not isinstance(census, Mapping):
        raise W3DChunkBacklogError("census must be a mapping")
    if census.get("schema") != RETAIL_W3D_CHUNK_BACKLOG_SCHEMA:
        raise W3DChunkBacklogError("census schema is not the chunk backlog schema")
    return json.dumps(census, ensure_ascii=True, indent="\t") + "\n"


def scan_w3d_chunk_backlog_tree(
    effective_assets_root: Path | str,
    *,
    faction_closures: Mapping[str, Collection[str]],
    decode_corpus: Mapping[str, object] | None = None,
    game: str = "bfme2",
) -> dict[str, object]:
    """Stream-scan a verified effective-assets tree into the ranked census."""

    # Imported here so pure aggregation (and its tests) never require the
    # corpus module's filesystem contract.
    from .w3d_corpus import (
        _load_manifest,
        _read_verified_w3d,
        _resolve_root,
        _validate_tree,
    )

    root = _resolve_root(effective_assets_root)
    _, manifest = _load_manifest(root)
    actual_files = _validate_tree(root, manifest)
    selected = [item for item in manifest.files if item.is_w3d]
    if not selected:
        raise W3DChunkBacklogError("effective-assets manifest declares no W3D files")

    decode_states: dict[str, Mapping[str, bool]] | None = None
    decode_rows: dict[int, Mapping[str, int]] | None = None
    decode_identity: dict[str, object] = {}
    if decode_corpus is not None:
        if not isinstance(decode_corpus, Mapping):
            raise W3DChunkBacklogError("decode corpus must be a mapping")
        sources = decode_corpus.get("sources")
        chunks = decode_corpus.get("chunks")
        if not isinstance(sources, list) or not isinstance(chunks, list):
            raise W3DChunkBacklogError("decode corpus lacks sources/chunks arrays")
        decode_states = {}
        for source in sources:
            if not isinstance(source, Mapping):
                raise W3DChunkBacklogError("decode corpus sources must be objects")
            sha = source.get("sourceSha256")
            status = source.get("status")
            if not _is_sha256(sha) or not isinstance(status, Mapping):
                raise W3DChunkBacklogError("decode corpus source row is invalid")
            decode_states[sha] = {
                **{flag: bool(status.get(flag)) for flag in _STATUS_FLAGS},
                "streamComplete": bool(status.get("streamComplete")),
            }
        decode_rows = {}
        for chunk in chunks:
            if not isinstance(chunk, Mapping):
                raise W3DChunkBacklogError("decode corpus chunks must be objects")
            decode_rows[int(chunk["chunkId"])] = {
                "decodedStreamCount": int(chunk["decodedStreamCount"]),
                "terminalCount": int(chunk["terminalCount"]),
            }
        hashes = decode_corpus.get("hashes")
        decode_identity = {
            "schema": decode_corpus.get("schema"),
            "schemaVersion": decode_corpus.get("schemaVersion"),
            "hashes": dict(hashes) if isinstance(hashes, Mapping) else None,
        }

    def evidence_rows() -> Iterable[W3DBacklogFileEvidence]:
        for item in selected:
            actual = actual_files[item.path.casefold()]
            data = _read_verified_w3d(actual, item)
            yield collect_w3d_backlog_file_evidence(data, item.path)

    corpus_identity: dict[str, object] = {
        "game": game,
        "manifestFileCount": manifest.total_files,
        "manifestTotalBytes": manifest.total_bytes,
        "manifestSha256": manifest.raw_sha256,
        "manifestAggregateSha256": manifest.aggregate_sha256,
        "w3dFileCount": len(selected),
        "w3dTotalBytes": sum(item.size for item in selected),
        "factionClosureSource": (
            "union of W3D leaves across the faction-slice plans per faction"
        ),
        "decodeCorpus": decode_identity or None,
    }
    return build_w3d_chunk_backlog(
        evidence_rows(),
        faction_closures=faction_closures,
        corpus_identity=corpus_identity,
        decode_source_states=decode_states,
        decode_chunk_rows=decode_rows,
    )


def faction_closures_from_reports_dir(
    reports_dir: Path | str,
    *,
    factions: tuple[str, ...] = (
        "men",
        "elves",
        "dwarves",
        "isengard",
        "mordor",
        "wild",
    ),
) -> dict[str, frozenset[str]]:
    """Union W3D closure paths per faction from ``faction-slice-*-plan.json``."""

    directory = Path(reports_dir)
    plans = sorted(directory.glob("faction-slice-*-plan.json"))
    if not plans:
        raise W3DChunkBacklogError("reports directory contains no slice plans")
    closures: dict[str, set[str]] = {faction: set() for faction in factions}
    for plan_path in plans:
        try:
            plan = json.loads(plan_path.read_text(encoding="utf-8"))
        except (OSError, UnicodeDecodeError, json.JSONDecodeError) as exc:
            raise W3DChunkBacklogError(f"invalid slice plan: {exc}") from exc
        pack = plan.get("pack") if isinstance(plan, Mapping) else None
        if not isinstance(pack, str):
            raise W3DChunkBacklogError("slice plan lacks a pack identity")
        matched = [faction for faction in factions if f"-{faction}-" in pack]
        if len(matched) != 1:
            continue
        closures[matched[0]] |= w3d_paths_from_slice_plan(plan)
    return {faction: frozenset(paths) for faction, paths in closures.items()}


def _main(argv: list[str]) -> int:
    if len(argv) != 3:
        raise SystemExit(
            "usage: python -m openbfme_importer.w3d_chunk_backlog "
            "<effective-assets-root> <reports-dir> <output-json>"
        )
    root, reports_dir, output = argv
    decode_corpus_path = Path(reports_dir) / "bfme2-w3d-decode-corpus.json"
    decode_corpus = json.loads(decode_corpus_path.read_text(encoding="utf-8"))
    census = scan_w3d_chunk_backlog_tree(
        root,
        faction_closures=faction_closures_from_reports_dir(reports_dir),
        decode_corpus=decode_corpus,
    )
    rendered = render_w3d_chunk_backlog(census)
    with open(output, "w", encoding="utf-8", newline="\n") as handle:
        handle.write(rendered)
    return 0


if __name__ == "__main__":  # pragma: no cover - thin CLI shim
    import sys

    raise SystemExit(_main(sys.argv[1:]))


__all__ = [
    "RETAIL_W3D_CHUNK_BACKLOG_SCHEMA",
    "RETAIL_W3D_CHUNK_BACKLOG_SCHEMA_VERSION",
    "W3DBacklogFileEvidence",
    "W3DChunkBacklogError",
    "backlog_file_evidence_from_metadata",
    "build_w3d_chunk_backlog",
    "collect_w3d_backlog_file_evidence",
    "faction_closures_from_reports_dir",
    "render_w3d_chunk_backlog",
    "scan_w3d_chunk_backlog_tree",
    "w3d_paths_from_slice_plan",
]
