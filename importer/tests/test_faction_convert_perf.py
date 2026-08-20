"""Guards for the faction-convert wall-clock optimizations.

Every test here is a correctness guard on a change made for speed: the
optimized path must return exactly what the unoptimized path returned.
"""

from __future__ import annotations

import json
import sys
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
from openbfme_importer.faction_plan_cache import (
    PLAN_CACHE_VERSION,
    FactionPlanRowCache,
    rows_digest,
)
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


def _plan_cache_probe(tmp_path, monkeypatch):
    """Return (documents, graph, cache, call_counter) wired for plan caching."""

    from openbfme_importer import faction_import as fi

    documents, graph = _fixture()
    cache = FactionPlanRowCache(tmp_path / "faction-plan-rows")
    calls: list[str] = []
    real = fi.compile_playable_unit_descriptor

    def _counted(object_id, *args, **kwargs):
        calls.append(object_id)
        return real(object_id, *args, **kwargs)

    monkeypatch.setattr(fi, "compile_playable_unit_descriptor", _counted)
    return documents, graph, cache, calls


def _build_with_cache(documents, graph, cache):
    return build_faction_import_plan(
        graph,
        documents,
        catalog_identity_sha256="2" * 64,
        row_cache=cache,
        row_cache_assets_fp="non-ini-manifest:" + "a" * 64,
    )


def test_plan_rows_are_recomputed_without_a_cache_and_loaded_with_one(
    tmp_path, monkeypatch
) -> None:
    """Failing-first shape: no cache => compile; warm cache => load."""

    documents, graph, cache, calls = _plan_cache_probe(tmp_path, monkeypatch)

    # No cache at all: every descriptor is compiled.
    uncached = build_faction_import_plan(
        graph, documents, catalog_identity_sha256="2" * 64
    )
    assert calls, "expected the uncached plan to compile descriptors"

    # Cold cache: compiles, and populates.
    calls.clear()
    cold = _build_with_cache(documents, graph, cache)
    assert calls, "expected the cold plan-cache run to compile descriptors"
    assert cache.hits == 0
    assert cache.misses > 0

    # Warm cache: no descriptor compiled at all, identical plan.
    calls.clear()
    warm = _build_with_cache(documents, graph, cache)
    assert calls == [], f"warm plan recompiled {calls}"
    assert cache.hits > 0
    assert warm == cold
    assert warm["aggregateSha256"] == cold["aggregateSha256"] == uncached["aggregateSha256"]


def test_poisoned_plan_cache_row_is_refused_and_recomputed(
    tmp_path, monkeypatch
) -> None:
    """Flip one byte of a cached row: the entry is refused, not served."""

    documents, graph, cache, calls = _plan_cache_probe(tmp_path, monkeypatch)
    clean = _build_with_cache(documents, graph, cache)

    entries = sorted((tmp_path / "faction-plan-rows").rglob("plan-rows.json"))
    assert entries, "expected the cold run to write cache entries"
    poisoned = None
    for entry in entries:
        payload = json.loads(entry.read_text(encoding="utf-8"))
        rows = payload.get("rows") or []
        if rows and isinstance(rows[0].get("descriptorSha256"), str):
            digest = rows[0]["descriptorSha256"]
            rows[0]["descriptorSha256"] = ("0" if digest[0] != "0" else "1") + digest[1:]
            entry.write_text(json.dumps(payload), encoding="utf-8")
            poisoned = entry
            break
    assert poisoned is not None, "expected a row carrying a descriptor digest"

    calls.clear()
    cache.hits = cache.misses = cache.refusals = 0
    after = _build_with_cache(documents, graph, cache)
    assert cache.refusals >= 1, "poisoned entry was not refused"
    assert after == clean, "a refused entry must recompute the true row"
    assert after["aggregateSha256"] == clean["aggregateSha256"]


def test_plan_cache_refuses_a_foreign_compiler_identity(tmp_path, monkeypatch) -> None:
    """A row compiled by different converter bytes may not be reused."""

    documents, graph, cache, _calls = _plan_cache_probe(tmp_path, monkeypatch)
    _build_with_cache(documents, graph, cache)

    entries = sorted((tmp_path / "faction-plan-rows").rglob("plan-rows.json"))
    payload = json.loads(entries[0].read_text(encoding="utf-8"))
    payload["compilerIdentity"] = "f" * 64
    payload["rowsSha256"] = rows_digest(payload["rows"])  # keep self-digest honest
    entries[0].write_text(json.dumps(payload), encoding="utf-8")

    cache.hits = cache.misses = cache.refusals = 0
    _build_with_cache(documents, graph, cache)
    assert cache.refusals >= 1


