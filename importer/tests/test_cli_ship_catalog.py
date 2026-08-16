from __future__ import annotations

import json
from pathlib import Path
from unittest import mock

import pytest

from openbfme_importer import cli


def _catalog(game: str) -> dict[str, object]:
    return {
        "schema": "openbfme.ship-catalog",
        "schemaVersion": 1,
        "game": game,
        "ships": [
            {
                "objectId": "BuildableShip",
                "side": "Elves",
                "role": "buildable",
                "runtimeStatus": "descriptor-ready",
                "descriptor": {"descriptorSha256": "a" * 64},
            },
            {
                "objectId": "ShipInterface",
                "side": "Elves",
                "role": "inheritance-template",
                "runtimeStatus": "descriptor-ready",
                "descriptor": {"descriptorSha256": "b" * 64},
            },
        ],
        "summary": {
            "shipCount": 2,
            "buildableDescriptorCount": 1,
            "inheritanceTemplateCount": 1,
            "scenarioOnlyCount": 0,
            "runtimeDeferredCount": 0,
        },
        "catalogSha256": "c" * 64,
    }


@pytest.mark.parametrize("game", ("bfme2", "rotwk"))
def test_cli_writes_complete_catalog_for_each_edition(
    tmp_path: Path, game: str
) -> None:
    output = tmp_path / f"{game}-ships.json"
    expected = _catalog(game)
    with (
        mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
        mock.patch.object(
            cli, "read_catalog_documents", return_value=iter((("ships.ini", b"x"),))
        ),
        mock.patch.object(cli, "compile_ship_catalog", return_value=expected) as compile_,
    ):
        result = cli.main(
            [
                "--state-root",
                str(tmp_path / "state"),
                "compile-ship-catalog",
                "--install",
                str(tmp_path / "install"),
                "--game",
                game,
                "--output",
                str(output),
            ]
        )

    assert result == 0
    assert json.loads(output.read_text(encoding="utf-8")) == expected
    compile_.assert_called_once_with({"ships.ini": b"x"}, game=game)


def test_cli_routes_buildable_and_scenario_ships_through_recipe_pipeline(
    tmp_path: Path,
) -> None:
    output = tmp_path / "ships.json"
    assets = tmp_path / "assets"
    assets.mkdir()
    graph = {"schema": "graph"}
    descriptor = {"descriptorSha256": "d" * 64}
    closure = {"schema": "closure"}
    recipe = {"recipeSha256": "e" * 64}
    with (
        mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
        mock.patch.object(cli, "read_catalog_documents", return_value=iter(())),
        mock.patch.object(cli, "compile_ship_catalog", return_value=_catalog("bfme2")),
        mock.patch.object(
            cli,
            "compile_unit_recipe",
            return_value=(graph, descriptor, closure, recipe),
        ) as compile_recipe,
    ):
        result = cli.main(
            [
                "--state-root",
                str(tmp_path / "state"),
                "compile-ship-catalog",
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
    assert compile_recipe.call_args_list == [
        mock.call(
            mock.ANY,
            assets.resolve(),
            "BuildableShip",
            game="bfme2",
            faction="Elves",
            admitted_producer_ids=(),
            scenario_admission_role=None,
        ),
        mock.call(
            mock.ANY,
            assets.resolve(),
            "ShipInterface",
            game="bfme2",
            faction="Elves",
            admitted_producer_ids=(),
            scenario_admission_role="inheritance-template",
        ),
    ]
    object_root = tmp_path / "ships-artifacts" / "buildableship"
    assert json.loads((object_root / "recipe.json").read_text(encoding="utf-8")) == recipe
    assert (tmp_path / "ships-artifacts" / "shipinterface" / "recipe.json").is_file()
    integration = json.loads(
        (tmp_path / "ships-recipe-integration.json").read_text(encoding="utf-8")
    )
    assert integration["summary"] == {
        "candidateCount": 2,
        "recipeReadyCount": 2,
        "deferredCount": 0,
    }
    assert integration["rows"][0]["artifactDirectory"] == (
        "ships-artifacts/buildableship"
    )


def test_cli_keeps_recipe_failure_deferred_and_returns_six(tmp_path: Path) -> None:
    output = tmp_path / "ships.json"
    assets = tmp_path / "assets"
    assets.mkdir()
    with (
        mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
        mock.patch.object(cli, "read_catalog_documents", return_value=iter(())),
        mock.patch.object(cli, "compile_ship_catalog", return_value=_catalog("bfme2")),
        mock.patch.object(
            cli,
            "compile_unit_recipe",
            side_effect=ValueError("visual closure is not conversion-ready"),
        ),
    ):
        result = cli.main(
            [
                "--state-root",
                str(tmp_path / "state"),
                "compile-ship-catalog",
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
    integration = json.loads(
        (tmp_path / "ships-recipe-integration.json").read_text(encoding="utf-8")
    )
    assert integration["rows"] == [
        {
            "objectId": "BuildableShip",
            "status": "deferred",
            "reason": "visual closure is not conversion-ready",
        },
        {
            "objectId": "ShipInterface",
            "status": "deferred",
            "reason": "visual closure is not conversion-ready",
        },
    ]
