"""InstallCatalog-compatible views of one sealed effective-assets manifest.

RotWK's canonical layered oracle can contain winning archive members that are
not present in the synthetic junction install used only for retail discovery.
Once ``verify_effective_assets`` has authenticated the tree, its manifest can
either be used as an exact byte catalog or as a non-semantic asset overlay on
the layered install catalog.  The overlay matters because SAGE INI layering is
additive: replacing the install catalog with one last-winner file per path
would discard lower-layer object bodies and leave only patch ``ObjectReskin``
or ``ChildObject`` fragments.  Only missing paths outside ``data/ini`` and
``data/lotr.str`` are admitted by that mode.
"""

from __future__ import annotations

from pathlib import Path

from .big import sha256_file
from .catalog import ArchiveInfo, CatalogEntry, InstallCatalog
from .paths import safe_relative_parts
from .util import read_json


_SYNTHETIC_ARCHIVE = ".openbfme/sealed-effective-assets.bin"


class _EffectiveAssetsReader:
    def __init__(self, root: Path) -> None:
        self.root = root

    def read_entry(self, entry: object, *, max_bytes: int) -> bytes:
        name = getattr(entry, "name", None)
        size = getattr(entry, "size", None)
        if not isinstance(name, str) or not isinstance(size, int):
            raise ValueError("effective-assets entry is invalid")
        if size > max_bytes:
            raise ValueError("effective-assets entry exceeds read bound")
        path = self.root.joinpath(*safe_relative_parts(name))
        data = path.read_bytes()
        if len(data) != size:
            raise ValueError(f"effective-assets entry size drifted: {name}")
        return data


class EffectiveAssetsCatalog(InstallCatalog):
    """Exact sealed catalog, or a sealed asset overlay on a layered catalog."""

    def __init__(
        self,
        effective_assets_root: Path | str,
        *,
        base_catalog: InstallCatalog | None = None,
    ) -> None:
        root = Path(effective_assets_root).expanduser().resolve()
        manifest_path = root / ".openbfme" / "manifest.json"
        manifest = read_json(manifest_path)
        if not isinstance(manifest, dict):
            raise ValueError("effective-assets manifest root is invalid")
        catalog = manifest.get("catalog")
        files = manifest.get("files")
        identity = catalog.get("identity_sha256") if isinstance(catalog, dict) else None
        aggregate = manifest.get("aggregate_sha256")
        if (
            not isinstance(identity, str)
            or len(identity) != 64
            or not isinstance(aggregate, str)
            or len(aggregate) != 64
            or not isinstance(files, list)
            or not files
        ):
            raise ValueError("effective-assets manifest catalog identity is invalid")

        entries: list[CatalogEntry] = []
        offset = 16
        keys: set[str] = set()
        for index, row in enumerate(files):
            if not isinstance(row, dict):
                raise ValueError(f"effective-assets manifest file {index} is invalid")
            raw_path = row.get("path")
            size = row.get("size")
            if not isinstance(raw_path, str):
                raise ValueError(f"effective-assets manifest file {index} path is invalid")
            path = "/".join(safe_relative_parts(raw_path))
            if path != raw_path.replace("\\", "/"):
                raise ValueError(f"effective-assets manifest path is not canonical: {raw_path}")
            if isinstance(size, bool) or not isinstance(size, int) or size < 0:
                raise ValueError(f"effective-assets manifest size is invalid: {path}")
            key = path.casefold()
            if key in keys:
                raise ValueError(f"effective-assets manifest path collides: {path}")
            keys.add(key)
            semantic_source = (
                key.startswith("data/ini/")
                or key == "data/lotr.str"
            )
            missing_from_base = (
                base_catalog is None or base_catalog.resolve_exact(path) is None
            )
            if base_catalog is None or (missing_from_base and not semantic_source):
                entries.append(
                    CatalogEntry(_SYNTHETIC_ARCHIVE, path, offset, size, 0)
                )
            offset += size

        archive = ArchiveInfo(
            relative_path=_SYNTHETIC_ARCHIVE,
            size=offset,
            mtime_ns=manifest_path.stat().st_mtime_ns,
            magic="OPENBFME",
            header_size=16,
            entry_count=len(entries),
            directory_sha256=aggregate,
        )
        if base_catalog is None:
            install_root = root
            archives = (archive,)
            combined_entries = tuple(entries)
            source_policy = None
            payload_samples = None
        else:
            install_root = base_catalog.install_root
            archives = (*base_catalog.archives, archive)
            combined_entries = (*base_catalog.entries, *entries)
            source_policy = base_catalog.source_policy
            payload_samples = base_catalog.payload_samples
        super().__init__(
            install_root,
            tuple(archives),
            tuple(combined_entries),
            source_policy=source_policy,
            payload_samples=payload_samples,
        )
        self._base_catalog = base_catalog
        self._sealed_identity = identity.casefold()
        self._sealed_aggregate = aggregate.casefold()
        self._reader = _EffectiveAssetsReader(root)

    def identity_sha256(self) -> str:
        if self._base_catalog is None:
            return self._sealed_identity
        return super().identity_sha256()

    def stale_reasons(self) -> list[str]:
        # The caller verifies every manifest hash before constructing this view.
        if self._base_catalog is None:
            return []
        return self._base_catalog.stale_reasons()

    def archive_sha256(self, relative_path: str) -> str:
        if relative_path == _SYNTHETIC_ARCHIVE:
            return self._sealed_aggregate
        if self._base_catalog is None:
            raise ValueError(f"unknown effective-assets archive: {relative_path}")
        return sha256_file(self._base_catalog.install_root / Path(relative_path))

    def open_archive_for(self, entry: CatalogEntry) -> _EffectiveAssetsReader:
        if entry.archive == _SYNTHETIC_ARCHIVE:
            return self._reader
        if self._base_catalog is None:
            raise ValueError(f"unknown effective-assets archive: {entry.archive}")
        return self._base_catalog.open_archive_for(entry)  # type: ignore[return-value]

    def as_entry(self, entry: CatalogEntry) -> CatalogEntry:
        if entry.archive == _SYNTHETIC_ARCHIVE:
            return entry
        if self._base_catalog is None:
            return entry
        return self._base_catalog.as_entry(entry)  # type: ignore[return-value]


__all__ = ["EffectiveAssetsCatalog"]
