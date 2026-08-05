"""Compile a retail ``CastleBehavior`` assignment and its referenced BSE.

The INI module chooses a base template by owning faction.  The BSE is the
authority for which objects are created and for their exact XYZ offsets and
angles; this compiler deliberately has no fortress-name fallback table.
"""

from __future__ import annotations

from collections.abc import Mapping, Sequence
import hashlib
import math
from pathlib import Path
import re

from .sage_map import SageMapError, parse_sage_base_template_bytes
from .sage_cst import SageObject


class CastleBehaviorCompilerError(ValueError):
    """Retail castle selection or BSE evidence is missing or malformed."""


def _q32_raw(value: int | float) -> int:
    numerator, denominator = value.as_integer_ratio()
    magnitude = (abs(numerator) << 32) // denominator
    return -magnitude if numerator < 0 else magnitude


def _resolve_casefold(root: Path, virtual_path: str) -> Path:
    current = root
    for part in virtual_path.split("/"):
        matches = [row for row in current.iterdir() if row.name.casefold() == part.casefold()]
        if len(matches) != 1:
            raise CastleBehaviorCompilerError(
                f"castle BSE path is missing or ambiguous: {virtual_path}"
            )
        current = matches[0]
    if not current.is_file():
        raise CastleBehaviorCompilerError(f"castle BSE is not a file: {virtual_path}")
    return current


def harvest_castle_upgrade_behaviors(
    lineage: Sequence[SageObject],
) -> dict[str, tuple[str, str | None]]:
    """Harvest effective CastleUpgrade trigger-to-grant assignments.

    The returned mapping preserves the authored trigger spelling as its key;
    callers compare those keys case-insensitively because SAGE identifiers are
    case-insensitive.  The tuple is ``(Upgrade, WallUpgradeRadius)`` and the
    radius remains text because this surface only needs the trigger-to-grant
    identity; the opaque module contract carries the authoritative field.
    """

    from .playable_unit_compiler import (
        _effective_top_blocks,
        _tokens,
        _walk_blocks,
    )

    result: dict[str, tuple[str, str | None]] = {}
    folded_keys: dict[str, str] = {}
    for block in _walk_blocks(_effective_top_blocks(lineage)):
        if (block.header_key or "").casefold() != "behavior":
            continue
        if block.kind.casefold() != "castleupgrade":
            continue
        triggers = [
            token
            for value in block.values("TriggeredBy")
            for token in _tokens(value)
            if token.casefold() not in {"none", "null"}
        ]
        grants = [
            token
            for value in block.values("Upgrade")
            for token in _tokens(value)
            if token.casefold() not in {"none", "null"}
        ]
        if len(triggers) != 1 or len(grants) != 1:
            raise CastleBehaviorCompilerError(
                "CastleUpgrade at "
                f"{block.source_virtual_path}:{block.line} must author one "
                "TriggeredBy and one Upgrade"
            )
        trigger = triggers[0]
        folded = trigger.casefold()
        previous = folded_keys.get(folded)
        radius_values = tuple(
            value.strip()
            for value in block.values("WallUpgradeRadius")
            if value.strip() and value.strip().casefold() not in {"none", "null"}
        )
        radius = radius_values[0] if radius_values else None
        assignment = (grants[0], radius)
        if previous is not None:
            if result[previous] != assignment:
                raise CastleBehaviorCompilerError(
                    f"CastleUpgrade trigger {trigger} is authored more than once "
                    "with conflicting grants"
                )
            continue
        folded_keys[folded] = trigger
        result[trigger] = assignment
    return result


