"""Guards for the faction-convert wall-clock optimizations.

Every test here is a correctness guard on a change made for speed: the
optimized path must return exactly what the unoptimized path returned.
"""

from __future__ import annotations

import json
import threading
from pathlib import Path

from openbfme_importer import incremental_rebuild
from openbfme_importer.faction_import import (
    _convert_one_plan_object,
    build_faction_import_plan,
    resolve_convert_worker_count,
    resolve_plan_worker_count,
)
from openbfme_importer.faction_object_cache import clear_compiler_identity_token_memo
from openbfme_importer.incremental_rebuild import (
    compiler_dependency_identity,
    document_closure_identity,
)
from openbfme_importer.playable_unit_compiler import (
    clear_prepared_playable_unit_compiler_memo,
    prepare_playable_unit_compiler,
)

from importer.tests.test_playable_unit_compiler import _hero_roster_fixture


def _fixture() -> tuple[dict[str, bytes], dict[str, object]]:
    documents, graph = _hero_roster_fixture()
    graph["target"]["faction"] = "Men"
    graph["summary"] = {"unresolvedCount": 0}
    graph["inputSetSha256"] = "1" * 64
    return documents, graph


def test_prepared_compiler_inputs_are_shared_for_one_document_view() -> None:
    """The batch must parse one document view exactly once — but only one.

    Compilers fail closed when ``prepared.documents is not documents``. The
    memo must therefore never hand a caller inputs parsed from a different
    mapping object, even when the bytes are equal.
    """

    documents, _ = _fixture()
    clear_prepared_playable_unit_compiler_memo()
    first = prepare_playable_unit_compiler(documents)
    assert prepare_playable_unit_compiler(documents) is first

    equal_copy = dict(documents)
    shadow = prepare_playable_unit_compiler(equal_copy)
    assert shadow is not first
    assert shadow.documents is equal_copy

    changed = dict(documents)
    key = next(iter(changed))
    changed[key] = changed[key] + b"\n; edited\n"
    assert prepare_playable_unit_compiler(changed) is not first

    clear_prepared_playable_unit_compiler_memo()
    assert prepare_playable_unit_compiler(documents) is not first


def test_source_document_view_is_shared_across_factions(monkeypatch, tmp_path) -> None:
    """One mapping object per (tree, catalog, tree fingerprint) key."""

    from openbfme_importer import spellbook_import

    spellbook_import.clear_spellbook_source_documents_memo()
    manifest = tmp_path / ".openbfme" / "manifest.json"
    manifest.parent.mkdir(parents=True)
    manifest.write_text(
        json.dumps({"aggregate_sha256": "b" * 64, "files": []}), encoding="utf-8"
    )
    built: list[int] = []
    payload = {"data/ini/gamedata.ini": b"; fixture\n"}

    def _fake(effective_root, catalog=None):
        built.append(1)
        return dict(payload)

    monkeypatch.setattr(
        spellbook_import, "_spellbook_source_documents_uncached", _fake
    )
    first = spellbook_import.spellbook_source_documents(tmp_path)
    second = spellbook_import.spellbook_source_documents(tmp_path)
    assert first is second
    assert len(built) == 1
    spellbook_import.clear_spellbook_source_documents_memo()
    assert spellbook_import.spellbook_source_documents(tmp_path) is not first


def test_unsealed_tree_is_never_memoized(monkeypatch, tmp_path) -> None:
    """No sealed manifest, no cheap identity — so no memo (and no rglob)."""

    from openbfme_importer import spellbook_import

    spellbook_import.clear_spellbook_source_documents_memo()
    built: list[int] = []

    def _fake(effective_root, catalog=None):
        built.append(1)
        return {"data/ini/gamedata.ini": b"; fixture\n"}

    monkeypatch.setattr(
        spellbook_import, "_spellbook_source_documents_uncached", _fake
    )
    first = spellbook_import.spellbook_source_documents(tmp_path)
    second = spellbook_import.spellbook_source_documents(tmp_path)
    assert first is not second
    assert len(built) == 2


def test_live_compiler_source_snapshot_is_reused_and_cleared() -> None:
    """Package sources are the identity oracle; read them once per process."""

    clear_compiler_identity_token_memo()
    first = incremental_rebuild._live_module_sources()
    assert first is incremental_rebuild._live_module_sources()
    assert "faction_import.py" in first
    clear_compiler_identity_token_memo()
    assert incremental_rebuild._LIVE_SOURCE_MEMO is None


def test_compiler_dependency_identity_computes_once_under_concurrency(
    monkeypatch,
) -> None:
    """Sixteen convert workers must not each re-hash the package corpus."""

    clear_compiler_identity_token_memo()
    calls: list[str] = []
    real = incremental_rebuild._compute_compiler_dependency_identity

    def _counted(family, sources, *, memoize):
        calls.append(family)
        return real(family, sources, memoize=memoize)

    monkeypatch.setattr(
        incremental_rebuild, "_compute_compiler_dependency_identity", _counted
    )
    results: list[str] = []
    lock = threading.Lock()

    def _worker() -> None:
        value = incremental_rebuild.compiler_dependency_identity("create-a-hero")
        with lock:
            results.append(value["sha256"])

    threads = [threading.Thread(target=_worker) for _ in range(8)]
    for thread in threads:
        thread.start()
    for thread in threads:
        thread.join()

    assert len(set(results)) == 1
    assert len(calls) == 1
    clear_compiler_identity_token_memo()


