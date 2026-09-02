"""Build the native-core bundle and map documents from a RotWK install."""

from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
from typing import Any, Mapping, Sequence

from .catalog import InstallCatalog, catalog_provenance_reason
from .cook import objects
from .cook._bundle import _root_paths, cook_ini_root, write_bundle
from .cook.maps import SCHEMA as MAP_SCHEMA
from .cook.maps import convert_cooked_map
from .effective_assets_identity import verify_effective_assets
from .paths import ensure_external_to_repo, repo_root_from_module
from .pipeline import ImportPipeline


SELECTION_SCHEMA = "openbfme.native-selection"
SELECTION_VERSION = 1
FACTIONS = ("men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar")


@dataclass(frozen=True)
class NativeMapSource:
    name: str
    slug: str
    players: int
    cooked_root: Path


@dataclass(frozen=True)
class NativeBuildResult:
    active: str
    bundle_written: bool
    maps_written: int
    selection_written: bool
    maps: tuple[Mapping[str, object], ...]


def _emit(phase: str, message: str, percent: float) -> None:
    print(
        json.dumps(
            {"phase": phase, "message": message, "percent": percent},
            ensure_ascii=False,
            separators=(",", ":"),
        ),
        flush=True,
    )


def _repository_root() -> Path:
    return Path(__file__).resolve().parents[2]


def _load_or_build_catalog(state_root: Path, layered_install: Path) -> InstallCatalog:
    path = state_root / "catalog" / "rotwk.json"
    if path.is_file():
        catalog = InstallCatalog.load(path)
        reason = catalog_provenance_reason(
            (archive.relative_path for archive in catalog.archives), "rotwk"
        )
        if reason is None and Path(catalog.install_root).resolve() == layered_install.resolve():
            return catalog
    catalog = InstallCatalog.build(layered_install)
    path.parent.mkdir(parents=True, exist_ok=True)
    catalog.save(path)
    return catalog


def prepare_effective_ini_tree(install: Path, state_root: Path) -> tuple[Path, Path, bool]:
    """Use the established layered-install and ImportPipeline extraction path."""

    tools_root = _repository_root() / "tools"
    sys.path.insert(0, str(tools_root))
    try:
        from rotwk_layered_install import ensure_layered_rotwk_install
    finally:
        try:
            sys.path.remove(str(tools_root))
        except ValueError:
            pass

    layered = ensure_layered_rotwk_install(state_root, rotwk_install=install)
    catalog = _load_or_build_catalog(state_root, layered)
    pipeline = ImportPipeline(catalog, state_root, game="rotwk")
    effective_root, manifest_path, _staging, _backup = pipeline._effective_asset_paths()
    built = False
    if not manifest_path.is_file():
        pipeline.extract_all_assets(force=False)
        built = True
    verify_effective_assets(
        effective_root,
        game="rotwk",
        catalog=None,
        consumer="native-content",
    )
    ini_root = effective_root / "data" / "ini"
    if not ini_root.is_dir():
        raise FileNotFoundError(f"effective INI tree is missing: {ini_root}")
    return ini_root, layered, built


def effective_ini_sha256(ini_root: Path | str) -> str:
    root = Path(ini_root).resolve()
    documents = objects._path_documents(_root_paths(root), root)
    return str(objects._source_identity(documents)["effective_tree_sha256"])


def _valid_bundle(path: Path, expected_sha256: str) -> bool:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
        array_fields = (
            "templates",
            "weapons",
            "armors",
            "damage_fx",
            "locomotors",
            "locomotor_sets",
            "hordes",
            "upgrades",
            "sciences",
            "special_powers",
            "command_buttons",
            "command_sets",
            "diagnostics",
        )
        return (
            document.get("schema") == objects.BUNDLE_SCHEMA
            and document.get("source", {}).get("effective_tree_sha256")
            == expected_sha256
            and all(isinstance(document.get(field), list) for field in array_fields)
            and isinstance(document.get("defines"), Mapping)
        )
    except (OSError, UnicodeError, json.JSONDecodeError, AttributeError):
        return False


def _cooked_source_sha256(root: Path) -> str:
    document = json.loads((root / "map.json").read_text(encoding="utf-8"))
    value = document.get("source", {}).get("sha256")
    if not isinstance(value, str) or len(value) != 64:
        raise ValueError(f"cooked map has no source sha256: {root}")
    return value


