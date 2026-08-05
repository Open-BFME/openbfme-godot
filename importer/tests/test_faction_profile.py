from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path

import pytest

import openbfme_importer.faction_slice_profile as subject
from openbfme_importer.faction_slice_profile import compose_faction_profile


def _digest(value: object) -> str:
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":")).encode()).hexdigest()


def _base() -> dict[str, object]:
    return {
        "format": 1, "id": "base", "title": "base",
        "pack": {"id": "bfme2-men-vslice", "version": "test", "files": {}},
        "resources": [], "runtime_data": {},
    }


def _resource(identifier: str) -> dict[str, object]:
    return {
        "id": identifier, "kind": "model", "converter": "copy",
        "patterns": [f"art/{identifier}.w3d"], "output": f"assets/{identifier}.w3d",
        "options": {}, "required": True, "limit": 1, "expected_count": 1,
    }


def _write_json(path: Path, value: object) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value), encoding="utf-8")


def _coverage(root: Path, *, tamper: bool = False) -> None:
    unit_recipe = {
        "objectId": "ElvenArcher", "category": "infantry",
        "descriptorSha256": "a" * 64, "recipeSha256": "b" * 64,
        "resources": [_resource("unit-elvenarcher")],
        "runtimeRegistration": {"production": []},
    }
    structure_recipe = {
        "objectId": "ElvenFortress", "recipeSha256": "c" * 64,
        "resources": [_resource("structure-elvenfortress")],
    }
    runtime = {
        "schema": "openbfme.playable-structure-runtime", "schemaVersion": 0,
        "objectId": "ElvenFortress", "slug": "elvenfortress",
        "recipeSha256": "c" * 64, "registration": {},
    }
    runtime["runtimeSha256"] = _digest(runtime)
    rows = [
        {"id": "ElvenArcher", "family": "playable-unit", "status": "converted", "recipeSha256": "b" * 64},
        {"id": "ElvenFortress", "family": "structure", "status": "converted", "recipeSha256": "c" * 64, "runtimeSha256": runtime["runtimeSha256"]},
        {"id": "ElvenPorter", "family": "playable-unit", "status": "converter-gap", "reason": "missing stats"},
    ]
    coverage = {
        "schema": "openbfme.faction-import-coverage", "schemaVersion": 0,
        "objects": rows,
        "summary": {"convertedCount": 2, "converterGapCount": 1, "conversionComplete": False},
    }
    coverage["aggregateSha256"] = _digest(coverage)
    if tamper:
        coverage["summary"]["convertedCount"] = 3
    _write_json(root / "elves-coverage.json", coverage)
    _write_json(root / "elves/objects/elvenarcher/pack-recipe.json", unit_recipe)
    _write_json(root / "elves/objects/elvenfortress/pack-recipe.json", structure_recipe)
    _write_json(root / "elves/objects/elvenfortress/runtime.json", runtime)


