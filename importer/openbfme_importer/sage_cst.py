"""Bounded, include-aware concrete syntax for SAGE object INI documents.

The importer needs more structure than :mod:`sage_ini` exposes when following
visual and lifecycle dependencies.  This module deliberately remains a syntax
reader, not a SAGE interpreter: callers provide every virtual document, include
resolution never touches the host filesystem, and inheritance/module semantics
are left to later typed passes.

Only ``Object``, ``ChildObject`` and ``ObjectReskin`` top-level definitions are
materialized.  Their bodies retain assignments and nested blocks in source
order.  Assignment ``ordinal`` values are zero-based within their containing
scope; ``key_ordinal`` is the zero-based occurrence of that case-insensitive key.

A top-level ``Object`` header may carry a second token which SAGE treats as a
parent template (retail ``Object DwarvenFortressMightyCatapult DwarvenCatapult``)
or, in demo-only leftovers, as an editor annotation (``Object Water Plane``,
``Object MoriaDebrisPileA (Rocks)``).  This reader records that token as
``parent`` exactly like the flat :mod:`sage_ini` reader and leaves its meaning
to later typed passes.  A line opening with an Object-family keyword that fits
no header shape fails closed instead of being silently skipped.

Retail data occasionally declares an empty module or state block with no body
and no terminating ``End``.  Such headers are only syntactically
indistinguishable from real block openers, so when an enclosing scope fails to
close, the most recent syntactically-guessed block is demoted back to a plain
assignment and parsing resumes; name-known block kinds still fail closed.
"""

from __future__ import annotations

from dataclasses import dataclass, replace
import posixpath
import re
from collections.abc import Iterable, Mapping
from typing import TypeAlias


MAX_SOURCE_BYTES = 16 * 1024 * 1024
MAX_TOTAL_BYTES = 128 * 1024 * 1024
MAX_DEPTH = 128
MAX_INCLUDE_DEPTH = 64
MAX_NODES = 250_000
MAX_ASSIGNMENTS = 1_000_000
MAX_INCLUDES = 8_192
MAX_PATH_CHARS = 1_024


class SageCstError(ValueError):
    """Base class for bounded SAGE CST failures."""


class SageCstSyntaxError(SageCstError):
    """The source cannot be represented without guessing at its grammar."""


class SageCstLimitError(SageCstError):
    """A configured resource bound was exceeded."""


class SageIncludeError(SageCstError):
    """A caller-supplied include graph is unsafe or cannot be resolved."""


@dataclass(frozen=True, slots=True)
class SageCstLimits:
    max_source_bytes: int = MAX_SOURCE_BYTES
    max_total_bytes: int = MAX_TOTAL_BYTES
    max_depth: int = MAX_DEPTH
    max_include_depth: int = MAX_INCLUDE_DEPTH
    max_nodes: int = MAX_NODES
    max_assignments: int = MAX_ASSIGNMENTS
    max_includes: int = MAX_INCLUDES
    max_path_chars: int = MAX_PATH_CHARS

    def __post_init__(self) -> None:
        for field_name in self.__dataclass_fields__:
            if getattr(self, field_name) < 1:
                raise ValueError(f"{field_name} must be positive")


DEFAULT_LIMITS = SageCstLimits()


@dataclass(frozen=True, slots=True)
class SageSourceLocation:
    virtual_path: str
    line: int


@dataclass(frozen=True, slots=True)
class SageAssignment:
    key: str
    value: str
    ordinal: int
    key_ordinal: int
    item_ordinal: int
    source_virtual_path: str
    line: int
    has_equals: bool = True

    @property
    def location(self) -> SageSourceLocation:
        return SageSourceLocation(self.source_virtual_path, self.line)


@dataclass(frozen=True, slots=True)
class SageScriptLine:
    """One uninterpreted statement inside a BeginScript/EndScript body."""

    text: str
    indent: int
    ordinal: int
    source_virtual_path: str
    line: int

    @property
    def location(self) -> SageSourceLocation:
        return SageSourceLocation(self.source_virtual_path, self.line)


@dataclass(frozen=True, slots=True)
class SageScript:
    """Opaque script body retained without applying SAGE block grammar."""

    lines: tuple[SageScriptLine, ...]
    item_ordinal: int
    source_virtual_path: str
    line: int
    end_line: int
    raw_header: str = "BeginScript"

    @property
    def location(self) -> SageSourceLocation:
        return SageSourceLocation(self.source_virtual_path, self.line)

    @property
    def end_location(self) -> SageSourceLocation:
        return SageSourceLocation(self.source_virtual_path, self.end_line)


@dataclass(frozen=True, slots=True)
class SageIncludeRef:
    raw_ref: str
    normalized_ref: str
    relative_virtual_path: str
    ordinal: int
    source_virtual_path: str
    line: int
    resolved_virtual_path: str | None = None

    @property
    def location(self) -> SageSourceLocation:
        return SageSourceLocation(self.source_virtual_path, self.line)

    @property
    def virtual_path(self) -> str:
        """Best available safe virtual target path."""

        return self.resolved_virtual_path or self.relative_virtual_path


