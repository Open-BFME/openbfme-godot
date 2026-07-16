"""Transactional runtime-native corpus builder for every verified SAGE map.

The builder consumes only a verified effective-assets tree.  It delegates the
complete manifest/tree census to :func:`map_corpus.scan_map_corpus`, stages and
SHA-verifies every candidate again, cooks only structurally proven terrain maps
with the strict public SAGE map converter, and independently backtests every
cooked directory.  Structurally proven SCB script/camera containers are bound
into the manifest as no-output handoffs.

Published map directories are addressed by source SHA-256 plus the explicit,
output-affecting profile rather than a retail name.  The private manifest
remains payload-free: it contains hashes, sizes, bounded parser/backtest
evidence, and unresolved object-binding counts, but never source map bytes,
scripts, coordinates, object type names, or host paths.  A parser, conversion,
or backtest rejection aborts the entire publish; the raised
:class:`MapNativeCorpusBuildError` retains the deterministic report.

This proves structural conversion and native artifact integrity.  It does not
claim runtime gameplay or audiovisual fidelity for any map.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
from typing import Any, Iterable, Mapping, Sequence
import uuid

from .map_corpus import (
    MAP_DISCOVERY_REASON_ORDER,
    MAP_DISCOVERY_REASONS,
    MAP_PROFILES,
    MAX_MAP_CORPUS_FILES,
    MAX_MAP_CORPUS_TOTAL_BYTES,
    SCRIPT_CONTAINER_ARTIFACT,
    UNCLASSIFIED_CANDIDATE_ARTIFACT,
    MapCorpusError,
    MapCorpusFile,
    MapCorpusLimitError,
    MapCorpusReport,
    scan_map_corpus,
)
from .native_backtest import validate_cooked_sage_map
from .paths import safe_relative_parts
from .sage_map import SAGE_MAP_PROFILE_VERSION, SageMapError, convert_sage_map


MAP_NATIVE_CORPUS_SCHEMA = "openbfme.map-native-corpus"
MAP_NATIVE_CORPUS_SCHEMA_VERSION = 0
MAP_NATIVE_CORPUS_MANIFEST = "manifest.json"

MAX_MAP_NATIVE_MANIFEST_BYTES = 64 * 1024 * 1024
HASH_BLOCK_BYTES = 1024 * 1024

_SHA256_CHARACTERS = frozenset("0123456789abcdef")
_OBJECT_RESOLUTION_KEYS = frozenset(
    {
        "resolutionStatus",
        "typeCount",
        "placementCount",
        "resolvedTypeCount",
        "resolvedPlacementCount",
        "boundTypeCount",
        "boundPlacementCount",
        "logicalTypeCount",
        "logicalPlacementCount",
        "unresolvedTypeCount",
        "unresolvedPlacementCount",
    }
)
_PROFILE_RUNNABLE = {
    "multiplayer": True,
    "scenario": True,
    "library": False,
    "placeholder": False,
}


class MapNativeCorpusError(ValueError):
    """Base class for a rejected map-native corpus operation."""


class MapNativeCorpusLimitError(MapNativeCorpusError):
    """Raised before conversion when a selected corpus exceeds a bound."""


class MapNativeCorpusReuseError(MapNativeCorpusError):
    """Raised when a pre-existing destination cannot be safely reused."""


@dataclass(frozen=True, slots=True)
class MapNativeArtifact:
    """One authored cooked-map file and its payload-free identity."""

    path: str
    byte_length: int
    sha256: str

    def neutral(self) -> dict[str, object]:
        return {
            "path": self.path,
            "bytes": self.byte_length,
            "sha256": self.sha256,
        }


@dataclass(frozen=True, slots=True)
class MapNativeOutput:
    """One unique, content-addressed cooked map directory."""

    path: str
    source_sha256: str
    profile: str
    profile_version: int
    runnable: bool
    structural_status: str
    inventory: tuple[MapNativeArtifact, ...]
    tree_sha256: str
    backtest_evidence: Mapping[str, Any]
    object_resolution: Mapping[str, Any]

    @property
    def byte_length(self) -> int:
        return sum(item.byte_length for item in self.inventory)

    def neutral(self) -> dict[str, object]:
        return {
            "path": self.path,
            "sourceSha256": self.source_sha256,
            "mapKind": self.profile,
            "profileVersion": self.profile_version,
            "runnable": self.runnable,
            "structuralStatus": self.structural_status,
            "fileCount": len(self.inventory),
            "bytes": self.byte_length,
            "treeSha256": self.tree_sha256,
            "inventory": [item.neutral() for item in self.inventory],
            "backtestEvidence": _json_clone(self.backtest_evidence),
            "objectResolution": _json_clone(self.object_resolution),
        }


@dataclass(frozen=True, slots=True)
class MapNativeEntry:
    """One suffix- or signature-discovered source map and its conversion result."""

    source_path: str
    source_bytes: int
    source_sha256: str
    discovery_reasons: tuple[str, ...]
    profile: str
    profile_version: int
    runnable: bool
    structural_status: str
    census_evidence_sha256: str
    body_sha256: str | None
    status: str
    rejection_code: str | None
    rejection_evidence_sha256: str | None
    output_path: str | None

    @property
    def accepted(self) -> bool:
        return self.status == "accepted"

    def neutral(self) -> dict[str, object]:
        return {
            "sourcePath": self.source_path,
            "sourceBytes": self.source_bytes,
            "sourceSha256": self.source_sha256,
            "discoveryReasons": list(self.discovery_reasons),
            "mapKind": self.profile,
            "profileVersion": self.profile_version,
            "runnable": self.runnable,
            "structuralStatus": self.structural_status,
            "censusEvidenceSha256": self.census_evidence_sha256,
            "bodySha256": self.body_sha256,
            "status": self.status,
            "rejectionCode": self.rejection_code,
            "rejectionEvidenceSha256": self.rejection_evidence_sha256,
            "outputPath": self.output_path,
        }


@dataclass(frozen=True, slots=True)
class MapNativeHandoff:
    """One non-terrain candidate accounted without a cooked output."""

    source_path: str
    source_bytes: int
    source_sha256: str
    discovery_reasons: tuple[str, ...]
    artifact_kind: str
    classification_evidence_sha256: str
    chunk_name_set_sha256: str
    census_evidence_sha256: str
    body_sha256: str | None
    status: str
    rejection_code: str | None
    rejection_evidence_sha256: str | None
    handoff_evidence_sha256: str

    @property
    def handed_off(self) -> bool:
        return self.status == "handed-off"

    def neutral(self) -> dict[str, object]:
        return {
            "sourcePath": self.source_path,
            "sourceBytes": self.source_bytes,
            "sourceSha256": self.source_sha256,
            "discoveryReasons": list(self.discovery_reasons),
            "artifactKind": self.artifact_kind,
            "classificationEvidenceSha256": self.classification_evidence_sha256,
            "chunkNameSetSha256": self.chunk_name_set_sha256,
            "censusEvidenceSha256": self.census_evidence_sha256,
            "bodySha256": self.body_sha256,
            "status": self.status,
            "rejectionCode": self.rejection_code,
            "rejectionEvidenceSha256": self.rejection_evidence_sha256,
            "handoffEvidenceSha256": self.handoff_evidence_sha256,
            "outputPath": None,
        }


@dataclass(frozen=True, slots=True)
class MapNativeCorpusReport:
    """Local paths plus deterministic, host-independent corpus evidence."""

    source_root: Path
    output_root: Path
    manifest_path: Path
    source_manifest_sha256: str
    source_manifest_aggregate_sha256: str
    source_map_corpus_sha256: str
    source_profile_selection_sha256: str
    source_map_inventory_sha256: str
    source_map_evidence_sha256: str
    request_sha256: str
    identity_sha256: str
    manifest_sha256: str | None
    entries: tuple[MapNativeEntry, ...]
    handoffs: tuple[MapNativeHandoff, ...]
    outputs: tuple[MapNativeOutput, ...]
    published: bool
    reused: bool

    @property
    def complete(self) -> bool:
        return (
            self.published
            and all(item.accepted for item in self.entries)
            and all(item.handed_off for item in self.handoffs)
        )

    @property
    def selected_bytes(self) -> int:
        return sum(item.source_bytes for item in self.entries)

    @property
    def candidate_bytes(self) -> int:
        return self.selected_bytes + sum(
            item.source_bytes for item in self.handoffs
        )

    @property
    def output_bytes(self) -> int:
        return sum(item.byte_length for item in self.outputs)

    def neutral(self) -> dict[str, object]:
        """Return the canonical payload-free report without local paths."""

        basis = _document_basis(
            source_manifest_sha256=self.source_manifest_sha256,
            source_manifest_aggregate_sha256=self.source_manifest_aggregate_sha256,
            source_map_corpus_sha256=self.source_map_corpus_sha256,
            source_profile_selection_sha256=self.source_profile_selection_sha256,
            source_map_inventory_sha256=self.source_map_inventory_sha256,
            source_map_evidence_sha256=self.source_map_evidence_sha256,
            request_sha256=self.request_sha256,
            entries=self.entries,
            handoffs=self.handoffs,
            outputs=self.outputs,
            published=self.published,
        )
        return {**basis, "identitySha256": self.identity_sha256}

    json_ready = neutral


class MapNativeCorpusBuildError(MapNativeCorpusError):
    """Raised after complete bounded processing when any map was rejected."""

    def __init__(self, report: MapNativeCorpusReport):
        rejected = sum(not item.accepted for item in report.entries) + sum(
            not item.handed_off for item in report.handoffs
        )
        if report.published or rejected == 0:
            raise ValueError(
                "map-native build error requires an unpublished rejection report"
            )
        self.report = report
        super().__init__(
            "map-native corpus rejected "
            f"{rejected} candidate artifact(s); no output was published"
        )


class _DuplicateJsonKey(ValueError):
    pass


def _json_clone(value: Any) -> Any:
    return json.loads(
        json.dumps(
            value,
            allow_nan=False,
            ensure_ascii=True,
            sort_keys=True,
            separators=(",", ":"),
        )
    )


def _canonical_json_bytes(value: object, *, pretty: bool = False) -> bytes:
    options: dict[str, Any] = {
        "allow_nan": False,
        "ensure_ascii": True,
        "sort_keys": True,
    }
    if pretty:
        options["indent"] = 2
    else:
        options["separators"] = (",", ":")
    return (json.dumps(value, **options) + "\n").encode("utf-8")


def _canonical_sha256(value: object) -> str:
    return hashlib.sha256(_canonical_json_bytes(value)).hexdigest()


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and value == value.casefold()
        and all(character in _SHA256_CHARACTERS for character in value)
    )


def _is_int(value: object, *, minimum: int = 0) -> bool:
    return not isinstance(value, bool) and isinstance(value, int) and value >= minimum


def _structural_status(runnable: bool) -> str:
    return "runnable-structure" if runnable else "non-runnable-structural-map"


def _profile_fields(
    raw: Mapping[str, Any], *, label: str
) -> tuple[str, int, bool, str]:
    profile = raw.get("mapKind")
    version = raw.get("profileVersion")
    runnable = raw.get("runnable")
    structural_status = raw.get("structuralStatus")
    if (
        not isinstance(profile, str)
        or profile not in MAP_PROFILES
        or version != SAGE_MAP_PROFILE_VERSION
        or isinstance(version, bool)
        or not isinstance(version, int)
        or not isinstance(runnable, bool)
        or runnable is not _PROFILE_RUNNABLE[profile]
        or structural_status != _structural_status(runnable)
    ):
        raise MapNativeCorpusError(f"{label} profile evidence is invalid")
    return profile, version, runnable, structural_status


def _discovery_reasons(raw: object, *, label: str) -> tuple[str, ...]:
    if (
        not isinstance(raw, list)
        or not raw
        or any(not isinstance(item, str) for item in raw)
    ):
        raise MapNativeCorpusError(f"{label} discovery evidence is invalid")
    reasons = tuple(raw)
    canonical = tuple(
        reason for reason in MAP_DISCOVERY_REASON_ORDER if reason in reasons
    )
    if (
        reasons != canonical
        or len(reasons) != len(set(reasons))
        or any(reason not in MAP_DISCOVERY_REASONS for reason in reasons)
        or {"ear-signature", "ckmp-signature"}.issubset(reasons)
    ):
        raise MapNativeCorpusError(f"{label} discovery evidence is invalid")
    return reasons


def _verify_backtest_profile(
    evidence: Mapping[str, Any],
    expected: tuple[str, int, bool, str],
) -> None:
    facts = evidence.get("facts")
    if not isinstance(facts, dict):
        raise MapNativeCorpusError("native backtest profile evidence is missing")
    profile, version, runnable, _structural = expected
    if (
        facts.get("mapKind") != profile
        or facts.get("profileVersion") != version
        or facts.get("runnable") is not runnable
        or evidence.get("runnable") is not runnable
        or evidence.get("gameplayFidelityClaimed") is not False
    ):
        raise MapNativeCorpusError("native backtest profile evidence disagrees")


def _is_link_like(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(is_junction and is_junction())


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
            raise MapNativeCorpusError(
                f"{context} link state cannot be inspected"
            ) from exc
        if linked:
            raise MapNativeCorpusError(f"{context} is linked: {current}")


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
        raise TypeError(
            "map-native corpus output root must be a filesystem path"
        ) from exc
    absolute = Path(os.path.abspath(candidate))
    if not absolute.name:
        raise MapNativeCorpusError(
            "map-native corpus output root cannot be a filesystem anchor"
        )
    _refuse_link_chain(absolute.parent, context="map-native corpus output parent")
    try:
        absolute.parent.mkdir(parents=True, exist_ok=True)
        _refuse_link_chain(absolute.parent, context="map-native corpus output parent")
        parent = absolute.parent.resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise MapNativeCorpusError(
            "map-native corpus output parent is unavailable"
        ) from exc
    if not parent.is_dir() or _is_link_like(parent):
        raise MapNativeCorpusError(
            "map-native corpus output parent is not an unlinked directory"
        )
    output = parent / absolute.name
    if _paths_overlap(source_root, output):
        raise MapNativeCorpusError(
            "effective-assets root and map-native corpus output root must not overlap"
        )
    if os.path.lexists(output):
        if _is_link_like(output):
            raise MapNativeCorpusError(
                "map-native corpus output root must not be linked"
            )
        if not output.is_dir():
            raise MapNativeCorpusError(
                "map-native corpus output root is not a directory"
            )
    return output


def _safe_relative(value: str, *, label: str) -> str:
    try:
        parts = safe_relative_parts(value)
    except (TypeError, ValueError) as exc:
        raise MapNativeCorpusError(f"{label} has an unsafe path") from exc
    canonical = "/".join(parts)
    if canonical != value:
        raise MapNativeCorpusError(f"{label} path is not canonical")
    return canonical


def _safe_child(root: Path, relative: str, *, label: str) -> Path:
    canonical = _safe_relative(relative, label=label)
    target = root.joinpath(*PurePosixPath(canonical).parts)
    try:
        target.relative_to(root)
    except ValueError as exc:
        raise MapNativeCorpusError(f"{label} escaped its root") from exc
    return target


def _output_relative(source_sha256: str, profile: str, profile_version: int) -> str:
    return (
        f"maps/sha256/{source_sha256[:2]}/{source_sha256}/"
        f"profile/{profile}-v{profile_version}"
    )


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


def _read_strict_json(
    path: Path, maximum: int, *, label: str
) -> tuple[dict[str, Any], bytes]:
    try:
        if _is_link_like(path) or not path.is_file():
            raise MapNativeCorpusError(f"{label} is missing, linked, or not a file")
        before = path.stat()
        if not 1 <= before.st_size <= maximum:
            raise MapNativeCorpusLimitError(f"{label} exceeds its safety bound")
        raw = path.read_bytes()
        after = path.stat()
    except MapNativeCorpusError:
        raise
    except OSError as exc:
        raise MapNativeCorpusError(f"{label} cannot be read") from exc
    if (
        len(raw) != before.st_size
        or after.st_size != before.st_size
        or after.st_mtime_ns != before.st_mtime_ns
    ):
        raise MapNativeCorpusError(f"{label} changed during read")
    try:
        value = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_object_without_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise MapNativeCorpusError(f"{label} is invalid JSON") from exc
    if not isinstance(value, dict):
        raise MapNativeCorpusError(f"{label} root is not an object")
    return value, raw


def _hash_file(path: Path, *, label: str) -> tuple[int, str]:
    digest = hashlib.sha256()
    size = 0
    try:
        if _is_link_like(path) or not path.is_file():
            raise MapNativeCorpusError(f"{label} is missing, linked, or not a file")
        before = path.stat()
        with path.open("rb") as stream:
            while True:
                block = stream.read(HASH_BLOCK_BYTES)
                if not block:
                    break
                digest.update(block)
                size += len(block)
        after = path.stat()
    except MapNativeCorpusError:
        raise
    except OSError as exc:
        raise MapNativeCorpusError(f"{label} cannot be read") from exc
    if (
        size != before.st_size
        or after.st_size != before.st_size
        or after.st_mtime_ns != before.st_mtime_ns
    ):
        raise MapNativeCorpusError(f"{label} changed during read")
    return size, digest.hexdigest()


def _scan_files(root: Path, *, label: str) -> tuple[dict[str, Path], set[str]]:
    files: dict[str, Path] = {}
    directories: set[str] = set()
    pending = [root]
    while pending:
        directory = pending.pop()
        try:
            entries = sorted(
                os.scandir(directory),
                key=lambda item: (item.name.casefold(), item.name),
            )
        except OSError as exc:
            raise MapNativeCorpusError(f"{label} cannot be enumerated") from exc
        for entry in entries:
            path = Path(entry.path)
            relative = path.relative_to(root).as_posix()
            if entry.is_symlink() or _is_link_like(path):
                raise MapNativeCorpusError(f"{label} contains a link")
            key = relative.casefold()
            if key in files or key in directories:
                raise MapNativeCorpusError(f"{label} contains case-colliding paths")
            try:
                if entry.is_file(follow_symlinks=False):
                    files[key] = path
                elif entry.is_dir(follow_symlinks=False):
                    directories.add(key)
                    pending.append(path)
                else:
                    raise MapNativeCorpusError(
                        f"{label} contains an unsupported filesystem entry"
                    )
            except OSError as exc:
                raise MapNativeCorpusError(
                    f"{label} entry cannot be inspected"
                ) from exc
    return files, directories


def _expected_directories(paths: Iterable[str]) -> set[str]:
    result: set[str] = set()
    for value in paths:
        parts = PurePosixPath(value).parts
        for index in range(1, len(parts)):
            result.add("/".join(parts[:index]).casefold())
    return result


def _tree_sha256(inventory: Sequence[MapNativeArtifact]) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.map-native-output-tree",
            "schemaVersion": 0,
            "files": [item.neutral() for item in inventory],
        }
    )


def _inventory_directory(root: Path) -> tuple[MapNativeArtifact, ...]:
    files, directories = _scan_files(root, label="cooked map directory")
    rows: list[MapNativeArtifact] = []
    for key in sorted(files, key=lambda value: (value.casefold(), value)):
        path = files[key]
        relative = path.relative_to(root).as_posix()
        byte_length, digest = _hash_file(path, label="cooked map artifact")
        rows.append(MapNativeArtifact(relative, byte_length, digest))
    inventory = tuple(rows)
    if not inventory:
        raise MapNativeCorpusError("cooked map directory contains no artifacts")
    declared_directories = _expected_directories(item.path for item in inventory)
    if directories != declared_directories:
        raise MapNativeCorpusError(
            "cooked map directory contains undeclared directories"
        )
    return inventory


def _resolution_summary(cooked_root: Path) -> dict[str, object]:
    document, _ = _read_strict_json(
        cooked_root / "map.json",
        MAX_MAP_NATIVE_MANIFEST_BYTES,
        label="cooked map descriptor",
    )
    summary = document.get("objectResolution")
    if not isinstance(summary, dict) or set(summary) != _OBJECT_RESOLUTION_KEYS:
        raise MapNativeCorpusError("cooked map object-resolution summary is invalid")
    status = summary.get("resolutionStatus")
    integer_keys = _OBJECT_RESOLUTION_KEYS - {"resolutionStatus"}
    if status not in {"complete", "partial"} or not all(
        _is_int(summary.get(key)) for key in integer_keys
    ):
        raise MapNativeCorpusError("cooked map object-resolution summary is invalid")
    if (
        summary["resolvedTypeCount"]
        != summary["boundTypeCount"] + summary["logicalTypeCount"]
        or summary["resolvedPlacementCount"]
        != summary["boundPlacementCount"] + summary["logicalPlacementCount"]
        or summary["typeCount"]
        != summary["resolvedTypeCount"] + summary["unresolvedTypeCount"]
        or summary["placementCount"]
        != summary["resolvedPlacementCount"] + summary["unresolvedPlacementCount"]
        or (status == "complete") != (summary["unresolvedTypeCount"] == 0)
    ):
        raise MapNativeCorpusError(
            "cooked map object-resolution totals are inconsistent"
        )
    return _json_clone(summary)


def _compact_backtest_rejection(evidence: object) -> str:
    try:
        return _canonical_sha256(
            {
                "schema": "openbfme.map-native-backtest-rejection",
                "schemaVersion": 0,
                "evidence": _json_clone(evidence),
            }
        )
    except (TypeError, ValueError, UnicodeError):
        return _canonical_sha256(
            {
                "schema": "openbfme.map-native-backtest-rejection",
                "schemaVersion": 0,
                "evidenceContract": "not-json-ready",
            }
        )


def _rejection_hash(code: str, token: object) -> str:
    normalized = " ".join(str(token).split())[:512]
    return _canonical_sha256(
        {
            "schema": "openbfme.map-native-rejection",
            "schemaVersion": 0,
            "code": code,
            "token": normalized,
        }
    )


def _handoff_evidence_sha256(
    item: MapCorpusFile,
    *,
    status: str,
    rejection_code: str | None,
    rejection_evidence_sha256: str | None,
) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.map-native-handoff-evidence",
            "schemaVersion": 0,
            "sourcePath": item.virtual_path,
            "sourceBytes": item.byte_length,
            "sourceSha256": item.source_sha256,
            "discoveryReasons": list(item.discovery_reasons),
            "artifactKind": item.artifact_kind,
            "classificationEvidenceSha256": item.classification_evidence_sha256,
            "chunkNameSetSha256": item.chunk_name_set_sha256,
            "censusEvidenceSha256": item.census_evidence_sha256,
            "status": status,
            "rejectionCode": rejection_code,
            "rejectionEvidenceSha256": rejection_evidence_sha256,
            "conversionDisposition": "no-native-output",
        }
    )


def _handoff_from_candidate(
    item: MapCorpusFile,
    *,
    verification_code: str | None = None,
    verification_evidence_sha256: str | None = None,
) -> MapNativeHandoff:
    if verification_code is not None:
        status = "rejected"
        rejection_code = verification_code
        rejection_evidence = verification_evidence_sha256 or _rejection_hash(
            verification_code, "missing-verification-evidence"
        )
    elif item.artifact_kind == SCRIPT_CONTAINER_ARTIFACT:
        status = "handed-off"
        rejection_code = None
        rejection_evidence = None
    else:
        status = "rejected"
        rejection_code = f"map-corpus:{item.rejection_code or 'unclassified'}"
        rejection_evidence = item.rejection_evidence_sha256 or _rejection_hash(
            rejection_code, item.census_evidence_sha256
        )
    handoff_evidence = _handoff_evidence_sha256(
        item,
        status=status,
        rejection_code=rejection_code,
        rejection_evidence_sha256=rejection_evidence,
    )
    return MapNativeHandoff(
        source_path=item.virtual_path,
        source_bytes=item.byte_length,
        source_sha256=item.source_sha256,
        discovery_reasons=item.discovery_reasons,
        artifact_kind=item.artifact_kind,
        classification_evidence_sha256=item.classification_evidence_sha256,
        chunk_name_set_sha256=item.chunk_name_set_sha256,
        census_evidence_sha256=item.census_evidence_sha256,
        body_sha256=item.body_sha256,
        status=status,
        rejection_code=rejection_code,
        rejection_evidence_sha256=rejection_evidence,
        handoff_evidence_sha256=handoff_evidence,
    )


def _entry_from_census_rejection(item: MapCorpusFile) -> MapNativeEntry:
    code = f"map-corpus:{item.rejection_code or 'rejected'}"
    return MapNativeEntry(
        source_path=item.virtual_path,
        source_bytes=item.byte_length,
        source_sha256=item.source_sha256,
        discovery_reasons=item.discovery_reasons,
        profile=item.profile,
        profile_version=item.profile_version,
        runnable=item.runnable,
        structural_status=item.structural_status,
        census_evidence_sha256=item.census_evidence_sha256,
        body_sha256=item.body_sha256,
        status="rejected",
        rejection_code=code,
        rejection_evidence_sha256=(
            item.rejection_evidence_sha256
            or _rejection_hash(code, item.census_evidence_sha256)
        ),
        output_path=None,
    )


def _rejected_entry(
    item: MapCorpusFile, *, code: str, evidence_sha256: str
) -> MapNativeEntry:
    return MapNativeEntry(
        source_path=item.virtual_path,
        source_bytes=item.byte_length,
        source_sha256=item.source_sha256,
        discovery_reasons=item.discovery_reasons,
        profile=item.profile,
        profile_version=item.profile_version,
        runnable=item.runnable,
        structural_status=item.structural_status,
        census_evidence_sha256=item.census_evidence_sha256,
        body_sha256=item.body_sha256,
        status="rejected",
        rejection_code=code,
        rejection_evidence_sha256=evidence_sha256,
        output_path=None,
    )


def _accepted_entry(item: MapCorpusFile, output_path: str) -> MapNativeEntry:
    return MapNativeEntry(
        source_path=item.virtual_path,
        source_bytes=item.byte_length,
        source_sha256=item.source_sha256,
        discovery_reasons=item.discovery_reasons,
        profile=item.profile,
        profile_version=item.profile_version,
        runnable=item.runnable,
        structural_status=item.structural_status,
        census_evidence_sha256=item.census_evidence_sha256,
        body_sha256=item.body_sha256,
        status="accepted",
        rejection_code=None,
        rejection_evidence_sha256=None,
        output_path=output_path,
    )


def _copy_verified_source(
    source_root: Path,
    item: MapCorpusFile,
    target: Path,
) -> tuple[str | None, str | None]:
    source = _safe_child(
        source_root, item.virtual_path, label="manifest-declared source map"
    )
    try:
        _refuse_link_chain(source, context="manifest-declared source map")
        if _is_link_like(source) or not source.is_file():
            return "source-unavailable", _rejection_hash(
                "source-unavailable", "missing-linked-or-not-file"
            )
        before = source.stat()
        digest = hashlib.sha256()
        copied = 0
        with source.open("rb") as input_stream, target.open("xb") as output_stream:
            while True:
                block = input_stream.read(HASH_BLOCK_BYTES)
                if not block:
                    break
                output_stream.write(block)
                digest.update(block)
                copied += len(block)
        after = source.stat()
        linked_after = _is_link_like(source)
    except (OSError, MapNativeCorpusError):
        target.unlink(missing_ok=True)
        return "source-read-failed", _rejection_hash(
            "source-read-failed", "copy-or-link-check-failed"
        )
    if (
        linked_after
        or copied != item.byte_length
        or copied != before.st_size
        or after.st_size != before.st_size
        or after.st_mtime_ns != before.st_mtime_ns
    ):
        target.unlink(missing_ok=True)
        return "source-changed", _rejection_hash(
            "source-changed", "size-timestamp-or-link-state"
        )
    if digest.hexdigest() != item.source_sha256:
        target.unlink(missing_ok=True)
        return "source-sha256-mismatch", _rejection_hash(
            "source-sha256-mismatch", digest.hexdigest()
        )
    return None, None


def _request_sha256(report: MapCorpusReport) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.map-native-corpus-request",
            "schemaVersion": 0,
            "sourceManifestSha256": report.manifest_sha256,
            "sourceManifestAggregateSha256": report.manifest_aggregate_sha256,
            "sourceMapCorpusSha256": report.corpus_sha256,
            "sourceProfileSelectionSha256": report.profile_selection_sha256,
            "sourceMapInventorySha256": report.map_inventory_sha256,
            "sourceMapEvidenceSha256": report.map_evidence_sha256,
            "candidateArtifacts": [
                {
                    "sourcePath": item.virtual_path,
                    "sourceBytes": item.byte_length,
                    "sourceSha256": item.source_sha256,
                    "discoveryReasons": list(item.discovery_reasons),
                    "artifactKind": item.artifact_kind,
                    "classificationEvidenceSha256": (
                        item.classification_evidence_sha256
                    ),
                    "chunkNameSetSha256": item.chunk_name_set_sha256,
                    "censusEvidenceSha256": item.census_evidence_sha256,
                    "censusStatus": item.status,
                    "censusRejectionCode": item.rejection_code,
                }
                for item in report.artifacts
            ],
            "maps": [
                {
                    "sourcePath": item.virtual_path,
                    "sourceBytes": item.byte_length,
                    "sourceSha256": item.source_sha256,
                    "discoveryReasons": list(item.discovery_reasons),
                    "mapKind": item.profile,
                    "profileVersion": item.profile_version,
                    "runnable": item.runnable,
                    "structuralStatus": item.structural_status,
                    "censusEvidenceSha256": item.census_evidence_sha256,
                    "censusStatus": item.status,
                    "censusRejectionCode": item.rejection_code,
                }
                for item in report.maps
            ],
        }
    )


def _summary(
    entries: Sequence[MapNativeEntry],
    handoffs: Sequence[MapNativeHandoff],
    outputs: Sequence[MapNativeOutput],
    *,
    published: bool,
) -> dict[str, object]:
    output_lookup = {item.path: item for item in outputs}
    unresolved_maps = 0
    unresolved_types = 0
    unresolved_placements = 0
    for entry in entries:
        output = output_lookup.get(entry.output_path or "")
        if output is None:
            continue
        type_count = int(output.object_resolution["unresolvedTypeCount"])
        placement_count = int(output.object_resolution["unresolvedPlacementCount"])
        if type_count:
            unresolved_maps += 1
        unresolved_types += type_count
        unresolved_placements += placement_count
    accepted = sum(item.accepted for item in entries)
    rejected = len(entries) - accepted
    runnable = sum(item.runnable for item in entries)
    candidates: tuple[MapNativeEntry | MapNativeHandoff, ...] = (
        *entries,
        *handoffs,
    )
    map_suffix_discoveries = sum(
        "map-suffix" in item.discovery_reasons for item in candidates
    )
    ear_discoveries = sum(
        "ear-signature" in item.discovery_reasons for item in candidates
    )
    ckmp_discoveries = sum(
        "ckmp-signature" in item.discovery_reasons for item in candidates
    )
    script_handoffs = tuple(
        item
        for item in handoffs
        if item.artifact_kind == SCRIPT_CONTAINER_ARTIFACT
    )
    source_accounting_complete = all(item.handed_off for item in handoffs)
    return {
        "candidateArtifactCount": len(candidates),
        "candidateArtifactBytes": sum(item.source_bytes for item in candidates),
        "selectedMapCount": len(entries),
        "selectedMapBytes": sum(item.source_bytes for item in entries),
        "scriptContainerCount": len(script_handoffs),
        "scriptContainerBytes": sum(
            item.source_bytes for item in script_handoffs
        ),
        "scriptContainerHandoffCount": sum(
            item.handed_off for item in script_handoffs
        ),
        "unclassifiedCandidateCount": sum(
            item.artifact_kind == UNCLASSIFIED_CANDIDATE_ARTIFACT
            for item in handoffs
        ),
        "handoffEvidenceSha256": _canonical_sha256(
            {
                "schema": "openbfme.map-native-handoff-set",
                "schemaVersion": 0,
                "handoffs": [
                    item.handoff_evidence_sha256 for item in handoffs
                ],
            }
        ),
        "mapSuffixDiscoveryCount": map_suffix_discoveries,
        "earSignatureDiscoveryCount": ear_discoveries,
        "ckmpSignatureDiscoveryCount": ckmp_discoveries,
        "signatureDiscoveredMapCount": sum(
            bool({"ear-signature", "ckmp-signature"} & set(item.discovery_reasons))
            for item in entries
        ),
        "signatureDiscoveredCandidateCount": sum(
            bool({"ear-signature", "ckmp-signature"} & set(item.discovery_reasons))
            for item in candidates
        ),
        "acceptedMapCount": accepted,
        "rejectedMapCount": rejected,
        "runnableMapCount": runnable,
        "nonRunnableMapCount": len(entries) - runnable,
        "uniqueOutputMapCount": len(outputs),
        "outputFileCount": sum(len(item.inventory) for item in outputs),
        "outputBytes": sum(item.byte_length for item in outputs),
        "mapsWithUnresolvedObjectBindings": unresolved_maps,
        "unresolvedObjectTypeCount": unresolved_types,
        "unresolvedObjectPlacementCount": unresolved_placements,
        "structuralConversionComplete": rejected == 0,
        "sourceAccountingComplete": source_accounting_complete,
        "objectBindingSemanticsComplete": unresolved_types == 0,
        "gameplayFidelityClaimed": False,
        "gameplaySemanticFidelityClaimed": False,
        "published": published,
    }


def _document_basis(
    *,
    source_manifest_sha256: str,
    source_manifest_aggregate_sha256: str,
    source_map_corpus_sha256: str,
    source_profile_selection_sha256: str,
    source_map_inventory_sha256: str,
    source_map_evidence_sha256: str,
    request_sha256: str,
    entries: Sequence[MapNativeEntry],
    handoffs: Sequence[MapNativeHandoff],
    outputs: Sequence[MapNativeOutput],
    published: bool,
) -> dict[str, object]:
    return {
        "schema": MAP_NATIVE_CORPUS_SCHEMA,
        "schemaVersion": MAP_NATIVE_CORPUS_SCHEMA_VERSION,
        "selection": {
            "sourceManifestSha256": source_manifest_sha256,
            "sourceManifestAggregateSha256": source_manifest_aggregate_sha256,
            "sourceMapCorpusSha256": source_map_corpus_sha256,
            "sourceProfileSelectionSha256": source_profile_selection_sha256,
            "sourceMapInventorySha256": source_map_inventory_sha256,
            "sourceMapEvidenceSha256": source_map_evidence_sha256,
            "requestSha256": request_sha256,
        },
        "summary": _summary(entries, handoffs, outputs, published=published),
        "entries": [item.neutral() for item in entries],
        "handoffs": [item.neutral() for item in handoffs],
        "outputs": [item.neutral() for item in outputs],
    }


def _make_report(
    source_report: MapCorpusReport,
    output_root: Path,
    request_sha256: str,
    entries: Sequence[MapNativeEntry],
    handoffs: Sequence[MapNativeHandoff],
    outputs: Sequence[MapNativeOutput],
    *,
    published: bool,
    reused: bool,
    manifest_sha256: str | None = None,
) -> MapNativeCorpusReport:
    entries_tuple = tuple(entries)
    handoffs_tuple = tuple(handoffs)
    outputs_tuple = tuple(outputs)
    basis = _document_basis(
        source_manifest_sha256=source_report.manifest_sha256,
        source_manifest_aggregate_sha256=source_report.manifest_aggregate_sha256,
        source_map_corpus_sha256=source_report.corpus_sha256,
        source_profile_selection_sha256=source_report.profile_selection_sha256,
        source_map_inventory_sha256=source_report.map_inventory_sha256,
        source_map_evidence_sha256=source_report.map_evidence_sha256,
        request_sha256=request_sha256,
        entries=entries_tuple,
        handoffs=handoffs_tuple,
        outputs=outputs_tuple,
        published=published,
    )
    return MapNativeCorpusReport(
        source_root=source_report.asset_root,
        output_root=output_root,
        manifest_path=output_root / MAP_NATIVE_CORPUS_MANIFEST,
        source_manifest_sha256=source_report.manifest_sha256,
        source_manifest_aggregate_sha256=source_report.manifest_aggregate_sha256,
        source_map_corpus_sha256=source_report.corpus_sha256,
        source_profile_selection_sha256=source_report.profile_selection_sha256,
        source_map_inventory_sha256=source_report.map_inventory_sha256,
        source_map_evidence_sha256=source_report.map_evidence_sha256,
        request_sha256=request_sha256,
        identity_sha256=_canonical_sha256(basis),
        manifest_sha256=manifest_sha256,
        entries=entries_tuple,
        handoffs=handoffs_tuple,
        outputs=outputs_tuple,
        published=published,
        reused=reused,
    )


def _verify_manifest_unchanged(report: MapCorpusReport) -> None:
    try:
        _refuse_link_chain(report.manifest_path, context="effective-assets manifest")
        _, digest = _hash_file(report.manifest_path, label="effective-assets manifest")
    except MapNativeCorpusError:
        raise
    if digest != report.manifest_sha256:
        raise MapNativeCorpusError(
            "effective-assets manifest changed during map-native conversion"
        )


def _parse_artifact(raw: object) -> MapNativeArtifact:
    if not isinstance(raw, dict) or set(raw) != {"path", "bytes", "sha256"}:
        raise MapNativeCorpusError("map-native artifact inventory entry is invalid")
    path = raw.get("path")
    byte_length = raw.get("bytes")
    digest = raw.get("sha256")
    if not isinstance(path, str) or not _is_int(byte_length) or not _is_sha256(digest):
        raise MapNativeCorpusError("map-native artifact inventory entry is invalid")
    _safe_relative(path, label="map-native artifact")
    return MapNativeArtifact(path, byte_length, digest)


def _parse_resolution(raw: object) -> dict[str, object]:
    if not isinstance(raw, dict) or set(raw) != _OBJECT_RESOLUTION_KEYS:
        raise MapNativeCorpusError("map-native object-resolution evidence is invalid")
    status = raw.get("resolutionStatus")
    integer_keys = _OBJECT_RESOLUTION_KEYS - {"resolutionStatus"}
    if status not in {"complete", "partial"} or not all(
        _is_int(raw.get(key)) for key in integer_keys
    ):
        raise MapNativeCorpusError("map-native object-resolution evidence is invalid")
    return _json_clone(raw)


def _parse_output(raw: object, root: Path) -> MapNativeOutput:
    expected = {
        "path",
        "sourceSha256",
        "mapKind",
        "profileVersion",
        "runnable",
        "structuralStatus",
        "fileCount",
        "bytes",
        "treeSha256",
        "inventory",
        "backtestEvidence",
        "objectResolution",
    }
    if not isinstance(raw, dict) or set(raw) != expected:
        raise MapNativeCorpusError("map-native output entry has an invalid shape")
    profile = _profile_fields(raw, label="map-native output")
    relative = raw.get("path")
    source_sha256 = raw.get("sourceSha256")
    tree_sha256 = raw.get("treeSha256")
    raw_inventory = raw.get("inventory")
    if (
        not isinstance(relative, str)
        or not _is_sha256(source_sha256)
        or relative != _output_relative(source_sha256, profile[0], profile[1])
        or not _is_sha256(tree_sha256)
        or not isinstance(raw_inventory, list)
    ):
        raise MapNativeCorpusError("map-native output entry has invalid metadata")
    inventory = tuple(_parse_artifact(item) for item in raw_inventory)
    paths = [item.path for item in inventory]
    if (
        not inventory
        or paths != sorted(paths, key=lambda value: (value.casefold(), value))
        or len({item.casefold() for item in paths}) != len(paths)
        or raw.get("fileCount") != len(inventory)
        or raw.get("bytes") != sum(item.byte_length for item in inventory)
        or tree_sha256 != _tree_sha256(inventory)
    ):
        raise MapNativeCorpusError("map-native output inventory is invalid")
    output_root = _safe_child(root, relative, label="map-native output")
    if _is_link_like(output_root) or not output_root.is_dir():
        raise MapNativeCorpusError("map-native output directory is missing or linked")
    actual = _inventory_directory(output_root)
    if actual != inventory:
        raise MapNativeCorpusError("map-native output artifact identity changed")
    evidence = validate_cooked_sage_map(output_root)
    if not isinstance(evidence, dict) or evidence.get("valid") is not True:
        raise MapNativeCorpusError("map-native output failed its native backtest")
    _verify_backtest_profile(evidence, profile)
    if _json_clone(evidence) != raw.get("backtestEvidence"):
        raise MapNativeCorpusError("map-native output backtest evidence changed")
    resolution = _resolution_summary(output_root)
    if resolution != raw.get("objectResolution"):
        raise MapNativeCorpusError("map-native object-resolution evidence changed")
    return MapNativeOutput(
        path=relative,
        source_sha256=source_sha256,
        profile=profile[0],
        profile_version=profile[1],
        runnable=profile[2],
        structural_status=profile[3],
        inventory=inventory,
        tree_sha256=tree_sha256,
        backtest_evidence=_json_clone(evidence),
        object_resolution=resolution,
    )


def _parse_entry(raw: object, outputs: Mapping[str, MapNativeOutput]) -> MapNativeEntry:
    expected = {
        "sourcePath",
        "sourceBytes",
        "sourceSha256",
        "discoveryReasons",
        "mapKind",
        "profileVersion",
        "runnable",
        "structuralStatus",
        "censusEvidenceSha256",
        "bodySha256",
        "status",
        "rejectionCode",
        "rejectionEvidenceSha256",
        "outputPath",
    }
    if not isinstance(raw, dict) or set(raw) != expected:
        raise MapNativeCorpusError("map-native source entry has an invalid shape")
    profile = _profile_fields(raw, label="map-native source entry")
    source_path = raw.get("sourcePath")
    source_bytes = raw.get("sourceBytes")
    source_sha256 = raw.get("sourceSha256")
    discovery_reasons = _discovery_reasons(
        raw.get("discoveryReasons"), label="map-native source entry"
    )
    census_sha256 = raw.get("censusEvidenceSha256")
    body_sha256 = raw.get("bodySha256")
    status = raw.get("status")
    output_path = raw.get("outputPath")
    if (
        not isinstance(source_path, str)
        or not _is_int(source_bytes)
        or not _is_sha256(source_sha256)
        or not _is_sha256(census_sha256)
        or not _is_sha256(body_sha256)
        or status != "accepted"
        or raw.get("rejectionCode") is not None
        or raw.get("rejectionEvidenceSha256") is not None
        or not isinstance(output_path, str)
    ):
        raise MapNativeCorpusError("published map-native source entry is invalid")
    _safe_relative(source_path, label="map-native source")
    output = outputs.get(output_path.casefold())
    if (
        output is None
        or output.path != output_path
        or output.source_sha256 != source_sha256
        or (
            output.profile,
            output.profile_version,
            output.runnable,
            output.structural_status,
        )
        != profile
    ):
        raise MapNativeCorpusError("map-native source/output mapping is invalid")
    return MapNativeEntry(
        source_path=source_path,
        source_bytes=source_bytes,
        source_sha256=source_sha256,
        discovery_reasons=discovery_reasons,
        profile=profile[0],
        profile_version=profile[1],
        runnable=profile[2],
        structural_status=profile[3],
        census_evidence_sha256=census_sha256,
        body_sha256=body_sha256,
        status=status,
        rejection_code=None,
        rejection_evidence_sha256=None,
        output_path=output_path,
    )


def _parse_handoff(raw: object) -> MapNativeHandoff:
    expected = {
        "sourcePath",
        "sourceBytes",
        "sourceSha256",
        "discoveryReasons",
        "artifactKind",
        "classificationEvidenceSha256",
        "chunkNameSetSha256",
        "censusEvidenceSha256",
        "bodySha256",
        "status",
        "rejectionCode",
        "rejectionEvidenceSha256",
        "handoffEvidenceSha256",
        "outputPath",
    }
    if not isinstance(raw, dict) or set(raw) != expected:
        raise MapNativeCorpusError("map-native handoff entry has an invalid shape")
    source_path = raw.get("sourcePath")
    source_bytes = raw.get("sourceBytes")
    source_sha256 = raw.get("sourceSha256")
    artifact_kind = raw.get("artifactKind")
    classification_sha256 = raw.get("classificationEvidenceSha256")
    chunk_name_set_sha256 = raw.get("chunkNameSetSha256")
    census_sha256 = raw.get("censusEvidenceSha256")
    body_sha256 = raw.get("bodySha256")
    handoff_sha256 = raw.get("handoffEvidenceSha256")
    discovery_reasons = _discovery_reasons(
        raw.get("discoveryReasons"), label="map-native handoff"
    )
    if (
        not isinstance(source_path, str)
        or not _is_int(source_bytes)
        or not _is_sha256(source_sha256)
        or artifact_kind != SCRIPT_CONTAINER_ARTIFACT
        or not _is_sha256(classification_sha256)
        or not _is_sha256(chunk_name_set_sha256)
        or not _is_sha256(census_sha256)
        or not _is_sha256(body_sha256)
        or raw.get("status") != "handed-off"
        or raw.get("rejectionCode") is not None
        or raw.get("rejectionEvidenceSha256") is not None
        or not _is_sha256(handoff_sha256)
        or raw.get("outputPath") is not None
    ):
        raise MapNativeCorpusError("published map-native handoff is invalid")
    _safe_relative(source_path, label="map-native handoff source")
    return MapNativeHandoff(
        source_path=source_path,
        source_bytes=source_bytes,
        source_sha256=source_sha256,
        discovery_reasons=discovery_reasons,
        artifact_kind=artifact_kind,
        classification_evidence_sha256=classification_sha256,
        chunk_name_set_sha256=chunk_name_set_sha256,
        census_evidence_sha256=census_sha256,
        body_sha256=body_sha256,
        status="handed-off",
        rejection_code=None,
        rejection_evidence_sha256=None,
        handoff_evidence_sha256=handoff_sha256,
    )


def _verify_output(
    root: Path,
    source_report: MapCorpusReport,
    *,
    reused: bool,
) -> MapNativeCorpusReport:
    if _is_link_like(root) or not root.is_dir():
        raise MapNativeCorpusError(
            "map-native corpus output root is missing, linked, or not a directory"
        )
    manifest_path = root / MAP_NATIVE_CORPUS_MANIFEST
    document, raw_bytes = _read_strict_json(
        manifest_path,
        MAX_MAP_NATIVE_MANIFEST_BYTES,
        label="map-native corpus manifest",
    )
    if raw_bytes != _canonical_json_bytes(document, pretty=True):
        raise MapNativeCorpusError(
            "map-native corpus manifest encoding is not canonical"
        )
    expected_top = {
        "schema",
        "schemaVersion",
        "selection",
        "summary",
        "entries",
        "handoffs",
        "outputs",
        "identitySha256",
    }
    if (
        set(document) != expected_top
        or document.get("schema") != MAP_NATIVE_CORPUS_SCHEMA
        or document.get("schemaVersion") != MAP_NATIVE_CORPUS_SCHEMA_VERSION
        or isinstance(document.get("schemaVersion"), bool)
    ):
        raise MapNativeCorpusError("map-native corpus manifest contract is invalid")
    selection = document.get("selection")
    selection_keys = {
        "sourceManifestSha256",
        "sourceManifestAggregateSha256",
        "sourceMapCorpusSha256",
        "sourceProfileSelectionSha256",
        "sourceMapInventorySha256",
        "sourceMapEvidenceSha256",
        "requestSha256",
    }
    if (
        not isinstance(selection, dict)
        or set(selection) != selection_keys
        or not all(_is_sha256(selection.get(key)) for key in selection_keys)
    ):
        raise MapNativeCorpusError("map-native corpus selection is invalid")
    raw_outputs = document.get("outputs")
    raw_entries = document.get("entries")
    raw_handoffs = document.get("handoffs")
    if (
        not isinstance(raw_outputs, list)
        or not isinstance(raw_entries, list)
        or not isinstance(raw_handoffs, list)
    ):
        raise MapNativeCorpusError("map-native corpus inventories are invalid")
    outputs = tuple(_parse_output(item, root) for item in raw_outputs)
    output_paths = [item.path for item in outputs]
    if output_paths != sorted(
        output_paths, key=lambda value: (value.casefold(), value)
    ) or len({item.casefold() for item in output_paths}) != len(output_paths):
        raise MapNativeCorpusError("map-native output inventory is not canonical")
    output_lookup = {item.path.casefold(): item for item in outputs}
    entries = tuple(_parse_entry(item, output_lookup) for item in raw_entries)
    handoffs = tuple(_parse_handoff(item) for item in raw_handoffs)
    entry_paths = [item.source_path for item in entries]
    handoff_paths = [item.source_path for item in handoffs]
    if (
        not entries and not handoffs
    ) or (
        entry_paths
        != sorted(entry_paths, key=lambda value: (value.casefold(), value))
        or len({item.casefold() for item in entry_paths}) != len(entry_paths)
        or handoff_paths
        != sorted(handoff_paths, key=lambda value: (value.casefold(), value))
        or len({item.casefold() for item in handoff_paths}) != len(handoff_paths)
        or len(
            {item.casefold() for item in (*entry_paths, *handoff_paths)}
        )
        != len(entry_paths) + len(handoff_paths)
    ):
        raise MapNativeCorpusError("map-native source inventory is not canonical")
    if len(entries) != len(source_report.maps):
        raise MapNativeCorpusError(
            "map-native source inventory disagrees with the verified selection"
        )
    for entry, source in zip(entries, source_report.maps, strict=True):
        if (
            not source.accepted
            or entry.source_path != source.virtual_path
            or entry.source_bytes != source.byte_length
            or entry.source_sha256 != source.source_sha256
            or entry.discovery_reasons != source.discovery_reasons
            or entry.profile != source.profile
            or entry.profile_version != source.profile_version
            or entry.runnable is not source.runnable
            or entry.structural_status != source.structural_status
            or entry.census_evidence_sha256 != source.census_evidence_sha256
            or entry.body_sha256 != source.body_sha256
        ):
            raise MapNativeCorpusError(
                "map-native source entry disagrees with the verified selection"
            )
    if len(handoffs) != len(source_report.script_containers):
        raise MapNativeCorpusError(
            "map-native handoff inventory disagrees with the verified selection"
        )
    for handoff, source in zip(
        handoffs, source_report.script_containers, strict=True
    ):
        expected_handoff = _handoff_from_candidate(source)
        if handoff != expected_handoff:
            raise MapNativeCorpusError(
                "map-native handoff disagrees with the verified selection"
            )
    if len(entries) + len(handoffs) != len(source_report.artifacts):
        raise MapNativeCorpusError(
            "map-native candidate accounting omits a verified artifact"
        )
    if {item.output_path.casefold() for item in entries if item.output_path} != set(
        output_lookup
    ):
        raise MapNativeCorpusError("map-native corpus contains an unreferenced output")

    request_sha256 = _request_sha256(source_report)
    expected_selection = {
        "sourceManifestSha256": source_report.manifest_sha256,
        "sourceManifestAggregateSha256": source_report.manifest_aggregate_sha256,
        "sourceMapCorpusSha256": source_report.corpus_sha256,
        "sourceProfileSelectionSha256": source_report.profile_selection_sha256,
        "sourceMapInventorySha256": source_report.map_inventory_sha256,
        "sourceMapEvidenceSha256": source_report.map_evidence_sha256,
        "requestSha256": request_sha256,
    }
    if selection != expected_selection:
        raise MapNativeCorpusReuseError(
            "existing map-native corpus does not match the verified source selection"
        )
    if document.get("summary") != _summary(
        entries, handoffs, outputs, published=True
    ):
        raise MapNativeCorpusError("map-native corpus summary is inconsistent")
    basis = {key: value for key, value in document.items() if key != "identitySha256"}
    identity = document.get("identitySha256")
    if not _is_sha256(identity) or identity != _canonical_sha256(basis):
        raise MapNativeCorpusError("map-native corpus identity SHA-256 is invalid")

    files, directories = _scan_files(root, label="map-native corpus tree")
    declared = [
        MAP_NATIVE_CORPUS_MANIFEST,
        *[
            f"{output.path}/{artifact.path}"
            for output in outputs
            for artifact in output.inventory
        ],
    ]
    expected_files = {item.casefold() for item in declared}
    expected_directories = _expected_directories(declared)
    if set(files) != expected_files or directories != expected_directories:
        raise MapNativeCorpusError("map-native corpus tree disagrees with its manifest")
    return MapNativeCorpusReport(
        source_root=source_report.asset_root,
        output_root=root,
        manifest_path=manifest_path,
        source_manifest_sha256=source_report.manifest_sha256,
        source_manifest_aggregate_sha256=source_report.manifest_aggregate_sha256,
        source_map_corpus_sha256=source_report.corpus_sha256,
        source_profile_selection_sha256=source_report.profile_selection_sha256,
        source_map_inventory_sha256=source_report.map_inventory_sha256,
        source_map_evidence_sha256=source_report.map_evidence_sha256,
        request_sha256=request_sha256,
        identity_sha256=identity,
        manifest_sha256=hashlib.sha256(raw_bytes).hexdigest(),
        entries=entries,
        handoffs=handoffs,
        outputs=outputs,
        published=True,
        reused=reused,
    )


def _remove_owned_tree(path: Path, parent: Path, prefix: str) -> None:
    if not os.path.lexists(path):
        return
    if path.parent != parent or not path.name.startswith(prefix) or _is_link_like(path):
        raise MapNativeCorpusError(
            "refused to remove an unowned map-native transaction path"
        )
    _scan_files(path, label="map-native transaction tree")
    shutil.rmtree(path)


def _publish(stage: Path, destination: Path, backup: Path) -> None:
    parent = destination.parent
    had_destination = os.path.lexists(destination)
    if had_destination:
        _scan_files(destination, label="existing map-native corpus tree")
        os.replace(destination, backup)
    try:
        os.replace(stage, destination)
    except Exception as publish_error:
        if had_destination and os.path.lexists(backup):
            try:
                os.replace(backup, destination)
            except Exception as rollback_error:
                raise MapNativeCorpusError(
                    "map-native publish failed and rollback could not restore the prior output"
                ) from rollback_error
        raise MapNativeCorpusError(
            "map-native publish failed; prior output was preserved"
        ) from publish_error
    if had_destination:
        _remove_owned_tree(backup, parent, f".{destination.name}.backup-")


def _build_output(
    staged_source: Path,
    candidate: Path,
    item: MapCorpusFile,
) -> tuple[MapNativeOutput | None, str | None, str | None]:
    # Corpus conversion cannot inherit convert_sage_map's historical Fords
    # presentation defaults or infer a retail name from the path.  Bind the
    # generic descriptor to source bytes plus the explicit conversion profile.
    if item.profile == "multiplayer":
        metadata = {
            "id": f"openbfme.retail-map.{item.source_sha256}",
            "displayName": f"Retail map {item.source_sha256[:12]}",
        }
    else:
        metadata = {
            "id": (
                f"openbfme.content-map.{item.source_sha256}."
                f"{item.profile}.v{item.profile_version}"
            ),
            "displayName": (
                f"Content map {item.source_sha256[:12]} "
                f"[{item.profile} v{item.profile_version}]"
            ),
        }
    try:
        convert_sage_map(
            staged_source,
            candidate,
            metadata,
            {},
            None,
            profile=item.profile,
        )
    except SageMapError as exc:
        code = "sage-map-conversion-rejected"
        return (
            None,
            code,
            _rejection_hash(
                code, {"exception": type(exc).__name__, "message": str(exc)}
            ),
        )
    except OSError as exc:
        code = "sage-map-conversion-io-failed"
        return None, code, _rejection_hash(code, type(exc).__name__)

    evidence = validate_cooked_sage_map(candidate)
    if not isinstance(evidence, dict) or evidence.get("valid") is not True:
        return (
            None,
            "native-backtest-rejected",
            _compact_backtest_rejection(evidence),
        )
    try:
        _verify_backtest_profile(
            evidence,
            (
                item.profile,
                item.profile_version,
                item.runnable,
                item.structural_status,
            ),
        )
        inventory = _inventory_directory(candidate)
        resolution = _resolution_summary(candidate)
        cloned_evidence = _json_clone(evidence)
    except (MapNativeCorpusError, TypeError, ValueError) as exc:
        code = "native-output-contract-rejected"
        return None, code, _rejection_hash(code, type(exc).__name__)
    relative = _output_relative(item.source_sha256, item.profile, item.profile_version)
    return (
        MapNativeOutput(
            path=relative,
            source_sha256=item.source_sha256,
            profile=item.profile,
            profile_version=item.profile_version,
            runnable=item.runnable,
            structural_status=item.structural_status,
            inventory=inventory,
            tree_sha256=_tree_sha256(inventory),
            backtest_evidence=cloned_evidence,
            object_resolution=resolution,
        ),
        None,
        None,
    )


def build_map_native_corpus(
    effective_assets_root: Path | str,
    output_root: Path | str,
    *,
    profiles: Mapping[str, str] | None = None,
    force: bool = False,
    max_files: int | None = None,
    max_total_bytes: int | None = None,
) -> MapNativeCorpusReport:
    """Cook terrain maps and account non-terrain candidates atomically.

    The source tree is fully verified before conversion.  ``profiles`` uses
    exact canonical terrain-map paths; omitted maps remain strict multiplayer.
    Script/camera containers are SHA-verified handoffs and never enter the
    converter or output inventory.  Limits can lower but never raise the
    map-corpus hard bounds and never truncate the selection.
    Valid existing output is reused only when its complete source request,
    artifact inventory, hashes, and native backtests still match.  ``force``
    rebuilds through a sibling staging directory and preserves the old output
    until the replacement passes the same verification.
    """

    if not isinstance(force, bool):
        raise TypeError("map-native corpus force flag must be a boolean")
    try:
        source_report = scan_map_corpus(
            effective_assets_root,
            profiles=profiles,
            strict=False,
            max_files=max_files,
            max_total_bytes=max_total_bytes,
        )
    except MapCorpusLimitError as exc:
        raise MapNativeCorpusLimitError(str(exc)) from exc
    except MapCorpusError as exc:
        raise MapNativeCorpusError(str(exc)) from exc

    request_sha256 = _request_sha256(source_report)
    destination = _resolve_output_root(output_root, source_report.asset_root)
    if destination.is_dir() and not force:
        try:
            return _verify_output(destination, source_report, reused=True)
        except MapNativeCorpusReuseError:
            raise
        except MapNativeCorpusError as exc:
            raise MapNativeCorpusReuseError(
                f"existing map-native corpus failed verification: {exc}"
            ) from exc

    parent = destination.parent
    token = uuid.uuid4().hex
    stage = parent / f".{destination.name}.staging-{token}"
    backup = parent / f".{destination.name}.backup-{token}"
    try:
        stage.mkdir()
        work = stage / ".work"
        sources = work / "sources"
        candidates = work / "candidates"
        sources.mkdir(parents=True)
        candidates.mkdir()
    except OSError as exc:
        if os.path.lexists(stage):
            _remove_owned_tree(stage, parent, f".{destination.name}.staging-")
        raise MapNativeCorpusError(
            "map-native corpus staging directory could not be created"
        ) from exc

    entries: list[MapNativeEntry] = []
    handoffs: list[MapNativeHandoff] = []
    outputs_by_conversion: dict[tuple[str, str, int], MapNativeOutput] = {}
    rejections_by_conversion: dict[tuple[str, str, int], tuple[str, str]] = {}
    try:
        non_terrain = tuple(
            item for item in source_report.artifacts if not item.is_terrain_map
        )
        for index, item in enumerate(non_terrain):
            staged_source = sources / f"handoff-{index:08d}.payload"
            code, evidence_sha256 = _copy_verified_source(
                source_report.asset_root, item, staged_source
            )
            staged_source.unlink(missing_ok=True)
            handoffs.append(
                _handoff_from_candidate(
                    item,
                    verification_code=code,
                    verification_evidence_sha256=evidence_sha256,
                )
            )

        for index, item in enumerate(source_report.maps):
            if not item.accepted:
                entries.append(_entry_from_census_rejection(item))
                continue

            staged_source = sources / f"source-{index:08d}.payload"
            code, evidence_sha256 = _copy_verified_source(
                source_report.asset_root, item, staged_source
            )
            if code is not None or evidence_sha256 is not None:
                entries.append(
                    _rejected_entry(
                        item,
                        code=code or "source-verification-rejected",
                        evidence_sha256=evidence_sha256
                        or _rejection_hash("source-verification-rejected", "unknown"),
                    )
                )
                continue

            conversion_key = (
                item.source_sha256,
                item.profile,
                item.profile_version,
            )
            prior_output = outputs_by_conversion.get(conversion_key)
            if prior_output is not None:
                staged_source.unlink(missing_ok=True)
                entries.append(_accepted_entry(item, prior_output.path))
                continue
            prior_rejection = rejections_by_conversion.get(conversion_key)
            if prior_rejection is not None:
                staged_source.unlink(missing_ok=True)
                entries.append(
                    _rejected_entry(
                        item,
                        code=prior_rejection[0],
                        evidence_sha256=prior_rejection[1],
                    )
                )
                continue

            candidate = candidates / f"map-{index:08d}"
            output, rejection_code, rejection_sha256 = _build_output(
                staged_source, candidate, item
            )
            staged_source.unlink(missing_ok=True)
            if output is None:
                code = rejection_code or "map-conversion-rejected"
                evidence = rejection_sha256 or _rejection_hash(code, "unknown")
                rejections_by_conversion[conversion_key] = (code, evidence)
                entries.append(
                    _rejected_entry(item, code=code, evidence_sha256=evidence)
                )
                if os.path.lexists(candidate):
                    _remove_owned_tree(candidate, candidates, "map-")
                continue

            final_output = _safe_child(stage, output.path, label="map-native output")
            final_output.parent.mkdir(parents=True, exist_ok=True)
            if os.path.lexists(final_output):
                raise MapNativeCorpusError(
                    "content-addressed map output path already exists unexpectedly"
                )
            os.replace(candidate, final_output)
            outputs_by_conversion[conversion_key] = output
            entries.append(_accepted_entry(item, output.path))

        _verify_manifest_unchanged(source_report)
        outputs = tuple(
            sorted(
                outputs_by_conversion.values(),
                key=lambda item: (item.path.casefold(), item.path),
            )
        )
        if any(not item.accepted for item in entries) or any(
            not item.handed_off for item in handoffs
        ):
            failure_report = _make_report(
                source_report,
                destination,
                request_sha256,
                entries,
                handoffs,
                outputs,
                published=False,
                reused=False,
            )
            raise MapNativeCorpusBuildError(failure_report)

        shutil.rmtree(work)
        staged_report = _make_report(
            source_report,
            stage,
            request_sha256,
            entries,
            handoffs,
            outputs,
            published=True,
            reused=False,
        )
        manifest_bytes = _canonical_json_bytes(staged_report.neutral(), pretty=True)
        (stage / MAP_NATIVE_CORPUS_MANIFEST).write_bytes(manifest_bytes)
        verified_stage = _verify_output(stage, source_report, reused=False)
        if verified_stage.request_sha256 != request_sha256:
            raise MapNativeCorpusError("staged map-native request identity changed")
        _publish(stage, destination, backup)
        return _verify_output(destination, source_report, reused=False)
    finally:
        if os.path.lexists(stage):
            _remove_owned_tree(stage, parent, f".{destination.name}.staging-")
        if os.path.lexists(backup) and os.path.lexists(destination):
            _remove_owned_tree(backup, parent, f".{destination.name}.backup-")


# Concise alias for converter orchestration code.
build_corpus = build_map_native_corpus


__all__ = [
    "MAP_NATIVE_CORPUS_MANIFEST",
    "MAP_NATIVE_CORPUS_SCHEMA",
    "MAP_NATIVE_CORPUS_SCHEMA_VERSION",
    "MAX_MAP_CORPUS_FILES",
    "MAX_MAP_CORPUS_TOTAL_BYTES",
    "MapNativeArtifact",
    "MapNativeCorpusBuildError",
    "MapNativeCorpusError",
    "MapNativeCorpusLimitError",
    "MapNativeCorpusReport",
    "MapNativeCorpusReuseError",
    "MapNativeEntry",
    "MapNativeHandoff",
    "MapNativeOutput",
    "build_corpus",
    "build_map_native_corpus",
]
