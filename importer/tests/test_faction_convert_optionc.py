"""Option C: workers produce the coverage rows, the parent assembles them.

The parent is no longer the process that produced the content, so the
byte-identity that §8's design got structurally has to be *proved* here. These
tests are the proof, and they are deliberately cheap enough to run in CI: the
whole file runs against the synthetic hero-roster fixture, not retail.
"""

from __future__ import annotations

import hashlib
import importlib.util
import json
from pathlib import Path
import sys
from unittest import mock

import pytest

from openbfme_importer.faction_census_cache import graph_digest
from openbfme_importer.faction_import import (
    CONVERT_SHARD_SCHEMA,
    ShardAssemblyError,
    assemble_faction_convert_shards,
    build_faction_conversion,
    coverage_digest_payload,
)
from importer.tests.test_faction_import import (
    _fixture,
    _structure_success_patches,
    _unit_conversion_patches,
)


def _load_batch_module():
    root = Path(__file__).resolve().parents[2]
    path = root / "tools" / "rotwk_faction_convert_batch.py"
    spec = importlib.util.spec_from_file_location("_optionc_batch_probe", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules["_optionc_batch_probe"] = module
    spec.loader.exec_module(module)
    return module


def _canonical(value: object) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def _patches():
    unit_patches = _unit_conversion_patches()
    structure_patches = _structure_success_patches()
    return [
        *unit_patches,
        *structure_patches,
        mock.patch(
            "openbfme_importer.faction_import.durable_non_ini_assets_fingerprint",
            return_value="non-ini-manifest:" + "a" * 64,
        ),
    ]


def _run(tmp_path: Path, *, shards: int | None):
    """Convert the fixture serially, or as ``shards`` producing shards.

    Returns ``(document_or_shards, artifacts)`` where ``artifacts`` maps
    ``(object id, kind)`` to the canonical bytes the writer received.
    """

    documents, graph = _fixture()
    artifacts: dict[tuple[str, str], bytes] = {}

    def _writer(object_id: str, kind: str, document: object) -> None:
        artifacts[(object_id.casefold(), kind)] = _canonical(document)

    stack = _patches()
    for patch in stack:
        patch.__enter__()
    try:
        if shards is None:
            out = build_faction_conversion(
                graph,
                documents,
                Path("unused-effective-root"),
                catalog_identity_sha256="2" * 64,
                state_root=tmp_path / "serial",
                convert_jobs=1,
                artifact_writer=_writer,
            )
        else:
            batch = _load_batch_module()
            out = [
                build_faction_conversion(
                    graph,
                    documents,
                    Path("unused-effective-root"),
                    catalog_identity_sha256="2" * 64,
                    state_root=tmp_path / f"shard-{index}",
                    convert_jobs=1,
                    artifact_writer=_writer,
                    object_selector=batch.shard_selector(index, shards),
                    produce_shard=True,
                    shard_index=index,
                    shard_count=shards,
                )
                for index in range(shards)
            ]
    finally:
        for patch in reversed(stack):
            patch.__exit__(None, None, None)
    return out, artifacts


def _graph_sha256() -> str:
    _documents, graph = _fixture()
    return graph_digest(graph)


def _normalise(coverage: dict) -> dict:
    """Drop only the per-run timing fields; keep every content field."""

    body = json.loads(json.dumps(coverage))
    for row in body.get("objects", []):
        row.pop("convertElapsedMs", None)
    summary = body.get("summary", {})
    for key in (
        "convertLoopMs",
        "convertWorkers",
        "objectElapsedMsTotal",
        "objectElapsedMsP50",
        "objectElapsedMsP95",
        "objectsPerSecond",
        "slowestObjects",
    ):
        summary.pop(key, None)
    return body


# --------------------------------------------------------------------------
# The identity test the brief asks for: one faction both ways.
# --------------------------------------------------------------------------


@pytest.mark.parametrize("shards", [1, 2, 3, 5])
def test_pooled_option_c_is_byte_identical_to_the_serial_parent(
    tmp_path: Path, shards: int
) -> None:
    """Every artifact, the coverage document and both aggregates must match.

    This is the oracle test. The serial parent-recompute path produced the
    document; Option C only reorders results that workers produced. If those
    two ever disagree, Option C is wrong and must not ship.
    """

    serial, serial_artifacts = _run(tmp_path / "a", shards=None)
    payloads, pooled_artifacts = _run(tmp_path / "b", shards=shards)
    pooled = assemble_faction_convert_shards(
        payloads,
        faction=str(serial["target"]["faction"]),
        graph_sha256=_graph_sha256(),
        shard_count=shards,
    )

    assert pooled_artifacts == serial_artifacts
    assert set(serial_artifacts), "the fixture must actually write artifacts"
    assert _canonical(_normalise(pooled)) == _canonical(_normalise(serial))
    assert pooled["planAggregateSha256"] == serial["planAggregateSha256"]
    assert pooled["aggregateSha256"] == serial["aggregateSha256"]
    assert coverage_digest_payload(pooled) == coverage_digest_payload(serial)
    assert [row["id"] for row in pooled["objects"]] == [
        row["id"] for row in serial["objects"]
    ]


def test_assembly_does_not_depend_on_the_order_shards_arrive(tmp_path: Path) -> None:
    """A slow worker finishing last must not move a single byte."""

    payloads, _artifacts = _run(tmp_path, shards=3)
    faction = str(payloads[0]["faction"])
    forward = assemble_faction_convert_shards(
        payloads, faction=faction, graph_sha256=_graph_sha256(), shard_count=3
    )
    reversed_order = assemble_faction_convert_shards(
        list(reversed(payloads)),
        faction=faction,
        graph_sha256=_graph_sha256(),
        shard_count=3,
    )
    assert _canonical(_normalise(reversed_order)) == _canonical(_normalise(forward))
    assert reversed_order["aggregateSha256"] == forward["aggregateSha256"]


def test_assembly_is_independent_of_the_worker_count(tmp_path: Path) -> None:
    aggregates = set()
    plan_aggregates = set()
    for count in (1, 2, 3, 5):
        payloads, _artifacts = _run(tmp_path / f"n{count}", shards=count)
        document = assemble_faction_convert_shards(
            payloads,
            faction=str(payloads[0]["faction"]),
            graph_sha256=_graph_sha256(),
            shard_count=count,
        )
        aggregates.add(document["aggregateSha256"])
        plan_aggregates.add(document["planAggregateSha256"])
    assert len(aggregates) == 1, aggregates
    assert len(plan_aggregates) == 1, plan_aggregates


# --------------------------------------------------------------------------
# Refusals. Every one of these must fall through to the serial oracle.
# --------------------------------------------------------------------------


def test_assembly_refuses_a_graph_the_parent_did_not_ship(tmp_path: Path) -> None:
    """The graph digest keys both durable caches — drift is never tolerated."""

    payloads, _artifacts = _run(tmp_path, shards=2)
    with pytest.raises(ShardAssemblyError, match="graph digest"):
        assemble_faction_convert_shards(
            payloads,
            faction=str(payloads[0]["faction"]),
            graph_sha256="f" * 64,
            shard_count=2,
        )


def test_assembly_refuses_an_incomplete_shard_set(tmp_path: Path) -> None:
    payloads, _artifacts = _run(tmp_path, shards=3)
    with pytest.raises(ShardAssemblyError, match="expected 3 shards"):
        assemble_faction_convert_shards(
            payloads[:2],
            faction=str(payloads[0]["faction"]),
            graph_sha256=_graph_sha256(),
            shard_count=3,
        )


def test_assembly_refuses_a_shard_that_dropped_an_object(tmp_path: Path) -> None:
    """A truncated shard must never become a short faction coverage document."""

    payloads, _artifacts = _run(tmp_path, shards=2)
    damaged = [dict(payload) for payload in payloads]
    for payload in damaged:
        if payload["rows"]:
            payload["rows"] = payload["rows"][:-1]
            break
    with pytest.raises(ShardAssemblyError, match="do not cover the faction"):
        assemble_faction_convert_shards(
            damaged,
            faction=str(payloads[0]["faction"]),
            graph_sha256=_graph_sha256(),
            shard_count=2,
        )


def test_assembly_refuses_an_object_produced_by_two_shards(tmp_path: Path) -> None:
    payloads, _artifacts = _run(tmp_path, shards=2)
    damaged = [dict(payload) for payload in payloads]
    donor = next(payload for payload in damaged if payload["rows"])
    other = next(payload for payload in damaged if payload is not donor)
    other["rows"] = list(other["rows"]) + [donor["rows"][0]]
    with pytest.raises(ShardAssemblyError, match="produced by shards"):
        assemble_faction_convert_shards(
            damaged,
            faction=str(payloads[0]["faction"]),
            graph_sha256=_graph_sha256(),
            shard_count=2,
        )


def test_assembly_refuses_shards_that_disagree_on_the_compiler(tmp_path: Path) -> None:
    """Two workers running different importer bytes must not be merged."""

    payloads, _artifacts = _run(tmp_path, shards=2)
    damaged = [dict(payload) for payload in payloads]
    damaged[1]["compilerIdentityToken"] = "moved-under-us"
    with pytest.raises(ShardAssemblyError, match="disagree on compilerIdentityToken"):
        assemble_faction_convert_shards(
            damaged,
            faction=str(payloads[0]["faction"]),
            graph_sha256=_graph_sha256(),
            shard_count=2,
        )


def test_assembly_refuses_a_pool_that_ran_other_importer_bytes(tmp_path: Path) -> None:
    """Unanimity is not enough: the pool must agree with the PARENT.

    A source edit while a run is in flight gives every worker the same, new
    compiler identity — unanimous, and not the one the parent is assembling for.
    """

    payloads, _artifacts = _run(tmp_path, shards=2)
    token = str(payloads[0]["compilerIdentityToken"])
    faction = str(payloads[0]["faction"])
    # Unanimous and correct: accepted.
    assemble_faction_convert_shards(
        payloads,
        faction=faction,
        graph_sha256=_graph_sha256(),
        shard_count=2,
        compiler_identity_token=token,
        catalog_identity_sha256=str(payloads[0]["catalogIdentitySha256"]),
    )
    with pytest.raises(ShardAssemblyError, match="ran compiler identity"):
        assemble_faction_convert_shards(
            payloads,
            faction=faction,
            graph_sha256=_graph_sha256(),
            shard_count=2,
            compiler_identity_token="edited-mid-run",
        )
    with pytest.raises(ShardAssemblyError, match="used catalog identity"):
        assemble_faction_convert_shards(
            payloads,
            faction=faction,
            graph_sha256=_graph_sha256(),
            shard_count=2,
            catalog_identity_sha256="9" * 64,
        )


def test_assembly_refuses_a_duplicate_shard_index(tmp_path: Path) -> None:
    payloads, _artifacts = _run(tmp_path, shards=2)
    damaged = [dict(payloads[0]), dict(payloads[0])]
    with pytest.raises(ShardAssemblyError, match="duplicate or out-of-range"):
        assemble_faction_convert_shards(
            damaged,
            faction=str(payloads[0]["faction"]),
            graph_sha256=_graph_sha256(),
            shard_count=2,
        )


def test_assembly_refuses_a_foreign_faction(tmp_path: Path) -> None:
    payloads, _artifacts = _run(tmp_path, shards=2)
    with pytest.raises(ShardAssemblyError, match="produced faction"):
        assemble_faction_convert_shards(
            payloads, faction="Elves", graph_sha256=_graph_sha256(), shard_count=2
        )


def test_assembly_accepts_the_discovered_short_name(tmp_path: Path) -> None:
    """Regression: the census graph says "Men", the batch holds "men".

    The first live seven-faction attempt refused every faction on exactly this,
    and no unit test caught it because every fixture used one spelling.
    """

    payloads, _artifacts = _run(tmp_path, shards=2)
    assert str(payloads[0]["faction"]) == "Men"
    document = assemble_faction_convert_shards(
        payloads, faction="men", graph_sha256=_graph_sha256(), shard_count=2
    )
    assert document["target"]["faction"] == "Men"


def test_assembly_refuses_schema_drift(tmp_path: Path) -> None:
    payloads, _artifacts = _run(tmp_path, shards=2)
    damaged = [dict(payload) for payload in payloads]
    damaged[0]["schemaVersion"] = 99
    with pytest.raises(ShardAssemblyError, match="schema drift"):
        assemble_faction_convert_shards(
            damaged,
            faction=str(payloads[0]["faction"]),
            graph_sha256=_graph_sha256(),
            shard_count=2,
        )


def test_a_shard_payload_is_not_a_coverage_document(tmp_path: Path) -> None:
    """Nothing downstream may mistake a partial shard for publishable output."""

    payloads, _artifacts = _run(tmp_path, shards=2)
    for payload in payloads:
        assert payload["schema"] == CONVERT_SHARD_SCHEMA
        assert "summary" not in payload
        assert "aggregateSha256" not in payload


def test_produce_mode_requires_a_selector_and_shard_coordinates() -> None:
    documents, graph = _fixture()
    with pytest.raises(ValueError, match="produce_shard requires"):
        build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
            produce_shard=True,
        )


