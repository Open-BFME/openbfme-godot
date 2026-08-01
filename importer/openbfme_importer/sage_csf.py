"""Bounded parser for SAGE compiled string tables (``.csf``).

CSF is the Westwood/EA binary localization format: a 24-byte header whose
magic is ``' FSC'``, followed by one ``' LBL'`` block per label.  Each label
block carries zero or more string rows whose magic is ``' RTS'`` (plain) or
``'WRTS'`` (plain plus an extra ASCII string); the UTF-16LE payload is stored
bitwise-NOTed.  Retail BFME launchers ship version 0; other SAGE titles ship
up to version 3, so this parser accepts 0..3 and fails closed above that.

The catalog keeps localized values in memory for private conversion work
while ``to_report`` exposes only counts, the version/language words, and
deterministic digests -- never retail text.
"""

from __future__ import annotations

import hashlib
import struct
from typing import Any


MAX_CSF_BYTES = 64 * 1024 * 1024
MAX_CSF_LABELS = 1_000_000
MAX_CSF_STRINGS_PER_LABEL = 4_096
MAX_LABEL_BYTES = 4_096
MAX_VALUE_CHARS = 1024 * 1024
MAX_EXTRA_BYTES = 1024 * 1024
SUPPORTED_MAX_VERSION = 3

_HEADER = struct.Struct("<4sIIIII")
_LABEL_HEADER = struct.Struct("<4sII")
_STRING_HEADER = struct.Struct("<4sI")
_U32 = struct.Struct("<I")

_FILE_MAGIC = b" FSC"
_LABEL_MAGIC = b" LBL"
_STRING_MAGIC = b" RTS"
_WIDE_STRING_MAGIC = b"WRTS"


class CsfFormatError(ValueError):
    """The input is not a structurally valid CSF string table."""


class CsfRecord:
    """One decoded label row, keeping the optional ``'WRTS'`` extra string."""

    __slots__ = ("label", "value", "extra")

    def __init__(self, label: str, value: str, extra: str | None) -> None:
        self.label = label
        self.value = value
        self.extra = extra

    def __repr__(self) -> str:  # payload-free by design
        return f"CsfRecord(label={self.label!r}, wide={self.extra is not None})"


class CsfCatalog:
    """Source-ordered CSF records plus case-insensitive resolution."""

    __slots__ = (
        "records",
        "rows",
        "version",
        "language_code",
        "declared_string_count",
        "wide_record_count",
        "source_sha256",
        "_by_label",
    )

    def __init__(
        self,
        rows: tuple[CsfRecord, ...],
        *,
        version: int,
        language_code: int,
        declared_string_count: int,
        source_sha256: str,
    ) -> None:
        self.rows = rows
        self.records: dict[str, str] = {row.label: row.value for row in rows}
        self.version = version
        self.language_code = language_code
        self.declared_string_count = declared_string_count
        self.wide_record_count = sum(1 for row in rows if row.extra is not None)
        self.source_sha256 = source_sha256
        self._by_label = {row.label.casefold(): row for row in rows}

    def __len__(self) -> int:
        return len(self.rows)

    def record(self, label: str) -> CsfRecord | None:
        """Return a record by case-insensitive label."""

        return self._by_label.get(label.casefold())

    def resolve(self, label: str) -> str | None:
        """Resolve a label to its decoded localized value."""

        record = self.record(label)
        return None if record is None else record.value

    def to_report(self) -> dict[str, Any]:
        """Return deterministic evidence without retail labels or text."""

        label_digest = hashlib.sha256()
        content_digest = hashlib.sha256()
        for row in self.rows:
            _digest_field(label_digest, row.label)
            _digest_field(content_digest, row.label)
            _digest_field(content_digest, row.value)
            _digest_field(content_digest, row.extra or "")
        return {
            "format": 1,
            "version": self.version,
            "languageCode": self.language_code,
            "recordCount": len(self.rows),
            "declaredStringCount": self.declared_string_count,
            "wideRecordCount": self.wide_record_count,
            "maxLabelChars": max((len(row.label) for row in self.rows), default=0),
            "maxValueChars": max((len(row.value) for row in self.rows), default=0),
            "sourceSha256": self.source_sha256,
            "labelSetSha256": label_digest.hexdigest(),
            "logicalContentSha256": content_digest.hexdigest(),
        }


def _digest_field(digest: Any, value: str) -> None:
    encoded = value.encode("utf-8")
    digest.update(len(encoded).to_bytes(8, "big"))
    digest.update(encoded)


class _Reader:
    """Bounded cursor that raises payload-free ``CsfFormatError`` on overrun."""

    __slots__ = ("data", "position")

    def __init__(self, data: bytes) -> None:
        self.data = data
        self.position = 0

    def take(self, count: int, context: str) -> bytes:
        end = self.position + count
        if count < 0 or end > len(self.data):
            raise CsfFormatError(
                f"truncated CSF: {context} needs {count} bytes at offset "
                f"{self.position} of {len(self.data)}"
            )
        chunk = self.data[self.position : end]
        self.position = end
        return chunk

    @property
    def exhausted(self) -> bool:
        return self.position >= len(self.data)


def _decode_label(raw: bytes, label_index: int) -> str:
    try:
        label = raw.decode("ascii")
    except UnicodeDecodeError as exc:
        raise CsfFormatError(
            f"CSF label {label_index} is not ASCII-decodable"
        ) from exc
    if not label:
        raise CsfFormatError(f"CSF label {label_index} is empty")
    if any(ord(character) < 0x20 or ord(character) == 0x7F for character in label):
        raise CsfFormatError(
            f"CSF label {label_index} contains control characters"
        )
    return label


