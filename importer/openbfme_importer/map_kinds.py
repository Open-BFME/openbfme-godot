"""Deterministic player-facing classification for effective retail map paths."""

from __future__ import annotations


MAP_KINDS = frozenset(
    {"multiplayer", "campaign", "tutorial", "wotr", "test", "other"}
)


def canonical_map_path(value: str) -> str:
    return value.replace("\\", "/").strip("/").casefold()


def classify_map_path(
    virtual_path: str, *, multiplayer_paths: frozenset[str] = frozenset()
) -> str:
    """Classify one effective ``.map`` winner without opening retail payloads."""

    path = canonical_map_path(virtual_path)
    multiplayer = {canonical_map_path(value) for value in multiplayer_paths}
    name = path.rsplit("/", 1)[-1]
    folder = path.rsplit("/", 1)[0].rsplit("/", 1)[-1]
    if folder in {"camera_demo", "createahero", "shellmap1", "shellmapbackup"}:
        return "other"
    if "tutorial" in folder or "tutorial" in name:
        return "tutorial"
    if folder.startswith("map wor "):
        return "wotr"
    if "test" in folder or "test" in name:
        return "test"
    if folder.startswith(("map good ", "map evil ", "map ang ", "cin ")):
        return "campaign"
    if path in multiplayer or folder.startswith("map mp "):
        return "multiplayer"
    return "other"


def sage_profile_for_map(virtual_path: str, kind: str) -> str:
    """Select the existing strict SAGE profile appropriate to a map path."""

    path = canonical_map_path(virtual_path)
    if kind == "multiplayer":
        return "multiplayer"
    if path.startswith(("libraries/", "bases/")):
        return "library"
    if any(token in path for token in ("/shellmap", "/camera_demo", "/createahero")):
        return "placeholder"
    return "scenario"
