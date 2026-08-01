"""Convert retail's ``gamedata.ini`` ``#define`` table, so the strategic layer
can resolve the bonus macros the living-world document leaves symbolic.

WHY THIS MODULE EXISTS
----------------------
Retail authors several region bonuses as a MACRO rather than a literal::

    Region Mordor
        FertileTerritoryBonus = FERTILE_TERRITORY_BONUS

The living-world importer carries that through honestly, as
``bonusMacros: {"fertileTerritory": "FERTILE_TERRITORY_BONUS"}`` with no value,
because the value is not in the file it read. So the strategic screen could
show the macro's NAME but not its number - and retail's own region panel reads
``+500 Treasure`` for Mordor.

The number is not missing from the game; it is one file away::

    data/ini/gamedata.ini:  #define FERTILE_TERRITORY_BONUS 500

This module reads that file and emits the ``#define`` table, so the panel shows
retail's number BECAUSE IT READ IT, not because 500 looked plausible. A macro
this table does not carry stays unresolved and the screen says so.

WHAT IT REFUSES TO DO
---------------------
* It NEVER evaluates an expression. Retail writes ``#DIVIDE( 1.0, 0.95 )`` in
  places; a define whose body is not a bare integer or float is recorded with
  its RAW TEXT and ``"numeric": false``, never computed.
* It NEVER invents a define. A name absent from the file is absent here.
* It reads through the CATALOGS, never through the effective-assets cache.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re

from .livingmap_bundle import CatalogReader

SCHEMA = "openbfme.living-world-macros"
SCHEMA_VERSION = 1

GAMEDATA_INI = "data/ini/gamedata.ini"
MAX_ASSET_BYTES = 64 * 1024 * 1024

_DEFINE = re.compile(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(.+?)\s*$")
_INTEGER = re.compile(r"^-?\d+$")
_FLOAT = re.compile(r"^-?\d*\.\d+$")


class MacroTableError(RuntimeError):
    """The table cannot be read, with the exact reason."""


def parse_defines(text: str) -> dict[str, dict]:
    """Every ``#define`` in the file, with its raw body preserved."""
    table: dict[str, dict] = {}
    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.split(";", 1)[0].split("//", 1)[0]
        match = _DEFINE.match(line)
        if match is None:
            continue
        name, body = match.group(1), match.group(2).strip()
        record: dict = {"raw": body, "line": number, "numeric": False}
        if _INTEGER.match(body):
            record["value"] = int(body)
            record["numeric"] = True
        elif _FLOAT.match(body):
            record["value"] = float(body)
            record["numeric"] = True
        table[name] = record
    return table


def build_bundle(
    catalog_path: pathlib.Path | str, output_path: pathlib.Path | str
) -> dict:
    reader = CatalogReader(catalog_path)
    entry = reader.resolve(GAMEDATA_INI)
    if entry is None:
        raise MacroTableError(f"{GAMEDATA_INI} is not in the catalog")
    if entry.size > MAX_ASSET_BYTES:
        raise MacroTableError(f"{GAMEDATA_INI} is {entry.size} bytes, over the read ceiling")
    payload = reader.read(entry)
    table = parse_defines(payload.decode("cp1252", errors="replace"))

    numeric = sum(1 for record in table.values() if record["numeric"])
    bundle = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "source": {
            "virtualPath": GAMEDATA_INI,
            "archive": entry.archive,
            "bytes": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
        },
        "totals": {
            "defines": len(table),
            "numeric": numeric,
            "nonNumeric": len(table) - numeric,
        },
        "defines": dict(sorted(table.items())),
    }
    output = pathlib.Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(bundle, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return bundle


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m openbfme_importer.living_world_macros",
        description="Convert retail's gamedata.ini #define table.",
    )
    parser.add_argument("--catalog", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args(argv)
    bundle = build_bundle(args.catalog, args.out)
    totals = bundle["totals"]
    print(
        f"wrote {args.out} - {totals['defines']} defines "
        f"({totals['numeric']} numeric, {totals['nonNumeric']} left as raw text)"
    )
    for name in ("FERTILE_TERRITORY_BONUS", "WOTR_FORTRESS_COST", "WOTR_BARRACKS_COST"):
        record = bundle["defines"].get(name)
        print(f"  {name} = {record['raw'] if record else 'ABSENT'}")
    return 0


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    raise SystemExit(main())