def test_plan_cache_version_drift_misses_cleanly(tmp_path, monkeypatch) -> None:
    """A stale format is a miss, never a half-load."""

    documents, graph, cache, _calls = _plan_cache_probe(tmp_path, monkeypatch)
    clean = _build_with_cache(documents, graph, cache)

    for entry in (tmp_path / "faction-plan-rows").rglob("plan-rows.json"):
        payload = json.loads(entry.read_text(encoding="utf-8"))
        payload["version"] = PLAN_CACHE_VERSION + 1
        entry.write_text(json.dumps(payload), encoding="utf-8")

    cache.hits = cache.misses = cache.refusals = 0
    after = _build_with_cache(documents, graph, cache)
    assert cache.hits == 0
    assert after == clean


def test_corrupt_plan_cache_entry_never_fails_the_plan(tmp_path, monkeypatch) -> None:
    """Fail open: unreadable JSON costs time, not the convert."""

    documents, graph, cache, _calls = _plan_cache_probe(tmp_path, monkeypatch)
    clean = _build_with_cache(documents, graph, cache)

    for entry in (tmp_path / "faction-plan-rows").rglob("plan-rows.json"):
        entry.write_text("{ this is not json", encoding="utf-8")

    cache.hits = cache.misses = cache.refusals = 0
    after = _build_with_cache(documents, graph, cache)
    assert cache.hits == 0
    assert after == clean


def test_plan_cache_is_disabled_by_environment(monkeypatch, tmp_path) -> None:
    from openbfme_importer import faction_import as fi

    monkeypatch.setenv("OPENBFME_NO_PLAN_CACHE", "1")
    assert fi._resolve_plan_row_cache(tmp_path, tmp_path) is None
    monkeypatch.setenv("OPENBFME_NO_PLAN_CACHE", "0")
    monkeypatch.setenv("OPENBFME_NO_OBJECT_CACHE", "1")
    assert fi._resolve_plan_row_cache(tmp_path, tmp_path) is None
    monkeypatch.delenv("OPENBFME_NO_OBJECT_CACHE")
    assert fi._resolve_plan_row_cache(tmp_path, tmp_path) is not None
    assert fi._resolve_plan_row_cache(None, tmp_path) is None


# --------------------------------------------------------------------------
# Dependency-identity precision: a compiler edit must invalidate the lanes that
# actually run that code and nothing else. Over-invalidation only costs time;
# under-invalidation ships a stale artifact, so every "stays a hit" assertion
# below is paired with a "does invalidate" assertion in the other direction.
# --------------------------------------------------------------------------

_PRECISION_MODULES = {
    "armor_compiler.py": b"A = 1\n",
    "faction_import.py": b"A = 1\n",
    "faction_object_cache.py": b"A = 1\n",
    "faction_census.py": b"A = 1\n",
    "incremental_rebuild.py": b"A = 1\n",
    "faction_policy.py": b"A = 1\n",
    "pack_recipe_catalog_identity.py": b"A = 1\n",
    "sage_cst.py": b"A = 1\n",
    "sage_ini.py": b"A = 1\n",
    "sage_string.py": b"A = 1\n",
    "visual_leaf.py": b"A = 1\n",
    "playable_unit_compiler.py": b"A = 1\n",
    "playable_unit_import.py": b"A = 1\n",
    "playable_unit_pack_compiler.py": b"A = 1\n",
    "playable_structure_compiler.py": b"A = 1\n",
    "playable_structure_lifecycle_evidence.py": b"A = 1\n",
    "playable_structure_pack_compiler.py": b"A = 1\n",
    "castle_behavior.py": b"A = 1\n",
    "retail_building_lifecycle.py": b"A = 1\n",
    "spellbook_compiler.py": b"A = 1\n",
    "spellbook_import.py": b"A = 1\n",
    "spellbook_pack_compiler.py": b"A = 1\n",
    "spellbook_visual_ingress.py": b"A = 1\n",
    "retail_ability_fx_ingress.py": b"A = 1\n",
    "retail_visual_closure.py": b"A = 1\n",
    "typed_visual_graph.py": b"A = 1\n",
    "w3d_glb_validation.py": b"A = 1\n",
    "w3d_index.py": b"A = 1\n",
    "w3d_texture_closure.py": b"A = 1\n",
    # A module no convert lane imports (living-world/HUD/cursor class).
    "living_world_ui.py": b"A = 1\n",
    "blender/w3d_to_glb.py": b"A = 1\n",
}


def _identity(family: str, modules) -> str:
    return compiler_dependency_identity(family, module_sources=modules)["sha256"]


