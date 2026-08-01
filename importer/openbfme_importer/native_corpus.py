"""Deterministic runtime-native corpus builder for verified effective assets.

This module is intentionally narrower than a complete BFME converter.  It
handles only formats that can be made runtime-native without semantic guesses:

* DDS/TGA/JPEG/PNG textures become deterministic RGBA PNG files with the
  repository-pinned Pillow release;
* every FFmpeg-decodable WAV becomes a deterministic integer PCM s16le WAV;
* structurally valid MP3 files are copied exactly;
* texture-suffixed CkMp or bounded EAR/RefPack CkMp payloads are explicitly
  handed off to the map lane without packaging their bytes;
* recognized but unsupported texture/audio formats fail the whole transaction.

Inputs are trusted only after their effective-assets manifest and exact tree
shape have been verified.  Selected payloads are then copied into private
staging while their declared SHA-256 is checked, so conversion never races a
second read of the source tree.  Outputs are content-addressed, independently
backtested, recorded in a canonical payload-free manifest, and published with
rollback.  No W3D or map capability is claimed here.
"""

from __future__ import annotations

from collections import Counter, deque
from concurrent.futures import Future, ThreadPoolExecutor
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
import subprocess
from typing import Any, Iterable, Mapping, Sequence
import uuid
import warnings

from .bootstrap import FFMPEG_EXE_SHA256, FFMPEG_VERSION
from .native_backtest import validate_native_output
from .paths import safe_relative_parts
from .sage_map import MAX_SOURCE_BYTES as MAX_SAGE_MAP_SOURCE_BYTES
from .sage_map import SageMapError, decode_sage_map_blob
from .tools import discover_executable


EFFECTIVE_ASSET_MANIFEST_SCHEMA = "openbfme.effective-assets-manifest"
EFFECTIVE_ASSET_MANIFEST_VERSION = 0
EFFECTIVE_ASSET_MANIFEST_RELATIVE = ".openbfme/manifest.json"

NATIVE_CORPUS_SCHEMA = "openbfme.native-corpus"
NATIVE_CORPUS_SCHEMA_VERSION = 0
NATIVE_CORPUS_MANIFEST = "manifest.json"
NATIVE_CORPUS_FAMILIES = ("texture", "sound", "music")

PINNED_PILLOW_VERSION = "12.2.0"
PINNED_FFMPEG_VERSION = FFMPEG_VERSION
MAX_MANIFEST_BYTES = 64 * 1024 * 1024
MAX_NATIVE_CORPUS_FILES = 50_000
MAX_NATIVE_CORPUS_BYTES = 8 * 1024 * 1024 * 1024
HASH_BLOCK_BYTES = 1024 * 1024
FFMPEG_TIMEOUT_SECONDS = 600

# This complete argument template is part of the corpus identity.  Input and
# output paths are substituted only in memory and are never written to the
# neutral report.  Rate and channel layout intentionally follow the source so
# transcode does not silently alter pitch, duration, or spatial presentation.
FFMPEG_WAV_ARGUMENT_TEMPLATE = (
    "-nostdin",
    "-hide_banner",
    "-loglevel",
    "error",
    "-y",
    "-i",
    "<input>",
    "-fflags",
    "+bitexact",
    "-flags:a",
    "+bitexact",
    "-map_metadata",
    "-1",
    "-vn",
    "-c:a",
    "pcm_s16le",
    "<output>",
)

_KNOWN_TEXTURE_EXTENSIONS = frozenset(
    {".bmp", ".dds", ".jpeg", ".jpg", ".pcx", ".png", ".tga"}
)
_SUPPORTED_TEXTURE_EXTENSIONS = frozenset({".dds", ".jpeg", ".jpg", ".png", ".tga"})
_KNOWN_AUDIO_EXTENSIONS = frozenset(
    {".ac3", ".aif", ".aiff", ".au", ".flac", ".mp3", ".ogg", ".snd", ".wav"}
)
_SUPPORTED_AUDIO_EXTENSIONS = frozenset({".mp3", ".wav"})
_SHA256_CHARACTERS = frozenset("0123456789abcdef")


class NativeCorpusError(ValueError):
    """Base class for a rejected or failed native-corpus operation."""


class NativeCorpusLimitError(NativeCorpusError):
    """Raised before selected payloads are read when a caller bound is exceeded."""


class NativeCorpusDependencyError(NativeCorpusError):
    """Raised when a pinned deterministic conversion dependency is unavailable."""


class NativeCorpusReuseError(NativeCorpusError):
    """Raised when a pre-existing destination cannot be safely reused."""


@dataclass(frozen=True, slots=True)
class NativeCorpusFailure:
    """One exact selected-source failure from a rolled-back build."""

    source_path: str
    family: str
    code: str
    detail: str

    def neutral(self) -> dict[str, str]:
        return {
            "sourcePath": self.source_path,
            "family": self.family,
            "code": self.code,
            "detail": self.detail,
        }


class NativeCorpusBuildError(NativeCorpusError):
    """Raised with every deterministic conversion/backtest failure found."""

    def __init__(self, failures: Sequence[NativeCorpusFailure]):
        ordered = tuple(
            sorted(
                failures,
                key=lambda item: (
                    item.source_path.casefold(),
                    item.source_path,
                    item.code,
                    item.detail,
                ),
            )
        )
        if not ordered:
            raise ValueError("NativeCorpusBuildError requires at least one failure")
        self.failures = ordered
        family_summary = _bounded_count_summary(item.family for item in ordered)
        code_summary = _bounded_count_summary(item.code for item in ordered)
        super().__init__(
            f"native corpus build failed ({len(ordered)} failures; "
            f"families: {family_summary}; codes: {code_summary})"
        )


@dataclass(frozen=True, slots=True)
class NativeCorpusEntry:
    """One selected source mapped to one validated content-addressed output."""

    source_path: str
    source_archive: str
    source_extension: str
    source_bytes: int
    source_sha256: str
    family: str
    output_path: str
    output_bytes: int
    output_sha256: str
    native_family: str
    evidence: Mapping[str, Any]

    def neutral(self) -> dict[str, Any]:
        return {
            "sourcePath": self.source_path,
            "sourceArchive": self.source_archive,
            "sourceExtension": self.source_extension,
            "sourceBytes": self.source_bytes,
            "sourceSha256": self.source_sha256,
            "family": self.family,
            "outputPath": self.output_path,
            "outputBytes": self.output_bytes,
            "outputSha256": self.output_sha256,
            "nativeFamily": self.native_family,
            "evidence": _json_clone(self.evidence),
        }


@dataclass(frozen=True, slots=True)
class NativeCorpusReclassification:
    """One extension-selected media source proven to contain a SAGE map."""

    source_path: str
    source_bytes: int
    source_sha256: str
    original_family: str
    original_extension: str
    detected_kind: str
    evidence_sha256: str

    def neutral(self) -> dict[str, Any]:
        return {
            "sourcePath": self.source_path,
            "sourceBytes": self.source_bytes,
            "sourceSha256": self.source_sha256,
            "originalFamily": self.original_family,
            "originalExtension": self.original_extension,
            "classification": "map-payload",
            "detectedKind": self.detected_kind,
            "evidenceSha256": self.evidence_sha256,
        }


@dataclass(frozen=True, slots=True)
class NativeCorpusOutput:
    """One unique validated object in the published corpus."""

    path: str
    byte_length: int
    sha256: str
    native_family: str
    evidence: Mapping[str, Any]

    def neutral(self) -> dict[str, Any]:
        return {
            "path": self.path,
            "bytes": self.byte_length,
            "sha256": self.sha256,
            "nativeFamily": self.native_family,
            "evidence": _json_clone(self.evidence),
        }


@dataclass(frozen=True, slots=True)
class NativeCorpusReport:
    """Local paths plus deterministic, JSON-ready native corpus evidence."""

    source_root: Path
    output_root: Path
    manifest_path: Path
    source_manifest_sha256: str
    source_manifest_aggregate_sha256: str
    request_sha256: str
    identity_sha256: str
    manifest_sha256: str
    families: tuple[str, ...]
    conversion: Mapping[str, Any]
    entries: tuple[NativeCorpusEntry, ...]
    reclassified: tuple[NativeCorpusReclassification, ...]
    outputs: tuple[NativeCorpusOutput, ...]
    reused: bool

    @property
    def complete(self) -> bool:
        return True

    @property
    def candidate_file_count(self) -> int:
        return len(self.entries) + len(self.reclassified)

    @property
    def candidate_bytes(self) -> int:
        return sum(item.source_bytes for item in self.entries) + sum(
            item.source_bytes for item in self.reclassified
        )

    @property
    def converted_file_count(self) -> int:
        return len(self.entries)

    @property
    def converted_bytes(self) -> int:
        return sum(item.source_bytes for item in self.entries)

    @property
    def reclassified_file_count(self) -> int:
        return len(self.reclassified)

    @property
    def reclassified_bytes(self) -> int:
        return sum(item.source_bytes for item in self.reclassified)

    @property
    def selected_bytes(self) -> int:
        """Backward-compatible alias for all extension-selected candidate bytes."""

        return self.candidate_bytes

    @property
    def output_bytes(self) -> int:
        return sum(item.byte_length for item in self.outputs)

    def neutral(self) -> dict[str, Any]:
        """Return the exact canonical manifest document (host paths omitted)."""

        selection = {
            "families": list(self.families),
            "sourceManifestSha256": self.source_manifest_sha256,
            "sourceManifestAggregateSha256": self.source_manifest_aggregate_sha256,
            "requestSha256": self.request_sha256,
            "conversion": _json_clone(self.conversion),
        }
        totals = {
            "candidateFileCount": self.candidate_file_count,
            "candidateBytes": self.candidate_bytes,
            "convertedFileCount": self.converted_file_count,
            "convertedBytes": self.converted_bytes,
            "reclassifiedFileCount": self.reclassified_file_count,
            "reclassifiedBytes": self.reclassified_bytes,
            "outputFileCount": len(self.outputs),
            "outputBytes": self.output_bytes,
        }
        return {
            "schema": NATIVE_CORPUS_SCHEMA,
            "schemaVersion": NATIVE_CORPUS_SCHEMA_VERSION,
            "selection": selection,
            "totals": totals,
            "entries": [item.neutral() for item in self.entries],
            "reclassified": [item.neutral() for item in self.reclassified],
            "outputs": [item.neutral() for item in self.outputs],
            "identitySha256": self.identity_sha256,
        }

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class _ManifestFile:
    path: str
    archive: str
    size: int
    sha256: str


