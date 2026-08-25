"""Compile retail skirmish-AI INI blocks into a provenance-rich raw document.

This compiler is intentionally lexical.  It groups authored fields and tokens,
but does not calculate build choices, target scores, phase transitions, or
difficulty effects.  Those decisions belong to the deterministic runtime.
"""
from __future__ import annotations

from dataclasses import dataclass, field
from pathlib import Path
from typing import Iterable, Mapping


SCHEMA = "openbfme.skirmish-ai"
SCHEMA_VERSION = 0
SKIRMISH_AI_RUNTIME_PATH = "ai/skirmish.json"
SKIRMISH_AI_PACK_KEY = "skirmishAi"
AI_DATA_PATH = "data/ini/default/aidata.ini"
SKIRMISH_AI_DATA_PATH = "data/ini/default/skirmishaidata.ini"

_BLOCK_KINDS = frozenset(
    {
        "aidata",
        "sideinfo",
        "skirmishbuildlist",
        "structure",
        "attackpriority",
        "aibase",
        "aidozerassignment",
        "skirmishaidata",
        "combatchaindefinition",
        "brutaldifficultycheats",
        "difficultytuning",
        "armydefinition",
        "aieconomyassigment",  # Retail spelling; part of the authored format.
        "aiwallnodeassignment",
        "armymemberdefinition",
    }
)


class SkirmishAICompilerError(ValueError):
    """The two-file retail AI input is missing or structurally invalid."""


@dataclass
class _Node:
    kind: str
    name: str
    source_ini: str
    line: int
    fields: dict[str, dict[str, object]] = field(default_factory=dict)
    children: list["_Node"] = field(default_factory=list)


def _fact(value: object, source_ini: str, line: int) -> dict[str, object]:
    return {"value": value, "sourceIni": source_ini, "line": line}


def _strip_comment(line: str) -> str:
    """Remove SAGE semicolon comments while retaining the authored value text."""

    stripped = line.strip()
    if not stripped or stripped.startswith(";") or stripped.startswith("//"):
        return ""
    return line.split(";", 1)[0].strip()


def _value(text: str) -> str:
    value = text.strip()
    if len(value) >= 2 and value[0] == value[-1] == '"':
        return value[1:-1]
    return value


def _parse_document(
    data: bytes, source_ini: str
) -> tuple[list[_Node], list[dict[str, object]]]:
    text = data.decode("latin-1")
    roots: list[_Node] = []
    stack: list[_Node] = []
    unsupported: list[dict[str, object]] = []
    for line_number, original in enumerate(text.splitlines(), start=1):
        code = _strip_comment(original)
        if not code:
            continue
        if code.casefold() == "end":
            if not stack:
                unsupported.append(
                    {
                        "sourceIni": source_ini,
                        "line": line_number,
                        "text": original.strip(),
                        "reason": "unmatched End",
                    }
                )
            else:
                stack.pop()
            continue
        if "=" in code:
            key, raw_value = code.split("=", 1)
            key = key.strip()
            if not key or not stack:
                unsupported.append(
                    {
                        "sourceIni": source_ini,
                        "line": line_number,
                        "text": original.strip(),
                        "reason": "assignment outside a supported block",
                    }
                )
                continue
            if key in stack[-1].fields:
                unsupported.append(
                    {
                        "sourceIni": source_ini,
                        "line": line_number,
                        "text": original.strip(),
                        "reason": f"duplicate scalar key in {stack[-1].kind}: {key}",
                    }
                )
                continue
            stack[-1].fields[key] = _fact(
                _value(raw_value), source_ini, line_number
            )
            continue
        words = code.split()
        kind = words[0]
        if kind.casefold() not in _BLOCK_KINDS:
            unsupported.append(
                {
                    "sourceIni": source_ini,
                    "line": line_number,
                    "text": original.strip(),
                    "reason": f"unsupported block or statement: {kind}",
                }
            )
            continue
        node = _Node(
            kind=kind,
            name=" ".join(words[1:]),
            source_ini=source_ini,
            line=line_number,
        )
        if stack:
            stack[-1].children.append(node)
        else:
            roots.append(node)
        stack.append(node)
    if stack:
        unsupported.extend(
            {
                "sourceIni": node.source_ini,
                "line": node.line,
                "text": f"{node.kind} {node.name}".strip(),
                "reason": "unterminated block",
            }
            for node in stack
        )
    return roots, unsupported


def _children(node: _Node, kind: str) -> list[_Node]:
    folded = kind.casefold()
    return [child for child in node.children if child.kind.casefold() == folded]


def _row(node: _Node) -> dict[str, object]:
    return {
        "name": _fact(node.name, node.source_ini, node.line),
        "sourceIni": node.source_ini,
        "line": node.line,
        "fields": dict(node.fields),
    }


