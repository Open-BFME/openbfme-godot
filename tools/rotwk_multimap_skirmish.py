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
    load_map_display_names,
    parse_mapcache_bytes,
    resolve_map_display_name,
)
from openbfme_importer.map_profile import (  # noqa: E402
    castle_siege_map_evidence,
    SKIRMISH_CATEGORY,
    WOTR_BATTLE_CATEGORY,
    classify_map_directory,
)
from openbfme_importer.paths import (  # noqa: E402
    ensure_external_to_repo,
    repo_root_from_module,
)
from openbfme_importer.profile import (  # noqa: E402
    canonical_multiplayer_map_runtime_slug,
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
    # The layered install is the archive source; conversion must consume the
    # one manifest-sealed canonical extraction.  Never revive an old overlay
    # directory whose catalog identity may belong to another patch edition.
    for rel in (
        ("editions", game, "cache", "effective-assets"),
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
            # Never silently rebuild-and-overwrite a live cached catalog over an
            # install-root mismatch: doing exactly that once replaced the layered
            # RotWK catalog with a BFME2-rooted one, with no prompt and no
            # backup. Refuse and name both roots; the operator decides.
            raise SystemExit(
                "catalog-install-mismatch: cached catalog "
                f"{path} was built from install root "
                f"{Path(catalog.install_root).resolve()}, but this run targets "
                f"{install.resolve()}. Refusing to overwrite the cached "
                "catalog. Use the state root that owns this catalog, pass a "
                "dedicated --state-root for the other install, or delete the "
                "cached file deliberately."
            )
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
    """Stable unique slug, aligned with the runtime's canonical map-id grammar.

    Canonical skirmish sources (``maps/map mp <name>/<same>.map``) use the
    canonical multiplayer runtime slug: the runtime installs a map's script
    composite ONLY under ``<game>.map.<that slug>``
    (``retail_vertical_slice.gd:5027-5037``), so a catalog row authored with a
    kept ``mp`` token (``rotwk.map.mp-harlindon``) ships scripts that are
    refused on every launch - the exact defect the goal-official-72 pack
    inherited from this function.  Every other family keeps its retail kind
    tokens (wor/ang/good/evil) for collision-free ids: ``map wor harlindon``
    stays ``wor-harlindon`` beside skirmish ``harlindon``.
    """
    canonical = canonical_multiplayer_map_runtime_slug(
        virtual_path.replace("\\", "/")
    )
    if canonical is not None:
        return canonical
    parts = virtual_path.replace("\\", "/").split("/")
    folder = parts[-2] if len(parts) >= 2 else Path(virtual_path).stem
    words = [w for w in folder.casefold().split() if w]
    if words and words[0] == "map":
        words = words[1:]
    return "-".join(words) if words else Path(virtual_path).stem.casefold()


def _derived_display_name(virtual_path: str) -> str:
    """Last-resort readable name when the install authors none.

    Drops the retail ``map <kind>`` directory bookkeeping, because those tokens
    ("mp", "wor", "wor ang") are not part of any authored name and reading
    "Wor Ang Barrow Downs" in a map list is a defect, not disambiguation.
    """
    parts = virtual_path.replace("\\", "/").split("/")
    folder = parts[-2] if len(parts) >= 2 else Path(virtual_path).stem
    words = [w for w in folder.split() if w]
    if words and words[0].casefold() == "map":
        words = words[1:]
    while words and words[0].casefold() in _DIRECTORY_KIND_TOKENS:
        words = words[1:]
    return " ".join(w.capitalize() for w in words) if words else folder


#: Retail ``maps/map <kind> <name>`` directory tokens. ``mp`` is the skirmish
#: lobby corpus; ``wor``/``wor ang`` are War of the Ring living-world battle
#: maps, which share the multiplayer registry flags but are NOT skirmish
#: offerings and must never be mixed into a skirmish map list.
_DIRECTORY_KIND_TOKENS = {"mp", "wor", "ang", "good", "evil"}


def _registry_category(virtual_path: str) -> str:
    """Classify one official-multiplayer registry row by its retail directory.

    ``isMultiplayer = yes`` covers BOTH the skirmish corpus and the WOTR battle
    maps, so the flag alone cannot define a skirmish list.  The shipped
    directory kind is the authored fact and the only one that separates them.
    """
    if castle_siege_map_evidence(virtual_path) is not None:
        return SKIRMISH_CATEGORY
    parts = virtual_path.replace("\\", "/").split("/")
    folder = parts[-2] if len(parts) >= 2 else Path(virtual_path).stem
    words = [w for w in folder.casefold().split() if w]
    if words and words[0] == "map":
        words = words[1:]
    if words and words[0] == "mp":
        return SKIRMISH_CATEGORY
    if words and words[0] == "wor":
        return WOTR_BATTLE_CATEGORY
    return classify_map_directory(folder)


def _map_art_entries(
    catalog: InstallCatalog, virtual_path: str
) -> tuple[str | None, str | None]:
    """Resolve the optional ``_art.tga`` / ``_pic.tga`` companions of one map."""
    normalized = virtual_path.replace("\\", "/")
    stem = normalized[: -len(".map")] if normalized.casefold().endswith(".map") else normalized
    found: list[str | None] = []
    for suffix in ("_art.tga", "_pic.tga"):
        companion = f"{stem}{suffix}"
        entry = catalog.resolve_exact(companion)
        found.append(entry.name if entry is not None else None)
    return found[0], found[1]


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
    # Authored names come from retail's own string table; the derived fallback
    # is recorded per map so a pack can never quietly ship a made-up name.
    display_names = load_map_display_names(catalog)
    derived_name_slugs: list[str] = []

    for record in selected:
        virtual = str(record["virtualPath"])
        slug = _map_slug(virtual)
        category = _registry_category(virtual)
        display = resolve_map_display_name(
            display_names, str(record.get("displayNameKey") or "")
        )
        if not display:
            display = _derived_display_name(virtual)
            derived_name_slugs.append(slug)
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
                    "category": category,
                    "profile": "multiplayer",
                },
            }
        )
        row: dict[str, Any] = {
            "id": map_id,
            "displayName": display,
            "category": category,
            "map": f"{output_root}/map.json",
            "playerCount": player_count,
            "registryPlayerCount": int(record.get("numPlayers") or 0) or None,
            "routingGraphStatus": "source-waypoint-edges-present-runtime-pending",
            "navigationMeshStatus": "not-generated-or-validated-by-map-profile",
            "terrainMaterialsStatus": "pending-bfme2-base-or-layered-assets",
        }
        castle_evidence = castle_siege_map_evidence(virtual)
        if castle_evidence is not None:
            row["castleSiege"] = dict(castle_evidence["runtimeContract"])
        # Retail ships ``<map>_pic.tga`` (lobby/minimap preview) and
        # ``<map>_art.tga`` (loading plate) beside the binary. This lane used to
        # cook neither, so every published row carried an empty preview/art and
        # the slice refused the match on a missing-capability check. Both stay
        # OPTIONAL: an absent companion is recorded, never substituted.
        art_entry, preview_entry = _map_art_entries(catalog, virtual)
        if art_entry is not None:
            art_output = f"assets/ui/maps/{slug}-art.png"
            row["art"] = art_output
            resources.append(
                {
                    "id": f"map-{slug}-art",
                    "kind": "ui",
                    "converter": "texture",
                    "patterns": [art_entry],
                    "output": art_output,
                    "limit": 1,
                    "expected_count": 1,
                }
            )
        if preview_entry is not None:
            preview_output = f"assets/ui/maps/{slug}-preview.png"
            row["preview"] = preview_output
            resources.append(
                {
                    "id": f"map-{slug}-preview",
                    "kind": "ui",
                    "converter": "texture",
                    "patterns": [preview_entry],
                    "output": preview_output,
                    "limit": 1,
                    "expected_count": 1,
                }
            )
        maps.append(row)

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
                # The entry map is the pack's default match, so it must be a
                # skirmish map even when the catalog also carries WOTR battle
                # maps (both are official-multiplayer registry rows).
                "entryMap": str(
                    next(
                        (
                            row["map"]
                            for row in maps
                            if row["category"] == SKIRMISH_CATEGORY
                        ),
                        maps[0]["map"],
                    )
                ),
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
            "categoryCounts": {
                name: sum(1 for row in maps if row["category"] == name)
                for name in sorted({str(row["category"]) for row in maps})
            },
            "displayNameSource": {
                "authoredTable": "data/lotr.str",
                "authoredKeyField": "mapcache displayName",
                "authoredCount": len(maps) - len(derived_name_slugs),
                "derivedFromDirectoryCount": len(derived_name_slugs),
                "derivedFromDirectorySlugs": sorted(derived_name_slugs),
            },
            "mapArtCounts": {
                "withPreview": sum(1 for row in maps if row.get("preview")),
                "withArt": sum(1 for row in maps if row.get("art")),
            },
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


