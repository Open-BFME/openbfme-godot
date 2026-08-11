"""Per-projectile art compilation, pinned against the pure RotWK oracle.

The projectile *identity* lane already compiles
``registration.gameplay.simulation.resolved.combat.projectileObjectId`` for
every faction (``test_projectile_lane_oracle``).  This module pins the ART that
identity must resolve to, so a Mordor archer stops borrowing the Gondor
sidecar's Good arrow from a foreign pack and a RotWK-only mount has visible
arrows of its own.

Oracle: ``.private/retail-work/editions/rotwk/cache/effective-assets`` -- the
PURE effective-assets view, never ``layered-effective-assets``.
"""

from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.projectile_art_compiler import (
    PROJECTILE_ART_PACK_KEY,
    PROJECTILE_ART_RUNTIME_PATH,
    PROJECTILE_ART_SCHEMA,
    ProjectileArtCompilerError,
    collect_projectile_object_ids,
    compile_projectile_art,
)


ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = ROOT / ".private" / "retail-work"
if (
    not (PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets").is_dir()
    and ROOT.parent.name == "worktrees"
):
    PRIVATE_ROOT = ROOT.parents[2] / ".private" / "retail-work"
EFFECTIVE_ASSETS = PRIVATE_ROOT / "editions" / "rotwk" / "cache" / "effective-assets"

private_sources = pytest.mark.skipif(
    not (EFFECTIVE_ASSETS / "data" / "ini" / "weapon.ini").is_file(),
    reason="private RotWK effective-assets oracle is not present",
)

#: The three arrow projectile object ids every faction's archers compile to
#: (see ``test_projectile_lane_oracle.ARCHER_HORDES``).
ARROW_PROJECTILE_IDS = ("EvilFactionArrow", "GondorArcherArrow", "GoodFactionArrow")


def _entry(runtime: dict, projectile_object_id: str) -> dict:
    rows = [
        row
        for row in runtime["projectiles"]
        if row["projectileObjectId"] == projectile_object_id
    ]
    assert len(rows) == 1, f"expected exactly one {projectile_object_id} row"
    return rows[0]


def _streak(entry: dict) -> dict:
    rows = [row for row in entry["draws"] if row["kind"] == "W3DStreakDraw"]
    assert len(rows) == 1, f"expected one W3DStreakDraw on {entry['projectileObjectId']}"
    return rows[0]


def test_collect_projectile_object_ids_is_data_driven() -> None:
    """Ids come from compiled runtime documents, never a hardcoded list."""

    runtime_data = {
        "data/playable-units/a.json": {
            "registration": {
                "gameplay": {
                    "simulation": {
                        "resolved": {"combat": {"projectileObjectId": "EvilFactionArrow"}}
                    }
                }
            }
        },
        "data/playable-units/b.json": {
            "registration": {
                "gameplay": {
                    "simulation": {
                        "resolved": {"combat": {"projectileObjectId": "EvilFactionArrow"}}
                    }
                }
            }
        },
        "data/playable-structures/c.json": {
            "registration": {
                "gameplay": {
                    "combat": {"projectileObjectId": "MordorCatapultRockProjectile"}
                }
            }
        },
        "data/strings.json": {"schema": "openbfme.pack-strings"},
    }
    assert collect_projectile_object_ids(runtime_data) == [
        "EvilFactionArrow",
        "MordorCatapultRockProjectile",
    ]


@private_sources
def test_arrow_projectiles_compile_their_own_retail_streak_art() -> None:
    result = compile_projectile_art(ARROW_PROJECTILE_IDS, EFFECTIVE_ASSETS)
    runtime = result["runtime"]
    assert runtime["schema"] == PROJECTILE_ART_SCHEMA
    assert runtime["schemaVersion"] == 0
    assert [row["projectileObjectId"] for row in runtime["projectiles"]] == list(
        ARROW_PROJECTILE_IDS
    )
    for projectile_object_id in ARROW_PROJECTILE_IDS:
        streak = _streak(_entry(runtime, projectile_object_id))
        assert streak["texture"] == "assets/textures/projectiles/exarrowstreak01.png"
        assert streak["weatherTextures"] == {
            "SNOWY": "assets/textures/projectiles/exarrowstreak_snow.png"
        }
        assert streak["length"] == 15
        assert streak["width"] == 2
        assert streak["numSegments"] == 1
        assert streak["additive"] is False
        assert streak["color"] == {"r": 255, "g": 255, "b": 255}


@private_sources
def test_evil_arrow_art_is_authored_by_the_evil_faction_source() -> None:
    """Identity proof: the evil arrow may not be sourced from the good file."""

    runtime = compile_projectile_art(ARROW_PROJECTILE_IDS, EFFECTIVE_ASSETS)["runtime"]
    evil = _streak(_entry(runtime, "EvilFactionArrow"))["source"]
    assert evil["definingObject"] == "EvilFactionArrow"
    assert evil["virtualPath"] == "data/ini/object/evilfaction/evilfactionsubobjects.ini"
    assert evil["line"] > 0
    for good_id in ("GondorArcherArrow", "GoodFactionArrow"):
        good = _streak(_entry(runtime, good_id))["source"]
        assert good["definingObject"] == good_id
        assert (
            good["virtualPath"]
            == "data/ini/object/goodfaction/goodfactionsubobjects.ini"
        )


@private_sources
def test_streak_textures_are_declared_as_convertible_resources() -> None:
    result = compile_projectile_art(ARROW_PROJECTILE_IDS, EFFECTIVE_ASSETS)
    rows = [row for row in result["resources"] if row["kind"] == "texture"]
    assert len(rows) == 1
    row = rows[0]
    assert row["converter"] == "texture"
    assert row["output"] == "assets/textures/projectiles/{stem}.png"
    assert sorted(row["patterns"]) == [
        "art/compiledtextures/ex/exarrowstreak01.dds",
        "art/compiledtextures/ex/exarrowstreak_snow.dds",
    ]
    assert row["expected_count"] == len(row["patterns"])
    assert row["limit"] == len(row["patterns"])
    assert row["required"] is True
    assert result["packFileKey"] == PROJECTILE_ART_PACK_KEY
    assert result["runtimePath"] == PROJECTILE_ART_RUNTIME_PATH


@private_sources
def test_model_projectiles_are_reported_as_a_named_gap_not_dropped() -> None:
    """A rock projectile's authored model is recorded, never silently lost."""

    result = compile_projectile_art(
        ("GondorTrebuchetRockProjectile",), EFFECTIVE_ASSETS
    )
    runtime = result["runtime"]
    entry = _entry(runtime, "GondorTrebuchetRockProjectile")
    models = [row for row in entry["draws"] if row["kind"].casefold().endswith("modeldraw")]
    assert any(row.get("model") == "GUSiegTreRk" for row in models)
    assert all(row.get("modelAsset") is None for row in models)
    assert runtime["unconvertedModelProjectileObjectIds"] == [
        "GondorTrebuchetRockProjectile"
    ]


@private_sources
def test_arrow_projectiles_report_no_model_gap() -> None:
    runtime = compile_projectile_art(ARROW_PROJECTILE_IDS, EFFECTIVE_ASSETS)["runtime"]
    assert runtime["unconvertedModelProjectileObjectIds"] == []


@private_sources
def test_silverthorn_layered_streaks_and_unauthored_additive() -> None:
    """Retail does not author every W3DStreakDraw field, and layers streaks.

    The Mirkwood silverthorn is the corpus's counter-example to both
    assumptions this compiler started with: it authors TWO W3DStreakDraw
    modules on one object, and omits ``Additive`` on both. A strict
    exactly-one-value read of ``Additive`` fails closed here -- which would
    abort an elves publish, since this is a compiled elves projectile.
    """

    runtime = compile_projectile_art(
        ("MirkwoodArcherSilverthornProjectile",), EFFECTIVE_ASSETS
    )["runtime"]
    entry = _entry(runtime, "MirkwoodArcherSilverthornProjectile")
    streaks = [row for row in entry["draws"] if row["kind"] == "W3DStreakDraw"]
    assert len(streaks) == 2
    # Both layers are compiled with their own authored geometry and texture.
    assert [row["texture"] for row in streaks] == [
        "assets/textures/projectiles/exarrowstreakfire.png",
        "assets/textures/projectiles/exlightstreaks2.png",
    ]
    assert [(row["length"], row["width"], row["numSegments"]) for row in streaks] == [
        (15, 3, 6),
        (50, 1, 6),
    ]
    assert all(row["color"] == {"r": 0, "g": 128, "b": 255} for row in streaks)
    for row in streaks:
        # Additive is NOT authored here; the value carried is the engine
        # default recorded in retail's own GoodFactionArrow comment
        # ("Additive = No ; Yes by default"), and authoredFields says so.
        assert "Additive" not in row["authoredFields"]
        assert row["additive"] is True
        assert sorted(row["authoredFields"]) == ["Color", "Length", "NumSegments", "Width"]


@private_sources
def test_authored_additive_is_reported_as_authored() -> None:
    """Control for the test above: the arrow DOES author Additive = No."""

    runtime = compile_projectile_art(("GoodFactionArrow",), EFFECTIVE_ASSETS)["runtime"]
    streak = _streak(_entry(runtime, "GoodFactionArrow"))
    assert "Additive" in streak["authoredFields"]
    assert streak["additive"] is False


@private_sources
def test_unknown_projectile_object_id_fails_closed() -> None:
    with pytest.raises(ProjectileArtCompilerError):
        compile_projectile_art(("NotARetailProjectile",), EFFECTIVE_ASSETS)


# --- compose wiring ---------------------------------------------------------
# The oracle tests above prove the ART is compiled; these prove a composed pack
# actually SHIPS it, keyed so any mounted pack can answer for its own arrows.

import hashlib
import json
from copy import deepcopy

from openbfme_importer.faction_slice_profile import compose_faction_profile
from openbfme_importer.retail_fords_completion_profile import (
    MEN_SELECTION_PACK_KEY,
    MEN_SELECTION_RESOURCES,
    MEN_SELECTION_RUNTIME,
    MEN_SELECTION_RUNTIME_PATH,
)


def _digest(value: object) -> str:
    return hashlib.sha256(
        json.dumps(value, sort_keys=True, separators=(",", ":")).encode()
    ).hexdigest()


def _write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def _archer_coverage(root: Path, faction: str, projectile_object_id: str) -> None:
    unit_recipe = {
        "objectId": "FactionArcher",
        "category": "infantry",
        "descriptorSha256": "a" * 64,
        "recipeSha256": "b" * 64,
        "resources": [],
        "runtimeRegistration": {
            "production": [],
            "gameplay": {
                "simulation": {
                    "resolved": {"combat": {"projectileObjectId": projectile_object_id}}
                }
            },
        },
    }
    coverage = {
        "schema": "openbfme.faction-import-coverage",
        "schemaVersion": 0,
        "objects": [
            {
                "id": "FactionArcher",
                "family": "playable-unit",
                "status": "converted",
                "recipeSha256": "b" * 64,
            }
        ],
        "summary": {
            "convertedCount": 1,
            "converterGapCount": 0,
            "conversionComplete": True,
        },
    }
    coverage["aggregateSha256"] = _digest(coverage)
    _write_json(root / f"{faction}-coverage.json", coverage)
    _write_json(root / f"{faction}/objects/factionarcher/pack-recipe.json", unit_recipe)


def _art_result(projectile_object_id: str) -> dict:
    runtime = {
        "schema": PROJECTILE_ART_SCHEMA,
        "schemaVersion": 0,
        "projectiles": [
            {
                "projectileObjectId": projectile_object_id,
                "objectKind": "Object",
                "source": {"virtualPath": "data/ini/object/x.ini", "line": 1},
                "draws": [
                    {
                        "kind": "W3DStreakDraw",
                        "instanceTag": "ModuleTag_Draw2",
                        "texture": "assets/textures/projectiles/exarrowstreak01.png",
                        "weatherTextures": {},
                        "length": 15,
                        "width": 2,
                        "numSegments": 1,
                        "additive": False,
                        "color": {"r": 255, "g": 255, "b": 255},
                        "source": {
                            "definingObject": projectile_object_id,
                            "virtualPath": "data/ini/object/x.ini",
                            "line": 5,
                        },
                    }
                ],
            }
        ],
        "unconvertedModelProjectileObjectIds": [],
        "unsupportedDrawModules": [],
        "sourceClosureSha256": "d" * 64,
    }
    runtime["runtimeSha256"] = _digest(runtime)
    return {
        "runtime": runtime,
        "resources": [
            {
                "id": "projectile-art-textures",
                "kind": "texture",
                "converter": "texture",
                "patterns": ["art/compiledtextures/ex/exarrowstreak01.dds"],
                "output": "assets/textures/projectiles/{stem}.png",
                "required": True,
                "limit": 1,
                "expected_count": 1,
            }
        ],
        "runtimePath": PROJECTILE_ART_RUNTIME_PATH,
        "packFileKey": PROJECTILE_ART_PACK_KEY,
        "summary": {},
    }


def _lean_base() -> dict:
    return {
        "format": 1,
        "id": "base",
        "title": "base",
        "pack": {
            "id": "bfme2-men-vslice",
            "version": "test",
            "files": {MEN_SELECTION_PACK_KEY: MEN_SELECTION_RUNTIME_PATH},
        },
        "resources": [deepcopy(row) for row in MEN_SELECTION_RESOURCES],
        "runtime_data": {MEN_SELECTION_RUNTIME_PATH: deepcopy(MEN_SELECTION_RUNTIME)},
    }


def test_compose_ships_projectile_art_for_the_ids_its_units_resolve(
    tmp_path: Path,
) -> None:
    _archer_coverage(tmp_path, "angmar", "EvilFactionArrow")
    seen: list[list[str]] = []

    def builder(ids):
        seen.append(list(ids))
        return _art_result("EvilFactionArrow")

    target, receipt = compose_faction_profile(
        _lean_base(),
        tmp_path,
        ["angmar"],
        game="rotwk",
        projectile_art_builder=builder,
    )
    # Data-driven: the builder is asked for exactly the compiled id.
    assert seen == [["EvilFactionArrow"]]
    # ... and the lean expansion filter does not strip the emission.
    assert target["pack"]["files"][PROJECTILE_ART_PACK_KEY] == PROJECTILE_ART_RUNTIME_PATH
    document = target["runtime_data"][PROJECTILE_ART_RUNTIME_PATH]
    assert document["projectiles"][0]["projectileObjectId"] == "EvilFactionArrow"
    assert "projectile-art-textures" in [row["id"] for row in target["resources"]]
    assert receipt["projectileArt"]["projectileObjectIds"] == ["EvilFactionArrow"]


def test_compose_without_a_builder_is_byte_identical(tmp_path: Path) -> None:
    _archer_coverage(tmp_path, "angmar", "EvilFactionArrow")
    without, _ = compose_faction_profile(
        _lean_base(), tmp_path, ["angmar"], game="rotwk"
    )
    assert PROJECTILE_ART_PACK_KEY not in without["pack"]["files"]
    assert PROJECTILE_ART_RUNTIME_PATH not in without["runtime_data"]


def test_compose_rejects_a_tampered_projectile_art_document(tmp_path: Path) -> None:
    _archer_coverage(tmp_path, "angmar", "EvilFactionArrow")

    def builder(ids):
        result = _art_result("EvilFactionArrow")
        result["runtime"]["projectiles"][0]["projectileObjectId"] = "GoodFactionArrow"
        return result

    with pytest.raises(ValueError, match="projectile art document is invalid"):
        compose_faction_profile(
            _lean_base(),
            tmp_path,
            ["angmar"],
            game="rotwk",
            projectile_art_builder=builder,
        )
