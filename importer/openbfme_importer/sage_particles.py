"""Bounded, deterministic parsing for SAGE particle definitions.

The BFME2 data set contains two related definition families.  Legacy
``ParticleSystem`` bodies are generally flat, while ``FXParticleSystem``
bodies contain nested sections.  Some FX sections use a SAGE-specific
assignment-shaped header such as ``Section = SectionType`` followed by child
entries and an ``End``.  This module preserves that distinction and source
provenance without interpreting any particle parameter.

The parser accepts bytes only, never reads a retail install, and never returns
source bytes or filesystem paths.  Callers decide whether and where parsed
values may be serialized.
"""

from __future__ import annotations

from dataclasses import dataclass
from hashlib import sha256
import re
from typing import Iterable, TypeAlias


MAX_PARTICLE_SOURCE_BYTES = 16 * 1024 * 1024
MAX_PARTICLE_LINE_BYTES = 64 * 1024
MAX_PARTICLE_DEFINITIONS = 65_536
MAX_PARTICLE_ENTRIES_PER_DEFINITION = 65_536
MAX_PARTICLE_TOTAL_ENTRIES = 2_000_000
MAX_PARTICLE_NESTING = 64
MAX_PARTICLE_VALUE_CHARACTERS = 16_384
PARTICLE_DEFINITION_DOCUMENT_SCHEMA = "openbfme.sage-particle-definition"
PARTICLE_DEFINITION_DOCUMENT_SCHEMA_VERSION = 0

_DEFINITION_KINDS = ("ParticleSystem", "FXParticleSystem")
_KIND_BY_FOLD = {kind.casefold(): kind for kind in _DEFINITION_KINDS}
_HEADER = re.compile(r"^(ParticleSystem|FXParticleSystem)\s+(\S+)\s*$", re.IGNORECASE)
_FIELD = re.compile(r"^[A-Za-z_][A-Za-z0-9_.-]{0,127}$")
_IDENTIFIER = re.compile(r"^[A-Za-z_][A-Za-z0-9_.+:-]{0,255}$")


@dataclass(frozen=True, slots=True)
class ParticleSourceSpan:
    """Inclusive line and half-open byte provenance for one parsed item."""

    start_line: int
    end_line: int
    start_byte: int
    end_byte: int
    sha256: str

    @property
    def byte_length(self) -> int:
        return self.end_byte - self.start_byte


@dataclass(frozen=True, slots=True)
class ParticleAssignment:
    """One ordered scalar assignment exactly as authored after comment removal."""

    field: str
    value: str
    source: ParticleSourceSpan


@dataclass(frozen=True, slots=True)
class ParticleBlock:
    """One nested particle section.

    ``selector`` is populated for SAGE assignment-shaped section headers and
    is ``None`` for a bare header such as ``System``.
    """

    field: str
    selector: str | None
    entries: tuple[ParticleEntry, ...]
    source: ParticleSourceSpan

    def assignments(self, *, recursive: bool = False) -> tuple[ParticleAssignment, ...]:
        return _assignments(self.entries, recursive=recursive)


ParticleEntry: TypeAlias = ParticleAssignment | ParticleBlock


@dataclass(frozen=True, slots=True)
class ParticleDefinition:
    """One named top-level SAGE particle definition."""

    kind: str
    name: str
    entries: tuple[ParticleEntry, ...]
    source: ParticleSourceSpan

    def assignments(self, *, recursive: bool = False) -> tuple[ParticleAssignment, ...]:
        """Return scalar assignments in authored order.

        With ``recursive=True``, nested sections are walked depth-first at
        their authored position in the surrounding entry stream.
        """

        return _assignments(self.entries, recursive=recursive)

    def blocks(self, *, recursive: bool = False) -> tuple[ParticleBlock, ...]:
        """Return nested blocks in authored, depth-first order."""

        result: list[ParticleBlock] = []
        for entry in self.entries:
            if not isinstance(entry, ParticleBlock):
                continue
            result.append(entry)
            if recursive:
                result.extend(_blocks(entry.entries))
        return tuple(result)


@dataclass(frozen=True, slots=True)
class _Line:
    number: int
    start_byte: int
    end_byte: int
    indent: int
    text: str


@dataclass(slots=True)
class _OpenBlock:
    field: str
    selector: str | None
    entries: list[ParticleEntry | _OpenBlock]
    line: _Line
    end_line: _Line | None = None