def _touch(name: str):
    """Comment-only edit: different bytes, identical behaviour."""

    edited = dict(_PRECISION_MODULES)
    edited[name] = edited[name] + b"# touched\n"
    return edited


def test_doc_compiler_edit_does_not_invalidate_unrelated_lanes() -> None:
    """Touch playable_unit_compiler.py: unit/structure move, spellbook does not."""

    edited = _touch("playable_unit_compiler.py")
    # Direction 1 — the lanes that really run it must invalidate.
    assert _identity("playable-unit", edited) != _identity(
        "playable-unit", _PRECISION_MODULES
    )
    assert _identity("structure", edited) != _identity(
        "structure", _PRECISION_MODULES
    )
    # Direction 2 — a lane that does not run it must stay a cache hit.
    assert _identity("spellbook", edited) == _identity(
        "spellbook", _PRECISION_MODULES
    )


def test_structure_pack_edit_leaves_unit_and_spellbook_rows_hits() -> None:
    edited = _touch("playable_structure_pack_compiler.py")
    assert _identity("structure", edited) != _identity(
        "structure", _PRECISION_MODULES
    )
    assert _identity("playable-unit", edited) == _identity(
        "playable-unit", _PRECISION_MODULES
    )
    assert _identity("spellbook", edited) == _identity(
        "spellbook", _PRECISION_MODULES
    )


def test_unrelated_module_edit_invalidates_no_convert_lane() -> None:
    """The everyday case: editing code no lane imports must evict nothing."""

    edited = _touch("living_world_ui.py")
    for family in ("playable-unit", "structure", "spellbook", "create-a-hero"):
        assert _identity(family, edited) == _identity(
            family, _PRECISION_MODULES
        ), family


def test_accounted_families_have_a_precise_lane_not_the_whole_package() -> None:
    """Excluded/gap rows must not be evicted by every module in the package."""

    for family in (
        "create-a-hero",
        "projectile",
        "object-inheritance",
        "retail-object-parser",
        "missing-object",
        "unclassified",
    ):
        identity = compiler_dependency_identity(
            family, module_sources=_PRECISION_MODULES
        )
        assert identity["mode"] == "explicit-family-manifest", family
        # A payload lane it never runs must not move it...
        assert _identity(family, _touch("spellbook_pack_compiler.py")) == _identity(
            family, _PRECISION_MODULES
        ), family
        # ...but the table that decides the exclusion must.
        assert _identity(family, _touch("faction_import.py")) != _identity(
            family, _PRECISION_MODULES
        ), family


def test_banner_member_and_builder_use_the_unit_lane() -> None:
    """Both compile unit descriptors; neither may take the broad fallback."""

    for family in ("banner-member", "builder"):
        identity = compiler_dependency_identity(
            family, module_sources=_PRECISION_MODULES
        )
        assert identity["mode"] == "explicit-family-manifest", family
        assert _identity(family, _touch("playable_unit_compiler.py")) != _identity(
            family, _PRECISION_MODULES
        ), family
        assert _identity(family, _touch("spellbook_compiler.py")) == _identity(
            family, _PRECISION_MODULES
        ), family


def test_unknown_family_still_fails_closed_to_the_whole_package() -> None:
    """An unclassifiable family keeps the broad identity. This is the guard."""

    identity = compiler_dependency_identity(
        "unknown", module_sources=_PRECISION_MODULES
    )
    assert identity["mode"] == "full-compiler-fallback"
    assert _identity("unknown", _touch("living_world_ui.py")) != _identity(
        "unknown", _PRECISION_MODULES
    )


def test_blender_adapter_edit_invalidates_every_lane() -> None:
    """The adapter produces the GLB bytes every lane embeds."""

    edited = _touch("blender/w3d_to_glb.py")
    for family in ("playable-unit", "structure", "spellbook"):
        assert _identity(family, edited) != _identity(
            family, _PRECISION_MODULES
        ), family


def test_plan_stage_identity_is_lane_union_not_whole_package(monkeypatch) -> None:
    """Plan rows must survive an edit to code no plan lane imports."""

    from openbfme_importer import incremental_rebuild as ir

    calls: list[str] = []
    lanes = sorted(ir._COMPILER_DEPENDENCY_MANIFESTS)
    table = {lane: f"{index}".rjust(64, "0") for index, lane in enumerate(lanes)}

    def _fake(family, *, module_sources=None):
        calls.append(family)
        return {"sha256": table[family]}

    monkeypatch.setattr(ir, "compiler_dependency_identity", _fake)
    before = ir.plan_stage_identity()
    assert sorted(calls) == lanes
    # Unchanged lanes -> unchanged plan identity.
    assert ir.plan_stage_identity() == before
    # A moved lane -> moved plan identity.
    table["structure"] = "9" * 64
    assert ir.plan_stage_identity() != before


