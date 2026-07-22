"""Command-line interface for the local-only BFME II importer."""

from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
from typing import Any, Mapping

from .catalog import (
    ArchivePolicy,
    DEFAULT_BFME2_ARCHIVE_POLICY,
    InstallCatalog,
    doctor_install,
)
from .bootstrap import bootstrap_tools, tool_status
from .dependency_check import check_dependencies, format_dependency_report
from .asset_census import census_assets
from .game import RETAIL_GAME_IDS, workspace_root
from .paths import (
    default_godot_content_root,
    default_state_root,
    ensure_external_to_repo,
    repo_root_from_module,
)
from .pipeline import ImportPipeline, audit_pack, bundle_digest
from .playable_unit_import import import_playable_unit
from .faction_census import census_playable_faction
from .faction_import import convert_faction_import, plan_faction_import
from .faction_policy import implicit_object_roots
from .faction_profile import build_men_leaf_profile
from .faction_slice_profile import compose_faction_profile
from .map_profile import build_five_map_profile
from .map_census import census_multiplayer_maps
from .profile import ImportProfile, profile_path, resolve_profile
from .retail_visual_closure import (
    build_retail_visual_closure,
    default_visual_closure_report_name,
)
from .sage_roads import build_road_closure, default_road_closure_report_name
from .progress import configure_progress, complete as progress_complete
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
    source_policy = (
        ArchivePolicy.load(DEFAULT_BFME2_ARCHIVE_POLICY)
        if args.game == "bfme2"
        else None
    )
    if path.is_file() and not args.reindex:
        try:
            catalog = InstallCatalog.load(path)
            if (
                catalog.install_root == install
                and catalog.source_policy == source_policy
                and not catalog.stale_reasons()
            ):
                return catalog
        except (OSError, ValueError, KeyError, TypeError):
            pass
    catalog = InstallCatalog.build(install, source_policy=source_policy)
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

    bootstrap = sub.add_parser(
        "bootstrap-tools", help="provision and hash-pin private conversion tools"
    )
    bootstrap.add_argument(
        "--ffmpeg",
        type=Path,
        default=None,
        help="path to the pinned FFmpeg 8.1.1 executable",
    )

    doctor = sub.add_parser("doctor", help="diagnose an install and conversion tools")
    doctor.add_argument("--install", required=True)
    _add_game_argument(doctor)
    doctor.add_argument(
        "--deep",
        action="store_true",
        help="hash-attest every archive used by the profile",
    )

    index = sub.add_parser(
        "index", help="stream archive directories into a reusable catalog"
    )
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
        help="inventory BFME2 1.06 faction command-reachable dependency roots",
    )
    faction_census.add_argument("--install", required=True)
    _add_game_argument(faction_census)
    faction_census.add_argument(
        "--faction",
        default="men",
        choices=("men", "elves", "dwarves", "isengard", "mordor", "wild"),
    )
    faction_census.add_argument("--reindex", action="store_true")

    import_faction = sub.add_parser(
        "import-faction",
        help="plan a complete BFME2 faction import and expose every converter gap",
    )
    import_faction.add_argument("--install", required=True)
    _add_game_argument(import_faction)
    import_faction.add_argument(
        "--faction",
        required=True,
        choices=("men", "elves", "dwarves", "isengard", "mordor", "wild"),
    )
    import_faction.add_argument("--reindex", action="store_true")
    import_faction.add_argument(
        "--plan-only",
        action="store_true",
        help="account for every object without compiling conversion artifacts",
    )
    import_faction.add_argument(
        "--convert",
        action="store_true",
        help="compile every supported descriptor, recipe, and runtime artifact",
    )
    import_faction.add_argument(
        "--convert-jobs",
        type=int,
        default=None,
        metavar="N",
        help="parallel object convert workers (default: min(8, cpu-1))",
    )
    import_faction.add_argument(
        "--dev",
        action="store_true",
        help=(
            "developer mode: PNG level 6 and other process defaults for this "
            "invocation only (OPENBFME_DEV=1). Object DDC is separate "
            "(state_root / OPENBFME_SHARED_CACHE)."
        ),
    )

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

    import_unit = sub.add_parser(
        "import-unit",
        help="discover, convert, install, and select one BFME2 playable unit",
    )
    import_unit.add_argument("--install", required=True)
    _add_game_argument(import_unit)
    import_unit.add_argument(
        "--object", required=True, help="retail Object or horde id"
    )
    import_unit.add_argument(
        "--faction",
        default="auto",
        help="auto, Men, Elves, Dwarves, Isengard, Mordor, or Wild",
    )
    import_unit.add_argument("--base-profile", type=Path, default=None)
    import_unit.add_argument(
        "--bootstrap-selection",
        action="store_true",
        help="allow creation of the first selected pack only when no selected base exists",
    )
    import_unit.add_argument("--reindex", action="store_true")
    import_unit.add_argument(
        "--plan-only",
        action="store_true",
        help="generate and validate the exact profile delta without converting or publishing",
    )
    import_unit.add_argument("--conversion-jobs", type=int, default=None, metavar="N")
    import_unit.add_argument(
        "--godot-content-root",
        type=Path,
        default=default_godot_content_root(),
    )

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
            command.add_argument(
                "--no-publish",
                action="store_true",
                help="build without selecting the pack in Godot",
            )
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
                help="parallel W3D conversion workers (default: min(16, cpu_count-2))",
            )
            command.add_argument(
                "--godot-content-root",
                type=Path,
                default=default_godot_content_root(),
                help="private Godot content-packs directory",
            )
            command.add_argument(
                "--dev",
                action="store_true",
                help=(
                    "developer cook: PNG level 6, soft tool re-attest, light pack "
                    "audit (size-only). Sets OPENBFME_DEV=1 for the process."
                ),
            )

    publish_faction = sub.add_parser(
        "publish-faction-to-slice",
        help=(
            "compose converted faction coverage into a pack profile, cook the "
            "Godot pack, and select it for the retail vertical slice "
            "(converter → pack → selection auto path)"
        ),
    )
    publish_faction.add_argument("--install", required=True)
    _add_game_argument(publish_faction)
    publish_faction.add_argument(
        "--faction",
        required=True,
        choices=("men", "elves", "dwarves", "isengard", "mordor", "wild"),
    )
    publish_faction.add_argument(
        "--base-profile",
        type=Path,
        default=None,
        help="host pack profile to extend (default: men-fords-v1.generated.json)",
    )
    publish_faction.add_argument(
        "--profile-output",
        type=Path,
        default=None,
        help="where to write the composed profile (default: state profiles/faction-slice-<faction>.generated.json)",
    )
    publish_faction.add_argument(
        "--coverage-root",
        type=Path,
        default=None,
        help="faction-import report root with <faction>-coverage.json (default: state reports/faction-import)",
    )
    publish_faction.add_argument("--reindex", action="store_true")
    publish_faction.add_argument("--force", action="store_true")
    publish_faction.add_argument("--allow-incomplete", action="store_true")
    publish_faction.add_argument(
        "--no-publish",
        action="store_true",
        help="cook the pack without updating Godot selection.json",
    )
    publish_faction.add_argument(
        "--no-conversion-cache",
        action="store_true",
        help="force cold W3D conversion without reading or populating the conversion cache",
    )
    publish_faction.add_argument(
        "--conversion-jobs",
        type=int,
        default=None,
        metavar="N",
        help="parallel W3D conversion workers (default: min(16, cpu_count-2))",
    )
    publish_faction.add_argument(
        "--godot-content-root",
        type=Path,
        default=default_godot_content_root(),
        help="private Godot content-packs directory",
    )
    publish_faction.add_argument(
        "--dev",
        action="store_true",
        help=(
            "developer cook: PNG level 6, soft tool re-attest, light pack "
            "audit (size-only). Sets OPENBFME_DEV=1 for the process."
        ),
    )

    audit = sub.add_parser(
        "audit", help="verify every converted output against provenance hashes"
    )
    audit.add_argument("pack", type=Path)
    return parser


