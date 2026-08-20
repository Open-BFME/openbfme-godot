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
import threading
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


class ProducePool:
    """A persistent pool of worker PROCESSES fed by a job queue.

    One process per slot, alive for the whole batch, so the ~35 s catalog and
    INI-corpus load is paid once per process rather than once per unit of work.
    Jobs are handed out dynamically over a queue instead of being statically
    assigned, so a worker that draws a heavy shard does not strand the others.

    The transport is deliberately boring: one JSON job per line on the worker's
    stdin, one ``RESULT <json>`` line back on its stdout, its progress and
    diagnostics on stderr into a per-worker log file. Nothing large crosses the
    pipe — the census graph is shipped as a file plus a digest, and finished
    rows come back as files the parent reads.
    """

    def __init__(
        self,
        *,
        python: str,
        script: Path,
        base_command: list[str],
        procs: int,
        logs_root: Path,
    ) -> None:
        import subprocess

        self._procs: list[object] = []
        self._handles: list[object] = []
        self.failures: list[str] = []
        logs_root.mkdir(parents=True, exist_ok=True)
        for index in range(procs):
            log_path = logs_root / f"produce-worker{index}.log"
            handle = log_path.open("w", encoding="utf-8", errors="replace")
            command = [python, str(script), *base_command, "--produce-worker", str(index)]
            process = subprocess.Popen(
                command,
                stdin=subprocess.PIPE,
                stdout=subprocess.PIPE,
                stderr=handle,
                text=True,
                encoding="utf-8",
                errors="replace",
                bufsize=1,
            )
            self._procs.append(process)
            self._handles.append(handle)
        # Wait for every worker to finish loading before the first round, so a
        # round's wall time measures work and not startup skew.
        for index, process in enumerate(self._procs):
            while True:
                line = process.stdout.readline()
                if not line:
                    self.failures.append(f"worker {index} died during startup")
                    break
                if line.startswith("WORKER_READY"):
                    break

    def run_round(
        self,
        jobs: list[dict[str, object]],
        on_reply=None,
    ) -> list[dict[str, object]]:
        """Drain ``jobs`` across the live workers and return every reply.

        Jobs are handed out in the order given, so the caller controls priority.
        ``on_reply`` is called (outside the shared lock) as each reply lands, so
        a faction can be finished and published the moment its last shard is in
        rather than at the end of the round.
        """

        import queue as queue_module
        import threading as threading_module

        pending: "queue_module.Queue[dict[str, object]]" = queue_module.Queue()
        for job in jobs:
            pending.put(job)
        replies: list[dict[str, object]] = []
        lock = threading_module.Lock()

        def _pump(index: int, process) -> None:
            while True:
                try:
                    job = pending.get_nowait()
                except queue_module.Empty:
                    return
                reply: dict[str, object]
                try:
                    process.stdin.write(json.dumps(job) + "\n")
                    process.stdin.flush()
                    reply = {"ok": False, "reason": "worker produced no result"}
                    while True:
                        line = process.stdout.readline()
                        if not line:
                            reply = {"ok": False, "reason": "worker exited"}
                            break
                        if line.startswith("RESULT "):
                            reply = json.loads(line[len("RESULT ") :])
                            break
                except (OSError, ValueError) as exc:
                    reply = {"ok": False, "reason": f"{type(exc).__name__}: {exc}"}
                reply = {**job, **reply, "worker": index}
                with lock:
                    replies.append(reply)
                if on_reply is not None:
                    try:
                        on_reply(reply)
                    except Exception as exc:  # noqa: BLE001 — never kill a pump
                        print(
                            f"PRODUCE_ON_REPLY_FAILED worker={index} "
                            f"{type(exc).__name__}: {exc}",
                            flush=True,
                        )
                if not reply.get("ok"):
                    with lock:
                        self.failures.append(
                            f"worker {index} job {job.get('kind')} "
                            f"{job.get('faction')}#{job.get('shard')}: "
                            f"{reply.get('reason')}"
                        )
                    print(
                        f"PRODUCE_JOB_FAILED worker={index} {job.get('kind')} "
                        f"{job.get('faction')}#{job.get('shard')} "
                        f"reason={reply.get('reason')}",
                        flush=True,
                    )
                    if reply.get("reason") == "worker exited":
                        return

        threads = [
            threading_module.Thread(target=_pump, args=(index, process), daemon=True)
            for index, process in enumerate(self._procs)
        ]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
        return replies

    def close(self) -> None:
        for process in self._procs:
            try:
                process.stdin.write(json.dumps({"kind": "quit"}) + "\n")
                process.stdin.flush()
                process.stdin.close()
            except OSError:
                pass
        for process in self._procs:
            try:
                process.wait(timeout=60)
            except Exception:
                process.kill()
        for handle in self._handles:
            try:
                handle.close()
            except OSError:
                pass


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


