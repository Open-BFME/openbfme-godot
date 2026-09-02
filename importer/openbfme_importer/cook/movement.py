"""Cook Locomotor, LocomotorSet, and horde formation data into bundle v1."""

from __future__ import annotations

from collections import OrderedDict
from collections.abc import Mapping, Sequence
import re
import sys
from typing import Any

from ._blocks import (
    append_diagnostic,
    folded_defines,
    parse_named_blocks,
    typed_fields,
)
from .objects import _value


_RANK = re.compile(r"(?:^|\s)RankNumber:\s*(\S+)", re.IGNORECASE)
_UNIT = re.compile(r"(?:^|\s)UnitType:\s*(\S+)", re.IGNORECASE)
_POSITION = re.compile(
    r"(?:^|\s)Position:X:\s*(\S+)\s+Y:\s*(\S+)", re.IGNORECASE
)


def _walk_rows(blocks):
    for block in blocks:
        yield block
        yield from _walk_rows(block.get("blocks", []))


def _locomotor_sets(templates: Sequence[Mapping[str, Any]]) -> list[dict[str, object]]:
    rows: list[dict[str, object]] = []
    for template in templates:
        for block in _walk_rows(template.get("blocks", [])):
            if str(block.get("type", "")).casefold() == "locomotorset":
                rows.append(
                    {
                        "name": str(template["name"]),
                        "fields": dict(block.get("fields", {})),
                    }
                )
    return rows


def _scalar(raw: str, defines: Mapping[str, object], label: str) -> int | float:
    value = _value(raw, defines)
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} is not numeric: {value!r}")
    return value


def _rank_info(raw: object, defines: Mapping[str, object]) -> dict[str, object]:
    if not isinstance(raw, str):
        raise ValueError(f"RankInfo is not a string: {raw!r}")
    rank = _RANK.search(raw)
    unit = _UNIT.search(raw)
    positions = list(_POSITION.finditer(raw))
    if rank is None or unit is None or not positions:
        raise ValueError(f"malformed RankInfo: {raw!r}")
    rank_value = _scalar(rank.group(1), defines, "RankInfo rank")
    if not isinstance(rank_value, int):
        raise ValueError(f"RankInfo rank is not an integer: {rank_value!r}")
    return {
        "rank": rank_value,
        "unit_type": unit.group(1),
        "position": [
            {
                "x": _scalar(item.group(1), defines, "RankInfo X"),
                "y": _scalar(item.group(2), defines, "RankInfo Y"),
            }
            for item in positions
        ],
    }


def _values(value: object | None) -> list[object]:
    if value is None:
        return []
    return value if isinstance(value, list) else [value]


def _merge_field(target: dict[str, object], key: str, value: object) -> None:
    if key not in target:
        target[key] = value
        return
    existing = target[key]
    target[key] = [*_values(existing), *_values(value)]


def _object_source(
    documents: Sequence[tuple[str, bytes]], name: str
) -> str:
    header = re.compile(
        rb"(?mi)^[ \t]*(?:Object|ChildObject|ObjectReskin)[ \t]+"
        + re.escape(name.encode("cp1252", errors="replace"))
        + rb"(?:[ \t]|$)"
    )
    return next((path for path, source in documents if header.search(source)), "")


def _hordes(
    templates: Sequence[Mapping[str, Any]],
    locomotors: Sequence[Mapping[str, Any]],
    documents: Sequence[tuple[str, bytes]],
    defines: Mapping[str, object],
) -> tuple[list[dict[str, object]], list[dict[str, str]]]:
    locomotor_by_name = {
        str(row["name"]).casefold(): row for row in locomotors
    }
    output: list[dict[str, object]] = []
    failures: list[dict[str, str]] = []
    for template in templates:
        modules = [
            module
            for module in template.get("modules", [])
            if str(module.get("type", "")).casefold().endswith("hordecontain")
        ]
        if not modules:
            continue
        fields: dict[str, object] = OrderedDict()
        rank_rows: list[dict[str, object]] = []
        try:
            for module in modules:
                module_fields = module.get("fields", {})
                if not isinstance(module_fields, Mapping):
                    raise ValueError("HordeContain fields are not an object")
                for raw in _values(module_fields.get("RankInfo")):
                    rank_rows.append(_rank_info(raw, defines))
                for key, value in module_fields.items():
                    if str(key).casefold() != "rankinfo":
                        _merge_field(fields, str(key), value)

            locomotor_name = None
            for block in _walk_rows(template.get("blocks", [])):
                if str(block.get("type", "")).casefold() != "locomotorset":
                    continue
                block_fields = block.get("fields", {})
                if isinstance(block_fields, Mapping) and "Locomotor" in block_fields:
                    locomotor_name = str(_values(block_fields["Locomotor"])[-1])
                    break
            if locomotor_name:
                locomotor = locomotor_by_name.get(locomotor_name.casefold())
                locomotor_fields = locomotor.get("fields", {}) if locomotor else {}
                if isinstance(locomotor_fields, Mapping) and "MaxTurnWithoutReform" in locomotor_fields:
                    fields["MaxTurnWithoutReform"] = locomotor_fields[
                        "MaxTurnWithoutReform"
                    ]
        except ValueError as exc:
            name = str(template.get("name", ""))
            path = _object_source(documents, name)
            failure = {
                "file": path,
                "block": name,
                "message": f"{path or '<unknown>'}: {name}: {exc}",
            }
            failures.append(failure)
            continue
        output.append(
            {
                "name": str(template["name"]),
                "rank_info": rank_rows,
                "fields": dict(fields),
            }
        )
    return output, failures


def add_movement_tables(
    bundle: dict[str, Any], documents: Sequence[tuple[str, bytes]]
) -> list[dict[str, str]]:
    """Add movement rows to an Object-cook bundle and return named failures."""

    defines = folded_defines(bundle)
    parsed = parse_named_blocks(documents, ("Locomotor",))
    failures = list(parsed.failures)
    locomotors = [
        {"name": block.name, "fields": typed_fields(block.assignments, defines)}
        for block in parsed.blocks
    ]
    templates = bundle.get("templates", [])
    if not isinstance(templates, list):
        raise ValueError("bundle templates must be an array")
    locomotor_sets = _locomotor_sets(templates)
    hordes, horde_failures = _hordes(
        templates, locomotors, documents, defines
    )
    failures.extend(horde_failures)
    for failure in failures:
        append_diagnostic(bundle, failure["block"], failure["message"])
    bundle["locomotors"] = locomotors
    bundle["locomotor_sets"] = locomotor_sets
    bundle["hordes"] = hordes
    return failures


def cook_documents(documents):
    from ._bundle import cook_documents as cook_all_documents

    return cook_all_documents(documents)


def cook_ini_root(ini_root):
    from ._bundle import cook_ini_root as cook_all_root

    return cook_all_root(ini_root)


def cook_files(files):
    from ._bundle import cook_files as cook_all_files

    return cook_all_files(files)


def main(argv: Sequence[str] | None = None) -> int:
    from ._bundle import run_cli

    return run_cli("movement", __doc__, argv)


if __name__ == "__main__":
    sys.exit(main())
