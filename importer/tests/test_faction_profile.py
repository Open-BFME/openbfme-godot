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