def test_live_lanes_all_resolve_precisely_against_the_real_package() -> None:
    """Against the real package, no convert lane may take the broad fallback.

    This is the gate that catches a precise-looking manifest that silently
    degrades in production — exactly what happened when ``plan_stage_identity``
    asked for the lane named "accounted" and got a 185-module whole-package
    digest back.
    """

    from openbfme_importer.incremental_rebuild import (
        _COMPILER_DEPENDENCY_MANIFESTS,
        plan_stage_identity,
    )

    total = len(incremental_rebuild._live_module_sources())
    for lane in _COMPILER_DEPENDENCY_MANIFESTS:
        identity = compiler_dependency_identity(lane)
        assert identity["mode"] == "explicit-family-manifest", lane
        assert len(identity["modules"]) < total, lane
    for family in (
        "playable-unit",
        "structure",
        "spellbook",
        "create-a-hero",
        "banner-member",
        "builder",
    ):
        identity = compiler_dependency_identity(family)
        assert identity["mode"] == "explicit-family-manifest", family
        assert len(identity["modules"]) < total, family
    # And the plan identity must be a pure function of those lanes.
    assert len(plan_stage_identity()) == 64


def test_plan_identity_ignores_a_module_outside_every_lane(monkeypatch) -> None:
    """A synthetic edit to a non-lane module must not move the plan identity."""

    from openbfme_importer import incremental_rebuild as ir

    sources = dict(ir._live_module_sources())
    assert "living_world_ui.py" in sources

    def _with(mods):
        lanes = sorted(ir._COMPILER_DEPENDENCY_MANIFESTS)
        return {
            lane: ir.compiler_dependency_identity(lane, module_sources=mods)["sha256"]
            for lane in lanes
        }

    before = _with(sources)
    edited = dict(sources)
    edited["living_world_ui.py"] = edited["living_world_ui.py"] + b"\n# touched\n"
    assert _with(edited) == before


def test_atomic_write_never_exposes_a_torn_file_to_concurrent_readers(
    tmp_path,
) -> None:
    """Same-key concurrent writers: last-write-wins is fine, torn files are not.

    This is the precondition for sharing one conversion cache across parallel
    convert workers. ``write_json_atomic`` writes a unique temp file, fsyncs it
    and then ``os.replace``s it into position, so a reader either sees the old
    complete file or a new complete file — never a partial one.
    """

    from openbfme_importer.util import read_json, write_json_atomic

    target = tmp_path / "shared" / "entry.json"
    write_json_atomic(target, {"writer": "seed", "rows": ["seed"]})
    stop = threading.Event()
    torn: list[str] = []
    seen: list[str] = []

    def _writer(name: str) -> None:
        for _ in range(40):
            write_json_atomic(target, {"writer": name, "rows": [name] * 200})

    def _reader() -> None:
        while not stop.is_set():
            try:
                value = read_json(target)
            except (ValueError, OSError) as exc:
                # A torn or missing file is the failure this test exists for.
                torn.append(f"{type(exc).__name__}: {exc}")
                return
            if not isinstance(value, dict) or "writer" not in value:
                torn.append(f"malformed: {value!r}")
                return
            if set(value["rows"]) != {value["writer"]}:
                torn.append(f"mixed payload: {value!r}")
                return
            seen.append(str(value["writer"]))

    readers = [threading.Thread(target=_reader) for _ in range(4)]
    for thread in readers:
        thread.start()
    writers = [
        threading.Thread(target=_writer, args=(f"w{index}",)) for index in range(4)
    ]
    for thread in writers:
        thread.start()
    for thread in writers:
        thread.join()
    stop.set()
    for thread in readers:
        thread.join()

    assert torn == [], torn[:3]
    assert seen, "readers observed nothing"
    # Whatever landed last is one writer's complete payload.
    final = read_json(target)
    assert set(final["rows"]) == {final["writer"]}