@dataclass(frozen=True, slots=True)
class SageBlock:
    """A nested SAGE block or module.

    ``header_key`` is the left side of an assignment-shaped header.  For module
    carriers such as ``Draw`` or ``Behavior``, ``kind`` is the module class and
    ``instance_tag`` is its optional second token.  For state blocks, ``kind``
    is the state family and ``header_tokens`` holds the conditions.
    """

    kind: str
    instance_tag: str | None
    header_key: str | None
    header_tokens: tuple[str, ...]
    model_condition_tokens: tuple[str, ...]
    items: "tuple[SageAssignment | SageBlock | SageScript | SageIncludeRef, ...]"
    item_ordinal: int
    source_virtual_path: str
    line: int
    raw_header: str

    @property
    def location(self) -> SageSourceLocation:
        return SageSourceLocation(self.source_virtual_path, self.line)

    @property
    def module_kind(self) -> str:
        return self.kind

    @property
    def assignments(self) -> tuple[SageAssignment, ...]:
        return tuple(item for item in self.items if isinstance(item, SageAssignment))

    @property
    def blocks(self) -> tuple["SageBlock", ...]:
        return tuple(item for item in self.items if isinstance(item, SageBlock))

    @property
    def scripts(self) -> tuple[SageScript, ...]:
        return tuple(item for item in self.items if isinstance(item, SageScript))

    @property
    def includes(self) -> tuple[SageIncludeRef, ...]:
        return tuple(item for item in self.items if isinstance(item, SageIncludeRef))

    def values(self, key: str) -> tuple[str, ...]:
        folded = key.casefold()
        return tuple(item.value for item in self.assignments if item.key.casefold() == folded)


SageBodyItem: TypeAlias = SageAssignment | SageBlock | SageScript | SageIncludeRef


@dataclass(frozen=True, slots=True)
class SageObject:
    kind: str
    name: str
    parent: str | None
    items: tuple[SageBodyItem, ...]
    ordinal: int
    source_virtual_path: str
    line: int

    @property
    def location(self) -> SageSourceLocation:
        return SageSourceLocation(self.source_virtual_path, self.line)

    @property
    def assignments(self) -> tuple[SageAssignment, ...]:
        return tuple(item for item in self.items if isinstance(item, SageAssignment))

    @property
    def blocks(self) -> tuple[SageBlock, ...]:
        return tuple(item for item in self.items if isinstance(item, SageBlock))

    @property
    def scripts(self) -> tuple[SageScript, ...]:
        return tuple(item for item in self.items if isinstance(item, SageScript))

    @property
    def includes(self) -> tuple[SageIncludeRef, ...]:
        return tuple(item for item in self.items if isinstance(item, SageIncludeRef))

    def values(self, key: str, *, recursive: bool = False) -> tuple[str, ...]:
        folded = key.casefold()
        values: list[str] = []

        def visit(items: tuple[SageBodyItem, ...]) -> None:
            for item in items:
                if isinstance(item, SageAssignment):
                    if item.key.casefold() == folded:
                        values.append(item.value)
                elif isinstance(item, SageBlock) and recursive:
                    visit(item.items)
                elif isinstance(item, (SageBlock, SageScript, SageIncludeRef)):
                    continue
                else:
                    raise TypeError(
                        f"unexpected SAGE body item in Object {self.name}: "
                        f"{type(item).__name__}"
                    )

        visit(self.items)
        return tuple(values)

SageTopLevelItem: TypeAlias = SageIncludeRef | SageObject


@dataclass(frozen=True, slots=True)
class SageDocument:
    virtual_path: str
    items: tuple[SageTopLevelItem, ...]

    @property
    def includes(self) -> tuple[SageIncludeRef, ...]:
        return tuple(item for item in self.items if isinstance(item, SageIncludeRef))

    @property
    def objects(self) -> tuple[SageObject, ...]:
        return tuple(item for item in self.items if isinstance(item, SageObject))


@dataclass(frozen=True, slots=True)
class SageBodyFragment:
    """A caller-supplied include document parsed as one balanced body scope."""

    virtual_path: str
    items: tuple[SageBodyItem, ...]

    @property
    def includes(self) -> tuple[SageIncludeRef, ...]:
        return tuple(item for item in self.items if isinstance(item, SageIncludeRef))


@dataclass(frozen=True, slots=True)
class ResolvedSageCst:
    entry_virtual_path: str
    documents: tuple[SageDocument, ...]
    objects: tuple[SageObject, ...]
    includes: tuple[SageIncludeRef, ...]
    fragments: tuple[SageBodyFragment, ...] = ()

    def objects_named(self, name: str) -> tuple[SageObject, ...]:
        folded = name.casefold()
        return tuple(item for item in self.objects if item.name.casefold() == folded)


_OBJECT_HEADER = re.compile(
    r"^(Object|ChildObject|ObjectReskin)\s+(\S+)(?:\s+(\S+))?\s*$",
    re.IGNORECASE,
)
_OBJECT_KEYWORD = re.compile(r"^(?:Object|ChildObject|ObjectReskin)\b", re.IGNORECASE)
_INCLUDE_DIRECTIVE = re.compile(r"^#\s*include\s+(.+?)\s*$", re.IGNORECASE)
_BEGIN_SCRIPT = re.compile(r"^BeginScript\s*$", re.IGNORECASE)
_END_SCRIPT = re.compile(r"^EndScript\s*$", re.IGNORECASE)
_DRIVE_PREFIX = re.compile(r"^[A-Za-z]:")
_MODULE_TAG = re.compile(r"^ModuleTag(?:_|$)", re.IGNORECASE)

# Assignment-shaped declarations that SAGE terminates with End even if a file
# does not use indentation.  The module carrier list is intentionally syntactic;
# it does not assign runtime meaning to the module class.
_MODULE_CARRIERS = frozenset(
    {
        "behavior",
        "body",
        "clientbehavior",
        "clientupdate",
        "draw",
        "flasher",
    }
)
_STATE_BLOCK_KEYS = frozenset(
    {
        "animation",
        "animationstate",
        "armorset",
        "conditionstate",
        "lodoptions",
        "locomotorset",
        "meleebehavior",
        "modelconditionstate",
        # Retail UpgradeSoundSelectorClientBehavior modules nest End-terminated
        # SoundUpgrade blocks (voice overrides gated on an upgrade).
        "soundupgrade",
        "soundstate",
        # RotWK 2.01 units carry an End-terminated auto-resolve threat
        # breakdown (``ThreatBreakdown = X`` wrapping ``AIKindOf``), which is
        # assignment-shaped and appears in 31 shipped object files.
        "threatbreakdown",
        "transitionstate",
        "weaponset",
    }
)
#: Assignment values that name an End-terminated block type instead of a scalar
#: (``Radius = FCurve`` in object/system/system.ini's PartTheHeavensUpdate).
_BLOCK_VALUED_TYPES = frozenset({"fcurve"})
_BARE_BLOCK_KINDS = frozenset(
    {
        "defaultmodelconditionstate",
        "idleanimationstate",
        # Retail files contain LocomotorSet blocks without ``=`` and some use
        # inconsistent mixed tab/space indentation, so indentation inference
        # alone cannot safely identify their terminating End.
        "locomotorset",
    }
)


