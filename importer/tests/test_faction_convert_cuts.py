"""§14 cuts: the descriptor memo, the corpus-digest memo, faction discovery.

The load-bearing test here is the identity one. A memo that returns anything
other than exactly what the second compile would have produced is a correctness
bug wearing a performance costume, so every cut is proved output-identical
before it is allowed to be fast.
"""

from __future__ import annotations

import json
from pathlib import Path
from unittest import mock

import pytest

from openbfme_importer import faction_census
from openbfme_importer import faction_import as fi
from openbfme_importer import faction_object_cache as foc
from openbfme_importer.faction_import import build_faction_conversion
from importer.tests.test_faction_import import (
    _fixture,
    _structure_success_patches,
    _unit_conversion_patches,
)


def _canonical(value: object) -> bytes:
    return json.dumps(
        value, sort_keys=True, separators=(",", ":"), ensure_ascii=False
    ).encode("utf-8")


def _patches():
    return [
        *_unit_conversion_patches(),
        *_structure_success_patches(),
        mock.patch(
            "openbfme_importer.faction_import.durable_non_ini_assets_fingerprint",
            return_value="non-ini-manifest:" + "a" * 64,
        ),
    ]


def _convert(tmp_path: Path):
    documents, graph = _fixture()
    artifacts: dict[tuple[str, str], bytes] = {}

    def _writer(object_id: str, kind: str, document: object) -> None:
        artifacts[(object_id.casefold(), kind)] = _canonical(document)

    stack = _patches()
    for patch in stack:
        patch.__enter__()
    try:
        coverage = build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
            state_root=tmp_path,
            convert_jobs=1,
            artifact_writer=_writer,
        )
    finally:
        for patch in reversed(stack):
            patch.__exit__(None, None, None)
    return coverage, artifacts


def _normalise(coverage: dict) -> dict:
    body = json.loads(json.dumps(coverage))
    for row in body.get("objects", []):
        row.pop("convertElapsedMs", None)
    for key in (
        "convertLoopMs",
        "convertWorkers",
        "objectElapsedMsTotal",
        "objectElapsedMsP50",
        "objectElapsedMsP95",
        "objectsPerSecond",
        "slowestObjects",
    ):
        body.get("summary", {}).pop(key, None)
    return body


# --------------------------------------------------------------------------
# Cut 1 — the plan/draft descriptor memo
# --------------------------------------------------------------------------


def test_descriptor_memo_output_is_byte_identical_to_recompiling(
    tmp_path: Path, monkeypatch
) -> None:
    """The whole point: the memo returns what the second compile would have.

    Same fixture, once with the memo and once with it switched off. Every
    artifact byte-for-byte, the coverage document field-for-field, and both
    aggregates.
    """

    monkeypatch.setenv("OPENBFME_NO_DESCRIPTOR_MEMO", "1")
    fi.clear_descriptor_memo()
    without, artifacts_without = _convert(tmp_path / "off")

    monkeypatch.delenv("OPENBFME_NO_DESCRIPTOR_MEMO", raising=False)
    fi.clear_descriptor_memo()
    with_memo, artifacts_with = _convert(tmp_path / "on")

    assert artifacts_with == artifacts_without
    assert set(artifacts_with), "the fixture must actually write artifacts"
    assert _canonical(_normalise(with_memo)) == _canonical(_normalise(without))
    assert with_memo["aggregateSha256"] == without["aggregateSha256"]
    assert with_memo["planAggregateSha256"] == without["planAggregateSha256"]


def _counted_convert(tmp_path: Path) -> dict[str, int]:
    """Convert the fixture, counting EVERY unit descriptor compile.

    Both the call sites in ``faction_import`` and the compiler module's own
    symbol are wrapped, so a horde re-entering its member's compile inside the
    compiler is counted too — that is the §14.5 question.
    """

    counts: dict[str, int] = {}
    import openbfme_importer.playable_unit_compiler as puc

    real = fi.compile_playable_unit_descriptor

    def _counting(object_id, documents, **kwargs):
        counts[str(object_id).casefold()] = counts.get(str(object_id).casefold(), 0) + 1
        return real(object_id, documents, **kwargs)

    with (
        mock.patch.object(fi, "compile_playable_unit_descriptor", _counting),
        mock.patch.object(puc, "compile_playable_unit_descriptor", _counting),
    ):
        _convert(tmp_path)
    return counts


