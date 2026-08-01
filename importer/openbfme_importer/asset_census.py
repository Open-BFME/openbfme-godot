"""Deterministic, payload-free inventory of an install catalog's winners.

The census deliberately stops at BIG directory metadata.  It classifies the
case-insensitive winning virtual-path view without opening an archive or
reading an entry payload.  Families describe the first conversion lane that a
winning file needs; unknown formats remain visible in the explicit ``other``
family instead of disappearing from totals.
"""

from __future__ import annotations

from collections import defaultdict
from dataclasses import dataclass
from pathlib import PurePosixPath
from typing import Iterable, Literal

from .catalog import CatalogEntry, InstallCatalog
from .paths import safe_relative_parts


AssetFamily = Literal[
    "map",
    "w3d",
    "texture",
    "sound",
    "music",
    "ui",
    "data",
    "other",
]

ASSET_FAMILIES: tuple[AssetFamily, ...] = (
    "map",
    "w3d",
    "texture",
    "sound",
    "music",
    "ui",
    "data",
    "other",
)

_TEXTURE_EXTENSIONS = frozenset(
    {".bmp", ".dds", ".jpeg", ".jpg", ".pcx", ".png", ".tga"}
)
_AUDIO_EXTENSIONS = frozenset(
    {".ac3", ".aif", ".aiff", ".au", ".flac", ".mp3", ".ogg", ".snd", ".wav"}
)
_UI_EXTENSIONS = frozenset({".apt", ".const", ".wnd"})
_DATA_EXTENSIONS = frozenset(
    {".cfg", ".csf", ".inc", ".ini", ".lua", ".skudef", ".str", ".txt", ".xml"}
)
_UI_PATH_SEGMENTS = frozenset({"apt", "shell", "ui", "window", "windows"})
_UI_ARCHIVES = frozenset({"window.big"})


@dataclass(frozen=True, slots=True)
class AssetExtensionTotals:
    """File and byte totals for one normalized final extension."""

    extension: str
    file_count: int
    total_bytes: int

    def neutral(self) -> dict[str, object]:
        return {
            "extension": self.extension,
            "fileCount": self.file_count,
            "bytes": self.total_bytes,
        }


@dataclass(frozen=True, slots=True)
class AssetFamilyTotals:
    """Aggregate inventory for one explicit conversion family."""

    family: AssetFamily
    file_count: int
    total_bytes: int
    extensions: tuple[AssetExtensionTotals, ...]

    def neutral(self) -> dict[str, object]:
        return {
            "family": self.family,
            "fileCount": self.file_count,
            "bytes": self.total_bytes,
            "extensions": [item.neutral() for item in self.extensions],
        }


@dataclass(frozen=True, slots=True)
class AssetInventoryEntry:
    """One effective case-insensitive catalog winner."""

    virtual_path: str
    archive: str
    precedence: int
    size: int
    extension: str
    family: AssetFamily
    classification_rule: str

    def neutral(self) -> dict[str, object]:
        return {
            "virtualPath": self.virtual_path,
            "archive": self.archive,
            "precedence": self.precedence,
            "bytes": self.size,
            "extension": self.extension,
            "family": self.family,
            "classificationRule": self.classification_rule,
        }


@dataclass(frozen=True, slots=True)
class AssetCensusReport:
    """Immutable full winning-catalog inventory and accounting evidence."""

    entries: tuple[AssetInventoryEntry, ...]
    families: tuple[AssetFamilyTotals, ...]
    extensions: tuple[AssetExtensionTotals, ...]
    catalog_entry_count: int
    catalog_entry_bytes: int

    @property
    def winner_file_count(self) -> int:
        return len(self.entries)

    @property
    def winner_bytes(self) -> int:
        return sum(item.size for item in self.entries)

    @property
    def shadowed_entry_count(self) -> int:
        return self.catalog_entry_count - self.winner_file_count

    @property
    def shadowed_entry_bytes(self) -> int:
        return self.catalog_entry_bytes - self.winner_bytes

    def neutral(self) -> dict[str, object]:
        other = next(item for item in self.families if item.family == "other")
        return {
            "schema": "openbfme.asset-census",
            "schemaVersion": 0,
            "selection": {
                "kind": "case-insensitive-catalog-winners",
                "payloadBytesRead": 0,
            },
            "totals": {
                "catalogEntryCount": self.catalog_entry_count,
                "catalogEntryBytes": self.catalog_entry_bytes,
                "winnerFileCount": self.winner_file_count,
                "winnerBytes": self.winner_bytes,
                "shadowedEntryCount": self.shadowed_entry_count,
                "shadowedEntryBytes": self.shadowed_entry_bytes,
                "accountedWinnerFileCount": sum(
                    item.file_count for item in self.families
                ),
                "unclassifiedWinnerFileCount": 0,
                "otherFileCount": other.file_count,
                "otherBytes": other.total_bytes,
            },
            "extensions": [item.neutral() for item in self.extensions],
            "families": [item.neutral() for item in self.families],
            "entries": [item.neutral() for item in self.entries],
        }