@dataclass(frozen=True, slots=True)
class _Line:
    text: str
    indent: int
    number: int


@dataclass(slots=True)
class _Budget:
    limits: SageCstLimits
    total_bytes: int = 0
    nodes: int = 0
    assignments: int = 0
    includes: int = 0

    def add_source(self, size: int, virtual_path: str) -> None:
        if size > self.limits.max_source_bytes:
            raise SageCstLimitError(
                f"SAGE source {virtual_path!r} exceeds {self.limits.max_source_bytes} byte limit"
            )
        self.total_bytes += size
        if self.total_bytes > self.limits.max_total_bytes:
            raise SageCstLimitError(
                f"SAGE source graph exceeds {self.limits.max_total_bytes} total byte limit"
            )

    def add_node(self) -> None:
        self.nodes += 1
        if self.nodes > self.limits.max_nodes:
            raise SageCstLimitError(f"SAGE node count exceeds {self.limits.max_nodes} limit")

    def add_assignment(self) -> None:
        self.assignments += 1
        if self.assignments > self.limits.max_assignments:
            raise SageCstLimitError(
                f"SAGE assignment count exceeds {self.limits.max_assignments} limit"
            )

    def add_include(self) -> None:
        self.includes += 1
        if self.includes > self.limits.max_includes:
            raise SageCstLimitError(
                f"SAGE include count exceeds {self.limits.max_includes} limit"
            )


def strip_sage_comments(raw: str) -> str:
    """Strip ``;`` and ``//`` comments occurring outside quoted strings."""

    quote: str | None = None
    escaped = False
    index = 0
    while index < len(raw):
        character = raw[index]
        if quote is not None:
            if escaped:
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == quote:
                quote = None
        elif character in {'"', "'"}:
            quote = character
        elif character == ";":
            return raw[:index].rstrip()
        elif character == "/" and index + 1 < len(raw) and raw[index + 1] == "/":
            return raw[:index].rstrip()
        index += 1
    return raw.rstrip()


def normalize_virtual_path(
    path: str,
    *,
    base_virtual_path: str | None = None,
    max_path_chars: int = MAX_PATH_CHARS,
) -> str:
    """Return a safe, slash-normalized relative virtual path.

    ``base_virtual_path`` is a document path, not a directory.  Parent segments
    may traverse within its virtual root but may never escape that root.
    """

    if not isinstance(path, str):
        raise SageIncludeError("virtual path must be text")
    if "\0" in path:
        raise SageIncludeError("virtual path contains a NUL character")
    candidate = path.strip().replace("\\", "/")
    if not candidate:
        raise SageIncludeError("virtual path is empty")
    if len(candidate) > max_path_chars:
        raise SageCstLimitError(f"virtual path exceeds {max_path_chars} character limit")
    if candidate.startswith("/") or candidate.startswith("//") or _DRIVE_PREFIX.match(candidate):
        raise SageIncludeError(f"unsafe absolute virtual path: {path!r}")

    if base_virtual_path is not None:
        base = normalize_virtual_path(base_virtual_path, max_path_chars=max_path_chars)
        candidate = posixpath.join(posixpath.dirname(base), candidate)

    parts: list[str] = []
    for part in candidate.split("/"):
        if part in {"", "."}:
            continue
        if part == "..":
            if not parts:
                raise SageIncludeError(f"unsafe escaping virtual path: {path!r}")
            parts.pop()
            continue
        if ":" in part:
            raise SageIncludeError(f"unsafe virtual path segment: {part!r}")
        parts.append(part)
    if not parts:
        raise SageIncludeError("virtual path normalizes to an empty path")
    normalized = "/".join(parts)
    if len(normalized) > max_path_chars:
        raise SageCstLimitError(f"virtual path exceeds {max_path_chars} character limit")
    return normalized


def _decode_source(source: bytes, virtual_path: str, budget: _Budget) -> str:
    if not isinstance(source, bytes):
        raise TypeError(f"SAGE source {virtual_path!r} must be bytes")
    budget.add_source(len(source), virtual_path)
    if b"\0" in source:
        raise SageCstSyntaxError(f"SAGE source {virtual_path!r} contains a NUL byte")
    try:
        return source.decode("cp1252")
    except UnicodeDecodeError as exc:  # CP1252 rejects only five undefined bytes.
        raise SageCstSyntaxError(f"SAGE source {virtual_path!r} is not valid CP1252") from exc


def _indent_width(raw: str) -> int:
    width = 0
    for character in raw:
        if character == " ":
            width += 1
        elif character == "\t":
            width += 4 - (width % 4)
        else:
            break
    return width


def _lex_lines(text: str) -> list[_Line]:
    lines: list[_Line] = []
    for number, raw in enumerate(text.splitlines(), start=1):
        stripped_comment = strip_sage_comments(raw)
        if not stripped_comment.strip():
            continue
        lines.append(_Line(stripped_comment.strip(), _indent_width(stripped_comment), number))
    return lines


