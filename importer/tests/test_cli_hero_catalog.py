from __future__ import annotations

import json
from pathlib import Path
from unittest import mock

from openbfme_importer import cli


def test_cli_writes_complete_hero_catalog_and_keeps_deferrals_visible(
    tmp_path: Path, capsys,
) -> None:
    output = tmp_path / "heroes.json"
    expected = {
        "schema": "openbfme.hero-catalog",
        "schemaVersion": 1,
        "game": "rotwk",
        "heroes": [],
        "summary": {
            "heroCount": 139,
            "descriptorReadyCount": 61,
            "runtimeDeferredCount": 78,
            "summonedCount": 5,
            "variantCount": 20,
        },
        "catalogSha256": "e" * 64,
    }
    with (
        mock.patch.object(cli, "_load_or_build_catalog", return_value=object()),
        mock.patch.object(
            cli, "read_catalog_documents", return_value=iter((("heroes.ini", b"x"),))
        ),
        mock.patch.object(cli, "compile_hero_catalog", return_value=expected) as compile_,
    ):
        result = cli.main(
            [
                "--state-root",
                str(tmp_path / "state"),
                "--json",
                "compile-hero-catalog",
                "--install",
                str(tmp_path / "install"),
                "--game",
                "rotwk",
                "--output",
                str(output),
            ]
        )
    assert result == 0
    assert json.loads(output.read_text(encoding="utf-8")) == expected
    compile_.assert_called_once_with({"heroes.ini": b"x"}, game="rotwk")
    rendered = json.loads(capsys.readouterr().out)
    assert rendered["catalog_complete"] is True
    assert rendered["ready"] is False
    assert rendered["hero_count"] == 139