# --------------------------------------------------------------------------
# The shipped graph.
# --------------------------------------------------------------------------


def test_shipped_graph_bytes_reproduce_the_canonical_digest() -> None:
    """The ship format must BE the serialisation the cache keys are cut from."""

    batch = _load_batch_module()
    _documents, graph = _fixture()
    payload = batch._canonical_graph_bytes(graph)
    assert hashlib.sha256(payload).hexdigest() == graph_digest(graph)
    assert batch.verify_shipped_graph(payload, graph_digest(graph)) == graph


def test_a_worker_refuses_a_graph_whose_digest_moved() -> None:
    batch = _load_batch_module()
    _documents, graph = _fixture()
    payload = batch._canonical_graph_bytes(graph)
    assert batch.verify_shipped_graph(payload, "0" * 64) is None
    assert batch.verify_shipped_graph(payload + b" ", graph_digest(graph)) is None
    assert batch.verify_shipped_graph(b"not json", graph_digest(graph)) is None


def test_a_worker_refuses_a_graph_that_lost_a_type_in_transit() -> None:
    """The tuple->list round-trip trap, demonstrated against the real check."""

    batch = _load_batch_module()
    original = {"target": {"faction": "Men"}, "edges": (1, 2)}
    declared = graph_digest(original)
    # json.dumps renders the tuple exactly like a list, so the bytes digest
    # matches while the parsed object is a different Python value.
    payload = batch._canonical_graph_bytes(original)
    assert hashlib.sha256(payload).hexdigest() == declared
    received = batch.verify_shipped_graph(payload, declared)
    assert received is not None
    assert received["edges"] == [1, 2]
    # The digest is what keys the caches, and it is unmoved — which is exactly
    # why the census cache stores JSON and pins JSON stability separately.
    assert graph_digest(received) == declared


