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
    CatalogProvenanceError,
    DEFAULT_BFME2_ARCHIVE_POLICY,
    InstallCatalog,
    catalog_provenance_reason,
    doctor_install,
)
from .bootstrap import bootstrap_tools, tool_status
from .dependency_check import check_dependencies, format_dependency_report
from .asset_census import census_assets
from .effective_assets_identity import (
    VERIFY_CHOICES,
    EffectiveAssetsIdentityError,
    verify_effective_assets,
)
from .game import RETAIL_GAME_IDS, workspace_root
from .paths import (
    default_godot_content_root,
    default_state_root,
    ensure_external_to_repo,
    repo_root_from_module,
)
from .pipeline import (
    ImportPipeline,
    audit_pack,
    apply_selection_transaction,
    bundle_digest,
    update_selection_entry,
)
from .playable_unit_import import import_playable_unit
from . import publish_gate
from .faction_census import (
    census_playable_faction,
    discover_playable_factions,
    resolve_playable_faction,
)
from .faction_import import convert_faction_import, plan_faction_import
from .faction_policy import implicit_object_roots
from .sage_video import convert_videos
from .tools import discover_executable
from .faction_profile import build_men_leaf_profile
from .faction_slice_profile import compose_faction_profile
from .map_profile import (
    MAP_SETS,
    build_category_map_profile,
    build_five_map_profile,
    make_effective_assets_binder,
)
from .map_census import census_multiplayer_maps
from .music_import import (
    AUDIO_SETTINGS_INI_PATH,
    MISC_AUDIO_INI_PATH,
    MUSIC_INI_PATH,
    MUSIC_SCRIPT_LIBRARY_PATH,
    build_music_document,
    compose_music_profile,
)
from .sage_scripts import map_scripts_document
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


def _faction_slice_pack_id(game: str, factions: list[str]) -> str:
    """Edition-qualified deterministic id for a composed faction pack."""

    if not factions:
        raise ValueError("faction slice pack id requires at least one faction")
    return f"{game}-{'-'.join(factions)}-vslice"


def _conversion_failure_report(pack_root: Path) -> dict[str, Any]:
    """Summarise per-resource conversion failures recorded in the pack.

    build() records every converter failure in provenance ``incomplete`` but
    nothing ever showed it to the operator: an --allow-incomplete run could
    lose hundreds of conversions and still print a clean, valid, exit-0
    result. That is the exact shape of the historical bug where every audio
    job failed a pinned-tool attest and the run produced a silently
    audio-less pack. The count and a per-converter breakdown now ride the
    command result, and a non-empty list is echoed to stderr regardless of
    --json so it cannot be scrolled past.
    """

    manifest_path = Path(pack_root) / "provenance" / "manifest.json"
    try:
        with manifest_path.open("r", encoding="utf-8") as stream:
            manifest = json.load(stream)
    except (OSError, json.JSONDecodeError):
        return {"conversion_failures": None}
    incomplete = manifest.get("incomplete")
    if not isinstance(incomplete, list) or not incomplete:
        return {"conversion_failures": 0}

    converter_by_resource: dict[str, str] = {}
    for entry in manifest.get("entries", []):
        if isinstance(entry, dict):
            converter_by_resource[str(entry.get("resource_id"))] = str(
                entry.get("converter", "?")
            )
    by_converter: dict[str, int] = {}
    for item in incomplete:
        if not isinstance(item, dict):
            continue
        resource = str(item.get("resource", "?"))
        by_converter[converter_by_resource.get(resource, "?")] = (
            by_converter.get(converter_by_resource.get(resource, "?"), 0) + 1
        )

    print(
        f"WARNING: {len(incomplete)} resource(s) did not convert; the pack is "
        "incomplete. Breakdown by converter: "
        + ", ".join(f"{name}={count}" for name, count in sorted(by_converter.items())),
        file=sys.stderr,
    )
    for item in incomplete[:10]:
        if isinstance(item, dict):
            print(
                f"  - {item.get('resource', '?')}: {item.get('reason', '?')}",
                file=sys.stderr,
            )
    if len(incomplete) > 10:
        print(
            f"  ... {len(incomplete) - 10} more; full list in "
            f"{manifest_path}",
            file=sys.stderr,
        )
    return {
        "conversion_failures": len(incomplete),
        "conversion_failures_by_converter": by_converter,
    }


# The gate rules live in `publish_gate` so `build`, `import-unit` and
# `publish-faction-to-slice` can all call the same code, and so
# `playable_unit_import` can reach them without importing `cli`. These aliases
# keep the historical private names working for existing callers and tests.
PUBLISH_GATE_EXIT = publish_gate.PUBLISH_GATE_EXIT
_coverage_publish_blockers = publish_gate.coverage_publish_blockers
_coverage_binding_blockers = publish_gate.coverage_binding_blockers
_pack_playable_unit_count = publish_gate.pack_playable_unit_count
_pack_playable_unit_ids = publish_gate.pack_playable_unit_ids
_incumbent_playable_unit_count = publish_gate.incumbent_playable_unit_count
_incumbent_playable_unit_ids = publish_gate.incumbent_playable_unit_ids
_playable_unit_regression_blocker = publish_gate.playable_unit_regression_blocker


def _refuse(headline: str, blockers: list[str], override_flag: str) -> int:
    """Print a gate refusal in the one shape every command uses, and return 7."""

    print(f"REFUSING TO PUBLISH: {headline}", file=sys.stderr)
    for reason in blockers:
        print(f"  - {reason}", file=sys.stderr)
    print(f"  Pass {override_flag} to publish anyway.", file=sys.stderr)
    return PUBLISH_GATE_EXIT


def _warn_override(override_flag: str, blockers: list[str]) -> None:
    print(f"WARNING: publishing anyway ({override_flag}):", file=sys.stderr)
    for reason in blockers:
        print(f"  - {reason}", file=sys.stderr)


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


def _catalog_install_root(args: argparse.Namespace) -> Path:
    """Return the exact install tree that owns the edition catalog.

    RotWK is an expansion, so an expansion-only catalog legitimately omits
    base-game art.  Once the canonical layered install exists, a command given
    its matching raw RotWK root must keep the layered catalog rather than
    silently replacing it with an expansion-only one.
    """

    requested = Path(args.install).expanduser().resolve()
    if args.game != "rotwk":
        return requested
    layered = _state_root(args) / "editions" / "rotwk" / "layered-install"
    layer_rotwk = layered / "layer-0-rotwk"
    layer_bfme2 = layered / "layer-1-bfme2"
    if not (layer_rotwk / "game.dat").is_file() or not (
        layer_bfme2 / "game.dat"
    ).is_file():
        return requested
    if requested == layered.resolve() or requested == layer_rotwk.resolve():
        return layered.resolve()
    return requested