@dataclass(frozen=True, slots=True)
class _ValidatedInput:
    manifest_path: Path
    manifest_sha256: str
    aggregate_sha256: str
    files: tuple[_ManifestFile, ...]
    total_bytes: int


@dataclass(frozen=True, slots=True)
class _TreeFile:
    relative_path: str
    path: Path
    size: int


@dataclass(frozen=True, slots=True)
class _SelectedFile:
    source: _ManifestFile
    family: str
    extension: str


@dataclass(frozen=True, slots=True)
class _FFmpegTool:
    path: Path
    size: int
    mtime_ns: int
    device: int
    inode: int
    evidence: Mapping[str, Any]


@dataclass(frozen=True, slots=True)
class _PreparedCandidate:
    """One isolated native candidate prepared for canonical publication."""

    candidate: Path
    native_family: str
    suffix: str
    evidence: Mapping[str, Any]


class _DuplicateJsonKey(ValueError):
    pass


def _json_clone(value: Any) -> Any:
    return json.loads(
        json.dumps(
            value,
            ensure_ascii=True,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        )
    )


def _summary_token(value: object) -> str:
    """Return one bounded log-safe counter label with no path punctuation."""

    raw = value if isinstance(value, str) else "unknown"
    normalized = "".join(
        character
        for character in raw.casefold()
        if character.isascii() and (character.isalnum() or character in "-_")
    )
    return (normalized or "unknown")[:40]


def _bounded_count_summary(values: Iterable[object]) -> str:
    counts = Counter(_summary_token(value) for value in values)
    ordered = sorted(counts.items())
    shown = ordered[:8]
    summary = ", ".join(f"{name}={count}" for name, count in shown)
    omitted = sum(count for _, count in ordered[8:])
    if omitted:
        summary += f", other={omitted}"
    return summary or "none"


def _canonical_json_bytes(value: object, *, pretty: bool = False) -> bytes:
    if pretty:
        text = json.dumps(
            value,
            ensure_ascii=True,
            allow_nan=False,
            indent=2,
            sort_keys=True,
        )
    else:
        text = json.dumps(
            value,
            ensure_ascii=True,
            allow_nan=False,
            sort_keys=True,
            separators=(",", ":"),
        )
    return (text + "\n").encode("utf-8")


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
        if os.path.lexists(current) and _is_link_like(current):
            raise NativeCorpusError(f"{context} is linked: {current}")


def _resolve_source_root(value: Path | str) -> Path:
    try:
        candidate = Path(value)
    except TypeError as exc:
        raise TypeError("effective-assets root must be a filesystem path") from exc
    _refuse_link_chain(candidate, context="effective-assets root")
    try:
        root = candidate.expanduser().resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise NativeCorpusError(
            f"effective-assets root is unavailable: {candidate}"
        ) from exc
    if not root.is_dir() or _is_link_like(root):
        raise NativeCorpusError("effective-assets root is not an unlinked directory")
    return root


def _paths_overlap(first: Path, second: Path) -> bool:
    try:
        common = os.path.commonpath(
            [os.path.normcase(str(first)), os.path.normcase(str(second))]
        )
    except ValueError:
        return False
    return common in {
        os.path.normcase(str(first)),
        os.path.normcase(str(second)),
    }


def _resolve_output_root(value: Path | str, source_root: Path) -> Path:
    try:
        candidate = Path(value).expanduser()
    except TypeError as exc:
        raise TypeError("native corpus output root must be a filesystem path") from exc
    absolute = Path(os.path.abspath(candidate))
    if not absolute.name:
        raise NativeCorpusError(
            "native corpus output root cannot be a filesystem anchor"
        )
    _refuse_link_chain(absolute.parent, context="native corpus output parent")
    try:
        absolute.parent.mkdir(parents=True, exist_ok=True)
        _refuse_link_chain(absolute.parent, context="native corpus output parent")
        parent = absolute.parent.resolve(strict=True)
    except (OSError, RuntimeError) as exc:
        raise NativeCorpusError("native corpus output parent is unavailable") from exc
    if not parent.is_dir() or _is_link_like(parent):
        raise NativeCorpusError(
            "native corpus output parent is not an unlinked directory"
        )
    output = parent / absolute.name
    if _paths_overlap(source_root, output):
        raise NativeCorpusError(
            "effective-assets root and native corpus output root must not overlap"
        )
    if os.path.lexists(output):
        if _is_link_like(output):
            raise NativeCorpusError("native corpus output root must not be linked")
        if not output.is_dir():
            raise NativeCorpusError("native corpus output root is not a directory")
    return output


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
    if _is_link_like(path) or not path.is_file():
        raise NativeCorpusError(f"{label} is missing, linked, or not a file")
    try:
        before = path.stat()
        if not 1 <= before.st_size <= maximum:
            raise NativeCorpusLimitError(f"{label} exceeds its safety bound")
        raw = path.read_bytes()
        after = path.stat()
    except NativeCorpusError:
        raise
    except OSError as exc:
        raise NativeCorpusError(f"{label} cannot be read") from exc
    if (
        len(raw) != before.st_size
        or after.st_size != before.st_size
        or after.st_mtime_ns != before.st_mtime_ns
    ):
        raise NativeCorpusError(f"{label} changed during read")
    try:
        value = json.loads(
            raw.decode("utf-8"),
            object_pairs_hook=_object_without_duplicate_keys,
            parse_constant=_reject_json_constant,
        )
    except (UnicodeDecodeError, json.JSONDecodeError, ValueError) as exc:
        raise NativeCorpusError(f"{label} is invalid JSON") from exc
    if not isinstance(value, dict):
        raise NativeCorpusError(f"{label} root is not an object")
    return value, raw


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


def _validate_effective_identity(manifest: Mapping[str, object]) -> None:
    catalog = manifest.get("catalog")
    if not isinstance(catalog, dict) or set(catalog) != {
        "archive_count",
        "entry_count",
        "format",
        "identity_sha256",
    }:
        raise NativeCorpusError("effective-assets manifest catalog identity is invalid")
    if (
        not _is_int(catalog.get("archive_count"))
        or not _is_int(catalog.get("entry_count"))
        or not _is_int(catalog.get("format"), minimum=1)
        or not _is_sha256(catalog.get("identity_sha256"))
    ):
        raise NativeCorpusError("effective-assets manifest catalog identity is invalid")
    install = manifest.get("install")
    if not isinstance(install, dict) or set(install) != {"identity_sha256", "root"}:
        raise NativeCorpusError("effective-assets manifest install identity is invalid")
    if not _is_sha256(install.get("identity_sha256")) or not isinstance(
        install.get("root"), str
    ):
        raise NativeCorpusError("effective-assets manifest install identity is invalid")


def _validate_effective_file(raw: object, index: int) -> _ManifestFile:
    if not isinstance(raw, dict) or set(raw) != {
        "archive",
        "offset",
        "path",
        "precedence",
        "sha256",
        "size",
    }:
        raise NativeCorpusError(
            f"effective-assets manifest file entry {index} has an invalid shape"
        )
    archive = raw.get("archive")
    path_value = raw.get("path")
    size = raw.get("size")
    digest = raw.get("sha256")
    if not isinstance(archive, str) or not archive:
        raise NativeCorpusError(
            f"effective-assets manifest file entry {index} has an invalid archive"
        )
    if (
        not _is_int(raw.get("offset"))
        or not _is_int(raw.get("precedence"))
        or not _is_int(size)
        or not _is_sha256(digest)
    ):
        raise NativeCorpusError(
            f"effective-assets manifest file entry {index} has invalid metadata"
        )
    if not isinstance(path_value, str):
        raise NativeCorpusError(
            f"effective-assets manifest file entry {index} has an invalid path"
        )
    try:
        parts = safe_relative_parts(path_value)
        archive_parts = safe_relative_parts(archive)
    except (TypeError, ValueError) as exc:
        raise NativeCorpusError(
            f"effective-assets manifest file entry {index} has an unsafe path"
        ) from exc
    canonical_path = "/".join(parts)
    canonical_archive = "/".join(archive_parts)
    if canonical_path != path_value or canonical_archive != archive:
        raise NativeCorpusError(
            f"effective-assets manifest file entry {index} has a non-canonical path"
        )
    if parts[0].casefold() == ".openbfme":
        raise NativeCorpusError(
            f"effective-assets manifest path uses reserved metadata space: {path_value!r}"
        )
    return _ManifestFile(path_value, archive, size, digest)


def _validate_inventory_paths(files: tuple[_ManifestFile, ...]) -> None:
    seen: dict[str, str] = {}
    for item in files:
        key = item.path.casefold()
        if key in seen:
            raise NativeCorpusError(
                "effective-assets manifest contains case-colliding paths: "
                f"{seen[key]!r} and {item.path!r}"
            )
        seen[key] = item.path
    paths = [item.path for item in files]
    if paths != sorted(paths, key=lambda value: value.casefold()):
        raise NativeCorpusError("effective-assets manifest inventory is not canonical")
    folded = set(seen)
    for key in folded:
        parts = key.split("/")
        for index in range(1, len(parts)):
            if "/".join(parts[:index]) in folded:
                raise NativeCorpusError(
                    "effective-assets manifest has a file/directory path collision"
                )


