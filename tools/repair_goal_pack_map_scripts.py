"""Close the missing map-script composites in an existing private content pack.

This is intentionally a pack-repair tool, not another map cooker (the same
stance as ``tools/close_goal_prop_bindings.py``).  The goal-official-72 pack
was hand-assembled from sage-map outputs and never received the
``sage-script-composite`` outputs its own generated profile declares, so its
qualifying multiplayer maps install NO scripts at runtime (the runtime's
documented inert default).  This tool decodes each declared retail map source
plus the two qualified AI libraries through the importer's own functions
(``sage_scripts.map_scripts_document`` / ``compose_map_scripts_document``),
binds provenance exactly as ``pipeline._convert_script_composite_bundle``
does, and writes ``scripts.json`` beside each map's cooked ``map.json``.
It also backfills the ``map.json`` source provenance keys
(``virtualPath``/``sourceBytes``) the pack assembler dropped — without them
the runtime's composite install check refuses every scripts.json
(``retail_vertical_slice.gd:5027-5047``).

Fail-closed rules:

- The qualifying map list is DERIVED from the generated profile's declared
  ``sage-script-composite`` resources, never hand-written.
- Maps are matched to pack directories by retail source SHA-256 (the exact
  binding ``repair_pack_catalog`` uses); slugs are not trusted.
- Every declared composite must bind to exactly one skirmish catalog row and
  every skirmish row must be claimed by exactly one composite.
- A missing/undecodable source, an unmatched map, or an existing differing
  ``scripts.json`` (without ``--force``) refuses the WHOLE run: everything is
  composed and validated first, and nothing is written unless every map
  passed.  No silent partial success.
- Catalog rows whose id kept an older slug scheme (``rotwk.map.mp-harlindon``)
  are MIGRATED to the canonical runtime id (``rotwk.map.harlindon``): the
  runtime refuses to install a composite unless the mounted row id equals
  ``<game>.map.<canonical slug>`` (``retail_vertical_slice.gd:5027-5037``), so
  without the migration the composite ships but stays inert forever.  The
  migration rewrites BOTH the ``data/maps.json`` row id and the cooked
  ``map.json`` id (ContentDB requires them equal, ``content_db.gd:421``), and
  refuses when another row already claims the canonical id.
"""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import sys
from typing import Any

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "importer"))

from openbfme_importer.catalog import InstallCatalog  # noqa: E402
from openbfme_importer.profile import (  # noqa: E402
    canonical_multiplayer_map_runtime_slug,
)
from openbfme_importer.sage_scripts import (  # noqa: E402
    compose_map_scripts_document,
    map_scripts_document,
)
from openbfme_importer.util import write_json_atomic  # noqa: E402


AI_LIBRARY_VIRTUAL_PATHS = [
    "libraries/ai_initialize/ai_initialize.map",
    "libraries/ai_mp_inherit_management/ai_mp_inherit_management.map",
]
COMPOSITE_CONVERTER = "sage-script-composite"
MAX_SOURCE_BYTES = 256 * 1024 * 1024


class RepairRefusal(RuntimeError):
    """A fail-closed refusal with its reason named."""


MAP_PACK_ID = "rotwk-skirmish-maps-private"


def resolve_selected_pack(content_root: Path, pack_id: str = MAP_PACK_ID) -> Path:
    """Resolve the SELECTED map pack from the workspace selection.json.

    The runtime is workspace-first: `workspace/content-packs/selection.json`
    names the mounted bundles as `<pack-id>/<bundle-dir>`.  The name-addressed
    `goal-official-72` sibling directory is NOT the mounted pack (selection
    points at a hash-addressed copy), so the default target must be derived
    from selection, never hand-written.  Fail-closed: refuse a missing
    selection, zero matches, or more than one match.
    """

    selection_path = content_root / "selection.json"
    if not selection_path.is_file():
        raise RepairRefusal(f"workspace selection is missing: {selection_path}")
    selection = json.loads(selection_path.read_text(encoding="utf-8"))
    entries = [str(selection.get("activePack") or "")] + [
        str(item) for item in selection.get("supplementalPacks") or []
    ]
    matches = [
        entry
        for entry in entries
        if entry.replace("\\", "/").startswith(f"{pack_id}/")
    ]
    if len(matches) != 1:
        raise RepairRefusal(
            f"selection names {len(matches)} {pack_id!r} packs, expected "
            f"exactly 1: {selection_path}"
        )
    relative = PurePosixPath(matches[0].replace("\\", "/"))
    if any(part in {"", ".", ".."} for part in relative.parts):
        raise RepairRefusal(f"unsafe selection entry: {matches[0]!r}")
    pack = content_root.joinpath(*relative.parts)
    if not (pack / "pack.json").is_file():
        raise RepairRefusal(
            f"selected pack has no pack.json: {pack} (from {selection_path})"
        )
    return pack


