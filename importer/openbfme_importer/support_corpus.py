"""Verified exact-copy and handoff corpus for residual retail asset formats.

This lane intentionally makes only three narrow claims:

* strictly validated UTF-8 text and safely parsed XML schema files are copied
  byte-for-byte into content-addressed objects;
* structurally validated SFNT fonts are copied byte-for-byte after a bounded
  fontTools table/checksum backtest;
* opaque formats, and text-looking extensions whose payload is not proven
  text, receive a no-output ``runtime-converter-required`` handoff.

The effective-assets manifest and its exact unlinked tree are revalidated
before and after inspection.  A successful corpus has one disposition per
selected source, reconciled byte totals, canonical evidence, and an atomic
publish/rollback boundary.  It never calls an opaque payload runtime-native.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass
import hashlib
import json
import os
from pathlib import Path, PurePosixPath
import shutil
from typing import Any, Iterable, Mapping, Sequence
import uuid

from . import native_corpus as _native
from .bootstrap import DEFUSEDXML_VERSION, FONTTOOLS_VERSION
from .paths import safe_relative_parts


SUPPORT_CORPUS_SCHEMA = "openbfme.support-corpus"
SUPPORT_CORPUS_SCHEMA_VERSION = 0
SUPPORT_CORPUS_MANIFEST = "manifest.json"
SUPPORT_CORPUS_EXTENSIONS = (
    ".basis",
    ".bhav",
    ".cah",
    ".csv",
    ".dat",
    ".doc",
    ".fxo",
    ".nvp",
    ".otf",
    ".pso",
    ".ru",
    ".sec",
    ".ttf",
    ".vso",
    ".wak",
    ".xsd",
    ".xsx",
)

TEXT_EXTENSIONS = frozenset({".basis", ".bhav", ".csv", ".dat", ".nvp", ".ru"})
XML_EXTENSIONS = frozenset({".xsd", ".xsx"})
FONT_EXTENSIONS = frozenset({".otf", ".ttf"})
OPAQUE_EXTENSIONS = frozenset(
    {".cah", ".doc", ".fxo", ".pso", ".sec", ".vso", ".wak"}
)

if (
    TEXT_EXTENSIONS | XML_EXTENSIONS | FONT_EXTENSIONS | OPAQUE_EXTENSIONS
    != frozenset(SUPPORT_CORPUS_EXTENSIONS)
    or sum(
        len(items)
        for items in (
            TEXT_EXTENSIONS,
            XML_EXTENSIONS,
            FONT_EXTENSIONS,
            OPAQUE_EXTENSIONS,
        )
    )
    != len(SUPPORT_CORPUS_EXTENSIONS)
):  # pragma: no cover - import-time maintenance invariant.
    raise RuntimeError("support extension dispositions must be a complete partition")

MAX_SUPPORT_CORPUS_FILES = 50_000
MAX_SUPPORT_CORPUS_BYTES = 8 * 1024 * 1024 * 1024
MAX_TEXT_SOURCE_BYTES = 64 * 1024 * 1024
MAX_XML_NODES = 250_000
MAX_XML_DEPTH = 256
MAX_FONT_SOURCE_BYTES = 64 * 1024 * 1024
MAX_FONT_TABLES = 256
SIGNATURE_WINDOW_BYTES = 64
HASH_BLOCK_BYTES = 1024 * 1024
_SHA256_CHARACTERS = frozenset("0123456789abcdef")
_OUTPUT_FAMILIES = frozenset({"utf8-text", "xml-schema", "sfnt-font"})


class SupportCorpusError(ValueError):
    """Base class for rejected support-corpus operations."""


class SupportCorpusLimitError(SupportCorpusError):
    """Raised when a caller or validator safety bound is invalid/exceeded."""


class SupportCorpusDependencyError(SupportCorpusError):
    """Raised when a required authoritative validator is unavailable."""


class SupportCorpusReuseError(SupportCorpusError):
    """Raised when an existing destination cannot be safely reused."""


@dataclass(frozen=True, slots=True)
class SupportCorpusFailure:
    """One exact source failure; exception text exposes only aggregate labels."""

    source_path: str
    extension: str
    code: str
    detail: str


class SupportCorpusBuildError(SupportCorpusError):
    """Raised after a transactional build collects deterministic failures."""

    def __init__(self, failures: Sequence[SupportCorpusFailure]):
        ordered = tuple(
            sorted(
                failures,
                key=lambda item: (
                    item.source_path.casefold(),
                    item.source_path,
                    item.code,
                ),
            )
        )
        if not ordered:
            raise ValueError("SupportCorpusBuildError requires at least one failure")
        self.failures = ordered
        extensions = _bounded_count_summary(item.extension for item in ordered)
        codes = _bounded_count_summary(item.code for item in ordered)
        super().__init__(
            f"support corpus build failed ({len(ordered)} failures; "
            f"extensions: {extensions}; codes: {codes})"
        )


@dataclass(frozen=True, slots=True)
class SupportCorpusOutput:
    """One unique, independently revalidated exact-copy object."""

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
class SupportCorpusEntry:
    """One selected source proven safe for an exact-copy object."""

    source_path: str
    source_archive: str
    source_extension: str
    source_bytes: int
    source_sha256: str
    output_path: str
    output_bytes: int
    output_sha256: str
    native_family: str
    evidence: Mapping[str, Any]

    @property
    def disposition(self) -> str:
        return "exact-copy"

    def neutral(self) -> dict[str, Any]:
        return {
            "sourcePath": self.source_path,
            "sourceArchive": self.source_archive,
            "sourceExtension": self.source_extension,
            "sourceBytes": self.source_bytes,
            "sourceSha256": self.source_sha256,
            "disposition": "exact-copy",
            "outputPath": self.output_path,
            "outputBytes": self.output_bytes,
            "outputSha256": self.output_sha256,
            "nativeFamily": self.native_family,
            "evidence": _json_clone(self.evidence),
        }


@dataclass(frozen=True, slots=True)
class SupportCorpusHandoff:
    """One selected source deliberately left without an output object."""

    source_path: str
    source_archive: str
    source_extension: str
    source_bytes: int
    source_sha256: str
    reason: str
    evidence: Mapping[str, Any]

    @property
    def disposition(self) -> str:
        return "handoff"

    @property
    def status(self) -> str:
        return "runtime-converter-required"

    def neutral(self) -> dict[str, Any]:
        return {
            "sourcePath": self.source_path,
            "sourceArchive": self.source_archive,
            "sourceExtension": self.source_extension,
            "sourceBytes": self.source_bytes,
            "sourceSha256": self.source_sha256,
            "disposition": "handoff",
            "status": "runtime-converter-required",
            "reason": self.reason,
            "evidence": _json_clone(self.evidence),
        }


@dataclass(frozen=True, slots=True)
class SupportCorpusReport:
    """Local roots plus the canonical private support-corpus evidence."""

    source_root: Path
    output_root: Path
    manifest_path: Path
    source_manifest_sha256: str
    source_manifest_aggregate_sha256: str
    request_sha256: str
    identity_sha256: str
    manifest_sha256: str
    validators: Mapping[str, Any]
    entries: tuple[SupportCorpusEntry, ...]
    handoffs: tuple[SupportCorpusHandoff, ...]
    outputs: tuple[SupportCorpusOutput, ...]
    reused: bool

    @property
    def complete(self) -> bool:
        return True

    @property
    def candidate_file_count(self) -> int:
        return len(self.entries) + len(self.handoffs)

    @property
    def candidate_bytes(self) -> int:
        return sum(item.source_bytes for item in self.entries) + sum(
            item.source_bytes for item in self.handoffs
        )

    @property
    def converted_file_count(self) -> int:
        return len(self.entries)

    @property
    def converted_bytes(self) -> int:
        return sum(item.source_bytes for item in self.entries)

    @property
    def handoff_file_count(self) -> int:
        return len(self.handoffs)

    @property
    def handoff_bytes(self) -> int:
        return sum(item.source_bytes for item in self.handoffs)

    @property
    def output_bytes(self) -> int:
        return sum(item.byte_length for item in self.outputs)

    def neutral(self) -> dict[str, Any]:
        selection = {
            "extensions": list(SUPPORT_CORPUS_EXTENSIONS),
            "sourceManifestSha256": self.source_manifest_sha256,
            "sourceManifestAggregateSha256": self.source_manifest_aggregate_sha256,
            "requestSha256": self.request_sha256,
            "validators": _json_clone(self.validators),
        }
        return {
            "schema": SUPPORT_CORPUS_SCHEMA,
            "schemaVersion": SUPPORT_CORPUS_SCHEMA_VERSION,
            "selection": selection,
            "totals": _totals(self.entries, self.handoffs, self.outputs),
            "entries": [item.neutral() for item in self.entries],
            "handoffs": [item.neutral() for item in self.handoffs],
            "outputs": [item.neutral() for item in self.outputs],
            "identitySha256": self.identity_sha256,
        }

    json_ready = neutral


@dataclass(frozen=True, slots=True)
class _SelectedFile:
    source: _native._ManifestFile
    extension: str


@dataclass(frozen=True, slots=True)
class _Inspection:
    selected: _SelectedFile
    disposition: str
    native_family: str | None
    object_suffix: str | None
    reason: str | None
    evidence: Mapping[str, Any]
    staged_path: Path | None


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


def _summary_token(value: object) -> str:
    raw = value if isinstance(value, str) else "unknown"
    token = "".join(
        character
        for character in raw.casefold()
        if character.isascii() and (character.isalnum() or character in "-_")
    )
    return (token or "unknown")[:40]


def _bounded_count_summary(values: Iterable[object]) -> str:
    counts = Counter(_summary_token(value) for value in values)
    ordered = sorted(counts.items())
    shown = ordered[:8]
    result = ", ".join(f"{name}={count}" for name, count in shown)
    omitted = sum(count for _, count in ordered[8:])
    if omitted:
        result += f", other={omitted}"
    return result or "none"


def _translate_native_error(exc: Exception) -> SupportCorpusError:
    message = str(exc).replace("native corpus", "support corpus")
    if isinstance(exc, _native.NativeCorpusLimitError):
        return SupportCorpusLimitError(message)
    return SupportCorpusError(message)


def _resolve_source_root(value: Path | str) -> Path:
    try:
        return _native._resolve_source_root(value)
    except _native.NativeCorpusError as exc:
        raise _translate_native_error(exc) from exc


def _resolve_output_root(value: Path | str, source_root: Path) -> Path:
    try:
        return _native._resolve_output_root(value, source_root)
    except _native.NativeCorpusError as exc:
        raise _translate_native_error(exc) from exc


def _load_input(root: Path) -> _native._ValidatedInput:
    try:
        manifest = _native._load_effective_manifest(root)
        _native._validate_effective_tree(root, manifest)
        return manifest
    except _native.NativeCorpusError as exc:
        raise _translate_native_error(exc) from exc


def _revalidate_input(
    root: Path,
    expected: _native._ValidatedInput,
    *,
    selected: tuple[_SelectedFile, ...] | None = None,
) -> None:
    actual = _load_input(root)
    if actual != expected:
        raise SupportCorpusError(
            "effective-assets manifest identity changed during support inspection"
        )
    if selected is not None:
        _revalidate_selected_payloads(root, expected, selected)


def _revalidate_selected_payloads(
    root: Path,
    manifest: _native._ValidatedInput,
    selected: tuple[_SelectedFile, ...],
) -> None:
    try:
        actual_files = _native._validate_effective_tree(root, manifest)
    except _native.NativeCorpusError as exc:
        raise _translate_native_error(exc) from exc
    failures: list[SupportCorpusFailure] = []
    for item in selected:
        source = item.source
        actual = actual_files[source.path.casefold()]
        digest = hashlib.sha256()
        copied = 0
        try:
            before = actual.path.stat()
            with actual.path.open("rb") as stream:
                while block := stream.read(HASH_BLOCK_BYTES):
                    copied += len(block)
                    digest.update(block)
            after = actual.path.stat()
        except OSError:
            failures.append(
                SupportCorpusFailure(
                    source.path,
                    item.extension,
                    "source-reread-failed",
                    "source could not be revalidated after inspection",
                )
            )
            continue
        if (
            copied != source.size
            or copied != before.st_size
            or after.st_size != before.st_size
            or after.st_mtime_ns != before.st_mtime_ns
        ):
            failures.append(
                SupportCorpusFailure(
                    source.path,
                    item.extension,
                    "source-changed",
                    "source identity changed after inspection",
                )
            )
        elif digest.hexdigest() != source.sha256:
            failures.append(
                SupportCorpusFailure(
                    source.path,
                    item.extension,
                    "source-sha256-mismatch",
                    "source no longer matches the effective-assets manifest",
                )
            )
    if failures:
        raise SupportCorpusBuildError(failures)


def _selected_limit(value: int | None, maximum: int, *, label: str) -> int:
    selected = maximum if value is None else value
    if isinstance(selected, bool) or not isinstance(selected, int):
        raise TypeError(f"support corpus {label} limit must be an integer")
    if not 1 <= selected <= maximum:
        raise ValueError(f"support corpus {label} limit must be 1..{maximum}")
    return selected


def _select_files(
    manifest: _native._ValidatedInput,
    *,
    max_files: int,
    max_total_bytes: int,
) -> tuple[_SelectedFile, ...]:
    allowed = set(SUPPORT_CORPUS_EXTENSIONS)
    selected = tuple(
        _SelectedFile(item, PurePosixPath(item.path).suffix.casefold())
        for item in manifest.files
        if PurePosixPath(item.path).suffix.casefold() in allowed
    )
    if not selected:
        raise SupportCorpusError(
            "effective-assets manifest declares no whitelisted support assets"
        )
    if len(selected) > max_files:
        raise SupportCorpusLimitError(
            f"support corpus selected file count exceeds {max_files}"
        )
    byte_length = sum(item.source.size for item in selected)
    if byte_length > max_total_bytes:
        raise SupportCorpusLimitError(
            f"support corpus selected byte total exceeds {max_total_bytes}"
        )
    return selected


def _font_dependency() -> tuple[Any, Any, str]:
    try:
        import fontTools
        from fontTools.ttLib import TTFont
        from fontTools.ttLib.sfnt import calcChecksum
    except ImportError as exc:
        raise SupportCorpusDependencyError(
            "fontTools is required for authoritative TTF/OTF validation"
        ) from exc
    version = getattr(fontTools, "__version__", None)
    if version != FONTTOOLS_VERSION:
        raise SupportCorpusDependencyError(
            f"fontTools {FONTTOOLS_VERSION} is required for deterministic validation; "
            f"found {version!r}"
        )
    return TTFont, calcChecksum, version


def _xml_dependency() -> tuple[Any, str]:
    try:
        import defusedxml
        from defusedxml import ElementTree
    except ImportError as exc:
        raise SupportCorpusDependencyError(
            "defusedxml is required for external-resolution-free XML validation"
        ) from exc
    version = getattr(defusedxml, "__version__", None)
    if version != DEFUSEDXML_VERSION:
        raise SupportCorpusDependencyError(
            f"defusedxml {DEFUSEDXML_VERSION} is required for deterministic validation; "
            f"found {version!r}"
        )
    return ElementTree, version


def _validators_for(selected: tuple[_SelectedFile, ...]) -> tuple[dict[str, Any], Any, Any]:
    validators: dict[str, Any] = {
        "signature": {
            "name": "bounded-prefix-signature",
            "version": 1,
            "windowBytes": SIGNATURE_WINDOW_BYTES,
        },
        "text": {
            "name": "strict-utf8-text",
            "version": 1,
            "maximumBytes": MAX_TEXT_SOURCE_BYTES,
        },
    }
    xml_module: Any | None = None
    font_tools: tuple[Any, Any] | None = None
    if any(item.extension in XML_EXTENSIONS for item in selected):
        xml_module, version = _xml_dependency()
        validators["xml"] = {
            "name": "defusedxml-schema",
            "version": version,
            "forbidDtd": True,
            "forbidEntities": True,
            "forbidExternal": True,
            "maximumNodes": MAX_XML_NODES,
            "maximumDepth": MAX_XML_DEPTH,
        }
    if any(item.extension in FONT_EXTENSIONS for item in selected):
        tt_font, checksum, version = _font_dependency()
        font_tools = (tt_font, checksum)
        validators["font"] = {
            "name": "fonttools-sfnt",
            "version": version,
            "maximumBytes": MAX_FONT_SOURCE_BYTES,
            "maximumTables": MAX_FONT_TABLES,
            "checksumsRequired": True,
        }
    return _json_clone(validators), xml_module, font_tools


def _copy_verified_source(
    actual: _native._TreeFile,
    expected: _native._ManifestFile,
    target: Path,
) -> SupportCorpusFailure | None:
    failure = _native._copy_verified_source(actual, expected, target)
    if failure is None:
        return None
    return SupportCorpusFailure(
        expected.path,
        PurePosixPath(expected.path).suffix.casefold(),
        failure.code,
        failure.detail,
    )


def _signature_kind(prefix: bytes) -> str:
    if not prefix:
        return "empty"
    if prefix.startswith(bytes.fromhex("d0cf11e0a1b11ae1")):
        return "ole-compound-file"
    if prefix.startswith(b"PK\x03\x04"):
        return "zip-container"
    if prefix.startswith(b"DXBC"):
        return "directx-bytecode-container"
    if prefix.startswith(b"ttcf"):
        return "sfnt-collection"
    if prefix.startswith((b"OTTO", b"true", b"typ1", b"\x00\x01\x00\x00")):
        return "sfnt-font"
    if prefix.startswith(b"MZ"):
        return "portable-executable"
    if prefix.startswith(b"\x7fELF"):
        return "elf-object"
    if len(prefix) >= 4 and all(32 <= value <= 126 for value in prefix[:4]):
        return "printable-fourcc"
    return "opaque-binary"


def _signature_evidence(path: Path, byte_length: int, digest: str) -> dict[str, Any]:
    try:
        with path.open("rb") as stream:
            prefix = stream.read(SIGNATURE_WINDOW_BYTES)
    except OSError as exc:
        raise SupportCorpusError("staged support source cannot be inspected") from exc
    return {
        "validator": "bounded-prefix-signature-v1",
        "byteLength": byte_length,
        "sha256": digest,
        "signatureBytes": len(prefix),
        "signatureSha256": hashlib.sha256(prefix).hexdigest(),
        "signatureKind": _signature_kind(prefix),
    }


def _text_evidence(payload: bytes) -> tuple[dict[str, Any] | None, str | None]:
    if len(payload) > MAX_TEXT_SOURCE_BYTES:
        return None, "text-validation-bound"
    try:
        decoded = payload.decode("utf-8", errors="strict")
    except UnicodeDecodeError:
        return None, "invalid-utf8"
    forbidden = sum(
        1
        for character in decoded
        if ord(character) < 32 and character not in {"\t", "\n", "\r"}
    )
    if forbidden:
        return None, "forbidden-text-controls"
    return (
        {
            "valid": True,
            "family": "utf8-text",
            "validator": "strict-utf8-text-v1",
            "size": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
            "codePoints": len(decoded),
            "byteOrderMark": decoded.startswith("\ufeff"),
            "lineFeeds": decoded.count("\n"),
            "carriageReturns": decoded.count("\r"),
        },
        None,
    )


def _xml_evidence(payload: bytes, xml_module: Any) -> tuple[dict[str, Any] | None, str | None]:
    text, reason = _text_evidence(payload)
    if text is None:
        return None, reason
    try:
        root = xml_module.fromstring(
            payload,
            forbid_dtd=True,
            forbid_entities=True,
            forbid_external=True,
        )
    except Exception:
        return None, "unsafe-or-invalid-xml"
    count = 0
    attributes = 0
    maximum_depth = 0
    pending = [(root, 1)]
    while pending:
        element, depth = pending.pop()
        count += 1
        if count > MAX_XML_NODES or depth > MAX_XML_DEPTH:
            return None, "xml-structure-bound"
        maximum_depth = max(maximum_depth, depth)
        attributes += len(element.attrib)
        pending.extend((child, depth + 1) for child in list(element))
    return (
        {
            **text,
            "family": "xml-schema",
            "validator": "defusedxml-schema-v1",
            "elementCount": count,
            "attributeCount": attributes,
            "maximumDepth": maximum_depth,
            "externalResolution": False,
        },
        None,
    )


def _font_evidence(
    path: Path,
    byte_length: int,
    digest: str,
    font_tools: tuple[Any, Any],
) -> tuple[dict[str, Any] | None, str | None]:
    if byte_length > MAX_FONT_SOURCE_BYTES:
        return None, "font-validation-bound"
    tt_font, calc_checksum = font_tools
    font: Any | None = None
    try:
        font = tt_font(
            path,
            lazy=False,
            recalcBBoxes=False,
            recalcTimestamp=False,
            ignoreDecompileErrors=False,
        )
        reader = font.reader
        if reader is None or getattr(reader, "numFonts", 1) != 1:
            return None, "font-collection-requires-converter"
        if getattr(font, "flavor", None) is not None or font.sfntVersion not in {
            "\x00\x01\x00\x00",
            "OTTO",
            "true",
            "typ1",
        }:
            return None, "font-not-plain-sfnt"
        tables = getattr(reader, "tables", None)
        if not isinstance(tables, dict) or not 1 <= len(tables) <= MAX_FONT_TABLES:
            return None, "font-table-bound"
        required = {"head", "hhea", "maxp", "hmtx", "cmap", "name"}
        tags = {str(tag) for tag in tables}
        if not required.issubset(tags) or not ({"glyf", "CFF ", "CFF2"} & tags):
            return None, "font-required-tables-missing"
        for tag in sorted(tags):
            font[tag]
        glyph_count = int(font["maxp"].numGlyphs)
        units_per_em = int(font["head"].unitsPerEm)
        if not 1 <= glyph_count <= 65_535 or not 16 <= units_per_em <= 16_384:
            return None, "font-metric-bound"
        directory_records: list[dict[str, Any]] = []
        with path.open("rb") as stream:
            for tag in sorted(tags):
                record = tables[tag]
                length = int(record.length)
                offset = int(record.offset)
                checksum = int(record.checkSum)
                if length < 0 or offset < 0 or offset + length > byte_length:
                    return None, "font-table-out-of-bounds"
                stream.seek(offset)
                raw = stream.read(length)
                if len(raw) != length:
                    return None, "font-table-truncated"
                checksum_raw = raw
                if tag == "head" and len(raw) >= 12:
                    checksum_raw = raw[:8] + b"\0\0\0\0" + raw[12:]
                if int(calc_checksum(checksum_raw)) != checksum:
                    return None, "font-table-checksum-mismatch"
                directory_records.append(
                    {"tag": tag, "bytes": length, "checksum": checksum}
                )
        if int(calc_checksum(path.read_bytes())) != 0xB1B0AFBA:
            return None, "font-whole-file-checksum-mismatch"
        sfnt_version = getattr(font, "sfntVersion", "")
        flavor = "cff" if str(sfnt_version) == "OTTO" else "truetype"
        return (
            {
                "valid": True,
                "family": "sfnt-font",
                "validator": "fonttools-sfnt-v1",
                "size": byte_length,
                "sha256": digest,
                "outlineFlavor": flavor,
                "tableCount": len(tags),
                "glyphCount": glyph_count,
                "unitsPerEm": units_per_em,
                "tableDirectorySha256": _canonical_sha256(directory_records),
                "checksumsValidated": True,
                "wholeFileChecksumValidated": True,
            },
            None,
        )
    except Exception:
        return None, "font-structure-invalid"
    finally:
        if font is not None:
            try:
                font.close()
            except Exception:
                pass


def _inspect_one(
    selected: _SelectedFile,
    staged: Path,
    *,
    xml_module: Any,
    font_tools: tuple[Any, Any] | None,
) -> _Inspection | SupportCorpusFailure:
    source = selected.source
    extension = selected.extension
    signature = _signature_evidence(staged, source.size, source.sha256)
    if extension in OPAQUE_EXTENSIONS:
        staged.unlink(missing_ok=True)
        return _Inspection(
            selected,
            "handoff",
            None,
            None,
            "explicit-opaque-format",
            signature,
            None,
        )
    if extension in FONT_EXTENSIONS:
        if font_tools is None:
            raise SupportCorpusDependencyError(
                "fontTools is required for authoritative TTF/OTF validation"
            )
        evidence, reason = _font_evidence(
            staged, source.size, source.sha256, font_tools
        )
        if evidence is None:
            staged.unlink(missing_ok=True)
            return SupportCorpusFailure(
                source.path,
                extension,
                reason or "font-validation-failed",
                "font payload failed bounded SFNT validation",
            )
        return _Inspection(
            selected,
            "exact-copy",
            "sfnt-font",
            ".font",
            None,
            evidence,
            staged,
        )
    if source.size > MAX_TEXT_SOURCE_BYTES:
        staged.unlink(missing_ok=True)
        return _Inspection(
            selected,
            "handoff",
            None,
            None,
            "text-validation-bound",
            {**signature, "textDisposition": "text-validation-bound"},
            None,
        )
    try:
        payload = staged.read_bytes()
    except OSError:
        staged.unlink(missing_ok=True)
        return SupportCorpusFailure(
            source.path,
            extension,
            "staged-read-failed",
            "verified staged source could not be read",
        )
    if extension in XML_EXTENSIONS:
        if xml_module is None:
            raise SupportCorpusDependencyError(
                "defusedxml is required for safe XML validation"
            )
        evidence, reason = _xml_evidence(payload, xml_module)
        family, suffix = "xml-schema", ".xml"
    else:
        evidence, reason = _text_evidence(payload)
        family, suffix = "utf8-text", ".txt"
    if evidence is None:
        staged.unlink(missing_ok=True)
        return _Inspection(
            selected,
            "handoff",
            None,
            None,
            reason or "unproven-text-payload",
            {**signature, "textDisposition": reason or "unproven"},
            None,
        )
    return _Inspection(
        selected,
        "exact-copy",
        family,
        suffix,
        None,
        evidence,
        staged,
    )


def _inspect_sources(
    source_root: Path,
    manifest: _native._ValidatedInput,
    selected: tuple[_SelectedFile, ...],
    work: Path,
    *,
    xml_module: Any,
    font_tools: tuple[Any, Any] | None,
) -> tuple[_Inspection, ...]:
    try:
        actual_files = _native._validate_effective_tree(source_root, manifest)
    except _native.NativeCorpusError as exc:
        raise _translate_native_error(exc) from exc
    inspections: list[_Inspection] = []
    failures: list[SupportCorpusFailure] = []
    for index, item in enumerate(selected):
        staged = work / f"source-{index:08d}{item.extension}"
        failure = _copy_verified_source(
            actual_files[item.source.path.casefold()], item.source, staged
        )
        if failure is not None:
            failures.append(failure)
            continue
        inspected = _inspect_one(
            item,
            staged,
            xml_module=xml_module,
            font_tools=font_tools,
        )
        if isinstance(inspected, SupportCorpusFailure):
            failures.append(inspected)
        else:
            inspections.append(inspected)
    if failures:
        raise SupportCorpusBuildError(failures)
    if len(inspections) != len(selected):
        raise SupportCorpusError("support source disposition reconciliation failed")
    return tuple(inspections)


def _request_sources(inspections: Sequence[_Inspection]) -> list[dict[str, Any]]:
    records: list[dict[str, Any]] = []
    for item in inspections:
        source = item.selected.source
        record: dict[str, Any] = {
            "path": source.path,
            "bytes": source.size,
            "sha256": source.sha256,
            "extension": item.selected.extension,
            "disposition": item.disposition,
            "evidence": _json_clone(item.evidence),
        }
        if item.disposition == "exact-copy":
            record["nativeFamily"] = item.native_family
        else:
            record.update(
                {
                    "status": "runtime-converter-required",
                    "reason": item.reason,
                }
            )
        records.append(record)
    return records


def _request_sha256(
    manifest: _native._ValidatedInput,
    validators: Mapping[str, Any],
    inspections: Sequence[_Inspection],
) -> str:
    return _canonical_sha256(
        {
            "schema": "openbfme.support-corpus-request",
            "schemaVersion": 0,
            "extensions": list(SUPPORT_CORPUS_EXTENSIONS),
            "sourceManifestSha256": manifest.manifest_sha256,
            "sourceManifestAggregateSha256": manifest.aggregate_sha256,
            "validators": _json_clone(validators),
            "sources": _request_sources(inspections),
        }
    )


def _object_relative(digest: str, suffix: str) -> str:
    return f"objects/sha256/{digest[:2]}/{digest}{suffix}"


def _safe_child(root: Path, relative: str, *, label: str) -> Path:
    try:
        return _native._safe_child(root, relative, label=label)
    except _native.NativeCorpusError as exc:
        raise _translate_native_error(exc) from exc


def _make_output(
    stage: Path,
    inspection: _Inspection,
    outputs: dict[str, SupportCorpusOutput],
) -> SupportCorpusOutput:
    if (
        inspection.staged_path is None
        or inspection.native_family is None
        or inspection.object_suffix is None
    ):
        raise SupportCorpusError("exact-copy inspection lacks an output candidate")
    digest = inspection.selected.source.sha256
    byte_length = inspection.selected.source.size
    relative = _object_relative(digest, inspection.object_suffix)
    existing = outputs.get(relative)
    if existing is not None:
        if (
            existing.sha256 != digest
            or existing.byte_length != byte_length
            or existing.native_family != inspection.native_family
            or existing.evidence != inspection.evidence
        ):
            raise SupportCorpusError(
                "content-address collision produced inconsistent support evidence"
            )
        inspection.staged_path.unlink(missing_ok=True)
        return existing
    target = _safe_child(stage, relative, label="support corpus object")
    target.parent.mkdir(parents=True, exist_ok=True)
    if os.path.lexists(target):
        raise SupportCorpusError(
            "content-addressed support output exists unexpectedly"
        )
    try:
        shutil.copyfile(inspection.staged_path, target)
    except OSError as exc:
        target.unlink(missing_ok=True)
        raise SupportCorpusError("support output could not be staged") from exc
    inspection.staged_path.unlink(missing_ok=True)
    if target.stat().st_size != byte_length or _file_sha256(target) != digest:
        target.unlink(missing_ok=True)
        raise SupportCorpusError("support exact-copy output identity changed")
    output = SupportCorpusOutput(
        relative,
        byte_length,
        digest,
        inspection.native_family,
        _json_clone(inspection.evidence),
    )
    outputs[relative] = output
    return output


def _file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(HASH_BLOCK_BYTES):
            digest.update(block)
    return digest.hexdigest()


def _totals(
    entries: Sequence[SupportCorpusEntry],
    handoffs: Sequence[SupportCorpusHandoff],
    outputs: Sequence[SupportCorpusOutput],
) -> dict[str, int]:
    converted_bytes = sum(item.source_bytes for item in entries)
    handoff_bytes = sum(item.source_bytes for item in handoffs)
    return {
        "candidateFileCount": len(entries) + len(handoffs),
        "candidateBytes": converted_bytes + handoff_bytes,
        "convertedFileCount": len(entries),
        "convertedBytes": converted_bytes,
        "handoffFileCount": len(handoffs),
        "handoffBytes": handoff_bytes,
        "outputFileCount": len(outputs),
        "outputBytes": sum(item.byte_length for item in outputs),
    }


def _document(
    manifest: _native._ValidatedInput,
    validators: Mapping[str, Any],
    request_sha256: str,
    entries: tuple[SupportCorpusEntry, ...],
    handoffs: tuple[SupportCorpusHandoff, ...],
    outputs: tuple[SupportCorpusOutput, ...],
) -> dict[str, Any]:
    basis: dict[str, Any] = {
        "schema": SUPPORT_CORPUS_SCHEMA,
        "schemaVersion": SUPPORT_CORPUS_SCHEMA_VERSION,
        "selection": {
            "extensions": list(SUPPORT_CORPUS_EXTENSIONS),
            "sourceManifestSha256": manifest.manifest_sha256,
            "sourceManifestAggregateSha256": manifest.aggregate_sha256,
            "requestSha256": request_sha256,
            "validators": _json_clone(validators),
        },
        "totals": _totals(entries, handoffs, outputs),
        "entries": [item.neutral() for item in entries],
        "handoffs": [item.neutral() for item in handoffs],
        "outputs": [item.neutral() for item in outputs],
    }
    return {**basis, "identitySha256": _canonical_sha256(basis)}


def _validate_path(value: object, *, label: str) -> str:
    if not isinstance(value, str):
        raise SupportCorpusError(f"{label} is not a string")
    try:
        if "/".join(safe_relative_parts(value)) != value:
            raise ValueError
    except (TypeError, ValueError) as exc:
        raise SupportCorpusError(f"{label} is unsafe or non-canonical") from exc
    return value


def _validate_output_evidence(
    path: Path,
    family: str,
    expected: object,
    *,
    xml_module: Any,
    font_tools: tuple[Any, Any] | None,
) -> dict[str, Any]:
    byte_length = path.stat().st_size
    digest = _file_sha256(path)
    if family == "utf8-text":
        evidence, reason = _text_evidence(path.read_bytes())
    elif family == "xml-schema":
        evidence, reason = _xml_evidence(path.read_bytes(), xml_module)
    elif family == "sfnt-font":
        if font_tools is None:
            raise SupportCorpusDependencyError(
                "fontTools is required to backtest support font objects"
            )
        evidence, reason = _font_evidence(path, byte_length, digest, font_tools)
    else:
        raise SupportCorpusError("support output family is unsupported")
    if evidence is None:
        raise SupportCorpusError(
            f"support object failed {family} backtest ({reason or 'invalid'})"
        )
    if evidence != expected:
        raise SupportCorpusError("support output evidence is not canonical")
    return _json_clone(evidence)


def _parse_output(
    raw: object,
    root: Path,
    *,
    xml_module: Any,
    font_tools: tuple[Any, Any] | None,
) -> SupportCorpusOutput:
    if not isinstance(raw, dict) or set(raw) != {
        "path",
        "bytes",
        "sha256",
        "nativeFamily",
        "evidence",
    }:
        raise SupportCorpusError("support corpus output entry has an invalid shape")
    relative = _validate_path(raw.get("path"), label="support output path")
    byte_length = raw.get("bytes")
    digest = raw.get("sha256")
    family = raw.get("nativeFamily")
    if (
        not _is_int(byte_length)
        or not _is_sha256(digest)
        or family not in _OUTPUT_FAMILIES
    ):
        raise SupportCorpusError("support corpus output metadata is invalid")
    suffix = {"utf8-text": ".txt", "xml-schema": ".xml", "sfnt-font": ".font"}[
        family
    ]
    if relative != _object_relative(digest, suffix):
        raise SupportCorpusError("support output path is not content-address canonical")
    path = _safe_child(root, relative, label="support output")
    if _native._is_link_like(path) or not path.is_file():
        raise SupportCorpusError("support output is missing, linked, or not a file")
    if path.stat().st_size != byte_length or _file_sha256(path) != digest:
        raise SupportCorpusError("support output identity is invalid")
    evidence = _validate_output_evidence(
        path,
        family,
        raw.get("evidence"),
        xml_module=xml_module,
        font_tools=font_tools,
    )
    return SupportCorpusOutput(relative, byte_length, digest, family, evidence)


def _parse_entry(
    raw: object, outputs: Mapping[str, SupportCorpusOutput]
) -> SupportCorpusEntry:
    expected = {
        "sourcePath",
        "sourceArchive",
        "sourceExtension",
        "sourceBytes",
        "sourceSha256",
        "disposition",
        "outputPath",
        "outputBytes",
        "outputSha256",
        "nativeFamily",
        "evidence",
    }
    if not isinstance(raw, dict) or set(raw) != expected:
        raise SupportCorpusError("support source entry has an invalid shape")
    source_path = _validate_path(raw.get("sourcePath"), label="support source path")
    source_archive = _validate_path(
        raw.get("sourceArchive"), label="support source archive"
    )
    extension = raw.get("sourceExtension")
    source_bytes = raw.get("sourceBytes")
    source_sha256 = raw.get("sourceSha256")
    output_path = raw.get("outputPath")
    output_bytes = raw.get("outputBytes")
    output_sha256 = raw.get("outputSha256")
    family = raw.get("nativeFamily")
    if (
        extension not in SUPPORT_CORPUS_EXTENSIONS
        or not _is_int(source_bytes)
        or not _is_sha256(source_sha256)
        or raw.get("disposition") != "exact-copy"
        or not isinstance(output_path, str)
        or not _is_int(output_bytes)
        or not _is_sha256(output_sha256)
        or family not in _OUTPUT_FAMILIES
        or PurePosixPath(source_path).suffix.casefold() != extension
    ):
        raise SupportCorpusError("support source entry metadata is invalid")
    output = outputs.get(output_path.casefold())
    if output is None or (
        output.path != output_path
        or output.byte_length != output_bytes
        or output.sha256 != output_sha256
        or output.native_family != family
        or output.evidence != raw.get("evidence")
        or source_bytes != output_bytes
        or source_sha256 != output_sha256
    ):
        raise SupportCorpusError("support exact-copy source/output mapping is invalid")
    expected_family = (
        "sfnt-font"
        if extension in FONT_EXTENSIONS
        else "xml-schema"
        if extension in XML_EXTENSIONS
        else "utf8-text"
    )
    if family != expected_family:
        raise SupportCorpusError("support source family is inconsistent")
    return SupportCorpusEntry(
        source_path,
        source_archive,
        extension,
        source_bytes,
        source_sha256,
        output_path,
        output_bytes,
        output_sha256,
        family,
        _json_clone(output.evidence),
    )


def _validate_signature_evidence(raw: object, *, size: int, digest: str) -> dict[str, Any]:
    if not isinstance(raw, dict):
        raise SupportCorpusError("support handoff evidence is not an object")
    base = {
        "validator",
        "byteLength",
        "sha256",
        "signatureBytes",
        "signatureSha256",
        "signatureKind",
    }
    if frozenset(raw) not in {
        frozenset(base),
        frozenset({*base, "textDisposition"}),
    }:
        raise SupportCorpusError("support handoff evidence shape is invalid")
    signature_kinds = {
        "empty",
        "ole-compound-file",
        "zip-container",
        "directx-bytecode-container",
        "sfnt-collection",
        "sfnt-font",
        "portable-executable",
        "elf-object",
        "printable-fourcc",
        "opaque-binary",
    }
    if (
        raw.get("validator") != "bounded-prefix-signature-v1"
        or raw.get("byteLength") != size
        or raw.get("sha256") != digest
        or raw.get("signatureBytes") != min(size, SIGNATURE_WINDOW_BYTES)
        or not _is_sha256(raw.get("signatureSha256"))
        or raw.get("signatureKind") not in signature_kinds
    ):
        raise SupportCorpusError("support handoff evidence metadata is invalid")
    return _json_clone(raw)


def _parse_handoff(raw: object) -> SupportCorpusHandoff:
    expected = {
        "sourcePath",
        "sourceArchive",
        "sourceExtension",
        "sourceBytes",
        "sourceSha256",
        "disposition",
        "status",
        "reason",
        "evidence",
    }
    if not isinstance(raw, dict) or set(raw) != expected:
        raise SupportCorpusError("support handoff entry has an invalid shape")
    source_path = _validate_path(raw.get("sourcePath"), label="handoff source path")
    source_archive = _validate_path(
        raw.get("sourceArchive"), label="handoff source archive"
    )
    extension = raw.get("sourceExtension")
    byte_length = raw.get("sourceBytes")
    digest = raw.get("sourceSha256")
    reason = raw.get("reason")
    if (
        extension not in SUPPORT_CORPUS_EXTENSIONS
        or not _is_int(byte_length)
        or not _is_sha256(digest)
        or raw.get("disposition") != "handoff"
        or raw.get("status") != "runtime-converter-required"
        or not isinstance(reason, str)
        or not reason
        or PurePosixPath(source_path).suffix.casefold() != extension
    ):
        raise SupportCorpusError("support handoff metadata is invalid")
    evidence = _validate_signature_evidence(
        raw.get("evidence"), size=byte_length, digest=digest
    )
    if extension in OPAQUE_EXTENSIONS:
        if reason != "explicit-opaque-format" or "textDisposition" in evidence:
            raise SupportCorpusError(
                "explicit opaque extension handoff reason is invalid"
            )
    else:
        allowed_reasons = {
            "invalid-utf8",
            "forbidden-text-controls",
            "text-validation-bound",
            "unsafe-or-invalid-xml",
            "xml-structure-bound",
        }
        if reason not in allowed_reasons or evidence.get("textDisposition") != reason:
            raise SupportCorpusError("unproven text handoff evidence is inconsistent")
    if extension in FONT_EXTENSIONS:
        raise SupportCorpusError("invalid fonts cannot be relabeled as handoffs")
    return SupportCorpusHandoff(
        source_path,
        source_archive,
        extension,
        byte_length,
        digest,
        reason,
        evidence,
    )


def _inspection_from_records(
    entries: Sequence[SupportCorpusEntry], handoffs: Sequence[SupportCorpusHandoff]
) -> tuple[_Inspection, ...]:
    result: list[_Inspection] = []
    for item in entries:
        source = _native._ManifestFile(
            item.source_path,
            item.source_archive,
            item.source_bytes,
            item.source_sha256,
        )
        suffix = {
            "utf8-text": ".txt",
            "xml-schema": ".xml",
            "sfnt-font": ".font",
        }[item.native_family]
        result.append(
            _Inspection(
                _SelectedFile(source, item.source_extension),
                "exact-copy",
                item.native_family,
                suffix,
                None,
                item.evidence,
                None,
            )
        )
    for item in handoffs:
        source = _native._ManifestFile(
            item.source_path,
            item.source_archive,
            item.source_bytes,
            item.source_sha256,
        )
        result.append(
            _Inspection(
                _SelectedFile(source, item.source_extension),
                "handoff",
                None,
                None,
                item.reason,
                item.evidence,
                None,
            )
        )
    return tuple(
        sorted(
            result,
            key=lambda item: (
                item.selected.source.path.casefold(),
                item.selected.source.path,
            ),
        )
    )


def _read_manifest(path: Path) -> tuple[dict[str, Any], bytes]:
    try:
        return _native._read_strict_json(
            path, _native.MAX_MANIFEST_BYTES, label="support corpus manifest"
        )
    except _native.NativeCorpusError as exc:
        raise _translate_native_error(exc) from exc


def _scan_tree(root: Path, *, label: str) -> tuple[dict[str, Any], dict[str, str]]:
    try:
        return _native._scan_tree(root, label=label)
    except _native.NativeCorpusError as exc:
        raise _translate_native_error(exc) from exc


def _verify_output(
    root: Path,
    source_root: Path,
    *,
    reused: bool,
) -> SupportCorpusReport:
    if _native._is_link_like(root) or not root.is_dir():
        raise SupportCorpusError(
            "support corpus output is missing, linked, or not a directory"
        )
    manifest_path = root / SUPPORT_CORPUS_MANIFEST
    document, raw_bytes = _read_manifest(manifest_path)
    if raw_bytes != _canonical_json_bytes(document, pretty=True):
        raise SupportCorpusError("support corpus manifest encoding is not canonical")
    if set(document) != {
        "schema",
        "schemaVersion",
        "selection",
        "totals",
        "entries",
        "handoffs",
        "outputs",
        "identitySha256",
    }:
        raise SupportCorpusError("support corpus manifest shape is invalid")
    if (
        document.get("schema") != SUPPORT_CORPUS_SCHEMA
        or document.get("schemaVersion") != SUPPORT_CORPUS_SCHEMA_VERSION
        or isinstance(document.get("schemaVersion"), bool)
    ):
        raise SupportCorpusError("support corpus manifest schema is unsupported")
    selection = document.get("selection")
    if not isinstance(selection, dict) or set(selection) != {
        "extensions",
        "sourceManifestSha256",
        "sourceManifestAggregateSha256",
        "requestSha256",
        "validators",
    }:
        raise SupportCorpusError("support corpus selection is invalid")
    if selection.get("extensions") != list(SUPPORT_CORPUS_EXTENSIONS):
        raise SupportCorpusError("support corpus extension whitelist is invalid")
    source_manifest_sha256 = selection.get("sourceManifestSha256")
    source_aggregate = selection.get("sourceManifestAggregateSha256")
    request_sha256 = selection.get("requestSha256")
    if not all(
        _is_sha256(item)
        for item in (source_manifest_sha256, source_aggregate, request_sha256)
    ):
        raise SupportCorpusError("support corpus selection hashes are invalid")
    raw_entries = document.get("entries")
    raw_handoffs = document.get("handoffs")
    raw_outputs = document.get("outputs")
    if not all(isinstance(item, list) for item in (raw_entries, raw_handoffs, raw_outputs)):
        raise SupportCorpusError("support corpus inventories are invalid")
    extension_hints = [
        item.get("sourceExtension")
        for item in [*raw_entries, *raw_handoffs]
        if isinstance(item, dict)
    ]
    synthetic_selected = tuple(
        _SelectedFile(_native._ManifestFile(f"x{index}{extension}", "x.big", 0, "0" * 64), extension)
        for index, extension in enumerate(extension_hints)
        if extension in SUPPORT_CORPUS_EXTENSIONS
    )
    expected_validators, xml_module, font_tools = _validators_for(synthetic_selected)
    validators = selection.get("validators")
    if validators != expected_validators:
        raise SupportCorpusError("support corpus validator evidence is not current")
    outputs = tuple(
        _parse_output(
            item,
            root,
            xml_module=xml_module,
            font_tools=font_tools,
        )
        for item in raw_outputs
    )
    output_paths = [item.path for item in outputs]
    if output_paths != sorted(output_paths, key=lambda value: value.casefold()) or len(
        {item.casefold() for item in output_paths}
    ) != len(output_paths):
        raise SupportCorpusError("support output inventory is not canonical")
    output_lookup = {item.path.casefold(): item for item in outputs}
    entries = tuple(_parse_entry(item, output_lookup) for item in raw_entries)
    handoffs = tuple(_parse_handoff(item) for item in raw_handoffs)
    entry_paths = [item.source_path for item in entries]
    handoff_paths = [item.source_path for item in handoffs]
    for label, paths in (("entry", entry_paths), ("handoff", handoff_paths)):
        if paths != sorted(paths, key=lambda value: value.casefold()) or len(
            {item.casefold() for item in paths}
        ) != len(paths):
            raise SupportCorpusError(f"support {label} inventory is not canonical")
    if {item.casefold() for item in entry_paths} & {
        item.casefold() for item in handoff_paths
    }:
        raise SupportCorpusError("support source has more than one disposition")
    referenced = {item.output_path.casefold() for item in entries}
    if referenced != set(output_lookup):
        raise SupportCorpusError("support corpus contains an unreferenced output")
    inspections = _inspection_from_records(entries, handoffs)
    fake_manifest = _native._ValidatedInput(
        Path("manifest.json"),
        source_manifest_sha256,
        source_aggregate,
        (),
        0,
    )
    if request_sha256 != _request_sha256(fake_manifest, validators, inspections):
        raise SupportCorpusError("support corpus request SHA-256 is invalid")
    if document.get("totals") != _totals(entries, handoffs, outputs):
        raise SupportCorpusError("support corpus totals are invalid")
    basis = {key: value for key, value in document.items() if key != "identitySha256"}
    identity = document.get("identitySha256")
    if not _is_sha256(identity) or identity != _canonical_sha256(basis):
        raise SupportCorpusError("support corpus identity SHA-256 is invalid")
    actual_files, actual_directories = _scan_tree(root, label="support corpus tree")
    declared = [SUPPORT_CORPUS_MANIFEST, *output_paths]
    expected_files = {item.casefold(): item for item in declared}
    expected_directories = _native._expected_directories(declared)
    if set(actual_files) != set(expected_files):
        raise SupportCorpusError("support corpus tree has missing or undeclared files")
    if set(actual_directories) != set(expected_directories):
        raise SupportCorpusError(
            "support corpus tree has missing or undeclared directories"
        )
    return SupportCorpusReport(
        source_root,
        root,
        manifest_path,
        source_manifest_sha256,
        source_aggregate,
        request_sha256,
        identity,
        hashlib.sha256(raw_bytes).hexdigest(),
        _json_clone(validators),
        entries,
        handoffs,
        outputs,
        reused,
    )


def _remove_owned_tree(path: Path, parent: Path, prefix: str) -> None:
    if not os.path.lexists(path):
        return
    if path.parent != parent or not path.name.startswith(prefix) or _native._is_link_like(path):
        raise SupportCorpusError("refused to remove an unowned support transaction path")
    _scan_tree(path, label="support transaction tree")
    shutil.rmtree(path)


def _publish(stage: Path, destination: Path, backup: Path) -> None:
    had_destination = os.path.lexists(destination)
    if had_destination:
        _scan_tree(destination, label="existing support corpus tree")
        os.replace(destination, backup)
    try:
        os.replace(stage, destination)
    except Exception as publish_error:
        if had_destination and os.path.lexists(backup):
            try:
                os.replace(backup, destination)
            except Exception as rollback_error:
                raise SupportCorpusError(
                    "support corpus publish failed and rollback could not restore prior output"
                ) from rollback_error
        raise SupportCorpusError(
            "support corpus publish failed; prior output was preserved"
        ) from publish_error
    if had_destination:
        _remove_owned_tree(
            backup, destination.parent, f".{destination.name}.backup-"
        )


def build_support_corpus(
    effective_assets_root: Path | str,
    output_root: Path | str,
    *,
    max_files: int | None = None,
    max_total_bytes: int | None = None,
    force: bool = False,
) -> SupportCorpusReport:
    """Build or reuse the verified residual support-asset corpus.

    All seventeen whitelisted extensions are selected.  Valid UTF-8/XML and
    SFNT files become exact-copy objects; the seven explicitly opaque formats
    and unproven text payloads become no-output handoffs.  Invalid fonts are a
    build failure because no weaker font claim is permitted.
    """

    if not isinstance(force, bool):
        raise TypeError("support corpus force flag must be a boolean")
    selected_max_files = _selected_limit(
        max_files, MAX_SUPPORT_CORPUS_FILES, label="file count"
    )
    selected_max_bytes = _selected_limit(
        max_total_bytes, MAX_SUPPORT_CORPUS_BYTES, label="total byte"
    )
    source_root = _resolve_source_root(effective_assets_root)
    manifest = _load_input(source_root)
    selected = _select_files(
        manifest,
        max_files=selected_max_files,
        max_total_bytes=selected_max_bytes,
    )
    validators, xml_module, font_tools = _validators_for(selected)
    destination = _resolve_output_root(output_root, source_root)
    parent = destination.parent
    token = uuid.uuid4().hex
    work = parent / f".{destination.name}.inspection-{token}"
    stage = parent / f".{destination.name}.staging-{token}"
    backup = parent / f".{destination.name}.backup-{token}"
    try:
        work.mkdir()
    except OSError as exc:
        raise SupportCorpusError(
            "support corpus inspection directory could not be created"
        ) from exc
    try:
        inspections = _inspect_sources(
            source_root,
            manifest,
            selected,
            work,
            xml_module=xml_module,
            font_tools=font_tools,
        )
        request_sha256 = _request_sha256(manifest, validators, inspections)
        _revalidate_input(source_root, manifest, selected=selected)
        if destination.is_dir() and not force:
            try:
                report = _verify_output(destination, source_root, reused=True)
            except SupportCorpusError as exc:
                raise SupportCorpusReuseError(
                    f"existing support corpus failed verification: {exc}"
                ) from exc
            if report.request_sha256 != request_sha256:
                raise SupportCorpusReuseError(
                    "existing support corpus does not match the requested verified sources; use force=True"
                )
            return report

        try:
            stage.mkdir()
        except OSError as exc:
            raise SupportCorpusError(
                "support corpus staging directory could not be created"
            ) from exc
        outputs_by_path: dict[str, SupportCorpusOutput] = {}
        entries: list[SupportCorpusEntry] = []
        handoffs: list[SupportCorpusHandoff] = []
        for inspection in inspections:
            source = inspection.selected.source
            if inspection.disposition == "handoff":
                handoffs.append(
                    SupportCorpusHandoff(
                        source.path,
                        source.archive,
                        inspection.selected.extension,
                        source.size,
                        source.sha256,
                        inspection.reason or "runtime-converter-required",
                        _json_clone(inspection.evidence),
                    )
                )
                continue
            output = _make_output(stage, inspection, outputs_by_path)
            entries.append(
                SupportCorpusEntry(
                    source.path,
                    source.archive,
                    inspection.selected.extension,
                    source.size,
                    source.sha256,
                    output.path,
                    output.byte_length,
                    output.sha256,
                    output.native_family,
                    _json_clone(output.evidence),
                )
            )
        outputs = tuple(
            outputs_by_path[path]
            for path in sorted(outputs_by_path, key=lambda value: value.casefold())
        )
        entries_tuple = tuple(entries)
        handoffs_tuple = tuple(handoffs)
        if len(entries_tuple) + len(handoffs_tuple) != len(selected):
            raise SupportCorpusError("support source disposition totals do not reconcile")
        document = _document(
            manifest,
            validators,
            request_sha256,
            entries_tuple,
            handoffs_tuple,
            outputs,
        )
        (stage / SUPPORT_CORPUS_MANIFEST).write_bytes(
            _canonical_json_bytes(document, pretty=True)
        )
        staged = _verify_output(stage, source_root, reused=False)
        if staged.request_sha256 != request_sha256:
            raise SupportCorpusError("staged support corpus request identity changed")
        _revalidate_input(source_root, manifest)
        _publish(stage, destination, backup)
        return _verify_output(destination, source_root, reused=False)
    finally:
        if os.path.lexists(work):
            _remove_owned_tree(work, parent, f".{destination.name}.inspection-")
        if os.path.lexists(stage):
            _remove_owned_tree(stage, parent, f".{destination.name}.staging-")
        if os.path.lexists(backup) and os.path.lexists(destination):
            _remove_owned_tree(backup, parent, f".{destination.name}.backup-")


build_corpus = build_support_corpus


__all__ = [
    "FONT_EXTENSIONS",
    "MAX_FONT_SOURCE_BYTES",
    "MAX_SUPPORT_CORPUS_BYTES",
    "MAX_SUPPORT_CORPUS_FILES",
    "MAX_TEXT_SOURCE_BYTES",
    "OPAQUE_EXTENSIONS",
    "SUPPORT_CORPUS_EXTENSIONS",
    "SUPPORT_CORPUS_MANIFEST",
    "SUPPORT_CORPUS_SCHEMA",
    "SUPPORT_CORPUS_SCHEMA_VERSION",
    "SupportCorpusBuildError",
    "SupportCorpusDependencyError",
    "SupportCorpusEntry",
    "SupportCorpusError",
    "SupportCorpusFailure",
    "SupportCorpusHandoff",
    "SupportCorpusLimitError",
    "SupportCorpusOutput",
    "SupportCorpusReport",
    "SupportCorpusReuseError",
    "TEXT_EXTENSIONS",
    "XML_EXTENSIONS",
    "build_corpus",
    "build_support_corpus",
]
