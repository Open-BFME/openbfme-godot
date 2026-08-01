import pytest

from openbfme_importer.faction_census import _IMPLICIT_MEN_ROOTS
from openbfme_importer.faction_policy import (
    FactionPolicyError,
    IMPLICIT_OBJECT_ROOTS,
    implicit_object_roots,
    music_roots,
    source_null_command_sets,
    source_null_mapped_image_textures,
)
from openbfme_importer.playable_unit_import import FACTIONS


def test_every_playable_faction_has_curated_roots() -> None:
    for _short, template, _side in FACTIONS:
        roots = implicit_object_roots(template)
        assert roots, template


def test_men_roots_preserve_the_established_census_identity() -> None:
    assert implicit_object_roots("FactionMen") == _IMPLICIT_MEN_ROOTS


def test_lookup_is_casefold_insensitive() -> None:
    assert implicit_object_roots("FACTIONMORDOR") == implicit_object_roots(
        "FactionMordor"
    )


def test_unknown_faction_fails_closed() -> None:
    with pytest.raises(FactionPolicyError, match="no curated implicit census roots"):
        implicit_object_roots("FactionRohan")


def test_rotwk_angmar_curates_only_the_fortress_composite_roots() -> None:
    # Angmar's fortress composite mirrors the BFME2 factions: the map-placed
    # camp unpacks into an engine-spawned citadel (the porter/hero producer
    # CommandSet carrier) and expansion pads no CommandSet reaches.  Nothing
    # else is curated for RotWK.
    assert implicit_object_roots("FactionAngmar", game="rotwk") == (
        ("AngmarFortressCenterGeneric", "fortress-composite-center"),
        ("AngmarFortressCitadel", "fortress-composite-citadel"),
        ("AngmarFortressExpansionPadCorner", "fortress-composite-corner-pad"),
        ("AngmarFortressExpansionPadSide", "fortress-composite-side-pad"),
    )
    assert source_null_mapped_image_textures("FactionAngmar", game="rotwk") == ()
    assert source_null_command_sets("FactionAngmar", game="rotwk") == ()
    assert music_roots("FactionAngmar", game="rotwk") == ()


def test_rotwk_faction_without_curated_roots_keeps_empty_roots() -> None:
    assert implicit_object_roots("FactionMordor", game="rotwk") == ()


@pytest.mark.parametrize("value", ("", None, 7))
def test_invalid_player_template_identity_is_rejected(value: object) -> None:
    with pytest.raises(FactionPolicyError, match="invalid"):
        implicit_object_roots(value)  # type: ignore[arg-type]


def test_root_rows_are_well_formed_and_unique() -> None:
    for template, roots in IMPLICIT_OBJECT_ROOTS.items():
        assert template == template.casefold()
        seen: set[str] = set()
        for row in roots:
            assert isinstance(row, tuple) and len(row) == 2
            object_id, reason = row
            assert object_id and isinstance(object_id, str)
            assert reason and isinstance(reason, str)
            key = object_id.casefold()
            assert key not in seen, object_id
            seen.add(key)