def _load_effective_manifest(root: Path) -> _ValidatedInput:
    manifest_path = root.joinpath(
        *PurePosixPath(EFFECTIVE_ASSET_MANIFEST_RELATIVE).parts
    )
    _refuse_link_chain(manifest_path, context="effective-assets manifest")
    manifest, raw = _read_strict_json(
        manifest_path, MAX_MANIFEST_BYTES, label="effective-assets manifest"
    )
    if set(manifest) != {
        "aggregate_sha256",
        "catalog",
        "files",
        "install",
        "schema",
        "schema_version",
        "totals",
    }:
        raise NativeCorpusError("effective-assets manifest top-level shape is invalid")
    if manifest.get("schema") != EFFECTIVE_ASSET_MANIFEST_SCHEMA:
        raise NativeCorpusError("effective-assets manifest schema is unsupported")
    if manifest.get("schema_version") != EFFECTIVE_ASSET_MANIFEST_VERSION or isinstance(
        manifest.get("schema_version"), bool
    ):
        raise NativeCorpusError(
            "effective-assets manifest schema version is unsupported"
        )
    _validate_effective_identity(manifest)
    raw_files = manifest.get("files")
    if not isinstance(raw_files, list):
        raise NativeCorpusError("effective-assets manifest file inventory is invalid")
    if len(raw_files) > MAX_NATIVE_CORPUS_FILES:
        raise NativeCorpusLimitError(
            "effective-assets manifest file count exceeds its safety bound"
        )
    files = tuple(
        _validate_effective_file(item, index) for index, item in enumerate(raw_files)
    )
    _validate_inventory_paths(files)
    totals = manifest.get("totals")
    if not isinstance(totals, dict) or set(totals) != {"bytes", "files"}:
        raise NativeCorpusError("effective-assets manifest totals are invalid")
    total_files = totals.get("files")
    total_bytes = totals.get("bytes")
    if not _is_int(total_files) or not _is_int(total_bytes):
        raise NativeCorpusError("effective-assets manifest totals are invalid")
    calculated = sum(item.size for item in files)
    if total_files != len(files) or total_bytes != calculated:
        raise NativeCorpusError(
            "effective-assets manifest totals do not match its inventory"
        )
    if total_bytes > MAX_NATIVE_CORPUS_BYTES:
        raise NativeCorpusLimitError(
            "effective-assets manifest byte total exceeds its safety bound"
        )
    aggregate = manifest.get("aggregate_sha256")
    if not _is_sha256(aggregate) or aggregate != _manifest_aggregate(files):
        raise NativeCorpusError(
            "effective-assets manifest aggregate SHA-256 is invalid"
        )
    return _ValidatedInput(
        manifest_path=manifest_path,
        manifest_sha256=hashlib.sha256(raw).hexdigest(),
        aggregate_sha256=aggregate,
        files=files,
        total_bytes=total_bytes,
    )


def _tree_collision(
    values: Mapping[str, _TreeFile | str], key: str, relative: str, *, label: str
) -> None:
    previous = values.get(key)
    if previous is None:
        return
    previous_path = (
        previous.relative_path if isinstance(previous, _TreeFile) else previous
    )
    raise NativeCorpusError(
        f"{label} contains case-colliding paths: {previous_path!r} and {relative!r}"
    )


def _scan_tree(
    root: Path, *, label: str
) -> tuple[dict[str, _TreeFile], dict[str, str]]:
    files: dict[str, _TreeFile] = {}
    directories: dict[str, str] = {}
    pending = [root]
    while pending:
        directory = pending.pop()
        try:
            with os.scandir(directory) as iterator:
                entries = sorted(
                    iterator, key=lambda item: (item.name.casefold(), item.name)
                )
        except OSError as exc:
            raise NativeCorpusError(f"{label} cannot be enumerated") from exc
        for entry in entries:
            path = Path(entry.path)
            relative = path.relative_to(root).as_posix()
            if entry.is_symlink() or _is_link_like(path):
                raise NativeCorpusError(f"{label} contains a link: {relative!r}")
            key = relative.casefold()
            try:
                if entry.is_file(follow_symlinks=False):
                    _tree_collision(files, key, relative, label=label)
                    _tree_collision(directories, key, relative, label=label)
                    files[key] = _TreeFile(
                        relative_path=relative,
                        path=path,
                        size=entry.stat(follow_symlinks=False).st_size,
                    )
                elif entry.is_dir(follow_symlinks=False):
                    _tree_collision(directories, key, relative, label=label)
                    _tree_collision(files, key, relative, label=label)
                    directories[key] = relative
                    pending.append(path)
                else:
                    raise NativeCorpusError(
                        f"{label} contains an unsupported filesystem entry: {relative!r}"
                    )
            except OSError as exc:
                raise NativeCorpusError(
                    f"{label} entry cannot be inspected: {relative!r}"
                ) from exc
    return files, directories


def _expected_directories(paths: Iterable[str]) -> dict[str, str]:
    expected: dict[str, str] = {}
    for relative in paths:
        parts = PurePosixPath(relative).parts
        for index in range(1, len(parts)):
            directory = "/".join(parts[:index])
            expected[directory.casefold()] = directory
    return expected


def _validate_effective_tree(
    root: Path, manifest: _ValidatedInput
) -> dict[str, _TreeFile]:
    actual_files, actual_directories = _scan_tree(root, label="effective-assets tree")
    declared = [item.path for item in manifest.files]
    declared.append(EFFECTIVE_ASSET_MANIFEST_RELATIVE)
    expected_files = {item.casefold(): item for item in declared}
    expected_directories = _expected_directories(declared)
    missing_files = sorted(set(expected_files) - set(actual_files))
    extra_files = sorted(set(actual_files) - set(expected_files))
    missing_directories = sorted(set(expected_directories) - set(actual_directories))
    extra_directories = sorted(set(actual_directories) - set(expected_directories))
    if missing_files:
        raise NativeCorpusError(
            "effective-assets tree is missing declared files: "
            + ", ".join(expected_files[key] for key in missing_files[:5])
        )
    if extra_files:
        raise NativeCorpusError(
            "effective-assets tree contains undeclared files: "
            + ", ".join(actual_files[key].relative_path for key in extra_files[:5])
        )
    if missing_directories:
        raise NativeCorpusError(
            "effective-assets tree is missing declared directories: "
            + ", ".join(expected_directories[key] for key in missing_directories[:5])
        )
    if extra_directories:
        raise NativeCorpusError(
            "effective-assets tree contains undeclared directories: "
            + ", ".join(actual_directories[key] for key in extra_directories[:5])
        )
    for item in manifest.files:
        if actual_files[item.path.casefold()].size != item.size:
            raise NativeCorpusError(
                f"effective-assets file size does not match the manifest: {item.path!r}"
            )
    return actual_files


def _normalize_families(families: Iterable[str] | None) -> tuple[str, ...]:
    if families is None:
        return NATIVE_CORPUS_FAMILIES
    if isinstance(families, (str, bytes)):
        raise TypeError("native corpus families must be an iterable of family names")
    try:
        raw_values = tuple(families)
    except TypeError as exc:
        raise TypeError(
            "native corpus families must be an iterable of family names"
        ) from exc
    if not raw_values:
        raise ValueError("native corpus families must not be empty")
    normalized: set[str] = set()
    for value in raw_values:
        if not isinstance(value, str):
            raise TypeError("native corpus family names must be strings")
        family = value.strip().casefold()
        if family not in NATIVE_CORPUS_FAMILIES:
            raise ValueError(f"unsupported native corpus family: {value!r}")
        normalized.add(family)
    return tuple(item for item in NATIVE_CORPUS_FAMILIES if item in normalized)


def _selected_limit(value: int | None, maximum: int, *, label: str) -> int:
    selected = maximum if value is None else value
    if isinstance(selected, bool) or not isinstance(selected, int):
        raise TypeError(f"native corpus {label} limit must be an integer")
    if not 1 <= selected <= maximum:
        raise ValueError(f"native corpus {label} limit must be 1..{maximum}")
    return selected


def _texture_worker_count(value: int) -> int:
    if isinstance(value, bool) or not isinstance(value, int):
        raise TypeError("native corpus texture worker count must be an integer")
    if not 1 <= value <= 8:
        raise ValueError("native corpus texture worker count must be 1..8")
    return value


def _classify(item: _ManifestFile) -> tuple[str, str] | None:
    path = PurePosixPath(item.path)
    extension = path.suffix.casefold()
    if extension in _KNOWN_TEXTURE_EXTENSIONS:
        return "texture", extension
    if extension not in _KNOWN_AUDIO_EXTENSIONS:
        return None
    parts = tuple(part.casefold() for part in path.parts)
    archive_name = PurePosixPath(item.archive).name.casefold()
    if (
        archive_name == "music.big"
        or "music" in parts
        or parts[:3] == ("data", "audio", "tracks")
    ):
        return "music", extension
    return "sound", extension


def _select_files(
    manifest: _ValidatedInput,
    families: tuple[str, ...],
    *,
    max_files: int,
    max_total_bytes: int,
) -> tuple[_SelectedFile, ...]:
    allowed = set(families)
    selected: list[_SelectedFile] = []
    for item in manifest.files:
        classified = _classify(item)
        if classified is None or classified[0] not in allowed:
            continue
        selected.append(_SelectedFile(item, classified[0], classified[1]))
    result = tuple(selected)
    if not result:
        raise NativeCorpusError(
            "effective-assets manifest declares no files for the selected native families"
        )
    if len(result) > max_files:
        raise NativeCorpusLimitError(
            f"native corpus selected file count exceeds {max_files}"
        )
    total = sum(item.source.size for item in result)
    if total > max_total_bytes:
        raise NativeCorpusLimitError(
            f"native corpus selected byte total exceeds {max_total_bytes}"
        )
    return result