def _split_header_tokens(value: str, *, virtual_path: str, line: int) -> tuple[str, ...]:
    tokens: list[str] = []
    token: list[str] = []
    quote: str | None = None
    escaped = False
    for character in value:
        if quote is not None:
            if escaped:
                token.append(character)
                escaped = False
            elif character == "\\":
                escaped = True
            elif character == quote:
                quote = None
            else:
                token.append(character)
        elif character in {'"', "'"}:
            quote = character
        elif character.isspace():
            if token:
                tokens.append("".join(token))
                token = []
        else:
            token.append(character)
    if escaped:
        token.append("\\")
    if quote is not None:
        raise SageCstSyntaxError(f"unterminated quote at {virtual_path}:{line}")
    if token:
        tokens.append("".join(token))
    return tuple(tokens)


def _parse_include_operand(operand: str, *, virtual_path: str, line: int) -> str:
    operand = operand.strip()
    if not operand:
        raise SageCstSyntaxError(f"empty include directive at {virtual_path}:{line}")
    if operand[0] in {'"', "'"}:
        quote = operand[0]
        escaped = False
        result: list[str] = []
        closing_index: int | None = None
        for index, character in enumerate(operand[1:], start=1):
            if escaped:
                result.append(character)
                escaped = False
            elif character == "\\":
                # Backslashes are path separators unless they quote the active
                # quote or another backslash.
                following = operand[index + 1] if index + 1 < len(operand) else ""
                if following in {quote, "\\"}:
                    escaped = True
                else:
                    result.append(character)
            elif character == quote:
                closing_index = index
                break
            else:
                result.append(character)
        if closing_index is None:
            raise SageCstSyntaxError(f"unterminated include quote at {virtual_path}:{line}")
        if operand[closing_index + 1 :].strip():
            raise SageCstSyntaxError(f"trailing include tokens at {virtual_path}:{line}")
        return "".join(result)
    if any(character.isspace() for character in operand):
        raise SageCstSyntaxError(f"unquoted include path contains whitespace at {virtual_path}:{line}")
    return operand


def _make_include_ref(
    operand: str,
    *,
    virtual_path: str,
    line: int,
    ordinal: int,
    budget: _Budget,
) -> SageIncludeRef:
    raw_ref = _parse_include_operand(
        operand, virtual_path=virtual_path, line=line
    )
    relative_path = normalize_virtual_path(
        raw_ref,
        base_virtual_path=virtual_path,
        max_path_chars=budget.limits.max_path_chars,
    )
    try:
        normalized_ref = normalize_virtual_path(
            raw_ref, max_path_chars=budget.limits.max_path_chars
        )
    except SageIncludeError:
        # A leading parent segment is invalid as a virtual-root path but can be
        # valid relative to the including document.  Relative normalization
        # above already proved that it remains contained.
        if not raw_ref.replace("\\", "/").startswith("../"):
            raise
        normalized_ref = relative_path
    budget.add_include()
    budget.add_node()
    return SageIncludeRef(
        raw_ref=raw_ref,
        normalized_ref=normalized_ref,
        relative_virtual_path=relative_path,
        ordinal=ordinal,
        source_virtual_path=virtual_path,
        line=line,
    )


def _assignment_parts(text: str) -> tuple[str, str] | None:
    if "=" not in text:
        return None
    key, value = (part.strip() for part in text.split("=", 1))
    if not key or any(character.isspace() for character in key):
        return None
    return key, value


def _looks_like_module(key: str, tokens: tuple[str, ...]) -> bool:
    if key.casefold() in _MODULE_CARRIERS:
        return True
    return len(tokens) >= 2 and bool(_MODULE_TAG.match(tokens[1]))


def _assignment_is_block(
    key: str,
    tokens: tuple[str, ...],
    line: _Line | None = None,
    following: _Line | None = None,
) -> bool:
    folded = key.casefold()
    if (
        folded in _STATE_BLOCK_KEYS
        or (folded == "addemotion" and bool(tokens) and tokens[0].casefold() == "override")
        or _looks_like_module(key, tokens)
    ):
        return True
    # Retail authors a handful of assignment-shaped sub-blocks whose *value*
    # names the block type rather than the key: the animation curves inside
    # PartTheHeavensUpdate (``Radius = FCurve``, ``Opacity = FCurve``,
    # ``Angle = FCurve``) carry End-terminated ``Key = T:.. V:..`` bodies.  The
    # value is the only deterministic signal; a generic indentation guess was
    # measured to misfire on retail whitespace noise and is deliberately not
    # used here.
    return len(tokens) == 1 and tokens[0].casefold() in _BLOCK_VALUED_TYPES


def _bare_is_block(tokens: tuple[str, ...], line: _Line, following: _Line | None) -> bool:
    if tokens and tokens[0].casefold() in (_BARE_BLOCK_KINDS | _STATE_BLOCK_KEYS):
        return True
    # RotWK authors two UnitSpecificSounds bodies at the same indentation as
    # the header (AngmarForgeWorks, MordorEasterling), which the generic
    # greater-indent inference below cannot see.  The block is always
    # End-terminated in SAGE, so a following body line at any indent admits
    # it.  A bare header directly followed by End (the default object.ini
    # template) stays on the existing ambiguous fail-closed path.
    if (
        len(tokens) == 1
        and tokens[0].casefold() == "unitspecificsounds"
        and following is not None
        and following.text.casefold() != "end"
    ):
        return True
    return following is not None and following.text.casefold() != "end" and following.indent > line.indent


def _make_block_header(
    text: str,
    *,
    virtual_path: str,
    line: int,
) -> tuple[str, str | None, str | None, tuple[str, ...], tuple[str, ...]]:
    assignment = _assignment_parts(text)
    if assignment is None:
        tokens = _split_header_tokens(text, virtual_path=virtual_path, line=line)
        if not tokens:
            raise SageCstSyntaxError(f"empty block header at {virtual_path}:{line}")
        return tokens[0], None, None, tokens[1:], ()

    key, value = assignment
    tokens = _split_header_tokens(value, virtual_path=virtual_path, line=line)
    folded = key.casefold()
    if _looks_like_module(key, tokens):
        if not tokens:
            raise SageCstSyntaxError(f"module header lacks a kind at {virtual_path}:{line}")
        instance_tag = tokens[1] if len(tokens) >= 2 else None
        return tokens[0], instance_tag, key, tokens, ()
    conditions = tokens if folded == "modelconditionstate" else ()
    return key, None, key, tokens, conditions


