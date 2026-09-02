"""Cook Upgrade, Science, SpecialPower, CommandButton, and CommandSet data."""

from __future__ import annotations

from collections.abc import Mapping, Sequence
import sys
from typing import Any

from ._blocks import (
    CookAssignment,
    append_diagnostic,
    folded_defines,
    parse_named_blocks,
    typed_fields,
)
from .objects import _value


_KINDS = ("Upgrade", "Science", "SpecialPower", "CommandButton", "CommandSet")
_TABLES = {
    "Upgrade": "upgrades",
    "Science": "sciences",
    "SpecialPower": "special_powers",
    "CommandButton": "command_buttons",
}


def _command_set_row(
    block_name: str,
    assignments: Sequence[CookAssignment],
    defines: Mapping[str, object],
) -> dict[str, object]:
    entries: list[dict[str, object]] = []
    fields: list[CookAssignment] = []
    for assignment in assignments:
        try:
            slot = int(assignment.key)
        except ValueError:
            fields.append(assignment)
            continue
        entries.append(
            {
                "slot": slot,
                "button": _value(assignment.value, defines),
            }
        )
    return {
        "name": block_name,
        "entries": entries,
        "fields": typed_fields(fields, defines),
    }


def add_tech_tables(
    bundle: dict[str, Any], documents: Sequence[tuple[str, bytes]]
) -> list[dict[str, str]]:
    """Add every generic technology table and return named parse failures."""

    defines = folded_defines(bundle)
    parsed = parse_named_blocks(documents, _KINDS, retain_malformed=True)
    failures = list(parsed.failures)
    tables: dict[str, list[dict[str, object]]] = {
        table: [] for table in (*_TABLES.values(), "command_sets")
    }
    for block in parsed.blocks:
        if block.kind == "CommandSet":
            tables["command_sets"].append(
                _command_set_row(block.name, block.assignments, defines)
            )
            continue
        tables[_TABLES[block.kind]].append(
            {
                "name": block.name,
                "fields": typed_fields(block.assignments, defines),
            }
        )
    for failure in failures:
        append_diagnostic(bundle, failure["block"], failure["message"])
    bundle.update(tables)
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

    return run_cli("tech", __doc__, argv)


if __name__ == "__main__":
    sys.exit(main())
