from __future__ import annotations

from pathlib import Path

import pytest

from openbfme_importer.catalog import InstallCatalog
from openbfme_importer.hero_catalog import (
    HeroCatalogError,
    compile_hero_catalog,
    validate_hero_catalog,
)
from openbfme_importer.module_census import read_catalog_documents


def _documents() -> dict[str, bytes]:
    return {
        "data/ini/object/heroes.ini": b"""
Object OrdinarySoldier
 Side = Men
 KindOf = INFANTRY
End
Object TestHero
 Side = Men
 KindOf = HERO INFANTRY CAN_ATTACK
End
ChildObject TestHeroVariant TestHero
 KindOf = +HERO
End
""",
        "data/ini/commandset.ini": b"",
        "data/ini/commandbutton.ini": b"",
    }


def test_catalog_accounts_for_hero_family_without_fake_route() -> None:
    result = compile_hero_catalog(_documents())
    validate_hero_catalog(result)
    rows = {row["objectId"]: row for row in result["heroes"]}
    # The coverage family is intentionally broader than KindOf HERO: a retail
    # object declared in a hero-owned document is also in scope.
    assert set(rows) == {"OrdinarySoldier", "TestHero", "TestHeroVariant"}
    assert all(row["runtimeStatus"] == "deferred" for row in rows.values())
    assert rows["TestHeroVariant"]["role"] == "variant"
    assert result["summary"] == {
        "heroCount": 3,
        "descriptorReadyCount": 0,
        "runtimeDeferredCount": 3,
        "summonedCount": 0,
        "variantCount": 1,
    }


def test_catalog_rejects_unknown_game() -> None:
    with pytest.raises(HeroCatalogError, match="unsupported game"):
        compile_hero_catalog(_documents(), game="invalid")


@pytest.mark.parametrize(
    ("catalog_name", "game", "expected_count"),
    (("bfme2.json", "bfme2", 113), ("rotwk-layered.json", "rotwk", 139)),
)
def test_effective_retail_direct_hero_family_is_completely_accounted_for(
    catalog_name: str, game: str, expected_count: int
) -> None:
    repo = Path(__file__).resolve().parents[2]
    path = repo / ".private" / "retail-work" / "catalog" / catalog_name
    if not path.is_file():
        pytest.skip("operator retail catalog is not available")
    documents = dict(read_catalog_documents(InstallCatalog.load(path)))
    result = compile_hero_catalog(documents, game=game)
    assert result["summary"]["heroCount"] == expected_count
    assert result["summary"]["descriptorReadyCount"] + result["summary"]["runtimeDeferredCount"] == expected_count
