"""Delivery-side pins for the per-side EVA announcer overlay packs.

The runtime half of the EVA lane already exists; the missing half is delivery:
one overlay pack per playable side, composed by ``tools/compose-eva-overlay.py``.
Two gaps blocked that, and both are pinned here against external retail bytes
(pure RotWK 2.01 ``eva.ini``) rather than against the importer's own tables:

1. ``_FACTION_PLAYER_TEMPLATES`` covered only BFME2's six sides, so composing
   the Angmar overlay raised ``ValueError`` even though retail authors 93
   Angmar side-sound rows in ``eva.ini``.
2. The compose tool hardcoded ``<state-root>/catalog/bfme2.json``, so it could
   not address the RotWK catalog the 2.01 content baseline is imported from.
3. The audio-scoped census validator accepted only ``BFME2``/``1.06`` census
   targets, so every RotWK census — the edition the shipped content baseline is
   built from — was rejected before a single announcer sound was resolved.
"""

from __future__ import annotations

import importlib.util
from pathlib import Path
import re
import sys

import pytest

from openbfme_importer.faction_profile import _FACTION_PLAYER_TEMPLATES


ROOT = Path(__file__).resolve().parents[2]
PRIVATE_ROOT = ROOT / ".private" / "retail-work"
if not (PRIVATE_ROOT / "editions/rotwk/cache/effective-assets").is_dir() and ROOT.parent.name == "worktrees":
    PRIVATE_ROOT = ROOT.parents[2] / ".private" / "retail-work"
EVA_INI = PRIVATE_ROOT / "editions/rotwk/cache/effective-assets/data/ini/eva.ini"

_SIDE_LINE = re.compile(rb"^\s*Side\s*=\s*([A-Za-z0-9_]+)\s*$", re.IGNORECASE | re.MULTILINE)


def _oracle_sides() -> set[str]:
    """Every side name pure RotWK 2.01 eva.ini binds an announcer sound to.

    ``PlayerXxx`` rows are the same seven sides under eva.ini's second naming
    convention; both collapse onto the playable side name.
    """

    if not EVA_INI.is_file():
        pytest.fail("pure RotWK 2.01 eva.ini oracle is not present")
    source = EVA_INI.read_bytes()
    sides: set[str] = set()
    for match in _SIDE_LINE.finditer(source):
        name = match.group(1).decode("latin-1")
        if name.casefold().startswith("player"):
            name = name[len("player") :]
        sides.add(name)
    return sides


