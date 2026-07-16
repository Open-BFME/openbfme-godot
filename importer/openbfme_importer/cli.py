"""Command-line interface for the local-only BFME II importer."""

from __future__ import annotations

import argparse
from dataclasses import asdict
import hashlib
import json
from pathlib import Path
import sys
from typing import Any

from .catalog import InstallCatalog, doctor_install
from .bootstrap import bootstrap_tools, tool_status
from .asset_census import census_assets
from .game import RETAIL_GAME_IDS, workspace_root
from .paths import (
    default_godot_content_root,
    default_state_root,
    ensure_external_to_repo,
    repo_root_from_module,
)
from .pipeline import ImportPipeline, audit_pack, bundle_digest
from .faction_census import census_men_faction
from .faction_profile import build_men_leaf_profile
from .map_profile import build_five_map_profile
from .map_census import census_multiplayer_maps
from .profile import ImportProfile, profile_path, resolve_profile
from .retail_visual_closure import (
    build_retail_visual_closure,
    default_visual_closure_report_name,
)
from .sage_roads import build_road_closure, default_road_closure_report_name
from .tools import inspect_tool
from .util import write_json_atomic
from .version import __version__


PROFILES_ROOT = Path(__file__).resolve().parents[1] / "profiles"


def _render(value: Any, as_json: bool) -> None:
    if as_json:
        print(json.dumps(value, indent=2, sort_keys=True))
        return
    if isinstance(value, dict):
        for key, item in value.items():
            if isinstance(item, (dict, list)):
                print(f"{key}: {json.dumps(item, sort_keys=True)}")
            else:
                print(f"{key}: {item}")
    else:
        print(value)


def _state_root(args: argparse.Namespace) -> Path:
    return ensure_external_to_repo(Path(args.state_root), repo_root_from_module())


def _catalog_path(args: argparse.Namespace) -> Path:
    return _state_root(args) / "catalog" / f"{args.game}.json"


def _workspace_root(args: argparse.Namespace) -> Path:
    return workspace_root(_state_root(args), args.game)


def _add_game_argument(command: argparse.ArgumentParser) -> None:
    command.add_argument(
        "--game",
        choices=RETAIL_GAME_IDS,
        default="bfme2",
        help="retail game identity; expansion state is isolated from BFME2",
    )


def _load_or_build_catalog(args: argparse.Namespace) -> InstallCatalog:
    path = _catalog_path(args)
    install = Path(args.install).expanduser().resolve()
    if path.is_file() and not args.reindex:
        try:
            catalog = InstallCatalog.load(path)
            if catalog.install_root == install and not catalog.stale_reasons():
                return catalog
        except (OSError, ValueError, KeyError, TypeError):
            pass
    catalog = InstallCatalog.build(install)
    catalog.save(path)
    return catalog