def _workspace_root(args: argparse.Namespace) -> Path:
    return workspace_root(_state_root(args), args.game)


DEFAULT_GAME = "rotwk"


def _add_game_argument(command: argparse.ArgumentParser) -> None:
    # RotWK is the content baseline (owner decision, 2026-07-27): it is a
    # superset carrying 305 objects BFME2 lacks, only 72 of which are Angmar.
    # BFME2 stays fully reachable via an explicit --game bfme2.
    command.add_argument(
        "--game",
        choices=RETAIL_GAME_IDS,
        default=DEFAULT_GAME,
        help=(
            "retail game identity (default: rotwk, the content baseline); "
            "pass --game bfme2 for the base-game comparison baseline"
        ),
    )


def _load_or_build_catalog(args: argparse.Namespace) -> InstallCatalog:
    path = _catalog_path(args)
    install = _catalog_install_root(args)
    source_policy = (
        ArchivePolicy.load(DEFAULT_BFME2_ARCHIVE_POLICY)
        if args.game == "bfme2"
        else None
    )
    def _guard(catalog: InstallCatalog, origin: str) -> InstallCatalog:
        # Fail closed: never let a catalog that cannot evidence the requested
        # edition supply content under that edition's name.
        reason = catalog_provenance_reason(
            (archive.relative_path for archive in catalog.archives), args.game
        )
        if reason is not None:
            raise CatalogProvenanceError(
                {
                    "error": "catalog-game-mismatch",
                    "game": args.game,
                    "catalog": str(path),
                    "install_root": str(catalog.install_root),
                    "origin": origin,
                    "reason": reason,
                    "message": (
                        f"refusing to use a catalog that is not {args.game}: {reason}. "
                        f"Point --install at a {args.game} install "
                        f"(RotWK uses the layered install root) and re-run with --reindex."
                    ),
                }
            )
        return catalog

    if path.is_file() and not args.reindex:
        try:
            catalog = InstallCatalog.load(path)
        except (OSError, ValueError, KeyError, TypeError) as exc:
            # A malformed catalog is a hard stop, not a cue to quietly rebuild.
            # Silently rebuilding here is how a wrong --install turns into
            # confidently wrong content with no diagnostic.
            raise CatalogProvenanceError(
                {
                    "error": "catalog-unreadable",
                    "game": args.game,
                    "catalog": str(path),
                    "reason": f"{type(exc).__name__}: {exc}",
                    "message": (
                        f"{args.game} catalog is unreadable or malformed: {path}. "
                        "Re-run with --reindex to rebuild it deliberately."
                    ),
                }
            ) from exc
        _guard(catalog, "cached")
        if (
            catalog.install_root == install
            and catalog.source_policy == source_policy
            and not catalog.stale_reasons()
        ):
            return catalog
    catalog = _guard(
        InstallCatalog.build(install, source_policy=source_policy), "rebuilt"
    )
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

    verify_assets = sub.add_parser(
        "verify-effective-assets",
        help="name the install an effective-assets tree holds, and refuse a wrong or stale one",
    )
    verify_assets.add_argument(
        "--assets-root",
        type=Path,
        required=True,
        help="effective-assets root produced by extract-all-assets",
    )
    verify_assets.add_argument(
        "--expect-game",
        choices=RETAIL_GAME_IDS,
        help=(
            "refuse unless the tree serves this edition's bytes; omit to only "
            "report which edition it holds"
        ),
    )
    verify_assets.add_argument(
        "--catalog",
        type=Path,
        help=(
            "refuse unless the tree was extracted from this exact catalog "
            "(catches a cache left behind by an older install)"
        ),
    )
    verify_assets.add_argument(
        "--verify",
        choices=VERIFY_CHOICES,
        default="manifest",
        help=(
            "manifest: trust the sealed manifest; sizes: stat every file; "
            "hashes: re-digest every byte (default: manifest)"
        ),
    )
    verify_assets.add_argument(
        "--consumer",
        help="record who is reading this tree, for the report",
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

    cah_system = sub.add_parser(
        "compile-cah-system",
        help=(
            "compile the Create-a-Hero class system (classes, subclasses, "
            "attribute ladders and point budgets) from effective assets"
        ),
    )
    cah_system.add_argument(
        "--assets-root",
        type=Path,
        required=True,
        help="read-only effective-assets root produced by extract-all-assets",
    )
    _add_game_argument(cah_system)
    cah_system.add_argument(
        "--runtime-out",
        type=Path,
        default=None,
        help=(
            "also write the Godot-facing openbfme.cah-system-runtime document "
            "here (defaults to alongside the descriptor)"
        ),
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
        help="inventory one discovered faction's command-reachable dependency roots",
    )
    faction_census.add_argument("--install", required=True)
    _add_game_argument(faction_census)
    faction_census.add_argument(
        "--faction",
        default="men",
    )
    faction_census.add_argument("--reindex", action="store_true")

    factions_census = sub.add_parser(
        "census-factions",
        help="list playable factions discovered from effective PlayerTemplate data",
    )
    factions_census.add_argument("--install", required=True)
    _add_game_argument(factions_census)
    factions_census.add_argument("--reindex", action="store_true")

    strategic_census_cmd = sub.add_parser(
        "census-strategic",
        help="census the War-of-the-Ring and Create-a-Hero INI surface, fail-closed",
    )
    strategic_census_cmd.add_argument("--install", required=True)
    _add_game_argument(strategic_census_cmd)
    strategic_census_cmd.add_argument("--reindex", action="store_true")

    living_world_cmd = sub.add_parser(
        "living-world",
        help=(
            "produce the openbfme.living-world strategic document (region "
            "graph, armies, scenarios) the WOTR layer consumes, fail-closed"
        ),
    )
    living_world_cmd.add_argument("--install", required=True)
    _add_game_argument(living_world_cmd)
    living_world_cmd.add_argument("--reindex", action="store_true")
    living_world_cmd.add_argument(
        "--output",
        help=(
            "explicit output path for the document; must stay outside the "
            "repository (default: workspace reports/<game>-living-world.json)"
        ),
    )

    convert_videos_cmd = sub.add_parser(
        "convert-videos",
        help="convert loose data/movies VP6/Bink cinematics to Ogg Theora via pinned ffmpeg",
    )
    convert_videos_cmd.add_argument("--install", required=True)
    _add_game_argument(convert_videos_cmd)
    convert_videos_cmd.add_argument(
        "--only",
        default=None,
        help="convert only movies whose path contains this substring",
    )
    convert_videos_cmd.add_argument(
        "--limit",
        type=int,
        default=None,
        help="convert at most N movies (discovery order)",
    )

    import_faction = sub.add_parser(
        "import-faction",
        help="plan a complete BFME2 faction import and expose every converter gap",
    )
    import_faction.add_argument("--install", required=True)
    _add_game_argument(import_faction)
    import_faction.add_argument(
        "--faction",
        required=True,
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
        help="generate a private map source/terrain profile from retail bytes",
    )
    map_profile.add_argument("--install", required=True)
    _add_game_argument(map_profile)
    map_profile.add_argument("--reindex", action="store_true")
    map_profile.add_argument(
        "--map-set",
        choices=("five-map", *sorted(MAP_SETS)),
        default="five-map",
        help=(
            "five-map: the legacy private BFME2 1.06 development set; every "
            "other value selects a retail map category discovered from "
            "maps/mapcache.ini (skirmish, wotr-battle, campaign, cinematic, "
            "tutorial, playable, single-player, all)"
        ),
    )
    map_profile.add_argument(
        "--effective-assets",
        type=Path,
        default=None,
        help=(
            "extracted effective-assets root; enables automatic per-map prop "
            "binding through the visual-closure planners. Without it no "
            "objectBindings are declared."
        ),
    )
    map_profile.add_argument(
        "--strict",
        action="store_true",
        help=(
            "skirmish only: abort instead of recording a rejection when a "
            "shipped map cannot be parsed"
        ),
    )
    map_profile.add_argument(
        "--map-limit",
        type=int,
        default=None,
        metavar="N",
        help="deterministically include only the first N discovered maps",
    )

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
        "--allow-fewer-playable-units",
        action="store_true",
        help=(
            "OVERRIDE: publish even though the rebuilt pack drops "
            "playable-unit ids the already-published bundle ships. Without "
            "this the command refuses (exit 7)"
        ),
    )
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
                "--allow-fewer-playable-units",
                action="store_true",
                help=(
                    "OVERRIDE: publish even though this build drops "
                    "playable-unit ids the richest already-published bundle of "
                    "the same pack id ships. Without this the command refuses "
                    "(exit 7)"
                ),
            )
            command.add_argument(
                "--no-publish",
                action="store_true",
                help="build without selecting the pack in Godot",
            )
            command.add_argument(
                "--no-select",
                action="store_true",
                help="publish the pack without rewriting selection.json",
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
            "Godot pack, and publish the bundle to the Godot content root; "
            "selection.json is left untouched unless --select is passed"
        ),
    )
    publish_faction.add_argument("--install", required=True)
    _add_game_argument(publish_faction)
    publish_faction.add_argument(
        "--faction",
        required=True,
        action="append",
        # BFME2's six factions plus the RotWK 2.01 data-driven expansion
        # factions (faction_slice_profile validates the game/faction pair).
        choices=("men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar"),
        help=(
            "faction to compose; repeat to cook one multi-faction pack "
            "(compose_faction_profile has always accepted a sequence, and a "
            "cross-faction skirmish needs every side in the active pack)"
        ),
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
        "--allow-incomplete-coverage",
        action="store_true",
        help=(
            "OVERRIDE: cook and publish even though a faction's converted "
            "coverage report records converter gaps. Without this the command "
            "refuses (exit 7) rather than shipping a known-short slice — the "
            "failure mode that produced a 20-of-24-unit Men bundle twice"
        ),
    )
    publish_faction.add_argument(
        "--allow-stale-coverage",
        action="store_true",
        help=(
            "OVERRIDE: publish even though the coverage report does not "
            "describe the content being cooked — its catalog identity or its "
            "compiler identity token disagrees with the tree on disk. Without "
            "this the command refuses (exit 7) rather than shipping "
            "descriptors from a compiler the tree has moved past, the failure "
            "that made 20 units unbuildable across six faction packs"
        ),
    )
    publish_faction.add_argument(
        "--allow-fewer-playable-units",
        action="store_true",
        help=(
            "OVERRIDE: publish even though this cook drops playable-unit ids "
            "the richest already-published bundle of the same pack id ships. "
            "Checked by name, so a swap at constant count refuses too. "
            "Without this the command refuses (exit 7)"
        ),
    )
    publish_faction.add_argument(
        "--no-publish",
        action="store_true",
        help="cook the pack without copying it into the Godot content root",
    )
    publish_faction.add_argument(
        "--select",
        action="store_true",
        help=(
            "also rewrite the live selection.json to activate the published "
            "pack (legacy auto-select behavior); without this flag the "
            "selection is never touched — use update-selection-entry to "
            "repoint an existing entry"
        ),
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

    publish_music = sub.add_parser(
        "publish-music-pack",
        help=(
            "extract the authored per-faction music binding (music.ini + the "
            "Music_MusicScripts_Single library) and cook it into a standalone "
            "music pack; selection.json is left untouched"
        ),
    )
    publish_music.add_argument("--install", required=True)
    _add_game_argument(publish_music)
    publish_music.add_argument("--reindex", action="store_true")
    publish_music.add_argument("--force", action="store_true")
    publish_music.add_argument(
        "--pack-id",
        default=None,
        help="pack id to publish (default: <game>-music-vslice)",
    )
    publish_music.add_argument(
        "--profile-output",
        type=Path,
        default=None,
        help="where to write the generated profile (default: workspace/profiles)",
    )
    publish_music.add_argument(
        "--no-publish",
        action="store_true",
        help="cook the pack without publishing it to the Godot content root",
    )
    publish_music.add_argument(
        "--godot-content-root",
        type=Path,
        default=default_godot_content_root(),
        help="private Godot content-packs directory",
    )
    publish_music.add_argument(
        "--dev",
        action="store_true",
        help="developer cook: light pack audit (size-only)",
    )

    update_selection = sub.add_parser(
        "update-selection-entry",
        help=(
            "atomically repoint the selection.json entry (active or "
            "supplemental) for a pack id to a newly published bundle hash, "
            "preserving activePack and entry order"
        ),
    )
    update_selection.add_argument(
        "--pack-id", required=True, help="published pack id to retarget"
    )
    update_selection.add_argument(
        "--bundle-sha256",
        required=True,
        help="bundle digest of the already-published target bundle",
    )
    update_selection.add_argument(
        "--godot-content-root",
        type=Path,
        default=default_godot_content_root(),
        help="private Godot content-packs directory",
    )

    selection_transaction = sub.add_parser(
        "apply-selection-transaction",
        help=(
            "replace the COMPLETE selection in the workspace and (optionally) "
            "the durable mirror in one staged, verified, all-or-nothing swap "
            "per target; any failure restores both to their exact prior bytes"
        ),
    )
    selection_transaction.add_argument(
        "--active-pack",
        required=True,
        metavar="PACK_ID/SHA256",
        help="content-addressed bundle that becomes activePack",
    )
    selection_transaction.add_argument(
        "--supplemental-pack",
        action="append",
        default=[],
        dest="supplemental_packs",
        metavar="PACK_ID/SHA256",
        help=(
            "content-addressed supplement, repeatable; the flag order IS the "
            "supplementalPacks order written to the document"
        ),
    )
    selection_transaction.add_argument(
        "--godot-content-root",
        type=Path,
        default=default_godot_content_root(),
        help="private Godot content-packs directory (the workspace selection)",
    )
    selection_transaction.add_argument(
        "--durable-root",
        type=Path,
        default=None,
        help=(
            "durable user pack cache whose selection.json must end up "
            "byte-identical to the workspace one (omit to leave it untouched)"
        ),
    )

    audit = sub.add_parser(
        "audit", help="verify every converted output against provenance hashes"
    )
    audit.add_argument("pack", type=Path)

    describe = sub.add_parser(
        "describe-pack",
        help=(
            "explain a pack's provenance in plain language: where it came "
            "from, what produced it, what is in it, and what is not known"
        ),
    )
    describe.add_argument("pack", type=Path)
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
    # `default_state_root()` is consulted directly by helpers that never see
    # `args` (tool discovery is the load-bearing one: it resolves the pinned
    # ffmpeg/blender under <state-root>/tools). Without this, an explicit
    # --state-root left those helpers pointing at the checkout's own .private,
    # and a missing pinned ffmpeg silently fell through to whatever is on PATH
    # — which then fails the pinned-hash attest on every audio conversion.
    # Scoped to this invocation. A bare `os.environ[...] = ...` here outlives
    # the call for every in-process caller (the test suite is the one that
    # notices): one CLI run against a temporary --state-root silently
    # repointed `default_state_root()` for everything that ran afterwards.
    # Restored in the `finally` below alongside the dev-mode overrides.
    state_root_env_prior: dict[str, str | None] = {
        "OPENBFME_IMPORT_ROOT": os.environ.get("OPENBFME_IMPORT_ROOT")
    }
    os.environ["OPENBFME_IMPORT_ROOT"] = str(
        Path(args.state_root).expanduser().resolve()
    )
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
                mode="men-build" if args.game == "bfme2" else "faction-plan",
                deep=bool(getattr(args, "deep", False)),
                game=args.game,
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

        if args.command == "describe-pack":
            from .pack_report import describe_pack, render_pack_report

            report = describe_pack(args.pack)
            if args.json:
                print(json.dumps(report, indent=2, sort_keys=True))
            else:
                print(render_pack_report(report))
            return 0 if report["health"]["valid"] else 3

        if args.command == "verify-effective-assets":
            # Read-only. Answers the one question the shared "effective-assets"
            # directory name cannot: which install are these bytes from?
            value = verify_effective_assets(
                args.assets_root,
                game=args.expect_game,
                catalog=args.catalog,
                verify=args.verify,
                consumer=args.consumer,
            )
            _render(value, args.json)
            return 0

        if args.command == "apply-selection-transaction":
            # RULE P2: no --select anywhere on this path. The whole selection is
            # composed, staged, swapped once per target and verified, or nothing
            # changes at all.
            value = apply_selection_transaction(
                args.godot_content_root,
                args.active_pack,
                args.supplemental_packs,
                durable_root=args.durable_root,
            )
            _render(value, args.json)
            return 0

        if args.command == "update-selection-entry":
            value = update_selection_entry(
                args.godot_content_root, args.pack_id, args.bundle_sha256
            )
            _render(value, args.json)
            return 0

        if args.command == "compile-cah-system":
            from .cah_system_compiler import (
                REQUIRED_DOCUMENTS,
                build_cah_system_runtime,
                compile_cah_system_descriptor,
            )

            assets_root = ensure_external_to_repo(
                args.assets_root, repo_root_from_module()
            )
            # Read only what the compiler declares it needs, plus the .inc files
            # the system file includes. Sweeping data/ini wholesale would pull in
            # tens of megabytes for a table that reaches seven documents.
            documents: dict[str, bytes] = {}
            ini_root = assets_root / "data" / "ini"
            for path in sorted(ini_root.glob("createahero*.in[ci]")):
                documents[path.relative_to(assets_root).as_posix()] = path.read_bytes()
            for relative in REQUIRED_DOCUMENTS:
                candidate = assets_root / relative
                if candidate.is_file():
                    documents[relative] = candidate.read_bytes()
            descriptor = compile_cah_system_descriptor(documents)
            runtime = build_cah_system_runtime(descriptor)

            reports = _state_root(args) / "reports"
            descriptor_path = reports / f"{args.game}-cah-system-descriptor.json"
            runtime_path = (
                args.runtime_out
                if args.runtime_out is not None
                else reports / f"{args.game}-cah-system-runtime.json"
            )
            write_json_atomic(descriptor_path, descriptor)
            write_json_atomic(runtime_path, runtime)
            _render(
                {
                    "ready": True,
                    "game": args.game,
                    "descriptor": str(descriptor_path),
                    "runtime": str(runtime_path),
                    "descriptor_sha256": descriptor["descriptorSha256"],
                    "class_count": len(descriptor["classes"]),
                    "sub_class_count": sum(
                        len(row["subClasses"]) for row in descriptor["classes"]
                    ),
                    "attribute_group_count": len(descriptor["attributeGroups"]),
                    "build_cost": descriptor["system"]["buildCost"],
                    "build_cost_expression": descriptor["system"]["buildCostExpression"],
                },
                args.json,
            )
            return 0

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

        if args.command == "convert-videos":
            # Loose-file lane: reads data/movies directly, no catalog required.
            ffmpeg = discover_executable("ffmpeg", "OPENBFME_FFMPEG")
            if ffmpeg is None:
                raise SystemExit(
                    "ffmpeg is required; run bootstrap-tools or set OPENBFME_FFMPEG"
                )
            output_root = _workspace_root(args) / "videos"
            report = convert_videos(
                Path(args.install).expanduser().resolve(),
                output_root,
                Path(ffmpeg),
                only=args.only,
                limit=args.limit,
            )
            _render(
                {
                    "ready": True,
                    "game": args.game,
                    "output": str(output_root),
                    "discovered": report["discovered"],
                    "converted": report["converted"],
                    "failed": report["failed"],
                },
                args.json,
            )
            return 0

        if args.command in {"import-faction", "import-unit", "build", "plan", "extract"}:
            from .progress import emit as progress_emit

            progress_emit("catalog", "loading / verifying install catalog")
        catalog = _load_or_build_catalog(args)
        if args.command == "import-faction":
            from .progress import emit as progress_emit

            if args.plan_only == args.convert:
                raise ValueError(
                    "pass exactly one of --plan-only or --convert; pack "
                    "publication is a later stage"
                )
            if args.convert and args.game not in {"bfme2", "rotwk"}:
                raise ValueError(
                    "import-faction --convert supports BFME2 1.06 and RotWK 2.01"
                )
            faction = resolve_playable_faction(catalog, args.faction)
            faction_key = faction.short_name
            pipeline = ImportPipeline(catalog, _state_root(args), game=args.game)
            effective_root, manifest_path, _staging, _backup = (
                pipeline._effective_asset_paths()
            )
            if not manifest_path.is_file():
                progress_emit("extract-assets", "extracting effective asset tree")
                pipeline.extract_all_assets(force=False)
                # The pipeline was constructed BEFORE this tree existed, so its
                # RotWK oracle bind was deferred (see
                # ImportPipeline.source_override_root). Resolve it now, while
                # the ordering is obvious, rather than leaving the first
                # downstream `pipeline.catalog` read to decide whether this run
                # cooks from the sealed oracle or from the raw catalog.
                _ = pipeline.source_override_root
            else:
                progress_emit(
                    "extract-assets",
                    "skipped — effective-assets manifest already present",
                    extra={"skipped": True},
                )
            report_root = _state_root(args) / "reports" / "faction-import"
            if args.game != "bfme2":
                report_root = _workspace_root(args) / "reports" / "faction-import"
            if args.convert:
                artifact_root = report_root / faction_key / "objects"

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
                    faction_key,
                    artifact_writer=_write_artifact,
                    state_root=_state_root(args),
                    convert_jobs=getattr(args, "convert_jobs", None),
                    game=args.game,
                )
                report_path = report_root / f"{faction_key}-coverage.json"
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
            value = plan_faction_import(
                catalog, effective_root, faction_key, game=args.game
            )
            report_path = report_root / f"{faction_key}-plan.json"
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
            # import-unit rebuilds and republishes the whole host pack when the
            # profile delta is an update, so it can regress the roster exactly
            # like the faction publish lane can. The gate lives inside
            # import_playable_unit (the publication happens there, below this
            # frame) and reaches this handler as PublishGateError.
            try:
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
                    allow_fewer_playable_units=args.allow_fewer_playable_units,
                )
            except publish_gate.PublishGateError as gate_error:
                return _refuse(
                    "this unit import drops playable units the slice already "
                    "ships.",
                    gate_error.blockers,
                    gate_error.override_flag,
                )
            _render(value, args.json)
            return 0
        if args.command == "publish-faction-to-slice":
            if args.game not in {"bfme2", "rotwk"}:
                raise ValueError(
                    "publish-faction-to-slice supports BFME2 1.06 and RotWK 2.01"
                )
            from .progress import emit as progress_emit

            workspace = _workspace_root(args)
            coverage_root = Path(
                args.coverage_root
                or (workspace / "reports" / "faction-import")
            ).expanduser()
            # `--faction` is repeatable; compose order is the order given so a
            # multi-faction profile id stays reproducible for the same request.
            factions: list[str] = list(args.faction)
            if len(set(factions)) != len(factions):
                raise ValueError(f"--faction repeated with a duplicate: {factions}")
            for faction in factions:
                coverage_path = coverage_root / f"{faction}-coverage.json"
                if not coverage_path.is_file():
                    raise FileNotFoundError(
                        f"faction coverage missing at {coverage_path}; "
                        f"run: openbfme-import import-faction --faction {faction} --convert"
                    )
            # Fail closed BEFORE the expensive cook: a coverage report that
            # records converter gaps must not become a published bundle.
            coverage_blockers = _coverage_publish_blockers(coverage_root, factions)
            if coverage_blockers:
                if not args.allow_incomplete_coverage:
                    return _refuse(
                        "converted coverage is incomplete.",
                        coverage_blockers,
                        "--allow-incomplete-coverage",
                    )
                _warn_override("--allow-incomplete-coverage", coverage_blockers)
            # Second pre-cook gate: a *clean* report still has to be a report
            # about the tree being cooked right now. Bound against the same
            # catalog object `import-faction` records, plus the compiler
            # identity token, which is the binding that catches a report whose
            # descriptors came from a compiler the tree has since moved past.
            #
            # It must be bound to the catalog the COOK resolves against, NOT
            # the outer catalog loaded from the install. For RotWK those are
            # different objects: the cook resolves an EffectiveAssetsCatalog
            # view over the sealed oracle tree, and `import-faction` stamps THAT
            # identity into the coverage report. Comparing the outer install
            # catalog here made the check unsatisfiable for every RotWK
            # publish - it reported a "stale coverage" mismatch on a report that
            # had just been generated minutes earlier, and the only way past it
            # was `--allow-stale-coverage`, which disables the very check that
            # exists to catch genuinely stale reports. The music publish path a
            # few hundred lines below already documents this exact trap
            # ("Bind to the catalog the COOK resolves against, which may be an
            # effective-assets view over the one loaded above"); this path had
            # not been given the same treatment.
            #
            # The pipeline is constructed here, above the gate, purely to
            # resolve that identity. It is cheap - it loads and wraps the
            # catalog and touches no content - so the gates still fail closed
            # before any expensive cook work.
            pipeline = ImportPipeline(
                catalog,
                _state_root(args),
                game=args.game,
                conversion_cache_enabled=not args.no_conversion_cache,
                conversion_jobs=args.conversion_jobs,
            )
            # Force the deferred RotWK oracle bind (ImportPipeline
            # .source_override_root) so `pipeline.catalog` is the effective
            # view before its identity is read, rather than depending on which
            # attribute happens to be touched first.
            #
            # A MISSING oracle tree must not be reported as a coverage-binding
            # failure: those are different problems with different fixes, and
            # this gate's job is to compare identities, not to police the tree's
            # existence. The cook a few lines below asks for the same oracle and
            # fails with its own explicit "run extract-all-assets" message, so
            # nothing is swallowed - the run still stops, with the accurate
            # error rather than a misleading "stale coverage" one. With no
            # effective view to speak of, the outer catalog is the only defined
            # identity, so comparing against it here is exact, not a guess.
            try:
                _ = pipeline.source_override_root
                cook_catalog = pipeline.catalog
            except FileNotFoundError:
                cook_catalog = catalog
            binding_blockers = _coverage_binding_blockers(
                coverage_root, factions, catalog_identity=cook_catalog.identity_sha256()
            )
            if binding_blockers:
                if not args.allow_stale_coverage:
                    return _refuse(
                        "converted coverage does not describe the content "
                        "being cooked.",
                        binding_blockers,
                        "--allow-stale-coverage",
                    )
                _warn_override("--allow-stale-coverage", binding_blockers)
            faction_slug = "-".join(factions)
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
                    / f"faction-slice-{faction_slug}.generated.json"
                )
            ).expanduser()
            progress_emit(
                "compose",
                f"composing {', '.join(factions)} coverage into pack profile",
            )
            base = json.loads(base_profile_path.read_text(encoding="utf-8"))
            if not isinstance(base, dict):
                raise ValueError(f"base profile root is not an object: {base_profile_path}")
            # Pack strings lane: the composed pack ships a data/strings.json
            # covering every CONTROLBAR: id its own runtime documents
            # reference, resolved against the SAME table the unit compiler
            # records sourceNullStringIds evidence from. A publish that cannot
            # see the retail table must refuse rather than ship a pack whose
            # HUD text silently never resolves.
            from .faction_import import load_retail_string_catalog

            string_catalog = load_retail_string_catalog(cook_catalog)
            if string_catalog is None:
                raise ValueError(
                    "data/lotr.str is unresolved by the cook catalog; the pack "
                    "strings document cannot be composed"
                )
            # The RotWK Men host pack owns the Create-a-Hero system table
            # (same single-owner rule as livingWorld). Fail closed: a Men host
            # publish without the compiled table would ship a slice whose MY
            # HEROES screen can never answer, so name the missing artifact and
            # the command that produces it instead of cooking without it.
            cah_runtime = None
            if args.game == "rotwk" and factions == ["men"]:
                cah_runtime_path = (
                    _state_root(args)
                    / "reports"
                    / f"{args.game}-cah-system-runtime.json"
                )
                if not cah_runtime_path.is_file():
                    raise FileNotFoundError(
                        f"CaH system runtime missing at {cah_runtime_path}; "
                        f"run: openbfme-import compile-cah-system --game {args.game} "
                        "--assets-root <effective-assets> first (the RotWK Men "
                        "host pack owns cah.system and must ship it)"
                    )
                cah_runtime = json.loads(
                    cah_runtime_path.read_text(encoding="utf-8")
                )
                if not isinstance(cah_runtime, dict):
                    raise ValueError(
                        f"CaH system runtime root is not an object: {cah_runtime_path}"
                    )
            composed, receipt = compose_faction_profile(
                base,
                coverage_root,
                factions,
                game=args.game,
                string_catalog=string_catalog,
                cah_runtime=cah_runtime,
            )
            # `pipeline` / `cook_catalog` were already built above so the
            # coverage-binding gate could read the cook's real catalog identity.
            pack = composed.get("pack")
            if isinstance(pack, dict):
                # The base profile is the historical BFME2 Men host, but its
                # identity must never leak into a RotWK Men publication.
                # Single- and multi-faction products are both edition-qualified.
                pack["id"] = _faction_slice_pack_id(args.game, factions)
                if factions == ["men"]:
                    composed["title"] = (
                        f"{args.game.upper()} Men full faction vertical slice"
                    )
                elif len(factions) > 1:
                    composed["title"] = (
                        f"{args.game.upper()} {', '.join(factions)} faction slice"
                    )
            # Freshly composed profiles bind to the catalog they were composed
            # against; inherited m3 markers otherwise fail the build's source
            # catalog identity check with no stamping path.
            if isinstance(pack, dict) and (
                "sourceCatalogIdentitySha256" not in pack
                or args.game != "bfme2"
            ):
                # Non-BFME2 publishes always rebind: the host base profile is
                # composed from the BFME2 men slice and carries that game's
                # catalog identity, but this cook resolves everything against
                # the expansion catalog it was invoked with.
                pack["sourceCatalogIdentitySha256"] = cook_catalog.identity_sha256()
            write_json_atomic(profile_output, composed)
            receipt_path = profile_output.with_suffix(".receipt.json")
            write_json_atomic(receipt_path, receipt)
            # Validate the composed profile loads under the import schema.
            ImportProfile.load(profile_output)
            progress_emit(
                "compose",
                f"profile={profile_output.name} objects={len(receipt.get('objects', []))}",
            )
            resolved = resolve_profile(
                ImportProfile.load(profile_output), cook_catalog
            )
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
            value["faction"] = factions[0] if len(factions) == 1 else faction_slug
            value["factions"] = factions
            value["composed_objects"] = len(receipt.get("objects", []))
            if light_audit:
                value["bundle_sha256"] = "dev-skipped"
                value["dev_mode"] = True
            else:
                value["bundle_sha256"] = bundle_digest(pack_root)
            value["conversion_cache"] = pipeline.conversion_cache_stats
            value.update(_conversion_failure_report(pack_root))
            value["playable_unit_count"] = _pack_playable_unit_count(pack_root)
            # Post-cook half of the gate: even a clean-looking cook must not
            # drop playable-unit *names* the published bundle already ships.
            try:
                # Only when this cook actually publishes: --no-publish leaves
                # the installed slice untouched, so there is nothing to regress.
                regression = (
                    None
                    if args.no_publish
                    else publish_gate.enforce_playable_unit_gate(
                        pack_root,
                        args.godot_content_root,
                        pack["id"] if isinstance(pack, dict) else "",
                        allow_fewer=args.allow_fewer_playable_units,
                    )
                )
            except publish_gate.PublishGateError as gate_error:
                value["playable_unit_regression"] = gate_error.blockers[0]
                _render(value, args.json)
                return _refuse(
                    "this cook drops playable units the slice already ships.",
                    gate_error.blockers,
                    gate_error.override_flag,
                )
            if regression is not None:
                value["playable_unit_regression"] = regression
                _warn_override("--allow-fewer-playable-units", [regression])
            if not args.no_publish:
                if args.select:
                    progress_emit("publish", "selecting pack for Godot vertical slice")
                else:
                    progress_emit(
                        "publish",
                        "publishing pack bundle (selection.json untouched; "
                        "pass --select to activate)",
                    )
                # A full (non-light) audit plus bundle_digest just ran over
                # this exact pack above; publication would otherwise repeat
                # both full passes. Only hand the digest over when the audit
                # really was full and clean - dev/light runs must re-verify.
                reusable_digest = None
                if not light_audit and value.get("valid", False):
                    reusable_digest = value["bundle_sha256"]
                publication = pipeline.publish_to_godot(
                    pack_root,
                    args.godot_content_root,
                    allow_incomplete=bool(args.allow_incomplete),
                    select=bool(args.select),
                    verified_digest=reusable_digest,
                )
                value.update(publication)
                # Durable record of what was published so orchestration can
                # retarget selection entries without racing the live file.
                publish_receipt = {
                    "schema": "openbfme.publish-receipt",
                    "schemaVersion": 0,
                    "faction": args.faction,
                    "game": args.game,
                    "packId": publication["pack_id"],
                    "bundleSha256": publication["bundle_sha256"],
                    "packRelative": publication["pack_relative"],
                    "publishedPack": publication["published_pack"],
                    "contentRoot": str(args.godot_content_root),
                    "selected": bool(args.select),
                    "selectionUpdateCommand": (
                        "openbfme-import update-selection-entry "
                        f"--pack-id {publication['pack_id']} "
                        f"--bundle-sha256 {publication['bundle_sha256']}"
                    ),
                }
                publish_receipt_path = profile_output.with_suffix(".publish.json")
                write_json_atomic(publish_receipt_path, publish_receipt)
                value["publish_receipt"] = str(publish_receipt_path)
            progress_complete(
                f"faction={faction_slug} pack={pack_root} slice path ready"
            )
            _render(value, args.json)
            return 0 if value.get("valid", False) else 3
        if args.command == "publish-music-pack":
            # Everything this pack ships is derived from two INIs and the
            # music-script library map, all read through the same install
            # catalog every other pack resolves against.
            def _catalog_bytes(virtual_path: str, maximum: int) -> bytes:
                entry = catalog.resolve_exact(virtual_path)
                if entry is None:
                    raise FileNotFoundError(
                        f"{virtual_path} is missing from the {args.game} catalog"
                    )
                return catalog.open_archive_for(entry).read_entry(
                    catalog.as_entry(entry), max_bytes=maximum
                )

            pack_id = args.pack_id or f"{args.game}-music-vslice"
            music_ini = _catalog_bytes(MUSIC_INI_PATH, 8 * 1024 * 1024)
            misc_audio_ini = _catalog_bytes(MISC_AUDIO_INI_PATH, 8 * 1024 * 1024)
            script_library = _catalog_bytes(
                MUSIC_SCRIPT_LIBRARY_PATH, 32 * 1024 * 1024
            )
            # Read only to prove the library path this command hard-codes is
            # the one audiosettings.ini actually names; a retail edition that
            # moved it must fail loudly instead of silently binding nothing.
            audio_settings = _catalog_bytes(AUDIO_SETTINGS_INI_PATH, 4 * 1024 * 1024)
            declared_library = (
                MUSIC_SCRIPT_LIBRARY_PATH.rsplit("/", 1)[-1].encode("ascii")
                in audio_settings.replace(b"\\", b"/").lower()
            )
            if not declared_library:
                raise ValueError(
                    "audiosettings.ini does not name "
                    f"{MUSIC_SCRIPT_LIBRARY_PATH}; refusing to bind music "
                    "scripts this edition does not declare"
                )
            document = build_music_document(
                music_ini=music_ini,
                misc_audio_ini=misc_audio_ini,
                script_document=map_scripts_document(
                    script_library, container="map"
                ),
                provenance={
                    "game": args.game,
                    "catalogIdentitySha256": catalog.identity_sha256(),
                    "musicIni": MUSIC_INI_PATH,
                    "miscAudioIni": MISC_AUDIO_INI_PATH,
                    "audioSettingsIni": AUDIO_SETTINGS_INI_PATH,
                    "musicScriptLibrary": MUSIC_SCRIPT_LIBRARY_PATH,
                },
            )
            profile_output = Path(
                args.profile_output
                or (_workspace_root(args) / "profiles" / f"{pack_id}.generated.json")
            ).expanduser()
            pipeline = ImportPipeline(catalog, _state_root(args), game=args.game)
            composed = compose_music_profile(
                document,
                pack_id=pack_id,
                game=args.game,
                # Bind to the catalog the COOK resolves against, which may be
                # an effective-assets view over the one loaded above; stamping
                # the outer catalog fails the build's own identity check.
                catalog_identity=pipeline.catalog.identity_sha256(),
            )
            write_json_atomic(profile_output, composed)
            ImportProfile.load(profile_output)
            pack_root = pipeline.build(
                resolve_profile(ImportProfile.load(profile_output), pipeline.catalog),
                force=args.force,
            )
            light_audit = bool(args.dev) or os.environ.get(
                "OPENBFME_DEV", ""
            ).strip().casefold() in {"1", "true", "yes"}
            value = audit_pack(pack_root, light=light_audit)
            value["pack"] = str(pack_root)
            value["profile"] = str(profile_output)
            value["pack_id"] = pack_id
            value["music_factions"] = sorted(document["factions"])
            value["music_tracks"] = len(document["tracks"])
            value["music_playlists"] = len(document["playlists"])
            if light_audit:
                value["bundle_sha256"] = "dev-skipped"
                value["dev_mode"] = True
            else:
                value["bundle_sha256"] = bundle_digest(pack_root)
            if not args.no_publish:
                # Supplemental content only: this pack declares `music` and no
                # faction/object rows, so it never needs to be the active pack.
                # selection.json is untouched here on purpose - retarget it
                # with `update-selection-entry` once the bundle is published.
                publication = pipeline.publish_to_godot(
                    pack_root,
                    args.godot_content_root,
                    select=False,
                    verified_digest=(
                        value["bundle_sha256"]
                        if not light_audit and value.get("valid", False)
                        else None
                    ),
                )
                value.update(publication)
                value["selectionUpdateCommand"] = (
                    "openbfme-import update-selection-entry "
                    f"--pack-id {publication['pack_id']} "
                    f"--bundle-sha256 {publication['bundle_sha256']}"
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

        if args.command == "census-factions":
            factions = discover_playable_factions(catalog)
            rows = [
                {
                    "name": faction.name,
                    "side": faction.side,
                    "object_count": faction.object_count,
                }
                for faction in factions
            ]
            report = {
                "format": 1,
                "schema": "openbfme.playable-faction-census",
                "schemaVersion": 1,
                "game": args.game,
                "factionCount": len(rows),
                "factions": rows,
            }
            report_path = (
                _workspace_root(args)
                / "reports"
                / f"{args.game}-playable-factions.json"
            )
            write_json_atomic(report_path, report)
            _render(
                {
                    "ready": True,
                    "game": args.game,
                    "report": str(report_path),
                    "faction_count": len(rows),
                    "factions": rows,
                },
                args.json,
            )
            return 0

        if args.command == "living-world":
            from .livingworld import profile_living_world, require_shippable

            if args.output:
                # The document is retail-derived payload: it may never land
                # inside the repository, only in a pack or a private workspace.
                document_path = ensure_external_to_repo(
                    Path(args.output).expanduser().resolve(),
                    repo_root_from_module(),
                )
            else:
                document_path = (
                    _workspace_root(args)
                    / "reports"
                    / f"{args.game}-living-world.json"
                )
            document = require_shippable(profile_living_world(catalog, args.game))
            write_json_atomic(document_path, document)
            campaigns = [
                {
                    "name": campaign["name"],
                    "regions": len(campaign["regions"]),
                    "connections": sum(
                        len(region["connections"])
                        for region in campaign["regions"]
                    ),
                }
                for campaign in document["regionCampaigns"]
            ]
            _render(
                {
                    "ready": True,
                    "game": args.game,
                    "document": str(document_path),
                    "region_count": document["regionCount"],
                    "connection_count": document["connectionCount"],
                    "scenario_count": document["scenarioCount"],
                    "campaigns": campaigns,
                    "gap_count": len(document["gaps"]),
                },
                args.json,
            )
            return 0

        if args.command == "census-strategic":
            from .strategic_census import census_strategic_surface

            report = census_strategic_surface(catalog, args.game)
            report_path = (
                _workspace_root(args)
                / "reports"
                / f"{args.game}-strategic-census.json"
            )
            write_json_atomic(report_path, report)
            _render(
                {
                    "ready": True,
                    "game": args.game,
                    "report": str(report_path),
                    "file_count": report["fileCount"],
                    "surface_file_counts": report["surfaceFileCounts"],
                    "family_totals": report["familyTotals"],
                    "unresolved_file_count": len(report["unresolvedFiles"]),
                    "unclassified_block_count": len(report["unclassifiedBlocks"]),
                },
                args.json,
            )
            return 0

        if args.command == "census-faction":
            faction = resolve_playable_faction(catalog, args.faction)
            template, side = faction.name, faction.side
            report = census_playable_faction(
                catalog,
                player_template=template,
                game=args.game,
                expected_side=side,
                implicit_object_roots=implicit_object_roots(
                    template, game=args.game
                ),
            )
            report_path = (
                _workspace_root(args)
                / "reports"
                / f"{faction.short_name}-faction-leaf-census.json"
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
            map_set = getattr(args, "map_set", "five-map")
            if map_set == "five-map":
                if args.game != "bfme2":
                    raise ValueError(
                        "the five-map development set is BFME2 only; use "
                        "--map-set skirmish for other editions"
                    )
                profile = build_five_map_profile(catalog)
                generated_name = "five-maps.generated.json"
            else:
                assets_root = getattr(args, "effective_assets", None)
                binder = None
                if assets_root is not None:
                    from .map_prop_bindings import load_effective_assets_manifest

                    binder = make_effective_assets_binder(
                        assets_root, load_effective_assets_manifest(assets_root)
                    )
                profile = build_category_map_profile(
                    catalog,
                    game=args.game,
                    map_set=map_set,
                    strict=bool(getattr(args, "strict", False)),
                    target_limit=getattr(args, "map_limit", None),
                    **({"binder": binder} if binder is not None else {}),
                )
                generated_name = f"{args.game}-{map_set}-maps.generated.json"
            generated_path = _workspace_root(args) / "profiles" / generated_name
            write_json_atomic(generated_path, profile)
            payload = (
                json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
            )
            evidence = profile.get("planning_evidence", {})
            value = {
                "ready": True,
                "profile": str(generated_path),
                "profile_sha256": hashlib.sha256(payload.encode("utf-8")).hexdigest(),
                "resource_count": len(profile["resources"]),
                "map_count": len(profile["runtime_data"]["data/maps.json"]["maps"]),
                "rejected_map_count": len(evidence.get("rejectedMaps", [])),
                "unbound_object_type_count": sum(
                    int(row["count"])
                    for row in evidence.get("unboundObjectTypes", {}).values()
                ),
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
            # pipeline.build returns the pack directory as `pack` in this
            # command path; do not reference an undefined pack_root here
            # (prior UnboundLocalError after a successful audit).
            value.update(_conversion_failure_report(pack))
            value["playable_unit_count"] = _pack_playable_unit_count(pack)
            # `build` publishes AND (unless --no-select) activates the pack, so
            # it was the widest of the three bypass routes around the publish
            # gates. It now runs the same roster-regression gate.
            #
            # It deliberately does NOT run the coverage or binding gates. Those
            # two read `<faction>-coverage.json`, an artefact only the faction
            # import lane produces; `build` cooks from an arbitrary profile
            # that may name no faction at all (map packs, leaf profiles), so
            # there is no report to bind against and demanding one would refuse
            # every legitimate map-pack build. The roster gate needs no such
            # artefact - it compares the cook against what is already on disk.
            # Only when this build actually publishes. A --no-publish proof
            # build (tools/gate-retail.ps1 runs two of them) cooks into the
            # private pack root and ships nothing, so there is no publication
            # to refuse and blocking it would break the gate.
            build_pack_id = getattr(resolved, "pack_id", "") or ""
            try:
                build_regression = (
                    None
                    if args.no_publish
                    else publish_gate.enforce_playable_unit_gate(
                        pack,
                        args.godot_content_root,
                        build_pack_id,
                        allow_fewer=args.allow_fewer_playable_units,
                    )
                )
            except publish_gate.PublishGateError as gate_error:
                value["playable_unit_regression"] = gate_error.blockers[0]
                _render(value, args.json)
                return _refuse(
                    "this build drops playable units the slice already ships.",
                    gate_error.blockers,
                    gate_error.override_flag,
                )
            if build_regression is not None:
                value["playable_unit_regression"] = build_regression
                _warn_override("--allow-fewer-playable-units", [build_regression])
            if not args.no_publish:
                progress_emit("assemble", "publishing pack to Godot content root")
                value.update(
                    pipeline.publish_to_godot(
                        pack,
                        args.godot_content_root,
                        allow_incomplete=bool(args.allow_incomplete),
                        select=not bool(getattr(args, "no_select", False)),
                    )
                )
            progress_complete(f"report={pack} pack build finished")
            _render(value, args.json)
            return 0 if value["valid"] else 3
        parser.error(f"unknown command: {args.command}")
    except EffectiveAssetsIdentityError as exc:
        # Structured, and never a silent read of another edition's bytes.
        if args.json:
            print(json.dumps(exc.diagnostic, indent=2, sort_keys=True), file=sys.stderr)
        else:
            print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    except CatalogProvenanceError as exc:
        # Structured, never a silent fallback to another edition.
        if args.json:
            print(json.dumps(exc.diagnostic, indent=2, sort_keys=True), file=sys.stderr)
        else:
            print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    except (FileNotFoundError, ValueError, RuntimeError, OSError) as exc:
        if args.json:
            print(json.dumps({"error": str(exc)}, indent=2), file=sys.stderr)
        else:
            print(f"ERROR: {exc}", file=sys.stderr)
        return 1
    finally:
        if dev_env_prior is not None:
            _restore_env(dev_env_prior)
        _restore_env(state_root_env_prior)
    return 0


if __name__ == "__main__":
    # Without this hook, `python -m openbfme_importer.cli <args>` imports the
    # module, runs nothing, and exits 0 — a silent no-op that masked seven
    # dropped selection.json updates on 2026-08-05. Pinned by
    # tests/test_cli_module_entrypoint.py.
    raise SystemExit(main())