def _canonical_graph_bytes(graph: object) -> bytes:
    """Exactly the serialisation whose digest keys the plan and object caches."""

    return json.dumps(
        graph,
        sort_keys=True,
        separators=(",", ":"),
        ensure_ascii=False,
        allow_nan=False,
    ).encode("utf-8")


def verify_shipped_graph(payload: bytes, expected_sha256: str):
    """Parse a shipped census graph only if it proves it is the shipped one.

    Returns the graph, or ``None`` when the received bytes are not the graph
    the parent declared. Two independent checks, because the failure modes are
    different: the raw bytes must digest to the declared value (transport or
    the wrong file), and the parsed object must re-canonicalise to it (a JSON
    round trip that changed a type — the tuple/list trap that would silently
    move every plan-row and object cache key).
    """

    from openbfme_importer.faction_census_cache import graph_digest

    if hashlib.sha256(payload).hexdigest() != expected_sha256:
        return None
    try:
        graph = json.loads(payload.decode("utf-8"))
    except (ValueError, UnicodeDecodeError):
        return None
    if not isinstance(graph, dict) or graph_digest(graph) != expected_sha256:
        return None
    return graph


def census_object_count(graph) -> int:
    definitions = graph.get("definitions") if isinstance(graph, dict) else None
    objects = definitions.get("objects") if isinstance(definitions, dict) else None
    return len(objects) if isinstance(objects, list) else 0


def produce_faction_order(graphs) -> list[str]:
    """Largest faction first, ties broken by name so the order is reproducible.

    The publish stage downstream runs per faction and is long, so the pipeline's
    end-to-end time is set by when the LAST faction's coverage lands. Finishing
    the biggest faction first lets its (dearest) publish overlap the remaining
    converts and leaves the cheapest publish as the tail.
    """

    return sorted(
        graphs, key=lambda name: (-census_object_count(graphs[name]), name)
    )


def _shortcircuit_probe(
    *, catalog, assets: Path, state_root: Path, faction: str, artifact_root: Path | None, game: str
) -> dict[str, object] | None:
    """Reuse a stored coverage document without paying census or the corpus.

    Census is looked up but never *built* here: a probe that computed census
    would put ~17 s per faction back on the parent's serial path, which is the
    exact cost this lane exists to remove.
    """

    from openbfme_importer.faction_coverage_cache import (
        FactionCoverageCache,
        coverage_cache_disabled,
        coverage_cache_key,
        default_coverage_cache_root,
    )
    from openbfme_importer.faction_import import (
        faction_census_graph,
        faction_coverage_components,
    )

    if coverage_cache_disabled():
        return None
    try:
        spec, graph = faction_census_graph(
            catalog,
            assets,
            faction,
            state_root=state_root,
            game=game,
            build_if_missing=False,
        )
        if graph is None:
            return None
        cache = FactionCoverageCache(default_coverage_cache_root(state_root))
        components = faction_coverage_components(
            catalog, assets, spec, graph, artifact_root=artifact_root, game=game
        )
        return cache.get(
            coverage_cache_key(components),
            components=components,
            artifact_root=artifact_root,
        )
    except (OSError, TypeError, ValueError):
        return None


