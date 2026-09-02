"""Validate every named reference in a base INI tree plus loose mod layers."""

from __future__ import annotations

import argparse
from collections import OrderedDict
from collections.abc import Iterable, Mapping, Sequence
import json
from pathlib import Path
import re
import sys
from typing import Any

from . import objects
from ._blocks import CookAssignment, CookBlock, parse_named_blocks
from ._bundle import cook_documents, layered_documents


_LOCATION = re.compile(r":(?P<line>[0-9]+):")
_IDENTIFIER = re.compile(r"[A-Za-z_][A-Za-z0-9_]*\Z")
_UNIT_TYPE = re.compile(r"(?:^|\s)UnitType:\s*(\S+)", re.IGNORECASE)
_SAGE_MODULE = re.compile(r'\[SageModule\("([^"]+)"')
_SENTINELS = {"0", "and", "none", "noarmor", "nocommandset", "noweapon", "or"}
_NUMERIC_FIELDS = {
    "acceleration",
    "attackrange",
    "braking",
    "buildcost",
    "buildtime",
    "commandpoints",
    "damage",
    "delaybetweenshots",
    "geometryheight",
    "geometrymajorradius",
    "geometryminorradius",
    "initialhealth",
    "maxhealth",
    "maxhealthdamaged",
    "maxturnwithoutreform",
    "minimumattackrange",
    "radius",
    "reloadtime",
    "sciencepurchasepointcost",
    "speed",
    "speeddamaged",
    "turnrate",
}


def _issue(
    kind: str,
    file: str,
    line: int,
    template: str,
    field: str,
    target: str,
    *,
    severity: str = "failure",
) -> dict[str, object]:
    return {
        "kind": kind,
        "severity": severity,
        "file": file,
        "line": line,
        "template": template,
        "field": field,
        "target": target,
    }


def _walk_blocks(blocks: Iterable[Any]) -> Iterable[Any]:
    for block in blocks:
        yield block
        yield from _walk_blocks(block.blocks)


def _effective_objects(documents: Sequence[tuple[str, bytes]]) -> list[Any]:
    resolved, _failures = objects._resolve_objects(documents)
    effective: OrderedDict[str, Any] = OrderedDict()
    for obj in resolved:
        key = obj.name.casefold()
        if key in effective:
            del effective[key]
        effective[key] = obj
    return list(effective.values())


def _names(rows: object) -> set[str]:
    if not isinstance(rows, list):
        return set()
    return {
        str(row["name"]).casefold()
        for row in rows
        if isinstance(row, Mapping) and "name" in row
    }


def _target(raw: str) -> str:
    tokens = raw.strip().strip('"').split()
    return tokens[-1].strip('"') if tokens else ""


def _tokens(raw: str) -> list[str]:
    return [
        token.strip('"')
        for token in re.split(r"[\s,]+", raw.strip())
        if token and token.casefold() not in _SENTINELS
    ]


def _missing(
    output: list[dict[str, object]],
    kind: str,
    assignment: Any,
    template: str,
    field: str,
    target: str,
    available: set[str],
) -> None:
    if target and target.casefold() not in available and target.casefold() not in _SENTINELS:
        output.append(
            _issue(
                kind,
                assignment.source_virtual_path,
                assignment.line,
                template,
                field,
                target,
            )
        )


