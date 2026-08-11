from __future__ import annotations

import os
from pathlib import Path

import pytest

from openbfme_importer.playable_structure_compiler import (
    compile_playable_structure_descriptor,
)
from openbfme_importer.playable_unit_compiler import (
    compile_playable_unit_descriptor,
    prepare_playable_unit_compiler,
)


ARCHER_HORDES = {
    "GondorArcherHorde": "GondorArcherArrow",
    "ElvenLorienArcherHorde": "GoodFactionArrow",
    "ElvenMirkwoodArcherHorde": "GoodFactionArrow",
    "ElvenRivendellArcherHorde": "GoodFactionArrow",
    "DwarvenMenOfDaleHorde": "GoodFactionArrow",
    "IsengardUrukCrossbowHorde": "GoodFactionArrow",
    "MordorArcherHorde": "EvilFactionArrow",
    "MordorHaradrimArcherHorde": "EvilFactionArrow",
    "GoblinArcherHorde": "EvilFactionArrow",
    "AngmarDarkRangerHorde": "EvilFactionArrow",
}

FORTRESS_ARTILLERY = {
    "MenTrebuchetExpansion": ("GondorTrebuchetRock", "GondorTrebuchetExpansionWeapon", "MenTrebuchetFortress", "GondorTrebuchetRockProjectile", 500, 300, 321, 8000, 1200, 5400, 600),
    "MenTrebuchetSideExpansion": ("GondorTrebuchetRock", "GondorTrebuchetSideExpansionWeapon", "MenTrebuchetFortress", "GondorTrebuchetRockProjectile", 500, 300, 321, 8000, 1200, 5400, 600),
    "DwarvenCatapultExpansion": ("DwarvenIronHillsCatapultRock", "DwarvenCatapultExpansionWeapon", "DwarvenCatapultFortress", "DwarvenCatapultRockProjectile", 500, 150, 201, 6000, 800, 3000, 300),
    "MordorWallCatapultExpansion": ("MordorCatapultRock_Structural", "MordorCatapultExpansionWeapon", "MordorFortressCatapult", "MordorCatapultRockProjectile", 500, 300, 201, 6000, 800, 3000, 1001),
    "AngmarCatapultExpansion": ("AngmarStoneThrowerRock", "DwarvenCatapultExpansionWeapon", "AngmarTrollSlingFortress", "RockBigTroll", 500, 150, 321, 6000, 800, 3000, 600),
}


def _oracle_documents(root: Path) -> dict[str, bytes]:
    ini_root = root / "data" / "ini"
    return {
        path.relative_to(root).as_posix(): path.read_bytes()
        for path in ini_root.rglob("*")
        if path.is_file() and path.suffix.casefold() in {".ini", ".inc"}
    }


def test_rotwk_oracle_compiles_all_archer_and_fortress_projectile_bindings() -> None:
    raw = os.environ.get("OPENBFME_EFFECTIVE_ASSETS_ROOT", "")
    root = Path(raw)
    if not raw or not (root / "data" / "ini" / "weapon.ini").is_file():
        pytest.skip("OPENBFME_EFFECTIVE_ASSETS_ROOT does not name the pure effective-assets oracle")
    documents = _oracle_documents(root)
    prepared = prepare_playable_unit_compiler(documents)

    for object_id, projectile_id in ARCHER_HORDES.items():
        descriptor = compile_playable_unit_descriptor(
            object_id, documents, prepared=prepared, game="rotwk"
        )
        combat = descriptor["gameplay"]["simulation"]["resolved"]["combat"]
        assert combat["projectileObjectId"] == projectile_id

    for object_id, expected in FORTRESS_ARTILLERY.items():
        descriptor = compile_playable_structure_descriptor(
            object_id, documents, prepared=prepared, game="rotwk"
        )
        combat = descriptor["gameplay"]["combat"]
        actual = (
            combat["weaponId"],
            combat["targetAcquisitionWeaponId"],
            combat["spawnedObjectId"],
            combat["projectileObjectId"],
            combat["attackRange"]["value"],
            combat["minimumAttackRange"]["value"],
            combat["projectileSpeed"]["value"],
            combat["delayBetweenShotsMs"]["value"],
            combat["preAttackDelayMs"]["value"],
            combat["firingDurationMs"]["value"],
            combat["damage"]["value"],
        )
        assert actual == expected
