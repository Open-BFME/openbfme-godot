from __future__ import annotations

from copy import deepcopy
import hashlib
import json
from pathlib import Path

import pytest

from importer.tests.test_playable_structure_compiler import _structure_documents
from importer.tests.test_playable_structure_pack_compiler import _closure, _rehash
from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.module_census import read_catalog_documents
from openbfme_importer.neutral_mob_catalog import compile_neutral_mob_catalog
from openbfme_importer.neutral_structure_pack_compiler import (
    NeutralStructurePackCompilerError,
    compile_neutral_structure_pack_artifact,
    validate_neutral_structure_pack_artifact,
)
from openbfme_importer.playable_structure_pack_compiler import _digest
from openbfme_importer.playable_unit_compiler import prepare_playable_unit_compiler
from openbfme_importer.retail_visual_closure import build_retail_visual_closure
from openbfme_importer.retail_ability_fx_ingress import build_texture_index


BFME2_NEUTRAL_STRUCTURES = (
    "BarrowWightLair",
    "BarrowWightLairHole",
    "CaveTrollLair",
    "CaveTrollLairHole",
    "CaveTrollLairSnow",
    "FangornTrollCave",
    "FireDrakeLair",
    "FireDrakeLairHole",
    "MoriarGoblinLair",
    "MoriarGoblinLairHole",
    "MoriarGoblinLairSnow",
    "SpiderLair",
    "SpiderLairHole",
    "WargLair",
    "WargLairHole",
)
ROTWK_NEUTRAL_STRUCTURES = (
    "BarrowWightLair",
    "BarrowWightLairHole",
    "CaveTrollLair",
    "CaveTrollLairHole",
    "CaveTrollLairSnow",
    "DireWolfLair",
    "DireWolfLairHole",
    "FangornTrollCave",
    "FireDrakeLair",
    "FireDrakeLairHole",
    "HillTrollLair",
    "HillTrollLairHole",
    "HillTrollLairSnow",
    "MoriarGoblinLair",
    "MoriarGoblinLairHole",
    "MoriarGoblinLairSnow",
    "SnowTrollLair",
    "SnowTrollLairHole",
    "SnowTrollLairSnow",
    "SpiderLair",
    "SpiderLairHole",
    "WargLair",
    "WargLairHole",
)


def test_bfme2_fangorn_capability_classification_binds_exact_artifact_descriptor() -> None:
    repo = Path(__file__).resolve().parents[2]
    retail = repo / ".private" / "retail-work"
    catalog_path = retail / "catalog" / "bfme2.json"
    effective_root = retail / "cache" / "effective-assets"
    if not catalog_path.is_file() or not effective_root.is_dir():
        pytest.skip("operator BFME2 retail catalog/effective assets are unavailable")

    catalog = InstallCatalog.load(catalog_path)
    documents = dict(read_catalog_documents(catalog))
    prepared = prepare_playable_unit_compiler(documents)
    neutral = compile_neutral_mob_catalog(
        documents, game="bfme2", prepared=prepared
    )
    row = next(
        value
        for value in neutral["neutralMobs"]
        if value["objectId"] == "FangornTrollCave"
    )
    closure = build_retail_visual_closure(
        effective_root, ["FangornTrollCave"], catalog=catalog
    )
    artifact = compile_neutral_structure_pack_artifact(
        "FangornTrollCave",
        documents,
        closure,
        role="neutral-structure",
        surfaces=["map-placement", "script-spawn", "object-creation-list"],
        prepared=prepared,
        game="bfme2",
    )

    assert row["role"] == "ambient-or-scenario"
    assert row["runtimeDomain"] == "structure"
    assert artifact["role"] == "neutral-structure"
    assert artifact["descriptor"] == row["descriptor"]


def _synthetic_closure() -> dict[str, object]:
    closure = _closure(include_construction=False)
    closure["targets"] = [{"name": "TestCitadel", "status": "resolved"}]
    for row in closure["exactLeaves"]:
        row["targetObject"] = "TestCitadel"
        row["provenance"]["definingObject"] = "TestCitadel"
    _rehash(closure)
    return closure


def _resign(value: dict[str, object]) -> None:
    value.pop("artifactSha256", None)
    value["artifactSha256"] = _digest(value)


