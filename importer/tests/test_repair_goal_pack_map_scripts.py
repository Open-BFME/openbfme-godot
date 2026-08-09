from __future__ import annotations

import json
from pathlib import Path

import pytest

from tools.repair_goal_pack_map_scripts import (
    AI_LIBRARY_VIRTUAL_PATHS,
    RepairRefusal,
    compose_pack_scripts_document,
    decode_library_documents,
    plan_map_id_migration,
    plan_map_source_backfill,
    resolve_selected_pack,
    write_composed_document,
)

ROOT = Path(__file__).resolve().parents[2]
CATALOG = ROOT / ".private/retail-work/catalog/rotwk.json"
ORACLE_PACK = (
    ROOT
    / ".private/content-packs/rotwk-skirmish-maps-private"
    / "95d51e53fa194e1de097b46a25b38e03fbf7b8a3292e01973ff0e8c3a00be686"
)
REAL_MAP = "maps/map mp adorn river/map mp adorn river.map"


@pytest.mark.skipif(not CATALOG.is_file(), reason="RotWK catalog is not present")
def test_compose_and_place_produces_schema_v2_composite_for_known_map() -> None:
    from openbfme_importer.catalog import InstallCatalog

    catalog = InstallCatalog.load(CATALOG)
    stale = catalog.stale_reasons()
    if stale:
        pytest.skip(f"stale RotWK catalog: {stale}")

    libraries = decode_library_documents(catalog)
    assert [path for path, _document in libraries] == AI_LIBRARY_VIRTUAL_PATHS

    entry = catalog.resolve_exact(REAL_MAP)
    assert entry is not None
    archive = catalog.open_archive_for(entry)
    map_bytes = archive.read_entry(catalog.as_entry(entry), max_bytes=max(entry.size, 1))

    document, slug = compose_pack_scripts_document(
        map_bytes,
        [library_document for _path, library_document in libraries],
        map_virtual_path=REAL_MAP,
    )

    assert slug == "adorn-river"
    # Schema v2 composite shape, exactly what the pipeline's
    # _convert_script_composite_bundle emits (pipeline.py:5386-5506).
    assert document["schema"] == "openbfme.map-scripts"
    assert document["schemaVersion"] == 2
    assert document["source"]["container"] == "composite"
    assert document["source"]["game"] == "rotwk"
    assert document["source"]["map"]["virtualPath"] == REAL_MAP.casefold()
    assert [
        row["virtualPath"] for row in document["source"]["libraries"]
    ] == AI_LIBRARY_VIRTUAL_PATHS
    assert document["world"]["available"] is True
    assert len(document["scripts"]) > 0

    # Player-placeholder contract (sage_scripts.py:791-815) intact: both
    # libraries survive as templates with exactly one Player placeholder.
    templates = document["libraryTemplates"]
    assert len(templates) == 2
    for template in templates:
        assert template["instantiateFor"] == "aiPlayers"
        assert template["playerPlaceholder"] == "Player"
        placeholders = [
            row
            for row in template["world"]["players"]
            if row.get("name") == "Player"
        ]
        assert len(placeholders) == 1
        placeholder_index = placeholders[0]["index"]
        assert template["scripts"]
        assert all(
            script["playerIndex"] == placeholder_index
            for script in template["scripts"]
        )
        for team in template["world"].get("teams", []):
            assert team.get("owner") in ("", "Player")

    # Oracle: the 23-map pack cooked the SAME retail sources through the
    # pipeline lane; the repaired document must be identical.
    oracle_path = ORACLE_PACK / "maps" / "adorn-river" / "scripts.json"
    if oracle_path.is_file():
        oracle = json.loads(oracle_path.read_text(encoding="utf-8"))
        assert document == oracle


def _minimal_document() -> dict[str, object]:
    return {"schema": "openbfme.map-scripts", "schemaVersion": 2, "scripts": []}


def test_write_refuses_existing_scripts_json_without_force(tmp_path: Path) -> None:
    target = tmp_path / "maps" / "some-map" / "scripts.json"

    status = write_composed_document(target, _minimal_document(), force=False)
    assert status == "repaired"
    assert json.loads(target.read_text(encoding="utf-8")) == _minimal_document()

    # Re-running over an identical document is an idempotent skip, not a
    # refusal: nothing would change on disk.
    status = write_composed_document(target, _minimal_document(), force=False)
    assert status == "skipped-identical"

    different = _minimal_document()
    different["scripts"] = [{"name": "changed"}]
    with pytest.raises(RepairRefusal, match="already exists"):
        write_composed_document(target, different, force=False)
    # Refusal leaves the original bytes untouched.
    assert json.loads(target.read_text(encoding="utf-8")) == _minimal_document()

    status = write_composed_document(target, different, force=True)
    assert status == "repaired"
    assert json.loads(target.read_text(encoding="utf-8")) == different


