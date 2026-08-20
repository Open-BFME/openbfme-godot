#!/usr/bin/env python3
"""Run import-faction --convert for every playable RotWK (or BFME2) faction.

Writes:
  - conversion ledger JSONL + summary with converted/gap/failed percentages
  - durable ``<workspace>/reports/faction-import/<faction>-coverage.json``
  - per-object pack-recipe / runtime artifacts under
    ``.../faction-import/<faction>/objects/<id>/`` (same layout as the CLI)

``publicationReady`` on convert coverage stays false by design until a pack
cook/audit receipt exists (see ``tools/rotwk_faction_pack_proof.py``).

Does not rewrite selection.json.

Requires the canonical manifest-sealed effective-assets tree (extracted from
the layered indexed install for RotWK).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import time
import traceback
import uuid
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.catalog import (  # noqa: E402
    ArchivePolicy,
    DEFAULT_BFME2_ARCHIVE_POLICY,
    InstallCatalog,
    catalog_provenance_reason,
)
from openbfme_importer.conversion_ledger import ConversionLedger  # noqa: E402
from openbfme_importer.effective_assets_identity import (  # noqa: E402
    verify_effective_assets,
)
from openbfme_importer.effective_assets_catalog import EffectiveAssetsCatalog  # noqa: E402
from openbfme_importer.faction_census import resolve_playable_faction  # noqa: E402
from openbfme_importer.faction_import import convert_faction_import  # noqa: E402
from openbfme_importer.game import workspace_root  # noqa: E402
from openbfme_importer.paths import (  # noqa: E402
    ensure_external_to_repo,
    repo_root_from_module,
)
from openbfme_importer.progress import configure_progress  # noqa: E402
from openbfme_importer.util import write_json_atomic  # noqa: E402


def faction_import_report_root(state_root: Path, game: str) -> Path:
    """Match CLI layout: BFME2 uses state root; RotWK uses editions/rotwk."""

    return workspace_root(state_root, game) / "reports" / "faction-import"


def _load_catalog(state_root: Path, game: str, install: Path) -> InstallCatalog:
    path = state_root / "catalog" / f"{game}.json"
    source_policy = (
        ArchivePolicy.load(DEFAULT_BFME2_ARCHIVE_POLICY) if game == "bfme2" else None
    )
    if path.is_file():
        catalog = InstallCatalog.load(path)
        reason = catalog_provenance_reason(
            (a.relative_path for a in catalog.archives), game
        )
        if reason is not None:
            raise SystemExit(f"catalog-game-mismatch: {reason}")
        if Path(catalog.install_root).resolve() != install.resolve():
            catalog = InstallCatalog.build(install, source_policy=source_policy)
            path.parent.mkdir(parents=True, exist_ok=True)
            catalog.save(path)
        return catalog
    catalog = InstallCatalog.build(install, source_policy=source_policy)
    path.parent.mkdir(parents=True, exist_ok=True)
    catalog.save(path)
    return catalog


def _effective_assets(state_root: Path, game: str) -> Path:
    """Return the owner-selected canonical asset oracle for this edition.

    REBOUND 2026-08-04 (owner decision): the RotWK oracle is PURE RETAIL 2.01,
    ``editions/rotwk/cache/effective-assets``. It was
    ``layered-effective-assets``, which carries Unofficial-2.02 three-way merge
    markers in 530 INI files (the pure tree has zero) and therefore sells
    fan-patched values - expansion pads at cost 0, 44 ``NeededUpgradeAny`` rows
    where retail authors 9, and rebalanced damage/duration defines. The layered
    tree is quarantined: never read as an oracle, never deleted.
    """

    candidates = [
        (
            "editions",
            "rotwk",
            "cache",
            "effective-assets",
        )
        if game == "rotwk"
        else ("cache", "effective-assets")
    ]
    for rel in candidates:
        path = state_root.joinpath(*rel)
        if path.is_dir():
            return path
    raise SystemExit(f"canonical {game} effective-assets tree missing")


def _discover_factions(catalog: InstallCatalog) -> list[str]:
    from openbfme_importer.faction_census import discover_playable_factions

    rows = discover_playable_factions(catalog)
    sides: list[str] = []
    for row in rows:
        short = getattr(row, "short_name", None)
        if short:
            sides.append(str(short).casefold())
            continue
        side = getattr(row, "side", None) or (
            row.get("side") if isinstance(row, dict) else None
        )
        name = getattr(row, "name", None) or (
            row.get("name") if isinstance(row, dict) else None
        )
        text = str(side or name or row)
        if text.casefold().startswith("faction"):
            text = text[7:]
        sides.append(text.casefold())
    seen: set[str] = set()
    out: list[str] = []
    for s in sides:
        if s not in seen:
            seen.add(s)
            out.append(s)
    if not out:
        raise SystemExit("no playable factions discovered (fail closed)")
    return out


def shard_selector(index: int, count: int):
    """Deterministic round-robin over object ids, independent of the id list.

    A stable digest of the folded id decides the shard, so a worker never needs
    the faction's object list up front and two runs with the same ``count``
    always give the same split.
    """

    def _selected(object_id: str) -> bool:
        digest = hashlib.sha256(object_id.casefold().encode("utf-8")).digest()
        return int.from_bytes(digest[:8], "big") % count == index

    return _selected


def _warm_shards(
    *,
    python: str,
    script: Path,
    install: Path,
    game: str,
    state_root: Path,
    assets: Path,
    factions: list[str],
    procs: int,
) -> dict[str, object]:
    """Fill the durable caches using ``procs`` worker processes.

    The workers are pooled over every requested faction's objects at once, so
    the tail is one slow object rather than one slow faction. They write only
    caches — never coverage, ledger or artifacts — so nothing they produce is
    trusted as content. The parent then runs its ordinary serial pass and finds
    the work already done, which is what keeps the output byte-identical to a
    serial run by construction.
    """

    import subprocess

    started = time.perf_counter()
    # One process per shard, each handling that shard of EVERY faction. Spawning
    # per (faction, shard) instead would re-pay the ~35 s catalog+corpus load
    # once per faction per shard; this pays it once per process.
    work: list[tuple[str, int]] = [("*", index) for index in range(procs)]
    print(
        f"WARM_POOL procs={procs} factions={len(factions)} shards={len(work)}",
        flush=True,
    )
    running: list[tuple[subprocess.Popen, str, int]] = []
    failures: list[str] = []
    pending = list(work)
    logs_root = state_root / "reports" / "warm-shards"
    logs_root.mkdir(parents=True, exist_ok=True)

    def _drain(block: bool) -> None:
        for entry in list(running):
            process, faction, index = entry
            code = process.poll()
            if code is None:
                continue
            running.remove(entry)
            if code != 0:
                # A warm shard is an optimisation. Record it loudly and keep
                # going: the parent recomputes anything the shard missed.
                failures.append(f"{faction}#{index} exit {code}")
                print(
                    f"WARM_SHARD_FAILED {faction}#{index} exit={code} "
                    f"(parent will recompute)",
                    flush=True,
                )
        if block and running:
            process, faction, index = running[0]
            process.wait()

    while pending or running:
        while pending and len(running) < procs:
            faction, index = pending.pop(0)
            log_path = logs_root / f"{game}-shard{index}.log"
            command = [
                python,
                str(script),
                "--install",
                str(install),
                "--game",
                game,
                "--state-root",
                str(state_root),
                "--assets-root",
                str(assets),
            ]
            for name in factions:
                command.extend(["--faction", name])
            command.extend(["--warm-shard", f"{index}/{procs}"])
            handle = log_path.open("w", encoding="utf-8", errors="replace")
            running.append(
                (
                    subprocess.Popen(
                        command, stdout=handle, stderr=subprocess.STDOUT
                    ),
                    faction,
                    index,
                )
            )
        _drain(block=bool(running) and not pending)
        if running and pending:
            time.sleep(0.2)
        elif running:
            _drain(block=True)

    wall_ms = int((time.perf_counter() - started) * 1000)
    print(f"WARM_POOL_DONE wall_ms={wall_ms} failures={len(failures)}", flush=True)
    return {"wallMs": wall_ms, "shards": len(work), "failures": failures}


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--game", choices=("rotwk", "bfme2"), default="rotwk")
    parser.add_argument("--state-root", type=Path, default=None)
    parser.add_argument(
        "--assets-root",
        type=Path,
        default=None,
        help=(
            "exact manifest-sealed effective-assets tree (the canonical "
            "edition cache is used when omitted)"
        ),
    )
    parser.add_argument("--faction", action="append", default=None)
    parser.add_argument(
        "--convert-jobs",
        type=int,
        default=None,
        help=(
            "parallel object convert workers (default: min(16, cpu_count) or "
            "OPENBFME_FACTION_CONVERT_JOBS)"
        ),
    )
    parser.add_argument(
        "--object-procs",
        type=int,
        default=0,
        help=(
            "warm the durable plan/object caches with N worker PROCESSES, "
            "sharded over every requested faction's objects, before the "
            "ordinary serial pass. The convert loop is GIL-bound (measured at "
            "0.98 of 24 cores under 16 threads), so processes are the only way "
            "to use the machine. 0 (default) keeps today's behaviour."
        ),
    )
    parser.add_argument(
        "--warm-shard",
        type=str,
        default=None,
        help=(
            "internal: '<index>/<count>' — convert only this shard of the "
            "faction's objects, writing caches only. No coverage, ledger or "
            "artifact output. Used by --object-procs."
        ),
    )
    parser.add_argument("--plan-only", action="store_true")
    parser.add_argument(
        "--no-write-artifacts",
        action="store_true",
        help=(
            "skip durable coverage/object artifact writes (ledger-only mode; "
            "blocks later pack proof)"
        ),
    )
    parser.add_argument(
        "--progress-log",
        type=Path,
        default=None,
        help=(
            "JSONL progress sink (default: "
            "<state>/reports/<game>-faction-convert-<runId>-progress.jsonl)"
        ),
    )
    parser.add_argument(
        "--verbose-objects",
        action="store_true",
        help="echo every object ledger line to stderr (default: gaps only)",
    )
    args = parser.parse_args(argv)

    operator_install = args.install.expanduser().resolve()
    if not (operator_install / "game.dat").is_file():
        print(f"FAIL: no game.dat at {operator_install}", file=sys.stderr)
        return 2

    state_root = args.state_root
    if state_root is None:
        state_root = ROOT / "workspace" / "retail-work"
    state_root = ensure_external_to_repo(
        Path(state_root).expanduser().resolve(), repo_root_from_module()
    )
    os.environ["OPENBFME_IMPORT_ROOT"] = str(state_root)

    # RotWK unit UI textures mostly live in the BFME2 base. Prefer the layered
    # install so MappedImage resolution sees both installs.
    install = operator_install
    if args.game == "rotwk":
        sys.path.insert(0, str(ROOT / "tools"))
        from rotwk_layered_install import (  # type: ignore
            ensure_layered_rotwk_install,
            layered_rotwk_install,
        )

        layered = layered_rotwk_install(state_root)
        if layered is None:
            try:
                layered = ensure_layered_rotwk_install(
                    state_root, rotwk_install=operator_install
                )
            except Exception as exc:
                print(f"WARN layered install unavailable ({exc}); using operator install")
                layered = None
        if layered is not None:
            install = layered
            print(f"LAYERED_INSTALL {install}", flush=True)

    catalog = _load_catalog(state_root, args.game, install)
    assets = (
        Path(args.assets_root).expanduser().resolve()
        if args.assets_root is not None
        else _effective_assets(state_root, args.game)
    )
    canonical_assets = _effective_assets(state_root, args.game).resolve()
    if assets.resolve() != canonical_assets:
        print(
            "FAIL: --assets-root disagrees with the canonical tree ImportPipeline "
            f"will cook from: supplied={assets} canonical={canonical_assets}",
            file=sys.stderr,
        )
        return 2
    if not assets.is_dir():
        print(f"FAIL: effective-assets tree missing: {assets}", file=sys.stderr)
        return 2
    try:
        verify_effective_assets(
            assets,
            game=args.game,
            # The owner-selected RotWK oracle is sealed to its recorded
            # base+expansion layered catalog. The current synthetic install
            # catalog is a different identity, so verify the sealed edition
            # manifest without falsely rebinding it to that live catalog.
            catalog=None if args.game == "rotwk" else catalog,
            consumer="rotwk-faction-convert-batch",
        )
    except (OSError, RuntimeError, ValueError) as exc:
        print(f"FAIL: effective-assets identity: {exc}", file=sys.stderr)
        return 2
    if args.game == "rotwk":
        catalog = EffectiveAssetsCatalog(assets, base_catalog=catalog)
    factions = args.faction or _discover_factions(catalog)

    if args.warm_shard:
        # Worker mode: convert one shard of one faction into the durable
        # caches and exit. No coverage, no ledger, no artifacts — the parent
        # owns every write that anything downstream reads.
        index_text, _, count_text = args.warm_shard.partition("/")
        shard_index, shard_count = int(index_text), int(count_text)
        if not 0 <= shard_index < shard_count:
            print(f"FAIL: bad --warm-shard {args.warm_shard}", file=sys.stderr)
            return 2
        selector = shard_selector(shard_index, shard_count)
        for faction in factions:
            started = time.perf_counter()
            resolved = resolve_playable_faction(catalog, faction)
            result = convert_faction_import(
                catalog,
                assets,
                resolved.short_name,
                artifact_writer=None,
                state_root=state_root,
                convert_jobs=args.convert_jobs,
                object_selector=selector,
                game=args.game,
            )
            summary = result.get("summary") or {}
            print(
                f"WARM_SHARD {resolved.short_name} {shard_index}/{shard_count} "
                f"objects={summary.get('objectCount')} "
                f"converted={summary.get('convertedCount')} "
                f"cache_hits={summary.get('cacheHits')} "
                f"wall_ms={int((time.perf_counter() - started) * 1000)}",
                flush=True,
            )
        return 0

    warm_pool: dict[str, object] | None = None
    if args.object_procs and args.object_procs > 1 and not args.plan_only:
        warm_pool = _warm_shards(
            python=sys.executable,
            script=Path(__file__).resolve(),
            install=operator_install,
            game=args.game,
            state_root=state_root,
            assets=assets,
            factions=[
                resolve_playable_faction(catalog, faction).short_name
                for faction in factions
            ],
            procs=int(args.object_procs),
        )

    run_id = uuid.uuid4().hex
    reports = state_root / "reports"
    reports.mkdir(parents=True, exist_ok=True)
    ledger_path = reports / f"{args.game}-faction-convert-{run_id}.jsonl"
    progress_path = (
        Path(args.progress_log).expanduser().resolve()
        if args.progress_log
        else reports / f"{args.game}-faction-convert-{run_id}-progress.jsonl"
    )
    configure_progress(
        sink=progress_path,
        stages=["catalog", "census", "faction-plan", "faction-convert", "report"],
    )
    print(f"RUN_ID {run_id}", flush=True)
    print(f"PROGRESS_LOG {progress_path}", flush=True)
    print(f"LEDGER {ledger_path}", flush=True)
    if args.convert_jobs is not None:
        print(f"CONVERT_JOBS {args.convert_jobs}", flush=True)
    ledger = ConversionLedger(
        run_id=run_id,
        game=args.game,
        install_root=str(install),
        sink_path=ledger_path,
    )

    faction_rows: list[dict[str, Any]] = []
    exit_code = 0
    batch_started = time.perf_counter()

    if args.plan_only:
        from openbfme_importer.faction_import import plan_faction_import

        for faction in factions:
            try:
                resolved = resolve_playable_faction(catalog, faction)
                key = resolved.short_name
                plan = plan_faction_import(
                    catalog, assets, key, game=args.game
                )
                objects = plan.get("objects") or []
                ready = sum(1 for o in objects if o.get("status") == "descriptor-ready")
                gaps = sum(1 for o in objects if o.get("status") == "converter-gap")
                excluded = sum(1 for o in objects if o.get("status") == "excluded")
                for obj in objects:
                    st = str(obj.get("status") or "unknown")
                    ledger.record(
                        kind="faction-object",
                        unit_id=f"{key}:{obj.get('id')}",
                        status=(
                            "planned"
                            if st == "descriptor-ready"
                            else "gap"
                            if st == "converter-gap"
                            else "excluded"
                            if st == "excluded"
                            else st
                        ),
                        detail=str(obj.get("reason") or obj.get("family") or ""),
                        log_to_stderr=False,
                    )
                ledger.record(
                    kind="faction",
                    unit_id=key,
                    status="planned",
                    detail=f"ready={ready} gaps={gaps} excluded={excluded}",
                    metrics={"ready": ready, "gaps": gaps, "excluded": excluded},
                )
                faction_rows.append(
                    {
                        "faction": key,
                        "mode": "plan-only",
                        "ready": ready,
                        "gaps": gaps,
                        "excluded": excluded,
                        "objectCount": len(objects),
                    }
                )
            except Exception as exc:
                exit_code = 3
                ledger.record(
                    kind="faction",
                    unit_id=faction,
                    status="failed",
                    error=f"{type(exc).__name__}: {exc}",
                    traceback_tail=traceback.format_exc(),
                )
                faction_rows.append(
                    {"faction": faction, "mode": "plan-only", "error": str(exc)[:500]}
                )
    else:

        coverage_root = faction_import_report_root(state_root, args.game)
        coverage_root.mkdir(parents=True, exist_ok=True)
        write_artifacts = not args.no_write_artifacts
        print(
            f"COVERAGE_ROOT {coverage_root} write_artifacts={write_artifacts}",
            flush=True,
        )

        for faction in factions:
            faction_started = time.perf_counter()
            try:
                resolved = resolve_playable_faction(catalog, faction)
                key = resolved.short_name
                print(f"FACTION_BEGIN {key}", flush=True)
                artifact_writer = None
                coverage_path: Path | None = None
                artifact_root: Path | None = None
                if write_artifacts:
                    artifact_root = coverage_root / key / "objects"

                    def _write_artifact(
                        object_id: str,
                        kind: str,
                        document: object,
                        *,
                        _root: Path = artifact_root,
                    ) -> None:
                        write_json_atomic(
                            _root / object_id.casefold() / f"{kind}.json",
                            document,
                        )

                    artifact_writer = _write_artifact
                result = convert_faction_import(
                    catalog,
                    assets,
                    key,
                    artifact_writer=artifact_writer,
                    state_root=state_root,
                    convert_jobs=args.convert_jobs,
                    artifact_root=artifact_root,
                    game=args.game,
                )
                faction = key
                summary = result.get("summary")
                if not isinstance(summary, dict):
                    raise RuntimeError(
                        f"{faction}: convert result missing summary object"
                    )
                objects = result.get("objects") or []
                converted = int(summary.get("convertedCount") or 0)
                gaps = int(summary.get("converterGapCount") or 0)
                unresolved = int(summary.get("unresolvedLeafCount") or 0)
                complete = bool(summary.get("conversionComplete"))
                wall_ms = int((time.perf_counter() - faction_started) * 1000)
                if write_artifacts:
                    coverage_path = coverage_root / f"{faction}-coverage.json"
                    write_json_atomic(coverage_path, result)
                    print(
                        f"COVERAGE {faction} -> {coverage_path} "
                        f"converted={converted} gaps={gaps} complete={complete} "
                        f"wall_ms={wall_ms} loop_ms={summary.get('convertLoopMs')} "
                        f"cache_hits={summary.get('cacheHits')} "
                        f"ops={summary.get('objectsPerSecond')}/s "
                        f"p50_ms={summary.get('objectElapsedMsP50')} "
                        f"p95_ms={summary.get('objectElapsedMsP95')}",
                        flush=True,
                    )
                for obj in objects:
                    st = str(obj.get("status") or "")
                    if st == "converted":
                        status = "converted"
                    elif st == "converter-gap":
                        status = "gap"
                    else:
                        status = st or "unknown"
                    ledger.record(
                        kind="faction-object",
                        unit_id=f"{faction}:{obj.get('id')}",
                        status=status,
                        detail=str(obj.get("reason") or ""),
                        metrics={
                            "convertElapsedMs": obj.get("convertElapsedMs"),
                            "cacheHit": bool(obj.get("cacheHit")),
                            "family": obj.get("family"),
                        },
                        log_to_stderr=bool(
                            args.verbose_objects or st == "converter-gap"
                        ),
                    )
                # Converter-gap is the hard bar. Residual census leaves
                # (retail-absent audio samples / source-null UI textures) are
                # reported but do not fail the convert batch.
                if not complete or gaps > 0:
                    exit_code = 3
                fully_ok = complete and gaps == 0
                ledger.record(
                    kind="faction",
                    unit_id=faction,
                    status="converted" if fully_ok else "gap",
                    detail=(
                        f"converted={converted} gaps={gaps} "
                        f"unresolvedLeaves={unresolved} complete={complete} "
                        f"wall_ms={wall_ms}"
                    ),
                    metrics={
                        "converted": converted,
                        "gaps": gaps,
                        "unresolvedLeafCount": unresolved,
                        "conversionComplete": complete,
                        "censusLeafCoverageComplete": bool(
                            summary.get("censusLeafCoverageComplete")
                        ),
                        "objectCount": int(summary.get("objectCount") or len(objects)),
                        "wallMs": wall_ms,
                        "convertLoopMs": summary.get("convertLoopMs"),
                        "cacheHits": summary.get("cacheHits"),
                        "objectsPerSecond": summary.get("objectsPerSecond"),
                        "objectElapsedMsP50": summary.get("objectElapsedMsP50"),
                        "objectElapsedMsP95": summary.get("objectElapsedMsP95"),
                        "slowestObjects": summary.get("slowestObjects"),
                    },
                )
                print(
                    f"FACTION_END {faction} status={'ok' if fully_ok else 'gap'} "
                    f"wall_ms={wall_ms}",
                    flush=True,
                )
                faction_rows.append(
                    {
                        "faction": faction,
                        "mode": "convert",
                        "converted": converted,
                        "gaps": gaps,
                        "unresolvedLeafCount": unresolved,
                        "conversionComplete": complete,
                        "objectCount": int(summary.get("objectCount") or len(objects)),
                        "wallMs": wall_ms,
                        "summary": summary,
                        "coveragePath": str(coverage_path) if coverage_path else None,
                        "artifactsWritten": write_artifacts,
                        # Convert stage never claims publication; pack proof is separate.
                        "publicationReady": False,
                        "publicationBlockingReason": summary.get("blockingReason"),
                    }
                )
            except Exception as exc:
                exit_code = 3
                ledger.record(
                    kind="faction",
                    unit_id=faction,
                    status="failed",
                    error=f"{type(exc).__name__}: {exc}",
                    traceback_tail=traceback.format_exc(),
                )
                faction_rows.append(
                    {
                        "faction": faction,
                        "mode": "convert",
                        "error": f"{type(exc).__name__}: {exc}"[:800],
                        "publicationReady": False,
                    }
                )
                print(f"FACTION FAIL {faction}: {exc}", file=sys.stderr)
                print(
                    f"FACTION_END {faction} status=failed wall_ms="
                    f"{int((time.perf_counter() - faction_started) * 1000)}",
                    flush=True,
                )

    batch_wall_ms = int((time.perf_counter() - batch_started) * 1000)
    summary = ledger.write_summary(
        reports / f"{args.game}-faction-convert-ledger-summary.json"
    )
    report = {
        "schema": "openbfme.rotwk-faction-convert-batch",
        "schemaVersion": 1,
        "game": args.game,
        "installRoot": str(install),
        "effectiveAssets": str(assets),
        "planOnly": bool(args.plan_only),
        "runId": run_id,
        "progressLog": str(progress_path),
        "ledgerJsonl": str(ledger_path),
        "batchWallMs": batch_wall_ms,
        "convertJobs": args.convert_jobs,
        "objectProcs": int(args.object_procs or 0),
        "warmPool": warm_pool,
        "coverageRoot": (
            str(faction_import_report_root(state_root, args.game))
            if not args.plan_only
            else None
        ),
        "artifactsWritten": (
            False if args.plan_only else (not args.no_write_artifacts)
        ),
        "factions": faction_rows,
        "ledger": summary,
        "publicationNote": (
            "Convert coverage keeps publicationReady=false. Run "
            "tools/rotwk_faction_pack_proof.py after convert to cook/audit "
            "packs and emit faction-publication receipts."
        ),
    }
    out = reports / f"{args.game}-faction-convert-batch.json"
    write_json_atomic(out, report)
    print(f"REPORT {out}", flush=True)
    print(f"BATCH_WALL_MS {batch_wall_ms}", flush=True)
    # Compact timing table for operators.
    print("FACTION_TIMING", flush=True)
    for row in faction_rows:
        if row.get("error"):
            print(f"  {row.get('faction')}: FAILED {row.get('error')[:120]}", flush=True)
            continue
        print(
            f"  {row.get('faction')}: wall_ms={row.get('wallMs')} "
            f"converted={row.get('converted')} gaps={row.get('gaps')} "
            f"complete={row.get('conversionComplete')} "
            f"cache_hits={(row.get('summary') or {}).get('cacheHits')} "
            f"ops={(row.get('summary') or {}).get('objectsPerSecond')}",
            flush=True,
        )
    # Hard failures only: plan-only converter-gaps are expected burn-down, not
    # process failure. Convert mode already set exit_code on incomplete rows.
    if summary.get("failureCount", 0) > 0 or summary.get("sinkErrorCount", 0) > 0:
        exit_code = max(exit_code, 3)
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
