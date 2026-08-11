"""Tests for per-object faction convert cache and parallel convert."""

from __future__ import annotations

import json
from pathlib import Path
from unittest import mock

import pytest

from openbfme_importer.faction_import import build_faction_conversion
from openbfme_importer.faction_object_cache import (
    _COMPILER_SALT_MODULES,
    CACHE_SCHEMA,
    FactionObjectCache,
    documents_fingerprint,
    durable_effective_assets_fingerprint,
    durable_non_ini_assets_fingerprint,
    object_cache_key,
    policy_roots_fingerprint,
)
from importer.tests.test_faction_import import (
    _fixture,
    _structure_success_patches,
    _unit_conversion_patches,
)


def test_documents_fingerprint_is_order_independent() -> None:
    a = {"b.ini": b"two", "a.ini": b"one"}
    b = {"a.ini": b"one", "b.ini": b"two"}
    assert documents_fingerprint(a) == documents_fingerprint(b)
    assert documents_fingerprint(a) != documents_fingerprint({"a.ini": b"changed"})


def test_compiler_salt_covers_shared_armor_upgrade_compiler() -> None:
    assert "armor_compiler.py" in _COMPILER_SALT_MODULES


def test_object_cache_key_changes_with_inputs() -> None:
    base = dict(
        family="unit",
        object_id="HeroSeven",
        documents_fp="d" * 64,
        catalog_identity_sha256="c" * 64,
        effective_root_fp="manifest-agg:" + "e" * 64,
        graph_input_set_sha256="g" * 64,
        plan_aggregate_sha256="p" * 64,
        policy_fp="o" * 64,
        compiler_token="t" * 64,
    )
    k1 = object_cache_key(**base)
    k2 = object_cache_key(**{**base, "object_id": "HeroEight"})
    k3 = object_cache_key(**{**base, "extra": {"plan_status": "descriptor-ready"}})
    k4 = object_cache_key(**{**base, "graph_input_set_sha256": "h" * 64})
    k5 = object_cache_key(**{**base, "compiler_token": "u" * 64})
    k6 = object_cache_key(**{**base, "policy_fp": "q" * 64})
    assert len(k1) == 64
    assert k1 != k2
    assert k1 != k3
    assert k1 != k4
    assert k1 != k5
    assert k1 != k6
    assert object_cache_key(**base) == k1


def test_durable_assets_fp_uses_manifest_aggregate(tmp_path: Path) -> None:
    root = tmp_path / "assets"
    meta = root / ".openbfme"
    meta.mkdir(parents=True)
    aggregate = "a" * 64
    (meta / "manifest.json").write_text(
        json.dumps(
            {
                "schema": "openbfme.effective-assets-manifest",
                "aggregate_sha256": aggregate,
                "files": [],
            }
        ),
        encoding="utf-8",
    )
    fp = durable_effective_assets_fingerprint(root)
    assert fp == f"manifest-agg:{aggregate}"
    # Absolute path must not appear (shared-cache portable).
    assert str(root).casefold() not in fp.casefold()
    # Manifest-less roots fall back to a tree-inventory fingerprint: distinct
    # trees must never share one identity bucket under a shared DDC root.
    missing_a = durable_effective_assets_fingerprint(tmp_path / "missing")
    assert missing_a.startswith("tree-inventory:")
    assert str(tmp_path).casefold() not in missing_a.casefold()
    assert durable_effective_assets_fingerprint(tmp_path / "missing") == missing_a
    other = tmp_path / "other"
    other.mkdir()
    (other / "dummy.bin").write_bytes(b"dummy")
    other_fp = durable_effective_assets_fingerprint(other)
    assert other_fp.startswith("tree-inventory:")
    assert other_fp != missing_a


def test_non_ini_assets_fp_ignores_ini_rows_but_keeps_visual_bytes(tmp_path: Path) -> None:
    root = tmp_path / "assets"
    meta = root / ".openbfme"
    meta.mkdir(parents=True)

    def write_manifest(ini_hash: str, model_hash: str) -> None:
        (meta / "manifest.json").write_text(
            json.dumps(
                {
                    "files": [
                        {
                            "path": "data/ini/object/men.ini",
                            "size": 10,
                            "sha256": ini_hash,
                        },
                        {
                            "path": "art/model.w3d",
                            "size": 20,
                            "sha256": model_hash,
                        },
                    ]
                }
            ),
            encoding="utf-8",
        )

    write_manifest("1" * 64, "2" * 64)
    before = durable_non_ini_assets_fingerprint(root)
    write_manifest("3" * 64, "2" * 64)
    assert durable_non_ini_assets_fingerprint(root) == before
    write_manifest("3" * 64, "4" * 64)
    assert durable_non_ini_assets_fingerprint(root) != before