def _request_sha256(
    manifest: _ValidatedInput,
    families: tuple[str, ...],
    selected: tuple[_SelectedFile, ...],
    reclassified: tuple[NativeCorpusReclassification, ...],
    conversion: Mapping[str, Any],
) -> str:
    by_path = {item.source_path.casefold(): item for item in reclassified}
    sources: list[dict[str, Any]] = []
    for item in selected:
        record: dict[str, Any] = {
            "path": item.source.path,
            "bytes": item.source.size,
            "sha256": item.source.sha256,
            "family": item.family,
            "extension": item.extension,
            "disposition": "media-conversion",
        }
        detected = by_path.get(item.source.path.casefold())
        if detected is not None:
            record.update(
                {
                    "disposition": "map-payload",
                    "detectedKind": detected.detected_kind,
                    "evidenceSha256": detected.evidence_sha256,
                }
            )
        sources.append(record)
    return _request_sha256_from_sources(
        manifest.manifest_sha256,
        manifest.aggregate_sha256,
        families,
        conversion,
        sources,
    )


def _request_sha256_from_sources(
    source_manifest_sha256: str,
    source_manifest_aggregate_sha256: str,
    families: tuple[str, ...],
    conversion: Mapping[str, Any],
    sources: Sequence[Mapping[str, Any]],
) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.native-corpus-request",
            "schemaVersion": 0,
            "families": list(families),
            "sourceManifestSha256": source_manifest_sha256,
            "sourceManifestAggregateSha256": source_manifest_aggregate_sha256,
            "conversion": _json_clone(conversion),
            "sources": [_json_clone(item) for item in sources],
        }
    )


def _dependency() -> tuple[Any, Any]:
    try:
        import PIL
        from PIL import Image
    except ImportError as exc:
        raise NativeCorpusDependencyError(
            f"Pillow {PINNED_PILLOW_VERSION} is required for native texture conversion"
        ) from exc
    if PIL.__version__ != PINNED_PILLOW_VERSION:
        raise NativeCorpusDependencyError(
            f"Pillow {PINNED_PILLOW_VERSION} is required for deterministic texture output; "
            f"found {PIL.__version__}"
        )
    return PIL, Image


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(HASH_BLOCK_BYTES):
            digest.update(block)
    return digest.hexdigest()


def _ffmpeg_evidence() -> dict[str, Any]:
    """Return the path-free pinned WAV conversion attestation."""

    return {
        "ffmpeg": {
            "tool": "ffmpeg",
            "version": PINNED_FFMPEG_VERSION,
            "executableSha256": FFMPEG_EXE_SHA256,
            "container": "wav",
            "audioCodec": "pcm_s16le",
            "sampleFormat": "s16",
            "argumentTemplate": list(FFMPEG_WAV_ARGUMENT_TEMPLATE),
        }
    }


def _ffmpeg_stat(path: Path) -> tuple[int, int, int, int]:
    value = path.stat()
    return value.st_size, value.st_mtime_ns, value.st_dev, value.st_ino


def _resolve_ffmpeg(value: Path | str | None) -> _FFmpegTool:
    """Locate and attest the pinned FFmpeg without exposing its host path."""

    if value is None:
        candidate = discover_executable("ffmpeg", "OPENBFME_FFMPEG")
    else:
        try:
            candidate = Path(value).expanduser()
        except TypeError as exc:
            raise TypeError(
                "native corpus ffmpeg path must be a filesystem path"
            ) from exc
    if candidate is None:
        raise NativeCorpusDependencyError(
            f"pinned FFmpeg {PINNED_FFMPEG_VERSION} is required for WAV conversion"
        )
    try:
        _refuse_link_chain(candidate, context="FFmpeg executable")
        executable = candidate.resolve(strict=True)
        if not executable.is_file() or _is_link_like(executable):
            raise OSError
        before = _ffmpeg_stat(executable)
        digest = _file_sha256(executable)
        after = _ffmpeg_stat(executable)
    except (NativeCorpusError, OSError, RuntimeError) as exc:
        raise NativeCorpusDependencyError(
            f"pinned FFmpeg {PINNED_FFMPEG_VERSION} is unavailable or unsafe"
        ) from exc
    if before != after:
        raise NativeCorpusDependencyError(
            "pinned FFmpeg changed while its identity was verified"
        )
    if digest.casefold() != FFMPEG_EXE_SHA256:
        raise NativeCorpusDependencyError(
            f"FFmpeg executable does not match the pinned {PINNED_FFMPEG_VERSION} hash"
        )
    try:
        version_result = subprocess.run(
            [str(executable), "-version"],
            check=False,
            capture_output=True,
            text=True,
            encoding="utf-8",
            errors="replace",
            timeout=15,
        )
    except (OSError, subprocess.TimeoutExpired) as exc:
        raise NativeCorpusDependencyError(
            f"pinned FFmpeg {PINNED_FFMPEG_VERSION} could not report its version"
        ) from exc
    lines = (version_result.stdout or version_result.stderr).splitlines()
    first_line = lines[0].strip() if lines else ""
    version_prefix = f"ffmpeg version {PINNED_FFMPEG_VERSION}"
    if version_result.returncode != 0 or not (
        first_line == version_prefix
        or first_line.startswith(version_prefix + "-")
        or first_line.startswith(version_prefix + " ")
    ):
        raise NativeCorpusDependencyError(
            f"FFmpeg did not report the pinned {PINNED_FFMPEG_VERSION} version"
        )
    try:
        final_stat = _ffmpeg_stat(executable)
    except OSError as exc:
        raise NativeCorpusDependencyError(
            "pinned FFmpeg became unavailable during verification"
        ) from exc
    if final_stat != after:
        raise NativeCorpusDependencyError(
            "pinned FFmpeg changed during version verification"
        )
    return _FFmpegTool(executable, *after, _ffmpeg_evidence())


def _verify_ffmpeg_unchanged(tool: _FFmpegTool) -> None:
    expected = (tool.size, tool.mtime_ns, tool.device, tool.inode)
    try:
        if _ffmpeg_stat(tool.path) != expected:
            raise NativeCorpusDependencyError(
                "pinned FFmpeg changed during native corpus conversion"
            )
        if _file_sha256(tool.path).casefold() != FFMPEG_EXE_SHA256:
            raise NativeCorpusDependencyError(
                "pinned FFmpeg changed during native corpus conversion"
            )
    except NativeCorpusDependencyError:
        raise
    except OSError as exc:
        raise NativeCorpusDependencyError(
            "pinned FFmpeg became unavailable during native corpus conversion"
        ) from exc


def _render_ffmpeg_command(tool: _FFmpegTool, source: Path, target: Path) -> list[str]:
    values = {
        "<input>": str(source),
        "<output>": str(target),
    }
    return [
        str(tool.path),
        *(values.get(argument, argument) for argument in FFMPEG_WAV_ARGUMENT_TEMPLATE),
    ]


def _transcode_wav(
    source: Path,
    target: Path,
    tool: _FFmpegTool,
) -> str | None:
    expected = (tool.size, tool.mtime_ns, tool.device, tool.inode)
    try:
        if _ffmpeg_stat(tool.path) != expected:
            return "trusted FFmpeg changed before conversion"
        result = subprocess.run(
            _render_ffmpeg_command(tool, source, target),
            check=False,
            capture_output=True,
            timeout=FFMPEG_TIMEOUT_SECONDS,
        )
    except subprocess.TimeoutExpired:
        target.unlink(missing_ok=True)
        return f"FFmpeg WAV conversion exceeded {FFMPEG_TIMEOUT_SECONDS} seconds"
    except OSError:
        target.unlink(missing_ok=True)
        return "FFmpeg WAV conversion could not be executed"
    if result.returncode != 0:
        target.unlink(missing_ok=True)
        return f"FFmpeg WAV conversion failed with exit code {result.returncode}"
    if not target.is_file() or _is_link_like(target):
        target.unlink(missing_ok=True)
        return "FFmpeg WAV conversion did not produce an unlinked output file"
    return None


def _copy_verified_source(
    actual: _TreeFile, expected: _ManifestFile, target: Path
) -> NativeCorpusFailure | None:
    if _is_link_like(actual.path):
        return NativeCorpusFailure(
            expected.path,
            "unknown",
            "source-became-linked",
            "source became linked before read",
        )
    digest = hashlib.sha256()
    copied = 0
    try:
        before = actual.path.stat()
        with actual.path.open("rb") as source, target.open("xb") as output:
            while True:
                block = source.read(HASH_BLOCK_BYTES)
                if not block:
                    break
                output.write(block)
                digest.update(block)
                copied += len(block)
        after = actual.path.stat()
    except OSError:
        target.unlink(missing_ok=True)
        return NativeCorpusFailure(
            expected.path,
            "unknown",
            "source-read-failed",
            "source could not be copied into staging",
        )
    if (
        copied != expected.size
        or copied != before.st_size
        or after.st_size != before.st_size
        or after.st_mtime_ns != before.st_mtime_ns
    ):
        target.unlink(missing_ok=True)
        return NativeCorpusFailure(
            expected.path,
            "unknown",
            "source-changed",
            "source size or timestamp changed during staging",
        )
    if digest.hexdigest() != expected.sha256:
        target.unlink(missing_ok=True)
        return NativeCorpusFailure(
            expected.path,
            "unknown",
            "source-sha256-mismatch",
            "source SHA-256 does not match the effective-assets manifest",
        )
    return None