def _parse_script(
    lines: list[_Line],
    start: int,
    *,
    virtual_path: str,
    budget: _Budget,
    begin_line: _Line,
) -> tuple[tuple[SageScriptLine, ...], int, int]:
    statements: list[SageScriptLine] = []
    index = start
    while index < len(lines):
        line = lines[index]
        if _END_SCRIPT.fullmatch(line.text):
            return tuple(statements), index + 1, line.number
        if _BEGIN_SCRIPT.fullmatch(line.text):
            raise SageCstSyntaxError(
                f"nested BeginScript at {virtual_path}:{line.number}; "
                f"script began at line {begin_line.number}"
            )
        budget.add_node()
        statements.append(
            SageScriptLine(
                text=line.text,
                indent=line.indent,
                ordinal=len(statements),
                source_virtual_path=virtual_path,
                line=line.number,
            )
        )
        index += 1
    raise SageCstSyntaxError(
        f"unterminated BeginScript at {virtual_path}:{begin_line.number}"
    )


def _parse_scope(
    lines: list[_Line],
    start: int,
    *,
    virtual_path: str,
    depth: int,
    budget: _Budget,
    owner: str,
    require_end: bool = True,
    allow_includes: bool = False,
) -> tuple[tuple[SageBodyItem, ...], int]:
    if depth > budget.limits.max_depth:
        raise SageCstLimitError(f"SAGE nesting depth exceeds {budget.limits.max_depth} limit")

    items: list[SageBodyItem] = []
    assignment_ordinal = 0
    include_ordinal = 0
    key_ordinals: dict[str, int] = {}
    index = start
    # Retail objects occasionally declare an empty module or state block with
    # no body and no terminating End (for example
    # ``Draw = W3DScriptedModelDraw ModuleTag_DRAW`` in the BFME2 1.06
    # create-a-hero object).  Such a header is syntactically indistinguishable
    # from a real block opener until the enclosing scope fails to find its own
    # End.  ``last_flippable`` records the most recent block that was opened on
    # syntactic evidence alone so a scope failure can demote that header to a
    # plain assignment and resume, instead of misreporting the whole object.
    last_flippable: tuple[int, int, tuple[int, int, int], str, str, _Line, bool] | None = None

    def try_flip() -> bool:
        nonlocal index, assignment_ordinal, last_flippable
        if last_flippable is None or last_flippable[0] != len(items) - 1:
            return False
        _, header_index, snapshot, key, value, header_line, has_equals = last_flippable
        budget.nodes, budget.assignments, budget.includes = snapshot
        items.pop()
        folded = key.casefold()
        key_ordinal = key_ordinals.get(folded, 0)
        key_ordinals[folded] = key_ordinal + 1
        budget.add_assignment()
        items.append(
            SageAssignment(
                key=key,
                value=value,
                ordinal=assignment_ordinal,
                key_ordinal=key_ordinal,
                item_ordinal=len(items),
                source_virtual_path=virtual_path,
                line=header_line.number,
                has_equals=has_equals,
            )
        )
        assignment_ordinal += 1
        index = header_index + 1
        last_flippable = None
        return True

    while True:
        if index >= len(lines):
            if not require_end:
                return tuple(items), index
            if try_flip():
                continue
            raise SageCstSyntaxError(f"unterminated {owner} at end of {virtual_path}")
        line = lines[index]
        if line.text.casefold() == "end":
            if require_end:
                return tuple(items), index + 1
            raise SageCstSyntaxError(
                f"stray End in {owner} at {virtual_path}:{line.number}"
            )
        if _END_SCRIPT.fullmatch(line.text):
            raise SageCstSyntaxError(
                f"stray EndScript at {virtual_path}:{line.number}"
            )
        if _BEGIN_SCRIPT.fullmatch(line.text):
            budget.add_node()
            script_lines, index, end_line = _parse_script(
                lines,
                index + 1,
                virtual_path=virtual_path,
                budget=budget,
                begin_line=line,
            )
            items.append(
                SageScript(
                    lines=script_lines,
                    item_ordinal=len(items),
                    source_virtual_path=virtual_path,
                    line=line.number,
                    end_line=end_line,
                    raw_header=line.text,
                )
            )
            continue
        if _OBJECT_HEADER.fullmatch(line.text):
            if not require_end:
                raise SageCstSyntaxError(
                    f"unexpected object header in {owner} at "
                    f"{virtual_path}:{line.number}"
                )
            if try_flip():
                continue
            raise SageCstSyntaxError(
                f"unterminated {owner} before object header at {virtual_path}:{line.number}"
            )
        include_match = _INCLUDE_DIRECTIVE.fullmatch(line.text)
        if include_match:
            if not allow_includes:
                raise SageCstSyntaxError(
                    f"include directive inside object scope at "
                    f"{virtual_path}:{line.number}"
                )
            items.append(
                _make_include_ref(
                    include_match.group(1),
                    virtual_path=virtual_path,
                    line=line.number,
                    ordinal=include_ordinal,
                    budget=budget,
                )
            )
            include_ordinal += 1
            index += 1
            continue

        following = lines[index + 1] if index + 1 < len(lines) else None
        assignment = _assignment_parts(line.text)
        if assignment is not None:
            key, value = assignment
            tokens = _split_header_tokens(value, virtual_path=virtual_path, line=line.number)
            if _assignment_is_block(key, tokens, line, following):
                kind, instance_tag, header_key, header_tokens, conditions = _make_block_header(
                    line.text, virtual_path=virtual_path, line=line.number
                )
                snapshot = (budget.nodes, budget.assignments, budget.includes)
                budget.add_node()
                try:
                    nested, next_index = _parse_scope(
                        lines,
                        index + 1,
                        virtual_path=virtual_path,
                        depth=depth + 1,
                        budget=budget,
                        owner=f"{kind} block",
                        allow_includes=allow_includes,
                    )
                except SageCstSyntaxError:
                    # The block can never close before its enclosing scope ends
                    # (an End-less empty module declaration), so retain the
                    # header as a plain assignment.
                    budget.nodes, budget.assignments, budget.includes = snapshot
                else:
                    last_flippable = (
                        len(items), index, snapshot, key, value, line, True
                    )
                    items.append(
                        SageBlock(
                            kind=kind,
                            instance_tag=instance_tag,
                            header_key=header_key,
                            header_tokens=header_tokens,
                            model_condition_tokens=conditions,
                            items=nested,
                            item_ordinal=len(items),
                            source_virtual_path=virtual_path,
                            line=line.number,
                            raw_header=line.text,
                        )
                    )
                    index = next_index
                    continue

            folded = key.casefold()
            key_ordinal = key_ordinals.get(folded, 0)
            key_ordinals[folded] = key_ordinal + 1
            budget.add_assignment()
            items.append(
                SageAssignment(
                    key=key,
                    value=value,
                    ordinal=assignment_ordinal,
                    key_ordinal=key_ordinal,
                    item_ordinal=len(items),
                    source_virtual_path=virtual_path,
                    line=line.number,
                )
            )
            assignment_ordinal += 1
            index += 1
            continue

        tokens = _split_header_tokens(line.text, virtual_path=virtual_path, line=line.number)
        bare_named_block = bool(tokens) and tokens[0].casefold() in (
            _BARE_BLOCK_KINDS | _STATE_BLOCK_KEYS
        )
        if _bare_is_block(tokens, line, following):
            kind, instance_tag, header_key, header_tokens, conditions = _make_block_header(
                line.text, virtual_path=virtual_path, line=line.number
            )
            snapshot = (budget.nodes, budget.assignments, budget.includes)
            budget.add_node()
            nested, next_index = _parse_scope(
                lines,
                index + 1,
                virtual_path=virtual_path,
                depth=depth + 1,
                budget=budget,
                owner=f"{kind} block",
                allow_includes=allow_includes,
            )
            if not bare_named_block:
                # An indentation-inferred block kind is a guess; allow a scope
                # failure to demote it.  Name-known kinds must fail loudly.
                last_flippable = (
                    len(items), index, snapshot, tokens[0], " ".join(tokens[1:]), line, False
                )
            items.append(
                SageBlock(
                    kind=kind,
                    instance_tag=instance_tag,
                    header_key=header_key,
                    header_tokens=header_tokens,
                    model_condition_tokens=conditions,
                    items=nested,
                    item_ordinal=len(items),
                    source_virtual_path=virtual_path,
                    line=line.number,
                    raw_header=line.text,
                )
            )
            index = next_index
            continue

        if following is not None and following.text.casefold() == "end" and len(tokens) == 1:
            raise SageCstSyntaxError(
                f"ambiguous bare statement or empty block at {virtual_path}:{line.number}: {line.text!r}"
            )
        if not tokens:
            raise SageCstSyntaxError(f"empty statement at {virtual_path}:{line.number}")
        key, value = tokens[0], " ".join(tokens[1:])
        folded = key.casefold()
        key_ordinal = key_ordinals.get(folded, 0)
        key_ordinals[folded] = key_ordinal + 1
        budget.add_assignment()
        items.append(
            SageAssignment(
                key=key,
                value=value,
                ordinal=assignment_ordinal,
                key_ordinal=key_ordinal,
                item_ordinal=len(items),
                source_virtual_path=virtual_path,
                line=line.number,
                has_equals=False,
            )
        )
        assignment_ordinal += 1
        index += 1