def _read_virtual(
    catalog: InstallCatalog, virtual_path: str, *, max_bytes: int = MAX_SOURCE_BYTES
) -> bytes:
    entry = catalog.resolve_exact(virtual_path)
    if entry is None:
        raise RepairRefusal(f"missing catalog entry: {virtual_path}")
    archive = catalog.open_archive_for(entry)
    return archive.read_entry(catalog.as_entry(entry), max_bytes=max_bytes)


def decode_library_documents(
    catalog: InstallCatalog,
) -> list[tuple[str, dict[str, Any]]]:
    """Decode the two qualified AI libraries ONCE, in contract order."""

    documents: list[tuple[str, dict[str, Any]]] = []
    for virtual_path in AI_LIBRARY_VIRTUAL_PATHS:
        try:
            source = _read_virtual(catalog, virtual_path)
            documents.append(
                (virtual_path, map_scripts_document(source, container="map"))
            )
        except RepairRefusal:
            raise
        except Exception as exc:
            raise RepairRefusal(
                f"undecodable AI library {virtual_path}: "
                f"{type(exc).__name__}: {exc}"
            ) from exc
    return documents


def compose_pack_scripts_document(
    map_bytes: bytes,
    library_documents: list[dict[str, Any]],
    *,
    map_virtual_path: str,
    library_virtual_paths: list[str] | None = None,
    game: str = "rotwk",
) -> tuple[dict[str, Any], str]:
    """Compose one map's schema-v2 script document, provenance bound.

    Mirrors ``pipeline._convert_script_composite_bundle``: same slug gate,
    same ordered two-library closure, same provenance binding (game id plus
    casefolded map virtual path), so the emitted document is byte-comparable
    with the pipeline-cooked oracle pack.
    """

    if library_virtual_paths is None:
        library_virtual_paths = AI_LIBRARY_VIRTUAL_PATHS
    slug = canonical_multiplayer_map_runtime_slug(map_virtual_path)
    if slug is None:
        raise RepairRefusal(
            "mapVirtualPath must name one canonical multiplayer map source: "
            f"{map_virtual_path!r}"
        )
    if library_virtual_paths != AI_LIBRARY_VIRTUAL_PATHS:
        raise RepairRefusal(
            "libraryVirtualPaths must be the ordered ai_initialize and "
            "ai_mp_inherit_management closure"
        )
    if len(library_documents) != len(library_virtual_paths):
        raise RepairRefusal(
            "library document count does not match the qualified closure"
        )
    try:
        map_document = map_scripts_document(map_bytes, container="map")
        document = compose_map_scripts_document(map_document, library_documents)
    except RepairRefusal:
        raise
    except Exception as exc:
        raise RepairRefusal(
            f"undecodable map source {map_virtual_path}: "
            f"{type(exc).__name__}: {exc}"
        ) from exc
    source_provenance = document["source"]
    source_provenance["game"] = game
    source_provenance["map"]["virtualPath"] = str(map_virtual_path).casefold()
    for index, virtual_path in enumerate(library_virtual_paths):
        source_provenance["libraries"][index]["virtualPath"] = virtual_path
    return document, slug


