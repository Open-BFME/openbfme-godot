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
from .map_kinds import classify_map_path
from .paths import ensure_external_to_repo, repo_root_from_module
from .pipeline import ImportPipeline
from .util import write_json_atomic


SELECTION_SCHEMA = "openbfme.native-selection"
SELECTION_VERSION = 1
FACTIONS = ("men", "elves", "dwarves", "isengard", "mordor", "wild", "angmar")


@dataclass(frozen=True)
class NativeMapSource:
    name: str
    slug: str
    players: int
    cooked_root: Path
    kind: str = "other"
    virtual_path: str | None = None


@dataclass(frozen=True)
class NativeBuildResult:
    active: str
    bundle_written: bool
    maps_written: int
    selection_written: bool
    maps: tuple[Mapping[str, object], ...]
    failures: tuple[Mapping[str, object], ...] = ()


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


def _corpus_slug(value: object) -> str:
    """Validate, but do not rewrite, the corpus cache key."""

    slug = str(value).strip().casefold()
    if not slug or any(char not in "abcdefghijklmnopqrstuvwxyz0123456789-" for char in slug):
        raise ValueError(f"unsafe corpus map slug: {value!r}")
    return slug


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
        if verdict not in {
            "cooked",
            "cooked-and-connected",
            "cooked-but-starts-disconnected",
            "under-two-player-starts",
        }:
            continue
        slug = _corpus_slug(row.get("slug", ""))
        cooked = cache_root / slug
        if not (cooked / "map.json").is_file():
            continue
        sources.append(
            NativeMapSource(
                name=str(row.get("displayName") or slug.replace("-", " ").title()),
                slug=slug,
                players=int(row.get("registryPlayerCount", row.get("playerStarts", 0)) or 0),
                cooked_root=cooked.resolve(),
                kind=str(row.get("kind") or classify_map_path(str(row.get("path", "")))),
                virtual_path=str(row.get("path", "")),
            )
        )
    return sources


def _expected_corpus_slugs(state_root: Path, limit: int | None) -> list[str]:
    rows = sorted(
        _read_corpus_rows(state_root), key=lambda row: str(row.get("path", "")).casefold()
    )
    if limit is not None:
        rows = rows[:limit]
    return [
        _corpus_slug(row.get("slug", ""))
        for row in rows
        if str(row.get("verdict", ""))
        in {
            "cooked",
            "cooked-and-connected",
            "cooked-but-starts-disconnected",
            "under-two-player-starts",
        }
    ]


def _corpus_report_covers_request(state_root: Path, limit: int | None) -> bool:
    path = _corpus_report_path(state_root)
    if not path.is_file():
        return False
    try:
        document = json.loads(path.read_text(encoding="utf-8"))
        if int(document.get("schemaVersion", -1)) != 3:
            return False
        processed = int(document.get("mapCount", -1))
        eligible = int(document.get("eligibleMapCount", -1))
    except (OSError, UnicodeError, json.JSONDecodeError, TypeError, ValueError):
        return False
    if limit is None and eligible != 419:
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
    if completed.returncode not in {0, 3} or not _corpus_report_covers_request(
        state_root, limit
    ):
        tail = (completed.stderr or completed.stdout).strip()[-2000:]
        raise RuntimeError(
            f"corpus map cook exited {completed.returncode}: {tail or 'no output'}"
        )