def _read_verified_map_candidate(
    actual: _TreeFile,
    expected: _ManifestFile,
) -> tuple[bytes | None, NativeCorpusFailure | None]:
    """Verify a texture candidate exactly and retain only decoder-bounded bytes."""

    if _is_link_like(actual.path):
        return None, NativeCorpusFailure(
            expected.path,
            "unknown",
            "source-became-linked",
            "source became linked before map-payload detection",
        )
    digest = hashlib.sha256()
    copied = 0
    captured = bytearray() if expected.size <= MAX_SAGE_MAP_SOURCE_BYTES else None
    try:
        before = actual.path.stat()
        with actual.path.open("rb") as source:
            while block := source.read(HASH_BLOCK_BYTES):
                digest.update(block)
                copied += len(block)
                if captured is not None:
                    captured.extend(block)
        after = actual.path.stat()
    except OSError:
        return None, NativeCorpusFailure(
            expected.path,
            "unknown",
            "source-read-failed",
            "source could not be verified for map-payload detection",
        )
    if (
        copied != expected.size
        or copied != before.st_size
        or after.st_size != before.st_size
        or after.st_mtime_ns != before.st_mtime_ns
    ):
        return None, NativeCorpusFailure(
            expected.path,
            "unknown",
            "source-changed",
            "source size or timestamp changed during map-payload detection",
        )
    if digest.hexdigest() != expected.sha256:
        return None, NativeCorpusFailure(
            expected.path,
            "unknown",
            "source-sha256-mismatch",
            "source SHA-256 does not match the effective-assets manifest",
        )
    return (bytes(captured) if captured is not None else None), None


def _reclassification_evidence_sha256(
    item: _SelectedFile,
    body: bytes,
    decoder_evidence: Mapping[str, Any],
) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.native-corpus-map-payload-evidence",
            "schemaVersion": 0,
            "sourcePath": item.source.path,
            "sourceBytes": item.source.size,
            "sourceSha256": item.source.sha256,
            "originalFamily": item.family,
            "originalExtension": item.extension,
            "decodedBodyBytes": len(body),
            "decodedBodySha256": hashlib.sha256(body).hexdigest(),
            "decoder": _json_clone(decoder_evidence),
        }
    )


def _detect_map_payload(
    item: _SelectedFile, payload: bytes | None
) -> NativeCorpusReclassification | None:
    if payload is None or not (
        payload.startswith(b"CkMp") or payload.startswith(b"EAR\0")
    ):
        return None
    try:
        body, evidence = decode_sage_map_blob(payload)
    except SageMapError:
        return None
    kind = evidence.get("kind")
    if not body.startswith(b"CkMp") or kind not in {"uncompressed", "ear-refpack"}:
        return None
    return NativeCorpusReclassification(
        source_path=item.source.path,
        source_bytes=item.source.size,
        source_sha256=item.source.sha256,
        original_family=item.family,
        original_extension=item.extension,
        detected_kind=str(kind),
        evidence_sha256=_reclassification_evidence_sha256(item, body, evidence),
    )


def _scan_reclassifications(
    selected: tuple[_SelectedFile, ...],
    actual_files: Mapping[str, _TreeFile],
) -> tuple[
    tuple[NativeCorpusReclassification, ...],
    tuple[NativeCorpusFailure, ...],
]:
    reclassified: list[NativeCorpusReclassification] = []
    failures: list[NativeCorpusFailure] = []
    for item in selected:
        if item.family != "texture":
            continue
        payload, failure = _read_verified_map_candidate(
            actual_files[item.source.path.casefold()], item.source
        )
        if failure is not None:
            failures.append(
                NativeCorpusFailure(
                    item.source.path,
                    item.family,
                    failure.code,
                    failure.detail,
                )
            )
            continue
        detected = _detect_map_payload(item, payload)
        if detected is not None:
            reclassified.append(detected)
    return tuple(reclassified), tuple(failures)


def _unsupported_failure(item: _SelectedFile) -> NativeCorpusFailure | None:
    supported = (
        _SUPPORTED_TEXTURE_EXTENSIONS
        if item.family == "texture"
        else _SUPPORTED_AUDIO_EXTENSIONS
    )
    if item.extension in supported:
        return None
    accepted = ", ".join(sorted(supported))
    return NativeCorpusFailure(
        item.source.path,
        item.family,
        "unsupported-extension",
        f"{item.extension or '<none>'} is not supported; accepted extensions: {accepted}",
    )


def _render_texture(source: Path, target: Path, image_module: Any) -> str | None:
    try:
        bomb_warning = getattr(image_module, "DecompressionBombWarning", Warning)
        with warnings.catch_warnings():
            warnings.simplefilter("error", bomb_warning)
            with image_module.open(source) as opened:
                opened.load()
                converted = opened.convert("RGBA")
                converted.save(
                    target,
                    format="PNG",
                    compress_level=9,
                    optimize=False,
                )
                converted.close()
    except Exception as exc:  # Pillow exposes format-specific exception classes.
        target.unlink(missing_ok=True)
        return (
            f"Pillow could not decode and normalize the texture ({type(exc).__name__})"
        )
    return None


def _prepare_texture_candidate(
    item: _SelectedFile,
    staged_source: Path,
    candidate: Path,
    image_module: Any,
) -> _PreparedCandidate | NativeCorpusFailure:
    """Render and backtest one isolated texture without publishing its object."""

    try:
        render_error = _render_texture(staged_source, candidate, image_module)
    finally:
        staged_source.unlink(missing_ok=True)
    if render_error is not None:
        return NativeCorpusFailure(
            item.source.path,
            item.family,
            "texture-decode-failed",
            render_error,
        )
    evidence = validate_native_output("png", candidate)
    return _PreparedCandidate(
        candidate=candidate,
        native_family="png",
        suffix=".png",
        evidence=_json_clone(evidence),
    )


def _prepare_textures(
    selected: tuple[_SelectedFile, ...],
    actual_files: Mapping[str, _TreeFile],
    work: Path,
    image_module: Any,
    *,
    reclassified_paths: set[str],
    unsupported_paths: set[str],
    texture_workers: int,
) -> dict[int, _PreparedCandidate | NativeCorpusFailure]:
    """Prepare textures with at most ``texture_workers`` bounded jobs in flight."""

    textures = tuple(
        (index, item)
        for index, item in enumerate(selected)
        if item.family == "texture"
        and item.source.path.casefold() not in reclassified_paths
        and item.source.path.casefold() not in unsupported_paths
    )
    results: dict[int, _PreparedCandidate | NativeCorpusFailure] = {}

    def stage_source(index: int, item: _SelectedFile) -> Path | None:
        staged_source = work / f"source-{index:08d}{item.extension}"
        copy_failure = _copy_verified_source(
            actual_files[item.source.path.casefold()], item.source, staged_source
        )
        if copy_failure is None:
            return staged_source
        results[index] = NativeCorpusFailure(
            item.source.path,
            item.family,
            copy_failure.code,
            copy_failure.detail,
        )
        return None

    if texture_workers == 1:
        for index, item in textures:
            staged_source = stage_source(index, item)
            if staged_source is None:
                continue
            candidate = work / f"candidate-{index:08d}.png"
            results[index] = _prepare_texture_candidate(
                item, staged_source, candidate, image_module
            )
        return results

    pending: deque[tuple[int, Future[_PreparedCandidate | NativeCorpusFailure]]] = (
        deque()
    )
    executor = ThreadPoolExecutor(
        max_workers=texture_workers,
        thread_name_prefix="openbfme-texture",
    )
    try:
        for index, item in textures:
            staged_source = stage_source(index, item)
            if staged_source is None:
                continue
            candidate = work / f"candidate-{index:08d}.png"
            future = executor.submit(
                _prepare_texture_candidate,
                item,
                staged_source,
                candidate,
                image_module,
            )
            pending.append((index, future))
            if len(pending) >= texture_workers:
                completed_index, completed = pending.popleft()
                results[completed_index] = completed.result()
        while pending:
            completed_index, completed = pending.popleft()
            results[completed_index] = completed.result()
    except BaseException:
        for _, future in pending:
            future.cancel()
        executor.shutdown(wait=True, cancel_futures=True)
        raise
    else:
        executor.shutdown(wait=True)
    return results


def _object_relative(digest: str, suffix: str) -> str:
    return f"objects/sha256/{digest[:2]}/{digest}{suffix}"


def _safe_child(root: Path, relative: str, *, label: str) -> Path:
    try:
        parts = safe_relative_parts(relative)
    except (TypeError, ValueError) as exc:
        raise NativeCorpusError(f"{label} has an unsafe path: {relative!r}") from exc
    canonical = "/".join(parts)
    if canonical != relative:
        raise NativeCorpusError(f"{label} path is not canonical: {relative!r}")
    target = root.joinpath(*parts)
    try:
        target.relative_to(root)
    except ValueError as exc:
        raise NativeCorpusError(f"{label} escaped its root: {relative!r}") from exc
    return target


def _make_output(
    stage: Path,
    candidate: Path,
    *,
    native_family: str,
    suffix: str,
    outputs_by_path: dict[str, NativeCorpusOutput],
    prevalidated_evidence: Mapping[str, Any] | None = None,
) -> tuple[NativeCorpusOutput | None, str | None]:
    evidence = (
        validate_native_output(native_family, candidate)
        if prevalidated_evidence is None
        else _json_clone(prevalidated_evidence)
    )
    if not evidence.get("valid"):
        errors = evidence.get("errors")
        detail = (
            "; ".join(str(item) for item in errors)
            if isinstance(errors, list)
            else "validation failed"
        )
        return None, detail
    digest = evidence.get("sha256")
    byte_length = evidence.get("size")
    if not _is_sha256(digest) or not _is_int(byte_length):
        return None, "native backtest did not return a valid output identity"
    relative = _object_relative(digest, suffix)
    existing = outputs_by_path.get(relative)
    if existing is not None:
        if (
            existing.sha256 != digest
            or existing.byte_length != byte_length
            or existing.native_family != evidence.get("family")
            or existing.evidence != evidence
        ):
            return None, "content-address collision produced inconsistent evidence"
        candidate.unlink(missing_ok=True)
        return existing, None
    target = _safe_child(stage, relative, label="native corpus object")
    target.parent.mkdir(parents=True, exist_ok=True)
    if os.path.lexists(target):
        return None, "content-addressed output path already exists unexpectedly"
    os.replace(candidate, target)
    output = NativeCorpusOutput(
        path=relative,
        byte_length=byte_length,
        sha256=digest,
        native_family=str(evidence["family"]),
        evidence=_json_clone(evidence),
    )
    outputs_by_path[relative] = output
    return output, None


