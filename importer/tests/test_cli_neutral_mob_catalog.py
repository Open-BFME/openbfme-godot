from __future__ import annotations

import json
from pathlib import Path
from types import SimpleNamespace
from unittest import mock

from openbfme_importer import cli


def test_map_object_documents_supply_exact_non_road_scenario_roots(
    tmp_path: Path,
) -> None:
    objects = tmp_path / "objects.json"
    objects.write_text(
        json.dumps(
            {
                "schema": "openbfme.sage-map-objects",
                "schemaVersion": 0,
                "count": 4,
                "objects": [
                    {"typeName": "Outpost", "roadType": 0},
                    {"typeName": "SignalFire", "roadType": 0},
                    {"typeName": "Outpost", "roadType": 0},
                    {"typeName": "RoadControlPoint", "roadType": 3},
                ],
            }
        ),
        encoding="utf-8",
    )

    assert cli._map_placement_root_ids([objects]) == ["Outpost", "SignalFire"]


def test_cli_writes_complete_neutral_mob_catalog(
    tmp_path: Path, capsys,
) -> None:
    output = tmp_path / "neutral-mobs.json"
    expected = {
        "schema": "openbfme.neutral-mob-catalog",
        "schemaVersion": 2,
        "game": "bfme2",
        "neutralMobs": [],
        "summary": {
            "neutralMobCount": 69,
            "descriptorReadyCount": 69,
            "runtimeDeferredCount": 0,
            "lairCount": 14,
            "hordeCount": 2,
            "unitDomainCount": 42,
            "structureDomainCount": 15,
            "propDomainCount": 12,
        },
        "catalogSha256": "d" * 64,
    }
    prepared = SimpleNamespace(documents={"neutral.ini": b"x"})
    with (
        mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
        mock.patch.object(
            cli, "read_catalog_documents", return_value=iter((("neutral.ini", b"x"),))
        ),
        mock.patch.object(
            cli, "prepare_playable_unit_compiler", return_value=prepared
        ),
        mock.patch.object(
            cli, "compile_neutral_mob_catalog", return_value=expected
        ) as compile_,
    ):
        result = cli.main(
            [
                "--state-root",
                str(tmp_path / "state"),
                "--json",
                "compile-neutral-mob-catalog",
                "--install",
                str(tmp_path / "install"),
                "--game",
                "bfme2",
                "--output",
                str(output),
            ]
        )

    assert result == 0
    assert json.loads(output.read_text(encoding="utf-8")) == expected
    compile_.assert_called_once_with(
        {"neutral.ini": b"x"}, game="bfme2", prepared=prepared
    )
    rendered = json.loads(capsys.readouterr().out)
    assert rendered["catalog_complete"] is True
    assert rendered["ready"] is True
    assert rendered["neutral_mob_count"] == 69
    assert rendered["runtime_deferred_count"] == 0


