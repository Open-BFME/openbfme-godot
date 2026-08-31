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
    python tools/check_pack_addresses.py [--packs-root PATH] [--json]

Exit codes: 0 all addresses honest / 1 drift found / 2 could not evaluate.
"""

from __future__ import annotations

import argparse
import json
import os
import re
import sys
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(REPO_ROOT / "importer"))

from openbfme_importer.pipeline import bundle_digest  # noqa: E402
from openbfme_importer.pack_recipe_catalog_identity import audit_pack_target_identity  # noqa: E402


BASELINE = REPO_ROOT / "contracts" / "rotwk-202-v9.7.7-baseline.json"
SHA256_RE = re.compile(r"^[0-9a-f]{64}$")


def default_packs_root() -> Path:
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


def _assignment_main_root() -> Path | None:
    assignments = list((REPO_ROOT / "workspace" / "logs").glob("*/assignment.json"))
    if len(assignments) != 1:
        return None
    value = json.loads(assignments[0].read_text(encoding="utf-8"))
    main = Path(str(value.get("mainPath", "")))
    return main.resolve() if main.is_dir() else None


def _workspace_root(path: Path) -> Path:
    if (path / "selection.json").is_file():
        return path
    main = _assignment_main_root()
    fallback = main / "workspace" / "content-packs" if main else None
    return fallback if fallback and (fallback / "selection.json").is_file() else path


def _target_contract(baseline_id: str) -> tuple[str, str]:
    baseline = json.loads(BASELINE.read_text(encoding="utf-8"))
    expected_id = str(baseline.get("baselineId", ""))
    catalog = str(baseline.get("authority", {}).get("catalogSha256", ""))
    if baseline_id != expected_id or SHA256_RE.fullmatch(catalog) is None:
        raise ValueError("required baseline differs from the tracked exact target")
    return expected_id, catalog


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


def audit_root_identity(
    label: str, root: Path, *, baseline_id: str, catalog_sha256: str,
) -> list[dict]:
    records: list[dict] = []
    for entry in selection_entries(root):
        pack_id, _, declared = entry.rpartition("/")
        pack_dir = root / pack_id / declared
        manifest = pack_dir / "pack.json"
        record: dict = {"root": label, "entry": entry}
        if not manifest.is_file():
            record.update({
                "matchesTarget": False,
                "observed": {"baselineId": None, "catalogSha256": None, "recipeSha256": None},
                "failures": ["pack-manifest-missing"],
            })
        else:
            pack = json.loads(manifest.read_text(encoding="utf-8"))
            if not isinstance(pack, dict):
                raise ValueError(f"pack manifest is not an object: {entry}")
            record.update(audit_pack_target_identity(
                pack, baseline_id=baseline_id, catalog_sha256=catalog_sha256,
            ))
        records.append(record)
    return records


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--packs-root", type=Path, default=default_packs_root())
    parser.add_argument("--durable-root", type=Path, default=default_durable_root())
    parser.add_argument("--json", action="store_true")
    parser.add_argument("--require-baseline")
    args = parser.parse_args(argv)

    packs_root = _workspace_root(args.packs_root)
    roots: list[tuple[str, Path]] = [("workspace", packs_root)]
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

    identity_records: list[dict] = []
    if args.require_baseline:
        baseline_id, catalog_sha256 = _target_contract(args.require_baseline)
        for label, root in roots:
            identity_records.extend(audit_root_identity(
                label, root, baseline_id=baseline_id, catalog_sha256=catalog_sha256,
            ))
        matched = sum(bool(row["matchesTarget"]) for row in identity_records)
        report = {
            "schema": "openbfme.legacy-selection-audit",
            "schemaVersion": 1,
            "baselineId": baseline_id,
            "catalogSha256": catalog_sha256,
            "roots": [label for label, _ in roots],
            "selectedEntries": len(identity_records),
            "targetMatches": matched,
            "addressDrift": len(drifted),
            "records": identity_records,
        }
        artifact = REPO_ROOT / "workspace" / "logs" / "P0-SELECTION-001" / "legacy-selection-audit.json"
        artifact.parent.mkdir(parents=True, exist_ok=True)
        artifact.write_text(json.dumps(report, indent=2, sort_keys=True) + "\n", encoding="utf-8", newline="\n")
        if args.json:
            print(json.dumps(report, indent=2, sort_keys=True))
        if drifted:
            print(f"PACK_ADDRESS_NEGATIVE FAIL address-drift={len(drifted)}", file=sys.stderr)
            return 1
        if matched:
            print(f"PACK_ADDRESS_NEGATIVE FAIL target-markers={matched}", file=sys.stderr)
            return 1
        print(f"PACK_ADDRESS_NEGATIVE PASS baseline={baseline_id}")
        return 0

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
            "      --godot-content-root workspace/content-packs \\\n"
            "      --durable-root \"%APPDATA%\\Godot\\app_userdata\\Open BFME\\content-packs\""
        )
        return 1

    print(f"PACK_ADDRESS_CHECK PASS packs={len(honest)} roots={len(roots)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