def _skip_foreign_top_level(
    lines: list[_Line],
    start: int,
    *,
    virtual_path: str,
) -> int:
    """Skip one non-Object top-level SAGE declaration and return the next index.

    Retail INIs freely mix Object definitions with other top-level families
    (``FXParticleSystem``, ``FXList``, ``Armor``, ``CommandButton``,
    ``AIData``...).  Those families are ``End``-terminated and may nest their
    own ``End``-terminated sub-blocks, for example the ``System`` /
    ``Color = DefaultColor`` / ``Update = DefaultUpdate`` sub-blocks inside an
    ``FXParticleSystem``.  Advancing one line at a time therefore leaks those
    inner ``End`` tokens into the top level, where they were misreported as a
    stray ``End`` (the historic
    ``object/cinematic/cinematicobjects.ini:1695`` failure, which is the
    ``System`` sub-block terminator, not an unbalanced file).

    This reader deliberately extracts no semantics from foreign families, so it
    only has to find where one ends.  Retail authors terminate a top-level
    declaration with an ``End`` at the declaration's own indent and indent every
    nested ``End`` further, so the region ends at the first ``End`` whose indent
    does not exceed the header's.  An Object-family header or include directive
    at that same outer indent also ends the region without being consumed, so a
    family whose ``End`` is absent or misindented can never swallow a real
    Object definition.
    """

    header = lines[start]
    index = start + 1
    while index < len(lines):
        line = lines[index]
        if line.indent > header.indent:
            index += 1
            continue
        if line.text.casefold() == "end":
            return index + 1
        if _OBJECT_HEADER.fullmatch(line.text) or _INCLUDE_DIRECTIVE.fullmatch(
            line.text
        ):
            return index
        index += 1
    return index