def test_composes_only_coverage_approved_artifacts(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    _coverage(tmp_path)
    monkeypatch.setattr(subject, "validate_structure_visual_recipe", lambda recipe: None)
    base = _base()
    target, receipt = compose_faction_profile(base, tmp_path, ["elves"])
    assert base == _base()
    assert [row["id"] for row in target["resources"]] == ["unit-elvenarcher", "structure-elvenfortress"]
    assert sorted(target["pack"]["files"]) == ["playableStructure.elvenfortress", "playableUnit.elvenarcher"]
    assert receipt["factions"] == [{
        "faction": "elves", "coverageAggregateSha256": json.loads((tmp_path / "elves-coverage.json").read_text())["aggregateSha256"],
        "convertedCount": 2, "converterGapCount": 1, "conversionComplete": False,
    }]
    assert target["pack"]["full_faction_complete"] is False


def _faction_coverage(root: Path, faction: str) -> None:
    """Write a minimal single-unit converted coverage set for ``faction``."""
    unit_recipe = {
        "objectId": "ElvenArcher", "category": "infantry",
        "descriptorSha256": "a" * 64, "recipeSha256": "b" * 64,
        "resources": [_resource("unit-elvenarcher")],
        "runtimeRegistration": {"production": []},
    }
    rows = [
        {"id": "ElvenArcher", "family": "playable-unit", "status": "converted", "recipeSha256": "b" * 64},
    ]
    coverage = {
        "schema": "openbfme.faction-import-coverage", "schemaVersion": 0,
        "objects": rows,
        "summary": {"convertedCount": 1, "converterGapCount": 0, "conversionComplete": True},
    }
    coverage["aggregateSha256"] = _digest(coverage)
    _write_json(root / f"{faction}-coverage.json", coverage)
    _write_json(root / f"{faction}/objects/elvenarcher/pack-recipe.json", unit_recipe)


def test_pack_id_is_derived_per_faction(tmp_path: Path) -> None:
    # Baseline: capture the Men composed pack id before asserting non-Men lanes,
    # to prove the Men path is byte-for-byte unchanged by the fix.
    _faction_coverage(tmp_path, "men")
    men_target, _ = compose_faction_profile(_base(), tmp_path, ["men"])
    assert men_target["pack"]["id"] == "bfme2-men-vslice"
    assert _base()["pack"]["id"] == "bfme2-men-vslice"  # base still Men-inherited

    for faction in ("dwarves", "isengard"):
        _faction_coverage(tmp_path, faction)
        target, _ = compose_faction_profile(_base(), tmp_path, [faction])
        # Non-Men publish must NOT inherit the base's bfme2-men-vslice id.
        assert target["pack"]["id"] == f"bfme2-{faction}-vslice"


def test_banner_carrier_composes_through_playable_unit_lane(tmp_path: Path) -> None:
    _faction_coverage(tmp_path, "men")
    coverage_path = tmp_path / "men-coverage.json"
    coverage = json.loads(coverage_path.read_text())
    coverage["objects"][0]["family"] = "banner-carrier"
    coverage["aggregateSha256"] = _digest(
        {key: value for key, value in coverage.items() if key != "aggregateSha256"}
    )
    _write_json(coverage_path, coverage)

    target, receipt = compose_faction_profile(_base(), tmp_path, ["men"])

    assert [row["id"] for row in target["resources"]] == ["unit-elvenarcher"]
    assert receipt["objects"][0]["family"] == "banner-carrier"


def test_rejects_unknown_faction(tmp_path: Path) -> None:
    with pytest.raises(ValueError, match="unknown faction"):
        compose_faction_profile(_base(), tmp_path, ["mirkwood"])


def test_angmar_is_a_registered_rotwk_faction(tmp_path: Path) -> None:
    # Angmar is a known composable faction: it must pass the known-faction guard
    # and fail later on the missing coverage artifact, not up front as unknown.
    from openbfme_importer.faction_slice_profile import _KNOWN_FACTIONS

    assert "angmar" in _KNOWN_FACTIONS
    with pytest.raises(ValueError, match="faction coverage"):
        compose_faction_profile(_base(), tmp_path, ["angmar"], game="rotwk")


def _base_with_selection_contract() -> dict[str, object]:
    """Base profile carrying the universal selection-decal contract plus one
    host-only artifact that the lean expansion filter must strip."""
    from openbfme_importer.retail_fords_completion_profile import (
        MEN_SELECTION_PACK_KEY,
        MEN_SELECTION_RESOURCES,
        MEN_SELECTION_RUNTIME,
        MEN_SELECTION_RUNTIME_PATH,
    )

    base = _base()
    base["resources"] = [deepcopy(row) for row in MEN_SELECTION_RESOURCES] + [_resource("host-only-unit")]
    base["runtime_data"] = {
        MEN_SELECTION_RUNTIME_PATH: deepcopy(MEN_SELECTION_RUNTIME),
        "data/host-only.json": {"host": True},
    }
    base["pack"]["files"] = {
        MEN_SELECTION_PACK_KEY: MEN_SELECTION_RUNTIME_PATH,
        "hostOnly": "data/host-only.json",
    }
    return base


def test_rotwk_faction_pack_id_is_game_prefixed(tmp_path: Path) -> None:
    # A RotWK faction publish lands under rotwk-<faction>-vslice, not bfme2-.
    _faction_coverage(tmp_path, "angmar")
    target, _ = compose_faction_profile(_base_with_selection_contract(), tmp_path, ["angmar"], game="rotwk")
    assert target["pack"]["id"] == "rotwk-angmar-vslice"


def test_rotwk_lean_pack_retains_universal_selection_decal_contract(tmp_path: Path) -> None:
    # The lean expansion filter strips host-only payload but must keep the
    # universal SHADOW_MERGE_DECAL selection contract so a solo-mounted Angmar
    # pack binds retail selection decals without borrowing from a host pack.
    _faction_coverage(tmp_path, "angmar")
    target, _ = compose_faction_profile(_base_with_selection_contract(), tmp_path, ["angmar"], game="rotwk")
    resource_ids = [row["id"] for row in target["resources"]]
    assert "men-selection-decal-level1" in resource_ids
    assert "men-selection-decal-good-co" in resource_ids
    assert "host-only-unit" not in resource_ids
    assert "effects/men-selection-decal.json" in target["runtime_data"]
    assert "data/host-only.json" not in target["runtime_data"]
    assert target["pack"]["files"]["menSelectionDecal"] == "effects/men-selection-decal.json"
    assert "hostOnly" not in target["pack"]["files"]


def test_rotwk_men_host_retains_living_world_without_duplicating_supplements(
    tmp_path: Path,
) -> None:
    base = _base_with_selection_contract()
    living_world = {"schema": "openbfme.living-world", "schemaVersion": 1}
    base["runtime_data"]["data/living-world.json"] = living_world
    base["pack"]["files"]["livingWorld"] = "data/living-world.json"

    _faction_coverage(tmp_path, "men")
    men, _ = compose_faction_profile(base, tmp_path, ["men"], game="rotwk")
    assert men["runtime_data"]["data/living-world.json"] == living_world
    assert men["pack"]["files"]["livingWorld"] == "data/living-world.json"

    _faction_coverage(tmp_path, "angmar")
    angmar, _ = compose_faction_profile(base, tmp_path, ["angmar"], game="rotwk")
    assert "data/living-world.json" not in angmar["runtime_data"]
    assert "livingWorld" not in angmar["pack"]["files"]


def test_rotwk_living_world_registration_is_atomic(tmp_path: Path) -> None:
    base = _base_with_selection_contract()
    base["runtime_data"]["data/living-world.json"] = {
        "schema": "openbfme.living-world",
        "schemaVersion": 1,
    }
    _faction_coverage(tmp_path, "men")
    with pytest.raises(ValueError, match="document and pack registration must coexist"):
        compose_faction_profile(base, tmp_path, ["men"], game="rotwk")


def test_rotwk_lean_pack_fails_closed_without_selection_decal_contract(tmp_path: Path) -> None:
    # Universality invariant: an expansion compose from a base profile that
    # does not ship the selection-decal contract must fail loudly, not publish
    # a pack that silently regresses to synthetic selection rings.
    _faction_coverage(tmp_path, "angmar")
    with pytest.raises(ValueError, match="universal selection-decal contract"):
        compose_faction_profile(_base(), tmp_path, ["angmar"], game="rotwk")


def test_bfme2_compose_does_not_require_selection_decal_contract(tmp_path: Path) -> None:
    # The fail-closed contract check is scoped to the lean expansion branch;
    # BFME2 factions inherit the full base profile and stay unaffected.
    _faction_coverage(tmp_path, "mordor")
    target, _ = compose_faction_profile(_base(), tmp_path, ["mordor"])
    assert target["pack"]["id"] == "bfme2-mordor-vslice"


def test_rejects_unsupported_compose_game(tmp_path: Path) -> None:
    _faction_coverage(tmp_path, "men")
    with pytest.raises(ValueError, match="unsupported compose game"):
        compose_faction_profile(_base(), tmp_path, ["men"], game="bfme3")


def test_rejects_tampered_coverage_before_reading_artifacts(tmp_path: Path) -> None:
    _coverage(tmp_path, tamper=True)
    with pytest.raises(ValueError, match="coverage digest is invalid"):
        compose_faction_profile(_base(), tmp_path, ["elves"])


def test_rejects_recipe_not_bound_to_coverage(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    _coverage(tmp_path)
    path = tmp_path / "elves/objects/elvenfortress/pack-recipe.json"
    value = json.loads(path.read_text())
    value["recipeSha256"] = "d" * 64
    _write_json(path, value)
    monkeypatch.setattr(subject, "validate_structure_visual_recipe", lambda recipe: None)
    with pytest.raises(ValueError, match="coverage/recipe identity mismatch"):
        compose_faction_profile(_base(), tmp_path, ["elves"])


def _horde_alias_coverage(root: Path, *, parent_row: bool = True) -> None:
    member_recipe = {
        "objectId": "ElvenArcherHorde", "category": "ranged-infantry",
        "descriptorSha256": "a" * 64, "recipeSha256": "b" * 64,
        "resources": [_resource("unit-elvenarcherhorde")],
        "runtimeRegistration": {"production": []},
    }
    horde_recipe = {
        "objectId": "ElvenArcherHorde", "category": "ranged-infantry",
        "descriptorSha256": "e" * 64, "recipeSha256": "f" * 64,
        "resources": [_resource("unit-elvenarcherhorde")],
        "runtimeRegistration": {"production": []},
    }
    rows = [
        {"id": "ElvenArcher", "family": "playable-unit", "status": "converted", "recipeSha256": "b" * 64},
    ]
    if parent_row:
        rows.append({"id": "ElvenArcherHorde", "family": "playable-unit", "status": "converted", "recipeSha256": "f" * 64})
    coverage = {
        "schema": "openbfme.faction-import-coverage", "schemaVersion": 0,
        "objects": rows,
        "summary": {"convertedCount": len(rows), "converterGapCount": 0, "conversionComplete": True},
    }
    coverage["aggregateSha256"] = _digest(coverage)
    _write_json(root / "elves-coverage.json", coverage)
    _write_json(root / "elves/objects/elvenarcher/pack-recipe.json", member_recipe)
    _write_json(root / "elves/objects/elvenarcherhorde/pack-recipe.json", horde_recipe)


def test_horde_member_row_aliases_parent_horde_recipe(tmp_path: Path) -> None:
    _horde_alias_coverage(tmp_path)
    target, receipt = compose_faction_profile(_base(), tmp_path, ["elves"])
    assert [row["id"] for row in target["resources"]] == ["unit-elvenarcherhorde"]
    assert target["pack"]["files"] == {"playableUnit.elvenarcherhorde": "data/playable-units/elvenarcherhorde.json"}
    document = target["runtime_data"]["data/playable-units/elvenarcherhorde.json"]
    assert document["objectId"] == "ElvenArcherHorde"
    assert document["recipeSha256"] == "f" * 64
    assert receipt["objects"] == [
        {"faction": "elves", "family": "playable-unit", "objectId": "ElvenArcher", "aliasOf": "ElvenArcherHorde"},
        {
            "faction": "elves", "family": "playable-unit", "objectId": "ElvenArcherHorde",
            "runtimePath": "data/playable-units/elvenarcherhorde.json",
            "packFileKey": "playableUnit.elvenarcherhorde",
            "resourceIds": ["unit-elvenarcherhorde"],
            "update": False,
        },
    ]


def test_rejects_horde_member_alias_without_parent_row(tmp_path: Path) -> None:
    _horde_alias_coverage(tmp_path, parent_row=False)
    with pytest.raises(ValueError, match="coverage/recipe identity mismatch"):
        compose_faction_profile(_base(), tmp_path, ["elves"])


def test_rejects_horde_member_alias_with_unbound_recipe(tmp_path: Path) -> None:
    _horde_alias_coverage(tmp_path)
    path = tmp_path / "elves/objects/elvenarcher/pack-recipe.json"
    value = json.loads(path.read_text())
    value["recipeSha256"] = "d" * 64
    _write_json(path, value)
    with pytest.raises(ValueError, match="coverage/recipe identity mismatch"):
        compose_faction_profile(_base(), tmp_path, ["elves"])