def _produce_convert(
    *,
    python: str,
    script: Path,
    base_command: list[str],
    catalog,
    assets: Path,
    state_root: Path,
    game: str,
    factions: list[str],
    procs: int,
    coverage_root: Path,
    write_artifacts: bool,
    run_id: str,
) -> tuple[dict[str, dict[str, Any]], dict[str, object]]:
    """Option C: workers produce the output rows, the parent assembles them.

    The parent never re-converts and never re-plans. What it does instead is
    prove the result: it is the sole authority on the census graph digest, it
    ships that graph to the workers with the digest attached, and it refuses
    any shard set that is incomplete, duplicated, or built against a different
    graph. A refused faction is not fudged — it falls through to the ordinary
    serial pass, which is the oracle this whole lane is diffed against.
    """

    from openbfme_importer.faction_census_cache import graph_digest
    from openbfme_importer.faction_object_cache import compiler_identity_token
    from openbfme_importer.faction_import import (
        ShardAssemblyError,
        assemble_faction_convert_shards,
        faction_census_graph,
        store_faction_coverage_shortcircuit,
    )

    started = time.perf_counter()
    work_root = state_root / "reports" / "produce-shards" / run_id
    work_root.mkdir(parents=True, exist_ok=True)
    artifact_root_for = (
        (lambda key: coverage_root / key / "objects") if write_artifacts else (lambda key: None)
    )
    assembled: dict[str, dict[str, Any]] = {}
    emitted: set[str] = set()
    emit_lock = threading.Lock()
    stats: dict[str, object] = {
        "procs": procs,
        "shortCircuited": [],
        "assembled": [],
        "refused": [],
        "graphRefusals": 0,
        "order": [],
        "coverageReadyMs": {},
    }

    def _emit_coverage(faction: str, coverage: dict[str, Any]) -> None:
        """Publish one faction's coverage the moment that faction is complete.

        The publish watcher keys on ``(mtime_ns, size, aggregateSha256)`` and
        refuses anything stale or partial, so this write happens exactly once
        per faction and only after every one of its rows is in. Writing all
        seven at batch end would hide six factions behind the slowest one.
        """

        with emit_lock:
            if faction in emitted:
                return
            path = coverage_root / f"{faction}-coverage.json"
            write_json_atomic(path, coverage)
            emitted.add(faction)
            ready_ms = int((time.perf_counter() - started) * 1000)
            stats["coverageReadyMs"][faction] = ready_ms
            stat = path.stat()
            print(
                f"FACTION_COVERAGE_READY {faction} ready_ms={ready_ms} "
                f"aggregate={coverage.get('aggregateSha256')} "
                f"mtime_ns={stat.st_mtime_ns} size={stat.st_size} path={path}",
                flush=True,
            )

    # 1. Reuse whatever the aggregate short-circuit already holds. This is the
    #    everyday repeat run and it must not spawn a pool at all.
    pending: list[str] = []
    for faction in factions:
        cached = _shortcircuit_probe(
            catalog=catalog,
            assets=assets,
            state_root=state_root,
            faction=faction,
            artifact_root=artifact_root_for(faction),
            game=game,
        )
        if cached is not None:
            assembled[faction] = dict(cached)
            stats["shortCircuited"].append(faction)
            print(f"PRODUCE_SHORTCIRCUIT {faction}", flush=True)
            if write_artifacts:
                _emit_coverage(faction, assembled[faction])
        else:
            pending.append(faction)
    if not pending:
        stats["wallMs"] = int((time.perf_counter() - started) * 1000)
        print(f"PRODUCE_DONE wall_ms={stats['wallMs']} pool=skipped", flush=True)
        return assembled, stats

    logs_root = state_root / "reports" / "produce-workers"
    finish_faction = None
    pool = ProducePool(
        python=python,
        script=script,
        base_command=base_command,
        procs=procs,
        logs_root=logs_root,
    )
    try:
        # 2. Census fan-out. Seven factions over N workers instead of N workers
        #    each computing all seven.
        census_started = time.perf_counter()
        need_census = []
        graphs: dict[str, dict[str, Any]] = {}
        specs: dict[str, tuple[str, str, str]] = {}
        for faction in pending:
            spec, graph = faction_census_graph(
                catalog, assets, faction, state_root=state_root, game=game,
                build_if_missing=False,
            )
            specs[faction] = spec
            if graph is None:
                need_census.append({"kind": "census", "faction": faction})
            else:
                graphs[faction] = graph
        if need_census:
            print(f"PRODUCE_CENSUS jobs={len(need_census)}", flush=True)
            pool.run_round(need_census)
        for faction in pending:
            if faction in graphs:
                continue
            spec, graph = faction_census_graph(
                catalog, assets, faction, state_root=state_root, game=game
            )
            specs[faction] = spec
            graphs[faction] = graph
        census_ms = int((time.perf_counter() - census_started) * 1000)

        # 3. Ship the graph as BYTES with its digest. The worker recomputes the
        #    digest from what it received and refuses on any mismatch: the
        #    digest keys both the plan-row and object caches, so silent drift
        #    here would be silently wrong cache entries.
        ship: dict[str, tuple[Path, str]] = {}
        for faction, graph in graphs.items():
            payload = _canonical_graph_bytes(graph)
            digest = hashlib.sha256(payload).hexdigest()
            if digest != graph_digest(graph):
                raise RuntimeError(
                    f"{faction}: shipped graph bytes do not reproduce the "
                    "canonical graph digest"
                )
            path = work_root / f"graph-{faction}.json"
            path.write_bytes(payload)
            ship[faction] = (path, digest)

        # 4. Produce fan-out: every (faction, shard) is one queued job, ordered
        #    LARGEST FACTION FIRST. The publish stage that consumes this runs
        #    per faction and is itself long, so the batch's end-to-end time is
        #    set by when the LAST faction's coverage lands. Finishing the
        #    biggest faction first lets its publish run while the small
        #    factions are still converting, leaving the cheapest publish as the
        #    tail instead of the dearest.
        from openbfme_importer.util import read_json

        order = produce_faction_order({name: graphs[name] for name in pending})
        stats["order"] = [
            f"{name}:{census_object_count(graphs[name])}" for name in order
        ]
        print("PRODUCE_ORDER " + " ".join(stats["order"]), flush=True)

        pool_started = time.perf_counter()
        jobs: list[dict[str, object]] = []
        for faction in order:
            graph_path, digest = ship[faction]
            for index in range(procs):
                jobs.append(
                    {
                        "kind": "produce",
                        "faction": faction,
                        "shard": index,
                        "count": procs,
                        "graph": str(graph_path),
                        "graphSha256": digest,
                        "out": str(work_root / f"{faction}-{index}-of-{procs}.json"),
                        "artifacts": bool(write_artifacts),
                    }
                )
        print(
            f"PRODUCE_POOL procs={procs} factions={len(pending)} jobs={len(jobs)}",
            flush=True,
        )

        def _finish(faction: str) -> bool:
            """Assemble one faction and publish it, or refuse it out loud."""

            try:
                shards = [
                    read_json(work_root / f"{faction}-{index}-of-{procs}.json")
                    for index in range(procs)
                ]
                coverage = assemble_faction_convert_shards(
                    shards,
                    faction=specs[faction][0],
                    graph_sha256=ship[faction][1],
                    shard_count=procs,
                    catalog_identity_sha256=str(catalog.identity_sha256()),
                    compiler_identity_token=compiler_identity_token(),
                )
            except (ShardAssemblyError, OSError, ValueError, KeyError) as exc:
                stats["refused"].append(f"{faction}: {type(exc).__name__}: {exc}")
                print(
                    f"PRODUCE_REFUSED {faction}: {type(exc).__name__}: {exc} "
                    "(falling back to the serial parent pass)",
                    flush=True,
                )
                return False
            assembled[faction] = coverage
            stats["assembled"].append(faction)
            store_faction_coverage_shortcircuit(
                catalog,
                assets,
                specs[faction],
                graphs[faction],
                coverage,
                state_root=state_root,
                artifact_root=artifact_root_for(faction),
                game=game,
            )
            if write_artifacts:
                _emit_coverage(faction, coverage)
            return True

        finish_faction = _finish
        done_counts: dict[str, int] = {}
        broken: set[str] = set()
        counter_lock = threading.Lock()

        def _on_reply(reply: dict[str, object]) -> None:
            faction = str(reply.get("faction") or "")
            if reply.get("kind") != "produce" or not faction:
                return
            with counter_lock:
                if not reply.get("ok"):
                    broken.add(faction)
                    return
                done_counts[faction] = done_counts.get(faction, 0) + 1
                ready = done_counts[faction] == procs and faction not in broken
            if ready:
                # Every row for this faction is in. Assemble and publish now —
                # not at batch end — so the publish lane can start on it while
                # the remaining factions are still converting.
                _finish(faction)

        replies = pool.run_round(jobs, on_reply=_on_reply)
        pool_ms = int((time.perf_counter() - pool_started) * 1000)
        stats["graphRefusals"] = sum(
            1 for reply in replies if reply.get("reason") == "graph-digest-mismatch"
        )
    finally:
        pool.close()

    # 5. Sweep: anything the in-flight path did not finish (a failed job, a
    #    torn shard) is retried once here, and refused out loud if it still
    #    does not verify. Refused means the serial parent pass takes it.
    assemble_started = time.perf_counter()
    if finish_faction is not None:
        for faction in pending:
            if faction in assembled:
                continue
            finish_faction(faction)
    stats["censusMs"] = census_ms
    stats["poolMs"] = pool_ms
    stats["assembleMs"] = int((time.perf_counter() - assemble_started) * 1000)
    stats["wallMs"] = int((time.perf_counter() - started) * 1000)
    stats["workerFailures"] = pool.failures
    print(
        f"PRODUCE_DONE wall_ms={stats['wallMs']} census_ms={census_ms} "
        f"pool_ms={pool_ms} assemble_ms={stats['assembleMs']} "
        f"assembled={len(stats['assembled'])} refused={len(stats['refused'])} "
        f"graph_refusals={stats['graphRefusals']}",
        flush=True,
    )
    return assembled, stats