def ensure_map_sources(
    layered_install: Path, state_root: Path, limit: int | None
) -> tuple[list[NativeMapSource], bool]:
    existing = _corpus_sources(state_root)
    expected = _expected_corpus_slugs(state_root, limit)
    existing_slugs = {source.slug for source in existing}
    complete_report = (
        _corpus_report_covers_request(state_root, limit)
        and all(slug in existing_slugs for slug in expected)
    )
    needs_cook = not complete_report
    if needs_cook:
        _run_corpus_cook(layered_install, state_root, limit)
        existing = _corpus_sources(state_root)
        expected = _expected_corpus_slugs(state_root, limit)
        existing_slugs = {source.slug for source in existing}
        missing = [slug for slug in expected if slug not in existing_slugs]
        if missing:
            raise RuntimeError(
                "corpus cook did not persist every cooked map: " + ", ".join(missing[:20])
            )
    if limit is not None:
        existing = existing[:limit]
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
    map_failures: list[Mapping[str, object]] = []
    maps_written = 0
    for source in sorted(map_sources, key=lambda item: item.slug):
        source_path = source.virtual_path or source.slug + ".map"
        output = version_root / "maps" / f"{source.slug}.map-v1.json"
        try:
            source_sha = _cooked_source_sha256(source.cooked_root)
            if _valid_map(output, source_sha):
                document = json.loads(output.read_text(encoding="utf-8"))
            else:
                document = convert_cooked_map(
                    source.cooked_root, output, source_path=source_path
                )
                maps_written += 1
        except (OSError, UnicodeError, ValueError) as exc:
            map_failures.append(
                {
                    "path": source_path,
                    "slug": source.slug,
                    "kind": source.kind,
                    "file": str(source.cooked_root),
                    "class": type(exc).__name__,
                    "message": str(exc),
                }
            )
            continue
        players = source.players or len(document.get("start_positions", {}))
        map_rows.append(
            {
                "name": source.name,
                "slug": source.slug,
                "path": _relative_path(output, content),
                "players": players,
                "kind": source.kind,
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
        failures=tuple(map_failures),
    )


def write_map_sweep_report(
    state_root: Path,
    result: NativeBuildResult,
    *,
    install: Path,
    content_root: Path,
) -> Path:
    """Merge strict-cook and map-v1 outcomes into one deterministic 419-row report."""

    success_by_slug = {str(row["slug"]): row for row in result.maps}
    failure_by_slug = {str(row["slug"]): row for row in result.failures}
    command = " ".join(
        (
            f'"{sys.executable}"',
            "-m openbfme_importer.native_content",
            f'--install "{install}"',
            f'--state-root "{state_root}"',
            f'--content-root "{content_root}"',
            "--maps all",
        )
    )
    rows: list[dict[str, object]] = []
    for cooked in sorted(_read_corpus_rows(state_root), key=lambda row: str(row.get("path", "")).casefold()):
        path = str(cooked.get("path", ""))
        slug = str(cooked.get("slug", ""))
        row: dict[str, object] = {
            "path": path,
            "slug": slug,
            "kind": str(cooked.get("kind") or classify_map_path(path)),
            "players": int(cooked.get("registryPlayerCount", cooked.get("playerStarts", 0)) or 0),
            "rerunCommand": command,
        }
        if slug in success_by_slug:
            selection_row = success_by_slug[slug]
            row.update(
                {
                    "status": "ok",
                    "mapV1": selection_row["path"],
                    "players": int(selection_row["players"]),
                }
            )
        elif slug in failure_by_slug:
            failure = failure_by_slug[slug]
            row.update(
                {
                    "status": "failed",
                    "stage": "map-v1",
                    "failure": {
                        "class": failure["class"],
                        "file": failure["file"],
                        "message": failure["message"],
                    },
                }
            )
        else:
            failure = cooked.get("failure")
            if not isinstance(failure, Mapping):
                failure = {
                    "class": "CookOutputUnavailable",
                    "file": path,
                    "message": str(cooked.get("error") or cooked.get("verdict") or "cooked output unavailable"),
                }
            row.update({"status": "failed", "stage": "sage-map", "failure": dict(failure)})
        rows.append(row)
    report = {
        "schema": "openbfme.native-map-sweep",
        "schemaVersion": 1,
        "attempted": len(rows),
        "ok": sum(row["status"] == "ok" for row in rows),
        "failed": sum(row["status"] == "failed" for row in rows),
        "maps": rows,
    }
    path = state_root / "reports" / "rotwk-map-v1-sweep.json"
    write_json_atomic(path, report)
    return path


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
    parser.add_argument(
        "--sweep",
        action="store_true",
        help="after bundle/maps, convert and index the complete APT/WND corpus",
    )
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
        sweep_report = write_map_sweep_report(
            state_root,
            result,
            install=install,
            content_root=content_root,
        )
        screen_detail = ""
        if args.sweep:
            _emit("screens", "Converting and indexing every effective APT/WND screen.", 85)
            # Local import avoids a module cycle: cook.screens deliberately
            # reuses this module's edition-specific effective-tree preparation.
            from .cook.screens import convert_screen_corpus

            screen_result = convert_screen_corpus(
                install=install,
                state_root=state_root,
                content_root=content_root,
            )
            screen_detail = (
                f", screens={screen_result.converted}/{screen_result.attempted}"
            )
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
            f"Native content ready: {len(result.maps)} maps, {len(result.failures)} map-v1 failures, "
            f"active {result.active}{screen_detail}; sweep report {sweep_report}.",
            100,
        )
        return 0
    except (OSError, RuntimeError, TypeError, ValueError) as exc:
        _emit("failed", str(exc), 100)
        print(f"NATIVE_CONTENT_FAIL {exc}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
