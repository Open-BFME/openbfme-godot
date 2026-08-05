"""Banner carrier contract compilation for RotWK hordes."""

from __future__ import annotations

import pytest

from openbfme_importer.playable_unit_compiler import (
    PlayableUnitCompilerError,
    _banner_carrier_contract,
    _banner_carrier_update_contract,
)
from openbfme_importer.sage_cst import parse_sage_document


def _object(source: str):
    document = parse_sage_document(
        source.encode("utf-8"),
        "data/ini/object/test.ini",
    )
    return document.objects[0]


def test_banner_carrier_defaults_min_level_to_two() -> None:
    # Retail authors banner fields on HordeContain, not free Object keys.
    target = _object(
        """
Object GondorFighterHorde
  KindOf = HORDE INFANTRY
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = GondorFighter 15
    BannerCarriersAllowed = GondorInfantryBanner
    BannerCarrierPosition = UnitType:GondorFighter Pos:X:70.0 Y:0.0
  End
End
"""
    )
    contract = _banner_carrier_contract(target, (target,))
    assert contract is not None
    assert contract["allowedObjectIds"] == ["GondorInfantryBanner"]
    assert contract["minLevel"] == 2
    assert contract["minLevelDefaulted"] is True
    assert contract["positions"][0]["unitType"] == "GondorFighter"
    assert contract["positions"][0]["x"] == 70.0
    assert contract["positions"][0]["y"] == 0.0


def test_banner_carrier_respects_authored_min_level_zero() -> None:
    target = _object(
        """
Object AngmarThrallHorde
  KindOf = HORDE INFANTRY
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = AngmarOrcWarrior 10
    BannerCarriersAllowed = AngmarThrallMasterBanner
    BannerCarrierMinLevel = 0
    BannerCarrierDestroyHordeOnDeath = Yes
  End
End
"""
    )
    contract = _banner_carrier_contract(target, (target,))
    assert contract is not None
    assert contract["minLevel"] == 0
    assert contract["minLevelDefaulted"] is False
    assert contract["destroyHordeOnBannerDeath"] is True


def test_banner_carrier_absent_when_not_authored() -> None:
    target = _object(
        """
Object SoloUnit
  KindOf = INFANTRY
End
"""
    )
    assert _banner_carrier_contract(target, (target,)) is None


def test_banner_carrier_rejects_bad_position() -> None:
    target = _object(
        """
Object BadHorde
  KindOf = HORDE
  Behavior = HordeContain ModuleTag_HordeContain
    InitialPayload = Fighter 5
    BannerCarriersAllowed = SomeBanner
    BannerCarrierPosition = not-a-position
  End
End
"""
    )
    with pytest.raises(PlayableUnitCompilerError, match="BannerCarrierPosition"):
        _banner_carrier_contract(target, (target,))


def test_banner_update_retains_authored_respawn_lower_bounds() -> None:
    target = _object(
        """
Object GondorInfantryBanner
  KindOf = INFANTRY
  Behavior = BannerCarrierUpdate BannerCarrierUpdateModuleTag
    DiedRespawnTime = 10000
    MeleeFreeBannerReSpawnTime = 20000
  End
End
"""
    )
    contract = _banner_carrier_update_contract(target, (target,))
    assert contract is not None
    assert contract["diedRespawnTime"]["milliseconds"] == 10000
    assert contract["meleeFreeBannerRespawnTime"]["milliseconds"] == 20000


def test_banner_update_without_authored_death_timer_cannot_respawn() -> None:
    target = _object(
        """
Object SpellBookHealingWell
  Behavior = BannerCarrierUpdate BannerCarrierUpdateModuleTag
    IdleSpawnRate = 2000
  End
End
"""
    )
    assert _banner_carrier_update_contract(target, (target,)) is None


def test_banner_update_rejects_invented_or_symbolic_respawn_time() -> None:
    target = _object(
        """
Object BadBanner
  Behavior = BannerCarrierUpdate BannerCarrierUpdateModuleTag
    DiedRespawnTime = DEFAULT_BANNER_RESPAWN
  End
End
"""
    )
    with pytest.raises(PlayableUnitCompilerError, match="DiedRespawnTime"):
        _banner_carrier_update_contract(target, (target,))
