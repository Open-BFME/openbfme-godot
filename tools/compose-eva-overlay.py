from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.profile import ImportProfile


def main() -> int:
    parser = argparse.ArgumentParser(
        description=(
            "Compose a minimal EVA announcer overlay profile for one faction: "
            "the faction side's eva.ini Camp* sound sets plus the global eva "
            "side map, mounted next to a full host pack (which keeps its own "
            "registry untouched)."
        )
    )
    parser.add_argument("--audio-census", type=Path, required=True)
    parser.add_argument("--faction", required=True, help="census faction side (Men, Elves, ...)")
    parser.add_argument("--pack-id", required=True)
    parser.add_argument("--pack-title", default=None)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument(
        "--state-root",
        type=Path,
        default=None,
        help="importer state root (default: .private/retail-work); its catalog is required",
    )
    args = parser.parse_args()

    state_root = args.state_root or (ROOT / ".private" / "retail-work")
    catalog_path = state_root / "catalog" / "bfme2.json"
    if not catalog_path.is_file():
        parser.error("state-root catalog is required (not found: %s)" % catalog_path)
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.faction_profile import build_faction_audio_extension

    catalog = InstallCatalog.load(catalog_path)
    report = json.loads(args.audio_census.read_text(encoding="utf-8"))
    extension = build_faction_audio_extension(
        catalog, report, args.faction, include_census_registry=False
    )

    profile = {
        "format": 1,
        "id": "faction-eva-overlay-" + args.faction.casefold(),
        "title": args.pack_title or ("BFME2 1.06 %s EVA announcer overlay" % args.faction),
        "pack": {
            "id": args.pack_id,
            "version": "1.06-eva-v0",
            "schema": "openbfme.content-pack",
            "schemaVersion": 0,
            "priority": 906,
            "vertical_slice_complete": False,
            "full_faction_complete": False,
            "asset_conversion_complete": False,
            "oracle_parity_complete": False,
            "capability_maturity": "eva-announcer-overlay",
            "censusInputSha256": report["inputSetSha256"],
            "sourceCatalogIdentitySha256": catalog.identity_sha256(),
            "dataPolicy": {
                "externalPathsAllowed": False,
                "redistributable": False,
            },
            "files": extension["files"],
        },
        "resources": extension["resources"],
        "runtime_data": extension["runtime_data"],
    }
    receipt = {
        "faction": args.faction,
        "packId": args.pack_id,
        "eventCount": len(extension["runtime_data"]["data/audio_events.json"]["events"]),
        "multisoundCount": len(extension["runtime_data"]["data/audio_events.json"]["multisounds"]),
        "sampleCount": len(extension["runtime_data"]["data/audio_events.json"]["samples"]),
        "evaEventCount": len(extension["runtime_data"]["data/eva_events.json"]["events"]),
        "unresolvedDiagnostics": extension["unresolvedDiagnostics"],
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(
        json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    args.receipt.write_text(
        json.dumps(receipt, indent=2, sort_keys=True, ensure_ascii=False) + "\n",
        encoding="utf-8",
    )
    ImportProfile.load(args.output)
    print(json.dumps({"profile": str(args.output), "receipt": str(args.receipt)}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
