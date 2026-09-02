"""Cook every effective Weapon, Armor, and DamageFX block into bundle v1."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
import sys
from typing import Any

from ._blocks import (
    CookAssignment,
    CookBlock,
    append_diagnostic,
    folded_defines,
    parse_named_blocks,
    typed_fields,
)


_KINDS = ("Weapon", "Armor", "DamageFX")
_NUGGET_KINDS = {
    "damagenugget": "DamageNugget",
    "metaimpactnugget": "MetaImpactNugget",
    "projectilenugget": "ProjectileNugget",
}


def _failure(block: CookBlock, message: str) -> dict[str, str]:
    return {
        "file": block.source_virtual_path,
        "block": block.name,
        "message": f"{block.source_virtual_path}:{block.line}: {message}",
    }


def _number(value: object, label: str) -> int | float:
    if isinstance(value, bool) or not isinstance(value, (int, float)):
        raise ValueError(f"{label} is not numeric: {value!r}")
    return value


def _armor_percent(
    assignment: CookAssignment, defines: Mapping[str, object]
) -> tuple[str, int | float]:
    tokens = assignment.value.split()
    if len(tokens) != 2:
        raise ValueError(
            f"Armor row at {assignment.source_virtual_path}:{assignment.line} "
            "is not '<damage type> <percent>'"
        )
    raw_percent = tokens[1]
    resolved = defines.get(raw_percent.casefold(), raw_percent)
    if isinstance(resolved, str):
        text = resolved.strip()
        if text.endswith("%"):
            text = text[:-1].strip()
        try:
            number = float(text)
        except ValueError as exc:
            raise ValueError(
                f"Armor percent at {assignment.source_virtual_path}:"
                f"{assignment.line} is unresolved: {raw_percent!r}"
            ) from exc
        percent: int | float = int(number) if number.is_integer() else number
    else:
        percent = _number(resolved, "Armor percent")
    if percent < 0:
        raise ValueError(
            f"Armor percent at {assignment.source_virtual_path}:"
            f"{assignment.line} is negative"
        )
    return tokens[0], percent


def _weapon_row(block: CookBlock, defines: Mapping[str, object]) -> dict[str, object]:
    nuggets = [
        {
            "kind": _NUGGET_KINDS.get(nugget.kind.casefold(), "other"),
            "fields": typed_fields(nugget.assignments, defines),
        }
        for nugget in block.blocks
    ]
    return {
        "name": block.name,
        "fields": typed_fields(block.assignments, defines),
        "nuggets": nuggets,
    }


def _armor_row(block: CookBlock, defines: Mapping[str, object]) -> dict[str, object]:
    entries = []
    other: list[CookAssignment] = []
    for assignment in block.assignments:
        if assignment.key.casefold() == "armor":
            damage_type, percent = _armor_percent(assignment, defines)
            entries.append({"damage_type": damage_type, "percent": percent})
        else:
            other.append(assignment)
    if not entries:
        raise ValueError("Armor block has no Armor entries")
    return {
        "name": block.name,
        "entries": entries,
        "fields": typed_fields(other, defines),
    }


def add_combat_tables(
    bundle: dict[str, Any], documents: Sequence[tuple[str, bytes]]
) -> list[dict[str, str]]:
    """Add combat rows to an Object-cook bundle and return named failures."""

    defines = folded_defines(bundle)
    parsed = parse_named_blocks(documents, _KINDS)
    failures = list(parsed.failures)
    weapons: list[dict[str, object]] = []
    armors: list[dict[str, object]] = []
    damage_fx: list[dict[str, object]] = []
    for block in parsed.blocks:
        try:
            if block.kind == "Weapon":
                weapons.append(_weapon_row(block, defines))
            elif block.kind == "Armor":
                armors.append(_armor_row(block, defines))
            else:
                damage_fx.append(
                    {
                        "name": block.name,
                        "fields": typed_fields(block.assignments, defines),
                    }
                )
        except ValueError as exc:
            failures.append(_failure(block, str(exc)))
    for failure in failures:
        append_diagnostic(bundle, failure["block"], failure["message"])
    bundle["weapons"] = weapons
    bundle["armors"] = armors
    bundle["damage_fx"] = damage_fx
    return failures


def cook_documents(documents):
    """Cook a complete bundle; callable by tests and future Object orchestration."""

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

    return run_cli("combat", __doc__, argv)


if __name__ == "__main__":
    sys.exit(main())