def compile_castle_behavior_contract(
    lifecycle_evidence: Mapping[str, object],
    faction: str,
    effective_root: Path,
) -> dict[str, object] | None:
    """Return the exact faction-selected unpack contract, or ``None`` if absent.

    Once a CastleBehavior module is present, every malformed/missing link fails
    closed.  ``None`` therefore means only that the object authors no such
    module at all.
    """

    if re.fullmatch(r"[A-Za-z0-9_+.-]+", faction) is None:
        raise CastleBehaviorCompilerError("castle faction identity is invalid")
    modules = lifecycle_evidence.get("runtimeModules")
    if not isinstance(modules, list):
        raise CastleBehaviorCompilerError("lifecycle runtimeModules are malformed")
    castle_modules = [
        row
        for row in modules
        if isinstance(row, Mapping)
        and str(row.get("moduleKind", "")).casefold() == "castlebehavior"
    ]
    if not castle_modules:
        return None
    assignments: list[Mapping[str, object]] = []
    for module in castle_modules:
        rows = module.get("assignments")
        if not isinstance(rows, list):
            raise CastleBehaviorCompilerError("CastleBehavior assignments are malformed")
        assignments.extend(
            row
            for row in rows
            if isinstance(row, Mapping)
            and str(row.get("key", "")).casefold().replace(" ", "")
            == "castletounpackforfaction"
        )
    selected: list[tuple[Mapping[str, object], str]] = []
    for row in assignments:
        raw = row.get("rawValue")
        if not isinstance(raw, str):
            raise CastleBehaviorCompilerError(
                "CastleToUnpackForFaction value is not text"
            )
        tokens = raw.split()
        if len(tokens) != 2 or re.fullmatch(r"[A-Za-z0-9_-]+", tokens[1]) is None:
            raise CastleBehaviorCompilerError(
                f"CastleToUnpackForFaction value is malformed: {raw!r}"
            )
        if tokens[0].casefold() == faction.casefold():
            selected.append((row, tokens[1]))
    if len(selected) != 1:
        raise CastleBehaviorCompilerError(
            f"expected exactly one CastleToUnpackForFaction for {faction}, found {len(selected)}"
        )
    assignment, token = selected[0]
    virtual_path = f"bases/{token.casefold()}/{token.casefold()}.bse"
    physical_path = _resolve_casefold(effective_root, virtual_path)
    payload = physical_path.read_bytes()
    try:
        parsed = parse_sage_base_template_bytes(payload)
    except SageMapError as exc:
        raise CastleBehaviorCompilerError(
            f"unsupported castle BSE {virtual_path}: {exc}"
        ) from exc
    castle_templates = parsed.get("castleTemplates")
    if not isinstance(castle_templates, Mapping):
        raise CastleBehaviorCompilerError("castle BSE CastleTemplates are malformed")
    property_key = castle_templates.get("propertyKey")
    if not isinstance(property_key, Mapping) or property_key.get("name") != token:
        raise CastleBehaviorCompilerError(
            "castle BSE property key does not match CastleToUnpackForFaction token"
        )
    raw_pieces = castle_templates.get("templates")
    if not isinstance(raw_pieces, list) or not raw_pieces:
        raise CastleBehaviorCompilerError("castle BSE has no unpack pieces")
    pieces: list[dict[str, object]] = []
    for expected_index, row in enumerate(raw_pieces):
        if not isinstance(row, Mapping) or row.get("index") != expected_index:
            raise CastleBehaviorCompilerError("castle BSE piece indexes are not contiguous")
        template = row.get("templateName")
        offset = row.get("offset")
        angle = row.get("angleRadians")
        if (
            not isinstance(template, str)
            or not template
            or not isinstance(offset, list)
            or len(offset) != 3
            or any(isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) for value in offset)
            or isinstance(angle, bool)
            or not isinstance(angle, (int, float))
            or not math.isfinite(angle)
        ):
            raise CastleBehaviorCompilerError(
                f"castle BSE piece {expected_index} is malformed"
            )
        pieces.append(
            {
                "index": expected_index,
                "objectId": template,
                "offset": list(offset),
                # JSON's shortest float spelling can sit infinitesimally below
                # the exact float32 value and truncate one Q32 unit too low.
                # Carry the exact binary-rational projection explicitly.
                "offsetRawQ32": [_q32_raw(value) for value in offset],
                "angleRadians": angle,
                "angleRadiansRawQ32": _q32_raw(angle),
                "priority": row.get("priority"),
                "phase": row.get("phase"),
            }
        )
    source = parsed.get("source")
    if not isinstance(source, Mapping):
        raise CastleBehaviorCompilerError("castle BSE source evidence is malformed")
    return {
        "faction": faction,
        "castleTemplateToken": token,
        "assignment": dict(assignment),
        "source": {
            "virtualPath": virtual_path,
            "byteCount": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
            "bodySha256": source.get("bodySha256"),
            "castleTemplatesVersion": castle_templates.get("version"),
        },
        "pieces": pieces,
    }


__all__ = [
    "CastleBehaviorCompilerError",
    "compile_castle_behavior_contract",
    "harvest_castle_upgrade_behaviors",
]
