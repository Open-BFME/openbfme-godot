"""Supported retail game identities and private workspace routing."""

from __future__ import annotations

from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class RetailGame:
    id: str
    display_name: str
    executable_names: tuple[str, ...]
    patch_archives: tuple[str, ...]
    patch_markers: tuple[str, ...]


RETAIL_GAMES = {
    "bfme2": RetailGame(
        id="bfme2",
        display_name="The Battle for Middle-earth II",
        executable_names=("lotrbfme2.exe",),
        patch_archives=(
            "_patch106.big",
            "_patch105.big",
            "_patch104.big",
            "_patch103.big",
            "_patch102.big",
            "_patch101.big",
        ),
        patch_markers=("1.06", "1.05", "1.04", "1.03", "1.02", "1.01"),
    ),
    "rotwk": RetailGame(
        id="rotwk",
        display_name="The Rise of the Witch-king",
        executable_names=("lotrbfme2ep1.exe",),
        patch_archives=("__patch202.big", "_patch201.big"),
        patch_markers=("2.02", "2.01"),
    ),
}

RETAIL_GAME_IDS = tuple(RETAIL_GAMES)


def retail_game(value: str) -> RetailGame:
    """Return a validated canonical retail-game definition."""

    key = value.casefold().strip()
    try:
        return RETAIL_GAMES[key]
    except KeyError as exc:
        raise ValueError(f"unsupported retail game: {value!r}") from exc


def workspace_root(state_root: Path, game_id: str) -> Path:
    """Keep the established BFME2 paths while isolating expansion state."""

    game = retail_game(game_id)
    return state_root if game.id == "bfme2" else state_root / "editions" / game.id