# --------------------------------------------------------------------------
# The pool itself.
# --------------------------------------------------------------------------


def test_produce_pool_drains_the_queue_across_live_workers(tmp_path: Path) -> None:
    """A queue, not a static split: one slow worker must not strand jobs."""

    stub = tmp_path / "stub_worker.py"
    stub.write_text(
        "\n".join(
            [
                "import json, os, sys, time",
                "slot = sys.argv[sys.argv.index('--produce-worker') + 1]",
                "print('WORKER_READY ' + slot, flush=True)",
                "for line in sys.stdin:",
                "    line = line.strip()",
                "    if not line:",
                "        continue",
                "    job = json.loads(line)",
                "    if job.get('kind') == 'quit':",
                "        break",
                "    if slot == '0':",
                "        time.sleep(0.25)",
                "    sys.stdout.write('RESULT ' + json.dumps("
                "{'ok': True, 'slot': slot}) + '\\n')",
                "    sys.stdout.flush()",
            ]
        ),
        encoding="utf-8",
    )
    batch = _load_batch_module()
    pool = batch.ProducePool(
        python=sys.executable,
        script=stub,
        base_command=[],
        procs=3,
        logs_root=tmp_path / "logs",
    )
    try:
        replies = pool.run_round(
            [{"kind": "produce", "faction": "men", "shard": index} for index in range(12)]
        )
    finally:
        pool.close()
    assert len(replies) == 12
    assert all(reply["ok"] for reply in replies)
    assert sorted(reply["shard"] for reply in replies) == list(range(12))
    # The deliberately slow worker must have taken strictly fewer jobs than it
    # would under a static 12/3 split.
    slow = sum(1 for reply in replies if reply["slot"] == "0")
    assert slow < 4, replies


