"""Fail-closed check: every selected pack's BYTES must match its content ADDRESS.

Packs are immutable. A pack directory named <sha256> is a promise that its
contents hash to that name. Cooking new output into a published pack directory
breaks that promise silently: the runtime keeps loading it, gates that only
match the NAME shape keep passing, and every downstream claim - "this build
used pack X" - becomes unfalsifiable.

This has happened three times in one session (map-script repair, CaH system
recook, and a host-bridge merge). Each time the fix was the same: publish a NEW
digest and swap the selection with apply-selection-transaction. This check
makes the mistake loud within seconds instead of surviving until an audit.

Usage:
    python tools/check_pack_addresses.py [--durable-root PATH] [--json]

Exit codes: 0 all addresses honest / 1 drift found / 2 could not evaluate.
"""

from __future__ import annotations

import argparse
import json
import os
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "importer"))

from openbfme_importer.pipeline import bundle_digest  # noqa: E402


def default_workspace_root() -> Path:
    return REPO_ROOT / ".private" / "content-packs"


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


def check_root(label: str, root: Path) -> tuple[list[dict], list[dict]]:
    """Return (honest, drifted) records for every selection entry under root."""
    honest: list[dict] = []
    drifted: list[dict] = []
    for entry in selection_entries(root):
        pack_id, _, declared = entry.rpartition("/")
        pack_dir = root / pack_id / declared
        record = {"root": label, "entry": entry, "path": str(pack_dir)}
        if not pack_dir.is_dir():
            record["actual"] = None
            record["reason"] = "pack directory is missing"
            drifted.append(record)
            continue
        actual = bundle_digest(pack_dir)
        record["declared"] = declared
        record["actual"] = actual
        if actual == declared:
            honest.append(record)
        else:
            record["reason"] = "contents were modified after publication"
            drifted.append(record)
    return honest, drifted


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--workspace-root", type=Path, default=default_workspace_root())
    parser.add_argument("--durable-root", type=Path, default=default_durable_root())
    parser.add_argument("--json", action="store_true")
    args = parser.parse_args(argv)

    roots: list[tuple[str, Path]] = [("workspace", args.workspace_root)]
    if args.durable_root is not None and (args.durable_root / "selection.json").is_file():
        roots.append(("durable", args.durable_root))

    honest: list[dict] = []
    drifted: list[dict] = []
    for label, root in roots:
        if not (root / "selection.json").is_file():
            print(f"PACK_ADDRESS_CHECK UNEVALUATED {label}: no selection.json at {root}")
            return 2
        root_honest, root_drifted = check_root(label, root)
        honest.extend(root_honest)
        drifted.extend(root_drifted)

    if args.json:
        print(json.dumps({"honest": honest, "drifted": drifted}, indent=2, sort_keys=True))
    else:
        for record in drifted:
            print(
                "PACK_ADDRESS_DRIFT {root} {entry}\n"
                "  declared {declared}\n"
                "  actual   {actual}\n"
                "  {reason}".format(
                    root=record["root"],
                    entry=record["entry"],
                    declared=record.get("declared", "(none)"),
                    actual=record.get("actual") or "(unreadable)",
                    reason=record["reason"],
                )
            )

    if drifted:
        print(
            f"PACK_ADDRESS_CHECK FAIL drifted={len(drifted)} honest={len(honest)}\n"
            "  A published pack was modified in place. Do NOT hand-edit it back.\n"
            "  Republish the corrected contents as a NEW digest and swap with:\n"
            "    python -m openbfme_importer.cli apply-selection-transaction \\\n"
            "      --active-pack <pack-id>/<new-sha256> \\\n"
            "      --supplemental-pack <each supplement, in order> \\\n"
            "      --godot-content-root .private/content-packs \\\n"
            "      --durable-root \"%APPDATA%\\Godot\\app_userdata\\Open BFME\\content-packs\""
        )
        return 1

    print(f"PACK_ADDRESS_CHECK PASS packs={len(honest)} roots={len(roots)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
