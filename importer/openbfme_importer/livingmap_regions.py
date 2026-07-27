"""Convert retail's War of the Ring REGION TERRITORY GEOMETRY into a bundle the
Godot strategic screen can shade regions with.

WHY THIS MODULE EXISTS
----------------------
The strategic screen drew regions as coloured DOTS on retail's 3D map. Retail
draws them as FILLED TERRITORIES with visible borders - the single largest
visual gap between what this project shipped and the real game.

A prior lane established, correctly, that ``art/w3d/li/livingmap.w3d`` carries
no per-region sub-objects: its 64 meshes are 20 terrain tiles, the coast and
water, the impassable volumes, the ambient cards and eleven named landmarks.
That was taken to mean retail's territory shapes were not in the files we have.
They are - they are just in a DIFFERENT model, and retail's own data says so.

``data/ini/livingworldregioneffects.ini`` (already converted into the
living-world document as ``regionEffects``) names the geometry retail shades
with, by name:

    regionObject = LMR_Fill
    BordersEffect            geometry LMR_Border
    FilledOwnershipEffect    geometry LMR_Fill
    HomeRegionHighlight      geometry LMR_Highlight
    MouseoverEffectFlareup   geometry LMR_Fill
    RegionSelectionEffect    geometry LMR_Edge
    UnifiedEffect            geometry LMR_RegFill, LMR_RegEdge

Those are five separate W3D models under ``art/w3d/lm/``. Each carries ONE
mesh per region, and the mesh's own name is the region's ``SubObject`` id:

    art/w3d/lm/lmr_fill.w3d       62 meshes: 51 regions + 11 impassable masses
    art/w3d/lm/lmr_border.w3d     52 meshes: the outline of each region
    art/w3d/lm/lmr_highlight.w3d  52 meshes: the home-region glow
    art/w3d/lm/lmr_edge.w3d       54 meshes: the selection edge
    art/w3d/lm/lmr_regfill.w3d     7 meshes: the seven TERRITORY groups
                                   (Arnor, Eriador, Forodwaith, Gondor,
                                    Mordor, Rhovanion, Rhun)

They decode through this project's existing W3D scanner with zero unsupported
chunks, and their vertices land in the SAME world-unit space as the living map
- lmr_fill spans X[-2107, 3088] Y[-1190, 2537] inside the living map's
X[-2784, 3237] Y[-1372, 3447]. So no fitting, no scaling and no registration
step is needed or performed.

WHAT IT REFUSES TO DO
---------------------
* It NEVER invents a coordinate. Every vertex is retail's own, transformed only
  by retail's own HLOD bone binding - the same transform ``livingmap_bundle``
  applies, imported from it rather than restated.
* It NEVER invents a region. A region in the living-world document with no mesh
  in ``lmr_fill.w3d`` is listed in the manifest's ``unmatchedRegions`` and the
  Godot side draws nothing for it and says so.
* A mesh in the model that matches no document region is listed in
  ``unmatchedMeshes`` rather than silently dropped.
* It reads through the CATALOGS, never through the effective-assets cache.

WHAT THE CENTROIDS ARE FOR
--------------------------
Each fill mesh gets an AREA-WEIGHTED XY CENTROID computed from its own
triangles. This is derivation from shipped geometry, not invention, and it
closes a named gap: BFME2 authors exactly one region without a
``CustomCenterPoint`` (Rhun), which the screen has been listing as "NOT ON THE
MAP" because ``livingmap.w3d`` carried no per-region mesh to take a centre
from. ``lmr_fill.w3d`` does. The manifest records both the retail-authored
centre point (when the document has one) and the derived centroid separately,
so the Godot side can say which one it is standing on.

BUNDLE LAYOUT
-------------
``manifest.json``  provenance, per-mesh records, the document match report
``regions.bin``    little-endian float32x3 positions, uint32 indices

There are no textures. Retail shades these surfaces with a flat owner colour
(``livingworldregioneffects.ini`` carries the neutral and border colours), so
the bundle carries geometry only.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import struct
from typing import Iterable, Mapping, Sequence

from .livingmap_bundle import CatalogReader, LivingMapError, decode_livingmap

SCHEMA = "openbfme.living-map-regions"
SCHEMA_VERSION = 1

#: The five region-geometry models, keyed by the LAYER name they play in
#: ``livingworldregioneffects.ini``. The value is the virtual path in the
#: archives. ``fill`` is the one the ownership shading uses.
LAYERS: dict[str, str] = {
    "fill": "art/w3d/lm/lmr_fill.w3d",
    "border": "art/w3d/lm/lmr_border.w3d",
    "highlight": "art/w3d/lm/lmr_highlight.w3d",
    "edge": "art/w3d/lm/lmr_edge.w3d",
    "territory": "art/w3d/lm/lmr_regfill.w3d",
}

#: Layers written by default. ``highlight`` and ``edge`` are retail's
#: home-region glow and selection edge; they are converted on request but are
#: not needed for ownership shading and would triple the bundle for nothing.
DEFAULT_LAYERS = ("fill", "border", "territory")

#: Mesh-name prefix retail uses for the land masses that are not regions -
#: mountain ranges and seas that the fill model covers so the shading does not
#: bleed. They are NOT regions and are never matched to one.
IMPASSABLE_PREFIX = "IMPASSABLE"


class RegionGeometryError(RuntimeError):
    """The bundle cannot be produced, with the exact reason."""


# --- geometry ----------------------------------------------------------------


def _area_weighted_centroid(
    positions: Sequence[float], indices: Sequence[int]
) -> tuple[float, float] | None:
    """The XY centroid of a mesh, weighted by triangle area.

    Returns ``None`` for a mesh with no positive area at all, rather than
    dividing by zero and returning a coordinate that means nothing.
    """
    total = 0.0
    sum_x = 0.0
    sum_y = 0.0
    for i in range(0, len(indices), 3):
        a, b, c = indices[i], indices[i + 1], indices[i + 2]
        ax, ay = positions[a * 3], positions[a * 3 + 1]
        bx, by = positions[b * 3], positions[b * 3 + 1]
        cx, cy = positions[c * 3], positions[c * 3 + 1]
        area = abs((bx - ax) * (cy - ay) - (cx - ax) * (by - ay)) * 0.5
        if area <= 0.0:
            continue
        total += area
        sum_x += area * (ax + bx + cx) / 3.0
        sum_y += area * (ay + by + cy) / 3.0
    if total <= 0.0:
        return None
    return (sum_x / total, sum_y / total)


# --- the living-world document -----------------------------------------------


def _document_regions(document: Mapping) -> dict[str, dict]:
    """Every region the document declares, keyed by the id used to match a mesh.

    Retail's ``SubObject`` field is the authoritative link between a region and
    its geometry, and it is NOT always the region id: BFME2 spells the region
    ``Rhudaur`` but names its mesh ``Rhudaul``. The ``SubObject`` value wins;
    the region id is only a fallback for a region that declares none.
    """
    by_key: dict[str, dict] = {}
    for campaign in document.get("regionCampaigns", []) or []:
        for region in campaign.get("regions", []) or []:
            region_id = str(region.get("id", ""))
            if not region_id:
                continue
            sub_object = str(region.get("subObject", "") or region_id)
            key = sub_object.upper()
            existing = by_key.get(key)
            if existing is None:
                by_key[key] = {
                    "regionId": region_id,
                    "subObject": sub_object,
                    "campaigns": [str(campaign.get("name", ""))],
                    "centerPoint": region.get("centerPoint"),
                    "customCenterPoint": bool(region.get("customCenterPoint", False)),
                }
            elif str(campaign.get("name", "")) not in existing["campaigns"]:
                existing["campaigns"].append(str(campaign.get("name", "")))
    return by_key


def _document_territories(document: Mapping) -> dict[str, str]:
    """The TERRITORY groups the document declares, keyed for mesh matching.

    ``lmr_regfill.w3d`` carries one mesh per territory, not per region, so its
    meshes are matched against ``territoryBonuses[].effectName`` and never
    against a region id. Keeping the two namespaces apart is why this is a
    separate map: retail names a REGION ``Arnor`` and a TERRITORY ``Arnor``,
    and they are different things with different geometry.
    """
    by_key: dict[str, str] = {}
    for campaign in document.get("regionCampaigns", []) or []:
        for bonus in campaign.get("territoryBonuses", []) or []:
            name = str(bonus.get("effectName", ""))
            if name:
                by_key.setdefault(name.upper(), name)
    return by_key


# --- the bundle ---------------------------------------------------------------


def build_bundle(
    catalog_path: pathlib.Path | str,
    output_root: pathlib.Path | str,
    *,
    document_path: pathlib.Path | str | None = None,
    layers: Iterable[str] = DEFAULT_LAYERS,
) -> dict:
    """Write the region-geometry bundle and return its manifest."""
    reader = CatalogReader(catalog_path)
    output = pathlib.Path(output_root)
    output.mkdir(parents=True, exist_ok=True)

    document: Mapping = {}
    if document_path is not None:
        document = json.loads(pathlib.Path(document_path).read_text(encoding="utf-8"))
    by_key = _document_regions(document)
    territories = _document_territories(document)

    blob = bytearray()
    records: list[dict] = []
    sources: list[dict] = []
    matched_keys: set[str] = set()
    matched_territories: set[str] = set()

    requested = list(layers)
    for layer in requested:
        virtual_path = LAYERS.get(layer)
        if virtual_path is None:
            raise RegionGeometryError(
                f"unknown layer {layer!r}; known layers are {sorted(LAYERS)}"
            )
        entry = reader.resolve(virtual_path)
        if entry is None:
            raise RegionGeometryError(
                f"{virtual_path} is not in the catalog, so the {layer} layer "
                "cannot be converted"
            )
        payload = reader.read(entry)
        try:
            sub_objects = decode_livingmap(payload, virtual_path)
        except LivingMapError as error:
            raise RegionGeometryError(f"{virtual_path}: {error}") from error
        sources.append(
            {
                "layer": layer,
                "virtualPath": virtual_path,
                "archive": entry.archive,
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
                "meshes": len(sub_objects),
            }
        )

        for sub in sorted(sub_objects, key=lambda s: s.name):
            key = sub.name.upper()
            impassable = key.startswith(IMPASSABLE_PREFIX)
            is_territory_layer = layer == "territory"
            match = None
            territory_name = None
            if impassable:
                pass
            elif is_territory_layer:
                territory_name = territories.get(key)
                if territory_name is not None:
                    matched_territories.add(key)
            else:
                match = by_key.get(key)
                if match is not None:
                    matched_keys.add(key)

            position_offset = len(blob)
            blob.extend(struct.pack(f"<{len(sub.positions)}f", *sub.positions))
            index_offset = len(blob)
            blob.extend(struct.pack(f"<{len(sub.indices)}I", *sub.indices))

            centroid = _area_weighted_centroid(sub.positions, sub.indices)
            records.append(
                {
                    "layer": layer,
                    "mesh": sub.name,
                    "regionId": None if match is None else match["regionId"],
                    "territoryName": territory_name,
                    "impassable": impassable,
                    "vertexCount": sub.vertex_count,
                    "triangleCount": sub.triangle_count,
                    "positionOffset": position_offset,
                    "positionBytes": index_offset - position_offset,
                    "indexOffset": index_offset,
                    "indexCount": len(sub.indices),
                    "boundsMin": list(sub.bounds_min),
                    "boundsMax": list(sub.bounds_max),
                    # DERIVED from retail's own triangles. Recorded separately
                    # from the authored centre point so the Godot side can say
                    # which of the two it is standing on.
                    "derivedCentroid": None if centroid is None else list(centroid),
                    "authoredCenterPoint": None if match is None else match["centerPoint"],
                    "hasAuthoredCenterPoint": bool(
                        match is not None and match["customCenterPoint"]
                    ),
                }
            )

    unmatched_regions = sorted(
        {
            value["regionId"]
            for key, value in by_key.items()
            if key not in matched_keys
        }
    )
    unmatched_meshes = sorted(
        {
            f"{record['layer']}:{record['mesh']}"
            for record in records
            if record["regionId"] is None
            and record["territoryName"] is None
            and not record["impassable"]
        }
    )
    unmatched_territories = sorted(
        {name for key, name in territories.items() if key not in matched_territories}
    )

    mesh_path = output / "regions.bin"
    mesh_path.write_bytes(bytes(blob))

    manifest = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "catalog": pathlib.Path(catalog_path).name,
        "sources": sources,
        "meshBin": {
            "file": "regions.bin",
            "bytes": len(blob),
            "sha256": hashlib.sha256(bytes(blob)).hexdigest(),
            "byteOrder": "little",
            "positionType": "float32x3",
            "indexType": "uint32",
        },
        "layers": requested,
        "records": records,
        "document": {
            "path": None if document_path is None else str(document_path),
            "regionsDeclared": len(by_key),
            "regionsMatched": len(matched_keys),
            # NAMED, not swallowed. A region with no geometry is a hole in the
            # shading and the screen has to be able to say which one.
            "unmatchedRegions": unmatched_regions,
            "unmatchedMeshes": unmatched_meshes,
            "territoriesDeclared": len(territories),
            "territoriesMatched": len(matched_territories),
            "unmatchedTerritories": unmatched_territories,
        },
        "totals": {
            "records": len(records),
            "vertices": sum(int(r["vertexCount"]) for r in records),
            "triangles": sum(int(r["triangleCount"]) for r in records),
        },
    }
    (output / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m openbfme_importer.livingmap_regions",
        description=(
            "Convert retail's War of the Ring region territory geometry "
            "(lmr_fill / lmr_border / lmr_regfill) into a Godot bundle."
        ),
    )
    parser.add_argument("--catalog", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    parser.add_argument(
        "--document",
        type=pathlib.Path,
        default=None,
        help="the living-world document, used to match meshes to region ids",
    )
    parser.add_argument(
        "--layer",
        action="append",
        choices=sorted(LAYERS),
        default=None,
        help="repeatable; defaults to fill, border and territory",
    )
    args = parser.parse_args(argv)

    manifest = build_bundle(
        args.catalog,
        args.out,
        document_path=args.document,
        layers=tuple(args.layer) if args.layer else DEFAULT_LAYERS,
    )
    totals = manifest["totals"]
    document = manifest["document"]
    print(
        f"wrote {args.out} - {totals['records']} meshes, "
        f"{totals['vertices']} vertices, {totals['triangles']} triangles, "
        f"{manifest['meshBin']['bytes']} bytes of geometry"
    )
    print(
        f"  document: {document['regionsMatched']}/{document['regionsDeclared']} "
        f"regions matched to geometry"
    )
    if document["unmatchedRegions"]:
        print(
            f"  REGIONS WITH NO GEOMETRY ({len(document['unmatchedRegions'])}): "
            + ", ".join(document["unmatchedRegions"])
        )
    if document["unmatchedMeshes"]:
        print(
            f"  MESHES MATCHING NO REGION ({len(document['unmatchedMeshes'])}): "
            + ", ".join(document["unmatchedMeshes"])
        )
    return 0


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    raise SystemExit(main())