def test_cli_recipe_assets_root_compiles_all_neutral_domains(
    tmp_path: Path, capsys,
) -> None:
    output = tmp_path / "neutral-mobs.json"
    assets = tmp_path / "assets"
    assets.mkdir()
    unit_descriptor = {
        "objectId": "NeutralWolf",
        "descriptorSha256": "a" * 64,
        "scenarioAdmission": {"role": "creature"},
    }
    expected = {
        "schema": "openbfme.neutral-mob-catalog",
        "schemaVersion": 2,
        "game": "bfme2",
        "neutralMobs": [
            {
                "objectId": "NeutralWolf",
                "runtimeDomain": "unit",
                "runtimeStatus": "descriptor-ready",
                "descriptor": unit_descriptor,
            },
            {
                "objectId": "NeutralLair",
                "role": "lair",
                "runtimeDomain": "structure",
                "runtimeStatus": "descriptor-ready",
                "descriptor": {"descriptorSha256": "b" * 64},
            },
            {
                "objectId": "SpiderWebs01",
                "role": "ambient-or-scenario",
                "runtimeDomain": "prop",
                "runtimeStatus": "descriptor-ready",
                "descriptor": {"descriptorSha256": "6" * 64},
            },
        ],
        "summary": {
            "neutralMobCount": 3,
            "descriptorReadyCount": 3,
            "runtimeDeferredCount": 0,
            "lairCount": 1,
            "hordeCount": 0,
            "unitDomainCount": 1,
            "structureDomainCount": 1,
            "propDomainCount": 1,
        },
        "catalogSha256": "d" * 64,
    }
    integrated_descriptor = {
        "objectId": "NeutralWolf",
        "descriptorSha256": "c" * 64,
    }
    closure = {"aggregateSha256": "e" * 64}
    recipe = {"recipeSha256": "f" * 64}
    structure_artifact = {
        "descriptor": {"descriptorSha256": "1" * 64},
        "visualRecipe": {"recipeSha256": "2" * 64},
        "lifecycleEvidence": {"evidenceSha256": "3" * 64},
        "runtime": {"runtimeSha256": "4" * 64},
        "artifactSha256": "5" * 64,
    }
    prop_artifact = {
        "descriptor": {"descriptorSha256": "6" * 64},
        "visualRecipe": {"recipeSha256": "7" * 64},
        "runtime": {"descriptorSha256": "8" * 64},
        "artifactSha256": "9" * 64,
    }
    unit_artifact = {
        "schema": "openbfme.neutral-unit-pack-artifact",
        "objectId": "NeutralWolf",
        "artifactSha256": "0" * 64,
    }
    profile = {
        "schema": "openbfme.import-profile",
        "profileSha256": "a" * 64,
    }
    dependency_plan = {
        "pickupObjectIds": ["TreasureChest1"],
        "planSha256": "1" * 64,
    }
    dependency_artifact = {
        "artifactSha256": "2" * 64,
        "pickupArtifacts": [
            {
                "objectId": "TreasureChest1",
                "descriptor": {},
                "visualRecipe": {},
                "runtime": {},
            }
        ],
        "summary": {"pickupObjectCount": 1},
        "runtimeSummary": {
            "contractCount": 1,
            "executableCount": 1,
            "deferredCount": 0,
            "ready": True,
        },
    }
    prepared = SimpleNamespace(documents={"neutral.ini": b"x"})
    with (
        mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
        mock.patch.object(
            cli, "read_catalog_documents", return_value=iter((("neutral.ini", b"x"),))
        ),
        mock.patch.object(
            cli, "prepare_playable_unit_compiler", return_value=prepared
        ),
        mock.patch.object(cli, "compile_neutral_mob_catalog", return_value=expected),
        mock.patch.object(
            cli,
            "compile_scenario_unit_recipe",
            return_value=(integrated_descriptor, closure, recipe),
        ) as compile_recipe,
        mock.patch.object(
            cli,
            "build_scenario_unit_visual_closure_batch",
            return_value={"NeutralWolf": closure},
        ) as build_unit_closures,
        mock.patch.object(
            cli,
            "compile_neutral_unit_pack_artifact",
            return_value=unit_artifact,
        ) as compile_unit_artifact,
        mock.patch.object(
            cli, "build_retail_visual_closure", side_effect=[closure, closure, closure]
        ) as build_visual_closure,
        mock.patch.object(
            cli,
            "compile_neutral_structure_pack_artifact",
            return_value=structure_artifact,
        ) as compile_structure,
        mock.patch.object(
            cli,
            "compile_neutral_prop_pack_artifact",
            return_value=prop_artifact,
        ) as compile_prop,
        mock.patch.object(
            cli, "compose_neutral_pack_profile", return_value=profile
        ) as compose_profile,
        mock.patch.object(
            cli, "discover_neutral_dependencies", return_value=dependency_plan
        ) as discover_dependencies,
        mock.patch.object(
            cli,
            "compile_neutral_dependency_pack_artifact",
            return_value=dependency_artifact,
        ) as compile_dependencies,
    ):
        result = cli.main(
            [
                "--state-root",
                str(tmp_path / "state"),
                "--json",
                "compile-neutral-mob-catalog",
                "--install",
                str(tmp_path / "install"),
                "--game",
                "bfme2",
                "--output",
                str(output),
                "--recipe-assets-root",
                str(assets),
            ]
        )

    assert result == 0
    build_unit_closures.assert_called_once_with(
        mock.ANY, assets.resolve(), [unit_descriptor]
    )
    compile_recipe.assert_called_once_with(
        mock.ANY,
        assets.resolve(),
        unit_descriptor,
        game="bfme2",
        prebuilt_visual_closure=closure,
        prepared=prepared,
        source_documents={"neutral.ini": b"x"},
    )
    compile_unit_artifact.assert_called_once_with(
        integrated_descriptor,
        closure,
        recipe,
        game="bfme2",
        catalog_descriptor=unit_descriptor,
    )
    assert build_visual_closure.call_args_list == [
        mock.call(assets.resolve(), ["NeutralLair"]),
        mock.call(assets.resolve(), ["SpiderWebs01"]),
        mock.call(assets.resolve(), ["TreasureChest1"]),
    ]
    compile_structure.assert_called_once_with(
        "NeutralLair",
        {"neutral.ini": b"x"},
        closure,
        role="lair",
        surfaces=["map-placement", "script-spawn", "object-creation-list", "lair-spawn"],
        game="bfme2",
    )
    compile_prop.assert_called_once_with(
        "SpiderWebs01", {"neutral.ini": b"x"}, closure, game="bfme2"
    )
    compose_profile.assert_called_once_with(
        expected,
        [unit_artifact, structure_artifact, prop_artifact],
        dependency_artifact=dependency_artifact,
        version="dddddddddddddddd",
    )
    discover_dependencies.assert_called_once_with(
        expected,
        [unit_artifact, structure_artifact, prop_artifact],
        {"neutral.ini": b"x"},
        game="bfme2",
    )
    compile_dependencies.assert_called_once_with(
        dependency_plan,
        {"neutral.ini": b"x"},
        closure,
        game="bfme2",
        prepared=prepared,
    )
    integration_path = tmp_path / "neutral-mobs-recipe-integration.json"
    integration = json.loads(integration_path.read_text(encoding="utf-8"))
    assert [row["objectId"] for row in integration["rows"]] == [
        "NeutralWolf", "NeutralLair", "SpiderWebs01"
    ]
    assert integration["summary"] == {
        "candidateCount": 3,
        "recipeReadyCount": 3,
        "deferredCount": 0,
    }
    assert (tmp_path / "neutral-mobs-artifacts" / "neutralwolf" / "recipe.json").is_file()
    assert json.loads(
        (tmp_path / "neutral-mobs-pack-profile.json").read_text(encoding="utf-8")
    ) == profile
    rendered = json.loads(capsys.readouterr().out)
    assert rendered["recipe_ready_count"] == 3
    assert rendered["recipe_deferred_count"] == 0
    assert rendered["dependency_ready"] is True
    assert rendered["pack_profile"].endswith("neutral-mobs-pack-profile.json")


