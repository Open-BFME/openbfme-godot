"""Compile retail map-specific ``AIBase`` bindings and their BSE layouts.

``skirmishaidata.ini`` selects a named base template by map and side.  Each
template's BSE is the authority for the structure identity, source-space
offset, facing, priority, and phase.  The compiler preserves those values and
does not substitute the generic ``<ANY>`` layout when a map-specific row is
absent; runtime owns that explicit fallback decision.
"""

from __future__ import annotations

from collections.abc import Mapping
import hashlib
import math
from pathlib import Path
import re
from typing import Any

from .sage_ini import (
    IniBlock,
    MAX_ASSIGNMENTS_PER_BLOCK,
    MAX_BLOCKS,
    _assignment,
    _lines,
)
from .sage_map import SageMapError, parse_sage_base_template_bytes


AI_BASES_SCHEMA = "openbfme.castle-ai-bases"
_SAFE_MAP_NAME = re.compile(r"[A-Za-z0-9 _.-]+")


class CastleAIBaseError(ValueError):
    """An authored AIBase binding or its BSE evidence is malformed."""


def _parse_ai_base_blocks(source: bytes) -> tuple[IniBlock, ...]:
    """Parse the flat family while preserving retail's space-bearing Wild id."""

    header = re.compile(r"^AIBase\s+(.+?)\s*$", re.IGNORECASE)
    blocks: list[IniBlock] = []
    name: str | None = None
    assignments: list[tuple[str, str]] = []
    for line in _lines(source):
        if name is None:
            match = header.fullmatch(line)
            if match:
                name = match.group(1)
                assignments = []
            continue
        if line.casefold() == "end":
            blocks.append(IniBlock("AIBase", name, None, tuple(assignments)))
            if len(blocks) > MAX_BLOCKS:
                raise CastleAIBaseError("AIBase block count exceeds limit")
            name = None
            assignments = []
            continue
        nested = header.fullmatch(line)
        if nested:
            raise CastleAIBaseError(f"unterminated AIBase block before {nested.group(1)}")
        item = _assignment(line)
        if item is not None:
            assignments.append(item)
            if len(assignments) > MAX_ASSIGNMENTS_PER_BLOCK:
                raise CastleAIBaseError("AIBase assignment count exceeds limit")
    if name is not None:
        raise CastleAIBaseError(f"unterminated AIBase block: {name}")
    return tuple(blocks)


def _unquote(value: str, *, field: str) -> str:
    text = value.strip()
    if len(text) >= 2 and text[0] == text[-1] == '"':
        text = text[1:-1]
    if not text:
        raise CastleAIBaseError(f"AIBase {field} is empty")
    return text


def _one_value(block: Any, field: str) -> str:
    values = block.values(field)
    if len(values) != 1:
        raise CastleAIBaseError(
            f"AIBase {block.name} must author exactly one {field}, found {len(values)}"
        )
    return _unquote(values[0], field=field)


def _casefold_child(directory: Path, name: str) -> Path:
    if not directory.is_dir():
        raise CastleAIBaseError(f"AIBase path parent is missing: {directory}")
    matches = [entry for entry in directory.iterdir() if entry.name.casefold() == name.casefold()]
    if len(matches) != 1:
        raise CastleAIBaseError(
            f"AIBase path component is missing or ambiguous: {directory / name}"
        )
    return matches[0]


def resolve_ai_base_template(effective_root: Path, template_name: str) -> tuple[Path, str]:
    """Resolve ``bases/<Map>/<Map>.bse`` without relying on host casing rules."""

    if not template_name or "/" in template_name or "\\" in template_name:
        raise CastleAIBaseError(f"AIBase Map token is invalid: {template_name!r}")
    bases = _casefold_child(effective_root, "bases")
    directory = _casefold_child(bases, template_name)
    physical = _casefold_child(directory, f"{template_name}.bse")
    if not physical.is_file():
        raise CastleAIBaseError(f"AIBase BSE is not a file: {physical}")
    return physical, physical.relative_to(effective_root).as_posix()


def _parse_binding(block: Any) -> dict[str, object]:
    side = _one_value(block, "Side")
    template = _one_value(block, "Map")
    game_map = _one_value(block, "GameMapToUseOn")
    positions = _one_value(block, "PlayerPositions")
    rotation = _one_value(block, "AllowsArbirtaryRotation")
    try:
        player_positions = int(positions)
    except ValueError as exc:
        raise CastleAIBaseError(
            f"AIBase {block.name} PlayerPositions is not an integer: {positions!r}"
        ) from exc
    if player_positions <= 0:
        raise CastleAIBaseError(f"AIBase {block.name} PlayerPositions must be positive")
    if rotation.casefold() not in {"yes", "no"}:
        raise CastleAIBaseError(
            f"AIBase {block.name} AllowsArbirtaryRotation is invalid: {rotation!r}"
        )
    return {
        "id": block.name,
        "side": side,
        "map": template,
        "gameMapToUseOn": game_map,
        "playerPositions": player_positions,
        "allowsArbitraryRotation": rotation.casefold() == "yes",
    }