def _document(
    manifest: _ValidatedInput,
    families: tuple[str, ...],
    request_sha256: str,
    conversion: Mapping[str, Any],
    entries: tuple[NativeCorpusEntry, ...],
    reclassified: tuple[NativeCorpusReclassification, ...],
    outputs: tuple[NativeCorpusOutput, ...],
) -> dict[str, Any]:
    selection = {
        "families": list(families),
        "sourceManifestSha256": manifest.manifest_sha256,
        "sourceManifestAggregateSha256": manifest.aggregate_sha256,
        "requestSha256": request_sha256,
        "conversion": _json_clone(conversion),
    }
    totals = {
        "candidateFileCount": len(entries) + len(reclassified),
        "candidateBytes": sum(item.source_bytes for item in entries)
        + sum(item.source_bytes for item in reclassified),
        "convertedFileCount": len(entries),
        "convertedBytes": sum(item.source_bytes for item in entries),
        "reclassifiedFileCount": len(reclassified),
        "reclassifiedBytes": sum(item.source_bytes for item in reclassified),
        "outputFileCount": len(outputs),
        "outputBytes": sum(item.byte_length for item in outputs),
    }
    basis: dict[str, Any] = {
        "schema": NATIVE_CORPUS_SCHEMA,
        "schemaVersion": NATIVE_CORPUS_SCHEMA_VERSION,
        "selection": selection,
        "totals": totals,
        "entries": [item.neutral() for item in entries],
        "reclassified": [item.neutral() for item in reclassified],
        "outputs": [item.neutral() for item in outputs],
    }
    return {**basis, "identitySha256": _canonical_sha256(basis)}


def _validate_evidence(
    raw: object,
    *,
    path: Path,
    native_family: str,
    digest: str,
    byte_length: int,
) -> dict[str, Any]:
    if not isinstance(raw, dict):
        raise NativeCorpusError("native corpus output evidence is not an object")
    actual = validate_native_output(native_family, path)
    if not actual.get("valid"):
        raise NativeCorpusError(f"native corpus object failed backtest: {path.name}")
    if actual.get("sha256") != digest or actual.get("size") != byte_length:
        raise NativeCorpusError(
            f"native corpus object identity disagrees with its evidence: {path.name}"
        )
    if actual != raw:
        raise NativeCorpusError(
            f"native corpus object evidence is not canonical: {path.name}"
        )
    return _json_clone(actual)


def _parse_output(raw: object, root: Path) -> NativeCorpusOutput:
    if not isinstance(raw, dict) or set(raw) != {
        "path",
        "bytes",
        "sha256",
        "nativeFamily",
        "evidence",
    }:
        raise NativeCorpusError("native corpus output entry has an invalid shape")
    relative = raw.get("path")
    byte_length = raw.get("bytes")
    digest = raw.get("sha256")
    native_family = raw.get("nativeFamily")
    if (
        not isinstance(relative, str)
        or not _is_int(byte_length)
        or not _is_sha256(digest)
        or native_family not in {"png", "wav-pcm", "mp3"}
    ):
        raise NativeCorpusError("native corpus output entry has invalid metadata")
    suffix = {"png": ".png", "wav-pcm": ".wav", "mp3": ".mp3"}[native_family]
    if relative != _object_relative(digest, suffix):
        raise NativeCorpusError(
            "native corpus output path is not content-address canonical"
        )
    path = _safe_child(root, relative, label="native corpus output")
    if _is_link_like(path) or not path.is_file():
        raise NativeCorpusError(
            f"native corpus output is missing or linked: {relative!r}"
        )
    evidence = _validate_evidence(
        raw.get("evidence"),
        path=path,
        native_family=native_family,
        digest=digest,
        byte_length=byte_length,
    )
    return NativeCorpusOutput(relative, byte_length, digest, native_family, evidence)


def _parse_entry(
    raw: object, outputs: Mapping[str, NativeCorpusOutput]
) -> NativeCorpusEntry:
    expected_keys = {
        "sourcePath",
        "sourceArchive",
        "sourceExtension",
        "sourceBytes",
        "sourceSha256",
        "family",
        "outputPath",
        "outputBytes",
        "outputSha256",
        "nativeFamily",
        "evidence",
    }
    if not isinstance(raw, dict) or set(raw) != expected_keys:
        raise NativeCorpusError("native corpus source entry has an invalid shape")
    source_path = raw.get("sourcePath")
    source_archive = raw.get("sourceArchive")
    extension = raw.get("sourceExtension")
    source_bytes = raw.get("sourceBytes")
    source_sha256 = raw.get("sourceSha256")
    family = raw.get("family")
    output_path = raw.get("outputPath")
    output_bytes = raw.get("outputBytes")
    output_sha256 = raw.get("outputSha256")
    native_family = raw.get("nativeFamily")
    if (
        not isinstance(source_path, str)
        or not isinstance(source_archive, str)
        or not isinstance(extension, str)
        or not _is_int(source_bytes)
        or not _is_sha256(source_sha256)
        or family not in NATIVE_CORPUS_FAMILIES
        or not isinstance(output_path, str)
        or not _is_int(output_bytes)
        or not _is_sha256(output_sha256)
        or native_family not in {"png", "wav-pcm", "mp3"}
    ):
        raise NativeCorpusError("native corpus source entry has invalid metadata")
    try:
        if "/".join(safe_relative_parts(source_path)) != source_path:
            raise ValueError
        if "/".join(safe_relative_parts(source_archive)) != source_archive:
            raise ValueError
    except (TypeError, ValueError) as exc:
        raise NativeCorpusError(
            "native corpus source entry has an unsafe path"
        ) from exc
    if PurePosixPath(source_path).suffix.casefold() != extension:
        raise NativeCorpusError("native corpus source extension is inconsistent")
    output = outputs.get(output_path.casefold())
    if output is None:
        raise NativeCorpusError(
            "native corpus source entry references an unknown output"
        )
    if (
        output.path != output_path
        or output.byte_length != output_bytes
        or output.sha256 != output_sha256
        or output.native_family != native_family
        or output.evidence != raw.get("evidence")
    ):
        raise NativeCorpusError("native corpus source/output mapping is inconsistent")
    return NativeCorpusEntry(
        source_path,
        source_archive,
        extension,
        source_bytes,
        source_sha256,
        family,
        output_path,
        output_bytes,
        output_sha256,
        native_family,
        _json_clone(output.evidence),
    )


def _parse_reclassification(raw: object) -> NativeCorpusReclassification:
    expected_keys = {
        "sourcePath",
        "sourceBytes",
        "sourceSha256",
        "originalFamily",
        "originalExtension",
        "classification",
        "detectedKind",
        "evidenceSha256",
    }
    if not isinstance(raw, dict) or set(raw) != expected_keys:
        raise NativeCorpusError(
            "native corpus reclassification entry has an invalid shape"
        )
    source_path = raw.get("sourcePath")
    source_bytes = raw.get("sourceBytes")
    source_sha256 = raw.get("sourceSha256")
    original_family = raw.get("originalFamily")
    original_extension = raw.get("originalExtension")
    detected_kind = raw.get("detectedKind")
    evidence_sha256 = raw.get("evidenceSha256")
    if (
        not isinstance(source_path, str)
        or not _is_int(source_bytes)
        or not _is_sha256(source_sha256)
        or original_family != "texture"
        or original_extension not in _KNOWN_TEXTURE_EXTENSIONS
        or raw.get("classification") != "map-payload"
        or detected_kind not in {"uncompressed", "ear-refpack"}
        or not _is_sha256(evidence_sha256)
    ):
        raise NativeCorpusError(
            "native corpus reclassification entry has invalid metadata"
        )
    try:
        if "/".join(safe_relative_parts(source_path)) != source_path:
            raise ValueError
    except (TypeError, ValueError) as exc:
        raise NativeCorpusError(
            "native corpus reclassification source has an unsafe path"
        ) from exc
    if PurePosixPath(source_path).suffix.casefold() != original_extension:
        raise NativeCorpusError(
            "native corpus reclassification extension is inconsistent"
        )
    return NativeCorpusReclassification(
        source_path=source_path,
        source_bytes=source_bytes,
        source_sha256=source_sha256,
        original_family=original_family,
        original_extension=original_extension,
        detected_kind=detected_kind,
        evidence_sha256=evidence_sha256,
    )


def _manifest_request_sources(
    entries: tuple[NativeCorpusEntry, ...],
    reclassified: tuple[NativeCorpusReclassification, ...],
) -> tuple[dict[str, Any], ...]:
    sources: list[dict[str, Any]] = [
        {
            "path": item.source_path,
            "bytes": item.source_bytes,
            "sha256": item.source_sha256,
            "family": item.family,
            "extension": item.source_extension,
            "disposition": "media-conversion",
        }
        for item in entries
    ]
    sources.extend(
        {
            "path": item.source_path,
            "bytes": item.source_bytes,
            "sha256": item.source_sha256,
            "family": item.original_family,
            "extension": item.original_extension,
            "disposition": "map-payload",
            "detectedKind": item.detected_kind,
            "evidenceSha256": item.evidence_sha256,
        }
        for item in reclassified
    )
    return tuple(
        sorted(
            sources,
            key=lambda item: (str(item["path"]).casefold(), str(item["path"])),
        )
    )


