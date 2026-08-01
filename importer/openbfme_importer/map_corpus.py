"""Verified effective-assets bridge for bounded, payload-free map census.

The extraction pipeline has already resolved archive precedence into one
effective-assets tree.  This module deliberately consumes only that tree and
its ``.openbfme/manifest.json``: it never opens a retail archive.  The complete
manifest/tree is validated before every manifest entry is prefix-probed once.
Entries with a ``.map`` suffix or a native ``EAR\\0``/``CkMp`` payload signature
are then read in full, checked against their declared size and SHA-256, and
passed to ``census_sage_map_bytes``.  Its chunk-name inventory separates
terrain maps from SCB script/camera containers without suffix inference.  Only
terrain maps may receive a caller-selected profile; unassigned maps remain
strict multiplayer.

Reports retain compact acceptance/rejection and handoff evidence, counts, and
hashes.  Map payloads, coordinates, script bodies, parser rejection text, and
host paths are not included in the JSON-ready representation.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass, replace
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
from typing import Iterable, Mapping

from .paths import safe_relative_parts
from .sage_map import (
    MAX_SOURCE_BYTES,
    SAGE_MAP_PROFILE_VERSION,
    SageMapError,
    census_sage_map_bytes,
)


EFFECTIVE_ASSET_MANIFEST_SCHEMA = "openbfme.effective-assets-manifest"
EFFECTIVE_ASSET_MANIFEST_VERSION = 0
EFFECTIVE_ASSET_MANIFEST_RELATIVE = ".openbfme/manifest.json"
MAP_CORPUS_SCHEMA = "openbfme.map-corpus"
MAP_CORPUS_SCHEMA_VERSION = 0

TERRAIN_MAP_ARTIFACT = "terrain-map"
SCRIPT_CONTAINER_ARTIFACT = "script-container"
UNCLASSIFIED_CANDIDATE_ARTIFACT = "unclassified-candidate"
MAP_ARTIFACT_KINDS = frozenset(
    {
        TERRAIN_MAP_ARTIFACT,
        SCRIPT_CONTAINER_ARTIFACT,
        UNCLASSIFIED_CANDIDATE_ARTIFACT,
    }
)
TERRAIN_MAP_CORE_CHUNKS = frozenset({"HeightMapData", "BlendTileData"})
SCB_SCRIPT_CONTAINER_CORE_CHUNKS = frozenset(
    {
        "ScriptImportSize",
        "PlayerScriptsList",
        "NamedCameras",
        "CameraAnimationList",
        "ScriptsPlayers",
        "ScriptTeams",
        "ObjectsList",
        "TriggerAreas",
        "WaypointsList",
    }
)

MAP_SIGNATURE_PROBE_BYTES = 4
MAP_DISCOVERY_REASON_ORDER = (
    "map-suffix",
    "ear-signature",
    "ckmp-signature",
)
MAP_DISCOVERY_REASONS = frozenset(MAP_DISCOVERY_REASON_ORDER)
_MAP_SIGNATURES = {
    b"EAR\0": "ear-signature",
    b"CkMp": "ckmp-signature",
}

MAX_EFFECTIVE_ASSET_MANIFEST_BYTES = 64 * 1024 * 1024
MAX_EFFECTIVE_ASSET_FILES = 50_000
MAX_EFFECTIVE_ASSET_BYTES = 8 * 1024 * 1024 * 1024
MAX_MAP_CORPUS_FILES = MAX_EFFECTIVE_ASSET_FILES
MAX_MAP_CORPUS_TOTAL_BYTES = MAX_EFFECTIVE_ASSET_BYTES

DEFAULT_MAP_PROFILE = "multiplayer"
MAP_PROFILES = frozenset({"multiplayer", "scenario", "library", "placeholder"})
_PROFILE_RUNNABLE = {
    "multiplayer": True,
    "scenario": True,
    "library": False,
    "placeholder": False,
}

_PROBE_STATUSES = frozenset(
    {
        "mixed",
        "parsed",
        "semantic-rejected",
        "unclassified",
        "unsupported-version",
    }
)
_ENVELOPE_KINDS = frozenset({"ear-refpack", "uncompressed"})


class MapCorpusError(ValueError):
    """Raised when an effective-assets map corpus cannot be trusted."""


class MapCorpusLimitError(MapCorpusError):
    """Raised before map reads when a corpus exceeds a declared safety bound."""


@dataclass(frozen=True, slots=True)
class MapCorpusFile:
    """Compact evidence for one verified manifest-declared map candidate.

    No member contains payload bytes, coordinates, scripts, or raw parser
    rejection text.
    """

    virtual_path: str
    byte_length: int
    source_sha256: str
    discovery_reasons: tuple[str, ...]
    artifact_kind: str
    classification_evidence_sha256: str
    chunk_name_set_sha256: str
    terrain_core_present: bool
    script_container_core_present: bool
    profile: str | None
    profile_version: int | None
    runnable: bool
    structural_status: str
    status: str
    rejection_code: str | None
    rejection_evidence_sha256: str | None
    census_evidence_sha256: str
    body_sha256: str | None
    envelope_kind: str | None
    chunk_type_count: int
    chunk_occurrence_count: int
    probe_status_counts: tuple[tuple[str, int], ...]

    @property
    def accepted(self) -> bool:
        return self.status == "accepted"

    @property
    def is_terrain_map(self) -> bool:
        return self.artifact_kind == TERRAIN_MAP_ARTIFACT

    @property
    def is_script_container(self) -> bool:
        return self.artifact_kind == SCRIPT_CONTAINER_ARTIFACT

    def neutral(self) -> dict[str, object]:
        """Return deterministic, host-independent, JSON-ready evidence."""

        return {
            "virtualPath": self.virtual_path,
            "byteLength": self.byte_length,
            "sourceSha256": self.source_sha256,
            "discoveryReasons": list(self.discovery_reasons),
            "artifactKind": self.artifact_kind,
            "classification": {
                "evidenceSha256": self.classification_evidence_sha256,
                "chunkNameSetSha256": self.chunk_name_set_sha256,
                "terrainCorePresent": self.terrain_core_present,
                "scriptContainerCorePresent": self.script_container_core_present,
            },
            "mapKind": self.profile,
            "profileVersion": self.profile_version,
            "runnable": self.runnable,
            "structuralStatus": self.structural_status,
            "status": self.status,
            "rejectionCode": self.rejection_code,
            "rejectionEvidenceSha256": self.rejection_evidence_sha256,
            "census": {
                "evidenceSha256": self.census_evidence_sha256,
                "bodySha256": self.body_sha256,
                "envelopeKind": self.envelope_kind,
                "chunkTypeCount": self.chunk_type_count,
                "chunkOccurrenceCount": self.chunk_occurrence_count,
                "probeStatusCounts": dict(self.probe_status_counts),
            },
        }

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class MapCorpusReport:
    """Verified corpus identity plus compact candidate census evidence."""

    asset_root: Path
    manifest_path: Path
    manifest_sha256: str
    manifest_aggregate_sha256: str
    manifest_file_count: int
    manifest_total_bytes: int
    artifacts: tuple[MapCorpusFile, ...]
    profile_selection_sha256: str
    map_inventory_sha256: str
    candidate_inventory_sha256: str
    candidate_evidence_sha256: str
    script_handoff_evidence_sha256: str
    map_evidence_sha256: str
    accepted_evidence_sha256: str
    rejected_evidence_sha256: str
    corpus_sha256: str

    @property
    def maps(self) -> tuple[MapCorpusFile, ...]:
        return tuple(item for item in self.artifacts if item.is_terrain_map)

    @property
    def script_containers(self) -> tuple[MapCorpusFile, ...]:
        return tuple(item for item in self.artifacts if item.is_script_container)

    @property
    def unclassified_candidates(self) -> tuple[MapCorpusFile, ...]:
        return tuple(
            item
            for item in self.artifacts
            if item.artifact_kind == UNCLASSIFIED_CANDIDATE_ARTIFACT
        )

    @property
    def complete(self) -> bool:
        return all(item.accepted for item in self.maps)

    @property
    def map_total_bytes(self) -> int:
        return sum(item.byte_length for item in self.maps)

    def neutral(self) -> dict[str, object]:
        """Return compact evidence without host paths or sensitive map facts."""

        maps = self.maps
        scripts = self.script_containers
        unclassified = self.unclassified_candidates
        accepted_count = sum(item.accepted for item in maps)
        rejection_codes = Counter(
            item.rejection_code
            for item in self.artifacts
            if item.rejection_code is not None
        )
        probe_statuses: Counter[str] = Counter()
        for item in self.artifacts:
            probe_statuses.update(dict(item.probe_status_counts))
        profile_counts = Counter(item.profile for item in maps)
        discovery_reasons = Counter(
            reason for item in self.artifacts for reason in item.discovery_reasons
        )
        discovery_combinations = Counter(
            "+".join(item.discovery_reasons) for item in self.artifacts
        )
        runnable_count = sum(item.runnable for item in maps)
        return {
            "schema": MAP_CORPUS_SCHEMA,
            "schemaVersion": MAP_CORPUS_SCHEMA_VERSION,
            "summary": {
                "manifestFileCount": self.manifest_file_count,
                "manifestTotalBytes": self.manifest_total_bytes,
                "candidateArtifactCount": len(self.artifacts),
                "candidateArtifactTotalBytes": sum(
                    item.byte_length for item in self.artifacts
                ),
                "scriptContainerCount": len(scripts),
                "scriptContainerTotalBytes": sum(
                    item.byte_length for item in scripts
                ),
                "unclassifiedCandidateCount": len(unclassified),
                "selectedMapCount": len(maps),
                "selectedMapTotalBytes": self.map_total_bytes,
                "mapSuffixDiscoveryCount": discovery_reasons["map-suffix"],
                "earSignatureDiscoveryCount": discovery_reasons["ear-signature"],
                "ckmpSignatureDiscoveryCount": discovery_reasons["ckmp-signature"],
                "signatureDiscoveredMapCount": sum(
                    bool(
                        {"ear-signature", "ckmp-signature"}
                        & set(item.discovery_reasons)
                    )
                    for item in maps
                ),
                "suffixOnlyDiscoveryCount": discovery_combinations["map-suffix"],
                "multiReasonDiscoveryCount": sum(
                    len(item.discovery_reasons) > 1 for item in self.artifacts
                ),
                "acceptedMapCount": accepted_count,
                "rejectedMapCount": len(maps) - accepted_count,
                "runnableMapCount": runnable_count,
                "nonRunnableMapCount": len(maps) - runnable_count,
                "profileVersion": SAGE_MAP_PROFILE_VERSION,
                "chunkTypeCount": sum(
                    item.chunk_type_count for item in self.artifacts
                ),
                "chunkOccurrenceCount": sum(
                    item.chunk_occurrence_count for item in self.artifacts
                ),
                "complete": self.complete,
            },
            "diagnostics": {
                "rejectionCodes": dict(sorted(rejection_codes.items())),
                "discoveryReasonCounts": dict(sorted(discovery_reasons.items())),
                "discoveryCombinationCounts": dict(
                    sorted(discovery_combinations.items())
                ),
                "profileCounts": dict(sorted(profile_counts.items())),
                "probeStatusCounts": dict(sorted(probe_statuses.items())),
            },
            "hashes": {
                "manifestSha256": self.manifest_sha256,
                "manifestAggregateSha256": self.manifest_aggregate_sha256,
                "profileSelectionSha256": self.profile_selection_sha256,
                "mapInventorySha256": self.map_inventory_sha256,
                "candidateInventorySha256": self.candidate_inventory_sha256,
                "candidateEvidenceSha256": self.candidate_evidence_sha256,
                "scriptHandoffEvidenceSha256": self.script_handoff_evidence_sha256,
                "mapEvidenceSha256": self.map_evidence_sha256,
                "acceptedEvidenceSha256": self.accepted_evidence_sha256,
                "rejectedEvidenceSha256": self.rejected_evidence_sha256,
                "corpusSha256": self.corpus_sha256,
            },
            "maps": [item.neutral() for item in maps],
            "scriptContainers": [item.neutral() for item in scripts],
            "unclassifiedCandidates": [item.neutral() for item in unclassified],
        }

    json_ready = neutral


class MapCorpusStrictError(MapCorpusError):
    """Raised after strict mode completes a corpus containing rejections."""

    def __init__(self, report: MapCorpusReport):
        self.report = report
        rejected = sum(not item.accepted for item in report.maps)
        super().__init__(f"strict map corpus rejected {rejected} map(s)")


@dataclass(frozen=True, slots=True)
class _ManifestFile:
    path: str
    size: int
    sha256: str
    discovery_reasons: tuple[str, ...] = ()

    @property
    def has_map_suffix(self) -> bool:
        return PurePosixPath(self.path).suffix.casefold() == ".map"


@dataclass(frozen=True, slots=True)
class _ValidatedManifest:
    raw_sha256: str
    aggregate_sha256: str
    total_files: int
    total_bytes: int
    files: tuple[_ManifestFile, ...]


@dataclass(frozen=True, slots=True)
class _TreeFile:
    relative_path: str
    path: Path
    size: int


class _DuplicateManifestKey(ValueError):
    pass


class _CensusContractError(ValueError):
    def __init__(self, code: str):
        self.code = code
        super().__init__(code)


def _canonical_sha256(value: object) -> str:
    payload = json.dumps(
        value,
        allow_nan=False,
        ensure_ascii=True,
        sort_keys=True,
        separators=(",", ":"),
    ).encode("utf-8")
    return hashlib.sha256(payload).hexdigest()


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and value == value.casefold()
        and all(character in "0123456789abcdef" for character in value)
    )


def _is_int(value: object, *, minimum: int = 0) -> bool:
    return not isinstance(value, bool) and isinstance(value, int) and value >= minimum


def _is_link_like(path: Path) -> bool:
    is_junction = getattr(path, "is_junction", None)
    return path.is_symlink() or bool(is_junction and is_junction())


def _absolute_unresolved(path: Path) -> Path:
    expanded = path.expanduser()
    if expanded.is_absolute():
        return expanded
    return Path.cwd() / expanded


def _refuse_link_chain(path: Path, *, context: str) -> None:
    """Reject links and junctions without resolving through them first."""

    absolute = _absolute_unresolved(path)
    current = Path(absolute.anchor)
    for part in absolute.parts[1:]:
        current = current / part
        try:
            linked = os.path.lexists(current) and _is_link_like(current)
        except OSError as exc:
            raise MapCorpusError(f"{context} link state cannot be inspected") from exc
        if linked:
            raise MapCorpusError(f"{context} is linked: {current}")


def _resolve_root(value: Path | str) -> Path:
    try:
        candidate = Path(value)
    except TypeError as exc:
        raise TypeError("effective-assets root must be a filesystem path") from exc
    _refuse_link_chain(candidate, context="effective-assets root")
    try:
        root = candidate.expanduser().resolve(strict=True)
        valid = root.is_dir() and not _is_link_like(root)
    except (OSError, RuntimeError) as exc:
        raise MapCorpusError(
            f"effective-assets root is unavailable: {candidate}"
        ) from exc
    if not valid:
        raise MapCorpusError("effective-assets root is not an unlinked directory")
    return root


def _object_without_duplicate_keys(
    pairs: Iterable[tuple[str, object]],
) -> dict[str, object]:
    result: dict[str, object] = {}
    for key, value in pairs:
        if key in result:
            raise _DuplicateManifestKey(f"duplicate manifest key: {key!r}")
        result[key] = value
    return result


def _reject_json_constant(value: str) -> object:
    raise ValueError(f"non-finite JSON value is not allowed: {value}")


def _manifest_aggregate(files: Iterable[_ManifestFile]) -> str:
    digest = hashlib.sha256()
    for item in files:
        digest.update(item.path.encode("utf-8"))
        digest.update(b"\0")
        digest.update(str(item.size).encode("ascii"))
        digest.update(b"\0")
        digest.update(item.sha256.encode("ascii"))
        digest.update(b"\n")
    return digest.hexdigest()


def _validate_identity_sections(manifest: Mapping[str, object]) -> None:
    catalog = manifest.get("catalog")
    if not isinstance(catalog, dict) or set(catalog) != {
        "archive_count",
        "entry_count",
        "format",
        "identity_sha256",
    }:
        raise MapCorpusError("effective-assets manifest catalog identity is invalid")
    if (
        not _is_int(catalog.get("archive_count"))
        or not _is_int(catalog.get("entry_count"))
        or not _is_int(catalog.get("format"), minimum=1)
        or not _is_sha256(catalog.get("identity_sha256"))
    ):
        raise MapCorpusError("effective-assets manifest catalog identity is invalid")

    install = manifest.get("install")
    if not isinstance(install, dict) or set(install) != {"identity_sha256", "root"}:
        raise MapCorpusError("effective-assets manifest install identity is invalid")
    if not _is_sha256(install.get("identity_sha256")) or not isinstance(
        install.get("root"), str
    ):
        raise MapCorpusError("effective-assets manifest install identity is invalid")


def _validate_manifest_file(raw: object, index: int) -> _ManifestFile:
    expected_keys = {"archive", "offset", "path", "precedence", "sha256", "size"}
    if not isinstance(raw, dict) or set(raw) != expected_keys:
        raise MapCorpusError(
            f"effective-assets manifest file entry {index} has an invalid shape"
        )
    archive = raw.get("archive")
    path_value = raw.get("path")
    size = raw.get("size")
    sha256 = raw.get("sha256")
    if not isinstance(archive, str) or not archive:
        raise MapCorpusError(
            f"effective-assets manifest file entry {index} has an invalid archive"
        )
    if (
        not _is_int(raw.get("offset"))
        or not _is_int(raw.get("precedence"))
        or not _is_int(size)
        or not _is_sha256(sha256)
    ):
        raise MapCorpusError(
            f"effective-assets manifest file entry {index} has invalid metadata"
        )
    if not isinstance(path_value, str):
        raise MapCorpusError(
            f"effective-assets manifest file entry {index} has an invalid path"
        )
    try:
        parts = safe_relative_parts(path_value)
    except ValueError as exc:
        raise MapCorpusError(
            f"effective-assets manifest file entry {index} has an unsafe path"
        ) from exc
    try:
        path_value.encode("utf-8")
    except UnicodeEncodeError as exc:
        raise MapCorpusError(
            f"effective-assets manifest file entry {index} has an invalid path"
        ) from exc
    canonical_path = "/".join(parts)
    if canonical_path != path_value:
        raise MapCorpusError(
            f"effective-assets manifest path is not canonical: {path_value!r}"
        )
    if parts[0].casefold() == ".openbfme":
        raise MapCorpusError(
            "effective-assets manifest path uses reserved metadata space: "
            f"{path_value!r}"
        )
    return _ManifestFile(path=path_value, size=size, sha256=sha256)


def _validate_inventory_paths(files: tuple[_ManifestFile, ...]) -> None:
    seen: dict[str, str] = {}
    for item in files:
        key = item.path.casefold()
        previous = seen.get(key)
        if previous is not None:
            raise MapCorpusError(
                "effective-assets manifest contains case-colliding paths: "
                f"{previous!r} and {item.path!r}"
            )
        seen[key] = item.path

    paths = [item.path for item in files]
    if paths != sorted(paths, key=lambda value: value.casefold()):
        raise MapCorpusError("effective-assets manifest inventory is not canonical")

    folded_paths = set(seen)
    for path in folded_paths:
        parts = path.split("/")
        for index in range(1, len(parts)):
            if "/".join(parts[:index]) in folded_paths:
                raise MapCorpusError(
                    "effective-assets manifest has a file/directory path collision"
                )


def _read_manifest_bytes(manifest_path: Path) -> bytes:
    try:
        declared_size = manifest_path.stat().st_size
    except OSError as exc:
        raise MapCorpusError("effective-assets manifest cannot be inspected") from exc
    if not 1 <= declared_size <= MAX_EFFECTIVE_ASSET_MANIFEST_BYTES:
        raise MapCorpusLimitError("effective-assets manifest exceeds its safety bound")
    try:
        with manifest_path.open("rb") as stream:
            raw_bytes = stream.read(MAX_EFFECTIVE_ASSET_MANIFEST_BYTES + 1)
        final_size = manifest_path.stat().st_size
    except OSError as exc:
        raise MapCorpusError("effective-assets manifest cannot be read") from exc
    if len(raw_bytes) > MAX_EFFECTIVE_ASSET_MANIFEST_BYTES:
        raise MapCorpusLimitError("effective-assets manifest exceeds its safety bound")
    if len(raw_bytes) != declared_size or final_size != declared_size:
        raise MapCorpusError("effective-assets manifest changed during read")
    return raw_bytes


def _verify_manifest_unchanged(manifest_path: Path, expected_sha256: str) -> None:
    """Recheck the small provenance boundary after a potentially long census."""

    _refuse_link_chain(manifest_path, context="effective-assets manifest")
    try:
        valid_manifest = manifest_path.is_file() and not _is_link_like(manifest_path)
    except OSError as exc:
        raise MapCorpusError("effective-assets manifest cannot be inspected") from exc
    if not valid_manifest:
        raise MapCorpusError("effective-assets manifest changed during map census")
    if (
        hashlib.sha256(_read_manifest_bytes(manifest_path)).hexdigest()
        != expected_sha256
    ):
        raise MapCorpusError("effective-assets manifest changed during map census")


def _load_manifest(root: Path) -> tuple[Path, _ValidatedManifest]:
    manifest_path = root.joinpath(
        *PurePosixPath(EFFECTIVE_ASSET_MANIFEST_RELATIVE).parts
    )
    _refuse_link_chain(manifest_path, context="effective-assets manifest")
    try:
        valid_manifest = manifest_path.is_file() and not _is_link_like(manifest_path)
    except OSError as exc:
        raise MapCorpusError("effective-assets manifest cannot be inspected") from exc
    if not valid_manifest:
        raise MapCorpusError(
            "effective-assets manifest is missing, linked, or not a file"
        )
    raw_bytes = _read_manifest_bytes(manifest_path)
    try:
        manifest = json.loads(
            raw_bytes.decode("utf-8"),
            object_pairs_hook=_object_without_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise MapCorpusError("effective-assets manifest is invalid JSON") from exc
    if not isinstance(manifest, dict):
        raise MapCorpusError("effective-assets manifest root is not an object")
    if set(manifest) != {
        "aggregate_sha256",
        "catalog",
        "files",
        "install",
        "schema",
        "schema_version",
        "totals",
    }:
        raise MapCorpusError("effective-assets manifest top-level shape is invalid")
    if manifest.get("schema") != EFFECTIVE_ASSET_MANIFEST_SCHEMA:
        raise MapCorpusError("effective-assets manifest schema is unsupported")
    schema_version = manifest.get("schema_version")
    if (
        isinstance(schema_version, bool)
        or not isinstance(schema_version, int)
        or schema_version != EFFECTIVE_ASSET_MANIFEST_VERSION
    ):
        raise MapCorpusError("effective-assets manifest schema version is unsupported")
    _validate_identity_sections(manifest)

    raw_files = manifest.get("files")
    if not isinstance(raw_files, list):
        raise MapCorpusError("effective-assets manifest file inventory is invalid")
    if len(raw_files) > MAX_EFFECTIVE_ASSET_FILES:
        raise MapCorpusLimitError(
            "effective-assets manifest file count exceeds its bound"
        )
    files = tuple(
        _validate_manifest_file(item, index) for index, item in enumerate(raw_files)
    )
    _validate_inventory_paths(files)

    totals = manifest.get("totals")
    if not isinstance(totals, dict) or set(totals) != {"bytes", "files"}:
        raise MapCorpusError("effective-assets manifest totals are invalid")
    total_files = totals.get("files")
    total_bytes = totals.get("bytes")
    if not _is_int(total_files) or not _is_int(total_bytes):
        raise MapCorpusError("effective-assets manifest totals are invalid")
    calculated_bytes = sum(item.size for item in files)
    if total_files != len(files) or total_bytes != calculated_bytes:
        raise MapCorpusError(
            "effective-assets manifest totals do not match its inventory"
        )
    if total_bytes > MAX_EFFECTIVE_ASSET_BYTES:
        raise MapCorpusLimitError(
            "effective-assets manifest byte total exceeds its bound"
        )

    aggregate = manifest.get("aggregate_sha256")
    if not _is_sha256(aggregate) or aggregate != _manifest_aggregate(files):
        raise MapCorpusError("effective-assets manifest aggregate SHA-256 is invalid")
    return manifest_path, _ValidatedManifest(
        raw_sha256=hashlib.sha256(raw_bytes).hexdigest(),
        aggregate_sha256=aggregate,
        total_files=total_files,
        total_bytes=total_bytes,
        files=files,
    )


def _record_tree_path(seen: dict[str, str], relative_path: str, *, label: str) -> str:
    key = relative_path.casefold()
    previous = seen.get(key)
    if previous is not None:
        raise MapCorpusError(
            f"effective-assets tree contains case-colliding {label}: "
            f"{previous!r} and {relative_path!r}"
        )
    seen[key] = relative_path
    return key


def _scan_tree(root: Path) -> tuple[dict[str, _TreeFile], dict[str, str]]:
    files: dict[str, _TreeFile] = {}
    directories: dict[str, str] = {}
    all_paths: dict[str, str] = {}
    pending = [root]
    while pending:
        directory = pending.pop()
        try:
            with os.scandir(directory) as iterator:
                entries = sorted(
                    iterator, key=lambda item: (item.name.casefold(), item.name)
                )
        except OSError as exc:
            raise MapCorpusError("effective-assets tree cannot be enumerated") from exc
        for entry in entries:
            path = Path(entry.path)
            relative = path.relative_to(root).as_posix()
            try:
                if entry.is_symlink() or _is_link_like(path):
                    raise MapCorpusError(
                        f"effective-assets tree contains a link: {relative!r}"
                    )
                if entry.is_dir(follow_symlinks=False):
                    key = _record_tree_path(all_paths, relative, label="paths")
                    directories[key] = relative
                    pending.append(path)
                elif entry.is_file(follow_symlinks=False):
                    key = _record_tree_path(all_paths, relative, label="paths")
                    size = entry.stat(follow_symlinks=False).st_size
                    files[key] = _TreeFile(relative, path, size)
                else:
                    raise MapCorpusError(
                        "effective-assets tree contains an unsupported filesystem "
                        f"entry: {relative!r}"
                    )
            except OSError as exc:
                raise MapCorpusError(
                    f"effective-assets tree entry cannot be inspected: {relative!r}"
                ) from exc
    return files, directories


def _expected_directories(paths: Iterable[str]) -> dict[str, str]:
    result: dict[str, str] = {}
    for relative in paths:
        parts = PurePosixPath(relative).parts
        for index in range(1, len(parts)):
            directory = "/".join(parts[:index])
            result.setdefault(directory.casefold(), directory)
    return result


def _validate_tree(root: Path, manifest: _ValidatedManifest) -> dict[str, _TreeFile]:
    actual_files, actual_directories = _scan_tree(root)
    expected_file_paths = [item.path for item in manifest.files]
    expected_file_paths.append(EFFECTIVE_ASSET_MANIFEST_RELATIVE)
    expected_files = {path.casefold(): path for path in expected_file_paths}
    expected_directories = _expected_directories(expected_file_paths)

    actual_file_keys = set(actual_files)
    expected_file_keys = set(expected_files)
    actual_directory_keys = set(actual_directories)
    expected_directory_keys = set(expected_directories)
    missing_files = sorted(expected_file_keys - actual_file_keys)
    extra_files = sorted(actual_file_keys - expected_file_keys)
    missing_directories = sorted(expected_directory_keys - actual_directory_keys)
    extra_directories = sorted(actual_directory_keys - expected_directory_keys)
    if missing_files:
        raise MapCorpusError(
            "effective-assets tree is missing declared files: "
            + ", ".join(expected_files[key] for key in missing_files[:5])
        )
    if extra_files:
        raise MapCorpusError(
            "effective-assets tree contains undeclared files: "
            + ", ".join(actual_files[key].relative_path for key in extra_files[:5])
        )
    if missing_directories:
        raise MapCorpusError(
            "effective-assets tree is missing declared directories: "
            + ", ".join(expected_directories[key] for key in missing_directories[:5])
        )
    if extra_directories:
        raise MapCorpusError(
            "effective-assets tree contains undeclared directories: "
            + ", ".join(actual_directories[key] for key in extra_directories[:5])
        )

    for item in manifest.files:
        actual = actual_files[item.path.casefold()]
        if actual.size != item.size:
            raise MapCorpusError(
                f"effective-assets file size does not match the manifest: {item.path!r}"
            )
    return actual_files


def _selected_limit(value: int | None, hard_limit: int, label: str) -> int:
    selected = hard_limit if value is None else value
    if isinstance(selected, bool) or not isinstance(selected, int):
        raise TypeError(f"map corpus {label} limit must be an integer")
    if not 1 <= selected <= hard_limit:
        raise ValueError(f"map corpus {label} limit must be 1..{hard_limit}")
    return selected


def _profile_intent(profile: str) -> tuple[int, bool, str]:
    runnable = _PROFILE_RUNNABLE[profile]
    return (
        SAGE_MAP_PROFILE_VERSION,
        runnable,
        "runnable-structure" if runnable else "non-runnable-structural-map",
    )


def _normalize_profiles(
    profiles: Mapping[str, str] | None,
    selected_files: tuple[_ManifestFile, ...],
    manifest_files: tuple[_ManifestFile, ...],
) -> dict[str, str]:
    """Resolve an exact caller mapping without deriving intent from names."""

    if profiles is None:
        raw_items: list[tuple[object, object]] = []
    elif not isinstance(profiles, Mapping):
        raise TypeError("map corpus profiles must be a path-to-profile mapping")
    else:
        raw_items = list(profiles.items())

    manifest_by_path = {item.path: item for item in manifest_files}
    manifest_by_folded = {item.path.casefold(): item.path for item in manifest_files}
    selected_by_path = {item.path: item for item in selected_files}
    explicit: dict[str, str] = {}
    seen_folded: dict[str, str] = {}
    for raw_path, raw_profile in raw_items:
        if not isinstance(raw_path, str):
            raise MapCorpusError("map profile mapping key must be a string path")
        try:
            parts = safe_relative_parts(raw_path)
        except ValueError as exc:
            raise MapCorpusError(
                f"map profile mapping contains an unsafe path: {raw_path!r}"
            ) from exc
        canonical = "/".join(parts)
        if canonical != raw_path:
            raise MapCorpusError(
                f"map profile mapping path is not canonical: {raw_path!r}"
            )
        try:
            raw_path.encode("utf-8")
        except UnicodeEncodeError as exc:
            raise MapCorpusError("map profile mapping path is not valid UTF-8") from exc
        folded = raw_path.casefold()
        previous = seen_folded.get(folded)
        if previous is not None:
            raise MapCorpusError(
                "map profile mapping contains case-colliding paths: "
                f"{previous!r} and {raw_path!r}"
            )
        seen_folded[folded] = raw_path

        item = manifest_by_path.get(raw_path)
        if item is None:
            if folded in manifest_by_folded:
                raise MapCorpusError(
                    "map profile mapping path must exactly match manifest casing: "
                    f"{raw_path!r}"
                )
            raise MapCorpusError(
                f"map profile mapping references an unknown manifest path: {raw_path!r}"
            )
        if raw_path not in selected_by_path:
            raise MapCorpusError(
                "map profile mapping references a non-map path or nonselected "
                f"candidate: {raw_path!r}"
            )
        if not isinstance(raw_profile, str) or raw_profile not in MAP_PROFILES:
            raise MapCorpusError(
                f"map profile mapping has an unsupported profile for {raw_path!r}"
            )
        explicit[raw_path] = raw_profile

    return {
        item.path: explicit.get(item.path, DEFAULT_MAP_PROFILE)
        for item in selected_files
    }


def _read_signature_prefix(actual: _TreeFile, expected: _ManifestFile) -> bytes:
    """Read only the bounded discovery prefix and detect in-read mutation."""

    _refuse_link_chain(actual.path, context="manifest-declared asset")
    try:
        if _is_link_like(actual.path):
            raise MapCorpusError(
                f"manifest-declared asset became linked: {expected.path!r}"
            )
        before = actual.path.stat()
        with actual.path.open("rb") as stream:
            prefix = stream.read(MAP_SIGNATURE_PROBE_BYTES)
        after = actual.path.stat()
        linked_after = _is_link_like(actual.path)
    except OSError as exc:
        raise MapCorpusError(
            f"manifest-declared asset cannot be prefix-probed: {expected.path!r}"
        ) from exc
    if (
        linked_after
        or before.st_size != expected.size
        or after.st_size != expected.size
        or after.st_mtime_ns != before.st_mtime_ns
    ):
        raise MapCorpusError(
            f"manifest-declared asset changed during prefix probe: {expected.path!r}"
        )
    return prefix


def _discover_maps(
    manifest_files: tuple[_ManifestFile, ...],
    actual_files: Mapping[str, _TreeFile],
) -> tuple[_ManifestFile, ...]:
    """Prefix-probe every entry and retain every suffix/signature candidate."""

    selected: list[_ManifestFile] = []
    for item in manifest_files:
        actual = actual_files[item.path.casefold()]
        prefix = _read_signature_prefix(actual, item)
        reasons: list[str] = []
        if item.has_map_suffix:
            reasons.append("map-suffix")
        signature_reason = _MAP_SIGNATURES.get(prefix)
        if signature_reason is not None:
            reasons.append(signature_reason)
        if reasons:
            selected.append(replace(item, discovery_reasons=tuple(reasons)))
    return tuple(selected)


def _read_verified_map(actual: _TreeFile, expected: _ManifestFile) -> bytes:
    _refuse_link_chain(actual.path, context="manifest-declared map")
    try:
        if _is_link_like(actual.path):
            raise MapCorpusError(
                f"manifest-declared map became linked: {expected.path!r}"
            )
        with actual.path.open("rb") as stream:
            source = stream.read(expected.size + 1)
        final_size = actual.path.stat().st_size
        linked_after = _is_link_like(actual.path)
    except OSError as exc:
        raise MapCorpusError(
            f"manifest-declared map cannot be read: {expected.path!r}"
        ) from exc
    if linked_after:
        raise MapCorpusError(f"manifest-declared map became linked: {expected.path!r}")
    if len(source) != expected.size or final_size != expected.size:
        raise MapCorpusError(
            f"manifest-declared map size changed during read: {expected.path!r}"
        )
    if hashlib.sha256(source).hexdigest() != expected.sha256:
        raise MapCorpusError(
            f"manifest-declared map SHA-256 does not match: {expected.path!r}"
        )
    return source


def _normalized_reason(value: object) -> str:
    return " ".join(str(value).split())


def _chunk_name_set_sha256(names: Iterable[str]) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.map-corpus-chunk-name-set",
            "schemaVersion": 0,
            "names": sorted(set(names), key=lambda value: (value.casefold(), value)),
        }
    )


def _classification_evidence_sha256(
    item: _ManifestFile,
    *,
    artifact_kind: str,
    chunk_names: frozenset[str],
) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.map-corpus-artifact-classification",
            "schemaVersion": 0,
            "virtualPath": item.path,
            "sourceSha256": item.sha256,
            "discoveryReasons": list(item.discovery_reasons),
            "artifactKind": artifact_kind,
            "chunkNameSetSha256": _chunk_name_set_sha256(chunk_names),
            "terrainCorePresent": TERRAIN_MAP_CORE_CHUNKS <= chunk_names,
            "terrainChunkPresent": bool(TERRAIN_MAP_CORE_CHUNKS & chunk_names),
            "scriptContainerCorePresent": (
                SCB_SCRIPT_CONTAINER_CORE_CHUNKS <= chunk_names
            ),
        }
    )


def _artifact_kind(item: _ManifestFile, chunk_names: frozenset[str]) -> str:
    if TERRAIN_MAP_CORE_CHUNKS <= chunk_names:
        return TERRAIN_MAP_ARTIFACT
    if (
        SCB_SCRIPT_CONTAINER_CORE_CHUNKS <= chunk_names
        and not TERRAIN_MAP_CORE_CHUNKS & chunk_names
    ):
        return SCRIPT_CONTAINER_ARTIFACT
    if item.has_map_suffix:
        return TERRAIN_MAP_ARTIFACT
    return UNCLASSIFIED_CANDIDATE_ARTIFACT


def _rejection_evidence_sha256(
    code: str,
    reason: object,
    *,
    discovery_reasons: tuple[str, ...],
    artifact_kind: str,
    profile: str | None,
) -> str:
    if profile is None:
        profile_version = None
        runnable = False
        structural_status = artifact_kind
    else:
        profile_version, runnable, structural_status = _profile_intent(profile)
    return _canonical_sha256(
        {
            "schema": "openbfme.map-corpus-rejection-evidence",
            "schemaVersion": 0,
            "code": code,
            "reason": _normalized_reason(reason),
            "discoveryReasons": list(discovery_reasons),
            "artifactKind": artifact_kind,
            "mapKind": profile,
            "profileVersion": profile_version,
            "runnable": runnable,
            "structuralStatus": structural_status,
        }
    )


def _rejected_artifact(
    item: _ManifestFile,
    *,
    artifact_kind: str,
    profile: str | None,
    code: str,
    reason: object,
    chunk_names: frozenset[str] = frozenset(),
    census_evidence_sha256: str | None = None,
    body_sha256: str | None = None,
    envelope_kind: str | None = None,
    chunk_type_count: int = 0,
    chunk_occurrence_count: int = 0,
    probe_status_counts: tuple[tuple[str, int], ...] = (),
) -> MapCorpusFile:
    rejection_hash = _rejection_evidence_sha256(
        code,
        reason,
        discovery_reasons=item.discovery_reasons,
        artifact_kind=artifact_kind,
        profile=profile,
    )
    if profile is None:
        profile_version = None
        runnable = False
        structural_status = artifact_kind
    else:
        profile_version, runnable, structural_status = _profile_intent(profile)
    classification_hash = _classification_evidence_sha256(
        item, artifact_kind=artifact_kind, chunk_names=chunk_names
    )
    return MapCorpusFile(
        virtual_path=item.path,
        byte_length=item.size,
        source_sha256=item.sha256,
        discovery_reasons=item.discovery_reasons,
        artifact_kind=artifact_kind,
        classification_evidence_sha256=classification_hash,
        chunk_name_set_sha256=_chunk_name_set_sha256(chunk_names),
        terrain_core_present=TERRAIN_MAP_CORE_CHUNKS <= chunk_names,
        script_container_core_present=(
            SCB_SCRIPT_CONTAINER_CORE_CHUNKS <= chunk_names
        ),
        profile=profile,
        profile_version=profile_version,
        runnable=runnable,
        structural_status=structural_status,
        status="rejected",
        rejection_code=code,
        rejection_evidence_sha256=rejection_hash,
        census_evidence_sha256=census_evidence_sha256 or rejection_hash,
        body_sha256=body_sha256,
        envelope_kind=envelope_kind,
        chunk_type_count=chunk_type_count,
        chunk_occurrence_count=chunk_occurrence_count,
        probe_status_counts=probe_status_counts,
    )


def _compact_census(
    item: _ManifestFile, facts: object, *, profile: str
) -> MapCorpusFile:
    if not isinstance(facts, dict):
        raise _CensusContractError("root-not-object")
    if facts.get("sourceBytes") != item.size:
        raise _CensusContractError("source-size-mismatch")
    if facts.get("sourceSha256") != item.sha256:
        raise _CensusContractError("source-sha256-mismatch")
    body_sha256 = facts.get("bodySha256")
    envelope_kind = facts.get("envelopeKind")
    if not _is_sha256(body_sha256):
        raise _CensusContractError("invalid-body-sha256")
    if envelope_kind not in _ENVELOPE_KINDS:
        raise _CensusContractError("invalid-envelope-kind")

    chunks = facts.get("chunks")
    if not isinstance(chunks, list):
        raise _CensusContractError("invalid-chunks")
    probe_statuses: Counter[str] = Counter()
    chunk_occurrences = 0
    chunk_names: set[str] = set()
    for chunk in chunks:
        if not isinstance(chunk, dict):
            raise _CensusContractError("invalid-chunk")
        occurrences = chunk.get("occurrences")
        status = chunk.get("probeStatus")
        name = chunk.get("name")
        if not isinstance(name, str) or not name:
            raise _CensusContractError("invalid-chunk-name")
        if not _is_int(occurrences, minimum=1):
            raise _CensusContractError("invalid-chunk-occurrences")
        if status not in _PROBE_STATUSES:
            raise _CensusContractError("invalid-probe-status")
        chunk_occurrences += occurrences
        probe_statuses[status] += occurrences
        chunk_names.add(name)

    strict_cook = facts.get("strictCook")
    if not isinstance(strict_cook, dict):
        raise _CensusContractError("invalid-strict-cook")
    accepted = strict_cook.get("accepted")
    reason = strict_cook.get("reason")
    profile_version, runnable, structural_status = _profile_intent(profile)
    if strict_cook.get("mapKind") != profile:
        raise _CensusContractError("profile-kind-mismatch")
    if strict_cook.get("profileVersion") != profile_version:
        raise _CensusContractError("profile-version-mismatch")
    if strict_cook.get("runnable") is not runnable:
        raise _CensusContractError("profile-runnable-mismatch")
    if strict_cook.get("structuralStatus") != structural_status:
        raise _CensusContractError("profile-structural-status-mismatch")
    if not isinstance(accepted, bool):
        raise _CensusContractError("invalid-strict-cook-accepted")
    if accepted and reason is not None:
        raise _CensusContractError("unexpected-strict-cook-reason")
    if not accepted and (not isinstance(reason, str) or not reason.strip()):
        raise _CensusContractError("missing-strict-cook-reason")
    try:
        census_hash = _canonical_sha256(facts)
    except (TypeError, ValueError, UnicodeError) as exc:
        raise _CensusContractError("census-not-json-ready") from exc

    probe_counts = tuple(sorted(probe_statuses.items()))
    frozen_chunk_names = frozenset(chunk_names)
    artifact_kind = _artifact_kind(item, frozen_chunk_names)
    classification_hash = _classification_evidence_sha256(
        item, artifact_kind=artifact_kind, chunk_names=frozen_chunk_names
    )
    if artifact_kind == SCRIPT_CONTAINER_ARTIFACT:
        return MapCorpusFile(
            virtual_path=item.path,
            byte_length=item.size,
            source_sha256=item.sha256,
            discovery_reasons=item.discovery_reasons,
            artifact_kind=artifact_kind,
            classification_evidence_sha256=classification_hash,
            chunk_name_set_sha256=_chunk_name_set_sha256(frozen_chunk_names),
            terrain_core_present=False,
            script_container_core_present=True,
            profile=None,
            profile_version=None,
            runnable=False,
            structural_status="script-container-handoff",
            status="handed-off",
            rejection_code=None,
            rejection_evidence_sha256=None,
            census_evidence_sha256=census_hash,
            body_sha256=body_sha256,
            envelope_kind=envelope_kind,
            chunk_type_count=len(chunks),
            chunk_occurrence_count=chunk_occurrences,
            probe_status_counts=probe_counts,
        )
    if artifact_kind == UNCLASSIFIED_CANDIDATE_ARTIFACT:
        return _rejected_artifact(
            item,
            artifact_kind=artifact_kind,
            profile=None,
            code="artifact-structure-unclassified",
            reason="candidate has neither the terrain nor script-container core",
            chunk_names=frozen_chunk_names,
            census_evidence_sha256=census_hash,
            body_sha256=body_sha256,
            envelope_kind=envelope_kind,
            chunk_type_count=len(chunks),
            chunk_occurrence_count=chunk_occurrences,
            probe_status_counts=probe_counts,
        )
    if not TERRAIN_MAP_CORE_CHUNKS <= frozen_chunk_names:
        return _rejected_artifact(
            item,
            artifact_kind=TERRAIN_MAP_ARTIFACT,
            profile=profile,
            code="terrain-structure-not-proven",
            reason="map-suffix candidate lacks HeightMapData plus BlendTileData",
            chunk_names=frozen_chunk_names,
            census_evidence_sha256=census_hash,
            body_sha256=body_sha256,
            envelope_kind=envelope_kind,
            chunk_type_count=len(chunks),
            chunk_occurrence_count=chunk_occurrences,
            probe_status_counts=probe_counts,
        )
    if not accepted:
        return _rejected_artifact(
            item,
            artifact_kind=TERRAIN_MAP_ARTIFACT,
            profile=profile,
            code="strict-cook-rejected",
            reason=reason,
            chunk_names=frozen_chunk_names,
            census_evidence_sha256=census_hash,
            body_sha256=body_sha256,
            envelope_kind=envelope_kind,
            chunk_type_count=len(chunks),
            chunk_occurrence_count=chunk_occurrences,
            probe_status_counts=probe_counts,
        )
    return MapCorpusFile(
        virtual_path=item.path,
        byte_length=item.size,
        source_sha256=item.sha256,
        discovery_reasons=item.discovery_reasons,
        artifact_kind=TERRAIN_MAP_ARTIFACT,
        classification_evidence_sha256=classification_hash,
        chunk_name_set_sha256=_chunk_name_set_sha256(frozen_chunk_names),
        terrain_core_present=True,
        script_container_core_present=(
            SCB_SCRIPT_CONTAINER_CORE_CHUNKS <= frozen_chunk_names
        ),
        profile=profile,
        profile_version=profile_version,
        runnable=runnable,
        structural_status=structural_status,
        status="accepted",
        rejection_code=None,
        rejection_evidence_sha256=None,
        census_evidence_sha256=census_hash,
        body_sha256=body_sha256,
        envelope_kind=envelope_kind,
        chunk_type_count=len(chunks),
        chunk_occurrence_count=chunk_occurrences,
        probe_status_counts=probe_counts,
    )


def _census_verified_map(
    item: _ManifestFile, source: bytes, *, profile: str
) -> MapCorpusFile:
    try:
        facts = census_sage_map_bytes(source, profile=profile)
    except SageMapError as exc:
        artifact_kind = (
            TERRAIN_MAP_ARTIFACT
            if item.has_map_suffix
            else UNCLASSIFIED_CANDIDATE_ARTIFACT
        )
        return _rejected_artifact(
            item,
            artifact_kind=artifact_kind,
            profile=profile if artifact_kind == TERRAIN_MAP_ARTIFACT else None,
            code="census-parse-rejected",
            reason=exc,
        )
    try:
        return _compact_census(item, facts, profile=profile)
    except _CensusContractError as exc:
        artifact_kind = (
            TERRAIN_MAP_ARTIFACT
            if item.has_map_suffix
            else UNCLASSIFIED_CANDIDATE_ARTIFACT
        )
        return _rejected_artifact(
            item,
            artifact_kind=artifact_kind,
            profile=profile if artifact_kind == TERRAIN_MAP_ARTIFACT else None,
            code="census-contract-rejected",
            reason=exc.code,
        )


def _profile_selection_sha256(
    files: tuple[_ManifestFile, ...], profiles: Mapping[str, str]
) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.map-corpus-profile-selection",
            "schemaVersion": 0,
            "assignments": [
                {
                    "virtualPath": item.path,
                    "discoveryReasons": list(item.discovery_reasons),
                    "mapKind": profiles[item.path],
                    "profileVersion": _profile_intent(profiles[item.path])[0],
                    "runnable": _profile_intent(profiles[item.path])[1],
                    "structuralStatus": _profile_intent(profiles[item.path])[2],
                }
                for item in files
            ],
        }
    )


def _map_inventory_sha256(
    files: tuple[_ManifestFile, ...], profiles: Mapping[str, str]
) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.map-corpus-inventory",
            "schemaVersion": 0,
            "files": [
                {
                    "virtualPath": item.path,
                    "byteLength": item.size,
                    "sourceSha256": item.sha256,
                    "discoveryReasons": list(item.discovery_reasons),
                    "mapKind": profiles[item.path],
                    "profileVersion": _profile_intent(profiles[item.path])[0],
                    "runnable": _profile_intent(profiles[item.path])[1],
                    "structuralStatus": _profile_intent(profiles[item.path])[2],
                }
                for item in files
            ],
        }
    )


def _map_evidence_set_sha256(label: str, maps: Iterable[MapCorpusFile]) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.map-corpus-evidence-set",
            "schemaVersion": 0,
            "kind": label,
            "maps": [item.neutral() for item in maps],
        }
    )


def _candidate_inventory_sha256(artifacts: Iterable[MapCorpusFile]) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.map-corpus-candidate-inventory",
            "schemaVersion": 0,
            "artifacts": [
                {
                    "virtualPath": item.virtual_path,
                    "byteLength": item.byte_length,
                    "sourceSha256": item.source_sha256,
                    "discoveryReasons": list(item.discovery_reasons),
                    "artifactKind": item.artifact_kind,
                    "classificationEvidenceSha256": (
                        item.classification_evidence_sha256
                    ),
                }
                for item in artifacts
            ],
        }
    )


def scan_map_corpus(
    effective_assets_root: Path | str,
    *,
    profiles: Mapping[str, str] | None = None,
    strict: bool = False,
    max_files: int | None = None,
    max_total_bytes: int | None = None,
) -> MapCorpusReport:
    """Verify and census every suffix- or signature-discovered candidate once.

    Chunk structure classifies terrain maps and script/camera containers.  A
    malformed explicit ``.map`` remains a rejected terrain-map candidate so it
    cannot disappear from map diagnostics.  ``profiles`` is an exact canonical
    manifest-path mapping for terrain maps only.  Unlisted maps use the strict
    ``multiplayer`` profile.  ``max_files`` and ``max_total_bytes`` may
    lower, but never raise, the hard
    corpus bounds.  Every selected map is additionally bounded by
    :data:`sage_map.MAX_SOURCE_BYTES`.  Bounds are checked before any map is
    opened; a limit never causes truncation or partial success.

    Normal mode records parser and strict-cook rejections while continuing the
    complete selected set.  Strict mode performs the same complete scan and
    raises :class:`MapCorpusStrictError` with the report if any map was rejected.
    Manifest/tree integrity failures always raise immediately.
    """

    if not isinstance(strict, bool):
        raise TypeError("map corpus strict flag must be a boolean")
    if profiles is not None and not isinstance(profiles, Mapping):
        raise TypeError("map corpus profiles must be a path-to-profile mapping")
    selected_max_files = _selected_limit(max_files, MAX_MAP_CORPUS_FILES, "file count")
    selected_max_bytes = _selected_limit(
        max_total_bytes, MAX_MAP_CORPUS_TOTAL_BYTES, "total byte"
    )

    root = _resolve_root(effective_assets_root)
    manifest_path, manifest = _load_manifest(root)
    actual_files = _validate_tree(root, manifest)
    selected = _discover_maps(manifest.files, actual_files)
    if not selected:
        raise MapCorpusError(
            "effective-assets manifest declares no map files or discoverable map payloads"
        )
    resolved_profiles = _normalize_profiles(profiles, selected, manifest.files)
    selected_bytes = sum(item.size for item in selected)
    if len(selected) > selected_max_files:
        raise MapCorpusLimitError(f"map corpus file count exceeds {selected_max_files}")
    if selected_bytes > selected_max_bytes:
        raise MapCorpusLimitError(f"map corpus total bytes exceed {selected_max_bytes}")
    oversized = next((item for item in selected if item.size > MAX_SOURCE_BYTES), None)
    if oversized is not None:
        raise MapCorpusLimitError(
            "manifest-declared map exceeds the per-file census bound: "
            f"{oversized.path!r}"
        )

    artifact_results: list[MapCorpusFile] = []
    for item in selected:
        actual = actual_files[item.path.casefold()]
        source = _read_verified_map(actual, item)
        artifact_results.append(
            _census_verified_map(item, source, profile=resolved_profiles[item.path])
        )
    artifacts = tuple(artifact_results)
    _verify_manifest_unchanged(manifest_path, manifest.raw_sha256)

    explicitly_profiled = set(profiles or {})
    invalid_profile_target = next(
        (
            item
            for item in artifacts
            if item.virtual_path in explicitly_profiled and not item.is_terrain_map
        ),
        None,
    )
    if invalid_profile_target is not None:
        raise MapCorpusError(
            "map profile mapping may target terrain maps only: "
            f"{invalid_profile_target.virtual_path!r}"
        )

    maps = tuple(item for item in artifacts if item.is_terrain_map)
    scripts = tuple(item for item in artifacts if item.is_script_container)
    terrain_paths = {item.virtual_path for item in maps}
    terrain_selected = tuple(item for item in selected if item.path in terrain_paths)
    profile_selection_hash = _profile_selection_sha256(
        terrain_selected, resolved_profiles
    )
    inventory_hash = _map_inventory_sha256(terrain_selected, resolved_profiles)
    candidate_inventory_hash = _candidate_inventory_sha256(artifacts)
    candidate_evidence_hash = _map_evidence_set_sha256("candidates", artifacts)
    script_handoff_hash = _map_evidence_set_sha256("script-handoffs", scripts)
    map_evidence_hash = _map_evidence_set_sha256("all", maps)
    accepted_hash = _map_evidence_set_sha256(
        "accepted", (item for item in maps if item.accepted)
    )
    rejected_hash = _map_evidence_set_sha256(
        "rejected", (item for item in maps if not item.accepted)
    )
    identity_basis = {
        "schema": "openbfme.map-corpus-identity",
        "schemaVersion": 0,
        "manifestSha256": manifest.raw_sha256,
        "manifestAggregateSha256": manifest.aggregate_sha256,
        "profileSelectionSha256": profile_selection_hash,
        "mapInventorySha256": inventory_hash,
        "candidateInventorySha256": candidate_inventory_hash,
        "candidateEvidenceSha256": candidate_evidence_hash,
        "scriptHandoffEvidenceSha256": script_handoff_hash,
        "mapEvidenceSha256": map_evidence_hash,
        "acceptedEvidenceSha256": accepted_hash,
        "rejectedEvidenceSha256": rejected_hash,
    }
    report = MapCorpusReport(
        asset_root=root,
        manifest_path=manifest_path,
        manifest_sha256=manifest.raw_sha256,
        manifest_aggregate_sha256=manifest.aggregate_sha256,
        manifest_file_count=manifest.total_files,
        manifest_total_bytes=manifest.total_bytes,
        artifacts=artifacts,
        profile_selection_sha256=profile_selection_hash,
        map_inventory_sha256=inventory_hash,
        candidate_inventory_sha256=candidate_inventory_hash,
        candidate_evidence_sha256=candidate_evidence_hash,
        script_handoff_evidence_sha256=script_handoff_hash,
        map_evidence_sha256=map_evidence_hash,
        accepted_evidence_sha256=accepted_hash,
        rejected_evidence_sha256=rejected_hash,
        corpus_sha256=_canonical_sha256(identity_basis),
    )
    if strict and not report.complete:
        raise MapCorpusStrictError(report)
    return report


# Concise alias for callers already operating at the corpus/import boundary.
scan_corpus = scan_map_corpus


__all__ = [
    "EFFECTIVE_ASSET_MANIFEST_RELATIVE",
    "EFFECTIVE_ASSET_MANIFEST_SCHEMA",
    "EFFECTIVE_ASSET_MANIFEST_VERSION",
    "MAP_CORPUS_SCHEMA",
    "MAP_CORPUS_SCHEMA_VERSION",
    "MAP_ARTIFACT_KINDS",
    "MAP_DISCOVERY_REASON_ORDER",
    "MAP_DISCOVERY_REASONS",
    "MAP_SIGNATURE_PROBE_BYTES",
    "DEFAULT_MAP_PROFILE",
    "MAP_PROFILES",
    "SCB_SCRIPT_CONTAINER_CORE_CHUNKS",
    "SCRIPT_CONTAINER_ARTIFACT",
    "TERRAIN_MAP_ARTIFACT",
    "TERRAIN_MAP_CORE_CHUNKS",
    "UNCLASSIFIED_CANDIDATE_ARTIFACT",
    "MAX_MAP_CORPUS_FILES",
    "MAX_MAP_CORPUS_TOTAL_BYTES",
    "MapCorpusError",
    "MapCorpusFile",
    "MapCorpusLimitError",
    "MapCorpusReport",
    "MapCorpusStrictError",
    "scan_corpus",
    "scan_map_corpus",
]
