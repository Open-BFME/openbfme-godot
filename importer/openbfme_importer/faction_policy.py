"""Owner-curated per-faction census policy shared by faction import callers."""

from __future__ import annotations

from collections.abc import Mapping

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


# Retail 1.06 authors button images whose atlas texture no shipped archive
# contains (the spawn-orcs/test buttons carry "; @todo get image" markers).
# Each entry is consumed only when the texture is genuinely absent; a texture
# which later resolves fails the census closed instead of being masked.
SOURCE_NULL_MAPPED_IMAGE_TEXTURES: Mapping[str, tuple[tuple[str, str], ...]] = {
    key: (
        (
            "SCUserInterface_001.tga",
            "retail 1.06 authors the SMSpawnOrcs MappedImage but ships no "
            "compiled atlas for SCUserInterface_001.tga",
        ),
        (
            "TrollPickup_but.tga",
            "retail 1.06 authors the SCGrabPassenger MappedImage but ships no "
            "compiled atlas for TrollPickup_but.tga",
        ),
    )
    for key in IMPLICIT_OBJECT_ROOTS
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


def implicit_object_roots(player_template: str) -> tuple[tuple[str, str], ...]:
    """Return the curated implicit census roots for one PlayerTemplate identity."""

    if not player_template or not isinstance(player_template, str):
        raise FactionPolicyError("player template identity is invalid")
    roots = IMPLICIT_OBJECT_ROOTS.get(player_template.casefold())
    if roots is None:
        raise FactionPolicyError(
            f"faction has no curated implicit census roots: {player_template}"
        )
    return roots


def source_null_mapped_image_textures(
    player_template: str,
) -> tuple[tuple[str, str], ...]:
    """Return curated retail-absent MappedImage textures for one faction."""

    if not player_template or not isinstance(player_template, str):
        raise FactionPolicyError("player template identity is invalid")
    entries = SOURCE_NULL_MAPPED_IMAGE_TEXTURES.get(player_template.casefold())
    if entries is None:
        raise FactionPolicyError(
            "faction has no curated source-null MappedImage texture policy: "
            f"{player_template}"
        )
    return entries


def source_null_command_sets(player_template: str) -> tuple[tuple[str, str], ...]:
    """Return curated retail-absent CommandSet references for one faction."""

    if not player_template or not isinstance(player_template, str):
        raise FactionPolicyError("player template identity is invalid")
    if player_template.casefold() not in IMPLICIT_OBJECT_ROOTS:
        raise FactionPolicyError(
            f"faction has no curated source-null CommandSet policy: {player_template}"
        )
    return SOURCE_NULL_COMMAND_SETS.get(player_template.casefold(), ())


__all__ = [
    "FactionPolicyError",
    "IMPLICIT_OBJECT_ROOTS",
    "SOURCE_NULL_COMMAND_SETS",
    "SOURCE_NULL_MAPPED_IMAGE_TEXTURES",
    "implicit_object_roots",
    "source_null_command_sets",
    "source_null_mapped_image_textures",
]