def _parse_outputs(
    raw_outputs: list[object],
    root: Path,
    *,
    workers: int,
) -> tuple[NativeCorpusOutput, ...]:
    """Backtest canonical objects with bounded, order-stable concurrency."""

    if workers == 1 or len(raw_outputs) < 2:
        return tuple(_parse_output(item, root) for item in raw_outputs)
    with ThreadPoolExecutor(
        max_workers=workers,
        thread_name_prefix="openbfme-native-verify",
    ) as executor:
        # ``map`` yields in input order.  A failure therefore remains
        # deterministic even when later objects finish first, and leaving the
        # context drains every worker before transaction cleanup can run.
        return tuple(executor.map(lambda item: _parse_output(item, root), raw_outputs))


def _verify_output(
    root: Path,
    source_root: Path,
    *,
    reused: bool,
    workers: int = 1,
) -> NativeCorpusReport:
    if _is_link_like(root) or not root.is_dir():
        raise NativeCorpusError(
            "native corpus output root is missing, linked, or not a directory"
        )
    manifest_path = root / NATIVE_CORPUS_MANIFEST
    document, raw_bytes = _read_strict_json(
        manifest_path, MAX_MANIFEST_BYTES, label="native corpus manifest"
    )
    if raw_bytes != _canonical_json_bytes(document, pretty=True):
        raise NativeCorpusError("native corpus manifest encoding is not canonical")
    if set(document) != {
        "schema",
        "schemaVersion",
        "selection",
        "totals",
        "entries",
        "reclassified",
        "outputs",
        "identitySha256",
    }:
        raise NativeCorpusError("native corpus manifest top-level shape is invalid")
    if document.get("schema") != NATIVE_CORPUS_SCHEMA:
        raise NativeCorpusError("native corpus manifest schema is unsupported")
    if document.get("schemaVersion") != NATIVE_CORPUS_SCHEMA_VERSION or isinstance(
        document.get("schemaVersion"), bool
    ):
        raise NativeCorpusError("native corpus manifest schema version is unsupported")
    selection = document.get("selection")
    if not isinstance(selection, dict) or set(selection) != {
        "families",
        "sourceManifestSha256",
        "sourceManifestAggregateSha256",
        "requestSha256",
        "conversion",
    }:
        raise NativeCorpusError("native corpus manifest selection is invalid")
    raw_families = selection.get("families")
    if not isinstance(raw_families, list):
        raise NativeCorpusError("native corpus manifest families are invalid")
    try:
        families = _normalize_families(raw_families)
    except (TypeError, ValueError) as exc:
        raise NativeCorpusError("native corpus manifest families are invalid") from exc
    if list(families) != raw_families:
        raise NativeCorpusError("native corpus manifest families are not canonical")
    source_manifest_sha256 = selection.get("sourceManifestSha256")
    source_aggregate = selection.get("sourceManifestAggregateSha256")
    request_sha256 = selection.get("requestSha256")
    if not all(
        _is_sha256(value)
        for value in (source_manifest_sha256, source_aggregate, request_sha256)
    ):
        raise NativeCorpusError("native corpus manifest selection hashes are invalid")
    raw_conversion = selection.get("conversion")
    if not isinstance(raw_conversion, dict):
        raise NativeCorpusError("native corpus manifest conversion evidence is invalid")
    conversion = _json_clone(raw_conversion)
    raw_outputs = document.get("outputs")
    raw_entries = document.get("entries")
    raw_reclassified = document.get("reclassified")
    if (
        not isinstance(raw_outputs, list)
        or not isinstance(raw_entries, list)
        or not isinstance(raw_reclassified, list)
    ):
        raise NativeCorpusError("native corpus manifest inventories are invalid")
    outputs = _parse_outputs(raw_outputs, root, workers=workers)
    output_paths = [item.path for item in outputs]
    if output_paths != sorted(output_paths, key=lambda value: value.casefold()):
        raise NativeCorpusError("native corpus output inventory is not canonical")
    if len({item.casefold() for item in output_paths}) != len(output_paths):
        raise NativeCorpusError("native corpus output inventory case-collides")
    output_lookup = {item.path.casefold(): item for item in outputs}
    entries = tuple(_parse_entry(item, output_lookup) for item in raw_entries)
    entry_paths = [item.source_path for item in entries]
    if entry_paths != sorted(entry_paths, key=lambda value: value.casefold()):
        raise NativeCorpusError("native corpus source inventory is not canonical")
    if len({item.casefold() for item in entry_paths}) != len(entry_paths):
        raise NativeCorpusError("native corpus source inventory case-collides")
    reclassified = tuple(_parse_reclassification(item) for item in raw_reclassified)
    reclassified_paths = [item.source_path for item in reclassified]
    if reclassified_paths != sorted(
        reclassified_paths, key=lambda value: value.casefold()
    ):
        raise NativeCorpusError(
            "native corpus reclassification inventory is not canonical"
        )
    if len({item.casefold() for item in reclassified_paths}) != len(reclassified_paths):
        raise NativeCorpusError(
            "native corpus reclassification inventory case-collides"
        )
    if {item.casefold() for item in entry_paths} & {
        item.casefold() for item in reclassified_paths
    }:
        raise NativeCorpusError(
            "native corpus source is both converted and reclassified"
        )
    expected_conversion = (
        _ffmpeg_evidence()
        if any(item.source_extension == ".wav" for item in entries)
        else {}
    )
    if conversion != expected_conversion:
        raise NativeCorpusError(
            "native corpus manifest conversion evidence does not match its sources"
        )
    expected_request = _request_sha256_from_sources(
        source_manifest_sha256,
        source_aggregate,
        families,
        conversion,
        _manifest_request_sources(entries, reclassified),
    )
    if request_sha256 != expected_request:
        raise NativeCorpusError("native corpus manifest request SHA-256 is invalid")
    referenced = {item.output_path.casefold() for item in entries}
    if referenced != set(output_lookup):
        raise NativeCorpusError("native corpus contains an unreferenced output")
    totals = document.get("totals")
    expected_totals = {
        "candidateFileCount": len(entries) + len(reclassified),
        "candidateBytes": sum(item.source_bytes for item in entries)
        + sum(item.source_bytes for item in reclassified),
        "convertedFileCount": len(entries),
        "convertedBytes": sum(item.source_bytes for item in entries),
        "reclassifiedFileCount": len(reclassified),
        "reclassifiedBytes": sum(item.source_bytes for item in reclassified),
        "outputFileCount": len(outputs),
        "outputBytes": sum(item.byte_length for item in outputs),
    }
    if totals != expected_totals:
        raise NativeCorpusError("native corpus manifest totals are invalid")
    basis = {key: value for key, value in document.items() if key != "identitySha256"}
    identity = document.get("identitySha256")
    if not _is_sha256(identity) or identity != _canonical_sha256(basis):
        raise NativeCorpusError("native corpus manifest identity SHA-256 is invalid")
    actual_files, actual_directories = _scan_tree(root, label="native corpus tree")
    declared = [NATIVE_CORPUS_MANIFEST, *output_paths]
    expected_files = {item.casefold(): item for item in declared}
    expected_directories = _expected_directories(declared)
    if set(actual_files) != set(expected_files):
        missing = sorted(set(expected_files) - set(actual_files))
        extra = sorted(set(actual_files) - set(expected_files))
        if missing:
            raise NativeCorpusError(
                "native corpus tree is missing declared files: "
                + ", ".join(expected_files[key] for key in missing[:5])
            )
        raise NativeCorpusError(
            "native corpus tree contains undeclared files: "
            + ", ".join(actual_files[key].relative_path for key in extra[:5])
        )
    if set(actual_directories) != set(expected_directories):
        missing = sorted(set(expected_directories) - set(actual_directories))
        extra = sorted(set(actual_directories) - set(expected_directories))
        if missing:
            raise NativeCorpusError(
                "native corpus tree is missing declared directories: "
                + ", ".join(expected_directories[key] for key in missing[:5])
            )
        raise NativeCorpusError(
            "native corpus tree contains undeclared directories: "
            + ", ".join(actual_directories[key] for key in extra[:5])
        )
    return NativeCorpusReport(
        source_root=source_root,
        output_root=root,
        manifest_path=manifest_path,
        source_manifest_sha256=source_manifest_sha256,
        source_manifest_aggregate_sha256=source_aggregate,
        request_sha256=request_sha256,
        identity_sha256=identity,
        manifest_sha256=hashlib.sha256(raw_bytes).hexdigest(),
        families=families,
        conversion=conversion,
        entries=entries,
        reclassified=reclassified,
        outputs=outputs,
        reused=reused,
    )


def _remove_owned_tree(path: Path, parent: Path, prefix: str) -> None:
    if not os.path.lexists(path):
        return
    if path.parent != parent or not path.name.startswith(prefix) or _is_link_like(path):
        raise NativeCorpusError(
            "refused to remove an unowned native corpus transaction path"
        )
    _scan_tree(path, label="native corpus transaction tree")
    shutil.rmtree(path)


def _assert_unlinked_tree(root: Path) -> None:
    _scan_tree(root, label="existing native corpus tree")


def _publish(stage: Path, destination: Path, backup: Path) -> None:
    parent = destination.parent
    had_destination = os.path.lexists(destination)
    if had_destination:
        _assert_unlinked_tree(destination)
        os.replace(destination, backup)
    try:
        os.replace(stage, destination)
    except Exception as publish_error:
        if had_destination and os.path.lexists(backup):
            try:
                os.replace(backup, destination)
            except Exception as rollback_error:
                raise NativeCorpusError(
                    "native corpus publish failed and rollback could not restore the prior output"
                ) from rollback_error
        raise NativeCorpusError(
            "native corpus publish failed; prior output was preserved"
        ) from publish_error
    if had_destination:
        _remove_owned_tree(backup, parent, f".{destination.name}.backup-")