def _object_references(
    output: list[dict[str, object]],
    sage_objects: Sequence[Any],
    tables: Mapping[str, set[str]],
) -> None:
    template_names = tables["templates"]
    for obj in sage_objects:
        if obj.parent and obj.parent.casefold() not in template_names:
            output.append(
                _issue(
                    "missing_parent",
                    obj.source_virtual_path,
                    obj.line,
                    obj.name,
                    "parent",
                    obj.parent,
                )
            )
        for assignment in obj.assignments:
            key = assignment.key.casefold()
            if key == "commandset":
                _missing(output, "missing_command_set", assignment, obj.name, assignment.key, _target(assignment.value), tables["command_sets"])
            elif key == "locomotor":
                _missing(output, "missing_locomotor", assignment, obj.name, assignment.key, _target(assignment.value), tables["locomotors"])
            elif key == "locomotorset":
                _missing(output, "missing_locomotor_set", assignment, obj.name, assignment.key, _target(assignment.value), tables["locomotor_sets"])
        for block in _walk_blocks(obj.blocks):
            block_kind = (block.header_key or block.kind).casefold()
            for assignment in block.assignments:
                key = assignment.key.casefold()
                if block_kind == "weaponset" and key == "weapon":
                    _missing(output, "missing_weapon", assignment, obj.name, "WeaponSet.Weapon", _target(assignment.value), tables["weapons"])
                elif block_kind == "armorset" and key == "armor":
                    _missing(output, "missing_armor", assignment, obj.name, "ArmorSet.Armor", _target(assignment.value), tables["armors"])
                elif block_kind == "locomotorset" and key == "locomotor":
                    _missing(output, "missing_locomotor", assignment, obj.name, "LocomotorSet.Locomotor", _target(assignment.value), tables["locomotors"])
                elif block.kind.casefold().endswith("hordecontain") and key == "rankinfo":
                    match = _UNIT_TYPE.search(assignment.value)
                    if match:
                        _missing(output, "missing_horde_unit", assignment, obj.name, "RankInfo.UnitType", match.group(1), template_names)


def _named_block_references(
    output: list[dict[str, object]],
    documents: Sequence[tuple[str, bytes]],
    tables: Mapping[str, set[str]],
) -> None:
    parsed = parse_named_blocks(
        documents,
        ("Upgrade", "Science", "CommandButton", "CommandSet"),
        retain_malformed=True,
    )
    for block in parsed.blocks:
        kind = block.kind.casefold()
        for assignment in block.assignments:
            field = assignment.key.casefold()
            if kind == "commandset" and assignment.key.isdigit():
                _missing(output, "missing_command_button", assignment, block.name, assignment.key, _target(assignment.value), tables["command_buttons"])
            elif kind == "commandbutton" and field in {
                "object", "upgrade", "science", "specialpower"
            }:
                table = {
                    "object": "templates",
                    "upgrade": "upgrades",
                    "science": "sciences",
                    "specialpower": "special_powers",
                }[field]
                _missing(output, f"missing_{field}", assignment, block.name, assignment.key, _target(assignment.value), tables[table])
            elif kind == "upgrade" and field in {
                "prerequisites", "prerequisiteupgrade", "requiresallupgrades", "requiresanyupgrade"
            }:
                available = tables["templates"] | tables["upgrades"]
                for target in _tokens(assignment.value):
                    _missing(output, "missing_prerequisite", assignment, block.name, assignment.key, target, available)
            elif kind == "science" and field == "prerequisitesciences":
                for target in _tokens(assignment.value):
                    _missing(output, "missing_science", assignment, block.name, assignment.key, target, tables["sciences"])


def _parse_failures(
    output: list[dict[str, object]], report: Mapping[str, Any]
) -> None:
    for row in report.get("parse_failures", []):
        if not isinstance(row, Mapping):
            continue
        message = str(row.get("message", "parse failure"))
        match = _LOCATION.search(message.replace("\\", "/"))
        output.append(
            _issue(
                "parse_failure",
                str(row.get("file", "")),
                int(match.group("line")) if match else 0,
                str(row.get("template", row.get("block", ""))),
                "parse",
                message,
            )
        )


def _runtime_module_names() -> set[str]:
    repo = Path(__file__).resolve().parents[3]
    module_root = repo / "engine" / "OpenBfme.Sim" / "Modules"
    names: set[str] = set()
    for path in sorted(module_root.glob("*.cs"), key=lambda item: item.name.casefold()):
        text = path.read_text(encoding="utf-8")
        names.update(match.casefold() for match in _SAGE_MODULE.findall(text))
    return names


def _gaps(
    output: list[dict[str, object]], sage_objects: Sequence[Any]
) -> None:
    known = _runtime_module_names()
    for obj in sage_objects:
        for block in _walk_blocks(obj.blocks):
            if not objects._is_module_block(block):
                continue
            carrier = objects._CANONICAL_CARRIERS.get(
                (block.header_key or "").casefold(), "other"
            )
            parser_gap = not objects._module_has_typed_fields(block, carrier)
            runtime_gap = bool(known) and block.kind.casefold() not in known
            if parser_gap or runtime_gap:
                output.append(
                    _issue(
                        "gap",
                        block.source_virtual_path,
                        block.line,
                        obj.name,
                        "module",
                        block.kind,
                        severity="gap",
                    )
                )