def parse_sage_document(
    source: bytes,
    virtual_path: str,
    *,
    limits: SageCstLimits = DEFAULT_LIMITS,
    _budget: _Budget | None = None,
) -> SageDocument:
    """Parse one caller-supplied object INI document without resolving includes."""

    normalized_path = normalize_virtual_path(
        virtual_path, max_path_chars=limits.max_path_chars
    )
    budget = _budget or _Budget(limits)
    text = _decode_source(source, normalized_path, budget)
    lines = _lex_lines(text)
    items: list[SageTopLevelItem] = []
    object_ordinal = 0
    include_ordinal = 0
    index = 0
    while index < len(lines):
        line = lines[index]
        include_match = _INCLUDE_DIRECTIVE.fullmatch(line.text)
        if include_match:
            items.append(
                _make_include_ref(
                    include_match.group(1),
                    virtual_path=normalized_path,
                    line=line.number,
                    ordinal=include_ordinal,
                    budget=budget,
                )
            )
            include_ordinal += 1
            index += 1
            continue

        object_match = _OBJECT_HEADER.fullmatch(line.text)
        if object_match:
            kind, name, parent = object_match.groups()
            if kind.casefold() != "object" and parent is None:
                raise SageCstSyntaxError(
                    f"{kind} header lacks a parent at {normalized_path}:{line.number}"
                )
            budget.add_node()
            body, index = _parse_scope(
                lines,
                index + 1,
                virtual_path=normalized_path,
                depth=1,
                budget=budget,
                owner=f"{kind} {name}",
                allow_includes=True,
            )
            items.append(
                SageObject(
                    kind=kind,
                    name=name,
                    parent=parent,
                    items=body,
                    ordinal=object_ordinal,
                    source_virtual_path=normalized_path,
                    line=line.number,
                )
            )
            object_ordinal += 1
            continue

        if _OBJECT_KEYWORD.match(line.text):
            raise SageCstSyntaxError(
                f"malformed Object-family header at {normalized_path}:{line.number}: "
                f"{line.text!r}"
            )
        if line.text.casefold() == "end":
            raise SageCstSyntaxError(f"stray End at {normalized_path}:{line.number}")
        if _BEGIN_SCRIPT.fullmatch(line.text) or _END_SCRIPT.fullmatch(line.text):
            raise SageCstSyntaxError(
                f"stray script delimiter at {normalized_path}:{line.number}: "
                f"{line.text}"
            )
        # Other top-level SAGE families are not object definitions and stay
        # outside this deliberately bounded CST, but they are End-terminated and
        # nest their own End-terminated sub-blocks.  Skipping the whole balanced
        # region keeps those inner End tokens from surfacing as a stray End at
        # the top level.  Preprocessor directives are single lines.
        if line.text.startswith("#"):
            index += 1
            continue
        index = _skip_foreign_top_level(
            lines, index, virtual_path=normalized_path
        )

    return SageDocument(normalized_path, tuple(items))


def parse_sage_body_fragment(
    source: bytes,
    virtual_path: str,
    *,
    limits: SageCstLimits = DEFAULT_LIMITS,
    _budget: _Budget | None = None,
) -> SageBodyFragment:
    """Parse one caller-supplied include file as a balanced body fragment."""

    normalized_path = normalize_virtual_path(
        virtual_path, max_path_chars=limits.max_path_chars
    )
    budget = _budget or _Budget(limits)
    text = _decode_source(source, normalized_path, budget)
    lines = _lex_lines(text)
    items, index = _parse_scope(
        lines,
        0,
        virtual_path=normalized_path,
        depth=1,
        budget=budget,
        owner="body fragment",
        require_end=False,
        allow_includes=True,
    )
    if index != len(lines):
        raise SageCstSyntaxError(
            f"body fragment parser stopped early at {normalized_path}:"
            f"{lines[index].number}"
        )
    return SageBodyFragment(normalized_path, items)


def _document_pairs(
    documents: Mapping[str, bytes] | Iterable[tuple[str, bytes]],
) -> list[tuple[str, bytes]]:
    return list(documents.items()) if isinstance(documents, Mapping) else list(documents)