def build_native_corpus(
    effective_assets_root: Path | str,
    output_root: Path | str,
    *,
    families: Iterable[str] | None = None,
    ffmpeg_path: Path | str | None = None,
    max_files: int | None = None,
    max_total_bytes: int | None = None,
    force: bool = False,
    texture_workers: int = 1,
) -> NativeCorpusReport:
    """Build or verify a deterministic runtime-native corpus transactionally.

    ``families`` is a non-empty subset of ``texture``, ``sound``, and ``music``.
    Every selected WAV is decoded by the trusted pinned FFmpeg located through
    ``ffmpeg_path``, ``OPENBFME_FFMPEG``, or normal importer discovery, then
    emitted as PCM s16le.  MP3 files remain exact copies.
    ``texture_workers`` bounds isolated texture render/backtest jobs to 1..8;
    it is execution policy only and never contributes to corpus identity.
    A valid existing corpus with the same verified request is returned with
    ``report.reused`` set.  Any mismatch or tamper requires ``force=True``;
    force rebuilds in a sibling staging directory and leaves the old output in
    place until the replacement has passed a complete manifest/tree backtest.
    """

    if not isinstance(force, bool):
        raise TypeError("native corpus force flag must be a boolean")
    selected_texture_workers = _texture_worker_count(texture_workers)
    selected_families = _normalize_families(families)
    selected_max_files = _selected_limit(
        max_files, MAX_NATIVE_CORPUS_FILES, label="file count"
    )
    selected_max_bytes = _selected_limit(
        max_total_bytes, MAX_NATIVE_CORPUS_BYTES, label="total byte"
    )
    source_root = _resolve_source_root(effective_assets_root)
    manifest = _load_effective_manifest(source_root)
    actual_files = _validate_effective_tree(source_root, manifest)
    selected = _select_files(
        manifest,
        selected_families,
        max_files=selected_max_files,
        max_total_bytes=selected_max_bytes,
    )
    reclassified, detection_failures = _scan_reclassifications(selected, actual_files)
    if detection_failures:
        raise NativeCorpusBuildError(detection_failures)
    reclassified_paths = {item.source_path.casefold() for item in reclassified}
    ffmpeg_tool = (
        _resolve_ffmpeg(ffmpeg_path)
        if any(item.extension == ".wav" for item in selected)
        else None
    )
    conversion = _json_clone(ffmpeg_tool.evidence) if ffmpeg_tool is not None else {}
    request_sha256 = _request_sha256(
        manifest,
        selected_families,
        selected,
        reclassified,
        conversion,
    )
    destination = _resolve_output_root(output_root, source_root)

    if destination.is_dir() and not force:
        try:
            reused = _verify_output(
                destination,
                source_root,
                reused=True,
                workers=selected_texture_workers,
            )
        except NativeCorpusError as exc:
            raise NativeCorpusReuseError(
                f"existing native corpus failed verification: {exc}"
            ) from exc
        if reused.request_sha256 != request_sha256:
            raise NativeCorpusReuseError(
                "existing native corpus does not match the requested verified source selection; use force=True"
            )
        return reused

    unsupported = tuple(
        failure
        for item in selected
        if item.source.path.casefold() not in reclassified_paths
        if (failure := _unsupported_failure(item)) is not None
    )
    unsupported_paths = {item.source_path.casefold() for item in unsupported}

    image_module: Any | None = None
    if any(
        item.family == "texture"
        and item.extension in _SUPPORTED_TEXTURE_EXTENSIONS
        and item.source.path.casefold() not in reclassified_paths
        for item in selected
    ):
        _, image_module = _dependency()

    parent = destination.parent
    token = uuid.uuid4().hex
    stage = parent / f".{destination.name}.staging-{token}"
    backup = parent / f".{destination.name}.backup-{token}"
    try:
        stage.mkdir()
    except OSError as exc:
        raise NativeCorpusError(
            "native corpus staging directory could not be created"
        ) from exc
    work = stage / ".work"
    work.mkdir()
    entries: list[NativeCorpusEntry] = []
    outputs_by_path: dict[str, NativeCorpusOutput] = {}
    failures: list[NativeCorpusFailure] = list(unsupported)
    try:
        texture_results = _prepare_textures(
            selected,
            actual_files,
            work,
            image_module,
            reclassified_paths=reclassified_paths,
            unsupported_paths=unsupported_paths,
            texture_workers=selected_texture_workers,
        )
        for index, item in enumerate(selected):
            if item.source.path.casefold() in reclassified_paths:
                continue
            if item.source.path.casefold() in unsupported_paths:
                continue
            if item.family == "texture":
                prepared = texture_results[index]
                if isinstance(prepared, NativeCorpusFailure):
                    failures.append(prepared)
                    continue
            else:
                staged_source = work / f"source-{index:08d}{item.extension}"
                copy_failure = _copy_verified_source(
                    actual_files[item.source.path.casefold()],
                    item.source,
                    staged_source,
                )
                if copy_failure is not None:
                    failures.append(
                        NativeCorpusFailure(
                            item.source.path,
                            item.family,
                            copy_failure.code,
                            copy_failure.detail,
                        )
                    )
                    continue

            if item.family == "texture":
                candidate = prepared.candidate
                native_family = prepared.native_family
                suffix = prepared.suffix
                prevalidated_evidence: Mapping[str, Any] | None = prepared.evidence
            elif item.extension == ".wav":
                if ffmpeg_tool is None:  # Defensive: selection resolved this above.
                    raise NativeCorpusDependencyError(
                        "pinned FFmpeg is required for WAV conversion"
                    )
                candidate = work / f"candidate-{index:08d}.wav"
                render_error = _transcode_wav(
                    staged_source,
                    candidate,
                    ffmpeg_tool,
                )
                staged_source.unlink(missing_ok=True)
                if render_error is not None:
                    failures.append(
                        NativeCorpusFailure(
                            item.source.path,
                            item.family,
                            "audio-transcode-failed",
                            render_error,
                        )
                    )
                    continue
                native_family, suffix = "wav-pcm", ".wav"
                prevalidated_evidence = None
            else:
                candidate = staged_source
                native_family = "mp3"
                suffix = item.extension
                prevalidated_evidence = None

            output, output_error = _make_output(
                stage,
                candidate,
                native_family=native_family,
                suffix=suffix,
                outputs_by_path=outputs_by_path,
                prevalidated_evidence=prevalidated_evidence,
            )
            if output is None:
                candidate.unlink(missing_ok=True)
                failures.append(
                    NativeCorpusFailure(
                        item.source.path,
                        item.family,
                        "native-backtest-failed",
                        output_error or "native output backtest failed",
                    )
                )
                continue
            entries.append(
                NativeCorpusEntry(
                    source_path=item.source.path,
                    source_archive=item.source.archive,
                    source_extension=item.extension,
                    source_bytes=item.source.size,
                    source_sha256=item.source.sha256,
                    family=item.family,
                    output_path=output.path,
                    output_bytes=output.byte_length,
                    output_sha256=output.sha256,
                    native_family=output.native_family,
                    evidence=_json_clone(output.evidence),
                )
            )

        if ffmpeg_tool is not None:
            _verify_ffmpeg_unchanged(ffmpeg_tool)
        reclassified_selected = tuple(
            item
            for item in selected
            if item.source.path.casefold() in reclassified_paths
        )
        confirmed_reclassified, confirmation_failures = _scan_reclassifications(
            reclassified_selected, actual_files
        )
        failures.extend(confirmation_failures)
        if not confirmation_failures and confirmed_reclassified != reclassified:
            failures.append(
                NativeCorpusFailure(
                    reclassified[0].source_path,
                    "texture",
                    "map-payload-classification-changed",
                    "map-payload evidence changed during the native corpus build",
                )
            )
        if failures:
            raise NativeCorpusBuildError(failures)
        shutil.rmtree(work)
        entries_tuple = tuple(entries)
        outputs_tuple = tuple(
            outputs_by_path[path]
            for path in sorted(outputs_by_path, key=lambda value: value.casefold())
        )
        document = _document(
            manifest,
            selected_families,
            request_sha256,
            conversion,
            entries_tuple,
            reclassified,
            outputs_tuple,
        )
        (stage / NATIVE_CORPUS_MANIFEST).write_bytes(
            _canonical_json_bytes(document, pretty=True)
        )
        staged_report = _verify_output(
            stage,
            source_root,
            reused=False,
            workers=selected_texture_workers,
        )
        if staged_report.request_sha256 != request_sha256:
            raise NativeCorpusError("staged native corpus request identity changed")
        _publish(stage, destination, backup)
        return _verify_output(
            destination,
            source_root,
            reused=False,
            workers=selected_texture_workers,
        )
    finally:
        if os.path.lexists(stage):
            _remove_owned_tree(stage, parent, f".{destination.name}.staging-")
        if os.path.lexists(backup) and os.path.lexists(destination):
            _remove_owned_tree(backup, parent, f".{destination.name}.backup-")


# Concise alias for converter orchestration code.
build_corpus = build_native_corpus


__all__ = [
    "EFFECTIVE_ASSET_MANIFEST_RELATIVE",
    "EFFECTIVE_ASSET_MANIFEST_SCHEMA",
    "EFFECTIVE_ASSET_MANIFEST_VERSION",
    "FFMPEG_TIMEOUT_SECONDS",
    "FFMPEG_WAV_ARGUMENT_TEMPLATE",
    "MAX_NATIVE_CORPUS_BYTES",
    "MAX_NATIVE_CORPUS_FILES",
    "NATIVE_CORPUS_FAMILIES",
    "NATIVE_CORPUS_MANIFEST",
    "NATIVE_CORPUS_SCHEMA",
    "NATIVE_CORPUS_SCHEMA_VERSION",
    "NativeCorpusBuildError",
    "NativeCorpusDependencyError",
    "NativeCorpusEntry",
    "NativeCorpusError",
    "NativeCorpusFailure",
    "NativeCorpusLimitError",
    "NativeCorpusOutput",
    "NativeCorpusReclassification",
    "NativeCorpusReport",
    "NativeCorpusReuseError",
    "PINNED_FFMPEG_VERSION",
    "PINNED_PILLOW_VERSION",
    "build_corpus",
    "build_native_corpus",
]