def test_policy_roots_fingerprint_is_order_independent() -> None:
    a = policy_roots_fingerprint(
        spawned=("B", "A"), wall_templates=("W",), source_null_sets=()
    )
    b = policy_roots_fingerprint(
        spawned=("A", "B"), wall_templates=("W",), source_null_sets=()
    )
    assert a == b
    assert a != policy_roots_fingerprint(spawned=("A",), wall_templates=("W",))
    assert a != policy_roots_fingerprint(
        spawned=("A", "B"),
        spawned_roles={"A": "fortress-composite-citadel"},
        wall_templates=("W",),
    )


def test_faction_object_cache_roundtrip(tmp_path: Path) -> None:
    cache = FactionObjectCache(tmp_path / "faction-objects")
    key = "a" * 64
    row = {"id": "HeroSeven", "status": "converted", "family": "unit"}
    artifacts = {
        "descriptor": {"descriptorSha256": "1" * 64},
        "pack-recipe": {"recipeSha256": "2" * 64},
        "visual-closure": {"aggregateSha256": "9" * 64, "huge": "x" * 100},
    }
    assert cache.get(key) is None
    cache.put(key, row=row, artifacts=artifacts)
    hit = cache.get(key)
    assert hit is not None
    assert hit["row"]["id"] == "HeroSeven"
    assert hit["artifacts"]["descriptor"]["descriptorSha256"] == "1" * 64
    # visual-closure must not bloat durable DDC
    assert "visual-closure" not in hit["artifacts"]

    # Corrupt / schema mismatch → miss
    bad = cache._entry_dir(key) / "artifacts.json"
    bad.write_text('{"schema":"wrong"}', encoding="utf-8")
    assert cache.get(key) is None


def test_unexpected_convert_exception_becomes_gap_not_batch_abort(
    tmp_path: Path,
) -> None:
    documents, graph = _fixture()
    unit_patches = _unit_conversion_patches()
    structure_patches = _structure_success_patches()

    def _closure(_root: object, targets: object) -> dict[str, object]:
        # Only trip one unit so the batch must continue for siblings.
        names = [str(t) for t in (targets or [])]
        if any(name.casefold() == "heroseven" for name in names):
            raise RuntimeError("boom-closure")
        return {"aggregateSha256": "8" * 64}

    with (
        unit_patches[1],
        unit_patches[2],
        structure_patches[0],
        structure_patches[1],
        structure_patches[2],
        structure_patches[3],
        mock.patch(
                "openbfme_importer.faction_import.durable_non_ini_assets_fingerprint",
                return_value="non-ini-manifest:" + "a" * 64,
        ),
        mock.patch(
            "openbfme_importer.faction_import.build_retail_visual_closure",
            side_effect=_closure,
        ),
    ):
        coverage = build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
            state_root=tmp_path,
            convert_jobs=4,
        )
    rows = {row["id"]: row for row in coverage["objects"]}
    assert rows["HeroSeven"]["status"] == "converter-gap"
    assert "unexpected convert error" in rows["HeroSeven"]["reason"]
    assert rows["HeroEight"]["status"] == "converted"
    assert rows["UniversalFactory"]["status"] == "converted"
    assert coverage["summary"]["objectCount"] == 3


def test_conversion_cache_hit_skips_recompile(tmp_path: Path) -> None:
    documents, graph = _fixture()
    unit_patches = _unit_conversion_patches()
    structure_patches = _structure_success_patches()
    pack_recipe = mock.Mock(return_value={"recipeSha256": "9" * 64, "resources": [{}]})
    structure_recipe = mock.Mock(
        return_value={"recipeSha256": "6" * 64, "resources": [{}, {}]}
    )
    with (
        unit_patches[0],
        unit_patches[2],
        structure_patches[0],
        structure_patches[2],
        structure_patches[3],
        mock.patch(
            "openbfme_importer.faction_import.compile_playable_unit_pack_recipe",
            pack_recipe,
        ),
        mock.patch(
            "openbfme_importer.faction_import.compile_structure_visual_recipe",
            structure_recipe,
        ),
        mock.patch(
                "openbfme_importer.faction_import.durable_non_ini_assets_fingerprint",
                return_value="non-ini-manifest:" + "a" * 64,
        ),
    ):
        first = build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
            state_root=tmp_path,
            convert_jobs=1,
        )
        pack_after_first = pack_recipe.call_count
        structure_after_first = structure_recipe.call_count
        second = build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
            state_root=tmp_path,
            convert_jobs=1,
        )

    assert first["summary"]["convertedCount"] == 3
    assert first["summary"]["cacheHits"] == 0
    # All three converted objects should cache-hit on the second pass.
    assert second["summary"]["cacheHits"] == 3
    # Convert-phase recipe compilers must not re-run on cache hits.
    # (Plan phase still recompiles descriptors — that is intentional.)
    assert pack_recipe.call_count == pack_after_first
    assert structure_recipe.call_count == structure_after_first
    hits = [row for row in second["objects"] if row.get("cacheHit")]
    assert len(hits) == 3
    assert all(row["status"] == "converted" for row in hits)
    # Cold vs warm aggregate must stay identity-stable for profile binding.
    assert first["aggregateSha256"] == second["aggregateSha256"]


