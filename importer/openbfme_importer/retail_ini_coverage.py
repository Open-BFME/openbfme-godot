"""Conservative, reproducible coverage inventory for the retail INI surface.

This module deliberately inventories before it judges.  Exact source-string
mentions are useful evidence, but they are never promoted to semantic parity:
``runtime-tested`` still means only that a matching authored term is named by
runtime and test source.  A behavior-level oracle is required for parity.

The public helpers are small so the report driver in ``tools/`` can combine
the retail corpus with selected-pack and live-Godot evidence without copying
retail values into tracked files.
"""

from __future__ import annotations

from collections import Counter
from dataclasses import dataclass, field
import re

from .sage_particles import _lines as lex_ini_lines


_HEADER_TOKEN = re.compile(r"^[A-Za-z_][A-Za-z0-9_.-]*$")
_FIELD_TOKEN = re.compile(r"^(?:[A-Za-z_][A-Za-z0-9_.%-]*|[0-9]+)$")
_DIRECTIVE = re.compile(r"^#\s*([A-Za-z_][A-Za-z0-9_]*)")
_SCALAR_VALUE = re.compile(
    r"(?:[+-]?(?:\d+(?:\.\d*)?|\.\d+)(?:[eE][+-]?\d+)?%?|"
    r"yes|no|true|false|none|null|\"(?:\\.|[^\"])*\"|'(?:\\.|[^'])*')",
    re.IGNORECASE,
)


@dataclass(frozen=True, slots=True)
class DefinitionSite:
    kind: str
    name: str | None
    line: int
    virtual_path: str


@dataclass(slots=True)
class FeatureUsage:
    sites: int = 0
    samples: list[dict[str, object]] = field(default_factory=list)

    def add(self, virtual_path: str, line: int, *, limit: int = 8) -> None:
        self.sites += 1
        if len(self.samples) < limit:
            self.samples.append({"path": virtual_path, "line": line})


@dataclass(slots=True)
class DocumentSurface:
    definitions: list[DefinitionSite] = field(default_factory=list)
    fields: dict[tuple[str, str], FeatureUsage] = field(default_factory=dict)
    nested_blocks: Counter[tuple[str, str]] = field(default_factory=Counter)
    directives: Counter[str] = field(default_factory=Counter)
    non_assignment_lines: Counter[tuple[str, str]] = field(default_factory=Counter)


def _split_assignment(text: str) -> tuple[str, str] | None:
    if "=" not in text:
        return None
    key, value = text.split("=", 1)
    key = key.strip()
    if not key or not _FIELD_TOKEN.fullmatch(key):
        return None
    return key, value.strip()


def classify_assignment_value(
    value: str, *, module_kind: str | None, asset_reference_count: int
) -> str:
    """Classify one value by syntax/evidence without a field-name allowlist."""

    if module_kind or asset_reference_count == 1:
        return "reference"
    if asset_reference_count > 1:
        return "collection-reference"
    if _SCALAR_VALUE.fullmatch(value.strip()):
        return "scalar"
    return "opaque-unresolved"


def collect_document_surface(virtual_path: str, source: bytes) -> DocumentSurface:
    """Inventory all lexical INI features under their nearest top-level block.

    This is intentionally broader than any semantic parser.  It records every
    assignment key, nested bare-block token, preprocessor directive, and
    top-level definition in an effective INI/INC winner.  Unknown grammar is
    retained in ``non_assignment_lines`` rather than silently skipped.
    """

    result = DocumentSurface()
    root_kind = "<global>"
    root_indent = -1
    for line in lex_ini_lines(source):
        text = line.text.strip()
        if not text:
            continue
        directive = _DIRECTIVE.match(text)
        if directive:
            result.directives[directive.group(1).casefold()] += 1
            continue
        if text.casefold() in {"end", "endscript"}:
            if line.indent <= root_indent:
                root_kind = "<global>"
                root_indent = -1
            continue

        assignment = _split_assignment(text)
        if assignment is not None:
            key, _value = assignment
            folded = (root_kind.casefold(), key.casefold())
            usage = result.fields.setdefault(folded, FeatureUsage())
            usage.add(virtual_path, line.number)
            continue

        parts = text.split()
        token = parts[0]
        if not _HEADER_TOKEN.fullmatch(token):
            result.non_assignment_lines[(root_kind.casefold(), text.casefold())] += 1
            continue
        if line.indent == 0:
            name = parts[1] if len(parts) > 1 else None
            result.definitions.append(
                DefinitionSite(token, name, line.number, virtual_path)
            )
            root_kind = token
            root_indent = line.indent
        else:
            result.nested_blocks[(root_kind.casefold(), token.casefold())] += 1
    return result


