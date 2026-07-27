"""Convert the SHELL half of retail's string table - the namespaces the War of
the Ring GAME SETUP screen reads and the strategic map screen does not.

WHY A SECOND BUNDLE RATHER THAN MORE PREFIXES ON THE FIRST
----------------------------------------------------------
``living_world_strings`` converts the namespaces the strategic *map* reads:
``LW:``, ``LWA:``, ``LWScenario:``, ``RULE:``, ``VALUE:``, ``STRATEGICHUD:``
and ``CONTROLBAR:LW_``. That is a deliberate, documented set and this module
does not widen it - a lane that is live in that file would find its bundle
changed underneath it.

The GAME SETUP screen needs a DIFFERENT set, and none of it is in that bundle:

* ``APT:``  - every heading, column title and button caption on the screen
  (``APT:GameSetup``, ``APT:TabMap``, ``APT:TabRules``, ``APT:Side``,
  ``APT:HeaderTeam``, ``APT:HdrHandicap``, ``APT:StartGame``, ...).
* ``Apt:``  - retail spells exactly one of them with a lowercase tail,
  ``Apt:Scenario``. ``APT:Scenario`` DOES NOT EXIST. Case is load-bearing and
  this module carries both spellings rather than folding them.
* ``SIDE:`` - the Army column's faction names. ``playertemplate.ini`` states
  outright that the presence of ``SIDE:Elves`` in the string file is what makes
  a side appear in multiplayer setup, so this is the setup namespace, not
  ``INI:Faction*``. Carried anyway: ``INI:Faction`` is the ``DisplayName`` field
  on the template itself, and the screen reports when the two disagree.
* ``Color:`` - the six ``AvailableInWotR`` colour names.
* ``GUI:``  - ``GUI:EasyAI`` / ``MediumAI`` / ``HardAI`` / ``BrutalAI``, the
  mixed-case AI tier names retail puts in a Player cell.
* ``ToolTip:WaroftheRing_`` - the four RULES-row tooltips retail authored, and
  the only four. Rows without one are shown without one.

WHAT IT REFUSES TO DO
---------------------
It NEVER invents a label, and it never derives one. A screen element whose key
is not in this bundle is drawn with its retail KEY visible and named in the
screen's own "did not resolve" list. It shares
``living_world_strings.parse_str`` verbatim - one parser, one defect list, one
set of retail table bugs reported the same way in both bundles.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib

from .livingmap_bundle import CatalogReader
from .living_world_strings import (
    MAX_ASSET_BYTES,
    STRING_TABLE,
    StringTableError,
    parse_str,
)

SCHEMA = "openbfme.wotr-setup-strings"
SCHEMA_VERSION = 1

#: Namespaces the GAME SETUP screen reads. Every one of them is a prefix that
#: exists in retail's shipped table; none is speculative.
DEFAULT_PREFIXES = (
    "APT:",  # headings, column titles, button captions
    "Apt:",  # retail's lone lowercase spelling: Apt:Scenario
    "SIDE:",  # the Army column's faction names
    "INI:Faction",  # the PlayerTemplate DisplayName for the same factions
    "Color:",  # the multiplayer colour names
    # The AI tier names a Player cell shows. Named ONE BY ONE rather than by a
    # bare `GUI:` prefix, which would drag 835 unrelated blocks into a bundle
    # that uses four of them.
    "GUI:EasyAI",
    "GUI:MediumAI",
    "GUI:HardAI",
    "GUI:BrutalAI",
    "ToolTip:WaroftheRing_",  # the four authored RULES-row tooltips
    "TOOLTIP:Skirmish/tooltipHandicap",  # the one handicap tooltip retail wrote
)


def build_bundle(
    catalog_path: pathlib.Path | str,
    output_path: pathlib.Path | str,
    *,
    prefixes: tuple[str, ...] = DEFAULT_PREFIXES,
) -> dict:
    """Write the setup-screen string bundle and return it."""
    reader = CatalogReader(catalog_path)
    entry = reader.resolve(STRING_TABLE)
    if entry is None:
        raise StringTableError(
            f"{STRING_TABLE} is not in the catalog, so the GAME SETUP screen has no labels"
        )
    if entry.size > MAX_ASSET_BYTES:
        raise StringTableError(f"{STRING_TABLE} is {entry.size} bytes, over the read ceiling")
    payload = reader.read(entry)
    text = payload.decode("cp1252", errors="replace")

    table, defects = parse_str(text)
    kept = {
        key: value
        for key, value in table.items()
        if any(key.startswith(prefix) for prefix in prefixes)
    }
    counts = {prefix: sum(1 for key in kept if key.startswith(prefix)) for prefix in prefixes}

    bundle = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "source": {
            "virtualPath": STRING_TABLE,
            "archive": entry.archive,
            "bytes": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
            "encoding": "cp1252",
        },
        "prefixes": list(prefixes),
        "totals": {
            "blocksInTable": len(table),
            "blocksKept": len(kept),
            "defects": len(defects),
            "byPrefix": counts,
        },
        "defects": defects[:200],
        "strings": dict(sorted(kept.items())),
    }
    output = pathlib.Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(bundle, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return bundle


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m openbfme_importer.wotr_setup_strings",
        description="Convert retail's data/lotr.str into the GAME SETUP screen's label table.",
    )
    parser.add_argument("--catalog", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args(argv)

    bundle = build_bundle(args.catalog, args.out)
    totals = bundle["totals"]
    print(
        f"wrote {args.out} - {totals['blocksKept']} of {totals['blocksInTable']} "
        f"blocks kept, {totals['defects']} defect(s)"
    )
    for prefix, count in sorted(totals["byPrefix"].items()):
        print(f"  {prefix:<36} {count}")
    return 0


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    raise SystemExit(main())
