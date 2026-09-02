"""Shared lossless readers for non-Object top-level SAGE INI blocks."""

from __future__ import annotations

from collections import OrderedDict
from collections.abc import Iterable, Mapping, Sequence
from dataclasses import dataclass
import re
from typing import Any

from .objects import _value


_HEADER = re.compile(r"^(?P<kind>[A-Za-z][A-Za-z0-9_]*)\s+(?P<name>\S+)\s*$")


@dataclass(frozen=True, slots=True)
class CookAssignment:
    key: str
    value: str
    source_virtual_path: str
    line: int


@dataclass(frozen=True, slots=True)
class CookBlock:
    kind: str
    name: str
    assignments: tuple[CookAssignment, ...]
    blocks: tuple["CookBlock", ...]
    source_virtual_path: str
    line: int


@dataclass(frozen=True, slots=True)
class ParseResult:
    blocks: tuple[CookBlock, ...]
    failures: tuple[dict[str, str], ...]


@dataclass(slots=True)
class _MutableBlock:
    kind: str
    name: str
    source_virtual_path: str
    line: int
    assignments: list[CookAssignment]
    blocks: list["_MutableBlock"]


def normalize_documents(
    documents: Iterable[tuple[str, bytes]],
) -> list[tuple[str, bytes]]:
    """Apply the Object cook's virtual-path ordering and duplicate rules."""

    normalized: list[tuple[str, bytes]] = []
    seen: set[str] = set()
    for raw_path, source in documents:
        path = raw_path.replace("\\", "/").lstrip("/")
        if not path or path.casefold() in seen:
            raise ValueError(f"duplicate or empty INI virtual path: {raw_path!r}")
        if not isinstance(source, bytes):
            raise TypeError(f"INI source {path!r} must be bytes")
        seen.add(path.casefold())
        normalized.append((path, source))
    normalized.sort(key=lambda item: (item[0].casefold(), item[0]))
    if not normalized:
        raise ValueError("no INI files were supplied")
    return normalized


def _strip_comment(raw: str) -> str:
    quoted = False
    index = 0
    while index < len(raw):
        char = raw[index]
        if char == '"':
            quoted = not quoted
        elif not quoted and raw[index : index + 3] == ";,;":
            return raw[:index].strip()
        elif not quoted and char == ";":
            return raw[:index].strip()
        elif not quoted and raw[index : index + 2] == "//":
            return raw[:index].strip()
        index += 1
    return raw.strip()


def _assignment(text: str) -> tuple[str, str] | None:
    if "=" not in text:
        return None
    key, value = (part.strip() for part in text.split("=", 1))
    return (key, value) if key and value else None


def _freeze(block: _MutableBlock) -> CookBlock:
    return CookBlock(
        kind=block.kind,
        name=block.name,
        assignments=tuple(block.assignments),
        blocks=tuple(_freeze(child) for child in block.blocks),
        source_virtual_path=block.source_virtual_path,
        line=block.line,
    )


def _parse_block(
    lines: Sequence[str],
    start: int,
    path: str,
    kind: str,
    name: str,
    target_kinds: frozenset[str],
    retain_malformed: bool,
) -> tuple[CookBlock | None, int, dict[str, str] | None]:
    root = _MutableBlock(kind, name, path, start + 1, [], [])
    stack = [root]
    index = start + 1
    while index < len(lines):
        text = _strip_comment(lines[index])
        if not text:
            index += 1
            continue
        header = _HEADER.fullmatch(text)
        if len(stack) == 1 and header and header.group("kind").casefold() in target_kinds:
            return (_freeze(root) if retain_malformed else None), index, {
                "file": path,
                "block": name,
                "message": f"{path}:{start + 1}: unterminated {kind} {name}",
            }
        if text.casefold() == "end":
            if len(stack) == 1:
                return _freeze(root), index + 1, None
            stack.pop()
            index += 1
            continue
        item = _assignment(text)
        if item is not None:
            stack[-1].assignments.append(
                CookAssignment(item[0], item[1], path, index + 1)
            )
            index += 1
            continue
        tokens = text.split()
        child = _MutableBlock(tokens[0], " ".join(tokens[1:]), path, index + 1, [], [])
        stack[-1].blocks.append(child)
        stack.append(child)
        index += 1
    return (_freeze(root) if retain_malformed else None), len(lines), {
        "file": path,
        "block": name,
        "message": f"{path}:{start + 1}: unterminated {kind} {name}",
    }


def parse_named_blocks(
    documents: Sequence[tuple[str, bytes]],
    kinds: Iterable[str],
    *,
    retain_malformed: bool = False,
) -> ParseResult:
    """Parse selected named block families and recover after malformed siblings."""

    spellings = {kind.casefold(): kind for kind in kinds}
    targets = frozenset(spellings)
    effective: OrderedDict[tuple[str, str], CookBlock] = OrderedDict()
    failures: list[dict[str, str]] = []
    for path, source in documents:
        try:
            lines = source.decode("cp1252").splitlines()
        except UnicodeDecodeError as exc:
            failures.append(
                {
                    "file": path,
                    "block": "",
                    "message": f"{path}: source is not cp1252: {exc}",
                }
            )
            continue
        index = 0
        while index < len(lines):
            text = _strip_comment(lines[index])
            header = _HEADER.fullmatch(text)
            if header is None or header.group("kind").casefold() not in targets:
                index += 1
                continue
            kind = spellings[header.group("kind").casefold()]
            name = header.group("name")
            block, next_index, failure = _parse_block(
                lines, index, path, kind, name, targets, retain_malformed
            )
            if failure is not None:
                failures.append(failure)
            if block is not None:
                key = (kind.casefold(), name.casefold())
                if key in effective:
                    del effective[key]
                effective[key] = block
            index = max(next_index, index + 1)
    return ParseResult(tuple(effective.values()), tuple(failures))


def typed_fields(
    assignments: Iterable[CookAssignment], defines: Mapping[str, object]
) -> dict[str, object]:
    spelling: OrderedDict[str, str] = OrderedDict()
    grouped: dict[str, list[object]] = {}
    for assignment in assignments:
        folded = assignment.key.casefold()
        spelling.setdefault(folded, assignment.key)
        grouped.setdefault(folded, []).append(_value(assignment.value, defines))
    return {
        spelling[key]: values[0] if len(values) == 1 else values
        for key, values in grouped.items()
    }


def folded_defines(bundle: Mapping[str, Any]) -> dict[str, object]:
    defines = bundle.get("defines", {})
    if not isinstance(defines, Mapping):
        raise ValueError("bundle defines must be an object")
    return {str(key).casefold(): value for key, value in defines.items()}


def append_diagnostic(
    bundle: dict[str, Any], block: str, message: str
) -> None:
    diagnostics = bundle.setdefault("diagnostics", [])
    if not isinstance(diagnostics, list):
        raise ValueError("bundle diagnostics must be an array")
    diagnostics.append({"template": block, "message": message})