def _produce_worker(
    *, worker_id: int, catalog, assets: Path, state_root: Path, game: str,
    coverage_root: Path, convert_jobs: int | None,
) -> int:
    """Worker mode: answer census and produce jobs until told to quit."""

    from openbfme_importer.faction_census_cache import graph_digest
    from openbfme_importer.faction_import import (
        convert_faction_import,
        faction_census_graph,
    )

    def _reply(payload: dict[str, object]) -> None:
        sys.stdout.write("RESULT " + json.dumps(payload) + "\n")
        sys.stdout.flush()

    print(f"WORKER_READY {worker_id}", flush=True)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            job = json.loads(line)
        except ValueError as exc:
            _reply({"ok": False, "reason": f"unparsable job: {exc}"})
            continue
        kind = str(job.get("kind", ""))
        if kind == "quit":
            return 0
        started = time.perf_counter()
        try:
            if kind == "census":
                faction = str(job["faction"])
                _spec, graph = faction_census_graph(
                    catalog, assets, faction, state_root=state_root, game=game
                )
                _reply(
                    {
                        "ok": True,
                        "graphSha256": graph_digest(graph),
                        "wallMs": int((time.perf_counter() - started) * 1000),
                    }
                )
                continue
            if kind != "produce":
                _reply({"ok": False, "reason": f"unknown job kind {kind!r}"})
                continue
            faction = str(job["faction"])
            index = int(job["shard"])
            count = int(job["count"])
            expected = str(job["graphSha256"])
            payload = Path(str(job["graph"])).read_bytes()
            graph = verify_shipped_graph(payload, expected)
            if graph is None:
                _reply({"ok": False, "reason": "graph-digest-mismatch"})
                continue
            artifact_root = None
            artifact_writer = None
            if job.get("artifacts"):
                # Must be byte-for-byte the path the parent will hash for the
                # short-circuit artifact manifest.
                artifact_root = coverage_root / faction / "objects"

                def _write_artifact(
                    object_id: str,
                    kind_name: str,
                    document: object,
                    *,
                    _root: Path = artifact_root,
                ) -> None:
                    write_json_atomic(
                        _root / object_id.casefold() / f"{kind_name}.json", document
                    )

                artifact_writer = _write_artifact
            shard = convert_faction_import(
                catalog,
                assets,
                faction,
                artifact_writer=artifact_writer,
                state_root=state_root,
                convert_jobs=convert_jobs,
                object_selector=shard_selector(index, count),
                artifact_root=artifact_root,
                census_graph=graph,
                produce_shard=True,
                shard_index=index,
                shard_count=count,
                game=game,
            )
            write_json_atomic(Path(str(job["out"])), shard)
            _reply(
                {
                    "ok": True,
                    "rows": len(shard.get("rows") or []),
                    "wallMs": int((time.perf_counter() - started) * 1000),
                }
            )
        except Exception as exc:  # noqa: BLE001 — a failed job is the parent's problem
            _reply(
                {
                    "ok": False,
                    "reason": f"{type(exc).__name__}: {exc}",
                    "traceback": traceback.format_exc()[-2000:],
                }
            )
    return 0


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
        "--produce-procs",
        type=int,
        default=0,
        help=(
            "Option C: run N persistent worker processes that PRODUCE the "
            "coverage rows and artifacts; the parent assembles them and never "
            "re-plans or re-converts. 0 (default) keeps the serial "
            "parent-recompute pass, which stays available as the oracle."
        ),
    )
    parser.add_argument(
        "--produce-worker",
        type=int,
        default=None,
        help="internal: Option C worker slot; reads jobs on stdin.",
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

    if args.produce_worker is not None:
        return _produce_worker(
            worker_id=int(args.produce_worker),
            catalog=catalog,
            assets=assets,
            state_root=state_root,
            game=args.game,
            coverage_root=faction_import_report_root(state_root, args.game),
            convert_jobs=args.convert_jobs,
        )

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

        produced: dict[str, dict[str, Any]] = {}
        produce_stats: dict[str, object] = {}
        if args.produce_procs and args.produce_procs > 1:
            if assets.name.casefold() == "layered-effective-assets":
                # The layered branch rewrites the graph after census, so the
                # digest the parent ships is not the digest the workers key on.
                # Refuse rather than ship a mismatch every worker would reject.
                print(
                    "FAIL: --produce-procs does not support the layered "
                    "effective-assets tree",
                    file=sys.stderr,
                )
                return 2
            produced, produce_stats = _produce_convert(
                python=sys.executable,
                script=Path(__file__).resolve(),
                base_command=[
                    "--install", str(operator_install),
                    "--game", args.game,
                    "--state-root", str(state_root),
                    "--assets-root", str(assets),
                ]
                + ([] if args.convert_jobs is None else ["--convert-jobs", str(args.convert_jobs)])
                + ([] if write_artifacts else ["--no-write-artifacts"]),
                catalog=catalog,
                assets=assets,
                state_root=state_root,
                game=args.game,
                factions=[
                    resolve_playable_faction(catalog, faction).short_name
                    for faction in factions
                ],
                procs=int(args.produce_procs),
                coverage_root=coverage_root,
                write_artifacts=write_artifacts,
                run_id=run_id,
            )
            warm_pool = {"produce": produce_stats}

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
                result = produced.get(key)
                if result is not None:
                    # Say which of the two it actually was. A short-circuited
                    # faction never went near a worker, and a log that calls
                    # both "assembled" hides which path was measured.
                    origin = (
                        "short-circuit reuse"
                        if key in (produce_stats.get("shortCircuited") or [])
                        else "workers produced, parent assembled"
                    )
                    print(f"ASSEMBLED {key} ({origin})", flush=True)
                else:
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
                    if faction in (produce_stats.get("coverageReadyMs") or {}):
                        # Already published the moment this faction completed.
                        # Rewriting identical bytes would only bump mtime_ns
                        # and make the publish watcher re-see a done faction.
                        print(
                            f"COVERAGE {faction} -> {coverage_path} "
                            "(already emitted at faction completion)",
                            flush=True,
                        )
                    else:
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
        "produceProcs": int(args.produce_procs or 0),
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