_CATEGORY_PATTERNS: tuple[tuple[str, re.Pattern[str]], ...] = (
    (
        "timers",
        re.compile(
            r"(?:buildtime|reload|cooldown|delay|duration|interval|period|"
            r"lifetime|frames|timebetween|attackdelay|preattack|clipreload|"
            r"productiontime|respawn)",
            re.IGNORECASE,
        ),
    ),
    (
        "scripts",
        re.compile(r"(?:script|lua|ai(?:update|data|state|goal|team)|condition|action)", re.IGNORECASE),
    ),
    (
        "assets",
        re.compile(
            r"(?:model|animation|texture|image|icon|portrait|sound|voice|music|"
            r"audio|video|movie|fx|particle|shader|cursor|font|apt|w3d)",
            re.IGNORECASE,
        ),
    ),
    (
        "hero-abilities",
        re.compile(r"(?:hero|specialpower|specialability|levelup|experiencelevel)", re.IGNORECASE),
    ),
    (
        "neutral-mobs",
        re.compile(r"(?:neutral|nature|civilian|creep|lair|cavetroll|warg|goblinlair)", re.IGNORECASE),
    ),
    ("ships", re.compile(r"(?:\bship\b|boat|naval|watercraft|transportship|corsairship)", re.IGNORECASE)),
    (
        "combat-effects",
        re.compile(
            r"(?:weapon|armor|damage|projectile|warhead|attack|combat|death|die|"
            r"crush|knockback|firefx|impactfx|modifier|status)",
            re.IGNORECASE,
        ),
    ),
    (
        "spellbooks",
        re.compile(r"(?:spellbook|science|specialpower|rank[123]|purchasepoint)", re.IGNORECASE),
    ),
    (
        "economy-production",
        re.compile(r"(?:production|buildcost|commandset|commandbutton|resource|money|upgrade)", re.IGNORECASE),
    ),
    (
        "locomotion-physics",
        re.compile(r"(?:locomotor|physics|speed|acceleration|turnrate|collision|geometry)", re.IGNORECASE),
    ),
    (
        "presentation-ui-audio",
        re.compile(r"(?:draw|display|hud|window|controlbar|eva|sound|voice|music|fx|particle)", re.IGNORECASE),
    ),
)


def classify_feature_categories(kind: str, name: str, context: str) -> tuple[str, ...]:
    """Return stable risk/category labels for a retail feature signature."""

    haystack = f"{kind} {name} {context}"
    return tuple(label for label, pattern in _CATEGORY_PATTERNS if pattern.search(haystack))


def is_neutral_mob_object(
    *,
    object_id: str,
    parent_id: str | None,
    source_ini: str,
    side: str | None,
    kind_of: tuple[str, ...] | list[str],
) -> bool:
    """Apply the closed neutral-mob/lair denominator used by coverage.

    Retail uses Side=Neutral for many passive props and markers, so side alone
    is deliberately insufficient. Keeping this predicate in importer code lets
    catalog compilers and the report driver share one exact family definition.
    """

    upper_kind = {token.upper().lstrip("+-") for token in kind_of}
    folded_path = source_ini.replace("\\", "/").casefold()
    neutral_source = (
        (side or "").casefold() in {"neutral", "creeps"}
        or "/object/neutral/" in f"/{folded_path}"
        or "/object/nature/" in f"/{folded_path}"
    )
    mob_kind = bool(
        upper_kind & {"CREEP", "MONSTER", "HORDE", "INFANTRY", "CAVALRY"}
    )
    identity = f"{object_id} {parent_id or ''}"
    lair_identity = bool(re.search(r"(?:lair|creep)", identity, re.IGNORECASE))
    mob_identity = bool(
        re.search(r"(?:warg|wolf|troll|goblin|spider|drake)", identity, re.IGNORECASE)
    )
    return bool(
        (neutral_source and (mob_kind or lair_identity or mob_identity))
        or "CREEP" in upper_kind
        or (side or "").casefold() == "creeps"
    )


def is_hero_family_object(
    *, object_id: str, source_ini: str, side: str | None, kind_of: tuple[str, ...] | list[str]
) -> bool:
    """Apply the exact requested hero-family predicate used by coverage."""

    context = " ".join((source_ini, side or "", *kind_of))
    categories = set(classify_feature_categories("object", object_id, context))
    if "HERO" in {token.upper().lstrip("+-") for token in kind_of}:
        categories.add("hero-abilities")
    return "hero-abilities" in categories


def evidence_status(
    importer_mentioned: bool,
    descriptor_emitted: bool,
    runtime_mentioned: bool,
    runtime_tested: bool,
) -> str:
    """Rank static evidence without ever claiming behavioral parity."""

    if importer_mentioned and descriptor_emitted and runtime_mentioned and runtime_tested:
        return "runtime-tested"
    if importer_mentioned and descriptor_emitted and runtime_mentioned:
        return "runtime-mentioned"
    if importer_mentioned and descriptor_emitted:
        return "descriptor-emitted"
    if importer_mentioned:
        return "importer-mentioned"
    return "unmapped"
