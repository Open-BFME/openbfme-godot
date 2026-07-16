"""Fail-closed evidence join for map script-container conversion closure.

The map-native corpus deliberately records SAGE script/camera containers as
no-output handoffs.  The independent SCB native corpus converts those same
sources and exact-wire backtests every persisted native document.  This
module joins the two *manifests* without opening either corpus tree.

Only canonical manifest bytes or JSON-ready mappings are accepted.  The
returned evidence replaces authored source and output paths with deterministic
identities.  It proves manifest-level source accounting and exact-wire SCB
conversion evidence; it makes no gameplay, audiovisual, or GLB claim.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
from typing import Any, Iterable, Mapping, Sequence

from .paths import safe_relative_parts


MAP_NATIVE_SCHEMA = "openbfme.map-native-corpus"
MAP_NATIVE_SCHEMA_VERSION = 0
SCB_NATIVE_SCHEMA = "openbfme.scb-native-corpus"
SCB_NATIVE_SCHEMA_VERSION = 0
SCB_BACKTEST_SCHEMA = "openbfme.sage-scb-backtest"
SCB_BACKTEST_SCHEMA_VERSION = 0
MAP_SCRIPT_CLOSURE_SCHEMA = "openbfme.map-script-closure-evidence"
MAP_SCRIPT_CLOSURE_SCHEMA_VERSION = 0

MAX_DOCUMENT_BYTES = 64 * 1024 * 1024
MAX_INVENTORY_ITEMS = 250_000
MAX_TOTAL_BYTES = 64 * 1024 * 1024 * 1024

_SHA256_CHARACTERS = frozenset("0123456789abcdef")
_WINDOWS_FORBIDDEN_FILENAME_CHARACTERS = frozenset('<>"|?*')
_MAP_DISCOVERY_REASON_ORDER = (
    "map-suffix",
    "ear-signature",
    "ckmp-signature",
)
_MAP_PROFILES = frozenset({"multiplayer", "scenario", "library", "placeholder"})
_PROFILE_RUNNABLE = {
    "multiplayer": True,
    "scenario": True,
    "library": False,
    "placeholder": False,
}
_MAP_PROFILE_VERSION = 1
_MAP_OBJECT_RESOLUTION_KEYS = frozenset(
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
_SCB_HARD_MAX_FILES = 1_024
_SCB_HARD_MAX_TOTAL_BYTES = 1024 * 1024 * 1024


class MapScriptClosureError(ValueError):
    """Raised when the two manifests cannot prove one exact source closure."""


@dataclass(frozen=True, slots=True)
class MapScriptClosureEntry:
    """Identifier-free evidence for one exact map-handoff/SCB-source join."""

    source_id: str
    source_bytes: int
    source_sha256: str
    handoff_evidence_sha256: str
    output_id: str
    native_bytes: int
    native_sha256: str
    semantic_sha256: str
    backtest_evidence_sha256: str

    def neutral(self) -> dict[str, object]:
        return {
            "sourceId": self.source_id,
            "sourceBytes": self.source_bytes,
            "sourceSha256": self.source_sha256,
            "handoffEvidenceSha256": self.handoff_evidence_sha256,
            "outputId": self.output_id,
            "nativeBytes": self.native_bytes,
            "nativeSha256": self.native_sha256,
            "semanticSha256": self.semantic_sha256,
            "backtestEvidenceSha256": self.backtest_evidence_sha256,
        }


@dataclass(frozen=True, slots=True)
class MapScriptClosureOutput:
    """Identifier-free evidence for one deduplicated native SCB output."""

    output_id: str
    source_sha256: str
    native_bytes: int
    native_sha256: str
    semantic_sha256: str
    backtest_evidence_sha256: str

    def neutral(self) -> dict[str, object]:
        return {
            "outputId": self.output_id,
            "sourceSha256": self.source_sha256,
            "nativeBytes": self.native_bytes,
            "nativeSha256": self.native_sha256,
            "semanticSha256": self.semantic_sha256,
            "backtestEvidenceSha256": self.backtest_evidence_sha256,
        }


@dataclass(frozen=True, slots=True)
class MapScriptClosureEvidence:
    """Immutable, payload-free proof that every map SCB handoff was closed."""

    entries: tuple[MapScriptClosureEntry, ...]
    outputs: tuple[MapScriptClosureOutput, ...]
    map_native_manifest_sha256: str
    map_native_identity_sha256: str
    map_source_manifest_sha256: str
    map_source_manifest_aggregate_sha256: str
    scb_native_manifest_sha256: str
    scb_native_identity_sha256: str
    scb_source_manifest_sha256: str
    scb_source_manifest_aggregate_sha256: str
    cross_manifest_exact_source_join: bool
    identity_sha256: str
    evidence_sha256: str

    @property
    def source_bytes(self) -> int:
        return sum(item.source_bytes for item in self.entries)

    @property
    def native_bytes(self) -> int:
        return sum(item.native_bytes for item in self.outputs)

    @property
    def complete(self) -> bool:
        return len({item.source_id for item in self.entries}) == len(self.entries) and {
            item.output_id for item in self.entries
        } == {item.output_id for item in self.outputs}

    def _neutral(self, *, include_evidence_sha256: bool) -> dict[str, object]:
        hashes: dict[str, object] = {
            "mapNativeManifestSha256": self.map_native_manifest_sha256,
            "mapNativeIdentitySha256": self.map_native_identity_sha256,
            "mapSourceManifestSha256": self.map_source_manifest_sha256,
            "mapSourceManifestAggregateSha256": (
                self.map_source_manifest_aggregate_sha256
            ),
            "scbNativeManifestSha256": self.scb_native_manifest_sha256,
            "scbNativeIdentitySha256": self.scb_native_identity_sha256,
            "scbSourceManifestSha256": self.scb_source_manifest_sha256,
            "scbSourceManifestAggregateSha256": (
                self.scb_source_manifest_aggregate_sha256
            ),
            "identitySha256": self.identity_sha256,
        }
        if include_evidence_sha256:
            hashes["evidenceSha256"] = self.evidence_sha256
        return {
            "schema": MAP_SCRIPT_CLOSURE_SCHEMA,
            "schemaVersion": MAP_SCRIPT_CLOSURE_SCHEMA_VERSION,
            "summary": {
                "mapScriptHandoffCount": len(self.entries),
                "scbSourceCount": len(self.entries),
                "uniqueNativeOutputCount": len(self.outputs),
                "sourceBytes": self.source_bytes,
                "nativeBytes": self.native_bytes,
                "exactWireBacktestCount": len(self.outputs),
                "crossManifestExactSourceJoin": (self.cross_manifest_exact_source_join),
                "complete": self.complete,
                "gameplayFidelityClaimed": False,
                "glbConversionClaimed": False,
            },
            "hashes": hashes,
            "entries": [item.neutral() for item in self.entries],
            "outputs": [item.neutral() for item in self.outputs],
        }

    def evidence_hash_basis(self) -> dict[str, object]:
        return self._neutral(include_evidence_sha256=False)

    def neutral(self) -> dict[str, object]:
        return self._neutral(include_evidence_sha256=True)

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class _MapOutput:
    path: str
    source_sha256: str
    profile: str
    profile_version: int
    runnable: bool
    structural_status: str
    file_count: int
    byte_length: int
    unresolved_type_count: int
    unresolved_placement_count: int


@dataclass(frozen=True, slots=True)
class _MapEntry:
    source_path: str
    source_bytes: int
    source_sha256: str
    discovery_reasons: tuple[str, ...]
    runnable: bool
    output_path: str


@dataclass(frozen=True, slots=True)
class _MapHandoff:
    source_path: str
    source_bytes: int
    source_sha256: str
    discovery_reasons: tuple[str, ...]
    handoff_evidence_sha256: str


@dataclass(frozen=True, slots=True)
class _MapManifest:
    manifest_sha256: str
    identity_sha256: str
    source_manifest_sha256: str
    source_manifest_aggregate_sha256: str
    handoffs: tuple[_MapHandoff, ...]


@dataclass(frozen=True, slots=True)
class _ScbEntry:
    source_path: str
    source_bytes: int
    source_sha256: str
    output_path: str


@dataclass(frozen=True, slots=True)
class _ScbOutput:
    path: str
    source_sha256: str
    native_bytes: int
    native_sha256: str
    semantic_sha256: str
    backtest_evidence_sha256: str


@dataclass(frozen=True, slots=True)
class _ScbManifest:
    manifest_sha256: str
    identity_sha256: str
    source_manifest_sha256: str
    source_manifest_aggregate_sha256: str
    entries: tuple[_ScbEntry, ...]
    outputs: tuple[_ScbOutput, ...]


class _DuplicateJsonKey(ValueError):
    pass


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and value == value.casefold()
        and all(character in _SHA256_CHARACTERS for character in value)
    )


def _is_int(value: object, *, minimum: int = 0) -> bool:
    return not isinstance(value, bool) and isinstance(value, int) and value >= minimum


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
    raise ValueError(f"non-finite JSON constant is forbidden: {value}")


def _pretty_json_bytes(value: object, *, ensure_ascii: bool) -> bytes:
    try:
        return (
            json.dumps(
                value,
                indent=2,
                sort_keys=True,
                ensure_ascii=ensure_ascii,
                allow_nan=False,
            )
            + "\n"
        ).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise MapScriptClosureError("manifest is not canonical JSON data") from exc


def _map_sha256(value: object) -> str:
    try:
        raw = (
            json.dumps(
                value,
                sort_keys=True,
                ensure_ascii=True,
                allow_nan=False,
                separators=(",", ":"),
            )
            + "\n"
        ).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise MapScriptClosureError(
            "map-native evidence is not canonical JSON"
        ) from exc
    return hashlib.sha256(raw).hexdigest()


def _scb_sha256(value: object) -> str:
    try:
        raw = json.dumps(
            value,
            sort_keys=True,
            ensure_ascii=False,
            allow_nan=False,
            separators=(",", ":"),
        ).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise MapScriptClosureError("SCB evidence is not canonical JSON") from exc
    return hashlib.sha256(raw).hexdigest()


def _closure_sha256(value: object) -> str:
    try:
        raw = json.dumps(
            value,
            sort_keys=True,
            ensure_ascii=True,
            allow_nan=False,
            separators=(",", ":"),
        ).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise MapScriptClosureError("closure evidence is not canonical JSON") from exc
    return hashlib.sha256(raw).hexdigest()


def _document(
    value: Mapping[str, object] | bytes,
    *,
    label: str,
    ensure_ascii: bool,
) -> tuple[dict[str, Any], bytes]:
    if isinstance(value, bytes):
        if not 1 <= len(value) <= MAX_DOCUMENT_BYTES:
            raise MapScriptClosureError(f"{label} exceeds its size bound")
        try:
            parsed = json.loads(
                value.decode("utf-8"),
                object_pairs_hook=_object_without_duplicate_keys,
                parse_constant=_reject_json_constant,
            )
        except (
            UnicodeDecodeError,
            json.JSONDecodeError,
            ValueError,
        ) as exc:
            raise MapScriptClosureError(f"{label} is invalid JSON") from exc
        if not isinstance(parsed, dict):
            raise MapScriptClosureError(f"{label} root must be an object")
        canonical = _pretty_json_bytes(parsed, ensure_ascii=ensure_ascii)
        if value != canonical:
            raise MapScriptClosureError(f"{label} encoding is not canonical")
        return parsed, canonical
    if not isinstance(value, Mapping):
        raise TypeError(f"{label} must be a mapping or canonical JSON bytes")
    try:
        raw = _pretty_json_bytes(dict(value), ensure_ascii=ensure_ascii)
        parsed = json.loads(raw.decode("utf-8"))
    except (TypeError, ValueError) as exc:
        raise MapScriptClosureError(f"{label} is not canonical JSON data") from exc
    if len(raw) > MAX_DOCUMENT_BYTES or not isinstance(parsed, dict):
        raise MapScriptClosureError(f"{label} exceeds its size bound")
    return parsed, raw


def _safe_path(value: object, *, label: str) -> str:
    if not isinstance(value, str):
        raise MapScriptClosureError(f"{label} path is invalid")
    try:
        parts = safe_relative_parts(value)
    except (TypeError, ValueError) as exc:
        raise MapScriptClosureError(f"{label} path is unsafe") from exc
    if (
        "/".join(parts) != value
        or "\\" in value
        or any(
            character in _WINDOWS_FORBIDDEN_FILENAME_CHARACTERS
            for part in parts
            for character in part
        )
    ):
        raise MapScriptClosureError(f"{label} path is not canonical")
    return value


def _case_unique_canonical(paths: Sequence[str], *, label: str) -> None:
    if list(paths) != sorted(paths, key=lambda item: (item.casefold(), item)):
        raise MapScriptClosureError(f"{label} inventory is not canonical")
    if len({item.casefold() for item in paths}) != len(paths):
        raise MapScriptClosureError(f"{label} inventory case-collides")
    folded = {item.casefold() for item in paths}
    for path in folded:
        parts = path.split("/")
        if any("/".join(parts[:index]) in folded for index in range(1, len(parts))):
            raise MapScriptClosureError(
                f"{label} inventory has a file/directory collision"
            )


def _profile(raw: Mapping[str, Any], *, label: str) -> tuple[str, int, bool, str]:
    profile = raw.get("mapKind")
    version = raw.get("profileVersion")
    runnable = raw.get("runnable")
    structural_status = raw.get("structuralStatus")
    if (
        profile not in _MAP_PROFILES
        or version != _MAP_PROFILE_VERSION
        or isinstance(version, bool)
        or not isinstance(runnable, bool)
        or runnable is not _PROFILE_RUNNABLE.get(str(profile))
        or structural_status
        != ("runnable-structure" if runnable else "non-runnable-structural-map")
    ):
        raise MapScriptClosureError(f"{label} profile evidence is invalid")
    return str(profile), int(version), runnable, str(structural_status)


def _discovery_reasons(raw: object, *, label: str) -> tuple[str, ...]:
    if (
        not isinstance(raw, list)
        or not raw
        or any(not isinstance(item, str) for item in raw)
    ):
        raise MapScriptClosureError(f"{label} discovery evidence is invalid")
    reasons = tuple(raw)
    canonical = tuple(item for item in _MAP_DISCOVERY_REASON_ORDER if item in reasons)
    if (
        reasons != canonical
        or len(reasons) != len(set(reasons))
        or {"ear-signature", "ckmp-signature"}.issubset(reasons)
    ):
        raise MapScriptClosureError(f"{label} discovery evidence is invalid")
    return reasons


def _map_output_relative(source_sha256: str, profile: str, version: int) -> str:
    return (
        f"maps/sha256/{source_sha256[:2]}/{source_sha256}/profile/{profile}-v{version}"
    )


def _parse_map_resolution(raw: object) -> dict[str, Any]:
    if not isinstance(raw, dict) or set(raw) != _MAP_OBJECT_RESOLUTION_KEYS:
        raise MapScriptClosureError("map-native object-resolution evidence is invalid")
    status = raw.get("resolutionStatus")
    integer_keys = _MAP_OBJECT_RESOLUTION_KEYS - {"resolutionStatus"}
    if status not in {"complete", "partial"} or not all(
        _is_int(raw.get(key)) for key in integer_keys
    ):
        raise MapScriptClosureError("map-native object-resolution evidence is invalid")
    if (
        raw["resolvedTypeCount"] != raw["boundTypeCount"] + raw["logicalTypeCount"]
        or raw["resolvedPlacementCount"]
        != raw["boundPlacementCount"] + raw["logicalPlacementCount"]
        or raw["typeCount"] != raw["resolvedTypeCount"] + raw["unresolvedTypeCount"]
        or raw["placementCount"]
        != raw["resolvedPlacementCount"] + raw["unresolvedPlacementCount"]
        or (status == "complete") != (raw["unresolvedTypeCount"] == 0)
    ):
        raise MapScriptClosureError("map-native object-resolution totals are invalid")
    return raw


def _parse_map_output(raw: object) -> _MapOutput:
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
        raise MapScriptClosureError("map-native output shape is invalid")
    profile, version, runnable, structural_status = _profile(
        raw, label="map-native output"
    )
    source_sha256 = raw.get("sourceSha256")
    path = _safe_path(raw.get("path"), label="map-native output")
    inventory = raw.get("inventory")
    if (
        not _is_sha256(source_sha256)
        or path != _map_output_relative(str(source_sha256), profile, version)
        or not isinstance(inventory, list)
        or not 1 <= len(inventory) <= MAX_INVENTORY_ITEMS
    ):
        raise MapScriptClosureError("map-native output metadata is invalid")
    rows: list[dict[str, object]] = []
    inventory_paths: list[str] = []
    for item in inventory:
        if not isinstance(item, dict) or set(item) != {"path", "bytes", "sha256"}:
            raise MapScriptClosureError("map-native artifact shape is invalid")
        artifact_path = _safe_path(item.get("path"), label="map-native artifact")
        byte_length = item.get("bytes")
        digest = item.get("sha256")
        if not _is_int(byte_length) or not _is_sha256(digest):
            raise MapScriptClosureError("map-native artifact evidence is invalid")
        inventory_paths.append(artifact_path)
        rows.append({"path": artifact_path, "bytes": byte_length, "sha256": digest})
    _case_unique_canonical(inventory_paths, label="map-native artifact")
    total_bytes = sum(int(item["bytes"]) for item in rows)
    expected_tree = _map_sha256(
        {
            "schema": "openbfme.map-native-output-tree",
            "schemaVersion": 0,
            "files": rows,
        }
    )
    if (
        raw.get("fileCount") != len(rows)
        or raw.get("bytes") != total_bytes
        or total_bytes > MAX_TOTAL_BYTES
        or raw.get("treeSha256") != expected_tree
    ):
        raise MapScriptClosureError("map-native output inventory seal is invalid")
    backtest = raw.get("backtestEvidence")
    facts = backtest.get("facts") if isinstance(backtest, dict) else None
    if (
        not isinstance(backtest, dict)
        or backtest.get("valid") is not True
        or backtest.get("runnable") is not runnable
        or backtest.get("gameplayFidelityClaimed") is not False
        or not isinstance(facts, dict)
        or facts.get("mapKind") != profile
        or facts.get("profileVersion") != version
        or facts.get("runnable") is not runnable
    ):
        raise MapScriptClosureError("map-native backtest evidence is invalid")
    resolution = _parse_map_resolution(raw.get("objectResolution"))
    return _MapOutput(
        path,
        str(source_sha256),
        profile,
        version,
        runnable,
        structural_status,
        len(rows),
        total_bytes,
        int(resolution["unresolvedTypeCount"]),
        int(resolution["unresolvedPlacementCount"]),
    )


def _parse_map_entry(raw: object, outputs: Mapping[str, _MapOutput]) -> _MapEntry:
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
        raise MapScriptClosureError("map-native entry shape is invalid")
    profile = _profile(raw, label="map-native entry")
    source_path = _safe_path(raw.get("sourcePath"), label="map-native source")
    output_path = _safe_path(raw.get("outputPath"), label="map-native output reference")
    source_bytes = raw.get("sourceBytes")
    source_sha256 = raw.get("sourceSha256")
    reasons = _discovery_reasons(raw.get("discoveryReasons"), label="map-native entry")
    if (
        not _is_int(source_bytes)
        or not _is_sha256(source_sha256)
        or not _is_sha256(raw.get("censusEvidenceSha256"))
        or not _is_sha256(raw.get("bodySha256"))
        or raw.get("status") != "accepted"
        or raw.get("rejectionCode") is not None
        or raw.get("rejectionEvidenceSha256") is not None
    ):
        raise MapScriptClosureError("map-native entry evidence is invalid")
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
        raise MapScriptClosureError("map-native source/output mapping is invalid")
    return _MapEntry(
        source_path,
        int(source_bytes),
        str(source_sha256),
        reasons,
        profile[2],
        output_path,
    )


def _parse_map_handoff(raw: object) -> _MapHandoff:
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
        raise MapScriptClosureError("map-native handoff shape is invalid")
    source_path = _safe_path(raw.get("sourcePath"), label="map-native handoff")
    source_bytes = raw.get("sourceBytes")
    source_sha256 = raw.get("sourceSha256")
    reasons = _discovery_reasons(
        raw.get("discoveryReasons"), label="map-native handoff"
    )
    hashes = (
        raw.get("classificationEvidenceSha256"),
        raw.get("chunkNameSetSha256"),
        raw.get("censusEvidenceSha256"),
        raw.get("bodySha256"),
        raw.get("handoffEvidenceSha256"),
    )
    if (
        not _is_int(source_bytes)
        or not _is_sha256(source_sha256)
        or raw.get("artifactKind") != "script-container"
        or not all(_is_sha256(item) for item in hashes)
        or raw.get("status") != "handed-off"
        or raw.get("rejectionCode") is not None
        or raw.get("rejectionEvidenceSha256") is not None
        or raw.get("outputPath") is not None
    ):
        raise MapScriptClosureError("map-native handoff evidence is invalid")
    evidence_basis = {
        "schema": "openbfme.map-native-handoff-evidence",
        "schemaVersion": 0,
        "sourcePath": source_path,
        "sourceBytes": source_bytes,
        "sourceSha256": source_sha256,
        "discoveryReasons": list(reasons),
        "artifactKind": "script-container",
        "classificationEvidenceSha256": hashes[0],
        "chunkNameSetSha256": hashes[1],
        "censusEvidenceSha256": hashes[2],
        "status": "handed-off",
        "rejectionCode": None,
        "rejectionEvidenceSha256": None,
        "conversionDisposition": "no-native-output",
    }
    if raw.get("handoffEvidenceSha256") != _map_sha256(evidence_basis):
        raise MapScriptClosureError("map-native handoff evidence seal is invalid")
    return _MapHandoff(
        source_path,
        int(source_bytes),
        str(source_sha256),
        reasons,
        str(raw["handoffEvidenceSha256"]),
    )


def _map_summary(
    entries: Sequence[_MapEntry],
    handoffs: Sequence[_MapHandoff],
    outputs: Sequence[_MapOutput],
) -> dict[str, object]:
    output_lookup = {item.path: item for item in outputs}
    unresolved_maps = 0
    unresolved_types = 0
    unresolved_placements = 0
    for entry in entries:
        output = output_lookup[entry.output_path]
        if output.unresolved_type_count:
            unresolved_maps += 1
        unresolved_types += output.unresolved_type_count
        unresolved_placements += output.unresolved_placement_count
    candidates: tuple[_MapEntry | _MapHandoff, ...] = (*entries, *handoffs)
    return {
        "candidateArtifactCount": len(candidates),
        "candidateArtifactBytes": sum(item.source_bytes for item in candidates),
        "selectedMapCount": len(entries),
        "selectedMapBytes": sum(item.source_bytes for item in entries),
        "scriptContainerCount": len(handoffs),
        "scriptContainerBytes": sum(item.source_bytes for item in handoffs),
        "scriptContainerHandoffCount": len(handoffs),
        "unclassifiedCandidateCount": 0,
        "handoffEvidenceSha256": _map_sha256(
            {
                "schema": "openbfme.map-native-handoff-set",
                "schemaVersion": 0,
                "handoffs": [item.handoff_evidence_sha256 for item in handoffs],
            }
        ),
        "mapSuffixDiscoveryCount": sum(
            "map-suffix" in item.discovery_reasons for item in candidates
        ),
        "earSignatureDiscoveryCount": sum(
            "ear-signature" in item.discovery_reasons for item in candidates
        ),
        "ckmpSignatureDiscoveryCount": sum(
            "ckmp-signature" in item.discovery_reasons for item in candidates
        ),
        "signatureDiscoveredMapCount": sum(
            bool({"ear-signature", "ckmp-signature"} & set(item.discovery_reasons))
            for item in entries
        ),
        "signatureDiscoveredCandidateCount": sum(
            bool({"ear-signature", "ckmp-signature"} & set(item.discovery_reasons))
            for item in candidates
        ),
        "acceptedMapCount": len(entries),
        "rejectedMapCount": 0,
        "runnableMapCount": sum(item.runnable for item in entries),
        "nonRunnableMapCount": sum(not item.runnable for item in entries),
        "uniqueOutputMapCount": len(outputs),
        "outputFileCount": sum(item.file_count for item in outputs),
        "outputBytes": sum(item.byte_length for item in outputs),
        "mapsWithUnresolvedObjectBindings": unresolved_maps,
        "unresolvedObjectTypeCount": unresolved_types,
        "unresolvedObjectPlacementCount": unresolved_placements,
        "structuralConversionComplete": True,
        "sourceAccountingComplete": True,
        "objectBindingSemanticsComplete": unresolved_types == 0,
        "gameplayFidelityClaimed": False,
        "gameplaySemanticFidelityClaimed": False,
        "published": True,
    }


def _validate_map_manifest(value: Mapping[str, object] | bytes) -> _MapManifest:
    document, raw = _document(
        value,
        label="map-native manifest",
        ensure_ascii=True,
    )
    if set(document) != {
        "schema",
        "schemaVersion",
        "selection",
        "summary",
        "entries",
        "handoffs",
        "outputs",
        "identitySha256",
    }:
        raise MapScriptClosureError("map-native manifest shape is invalid")
    if (
        document.get("schema") != MAP_NATIVE_SCHEMA
        or document.get("schemaVersion") != MAP_NATIVE_SCHEMA_VERSION
        or isinstance(document.get("schemaVersion"), bool)
    ):
        raise MapScriptClosureError("map-native manifest schema is unsupported")
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
        raise MapScriptClosureError("map-native selection seals are invalid")
    raw_outputs = document.get("outputs")
    raw_entries = document.get("entries")
    raw_handoffs = document.get("handoffs")
    if (
        not isinstance(raw_outputs, list)
        or not isinstance(raw_entries, list)
        or not isinstance(raw_handoffs, list)
        or len(raw_outputs) > MAX_INVENTORY_ITEMS
        or len(raw_entries) + len(raw_handoffs) > MAX_INVENTORY_ITEMS
    ):
        raise MapScriptClosureError("map-native inventories are invalid")
    outputs = tuple(_parse_map_output(item) for item in raw_outputs)
    output_paths = [item.path for item in outputs]
    _case_unique_canonical(output_paths, label="map-native output")
    outputs_by_fold = {item.path.casefold(): item for item in outputs}
    entries = tuple(_parse_map_entry(item, outputs_by_fold) for item in raw_entries)
    handoffs = tuple(_parse_map_handoff(item) for item in raw_handoffs)
    entry_paths = [item.source_path for item in entries]
    handoff_paths = [item.source_path for item in handoffs]
    _case_unique_canonical(entry_paths, label="map-native source")
    _case_unique_canonical(handoff_paths, label="map-native handoff")
    if not entries and not handoffs:
        raise MapScriptClosureError("map-native source inventory is empty")
    if len({item.casefold() for item in (*entry_paths, *handoff_paths)}) != (
        len(entry_paths) + len(handoff_paths)
    ):
        raise MapScriptClosureError("map-native source inventories case-collide")
    if {item.output_path.casefold() for item in entries} != set(outputs_by_fold):
        raise MapScriptClosureError("map-native output is unreferenced")
    if document.get("summary") != _map_summary(entries, handoffs, outputs):
        raise MapScriptClosureError("map-native summary is inconsistent")
    basis = {key: item for key, item in document.items() if key != "identitySha256"}
    identity = document.get("identitySha256")
    if not _is_sha256(identity) or identity != _map_sha256(basis):
        raise MapScriptClosureError("map-native identity SHA-256 is invalid")
    return _MapManifest(
        hashlib.sha256(raw).hexdigest(),
        str(identity),
        str(selection["sourceManifestSha256"]),
        str(selection["sourceManifestAggregateSha256"]),
        handoffs,
    )


def _scb_inventory_sha256(domain: str, rows: Iterable[Mapping[str, object]]) -> str:
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


def _scb_output_relative(source_sha256: str, native_sha256: str) -> str:
    return f"objects/{source_sha256[:2]}/{source_sha256}/{native_sha256}.json"


def _parse_scb_entry(raw: object) -> _ScbEntry:
    if not isinstance(raw, dict) or set(raw) != {
        "sourcePath",
        "sourceBytes",
        "sourceSha256",
        "outputPath",
    }:
        raise MapScriptClosureError("SCB source entry shape is invalid")
    source_path = _safe_path(raw.get("sourcePath"), label="SCB source")
    output_path = _safe_path(raw.get("outputPath"), label="SCB output reference")
    source_bytes = raw.get("sourceBytes")
    source_sha256 = raw.get("sourceSha256")
    if (
        not _is_int(source_bytes)
        or not _is_sha256(source_sha256)
        or not source_path.casefold().endswith(".scb")
    ):
        raise MapScriptClosureError("SCB source entry evidence is invalid")
    return _ScbEntry(
        source_path,
        int(source_bytes),
        str(source_sha256),
        output_path,
    )


def _parse_scb_backtest(
    raw: object, *, source_bytes: int, source_sha256: str, semantic_sha256: str
) -> str:
    expected = {
        "schema",
        "schemaVersion",
        "accepted",
        "exactWireMatch",
        "sourceBytes",
        "decodedBodyBytes",
        "chunkCount",
        "sourceSha256",
        "decodedBodySha256",
        "reconstructedBodySha256",
        "semanticSha256",
        "evidenceSha256",
    }
    if not isinstance(raw, dict) or set(raw) != expected:
        raise MapScriptClosureError("SCB backtest evidence shape is invalid")
    if (
        raw.get("schema") != SCB_BACKTEST_SCHEMA
        or raw.get("schemaVersion") != SCB_BACKTEST_SCHEMA_VERSION
        or isinstance(raw.get("schemaVersion"), bool)
        or raw.get("accepted") is not True
        or raw.get("exactWireMatch") is not True
        or raw.get("sourceBytes") != source_bytes
        or raw.get("sourceSha256") != source_sha256
        or not _is_int(raw.get("decodedBodyBytes"))
        or not _is_int(raw.get("chunkCount"))
        or not _is_sha256(raw.get("decodedBodySha256"))
        or raw.get("reconstructedBodySha256") != raw.get("decodedBodySha256")
        or raw.get("semanticSha256") != semantic_sha256
    ):
        raise MapScriptClosureError("SCB exact-wire backtest evidence is invalid")
    basis = {
        "schema": SCB_BACKTEST_SCHEMA,
        "schemaVersion": SCB_BACKTEST_SCHEMA_VERSION,
        "sourceBytes": source_bytes,
        "decodedBodyBytes": raw["decodedBodyBytes"],
        "chunkCount": raw["chunkCount"],
        "sourceSha256": source_sha256,
        "decodedBodySha256": raw["decodedBodySha256"],
        "reconstructedBodySha256": raw["reconstructedBodySha256"],
        "semanticSha256": semantic_sha256,
        "exactWireMatch": True,
    }
    evidence_sha256 = raw.get("evidenceSha256")
    if not _is_sha256(evidence_sha256) or evidence_sha256 != _scb_sha256(basis):
        raise MapScriptClosureError("SCB backtest evidence SHA-256 is invalid")
    return str(evidence_sha256)


def _parse_scb_output(raw: object) -> _ScbOutput:
    if not isinstance(raw, dict) or set(raw) != {
        "path",
        "sourceSha256",
        "nativeBytes",
        "nativeSha256",
        "semanticSha256",
        "backtestEvidence",
    }:
        raise MapScriptClosureError("SCB output shape is invalid")
    path = _safe_path(raw.get("path"), label="SCB native output")
    source_sha256 = raw.get("sourceSha256")
    native_bytes = raw.get("nativeBytes")
    native_sha256 = raw.get("nativeSha256")
    semantic_sha256 = raw.get("semanticSha256")
    if (
        not _is_sha256(source_sha256)
        or not _is_int(native_bytes, minimum=1)
        or not _is_sha256(native_sha256)
        or not _is_sha256(semantic_sha256)
        or path != _scb_output_relative(str(source_sha256), str(native_sha256))
    ):
        raise MapScriptClosureError("SCB output evidence is invalid")
    backtest_sha256 = _parse_scb_backtest(
        raw.get("backtestEvidence"),
        source_bytes=int(raw["backtestEvidence"].get("sourceBytes", -1))
        if isinstance(raw.get("backtestEvidence"), dict)
        else -1,
        source_sha256=str(source_sha256),
        semantic_sha256=str(semantic_sha256),
    )
    return _ScbOutput(
        path,
        str(source_sha256),
        int(native_bytes),
        str(native_sha256),
        str(semantic_sha256),
        backtest_sha256,
    )


def _scb_summary(
    entries: Sequence[_ScbEntry], outputs: Sequence[_ScbOutput]
) -> dict[str, object]:
    return {
        "selectedScbCount": len(entries),
        "selectedScbBytes": sum(item.source_bytes for item in entries),
        "uniqueOutputCount": len(outputs),
        "deduplicatedSourceCount": len(entries) - len(outputs),
        "nativeBytes": sum(item.native_bytes for item in outputs),
        "exactWireBacktestCount": len(outputs),
        "structuralConversionComplete": bool(entries)
        and len({item.output_path for item in entries}) == len(outputs),
        "published": True,
    }


def _validate_scb_manifest(value: Mapping[str, object] | bytes) -> _ScbManifest:
    document, raw = _document(
        value,
        label="SCB native manifest",
        ensure_ascii=False,
    )
    if set(document) != {
        "schema",
        "schemaVersion",
        "source",
        "selection",
        "limits",
        "summary",
        "entries",
        "outputs",
        "outputTreeSha256",
        "requestSha256",
        "identitySha256",
    }:
        raise MapScriptClosureError("SCB native manifest shape is invalid")
    if (
        document.get("schema") != SCB_NATIVE_SCHEMA
        or document.get("schemaVersion") != SCB_NATIVE_SCHEMA_VERSION
        or isinstance(document.get("schemaVersion"), bool)
    ):
        raise MapScriptClosureError("SCB native manifest schema is unsupported")
    source = document.get("source")
    if not isinstance(source, dict) or set(source) != {
        "manifestAggregateSha256",
        "manifestFileCount",
        "manifestSha256",
        "manifestTotalBytes",
    }:
        raise MapScriptClosureError("SCB source manifest seals are invalid")
    if (
        not _is_sha256(source.get("manifestAggregateSha256"))
        or not _is_sha256(source.get("manifestSha256"))
        or not _is_int(source.get("manifestFileCount"))
        or not _is_int(source.get("manifestTotalBytes"))
    ):
        raise MapScriptClosureError("SCB source manifest seals are invalid")
    selection = document.get("selection")
    if not isinstance(selection, dict) or set(selection) != {
        "caseInsensitiveSuffix",
        "files",
        "bytes",
        "inventorySha256",
    }:
        raise MapScriptClosureError("SCB selection shape is invalid")
    limits = document.get("limits")
    if not isinstance(limits, dict) or set(limits) != {
        "hardMaxFiles",
        "hardMaxTotalBytes",
        "maxFiles",
        "maxTotalBytes",
    }:
        raise MapScriptClosureError("SCB limits shape is invalid")
    if (
        limits.get("hardMaxFiles") != _SCB_HARD_MAX_FILES
        or limits.get("hardMaxTotalBytes") != _SCB_HARD_MAX_TOTAL_BYTES
        or not _is_int(limits.get("maxFiles"), minimum=1)
        or not _is_int(limits.get("maxTotalBytes"), minimum=1)
        or limits["maxFiles"] > _SCB_HARD_MAX_FILES
        or limits["maxTotalBytes"] > _SCB_HARD_MAX_TOTAL_BYTES
    ):
        raise MapScriptClosureError("SCB limits are invalid")
    raw_entries = document.get("entries")
    raw_outputs = document.get("outputs")
    if (
        not isinstance(raw_entries, list)
        or not isinstance(raw_outputs, list)
        or len(raw_entries) > MAX_INVENTORY_ITEMS
        or len(raw_outputs) > MAX_INVENTORY_ITEMS
    ):
        raise MapScriptClosureError("SCB inventories are invalid")
    entries = tuple(_parse_scb_entry(item) for item in raw_entries)
    entry_paths = [item.source_path for item in entries]
    _case_unique_canonical(entry_paths, label="SCB source")
    outputs = tuple(_parse_scb_output(item) for item in raw_outputs)
    output_paths = [item.path for item in outputs]
    _case_unique_canonical(output_paths, label="SCB output")
    if len({item.source_sha256 for item in outputs}) != len(outputs):
        raise MapScriptClosureError("SCB outputs duplicate a source identity")
    outputs_by_path = {item.path: item for item in outputs}
    for entry in entries:
        output = outputs_by_path.get(entry.output_path)
        if output is None or output.source_sha256 != entry.source_sha256:
            raise MapScriptClosureError("SCB source/output mapping is invalid")
        evidence = next(
            item.get("backtestEvidence")
            for item in raw_outputs
            if isinstance(item, dict) and item.get("path") == output.path
        )
        if not isinstance(evidence, dict) or (
            evidence.get("sourceBytes") != entry.source_bytes
            or evidence.get("sourceSha256") != entry.source_sha256
        ):
            raise MapScriptClosureError("SCB source/backtest mapping is invalid")
    if {item.output_path for item in entries} != set(outputs_by_path):
        raise MapScriptClosureError("SCB corpus contains an unreferenced output")
    selected_rows = [
        {
            "path": item.source_path,
            "size": item.source_bytes,
            "sha256": item.source_sha256,
        }
        for item in entries
    ]
    selected_bytes = sum(item.source_bytes for item in entries)
    inventory_sha256 = _scb_inventory_sha256(
        "openbfme.scb-native-corpus-selection-v0", selected_rows
    )
    if selection != {
        "caseInsensitiveSuffix": ".scb",
        "files": len(entries),
        "bytes": selected_bytes,
        "inventorySha256": inventory_sha256,
    }:
        raise MapScriptClosureError("SCB selection evidence is inconsistent")
    if (
        len(entries) > limits["maxFiles"]
        or selected_bytes > limits["maxTotalBytes"]
        or source["manifestFileCount"] < len(entries)
        or source["manifestTotalBytes"] < selected_bytes
    ):
        raise MapScriptClosureError(
            "SCB selection exceeds its declared source or limits"
        )
    if document.get("summary") != _scb_summary(entries, outputs):
        raise MapScriptClosureError("SCB summary is inconsistent")
    output_rows = [
        {"path": item.path, "size": item.native_bytes, "sha256": item.native_sha256}
        for item in outputs
    ]
    expected_tree = _scb_inventory_sha256(
        "openbfme.scb-native-corpus-output-tree-v0", output_rows
    )
    if document.get("outputTreeSha256") != expected_tree:
        raise MapScriptClosureError("SCB output-tree SHA-256 is invalid")
    request_basis = {
        "schema": "openbfme.scb-native-corpus-request",
        "schemaVersion": 0,
        "source": source,
        "selection": selection,
        "limits": limits,
    }
    if document.get("requestSha256") != _scb_sha256(request_basis):
        raise MapScriptClosureError("SCB request SHA-256 is invalid")
    basis = {key: item for key, item in document.items() if key != "identitySha256"}
    identity = document.get("identitySha256")
    if not _is_sha256(identity) or identity != _scb_sha256(basis):
        raise MapScriptClosureError("SCB identity SHA-256 is invalid")
    return _ScbManifest(
        hashlib.sha256(raw).hexdigest(),
        str(identity),
        str(source["manifestSha256"]),
        str(source["manifestAggregateSha256"]),
        entries,
        outputs,
    )


def _source_id(item: _MapHandoff) -> str:
    return _closure_sha256(
        {
            "schema": "openbfme.map-script-closure-source-id",
            "schemaVersion": 0,
            "sourcePath": item.source_path,
            "sourceBytes": item.source_bytes,
            "sourceSha256": item.source_sha256,
        }
    )


def _output_id(item: _ScbOutput) -> str:
    return _closure_sha256(
        {
            "schema": "openbfme.map-script-closure-output-id",
            "schemaVersion": 0,
            "outputPath": item.path,
            "sourceSha256": item.source_sha256,
            "nativeBytes": item.native_bytes,
            "nativeSha256": item.native_sha256,
        }
    )


def build_map_script_closure(
    map_native_manifest: Mapping[str, object] | bytes,
    scb_native_manifest: Mapping[str, object] | bytes,
) -> MapScriptClosureEvidence:
    """Join two sealed corpus manifests into identifier-free closure evidence.

    Every map-native ``script-container`` handoff must have exactly one SCB
    source entry with the same path *and casing*, byte count, and source SHA.
    Every SCB source must participate in that join and reference a validated
    exact-wire native output.  Any disagreement raises
    :class:`MapScriptClosureError`.
    """

    map_manifest = _validate_map_manifest(map_native_manifest)
    scb_manifest = _validate_scb_manifest(scb_native_manifest)
    if (
        map_manifest.source_manifest_sha256 == scb_manifest.source_manifest_sha256
        and map_manifest.source_manifest_aggregate_sha256
        != scb_manifest.source_manifest_aggregate_sha256
    ):
        raise MapScriptClosureError("equal source-manifest seals disagree on aggregate")
    if len(map_manifest.handoffs) != len(scb_manifest.entries):
        raise MapScriptClosureError("map handoff and SCB source counts disagree")
    scb_by_path = {item.source_path: item for item in scb_manifest.entries}
    scb_by_fold = {item.source_path.casefold(): item for item in scb_manifest.entries}
    output_by_path = {item.path: item for item in scb_manifest.outputs}
    joined: list[tuple[_MapHandoff, _ScbEntry, _ScbOutput]] = []
    for handoff in map_manifest.handoffs:
        entry = scb_by_path.get(handoff.source_path)
        if entry is None:
            if handoff.source_path.casefold() in scb_by_fold:
                raise MapScriptClosureError("map handoff and SCB source casing differs")
            raise MapScriptClosureError("map handoff has no SCB source")
        if (
            handoff.source_bytes != entry.source_bytes
            or handoff.source_sha256 != entry.source_sha256
        ):
            raise MapScriptClosureError("map handoff and SCB source identity differs")
        output = output_by_path[entry.output_path]
        joined.append((handoff, entry, output))
    joined_paths = {item[1].source_path for item in joined}
    if joined_paths != set(scb_by_path):
        raise MapScriptClosureError("SCB corpus contains an unmatched source")

    source_seals_differ = (
        map_manifest.source_manifest_sha256 != scb_manifest.source_manifest_sha256
        or map_manifest.source_manifest_aggregate_sha256
        != scb_manifest.source_manifest_aggregate_sha256
    )
    if (
        source_seals_differ
        and not joined
        and (
            map_manifest.source_manifest_aggregate_sha256
            != scb_manifest.source_manifest_aggregate_sha256
        )
    ):
        raise MapScriptClosureError(
            "different source manifests have no exact source join evidence"
        )

    closure_entries = tuple(
        sorted(
            (
                MapScriptClosureEntry(
                    source_id=_source_id(handoff),
                    source_bytes=handoff.source_bytes,
                    source_sha256=handoff.source_sha256,
                    handoff_evidence_sha256=handoff.handoff_evidence_sha256,
                    output_id=_output_id(output),
                    native_bytes=output.native_bytes,
                    native_sha256=output.native_sha256,
                    semantic_sha256=output.semantic_sha256,
                    backtest_evidence_sha256=output.backtest_evidence_sha256,
                )
                for handoff, _entry, output in joined
            ),
            key=lambda item: item.source_id,
        )
    )
    closure_outputs = tuple(
        sorted(
            (
                MapScriptClosureOutput(
                    output_id=_output_id(output),
                    source_sha256=output.source_sha256,
                    native_bytes=output.native_bytes,
                    native_sha256=output.native_sha256,
                    semantic_sha256=output.semantic_sha256,
                    backtest_evidence_sha256=output.backtest_evidence_sha256,
                )
                for output in scb_manifest.outputs
            ),
            key=lambda item: item.output_id,
        )
    )
    private_identity_basis = {
        "schema": "openbfme.map-script-closure-identity",
        "schemaVersion": 0,
        "bindings": {
            "mapNativeManifestSha256": map_manifest.manifest_sha256,
            "mapNativeIdentitySha256": map_manifest.identity_sha256,
            "mapSourceManifestSha256": map_manifest.source_manifest_sha256,
            "mapSourceManifestAggregateSha256": (
                map_manifest.source_manifest_aggregate_sha256
            ),
            "scbNativeManifestSha256": scb_manifest.manifest_sha256,
            "scbNativeIdentitySha256": scb_manifest.identity_sha256,
            "scbSourceManifestSha256": scb_manifest.source_manifest_sha256,
            "scbSourceManifestAggregateSha256": (
                scb_manifest.source_manifest_aggregate_sha256
            ),
            # Both inputs are distinct manifest contracts even when they bind
            # the same effective-assets manifest.  Reaching this basis means
            # their complete source inventories joined exactly.
            "crossManifestExactSourceJoin": True,
        },
        "joins": [
            {
                "sourcePath": handoff.source_path,
                "sourceBytes": handoff.source_bytes,
                "sourceSha256": handoff.source_sha256,
                "handoffEvidenceSha256": handoff.handoff_evidence_sha256,
                "outputPath": output.path,
                "nativeBytes": output.native_bytes,
                "nativeSha256": output.native_sha256,
                "semanticSha256": output.semantic_sha256,
                "backtestEvidenceSha256": output.backtest_evidence_sha256,
            }
            for handoff, _entry, output in joined
        ],
    }
    identity_sha256 = _closure_sha256(private_identity_basis)
    provisional = MapScriptClosureEvidence(
        entries=closure_entries,
        outputs=closure_outputs,
        map_native_manifest_sha256=map_manifest.manifest_sha256,
        map_native_identity_sha256=map_manifest.identity_sha256,
        map_source_manifest_sha256=map_manifest.source_manifest_sha256,
        map_source_manifest_aggregate_sha256=(
            map_manifest.source_manifest_aggregate_sha256
        ),
        scb_native_manifest_sha256=scb_manifest.manifest_sha256,
        scb_native_identity_sha256=scb_manifest.identity_sha256,
        scb_source_manifest_sha256=scb_manifest.source_manifest_sha256,
        scb_source_manifest_aggregate_sha256=(
            scb_manifest.source_manifest_aggregate_sha256
        ),
        cross_manifest_exact_source_join=True,
        identity_sha256=identity_sha256,
        evidence_sha256="",
    )
    evidence_sha256 = _closure_sha256(provisional.evidence_hash_basis())
    return MapScriptClosureEvidence(
        entries=provisional.entries,
        outputs=provisional.outputs,
        map_native_manifest_sha256=provisional.map_native_manifest_sha256,
        map_native_identity_sha256=provisional.map_native_identity_sha256,
        map_source_manifest_sha256=provisional.map_source_manifest_sha256,
        map_source_manifest_aggregate_sha256=(
            provisional.map_source_manifest_aggregate_sha256
        ),
        scb_native_manifest_sha256=provisional.scb_native_manifest_sha256,
        scb_native_identity_sha256=provisional.scb_native_identity_sha256,
        scb_source_manifest_sha256=provisional.scb_source_manifest_sha256,
        scb_source_manifest_aggregate_sha256=(
            provisional.scb_source_manifest_aggregate_sha256
        ),
        cross_manifest_exact_source_join=(provisional.cross_manifest_exact_source_join),
        identity_sha256=provisional.identity_sha256,
        evidence_sha256=evidence_sha256,
    )


build_closure = build_map_script_closure


__all__ = [
    "MAP_SCRIPT_CLOSURE_SCHEMA",
    "MAP_SCRIPT_CLOSURE_SCHEMA_VERSION",
    "MapScriptClosureEntry",
    "MapScriptClosureError",
    "MapScriptClosureEvidence",
    "MapScriptClosureOutput",
    "build_closure",
    "build_map_script_closure",
]
