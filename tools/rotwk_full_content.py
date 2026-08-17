#!/usr/bin/env python3
"""One-button RotWK convert, faction pack-proof, and bound 10-map publish.

Publishing never changes ``selection.json`` unless ``--select`` is explicit.
Retail outputs remain under the configured private state/content roots.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))
sys.path.insert(0, str(ROOT / "tools"))

from openbfme_importer.game import .private_root  # noqa: E402
from openbfme_importer.faction_policy import implicit_object_roots  # noqa: E402
from openbfme_importer.paths import (  # noqa: E402
    ensure_external_to_repo,
    repo_root_from_module,
)
from openbfme_importer.util import write_json_atomic  # noqa: E402
from rotwk_layered_install import ensure_layered_rotwk_install  # noqa: E402

FACTIONS = ("men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar")


def _fingerprint(path: Path) -> str | None:
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def _run(
    label: str,
    command: list[str],
    env: dict[str, str],
    *,
    ok_exits: set[int] | None = None,
) -> int:
    print(f"\n=== {label} ===", flush=True)
    print("RUN", " ".join(command), flush=True)
    completed = subprocess.run(command, cwd=ROOT, env=env, check=False)
    allowed = ok_exits or {0}
    if completed.returncode not in allowed:
        raise RuntimeError(f"{label} failed with exit {completed.returncode}")
    if completed.returncode != 0:
        print(
            f"WARN {label}: exit {completed.returncode} allowed ({sorted(allowed)})",
            flush=True,
        )
    return completed.returncode


def _load_object(path: Path) -> dict[str, Any]:
    value = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(value, dict):
        raise RuntimeError(f"JSON root is not an object: {path}")
    return value


HOST_PROFILE_NAME = "men-fords-v1.generated.json"


def _ensure_men_host_profile(
    state_root: Path,
    *,
    python: Path,
    env: dict[str, str],
    layered: Path,
) -> Path:
    """Return the Men/Fords host profile, deriving it when absent.

    ``publish-faction-to-slice`` extends this generated BFME2 Men host
    skeleton and fails closed when it is missing -- before this check ran,
    that refusal fired only at the pack-proof stage, AFTER the ~45-minute
    seven-faction convert. Derivation uses only the documented generators:
    ``m3_pack_expansion compose`` for the hash-bound skeleton and the
    ``living-world`` CLI for the strategic document the RotWK Men host pack
    owns. When any hash-bound input is absent this raises BEFORE the convert
    and names every missing artifact.
    """

    candidates = [
        state_root / "profiles" / HOST_PROFILE_NAME,
        .private_root(state_root, "rotwk") / "profiles" / HOST_PROFILE_NAME,
    ]
    existing = next((p for p in candidates if p.is_file()), None)
    if existing is not None:
        print(f"HOST_PROFILE {existing}", flush=True)
        return existing

    recipe_path = ROOT / "importer" / "profiles" / "men-fords-v1.json"
    base_profile = state_root / "profiles" / "men-fords-v0-complete.generated.json"
    census = state_root / "reports" / "men-faction-leaf-census.json"
    assets = state_root / "cache" / "effective-assets"
    manifest = assets / ".openbfme" / "manifest.json"
    catalog_path = state_root / "catalog" / "bfme2.json"
    missing = [
        str(path)
        for path in (recipe_path, base_profile, census, manifest, catalog_path)
        if not path.is_file()
    ]
    if not assets.is_dir():
        missing.append(str(assets))
    if missing:
        raise RuntimeError(
            "Men/Fords host profile is missing and its documented generator "
            "inputs are absent from the state root: " + "; ".join(missing)
        )
    recipe = _load_object(recipe_path)
    targets = (((recipe.get("pack") or {}).get("m3Recipe") or {}).get("targets") or {})
    objects = [
        *list(targets.get("buildings") or []),
        *list(targets.get("units") or []),
        *list(targets.get("selectionTransitions") or []),
    ]
    if not objects:
        raise RuntimeError(f"host profile recipe carries no targets: {recipe_path}")
    from openbfme_importer.retail_visual_closure import (  # noqa: E402
        default_visual_closure_report_name,
    )

    closure = (
        state_root / "reports" / default_visual_closure_report_name(objects)
    )
    if not closure.is_file():
        raise RuntimeError(
            f"host profile visual closure missing: {closure}; generate it with "
            f"openbfme-import visual-closure --game bfme2 --objects <recipe targets>"
        )
    from openbfme_importer.catalog import InstallCatalog  # noqa: E402

    bfme2_catalog_identity = InstallCatalog.load(catalog_path).identity_sha256()

    living_world = (
        .private_root(state_root, "rotwk") / "reports" / "rotwk-living-world.json"
    )
    if not living_world.is_file():
        _run(
            "generate living-world strategic document",
            [
                str(python),
                str(ROOT / "tools" / "openbfme_import.py"),
                "living-world",
                "--install",
                str(layered),
                "--game",
                "rotwk",
            ],
            env,
        )
    living_world_doc = _load_object(living_world)
    if living_world_doc.get("schema") != "openbfme.living-world":
        raise RuntimeError(f"living-world document schema is invalid: {living_world}")

    composing = state_root / "profiles" / "men-fords-v1.composing.json"
    _run(
        "compose men-fords-v1 host profile (m3 expansion)",
        [
            str(python),
            "-m",
            "openbfme_importer.m3_pack_expansion",
            "compose",
            "--recipe",
            str(recipe_path),
            "--base-profile",
            str(base_profile),
            "--census",
            str(census),
            "--visual-closure",
            str(closure),
            "--assets-root",
            str(assets),
            "--effective-manifest",
            str(manifest),
            "--expected-catalog-identity",
            bfme2_catalog_identity,
            "--output",
            str(composing),
            "--private-root",
            str(ROOT / "workspace"),
        ],
        env,
    )
    profile = _load_object(composing)
    runtime_data = profile.get("runtime_data")
    pack_files = (profile.get("pack") or {}).get("files")
    if not isinstance(runtime_data, dict) or not isinstance(pack_files, dict):
        composing.unlink(missing_ok=True)
        raise RuntimeError(f"composed host profile is malformed: {composing}")
    # Single-owner overlay: the RotWK Men host pack ships the generated
    # living-world document (faction_slice_profile preserves it only there).
    runtime_data["data/living-world.json"] = living_world_doc
    pack_files["livingWorld"] = "data/living-world.json"
    out = state_root / "profiles" / HOST_PROFILE_NAME
    write_json_atomic(out, profile)
    composing.unlink(missing_ok=True)
    print(f"HOST_PROFILE derived via documented pipeline -> {out}", flush=True)
    return out


def _serial_publication_rows(state_root: Path) -> list[dict[str, Any]]:
    """Load and fail closed on the seven receipts produced by serial proof runs."""

    coverage_root = .private_root(state_root, "rotwk") / "reports" / "faction-import"
    rows: list[dict[str, Any]] = []
    for faction in FACTIONS:
        path = coverage_root / f"{faction}-publication.json"
        if not path.is_file():
            raise RuntimeError(f"missing serial faction publication receipt: {path}")
        row = _load_object(path)
        published = Path(str(row.get("publishedPack") or ""))
        bundle = str(row.get("bundleSha256") or "")
        if (
            row.get("faction") != faction
            or row.get("publicationReady") is not True
            or row.get("auditValid") is not True
            or row.get("conversionFailures") != 0
            or len(bundle) != 64
            or published.name != bundle
            or published.parent.name != f"rotwk-{faction}-vslice"
            or not published.is_dir()
        ):
            raise RuntimeError(
                f"serial faction publication receipt is not ready: {faction}"
            )
        rows.append(row)
    return rows


def _select_existing_publications(
    state_root: Path, *, content_root: Path, selection: Path
) -> dict[str, Any]:
    """Atomically compose already-proven serial factions plus the ten-map pack."""

    rows = _serial_publication_rows(state_root)
    supplemental: list[str] = []
    for row in rows:
        published = Path(str(row["publishedPack"])).resolve()
        if published.parent.parent.resolve() != content_root.resolve():
            raise RuntimeError(f"published faction pack escaped content root: {published}")
        supplemental.append(f"{published.parent.name}/{published.name}")
    if len(set(supplemental)) != len(FACTIONS):
        raise RuntimeError("serial faction publication receipts are not seven unique packs")

    map_report_path = state_root / "reports" / "rotwk-multimap-skirmish.json"
    map_report = _load_object(map_report_path)
    build = map_report.get("build") or {}
    catalog_proof = map_report.get("catalogProof") or {}
    pack_mount = catalog_proof.get("packMount") or {}
    published_maps = Path(str(build.get("publishedPack") or "")).resolve()
    if (
        map_report.get("mapCount") != 10
        or map_report.get("rejectedMapCount") != 0
        or catalog_proof.get("ok") is not True
        or pack_mount.get("ok") is not True
        or build.get("exitCode") != 0
        or not published_maps.is_dir()
        or published_maps.parent.name != "rotwk-skirmish-maps-private"
        or published_maps.parent.parent.resolve() != content_root.resolve()
    ):
        raise RuntimeError("published ten-map report is not selection-ready")

    document = {
        "schema": "openbfme.pack-selection",
        "schemaVersion": 0,
        "activePack": f"{published_maps.parent.name}/{published_maps.name}",
        "supplementalPacks": supplemental,
    }
    selection.parent.mkdir(parents=True, exist_ok=True)
    write_json_atomic(selection, document)
    return document


def _assert_convert_batch_ok(state_root: Path, *, convert_exit: int) -> bool:
    """Exit 3 means residual gaps *or* hard failures — only gaps are allowed.

    Inspect the fresh batch report so a failed faction cannot be papered over
    by stale coverage from an earlier successful run.
    """
    report_path = state_root / "reports" / "rotwk-faction-convert-batch.json"
    if not report_path.is_file():
        raise RuntimeError(f"missing convert batch report: {report_path}")
    report = _load_object(report_path)
    rows = report.get("factions") or []
    if not isinstance(rows, list) or not rows:
        raise RuntimeError("convert batch report has no faction rows")
    by_faction = {
        str(row.get("faction") or ""): row
        for row in rows
        if isinstance(row, dict)
    }
    hard: list[str] = []
    residual: list[str] = []
    for faction in FACTIONS:
        row = by_faction.get(faction)
        if row is None:
            hard.append(f"{faction}: missing from batch report")
            continue
        if row.get("error"):
            hard.append(f"{faction}: {row.get('error')}")
            continue
        gaps = int(row.get("gaps") or 0)
        complete = bool(row.get("conversionComplete"))
        if gaps or not complete:
            residual.append(f"{faction}: gaps={gaps} complete={complete}")
    ledger = report.get("ledger") or {}
    if int(ledger.get("failureCount") or 0) > 0:
        hard.append(f"ledger failureCount={ledger.get('failureCount')}")
    if int(ledger.get("sinkErrorCount") or 0) > 0:
        hard.append(f"ledger sinkErrorCount={ledger.get('sinkErrorCount')}")
    if hard:
        raise RuntimeError(
            "convert hard failures (exit "
            f"{convert_exit} is not gap-only): " + "; ".join(hard)
        )
    if residual:
        print(
            "WARN residual converter gaps (honest, continuing): "
            + "; ".join(residual),
            flush=True,
        )
    elif convert_exit not in (0,):
        # Exit non-zero with no residual gaps and no hard failures is unexpected.
        raise RuntimeError(
            f"convert exit {convert_exit} without residual gaps or hard failures"
        )
    return bool(residual)


def _fortress_proof(state_root: Path) -> dict[str, list[str]]:
    coverage_root = .private_root(state_root, "rotwk") / "reports" / "faction-import"
    result: dict[str, list[str]] = {}
    for faction in FACTIONS:
        coverage = _load_object(coverage_root / f"{faction}-coverage.json")
        summary = coverage.get("summary") or {}
        gaps = int(summary.get("converterGapCount") or 0)
        if gaps:
            print(
                f"WARN {faction}: converterGapCount={gaps} "
                f"complete={summary.get('conversionComplete')} "
                "(honest residual; fortress check continues)",
                flush=True,
            )
        converted = {
            str(row.get("id") or "")
            for row in coverage.get("objects") or []
            if isinstance(row, dict) and row.get("status") == "converted"
        }
        gap_ids = sorted(
            str(row.get("id") or "")
            for row in coverage.get("objects") or []
            if isinstance(row, dict) and row.get("status") == "converter-gap"
        )
        fortress = sorted(
            name
            for name in converted
            if "fortress" in name.casefold() or "citadel" in name.casefold()
        )
        pads = sorted(name for name in converted if "expansionpad" in name.casefold())
        if not fortress or not pads:
            raise RuntimeError(
                f"{faction} fortress closure incomplete: fortress/citadel={fortress} pads={pads}"
            )
        # Fortress objects themselves must not be the residual gaps.
        fortress_gaps = [
            name
            for name in gap_ids
            if "fortress" in name.casefold() or "citadel" in name.casefold()
        ]
        if fortress_gaps:
            raise RuntimeError(
                f"{faction} fortress objects still converter-gap: {fortress_gaps}"
            )
        template = str((coverage.get("target") or {}).get("playerTemplate") or "")
        expected_roles = {
            object_id: role
            for object_id, role in implicit_object_roots(template, game="rotwk")
            if role == "fortress-composite-citadel" or role.endswith("-pad")
        }
        proven_roles: list[str] = []
        for object_id, expected_role in expected_roles.items():
            runtime_path = (
                coverage_root
                / faction
                / "objects"
                / object_id.casefold()
                / "runtime.json"
            )
            if not runtime_path.is_file():
                raise RuntimeError(
                    f"{faction} fortress composite runtime missing: {runtime_path}"
                )
            runtime = _load_object(runtime_path)
            if runtime.get("compositeRole") != expected_role:
                raise RuntimeError(
                    f"{faction} {object_id} compositeRole={runtime.get('compositeRole')!r} "
                    f"!= {expected_role!r}"
                )
            proven_roles.append(f"{expected_role}:{object_id}")
        if not any(value.startswith("fortress-composite-citadel:") for value in proven_roles):
            raise RuntimeError(f"{faction} has no proven fortress citadel runtime role")
        if not any("fortress-composite-" in value and "-pad:" in value for value in proven_roles):
            raise RuntimeError(f"{faction} has no proven fortress pad runtime role")
        result[faction] = sorted(proven_roles)
        if gap_ids:
            result[f"{faction}:residualGaps"] = gap_ids[:20]
    return result


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", type=Path, default=os.environ.get("ROTWK_INSTALL"))
    parser.add_argument("--bfme2-install", type=Path, default=None)
    parser.add_argument("--state-root", type=Path, default=ROOT / "workspace" / "retail-work")
    parser.add_argument("--effective-assets", type=Path, default=None)
    parser.add_argument("--python", type=Path, default=None)
    parser.add_argument("--map-limit", type=int, default=10)
    parser.add_argument("--select", action="store_true")
    parser.add_argument(
        "--select-existing",
        action="store_true",
        help=(
            "skip conversion/build and atomically select seven serial faction "
            "publication receipts plus the proven ten-map publication"
        ),
    )
    args = parser.parse_args(argv)

    if args.install is None:
        print("FAIL: pass --install or set ROTWK_INSTALL", file=sys.stderr)
        return 2
    if args.map_limit != 10:
        print("FAIL: the full-content playtest contract requires --map-limit 10", file=sys.stderr)
        return 2
    if args.select_existing and not args.select:
        print("FAIL: --select-existing requires explicit --select", file=sys.stderr)
        return 2
    install = Path(args.install).expanduser().resolve()
    if not (install / "game.dat").is_file():
        print(f"FAIL: RotWK game.dat missing: {install}", file=sys.stderr)
        return 2
    state_root = ensure_external_to_repo(
        Path(args.state_root).expanduser().resolve(), repo_root_from_module()
    )
    try:
        layered = ensure_layered_rotwk_install(
            state_root,
            rotwk_install=install,
            bfme2_install=args.bfme2_install,
        )
    except Exception as exc:
        print(f"FAIL layered install: {exc}", file=sys.stderr)
        return 2
    # REBOUND 2026-08-04 (owner decision): PURE RETAIL 2.01. See
    # tools/rotwk_faction_convert_batch.py::_effective_assets for the evidence
    # that layered-effective-assets is fan-patched (Unofficial 2.02) and is now
    # quarantined rather than canonical.
    canonical_assets = (
        state_root
        / "editions"
        / "rotwk"
        / "cache"
        / "effective-assets"
    )
    assets = (
        Path(args.effective_assets).expanduser().resolve()
        if args.effective_assets
        else canonical_assets
    )

    python = (
        Path(args.python).expanduser().resolve()
        if args.python
        else state_root / "tools" / "python-3.12-env" / "Scripts" / "python.exe"
    )
    if not python.is_file():
        python = Path(sys.executable)
    env = os.environ.copy()
    env["OPENBFME_IMPORT_ROOT"] = str(state_root)
    env["PYTHONPATH"] = str(ROOT / "importer") + os.pathsep + env.get("PYTHONPATH", "")
    selection = ROOT / "workspace" / "content-packs" / "selection.json"
    selection_before = _fingerprint(selection)
    if args.select_existing:
        try:
            document = _select_existing_publications(
                state_root,
                content_root=ROOT / "workspace" / "content-packs",
                selection=selection,
            )
        except Exception as exc:
            print(f"FAIL: {exc}", file=sys.stderr)
            return 4
        print(
            f"SELECTION wrote activePack={document['activePack']} "
            f"supplemental={len(document['supplementalPacks'])}",
            flush=True,
        )
        print("\n========== PLAYTEST ==========")
        print(f"selection: {selection} (updated from proven existing publications)")
        print(f"activePack: {document['activePack']}")
        print("supplementalPacks: " + ", ".join(document["supplementalPacks"]))
        print("factions: " + ", ".join(FACTIONS))
        print("maps: 10 published and pack-mounted")
        print("==============================")
        return 0
    try:
        catalog_path = state_root / "catalog" / "rotwk.json"
        # Refresh the catalog identity cheaply, then reuse the canonical tree
        # only when its sealed manifest matches that exact catalog. A stale
        # overlay/cache is transactionally rebuilt; a matching cache is kept.
        _run(
            "index exact layered install",
            [
                str(python),
                str(ROOT / "tools" / "openbfme_import.py"),
                "index",
                "--install",
                str(layered),
                "--game",
                "rotwk",
                "--reindex",
            ],
            env,
        )
        verify_cmd = [
            str(python),
            str(ROOT / "tools" / "openbfme_import.py"),
            "verify-effective-assets",
            "--assets-root",
            str(assets),
            "--expect-game",
            "rotwk",
        ]
        probe = subprocess.run(
            verify_cmd, cwd=ROOT, env=env, text=True, capture_output=True, check=False
        )
        if probe.returncode != 0:
            if args.effective_assets:
                raise RuntimeError(
                    "explicit effective-assets tree does not match the exact catalog: "
                    + (probe.stderr or probe.stdout).strip()
                )
            raise RuntimeError(
                "owner-selected pure-retail effective-assets oracle is missing or invalid: "
                + (probe.stderr or probe.stdout).strip()
            )
        _run("effective-assets identity", verify_cmd, env)
        # Fail fast or derive the Men/Fords host profile BEFORE the ~45-minute
        # convert: the pack proof refuses closed without it, and deriving it
        # afterwards wastes the whole convert when the state root is fresh.
        _ensure_men_host_profile(
            state_root, python=python, env=env, layered=layered
        )
        convert_cmd = [
            str(python),
            str(ROOT / "tools" / "rotwk_faction_convert_batch.py"),
            "--install",
            str(install),
            "--game",
            "rotwk",
            "--state-root",
            str(state_root),
            "--assets-root",
            str(assets),
        ]
        for faction in FACTIONS:
            convert_cmd.extend(["--faction", faction])
        # exit 3 from convert batch may mean residual gaps OR hard failures;
        # _assert_convert_batch_ok inspects the batch report before continuing.
        convert_exit = _run(
            "convert all seven factions",
            convert_cmd,
            env,
            ok_exits={0, 3},
        )
        has_residual_gaps = _assert_convert_batch_ok(
            state_root, convert_exit=convert_exit
        )
        fortress = _fortress_proof(state_root)
        # The Men host publish fails closed without the compiled Create-a-Hero
        # system table (publish-faction-to-slice ships it as cah.system), so
        # compile it from the same effective-assets oracle before pack proof.
        cah_cmd = [
            str(python),
            str(ROOT / "tools" / "openbfme_import.py"),
            "compile-cah-system",
            "--game",
            "rotwk",
            "--assets-root",
            str(assets),
        ]
        _run("compile Create-a-Hero system table", cah_cmd, env)
        proof_cmd = [
            str(python),
            str(ROOT / "tools" / "rotwk_faction_pack_proof.py"),
            "--install",
            str(install),
            "--game",
            "rotwk",
            "--state-root",
            str(state_root),
            "--publish",
        ]
        for faction in FACTIONS:
            proof_cmd.extend(["--faction", faction])
        # Never --select here: intermediate faction packs must not clobber selection.
        # Waive only honest residual converter-gap coverage when the fresh
        # batch proved it exists. Never pass --allow-incomplete-pack (that
        # would hide pack-build failures). Both flags are required:
        # --allow-incomplete waives only the batch tool's own pre-check and
        # forwards nothing, so without --allow-incomplete-coverage the CLI
        # still refuses the proven-residual coverage before the cook.
        if has_residual_gaps:
            proof_cmd.append("--allow-incomplete")
            proof_cmd.append("--allow-incomplete-coverage")
        _run("pack-proof all seven factions (publish only)", proof_cmd, env)
        map_cmd = [
            str(python),
            str(ROOT / "tools" / "rotwk_multimap_skirmish.py"),
            "--install",
            str(install),
            "--game",
            "rotwk",
            "--state-root",
            str(state_root),
            "--effective-assets",
            str(assets),
            "--full-profile",
            "--build",
            "--publish",
            "--map-limit",
            "10",
        ]
        # No --select: maps pack published but not activated until composite write below.
        _run("cook and publish bound 10-map pack (publish only)", map_cmd, env)
    except Exception as exc:
        print(f"FAIL: {exc}", file=sys.stderr)
        return 3

    if not args.select and _fingerprint(selection) != selection_before:
        print("FAIL: selection.json changed without --select", file=sys.stderr)
        return 4
    faction_report = _load_object(
        state_root / "reports" / "rotwk-faction-pack-proof.json"
    )
    if faction_report.get("publicationReadyCount") != 7:
        print("FAIL: faction publication proof is not 7/7", file=sys.stderr)
        return 4
    map_report = _load_object(state_root / "reports" / "rotwk-multimap-skirmish.json")
    profile = _load_object(Path(str(map_report["profile"])))
    map_ids = [
        str(row.get("id"))
        for row in profile["runtime_data"]["data/maps.json"]["maps"]
    ]
    unbound = list(map_report.get("unboundObjectTypes") or [])
    notable_unbound = [
        name
        for name in unbound
        if name in {"CaptureFlag", "SignalFire"} or "lair" in name.casefold()
    ]
    unbound_sample = list(dict.fromkeys([*notable_unbound, *unbound[:20]]))[:40]
    published = str((map_report.get("build") or {}).get("publishedPack") or "")
    content_root = ROOT / "workspace" / "content-packs"

    # Build supplemental pack paths for all 7 factions from proof report.
    supplemental_by_faction: dict[str, str] = {}
    for row in faction_report.get("factions") or []:
        if not isinstance(row, dict):
            continue
        pack_path = Path(str(row.get("publishedPack") or row.get("pack") or ""))
        # Prefer content-packs mount path if published next to content root.
        rel = None
        if pack_path.is_dir():
            try:
                # published as content-packs/<id>/<bundle>/
                if pack_path.parent.parent.resolve() == content_root.resolve():
                    rel = f"{pack_path.parent.name}/{pack_path.name}".replace("\\", "/")
                elif pack_path.parent.resolve() == content_root.resolve():
                    # content-packs/<id> with single child hash dir
                    children = [p for p in pack_path.iterdir() if p.is_dir()]
                    if len(children) == 1:
                        rel = f"{pack_path.name}/{children[0].name}".replace("\\", "/")
            except OSError:
                rel = None
        if rel is None:
            # Fall back: look for rotwk-<faction>-vslice under content-packs
            faction = str(row.get("faction") or "")
            candidates = sorted(content_root.glob(f"rotwk-{faction}-vslice/*"))
            dirs = [p for p in candidates if p.is_dir()]
            if dirs:
                newest = max(dirs, key=lambda p: p.stat().st_mtime)
                rel = f"{newest.parent.name}/{newest.name}".replace("\\", "/")
        if rel:
            faction = str(row.get("faction") or "")
            if faction not in FACTIONS:
                print(f"FAIL: pack proof returned unknown faction {faction!r}", file=sys.stderr)
                return 4
            if faction in supplemental_by_faction:
                print(f"FAIL: pack proof returned duplicate faction {faction}", file=sys.stderr)
                return 4
            supplemental_by_faction[faction] = rel

    missing_supplementals = [
        faction for faction in FACTIONS if faction not in supplemental_by_faction
    ]
    supplemental = [supplemental_by_faction[faction] for faction in FACTIONS if faction in supplemental_by_faction]

    maps_active = None
    if published:
        pub = Path(published)
        try:
            if pub.parent.parent.resolve() == content_root.resolve():
                maps_active = f"{pub.parent.name}/{pub.name}".replace("\\", "/")
        except OSError:
            maps_active = None
    if maps_active is None:
        # newest rotwk-skirmish-maps-private child
        sk = content_root / "rotwk-skirmish-maps-private"
        if sk.is_dir():
            kids = [p for p in sk.iterdir() if p.is_dir()]
            if kids:
                newest = max(kids, key=lambda p: p.stat().st_mtime)
                maps_active = f"rotwk-skirmish-maps-private/{newest.name}"

    if args.select:
        if not maps_active or missing_supplementals or len(set(supplemental)) != 7:
            print(
                "FAIL: cannot compose selection "
                f"(maps={maps_active!r} supplemental={len(supplemental)}/7 "
                f"missing={missing_supplementals})",
                file=sys.stderr,
            )
            return 4
        document = {
            "schema": "openbfme.pack-selection",
            "schemaVersion": 0,
            "activePack": maps_active,
            "supplementalPacks": supplemental,
        }
        selection.parent.mkdir(parents=True, exist_ok=True)
        write_json_atomic(selection, document)
        print(
            f"SELECTION wrote activePack={maps_active} "
            f"supplemental={len(supplemental)}",
            flush=True,
        )

    print("\n========== PLAYTEST ==========")
    print(f"maps pack: {published or maps_active}")
    print(f"selection: {selection} ({'updated composite' if args.select else 'unchanged'})")
    if args.select and maps_active:
        print(f"activePack: {maps_active}")
        print("supplementalPacks: " + ", ".join(supplemental))
    print("factions: " + ", ".join(FACTIONS))
    print("maps (10): " + ", ".join(map_ids))
    print(
        "fortress closure: "
        + "; ".join(f"{key}={len(value)}" for key, value in fortress.items())
    )
    if unbound:
        print(
            f"known unbound types ({len(unbound)} unique; sample): "
            + ", ".join(unbound_sample)
        )
        print(
            f"full unbound list: {state_root / 'reports' / 'rotwk-multimap-skirmish.json'}"
        )
    else:
        print("known unbound types: none reported")
    print("==============================")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
