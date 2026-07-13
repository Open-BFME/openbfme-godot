"""Bounded parser for SAGE ``.str`` localization catalogs.

The parser keeps localized values in memory for private conversion work while
``neutral_summary`` exposes only counts and deterministic digests.  It accepts
the BFME II lexical form: ``Category:Label``, a quoted or alphanumeric value,
and ``END``, with ``;``/``//`` line comments between those components.
"""

from __future__ import annotations

from dataclasses import dataclass
import hashlib
import re
from typing import Any, Literal


MAX_STRING_BYTES = 16 * 1024 * 1024
MAX_STRING_RECORDS = 100_000
MAX_IDENTIFIER_CHARS = 4_096
MAX_VALUE_CHARS = 1024 * 1024
MAX_DUPLICATE_VALUE_TERMINATORS = 16
MAX_DIAGNOSTIC_IDENTIFIERS = 4_096

DuplicatePolicy = Literal["reject", "first-wins"]

_CATEGORY = re.compile(r"[0-9A-Za-z]+")
_LABEL = re.compile(r"[!-z]+")
_BARE_VALUE = re.compile(r"[0-9A-Za-z]+")
_ESCAPES = {
    "n": "\n",
    "r": "\r",
    "t": "\t",
    "v": "\v",
    "\\": "\\",
    "'": "'",
    '"': '"',
    "/": "/",
}


@dataclass(frozen=True, slots=True)
class SageStringRecord:
    """One decoded localization value, preserving its source spelling."""

    identifier: str
    category: str
    label: str
    value: str


@dataclass(frozen=True, slots=True)
class SageStringDiagnostics:
    """Bounded duplicate-ID evidence with no localized values."""

    source_record_count: int
    duplicate_record_count: int
    conflicting_duplicate_record_count: int
    duplicate_identifier_count: int
    conflicting_identifier_count: int
    duplicate_identifiers: tuple[str, ...]
    conflicting_identifiers: tuple[str, ...]
    identifiers_truncated: bool

    def to_report(self) -> dict[str, Any]:
        """Return a bounded external-report object containing IDs, never values."""

        return {
            "sourceRecordCount": self.source_record_count,
            "duplicateRecordCount": self.duplicate_record_count,
            "conflictingDuplicateRecordCount": self.conflicting_duplicate_record_count,
            "duplicateIdentifierCount": self.duplicate_identifier_count,
            "conflictingIdentifierCount": self.conflicting_identifier_count,
            "duplicateIdentifiers": list(self.duplicate_identifiers),
            "conflictingIdentifiers": list(self.conflicting_identifiers),
            "identifiersTruncated": self.identifiers_truncated,
        }


class SageStringCatalog:
    """Deterministically ordered records plus case-insensitive resolution."""

    __slots__ = (
        "records",
        "duplicate_value_terminators",
        "diagnostics",
        "_by_identifier",
    )

    def __init__(
        self,
        records: tuple[SageStringRecord, ...],
        *,
        duplicate_value_terminators: int,
        diagnostics: SageStringDiagnostics,
    ) -> None:
        self.records = records
        self.duplicate_value_terminators = duplicate_value_terminators
        self.diagnostics = diagnostics
        self._by_identifier = {record.identifier.casefold(): record for record in records}

    def __len__(self) -> int:
        return len(self.records)

    def record(self, identifier: str) -> SageStringRecord | None:
        """Return a record by case-insensitive logical identifier."""

        return self._by_identifier.get(identifier.casefold())

    def resolve(self, identifier: str) -> str | None:
        """Resolve a command/object text identifier to its decoded value."""

        record = self.record(identifier)
        return None if record is None else record.value

    def neutral_summary(self) -> dict[str, Any]:
        """Return deterministic evidence without retail identifiers or text."""

        identifier_digest = hashlib.sha256()
        content_digest = hashlib.sha256()
        for record in self.records:
            _digest_field(identifier_digest, record.identifier)
            _digest_field(content_digest, record.identifier)
            _digest_field(content_digest, record.value)
        return {
            "format": 1,
            "sourceRecordCount": self.diagnostics.source_record_count,
            "recordCount": len(self.records),
            "categoryCount": len({record.category.casefold() for record in self.records}),
            "duplicateRecordCount": self.diagnostics.duplicate_record_count,
            "conflictingDuplicateRecordCount": (
                self.diagnostics.conflicting_duplicate_record_count
            ),
            "duplicateIdentifierCount": self.diagnostics.duplicate_identifier_count,
            "conflictingIdentifierCount": self.diagnostics.conflicting_identifier_count,
            "diagnosticIdentifiersTruncated": self.diagnostics.identifiers_truncated,
            "duplicateValueTerminatorCount": self.duplicate_value_terminators,
            "maxIdentifierChars": max(
                (len(record.identifier) for record in self.records), default=0
            ),
            "maxValueChars": max((len(record.value) for record in self.records), default=0),
            "identifierSetSha256": identifier_digest.hexdigest(),
            "logicalContentSha256": content_digest.hexdigest(),
        }


