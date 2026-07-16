"""Transactional native corpus builder for every manifest-declared SAGE SCB.

The input is a canonical, exact ``effective-assets`` tree.  Every declared
file is SHA-verified, every case-insensitive ``.scb`` member is selected
without truncation, and every selected source is converted with
``sage_scb``.  The persisted native JSON is independently backtested against
the source wire before publication and again on reuse.

Native JSON can contain retail identifiers and script payloads.  Callers must
therefore keep the output in the ignored private retail workspace.  The
corpus manifest and :meth:`ScbNativeCorpusReport.neutral` expose only canonical
source paths, sizes, hashes, counts, and identifier-free backtest evidence.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import stat
from typing import Any, Iterable, Mapping, Sequence
import uuid

from . import w3d_input_stage as _effective
from .sage_scb import (
    MAX_SCB_SOURCES,
    SageScbError,
    backtest_sage_scb_native,
    convert_sage_scb_bytes,
)


SCB_NATIVE_CORPUS_SCHEMA = "openbfme.scb-native-corpus"
SCB_NATIVE_CORPUS_SCHEMA_VERSION = 0
SCB_NATIVE_CORPUS_MANIFEST = ".openbfme/scb-native-corpus.json"

MAX_SCB_NATIVE_FILES = MAX_SCB_SOURCES
MAX_SCB_NATIVE_TOTAL_BYTES = 1024 * 1024 * 1024
HASH_BLOCK_BYTES = 1024 * 1024

_NATIVE_DIRECTORY = "objects"
_SHA256_CHARACTERS = frozenset("0123456789abcdef")


class ScbNativeCorpusError(ValueError):
    """Base class for a rejected SCB native-corpus operation."""


class ScbNativeCorpusLimitError(ScbNativeCorpusError):
    """Raised before conversion when the complete SCB selection is too large."""


class ScbNativeCorpusReuseError(ScbNativeCorpusError):
    """Raised when an existing destination cannot be exactly reused."""


@dataclass(frozen=True, slots=True)
class ScbNativeCorpusEntry:
    """One selected private manifest path and its content-addressed output."""

    source_path: str
    source_bytes: int
    source_sha256: str
    output_path: str

    def neutral(self) -> dict[str, object]:
        return {
            "sourcePath": self.source_path,
            "sourceBytes": self.source_bytes,
            "sourceSha256": self.source_sha256,
            "outputPath": self.output_path,
        }


@dataclass(frozen=True, slots=True)
class ScbNativeCorpusOutput:
    """One unique persisted native SCB and its exact backtest evidence."""

    path: str
    source_sha256: str
    native_bytes: int
    native_sha256: str
    semantic_sha256: str
    backtest_evidence: Mapping[str, object]

    def neutral(self) -> dict[str, object]:
        return {
            "path": self.path,
            "sourceSha256": self.source_sha256,
            "nativeBytes": self.native_bytes,
            "nativeSha256": self.native_sha256,
            "semanticSha256": self.semantic_sha256,
            "backtestEvidence": _json_clone(self.backtest_evidence),
        }


@dataclass(frozen=True, slots=True)
class ScbNativeCorpusReport:
    """Verified source, request, output-tree, and publication evidence."""

    source_root: Path
    output_root: Path
    manifest_path: Path
    source_manifest_sha256: str
    source_manifest_aggregate_sha256: str
    source_manifest_file_count: int
    source_manifest_total_bytes: int
    entries: tuple[ScbNativeCorpusEntry, ...]
    outputs: tuple[ScbNativeCorpusOutput, ...]
    selected_inventory_sha256: str
    output_tree_sha256: str
    request_sha256: str
    identity_sha256: str
    manifest_sha256: str
    max_files: int
    max_total_bytes: int
    published: bool
    reused: bool

    @property
    def source_count(self) -> int:
        return len(self.entries)

    @property
    def source_bytes(self) -> int:
        return sum(item.source_bytes for item in self.entries)

    @property
    def output_count(self) -> int:
        return len(self.outputs)

    @property
    def native_bytes(self) -> int:
        return sum(item.native_bytes for item in self.outputs)

    @property
    def complete(self) -> bool:
        return self.published and self.source_count > 0

    def neutral(self) -> dict[str, object]:
        """Return JSON-ready evidence with no host paths or retail payloads."""

        return {
            "schema": "openbfme.scb-native-corpus-report",
            "schemaVersion": 0,
            "source": {
                "manifestSha256": self.source_manifest_sha256,
                "manifestAggregateSha256": self.source_manifest_aggregate_sha256,
                "manifestFileCount": self.source_manifest_file_count,
                "manifestTotalBytes": self.source_manifest_total_bytes,
            },
            "selection": {
                "caseInsensitiveSuffix": ".scb",
                "files": self.source_count,
                "bytes": self.source_bytes,
                "inventorySha256": self.selected_inventory_sha256,
            },
            "limits": {
                "maxFiles": self.max_files,
                "maxTotalBytes": self.max_total_bytes,
            },
            "summary": _summary(self.entries, self.outputs),
            "entries": [item.neutral() for item in self.entries],
            "outputs": [item.neutral() for item in self.outputs],
            "outputTreeSha256": self.output_tree_sha256,
            "requestSha256": self.request_sha256,
            "identitySha256": self.identity_sha256,
            "manifestSha256": self.manifest_sha256,
            "published": self.published,
            "reused": self.reused,
        }

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class _CorpusSpec:
    entries: tuple[ScbNativeCorpusEntry, ...]
    outputs: tuple[ScbNativeCorpusOutput, ...]
    selected_inventory_sha256: str
    output_tree_sha256: str
    request_sha256: str
    identity_sha256: str
    document: Mapping[str, object]
    canonical_manifest: bytes
    max_files: int
    max_total_bytes: int


def _is_sha256(value: object) -> bool:
    return (
        isinstance(value, str)
        and len(value) == 64
        and value == value.casefold()
        and all(character in _SHA256_CHARACTERS for character in value)
    )


def _is_int(value: object, *, minimum: int = 0) -> bool:
    return not isinstance(value, bool) and isinstance(value, int) and value >= minimum


def _json_clone(value: Mapping[str, object]) -> dict[str, object]:
    try:
        cloned = json.loads(
            json.dumps(
                value,
                sort_keys=True,
                ensure_ascii=False,
                allow_nan=False,
                separators=(",", ":"),
            )
        )
    except (TypeError, ValueError, json.JSONDecodeError) as exc:
        raise ScbNativeCorpusError("SCB corpus evidence is not canonical JSON") from exc
    if not isinstance(cloned, dict):
        raise ScbNativeCorpusError("SCB corpus evidence is not an object")
    return cloned


def _canonical_json_bytes(value: object) -> bytes:
    try:
        return (
            json.dumps(
                value,
                indent=2,
                sort_keys=True,
                ensure_ascii=False,
                allow_nan=False,
            )
            + "\n"
        ).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise ScbNativeCorpusError("SCB corpus value is not canonical JSON") from exc


def _canonical_sha256(value: object) -> str:
    try:
        raw = json.dumps(
            value,
            sort_keys=True,
            ensure_ascii=False,
            allow_nan=False,
            separators=(",", ":"),
        ).encode("utf-8")
    except (TypeError, ValueError) as exc:
        raise ScbNativeCorpusError("SCB corpus value is not canonical JSON") from exc
    return hashlib.sha256(raw).hexdigest()


def _inventory_sha256(domain: str, rows: Iterable[Mapping[str, object]]) -> str:
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


def _selected_limit(value: int | None, hard: int, *, label: str) -> int:
    selected = hard if value is None else value
    if isinstance(selected, bool) or not isinstance(selected, int):
        raise TypeError(f"SCB native corpus {label} limit must be an integer")
    if not 1 <= selected <= hard:
        raise ValueError(f"SCB native corpus {label} limit must be 1..{hard}")
    return selected


def _translate_effective_error(exc: Exception) -> ScbNativeCorpusError:
    if isinstance(exc, _effective.W3DInputStageLimitError):
        return ScbNativeCorpusLimitError(str(exc))
    return ScbNativeCorpusError(str(exc))


def _resolve_source_root(value: Path | str) -> Path:
    try:
        return _effective._resolve_source_root(value)
    except (_effective.W3DInputStageError, TypeError) as exc:
        if isinstance(exc, TypeError):
            raise
        raise _translate_effective_error(exc) from exc


def _resolve_output_root(value: Path | str, source_root: Path) -> Path:
    try:
        return _effective._resolve_output_root(value, source_root)
    except (_effective.W3DInputStageError, TypeError) as exc:
        if isinstance(exc, TypeError):
            raise
        raise _translate_effective_error(exc) from exc


def _load_manifest(root: Path) -> Any:
    try:
        return _effective._load_manifest(root)
    except _effective.W3DInputStageError as exc:
        raise _translate_effective_error(exc) from exc


def _validate_source_tree(root: Path, manifest: Any) -> Mapping[str, Any]:
    try:
        return _effective._validate_source_tree(root, manifest)
    except _effective.W3DInputStageError as exc:
        raise _translate_effective_error(exc) from exc


def _revalidate_source(root: Path, manifest: Any, snapshot: Mapping[str, Any]) -> None:
    try:
        _effective._revalidate_source(root, manifest, snapshot)
    except _effective.W3DInputStageError as exc:
        raise _translate_effective_error(exc) from exc


def _scan_tree(root: Path, *, label: str) -> tuple[dict[str, Any], dict[str, str]]:
    try:
        return _effective._scan_tree(root, label=label)
    except _effective.W3DInputStageError as exc:
        raise _translate_effective_error(exc) from exc


def _read_strict_json(path: Path, *, label: str) -> tuple[dict[str, Any], bytes]:
    try:
        return _effective._read_strict_json(path, label=label)
    except _effective.W3DInputStageError as exc:
        raise _translate_effective_error(exc) from exc


def _selected_files(manifest: Any) -> tuple[Any, ...]:
    return tuple(
        item
        for item in manifest.files
        if PurePosixPath(item.path).suffix.casefold() == ".scb"
    )


def _check_selection_limits(
    selected: Sequence[Any], *, max_files: int, max_total_bytes: int
) -> None:
    if not selected:
        raise ScbNativeCorpusError(
            "effective-assets manifest declares no SCB files to convert"
        )
    selected_bytes = sum(item.size for item in selected)
    if len(selected) > max_files:
        raise ScbNativeCorpusLimitError(
            f"SCB native corpus selects {len(selected)} files; limit is {max_files}"
        )
    if selected_bytes > max_total_bytes:
        raise ScbNativeCorpusLimitError(
            "SCB native corpus selects "
            f"{selected_bytes} bytes; limit is {max_total_bytes}"
        )


def _verify_sources(
    manifest: Any,
    tree: Mapping[str, Any],
    *,
    selected: Sequence[Any],
    staging_directory: Path | None,
) -> dict[str, Path]:
    selected_by_path = {item.path: item for item in selected}
    staged: dict[str, Path] = {}
    selected_index = 0
    for item in manifest.files:
        target: Path | None = None
        if item.path in selected_by_path and staging_directory is not None:
            target = staging_directory / f"source-{selected_index:08d}.scb"
            selected_index += 1
            staged[item.path] = target
        try:
            _effective._copy_or_hash_verified(
                tree[item.path.casefold()],
                item,
                target=target,
                label="effective-assets file",
            )
        except _effective.W3DInputStageError as exc:
            raise _translate_effective_error(exc) from exc
    return staged


def _read_verified_source(actual: Any, expected: Any) -> bytes:
    path = actual.path
    try:
        if _effective._is_link_like(path):
            raise ScbNativeCorpusError("SCB source became linked before backtest")
        before = path.stat()
        if before.st_nlink != 1 or not stat.S_ISREG(before.st_mode):
            raise ScbNativeCorpusError("SCB source is not an ordinary independent file")
        if _effective._stat_identity(before) != actual.identity:
            raise ScbNativeCorpusError("SCB source changed before backtest")
        raw = path.read_bytes()
        after = path.stat()
    except ScbNativeCorpusError:
        raise
    except OSError as exc:
        raise ScbNativeCorpusError("SCB source cannot be read for backtest") from exc
    if (
        _effective._stat_identity(before) != _effective._stat_identity(after)
        or len(raw) != expected.size
        or hashlib.sha256(raw).hexdigest() != expected.sha256
    ):
        raise ScbNativeCorpusError("SCB source size or SHA-256 changed during backtest")
    return raw


def _read_staged_source(path: Path, expected: Any) -> bytes:
    try:
        if _effective._is_link_like(path):
            raise ScbNativeCorpusError("staged SCB source is linked")
        before = path.stat()
        if before.st_nlink != 1 or not stat.S_ISREG(before.st_mode):
            raise ScbNativeCorpusError(
                "staged SCB source is not an ordinary independent file"
            )
        raw = path.read_bytes()
        after = path.stat()
    except ScbNativeCorpusError:
        raise
    except OSError as exc:
        raise ScbNativeCorpusError("staged SCB source cannot be read") from exc
    if (
        _effective._stat_identity(before) != _effective._stat_identity(after)
        or len(raw) != expected.size
        or hashlib.sha256(raw).hexdigest() != expected.sha256
    ):
        raise ScbNativeCorpusError(
            "staged SCB source size or SHA-256 disagrees with its evidence"
        )
    return raw


def _output_relative(source_sha256: str, native_sha256: str) -> str:
    return (
        f"{_NATIVE_DIRECTORY}/{source_sha256[:2]}/{source_sha256}/{native_sha256}.json"
    )


def _write_independent(path: Path, raw: bytes, *, label: str) -> None:
    try:
        path.parent.mkdir(parents=True, exist_ok=True)
        with path.open("xb") as stream:
            stream.write(raw)
        metadata = path.stat()
    except OSError as exc:
        path.unlink(missing_ok=True)
        raise ScbNativeCorpusError(f"{label} could not be written") from exc
    if (
        metadata.st_nlink != 1
        or not stat.S_ISREG(metadata.st_mode)
        or metadata.st_size != len(raw)
    ):
        path.unlink(missing_ok=True)
        raise ScbNativeCorpusError(f"{label} is not an ordinary independent file")


def _convert_sources(
    stage: Path,
    selected: Sequence[Any],
    staged_sources: Mapping[str, Path],
) -> tuple[tuple[ScbNativeCorpusEntry, ...], tuple[ScbNativeCorpusOutput, ...]]:
    entries: list[ScbNativeCorpusEntry] = []
    outputs_by_source: dict[str, ScbNativeCorpusOutput] = {}
    for item in selected:
        prior = outputs_by_source.get(item.sha256)
        if prior is None:
            source = _read_staged_source(staged_sources[item.path], item)
            try:
                native = convert_sage_scb_bytes(source)
                backtest = backtest_sage_scb_native(source, native)
                native_raw = _canonical_json_bytes(native)
            except (SageScbError, TypeError, ValueError) as exc:
                raise ScbNativeCorpusError(
                    "an SCB source rejected conversion or exact-wire backtesting"
                ) from exc
            native_sha256 = hashlib.sha256(native_raw).hexdigest()
            relative = _output_relative(item.sha256, native_sha256)
            target = stage.joinpath(*PurePosixPath(relative).parts)
            _write_independent(target, native_raw, label="native SCB output")
            semantic_sha256 = native.get("semanticSha256")
            if not _is_sha256(semantic_sha256):
                raise ScbNativeCorpusError(
                    "SCB converter returned invalid semantic evidence"
                )
            prior = ScbNativeCorpusOutput(
                path=relative,
                source_sha256=item.sha256,
                native_bytes=len(native_raw),
                native_sha256=native_sha256,
                semantic_sha256=semantic_sha256,
                backtest_evidence=backtest.to_dict(),
            )
            outputs_by_source[item.sha256] = prior
        entries.append(
            ScbNativeCorpusEntry(
                source_path=item.path,
                source_bytes=item.size,
                source_sha256=item.sha256,
                output_path=prior.path,
            )
        )
    outputs = tuple(
        sorted(
            outputs_by_source.values(),
            key=lambda item: (item.path.casefold(), item.path),
        )
    )
    return tuple(entries), outputs


def _source_evidence(manifest: Any) -> dict[str, object]:
    return {
        "manifestAggregateSha256": manifest.aggregate_sha256,
        "manifestFileCount": len(manifest.files),
        "manifestSha256": manifest.sha256,
        "manifestTotalBytes": manifest.total_bytes,
    }


def _selection_evidence(
    selected: Sequence[Any], inventory_sha256: str
) -> dict[str, object]:
    return {
        "caseInsensitiveSuffix": ".scb",
        "files": len(selected),
        "bytes": sum(item.size for item in selected),
        "inventorySha256": inventory_sha256,
    }


def _limits_evidence(max_files: int, max_total_bytes: int) -> dict[str, object]:
    return {
        "hardMaxFiles": MAX_SCB_NATIVE_FILES,
        "hardMaxTotalBytes": MAX_SCB_NATIVE_TOTAL_BYTES,
        "maxFiles": max_files,
        "maxTotalBytes": max_total_bytes,
    }


def _summary(
    entries: Sequence[ScbNativeCorpusEntry],
    outputs: Sequence[ScbNativeCorpusOutput],
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


def _make_spec(
    manifest: Any,
    selected: Sequence[Any],
    entries: Sequence[ScbNativeCorpusEntry],
    outputs: Sequence[ScbNativeCorpusOutput],
    *,
    max_files: int,
    max_total_bytes: int,
) -> _CorpusSpec:
    selected_rows = [
        {"path": item.path, "size": item.size, "sha256": item.sha256}
        for item in selected
    ]
    selected_inventory_sha256 = _inventory_sha256(
        "openbfme.scb-native-corpus-selection-v0", selected_rows
    )
    output_rows = [
        {
            "path": item.path,
            "size": item.native_bytes,
            "sha256": item.native_sha256,
        }
        for item in outputs
    ]
    output_tree_sha256 = _inventory_sha256(
        "openbfme.scb-native-corpus-output-tree-v0", output_rows
    )
    source_evidence = _source_evidence(manifest)
    selection_evidence = _selection_evidence(selected, selected_inventory_sha256)
    limits_evidence = _limits_evidence(max_files, max_total_bytes)
    request_basis = {
        "schema": "openbfme.scb-native-corpus-request",
        "schemaVersion": 0,
        "source": source_evidence,
        "selection": selection_evidence,
        "limits": limits_evidence,
    }
    request_sha256 = _canonical_sha256(request_basis)
    basis: dict[str, object] = {
        "schema": SCB_NATIVE_CORPUS_SCHEMA,
        "schemaVersion": SCB_NATIVE_CORPUS_SCHEMA_VERSION,
        "source": source_evidence,
        "selection": selection_evidence,
        "limits": limits_evidence,
        "summary": _summary(entries, outputs),
        "entries": [item.neutral() for item in entries],
        "outputs": [item.neutral() for item in outputs],
        "outputTreeSha256": output_tree_sha256,
        "requestSha256": request_sha256,
    }
    identity_sha256 = _canonical_sha256(basis)
    document = {**basis, "identitySha256": identity_sha256}
    return _CorpusSpec(
        entries=tuple(entries),
        outputs=tuple(outputs),
        selected_inventory_sha256=selected_inventory_sha256,
        output_tree_sha256=output_tree_sha256,
        request_sha256=request_sha256,
        identity_sha256=identity_sha256,
        document=document,
        canonical_manifest=_canonical_json_bytes(document),
        max_files=max_files,
        max_total_bytes=max_total_bytes,
    )


def _safe_output_path(value: object, *, label: str) -> str:
    try:
        return _effective._safe_manifest_path(value, label=label)
    except _effective.W3DInputStageError as exc:
        raise ScbNativeCorpusError(str(exc)) from exc


def _parse_entry(raw: object) -> ScbNativeCorpusEntry:
    if not isinstance(raw, dict) or set(raw) != {
        "sourcePath",
        "sourceBytes",
        "sourceSha256",
        "outputPath",
    }:
        raise ScbNativeCorpusError("SCB corpus source entry has an invalid shape")
    source_path = _safe_output_path(raw.get("sourcePath"), label="SCB source path")
    output_path = _safe_output_path(raw.get("outputPath"), label="SCB output path")
    source_bytes = raw.get("sourceBytes")
    source_sha256 = raw.get("sourceSha256")
    if not _is_int(source_bytes) or not _is_sha256(source_sha256):
        raise ScbNativeCorpusError("SCB corpus source entry has invalid evidence")
    return ScbNativeCorpusEntry(
        source_path=source_path,
        source_bytes=source_bytes,
        source_sha256=source_sha256,
        output_path=output_path,
    )


def _parse_output(raw: object) -> ScbNativeCorpusOutput:
    if not isinstance(raw, dict) or set(raw) != {
        "path",
        "sourceSha256",
        "nativeBytes",
        "nativeSha256",
        "semanticSha256",
        "backtestEvidence",
    }:
        raise ScbNativeCorpusError("SCB corpus output entry has an invalid shape")
    path = _safe_output_path(raw.get("path"), label="native SCB output path")
    source_sha256 = raw.get("sourceSha256")
    native_bytes = raw.get("nativeBytes")
    native_sha256 = raw.get("nativeSha256")
    semantic_sha256 = raw.get("semanticSha256")
    evidence = raw.get("backtestEvidence")
    if (
        not _is_sha256(source_sha256)
        or not _is_int(native_bytes, minimum=1)
        or not _is_sha256(native_sha256)
        or not _is_sha256(semantic_sha256)
        or path != _output_relative(source_sha256, native_sha256)
        or not isinstance(evidence, dict)
    ):
        raise ScbNativeCorpusError("SCB corpus output entry has invalid evidence")
    return ScbNativeCorpusOutput(
        path=path,
        source_sha256=source_sha256,
        native_bytes=native_bytes,
        native_sha256=native_sha256,
        semantic_sha256=semantic_sha256,
        backtest_evidence=_json_clone(evidence),
    )


def _verify_native_output(
    root: Path,
    output: ScbNativeCorpusOutput,
    source_item: Any,
    source_actual: Any,
) -> None:
    native_path = root.joinpath(*PurePosixPath(output.path).parts)
    document, raw = _read_strict_json(native_path, label="native SCB output")
    if raw != _canonical_json_bytes(document):
        raise ScbNativeCorpusError("native SCB output encoding is not canonical")
    if len(raw) != output.native_bytes or hashlib.sha256(raw).hexdigest() != (
        output.native_sha256
    ):
        raise ScbNativeCorpusError("native SCB output identity changed")
    source = _read_verified_source(source_actual, source_item)
    try:
        backtest = backtest_sage_scb_native(source, document)
    except SageScbError as exc:
        raise ScbNativeCorpusError(
            "persisted native SCB failed exact-wire backtesting"
        ) from exc
    if (
        backtest.semantic_sha256 != output.semantic_sha256
        or backtest.to_dict() != output.backtest_evidence
    ):
        raise ScbNativeCorpusError("native SCB backtest evidence changed")


def _verify_output(
    root: Path,
    manifest: Any,
    tree: Mapping[str, Any],
    selected: Sequence[Any],
    *,
    max_files: int,
    max_total_bytes: int,
    reused: bool,
) -> ScbNativeCorpusReport:
    manifest_path = root.joinpath(*PurePosixPath(SCB_NATIVE_CORPUS_MANIFEST).parts)
    document, raw = _read_strict_json(manifest_path, label="SCB native corpus manifest")
    expected_keys = {
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
    }
    if (
        set(document) != expected_keys
        or document.get("schema") != SCB_NATIVE_CORPUS_SCHEMA
        or document.get("schemaVersion") != SCB_NATIVE_CORPUS_SCHEMA_VERSION
        or isinstance(document.get("schemaVersion"), bool)
        or raw != _canonical_json_bytes(document)
    ):
        raise ScbNativeCorpusError("SCB native corpus manifest contract is invalid")
    raw_entries = document.get("entries")
    raw_outputs = document.get("outputs")
    if not isinstance(raw_entries, list) or not isinstance(raw_outputs, list):
        raise ScbNativeCorpusError("SCB native corpus inventories are invalid")
    entries = tuple(_parse_entry(item) for item in raw_entries)
    outputs = tuple(_parse_output(item) for item in raw_outputs)
    entry_paths = [item.source_path for item in entries]
    output_paths = [item.path for item in outputs]
    if (
        entry_paths != sorted(entry_paths, key=lambda value: (value.casefold(), value))
        or len({value.casefold() for value in entry_paths}) != len(entry_paths)
        or output_paths
        != sorted(output_paths, key=lambda value: (value.casefold(), value))
        or len({value.casefold() for value in output_paths}) != len(output_paths)
    ):
        raise ScbNativeCorpusError("SCB native corpus inventory is not canonical")
    expected_entries = tuple((item.path, item.size, item.sha256) for item in selected)
    if (
        tuple(
            (item.source_path, item.source_bytes, item.source_sha256)
            for item in entries
        )
        != expected_entries
    ):
        raise ScbNativeCorpusError(
            "SCB native corpus sources disagree with the verified selection"
        )
    outputs_by_path = {item.path: item for item in outputs}
    outputs_by_source = {item.source_sha256: item for item in outputs}
    if len(outputs_by_source) != len(outputs) or {
        item.output_path for item in entries
    } != set(outputs_by_path):
        raise ScbNativeCorpusError("SCB native corpus output accounting is invalid")
    for entry in entries:
        output = outputs_by_path.get(entry.output_path)
        if output is None or output.source_sha256 != entry.source_sha256:
            raise ScbNativeCorpusError("SCB native source/output mapping is invalid")

    spec = _make_spec(
        manifest,
        selected,
        entries,
        outputs,
        max_files=max_files,
        max_total_bytes=max_total_bytes,
    )
    for key in (
        "source",
        "selection",
        "limits",
        "summary",
        "outputTreeSha256",
        "requestSha256",
        "identitySha256",
    ):
        if document.get(key) != spec.document.get(key):
            raise ScbNativeCorpusError(
                "SCB native corpus does not match the verified request"
            )
    if raw != spec.canonical_manifest:
        raise ScbNativeCorpusError(
            "SCB native corpus manifest is not the canonical verified request"
        )

    source_by_sha: dict[str, Any] = {}
    for item in selected:
        source_by_sha.setdefault(item.sha256, item)
    for output in outputs:
        source_item = source_by_sha[output.source_sha256]
        _verify_native_output(
            root,
            output,
            source_item,
            tree[source_item.path.casefold()],
        )

    files, directories = _scan_tree(root, label="SCB native corpus tree")
    declared = [SCB_NATIVE_CORPUS_MANIFEST, *(item.path for item in outputs)]
    expected_files = {item.casefold(): item for item in declared}
    expected_directories = _effective._expected_directories(declared)
    if set(files) != set(expected_files) or set(directories) != set(
        expected_directories
    ):
        raise ScbNativeCorpusError(
            "SCB native corpus tree does not contain the exact declared paths"
        )
    for relative in declared:
        actual = files[relative.casefold()]
        if actual.relative_path != relative:
            raise ScbNativeCorpusError("SCB native corpus path casing changed")
    return ScbNativeCorpusReport(
        source_root=manifest.path.parent.parent,
        output_root=root,
        manifest_path=manifest_path,
        source_manifest_sha256=manifest.sha256,
        source_manifest_aggregate_sha256=manifest.aggregate_sha256,
        source_manifest_file_count=len(manifest.files),
        source_manifest_total_bytes=manifest.total_bytes,
        entries=entries,
        outputs=outputs,
        selected_inventory_sha256=spec.selected_inventory_sha256,
        output_tree_sha256=spec.output_tree_sha256,
        request_sha256=spec.request_sha256,
        identity_sha256=spec.identity_sha256,
        manifest_sha256=hashlib.sha256(raw).hexdigest(),
        max_files=max_files,
        max_total_bytes=max_total_bytes,
        published=True,
        reused=reused,
    )


def _remove_owned_tree(path: Path, parent: Path, prefix: str) -> None:
    if not os.path.lexists(path):
        return
    if (
        path.parent != parent
        or not path.name.startswith(prefix)
        or _effective._is_link_like(path)
    ):
        raise ScbNativeCorpusError(
            "refused to remove an unowned SCB native-corpus transaction path"
        )
    _scan_tree(path, label="SCB native-corpus transaction tree")
    shutil.rmtree(path)


def _publish(
    stage: Path,
    destination: Path,
    backup: Path,
    source_root: Path,
    manifest: Any,
    source_tree: Mapping[str, Any],
    selected: Sequence[Any],
    *,
    max_files: int,
    max_total_bytes: int,
) -> ScbNativeCorpusReport:
    parent = destination.parent
    had_destination = os.path.lexists(destination)
    if had_destination:
        _scan_tree(destination, label="existing SCB native corpus tree")
        try:
            os.replace(destination, backup)
        except OSError as exc:
            raise ScbNativeCorpusError(
                "existing SCB native corpus could not enter the transaction"
            ) from exc
    try:
        os.replace(stage, destination)
        report = _verify_output(
            destination,
            manifest,
            source_tree,
            selected,
            max_files=max_files,
            max_total_bytes=max_total_bytes,
            reused=False,
        )
        _revalidate_source(source_root, manifest, source_tree)
    except Exception as publish_error:
        try:
            if os.path.lexists(destination):
                os.replace(destination, stage)
            if had_destination and os.path.lexists(backup):
                os.replace(backup, destination)
        except Exception as rollback_error:  # pragma: no cover - catastrophic fault
            raise ScbNativeCorpusError(
                "SCB native-corpus publish failed and rollback could not restore "
                "the prior output"
            ) from rollback_error
        raise ScbNativeCorpusError(
            "SCB native-corpus publish failed; prior output was preserved"
        ) from publish_error
    if had_destination and os.path.lexists(backup):
        try:
            _remove_owned_tree(backup, parent, f".{destination.name}.backup-")
        except (OSError, ScbNativeCorpusError):
            pass
    return report


def build_scb_native_corpus(
    effective_assets_root: Path | str,
    output_root: Path | str,
    *,
    max_files: int | None = None,
    max_total_bytes: int | None = None,
    force: bool = False,
) -> ScbNativeCorpusReport:
    """Convert and exactly backtest every manifest-declared ``.scb``.

    Selection is case-insensitive and never truncated.  Configurable limits
    may only lower the hard bounds.  A matching destination is a true no-op
    only after the complete source tree, every source byte, the exact output
    tree, and each persisted native SCB have been reverified.  ``force=True``
    transactionally replaces an existing ordinary output and restores it if
    publication or the post-publication source revalidation fails.
    """

    if not isinstance(force, bool):
        raise TypeError("SCB native corpus force flag must be a boolean")
    selected_max_files = _selected_limit(
        max_files, MAX_SCB_NATIVE_FILES, label="file count"
    )
    selected_max_bytes = _selected_limit(
        max_total_bytes, MAX_SCB_NATIVE_TOTAL_BYTES, label="total byte"
    )
    source = _resolve_source_root(effective_assets_root)
    destination = _resolve_output_root(output_root, source)
    manifest = _load_manifest(source)
    source_tree = _validate_source_tree(source, manifest)
    selected = _selected_files(manifest)
    _check_selection_limits(
        selected,
        max_files=selected_max_files,
        max_total_bytes=selected_max_bytes,
    )

    if destination.is_dir() and not force:
        try:
            _verify_sources(
                manifest,
                source_tree,
                selected=selected,
                staging_directory=None,
            )
            _revalidate_source(source, manifest, source_tree)
            report = _verify_output(
                destination,
                manifest,
                source_tree,
                selected,
                max_files=selected_max_files,
                max_total_bytes=selected_max_bytes,
                reused=True,
            )
            _revalidate_source(source, manifest, source_tree)
            return report
        except ScbNativeCorpusError as exc:
            raise ScbNativeCorpusReuseError(
                f"existing SCB native corpus failed verification: {exc}"
            ) from exc

    parent = destination.parent
    token = uuid.uuid4().hex
    stage = parent / f".{destination.name}.staging-{token}"
    backup = parent / f".{destination.name}.backup-{token}"
    try:
        stage.mkdir()
        work = stage / ".work"
        staged_sources_root = work / "sources"
        staged_sources_root.mkdir(parents=True)
    except OSError as exc:
        if os.path.lexists(stage):
            _remove_owned_tree(stage, parent, f".{destination.name}.staging-")
        raise ScbNativeCorpusError(
            "SCB native-corpus staging directory could not be created"
        ) from exc

    try:
        staged_sources = _verify_sources(
            manifest,
            source_tree,
            selected=selected,
            staging_directory=staged_sources_root,
        )
        entries, outputs = _convert_sources(stage, selected, staged_sources)
        shutil.rmtree(work)
        spec = _make_spec(
            manifest,
            selected,
            entries,
            outputs,
            max_files=selected_max_files,
            max_total_bytes=selected_max_bytes,
        )
        corpus_manifest = stage.joinpath(
            *PurePosixPath(SCB_NATIVE_CORPUS_MANIFEST).parts
        )
        _write_independent(
            corpus_manifest,
            spec.canonical_manifest,
            label="SCB native corpus manifest",
        )
        _revalidate_source(source, manifest, source_tree)
        staged_report = _verify_output(
            stage,
            manifest,
            source_tree,
            selected,
            max_files=selected_max_files,
            max_total_bytes=selected_max_bytes,
            reused=False,
        )
        if staged_report.identity_sha256 != spec.identity_sha256:
            raise ScbNativeCorpusError("staged SCB corpus identity changed")
        _revalidate_source(source, manifest, source_tree)
        return _publish(
            stage,
            destination,
            backup,
            source,
            manifest,
            source_tree,
            selected,
            max_files=selected_max_files,
            max_total_bytes=selected_max_bytes,
        )
    finally:
        if os.path.lexists(stage):
            _remove_owned_tree(stage, parent, f".{destination.name}.staging-")
        if os.path.lexists(backup) and not os.path.lexists(destination):
            try:
                os.replace(backup, destination)
            except OSError as exc:
                raise ScbNativeCorpusError(
                    "SCB native-corpus cleanup could not restore the prior output"
                ) from exc


# Concise orchestration alias.
build_corpus = build_scb_native_corpus


__all__ = [
    "MAX_SCB_NATIVE_FILES",
    "MAX_SCB_NATIVE_TOTAL_BYTES",
    "SCB_NATIVE_CORPUS_MANIFEST",
    "SCB_NATIVE_CORPUS_SCHEMA",
    "SCB_NATIVE_CORPUS_SCHEMA_VERSION",
    "ScbNativeCorpusEntry",
    "ScbNativeCorpusError",
    "ScbNativeCorpusLimitError",
    "ScbNativeCorpusOutput",
    "ScbNativeCorpusReport",
    "ScbNativeCorpusReuseError",
    "build_corpus",
    "build_scb_native_corpus",
]
