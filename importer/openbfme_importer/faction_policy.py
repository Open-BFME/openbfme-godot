"""Owner-curated per-faction census policy shared by faction import callers."""

from __future__ import annotations

from collections.abc import Mapping
from dataclasses import dataclass

from .faction_census import _IMPLICIT_MEN_ROOTS


class FactionPolicyError(ValueError):
    """The requested faction has no explicitly curated census policy."""


IMPLICIT_OBJECT_ROOTS: Mapping[str, tuple[tuple[str, str], ...]] = {
    "factionmen": _IMPLICIT_MEN_ROOTS,
    "factionelves": (
        ("ElvenFortressCenterGeneric", "fortress-composite-center"),
        ("ElvenCitadel", "fortress-composite-citadel"),
        ("ElvenFortressExpansionPadCorner", "fortress-composite-corner-pad"),
        ("ElvenFortressExpansionPadSide", "fortress-composite-side-pad"),
        ("ElvenFortressCrystalMoat", "fortress-composite-moat"),
    ),
    "factiondwarves": (
        ("DwarvenFortressCenterGeneric", "fortress-composite-center"),
        ("DwarvenFortressCitadel", "fortress-composite-citadel"),
        ("DwarvenFortressExpansionPadCorner", "fortress-composite-corner-pad"),
        ("DwarvenFortressExpansionPadSide", "fortress-composite-side-pad"),
    ),
    "factionisengard": (
        ("IsengardFortressCenterGeneric", "fortress-composite-center"),
        ("IsengardFortressCitadel", "fortress-composite-citadel"),
        ("IsengardFortressExpansionPadCorner", "fortress-composite-corner-pad"),
        ("IsengardFortressExpansionPadSide", "fortress-composite-side-pad"),
    ),
    "factionmordor": (
        ("MordorFortressCenterGeneric", "fortress-composite-center"),
        ("MordorFortressCitadel", "fortress-composite-citadel"),
        ("MordorFortressExpansionPadCorner", "fortress-composite-corner-pad"),
        ("MordorFortressLavaMoat", "fortress-composite-moat"),
        ("MordorFortressBarricadeExpansion", "fortress-composite-barricade"),
    ),
    "factionwild": (
        ("WildFortressCenterGeneric", "fortress-composite-center"),
        ("WildFortressCitadel", "fortress-composite-citadel"),
        ("WildFortressExpansionPadCorner", "fortress-composite-corner-pad"),
        ("WildFortressExpansionPadSide", "fortress-composite-side-pad"),
    ),
}


# Retail authors button images whose atlas texture no shipped archive
# contains (the spawn-orcs/test buttons carry "; @todo get image" markers).
# Each entry is consumed only when the texture is genuinely absent; a texture
# which later resolves fails the census closed instead of being masked.
# Shared by BFME2 1.06 and RotWK 2.01 — both ship the MappedImage rows without
# the compiled atlas textures.
_RETAIL_ABSENT_MAPPED_IMAGE_TEXTURES: tuple[tuple[str, str], ...] = (
    (
        "SCUserInterface_001.tga",
        "retail authors the SMSpawnOrcs MappedImage but ships no "
        "compiled atlas for SCUserInterface_001.tga",
    ),
    (
        "TrollPickup_but.tga",
        "retail authors the SCGrabPassenger MappedImage but ships no "
        "compiled atlas for TrollPickup_but.tga",
    ),
)

SOURCE_NULL_MAPPED_IMAGE_TEXTURES: Mapping[str, tuple[tuple[str, str], ...]] = {
    key: _RETAIL_ABSENT_MAPPED_IMAGE_TEXTURES for key in IMPLICIT_OBJECT_ROOTS
}

# Retail 1.06 authors CommandSet references without a matching definition.
SOURCE_NULL_COMMAND_SETS: Mapping[str, tuple[tuple[str, str], ...]] = {
    "factionisengard": (
        (
            "IsengardFortressExpansionPadSideCommandSet",
            "retail 1.06 authors no CommandSet definition for the Isengard "
            "fortress side expansion pad",
        ),
    ),
}

# The BFME2 1.06 skirmish shell plays one loop set while loading and one in
# the shell/game flow; both are engine-level constants, so they are declared
# here as caller-owned policy rather than guessed from INI traversal.
MUSIC_ROOTS: Mapping[str, tuple[tuple[str, str], ...]] = {
    key: (
        ("Shell2Music", "skirmish shell music loop"),
        ("Shell2MusicForLoadScreen", "skirmish load-screen music loop"),
    )
    for key in IMPLICIT_OBJECT_ROOTS
}


