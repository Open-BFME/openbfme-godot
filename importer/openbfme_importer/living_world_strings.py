"""Convert retail's string table into the labels the War of the Ring screen
shows, so regions stop carrying raw retail ids.

WHY THIS MODULE EXISTS
----------------------
Every human-readable name in the living world is a STRING TABLE KEY, not a
name. The living-world document carries ``displayName = "LW:DisplayNameArnor"``
for a region, ``territory = "LW:TerritoryEriador"`` for a territory group, and
``displayNameTag = "LWA:MenHeroArmy"`` for an army. The strategic screen has
been showing retail's internal ids (``The_Black_Gate``) because nothing had ever
converted the table - printing the key would be noise and inventing a name
would be worse.

The table is not a CSF. RotWK ships it as PLAIN TEXT: ``data/lotr.str``, ~1.2 MB,
CRLF, single-byte, in this grammar::

    KEY:SubKey
    // an optional comment line
    "the value text"
    END

The only ``.csf`` files in the build are ``launcher/gamelauncher.csf`` and
``launcher/launcher.csf``, which are installer strings, not game text.

WHAT IT REFUSES TO DO
---------------------
* It NEVER invents a label. A key with no block in the table is absent from the
  output, and the Godot side falls back to the retail id and says so.
* It NEVER guesses a key from a region id. Retail's key spelling and its region
  spelling disagree in real cases, so only a key that is literally present in
  the table produces a label.
* It reads through the CATALOGS, never through the effective-assets cache.

WHAT IT DOES NOT TRY TO DO
--------------------------
Retail's values carry ``\\n`` escapes, ``&`` markup (``&dropshadow``) and
printf-style placeholders (``+%d Treasure``). Those are preserved VERBATIM.
Interpreting them is the consumer's job; rewriting them here would be a silent
edit to retail text.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re

from .livingmap_bundle import CatalogReader

SCHEMA = "openbfme.living-world-strings"
SCHEMA_VERSION = 1

STRING_TABLE = "data/lotr.str"

#: Key namespaces the strategic layer actually reads. Everything else in the
#: table (unit tooltips, campaign text, the RTS control bar) is a different
#: lane's problem, and carrying 12,634 blocks into a UI bundle to use ~1,200 of
#: them would be waste, not honesty.
DEFAULT_PREFIXES = (
    "LW:",  # region, territory and notice text
    "LWA:",  # army and garrison names
    "LWScenario:",  # scenario names and descriptions
    "RULE:",  # the GAME SETUP screen's rule row labels
    "VALUE:",  # the GAME SETUP screen's dropdown values
    "STRATEGICHUD:",  # the in-play HUD
    "CONTROLBAR:LW_",  # build-plot and structure tooltips
)

#: Retail's own byte ceiling for one asset read.
MAX_ASSET_BYTES = 64 * 1024 * 1024

_KEY = re.compile(r"^[A-Za-z0-9_]+:[^\s\"]*$")


class StringTableError(RuntimeError):
    """The table cannot be read, with the exact reason."""


def parse_str(text: str) -> tuple[dict[str, str], list[dict]]:
    """Parse a retail ``.str`` table into ``{key: value}`` plus a defect list.

    A block that never reaches ``END``, or that carries no quoted value, is
    recorded as a DEFECT and left out of the table rather than half-read. The
    caller decides whether a defective table is shippable; this function never
    silently repairs one.
    """
    table: dict[str, str] = {}
    defects: list[dict] = []
    key: str | None = None
    key_line = 0
    value: str | None = None

    for number, raw in enumerate(text.splitlines(), start=1):
        line = raw.strip()
        if not line or line.startswith("//") or line.startswith(";"):
            continue
        if key is None:
            if not _KEY.match(line):
                defects.append({"line": number, "reason": "expected a key", "text": line[:120]})
                continue
            key = line
            key_line = number
            value = None
            continue
        if line.upper() == "END":
            if value is None:
                defects.append({"line": key_line, "reason": "block has no value", "text": key})
            elif key in table and table[key] != value:
                defects.append(
                    {"line": key_line, "reason": "duplicate key with a different value", "text": key}
                )
            else:
                table[key] = value
            key = None
            value = None
            continue
        if line.startswith('"'):
            # Retail quotes the value on one line. The closing quote is the LAST
            # one on the line, because values legitimately contain quotes.
            body = line[1:]
            if body.endswith('"'):
                body = body[:-1]
            value = body
            continue
        defects.append({"line": number, "reason": "unexpected line inside a block", "text": line[:120]})

    if key is not None:
        defects.append({"line": key_line, "reason": "block never reached END", "text": key})
    return table, defects


def build_bundle(
    catalog_path: pathlib.Path | str,
    output_path: pathlib.Path | str,
    *,
    prefixes: tuple[str, ...] = DEFAULT_PREFIXES,
) -> dict:
    """Write the string bundle and return it."""
    reader = CatalogReader(catalog_path)
    entry = reader.resolve(STRING_TABLE)
    if entry is None:
        raise StringTableError(
            f"{STRING_TABLE} is not in the catalog, so no display names can be converted"
        )
    if entry.size > MAX_ASSET_BYTES:
        raise StringTableError(f"{STRING_TABLE} is {entry.size} bytes, over the read ceiling")
    payload = reader.read(entry)
    # Retail writes this table in the single-byte Windows codepage, not UTF-8.
    text = payload.decode("cp1252", errors="replace")

    table, defects = parse_str(text)
    kept = {
        key: value
        for key, value in table.items()
        if any(key.startswith(prefix) for prefix in prefixes)
    }

    counts = {
        prefix: sum(1 for key in kept if key.startswith(prefix)) for prefix in prefixes
    }
    counts["LW:DisplayName"] = sum(1 for key in kept if key.startswith("LW:DisplayName"))

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
        # NAMED, not swallowed. A table this lane could not fully parse is a
        # defect the consumer has to be able to see.
        "defects": defects[:200],
        "strings": dict(sorted(kept.items())),
    }
    output = pathlib.Path(output_path)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(bundle, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return bundle


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m openbfme_importer.living_world_strings",
        description="Convert retail's data/lotr.str into the strategic layer's label table.",
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
        print(f"  {prefix:<20} {count}")
    return 0


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    raise SystemExit(main())
