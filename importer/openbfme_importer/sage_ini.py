"""Small, bounded lexical helpers for evidence-driven SAGE INI censuses.

This is not a runtime INI interpreter. It recognizes only named flat blocks and
top-level Object-family definitions so dependency censuses can replace filename
guessing without packaging retail text.
"""

from __future__ import annotations

from dataclasses import dataclass
import re


MAX_INI_BYTES = 16 * 1024 * 1024
MAX_BLOCKS = 100_000
MAX_ASSIGNMENTS_PER_BLOCK = 16_384

_OBJECT_HEADER = re.compile(
    r"^(Object|ChildObject|ObjectReskin)\s+(\S+)(?:\s+(\S+))?\s*$",
    re.IGNORECASE,
)


@dataclass(frozen=True, slots=True)
class IniBlock:
    kind: str
    name: str
    parent: str | None
    assignments: tuple[tuple[str, str], ...]

    def values(self, key: str) -> tuple[str, ...]:
        folded = key.casefold()
        return tuple(value for field, value in self.assignments if field.casefold() == folded)


def _decode(source: bytes) -> str:
    if len(source) > MAX_INI_BYTES:
        raise ValueError(f"INI source exceeds {MAX_INI_BYTES} byte limit")
    if b"\0" in source:
        raise ValueError("INI source contains a NUL byte")
    try:
        return source.decode("cp1252")
    except UnicodeDecodeError as exc:
        raise ValueError("INI source has unsupported encoding") from exc


def _strip_comments(raw: str) -> str:
    quoted = False
    index = 0
    while index < len(raw):
        character = raw[index]
        if character == '"':
            quoted = not quoted
        elif not quoted and character == ";":
            return raw[:index].strip()
        elif not quoted and character == "/" and index + 1 < len(raw) and raw[index + 1] == "/":
            return raw[:index].strip()
        index += 1
    return raw.strip()


def _lines(source: bytes) -> list[str]:
    return [line for raw in _decode(source).splitlines() if (line := _strip_comments(raw))]


def _assignment(line: str) -> tuple[str, str] | None:
    if "=" not in line:
        return None
    key, value = (part.strip() for part in line.split("=", 1))
    if not key or not value:
        return None
    return key, value


def parse_flat_named_blocks(source: bytes, kind: str) -> tuple[IniBlock, ...]:
    """Parse a non-nested SAGE block family such as CommandSet."""

    header = re.compile(r"^" + re.escape(kind) + r"\s+(\S+)\s*$", re.IGNORECASE)
    blocks: list[IniBlock] = []
    current_name: str | None = None
    assignments: list[tuple[str, str]] = []
    for line in _lines(source):
        if current_name is None:
            match = header.fullmatch(line)
            if match:
                current_name = match.group(1)
                assignments = []
            continue
        if line.casefold() == "end":
            blocks.append(IniBlock(kind, current_name, None, tuple(assignments)))
            if len(blocks) > MAX_BLOCKS:
                raise ValueError(f"{kind} block count exceeds limit")
            current_name = None
            assignments = []
            continue
        nested = header.fullmatch(line)
        if nested:
            raise ValueError(f"unterminated {kind} block before {nested.group(1)}")
        item = _assignment(line)
        if item is not None:
            assignments.append(item)
            if len(assignments) > MAX_ASSIGNMENTS_PER_BLOCK:
                raise ValueError(f"{kind} assignment count exceeds limit")
    if current_name is not None:
        raise ValueError(f"unterminated {kind} block: {current_name}")
    return tuple(blocks)


def parse_object_definitions(source: bytes) -> tuple[IniBlock, ...]:
    """Split top-level Object/ChildObject/ObjectReskin definitions.

    Object bodies are intentionally not interpreted. All scalar assignments are
    retained so a later typed edge whitelist can select source-proven references.
    """

    blocks: list[IniBlock] = []
    current: tuple[str, str, str | None] | None = None
    assignments: list[tuple[str, str]] = []

    def finish() -> None:
        nonlocal current, assignments
        if current is None:
            return
        blocks.append(IniBlock(current[0], current[1], current[2], tuple(assignments)))
        if len(blocks) > MAX_BLOCKS:
            raise ValueError("Object block count exceeds limit")
        current = None
        assignments = []

    for line in _lines(source):
        match = _OBJECT_HEADER.fullmatch(line)
        if match:
            finish()
            current = (match.group(1), match.group(2), match.group(3))
            continue
        if current is None:
            continue
        item = _assignment(line)
        if item is not None:
            assignments.append(item)
            if len(assignments) > MAX_ASSIGNMENTS_PER_BLOCK:
                raise ValueError("Object assignment count exceeds limit")
    finish()
    return tuple(blocks)