def _assignments(sage_objects: Sequence[Any], named_blocks: Sequence[CookBlock]) -> Iterable[Any]:
    for obj in sage_objects:
        yield from obj.assignments
        for block in _walk_blocks(obj.blocks):
            yield from block.assignments
    for block in named_blocks:
        yield from block.assignments
        for child in _walk_blocks(block.blocks):
            yield from child.assignments


def _undefined_defines(
    output: list[dict[str, object]],
    sage_objects: Sequence[Any],
    documents: Sequence[tuple[str, bytes]],
    defines: Mapping[str, object],
) -> None:
    blocks = parse_named_blocks(
        documents,
        ("Weapon", "Armor", "DamageFX", "Locomotor", "Upgrade", "Science", "SpecialPower", "CommandButton", "CommandSet"),
        retain_malformed=True,
    ).blocks
    known = {str(name).casefold() for name in defines}
    owner_by_path_line: dict[tuple[str, int], str] = {}
    for obj in sage_objects:
        owner_by_path_line[(obj.source_virtual_path.casefold(), obj.line)] = obj.name
    for block in blocks:
        owner_by_path_line[(block.source_virtual_path.casefold(), block.line)] = block.name
    for assignment in _assignments(sage_objects, blocks):
        raw = assignment.value.strip()
        if (
            assignment.key.casefold() in _NUMERIC_FIELDS
            and _IDENTIFIER.fullmatch(raw)
            and raw.casefold() not in known
        ):
            template = ""
            candidates = [
                (line, name)
                for (path, line), name in owner_by_path_line.items()
                if path == assignment.source_virtual_path.casefold()
                and line <= assignment.line
            ]
            if candidates:
                template = max(candidates)[1]
            output.append(
                _issue(
                    "undefined_define",
                    assignment.source_virtual_path,
                    assignment.line,
                    template,
                    assignment.key,
                    raw,
                )
            )


def validate_layers(
    ini_root: Path | str, mods: Sequence[Path | str] = ()
) -> list[dict[str, object]]:
    documents, _identity = layered_documents(ini_root, mods)
    result = cook_documents(documents)
    sage_objects = _effective_objects(documents)
    bundle = result.bundle
    tables = {
        name: _names(bundle.get(name))
        for name in (
            "templates",
            "weapons",
            "armors",
            "locomotors",
            "locomotor_sets",
            "upgrades",
            "sciences",
            "special_powers",
            "command_buttons",
            "command_sets",
        )
    }
    output: list[dict[str, object]] = []
    _parse_failures(output, result.report)
    _object_references(output, sage_objects, tables)
    _named_block_references(output, documents, tables)
    _gaps(output, sage_objects)
    _undefined_defines(output, sage_objects, documents, bundle.get("defines", {}))
    unique = {
        (
            str(row["kind"]),
            str(row["severity"]),
            str(row["file"]),
            int(row["line"]),
            str(row["template"]),
            str(row["field"]),
            str(row["target"]),
        ): row
        for row in output
    }
    return [
        unique[key]
        for key in sorted(
            unique,
            key=lambda item: (
                item[2].casefold(), item[3], item[0], item[4].casefold(), item[5].casefold(), item[6].casefold()
            ),
        )
    ]


def _json_bytes(value: object) -> bytes:
    return (
        json.dumps(value, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
    ).encode("utf-8")


def main(argv: Sequence[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--ini-root", type=Path, required=True)
    parser.add_argument("--mod", type=Path, action="append", default=[])
    parser.add_argument("--json", type=Path)
    args = parser.parse_args(argv)
    try:
        issues = validate_layers(args.ini_root, args.mod)
    except (OSError, TypeError, ValueError) as exc:
        parser.error(str(exc))
    encoded = _json_bytes(issues)
    if args.json is not None:
        try:
            args.json.parent.mkdir(parents=True, exist_ok=True)
            args.json.write_bytes(encoded)
        except OSError as exc:
            parser.error(str(exc))
    sys.stdout.buffer.write(encoded)
    return 1 if any(row["severity"] == "failure" for row in issues) else 0


if __name__ == "__main__":
    sys.exit(main())