def test_media_conversion_key_is_independent_of_doc_compilers() -> None:
    """W3D/GLB conversion identity must not depend on document compilers.

    The media cache keys on the Blender adapter bytes plus tool attestation,
    never on the importer's document-compiler sources, so a doc-compiler edit
    must never force a model reconversion.
    """

    from openbfme_importer.incremental_rebuild import w3d_adapter_cache_identity

    adapter_before = "a" * 64
    adapter_after = "b" * 64
    # No scoped-reconversion patterns: the identity is exactly the adapter's.
    assert w3d_adapter_cache_identity(adapter_before, "GondorFighter", ()) == (
        adapter_before
    )
    assert w3d_adapter_cache_identity(adapter_after, "GondorFighter", ()) == (
        adapter_after
    )
    # It is a pure function of the adapter identity and the asset id; no
    # document-compiler identity is an input at all.
    import inspect

    signature = inspect.signature(w3d_adapter_cache_identity)
    assert set(signature.parameters) == {
        "adapter_sha256",
        "asset_id",
        "patterns",
    }


def _load_batch_module():
    """Import tools/rotwk_faction_convert_batch.py without running it."""

    import importlib.util

    root = Path(__file__).resolve().parents[2]
    path = root / "tools" / "rotwk_faction_convert_batch.py"
    spec = importlib.util.spec_from_file_location("_convert_batch_probe", path)
    module = importlib.util.module_from_spec(spec)
    sys.modules["_convert_batch_probe"] = module
    spec.loader.exec_module(module)
    return module


def test_object_shards_partition_every_id_exactly_once() -> None:
    """Shards must cover the object set once each — no gaps, no duplicates."""

    batch = _load_batch_module()
    ids = [f"GondorObject{index}" for index in range(500)] + [
        "MenSpellBook",
        "CreateAHero",
    ]
    for count in (1, 2, 3, 8, 20):
        selectors = [batch.shard_selector(index, count) for index in range(count)]
        owners = [
            [index for index, sel in enumerate(selectors) if sel(object_id)]
            for object_id in ids
        ]
        assert all(len(owner) == 1 for owner in owners), count
    # Every shard of a 20-way split gets work from 500 ids (balance sanity).
    selectors = [batch.shard_selector(index, 20) for index in range(20)]
    sizes = [sum(1 for object_id in ids if sel(object_id)) for sel in selectors]
    assert min(sizes) > 0, sizes


def test_object_shard_assignment_is_stable_and_case_insensitive() -> None:
    batch = _load_batch_module()
    selector = batch.shard_selector(3, 8)
    assert selector("GondorArcher") is selector("gondorarcher")
    assert selector("GondorArcher") == batch.shard_selector(3, 8)("GondorArcher")


def test_object_selector_restricts_the_plan_without_changing_rows() -> None:
    """A sharded plan is a strict subset of the whole plan, row for row."""

    documents, graph = _fixture()
    whole = build_faction_import_plan(
        graph, documents, catalog_identity_sha256="2" * 64
    )
    whole_rows = {str(row["id"]): row for row in whole["objects"]}

    batch = _load_batch_module()
    seen: dict[str, dict] = {}
    for index in range(3):
        shard = build_faction_import_plan(
            graph,
            documents,
            catalog_identity_sha256="2" * 64,
            object_selector=batch.shard_selector(index, 3),
        )
        for row in shard["objects"]:
            object_id = str(row["id"])
            assert object_id not in seen, f"{object_id} planned by two shards"
            seen[object_id] = row

    assert set(seen) == set(whole_rows)
    for object_id, row in seen.items():
        assert row == whole_rows[object_id], object_id


def test_plan_row_order_is_by_object_id_not_completion_order() -> None:
    """Parent-side ordering must be deterministic regardless of scheduling."""

    documents, graph = _fixture()
    plan = build_faction_import_plan(
        graph, documents, catalog_identity_sha256="2" * 64, plan_jobs=8
    )
    ids = [str(row["id"]) for row in plan["objects"]]
    assert ids == sorted(ids, key=lambda value: (value.casefold(), value))


def test_object_cache_put_survives_a_refused_atomic_write(monkeypatch, tmp_path) -> None:
    """WinError 5 from a concurrent reader must degrade to a miss, not raise."""

    from openbfme_importer import faction_object_cache as foc

    cache = foc.FactionObjectCache(tmp_path / "objects")

    def _refuse(path, value):
        raise PermissionError(5, "Access is denied")

    monkeypatch.setattr(foc, "write_json_atomic", _refuse)
    cache.put("a" * 64, row={"id": "X", "status": "converted"}, artifacts={})
    assert cache.get("a" * 64) is None


def _census_cache(tmp_path):
    from openbfme_importer.faction_census_cache import FactionCensusCache

    return FactionCensusCache(tmp_path / "faction-census")


_CENSUS_KEY = {
    "faction": "men",
    "game": "rotwk",
    "catalog_identity_sha256": "a" * 64,
    "effective_root_fp": "manifest-agg:" + "b" * 64,
    "policy_fp": "c" * 64,
    "census_identity": "d" * 64,
}