def _decode_value(raw: bytes, label_index: int) -> str:
    inverted = bytes(byte ^ 0xFF for byte in raw)
    try:
        return inverted.decode("utf-16-le")
    except UnicodeDecodeError as exc:
        raise CsfFormatError(
            f"CSF string under label {label_index} is not valid UTF-16LE"
        ) from exc


def parse_csf(data: bytes) -> CsfCatalog:
    """Parse a binary CSF string table, failing closed on any structural defect."""

    if not isinstance(data, (bytes, bytearray, memoryview)):
        raise TypeError("parse_csf expects bytes")
    data = bytes(data)
    if len(data) > MAX_CSF_BYTES:
        raise CsfFormatError(f"CSF exceeds {MAX_CSF_BYTES} byte limit")

    reader = _Reader(data)
    header = reader.take(_HEADER.size, "file header")
    magic, version, label_count, string_count, _unused, language_code = (
        _HEADER.unpack(header)
    )
    if magic != _FILE_MAGIC:
        raise CsfFormatError(f"unsupported CSF magic {magic!r}")
    if version > SUPPORTED_MAX_VERSION:
        raise CsfFormatError(
            f"unsupported CSF version {version}; supported versions are "
            f"0..{SUPPORTED_MAX_VERSION}"
        )
    if label_count > MAX_CSF_LABELS:
        raise CsfFormatError(
            f"CSF label count {label_count} exceeds safety limit {MAX_CSF_LABELS}"
        )

    rows: list[CsfRecord] = []
    seen_labels: set[str] = set()
    decoded_string_count = 0
    for label_index in range(label_count):
        block = reader.take(_LABEL_HEADER.size, f"label block {label_index} header")
        block_magic, pair_count, label_length = _LABEL_HEADER.unpack(block)
        if block_magic != _LABEL_MAGIC:
            raise CsfFormatError(
                f"label block {label_index} has magic {block_magic!r}; "
                f"expected {_LABEL_MAGIC!r}"
            )
        if pair_count > MAX_CSF_STRINGS_PER_LABEL:
            raise CsfFormatError(
                f"label block {label_index} declares {pair_count} strings; "
                f"limit is {MAX_CSF_STRINGS_PER_LABEL}"
            )
        if label_length > MAX_LABEL_BYTES:
            raise CsfFormatError(
                f"label block {label_index} name length {label_length} exceeds "
                f"limit {MAX_LABEL_BYTES}"
            )
        label = _decode_label(
            reader.take(label_length, f"label block {label_index} name"), label_index
        )
        key = label.casefold()
        if key in seen_labels:
            raise CsfFormatError(
                f"duplicate case-insensitive CSF label at block {label_index}"
            )
        seen_labels.add(key)

        value = ""
        extra: str | None = None
        for pair_index in range(pair_count):
            string_header = reader.take(
                _STRING_HEADER.size,
                f"string {pair_index} header under label {label_index}",
            )
            string_magic, value_length = _STRING_HEADER.unpack(string_header)
            if string_magic not in (_STRING_MAGIC, _WIDE_STRING_MAGIC):
                raise CsfFormatError(
                    f"string {pair_index} under label {label_index} has magic "
                    f"{string_magic!r}; expected {_STRING_MAGIC!r} or "
                    f"{_WIDE_STRING_MAGIC!r}"
                )
            if value_length > MAX_VALUE_CHARS:
                raise CsfFormatError(
                    f"string {pair_index} under label {label_index} declares "
                    f"{value_length} UTF-16 units; limit is {MAX_VALUE_CHARS}"
                )
            decoded = _decode_value(
                reader.take(
                    value_length * 2,
                    f"string {pair_index} payload under label {label_index}",
                ),
                label_index,
            )
            pair_extra: str | None = None
            if string_magic == _WIDE_STRING_MAGIC:
                (extra_length,) = _U32.unpack(
                    reader.take(
                        _U32.size,
                        f"extra length of string {pair_index} under label "
                        f"{label_index}",
                    )
                )
                if extra_length > MAX_EXTRA_BYTES:
                    raise CsfFormatError(
                        f"extra string under label {label_index} declares "
                        f"{extra_length} bytes; limit is {MAX_EXTRA_BYTES}"
                    )
                extra_raw = reader.take(
                    extra_length,
                    f"extra string of string {pair_index} under label {label_index}",
                )
                try:
                    pair_extra = extra_raw.decode("ascii")
                except UnicodeDecodeError as exc:
                    raise CsfFormatError(
                        f"extra string under label {label_index} is not ASCII"
                    ) from exc
            if pair_index == 0:
                value = decoded
                extra = pair_extra
            decoded_string_count += 1
        rows.append(CsfRecord(label, value, extra))

    if decoded_string_count != string_count:
        raise CsfFormatError(
            f"CSF header declares {string_count} strings but label blocks "
            f"contain {decoded_string_count}"
        )
    if not reader.exhausted:
        raise CsfFormatError(
            f"CSF has {len(data) - reader.position} trailing bytes after the "
            f"last label block"
        )

    return CsfCatalog(
        tuple(rows),
        version=version,
        language_code=language_code,
        declared_string_count=string_count,
        source_sha256=hashlib.sha256(data).hexdigest(),
    )