def _canonical_path(value: str, *, kind: str) -> str:
    try:
        return "/".join(safe_relative_parts(value))
    except (TypeError, ValueError) as exc:
        raise ValueError(f"unsafe catalog {kind} path: {value!r}") from exc


def _validate_entry(entry: CatalogEntry) -> tuple[str, str]:
    if not isinstance(entry, CatalogEntry):
        raise TypeError("catalog entries must be CatalogEntry values")
    for label, value in (
        ("offset", entry.offset),
        ("size", entry.size),
        ("precedence", entry.precedence),
    ):
        if isinstance(value, bool) or not isinstance(value, int) or value < 0:
            raise ValueError(
                f"catalog entry has invalid {label}: {entry.archive}:{entry.name}"
            )
    return (
        _canonical_path(entry.name, kind="virtual"),
        _canonical_path(entry.archive, kind="archive"),
    )


def _classification(
    virtual_path: str,
    archive: str,
) -> tuple[AssetFamily, str, str]:
    path = PurePosixPath(virtual_path)
    extension = path.suffix.casefold()
    folded_parts = tuple(part.casefold() for part in path.parts)
    archive_name = PurePosixPath(archive).name.casefold()

    if extension == ".map":
        return "map", "extension:.map", extension
    if extension == ".w3d":
        return "w3d", "extension:.w3d", extension
    if extension in _TEXTURE_EXTENSIONS:
        return "texture", "extension:texture", extension
    if extension in _AUDIO_EXTENSIONS:
        if archive_name == "music.big":
            return "music", "audio-archive:music.big", extension
        if "music" in folded_parts or folded_parts[:3] == (
            "data",
            "audio",
            "tracks",
        ):
            return "music", "audio-path:music-or-tracks", extension
        return "sound", "extension:audio", extension
    if extension in _UI_EXTENSIONS:
        return "ui", "extension:ui", extension
    if archive_name in _UI_ARCHIVES or any(
        part in _UI_PATH_SEGMENTS for part in folded_parts[:-1]
    ):
        return "ui", "path-or-archive:ui", extension
    if extension in _DATA_EXTENSIONS:
        return "data", "extension:data", extension
    return "other", "explicit-fallback", extension


def _winner_entries(catalog: InstallCatalog) -> tuple[CatalogEntry, ...]:
    representatives: dict[str, str] = {}
    for entry in catalog.entries:
        canonical_name, _ = _validate_entry(entry)
        representatives.setdefault(canonical_name.casefold(), canonical_name)

    winners: list[CatalogEntry] = []
    for key in sorted(representatives):
        winner = catalog.resolve_exact(representatives[key])
        if winner is None:
            raise ValueError(f"catalog cannot resolve winning virtual path: {key}")
        canonical_name, _ = _validate_entry(winner)
        if canonical_name.casefold() != key:
            raise ValueError(f"catalog resolved the wrong winning virtual path: {key}")
        winners.append(winner)
    return tuple(winners)


def _extension_totals(
    entries: Iterable[AssetInventoryEntry],
) -> tuple[AssetExtensionTotals, ...]:
    counts: dict[str, int] = defaultdict(int)
    byte_totals: dict[str, int] = defaultdict(int)
    for entry in entries:
        counts[entry.extension] += 1
        byte_totals[entry.extension] += entry.size
    return tuple(
        AssetExtensionTotals(extension, counts[extension], byte_totals[extension])
        for extension in sorted(counts)
    )


def census_assets(catalog: InstallCatalog) -> AssetCensusReport:
    """Inventory every case-insensitive winner without reading payload bytes."""

    if not isinstance(catalog, InstallCatalog):
        raise TypeError("asset census requires an InstallCatalog")

    validated_entries = tuple(
        (entry, *_validate_entry(entry)) for entry in catalog.entries
    )
    inventory: list[AssetInventoryEntry] = []
    for winner in _winner_entries(catalog):
        virtual_path, archive = _validate_entry(winner)
        family, rule, extension = _classification(virtual_path, archive)
        inventory.append(
            AssetInventoryEntry(
                virtual_path=virtual_path,
                archive=archive,
                precedence=winner.precedence,
                size=winner.size,
                extension=extension,
                family=family,
                classification_rule=rule,
            )
        )

    selected = tuple(
        sorted(
            inventory,
            key=lambda item: (
                item.virtual_path.casefold(),
                item.virtual_path,
                item.archive.casefold(),
                item.archive,
            ),
        )
    )
    family_entries = {
        family: tuple(item for item in selected if item.family == family)
        for family in ASSET_FAMILIES
    }
    families = tuple(
        AssetFamilyTotals(
            family=family,
            file_count=len(family_entries[family]),
            total_bytes=sum(item.size for item in family_entries[family]),
            extensions=_extension_totals(family_entries[family]),
        )
        for family in ASSET_FAMILIES
    )
    return AssetCensusReport(
        entries=selected,
        families=families,
        extensions=_extension_totals(selected),
        catalog_entry_count=len(validated_entries),
        catalog_entry_bytes=sum(entry.size for entry, _, _ in validated_entries),
    )