def test_neutral_structure_artifact_is_deterministic_and_non_buildable() -> None:
    documents = _structure_documents()
    closure = _synthetic_closure()
    first = compile_neutral_structure_pack_artifact(
        "TestCitadel",
        documents,
        closure,
        role="neutral-structure",
        surfaces=["map-placement", "script-spawn", "object-creation-list"],
    )
    second = compile_neutral_structure_pack_artifact(
        "TestCitadel",
        dict(reversed(list(documents.items()))),
        closure,
        role="neutral-structure",
        surfaces=["object-creation-list", "map-placement", "script-spawn"],
    )

    validate_neutral_structure_pack_artifact(first)
    assert first == second
    assert first["objectId"] == "TestCitadel"
    assert first["descriptor"]["production"] == {
        "evidence": "authored-neutral-map",
        "routes": [],
    }
    scenario = first["descriptor"]["scenarioAdmission"]
    assert first["sourceIdentity"] == {
        "declarationKind": scenario["declarationKind"],
        "sourceIni": scenario["sourceIni"],
        "line": scenario["line"],
    }
    assert first["visualRecipe"]["visualClosureSha256"] == closure[
        "aggregateSha256"
    ]
    assert first["runtime"]["registration"]["scenarioAdmission"] == scenario
    assert first["runtime"]["registration"]["presentation"][
        "buildingLifecycle"
    ]["simulationFacts"]["construction"] == {
        "status": "never-constructed-authored-neutral-map"
    }


@pytest.mark.parametrize(
    ("role", "surfaces", "message"),
    (
        ("lair", ["map-placement"], "lair-spawn"),
        ("neutral-structure", ["map-placement", "lair-spawn"], "cannot claim"),
        ("neutral-structure", ["map-placement", "map-placement"], "duplicate"),
    ),
)
def test_neutral_structure_admission_fails_closed(
    role: str, surfaces: list[str], message: str
) -> None:
    with pytest.raises(NeutralStructurePackCompilerError, match=message):
        compile_neutral_structure_pack_artifact(
            "TestCitadel",
            _structure_documents(),
            _synthetic_closure(),
            role=role,
            surfaces=surfaces,
        )


def test_neutral_structure_artifact_rejects_cross_object_asset_drift() -> None:
    artifact = compile_neutral_structure_pack_artifact(
        "TestCitadel",
        _structure_documents(),
        _synthetic_closure(),
        role="neutral-structure",
        surfaces=["map-placement"],
    )
    broken = deepcopy(artifact)
    broken["visualRecipe"]["objectId"] = "OtherLair"
    broken["visualRecipe"]["slug"] = "otherlair"
    broken["visualRecipe"].pop("recipeSha256")
    broken["visualRecipe"]["recipeSha256"] = _digest(broken["visualRecipe"])
    _resign(broken)

    with pytest.raises(NeutralStructurePackCompilerError, match="cross-document"):
        validate_neutral_structure_pack_artifact(broken)