def _valid_map(path: Path, source_sha256: str) -> bool:
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
        required = {
            "world": Mapping,
            "height_grid": Mapping,
            "passability_grid": Mapping,
            "start_positions": Mapping,
            "waypoints": Mapping,
            "objects": list,
            "plots": list,
        }
        return (
            document.get("schema") == MAP_SCHEMA
            and document.get("source", {}).get("sha256") == source_sha256
            and all(isinstance(document.get(field), kind) for field, kind in required.items())
            and bool(document.get("height_grid", {}).get("data_base64"))
            and bool(document.get("passability_grid", {}).get("data_base64"))
        )
    except (OSError, UnicodeError, json.JSONDecodeError, AttributeError):
        return False


def _safe_slug(value: str) -> str:
    slug = value.strip().casefold().replace("_", "-").replace(" ", "-")
    while "--" in slug:
        slug = slug.replace("--", "-")
    if not slug or any(char not in "abcdefghijklmnopqrstuvwxyz0123456789-" for char in slug):
        raise ValueError(f"unsafe map slug: {value!r}")
    return slug


def _map_sources_from_pack(pack_root: Path) -> list[NativeMapSource]:
    catalog_path = pack_root / "data" / "maps.json"
    if not catalog_path.is_file():
        return []
    document = json.loads(catalog_path.read_text(encoding="utf-8"))
    sources: list[NativeMapSource] = []
    for row in document.get("maps", []):
        if not isinstance(row, Mapping):
            continue
        relative = Path(str(row.get("map", "")))
        cooked = (pack_root / relative).resolve().parent
        try:
            cooked.relative_to(pack_root.resolve())
        except ValueError as exc:
            raise ValueError(f"map catalog path escapes pack: {relative}") from exc
        if not (cooked / "map.json").is_file():
            continue
        slug = _safe_slug(relative.parent.name or str(row.get("id", "")).split(".")[-1])
        sources.append(
            NativeMapSource(
                name=str(row.get("displayName") or slug.replace("-", " ").title()),
                slug=slug,
                players=int(row.get("playerCount", row.get("registryPlayerCount", 0)) or 0),
                cooked_root=cooked,
            )
        )
    return sources


def _pack_roots(state_root: Path) -> list[Path]:
    roots = [
        state_root / "editions" / "rotwk" / "packs" / "rotwk-skirmish-maps-private",
        state_root / "packs" / "rotwk-skirmish-maps-private",
    ]
    return [path.resolve() for path in roots if path.is_dir()]


def _corpus_report_path(state_root: Path) -> Path:
    return state_root / "reports" / "rotwk-map-cook-corpus.json"


def _read_corpus_rows(state_root: Path) -> list[Mapping[str, Any]]:
    path = _corpus_report_path(state_root)
    if not path.is_file():
        return []
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError):
        return []
    rows = document.get("maps", [])
    return [row for row in rows if isinstance(row, Mapping)]


def _corpus_sources(state_root: Path) -> list[NativeMapSource]:
    cache_root = state_root / "editions" / "rotwk" / "cache" / "native-cooked-maps"
    sources: list[NativeMapSource] = []
    for row in _read_corpus_rows(state_root):
        verdict = str(row.get("verdict", ""))
        if not verdict.startswith("cooked"):
            continue
        slug = _safe_slug(str(row.get("slug", "")))
        cooked = cache_root / slug
        if not (cooked / "map.json").is_file():
            continue
        sources.append(
            NativeMapSource(
                name=str(row.get("displayName") or slug.replace("-", " ").title()),
                slug=slug,
                players=int(row.get("registryPlayerCount", row.get("playerStarts", 0)) or 0),
                cooked_root=cooked.resolve(),
            )
        )
    return sources


def _merge_sources(*groups: Sequence[NativeMapSource]) -> list[NativeMapSource]:
    merged: dict[str, NativeMapSource] = {}
    for group in groups:
        for source in group:
            incumbent = merged.get(source.slug)
            if incumbent is not None:
                if _cooked_source_sha256(incumbent.cooked_root) != _cooked_source_sha256(
                    source.cooked_root
                ):
                    raise ValueError(f"conflicting cooked maps share slug {source.slug}")
                continue
            merged[source.slug] = source
    return [merged[key] for key in sorted(merged)]


def _expected_corpus_slugs(state_root: Path, limit: int | None) -> list[str]:
    rows = sorted(
        _read_corpus_rows(state_root), key=lambda row: str(row.get("path", "")).casefold()
    )
    if limit is not None:
        rows = rows[:limit]
    return [
        _safe_slug(str(row.get("slug", "")))
        for row in rows
        if str(row.get("verdict", "")).startswith("cooked")
    ]


