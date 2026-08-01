#!/usr/bin/env python3
"""Generate RotWK skirmish multi-map catalog + optional full profile/build.

Two honest paths:

1. **Catalog proof (default for RotWK)** — build ``data/maps.json`` from the
   same official multiplayer registry the map-cook / binding factories use.
   RotWK's install ``terrain.big`` only ships a thin texture set; most map
   terrain bytes live in the BFME2 base that the layered effective-assets tree
   already extracted. Catalog proof does not invent terrain; it proves the
   skirmish shell can list every official map id.

2. **Full generate-map-profile** (``--full-profile``) — existing importer path
   with optional effective-assets binder. May reject maps when terrain textures
   are absent from the RotWK-only catalog.

Optional ``--build`` / ``--publish`` cook a pack (owner-controlled publish).
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import subprocess
import sys
import uuid
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
    parse_sage_map_bytes,
)
from openbfme_importer.util import write_json_atomic  # noqa: E402

REPORT_SCHEMA = "openbfme.rotwk-multimap-skirmish"
REPORT_SCHEMA_VERSION = 1
# Official multiplayer corpus sizes for product editions (fail closed if install
# registry yields a different official count).
EXPECTED_OFFICIAL_MP_MAPS = {
    "rotwk": 72,
}


def _default_effective_assets(state_root: Path, game: str) -> Path | None:
    for rel in (
        ("editions", game, "cache", "layered-effective-assets"),
        ("editions", game, "cache", "effective-assets"),
        ("cache", "layered-effective-assets"),
        ("cache", "effective-assets"),
    ):
        path = state_root.joinpath(*rel)
        if path.is_dir():
            return path
    return None


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
    """Stable unique slug; keep mp/wor/ang kind tokens to avoid name collisions."""
    parts = virtual_path.replace("\\", "/").split("/")
    folder = parts[-2] if len(parts) >= 2 else Path(virtual_path).stem
    words = [w for w in folder.casefold().split() if w]
    if words and words[0] == "map":
        words = words[1:]
    return "-".join(words) if words else Path(virtual_path).stem.casefold()


def _display_name(virtual_path: str) -> str:
    parts = virtual_path.replace("\\", "/").split("/")
    folder = parts[-2] if len(parts) >= 2 else Path(virtual_path).stem
    words = [w for w in folder.split() if w]
    if words and words[0].casefold() == "map":
        words = words[1:]
    # Keep kind token in the display name for disambiguation.
    return " ".join(w.capitalize() for w in words) if words else folder


def build_registry_skirmish_catalog(
    catalog: InstallCatalog, *, game: str
) -> dict[str, Any]:
    """Build maps.json + profile shell from official multiplayer registry rows."""
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

    maps: list[dict[str, Any]] = []
    rejections: list[dict[str, Any]] = []
    resources: list[dict[str, Any]] = []

    for record in selected:
        virtual = str(record["virtualPath"])
        slug = _map_slug(virtual)
        display = _display_name(virtual)
        if catalog.resolve_exact(virtual) is None:
            rejections.append(
                {
                    "virtualPath": virtual,
                    "slug": slug,
                    "status": "registry-stale-missing-payload",
                }
            )
            continue
        try:
            source = _read_virtual(catalog, virtual, max_bytes=MAX_SOURCE_BYTES)
            parsed = parse_sage_map_bytes(source, profile="multiplayer")
            player_count = len(parsed.player_starts)
        except (SageMapError, OSError, ValueError) as exc:
            rejections.append(
                {
                    "virtualPath": virtual,
                    "slug": slug,
                    "status": "parse-rejected",
                    "reason": str(exc)[:400],
                }
            )
            continue

        map_id = f"{game}.map.{slug}"
        output_root = f"maps/{slug}"
        resources.append(
            {
                "id": f"map-{slug}-binary",
                "kind": "map",
                "converter": "sage-map",
                "patterns": [virtual.replace("\\", "/")],
                "output": f"{output_root}/map.json",
                "required": True,
                "limit": 1,
                "expected_count": 1,
                "options": {
                    "id": map_id,
                    "displayName": display,
                    "profile": "multiplayer",
                },
            }
        )
        maps.append(
            {
                "id": map_id,
                "displayName": display,
                "category": "skirmish",
                "map": f"{output_root}/map.json",
                "playerCount": player_count,
                "registryPlayerCount": int(record.get("numPlayers") or 0) or None,
                "routingGraphStatus": "source-waypoint-edges-present-runtime-pending",
                "navigationMeshStatus": "not-generated-or-validated-by-map-profile",
                "terrainMaterialsStatus": "pending-bfme2-base-or-layered-assets",
            }
        )

    if not maps:
        raise SystemExit("registry skirmish catalog resolved zero convertible maps")
    if rejections:
        sample = "; ".join(
            f"{row.get('slug')}:{row.get('status')}" for row in rejections[:8]
        )
        raise SystemExit(
            f"registry skirmish catalog rejected {len(rejections)} official maps "
            f"(proof requires zero rejections): {sample}"
        )
    if len(maps) != len(selected):
        raise SystemExit(
            f"registry catalog incomplete: maps={len(maps)} selected={len(selected)}"
        )
    expected = EXPECTED_OFFICIAL_MP_MAPS.get(game)
    if expected is not None and len(maps) != expected:
        raise SystemExit(
            f"registry catalog mapCount={len(maps)} != expected official "
            f"corpus {expected} for game={game}"
        )

    resource_ids = [str(r["id"]) for r in resources]
    map_ids = [str(m["id"]) for m in maps]
    map_outputs = [str(m["map"]) for m in maps]
    for label, values in (
        ("resource id", resource_ids),
        ("map id", map_ids),
        ("map output", map_outputs),
    ):
        seen: set[str] = set()
        for value in values:
            key = value.casefold()
            if key in seen:
                raise SystemExit(f"duplicate {label} in registry catalog: {value}")
            seen.add(key)

    pack_id = f"{game}-skirmish-maps-private"
    profile = {
        "format": 1,
        "id": f"{game}-skirmish-maps-generated",
        "title": f"{game} skirmish map private generated pack (registry catalog)",
        "pack": {
            "id": pack_id,
            "version": "skirmish-registry-catalog-v0",
            "schema": "openbfme.content-pack",
            "schemaVersion": 0,
            "priority": 905,
            "vertical_slice_complete": False,
            "capability_maturity": (
                "registry-map-catalog-shell-ready-terrain-cook-pending"
            ),
            "dataPolicy": {
                "externalPathsAllowed": False,
                "redistributable": False,
            },
            "files": {
                "entryMap": str(maps[0]["map"]),
                "mapCatalog": "data/maps.json",
            },
        },
        "resources": resources,
        "runtime_data": {
            "data/maps.json": {
                "schema": "openbfme.map-catalog",
                "schemaVersion": 0,
                "maps": maps,
            }
        },
        "planning_evidence": {
            "schema": "openbfme.map-profile-planning-evidence",
            "schemaVersion": 0,
            "mode": "registry-skirmish-catalog",
            "mapCount": len(maps),
            "selectedOfficialMapCount": len(selected),
            "rejectedMaps": rejections,
            "terrainNote": (
                "RotWK install terrain.big is thin; full terrain material cook "
                "requires BFME2 base / layered effective-assets closure."
            ),
        },
    }
    return profile


def _run(cmd: list[str], *, env: dict[str, str]) -> subprocess.CompletedProcess[str]:
    print("RUN", " ".join(cmd), flush=True)
    return subprocess.run(
        cmd,
        cwd=str(ROOT),
        env=env,
        text=True,
        capture_output=True,
        check=False,
    )


def _parse_cli_json(text: str) -> dict[str, Any]:
    """Parse importer --json output (pretty multi-line or trailing progress)."""
    blob = (text or "").strip()
    if not blob:
        raise ValueError("empty CLI stdout")
    try:
        value = json.loads(blob)
        if isinstance(value, dict):
            return value
    except json.JSONDecodeError:
        pass
    decoder = json.JSONDecoder()
    last: dict[str, Any] | None = None
    for index, char in enumerate(blob):
        if char != "{":
            continue
        try:
            obj, _end = decoder.raw_decode(blob[index:])
        except json.JSONDecodeError:
            continue
        if isinstance(obj, dict):
            last = obj
    if last is None:
        raise ValueError("no JSON object found in CLI output")
    return last


def profile_binding_inventory(profile: dict[str, Any]) -> dict[str, Any]:
    """Summarize the exact model/structure rows handed to sage-map cooks."""
    maps: dict[str, dict[str, Any]] = {}
    for resource in profile.get("resources") or []:
        if resource.get("converter") != "sage-map":
            continue
        output = str(resource.get("output") or "").replace("\\", "/").rstrip("/")
        options = resource.get("options") or {}
        bindings = options.get("objectBindings")
        if not isinstance(bindings, dict):
            continue
        models = list(bindings.get("models") or [])
        structures = list(bindings.get("structures") or [])
        maps[output] = {
            "modelCount": len(models),
            "structureCount": len(structures),
            "logicalCount": len(bindings.get("logical") or []),
            "boundTypeNames": sorted(
                str(row.get("typeName") or "")
                for row in [*models, *structures]
                if isinstance(row, dict) and row.get("typeName")
            ),
        }
    return {
        "maps": maps,
        "mapCount": len(maps),
        "modelCount": sum(row["modelCount"] for row in maps.values()),
        "structureCount": sum(row["structureCount"] for row in maps.values()),
        "logicalCount": sum(row["logicalCount"] for row in maps.values()),
    }


def verify_pack_binding_inventory(
    pack_dir: Path, planned: dict[str, Any]
) -> dict[str, Any]:
    """Prove planned visual rows survived the sage-map cook into the pack."""
    rows: list[dict[str, Any]] = []
    missing: list[str] = []
    for output, expected in sorted((planned.get("maps") or {}).items()):
        expected_names = set(expected.get("boundTypeNames") or [])
        if not expected_names:
            continue
        path = pack_dir / output / "object-bindings.json"
        actual_names: set[str] = set()
        if path.is_file():
            document = json.loads(path.read_text(encoding="utf-8"))
            actual_names = {
                str(row.get("typeName") or "")
                for row in document.get("records") or []
                if isinstance(row, dict) and row.get("status") == "bound"
            }
        absent = sorted(expected_names - actual_names)
        if absent:
            missing.extend(f"{output}:{name}" for name in absent)
        rows.append(
            {
                "mapOutput": output,
                "path": str(path),
                "plannedBoundTypeCount": len(expected_names),
                "cookedBoundTypeCount": len(actual_names),
                "missingTypeNames": absent,
            }
        )
    return {
        "ok": bool(rows) and not missing,
        "checkedMapCount": len(rows),
        "missingCount": len(missing),
        "missingSample": missing[:40],
        "maps": rows,
    }


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--install", required=True, type=Path)
    parser.add_argument("--game", choices=("rotwk", "bfme2"), default="rotwk")
    parser.add_argument("--state-root", type=Path, default=None)
    parser.add_argument("--effective-assets", type=Path, default=None)
    parser.add_argument(
        "--full-profile",
        action="store_true",
        help="use generate-map-profile (terrain-closed) instead of registry catalog",
    )
    parser.add_argument(
        "--no-binder",
        action="store_true",
        help="with --full-profile, skip effective-assets prop binder",
    )
    parser.add_argument("--build", action="store_true")
    parser.add_argument("--publish", action="store_true")
    parser.add_argument(
        "--select",
        action="store_true",
        help="also activate the published pack; omitted by default",
    )
    parser.add_argument("--map-limit", type=int, default=None, metavar="N")
    parser.add_argument("--allow-incomplete", action="store_true")
    parser.add_argument("--python", type=Path, default=None)
    parser.add_argument("--output", type=Path, default=None)
    args = parser.parse_args(argv)

    if args.publish and not args.build:
        print("FAIL: --publish requires --build", file=sys.stderr)
        return 2
    if args.select and not args.publish:
        print("FAIL: --select requires --publish", file=sys.stderr)
        return 2
    if args.map_limit is not None and args.map_limit <= 0:
        print("FAIL: --map-limit must be greater than zero", file=sys.stderr)
        return 2
    if args.no_binder and not args.full_profile:
        print(
            "FAIL: --no-binder only applies with --full-profile "
            "(registry-catalog path has no binder)",
            file=sys.stderr,
        )
        return 2

    operator_install = args.install.expanduser().resolve()
    if not (operator_install / "game.dat").is_file():
        print(f"FAIL: no game.dat at {operator_install}", file=sys.stderr)
        return 2

    state_root = args.state_root
    if state_root is None:
        state_root = ROOT / ".private" / "retail-work"
    state_root = ensure_external_to_repo(
        Path(state_root).expanduser().resolve(), repo_root_from_module()
    )
    os.environ["OPENBFME_IMPORT_ROOT"] = str(state_root)

    # Full terrain-closed profiles need the layered RotWK+BFME2 install so
    # multiplayer map textures that only live in the BFME2 base resolve.
    content_install = operator_install
    layered_used = False
    if args.game == "rotwk" and args.full_profile:
        sys.path.insert(0, str(ROOT / "tools"))
        from rotwk_layered_install import (  # type: ignore
            ensure_layered_rotwk_install,
            layered_rotwk_install,
        )

        layered = layered_rotwk_install(state_root)
        if layered is None:
            try:
                layered = ensure_layered_rotwk_install(
                    state_root, rotwk_install=operator_install
                )
            except Exception as exc:
                print(f"FAIL layered install required for --full-profile: {exc}", file=sys.stderr)
                return 2
        content_install = layered
        layered_used = True
        print(f"LAYERED_INSTALL {content_install}", flush=True)

    python = args.python
    if python is None:
        python = state_root / "tools" / "python-3.12-env" / "Scripts" / "python.exe"
        if not python.is_file():
            python = Path(sys.executable)
    python = Path(python).expanduser().resolve()
    cli = ROOT / "tools" / "openbfme_import.py"

    assets: Path | None = None
    if args.full_profile and not args.no_binder:
        assets = (
            args.effective_assets.expanduser().resolve()
            if args.effective_assets
            else _default_effective_assets(state_root, args.game)
        )
        if assets is None:
            print(
                "FAIL: no effective-assets tree; pass --effective-assets or --no-binder",
                file=sys.stderr,
            )
            return 2

    env = os.environ.copy()
    env["OPENBFME_IMPORT_ROOT"] = str(state_root)
    env["PYTHONPATH"] = str(ROOT / "importer") + os.pathsep + env.get("PYTHONPATH", "")
    install = content_install  # used by generate-map-profile / build
    profiles_dir = state_root / "profiles"
    profiles_dir.mkdir(parents=True, exist_ok=True)
    mode = "full-profile" if args.full_profile else "registry-catalog"
    profile_path: Path
    gen_payload: dict[str, Any] = {}

    if args.full_profile:
        gen_cmd = [
            str(python),
            str(cli),
            "--json",
            "generate-map-profile",
            "--game",
            args.game,
            "--install",
            str(install),
            "--map-set",
            "skirmish",
        ]
        if assets is not None:
            gen_cmd.extend(["--effective-assets", str(assets)])
        if args.map_limit is not None:
            gen_cmd.extend(["--map-limit", str(args.map_limit)])
        gen = _run(gen_cmd, env=env)
        if gen.returncode != 0:
            print(gen.stdout, end="")
            print(gen.stderr, file=sys.stderr, end="")
            print(f"FAIL generate-map-profile exit={gen.returncode}", file=sys.stderr)
            return 3
        try:
            gen_payload = _parse_cli_json(gen.stdout or gen.stderr)
        except Exception as exc:
            print(gen.stdout, end="")
            print(gen.stderr, file=sys.stderr, end="")
            print(f"FAIL parse generate-map-profile JSON: {exc}", file=sys.stderr)
            return 3
        profile_path = Path(str(gen_payload.get("profile") or ""))
        if not profile_path.is_file():
            print(f"FAIL missing profile path: {profile_path}", file=sys.stderr)
            return 3
        profile = json.loads(profile_path.read_text(encoding="utf-8"))
    else:
        catalog = _load_catalog(state_root, args.game, operator_install)
        profile = build_registry_skirmish_catalog(catalog, game=args.game)
        profile_path = profiles_dir / f"{args.game}-skirmish-maps.generated.json"
        write_json_atomic(profile_path, profile)
        payload = json.dumps(profile, indent=2, sort_keys=True, ensure_ascii=False) + "\n"
        gen_payload = {
            "ready": True,
            "profile": str(profile_path),
            "profile_sha256": hashlib.sha256(payload.encode("utf-8")).hexdigest(),
            "resource_count": len(profile["resources"]),
            "map_count": len(profile["runtime_data"]["data/maps.json"]["maps"]),
            "rejected_map_count": len(
                (profile.get("planning_evidence") or {}).get("rejectedMaps") or []
            ),
            "unbound_object_type_count": 0,
        }
        print(f"PROFILE {profile_path} maps={gen_payload['map_count']}", flush=True)

    maps = list(
        (profile.get("runtime_data") or {})
        .get("data/maps.json", {})
        .get("maps")
        or []
    )
    if args.map_limit is not None and len(maps) != args.map_limit:
        print(
            f"FAIL profile mapCount={len(maps)} != requested --map-limit={args.map_limit}",
            file=sys.stderr,
        )
        return 4

    if not maps:
        print("FAIL profile has zero maps in data/maps.json", file=sys.stderr)
        return 4

    # Fail-closed uniqueness before any proof_ok=True claim.
    map_ids = [str(m.get("id") or "") for m in maps]
    map_paths = [str(m.get("map") or "") for m in maps]
    resource_ids = [str(r.get("id") or "") for r in (profile.get("resources") or [])]
    for label, values in (
        ("map id", map_ids),
        ("map output path", map_paths),
        ("resource id", resource_ids),
    ):
        folded = [v.casefold() for v in values if v]
        if len(folded) != len(set(folded)):
            print(f"FAIL profile has duplicate {label}s", file=sys.stderr)
            return 4

    rejected_count = int(gen_payload.get("rejected_map_count") or 0)
    evidence = profile.get("planning_evidence") or {}
    rejected_rows = list(evidence.get("rejectedMaps") or [])
    if rejected_count > 0 or rejected_rows:
        print(
            f"FAIL catalog proof requires zero rejected maps "
            f"(rejected_map_count={rejected_count} rejectedMaps={len(rejected_rows)})",
            file=sys.stderr,
        )
        return 4
    selected_official = evidence.get("selectedOfficialMapCount")
    if selected_official is not None and int(selected_official) != len(maps):
        print(
            f"FAIL catalog incomplete vs official registry: "
            f"maps={len(maps)} selectedOfficial={selected_official}",
            file=sys.stderr,
        )
        return 4
    # Registry-catalog mode proves the full official multiplayer corpus (72 on
    # RotWK). Full-profile skirmish uses retail mapcache skirmish category only
    # (smaller set) and must not be forced to 72.
    if mode == "registry-catalog":
        expected_corpus = EXPECTED_OFFICIAL_MP_MAPS.get(args.game)
        if expected_corpus is not None:
            if len(maps) != expected_corpus or (
                selected_official is not None
                and int(selected_official) != expected_corpus
            ):
                print(
                    f"FAIL catalog corpus size maps={len(maps)} "
                    f"selectedOfficial={selected_official} "
                    f"expected={expected_corpus} for game={args.game}",
                    file=sys.stderr,
                )
                return 4
    elif mode == "full-profile" and len(maps) < 2:
        print("FAIL full-profile produced fewer than 2 maps", file=sys.stderr)
        return 4

    # Full cook profiles must load under ImportProfile. Registry-catalog mode is
    # a shell catalog document (terrain cook still pending) and is validated by
    # uniqueness + full-corpus checks above, not by ImportProfile.
    if mode == "full-profile":
        try:
            from openbfme_importer.profile import ImportProfile

            ImportProfile.load(profile_path)
        except Exception as exc:
            print(f"FAIL ImportProfile.load: {exc}", file=sys.stderr)
            return 4

    categories: dict[str, int] = {}
    for row in maps:
        cat = str(row.get("category") or "unknown")
        categories[cat] = categories.get(cat, 0) + 1

    binding_inventory = profile_binding_inventory(profile)
    visual_binding_count = (
        binding_inventory["modelCount"] + binding_inventory["structureCount"]
    )
    if args.full_profile and assets is not None and visual_binding_count == 0:
        print(
            "FAIL full-profile binder planned zero model/structure bindings",
            file=sys.stderr,
        )
        return 4

    pack_meta = profile.get("pack") or {}
    pack_id = str(pack_meta.get("id") or "")
    map_catalog_rel = str((pack_meta.get("files") or {}).get("mapCatalog") or "")
    entry_map = str((pack_meta.get("files") or {}).get("entryMap") or "")

    proof: dict[str, Any] = {
        "schema": REPORT_SCHEMA,
        "schemaVersion": REPORT_SCHEMA_VERSION,
        "runId": uuid.uuid4().hex,
        "game": args.game,
        "installRoot": str(install),
        "operatorInstallRoot": str(operator_install),
        "layeredInstall": layered_used,
        "mode": mode,
        "effectiveAssets": str(assets) if assets else None,
        "binderEnabled": assets is not None,
        "profile": str(profile_path),
        "profileSha256": gen_payload.get("profile_sha256"),
        "mapCount": len(maps),
        "resourceCount": gen_payload.get("resource_count"),
        "rejectedMapCount": 0,
        "unboundObjectTypeCount": gen_payload.get("unbound_object_type_count"),
        "unboundObjectTypes": sorted(
            {
                str(name)
                for row in (evidence.get("unboundObjectTypes") or {}).values()
                for name in ((row or {}).get("typeNames") or [])
            }
        ),
        "bindingInventory": binding_inventory,
        "categoryCounts": categories,
        "packId": pack_id,
        "entryMap": entry_map,
        "mapCatalog": map_catalog_rel,
        "catalogSample": [
            {
                "id": row.get("id"),
                "displayName": row.get("displayName"),
                "category": row.get("category"),
                "playerCount": row.get("playerCount"),
                "map": row.get("map"),
            }
            for row in maps[:8]
        ],
        "catalogProof": {
            "ok": True,
            "reason": f"{mode}-runtime-data-maps-json-full-corpus",
            "mapCount": len(maps),
            "requiresMapCatalogField": map_catalog_rel == "data/maps.json",
            "zeroRejections": True,
        },
        "build": None,
    }

    if not proof["catalogProof"]["requiresMapCatalogField"]:
        proof["catalogProof"]["ok"] = False
        proof["catalogProof"]["reason"] = f"unexpected mapCatalog field {map_catalog_rel!r}"

    if args.build:
        build_cmd = [
            str(python),
            str(cli),
            "--json",
            "build",
            "--game",
            args.game,
            "--install",
            str(install),
            "--profile",
            str(profile_path),
        ]
        if not args.publish:
            build_cmd.append("--no-publish")
        elif not args.select:
            build_cmd.append("--no-select")
        if args.allow_incomplete:
            build_cmd.append("--allow-incomplete")
        built = _run(build_cmd, env=env)
        build_info: dict[str, Any] = {
            "exitCode": built.returncode,
            "publish": bool(args.publish),
            "stdoutTail": "\n".join(built.stdout.splitlines()[-40:]),
            "stderrTail": "\n".join(built.stderr.splitlines()[-40:]),
        }
        proof["build"] = build_info
        if built.returncode != 0:
            proof["catalogProof"]["packMount"] = {
                "ok": False,
                "reason": f"build-failed-exit-{built.returncode}",
            }
            reports = state_root / "reports"
            reports.mkdir(parents=True, exist_ok=True)
            out = args.output or (reports / f"{args.game}-multimap-skirmish.json")
            write_json_atomic(out, proof)
            print(f"REPORT {out}")
            print(f"FAIL build exit={built.returncode}", file=sys.stderr)
            return 5

        # Exact pack path from the build receipt (no stale directory scan):
        # - --publish: require top-level published_pack (content-packs mount path)
        # - build only: require top-level pack (private cook output path)
        maps_json_path: Path | None = None
        try:
            build_payload = _parse_cli_json(built.stdout or built.stderr)
            if args.publish:
                pack_dir = build_payload.get("published_pack")
                if not pack_dir:
                    raise ValueError(
                        "build receipt missing top-level published_pack "
                        "(publish mode)"
                    )
                build_info["publishedPack"] = str(pack_dir)
            else:
                pack_dir = build_payload.get("pack")
                if not pack_dir:
                    raise ValueError(
                        "build receipt missing top-level pack (build-only mode)"
                    )
                build_info["builtPack"] = str(pack_dir)
            candidate = Path(str(pack_dir)) / "data" / "maps.json"
            if not candidate.is_file():
                raise ValueError(f"receipt pack maps.json missing: {candidate}")
            maps_json_path = candidate
            if args.publish:
                build_info["selectionExpected"] = bool(args.select)
            build_info["buildPayloadKeys"] = sorted(build_payload.keys())
        except Exception as exc:
            build_info["receiptError"] = str(exc)[:500]

        if maps_json_path and maps_json_path.is_file():
            pack_doc = json.loads(maps_json_path.read_text(encoding="utf-8"))
            pack_maps = list(pack_doc.get("maps") or [])
            build_info["packMapsJson"] = str(maps_json_path)
            build_info["packMapCount"] = len(pack_maps)
            build_info["packMapsSha256"] = hashlib.sha256(
                maps_json_path.read_bytes()
            ).hexdigest()
            profile_pairs = {
                (str(row.get("id") or ""), str(row.get("map") or "")) for row in maps
            }
            pack_pairs = {
                (str(row.get("id") or ""), str(row.get("map") or ""))
                for row in pack_maps
            }
            pairs_match = (
                profile_pairs == pack_pairs
                and len(pack_maps) == len(maps)
                and len(pack_pairs) == len(maps)
            )
            proof["catalogProof"]["packMount"] = {
                "ok": pairs_match and len(pack_maps) > 0,
                "mapCount": len(pack_maps),
                "path": str(maps_json_path),
                "pairsMatch": pairs_match,
                "mode": "published_pack" if args.publish else "built_pack",
            }
            binding_proof = verify_pack_binding_inventory(
                Path(str(pack_dir)), binding_inventory
            )
            build_info["bindingProof"] = binding_proof
            if assets is not None and not binding_proof["ok"]:
                proof["catalogProof"]["ok"] = False
                proof["catalogProof"]["reason"] = (
                    "planned prop bindings did not survive the pack cook"
                )
            if not pairs_match:
                proof["catalogProof"]["ok"] = False
                proof["catalogProof"]["reason"] = (
                    "pack maps.json (id,map) pairs do not match generated profile"
                )
            if args.publish and args.select and pairs_match:
                # Fail-closed: selection.json must exist and activePack must
                # equal pack_id/<bundle-hash> for this published directory.
                selection_path = ROOT / ".private" / "content-packs" / "selection.json"
                if not selection_path.is_file():
                    proof["catalogProof"]["ok"] = False
                    proof["catalogProof"]["reason"] = (
                        "selection.json missing after publish"
                    )
                else:
                    try:
                        selection = json.loads(
                            selection_path.read_text(encoding="utf-8")
                        )
                        active = str(selection.get("activePack") or "").replace(
                            "\\", "/"
                        )
                        published = Path(str(build_info.get("publishedPack") or ""))
                        expected_active = f"{pack_id}/{published.name}".replace(
                            "\\", "/"
                        )
                        # Also accept pack_relative from the build receipt when present.
                        receipt_relative = ""
                        try:
                            receipt_obj = json.loads(
                                built.stdout.strip().splitlines()[-1]
                            )
                            if isinstance(receipt_obj, dict):
                                receipt_relative = str(
                                    receipt_obj.get("pack_relative") or ""
                                ).replace("\\", "/")
                        except Exception:
                            receipt_relative = ""
                        allowed = {expected_active}
                        if receipt_relative:
                            allowed.add(receipt_relative)
                        selected_ok = bool(active) and active in allowed
                        build_info["selectionActivePack"] = active
                        build_info["selectionExpected"] = sorted(allowed)
                        build_info["selectionMatchesPublish"] = selected_ok
                        if not selected_ok:
                            proof["catalogProof"]["ok"] = False
                            proof["catalogProof"]["reason"] = (
                                f"selection.json activePack={active!r} not in "
                                f"{sorted(allowed)!r}"
                            )
                    except Exception as exc:
                        proof["catalogProof"]["ok"] = False
                        proof["catalogProof"]["reason"] = (
                            f"selection.json unreadable after publish: {exc}"
                        )
        else:
            reason = (
                "pack-maps-json-not-found-from-published_pack-receipt"
                if args.publish
                else "pack-maps-json-not-found-from-build-pack-receipt"
            )
            proof["catalogProof"]["packMount"] = {
                "ok": False,
                "reason": reason,
            }
            proof["catalogProof"]["ok"] = False
            proof["catalogProof"]["reason"] = reason
        proof["build"] = build_info

    reports = state_root / "reports"
    reports.mkdir(parents=True, exist_ok=True)
    out = args.output or (reports / f"{args.game}-multimap-skirmish.json")
    out = Path(out).expanduser().resolve()
    try:
        out.relative_to(reports.resolve())
    except ValueError:
        out = reports / out.name
    write_json_atomic(out, proof)
    print(f"REPORT {out}")
    print(
        f"MULTIMAP mode={mode} maps={len(maps)} binder={assets is not None} "
        f"build={bool(args.build)} publish={bool(args.publish)} "
        f"proof_ok={proof['catalogProof']['ok']}",
        flush=True,
    )
    if not proof["catalogProof"]["ok"]:
        return 4
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