def test_produce_order_is_largest_faction_first() -> None:
    """The publish lane's tail is the last faction, so finish the big one first."""

    batch = _load_batch_module()
    graphs = {
        "wild": {"definitions": {"objects": [{}] * 46}},
        "men": {"definitions": {"objects": [{}] * 61}},
        "dwarves": {"definitions": {"objects": [{}] * 55}},
        "elves": {"definitions": {"objects": [{}] * 55}},
        "broken": {},
    }
    assert batch.produce_faction_order(graphs) == [
        "men",
        "dwarves",
        "elves",
        "wild",
        "broken",
    ]
    # Deterministic: same input, same order, and ties broken by name not by
    # dict insertion.
    reordered = {key: graphs[key] for key in reversed(list(graphs))}
    assert batch.produce_faction_order(reordered) == batch.produce_faction_order(graphs)


def test_round_reports_each_reply_as_it_lands_not_at_the_end(tmp_path: Path) -> None:
    """Per-faction emission needs completion signalled DURING the round."""

    stub = tmp_path / "counting_worker.py"
    stub.write_text(
        "\n".join(
            [
                "import json, sys",
                "print('WORKER_READY 0', flush=True)",
                "for line in sys.stdin:",
                "    job = json.loads(line)",
                "    if job.get('kind') == 'quit':",
                "        break",
                "    sys.stdout.write('RESULT ' + json.dumps({'ok': True}) + '\\n')",
                "    sys.stdout.flush()",
            ]
        ),
        encoding="utf-8",
    )
    batch = _load_batch_module()
    pool = batch.ProducePool(
        python=sys.executable,
        script=stub,
        base_command=[],
        procs=1,
        logs_root=tmp_path / "logs",
    )
    seen: list[str] = []
    try:
        replies = pool.run_round(
            [
                {"kind": "produce", "faction": "men", "shard": 0},
                {"kind": "produce", "faction": "wild", "shard": 0},
            ],
            on_reply=lambda reply: seen.append(str(reply["faction"])),
        )
    finally:
        pool.close()
    assert seen == ["men", "wild"], seen
    assert len(replies) == 2


def test_produce_pool_reports_a_worker_that_dies(tmp_path: Path) -> None:
    stub = tmp_path / "dead_worker.py"
    stub.write_text(
        "\n".join(
            [
                "import sys",
                "print('WORKER_READY 0', flush=True)",
                "sys.stdin.readline()",
                "raise SystemExit(7)",
            ]
        ),
        encoding="utf-8",
    )
    batch = _load_batch_module()
    pool = batch.ProducePool(
        python=sys.executable,
        script=stub,
        base_command=[],
        procs=1,
        logs_root=tmp_path / "logs",
    )
    try:
        replies = pool.run_round([{"kind": "produce", "faction": "men", "shard": 0}])
    finally:
        pool.close()
    assert replies and replies[0]["ok"] is False
    assert pool.failures, "a dead worker must be counted, not silently skipped"
