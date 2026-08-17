from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.profile import ImportProfile


_CENSUS_GAME_LABELS = {"bfme2": ("BFME2", "1.06"), "rotwk": ("RotWK", "2.01")}


def assert_census_edition(report: dict, game: str) -> None:
    """Refuse a census produced from a different retail edition than --game.

    The catalog and the census are two independent inputs; crossing them
    composes one edition's announcer bytes under the other edition's identity,
    which nothing downstream would catch.
    """

    expected = _CENSUS_GAME_LABELS[game]
    target = report.get("target")
    if not isinstance(target, dict):
        raise ValueError("audio census has no target identity")
    actual = (target.get("game"), target.get("patch"))
    if actual != expected:
        raise ValueError(
            "audio census edition %r does not match --game %s (expected %r)"
            % (actual, game, expected)
        )


def catalog_path(state_root: Path, game: str) -> Path:
    """Catalog for one retail edition, addressed exactly as the importer CLI does."""

    return state_root / "catalog" / ("%s.json" % game)


def oracle_catalog(state_root: Path, game: str, catalog):
    """Bind the same source the build pipeline will read.

    For RotWK the pipeline replaces the BIG catalog with the sealed
    effective-assets tree (`ImportPipeline.__init__`), and the composed profile
    records that tree's identity. Stamping the BIG catalog's identity instead
    makes every build refuse with "profile source catalog identity does not
    match the current catalog" - and would describe bytes other than the ones
    the pack is cooked from.
    """

    if game != "rotwk":
        return catalog
    from openbfme_importer.effective_assets_catalog import EffectiveAssetsCatalog
    from openbfme_importer.effective_assets_identity import verify_effective_assets

    root = state_root / "editions" / "rotwk" / "cache" / "effective-assets"
    if not root.is_dir():
        raise ValueError("RotWK effective-assets oracle is missing: %s" % root)
    verify_effective_assets(root, game="rotwk", catalog=None, consumer="compose-eva-overlay")
    return EffectiveAssetsCatalog(root, base_catalog=catalog)


def build_parser() -> argparse.ArgumentParser:
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
    parser.add_argument(
        "--game",
        choices=("bfme2", "rotwk"),
        default="bfme2",
        help=(
            "retail edition whose catalog and eva.ini are read (default: bfme2, "
            "the edition the first overlay was composed from)"
        ),
    )
    parser.add_argument("--pack-id", required=True)
    parser.add_argument("--pack-title", default=None)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--receipt", type=Path, required=True)
    parser.add_argument(
        "--state-root",
        type=Path,
        default=None,
        help="importer state root (default: workspace/retail-work); its catalog is required",
    )
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()

    state_root = args.state_root or (ROOT / "workspace" / "retail-work")
    catalog_file = catalog_path(state_root, args.game)
    if not catalog_file.is_file():
        parser.error("state-root catalog is required (not found: %s)" % catalog_file)
    from openbfme_importer.catalog import InstallCatalog
    from openbfme_importer.faction_profile import build_faction_audio_extension

    catalog = oracle_catalog(state_root, args.game, InstallCatalog.load(catalog_file))
    report = json.loads(args.audio_census.read_text(encoding="utf-8"))
    assert_census_edition(report, args.game)
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
        "unplayableRetailReferences": extension["evaDiagnostics"],
        "semanticFieldCoverage": extension["evaSemanticFieldCoverage"],
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