@dataclass(frozen=True, slots=True)
class FactionPolicyProfile:
    """Game-identity-selected curated allowances for faction analysis."""

    implicit_object_roots: Mapping[str, tuple[tuple[str, str], ...]]
    source_null_mapped_image_textures: Mapping[
        str, tuple[tuple[str, str], ...]
    ]
    source_null_command_sets: Mapping[str, tuple[tuple[str, str], ...]]
    music_roots: Mapping[str, tuple[tuple[str, str], ...]]


# RotWK fortresses share BFME2's composite shape: the map-placed camp object
# unpacks into an engine-spawned citadel and expansion pads that no CommandSet
# reaches, so each playable RotWK faction curates the same implicit roots.
# Angmar's members are read from angmarfortress.ini (AngmarFortressCitadel
# carries AngmarFortressCommandSet, the porter/hero producer surface).
ROTWK_IMPLICIT_OBJECT_ROOTS: Mapping[str, tuple[tuple[str, str], ...]] = {
    "factionangmar": (
        ("AngmarFortressCenterGeneric", "fortress-composite-center"),
        ("AngmarFortressCitadel", "fortress-composite-citadel"),
        ("AngmarFortressExpansionPadCorner", "fortress-composite-corner-pad"),
        ("AngmarFortressExpansionPadSide", "fortress-composite-side-pad"),
    ),
}


# Castle-map wall defenses (owner 2026-08-22). Minas Tirith, Carn Dum and Dol
# Guldur place wall-mounted defenses and upgrade slots that no faction's
# command set reaches (they are map-placed civilian objects), so the faction
# cook never compiled them and the runtime seeded them as inert props
# (`CASTLE_WALL_DEFENSE_STALE ... missing-playable-structure-document`). Each
# is declared a root of the castle's own faction; the runtime resolves
# structure documents across every mounted pack. Retail authors them three
# ways: bow towers carry a weapon outright; Minas slots (`MinisWallAUpgrade`,
# GondorCastleUpgrade children) gain theirs through the player's
# Trebuchet/Postern/Garrison upgrade (`WeaponSet Conditions = PLAYER_UPGRADE`);
# catapult mounts grant `Upgrade_TrebuchetTurret` on creation and spawn a
# slaved trebuchet (`ObjectCreationUpgrade`).
CASTLE_MAP_WALL_DEFENSE_ROOTS: Mapping[str, tuple[tuple[str, str], ...]] = {
    "factionmen": (
        ("MinisWallAUpgrade", "castle-map-wall-defense"),
        ("MinisWallAUpgradeNoGate", "castle-map-wall-defense"),
    ),
    "factionangmar": (
        ("AngmarWallCatapultCarnDum", "castle-map-wall-defense"),
        ("AngmarWallTowerCarnDum", "castle-map-wall-defense"),
    ),
    "factionmordor": (
        ("DoGoldurWallCatapultSmall", "castle-map-wall-defense"),
        ("DoGoldurWallTowerSmall", "castle-map-wall-defense"),
    ),
}


def _with_castle_map_wall_defenses(
    roots: Mapping[str, tuple[tuple[str, str], ...]],
) -> dict[str, tuple[tuple[str, str], ...]]:
    merged = dict(roots)
    for template, extra in CASTLE_MAP_WALL_DEFENSE_ROOTS.items():
        merged[template] = (*merged.get(template, ()), *extra)
    return merged


# RotWK reuses the BFME2 base-faction fortress composites and retail-absent UI
# texture nulls, plus Angmar's expansion fortress roots and the castle-map
# wall defenses above.
_ROTWK_IMPLICIT_OBJECT_ROOTS: Mapping[str, tuple[tuple[str, str], ...]] = (
    _with_castle_map_wall_defenses({
        **IMPLICIT_OBJECT_ROOTS,
        **ROTWK_IMPLICIT_OBJECT_ROOTS,
    })
)
_ROTWK_SOURCE_NULL_MAPPED_IMAGE_TEXTURES: Mapping[
    str, tuple[tuple[str, str], ...]
] = {
    key: _RETAIL_ABSENT_MAPPED_IMAGE_TEXTURES
    for key in _ROTWK_IMPLICIT_OBJECT_ROOTS
}
_ROTWK_MUSIC_ROOTS: Mapping[str, tuple[tuple[str, str], ...]] = {
    key: MUSIC_ROOTS[next(iter(MUSIC_ROOTS))]
    for key in _ROTWK_IMPLICIT_OBJECT_ROOTS
}