def _corpus_report_covers_request(state_root: Path, limit: int | None) -> bool:
    path = _corpus_report_path(state_root)
    if not path.is_file():
        return False
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
        processed = int(document.get("mapCount", -1))
        eligible = int(document.get("eligibleMapCount", -1))
    except (OSError, UnicodeError, json.JSONDecodeError, TypeError, ValueError):
        return False
    required = eligible if limit is None else min(limit, eligible)
    return eligible >= 0 and processed == required


def _run_corpus_cook(
    layered_install: Path, state_root: Path, limit: int | None
) -> None:
    tool = _repository_root() / "tools" / "rotwk_map_cook_corpus.py"
    if not tool.is_file():
        raise FileNotFoundError(f"bundled corpus map cooker is missing: {tool}")
    cooked_root = (
        state_root / "editions" / "rotwk" / "cache" / "native-cooked-maps"
    )
    command = [
        sys.executable,
        str(tool),
        "--install",
        str(layered_install),
        "--game",
        "rotwk",
        "--state-root",
        str(state_root),
        "--cooked-root",
        str(cooked_root),
    ]
    if limit is not None:
        command.extend(("--limit", str(limit)))
    environment = os.environ.copy()
    environment["PYTHONPATH"] = str(_repository_root() / "importer")
    completed = subprocess.run(
        command,
        cwd=_repository_root(),
        env=environment,
        text=True,
        capture_output=True,
        check=False,
    )
    if completed.returncode != 0:
        tail = (completed.stderr or completed.stdout).strip()[-2000:]
        raise RuntimeError(
            f"corpus map cook exited {completed.returncode}: {tail or 'no output'}"
        )


def ensure_map_sources(
    layered_install: Path, state_root: Path, limit: int | None
) -> tuple[list[NativeMapSource], bool]:
    pack_sources = _merge_sources(
        *[_map_sources_from_pack(pack) for pack in _pack_roots(state_root)]
    )
    existing = _merge_sources(pack_sources, _corpus_sources(state_root))
    expected = _expected_corpus_slugs(state_root, limit)
    existing_slugs = {source.slug for source in existing}
    enough_for_limit = limit is not None and len(existing) >= limit
    complete_report = (
        _corpus_report_covers_request(state_root, limit)
        and all(slug in existing_slugs for slug in expected)
    )
    needs_cook = not enough_for_limit and not complete_report
    if needs_cook:
        _run_corpus_cook(layered_install, state_root, limit)
        existing = _merge_sources(pack_sources, _corpus_sources(state_root))
        expected = _expected_corpus_slugs(state_root, limit)
        existing_slugs = {source.slug for source in existing}
        missing = [slug for slug in expected if slug not in existing_slugs]
        if missing:
            raise RuntimeError(
                "corpus cook did not persist every cooked map: " + ", ".join(missing[:20])
            )
    if limit is not None:
        existing = existing[:limit]
    if not existing:
        raise RuntimeError("no cooked skirmish maps are available")
    return existing, needs_cook


def _relative_path(path: Path, content_root: Path) -> str:
    return path.resolve().relative_to(content_root.resolve()).as_posix()


def _selection_bytes(document: Mapping[str, object]) -> bytes:
    return (
        json.dumps(
            document,
            ensure_ascii=False,
            sort_keys=True,
            separators=(",", ":"),
        )
        + "\n"
    ).encode("utf-8")


def _write_selection_atomic(path: Path, document: Mapping[str, object]) -> bool:
    payload = _selection_bytes(document)
    if path.is_file() and path.read_bytes() == payload:
        return False
    path.parent.mkdir(parents=True, exist_ok=True)
    descriptor, temporary = tempfile.mkstemp(
        prefix=f".{path.name}.", suffix=".tmp", dir=path.parent
    )
    try:
        with os.fdopen(descriptor, "wb") as stream:
            stream.write(payload)
            stream.flush()
            os.fsync(stream.fileno())
        os.replace(temporary, path)
    finally:
        try:
            os.unlink(temporary)
        except FileNotFoundError:
            pass
    return True