def _span_document(source: ParticleSourceSpan) -> dict[str, int | str]:
    return {
        "startLine": source.start_line,
        "endLine": source.end_line,
        "startByte": source.start_byte,
        "endByte": source.end_byte,
        "byteLength": source.byte_length,
        "sha256": source.sha256,
    }


def _entry_document(entry: ParticleEntry) -> dict[str, object]:
    if isinstance(entry, ParticleAssignment):
        return {
            "type": "assignment",
            "field": entry.field,
            "value": entry.value,
            "source": _span_document(entry.source),
        }
    return {
        "type": "block",
        "field": entry.field,
        "selector": entry.selector,
        "entries": [_entry_document(child) for child in entry.entries],
        "source": _span_document(entry.source),
    }


def particle_definition_document(
    definition: ParticleDefinition,
) -> dict[str, object]:
    """Serialize one selected definition into the private runtime contract.

    The document deliberately contains no host path and no unselected sibling
    definitions.  Scalar values are retained because the output is the exact
    private runtime payload, while public proof reports must remain hash-only.
    """

    if not isinstance(definition, ParticleDefinition):
        raise TypeError("particle definition document requires ParticleDefinition")
    return {
        "schema": PARTICLE_DEFINITION_DOCUMENT_SCHEMA,
        "schemaVersion": PARTICLE_DEFINITION_DOCUMENT_SCHEMA_VERSION,
        "kind": definition.kind,
        "name": definition.name,
        "entries": [_entry_document(entry) for entry in definition.entries],
        "source": _span_document(definition.source),
    }


def _assignments(
    entries: tuple[ParticleEntry, ...], *, recursive: bool
) -> tuple[ParticleAssignment, ...]:
    result: list[ParticleAssignment] = []
    for entry in entries:
        if isinstance(entry, ParticleAssignment):
            result.append(entry)
        elif recursive:
            result.extend(_assignments(entry.entries, recursive=True))
    return tuple(result)


def _blocks(entries: tuple[ParticleEntry, ...]) -> list[ParticleBlock]:
    result: list[ParticleBlock] = []
    for entry in entries:
        if isinstance(entry, ParticleBlock):
            result.append(entry)
            result.extend(_blocks(entry.entries))
    return result


def _safe_identifier(value: str, context: str) -> str:
    if not _IDENTIFIER.fullmatch(value):
        raise ValueError(f"unsafe {context}: {value!r}")
    return value


def _safe_field(value: str, context: str) -> str:
    if not _FIELD.fullmatch(value):
        raise ValueError(f"unsafe {context}: {value!r}")
    return value


def _strip_comment(raw: str, line_number: int) -> str:
    quoted = False
    index = 0
    while index < len(raw):
        character = raw[index]
        if quoted and character == "\\" and index + 1 < len(raw):
            if raw[index + 1] == '"':
                index += 2
                continue
        if character == '"':
            if quoted and index + 1 < len(raw) and raw[index + 1] == '"':
                index += 2
                continue
            quoted = not quoted
        elif not quoted and character == ";":
            return raw[:index].rstrip()
        elif not quoted and raw[index : index + 2] == "//":
            return raw[:index].rstrip()
        index += 1
    if quoted:
        raise ValueError(f"unterminated quoted value at line {line_number}")
    return raw.rstrip()


