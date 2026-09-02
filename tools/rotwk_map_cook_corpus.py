#!/usr/bin/env python3
"""Cook official multiplayer maps from a RotWK (or BFME2) retail catalog.

Uses the same mapcache selection rule as ``census-maps`` (official multiplayer
with a shipped payload). For each map: strict ``convert_sage_map`` into a
**private** job directory, then a passability connectivity check on player starts.

Writes a private JSON report under state-root/reports by default.

Usage:
  set PYTHONPATH=importer
  python tools/rotwk_map_cook_corpus.py --install C:/Path/To/RotWK --game rotwk
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
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
from openbfme_importer.map_census import (  # noqa: E402
    MAPCACHE_VIRTUAL_PATH,
    MAX_MAPCACHE_BYTES,
    parse_mapcache_bytes,
)
from openbfme_importer.paths import (  # noqa: E402
    ensure_external_to_repo,
    repo_root_from_module,
)
from openbfme_importer.sage_map import (  # noqa: E402
    MAX_SOURCE_BYTES,
    SageMapError,
    convert_sage_map,
    parse_sage_map_bytes,
)
from openbfme_importer.conversion_ledger import ConversionLedger  # noqa: E402
from openbfme_importer.util import write_json_atomic  # noqa: E402

REPORT_SCHEMA = "openbfme.rotwk-map-cook-corpus"
REPORT_SCHEMA_VERSION = 2

# Explicitly non-fatal gap verdicts (tool still exits 0 if only these occur).
NONFATAL_VERDICTS = frozenset(
    {
        "cooked",
        "cooked-and-connected",
        "cooked-but-starts-disconnected",
        "under-two-player-starts",
        "registry-stale-missing-payload",
    }
)
# Fatal: tool must exit nonzero when any row has these.
FATAL_VERDICTS = frozenset(
    {
        "cook-rejected",
        "cook-error",
        "cooked-connectivity-check-failed",
    }
)


def _load_catalog(state_root: Path, game: str, install: Path) -> InstallCatalog:
    path = state_root / "catalog" / f"{game}.json"
    source_policy = (
        ArchivePolicy.load(DEFAULT_BFME2_ARCHIVE_POLICY) if game == "bfme2" else None
    )
    if path.is_file():
        catalog = InstallCatalog.load(path)
        reason = catalog_provenance_reason(
            (archive.relative_path for archive in catalog.archives), game
        )
        if reason is not None:
            raise SystemExit(f"catalog-game-mismatch for {game}: {reason}")
        # Bind catalog to requested install when identity is present.
        if getattr(catalog, "install_root", None) is not None:
            catalog_root = Path(catalog.install_root).resolve()
            if catalog_root != install.resolve():
                # Rebuild rather than silently census the wrong install.
                catalog = InstallCatalog.build(install, source_policy=source_policy)
                path.parent.mkdir(parents=True, exist_ok=True)
                catalog.save(path)
                return catalog
        return catalog
    catalog = InstallCatalog.build(install, source_policy=source_policy)
    path.parent.mkdir(parents=True, exist_ok=True)
    catalog.save(path)
    return catalog


def _read_virtual(catalog: InstallCatalog, virtual_path: str, *, max_bytes: int) -> bytes:
    entry = catalog.resolve_exact(virtual_path)
    if entry is None:
        raise FileNotFoundError(f"missing catalog entry: {virtual_path}")
    archive = catalog.open_archive_for(entry)
    return archive.read_entry(catalog.as_entry(entry), max_bytes=max_bytes)


def _map_slug(virtual_path: str) -> str:
    """Stable unique slug; keep mp/wor/ang kind tokens to avoid name collisions."""
    parts = virtual_path.replace("\\", "/").split("/")
    folder = parts[-2] if len(parts) >= 2 else Path(virtual_path).stem
    words = [w for w in folder.casefold().split() if w]
    if words and words[0] == "map":
        words = words[1:]
    return "-".join(words) if words else Path(virtual_path).stem.casefold()


def _companion_flags(catalog: InstallCatalog, map_virtual: str) -> dict[str, bool]:
    folder = str(Path(map_virtual).parent).replace("\\", "/")
    stem = Path(map_virtual).stem

    def has(name: str) -> bool:
        return catalog.resolve_exact(f"{folder}/{name}") is not None

    return {
        "art": has(f"{stem}_art.tga") or has(f"{stem}_art.TGA"),
        "pic": has(f"{stem}_pic.tga") or has(f"{stem}_pic.TGA"),
        "map_ini": has("map.ini") or has("Map.ini"),
    }


def _starts_share_component(cooked_dir: Path) -> bool:
    """Flood-fill passability from start 0; true if all starts reachable."""

    terrain = json.loads((cooked_dir / "terrain.json").read_text(encoding="utf-8"))
    waypoints = json.loads((cooked_dir / "waypoints.json").read_text(encoding="utf-8"))
    height = terrain["height"]
    width = int(height["width"])
    height_cells = int(height["height"])
    scale = float(height.get("horizontalScale") or 10.0)
    passability = terrain["passability"]
    bit_path = cooked_dir / str(passability["path"])
    raw = bit_path.read_bytes()
    row_stride = int(passability["rowStrideBytes"])

    def impassable(gx: int, gy: int) -> bool:
        if gx < 0 or gy < 0 or gx >= width or gy >= height_cells:
            return True
        byte_index = gy * row_stride + (gx // 8)
        if byte_index >= len(raw):
            return True
        return bool(raw[byte_index] & (1 << (gx % 8)))

    starts: list[tuple[int, int]] = []
    player_starts = waypoints.get("playerStarts") or {}
    if isinstance(player_starts, dict):
        ordered = sorted(
            player_starts.values(),
            key=lambda row: int(row.get("playerIndex", 0)),
        )
        for row in ordered:
            pos = row.get("sagePosition")
            if not isinstance(pos, (list, tuple)) or len(pos) < 2:
                continue
            sx, sy = float(pos[0]), float(pos[1])
            gx = int(sx / scale + 0.5)
            gy = int(sy / scale + 0.5)
            starts.append((gx, gy))

    if len(starts) < 2:
        return False

    starts = [
        (max(0, min(width - 1, x)), max(0, min(height_cells - 1, y))) for x, y in starts
    ]

    def nearest_walkable(cell: tuple[int, int]) -> tuple[int, int] | None:
        if not impassable(*cell):
            return cell
        for radius in range(1, 16):
            for dx in range(-radius, radius + 1):
                for dy in range(-radius, radius + 1):
                    cx, cy = cell[0] + dx, cell[1] + dy
                    if cx < 0 or cy < 0 or cx >= width or cy >= height_cells:
                        continue
                    if not impassable(cx, cy):
                        return (cx, cy)
        return None

    snapped: list[tuple[int, int]] = []
    for cell in starts:
        walkable = nearest_walkable(cell)
        if walkable is None:
            return False
        snapped.append(walkable)

    origin = snapped[0]
    seen = {origin}
    stack = [origin]
    while stack:
        x, y = stack.pop()
        for nx, ny in ((x + 1, y), (x - 1, y), (x, y + 1), (x, y - 1)):
            if (nx, ny) in seen or impassable(nx, ny):
                continue
            seen.add((nx, ny))
            stack.append((nx, ny))

    return all(cell in seen for cell in snapped)


def _constrain_output(path: Path, state_root: Path) -> Path:
    """Reports must stay under state_root/reports (private)."""

    reports_root = (state_root / "reports").resolve()
    resolved = path.expanduser().resolve()
    try:
        resolved.relative_to(reports_root)
    except ValueError as exc:
        raise SystemExit(
            f"--output must be under private reports root {reports_root}, got {resolved}"
        ) from exc
    return resolved


def _constrain_cooked_root(path: Path, state_root: Path) -> Path:
    """Persistent cooked bytes must remain under the private state root."""

    private_root = state_root.resolve()
    resolved = path.expanduser().resolve()
    try:
        resolved.relative_to(private_root)
    except ValueError as exc:
        raise SystemExit(
            f"--cooked-root must be under private state root {private_root}, got {resolved}"
        ) from exc
    return resolved


def _cached_cook_matches(cooked: Path, source_sha256: str) -> bool:
    try:
        document = json.loads((cooked / "map.json").read_text(encoding="utf-8"))
        source = document.get("source", {})
        required = (
            cooked / str(document.get("terrain", "terrain.json")),
            cooked / str(document.get("objects", "objects.json")),
            cooked / str(document.get("waypoints", "waypoints.json")),
        )
        return source.get("sha256") == source_sha256 and all(
            path.is_file() for path in required
        )
    except (OSError, UnicodeError, json.JSONDecodeError, TypeError):
        return False


def _publish_cached_cook(staged: Path, destination: Path) -> None:
    """Replace one private cache directory without exposing a partial cook."""

    destination.parent.mkdir(parents=True, exist_ok=True)
    incoming = destination.parent / f".{destination.name}.incoming-{uuid.uuid4().hex}"
    backup = destination.parent / f".{destination.name}.previous-{uuid.uuid4().hex}"
    shutil.copytree(staged, incoming)
    try:
        if destination.exists():
            os.replace(destination, backup)
        os.replace(incoming, destination)
    except Exception:
        if not destination.exists() and backup.exists():
            os.replace(backup, destination)
        raise
    finally:
        shutil.rmtree(incoming, ignore_errors=True)
        shutil.rmtree(backup, ignore_errors=True)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--game", choices=("rotwk", "bfme2"), default="rotwk")
    parser.add_argument("--state-root", type=Path, default=None)
    parser.add_argument("--limit", type=int, default=0)
    parser.add_argument("--output", type=Path, default=None)
    parser.add_argument(
        "--cooked-root",
        type=Path,
        default=None,
        help=(
            "private state-root directory that persistently caches strict cooked "
            "map directories; unchanged source hashes are reused"
        ),
    )
    parser.add_argument(
        "--skip-connectivity",
        action="store_true",
        help="only strict cook; skip passability start-component check",
    )
    args = parser.parse_args(argv)

    install = args.install.expanduser().resolve()
    marker = install / "game.dat"
    if args.game == "rotwk" and not marker.is_file():
        marker = install / "layer-0-rotwk" / "game.dat"
    if not marker.is_file():
        print(f"FAIL: install has no game.dat: {install}", file=sys.stderr)
        return 2

    state_root = args.state_root
    if state_root is None:
        state_root = ROOT / "workspace" / "retail-work"
    state_root = ensure_external_to_repo(
        Path(state_root).expanduser().resolve(), repo_root_from_module()
    )
    os.environ["OPENBFME_IMPORT_ROOT"] = str(state_root)
    cooked_root = (
        _constrain_cooked_root(args.cooked_root, state_root)
        if args.cooked_root is not None
        else None
    )

    catalog = _load_catalog(state_root, args.game, install)
    run_id = uuid.uuid4().hex
    reports_dir = state_root / "reports"
    reports_dir.mkdir(parents=True, exist_ok=True)
    ledger = ConversionLedger(
        run_id=run_id,
        game=args.game,
        install_root=str(install),
        sink_path=reports_dir / f"{args.game}-map-cook-ledger-{run_id}.jsonl",
    )
    registry_source = _read_virtual(
        catalog, MAPCACHE_VIRTUAL_PATH, max_bytes=MAX_MAPCACHE_BYTES
    )
    registry = parse_mapcache_bytes(registry_source)
    selected = [
        row
        for row in registry
        if bool(row["isMultiplayer"])
        and bool(row["isOfficial"])
        and not bool(row["isScenarioMp"])
    ]
    selected.sort(key=lambda row: str(row["virtualPath"]).casefold())
    eligible_count = len(selected)
    if args.limit and args.limit > 0:
        selected = selected[: args.limit]

    results: list[dict[str, Any]] = []
    verdicts: Counter[str] = Counter()
    slug_owners: dict[str, str] = {}

    # Private job root for all retail map bytes (Agents.md: workspace/scratch/jobs).
    job_root = (
        ROOT
        / "workspace"
        / "scratch"
        / "jobs"
        / "rotwk-map-cook-corpus"
        / uuid.uuid4().hex
    )
    job_root.mkdir(parents=True, exist_ok=True)

    try:
        for index, record in enumerate(selected):
            virtual = str(record["virtualPath"])
            slug = _map_slug(virtual)
            if slug in slug_owners and slug_owners[slug] != virtual.casefold():
                # Disambiguate collisions
                slug = f"{slug}-{index:04d}"
            slug_owners[slug] = virtual.casefold()
            map_id = f"openbfme.map.{slug}"
            entry: dict[str, Any] = {
                "path": virtual,
                "slug": slug,
                "mapId": map_id,
                "registryPlayerCount": int(record["playerCount"]),
                "companions": _companion_flags(catalog, virtual),
            }
            try:
                entry_meta = catalog.resolve_exact(virtual)
                if entry_meta is None:
                    entry["verdict"] = "registry-stale-missing-payload"
                    results.append(entry)
                    verdicts[entry["verdict"]] += 1
                    ledger.record(
                        kind="map-cook",
                        unit_id=virtual,
                        status="skipped",
                        detail="registry-stale-missing-payload",
                    )
                    print(f"[{index + 1}/{len(selected)}] {entry['verdict']}: {virtual}")
                    continue
                source = _read_virtual(catalog, virtual, max_bytes=MAX_SOURCE_BYTES)
                entry["sourceSha256"] = hashlib.sha256(source).hexdigest()
                entry["sourceBytes"] = len(source)
                parsed = parse_sage_map_bytes(source, profile="multiplayer")
                starts = len(parsed.player_starts)
                entry["playerStarts"] = starts
                work = job_root / f"{index:04d}-{slug}"
                cached = cooked_root / slug if cooked_root is not None else None
                cache_hit = cached is not None and _cached_cook_matches(
                    cached, entry["sourceSha256"]
                )
                if cache_hit:
                    cooked = cached
                    entry["cacheHit"] = True
                else:
                    cooked = work / "cooked"
                    cooked.mkdir(parents=True, exist_ok=True)
                    source_path = work / "source.map"
                    source_path.write_bytes(source)
                    convert_sage_map(
                        source_path,
                        cooked,
                        metadata={"id": map_id, "displayName": slug},
                        expected={},
                        object_bindings=None,
                        profile="multiplayer",
                    )
                    entry["cacheHit"] = False
                map_doc = json.loads((cooked / "map.json").read_text(encoding="utf-8"))
                entry["conversionStatus"] = map_doc.get("conversionStatus")
                if starts < 2:
                    entry["verdict"] = "under-two-player-starts"
                elif args.skip_connectivity:
                    entry["verdict"] = "cooked"
                else:
                    try:
                        shared = _starts_share_component(cooked)
                        entry["startsShareComponent"] = shared
                        # Honest name: connectivity probe, not a full map contract.
                        entry["verdict"] = (
                            "cooked-and-connected"
                            if shared
                            else "cooked-but-starts-disconnected"
                        )
                    except Exception as conn_exc:
                        entry["verdict"] = "cooked-connectivity-check-failed"
                        entry["connectivityError"] = str(conn_exc)[:500]
                if cached is not None and not cache_hit and entry["verdict"] not in FATAL_VERDICTS:
                    _publish_cached_cook(cooked, cached)
                if cached is not None and entry["verdict"] not in FATAL_VERDICTS:
                    entry["cookedPath"] = str(cached)
            except SageMapError as exc:
                entry["verdict"] = "cook-rejected"
                entry["error"] = str(exc)[:800]
            except Exception as exc:
                entry["verdict"] = "cook-error"
                entry["error"] = f"{type(exc).__name__}: {exc}"[:800]
                entry["traceback"] = traceback.format_exc()[-1000:]
            results.append(entry)
            verdicts[entry["verdict"]] += 1
            status = str(entry["verdict"])
            ledger_status = (
                "connected"
                if status == "cooked-and-connected"
                else "disconnected"
                if status == "cooked-but-starts-disconnected"
                else "failed"
                if status in FATAL_VERDICTS
                else "skipped"
                if status == "registry-stale-missing-payload"
                else "cooked"
            )
            ledger.record(
                kind="map-cook",
                unit_id=virtual,
                status=ledger_status,
                detail=status,
                error=entry.get("error"),
                metrics={
                    "playerStarts": entry.get("playerStarts"),
                    "sourceBytes": entry.get("sourceBytes"),
                },
                log_to_stderr=ledger_status == "failed",
            )
            print(f"[{index + 1}/{len(selected)}] {entry['verdict']}: {virtual}")
    finally:
        # Best-effort cleanup of private job tree (may be large).
        try:
            import shutil

            shutil.rmtree(job_root, ignore_errors=True)
        except Exception:
            pass

    install_sha = ""
    game_dat = marker
    if game_dat.is_file():
        # Size + mtime marker only (not full hash of multi-GB tree).
        st = game_dat.stat()
        install_sha = hashlib.sha256(
            f"{install.resolve()}|{st.st_size}|{st.st_mtime_ns}".encode()
        ).hexdigest()

    ledger_summary = ledger.write_summary(
        reports_dir / f"{args.game}-map-cook-ledger-summary.json"
    )
    report = {
        "schema": REPORT_SCHEMA,
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "game": args.game,
        "installRoot": str(install.resolve()),
        "installIdentitySha256": install_sha,
        "criteria": (
            "mapcache isMultiplayer+isOfficial+!isScenarioMP; catalog payload; "
            "strict convert_sage_map multiplayer; passability start-component "
            "connectivity when not --skip-connectivity; temp under private jobs"
        ),
        "eligibleMapCount": eligible_count,
        "requestedLimit": args.limit or None,
        "mapCount": len(results),
        "verdictCounts": dict(sorted(verdicts.items())),
        "fatalVerdictCount": sum(verdicts[v] for v in FATAL_VERDICTS if v in verdicts),
        "conversionLedger": ledger_summary,
        "maps": results,
    }
    out = args.output
    if out is None:
        out = state_root / "reports" / f"{args.game}-map-cook-corpus.json"
    else:
        out = _constrain_output(Path(out), state_root)
    out.parent.mkdir(parents=True, exist_ok=True)
    write_json_atomic(out, report)
    print(f"REPORT {out}")
    print(f"VERDICTS {json.dumps(report['verdictCounts'], sort_keys=True)}")
    if report["fatalVerdictCount"] > 0:
        print(
            f"FAIL fatal verdicts={report['fatalVerdictCount']}",
            file=sys.stderr,
        )
        return 3
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