@pytest.mark.parametrize(
    ("catalog_name", "game", "effective_relative", "object_ids", "custom_owner_count", "custom_edge_count"),
    (
        (
            "bfme2.json",
            "bfme2",
            "cache/effective-assets",
            BFME2_NEUTRAL_STRUCTURES,
            8,
            48,
        ),
        (
            "rotwk-layered.json",
            "rotwk",
            "editions/rotwk/cache/layered-effective-assets",
            ROTWK_NEUTRAL_STRUCTURES,
            13,
            91,
        ),
    ),
)
def test_exact_retail_neutral_structure_family_is_pack_ready(
    catalog_name: str,
    game: str,
    effective_relative: str,
    object_ids: tuple[str, ...],
    custom_owner_count: int,
    custom_edge_count: int,
) -> None:
    repo = Path(__file__).resolve().parents[2]
    retail = repo / ".private" / "retail-work"
    catalog_path = retail / "catalog" / catalog_name
    effective_root = retail / effective_relative
    if not catalog_path.is_file() or not (
        effective_root / ".openbfme" / "manifest.json"
    ).is_file():
        pytest.skip("operator retail catalog/effective assets are not available")

    documents = dict(read_catalog_documents(InstallCatalog.load(catalog_path)))
    prepared = prepare_playable_unit_compiler(documents)
    closure = build_retail_visual_closure(effective_root, object_ids)
    effect_documents = {
        relative: (effective_root / relative).read_bytes()
        for relative in (
            "data/ini/fxlist.ini",
            "data/ini/particlesystem.ini",
            "data/ini/fxparticlesystem.ini",
        )
    }
    texture_index = build_texture_index(effective_root)
    artifacts = []
    for object_id in object_ids:
        is_lair = object_id != "FangornTrollCave"
        artifacts.append(
            compile_neutral_structure_pack_artifact(
                object_id,
                documents,
                closure,
                role="lair" if is_lair else "neutral-structure",
                surfaces=(
                    [
                        "map-placement",
                        "script-spawn",
                        "object-creation-list",
                        "lair-spawn",
                    ]
                    if is_lair
                    else ["map-placement", "script-spawn", "object-creation-list"]
                ),
                prepared=prepared,
                game=game,
                effect_documents=effect_documents,
                fx_texture_index=texture_index,
            )
        )

    assert len(artifacts) == len(object_ids)
    assert {row["objectId"] for row in artifacts} == set(object_ids)
    assert len({row["artifactSha256"] for row in artifacts}) == len(object_ids)
    assert all(row["descriptor"]["production"]["routes"] == [] for row in artifacts)
    assert all(row["visualRecipe"]["resources"] for row in artifacts)
    assert all(
        row["visualRecipe"]["visualClosureSha256"] == closure["aggregateSha256"]
        for row in artifacts
    )
    custom = [row["customAnimationPresentation"] for row in artifacts if "customAnimationPresentation" in row]
    assert len(custom) == custom_owner_count
    assert sum(len(row["edgeIds"]) for row in custom) == custom_edge_count
    assert {
        attachment["particleSystemId"]
        for request in custom
        for attachment in request["attachments"]
    } == {"UntamedAllegiance", "UntamedAllegiance2"}
    assert all(request["runtimeStatus"] == "deferred" for request in custom)
    assert all(request["activationAllowed"] is False for request in custom)
    assert all(request["particleEmissionAllowed"] is False for request in custom)
    assert all(request["fabricatedClip"] is False for request in custom)
    assert len({request["particleClosure"]["aggregateSha256"] for request in custom}) == 1

    if game == "rotwk":
        # Exact current retail map denominator for the generic non-lair map
        # root.  This proves that every authored RuinedTower placement has the
        # same sealed descriptor/runtime available; it is not a representative
        # one-map sample.
        map_digest = (
            "1739b61386b8242aafee7c46c2f2639f950dd8d5d7292687d2c10702b1e9972b"
        )
        map_root = (
            repo
            / ".private"
            / "content-packs"
            / "rotwk-playable-maps-private"
            / map_digest
        )
        catalog_path = map_root / "data" / "maps.json"
        if not catalog_path.is_file():
            pytest.skip("selected exact RotWK map catalog is unavailable")
        catalog_bytes = catalog_path.read_bytes()
        assert hashlib.sha256(catalog_bytes).hexdigest() == (
            "cad332168625e4ed19d5b5dd6468cd1744a60129d25262fff582ab7fbfd58764"
        )
        placement_maps: set[str] = set()
        ruined_tower_placements = 0
        for map_row in json.loads(catalog_bytes)["maps"]:
            map_path = map_root / map_row["map"]
            map_document = json.loads(map_path.read_text(encoding="utf-8"))
            objects_path = map_path.parent / map_document["objects"]
            objects_document = json.loads(objects_path.read_text(encoding="utf-8"))
            object_rows = (
                objects_document["objects"]
                if isinstance(objects_document, dict)
                else objects_document
            )
            count = sum(
                1
                for placement in object_rows
                if placement.get(
                    "typeName",
                    placement.get("type_name", placement.get("type", "")),
                )
                == "RuinedTower"
            )
            if count:
                placement_maps.add(map_row["id"])
                ruined_tower_placements += count
        ruined_tower_closure = build_retail_visual_closure(
            effective_root, ("RuinedTower",)
        )
        ruined_tower = compile_neutral_structure_pack_artifact(
            "RuinedTower",
            documents,
            ruined_tower_closure,
            role="neutral-structure",
            surfaces=["map-placement"],
            prepared=prepared,
            game=game,
            effect_documents=effect_documents,
            fx_texture_index=texture_index,
        )
        assert ruined_tower_placements == 55
        assert len(placement_maps) == 13
        assert ruined_tower["descriptor"]["production"] == {
            "evidence": "authored-neutral-map",
            "routes": [],
        }
        assert "map-placement" in ruined_tower["surfaces"]

    tampered = deepcopy(next(row for row in artifacts if "customAnimationPresentation" in row))
    tampered["customAnimationPresentation"]["particleClosure"]["aggregateSha256"] = "0" * 64
    _resign(tampered)
    with pytest.raises(NeutralStructurePackCompilerError, match="runtime binding drifted|prerequisite is invalid"):
        validate_neutral_structure_pack_artifact(tampered)
