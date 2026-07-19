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


__all__ = [
    "FactionPolicyError",
    "IMPLICIT_OBJECT_ROOTS",
    "implicit_object_roots",
]
