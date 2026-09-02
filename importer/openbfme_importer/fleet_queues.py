"""Generate private whole-corpus asset, map, and screen fleet queues.

The denominator is the case-insensitive winning view of the layered BIG
catalog.  Completion is derived only from selected content-pack provenance;
an output name that merely resembles a retail source never counts.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Iterable, Mapping, Sequence

from .asset_census import AssetInventoryEntry, census_assets
from .catalog import CatalogEntry, InstallCatalog
from .map_census import MAPCACHE_VIRTUAL_PATH, parse_mapcache_bytes
from .paths import safe_relative_parts
from .sage_particles import _lines as ini_lines
from .util import write_json_atomic


KINDS = ("assets", "maps", "screens")
IMAGE_EXTENSIONS = frozenset({".dds", ".tga", ".jpg", ".png"})
AUDIO_EXTENSIONS = frozenset({".wav", ".mp3"})
ASSET_EXTENSIONS = frozenset({".w3d", ".bse", *IMAGE_EXTENSIONS, *AUDIO_EXTENSIONS})
SCREEN_EXTENSIONS = frozenset({".apt", ".wnd"})
PRODUCT_BAR = {
    "w3d": 14_539,
    "images": 9_063,
    "audio": 19_194,
    "bseParticles": 233,
    "apt": 86,
    "wnd": 18,
    "maps": 419,
}
_PLAYABLE_KIND_OF = frozenset(
    {
        "BASE_FOUNDATION",
        "CAVALRY",
        "COMMANDCENTER",
        "HERO",
        "HORDE",
        "INFANTRY",
        "MACHINE",
        "MONSTER",
        "SELECTABLE",
        "STRUCTURE",
    }
)
_TOKEN = re.compile(r"[A-Za-z0-9_./\\-]+")


@dataclass(frozen=True, slots=True)
class OutputProvenance:
    path: str
    size: int | None
    sha256: str | None


@dataclass(frozen=True, slots=True)
class PackProvenance:
    pack: str
    root: Path
    converter: str
    source_path: str | None
    source_sha256: str | None
    source_archive: str | None
    source_offset: int | None
    source_size: int | None
    outputs: tuple[OutputProvenance, ...]


def _canonical(value: str) -> str:
    return "/".join(safe_relative_parts(value.replace("\\", "/"))).casefold()


def _json(path: Path) -> object:
    with path.open("r", encoding="utf-8") as stream:
        return json.load(stream)


def _selected_pack_paths(content_root: Path) -> tuple[tuple[str, Path], ...]:
    selection_path = content_root / "selection.json"
    if not selection_path.is_file():
        return ()
    selection = _json(selection_path)
    if not isinstance(selection, Mapping):
        raise ValueError("content-pack selection root must be an object")
    raw = [selection.get("activePack"), *(selection.get("supplementalPacks") or [])]
    selected: list[tuple[str, Path]] = []
    seen: set[str] = set()
    root = content_root.resolve()
    for value in raw:
        if not isinstance(value, str) or not value.strip():
            raise ValueError("content-pack selection contains an invalid pack path")
        canonical = "/".join(safe_relative_parts(value))
        key = canonical.casefold()
        if key in seen:
            continue
        seen.add(key)
        pack_root = (root / Path(*PurePosixPath(canonical).parts)).resolve()
        if root not in pack_root.parents:
            raise ValueError(f"selected pack escapes content root: {value}")
        selected.append((canonical, pack_root))
    return tuple(selected)


def selected_provenance(content_root: Path | str) -> tuple[PackProvenance, ...]:
    """Read source/output ownership from selected pack manifests."""

    records: list[PackProvenance] = []
    for pack, root in _selected_pack_paths(Path(content_root)):
        manifest_path = root / "provenance" / "manifest.json"
        if not manifest_path.is_file():
            continue
        manifest = _json(manifest_path)
        entries = manifest.get("entries", []) if isinstance(manifest, Mapping) else []
        if not isinstance(entries, list):
            raise ValueError(f"selected pack manifest entries are invalid: {pack}")
        for row in entries:
            if not isinstance(row, Mapping):
                continue
            source = row.get("source")
            if not isinstance(source, Mapping):
                continue
            path_value = next(
                (
                    source.get(key)
                    for key in ("virtual_path", "virtualPath", "source_path", "sourcePath", "path")
                    if isinstance(source.get(key), str) and source.get(key)
                ),
                None,
            )
            digest_value = next(
                (
                    source.get(key)
                    for key in ("sha256", "source_sha256", "sourceSha256", "digest", "sourceDigest")
                    if isinstance(source.get(key), str)
                    and re.fullmatch(r"[0-9A-Fa-f]{64}", str(source.get(key)))
                ),
                None,
            )
            outputs: list[OutputProvenance] = []
            raw_outputs = row.get("outputs", [])
            if isinstance(raw_outputs, list):
                for output in raw_outputs:
                    value = output.get("path") if isinstance(output, Mapping) else output
                    if isinstance(value, str) and value:
                        size = output.get("size") if isinstance(output, Mapping) else None
                        digest = output.get("sha256") if isinstance(output, Mapping) else None
                        outputs.append(
                            OutputProvenance(
                                path="/".join(safe_relative_parts(value)),
                                size=(size if isinstance(size, int) and not isinstance(size, bool) and size >= 0 else None),
                                sha256=(str(digest).casefold() if isinstance(digest, str) and re.fullmatch(r"[0-9A-Fa-f]{64}", digest) else None),
                            )
                        )
            records.append(
                PackProvenance(
                    pack=pack,
                    root=root,
                    converter=str(row.get("converter") or "unknown"),
                    source_path=_canonical(str(path_value)) if path_value else None,
                    source_sha256=str(digest_value).casefold() if digest_value else None,
                    source_archive=(
                        str(source.get("archive")).replace("\\", "/").casefold()
                        if isinstance(source.get("archive"), str)
                        else None
                    ),
                    source_offset=(
                        source.get("offset")
                        if isinstance(source.get("offset"), int)
                        and not isinstance(source.get("offset"), bool)
                        and source.get("offset") >= 0
                        else None
                    ),
                    source_size=(
                        source.get("size")
                        if isinstance(source.get("size"), int)
                        and not isinstance(source.get("size"), bool)
                        and source.get("size") >= 0
                        else None
                    ),
                    outputs=tuple(outputs),
                )
            )
    return tuple(
        sorted(
            records,
            key=lambda item: (
                item.source_path or "",
                item.source_sha256 or "",
                item.source_archive or "",
                item.source_offset if item.source_offset is not None else -1,
                item.pack.casefold(),
                item.converter.casefold(),
                tuple((output.path, output.size, output.sha256) for output in item.outputs),
            ),
        )
    )


def _outputs_exist(record: PackProvenance, kind: str) -> bool:
    existing = [
        output.path
        for output in record.outputs
        if (record.root / Path(*PurePosixPath(output.path).parts)).is_file()
    ]
    if not existing or len(existing) != len(record.outputs):
        return False
    if kind == "maps":
        return any(PurePosixPath(item).name.casefold() == "map.json" for item in existing)
    if kind == "screens":
        return any(PurePosixPath(item).suffix.casefold() == ".json" for item in existing)
    return True


def _read_catalog_entry(catalog: InstallCatalog, entry: CatalogEntry) -> bytes:
    archive = catalog.open_archive_for(entry)
    return archive.read_entry(catalog.as_entry(entry), max_bytes=max(entry.size, 1))


def _completion_index(
    catalog: InstallCatalog,
    entries: Sequence[AssetInventoryEntry],
    provenance: Sequence[PackProvenance],
    kind: str,
) -> tuple[dict[str, PackProvenance], dict[str, str]]:
    by_path: dict[str, list[PackProvenance]] = {}
    digest_only: dict[str, list[PackProvenance]] = {}
    for record in provenance:
        if not _outputs_exist(record, kind):
            continue
        if record.source_path:
            by_path.setdefault(record.source_path, []).append(record)
        elif record.source_sha256:
            digest_only.setdefault(record.source_sha256, []).append(record)
    completed: dict[str, PackProvenance] = {}
    reasons: dict[str, str] = {}
    for item in entries:
        path = _canonical(item.virtual_path)
        candidates = by_path.get(path, [])
        catalog_entry = catalog.resolve_exact(path)
        if catalog_entry is None:
            continue
        accepted = next(
            (
                record
                for record in candidates
                if record.source_sha256 is None
                or _same_catalog_source(record, catalog_entry)
            ),
            None,
        )
        if accepted is not None:
            completed[path] = accepted
            reasons[path] = "selected-pack-source-path-and-catalog-location"
            continue
        needs_digest = bool(digest_only or candidates)
        if not needs_digest:
            continue
        digest = hashlib.sha256(_read_catalog_entry(catalog, catalog_entry)).hexdigest()
        digest_candidates = [
            record for record in candidates if record.source_sha256 == digest
        ] or digest_only.get(digest, [])
        if digest_candidates:
            completed[path] = digest_candidates[0]
            reasons[path] = "selected-pack-source-digest"
    return completed, reasons


def _archive_identity(archive: str, pack: str) -> str:
    parts = archive.replace("\\", "/").casefold().split("/")
    match = re.fullmatch(r"layer-\d{1,4}(?:-([a-z0-9]+))?", parts[0])
    if match is not None:
        label = match.group(1) or "layer"
        return "/".join((label, *parts[1:]))
    game = "rotwk" if pack.casefold().startswith("rotwk-") else "bfme2"
    return "/".join((game, *parts))


def _same_catalog_source(record: PackProvenance, entry: CatalogEntry) -> bool:
    return bool(
        record.source_archive is not None
        and record.source_offset is not None
        and record.source_size is not None
        and _archive_identity(record.source_archive, record.pack)
        == _archive_identity(entry.archive, record.pack)
        and record.source_offset == entry.offset
        and record.source_size == entry.size
    )


def _entry_extension(entry: AssetInventoryEntry) -> str:
    return PurePosixPath(entry.virtual_path).suffix.casefold()


def _entries_for_kind(
    inventory: Sequence[AssetInventoryEntry], kind: str
) -> tuple[AssetInventoryEntry, ...]:
    extensions = {
        "assets": ASSET_EXTENSIONS,
        "maps": frozenset({".map"}),
        "screens": SCREEN_EXTENSIONS,
    }[kind]
    return tuple(item for item in inventory if _entry_extension(item) in extensions)


def _multiplayer_paths(catalog: InstallCatalog) -> set[str]:
    entry = catalog.resolve_exact(MAPCACHE_VIRTUAL_PATH)
    if entry is None:
        return set()
    try:
        records = parse_mapcache_bytes(_read_catalog_entry(catalog, entry))
    except (OSError, ValueError):
        return set()
    return {
        _canonical(str(row["virtualPath"]))
        for row in records
        if bool(row.get("isMultiplayer"))
    }


def _finish_object(tokens: set[str], kinds: set[str], result: set[str]) -> None:
    if kinds & _PLAYABLE_KIND_OF:
        result.update(tokens)


def _playable_reference_tokens(catalog: InstallCatalog) -> set[str]:
    """Collect exact lexical values authored inside playable Object blocks."""

    result: set[str] = set()
    for entry in census_assets(catalog).entries:
        if _entry_extension(entry) not in {".ini", ".inc"}:
            continue
        catalog_entry = catalog.resolve_exact(entry.virtual_path)
        if catalog_entry is None:
            continue
        try:
            lines = ini_lines(_read_catalog_entry(catalog, catalog_entry))
        except (OSError, UnicodeError, ValueError):
            continue
        active = False
        tokens: set[str] = set()
        kinds: set[str] = set()
        for line in lines:
            text = line.text.strip()
            head = text.split(None, 1)[0].casefold() if text else ""
            if line.indent == 0 and head in {"object", "childobject"}:
                if active:
                    _finish_object(tokens, kinds, result)
                active, tokens, kinds = True, set(), set()
                continue
            if active and line.indent == 0 and text.casefold() == "end":
                _finish_object(tokens, kinds, result)
                active, tokens, kinds = False, set(), set()
                continue
            if not active or "=" not in text:
                continue
            field, value = (part.strip() for part in text.split("=", 1))
            values = {token.casefold() for token in _TOKEN.findall(value)}
            tokens.update(values)
            if field.casefold() == "kindof":
                kinds.update(token.upper() for token in values)
        if active:
            _finish_object(tokens, kinds, result)
    return result


def _asset_is_referenced(entry: AssetInventoryEntry, tokens: set[str]) -> bool:
    path = _canonical(entry.virtual_path)
    pure = PurePosixPath(path)
    return bool({path, pure.name.casefold(), pure.stem.casefold()} & tokens)


def _converter(extension: str) -> str:
    return {
        ".w3d": "w3d-bundle",
        ".dds": "texture",
        ".tga": "texture",
        ".jpg": "texture",
        ".png": "texture",
        ".wav": "audio",
        ".mp3": "copy",
        ".bse": "sage-particle-definition",
        ".map": "sage-map",
        ".apt": "sage-apt-screen-runtime",
        ".wnd": "sage-apt-screen-runtime",
    }[extension]


def _rank(
    item: AssetInventoryEntry,
    kind: str,
    multiplayer: set[str],
    references: set[str],
) -> tuple[int, str]:
    path = _canonical(item.virtual_path)
    if kind == "maps":
        return (0, "retail mapcache multiplayer entry") if path in multiplayer else (100, "non-multiplayer effective map")
    if kind == "screens":
        shell_hud = any(token in path for token in ("shell", "hud", "controlbar", "mainmenu"))
        return (0, "shell/HUD screen") if shell_hud else (100, "other screen")
    referenced = _asset_is_referenced(item, references)
    return (0, "referenced by a playable Object") if referenced else (100, "not directly referenced by a playable Object")


def _quoted(value: Path | str) -> str:
    return '"' + str(value).replace('"', '\\"') + '"'


def _oracle(kind: str, item_id: str, install: Path, content_root: Path) -> str:
    return " ".join(
        (
            "python -m openbfme_importer.verify_item",
            "--kind",
            kind,
            "--id",
            _quoted(item_id),
            "--install",
            _quoted(install),
            "--content-root",
            _quoted(content_root),
        )
    )


def _category_counts(inventory: Sequence[AssetInventoryEntry]) -> dict[str, int]:
    counts = {key: 0 for key in PRODUCT_BAR}
    for item in inventory:
        extension = _entry_extension(item)
        if extension == ".w3d":
            counts["w3d"] += 1
        elif extension in IMAGE_EXTENSIONS:
            counts["images"] += 1
        elif extension in AUDIO_EXTENSIONS:
            counts["audio"] += 1
        elif extension == ".bse":
            counts["bseParticles"] += 1
        elif extension == ".apt":
            counts["apt"] += 1
        elif extension == ".wnd":
            counts["wnd"] += 1
        elif extension == ".map":
            counts["maps"] += 1
    return counts


def _denominators(inventory: Sequence[AssetInventoryEntry]) -> dict[str, object]:
    actual = _category_counts(inventory)
    categories = []
    for name, expected in PRODUCT_BAR.items():
        observed = actual[name]
        difference = observed - expected
        explanation = (
            "matches the audited 2.02 v9.7.7 product bar"
            if difference == 0
            else (
                "the supplied layered install/catalog has a different effective winner set; "
                "this machine's known private cache is 2.01-era, while the product bar was "
                "audited over Patch 2.02 v9.7.7"
            )
        )
        categories.append(
            {
                "category": name,
                "productBar": expected,
                "observed": observed,
                "difference": difference,
                "explanation": explanation,
            }
        )
    return {
        "baseline": "Rise of the Witch-king Patch 2.02 v9.7.7 audited product bar",
        "selection": "case-folded effective archive paths; archive-precedence winners only",
        "categories": categories,
    }


def generate_queue_documents(
    catalog: InstallCatalog,
    *,
    install: Path | str,
    content_root: Path | str,
    kinds: Iterable[str] = KINDS,
) -> dict[str, dict[str, object]]:
    """Build deterministic queue documents without writing them."""

    install_path = Path(install).expanduser().resolve()
    content_path = Path(content_root).expanduser().resolve()
    inventory = census_assets(catalog).entries
    provenance = selected_provenance(content_path)
    requested = tuple(kinds)
    unknown = sorted(set(requested) - set(KINDS))
    if unknown:
        raise ValueError(f"unknown queue kind(s): {', '.join(unknown)}")
    multiplayer = _multiplayer_paths(catalog) if "maps" in requested else set()
    references = _playable_reference_tokens(catalog) if "assets" in requested else set()
    denominators = _denominators(inventory)
    documents: dict[str, dict[str, object]] = {}
    for kind in requested:
        entries = _entries_for_kind(inventory, kind)
        completed, _reasons = _completion_index(catalog, entries, provenance, kind)
        rows = []
        for item in entries:
            item_id = _canonical(item.virtual_path)
            if item_id in completed:
                continue
            extension = _entry_extension(item)
            rank, priority = _rank(item, kind, multiplayer, references)
            converter = _converter(extension)
            rows.append(
                {
                    "id": item_id,
                    "title": f"Convert {item_id}",
                    "rank": rank,
                    "detail": (
                        f"{item.size} bytes; archive {item.archive}; converter {converter}; "
                        f"priority: {priority}; no selected pack provenance owns a complete output"
                    ),
                    "oracle": _oracle(kind, item_id, install_path, content_path),
                }
            )
        rows.sort(key=lambda row: (int(row["rank"]), str(row["id"])))
        documents[kind] = {
            "schema": "openbfme.fleet-derived-queue",
            "schemaVersion": 0,
            "kind": kind,
            "total": len(entries),
            "done": len(entries) - len(rows),
            "open": rows,
            "denominators": denominators,
        }
    return documents


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--content-root", required=True, type=Path)
    parser.add_argument("--out-dir", required=True, type=Path)
    parser.add_argument("--kind", choices=KINDS)
    return parser


def _effective_catalog_root(requested: Path, out_dir: Path) -> Path:
    """Prefer the canonical expansion+base layered cache when it is complete."""

    requested_root = requested.expanduser().resolve()
    layered = out_dir.resolve().parent / "editions" / "rotwk" / "layered-install"
    layer_rotwk = layered / "layer-1-rotwk"
    if all(
        path.is_dir()
        for path in (
            layered / "layer-0-patch202",
            layered / "layer-1-rotwk",
            layered / "layer-2-bfme2",
        )
    ) and requested_root in {layered.resolve(), layer_rotwk.resolve()}:
        return layered
    return requested_root


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    catalog_root = _effective_catalog_root(args.install, args.out_dir)
    catalog = InstallCatalog.build(catalog_root)
    kinds = (args.kind,) if args.kind else KINDS
    documents = generate_queue_documents(
        catalog,
        install=catalog_root,
        content_root=args.content_root,
        kinds=kinds,
    )
    for kind in kinds:
        path = args.out_dir / f"rotwk-{kind}-queue.json"
        write_json_atomic(path, documents[kind])
        print(f"{kind}: done={documents[kind]['done']} total={documents[kind]['total']} -> {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main())