def repair_pack_catalog(
    catalog: InstallCatalog, pack_dir: Path, *, apply: bool
) -> dict[str, Any]:
    """Re-derive ``category`` and ``displayName`` for an already-cooked pack.

    A pack cooked before this lane classified maps carries every official
    multiplayer row as ``category: skirmish`` - including the WOTR battle maps -
    and a display name title-cased from the retail directory ("Wor Ang Barrow
    Downs").  Both are pure metadata, so they can be corrected in place without
    recooking a single map.

    Maps are matched by the SHA-256 of their retail source bytes, which each
    cooked ``map.json`` already records.  That is an exact binding; slugs are
    not, because this lane's slug rule has changed over time.  A cooked map the
    registry cannot account for is REPORTED and left untouched, never guessed.

    Map art is re-bound only when the converted PNG is ALREADY in the pack: a
    pack can carry cooked ``assets/ui/maps/<slug>-art.png`` that its catalog
    never referenced, and pointing a row at a file that demonstrably exists is
    bookkeeping, not a cook.  A row with no such file on disk keeps its empty
    art and is reported; converting ``_art.tga`` / ``_pic.tga`` needs the normal
    build path.
    """

    catalog_path = pack_dir / "data" / "maps.json"
    if not catalog_path.is_file():
        raise SystemExit(f"pack has no map catalog to repair: {catalog_path}")
    document = json.loads(catalog_path.read_text(encoding="utf-8"))

    registry = parse_mapcache_bytes(
        _read_virtual(catalog, MAPCACHE_VIRTUAL_PATH, max_bytes=MAX_MAPCACHE_BYTES)
    )
    display_names = load_map_display_names(catalog)
    by_source_sha: dict[str, dict[str, str]] = {}
    for record in registry:
        virtual = str(record["virtualPath"])
        if catalog.resolve_exact(virtual) is None:
            continue
        try:
            payload = _read_virtual(catalog, virtual, max_bytes=MAX_SOURCE_BYTES)
        except (OSError, ValueError):
            continue
        name = resolve_map_display_name(
            display_names, str(record.get("displayNameKey") or "")
        )
        by_source_sha[hashlib.sha256(payload).hexdigest()] = {
            "displayName": name or _derived_display_name(virtual),
            "category": _registry_category(virtual),
            "displayNameSource": "authored" if name else "derived-from-directory",
        }

    def _slug_of(row: Any) -> str:
        relative = str(row.get("map") or "").replace("\\", "/")
        return relative.split("/")[1] if "/" in relative else ""

    def _stripped(slug: str) -> str:
        head, _, tail = slug.partition("-")
        return tail if tail and head in _DIRECTORY_KIND_TOKENS else ""

    # ``map mp harlindon`` and ``map wor harlindon`` are DIFFERENT maps that both
    # strip to "harlindon". Binding either to a bare ``harlindon-art.png`` would
    # give one of them the other's art, so the kind-stripped fallback is only
    # allowed when exactly one row claims that name.
    stripped_claims: dict[str, int] = {}
    for existing in document.get("maps") or []:
        name = _stripped(_slug_of(existing))
        if name:
            stripped_claims[name] = stripped_claims.get(name, 0) + 1

    changes: list[dict[str, Any]] = []
    unmatched: list[str] = []
    without_art: list[str] = []
    ambiguous_art: list[str] = []
    for row in document.get("maps") or []:
        map_relative = str(row.get("map") or "")
        map_path = pack_dir / map_relative
        if not map_path.is_file():
            unmatched.append(f"{row.get('id')}:cooked-map-document-missing")
            continue
        cooked = json.loads(map_path.read_text(encoding="utf-8"))
        source_sha = str((cooked.get("source") or {}).get("sha256") or "")
        truth = by_source_sha.get(source_sha)
        if truth is None:
            unmatched.append(f"{row.get('id')}:no-registry-row-for-source-sha")
            continue
        after = {
            "displayName": truth["displayName"],
            "category": truth["category"],
        }
        # Re-bind art the pack already carries. The cooked slug is normally the
        # map directory, but a pack whose map ids were re-slugged after the art
        # cook stores it under the kind-stripped name; both are checked and a
        # path is only written when the file is really there.
        slug = _slug_of(row)
        candidates = [slug]
        stripped = _stripped(slug)
        if stripped:
            if stripped_claims.get(stripped, 0) == 1:
                candidates.append(stripped)
            elif (pack_dir / f"assets/ui/maps/{stripped}-art.png").is_file():
                ambiguous_art.append(f"{row.get('id')}:{stripped}")
        found_art = False
        for kind, field in (("art", "art"), ("preview", "preview")):
            if str(row.get(field) or ""):
                continue
            for candidate in candidates:
                relative = f"assets/ui/maps/{candidate}-{kind}.png"
                if (pack_dir / relative).is_file():
                    after[field] = relative
                    found_art = True
                    break
        if not found_art and not str(row.get("art") or ""):
            without_art.append(str(row.get("id") or ""))
        before = {key: row.get(key) for key in after}
        if before == after:
            continue
        changes.append({"id": row.get("id"), "before": before, "after": after})
        if not apply:
            continue
        row.update(after)
        cooked.update(after)
        write_json_atomic(map_path, cooked)

    if apply and changes:
        write_json_atomic(catalog_path, document)

    categories: dict[str, int] = {}
    for row in document.get("maps") or []:
        key = str(row.get("category") or "unknown")
        categories[key] = categories.get(key, 0) + 1
    return {
        "schema": "openbfme.rotwk-multimap-catalog-repair",
        "schemaVersion": 0,
        "pack": str(pack_dir),
        "applied": bool(apply),
        "mapCount": len(document.get("maps") or []),
        "changedCount": len(changes),
        "unmatched": unmatched,
        "categoryCounts": categories,
        "mapsStillWithoutArt": sorted(without_art),
        # Cooked art whose slug two maps could both claim; left unbound on
        # purpose rather than handed to whichever row was seen first.
        "ambiguousArtLeftUnbound": sorted(ambiguous_art),
        "artNote": (
            "art already cooked into the pack is re-bound; a row still without "
            "art needs a texture cook via the normal build path"
        ),
        "changes": changes,
    }


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
    # Required for every generating path. --repair-catalog reads the already
    # extracted layered tree and the pack on disk, so it needs no operator
    # install and does not ask for one.
    parser.add_argument("--install", type=Path, default=None)
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
    parser.add_argument(
        "--repair-catalog",
        type=Path,
        default=None,
        metavar="PACK_DIR",
        help=(
            "re-derive category/displayName for an already-cooked pack from the "
            "retail registry and string table; reports without writing unless "
            "--apply is given. Never touches map art (that needs a cook)."
        ),
    )
    parser.add_argument(
        "--apply",
        action="store_true",
        help="with --repair-catalog, write the corrected metadata",
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

    if args.install is None:
        if args.repair_catalog is None:
            print("FAIL: --install is required", file=sys.stderr)
            return 2
        operator_install = None
    else:
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
    # The catalog repair reads the same registry, string table and map bytes the
    # cook did, so it needs the same layered tree.
    if args.game == "rotwk" and (args.full_profile or args.repair_catalog is not None):
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
                print(f"FAIL layered install required: {exc}", file=sys.stderr)
                return 2
        content_install = layered
        layered_used = True
        print(f"LAYERED_INSTALL {content_install}", flush=True)

    if args.repair_catalog is not None:
        pack_dir = args.repair_catalog.expanduser().resolve()
        if not (pack_dir / "pack.json").is_file():
            print(f"FAIL: no pack.json at {pack_dir}", file=sys.stderr)
            return 2
        report = repair_pack_catalog(
            _load_catalog(state_root, args.game, content_install),
            pack_dir,
            apply=bool(args.apply),
        )
        if args.output is not None:
            write_json_atomic(args.output.expanduser().resolve(), report)
        print(json.dumps(report, indent=2, sort_keys=True))
        print(
            "CATALOG_REPAIR "
            f"applied={report['applied']} maps={report['mapCount']} "
            f"changed={report['changedCount']} unmatched={len(report['unmatched'])} "
            f"categories={report['categoryCounts']}",
            flush=True,
        )
        return 0 if not report["unmatched"] else 5

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
            # Official RotWK multiplayer corpus is 72 maps: skirmish (mp) +
            # wotr-battle (wor). map-set "skirmish" alone only admits the ~22 mp
            # targets and under-cooks terrain materials for the full goal pack.
            "--map-set",
            "playable",
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