def build_native_documents(
    ini_root: Path,
    map_sources: Sequence[NativeMapSource],
    content_root: Path,
) -> NativeBuildResult:
    content = content_root.expanduser().resolve()
    content.mkdir(parents=True, exist_ok=True)
    active = effective_ini_sha256(ini_root)
    version_root = content / "native" / active
    bundle_path = version_root / "bundle-v1.json"
    bundle_written = not _valid_bundle(bundle_path, active)
    if bundle_written:
        result = cook_ini_root(ini_root)
        failures = result.report.get("parse_failures", [])
        if failures:
            names = ", ".join(str(row.get("file", "?")) for row in failures[:10])
            raise ValueError(f"generic bundle cook has {len(failures)} parse failures: {names}")
        if result.bundle.get("source", {}).get("effective_tree_sha256") != active:
            raise RuntimeError("generic bundle cook source identity changed during the run")
        write_bundle(result.bundle, bundle_path)

    map_rows: list[Mapping[str, object]] = []
    maps_written = 0
    for source in sorted(map_sources, key=lambda item: item.slug):
        source_sha = _cooked_source_sha256(source.cooked_root)
        output = version_root / "maps" / f"{source.slug}.map-v1.json"
        if _valid_map(output, source_sha):
            document = json.loads(output.read_text(encoding="utf-8"))
        else:
            document = convert_cooked_map(source.cooked_root, output)
            maps_written += 1
        players = source.players or len(document.get("start_positions", {}))
        map_rows.append(
            {
                "name": source.name,
                "slug": source.slug,
                "path": _relative_path(output, content),
                "players": players,
            }
        )

    selection = {
        "schema": SELECTION_SCHEMA,
        "version": SELECTION_VERSION,
        "active": active,
        "bundle": _relative_path(bundle_path, content),
        "maps": map_rows,
    }
    selection_written = _write_selection_atomic(
        content / "native" / "selection.json", selection
    )
    return NativeBuildResult(
        active=active,
        bundle_written=bundle_written,
        maps_written=maps_written,
        selection_written=selection_written,
        maps=tuple(map_rows),
    )


def _parse_maps(value: str) -> int | None:
    if value.casefold() == "all":
        return None
    try:
        limit = int(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("--maps must be 'all' or a positive integer") from exc
    if limit <= 0:
        raise argparse.ArgumentTypeError("--maps limit must be positive")
    return limit


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--state-root", required=True, type=Path)
    parser.add_argument("--content-root", required=True, type=Path)
    parser.add_argument("--maps", default="all", type=_parse_maps)
    parser.add_argument("--prepare-only", action="store_true", help=argparse.SUPPRESS)
    return parser


def main(argv: Sequence[str] | None = None) -> int:
    args = build_parser().parse_args(argv)
    install = args.install.expanduser().resolve()
    if not (install / "game.dat").is_file():
        build_parser().error(f"RotWK install has no game.dat: {install}")
    state_root = ensure_external_to_repo(
        args.state_root.expanduser().resolve(), repo_root_from_module()
    )
    content_root = args.content_root.expanduser().resolve()
    os.environ["OPENBFME_IMPORT_ROOT"] = str(state_root)
    try:
        _emit("effective-ini", "Building or reusing the layered effective INI tree.", 5)
        ini_root, layered, tree_built = prepare_effective_ini_tree(install, state_root)
        _emit(
            "effective-ini",
            "Built the effective INI tree." if tree_built else "Effective INI tree is unchanged; reused it.",
            20,
        )
        if args.prepare_only:
            _emit("complete", "Layered effective INI tree is ready.", 100)
            return 0
        _emit("maps-corpus", "Checking every requested cooked skirmish map.", 30)
        map_sources, corpus_ran = ensure_map_sources(layered, state_root, args.maps)
        _emit(
            "maps-corpus",
            f"Cooked map corpus is ready ({len(map_sources)} maps; "
            + ("corpus cook ran)." if corpus_ran else "no corpus work needed)."),
            55,
        )
        _emit("native-documents", "Cooking native bundle and map-v1 documents.", 65)
        result = build_native_documents(ini_root, map_sources, content_root)
        changed = result.bundle_written or result.maps_written or result.selection_written
        detail = (
            f"Wrote bundle={result.bundle_written}, maps={result.maps_written}, "
            f"selection={result.selection_written}."
            if changed
            else "Native content is unchanged; no documents were rewritten."
        )
        _emit("selection", detail, 95)
        _emit(
            "complete",
            f"Native content ready: {len(result.maps)} maps, active {result.active}.",
            100,
        )
        return 0
    except (OSError, RuntimeError, TypeError, ValueError) as exc:
        _emit("failed", str(exc), 100)
        print(f"NATIVE_CONTENT_FAIL {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
