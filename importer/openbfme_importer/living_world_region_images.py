"""Convert the PORTRAITS RETAIL DRAWS FOR A REGION - the fortress plate on the
region card and the region's own strategic portrait - into a bundle the Godot
strategic screen can crop.

WHY THIS MODULE EXISTS
----------------------
The region card names a region, its treasure, its plots and its territory in
retail's own words, and shows NO PICTURE. Retail's card carries one, and the
living-world data says which:

    Region Amon_Sul
        DisplayName    = LW:DisplayNameAmonSul
        RegionPortrait = LWPAmonSul
        Fortress
            Portrait   = BPCAmonSul
        End
    End

Both are ``MappedImage`` ids - an atlas name plus a pixel rectangle - and they
are the SAME kind of thing ``living_world_ui`` already resolves for portraits and
structure buttons. So this module does not re-implement the resolver: it imports
``living_world_ui.resolve_images`` and the ``mapped_image`` atlas-path rule and
asks them for a DIFFERENT id set - the one the REGIONS name, which the UI bundle
never requests because no building or army names it.

It is a separate module and a separate bundle for the reason the UI bundle is
separate from the map: they fail independently. Retail's portraits can be
converted with no region card art and the screen has to be able to say which.

WHAT IT REFUSES TO DO
---------------------
* It NEVER substitutes a portrait. An id with no ``MappedImage`` block, an id
  defined twice, or an id whose atlas is in no archive is recorded in ``gaps``
  by NAME, carries no crop, and the screen draws an empty plate saying which one
  is absent.
* It NEVER derives a portrait from a region id. The only links it follows are
  the document's own ``fortress.portrait`` and ``regionPortrait`` fields.
* It reads the region list out of the LIVING-WORLD DOCUMENT, which is retail's
  own ``livingworldregions.inc`` already converted, rather than re-parsing it.
* It reads image data through the CATALOGS, never through the effective-assets
  cache.

BUNDLE LAYOUT
-------------
``living-world-region-images.json``  the manifest: regions, images, atlases
``region-atlases/*``                 verbatim retail atlas bytes, unaltered
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
from typing import Any, Mapping

from .livingmap_bundle import CatalogReader
from .living_world_ui import (
    LivingWorldUiError,
    _atlas_file_name,
    resolve_images,
)
from .mapped_image import resolve_mapped_image_texture_paths_partial

SCHEMA = "openbfme.living-world-region-images"
SCHEMA_VERSION = 1

MANIFEST_NAME = "living-world-region-images.json"
ATLAS_DIRECTORY = "region-atlases"

MAX_ATLASES = 512
MAX_ATLAS_BYTES = 16 * 1024 * 1024


class RegionImageError(RuntimeError):
    """The bundle cannot be produced, with the exact reason."""


def _region_rows(document: Mapping[str, Any]) -> list[dict[str, Any]]:
    """Every region the document declares, with the two portrait ids it names.

    A region is keyed by its own id. Retail lists some regions in more than one
    campaign; the first campaign that declares one wins and the rest are checked
    for AGREEMENT rather than silently overwritten - two campaigns naming
    different portraits for one region would be a real conflict and is recorded
    as one.
    """

    rows: dict[str, dict[str, Any]] = {}
    conflicts: list[str] = []
    for campaign in document.get("regionCampaigns", []) or []:
        for region in campaign.get("regions", []) or []:
            region_id = str(region.get("id", ""))
            if not region_id:
                continue
            fortress = region.get("fortress") or {}
            row = {
                "region": region_id,
                "fortressPortrait": str(fortress.get("portrait", "") or ""),
                "regionPortrait": str(region.get("regionPortrait", "") or ""),
                "fortressDisplayName": str(fortress.get("displayName", "") or ""),
            }
            existing = rows.get(region_id)
            if existing is None:
                rows[region_id] = row
            elif existing != row:
                conflicts.append(region_id)
    for row in rows.values():
        row["conflicting"] = row["region"] in conflicts
    return [rows[key] for key in sorted(rows)]


def build_bundle(
    catalog_path: pathlib.Path | str,
    output_root: pathlib.Path | str,
    document_path: pathlib.Path | str,
) -> dict[str, Any]:
    """Write the region-image bundle and return its manifest."""

    reader = CatalogReader(catalog_path)
    output = pathlib.Path(output_root)
    output.mkdir(parents=True, exist_ok=True)
    document = json.loads(pathlib.Path(document_path).read_text(encoding="utf-8"))

    regions = _region_rows(document)
    if not regions:
        raise RegionImageError(
            f"{document_path} declares no regions, so there is no portrait to resolve"
        )

    wanted: set[str] = set()
    for row in regions:
        for key in ("fortressPortrait", "regionPortrait"):
            value = str(row[key])
            if value:
                wanted.add(value)

    by_id, missing_ids, ambiguous_ids = resolve_images(reader, wanted)

    # THE ID AS THE MAPPEDIMAGE BLOCK SPELLS IT, recorded beside the id as the
    # REGION spells it. Retail disagrees with itself about capitals in four
    # places - the region rows say `LWPBarrowDOwns`, `LWPRedhornPass`,
    # `LWPBlackGate` and `LWPBrownLands` while the image blocks say
    # `LWPBarrowDowns`, `LWPRedHornPass`, `LWPBlacKGate` and `LWPBrownlands` -
    # and INI ids are case-insensitive, so this is a FOLD of retail's own two
    # spellings of one id, not a resemblance between two different ids. It is
    # resolved HERE, once, so the Godot side never has to fold anything and can
    # never fold its way onto a different picture.
    for row in regions:
        for key in ("fortressPortrait", "regionPortrait"):
            requested = str(row[key])
            record = by_id.get(requested.casefold()) if requested else None
            row[f"{key}Resolved"] = "" if record is None else record.id

    catalog_names = sorted(
        entry.name for entry in reader._winners.values()  # noqa: SLF001
    )
    texture_paths, unresolved_textures = resolve_mapped_image_texture_paths_partial(
        by_id.values(), catalog_names
    )
    if len(texture_paths) > MAX_ATLASES:
        raise RegionImageError(
            f"{len(texture_paths)} atlases requested, over the {MAX_ATLASES} limit"
        )

    atlas_directory = output / ATLAS_DIRECTORY
    atlas_directory.mkdir(parents=True, exist_ok=True)
    atlases: list[dict[str, Any]] = []
    atlas_file_by_texture: dict[str, str] = {}
    for texture_key in sorted(texture_paths):
        virtual_path = texture_paths[texture_key]
        entry = reader.resolve(virtual_path)
        if entry is None:  # pragma: no cover - the resolver checked the catalog
            unresolved_textures = (*unresolved_textures, texture_key)
            continue
        payload = reader.read(entry)
        if len(payload) > MAX_ATLAS_BYTES:
            raise RegionImageError(
                f"{virtual_path} is {len(payload)} bytes, over the "
                f"{MAX_ATLAS_BYTES} limit"
            )
        try:
            stem = _atlas_file_name(texture_key)
        except LivingWorldUiError as error:
            raise RegionImageError(str(error)) from error
        file_name = f"{stem}{pathlib.Path(virtual_path).suffix.casefold()}"
        (atlas_directory / file_name).write_bytes(payload)
        atlas_file_by_texture[texture_key.casefold()] = file_name
        atlases.append(
            {
                "file": file_name,
                "texture": texture_key,
                "virtualPath": virtual_path,
                "archive": entry.archive,
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
            }
        )

    images: dict[str, Any] = {}
    crops_without_atlas: list[str] = []
    for _key, record in sorted(by_id.items()):
        file_name = atlas_file_by_texture.get(record.texture.casefold(), "")
        if not file_name:
            crops_without_atlas.append(record.id)
        images[record.id] = {
            "atlas": file_name,
            "texture": record.texture,
            "textureWidth": record.texture_width,
            "textureHeight": record.texture_height,
            "left": record.left,
            "top": record.top,
            "right": record.right,
            "bottom": record.bottom,
        }

    # REGIONS THAT NAME NO PORTRAIT AT ALL, listed rather than counted: the card
    # for one of these draws no plate, and that is retail's own silence rather
    # than a hole in this conversion.
    without_fortress = sorted(
        row["region"] for row in regions if not row["fortressPortrait"]
    )
    without_region_portrait = sorted(
        row["region"] for row in regions if not row["regionPortrait"]
    )
    conflicting = sorted(row["region"] for row in regions if row["conflicting"])
    # A REGION THAT NAMES A PORTRAIT AND GETS NONE. Different from a region that
    # names none: this one is retail asking for a picture that is not in retail's
    # own data, and the card must say so rather than borrow a neighbour's.
    named_but_absent = sorted(
        {
            f"{row['region']}.{key} -> {row[key]}"
            for row in regions
            for key in ("fortressPortrait", "regionPortrait")
            if row[key] and not row[f"{key}Resolved"]
        }
    )

    manifest = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "catalog": pathlib.Path(catalog_path).name,
        "document": str(document_path),
        "atlasDirectory": ATLAS_DIRECTORY,
        "regions": regions,
        "images": images,
        "atlases": atlases,
        "gaps": {
            "missingImageIds": sorted(missing_ids),
            "ambiguousImageIds": sorted(ambiguous_ids),
            "unresolvedAtlases": sorted(unresolved_textures),
            "cropsWithoutAtlas": sorted(crops_without_atlas),
            "regionsWithoutFortressPortrait": without_fortress,
            "regionsWithoutRegionPortrait": without_region_portrait,
            "regionsDeclaredTwiceWithDifferentArt": conflicting,
            "regionsNamingAPortraitRetailDoesNotDefine": named_but_absent,
        },
        "totals": {
            "regions": len(regions),
            "regionsWithFortressPortrait": sum(
                1 for row in regions if row["fortressPortraitResolved"]
            ),
            "regionsWithRegionPortrait": sum(
                1 for row in regions if row["regionPortraitResolved"]
            ),
            "imageIdsRequested": len(wanted),
            "imageIdsResolved": len(by_id),
            "atlases": len(atlases),
            "atlasBytes": sum(int(row["bytes"]) for row in atlases),
        },
    }
    (output / MANIFEST_NAME).write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m openbfme_importer.living_world_region_images",
        description=(
            "Resolve the fortress and region portraits retail's living-world "
            "regions name into a Godot bundle of atlas crops."
        ),
    )
    parser.add_argument("--catalog", required=True, type=pathlib.Path)
    parser.add_argument("--document", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args(argv)

    manifest = build_bundle(args.catalog, args.out, args.document)
    totals = manifest["totals"]
    gaps = manifest["gaps"]
    print(
        "wrote %s - %d regions, %d/%d image ids resolved across %d atlases (%d bytes)"
        % (
            args.out,
            totals["regions"],
            totals["imageIdsResolved"],
            totals["imageIdsRequested"],
            totals["atlases"],
            totals["atlasBytes"],
        )
    )
    for key, rows in sorted(gaps.items()):
        if rows:
            print("  %s (%d): %s" % (key.upper(), len(rows), ", ".join(rows)))
    return 0


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    raise SystemExit(main())