def test_map_source_backfill_adds_runtime_required_identity(tmp_path: Path) -> None:
    map_path = tmp_path / "map.json"

    # The goal-pack shape: sha256 only.  Backfill must add exactly the two
    # keys the runtime install check reads (retail_map_data.gd:239-242).
    cooked = {"id": "rotwk.map.x", "source": {"packaged": False, "sha256": "ab" * 32}}
    updated = plan_map_source_backfill(
        cooked, virtual_path=REAL_MAP.upper(), source_bytes=717104, map_path=map_path
    )
    assert updated is not None
    assert updated["source"]["virtualPath"] == REAL_MAP.casefold()
    assert updated["source"]["sourceBytes"] == 717104
    assert updated["source"]["sha256"] == "ab" * 32
    # The input document is not mutated in place.
    assert "virtualPath" not in cooked["source"]

    # Already complete and agreeing: nothing to do.
    assert (
        plan_map_source_backfill(
            updated, virtual_path=REAL_MAP, source_bytes=717104, map_path=map_path
        )
        is None
    )

    # Conflicting provenance is a refusal, never an overwrite.
    with pytest.raises(RepairRefusal, match="conflicts"):
        plan_map_source_backfill(
            updated,
            virtual_path="maps/map mp other/map mp other.map",
            source_bytes=717104,
            map_path=map_path,
        )
    with pytest.raises(RepairRefusal, match="conflicts"):
        plan_map_source_backfill(
            updated, virtual_path=REAL_MAP, source_bytes=1, map_path=map_path
        )


class _EmptyCatalog:
    """A catalog stub that resolves nothing: every source is missing."""

    def resolve_exact(self, virtual_path: str) -> None:
        return None


def test_decode_libraries_refuses_missing_source() -> None:
    with pytest.raises(RepairRefusal, match="missing catalog entry"):
        decode_library_documents(_EmptyCatalog())


def test_compose_refuses_undecodable_map_source() -> None:
    with pytest.raises(RepairRefusal, match="undecodable map source"):
        compose_pack_scripts_document(
            b"not a sage map",
            [{}, {}],
            map_virtual_path=REAL_MAP,
        )


def test_resolve_selected_pack_reads_workspace_selection(tmp_path: Path) -> None:
    with pytest.raises(RepairRefusal, match="selection is missing"):
        resolve_selected_pack(tmp_path)

    selection = tmp_path / "selection.json"
    selection.write_text(
        json.dumps(
            {
                "activePack": "rotwk-men-vslice/aaaa",
                "supplementalPacks": ["rotwk-skirmish-maps-private/bbbb"],
            }
        ),
        encoding="utf-8",
    )
    # Selected pack directory must exist and carry pack.json.
    with pytest.raises(RepairRefusal, match="no pack.json"):
        resolve_selected_pack(tmp_path)

    pack_dir = tmp_path / "rotwk-skirmish-maps-private" / "bbbb"
    pack_dir.mkdir(parents=True)
    (pack_dir / "pack.json").write_text("{}", encoding="utf-8")
    assert resolve_selected_pack(tmp_path) == pack_dir

    # A name-addressed sibling (goal-official-72) must NOT be picked up:
    # resolution follows selection.json only.
    sibling = tmp_path / "rotwk-skirmish-maps-private" / "goal-official-72"
    sibling.mkdir()
    (sibling / "pack.json").write_text("{}", encoding="utf-8")
    assert resolve_selected_pack(tmp_path) == pack_dir

    # Zero or ambiguous selection entries are refusals, not guesses.
    selection.write_text(json.dumps({"activePack": "other/x"}), encoding="utf-8")
    with pytest.raises(RepairRefusal, match="expected exactly 1"):
        resolve_selected_pack(tmp_path)


def test_map_id_migration_maps_stale_mp_slug_to_canonical_runtime_id() -> None:
    """rotwk.map.mp-harlindon must migrate to rotwk.map.harlindon.

    The runtime installs a script composite only when the mounted row id
    equals ``<game>.map.<canonical runtime slug>``
    (retail_vertical_slice.gd:5027-5037 compares ``source_map_data.map_id``
    against ``active_game + ".map." + map_slug``), so a row that kept an older
    slug scheme ships scripts.json that is refused on every launch.
    """

    catalog_ids = ["rotwk.map.mp-harlindon", "rotwk.map.wor-harlindon"]
    assert (
        plan_map_id_migration(
            "rotwk.map.mp-harlindon", "harlindon", catalog_ids, game="rotwk"
        )
        == "rotwk.map.harlindon"
    )

    # Already canonical: nothing to migrate.
    assert (
        plan_map_id_migration(
            "rotwk.map.harlindon",
            "harlindon",
            ["rotwk.map.harlindon", "rotwk.map.wor-harlindon"],
            game="rotwk",
        )
        is None
    )


def test_map_id_migration_refuses_ambiguous_canonical_collision() -> None:
    """A catalog that ALREADY claims the canonical id is a refusal, not a merge.

    Migrating the stale row would leave two rows with the same id, which
    ContentDB._load_map_catalog rejects wholesale (content_db.gd:412 refuses
    duplicate pending ids), so the collision must fail the repair loudly.
    """

    with pytest.raises(RepairRefusal, match="already claim"):
        plan_map_id_migration(
            "rotwk.map.mp-harlindon",
            "harlindon",
            ["rotwk.map.mp-harlindon", "rotwk.map.harlindon"],
            game="rotwk",
        )


def test_compose_refuses_non_multiplayer_virtual_path() -> None:
    with pytest.raises(RepairRefusal, match="canonical multiplayer"):
        compose_pack_scripts_document(
            b"",
            [],
            map_virtual_path="maps/map wor harlindon/map wor harlindon.map",
        )