def test_cli_red_rerun_removes_stale_neutral_profile(
    tmp_path: Path, capsys,
) -> None:
    output = tmp_path / "neutral-mobs.json"
    assets = tmp_path / "assets"
    assets.mkdir()
    stale_profile = tmp_path / "neutral-mobs-pack-profile.json"
    stale_profile.write_text('{"stale":true}', encoding="utf-8")
    expected = {
        "schema": "openbfme.neutral-mob-catalog",
        "schemaVersion": 2,
        "game": "bfme2",
        "neutralMobs": [
            {
                "objectId": "DeferredNeutral",
                "runtimeDomain": "unit",
                "runtimeStatus": "deferred",
                "deferredReason": "fixture blocker",
            }
        ],
        "summary": {
            "neutralMobCount": 1,
            "descriptorReadyCount": 0,
            "runtimeDeferredCount": 1,
            "lairCount": 0,
            "hordeCount": 0,
            "unitDomainCount": 1,
            "structureDomainCount": 0,
            "propDomainCount": 0,
        },
        "catalogSha256": "d" * 64,
    }
    prepared = SimpleNamespace(documents={"neutral.ini": b"x"})
    with (
        mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
        mock.patch.object(
            cli, "read_catalog_documents", return_value=iter((("neutral.ini", b"x"),))
        ),
        mock.patch.object(
            cli, "prepare_playable_unit_compiler", return_value=prepared
        ),
        mock.patch.object(cli, "compile_neutral_mob_catalog", return_value=expected),
        mock.patch.object(
            cli, "build_scenario_unit_visual_closure_batch", return_value={}
        ),
    ):
        result = cli.main(
            [
                "--state-root",
                str(tmp_path / "state"),
                "--json",
                "compile-neutral-mob-catalog",
                "--install",
                str(tmp_path / "install"),
                "--game",
                "bfme2",
                "--output",
                str(output),
                "--recipe-assets-root",
                str(assets),
            ]
        )

    assert result == 6
    assert not stale_profile.exists()
    rendered = json.loads(capsys.readouterr().out)
    assert rendered["ready"] is False
    assert rendered["dependency_ready"] is False
    assert rendered["pack_profile"] is None