def test_census_cache_round_trips_and_recomputes_on_any_key_change(tmp_path) -> None:
    from openbfme_importer.faction_census_cache import (
        census_cache_key,
        load_or_build_census,
    )

    cache = _census_cache(tmp_path)
    graph = {"definitions": {"objects": [{"id": "A"}]}, "summary": {"unresolvedCount": 0}}
    builds: list[int] = []

    def _build():
        builds.append(1)
        return graph

    first = load_or_build_census(cache, lambda: dict(_CENSUS_KEY), _build)
    second = load_or_build_census(cache, lambda: dict(_CENSUS_KEY), _build)
    assert first == second == graph
    assert len(builds) == 1, "warm census must not rebuild"
    assert cache.hits == 1

    for field in _CENSUS_KEY:
        moved = dict(_CENSUS_KEY)
        moved[field] = "9" * 64 if len(str(moved[field])) == 64 else "moved"
        assert census_cache_key(**moved) != census_cache_key(**_CENSUS_KEY), field
        load_or_build_census(cache, lambda m=moved: dict(m), _build)
    assert len(builds) == 1 + len(_CENSUS_KEY)


def test_poisoned_census_entry_is_refused_and_recomputed(tmp_path) -> None:
    """Flip one byte of the cached graph: the digest gate must refuse it."""

    from openbfme_importer.faction_census_cache import (
        census_cache_key,
        load_or_build_census,
    )

    cache = _census_cache(tmp_path)
    graph = {"definitions": {"objects": [{"id": "GondorArcher"}]}, "n": 1}
    load_or_build_census(cache, lambda: dict(_CENSUS_KEY), lambda: graph)

    entry = next((tmp_path / "faction-census").rglob("census.json"))
    payload = json.loads(entry.read_text(encoding="utf-8"))
    payload["graph"]["definitions"]["objects"][0]["id"] = "GondorArcherX"
    entry.write_text(json.dumps(payload), encoding="utf-8")

    cache.hits = cache.misses = cache.refusals = 0
    rebuilt = load_or_build_census(cache, lambda: dict(_CENSUS_KEY), lambda: graph)
    assert cache.refusals == 1
    assert rebuilt == graph, "a refused entry must recompute the true graph"


def test_corrupt_census_entry_never_fails_the_convert(tmp_path) -> None:
    from openbfme_importer.faction_census_cache import load_or_build_census

    cache = _census_cache(tmp_path)
    graph = {"ok": True}
    load_or_build_census(cache, lambda: dict(_CENSUS_KEY), lambda: graph)
    for entry in (tmp_path / "faction-census").rglob("census.json"):
        entry.write_text("{ not json", encoding="utf-8")
    cache.hits = cache.misses = cache.refusals = 0
    assert load_or_build_census(cache, lambda: dict(_CENSUS_KEY), lambda: graph) == graph
    assert cache.hits == 0


def test_census_version_drift_misses_cleanly(tmp_path) -> None:
    from openbfme_importer.faction_census_cache import (
        CENSUS_CACHE_VERSION,
        load_or_build_census,
    )

    cache = _census_cache(tmp_path)
    graph = {"ok": True}
    load_or_build_census(cache, lambda: dict(_CENSUS_KEY), lambda: graph)
    for entry in (tmp_path / "faction-census").rglob("census.json"):
        payload = json.loads(entry.read_text(encoding="utf-8"))
        payload["version"] = CENSUS_CACHE_VERSION + 1
        entry.write_text(json.dumps(payload), encoding="utf-8")
    cache.hits = cache.misses = cache.refusals = 0
    assert load_or_build_census(cache, lambda: dict(_CENSUS_KEY), lambda: graph) == graph
    assert cache.hits == 0


def test_census_cache_is_disabled_by_environment(monkeypatch) -> None:
    from openbfme_importer.faction_census_cache import census_cache_disabled

    monkeypatch.delenv("OPENBFME_NO_CENSUS_CACHE", raising=False)
    monkeypatch.delenv("OPENBFME_NO_OBJECT_CACHE", raising=False)
    assert census_cache_disabled() is False
    monkeypatch.setenv("OPENBFME_NO_CENSUS_CACHE", "1")
    assert census_cache_disabled() is True
    monkeypatch.delenv("OPENBFME_NO_CENSUS_CACHE")
    monkeypatch.setenv("OPENBFME_NO_OBJECT_CACHE", "yes")
    assert census_cache_disabled() is True