def resolve_sage_documents(
    entry_virtual_path: str,
    documents: Mapping[str, bytes] | Iterable[tuple[str, bytes]],
    *,
    limits: SageCstLimits = DEFAULT_LIMITS,
) -> ResolvedSageCst:
    """Resolve an include graph solely from caller-supplied virtual documents.

    Includes first try the path relative to their source document, then the
    normalized virtual-root path.  If both identify different documents the
    include is rejected as ambiguous.  Matching is case-insensitive, while two
    supplied paths that differ only in case are rejected whenever referenced.
    Includes encountered inside an object or nested block are parsed as body
    fragments, retained as resolved evidence, and expanded inline without any
    filesystem access by this API.
    """

    pairs = _document_pairs(documents)
    normalized_sources: dict[str, bytes] = {}
    original_paths: dict[str, list[str]] = {}
    folded_index: dict[str, list[str]] = {}
    for supplied_path, source in pairs:
        normalized = normalize_virtual_path(
            supplied_path, max_path_chars=limits.max_path_chars
        )
        if not isinstance(source, bytes):
            raise TypeError(f"SAGE source {supplied_path!r} must be bytes")
        original_paths.setdefault(normalized, []).append(supplied_path)
        if normalized not in normalized_sources:
            normalized_sources[normalized] = source
        elif normalized_sources[normalized] != source or len(original_paths[normalized]) > 1:
            raise SageIncludeError(f"duplicate normalized document path: {normalized!r}")
        folded_index.setdefault(normalized.casefold(), []).append(normalized)

    def lookup(candidate: str, *, context: str) -> str | None:
        matches = tuple(dict.fromkeys(folded_index.get(candidate.casefold(), ())))
        originals = [path for match in matches for path in original_paths.get(match, ())]
        if len(set(originals)) > 1:
            raise SageIncludeError(
                f"case-ambiguous include {context}: {', '.join(sorted(originals))}"
            )
        return matches[0] if matches else None

    normalized_entry = normalize_virtual_path(
        entry_virtual_path, max_path_chars=limits.max_path_chars
    )
    entry = lookup(normalized_entry, context=entry_virtual_path)
    if entry is None:
        raise SageIncludeError(f"missing entry document: {normalized_entry}")

    budget = _Budget(limits)
    parsed: dict[str, SageDocument] = {}
    parsed_fragments: dict[str, SageBodyFragment] = {}
    documents_in_order: list[SageDocument] = []
    fragments_in_order: list[SageBodyFragment] = []
    expanded_objects: list[SageObject] = []
    expanded_includes: list[SageIncludeRef] = []
    stack: list[str] = []

    def parse(path: str) -> SageDocument:
        cached = parsed.get(path)
        if cached is not None:
            return cached
        document = parse_sage_document(
            normalized_sources[path], path, limits=limits, _budget=budget
        )
        parsed[path] = document
        documents_in_order.append(document)
        return document

    def parse_fragment(path: str) -> SageBodyFragment:
        cached = parsed_fragments.get(path)
        if cached is not None:
            return cached
        fragment = parse_sage_body_fragment(
            normalized_sources[path], path, limits=limits, _budget=budget
        )
        parsed_fragments[path] = fragment
        fragments_in_order.append(fragment)
        return fragment

    def push_include(path: str) -> None:
        if len(stack) >= limits.max_include_depth:
            raise SageCstLimitError(
                f"SAGE include depth exceeds {limits.max_include_depth} limit"
            )
        folded_path = path.casefold()
        if any(item.casefold() == folded_path for item in stack):
            cycle = " -> ".join((*stack, path))
            raise SageIncludeError(f"include cycle detected: {cycle}")
        stack.append(path)

    def resolve_include(item: SageIncludeRef) -> tuple[SageIncludeRef, str]:
        relative = lookup(item.relative_virtual_path, context=item.raw_ref)
        root = lookup(item.normalized_ref, context=item.raw_ref)
        candidates = tuple(
            dict.fromkeys(value for value in (relative, root) if value is not None)
        )
        if not candidates:
            raise SageIncludeError(
                f"missing include {item.raw_ref!r} from "
                f"{item.source_virtual_path}:{item.line}"
            )
        if len(candidates) > 1:
            raise SageIncludeError(
                f"ambiguous relative/root include {item.raw_ref!r} from "
                f"{item.source_virtual_path}:{item.line}"
            )
        resolved_path = candidates[0]
        resolved_ref = replace(item, resolved_virtual_path=resolved_path)
        expanded_includes.append(resolved_ref)
        return resolved_ref, resolved_path

    def reindex_scope(items: list[SageBodyItem]) -> tuple[SageBodyItem, ...]:
        """Restore containing-scope ordinals after inline fragment splicing."""

        indexed: list[SageBodyItem] = []
        assignment_ordinal = 0
        include_ordinal = 0
        key_ordinals: dict[str, int] = {}
        for item_ordinal, item in enumerate(items):
            if isinstance(item, SageAssignment):
                folded = item.key.casefold()
                key_ordinal = key_ordinals.get(folded, 0)
                key_ordinals[folded] = key_ordinal + 1
                indexed.append(
                    replace(
                        item,
                        ordinal=assignment_ordinal,
                        key_ordinal=key_ordinal,
                        item_ordinal=item_ordinal,
                    )
                )
                assignment_ordinal += 1
            elif isinstance(item, SageBlock):
                indexed.append(replace(item, item_ordinal=item_ordinal))
            elif isinstance(item, SageScript):
                indexed.append(replace(item, item_ordinal=item_ordinal))
            elif isinstance(item, SageIncludeRef):
                indexed.append(replace(item, ordinal=include_ordinal))
                include_ordinal += 1
            else:  # pragma: no cover - the closed union makes this defensive.
                raise TypeError(f"unexpected SAGE body item: {type(item).__name__}")
        return tuple(indexed)

    def expand_fragment(path: str) -> tuple[SageBodyItem, ...]:
        push_include(path)
        try:
            return expand_body_items(parse_fragment(path).items)
        finally:
            stack.pop()

    def expand_body_items(items: tuple[SageBodyItem, ...]) -> tuple[SageBodyItem, ...]:
        expanded: list[SageBodyItem] = []
        for item in items:
            if isinstance(item, (SageAssignment, SageScript)):
                expanded.append(item)
            elif isinstance(item, SageBlock):
                expanded.append(replace(item, items=expand_body_items(item.items)))
            elif isinstance(item, SageIncludeRef):
                resolved_ref, resolved_path = resolve_include(item)
                # Keep the directive as evidence at its original position, then
                # splice the caller-supplied fragment immediately after it.
                expanded.append(resolved_ref)
                expanded.extend(expand_fragment(resolved_path))
            else:  # pragma: no cover - the closed union makes this defensive.
                raise TypeError(f"unexpected SAGE body item: {type(item).__name__}")
        return reindex_scope(expanded)

    def visit_document(path: str) -> None:
        push_include(path)
        try:
            document = parse(path)
            for item in document.items:
                if isinstance(item, SageObject):
                    expanded_objects.append(
                        replace(item, items=expand_body_items(item.items))
                    )
                    continue

                _, resolved_path = resolve_include(item)
                visit_document(resolved_path)
        finally:
            stack.pop()

    visit_document(entry)
    return ResolvedSageCst(
        entry_virtual_path=entry,
        documents=tuple(documents_in_order),
        objects=tuple(expanded_objects),
        includes=tuple(expanded_includes),
        fragments=tuple(fragments_in_order),
    )


# Short aliases make the intended public entry points discoverable without
# forcing callers to remember whether they are parsing one document or a graph.
parse_document = parse_sage_document
resolve_documents = resolve_sage_documents