def test_compiler_dependency_identity_still_tracks_source_bytes() -> None:
    """The snapshot must not make a real source edit invisible."""

    clear_compiler_identity_token_memo()
    live = compiler_dependency_identity("unit")
    synthetic = compiler_dependency_identity(
        "unit", module_sources={"playable_unit_compiler.py": b"tampered"}
    )
    assert synthetic["sha256"] != live["sha256"]
    clear_compiler_identity_token_memo()


def test_hoisted_full_corpus_closure_matches_the_per_object_computation() -> None:
    """Reusing one full-corpus closure must not change any emitted row."""

    documents = {"data/ini/object/a.ini": b"Object A\nEnd\n"}
    hoisted = document_closure_identity(documents, None)
    plan_row = {
        "id": "FixtureCreateAHero",
        "family": "create-a-hero",
        "status": "excluded",
        "reason": "",
    }
    common = dict(
        documents=documents,
        prepared=None,
        faction_graph={},
        effective_root=Path("."),
        catalog=None,
        spawned=(),
        wall_templates=(),
        source_null_sets=(),
        object_cache=None,
        catalog_identity_sha256="",
        assets_fp="",
        policy_fp="",
    )
    baseline, _ = _convert_one_plan_object(dict(plan_row), **common)
    reused, _ = _convert_one_plan_object(
        dict(plan_row), full_corpus_closure=hoisted, **common
    )
    for row in (baseline, reused):
        row.pop("convertElapsedMs", None)
    assert reused == baseline
    assert reused["incremental"]["dependencyMode"] == "full-corpus-fallback"


def test_parallel_plan_is_byte_identical_to_the_serial_plan() -> None:
    """Plan parallelism is an ordering-preserving change, not a content change."""

    documents, graph = _fixture()
    serial = build_faction_import_plan(
        graph, documents, catalog_identity_sha256="2" * 64, plan_jobs=1
    )
    parallel = build_faction_import_plan(
        graph, documents, catalog_identity_sha256="2" * 64, plan_jobs=8
    )
    assert parallel == serial
    assert parallel["aggregateSha256"] == serial["aggregateSha256"]
    assert [row["id"] for row in parallel["objects"]] == [
        row["id"] for row in serial["objects"]
    ]


def test_shared_kind_cache_computes_each_key_once_under_concurrency() -> None:
    """A per-kind corpus scan must not be repeated by every racing worker."""

    from openbfme_importer import playable_unit_compiler as puc

    documents, _ = _fixture()
    cache: dict[str, tuple] = {}
    lock = threading.Condition()
    scans: list[str] = []
    scan_lock = threading.Lock()
    real = puc.parse_flat_named_blocks

    def _counted(payload, kind):
        with scan_lock:
            scans.append(kind)
        return real(payload, kind)

    results: list[tuple] = []
    original = puc.parse_flat_named_blocks
    puc.parse_flat_named_blocks = _counted
    try:

        def _worker() -> None:
            value = puc._flat_blocks_for_kind(
                documents, "Weapon", cache, cache_lock=lock
            )
            with scan_lock:
                results.append(value)

        threads = [threading.Thread(target=_worker) for _ in range(8)]
        for thread in threads:
            thread.start()
        for thread in threads:
            thread.join()
    finally:
        puc.parse_flat_named_blocks = original

    assert len(results) == 8
    assert all(value is results[0] for value in results)
    # One scan per document, not eight.
    assert len(scans) == len(documents)


def test_shared_kind_cache_failure_does_not_strand_waiters() -> None:
    """A raising computation must clear its pending slot and wake waiters."""

    from openbfme_importer import playable_unit_compiler as puc

    cache: dict[str, object] = {}
    lock = threading.Condition()
    claimed, _ = puc._claim_cache_key(cache, "k", lock)
    assert claimed is True
    assert "k" in cache
    puc._publish_cache_key(cache, "k", lock, None, failed=True)
    assert "k" not in cache
    claimed, _ = puc._claim_cache_key(cache, "k", lock)
    assert claimed is True
    assert puc._publish_cache_key(cache, "k", lock, "value") == "value"
    assert puc._claim_cache_key(cache, "k", lock) == (False, "value")


def test_worker_count_resolution_is_explicit(monkeypatch) -> None:
    monkeypatch.delenv("OPENBFME_FACTION_CONVERT_JOBS", raising=False)
    assert resolve_convert_worker_count(4) == 4
    assert resolve_convert_worker_count(None) >= 1
    assert resolve_convert_worker_count(None) <= 16
    monkeypatch.setenv("OPENBFME_FACTION_CONVERT_JOBS", "3")
    assert resolve_convert_worker_count(None) == 3
    monkeypatch.setenv("OPENBFME_FACTION_CONVERT_JOBS", "not-a-number")
    assert resolve_convert_worker_count(None) >= 1


def test_plan_is_serial_by_default(monkeypatch) -> None:
    """Planning is GIL-bound and measured slower threaded — opt-in only."""

    monkeypatch.delenv("OPENBFME_FACTION_PLAN_JOBS", raising=False)
    monkeypatch.setenv("OPENBFME_FACTION_CONVERT_JOBS", "16")
    assert resolve_plan_worker_count(None) == 1
    assert resolve_plan_worker_count(8) == 8
    monkeypatch.setenv("OPENBFME_FACTION_PLAN_JOBS", "4")
    assert resolve_plan_worker_count(None) == 4
    monkeypatch.setenv("OPENBFME_FACTION_PLAN_JOBS", "not-a-number")
    assert resolve_plan_worker_count(None) == 1
