#!/usr/bin/env python3
"""Compose + cook + audit converted RotWK faction coverage into pack receipts.

This is the publication-stage proof that convert coverage deliberately refuses
to claim (``publicationReady=false`` until a pack/runtime receipt exists).

- Reads ``<coverage-root>/<faction>-coverage.json`` and object artifacts
- Runs ``publish-faction-to-slice`` with ``--no-publish`` (no selection rewrite)
  unless ``--publish`` is passed (still never ``--select`` unless requested)
- Writes ``openbfme.faction-publication-receipt`` next to coverage

Does not invent greening: pack audit must report ``valid=true``.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
import time
import uuid
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.game import workspace_root  # noqa: E402
from openbfme_importer.paths import (  # noqa: E402
    ensure_external_to_repo,
    repo_root_from_module,
)
from openbfme_importer.util import write_json_atomic  # noqa: E402

RECEIPT_SCHEMA = "openbfme.faction-publication-receipt"
RECEIPT_SCHEMA_VERSION = 0
KNOWN_FACTIONS = (
    "men",
    "elves",
    "dwarves",
    "isengard",
    "mordor",
    "wild",
    "angmar",
)


def _default_coverage_root(state_root: Path, game: str) -> Path:
    return workspace_root(state_root, game) / "reports" / "faction-import"


def _parse_cli_json(text: str) -> dict[str, Any]:
    blob = (text or "").strip()
    if not blob:
        raise ValueError("empty CLI stdout")
    try:
        value = json.loads(blob)
        if isinstance(value, dict):
            return value
    except json.JSONDecodeError:
        pass
    decoder = json.JSONDecoder()
    last: dict[str, Any] | None = None
    for index, char in enumerate(blob):
        if char != "{":
            continue
        try:
            obj, _end = decoder.raw_decode(blob[index:])
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict):
            last = obj
    if last is None:
        raise ValueError("no JSON object found in CLI output")
    return last


def _load_coverage(path: Path) -> dict[str, Any]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(raw, dict):
        raise ValueError(f"coverage root is not an object: {path}")
    if raw.get("schema") != "openbfme.faction-import-coverage":
        raise ValueError(f"unsupported coverage schema at {path}")
    summary = raw.get("summary")
    if not isinstance(summary, dict):
        raise ValueError(f"coverage summary missing at {path}")
    return raw


def _artifact_proof(coverage_root: Path, faction: str, coverage: dict[str, Any]) -> dict[str, Any]:
    """Verify converted rows have on-disk pack-recipe / runtime receipts."""
    objects = coverage.get("objects") or []
    converted = [
        row
        for row in objects
        if isinstance(row, dict) and row.get("status") == "converted"
    ]
    missing: list[dict[str, str]] = []
    present = 0
    for row in converted:
        object_id = str(row.get("id") or "")
        family = str(row.get("family") or "")
        root = coverage_root / faction / "objects" / object_id.casefold()
        recipe = root / "pack-recipe.json"
        if not recipe.is_file():
            missing.append(
                {
                    "objectId": object_id,
                    "family": family,
                    "missing": "pack-recipe.json",
                }
            )
            continue
        if family in {"structure", "spellbook"}:
            runtime = root / "runtime.json"
            if not runtime.is_file():
                missing.append(
                    {
                        "objectId": object_id,
                        "family": family,
                        "missing": "runtime.json",
                    }
                )
                continue
        present += 1
    return {
        "convertedCount": len(converted),
        "artifactsPresent": present,
        "missingCount": len(missing),
        "missingSample": missing[:20],
        "ok": len(missing) == 0 and len(converted) > 0,
    }


def coverage_fingerprint(path: Path) -> tuple[int, int, str] | None:
    """Identify a coverage document, or ``None`` if it is not usable yet.

    Returns ``(mtime_ns, size, aggregateSha256)``. ``None`` means the file is
    absent, unreadable, mid-write, or not a coverage document - all of which
    mean "not landed", never "close enough". The aggregate digest is part of
    the identity because a convert that reruns inside one filesystem mtime
    tick still changes it.
    """

    try:
        stat = path.stat()
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, ValueError):
        return None
    if not isinstance(raw, dict):
        return None
    if raw.get("schema") != "openbfme.faction-import-coverage":
        return None
    if not isinstance(raw.get("summary"), dict):
        return None
    aggregate = raw.get("aggregateSha256")
    if not isinstance(aggregate, str) or not aggregate:
        return None
    return (int(stat.st_mtime_ns), int(stat.st_size), aggregate)


def _await_coverage(
    path: Path,
    *,
    baseline: tuple[int, int, str] | None,
    accept_existing: bool,
    deadline: float,
    poll_seconds: float,
    faction: str,
) -> None:
    """Block until *path* holds a coverage document THIS run should publish.

    The bar is deliberately "different from what was there when we started",
    not "exists": a previous run's coverage file is sitting on disk for every
    faction, and treating it as a completion signal would publish stale
    descriptors the instant the watcher started. ``--watch-accept-existing``
    is the explicit opt-out, for reruns and for measuring.

    The identity must also be STABLE across two consecutive polls before the
    file counts as landed. Coverage is written atomically today, so this is
    belt-and-braces rather than the primary guard - but a half-written report
    that happened to parse would otherwise dispatch a cook against it.
    """

    if accept_existing:
        current = coverage_fingerprint(path)
        if current is not None:
            return
    settled: tuple[int, int, str] | None = None
    while True:
        current = coverage_fingerprint(path)
        landed = current is not None and (baseline is None or current != baseline)
        if landed and settled == current:
            return
        settled = current if landed else None
        if time.monotonic() >= deadline:
            raise TimeoutError(
                f"coverage for {faction} did not land at {path} before the "
                f"watch timeout. Nothing was published for it. If the convert "
                f"lane already finished and this file is from that run, pass "
                f"--watch-accept-existing."
            )
        time.sleep(poll_seconds)


def collect_row(faction: str, future: Any, coverage_path: Path) -> dict[str, Any]:
    """Turn one worker's outcome into a report row, never into a lost batch.

    ``proof_one`` is meant to return a failed row rather than raise, but it is
    a long function calling a lot of other code, and one escaping exception
    used to take ``main()`` with it - discarding the rows of every faction that
    had already succeeded. The batch report is the artifact of record; a worker
    that dies must cost its own row, not everyone else's.
    """

    try:
        return future.result()
    except BaseException as exc:  # noqa: BLE001 - a lost report is worse
        print(f"FAIL {faction}: {type(exc).__name__}: {exc}", file=sys.stderr)
        return {
            "faction": faction,
            "coverage": str(coverage_path),
            "status": "failed",
            "publicationReady": False,
            "error": f"{type(exc).__name__}: {exc}"[:1200],
        }


def _pack_incomplete_args(*, allow_incomplete_pack: bool) -> list[str]:
    """Translate only the explicit pack-build waiver to the downstream CLI."""

    return ["--allow-incomplete"] if allow_incomplete_pack else []


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--game", choices=("rotwk", "bfme2"), default="rotwk")
    parser.add_argument("--state-root", type=Path, default=None)
    parser.add_argument(
        "--faction",
        action="append",
        choices=KNOWN_FACTIONS,
        default=None,
        help="faction to pack-proof (repeatable; default: all with coverage)",
    )
    parser.add_argument(
        "--coverage-root",
        type=Path,
        default=None,
        help="override faction-import report root",
    )
    parser.add_argument(
        "--base-profile",
        type=Path,
        default=None,
        help="host profile for compose (default: men-fords-v1.generated.json)",
    )
    parser.add_argument(
        "--artifacts-only",
        action="store_true",
        help="only verify on-disk convert artifacts; skip pack cook",
    )
    parser.add_argument(
        "--allow-incomplete",
        action="store_true",
        help=(
            "allow honest residual converter-gap objects in coverage only; "
            "still pack converted objects. Does NOT waive pack-build failures."
        ),
    )
    parser.add_argument(
        "--allow-incomplete-pack",
        action="store_true",
        help=(
            "forward --allow-incomplete to publish-faction-to-slice (waives "
            "missing required pack resources). Prefer fixing converters instead."
        ),
    )
    # The two overrides below were previously unreachable through this batch
    # tool: `--allow-incomplete` waives only THIS tool's own pre-check and
    # forwards nothing, while `--allow-incomplete-pack` forwards the unrelated
    # pack-BUILD waiver. So a run that needed either downstream gate had no
    # option but `--allow-incomplete-pack`, which waives the wrong thing.
    # They are deliberately separate, separately named, and off by default.
    parser.add_argument(
        "--allow-incomplete-coverage",
        action="store_true",
        help=(
            "forward --allow-incomplete-coverage to publish-faction-to-slice: "
            "publish a faction whose coverage records honest residual CONVERTER "
            "gaps. Every waived gap must be named, with its retail cause, in the "
            "change that uses this."
        ),
    )
    parser.add_argument(
        "--allow-stale-coverage",
        action="store_true",
        help=(
            "forward --allow-stale-coverage to publish-faction-to-slice: cook "
            "coverage whose recorded COMPILER IDENTITY is not the compiler now "
            "on disk. This is the gate that caught 20 unbuildable units across "
            "six faction packs, so the only legitimate use is a lane that has "
            "deliberately changed importer code and is measuring or rebuilding, "
            "never a production recook - re-run import-faction --convert "
            "instead. Named here for the same reason as the flags below: a "
            "waiver that is unreachable from the batch tool gets replaced by "
            "the WRONG waiver, which is worse."
        ),
    )
    parser.add_argument(
        "--allow-fewer-playable-units",
        action="store_true",
        help=(
            "forward --allow-fewer-playable-units to publish-faction-to-slice: "
            "publish a cook whose playable-unit roster drops ids the incumbent "
            "bundle carries. This is a REGRESSION guard, so the only legitimate "
            "use is a baseline change that provably removes non-retail content; "
            "list every dropped id and its citation in the change that uses it."
        ),
    )
    parser.add_argument(
        "--single-build",
        action="store_true",
        help=(
            "forward --single-build to publish-faction-to-slice: record each "
            "cook as non-attested because the cold A/B byte comparison was "
            "skipped. MEASURED 2026-08-20: this buys ZERO seconds today - the "
            "pack cook has no second build to skip, and the flag only "
            "downgrades the reproducibility claim written into provenance. It "
            "is wired so the waiver is reachable and explicit rather than "
            "invented later; do not pass it to make a recook look faster."
        ),
    )
    parser.add_argument(
        "--publish",
        action="store_true",
        help="copy pack into content-packs (still no selection rewrite unless --select)",
    )
    parser.add_argument(
        "--select",
        action="store_true",
        help="rewrite selection.json (integration-owner only; requires --publish)",
    )
    parser.add_argument(
        "--publish-jobs",
        type=int,
        default=1,
        metavar="N",
        help=(
            "how many factions to pack-proof at once (default 1 = the "
            "historical one-at-a-time behaviour, byte-for-byte unchanged). "
            "Each faction cooks its own pack id into its own immutable digest "
            "directory, so concurrent children share no output path. Measured "
            "on a 24-core box: see perf-publish-report.md for N=1/4/7."
        ),
    )
    parser.add_argument(
        "--watch-coverage",
        action="store_true",
        help=(
            "OVERLAP MODE: start each faction's publish the moment ITS convert "
            "coverage lands, instead of waiting for all seven. Total wall "
            "becomes max over factions of (convert_i finish + publish_i) "
            "rather than (all converts) + (all publishes). Combine with "
            "--publish-jobs. A faction whose coverage never lands inside "
            "--watch-timeout-seconds is a FAILED row, never a skipped one."
        ),
    )
    parser.add_argument(
        "--watch-timeout-seconds",
        type=float,
        default=1800.0,
        metavar="SECONDS",
        help="how long --watch-coverage waits for a faction's coverage (default 1800)",
    )
    parser.add_argument(
        "--watch-poll-seconds",
        type=float,
        default=2.0,
        metavar="SECONDS",
        help="coverage poll interval for --watch-coverage (default 2)",
    )
    parser.add_argument(
        "--watch-accept-existing",
        action="store_true",
        help=(
            "treat coverage that is ALREADY on disk when the watch starts as "
            "landed. Off by default on purpose: every faction has a previous "
            "run's coverage file sitting there, and accepting it would publish "
            "the moment the watcher starts. Use for reruns and measurement, "
            "not to paper over a convert that never ran."
        ),
    )
    parser.add_argument(
        "--publish-lock-wait-seconds",
        type=float,
        default=300.0,
        metavar="SECONDS",
        help=(
            "how long a concurrent child may QUEUE for the content root's "
            "publish lock before it refuses exactly as it does today. Only "
            "used when --publish-jobs > 1; a serial run never waits. This is "
            "not a break-in: a lock held by a dead process still fails the "
            "run, just this many seconds later."
        ),
    )
    parser.add_argument("--python", type=Path, default=None)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args(argv)

    if args.select and not args.publish:
        print("FAIL: --select requires --publish", file=sys.stderr)
        return 2

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

    # Match convert batch: RotWK pack cook must resolve UI textures / media
    # against the layered install when present (same catalog identity as convert).
    cook_install = operator_install
    if args.game == "rotwk":
        sys.path.insert(0, str(ROOT / "tools"))
        try:
            from rotwk_layered_install import (  # type: ignore
                ensure_layered_rotwk_install,
                layered_rotwk_install,
            )

            layered = layered_rotwk_install(state_root)
            if layered is None:
                layered = ensure_layered_rotwk_install(
                    state_root, rotwk_install=operator_install
                )
            if layered is not None:
                cook_install = layered
                print(f"LAYERED_INSTALL {cook_install}", flush=True)
        except Exception as exc:
            print(
                f"WARN layered install unavailable ({exc}); using operator install",
                flush=True,
            )

    workspace = workspace_root(state_root, args.game)
    coverage_root = (
        Path(args.coverage_root).expanduser().resolve()
        if args.coverage_root
        else _default_coverage_root(state_root, args.game)
    )
    if not coverage_root.is_dir():
        print(f"FAIL: coverage root missing: {coverage_root}", file=sys.stderr)
        return 2

    base_profile = args.base_profile
    if base_profile is None:
        # Host skeleton lives on the BFME2 state profiles path historically.
        candidates = [
            state_root / "profiles" / "men-fords-v1.generated.json",
            workspace / "profiles" / "men-fords-v1.generated.json",
        ]
        base_profile = next((p for p in candidates if p.is_file()), candidates[0])
    else:
        base_profile = Path(base_profile).expanduser().resolve()
    if not args.artifacts_only and not base_profile.is_file():
        print(f"FAIL: base profile missing: {base_profile}", file=sys.stderr)
        return 2

    python = args.python
    if python is None:
        python = state_root / "tools" / "python-3.12-env" / "Scripts" / "python.exe"
        if not python.is_file():
            python = Path(sys.executable)
    else:
        python = Path(python).expanduser().resolve()

    cli = ROOT / "tools" / "openbfme_import.py"
    if not cli.is_file():
        print(f"FAIL: missing CLI {cli}", file=sys.stderr)
        return 2

    if args.faction:
        factions = list(dict.fromkeys(args.faction))
    elif args.watch_coverage:
        # In watch mode the coverage files are the thing being waited FOR, so
        # "which files exist right now" is precisely the wrong way to choose
        # the roster. Watch every faction this tool knows unless told otherwise.
        factions = list(KNOWN_FACTIONS)
    else:
        factions = [
            name
            for name in KNOWN_FACTIONS
            if (coverage_root / f"{name}-coverage.json").is_file()
        ]
    if not factions:
        print(f"FAIL: no coverage files under {coverage_root}", file=sys.stderr)
        return 2

    run_id = uuid.uuid4().hex
    rows: list[dict[str, Any]] = []
    exit_code = 0
    if args.publish_jobs < 1:
        print("FAIL: --publish-jobs must be at least 1", file=sys.stderr)
        return 2
    concurrent_children = args.publish_jobs > 1 and len(factions) > 1
    child_log_dir = ROOT / "workspace" / "logs" / f"pack-proof-{run_id[:12]}"
    # Snapshot every coverage document BEFORE anything can land, so "landed"
    # means "changed since this run started" rather than "a file exists".
    coverage_baseline: dict[str, tuple[int, int, str] | None] = {
        faction: coverage_fingerprint(coverage_root / f"{faction}-coverage.json")
        for faction in factions
    }
    watch_deadline = time.monotonic() + max(0.0, args.watch_timeout_seconds)
    if args.watch_coverage and args.watch_accept_existing:
        print(
            "WATCH_ACCEPT_EXISTING: coverage already on disk counts as landed; "
            "this run is NOT proving the convert that produced it is current "
            "(the coverage-binding gate still is)",
            flush=True,
        )
    # Split the machine across the children rather than letting each one size
    # itself against every core.
    usable_cores = max(1, (os.cpu_count() or 4) - 2)
    parallel_width = max(1, min(args.publish_jobs, len(factions)))
    child_conversion_jobs = max(2, usable_cores // parallel_width)
    child_hash_workers = max(1, min(8, usable_cores // parallel_width))

    # ONE catalog resolve, in the parent, before any child starts.
    #
    # `index` is the command whose whole job is to load or rebuild the install
    # catalog, so this is the real code path the children would otherwise each
    # take - not a reimplementation of it. Doing it here means the catalog is
    # written at most once, by one process; children then run with
    # OPENBFME_CATALOG_NO_REBUILD=1 and turn any disagreement into a hard
    # error. A wrong --install therefore fails here, loudly, once, instead of
    # becoming N processes racing to rewrite the same document - which is
    # exactly the failure a mistyped layered-install path produced by hand.
    if concurrent_children and not args.artifacts_only:
        index_cmd = [
            str(python),
            str(cli),
            "--json",
            "index",
            "--install",
            str(cook_install),
            "--game",
            args.game,
        ]
        index_env = os.environ.copy()
        index_env["OPENBFME_IMPORT_ROOT"] = str(state_root)
        index_env["PYTHONPATH"] = str(ROOT / "importer")
        print("RUN", " ".join(index_cmd), flush=True)
        index_proc = subprocess.run(
            index_cmd,
            cwd=str(ROOT),
            env=index_env,
            text=True,
            capture_output=True,
            check=False,
        )
        if index_proc.returncode != 0:
            tail = (index_proc.stderr or index_proc.stdout or "")[-2000:]
            print(
                "FAIL: could not resolve the install catalog before a "
                f"concurrent batch: exit={index_proc.returncode}: {tail}",
                file=sys.stderr,
            )
            return 2
        try:
            index_result = _parse_cli_json(index_proc.stdout)
        except ValueError:
            index_result = {}
        print(
            f"CATALOG_RESOLVED archives={index_result.get('archives')} "
            f"entries={index_result.get('entries')} (children may not rebuild)",
            flush=True,
        )

    def proof_one(faction: str) -> dict[str, Any]:
        coverage_path = coverage_root / f"{faction}-coverage.json"
        row: dict[str, Any] = {"faction": faction, "coverage": str(coverage_path)}
        try:
            if args.watch_coverage:
                # INSIDE the try, and after `row` exists, deliberately.
                #
                # The dispatcher has normally already seen this faction land,
                # so this usually returns at once - but not always:
                # `_await_coverage` requires the fingerprint to hold across two
                # consecutive polls, so a faction that lands within one poll
                # interval of the deadline can still time out HERE. Raised from
                # outside the try, that TimeoutError escaped `future.result()`
                # and killed `main()` outright, destroying the whole batch
                # report - including the rows of every faction that had already
                # succeeded. A late faction is a failed ROW, exactly like the
                # dispatcher's own timeout path.
                _await_coverage(
                    coverage_path,
                    baseline=coverage_baseline.get(faction),
                    accept_existing=args.watch_accept_existing,
                    deadline=watch_deadline,
                    poll_seconds=args.watch_poll_seconds,
                    faction=faction,
                )
            if not coverage_path.is_file():
                raise FileNotFoundError(f"missing coverage: {coverage_path}")
            coverage = _load_coverage(coverage_path)
            summary = coverage["summary"]
            assert isinstance(summary, dict)
            row["conversionComplete"] = bool(summary.get("conversionComplete"))
            row["converterGapCount"] = int(summary.get("converterGapCount") or 0)
            row["convertedCount"] = int(summary.get("convertedCount") or 0)
            row["coverageAggregateSha256"] = str(coverage.get("aggregateSha256") or "")
            artifacts = _artifact_proof(coverage_root, faction, coverage)
            row["artifactProof"] = artifacts
            if not artifacts["ok"]:
                raise RuntimeError(
                    f"artifact proof failed missing={artifacts['missingCount']} "
                    f"sample={artifacts['missingSample'][:3]}"
                )
            if not row["conversionComplete"] or row["converterGapCount"] > 0:
                if not args.allow_incomplete:
                    raise RuntimeError(
                        "refuse pack proof on incomplete convert "
                        f"(complete={row['conversionComplete']} "
                        f"gaps={row['converterGapCount']}; pass --allow-incomplete)"
                    )
                print(
                    f"WARN {faction}: incomplete convert allowed "
                    f"(complete={row['conversionComplete']} "
                    f"gaps={row['converterGapCount']})",
                    flush=True,
                )

            if args.artifacts_only:
                receipt = {
                    "schema": RECEIPT_SCHEMA,
                    "schemaVersion": RECEIPT_SCHEMA_VERSION,
                    "game": args.game,
                    "faction": faction,
                    "mode": "artifacts-only",
                    "publicationReady": False,
                    "blockingReason": (
                        "artifact proof passed; pack cook/audit not run "
                        "(pass without --artifacts-only)"
                    ),
                    "coveragePath": str(coverage_path),
                    "coverageAggregateSha256": row["coverageAggregateSha256"],
                    "artifactProof": artifacts,
                    "runId": run_id,
                }
                receipt_path = coverage_root / f"{faction}-publication.json"
                write_json_atomic(receipt_path, receipt)
                row["receipt"] = str(receipt_path)
                row["publicationReady"] = False
                row["status"] = "artifacts-ok"
                print(
                    f"ARTIFACTS_OK {faction} converted={artifacts['convertedCount']}",
                    flush=True,
                )
            else:
                profile_output = (
                    workspace
                    / "profiles"
                    / f"faction-slice-{faction}.generated.json"
                )
                # --json is a global flag (before the subcommand).
                cmd = [
                    str(python),
                    str(cli),
                    "--json",
                    "publish-faction-to-slice",
                    "--install",
                    str(cook_install),
                    "--game",
                    args.game,
                    "--faction",
                    faction,
                    "--coverage-root",
                    str(coverage_root),
                    "--base-profile",
                    str(base_profile),
                    "--profile-output",
                    str(profile_output),
                ]
                # Coverage residual gaps (allowed above) must not waive pack-build
                # resource failures. Only --allow-incomplete-pack forwards the
                # pipeline incomplete flag to publish-faction-to-slice.
                cmd.extend(
                    _pack_incomplete_args(
                        allow_incomplete_pack=args.allow_incomplete_pack
                    )
                )
                if args.allow_incomplete_coverage:
                    cmd.append("--allow-incomplete-coverage")
                if args.allow_stale_coverage:
                    cmd.append("--allow-stale-coverage")
                if concurrent_children:
                    # Divide the box instead of oversubscribing it. Each child
                    # otherwise sizes its own converter pool from the FULL core
                    # count, so seven children ask for seven full machines;
                    # this host has run out of Windows process handles under
                    # exactly that kind of parallel load before.
                    cmd.extend(["--conversion-jobs", str(child_conversion_jobs)])
                if args.allow_fewer_playable_units:
                    cmd.append("--allow-fewer-playable-units")
                if args.single_build:
                    cmd.append("--single-build")
                if not args.publish:
                    cmd.append("--no-publish")
                if args.select:
                    cmd.append("--select")
                print("RUN", " ".join(cmd), flush=True)
                env = os.environ.copy()
                env["OPENBFME_IMPORT_ROOT"] = str(state_root)
                env["PYTHONPATH"] = str(ROOT / "importer")
                if concurrent_children:
                    # The parent already resolved and verified the catalog.
                    # A child that still wants to rebuild it is telling us the
                    # parent's catalog does not describe this install, and that
                    # is a stop, not something to race N ways.
                    env["OPENBFME_CATALOG_NO_REBUILD"] = "1"
                    # Publishes are short and land in distinct digest
                    # directories; they only ever contend for the content
                    # root's lock. Queue for it, bounded - past the budget the
                    # refusal is the same refusal.
                    env["OPENBFME_SELECTION_LOCK_WAIT_SECONDS"] = str(
                        args.publish_lock_wait_seconds
                    )
                    # Same division for the pack hashing pool.
                    env["OPENBFME_HASH_WORKERS"] = str(child_hash_workers)
                started = time.monotonic()
                proc = subprocess.run(
                    cmd,
                    cwd=str(ROOT),
                    env=env,
                    text=True,
                    capture_output=True,
                    check=False,
                )
                row["seconds"] = round(time.monotonic() - started, 1)
                # One log file per child, named for the faction, so concurrent
                # children never interleave into one stream and a failure can
                # be read in full rather than from a 2000-character tail.
                log_path = child_log_dir / f"{faction}.log"
                try:
                    child_log_dir.mkdir(parents=True, exist_ok=True)
                    log_path.write_text(
                        f"$ {' '.join(cmd)}\n\n--- stdout ---\n{proc.stdout or ''}"
                        f"\n--- stderr ---\n{proc.stderr or ''}\n",
                        encoding="utf-8",
                    )
                    row["log"] = str(log_path)
                except OSError as log_error:
                    print(
                        f"WARN {faction}: could not write child log {log_path}: "
                        f"{log_error}",
                        file=sys.stderr,
                    )
                if proc.returncode != 0:
                    tail = (proc.stderr or proc.stdout or "")[-2000:]
                    raise RuntimeError(
                        f"publish-faction-to-slice exit={proc.returncode} "
                        f"(full log: {log_path}): {tail}"
                    )
                cli_result = _parse_cli_json(proc.stdout)
                valid = bool(cli_result.get("valid"))
                conversion_failures = cli_result.get("conversion_failures")
                # None means the pack has no provenance/manifest yet; treat as
                # unknown and fail closed. Zero is the only success path.
                if conversion_failures is None or int(conversion_failures) > 0:
                    raise RuntimeError(
                        "pack cook reported conversion_failures="
                        f"{conversion_failures!r} (coverage residual gaps must "
                        "not publish packs with pack-build resource failures)"
                    )
                publication_ready = valid and bool(
                    cli_result.get("pack") or cli_result.get("bundle_sha256")
                )
                receipt = {
                    "schema": RECEIPT_SCHEMA,
                    "schemaVersion": RECEIPT_SCHEMA_VERSION,
                    "game": args.game,
                    "faction": faction,
                    "mode": "pack-cook-audit",
                    "publicationReady": publication_ready,
                    "blockingReason": (
                        None
                        if publication_ready
                        else "pack audit did not report valid pack receipt"
                    ),
                    "coveragePath": str(coverage_path),
                    "coverageAggregateSha256": row["coverageAggregateSha256"],
                    "artifactProof": artifacts,
                    "pack": cli_result.get("pack"),
                    "profile": cli_result.get("profile"),
                    "composeReceipt": cli_result.get("receipt"),
                    "bundleSha256": cli_result.get("bundle_sha256"),
                    "auditValid": valid,
                    "conversionFailures": conversion_failures,
                    "composedObjects": cli_result.get("composed_objects"),
                    "publishedPack": cli_result.get("published_pack"),
                    "selectionTouched": bool(args.select),
                    "singleBuild": bool(args.single_build),
                    "bundleDigestSource": cli_result.get("bundle_digest_source"),
                    "runId": run_id,
                }
                receipt_path = coverage_root / f"{faction}-publication.json"
                write_json_atomic(receipt_path, receipt)
                row["receipt"] = str(receipt_path)
                row["publicationReady"] = publication_ready
                row["pack"] = cli_result.get("pack")
                row["bundleSha256"] = cli_result.get("bundle_sha256")
                row["publishedPack"] = cli_result.get("published_pack")
                row["conversionFailures"] = conversion_failures
                row["status"] = "publication-ready" if publication_ready else "pack-invalid"
                if not publication_ready:
                    raise RuntimeError("pack audit invalid / missing pack receipt")
                print(
                    f"PUBLICATION_READY {faction} pack={cli_result.get('pack')} "
                    f"bundle={cli_result.get('bundle_sha256')} "
                    f"published={cli_result.get('published_pack')}",
                    flush=True,
                )
        except Exception as exc:
            row["status"] = "failed"
            row["publicationReady"] = False
            row["error"] = f"{type(exc).__name__}: {exc}"[:1200]
            print(f"FAIL {faction}: {exc}", file=sys.stderr)
        return row

    # DISPATCH. `--publish-jobs 1` is the historical path, unchanged: one
    # faction at a time, in the order given. Above 1 the per-faction publishes
    # run as concurrent CHILD PROCESSES - safe because each faction cooks its
    # own pack id and lands in its own immutable digest directory, so no two
    # children ever write the same output path.
    #
    # Everything shared is handled explicitly, not hoped about:
    #   * the catalog is resolved and verified ONCE, above, by the parent, and
    #     children run with OPENBFME_CATALOG_NO_REBUILD=1 so a child that
    #     disagrees is a hard error instead of an Nth racing rewrite;
    #   * children queue for the content root's publish lock within a bounded
    #     budget instead of dying on contention (the refusal is unchanged once
    #     the budget is spent);
    #   * every receipt and THIS BATCH REPORT are written by the parent only,
    #     from returned rows. No child writes a shared document.
    if args.watch_coverage:
        workers = max(1, min(args.publish_jobs, len(factions)))
        print(
            f"PACK_PROOF_WATCH jobs={workers} factions={len(factions)} "
            f"timeout={args.watch_timeout_seconds:g}s "
            f"acceptExisting={bool(args.watch_accept_existing)} "
            f"logs={child_log_dir}",
            flush=True,
        )
        # DYNAMIC DISPATCH, because a fixed submission order would deadlock the
        # whole point of this mode: with N workers and seven waiting factions,
        # the first N submissions would each sit blocked on THEIR coverage
        # while some other faction's coverage landed first and no worker was
        # free to take it. So the parent watches, and hands a faction to the
        # pool only once that faction is actually ready.
        pending = list(factions)
        futures: dict[str, Any] = {}
        started_at = time.monotonic()
        with ThreadPoolExecutor(max_workers=workers) as pool:
            while pending:
                ready = [
                    faction
                    for faction in pending
                    if args.watch_accept_existing
                    or (
                        (current := coverage_fingerprint(
                            coverage_root / f"{faction}-coverage.json"
                        ))
                        is not None
                        and current != coverage_baseline.get(faction)
                    )
                ]
                for faction in ready:
                    pending.remove(faction)
                    waited = time.monotonic() - started_at
                    print(
                        f"COVERAGE_LANDED {faction} after {waited:.1f}s "
                        f"-> dispatching publish",
                        flush=True,
                    )
                    futures[faction] = pool.submit(proof_one, faction)
                if not pending:
                    break
                if time.monotonic() >= watch_deadline:
                    # Everything still pending is a failure row, not a silent
                    # omission: the report must account for all seven.
                    for faction in pending:
                        print(
                            f"FAIL {faction}: coverage did not land before the "
                            "watch timeout",
                            file=sys.stderr,
                        )
                    break
                time.sleep(args.watch_poll_seconds)
            timed_out = list(pending)
            collected = {
                faction: collect_row(
                    faction,
                    futures[faction],
                    coverage_root / f"{faction}-coverage.json",
                )
                for faction in futures
            }
        rows = []
        for faction in factions:
            if faction in collected:
                rows.append(collected[faction])
            else:
                rows.append(
                    {
                        "faction": faction,
                        "coverage": str(coverage_root / f"{faction}-coverage.json"),
                        "status": "failed",
                        "publicationReady": False,
                        "error": (
                            "TimeoutError: coverage did not land before the "
                            f"{args.watch_timeout_seconds:g}s watch timeout"
                        ),
                    }
                )
        assert len(rows) == len(factions)
        if timed_out:
            print(f"WATCH_TIMEOUT factions={','.join(timed_out)}", file=sys.stderr)
    elif args.publish_jobs > 1 and len(factions) > 1:
        workers = min(args.publish_jobs, len(factions))
        print(
            f"PACK_PROOF_CONCURRENCY jobs={workers} factions={len(factions)} "
            f"logs={child_log_dir}",
            flush=True,
        )
        with ThreadPoolExecutor(max_workers=workers) as pool:
            # Submitted in order, collected in order, so the report row order
            # does not depend on which faction happened to finish first.
            futures = [pool.submit(proof_one, faction) for faction in factions]
            rows = [
                collect_row(
                    faction, future, coverage_root / f"{faction}-coverage.json"
                )
                for faction, future in zip(factions, futures)
            ]
    else:
        rows = [proof_one(faction) for faction in factions]
    exit_code = 3 if any(row.get("status") == "failed" for row in rows) else 0

    report = {
        "schema": "openbfme.rotwk-faction-pack-proof",
        "schemaVersion": 1,
        "game": args.game,
        "installRoot": str(cook_install),
        "operatorInstallRoot": str(operator_install),
        "coverageRoot": str(coverage_root),
        "artifactsOnly": bool(args.artifacts_only),
        "publishJobs": int(args.publish_jobs),
        "concurrentChildren": bool(concurrent_children),
        "watchCoverage": bool(args.watch_coverage),
        "watchAcceptExisting": bool(args.watch_accept_existing),
        "childLogDir": str(child_log_dir) if concurrent_children else None,
        "runId": run_id,
        "factions": rows,
        "publicationReadyCount": sum(1 for r in rows if r.get("publicationReady")),
        "factionCount": len(rows),
    }
    out = args.output
    if out is None:
        out = state_root / "reports" / f"{args.game}-faction-pack-proof.json"
    else:
        out = Path(out).expanduser().resolve()
    write_json_atomic(out, report)
    print(f"REPORT {out}")
    print(
        f"PACK_PROOF ready={report['publicationReadyCount']}/{report['factionCount']} "
        f"exit={exit_code}"
    )
    return exit_code


if __name__ == "__main__":
    raise SystemExit(main())
