"""Bounded textual include expansion for SAGE blocks that cross file edges."""

from __future__ import annotations

from collections.abc import Mapping
import re

from ..sage_cst import SageIncludeError, normalize_virtual_path


_INCLUDE = re.compile(
    r'^\s*#include\s+(?:"(?P<quoted>[^"]+)"|<(?P<angled>[^>]+)>)\s*(?:(?://|;).*)?$',
    re.IGNORECASE,
)
MAX_EXPANDED_BYTES = 128 * 1024 * 1024
MAX_INCLUDE_DEPTH = 64
MAX_INCLUDES = 8_192


def expand_textual_includes(entry_virtual_path: str, sources: Mapping[str, bytes]) -> bytes:
    """Expand caller-owned includes before parsing, with the CST's path rules.

    Most SAGE includes are balanced body fragments and the ordinary CST keeps
    their per-file provenance. Retail Create-a-Hero animation includes are a
    genuine textual edge: an ``End`` in one file closes a ``Draw`` opened by
    its parent. This bounded fallback preserves that authored grammar without
    reading outside the supplied document set.
    """

    normalized_sources: dict[str, bytes] = {}
    folded: dict[str, list[str]] = {}
    for raw_path, source in sources.items():
        path = normalize_virtual_path(raw_path)
        if path in normalized_sources and normalized_sources[path] != source:
            raise SageIncludeError(f"duplicate normalized document path: {path!r}")
        normalized_sources[path] = source
        folded.setdefault(path.casefold(), []).append(path)

    def lookup(candidate: str, context: str) -> str | None:
        matches = tuple(dict.fromkeys(folded.get(candidate.casefold(), ())))
        if len(matches) > 1:
            raise SageIncludeError(
                f"case-ambiguous include {context}: {', '.join(sorted(matches))}"
            )
        return matches[0] if matches else None

    entry = lookup(normalize_virtual_path(entry_virtual_path), entry_virtual_path)
    if entry is None:
        raise SageIncludeError(f"missing entry document: {entry_virtual_path}")

    include_count = 0
    expanded_bytes = 0

    def expand(path: str, stack: tuple[str, ...]) -> bytes:
        nonlocal include_count, expanded_bytes
        if len(stack) >= MAX_INCLUDE_DEPTH:
            raise SageIncludeError(
                f"SAGE include depth exceeds {MAX_INCLUDE_DEPTH} limit"
            )
        if path.casefold() in {item.casefold() for item in stack}:
            raise SageIncludeError("include cycle detected: " + " -> ".join((*stack, path)))
        output = bytearray()
        source = normalized_sources[path]
        for raw_line in source.splitlines(keepends=True):
            text = raw_line.decode("cp1252")
            match = _INCLUDE.fullmatch(text.rstrip("\r\n"))
            if match is None:
                output.extend(raw_line)
                continue
            include_count += 1
            if include_count > MAX_INCLUDES:
                raise SageIncludeError(
                    f"SAGE include count exceeds {MAX_INCLUDES} limit"
                )
            operand = (match.group("quoted") or match.group("angled")).replace("\\", "/")
            relative = normalize_virtual_path(operand, base_virtual_path=path)
            try:
                rooted = normalize_virtual_path(operand)
            except SageIncludeError:
                rooted = relative
            candidates = tuple(
                dict.fromkeys(
                    value
                    for value in (
                        lookup(relative, operand),
                        lookup(rooted, operand),
                    )
                    if value is not None
                )
            )
            if not candidates:
                raise SageIncludeError(f"missing include {operand!r} from {path}")
            if len(candidates) > 1:
                raise SageIncludeError(
                    f"ambiguous relative/root include {operand!r} from {path}"
                )
            nested = expand(candidates[0], (*stack, path))
            output.extend(nested)
            if nested and not nested.endswith((b"\n", b"\r")):
                output.extend(b"\n")
        expanded_bytes += len(output)
        if expanded_bytes > MAX_EXPANDED_BYTES:
            raise SageIncludeError(
                f"expanded SAGE source exceeds {MAX_EXPANDED_BYTES} byte limit"
            )
        return bytes(output)

    return expand(entry, ())