def _compile_layout(binding: Mapping[str, object], effective_root: Path) -> dict[str, object]:
    template_name = str(binding["map"])
    physical, virtual = resolve_ai_base_template(effective_root, template_name)
    payload = physical.read_bytes()
    try:
        parsed = parse_sage_base_template_bytes(payload)
    except SageMapError as exc:
        raise CastleAIBaseError(f"unsupported AIBase BSE {virtual}: {exc}") from exc
    castle_templates = parsed.get("castleTemplates")
    if not isinstance(castle_templates, Mapping):
        raise CastleAIBaseError(f"AIBase BSE has no CastleTemplates: {virtual}")
    property_key = castle_templates.get("propertyKey")
    if (
        not isinstance(property_key, Mapping)
        or str(property_key.get("name", "")).casefold() != template_name.casefold()
    ):
        raise CastleAIBaseError(
            f"AIBase BSE property key does not match Map token: {virtual}"
        )
    rows = castle_templates.get("templates")
    if not isinstance(rows, list):
        raise CastleAIBaseError(f"AIBase BSE structure sites are malformed: {virtual}")
    sites: list[dict[str, object]] = []
    for expected_index, value in enumerate(rows):
        if not isinstance(value, Mapping) or value.get("index") != expected_index:
            raise CastleAIBaseError(f"AIBase BSE site indexes are not contiguous: {virtual}")
        object_id = value.get("templateName")
        offset = value.get("offset")
        angle = value.get("angleRadians")
        priority = value.get("priority")
        phase = value.get("phase")
        if (
            not isinstance(object_id, str)
            or not object_id
            or not isinstance(offset, list)
            or len(offset) != 3
            or any(
                isinstance(item, bool)
                or not isinstance(item, (int, float))
                or not math.isfinite(item)
                for item in offset
            )
            or isinstance(angle, bool)
            or not isinstance(angle, (int, float))
            or not math.isfinite(angle)
            or isinstance(priority, bool)
            or not isinstance(priority, int)
            or isinstance(phase, bool)
            or not isinstance(phase, int)
        ):
            raise CastleAIBaseError(f"AIBase BSE site {expected_index} is malformed: {virtual}")
        sites.append(
            {
                "index": expected_index,
                "objectId": object_id,
                "offsetSource": list(offset),
                "angleRadians": angle,
                "priority": priority,
                "phase": phase,
            }
        )
    source = parsed.get("source")
    return {
        "side": str(binding["side"]),
        "binding": dict(binding),
        "source": {
            "virtualPath": virtual,
            "byteCount": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
            "bodySha256": source.get("bodySha256") if isinstance(source, Mapping) else None,
            "castleTemplatesVersion": castle_templates.get("version"),
        },
        "siteCount": len(sites),
        "sites": sites,
    }


def compile_castle_ai_bases(
    skirmish_ai_data: bytes,
    effective_root: Path,
    game_map_to_use_on: str,
) -> dict[str, object]:
    """Compile every map-specific side layout for one retail map name."""

    requested = game_map_to_use_on.strip().replace("\\", "/").rsplit("/", 1)[-1]
    if not requested or _SAFE_MAP_NAME.fullmatch(requested) is None:
        raise CastleAIBaseError(f"GameMapToUseOn token is invalid: {game_map_to_use_on!r}")
    selected: list[dict[str, object]] = []
    for block in _parse_ai_base_blocks(skirmish_ai_data):
        # Generic <ANY> rows legitimately omit PlayerPositions/rotation.  Read
        # only the selector first, then apply the strict map-specific schema to
        # rows this document actually owns.
        game_map = _one_value(block, "GameMapToUseOn")
        if game_map.casefold() == requested.casefold():
            selected.append(_parse_binding(block))
    seen_sides: set[str] = set()
    layouts: list[dict[str, object]] = []
    for binding in selected:
        side_key = str(binding["side"]).casefold()
        if side_key in seen_sides:
            raise CastleAIBaseError(
                f"map {requested} has duplicate AIBase side {binding['side']}"
            )
        seen_sides.add(side_key)
        layouts.append(_compile_layout(binding, effective_root))
    layouts.sort(key=lambda row: str(row["side"]).casefold())
    return {
        "schema": AI_BASES_SCHEMA,
        "schemaVersion": 0,
        "gameMapToUseOn": (
            str(selected[0]["gameMapToUseOn"]) if selected else requested
        ),
        "coordinateTransform": "localOffset=(sage.x,-sage.y)*sourceMapTransformScale",
        "layoutCount": len(layouts),
        "layouts": layouts,
        "fallback": "authored-map-specific" if layouts else "generic-any",
    }


def make_effective_assets_ai_bases_builder(effective_root: Path):
    """Return a map-profile callback that compiles one ``ai_bases.json``."""

    ini_path = effective_root / "data" / "ini" / "default" / "skirmishaidata.ini"
    source = ini_path.read_bytes()

    def build(virtual_path: str, _parsed: object = None) -> dict[str, object]:
        return compile_castle_ai_bases(source, effective_root, Path(virtual_path).name)

    return build


__all__ = [
    "AI_BASES_SCHEMA",
    "CastleAIBaseError",
    "compile_castle_ai_bases",
    "make_effective_assets_ai_bases_builder",
    "resolve_ai_base_template",
]