def test_census_lane_is_precise_and_excludes_every_payload_compiler() -> None:
    """Census compiles nothing; no payload lane may invalidate it."""

    identity = compiler_dependency_identity("census")
    assert identity["mode"] == "explicit-family-manifest"
    names = {row["path"] for row in identity["modules"]}
    for payload in (
        "playable_unit_compiler.py",
        "playable_structure_compiler.py",
        "playable_structure_pack_compiler.py",
        "spellbook_compiler.py",
        "retail_visual_closure.py",
        "w3d_index.py",
    ):
        assert payload not in names, payload
    assert "faction_census.py" in names


def test_convert_faction_import_census_key_material_resolves(monkeypatch, tmp_path) -> None:
    """Exercise the real census-cache call site, not just the cache module.

    The first version of this wiring referenced a name that
    ``faction_import`` does not import; every unit test passed because they
    drove ``faction_census_cache`` directly, and only a full convert run
    caught the NameError. This drives the call site.
    """

    from openbfme_importer import faction_import as fi

    captured: dict[str, object] = {}
    real = fi.load_or_build_census

    def _capture(cache, key_material, build):
        # Force the key material to be evaluated exactly as production does.
        captured["key"] = key_material()
        return real(cache, key_material, build)

    monkeypatch.setattr(fi, "load_or_build_census", _capture)
    monkeypatch.setattr(
        fi, "census_playable_faction", lambda *a, **k: {"definitions": {"objects": []}}
    )
    monkeypatch.setattr(fi, "_faction_spec", lambda catalog, faction: ("men", "FactionMen", "Men"))

    class _Catalog:
        source_policy = None

        def identity_sha256(self):
            return "e" * 64

    try:
        fi.convert_faction_import(
            _Catalog(), tmp_path, "men", state_root=tmp_path, game="rotwk"
        )
    except Exception:
        # The conversion itself cannot succeed against an empty tree; all this
        # test asserts is that building the census key material does not raise.
        pass

    key = captured.get("key")
    assert isinstance(key, dict), "census key material was never built"
    assert set(key) == {
        "faction",
        "game",
        "catalog_identity_sha256",
        "effective_root_fp",
        "policy_fp",
        "census_identity",
    }
    assert len(key["census_identity"]) == 64


def test_census_graph_is_json_stable() -> None:
    """The graph digest keys the plan and object caches; JSON must round trip.

    If this ever fails, storing the census as JSON would silently move every
    downstream cache key, so the cache must not store JSON any more.
    """

    from openbfme_importer.faction_census_cache import graph_digest

    graph = {
        "definitions": {"objects": [{"id": "A", "edges": []}]},
        "summary": {"unresolvedCount": 0},
        "roots": [{"id": "R", "edgeKind": "engine-implicit-object"}],
    }
    round_tripped = json.loads(json.dumps(graph))
    assert round_tripped == graph
    assert graph_digest(round_tripped) == graph_digest(graph)


_COVERAGE_COMPONENTS = {
    "faction": "men",
    "game": "rotwk",
    "catalogIdentitySha256": "a" * 64,
    "effectiveAssetsFp": "manifest-agg:" + "b" * 64,
    "graphSha256": "c" * 64,
    "policyFp": "d" * 64,
    "laneIdentities": {
        "unit": "1" * 64,
        "structure": "2" * 64,
        "spellbook": "3" * 64,
        "accounted": "4" * 64,
        "census": "5" * 64,
    },
}


def _coverage_fixture(tmp_path):
    from openbfme_importer.faction_coverage_cache import (
        FactionCoverageCache,
        coverage_cache_key,
    )

    cache = FactionCoverageCache(tmp_path / "faction-coverage")
    artifact_root = tmp_path / "objects"
    (artifact_root / "gondorarcher").mkdir(parents=True)
    (artifact_root / "gondorarcher" / "descriptor.json").write_text(
        '{"descriptorSha256": "e"}', encoding="utf-8"
    )
    coverage = {
        "objects": [{"id": "GondorArcher", "status": "converted"}],
        "planAggregateSha256": "f" * 64,
        "aggregateSha256": "0" * 64,
    }
    key = coverage_cache_key(_COVERAGE_COMPONENTS)
    cache.put(
        key,
        components=_COVERAGE_COMPONENTS,
        coverage=coverage,
        artifact_root=artifact_root,
    )
    return cache, key, coverage, artifact_root


def test_coverage_short_circuit_reuses_only_on_a_full_input_match(tmp_path) -> None:
    cache, key, coverage, artifact_root = _coverage_fixture(tmp_path)
    hit = cache.get(
        key, components=_COVERAGE_COMPONENTS, artifact_root=artifact_root
    )
    assert hit == coverage
    assert cache.hits == 1