def test_descriptor_memo_removes_the_duplicate_unit_compile(
    tmp_path: Path, monkeypatch
) -> None:
    """§14.5's call counter, and it proves the horde question either way."""

    monkeypatch.setenv("OPENBFME_NO_DESCRIPTOR_MEMO", "1")
    fi.clear_descriptor_memo()
    before = _counted_convert(tmp_path / "off")

    monkeypatch.delenv("OPENBFME_NO_DESCRIPTOR_MEMO", raising=False)
    fi.clear_descriptor_memo()
    after = _counted_convert(tmp_path / "on")

    assert before, "the fixture must compile at least one unit descriptor"
    assert set(after) == set(before)
    # Strictly fewer compiles, and never more for any single object.
    assert sum(after.values()) < sum(before.values()), (before, after)
    for object_id, count in after.items():
        assert count <= before[object_id], (object_id, before, after)
    # Every object that was compiled at least twice loses exactly one compile:
    # the plan/draft pair collapses, the final compile stays.
    for object_id, count in before.items():
        if count >= 2:
            assert after[object_id] == count - 1, (object_id, before, after)


def test_structure_memo_key_carries_its_policy_arguments() -> None:
    """The structure call sites pass DIFFERENT locals; the key must notice.

    The plan passes graph-derived engine roots, the convert passes
    policy-derived ones. They are frequently equal and sometimes not, so the
    key carries the values and a mismatch simply misses.
    """

    base = fi._structure_descriptor_key(
        "GondorBarracks", "rotwk", ("A",), {"a": "spawned"}, ("W",), ("S",)
    )
    assert base == fi._structure_descriptor_key(
        "gondorbarracks", "rotwk", ("A",), {"a": "spawned"}, ("W",), ("S",)
    )
    for changed in (
        fi._structure_descriptor_key("GondorBarracks", "rotwk", ("B",), {"a": "spawned"}, ("W",), ("S",)),
        fi._structure_descriptor_key("GondorBarracks", "rotwk", ("A",), {"a": "other"}, ("W",), ("S",)),
        fi._structure_descriptor_key("GondorBarracks", "rotwk", ("A",), {"a": "spawned"}, ("X",), ("S",)),
        fi._structure_descriptor_key("GondorBarracks", "rotwk", ("A",), {"a": "spawned"}, ("W",), ("T",)),
        fi._structure_descriptor_key("GondorBarracks", "bfme2", ("A",), {"a": "spawned"}, ("W",), ("S",)),
    ):
        assert changed != base


def test_memo_entry_is_consumed_so_nothing_aliases_a_descriptor() -> None:
    owner = ({}, object(), {})
    key = ("unit", "x", "rotwk", False)
    calls = []

    def _compute():
        calls.append(1)
        return {"descriptorSha256": "a" * 64}

    fi.clear_descriptor_memo()
    fi._memoized_descriptor(owner=owner, key=key, compute=_compute, publish=True)
    assert len(calls) == 1
    fi._memoized_descriptor(owner=owner, key=key, compute=_compute, publish=False)
    assert len(calls) == 1, "the published entry must satisfy the consumer"
    fi._memoized_descriptor(owner=owner, key=key, compute=_compute, publish=False)
    assert len(calls) == 2, "an entry is consumed once, not shared forever"


def test_memo_is_dropped_when_the_corpus_or_graph_identity_moves() -> None:
    documents, prepared, graph = {}, object(), {}
    key = ("unit", "x", "rotwk", False)
    calls = []

    def _compute():
        calls.append(1)
        return {"v": len(calls)}

    fi.clear_descriptor_memo()
    fi._memoized_descriptor(
        owner=(documents, prepared, graph), key=key, compute=_compute, publish=True
    )
    # A different corpus object is a different world; the entry must not carry.
    fi._memoized_descriptor(
        owner=({}, prepared, graph), key=key, compute=_compute, publish=False
    )
    assert len(calls) == 2


