#!/usr/bin/env python3
"""Object-binding / visual-closure burn-down for official multiplayer maps.

For each mapcache multiplayer map:
  1. Load retail map bytes from the catalog
  2. Parse + cook placements
  3. Run map_prop_bindings / visual-closure against layered effective assets
  4. Record bound vs unbound type percentages with detailed gap reasons

Writes:
  - JSONL conversion ledger (every type / map)
  - Summary JSON with corpus-wide percentages

Does not publish packs or rewrite selection.json.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import traceback
import uuid
from collections import Counter
from pathlib import Path
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.catalog import (  # noqa: E402
    ArchivePolicy,
    DEFAULT_BFME2_ARCHIVE_POLICY,
    InstallCatalog,
    catalog_provenance_reason,
)
from openbfme_importer.conversion_ledger import ConversionLedger  # noqa: E402
from openbfme_importer.map_census import (  # noqa: E402
    MAPCACHE_VIRTUAL_PATH,
    MAX_MAPCACHE_BYTES,
    parse_mapcache_bytes,
)
from openbfme_importer.map_prop_bindings import (  # noqa: E402
    build_map_prop_binding_plan,
    load_effective_assets_manifest,
)
from openbfme_importer.paths import (  # noqa: E402
    ensure_external_to_repo,
    repo_root_from_module,
)
from openbfme_importer.sage_map import (  # noqa: E402
    MAX_SOURCE_BYTES,
    SageMapError,
    parse_sage_map_bytes,
)
from openbfme_importer.util import write_json_atomic  # noqa: E402

REPORT_SCHEMA = "openbfme.rotwk-binding-factory"
REPORT_SCHEMA_VERSION = 1


def _load_catalog(state_root: Path, game: str, install: Path) -> InstallCatalog:
    path = state_root / "catalog" / f"{game}.json"
    source_policy = (
        ArchivePolicy.load(DEFAULT_BFME2_ARCHIVE_POLICY) if game == "bfme2" else None
    )
    if path.is_file():
        catalog = InstallCatalog.load(path)
        reason = catalog_provenance_reason(
            (a.relative_path for a in catalog.archives), game
        )
        if reason is not None:
            raise SystemExit(f"catalog-game-mismatch: {reason}")
        if Path(catalog.install_root).resolve() != install.resolve():
            catalog = InstallCatalog.build(install, source_policy=source_policy)
            path.parent.mkdir(parents=True, exist_ok=True)
            catalog.save(path)
        return catalog
    catalog = InstallCatalog.build(install, source_policy=source_policy)
    path.parent.mkdir(parents=True, exist_ok=True)
    catalog.save(path)
    return catalog


def _read_virtual(catalog: InstallCatalog, virtual: str, *, max_bytes: int) -> bytes:
    entry = catalog.resolve_exact(virtual)
    if entry is None:
        raise FileNotFoundError(virtual)
    archive = catalog.open_archive_for(entry)
    return archive.read_entry(catalog.as_entry(entry), max_bytes=max_bytes)


def _map_slug(virtual_path: str) -> str:
    """Stable unique slug for one retail map directory.

    Keep the retail kind token (``mp``, ``wor``, ``ang``, …). RotWK reuses
    bare names across kinds (``map mp harlindon`` vs ``map wor harlindon``);
    stripping the kind collides resource ids and burn-down rows.
    """
    parts = virtual_path.replace("\\", "/").split("/")
    folder = parts[-2] if len(parts) >= 2 else Path(virtual_path).stem
    words = [w for w in folder.casefold().split() if w]
    if words and words[0] == "map":
        words = words[1:]
    return "-".join(words) if words else Path(virtual_path).stem.casefold()


def _default_effective_assets(state_root: Path, game: str) -> Path:
    candidates = [
        state_root / "editions" / game / "cache" / "layered-effective-assets",
        state_root / "editions" / game / "cache" / "effective-assets",
        state_root / "cache" / "layered-effective-assets",
        state_root / "cache" / "effective-assets",
    ]
    for path in candidates:
        if path.is_dir():
            return path
    raise SystemExit(
        "no effective-assets tree found; run extract-all-assets / edition overlay first. "
        f"looked in: {', '.join(str(p) for p in candidates)}"
    )


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--game", choices=("rotwk", "bfme2"), default="rotwk")
    parser.add_argument("--state-root", type=Path, default=None)
    parser.add_argument("--effective-assets", type=Path, default=None)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument(
        "--strict",
        action="store_true",
        help="exit nonzero if any map has unbound visual types or ledger sink errors",
    )
    args = parser.parse_args(argv)

    install = args.install.expanduser().resolve()
    if not (install / "game.dat").is_file():
        print(f"FAIL: no game.dat at {install}", file=sys.stderr)
        return 2

    state_root = args.state_root
    if state_root is None:
        state_root = ROOT / ".private" / "retail-work"
    state_root = ensure_external_to_repo(
        Path(state_root).expanduser().resolve(), repo_root_from_module()
    )
    os.environ["OPENBFME_IMPORT_ROOT"] = str(state_root)

    assets = (
        args.effective_assets.expanduser().resolve()
        if args.effective_assets
        else _default_effective_assets(state_root, args.game)
    )
    manifest = load_effective_assets_manifest(assets)

    run_id = uuid.uuid4().hex
    reports = state_root / "reports"
    reports.mkdir(parents=True, exist_ok=True)
    ledger_path = reports / f"{args.game}-binding-factory-{run_id}.jsonl"
    ledger = ConversionLedger(
        run_id=run_id,
        game=args.game,
        install_root=str(install),
        sink_path=ledger_path,
    )

    catalog = _load_catalog(state_root, args.game, install)
    registry = parse_mapcache_bytes(
        _read_virtual(catalog, MAPCACHE_VIRTUAL_PATH, max_bytes=MAX_MAPCACHE_BYTES)
    )
    selected = [
        row
        for row in registry
        if bool(row["isMultiplayer"])
        and bool(row["isOfficial"])
        and not bool(row["isScenarioMp"])
    ]
    selected.sort(key=lambda row: str(row["virtualPath"]).casefold())
    if args.limit and args.limit > 0:
        selected = selected[: args.limit]

    map_rows: list[dict[str, Any]] = []
    unbound_global: Counter[str] = Counter()
    bound_global = 0
    type_global = 0

    texture_owners: dict[str, str] = {}
    for index, record in enumerate(selected):
        virtual = str(record["virtualPath"])
        slug = _map_slug(virtual)
        try:
            if catalog.resolve_exact(virtual) is None:
                ledger.record(
                    kind="map-binding",
                    unit_id=virtual,
                    status="skipped",
                    detail="registry-stale-missing-payload",
                )
                map_rows.append(
                    {
                        "path": virtual,
                        "slug": slug,
                        "verdict": "registry-stale-missing-payload",
                    }
                )
                continue
            source = _read_virtual(catalog, virtual, max_bytes=MAX_SOURCE_BYTES)
            parsed = parse_sage_map_bytes(source, profile="multiplayer")
            plan = build_map_prop_binding_plan(
                parsed,
                effective_assets_root=assets,
                effective_assets_manifest=manifest,
                map_pattern=virtual,
                output_root=f"maps/{slug}",
                texture_owners=texture_owners,
            )
            bindings = plan["objectBindings"]
            models = list(bindings.get("models") or [])
            logical = list(bindings.get("logical") or [])
            evidence = plan.get("evidence") or {}
            unbound = [str(n) for n in (evidence.get("unboundTypeNames") or [])]
            bound_types = len(models)
            logical_types = len(logical)
            map_type_total = bound_types + logical_types + len(unbound)
            type_global += map_type_total
            bound_global += bound_types + logical_types
            for name in unbound:
                unbound_global[str(name)] += 1
                ledger.record(
                    kind="object-type",
                    unit_id=f"{slug}:{name}",
                    status="unbound",
                    detail="visual-closure-or-planner-gap",
                    log_to_stderr=False,
                )
            for row in models:
                ledger.record(
                    kind="object-type",
                    unit_id=f"{slug}:{row.get('typeName')}",
                    status="bound",
                    detail=str(row.get("matchMethod") or "model"),
                    log_to_stderr=False,
                )
            for row in logical:
                ledger.record(
                    kind="object-type",
                    unit_id=f"{slug}:{row.get('typeName')}",
                    status="bound",
                    detail=f"logical:{row.get('classification')}",
                    log_to_stderr=False,
                )
            pct_bound = (
                0.0
                if map_type_total == 0
                else round(
                    100.0 * (bound_types + logical_types) / map_type_total,
                    2,
                )
            )
            verdict = "binding-planned"
            # Map-level rollup is metrics-only; not counted as a unit conversion.
            map_rows.append(
                {
                    "path": virtual,
                    "slug": slug,
                    "verdict": verdict,
                    "boundModelTypes": bound_types,
                    "logicalTypes": logical_types,
                    "unboundTypes": len(unbound),
                    "unboundTypeNames": unbound[:40],
                    "percentBound": pct_bound,
                    "resourceCount": len(plan.get("resources") or []),
                    "sourceSha256": hashlib.sha256(source).hexdigest(),
                }
            )
            print(
                f"[{index + 1}/{len(selected)}] {slug}: "
                f"bound%={pct_bound} models={bound_types} "
                f"logical={logical_types} unbound={len(unbound)}",
                flush=True,
            )
        except SageMapError as exc:
            ledger.record(
                kind="map-binding",
                unit_id=virtual,
                status="rejected",
                error=str(exc),
            )
            map_rows.append(
                {"path": virtual, "slug": slug, "verdict": "parse-rejected", "error": str(exc)[:500]}
            )
        except Exception as exc:
            ledger.record(
                kind="map-binding",
                unit_id=virtual,
                status="failed",
                error=f"{type(exc).__name__}: {exc}",
                traceback_tail=traceback.format_exc(),
            )
            map_rows.append(
                {
                    "path": virtual,
                    "slug": slug,
                    "verdict": "binding-error",
                    "error": f"{type(exc).__name__}: {exc}"[:500],
                }
            )

    corpus_pct = (
        0.0 if type_global == 0 else round(100.0 * bound_global / type_global, 2)
    )
    # Fail closed on identity collisions (do not ship patched/duplicated rows).
    path_keys = [str(row.get("path") or "").casefold() for row in map_rows if row.get("path")]
    slug_keys = [str(row.get("slug") or "").casefold() for row in map_rows if row.get("slug")]
    if len(path_keys) != len(set(path_keys)):
        print("FAIL binding report has duplicate map paths", file=sys.stderr)
        return 3
    if len(slug_keys) != len(set(slug_keys)):
        print("FAIL binding report has duplicate map slugs", file=sys.stderr)
        return 3
    summary_path = reports / f"{args.game}-binding-factory-summary.json"
    ledger_summary = ledger.write_summary(
        reports / f"{args.game}-binding-factory-ledger-summary.json"
    )
    report = {
        "schema": REPORT_SCHEMA,
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "game": args.game,
        "installRoot": str(install),
        "effectiveAssets": str(assets),
        "runId": run_id,
        "mapCount": len(map_rows),
        "uniquePathCount": len(set(path_keys)),
        "uniqueSlugCount": len(set(slug_keys)),
        "typeTotal": type_global,
        "boundLikeTotal": bound_global,
        "percentBoundCorpus": corpus_pct,
        "topUnboundTypes": unbound_global.most_common(40),
        "ledger": ledger_summary,
        "ledgerJsonl": str(ledger_path),
        "maps": map_rows,
    }
    out = args.output or (reports / f"{args.game}-binding-factory.json")
    out = Path(out).expanduser().resolve()
    try:
        out.relative_to(reports.resolve())
    except ValueError:
        out = reports / out.name
    write_json_atomic(out, report)
    print(f"REPORT {out}")
    print(
        f"BINDING CORPUS bound%={corpus_pct} "
        f"bound={bound_global}/{type_global} maps={len(map_rows)}"
    )
    failed = sum(
        1 for row in map_rows if row.get("verdict") in {"binding-error", "parse-rejected"}
    )
    unbound_total = sum(int(row.get("unboundTypes") or 0) for row in map_rows)
    if ledger_summary.get("sinkErrorCount", 0) > 0:
        print("FAIL ledger sink errors", file=sys.stderr)
        return 3
    if failed:
        return 3
    if args.strict and unbound_total > 0:
        print(f"FAIL strict: {unbound_total} unbound types remain", file=sys.stderr)
        return 4
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