def _resolved(args: argparse.Namespace, catalog: InstallCatalog):
    selected = profile_path(args.profile, PROFILES_ROOT)
    return resolve_profile(ImportProfile.load(selected), catalog)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(
        prog="openbfme-import",
        description="Build private Godot content packs from a user-owned BFME II install.",
    )
    parser.add_argument("--version", action="version", version=__version__)
    parser.add_argument("--json", action="store_true", help="machine-readable output")
    parser.add_argument(
        "--state-root",
        default=str(default_state_root()),
        help="private cache/pack root (defaults to the ignored .private workspace)",
    )
    sub = parser.add_subparsers(dest="command", required=True)

    bootstrap = sub.add_parser("bootstrap-tools", help="provision and hash-pin private conversion tools")
    bootstrap.add_argument("--ffmpeg", type=Path, default=None, help="path to the pinned FFmpeg 8.1.1 executable")

    doctor = sub.add_parser("doctor", help="diagnose an install and conversion tools")
    doctor.add_argument("--install", required=True)
    _add_game_argument(doctor)
    doctor.add_argument("--deep", action="store_true", help="hash-attest every archive used by the profile")

    index = sub.add_parser("index", help="stream archive directories into a reusable catalog")
    index.add_argument("--install", required=True)
    _add_game_argument(index)
    index.add_argument("--reindex", action="store_true")

    extract_all = sub.add_parser(
        "extract-all-assets",
        help="materialize the complete winning retail virtual-file tree",
    )
    extract_all.add_argument("--install", required=True)
    _add_game_argument(extract_all)
    extract_all.add_argument("--reindex", action="store_true")
    extract_all.add_argument(
        "--force",
        action="store_true",
        help="transactionally rebuild an existing effective asset tree",
    )

    asset_census = sub.add_parser(
        "census-assets",
        help="classify every winning retail entry without reading payload bytes",
    )
    asset_census.add_argument("--install", required=True)
    _add_game_argument(asset_census)
    asset_census.add_argument("--reindex", action="store_true")

    visual_closure = sub.add_parser(
        "visual-closure",
        help="resolve exact Object visual leaves from an extracted effective-assets tree",
    )
    visual_closure.add_argument(
        "--assets-root",
        type=Path,
        required=True,
        help="read-only effective-assets root produced by extract-all-assets",
    )
    visual_closure.add_argument(
        "--object",
        dest="objects",
        action="append",
        required=True,
        help="target SAGE Object name (repeat for multiple targets)",
    )

    road_closure = sub.add_parser(
        "road-closure",
        help="resolve exact Road definitions and textures from effective assets",
    )
    road_closure.add_argument(
        "--assets-root",
        type=Path,
        required=True,
        help="read-only effective-assets root produced by extract-all-assets",
    )
    road_closure.add_argument(
        "--road",
        dest="roads",
        action="append",
        required=True,
        help="target SAGE Road id (repeat for multiple targets)",
    )

    census = sub.add_parser(
        "census-maps",
        help="inventory the shipped multiplayer maps selected by retail mapcache",
    )
    census.add_argument("--install", required=True)
    _add_game_argument(census)
    census.add_argument("--reindex", action="store_true")

    faction_census = sub.add_parser(
        "census-faction",
        help="inventory the BFME2 1.06 Men command-reachable dependency roots",
    )
    faction_census.add_argument("--install", required=True)
    _add_game_argument(faction_census)
    faction_census.add_argument("--faction", default="men", choices=("men",))
    faction_census.add_argument("--reindex", action="store_true")

    faction_profile = sub.add_parser(
        "generate-faction-profile",
        help="generate the private BFME2 1.06 Men command/UI/audio/gameplay-definition leaf profile",
    )
    faction_profile.add_argument("--install", required=True)
    _add_game_argument(faction_profile)
    faction_profile.add_argument("--reindex", action="store_true")

    map_profile = sub.add_parser(
        "generate-map-profile",
        help="generate the private BFME2 1.06 five-map source/terrain profile",
    )
    map_profile.add_argument("--install", required=True)
    _add_game_argument(map_profile)
    map_profile.add_argument("--reindex", action="store_true")

    for name, help_text in (
        ("plan", "resolve a profile without extracting retail bytes"),
        ("extract", "extract the exact resolved closure into the private cache"),
        ("build", "transactionally convert and assemble a Godot pack"),
    ):
        command = sub.add_parser(name, help=help_text)
        command.add_argument("--install", required=True)
        _add_game_argument(command)
        command.add_argument("--profile", default="men-fords-v0")
        command.add_argument("--reindex", action="store_true")
        if name in {"extract", "build"}:
            command.add_argument("--force", action="store_true")
        if name == "build":
            command.add_argument("--allow-incomplete", action="store_true")
            command.add_argument("--no-publish", action="store_true", help="build without selecting the pack in Godot")
            command.add_argument(
                "--no-conversion-cache",
                action="store_true",
                help="force cold W3D conversion without reading or populating the conversion cache",
            )
            command.add_argument(
                "--conversion-jobs",
                type=int,
                default=None,
                metavar="N",
                help="parallel W3D conversion workers (default: min(8, cpu_count-2))",
            )
            command.add_argument(
                "--godot-content-root",
                type=Path,
                default=default_godot_content_root(),
                help="private Godot content-packs directory",
            )

    audit = sub.add_parser("audit", help="verify every converted output against provenance hashes")
    audit.add_argument("pack", type=Path)
    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        if args.command == "bootstrap-tools":
            value = bootstrap_tools(_state_root(args), args.ffmpeg)
            _render(value, args.json)
            return 0

        if args.command == "doctor":
            value = doctor_install(args.install, deep=args.deep, game=args.game)
            value["state_root"] = str(_state_root(args))
            value["tools"] = tool_status(_state_root(args))
            value["ready"] = bool(value["ready"] and value["tools"]["ready"])
            _render(value, args.json)
            return 0 if value["ready"] else 2

        if args.command == "audit":
            value = audit_pack(args.pack)
            _render(value, args.json)
            return 0 if value["valid"] else 3

        if args.command == "visual-closure":
            assets_root = ensure_external_to_repo(
                args.assets_root, repo_root_from_module()
            )
            report = build_retail_visual_closure(assets_root, args.objects)
            report_path = (
                _state_root(args)
                / "reports"
                / default_visual_closure_report_name(args.objects)
            )
            write_json_atomic(report_path, report)
            summary = report["summary"]
            value = {
                "ready": bool(summary["ready"]),
                "report": str(report_path),
                "aggregate_sha256": report["aggregateSha256"],
                "target_count": int(summary["targetCount"]),
                "exact_leaf_count": int(summary["exactLeafCount"]),
                "semantic_leaf_count": int(summary["semanticLeafCount"]),
                "unresolved_reference_count": int(
                    summary["unresolvedReferenceCount"]
                ),
                "scanned_w3d_count": int(summary["scannedW3dCount"]),
            }
            _render(value, args.json)
            return 0 if value["ready"] else 6

        if args.command == "road-closure":
            assets_root = ensure_external_to_repo(
                args.assets_root, repo_root_from_module()
            )
            report = build_road_closure(assets_root, args.roads)
            report_path = (
                _state_root(args)
                / "reports"
                / default_road_closure_report_name(args.roads)
            )
            write_json_atomic(report_path, report)
            summary = report["summary"]
            value = {
                "ready": bool(summary["ready"]),
                "report": str(report_path),
                "aggregate_sha256": report["aggregateSha256"],
                "target_count": int(summary["targetCount"]),
                "resolved_road_count": int(summary["resolvedRoadCount"]),
                "resolved_texture_count": int(summary["resolvedTextureCount"]),
                "gap_count": int(summary["gapCount"]),
            }
            _render(value, args.json)
            return 0 if value["ready"] else 6

        catalog = _load_or_build_catalog(args)
        if args.command == "index":
            value = {
                "catalog": str(_catalog_path(args)),
                "archives": len(catalog.archives),
                "entries": len(catalog.entries),
                "stale": catalog.stale_reasons(),
            }
            _render(value, args.json)
            return 0

        if args.command == "extract-all-assets":
            pipeline = ImportPipeline(catalog, _state_root(args), game=args.game)
            value = pipeline.extract_all_assets(force=args.force)
            _render(value, args.json)
            return 0

        if args.command == "census-assets":
            report = census_assets(catalog).neutral()
            report_path = (
                _workspace_root(args) / "reports" / f"{args.game}-asset-census.json"
            )
            write_json_atomic(report_path, report)
            payload = (
                json.dumps(report, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
            )
            family_counts = {
                str(item["family"]): int(item["fileCount"])
                for item in report["families"]
            }
            value = {
                "ready": True,
                "game": args.game,
                "report": str(report_path),
                "report_sha256": hashlib.sha256(payload.encode("utf-8")).hexdigest(),
                "winner_file_count": int(report["totals"]["winnerFileCount"]),
                "winner_bytes": int(report["totals"]["winnerBytes"]),
                "family_counts": family_counts,
                "other_file_count": int(report["totals"]["otherFileCount"]),
                "unclassified_file_count": int(
                    report["totals"]["unclassifiedWinnerFileCount"]
                ),
            }
            _render(value, args.json)
            return 0

        if args.command == "census-maps":
            report = census_multiplayer_maps(catalog)
            report_path = (
                _workspace_root(args)
                / "reports"
                / f"{args.game}-multiplayer-map-census.json"
            )
            write_json_atomic(report_path, report)
            payload = json.dumps(report, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
            value = {
                "ready": True,
                "report": str(report_path),
                "report_sha256": hashlib.sha256(payload.encode("utf-8")).hexdigest(),
                "selected_map_count": int(report["scope"]["selectedMapCount"]),
                "stale_registry_record_count": int(
                    report["scope"]["staleMissingPayloadCount"]
                ),
                "strict_cook_accepted": int(report["summary"]["strictCookAccepted"]),
                "strict_cook_rejected": int(report["summary"]["strictCookRejected"]),
            }
            _render(value, args.json)
            return 0

        if args.command == "census-faction":
            if args.game != "bfme2":
                raise ValueError("census-faction currently supports BFME2 only")
            report = census_men_faction(catalog)
            report_path = _workspace_root(args) / "reports" / "men-faction-leaf-census.json"
            write_json_atomic(report_path, report)
            payload = json.dumps(report, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
            value = {
                "ready": int(report["summary"]["unresolvedCount"]) == 0,
                "report": str(report_path),
                "report_sha256": hashlib.sha256(payload.encode("utf-8")).hexdigest(),
                "object_count": int(report["summary"]["objectCount"]),
                "command_set_count": int(report["summary"]["commandSetCount"]),
                "command_button_count": int(report["summary"]["commandButtonCount"]),
                "upgrade_count": int(report["summary"]["upgradeCount"]),
                "special_power_count": int(report["summary"]["specialPowerCount"]),
                "mapped_image_count": int(report["summary"]["mappedImageResolvedCount"]),
                "localized_text_count": int(report["summary"]["textResolvedCount"]),
                "audio_sample_count": int(report["summary"]["audioSampleCount"]),
                "unresolved_count": int(report["summary"]["unresolvedCount"]),
            }
            _render(value, args.json)
            return 0 if value["ready"] else 5

        if args.command == "generate-faction-profile":
            if args.game != "bfme2":
                raise ValueError("generate-faction-profile currently supports BFME2 only")
            profile = build_men_leaf_profile(catalog)
            profile_path = (
                _workspace_root(args)
                / "profiles"
                / "men-command-leaves.generated.json"
            )
            write_json_atomic(profile_path, profile)
            payload = (
                json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False)
                + "\n"
            )
            value = {
                "ready": True,
                "profile": str(profile_path),
                "profile_sha256": hashlib.sha256(payload.encode("utf-8")).hexdigest(),
                "resource_count": len(profile["resources"]),
            }
            _render(value, args.json)
            return 0

        if args.command == "generate-map-profile":
            if args.game != "bfme2":
                raise ValueError("generate-map-profile currently supports BFME2 only")
            profile = build_five_map_profile(catalog)
            generated_path = (
                _workspace_root(args) / "profiles" / "five-maps.generated.json"
            )
            write_json_atomic(generated_path, profile)
            payload = (
                json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False)
                + "\n"
            )
            value = {
                "ready": True,
                "profile": str(generated_path),
                "profile_sha256": hashlib.sha256(payload.encode("utf-8")).hexdigest(),
                "resource_count": len(profile["resources"]),
                "map_count": len(profile["runtime_data"]["data/maps.json"]["maps"]),
            }
            _render(value, args.json)
            return 0


        resolved = _resolved(args, catalog)
        pipeline = ImportPipeline(
            catalog,
            _state_root(args),
            game=args.game,
            conversion_cache_enabled=not getattr(args, "no_conversion_cache", False),
            conversion_jobs=getattr(args, "conversion_jobs", None),
        )
        report = pipeline.plan_report(resolved)
        report_path = pipeline.reports_root / f"{resolved.profile.id}-plan.json"
        write_json_atomic(report_path, report)
        if args.command == "plan":
            report["report"] = str(report_path)
            _render(report, args.json)
            return 0 if report["ready"] else 4
        if args.command == "extract":
            extracted = pipeline.extract_sources(resolved, force=args.force)
            value = {
                "ready": not resolved.missing_required,
                "extracted": len(extracted),
                "source_cache": str(pipeline.sources_root),
                "missing_required": list(resolved.missing_required),
            }
            _render(value, args.json)
            return 0 if value["ready"] else 4
        if args.command == "build":
            pack = pipeline.build(
                resolved,
                force=args.force,
                allow_incomplete=args.allow_incomplete,
            )
            value = audit_pack(pack)
            value["pack"] = str(pack)
            value["bundle_sha256"] = bundle_digest(pack)
            value["conversion_cache"] = pipeline.conversion_cache_stats
            if not args.no_publish:
                value.update(pipeline.publish_to_godot(pack, args.godot_content_root))
            _render(value, args.json)
            return 0 if value["valid"] else 3
        parser.error(f"unknown command: {args.command}")
    except (FileNotFoundError, ValueError, RuntimeError, OSError) as exc:
        if args.json:
            print(json.dumps({"error": str(exc)}, indent=2), file=sys.stderr)
        else:
            print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    return 0