def test_descriptor_memo_is_disabled_by_environment(monkeypatch) -> None:
    monkeypatch.setenv("OPENBFME_NO_DESCRIPTOR_MEMO", "1")
    assert fi.descriptor_memo_disabled() is True
    owner = ({}, object(), {})
    key = ("unit", "x", "rotwk", False)
    calls = []
    fi.clear_descriptor_memo()
    fi._memoized_descriptor(
        owner=owner, key=key, compute=lambda: calls.append(1), publish=True
    )
    fi._memoized_descriptor(
        owner=owner, key=key, compute=lambda: calls.append(1), publish=False
    )
    assert len(calls) == 2, "disabled means every call compiles"


# --------------------------------------------------------------------------
# Cut 2 — corpus digests and the durable fingerprints
# --------------------------------------------------------------------------


def test_corpus_digests_are_memoized_per_corpus_object() -> None:
    documents = {"data/ini/a.ini": b"one", "data/ini/b.ini": b"two"}
    fi.clear_corpus_digest_memo()
    hashes_a, closure_a = fi._corpus_digests(documents)
    hashes_b, closure_b = fi._corpus_digests(documents)
    assert hashes_b is hashes_a and closure_b is closure_a
    # A different corpus OBJECT recomputes, and different bytes give a
    # different closure — the memo can never serve one corpus for another.
    other = {"data/ini/a.ini": b"changed", "data/ini/b.ini": b"two"}
    hashes_c, closure_c = fi._corpus_digests(other)
    assert hashes_c != hashes_a
    assert closure_c["sha256"] != closure_a["sha256"]
    # Equal-but-distinct object: recomputed, and equal in value.
    hashes_d, closure_d = fi._corpus_digests(dict(documents))
    assert hashes_d == hashes_a
    assert closure_d["sha256"] == closure_a["sha256"]


def _write_manifest(root: Path, rows: list[dict], aggregate: str) -> None:
    (root / ".openbfme").mkdir(parents=True, exist_ok=True)
    (root / ".openbfme" / "manifest.json").write_text(
        json.dumps({"aggregate_sha256": aggregate, "files": rows}), encoding="utf-8"
    )


def test_durable_fingerprints_memoize_but_follow_the_manifest(tmp_path: Path) -> None:
    root = tmp_path / "tree"
    root.mkdir()
    rows = [
        {"path": "data/ini/object.ini", "size": 3, "sha256": "a" * 64},
        {"path": "art/w3d/thing.w3d", "size": 4, "sha256": "b" * 64},
    ]
    _write_manifest(root, rows, "c" * 64)
    foc.clear_fingerprint_memo()
    first_effective = foc.durable_effective_assets_fingerprint(root)
    first_non_ini = foc.durable_non_ini_assets_fingerprint(root)
    assert foc.durable_effective_assets_fingerprint(root) == first_effective
    assert foc.durable_non_ini_assets_fingerprint(root) == first_non_ini

    # A changed manifest must change both, memo or no memo.
    rows[1]["sha256"] = "d" * 64
    _write_manifest(root, rows, "e" * 64)
    assert foc.durable_effective_assets_fingerprint(root) != first_effective
    assert foc.durable_non_ini_assets_fingerprint(root) != first_non_ini


def test_fingerprint_memo_is_skipped_without_a_manifest(tmp_path: Path) -> None:
    """No manifest means the tree walk decides, and that must not be memoized."""

    root = tmp_path / "bare"
    root.mkdir()
    (root / "a.txt").write_bytes(b"one")
    foc.clear_fingerprint_memo()
    first = foc.durable_effective_assets_fingerprint(root)
    (root / "b.txt").write_bytes(b"two")
    assert foc.durable_effective_assets_fingerprint(root) != first


# --------------------------------------------------------------------------
# Faction discovery memo (measured at 4.3 s per call on the real catalog)
# --------------------------------------------------------------------------