def plan_map_source_backfill(
    cooked: dict[str, Any],
    *,
    virtual_path: str,
    source_bytes: int,
    map_path: Path,
) -> dict[str, Any] | None:
    """Plan the map.json source-provenance backfill the runtime requires.

    The runtime refuses to install a script composite unless the mounted
    ``map.json`` names the SAME retail source identity
    (``retail_map_data.gd:239-242`` reads ``source.virtualPath`` /
    ``source.sourceBytes``; ``retail_vertical_slice.gd:5027-5047`` compares
    them against the composite's map row).  The hand-assembled goal pack
    dropped both keys (only ``sha256`` survived), so every composite would be
    refused at install time.  Returns the updated cooked document, or ``None``
    when nothing is missing.  Conflicting existing values are a refusal, never
    an overwrite: the sha256 binding proved which retail source this is, so a
    disagreeing virtualPath/sourceBytes means the pack is lying to someone.
    """

    source = cooked.get("source")
    if not isinstance(source, dict):
        raise RepairRefusal(f"cooked map document has no source object: {map_path}")
    canonical = str(virtual_path).casefold()
    existing_path = source.get("virtualPath")
    existing_bytes = source.get("sourceBytes")
    if existing_path is not None and str(existing_path) != canonical:
        raise RepairRefusal(
            f"cooked map source virtualPath {existing_path!r} conflicts with "
            f"the sha-matched retail source {canonical!r}: {map_path}"
        )
    if existing_bytes is not None and int(existing_bytes) != source_bytes:
        raise RepairRefusal(
            f"cooked map sourceBytes {existing_bytes!r} conflicts with the "
            f"sha-matched retail source ({source_bytes} bytes): {map_path}"
        )
    if existing_path is not None and existing_bytes is not None:
        return None
    updated = json.loads(json.dumps(cooked))
    updated["source"]["virtualPath"] = canonical
    updated["source"]["sourceBytes"] = source_bytes
    return updated


def plan_map_id_migration(
    row_id: str,
    slug: str,
    catalog_row_ids: list[str],
    *,
    game: str = "rotwk",
) -> str | None:
    """Plan one catalog row's migration to the canonical runtime map id.

    The runtime derives the ONLY id it will install scripts under from the
    map's retail source virtualPath (``maps/map mp <name>/...`` ->
    ``<game>.map.<name-slug>``; ``retail_vertical_slice.gd:4974-4996`` and the
    equality check at ``:5027-5037``).  ``slug`` here is the sha-bound
    canonical runtime slug the caller already derived through
    ``canonical_multiplayer_map_runtime_slug`` - never the row's own slug.

    Returns the canonical id when the row must move, ``None`` when it already
    matches.  Fail-closed: refuses when any OTHER row in the same catalog
    already claims the canonical id, because applying the migration would
    produce a duplicate id that ContentDB rejects the whole catalog for
    (``content_db.gd:412``).
    """

    canonical = f"{game}.map.{slug}"
    if row_id == canonical:
        return None
    others = [existing for existing in catalog_row_ids if existing != row_id]
    if canonical in others:
        raise RepairRefusal(
            f"cannot migrate catalog row id {row_id!r} to {canonical!r}: "
            "other rows already claim the canonical id (ambiguous collision)"
        )
    return canonical


def write_composed_document(
    target: Path, document: dict[str, Any], *, force: bool
) -> str:
    """Write one composed scripts.json; refuse a differing existing file.

    Returns ``"repaired"`` or ``"skipped-identical"`` (an existing file whose
    parsed content already equals the composed document; nothing to change).
    """

    if target.is_file():
        try:
            existing = json.loads(target.read_text(encoding="utf-8"))
        except (OSError, ValueError, json.JSONDecodeError):
            existing = None
        if existing == document:
            return "skipped-identical"
        if not force:
            raise RepairRefusal(
                f"scripts.json already exists and differs: {target} "
                "(pass --force to overwrite)"
            )
    target.parent.mkdir(parents=True, exist_ok=True)
    write_json_atomic(target, document)
    return "repaired"