def _digest_field(digest: Any, value: str) -> None:
    encoded = value.encode("utf-8")
    digest.update(len(encoded).to_bytes(8, "big"))
    digest.update(encoded)


def _decode(source: bytes) -> str:
    if len(source) > MAX_STRING_BYTES:
        raise ValueError(f"string catalog exceeds {MAX_STRING_BYTES} byte limit")
    if b"\0" in source:
        raise ValueError("string catalog contains a NUL byte")
    try:
        return source.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise ValueError("string catalog has unsupported encoding") from exc


class _Parser:
    __slots__ = (
        "source",
        "position",
        "duplicate_policy",
        "duplicate_value_terminators",
    )

    def __init__(self, source: str, duplicate_policy: DuplicatePolicy) -> None:
        self.source = source
        self.position = 0
        self.duplicate_policy = duplicate_policy
        self.duplicate_value_terminators = 0

    def _error(self, message: str, position: int | None = None) -> ValueError:
        offset = self.position if position is None else position
        line = self.source.count("\n", 0, offset) + 1
        previous_newline = self.source.rfind("\n", 0, offset)
        column = offset - previous_newline
        return ValueError(f"{message} at line {line}, column {column}")

    def _skip_layout(self) -> None:
        length = len(self.source)
        while self.position < length:
            if self.source[self.position].isspace():
                self.position += 1
                continue
            if self.source[self.position] == ";":
                newline = self.source.find("\n", self.position + 1)
                self.position = length if newline < 0 else newline + 1
                continue
            if self.source.startswith("//", self.position):
                newline = self.source.find("\n", self.position + 2)
                self.position = length if newline < 0 else newline + 1
                continue
            return

    def _read_identifier(self) -> tuple[str, str, str]:
        start = self.position
        length = len(self.source)
        while self.position < length and not self.source[self.position].isspace():
            self.position += 1
            if self.position - start > MAX_IDENTIFIER_CHARS:
                raise self._error("string identifier exceeds character limit", start)
        identifier = self.source[start : self.position]
        if ":" not in identifier:
            raise self._error("string identifier is missing category separator", start)
        category, label = identifier.split(":", 1)
        if _CATEGORY.fullmatch(category) is None:
            raise self._error("string identifier has an invalid category", start)
        if _LABEL.fullmatch(label) is None:
            raise self._error("string identifier has an invalid label", start)
        return identifier, category, label

    def _read_quoted_value(self) -> str:
        start = self.position
        self.position += 1
        value: list[str] = []
        length = len(self.source)
        while self.position < length:
            character = self.source[self.position]
            if character == '"':
                self.position += 1
                return "".join(value)
            if character == "\\":
                self.position += 1
                if self.position >= length:
                    raise self._error("truncated string escape", start)
                escaped = self.source[self.position]
                if escaped not in _ESCAPES:
                    raise self._error("unsupported string escape")
                value.append(_ESCAPES[escaped])
                self.position += 1
            else:
                value.append(character)
                self.position += 1
            if len(value) > MAX_VALUE_CHARS:
                raise self._error("localized value exceeds character limit", start)
        raise self._error("unterminated quoted localized value", start)

    def _read_bare_value(self) -> str:
        start = self.position
        length = len(self.source)
        while (
            self.position < length
            and self.source[self.position].isascii()
            and self.source[self.position].isalnum()
        ):
            self.position += 1
            if self.position - start > MAX_VALUE_CHARS:
                raise self._error("localized value exceeds character limit", start)
        value = self.source[start : self.position]
        if _BARE_VALUE.fullmatch(value) is None:
            raise self._error("localized value must be quoted or alphanumeric", start)
        return value

    def _consume_end(self) -> None:
        start = self.position
        if self.source[self.position : self.position + 3].casefold() != "end":
            raise self._error("localized record is missing END", start)
        self.position += 3
        if self.position >= len(self.source):
            return
        if self.source[self.position].isspace() or self.source[self.position] == ";":
            return
        if self.source.startswith("//", self.position):
            return
        raise self._error("unexpected characters after END", self.position)

    def parse(self) -> SageStringCatalog:
        records: list[SageStringRecord] = []
        seen: dict[str, SageStringRecord] = {}
        duplicate_record_count = 0
        conflicting_duplicate_record_count = 0
        duplicate_identifiers: dict[str, str] = {}
        conflicting_identifiers: set[str] = set()
        source_record_count = 0
        self._skip_layout()
        while self.position < len(self.source):
            if source_record_count >= MAX_STRING_RECORDS:
                raise self._error("string record count exceeds limit")
            source_record_count += 1
            identifier, category, label = self._read_identifier()
            self._skip_layout()
            if self.position >= len(self.source):
                raise self._error("localized record is missing a value")
            if self.source[self.position] == '"':
                value = self._read_quoted_value()
                # BFME II's shipped English catalog contains a bounded, known
                # lexical typo where a quoted value has a second terminator.
                # Accept only one immediately adjacent quote and report it as
                # neutral structural evidence; arbitrary trailing text remains
                # an error.
                if (
                    self.position < len(self.source)
                    and self.source[self.position] == '"'
                ):
                    self.position += 1
                    self.duplicate_value_terminators += 1
                    if (
                        self.duplicate_value_terminators
                        > MAX_DUPLICATE_VALUE_TERMINATORS
                    ):
                        raise self._error("duplicate value terminator count exceeds limit")
            else:
                value = self._read_bare_value()
            self._skip_layout()
            self._consume_end()

            key = identifier.casefold()
            record = SageStringRecord(identifier, category, label, value)
            previous = seen.get(key)
            if previous is not None:
                if self.duplicate_policy == "reject":
                    raise self._error("duplicate case-insensitive string identifier")
                duplicate_record_count += 1
                duplicate_identifiers.setdefault(key, previous.identifier)
                if value != previous.value:
                    conflicting_duplicate_record_count += 1
                    conflicting_identifiers.add(key)
            else:
                seen[key] = record
                records.append(record)
            self._skip_layout()

        ordered = tuple(
            sorted(records, key=lambda record: (record.identifier.casefold(), record.identifier))
        )
        ordered_duplicate_identifiers = tuple(
            sorted(
                duplicate_identifiers.values(),
                key=lambda identifier: (identifier.casefold(), identifier),
            )
        )
        bounded_duplicate_identifiers = ordered_duplicate_identifiers[
            :MAX_DIAGNOSTIC_IDENTIFIERS
        ]
        bounded_conflicting_identifiers = tuple(
            identifier
            for identifier in bounded_duplicate_identifiers
            if identifier.casefold() in conflicting_identifiers
        )
        diagnostics = SageStringDiagnostics(
            source_record_count=source_record_count,
            duplicate_record_count=duplicate_record_count,
            conflicting_duplicate_record_count=conflicting_duplicate_record_count,
            duplicate_identifier_count=len(duplicate_identifiers),
            conflicting_identifier_count=len(conflicting_identifiers),
            duplicate_identifiers=bounded_duplicate_identifiers,
            conflicting_identifiers=bounded_conflicting_identifiers,
            identifiers_truncated=(
                len(ordered_duplicate_identifiers) > len(bounded_duplicate_identifiers)
            ),
        )
        return SageStringCatalog(
            ordered,
            duplicate_value_terminators=self.duplicate_value_terminators,
            diagnostics=diagnostics,
        )


def parse_string_catalog(
    source: bytes,
    *,
    duplicate_policy: DuplicatePolicy = "reject",
) -> SageStringCatalog:
    """Parse a CP-1252 SAGE catalog with strict duplicate rejection by default.

    ``first-wins`` is an explicit BFME2 compatibility policy.  It preserves the
    first source record for resolution and accounts for every later duplicate
    without retaining duplicate localized values in diagnostics.
    """

    if duplicate_policy not in {"reject", "first-wins"}:
        raise ValueError(f"unsupported duplicate policy: {duplicate_policy!r}")
    return _Parser(_decode(source), duplicate_policy).parse()