def test_parallel_convert_preserves_plan_order_and_aggregate(tmp_path: Path) -> None:
    documents, graph = _fixture()
    unit_patches = _unit_conversion_patches()
    structure_patches = _structure_success_patches()
    with (
        unit_patches[0],
        unit_patches[1],
        unit_patches[2],
        structure_patches[0],
        structure_patches[1],
        structure_patches[2],
        structure_patches[3],
        mock.patch(
                "openbfme_importer.faction_import.durable_non_ini_assets_fingerprint",
                return_value="non-ini-manifest:" + "a" * 64,
        ),
    ):
        serial = build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
            state_root=tmp_path / "serial",
            convert_jobs=1,
        )
        parallel = build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
            state_root=tmp_path / "parallel",
            convert_jobs=4,
        )

    assert [row["id"] for row in serial["objects"]] == [
        row["id"] for row in parallel["objects"]
    ]
    assert serial["aggregateSha256"] == parallel["aggregateSha256"]
    assert parallel["summary"]["convertWorkers"] == 4


def test_object_cache_disabled_without_state_root(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    monkeypatch.delenv("OPENBFME_IMPORT_ROOT", raising=False)
    monkeypatch.delenv("OPENBFME_SHARED_CACHE", raising=False)
    monkeypatch.delenv("OPENBFME_NO_OBJECT_CACHE", raising=False)
    documents, graph = _fixture()
    unit_patches = _unit_conversion_patches()
    structure_patches = _structure_success_patches()
    with (
        unit_patches[0],
        unit_patches[1],
        unit_patches[2],
        structure_patches[0],
        structure_patches[1],
        structure_patches[2],
        structure_patches[3],
        mock.patch(
                "openbfme_importer.faction_import.durable_non_ini_assets_fingerprint",
                return_value="non-ini-manifest:" + "a" * 64,
        ),
    ):
        coverage = build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
            convert_jobs=1,
        )
    assert coverage["summary"]["cacheHits"] == 0


def test_whole_graph_identity_change_does_not_evict_unchanged_objects(
    tmp_path: Path,
) -> None:
    documents, graph = _fixture()
    unit_patches = _unit_conversion_patches()
    structure_patches = _structure_success_patches()
    pack_recipe = mock.Mock(return_value={"recipeSha256": "9" * 64, "resources": [{}]})
    with (
        unit_patches[0],
        unit_patches[2],
        structure_patches[0],
        structure_patches[1],
        structure_patches[2],
        structure_patches[3],
        mock.patch(
            "openbfme_importer.faction_import.compile_playable_unit_pack_recipe",
            pack_recipe,
        ),
        mock.patch(
                "openbfme_importer.faction_import.durable_non_ini_assets_fingerprint",
                return_value="non-ini-manifest:" + "a" * 64,
        ),
    ):
        first = build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
            state_root=tmp_path,
            convert_jobs=1,
        )
        graph2 = dict(graph)
        graph2["inputSetSha256"] = "9" * 64
        # Keep plan happy: plan validates graph identity format only.
        second = build_faction_conversion(
            graph2,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
            state_root=tmp_path,
            convert_jobs=1,
        )
    assert first["summary"]["cacheHits"] == 0
    # The plan descriptor + source closure are unchanged, so a broad census
    # aggregate change cannot evict every independent object.
    assert second["summary"]["cacheHits"] == 3


def test_shared_cache_env_routes_object_cache(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    shared = tmp_path / "shared-ddc"
    monkeypatch.setenv("OPENBFME_SHARED_CACHE", str(shared))
    documents, graph = _fixture()
    unit_patches = _unit_conversion_patches()
    structure_patches = _structure_success_patches()
    with (
        unit_patches[0],
        unit_patches[1],
        unit_patches[2],
        structure_patches[0],
        structure_patches[1],
        structure_patches[2],
        structure_patches[3],
        mock.patch(
                "openbfme_importer.faction_import.durable_non_ini_assets_fingerprint",
                return_value="non-ini-manifest:" + "a" * 64,
        ),
    ):
        build_faction_conversion(
            graph,
            documents,
            Path("unused-effective-root"),
            catalog_identity_sha256="2" * 64,
            convert_jobs=1,
        )
    assert (shared / "faction-objects").is_dir()
    # At least one cache entry written for converted objects.
    entries = list((shared / "faction-objects").rglob("artifacts.json"))
    assert entries
    payload = entries[0].read_text(encoding="utf-8")
    assert CACHE_SCHEMA in payload