def load_declared_composites(profile_path: Path) -> list[dict[str, Any]]:
    """Read the generated profile's sage-script-composite declarations.

    Each declaration is validated against the composite contract before it is
    trusted: exact option keys, the exact ordered library closure, and an
    output that matches the map's canonical runtime slug.
    """

    profile = json.loads(profile_path.read_text(encoding="utf-8"))
    declared: list[dict[str, Any]] = []
    for resource in profile.get("resources") or []:
        if resource.get("converter") != COMPOSITE_CONVERTER:
            continue
        resource_id = str(resource.get("id") or "")
        options = resource.get("options") or {}
        if set(options) != {"mapVirtualPath", "libraryVirtualPaths"}:
            raise RepairRefusal(
                f"declared composite {resource_id!r} has invalid options"
            )
        map_virtual_path = str(options["mapVirtualPath"])
        if list(options["libraryVirtualPaths"]) != AI_LIBRARY_VIRTUAL_PATHS:
            raise RepairRefusal(
                f"declared composite {resource_id!r} does not use the "
                "qualified two-library closure"
            )
        slug = canonical_multiplayer_map_runtime_slug(map_virtual_path)
        if slug is None:
            raise RepairRefusal(
                f"declared composite {resource_id!r} names a non-multiplayer "
                f"map source: {map_virtual_path!r}"
            )
        if str(resource.get("output") or "") != f"maps/{slug}/scripts.json":
            raise RepairRefusal(
                f"declared composite {resource_id!r} output does not match "
                f"its runtime slug {slug!r}"
            )
        declared.append(
            {"id": resource_id, "mapVirtualPath": map_virtual_path, "slug": slug}
        )
    if not declared:
        raise RepairRefusal(
            f"profile declares no {COMPOSITE_CONVERTER} resources: "
            f"{profile_path}"
        )
    return declared