def _apply_dev_mode(enabled: bool) -> dict[str, str | None]:
    """Enable developer cook defaults for this process.

    Returns prior env values so the caller can restore them (CLI scopes --dev
    to a single main() invocation and must not pollute later in-process work).
    """

    keys = (
        "OPENBFME_DEV",
        "OPENBFME_PNG_LEVEL",
        "OPENBFME_DEV_AUDIT",
        "OPENBFME_STRICT_TOOL_ATTEST",
    )
    prior = {key: os.environ.get(key) for key in keys}
    if not enabled:
        return prior
    os.environ["OPENBFME_DEV"] = "1"
    os.environ.setdefault("OPENBFME_PNG_LEVEL", "6")
    os.environ.setdefault("OPENBFME_DEV_AUDIT", "light")
    # Soft tool re-attest (skip full Blender tree re-hash at end).
    os.environ.pop("OPENBFME_STRICT_TOOL_ATTEST", None)
    return prior


def _restore_env(prior: Mapping[str, str | None]) -> None:
    for key, value in prior.items():
        if value is None:
            os.environ.pop(key, None)
        else:
            os.environ[key] = value


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    dev_env_prior: dict[str, str | None] | None = None
    try:
        if getattr(args, "dev", False):
            dev_env_prior = _apply_dev_mode(True)
        if args.command in {
            "import-faction",
            "import-unit",
            "build",
            "plan",
            "extract",
            "publish-faction-to-slice",
        }:
            progress_root = _state_root(args) / "reports" / "progress"
            stage_plan = {
                # Align with GUI 5-stage model (complete is terminal).
                "import-faction": [
                    "catalog",
                    "extract-assets",
                    "census",
                    "faction-plan",
                    "faction-convert",
                    "complete",
                ],
                "build": [
                    "catalog",
                    "extract",
                    "convert-assets",
                    "assemble",
                    "complete",
                ],
                "import-unit": [
                    "catalog",
                    "plan",
                    "convert",
                    "blender-w3d",
                    "publish",
                    "complete",
                ],
                "plan": ["catalog", "plan", "complete"],
                "extract": ["catalog", "extract", "complete"],
                "publish-faction-to-slice": [
                    "catalog",
                    "compose",
                    "extract",
                    "convert-assets",
                    "assemble",
                    "publish",
                    "complete",
                ],
            }.get(args.command, [])
            configure_progress(
                sink=progress_root / f"{args.command}.jsonl",
                stages=stage_plan,
            )

        if args.command == "bootstrap-tools":
            value = bootstrap_tools(_state_root(args), args.ffmpeg)
            _render(value, args.json)
            return 0

        if args.command == "doctor":
            if getattr(args, "deep", False):
                os.environ["OPENBFME_CATALOG_DEEP"] = "1"
            # Unified dependency preflight (install + tools + Godot + state).
            value = check_dependencies(
                args.install,
                _state_root(args),
                mode="men-build",
                deep=bool(getattr(args, "deep", False)),
            )
            # Keep legacy keys for scripts that parse doctor JSON.
            value["install_root"] = value.get("install", {}).get(
                "install_root", str(args.install)
            )
            value["tools"] = tool_status(
                _state_root(args),
                skip_w3d_attestation=not bool(getattr(args, "deep", False)),
            )
            if not args.json:
                print(format_dependency_report(value))
            else:
                _render(value, True)
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
                "unresolved_reference_count": int(summary["unresolvedReferenceCount"]),
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

        if args.command in {"import-faction", "import-unit", "build", "plan", "extract"}:
            from .progress import emit as progress_emit

            progress_emit("catalog", "loading / verifying install catalog")
        catalog = _load_or_build_catalog(args)
        if args.command == "import-faction":
            from .progress import emit as progress_emit

            if args.game != "bfme2":
                raise ValueError("import-faction currently supports BFME2 1.06 only")
            if args.plan_only == args.convert:
                raise ValueError(
                    "pass exactly one of --plan-only or --convert; pack "
                    "publication is a later stage"
                )
            pipeline = ImportPipeline(catalog, _state_root(args), game="bfme2")
            effective_root, manifest_path, _staging, _backup = (
                pipeline._effective_asset_paths()
            )
            if not manifest_path.is_file():
                progress_emit("extract-assets", "extracting effective asset tree")
                pipeline.extract_all_assets(force=False)
            else:
                progress_emit(
                    "extract-assets",
                    "skipped — effective-assets manifest already present",
                    extra={"skipped": True},
                )
            report_root = _state_root(args) / "reports" / "faction-import"
            if args.convert:
                artifact_root = report_root / args.faction / "objects"

                def _write_artifact(
                    object_id: str, kind: str, document: object
                ) -> None:
                    write_json_atomic(
                        artifact_root / object_id.casefold() / f"{kind}.json",
                        document,
                    )

                value = convert_faction_import(
                    catalog,
                    effective_root,
                    args.faction,
                    artifact_writer=_write_artifact,
                    state_root=_state_root(args),
                    convert_jobs=getattr(args, "convert_jobs", None),
                )
                report_path = report_root / f"{args.faction}-coverage.json"
                write_json_atomic(report_path, value)
                summary = value["summary"]
                progress_complete(
                    f"report={report_path} converted "
                    f"{summary['convertedCount']}/{summary['objectCount']}"
                )
                _render(
                    {
                        "ready": summary["conversionComplete"],
                        "report": str(report_path),
                        "aggregate_sha256": value["aggregateSha256"],
                        "object_count": summary["objectCount"],
                        "converted_count": summary["convertedCount"],
                        "excluded_count": summary["excludedCount"],
                        "converter_gap_count": summary["converterGapCount"],
                        "unresolved_leaf_count": summary["unresolvedLeafCount"],
                    },
                    args.json,
                )
                return 0 if bool(summary["conversionComplete"]) else 6
            value = plan_faction_import(catalog, effective_root, args.faction)
            report_path = report_root / f"{args.faction}-plan.json"
            write_json_atomic(report_path, value)
            summary = value["summary"]
            progress_complete(
                f"report={report_path} plan ready "
                f"({summary['descriptorReadyCount']}/{summary['objectCount']} ready)"
            )
            _render(
                {
                    "ready": summary["ready"],
                    "report": str(report_path),
                    "aggregate_sha256": value["aggregateSha256"],
                    "object_count": summary["objectCount"],
                    "descriptor_ready_count": summary["descriptorReadyCount"],
                    "converter_gap_count": summary["converterGapCount"],
                    "unresolved_leaf_count": summary["unresolvedLeafCount"],
                    "unsupported_families": summary["unsupportedFamilies"],
                },
                args.json,
            )
            return 0 if bool(summary["ready"]) else 6
        if args.command == "import-unit":
            if args.game != "bfme2":
                raise ValueError("import-unit currently supports BFME2 1.06 only")
            canonical_profile = args.base_profile or (
                _workspace_root(args) / "profiles" / "men-fords-v1.generated.json"
            )
            value = import_playable_unit(
                catalog,
                _state_root(args),
                args.object,
                faction=args.faction,
                canonical_profile=canonical_profile,
                content_root=args.godot_content_root,
                publish=not args.plan_only,
                bootstrap_selection=args.bootstrap_selection,
                conversion_jobs=args.conversion_jobs,
            )
            _render(value, args.json)
            return 0
        if args.command == "publish-faction-to-slice":
            if args.game != "bfme2":
                raise ValueError(
                    "publish-faction-to-slice currently supports BFME2 1.06 only"
                )
            from .progress import emit as progress_emit

            workspace = _workspace_root(args)
            coverage_root = Path(
                args.coverage_root
                or (workspace / "reports" / "faction-import")
            ).expanduser()
            coverage_path = coverage_root / f"{args.faction}-coverage.json"
            if not coverage_path.is_file():
                raise FileNotFoundError(
                    f"faction coverage missing at {coverage_path}; "
                    f"run: openbfme-import import-faction --faction {args.faction} --convert"
                )
            base_profile_path = Path(
                args.base_profile
                or (workspace / "profiles" / "men-fords-v1.generated.json")
            ).expanduser()
            if not base_profile_path.is_file():
                raise FileNotFoundError(
                    f"base profile missing at {base_profile_path}; "
                    "generate men-fords-v1 (or pass --base-profile)"
                )
            profile_output = Path(
                args.profile_output
                or (
                    workspace
                    / "profiles"
                    / f"faction-slice-{args.faction}.generated.json"
                )
            ).expanduser()
            progress_emit(
                "compose",
                f"composing {args.faction} coverage into pack profile",
            )
            base = json.loads(base_profile_path.read_text(encoding="utf-8"))
            if not isinstance(base, dict):
                raise ValueError(f"base profile root is not an object: {base_profile_path}")
            composed, receipt = compose_faction_profile(
                base, coverage_root, [args.faction]
            )
            # Keep the host pack id stable so the vertical slice host pack
            # assertion (bfme2-men-vslice) continues to pass for Men.
            pack = composed.get("pack")
            if isinstance(pack, dict) and args.faction == "men":
                pack["id"] = "bfme2-men-vslice"
                composed["title"] = "BFME2 Men full faction vertical slice"
            # Freshly composed profiles bind to the catalog they were composed
            # against; inherited m3 markers otherwise fail the build's source
            # catalog identity check with no stamping path.
            if isinstance(pack, dict) and "sourceCatalogIdentitySha256" not in pack:
                pack["sourceCatalogIdentitySha256"] = catalog.identity_sha256()
            write_json_atomic(profile_output, composed)
            receipt_path = profile_output.with_suffix(".receipt.json")
            write_json_atomic(receipt_path, receipt)
            # Validate the composed profile loads under the import schema.
            ImportProfile.load(profile_output)
            progress_emit(
                "compose",
                f"profile={profile_output.name} objects={len(receipt.get('objects', []))}",
            )
            pipeline = ImportPipeline(
                catalog,
                _state_root(args),
                game=args.game,
                conversion_cache_enabled=not args.no_conversion_cache,
                conversion_jobs=args.conversion_jobs,
            )
            resolved = resolve_profile(ImportProfile.load(profile_output), catalog)
            pack_root = pipeline.build(
                resolved,
                force=args.force,
                allow_incomplete=args.allow_incomplete,
            )
            progress_emit("assemble", "auditing pack")
            light_audit = bool(args.dev) or os.environ.get(
                "OPENBFME_DEV", ""
            ).strip().casefold() in {"1", "true", "yes"}
            value = audit_pack(pack_root, light=light_audit)
            value["pack"] = str(pack_root)
            value["profile"] = str(profile_output)
            value["receipt"] = str(receipt_path)
            value["faction"] = args.faction
            value["composed_objects"] = len(receipt.get("objects", []))
            if light_audit:
                value["bundle_sha256"] = "dev-skipped"
                value["dev_mode"] = True
            else:
                value["bundle_sha256"] = bundle_digest(pack_root)
            value["conversion_cache"] = pipeline.conversion_cache_stats
            if not args.no_publish:
                progress_emit("publish", "selecting pack for Godot vertical slice")
                value.update(
                    pipeline.publish_to_godot(
                        pack_root,
                        args.godot_content_root,
                        allow_incomplete=bool(args.allow_incomplete),
                    )
                )
            progress_complete(
                f"faction={args.faction} pack={pack_root} slice path ready"
            )
            _render(value, args.json)
            return 0 if value.get("valid", False) else 3
        if args.command == "index":
            value = {
                "catalog": str(_catalog_path(args)),
                "archives": len(catalog.archives),
                "entries": len(catalog.entries),
                # _load_or_build_catalog already validated a reused catalog or
                # built this catalog directly from the current install.
                "stale": [],
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
            payload = (
                json.dumps(report, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
            )
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
            faction_specs = {
                "men": ("FactionMen", "Men"),
                "elves": ("FactionElves", "Elves"),
                "dwarves": ("FactionDwarves", "Dwarves"),
                "isengard": ("FactionIsengard", "Isengard"),
                "mordor": ("FactionMordor", "Mordor"),
                "wild": ("FactionWild", "Wild"),
            }
            template, side = faction_specs[args.faction]
            report = census_playable_faction(
                catalog,
                player_template=template,
                expected_side=side,
                implicit_object_roots=implicit_object_roots(template),
            )
            report_path = (
                _workspace_root(args)
                / "reports"
                / f"{args.faction}-faction-leaf-census.json"
            )
            write_json_atomic(report_path, report)
            payload = (
                json.dumps(report, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
            )
            value = {
                "ready": int(report["summary"]["unresolvedCount"]) == 0,
                "report": str(report_path),
                "report_sha256": hashlib.sha256(payload.encode("utf-8")).hexdigest(),
                "object_count": int(report["summary"]["objectCount"]),
                "command_set_count": int(report["summary"]["commandSetCount"]),
                "command_button_count": int(report["summary"]["commandButtonCount"]),
                "upgrade_count": int(report["summary"]["upgradeCount"]),
                "special_power_count": int(report["summary"]["specialPowerCount"]),
                "mapped_image_count": int(
                    report["summary"]["mappedImageResolvedCount"]
                ),
                "localized_text_count": int(report["summary"]["textResolvedCount"]),
                "audio_sample_count": int(report["summary"]["audioSampleCount"]),
                "unresolved_count": int(report["summary"]["unresolvedCount"]),
            }
            _render(value, args.json)
            return 0 if value["ready"] else 5

        if args.command == "generate-faction-profile":
            if args.game != "bfme2":
                raise ValueError(
                    "generate-faction-profile currently supports BFME2 only"
                )
            profile = build_men_leaf_profile(catalog)
            profile_path = (
                _workspace_root(args) / "profiles" / "men-command-leaves.generated.json"
            )
            write_json_atomic(profile_path, profile)
            payload = (
                json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
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
                json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
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
            from .progress import emit as progress_emit

            pack = pipeline.build(
                resolved,
                force=args.force,
                allow_incomplete=args.allow_incomplete,
            )
            progress_emit("assemble", "auditing pack")
            light_audit = bool(getattr(args, "dev", False)) or os.environ.get(
                "OPENBFME_DEV", ""
            ).strip().casefold() in {"1", "true", "yes"}
            value = audit_pack(pack, light=light_audit)
            value["pack"] = str(pack)
            if light_audit:
                # Skip second full-pack SHA-256 walk in dev mode.
                value["bundle_sha256"] = "dev-skipped"
                value["dev_mode"] = True
            else:
                value["bundle_sha256"] = bundle_digest(pack)
            value["conversion_cache"] = pipeline.conversion_cache_stats
            if not args.no_publish:
                progress_emit("assemble", "publishing pack to Godot content root")
                value.update(
                    pipeline.publish_to_godot(
                        pack,
                        args.godot_content_root,
                        allow_incomplete=bool(args.allow_incomplete),
                    )
                )
            progress_complete(f"report={pack} pack build finished")
            _render(value, args.json)
            return 0 if value["valid"] else 3
        parser.error(f"unknown command: {args.command}")
    except (FileNotFoundError, ValueError, RuntimeError, OSError) as exc:
        if args.json:
            print(json.dumps({"error": str(exc)}, indent=2), file=sys.stderr)
        else:
            print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    finally:
        if dev_env_prior is not None:
            _restore_env(dev_env_prior)
    return 0