def test_poisoning_any_component_falls_through_to_the_full_walk(tmp_path) -> None:
    """Each input component, moved on its own, must refuse the short-circuit."""

    from openbfme_importer.faction_coverage_cache import coverage_cache_key

    cache, _key, _coverage, artifact_root = _coverage_fixture(tmp_path)
    for name in _COVERAGE_COMPONENTS:
        moved = dict(_COVERAGE_COMPONENTS)
        moved[name] = (
            {"unit": "9" * 64} if name == "laneIdentities" else "9" * 64
        )
        cache.refusals.clear()
        assert (
            cache.get(
                coverage_cache_key(moved),
                components=moved,
                artifact_root=artifact_root,
            )
            is None
        ), name
        assert cache.refusals, name


def test_short_circuit_names_the_component_that_moved(tmp_path) -> None:
    """A short-circuit that stops firing must say why, not just be slow."""

    from openbfme_importer.faction_coverage_cache import coverage_cache_key

    cache, key, _coverage, artifact_root = _coverage_fixture(tmp_path)
    moved = dict(_COVERAGE_COMPONENTS)
    moved["graphSha256"] = "9" * 64
    cache.refusals.clear()
    # Same stored entry, different declared components: the component check
    # fires before the key check can hide the reason.
    assert cache.get(key, components=moved, artifact_root=artifact_root) is None
    assert any("graphSha256" in reason for reason in cache.refusals), cache.refusals


def test_short_circuit_refuses_a_tampered_coverage_document(tmp_path) -> None:
    cache, key, _coverage, artifact_root = _coverage_fixture(tmp_path)
    entry = next((tmp_path / "faction-coverage").rglob("coverage.json"))
    payload = json.loads(entry.read_text(encoding="utf-8"))
    payload["coverage"]["objects"][0]["status"] = "converter-gap"
    entry.write_text(json.dumps(payload), encoding="utf-8")
    cache.refusals.clear()
    assert cache.get(key, components=_COVERAGE_COMPONENTS, artifact_root=artifact_root) is None
    assert any("digest" in reason for reason in cache.refusals), cache.refusals


def test_short_circuit_refuses_a_deleted_or_edited_artifact(tmp_path) -> None:
    """Skipping the loops must never leave a missing artifact unrestored."""

    cache, key, _coverage, artifact_root = _coverage_fixture(tmp_path)
    target = artifact_root / "gondorarcher" / "descriptor.json"

    target.write_text('{"descriptorSha256": "TAMPERED"}', encoding="utf-8")
    cache.refusals.clear()
    assert cache.get(key, components=_COVERAGE_COMPONENTS, artifact_root=artifact_root) is None
    assert any("artifact" in reason for reason in cache.refusals), cache.refusals

    target.unlink()
    cache.refusals.clear()
    assert cache.get(key, components=_COVERAGE_COMPONENTS, artifact_root=artifact_root) is None
    assert any("artifact" in reason for reason in cache.refusals), cache.refusals


def test_short_circuit_version_drift_and_corruption_miss_cleanly(tmp_path) -> None:
    from openbfme_importer.faction_coverage_cache import COVERAGE_CACHE_VERSION

    cache, key, _coverage, artifact_root = _coverage_fixture(tmp_path)
    entry = next((tmp_path / "faction-coverage").rglob("coverage.json"))

    payload = json.loads(entry.read_text(encoding="utf-8"))
    payload["version"] = COVERAGE_CACHE_VERSION + 1
    entry.write_text(json.dumps(payload), encoding="utf-8")
    cache.refusals.clear()
    assert cache.get(key, components=_COVERAGE_COMPONENTS, artifact_root=artifact_root) is None
    assert any("version" in reason for reason in cache.refusals)

    entry.write_text("{ not json", encoding="utf-8")
    cache.refusals.clear()
    assert cache.get(key, components=_COVERAGE_COMPONENTS, artifact_root=artifact_root) is None
    assert cache.refusals


def test_short_circuit_is_disabled_by_environment(monkeypatch) -> None:
    from openbfme_importer.faction_coverage_cache import coverage_cache_disabled

    monkeypatch.delenv("OPENBFME_NO_COVERAGE_SHORTCIRCUIT", raising=False)
    monkeypatch.delenv("OPENBFME_NO_OBJECT_CACHE", raising=False)
    assert coverage_cache_disabled() is False
    monkeypatch.setenv("OPENBFME_NO_COVERAGE_SHORTCIRCUIT", "1")
    assert coverage_cache_disabled() is True


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
