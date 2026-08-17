from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.faction_slice_profile import compose_faction_profile
from openbfme_importer.profile import ImportProfile


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-profile", type=Path, required=True)
    parser.add_argument("--report-root", type=Path, required=True)
    parser.add_argument("--faction", action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument(
        "--pack-id",
        default=None,
        help="override the composed pack id (default: keep the base profile pack id)",
    )
    parser.add_argument(
        "--pack-title",
        default=None,
        help="override the composed profile title (default: keep the base profile title)",
    )
    parser.add_argument(
        "--audio-census",
        type=Path,
        default=None,
        help="faction leaf census report (e.g. workspace/retail-work/reports/"
        "<faction>-faction-leaf-census.json); when given, the composed pack "
        "ships the faction's schemaVersion-1 audio registry, its converted "
        "audio samples, and the eva.ini announcer side map instead of the "
        "base profile's legacy 5-event stub",
    )
    parser.add_argument(
        "--state-root",
        type=Path,
        default=None,
        help="importer state root (default: workspace/retail-work); when its "
        "catalog exists, the composed pack is stamped with the source catalog "
        "identity that m3-bound builds require",
    )
    args = parser.parse_args()
    base = json.loads(args.base_profile.read_text(encoding="utf-8"))
    profile, receipt = compose_faction_profile(base, args.report_root, args.faction)
    if args.pack_id is not None:
        profile["pack"]["id"] = args.pack_id
    if args.pack_title is not None:
        profile["title"] = args.pack_title
    # Freshly composed profiles bind to the catalog they were composed against;
    # inherited m3 markers otherwise fail the build's source catalog identity
    # check with no stamping path (mirrors cli.py's publish handler).
    state_root = args.state_root or (ROOT / "workspace" / "retail-work")
    catalog_path = state_root / "catalog" / "bfme2.json"
    catalog = None
    if catalog_path.is_file():
        from openbfme_importer.catalog import InstallCatalog

        catalog = InstallCatalog.load(catalog_path)
        pack = profile.get("pack")
        if isinstance(pack, dict) and "sourceCatalogIdentitySha256" not in pack:
            pack["sourceCatalogIdentitySha256"] = catalog.identity_sha256()
    if args.audio_census is not None:
        if catalog is None:
            parser.error("--audio-census requires the state-root catalog (not found: %s)" % catalog_path)
        if len(args.faction) != 1:
            parser.error("--audio-census supports exactly one --faction per run")
        faction_sides = {
            "elves": "Elves",
            "dwarves": "Dwarves",
            "isengard": "Isengard",
            "mordor": "Mordor",
            "wild": "Wild",
        }
        faction_key = args.faction[0].strip().lower()
        side = faction_sides.get(faction_key)
        if side is None:
            parser.error("--audio-census has no eva side for faction %r" % faction_key)
        from openbfme_importer.faction_profile import build_faction_audio_extension

        audio_report = json.loads(args.audio_census.read_text(encoding="utf-8"))
        extension = build_faction_audio_extension(catalog, audio_report, side)
        existing_ids = {
            str(row["id"]) for row in profile["resources"] if isinstance(row, dict) and "id" in row
        }
        for row in extension["resources"]:
            if row["id"] in existing_ids:
                raise ValueError(f"audio extension resource id collision: {row['id']}")
            profile["resources"].append(row)
        profile["runtime_data"].update(extension["runtime_data"])
        profile["pack"]["files"].update(extension["files"])
        audio_manifest = extension["runtime_data"]["data/audio_events.json"]
        receipt["audioExtension"] = {
            "eventCount": len(audio_manifest["events"]),
            "multisoundCount": len(audio_manifest["multisounds"]),
            "sampleCount": len(audio_manifest["samples"]),
            "evaEventCount": len(extension["runtime_data"]["data/eva_events.json"]["events"]),
            "unresolvedDiagnostics": extension["unresolvedDiagnostics"],
        }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.receipt.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    args.receipt.write_text(json.dumps(receipt, indent=2, sort_keys=True, ensure_ascii=False) + "\n", encoding="utf-8")
    ImportProfile.load(args.output)
    print(json.dumps({"profile": str(args.output), "receipt": str(args.receipt), "objectCount": len(receipt["objects"])}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