def _indent_width(prefix: str) -> int:
    width = 0
    for character in prefix:
        if character == "\t":
            width = (width // 8 + 1) * 8
        else:
            width += 1
    return width


def _lines(source: bytes) -> tuple[_Line, ...]:
    if not isinstance(source, bytes):
        raise TypeError("particle definition source must be bytes")
    if len(source) > MAX_PARTICLE_SOURCE_BYTES:
        raise ValueError(
            f"particle definition source exceeds {MAX_PARTICLE_SOURCE_BYTES} byte limit"
        )
    if b"\0" in source:
        raise ValueError("particle definition source contains a NUL byte")
    try:
        decoded = source.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise ValueError("particle definition source has unsupported encoding") from exc
    for character in decoded:
        if ord(character) < 32 and character not in "\t\r\n":
            raise ValueError("particle definition source contains a control character")

    result: list[_Line] = []
    offset = 0
    for number, raw_bytes in enumerate(source.splitlines(keepends=True), start=1):
        if len(raw_bytes) > MAX_PARTICLE_LINE_BYTES:
            raise ValueError(
                f"particle definition line {number} exceeds "
                f"{MAX_PARTICLE_LINE_BYTES} byte limit"
            )
        raw_text = raw_bytes.decode("cp1252")
        if raw_text.endswith("\n"):
            raw_text = raw_text[:-1]
        if raw_text.endswith("\r"):
            raw_text = raw_text[:-1]
        uncommented = _strip_comment(raw_text, number)
        leading = uncommented[: len(uncommented) - len(uncommented.lstrip(" \t"))]
        text = uncommented[len(leading) :].strip()
        if text:
            result.append(
                _Line(
                    number=number,
                    start_byte=offset,
                    end_byte=offset + len(raw_bytes),
                    indent=_indent_width(leading),
                    text=text,
                )
            )
        offset += len(raw_bytes)
    return tuple(result)


def _span(source: bytes, start: _Line, end: _Line) -> ParticleSourceSpan:
    payload = source[start.start_byte : end.end_byte]
    return ParticleSourceSpan(
        start_line=start.number,
        end_line=end.number,
        start_byte=start.start_byte,
        end_byte=end.end_byte,
        sha256=sha256(payload).hexdigest(),
    )


def _split_assignment(text: str, context: str) -> tuple[str, str]:
    if "=" not in text:
        return _safe_field(text, context), ""
    field, value = (part.strip() for part in text.split("=", 1))
    _safe_field(field, context)
    if not value:
        raise ValueError(f"{context} has an empty value")
    if len(value) > MAX_PARTICLE_VALUE_CHARACTERS:
        raise ValueError(f"{context} value exceeds character limit")
    return field, value


def _freezed_entries(
    source: bytes, entries: list[ParticleEntry | _OpenBlock]
) -> tuple[ParticleEntry, ...]:
    result: list[ParticleEntry] = []
    for entry in entries:
        if isinstance(entry, ParticleAssignment):
            result.append(entry)
            continue
        if entry.end_line is None:
            raise ValueError(f"unterminated particle block: {entry.field!r}")
        result.append(
            ParticleBlock(
                field=entry.field,
                selector=entry.selector,
                entries=_freezed_entries(source, entry.entries),
                source=_span(source, entry.line, entry.end_line),
            )
        )
    return tuple(result)


def _assignment_opens_block(lines: tuple[_Line, ...], index: int) -> bool:
    if index + 1 >= len(lines):
        return False
    current = lines[index]
    following = lines[index + 1]
    if following.indent > current.indent:
        return True
    return following.text.casefold() == "end" and following.indent == current.indent


def _parse_definition(
    source: bytes,
    lines: tuple[_Line, ...],
    start_index: int,
    total_entries: int,
) -> tuple[ParticleDefinition, int, int]:
    header = lines[start_index]
    match = _HEADER.fullmatch(header.text)
    if match is None:
        raise ValueError(
            f"malformed particle definition header at line {header.number}"
        )
    if header.indent != 0:
        raise ValueError(
            f"particle definition header must be top-level at line {header.number}"
        )
    kind = _KIND_BY_FOLD[match.group(1).casefold()]
    name = _safe_identifier(match.group(2), f"{kind} name")

    root = _OpenBlock(kind, name, [], header)
    stack = [root]
    definition_entries = 0
    index = start_index + 1
    while index < len(lines):
        line = lines[index]
        current = stack[-1]
        header_indent = current.line.indent
        if line.text.casefold() == "end":
            if line.indent != header_indent:
                raise ValueError(
                    f"unbalanced End at line {line.number}: expected indentation "
                    f"{header_indent}, found {line.indent}"
                )
            current.end_line = line
            stack.pop()
            if not stack:
                definition = ParticleDefinition(
                    kind=kind,
                    name=name,
                    entries=_freezed_entries(source, root.entries),
                    source=_span(source, header, line),
                )
                return definition, index + 1, total_entries
            index += 1
            continue

        if line.indent <= header_indent:
            if _HEADER.match(line.text):
                raise ValueError(
                    f"unterminated {current.field!r} before line {line.number}"
                )
            raise ValueError(
                f"particle entry at line {line.number} is outside its containing block"
            )

        is_assignment = "=" in line.text
        field, value = _split_assignment(
            line.text, f"particle field at line {line.number}"
        )
        opens_block = not is_assignment or _assignment_opens_block(lines, index)
        if opens_block:
            selector = value if is_assignment else None
            if selector is not None:
                _safe_identifier(
                    selector, f"particle block selector at line {line.number}"
                )
            opened = _OpenBlock(field, selector, [], line)
            current.entries.append(opened)
            stack.append(opened)
            if len(stack) - 1 > MAX_PARTICLE_NESTING:
                raise ValueError(
                    f"particle nesting exceeds {MAX_PARTICLE_NESTING} block limit"
                )
        else:
            if not is_assignment:
                raise ValueError(f"malformed particle entry at line {line.number}")
            current.entries.append(
                ParticleAssignment(field, value, _span(source, line, line))
            )

        definition_entries += 1
        total_entries += 1
        if definition_entries > MAX_PARTICLE_ENTRIES_PER_DEFINITION:
            raise ValueError(
                f"{kind} {name!r} exceeds "
                f"{MAX_PARTICLE_ENTRIES_PER_DEFINITION} entry limit"
            )
        if total_entries > MAX_PARTICLE_TOTAL_ENTRIES:
            raise ValueError(
                f"particle definitions exceed {MAX_PARTICLE_TOTAL_ENTRIES} total entry limit"
            )
        index += 1

    raise ValueError(f"unterminated {kind} definition: {name!r}")


def parse_particle_definitions(source: bytes) -> tuple[ParticleDefinition, ...]:
    """Parse all top-level ``ParticleSystem`` and ``FXParticleSystem`` blocks.

    Definition order, entry order, scalar text, nested structure, and exact
    raw-block hashes are preserved.  Duplicate names remain visible here so a
    census can report them; :func:`select_particle_definition` fails closed
    when a caller requests an ambiguous record.
    """

    lines = _lines(source)
    definitions: list[ParticleDefinition] = []
    total_entries = 0
    index = 0
    while index < len(lines):
        line = lines[index]
        if line.text.casefold() == "end":
            raise ValueError(f"unbalanced top-level End at line {line.number}")
        match = _HEADER.fullmatch(line.text)
        if match is None:
            if line.text.casefold().startswith(
                ("particlesystem ", "fxparticlesystem ")
            ):
                raise ValueError(
                    f"malformed particle definition header at line {line.number}"
                )
            raise ValueError(f"unsupported top-level input at line {line.number}")
        definition, index, total_entries = _parse_definition(
            source, lines, index, total_entries
        )
        definitions.append(definition)
        if len(definitions) > MAX_PARTICLE_DEFINITIONS:
            raise ValueError(
                f"particle definition count exceeds {MAX_PARTICLE_DEFINITIONS} limit"
            )
    return tuple(definitions)


def select_particle_definition(
    definitions: Iterable[ParticleDefinition],
    name: str,
    *,
    kind: str | None = None,
) -> ParticleDefinition:
    """Return exactly one case-insensitive named definition or fail closed."""

    requested_name = _safe_identifier(name, "particle definition name")
    canonical_kind: str | None = None
    if kind is not None:
        canonical_kind = _KIND_BY_FOLD.get(kind.casefold())
        if canonical_kind is None:
            raise ValueError(f"unsupported particle definition kind: {kind!r}")

    matches = [
        definition
        for definition in definitions
        if definition.name.casefold() == requested_name.casefold()
        and (canonical_kind is None or definition.kind == canonical_kind)
    ]
    if not matches:
        label = f"{canonical_kind} " if canonical_kind is not None else ""
        raise ValueError(f"missing {label}particle definition: {requested_name!r}")
    if len(matches) != 1:
        kinds = ", ".join(definition.kind for definition in matches[:4])
        raise ValueError(
            f"ambiguous particle definition {requested_name!r}: "
            f"{len(matches)} matches ({kinds})"
        )
    return matches[0]


def parse_particle_definition(
    source: bytes, name: str, *, kind: str | None = None
) -> ParticleDefinition:
    """Parse a document and require one exact named definition."""

    return select_particle_definition(
        parse_particle_definitions(source), name, kind=kind
    )


__all__ = [
    "MAX_PARTICLE_DEFINITIONS",
    "MAX_PARTICLE_ENTRIES_PER_DEFINITION",
    "MAX_PARTICLE_LINE_BYTES",
    "MAX_PARTICLE_NESTING",
    "MAX_PARTICLE_SOURCE_BYTES",
    "MAX_PARTICLE_TOTAL_ENTRIES",
    "MAX_PARTICLE_VALUE_CHARACTERS",
    "PARTICLE_DEFINITION_DOCUMENT_SCHEMA",
    "PARTICLE_DEFINITION_DOCUMENT_SCHEMA_VERSION",
    "ParticleAssignment",
    "ParticleBlock",
    "ParticleDefinition",
    "ParticleEntry",
    "ParticleSourceSpan",
    "parse_particle_definition",
    "parse_particle_definitions",
    "particle_definition_document",
    "select_particle_definition",
]
