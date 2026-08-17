"""Make published pack directories read-only, so in-place cooking fails loudly.

A pack directory named <sha256> promises its bytes hash to that name. Three
separate lanes broke that promise in one night - a CaH recook rewriting
data/cah/system.json, an asset lane dropping 16 converted files in, and a
host-bridge merge - and every time the runtime went on loading a pack whose
address was a lie while name-shaped checks kept passing.

tools/check_pack_addresses.py catches that AFTER the fact. This seals it BEFORE:
the OS refuses the write at the moment of the mistake, next to the code that
made it, instead of an audit finding it hours later.

Costs legitimate flows nothing. Publishing writes a NEW digest directory, which
this tool then seals; it never edits an existing one. Reading is unaffected.

Removing a sealed pack needs the attribute cleared first:
    PowerShell:  Remove-Item -Recurse -Force <dir>      (handles it)
    Python:      unseal that pack, or pass an onexc handler to shutil.rmtree

Usage:
    python tools/seal_published_packs.py [--unseal] [--pack ID/DIGEST] [--json]

Exit codes: 0 sealed/unsealed cleanly / 1 nothing to do or a path refused.
"""

from __future__ import annotations

import argparse
import json
import os
import stat
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]

WRITE_BITS = stat.S_IWRITE | stat.S_IWGRP | stat.S_IWOTH


def default_.private_root() -> Path:
    return REPO_ROOT / "workspace" / "content-packs"


def default_durable_root() -> Path | None:
    appdata = os.environ.get("APPDATA")
    if not appdata:
        return None
    return Path(appdata) / "Godot" / "app_userdata" / "Open BFME" / "content-packs"


def selection_entries(root: Path) -> list[str]:
    document = json.loads((root / "selection.json").read_text(encoding="utf-8"))
    entries = [str(document["activePack"])]
    entries.extend(str(value) for value in document.get("supplementalPacks", []))
    return entries


def apply(path: Path, unseal: bool) -> int:
    """Seal or unseal every file under path. Returns the count changed."""
    changed = 0
    for current, _dirs, files in os.walk(path):
        for name in files:
            target = Path(current) / name
            try:
                mode = target.stat().st_mode
            except OSError:
                continue
            writable = bool(mode & stat.S_IWRITE)
            if unseal and not writable:
                os.chmod(target, mode | stat.S_IWRITE)
                changed += 1
            elif not unseal and writable:
                os.chmod(target, mode & ~WRITE_BITS)
                changed += 1
    return changed


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--.private-root", type=Path, default=default_.private_root())
    parser.add_argument("--durable-root", type=Path, default=default_durable_root())
    parser.add_argument(
        "--unseal",
        action="store_true",
        help="make packs writable again (for a re-address or a deletion)",
    )
    parser.add_argument(
        "--pack",
        default="",
        metavar="ID/DIGEST",
        help="operate on one selection entry instead of every selected pack",
    )
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    roots: list[tuple[str, Path]] = []
    for label, root in (("workspace", args..private_root), ("durable", args.durable_root)):
        if root is not None and (root / "selection.json").is_file():
            roots.append((label, root))
    if not roots:
        print("SEAL_PACKS UNEVALUATED: no selection.json under either root")
        return 1

    verb = "unsealed" if args.unseal else "sealed"
    records: list[dict] = []
    for label, root in roots:
        entries = selection_entries(root)
        if args.pack:
            entries = [entry for entry in entries if entry == args.pack]
        for entry in entries:
            pack_id, _, digest = entry.rpartition("/")
            pack_dir = root / pack_id / digest
            if not pack_dir.is_dir():
                records.append({"root": label, "entry": entry, "changed": 0, "missing": True})
                continue
            changed = apply(pack_dir, args.unseal)
            records.append({"root": label, "entry": entry, "changed": changed})

    if args.json:
        print(json.dumps({"action": verb, "packs": records}, indent=2, sort_keys=True))
    else:
        for record in records:
            if record.get("missing"):
                print(f"SEAL_PACKS MISSING {record['root']} {record['entry']}")
            else:
                print(f"SEAL_PACKS {verb.upper()} {record['root']} {record['entry']} files={record['changed']}")

    missing = [record for record in records if record.get("missing")]
    total = sum(record["changed"] for record in records)
    print(f"SEAL_PACKS DONE action={verb} packs={len(records) - len(missing)} files_changed={total}")
    if missing:
        print(f"SEAL_PACKS WARN missing={len(missing)} (selection names a pack that is not on disk)")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