def _load_batch_module():
    import importlib.util
    import sys

    path = Path(__file__).resolve().parents[2] / "tools" / "rotwk_faction_convert_batch.py"
    spec = importlib.util.spec_from_file_location("_cuts_batch_probe", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules["_cuts_batch_probe"] = module
    spec.loader.exec_module(module)
    return module


# --------------------------------------------------------------------------
# Cut 0 — cost-predicted sharding
# --------------------------------------------------------------------------


def test_balanced_sharding_isolates_the_expensive_object() -> None:
    """The measured pathology: one spellbook set the faction's finish time."""

    batch = _load_batch_module()
    ids = [f"GondorUnit{i}" for i in range(60)] + ["MenSpellBook"]
    costs = {f"gondorunit{i}": 8000 for i in range(60)}
    costs["menspellbook"] = 115000
    assignment = batch.balanced_shard_assignment(ids, costs, 24)
    assert len(assignment) == 61
    loads = [0] * 24
    for object_id, shard in assignment.items():
        loads[shard] += costs[object_id]
    # Hash sharding gave men a 115.2 s shard against a 53 s balanced ideal.
    # Balanced, the spellbook's shard carries the spellbook and little else.
    spellbook_shard = assignment["menspellbook"]
    assert loads[spellbook_shard] == 115000, loads
    assert max(loads) == 115000
    assert sum(loads) == 60 * 8000 + 115000


def test_balanced_sharding_covers_every_id_exactly_once() -> None:
    batch = _load_batch_module()
    ids = [f"Obj{i}" for i in range(37)]
    costs = {f"obj{i}": (i * 137) % 900 for i in range(37)}
    for count in (1, 3, 8, 24):
        assignment = batch.balanced_shard_assignment(ids, costs, count)
        selectors = [
            batch.assigned_shard_selector(i, count, assignment) for i in range(count)
        ]
        for object_id in ids:
            owners = [i for i, sel in enumerate(selectors) if sel(object_id)]
            assert owners == [assignment[object_id.casefold()]], (object_id, owners)


def test_unassigned_ids_still_fall_to_the_hash_exactly_once() -> None:
    """The compiler adds banner-carrier ids the parent never saw."""

    batch = _load_batch_module()
    assignment = {"known": 0}
    for count in (2, 5, 16):
        selectors = [
            batch.assigned_shard_selector(i, count, assignment) for i in range(count)
        ]
        for surprise in ("RohanBanner", "GondorArcherBanner", "LateAddition"):
            owners = [i for i, sel in enumerate(selectors) if sel(surprise)]
            assert len(owners) == 1, (surprise, owners)
        known = [i for i, sel in enumerate(selectors) if sel("Known")]
        assert known == [0]


def test_missing_costs_get_the_median_not_zero() -> None:
    batch = _load_batch_module()
    ids = ["Cheap", "Dear", "Unknown"]
    costs = {"cheap": 1000, "dear": 100000}
    assignment = batch.balanced_shard_assignment(ids, costs, 2)
    # Median of {1000, 100000} is 100000 here, so Unknown is treated as costly
    # and must not be stacked onto the expensive shard.
    assert assignment["dear"] != assignment["unknown"]


def test_no_prior_costs_means_no_assignment_and_a_pure_hash() -> None:
    batch = _load_batch_module()
    assert batch.balanced_shard_assignment(["A", "B"], {}, 4) == {}


def test_cost_table_reads_a_real_coverage_document(tmp_path: Path) -> None:
    batch = _load_batch_module()
    path = tmp_path / "men-coverage.json"
    path.write_text(
        json.dumps(
            {
                "objects": [
                    {"id": "MenSpellBook", "convertElapsedMs": 115071},
                    {"id": "GondorArcher", "convertElapsedMs": 53270},
                    {"id": "Broken"},
                    "not-a-row",
                ]
            }
        ),
        encoding="utf-8",
    )
    assert batch.object_cost_table(path) == {
        "menspellbook": 115071,
        "gondorarcher": 53270,
    }
    assert batch.object_cost_table(tmp_path / "absent.json") == {}
    bad = tmp_path / "bad.json"
    bad.write_text("{not json", encoding="utf-8")
    assert batch.object_cost_table(bad) == {}


# --------------------------------------------------------------------------
# Cut 3 — the ledger sink handle
# --------------------------------------------------------------------------


def test_ledger_keeps_one_handle_and_still_flushes_every_event(tmp_path: Path) -> None:
    """Streaming must not cost durability: each event is readable immediately."""

    from openbfme_importer.conversion_ledger import ConversionLedger

    sink = tmp_path / "ledger.jsonl"
    opened = []
    real_open = Path.open

    def _counting_open(self, *args, **kwargs):
        mode = str(kwargs.get("mode", args[0] if args else "r"))
        # Count only the append handle; the assertions below read the file.
        if self == sink and mode.startswith("a"):
            opened.append(1)
        return real_open(self, *args, **kwargs)

    ledger = ConversionLedger(
        run_id="r", game="rotwk", install_root="x", sink_path=sink
    )
    with mock.patch.object(Path, "open", _counting_open):
        for index in range(5):
            ledger.record(
                kind="faction-object",
                unit_id=f"men:Obj{index}",
                status="converted",
                log_to_stderr=False,
            )
            # Readable between events, not just at the end.
            assert len(sink.read_text(encoding="utf-8").splitlines()) == index + 1
    assert opened == [1], "the sink must be opened once, not once per event"
    ledger.close_sink()
    lines = [json.loads(line) for line in sink.read_text(encoding="utf-8").splitlines()]
    assert [row["unitId"] for row in lines] == [f"men:Obj{i}" for i in range(5)]


def test_ledger_survives_a_sink_that_cannot_be_written(tmp_path: Path) -> None:
    """Telemetry is fail-open; a broken sink must never abort a convert."""

    from openbfme_importer.conversion_ledger import ConversionLedger

    ledger = ConversionLedger(
        run_id="r", game="rotwk", install_root="x", sink_path=tmp_path / "l.jsonl"
    )
    with mock.patch.object(Path, "open", side_effect=OSError("nope")):
        ledger.record(kind="faction", unit_id="men", status="converted", log_to_stderr=False)
    assert ledger.summary()["sinkErrorCount"] >= 1
    # And it recovers once the sink works again.
    ledger.record(kind="faction", unit_id="wild", status="converted", log_to_stderr=False)
    assert (tmp_path / "l.jsonl").is_file()


def test_worker_logs_are_run_scoped(tmp_path: Path) -> None:
    """§14.6: an unscoped log path let one run overwrite another's evidence."""

    import inspect

    batch = _load_batch_module()
    source = inspect.getsource(batch._produce_convert)
    assert '"produce-workers" / run_id' in source


def test_faction_discovery_is_computed_once_per_catalog_object() -> None:
    calls = []

    def _discover(catalog):
        calls.append(catalog)
        return ("row",)

    faction_census.clear_playable_faction_discovery_memo()
    with mock.patch.object(faction_census, "_discover_playable_factions", _discover):
        catalog = object()
        assert faction_census.discover_playable_factions(catalog) == ("row",)
        assert faction_census.discover_playable_factions(catalog) == ("row",)
        assert len(calls) == 1
        # A different catalog is a different answer and must be recomputed.
        assert faction_census.discover_playable_factions(object()) == ("row",)
        assert len(calls) == 2
    faction_census.clear_playable_faction_discovery_memo()


def test_faction_discovery_memo_holds_the_catalog_so_ids_cannot_recycle() -> None:
    """The memo keys on id(); a strong reference is what makes that sound."""

    calls = []

    def _discover(catalog):
        calls.append(catalog)
        return (len(calls),)

    faction_census.clear_playable_faction_discovery_memo()
    with mock.patch.object(faction_census, "_discover_playable_factions", _discover):
        first = faction_census.discover_playable_factions(object())
        # Drop every local reference and churn allocations; if the memo did not
        # hold the catalog, a recycled id could serve `first` to a new object.
        for _ in range(200):
            probe = object()
            got = faction_census.discover_playable_factions(probe)
            assert got == (len(calls),), "a recycled id served a stale entry"
            del probe
        assert first == (1,)
    faction_census.clear_playable_faction_discovery_memo()