def load_skirmish_rows(pack: Path) -> tuple[dict[str, Any], list[dict[str, Any]]]:
    """Bind each skirmish catalog row to its cooked map dir and source sha.

    Returns the parsed ``data/maps.json`` document (whose row objects the
    returned bindings reference in place, so an id migration can rewrite the
    document through them) plus one binding per skirmish row.
    """

    catalog_path = pack / "data" / "maps.json"
    if not catalog_path.is_file():
        raise RepairRefusal(f"pack has no map catalog: {catalog_path}")
    document = json.loads(catalog_path.read_text(encoding="utf-8"))
    rows: list[dict[str, Any]] = []
    for row in document.get("maps") or []:
        if str(row.get("category") or "") != "skirmish":
            continue
        relative = str(row.get("map") or "").replace("\\", "/")
        pure = PurePosixPath(relative)
        if (
            pure.is_absolute()
            or not pure.parts
            or any(part in {"", ".", ".."} for part in pure.parts)
        ):
            raise RepairRefusal(
                f"catalog row {row.get('id')!r} has an unsafe map path: "
                f"{relative!r}"
            )
        map_path = pack.joinpath(*pure.parts)
        if not map_path.is_file():
            raise RepairRefusal(
                f"catalog row {row.get('id')!r} names a missing cooked map "
                f"document: {map_path}"
            )
        cooked = json.loads(map_path.read_text(encoding="utf-8"))
        source_sha = str((cooked.get("source") or {}).get("sha256") or "")
        if len(source_sha) != 64:
            raise RepairRefusal(
                f"cooked map document has no source sha256: {map_path}"
            )
        rows.append(
            {
                "id": str(row.get("id") or ""),
                "catalogRow": row,
                "mapDir": map_path.parent,
                "mapPath": map_path,
                "cooked": cooked,
                "sourceSha256": source_sha,
            }
        )
    if not rows:
        raise RepairRefusal(f"pack catalog has no skirmish rows: {catalog_path}")
    return document, rows


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--pack",
        type=Path,
        default=None,
        help="pack directory to repair; default: the pack the workspace "
        "selection.json actually mounts (fail-closed if unresolvable)",
    )
    parser.add_argument(
        "--profile",
        type=Path,
        default=ROOT
        / "workspace"
        / "retail-work"
        / "profiles"
        / "rotwk-playable-maps.generated.json",
    )
    parser.add_argument(
        "--state-root",
        type=Path,
        default=ROOT / "workspace" / "retail-work",
    )
    parser.add_argument("--game", default="rotwk")
    parser.add_argument("--force", action="store_true")
    parser.add_argument(
        "--dry-run",
        action="store_true",
        help="compose and validate everything; write nothing",
    )
    args = parser.parse_args(argv)

    if args.pack is None:
        try:
            pack = resolve_selected_pack(ROOT / "workspace" / "content-packs")
        except RepairRefusal as exc:
            raise SystemExit(f"cannot resolve the selected map pack: {exc}")
    else:
        pack = args.pack.expanduser().resolve()
    profile_path = args.profile.expanduser().resolve()
    state_root = args.state_root.expanduser().resolve()
    if not (pack / "pack.json").is_file():
        raise SystemExit(f"pack.json is missing: {pack}")
    if not profile_path.is_file():
        raise SystemExit(f"generated profile is missing: {profile_path}")
    catalog_path = state_root / "catalog" / f"{args.game}.json"
    if not catalog_path.is_file():
        raise SystemExit(f"install catalog is missing: {catalog_path}")
    catalog = InstallCatalog.load(catalog_path)
    stale = catalog.stale_reasons()
    if stale:
        raise SystemExit(f"install catalog is stale: {stale}")

    declared = load_declared_composites(profile_path)
    pack_catalog_path = pack / "data" / "maps.json"
    pack_catalog_document, rows = load_skirmish_rows(pack)
    all_row_ids = [
        str(row.get("id") or "") for row in pack_catalog_document.get("maps") or []
    ]

    libraries = decode_library_documents(catalog)
    library_documents = [document for _path, document in libraries]

    # Phase 1: bind, decode and compose EVERY declared map before any write.
    planned: list[dict[str, Any]] = []
    refusals: list[str] = []
    claimed_row_ids: dict[str, str] = {}
    for declaration in declared:
        slug = declaration["slug"]
        virtual_path = declaration["mapVirtualPath"]
        try:
            source = _read_virtual(catalog, virtual_path)
            source_sha = hashlib.sha256(source).hexdigest()
            matches = [row for row in rows if row["sourceSha256"] == source_sha]
            if len(matches) != 1:
                raise RepairRefusal(
                    f"retail source {virtual_path!r} matches "
                    f"{len(matches)} skirmish catalog rows, expected exactly 1"
                )
            row = matches[0]
            prior = claimed_row_ids.get(row["id"])
            if prior is not None:
                raise RepairRefusal(
                    f"catalog row {row['id']!r} is claimed by two declared "
                    f"composites ({prior!r} and {declaration['id']!r})"
                )
            claimed_row_ids[row["id"]] = declaration["id"]
            document, composed_slug = compose_pack_scripts_document(
                source,
                library_documents,
                map_virtual_path=virtual_path,
                game=args.game,
            )
            if composed_slug != slug:
                raise RepairRefusal(
                    f"composed slug {composed_slug!r} disagrees with the "
                    f"declared output slug {slug!r}"
                )
            backfill = plan_map_source_backfill(
                row["cooked"],
                virtual_path=virtual_path,
                source_bytes=len(source),
                map_path=row["mapPath"],
            )
            # The runtime additionally requires the catalog row id to equal
            # the canonical slug id; a stale id (e.g. rotwk.map.mp-harlindon
            # vs slug harlindon) is MIGRATED in place, fail-closed on
            # ambiguous collisions.  Without it the composite ships inert.
            migrated_id = plan_map_id_migration(
                row["id"], slug, all_row_ids, game=args.game
            )
            target = row["mapDir"] / "scripts.json"
            status = "write"
            if target.is_file() and not args.force:
                try:
                    existing = json.loads(target.read_text(encoding="utf-8"))
                except (OSError, ValueError, json.JSONDecodeError):
                    existing = None
                if existing == document:
                    status = "skipped-identical"
                else:
                    raise RepairRefusal(
                        f"scripts.json already exists and differs: {target} "
                        "(pass --force to overwrite)"
                    )
            planned.append(
                {
                    "slug": slug,
                    "rowId": row["id"],
                    "target": target,
                    "document": document,
                    "status": status,
                    "mapPath": row["mapPath"],
                    "cooked": row["cooked"],
                    "catalogRow": row["catalogRow"],
                    "backfill": backfill,
                    "migratedId": migrated_id,
                }
            )
            notes = []
            if backfill is not None:
                notes.append("backfill-map-source")
            if migrated_id is not None:
                notes.append(f"MIGRATE-ID({row['id']} -> {migrated_id})")
            print(
                f"PLAN {slug} {status} row={row['id']} "
                f"scripts={len(document['scripts'])} "
                f"libraries={len(document['libraryTemplates'])} "
                f"target={target.relative_to(pack).as_posix()}"
                + ("" if not notes else " " + " ".join(notes)),
                flush=True,
            )
        except RepairRefusal as exc:
            refusals.append(f"{slug}: {exc}")
            print(f"PLAN {slug} REFUSED {exc}", flush=True)

    unclaimed = sorted(
        row["id"] for row in rows if row["id"] not in claimed_row_ids
    )
    if unclaimed and not refusals:
        refusals.append(
            "skirmish catalog rows not claimed by any declared composite: "
            + ", ".join(unclaimed)
        )
        print(f"PLAN REFUSED unclaimed rows: {', '.join(unclaimed)}", flush=True)

    repaired = 0
    skipped = 0
    backfilled = 0
    migrated = 0
    planned_migrations = sorted(
        f"{plan['rowId']} -> {plan['migratedId']}"
        for plan in planned
        if plan["migratedId"] is not None
    )
    if refusals:
        print(
            "REFUSING the whole repair; nothing was written "
            f"({len(refusals)} refusal(s)):",
            flush=True,
        )
        for reason in refusals:
            print(f"  REFUSED {reason}", flush=True)
    elif args.dry_run:
        skipped = sum(
            1 for plan in planned if plan["status"] == "skipped-identical"
        )
        print("DRY RUN - nothing written.", flush=True)
    else:
        # Phase 2: everything validated; now write.
        for plan in planned:
            status = write_composed_document(
                plan["target"], plan["document"], force=args.force
            )
            cooked_update = plan["backfill"]
            if plan["backfill"] is not None:
                backfilled += 1
                status += "+map-source-backfill"
            if plan["migratedId"] is not None:
                # Row id and cooked map id move TOGETHER: ContentDB refuses
                # the whole catalog when they disagree (content_db.gd:421).
                if cooked_update is None:
                    cooked_update = json.loads(json.dumps(plan["cooked"]))
                cooked_update["id"] = plan["migratedId"]
                plan["catalogRow"]["id"] = plan["migratedId"]
                migrated += 1
                status += "+id-migrated"
            if cooked_update is not None:
                write_json_atomic(plan["mapPath"], cooked_update)
            if status.startswith("repaired"):
                repaired += 1
            else:
                skipped += 1
            print(f"REPAIR {plan['slug']} {status}", flush=True)
        if migrated:
            write_json_atomic(pack_catalog_path, pack_catalog_document)

    if planned_migrations:
        print(
            "ID-MIGRATIONS "
            + ("planned (dry run): " if args.dry_run or refusals else "applied: ")
            + ", ".join(planned_migrations),
            flush=True,
        )
    print(
        "REPAIR_GOAL_MAP_SCRIPTS_RESULT "
        f"declared={len(declared)} skirmishRows={len(rows)} "
        f"repaired={repaired} skipped={skipped} "
        f"backfilledMapSources={backfilled} "
        f"idMigrations={migrated} "
        f"refused={len(refusals)} "
        f"dryRun={bool(args.dry_run or refusals)}",
        flush=True,
    )
    return 0 if not refusals else 5


if __name__ == "__main__":
    raise SystemExit(main())