FACTION_POLICY_PROFILES: Mapping[str, FactionPolicyProfile] = {
    "bfme2": FactionPolicyProfile(
        implicit_object_roots=IMPLICIT_OBJECT_ROOTS,
        source_null_mapped_image_textures=SOURCE_NULL_MAPPED_IMAGE_TEXTURES,
        source_null_command_sets=SOURCE_NULL_COMMAND_SETS,
        music_roots=MUSIC_ROOTS,
    ),
    "rotwk": FactionPolicyProfile(
        implicit_object_roots=_ROTWK_IMPLICIT_OBJECT_ROOTS,
        source_null_mapped_image_textures=_ROTWK_SOURCE_NULL_MAPPED_IMAGE_TEXTURES,
        source_null_command_sets=SOURCE_NULL_COMMAND_SETS,
        music_roots=_ROTWK_MUSIC_ROOTS,
    ),
}


def _profile(game: str) -> FactionPolicyProfile:
    try:
        return FACTION_POLICY_PROFILES[game.casefold().strip()]
    except (AttributeError, KeyError) as exc:
        raise FactionPolicyError(
            f"unsupported faction policy profile: {game!r}"
        ) from exc


def implicit_object_roots(
    player_template: str, *, game: str = "bfme2"
) -> tuple[tuple[str, str], ...]:
    """Return the curated implicit census roots for one PlayerTemplate identity."""

    if not player_template or not isinstance(player_template, str):
        raise FactionPolicyError("player template identity is invalid")
    profile = _profile(game)
    roots = profile.implicit_object_roots.get(player_template.casefold())
    if roots is None:
        if game.casefold().strip() == "rotwk":
            # Unknown expansion templates keep empty roots rather than failing
            # the whole census before discovery can name them.
            return ()
        raise FactionPolicyError(
            f"faction has no curated implicit census roots: {player_template}"
        )
    return roots


def source_null_mapped_image_textures(
    player_template: str,
    *,
    game: str = "bfme2",
) -> tuple[tuple[str, str], ...]:
    """Return curated retail-absent MappedImage textures for one faction."""

    if not player_template or not isinstance(player_template, str):
        raise FactionPolicyError("player template identity is invalid")
    profile = _profile(game)
    entries = profile.source_null_mapped_image_textures.get(player_template.casefold())
    if entries is None:
        if game.casefold().strip() == "rotwk":
            return _RETAIL_ABSENT_MAPPED_IMAGE_TEXTURES
        raise FactionPolicyError(
            "faction has no curated source-null MappedImage texture policy: "
            f"{player_template}"
        )
    return entries


def source_null_command_sets(
    player_template: str, *, game: str = "bfme2"
) -> tuple[tuple[str, str], ...]:
    """Return curated retail-absent CommandSet references for one faction."""

    if not player_template or not isinstance(player_template, str):
        raise FactionPolicyError("player template identity is invalid")
    profile = _profile(game)
    key = player_template.casefold()
    if key not in profile.implicit_object_roots:
        if game.casefold().strip() == "rotwk":
            return ()
        raise FactionPolicyError(
            f"faction has no curated source-null CommandSet policy: {player_template}"
        )
    return profile.source_null_command_sets.get(key, ())


def music_roots(
    player_template: str, *, game: str = "bfme2"
) -> tuple[tuple[str, str], ...]:
    """Return the engine-level skirmish music roots for one faction."""

    if not player_template or not isinstance(player_template, str):
        raise FactionPolicyError("player template identity is invalid")
    profile = _profile(game)
    entries = profile.music_roots.get(player_template.casefold())
    if entries is None:
        if game.casefold().strip() == "rotwk":
            return ()
        raise FactionPolicyError(
            f"faction has no curated music root policy: {player_template}"
        )
    return entries


__all__ = [
    "FACTION_POLICY_PROFILES",
    "FactionPolicyError",
    "FactionPolicyProfile",
    "IMPLICIT_OBJECT_ROOTS",
    "MUSIC_ROOTS",
    "ROTWK_IMPLICIT_OBJECT_ROOTS",
    "SOURCE_NULL_COMMAND_SETS",
    "SOURCE_NULL_MAPPED_IMAGE_TEXTURES",
    "implicit_object_roots",
    "music_roots",
    "source_null_command_sets",
    "source_null_mapped_image_textures",
]