def _tokens(fact: Mapping[str, object]) -> dict[str, object]:
    return _fact(
        str(fact["value"]).split(), str(fact["sourceIni"]), int(fact["line"])
    )


def _bse_index(root: Path) -> dict[str, str]:
    rows: dict[str, str] = {}
    for path in sorted(root.rglob("*.bse"), key=lambda item: item.as_posix().casefold()):
        rows.setdefault(path.stem.casefold(), path.relative_to(root).as_posix())
    return rows


def _read_required(root: Path, relative: str) -> bytes:
    path = root.joinpath(*relative.split("/"))
    if not path.is_file():
        raise SkirmishAICompilerError(f"required retail AI source is missing: {path}")
    return path.read_bytes()


def _find_one(nodes: Iterable[_Node], kind: str) -> _Node:
    matches = [node for node in nodes if node.kind.casefold() == kind.casefold()]
    if len(matches) != 1:
        raise SkirmishAICompilerError(
            f"expected exactly one {kind} block, found {len(matches)}"
        )
    return matches[0]


def compile_skirmish_ai(effective_assets_root: Path, *, game: str) -> dict[str, object]:
    """Compile the effective retail AI files without interpreting their policy."""

    game_key = str(game).strip().casefold()
    if game_key not in {"bfme2", "rotwk"}:
        raise SkirmishAICompilerError(f"unsupported game: {game!r}")
    root = Path(effective_assets_root)
    ai_roots, ai_unsupported = _parse_document(
        _read_required(root, AI_DATA_PATH), AI_DATA_PATH
    )
    skirmish_roots, skirmish_unsupported = _parse_document(
        _read_required(root, SKIRMISH_AI_DATA_PATH), SKIRMISH_AI_DATA_PATH
    )
    ai_data = _find_one(ai_roots, "AIData")
    skirmish_data = _find_one(skirmish_roots, "SkirmishAIData")

    side_info = {
        node.name: _row(node) for node in _children(ai_data, "SideInfo")
    }

    dozers: dict[str, dict[str, object]] = {}
    for node in [
        item
        for item in skirmish_roots
        if item.kind.casefold() == "aidozerassignment"
    ]:
        side = node.fields.get("Side")
        if side is None:
            skirmish_unsupported.append(
                {
                    "sourceIni": node.source_ini,
                    "line": node.line,
                    "text": f"{node.kind} {node.name}",
                    "reason": "AIDozerAssignment has no Side",
                }
            )
            continue
        side_name = str(side["value"])
        if side_name in dozers:
            raise SkirmishAICompilerError(
                f"duplicate AIDozerAssignment Side at line {node.line}: {side_name}"
            )
        dozers[side_name] = _row(node)

    bse = _bse_index(root)
    ai_bases: list[dict[str, object]] = []
    for node in [item for item in skirmish_roots if item.kind.casefold() == "aibase"]:
        row = _row(node)
        side = node.fields.get("Side")
        template = node.fields.get("Map")
        map_name = node.fields.get("GameMapToUseOn")
        if side is None or template is None or map_name is None:
            skirmish_unsupported.append(
                {
                    "sourceIni": node.source_ini,
                    "line": node.line,
                    "text": f"{node.kind} {node.name}",
                    "reason": "AIBase is missing Side, Map, or GameMapToUseOn",
                }
            )
            continue
        row["side"] = dict(side)
        row["mapTemplateName"] = dict(template)
        row["gameMapToUseOn"] = dict(map_name)
        row["bseVirtualPath"] = _fact(
            bse.get(str(template["value"]).casefold(), "unresolved"),
            node.source_ini,
            int(template["line"]),
        )
        ai_bases.append(row)

    combat_chains: list[dict[str, object]] = []
    for node in _children(skirmish_data, "CombatChainDefinition"):
        row = _row(node)
        target_types = node.fields.get("TargetTypes")
        modifiers = node.fields.get("TargetPriorityModifiers")
        if target_types is None or modifiers is None:
            skirmish_unsupported.append(
                {
                    "sourceIni": node.source_ini,
                    "line": node.line,
                    "text": f"{node.kind} {node.name}",
                    "reason": "CombatChainDefinition is missing a target matrix row",
                }
            )
        else:
            row["targetTypes"] = _tokens(target_types)
            row["targetPriorityModifiers"] = _tokens(modifiers)
        combat_chains.append(row)

    armies: dict[str, dict[str, object]] = {}
    army_member_count = 0
    for node in [
        item for item in skirmish_roots if item.kind.casefold() == "armydefinition"
    ]:
        row = _row(node)
        side = node.fields.get("Side")
        if side is None:
            skirmish_unsupported.append(
                {
                    "sourceIni": node.source_ini,
                    "line": node.line,
                    "text": f"{node.kind} {node.name}",
                    "reason": "ArmyDefinition has no authored Side",
                }
            )
            row["side"] = _fact("unresolved", node.source_ini, node.line)
        else:
            # Side rule: the ArmyDefinition's own Side field is authoritative.
            # Economy/dozer rows are retained as authored data, never used to
            # invent a side when retail supplied one directly.
            row["side"] = dict(side)
        members = [_row(child) for child in _children(node, "ArmyMemberDefinition")]
        army_member_count += len(members)
        row["armyMembers"] = members
        row["economyAssignments"] = [
            _row(child) for child in _children(node, "AIEconomyAssigment")
        ]
        row["wallNodeAssignments"] = [
            _row(child) for child in _children(node, "AIWallNodeAssignment")
        ]
        hero = node.fields.get("HeroBuildOrder")
        offensive = node.fields.get("OffensiveBuildings")
        row["heroBuildOrder"] = _tokens(hero) if hero is not None else _fact([], node.source_ini, node.line)
        row["offensiveBuildings"] = _tokens(offensive) if offensive is not None else _fact([], node.source_ini, node.line)
        if node.name in armies:
            raise SkirmishAICompilerError(f"duplicate ArmyDefinition: {node.name}")
        armies[node.name] = row

    difficulty = {
        node.name: _row(node)
        for node in _children(skirmish_data, "DifficultyTuning")
    }
    brutal_nodes = _children(skirmish_data, "BrutalDifficultyCheats")
    brutal = _row(brutal_nodes[0]) if brutal_nodes else None
    if len(brutal_nodes) != 1:
        skirmish_unsupported.append(
            {
                "sourceIni": skirmish_data.source_ini,
                "line": skirmish_data.line,
                "text": "BrutalDifficultyCheats",
                "reason": f"expected one block, found {len(brutal_nodes)}",
            }
        )
    disables = {
        key: dict(value)
        for key, value in skirmish_data.fields.items()
        if key.casefold().startswith("disable")
    }
    skirmish_globals = {
        key: dict(value)
        for key, value in skirmish_data.fields.items()
        if not key.casefold().startswith("disable")
    }

    build_lists = [_row(node) for node in _children(ai_data, "SkirmishBuildList")]
    for row, node in zip(build_lists, _children(ai_data, "SkirmishBuildList")):
        row["deprecatedByRetail"] = True
        row["structures"] = [_row(child) for child in _children(node, "Structure")]
    priorities = [_row(node) for node in _children(ai_data, "AttackPriority")]
    for row in priorities:
        row["deprecatedByRetail"] = True
    comment_line = next(
        (
            index
            for index, line in enumerate(
                _read_required(root, AI_DATA_PATH).decode("latin-1").splitlines(),
                start=1,
            )
            if "entire AttackPriority system is no longer in use" in line
        ),
        0,
    )
    deprecated_comment = _fact(
        "The entire AttackPriority system is no longer in use",
        AI_DATA_PATH,
        comment_line or ai_data.line,
    )

    unsupported = sorted(
        [*ai_unsupported, *skirmish_unsupported],
        key=lambda row: (str(row["sourceIni"]), int(row["line"])),
    )
    return {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "game": game_key,
        "globals": dict(ai_data.fields),
        "sideInfo": side_info,
        "armies": armies,
        "combatChains": combat_chains,
        "difficultyTuning": difficulty,
        "brutalDifficultyCheats": brutal,
        "disables": disables,
        "skirmishGlobals": skirmish_globals,
        "dozerAssignments": dozers,
        "aiBases": ai_bases,
        "deprecated": {
            "deprecatedByRetail": True,
            "retailDeprecationComment": deprecated_comment,
            "skirmishBuildLists": build_lists,
            "attackPriorities": priorities,
        },
        "unsupported": unsupported,
        "census": {
            "armyDefinitionCount": len(armies),
            "armyMemberDefinitionCount": army_member_count,
            "combatChainDefinitionCount": len(combat_chains),
            "aiBaseCount": len(ai_bases),
        },
    }


def validate_skirmish_ai(document: Mapping[str, object], *, game: str | None = None) -> None:
    if document.get("schema") != SCHEMA or document.get("schemaVersion") != SCHEMA_VERSION:
        raise SkirmishAICompilerError("skirmish AI schema is invalid")
    if document.get("game") not in {"bfme2", "rotwk"}:
        raise SkirmishAICompilerError("skirmish AI game is invalid")
    if game is not None and document.get("game") != str(game).strip().casefold():
        raise SkirmishAICompilerError("skirmish AI game does not match the pack")
    for key in ("globals", "armies", "combatChains", "difficultyTuning", "aiBases", "census"):
        if key not in document:
            raise SkirmishAICompilerError(f"skirmish AI section is missing: {key}")


__all__ = [
    "SKIRMISH_AI_PACK_KEY",
    "SKIRMISH_AI_RUNTIME_PATH",
    "SkirmishAICompilerError",
    "compile_skirmish_ai",
    "validate_skirmish_ai",
]