def _compose_module():
    path = ROOT / "tools" / "compose-eva-overlay.py"
    spec = importlib.util.spec_from_file_location("compose_eva_overlay", path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules["compose_eva_overlay"] = module
    spec.loader.exec_module(module)
    return module


def test_every_eva_authored_side_has_a_player_template() -> None:
    # Retail authors announcer sounds for seven sides; a side with no player
    # template cannot be composed into an overlay pack at all.
    assert _oracle_sides() == set(_FACTION_PLAYER_TEMPLATES)


def test_angmar_maps_to_its_rotwk_player_template() -> None:
    assert _FACTION_PLAYER_TEMPLATES["Angmar"] == "FactionAngmar"


@pytest.mark.parametrize(
    ("game", "expected"),
    [("bfme2", "bfme2.json"), ("rotwk", "rotwk.json")],
)
def test_compose_tool_addresses_the_catalog_of_the_requested_game(
    game: str, expected: str, tmp_path: Path
) -> None:
    module = _compose_module()
    assert module.catalog_path(tmp_path, game) == tmp_path / "catalog" / expected


def test_compose_tool_defaults_to_the_bfme2_catalog(tmp_path: Path) -> None:
    # The pre-existing invocation passed no game; its resolved catalog must not
    # move underneath it.
    module = _compose_module()
    parser = module.build_parser()
    args = parser.parse_args(
        [
            "--audio-census",
            str(tmp_path / "census.json"),
            "--faction",
            "Men",
            "--pack-id",
            "x",
            "--output",
            str(tmp_path / "o.json"),
            "--receipt",
            str(tmp_path / "r.json"),
        ]
    )
    assert args.game == "bfme2"


def _rotwk_census(side: str) -> dict[str, object]:
    path = PRIVATE_ROOT / "editions/rotwk/reports" / f"{side.casefold()}-faction-leaf-census.json"
    if not path.is_file():
        pytest.skip(f"RotWK {side} leaf census has not been generated yet: {path}")
    import json

    return json.loads(path.read_text(encoding="utf-8"))


@pytest.mark.parametrize("side", ["Men", "Angmar"])
def test_audio_validator_accepts_the_rotwk_census_target(side: str) -> None:
    from openbfme_importer.faction_profile import _validate_faction_audio_report

    report = _rotwk_census(side)
    assert report["target"]["game"] == "RotWK"
    assert report["target"]["patch"] == "2.01"
    validated = _validate_faction_audio_report(
        report, side, _FACTION_PLAYER_TEMPLATES[side]
    )
    assert validated["target"]["faction"] == side


def test_audio_validator_still_refuses_an_incoherent_edition_target() -> None:
    from openbfme_importer.faction_profile import _validate_faction_audio_report

    report = _rotwk_census("Men")
    # 2.01 content under a BFME2 identity is exactly the mixed-edition read the
    # validator exists to refuse; widening it to RotWK must not widen it to any
    # (game, patch) pair.
    report["target"] = dict(report["target"], game="BFME2", patch="2.01")
    with pytest.raises(ValueError):
        _validate_faction_audio_report(report, "Men", "FactionMen")


@pytest.mark.parametrize("game", ["bfme2", "rotwk"])
def test_compose_tool_refuses_a_census_from_another_edition(game: str) -> None:
    module = _compose_module()
    report = _rotwk_census("Men")
    if game == "rotwk":
        report["target"] = dict(report["target"], game="BFME2", patch="1.06")
    # Composing RotWK catalog bytes against a BFME2 census (or the reverse)
    # silently produces an overlay for the wrong edition's announcer set.
    with pytest.raises(ValueError):
        module.assert_census_edition(report, game)


def test_compose_tool_accepts_the_matching_edition_census() -> None:
    module = _compose_module()
    module.assert_census_edition(_rotwk_census("Men"), "rotwk")


def _rotwk_catalog():
    catalog_path = PRIVATE_ROOT / "catalog" / "rotwk.json"
    if not catalog_path.is_file():
        pytest.skip(f"RotWK catalog is not present: {catalog_path}")
    from openbfme_importer.catalog import InstallCatalog

    return InstallCatalog.load(catalog_path)


def test_angmar_eva_set_survives_retails_own_broken_sample_references() -> None:
    """RotWK 2.01 references four announcer samples that do not exist.

    ``CampThrallUpgradeSanctum`` lists three samples of which only
    ``KUAngUpg_WKSnctm`` ships; ``CampThrallWORLostRhovanion`` and
    ``CampThrallWORUnifyRhovanion`` each list one sample that ships under no
    name at all. (The install does ship ``kuupg_sanct2``/``kuupg_sanctum`` and
    ``kuwar_losrhoa``, so these are EA prefix typos - but guessing which file
    EA meant is exactly the substitution this pipeline refuses.)

    A broken reference must cost retail its broken leaf and nothing else: the
    other six sides composed fine, and Angmar's remaining 100+ announcer sets
    must not be lost to a single typo.
    """

    from openbfme_importer.faction_profile import _eva_audio_extension

    catalog = _rotwk_catalog()
    resources, events, multisounds, samples, diagnostics = _eva_audio_extension(
        catalog, "Angmar", "angmar", {}, {}
    )
    assert resources and samples

    sanctum = events["CampThrallUpgradeSanctum"]
    assert [row["id"] for row in sanctum["sounds"]] == ["KUAngUpg_WKSnctm"]

    # Zero surviving leaves means the event cannot play at all; it is dropped
    # rather than shipped as a silent stub that would look like a routing bug.
    assert "CampThrallWORLostRhovanion" not in events
    assert "CampThrallWORUnifyRhovanion" not in events

    assert set(diagnostics["missingSamples"]) == {
        "KUAngUpg_Sanct2",
        "KUAngUpg_Sanctum",
        "KUWar_LosRova",
        "KUWar_UniRova",
    }
    assert set(diagnostics["droppedDefinitions"]) == {
        "CampThrallWORLostRhovanion",
        "CampThrallWORUnifyRhovanion",
    }
    for sample_id in diagnostics["missingSamples"]:
        assert sample_id not in samples


@pytest.mark.parametrize("side", ["Men", "Elves", "Dwarves", "Isengard", "Mordor", "Wild"])
def test_the_six_intact_sides_lose_nothing(side: str) -> None:
    from openbfme_importer.faction_profile import _eva_audio_extension

    catalog = _rotwk_catalog()
    _resources, events, _multisounds, _samples, diagnostics = _eva_audio_extension(
        catalog, side, side.casefold(), {}, {}
    )
    assert diagnostics == {"missingSamples": [], "droppedDefinitions": []}
    assert events


def test_rotwk_overlay_binds_the_effective_assets_oracle_not_the_big_catalog() -> None:
    """The pipeline swaps RotWK's catalog for the effective-assets tree.

    A profile stamped with the raw BIG catalog's identity is refused by
    `_validate_source_catalog_binding` at build time - which is exactly how the
    first seven composed profiles failed - and would in any case describe a
    different source than the one the pack is cooked from.
    """

    module = _compose_module()
    if not (PRIVATE_ROOT / "editions/rotwk/cache/effective-assets").is_dir():
        pytest.skip("RotWK effective-assets oracle is not present")
    base = _rotwk_catalog()
    oracle = module.oracle_catalog(PRIVATE_ROOT, "rotwk", base)
    assert oracle.identity_sha256() != base.identity_sha256()

    from openbfme_importer.effective_assets_catalog import EffectiveAssetsCatalog

    assert isinstance(oracle, EffectiveAssetsCatalog)


def test_bfme2_keeps_reading_its_big_catalog() -> None:
    module = _compose_module()
    base = _rotwk_catalog()
    assert module.oracle_catalog(PRIVATE_ROOT, "bfme2", base) is base
