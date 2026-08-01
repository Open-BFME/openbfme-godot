"""Convert retail's War of the Ring LIVING-WORLD MARKER MODELS - the 3D banners,
army-ant columns, structures and foundation decals retail stands on its strategic
map - into a bundle the Godot strategic screen can put in the world.

WHY THIS MODULE EXISTS
----------------------
The strategic screen draws every army as a FLAT PLATE and every build plot as a
FLAT RING. Retail draws them as MODELS STANDING ON THE MAP: a banner on a staff
with pips for the stack's size, a column of marching "army ants", a faction
foundation pad under a build plot.

A prior lane established, correctly, that ``data/ini/livingworldicons/*.ini``,
``livingworldbuildingicons.ini`` and ``livingworldbuildploticons.ini`` carry NO
portraits - they are 3D map-marker definitions and nothing else. It recorded the
model NAMES so the screen could say what it was standing in for, and said
converting them was a separate, larger job. This is that job.

    LivingWorldArmyIcon MoWArmyIcon
        Object Banner
            Model       = LWArmyHMoW
            SubObjects  = LWSTAFF LWBANNER
            ZOffset     = 5
            Scale       = 1.0
            OrientAngle = 270
            UseHouseColor = Yes
        End
    End

Every field above is retail's own and every one of them travels into the
manifest, because they are the placement: ``Model`` names the W3D, ``SubObjects``
selects WHICH MESHES OF IT this slot shows, ``ZOffset`` lifts it off the terrain,
``Scale`` sizes it and ``OrientAngle`` turns it. A marker drawn without them
would be retail geometry in a position this project chose, which is a different
claim from retail's marker.

THE CENSUS, MEASURED RATHER THAN QUOTED
---------------------------------------
Retail authors 75 marker families - 40 ``LivingWorldArmyIcon``, 28
``LivingWorldBuildingIcon``, 7 ``LivingWorldBuildPlotIcon`` - carrying 506
``Object`` slots between them. Those slots name 81 DISTINCT W3D models (43 named
by army icons, 31 by building icons, 9 by build-plot icons; three models are
shared between families, which is why the three counts sum to more than 81).

WHAT IT REFUSES TO DO
---------------------
* It NEVER substitutes a mesh. A model that is not in the catalog, or that the
  W3D scanner will not decode without unsupported chunks, is recorded in
  ``models`` with ``resolved: false`` and a REASON, keeps its retail name, and
  the Godot side draws its flat plate or ring instead and names the model.
* It NEVER invents a coordinate, a scale or an angle. Every vertex is retail's
  own with retail's own HLOD bone transform applied - the same transform
  ``livingmap_bundle`` applies, imported from it rather than restated - and every
  placement number is read off retail's own ``Object`` block.
* It NEVER invents a texture. A declared texture that resolves to nothing is
  recorded ``resolved: false`` and the mesh is drawn untextured.
* It reads through the CATALOGS, never through the effective-assets cache.

WHAT IT DOES NOT DECIDE
-----------------------
Which marker stands where. This module converts geometry and carries retail's
authored slot table; binding an army to a marker family is the screen's job and
uses retail's own authored links (``ArmyToSpawn.Icon`` for a recruited army,
``LivingWorldPlayerTemplate.DefaultArmyIconName`` for a seat), never a
resemblance between two names.

BUNDLE LAYOUT
-------------
``living-world-markers.json``  the manifest: families, slots, models, meshes
``markers.bin``                little-endian float32 positions/normals/uvs,
                               uint32 indices - the same layout as ``mesh.bin``
``marker-textures/*``          verbatim retail texture bytes, unaltered
"""

from __future__ import annotations

import argparse
import hashlib
import json
import pathlib
import re
import struct
from typing import Any, Iterable, Mapping

from .livingmap_bundle import (
    CatalogReader,
    LivingMapError,
    decode_livingmap,
    resolve_texture,
)
from .livingworld import Gap, expand_document, flatten_document, read_tree

SCHEMA = "openbfme.living-world-markers"
SCHEMA_VERSION = 1

MANIFEST_NAME = "living-world-markers.json"
MESH_NAME = "markers.bin"
TEXTURE_DIRECTORY = "marker-textures"

#: The three marker families retail authors, keyed by the family name this
#: bundle uses. The value is ``(block kind, virtual paths)``. The army icons are
#: split across seven per-faction documents; the other two are single files.
ARMY_ICON_PREFIX = "data/ini/livingworldicons/"
BUILDING_ICONS_PATH = "data/ini/livingworldbuildingicons.ini"
BUILD_PLOT_ICONS_PATH = "data/ini/livingworldbuildploticons.ini"

FAMILY_ARMY = "army"
FAMILY_BUILDING = "building"
FAMILY_BUILD_PLOT = "buildPlot"

#: The block kind each family is authored as.
FAMILY_KIND = {
    FAMILY_ARMY: "livingworldarmyicon",
    FAMILY_BUILDING: "livingworldbuildingicon",
    FAMILY_BUILD_PLOT: "livingworldbuildploticon",
}

#: Bounded so a hostile or corrupt catalog cannot make this module allocate
#: without limit. Retail's real numbers are far below every one of these: 75
#: families, 506 slots, 81 models, 251 meshes.
MAX_FAMILIES = 512
MAX_MODELS = 1024
MAX_TEXTURE_BYTES = 32 * 1024 * 1024

#: Every ``Object`` key retail authors in the three families, mapped to its
#: manifest name. THE LIST IS TOTAL - it was enumerated from retail's own 506
#: slots, and a key outside it is reported as a gap rather than dropped, so a
#: future document that adds one cannot go through silently.
_OBJECT_FIELDS = {
    "animmode": "animMode",
    "clickable": "clickable",
    "displayatrallypoint": "displayAtRallyPoint",
    "fadeholdpercent": "fadeHoldPercent",
    "fadeintime": "fadeInTime",
    "fademethod": "fadeMethod",
    "fadeouttime": "fadeOutTime",
    "fadetypeforhiding": "fadeTypeForHiding",
    "fadetypeforhilighting": "fadeTypeForHilighting",
    "fadetypeforselection": "fadeTypeForSelection",
    "fadetypeforshowing": "fadeTypeForShowing",
    "fadetypeforunhilighting": "fadeTypeForUnhilighting",
    "hidewhennotproducing": "hideWhenNotProducing",
    "hidewhennotunderconstruction": "hideWhenNotUnderConstruction",
    "hidewhenunderconstruction": "hideWhenUnderConstruction",
    "hidewhenunhilighted": "hideWhenUnhilighted",
    "hidewhenunselected": "hideWhenUnselected",
    "model": "model",
    "orientangle": "orientAngle",
    "pickbox": "pickbox",
    "scale": "scale",
    "shadow": "shadow",
    "showonlyaftermoveorder": "showOnlyAfterMoveOrder",
    "showonlyforallies": "showOnlyForAllies",
    "subobjects": "subObjects",
    "usehousecolor": "useHouseColor",
    "visiblearmysizes": "visibleArmySizes",
    "zoffset": "zOffset",
}

#: Block-level keys. Retail authors only sound and EVA cues here; they are
#: recorded so the manifest is a complete reading of the block, and because a
#: later audio lane will want them.
_BLOCK_FIELDS = {
    "disbandunitsound": "disbandUnitSound",
    "kickoutreinforcementssound": "kickOutReinforcementsSound",
    "onbuildingdestroyedsound": "onBuildingDestroyedSound",
    "onconstructionbegunsound": "onConstructionBegunSound",
    "onconstructionfinishedsound": "onConstructionFinishedSound",
    "onmoveplannedsound": "onMovePlannedSound",
    "onmovestartedsound": "onMoveStartedSound",
    "onselectedsound": "onSelectedSound",
    "retreatteleporttohomeregionevaevent": "retreatTeleportToHomeRegionEvaEvent",
    "retreatteleporttononhomeregionevaevent": "retreatTeleportToNonHomeRegionEvaEvent",
    "welcomereinforcementssound": "welcomeReinforcementsSound",
}

#: Which slot of each family is the marker's BODY - the thing that replaces the
#: flat plate or ring. Retail's own slot names, not this project's: an army is
#: read off the map by its ``Banner``, a structure by its ``Building``, a plot by
#: its ``Decal``. Recorded in the manifest so the Godot side does not restate it.
BODY_SLOT = {
    FAMILY_ARMY: "Banner",
    FAMILY_BUILDING: "Building",
    FAMILY_BUILD_PLOT: "Decal",
}

_SAFE_FILE = re.compile(r"[A-Za-z0-9._-]+")


class MarkerModelError(RuntimeError):
    """The bundle cannot be produced, with the exact reason."""


def _unquote(value: str) -> str:
    text = str(value or "").strip()
    if len(text) >= 2 and text[0] == '"' and text[-1] == '"':
        return text[1:-1]
    return text


def _tree(reader: CatalogReader, virtual_path: str, gaps: list[Gap]):
    def read(path: str) -> bytes:
        entry = reader.resolve(path)
        if entry is None:
            raise MarkerModelError(f"{path} is not in the catalog")
        return reader.read(entry)

    document = expand_document(virtual_path, read)
    lines = flatten_document(
        document, openers=frozenset(), whitespace_pairs=False, gaps=gaps
    )
    return read_tree(lines, openers=frozenset())


# --- the marker families -------------------------------------------------------


def _family_documents(reader: CatalogReader) -> dict[str, list[str]]:
    """The virtual paths each family is authored in, by the winner rule."""

    army = sorted(
        name
        for name in reader._winners  # noqa: SLF001 - read-only view
        if name.casefold().startswith(ARMY_ICON_PREFIX)
    )
    if not army:
        raise MarkerModelError(
            f"the catalog carries no {ARMY_ICON_PREFIX}** document, so no army "
            "marker can be converted"
        )
    return {
        FAMILY_ARMY: army,
        FAMILY_BUILDING: [BUILDING_ICONS_PATH],
        FAMILY_BUILD_PLOT: [BUILD_PLOT_ICONS_PATH],
    }


def read_families(reader: CatalogReader, gaps: list[Gap]) -> list[dict[str, Any]]:
    """Every marker family retail authors, with every slot it declares.

    Slots keep their AUTHORED ORDER inside a family. Retail's draw order is its
    own and reordering them would change which of two coincident models is on
    top - a presentation difference this module has no authority to make.
    """

    families: list[dict[str, Any]] = []
    for family, paths in _family_documents(reader).items():
        kind = FAMILY_KIND[family]
        for virtual_path in paths:
            tree = _tree(reader, virtual_path, gaps)
            for node in tree.roots:
                if node.kind.casefold() != kind:
                    gaps.append(
                        Gap(
                            virtual_path=node.virtual_path,
                            line=node.line,
                            scope="<root>",
                            reason="unmodelled-block",
                            detail=node.kind,
                        )
                    )
                    continue
                scope = f"{node.kind} {node.name}"
                row: dict[str, Any] = {
                    "id": str(node.name or ""),
                    "family": family,
                    "document": virtual_path,
                    "bodySlot": BODY_SLOT[family],
                }
                for key, value in node.fields:
                    folded = key.casefold()
                    if folded in _BLOCK_FIELDS:
                        row[_BLOCK_FIELDS[folded]] = _unquote(value)
                    else:
                        gaps.append(
                            Gap(
                                virtual_path=node.virtual_path,
                                line=node.line,
                                scope=scope,
                                reason="unknown-field",
                                detail=key,
                            )
                        )
                slots: list[dict[str, Any]] = []
                for child in node.children:
                    if child.kind.casefold() != "object":
                        gaps.append(
                            Gap(
                                virtual_path=child.virtual_path,
                                line=child.line,
                                scope=scope,
                                reason="unmodelled-block",
                                detail=child.kind,
                            )
                        )
                        continue
                    slot: dict[str, Any] = {"slot": str(child.name or "")}
                    for key, value in child.fields:
                        folded = key.casefold()
                        if folded in _OBJECT_FIELDS:
                            slot[_OBJECT_FIELDS[folded]] = _unquote(value)
                        else:
                            gaps.append(
                                Gap(
                                    virtual_path=child.virtual_path,
                                    line=child.line,
                                    scope=f"{scope} / Object {child.name}",
                                    reason="unknown-field",
                                    detail=key,
                                )
                            )
                    # `SubObjects` is a whitespace-separated list of MESH NAMES
                    # inside the model. An empty one means the whole model.
                    slot["subObjectList"] = str(slot.get("subObjects", "")).split()
                    slots.append(slot)
                row["slots"] = slots
                families.append(row)
    if len(families) > MAX_FAMILIES:
        raise MarkerModelError(
            f"{len(families)} marker families, over the {MAX_FAMILIES} limit"
        )
    families.sort(key=lambda item: (str(item["family"]), str(item["id"]).casefold()))
    return families


def model_names(families: Iterable[Mapping[str, Any]]) -> list[str]:
    """Every distinct model name the families name, in a stable order."""

    names: set[str] = set()
    for family in families:
        for slot in family.get("slots", []) or []:
            name = str(slot.get("model", "")).strip()
            if name:
                names.add(name)
    return sorted(names, key=str.casefold)


# --- the bundle -----------------------------------------------------------------


def _texture_file_name(declared: str) -> str:
    stem = declared.rsplit("/", 1)[-1].rsplit("\\", 1)[-1].casefold()
    if not _SAFE_FILE.fullmatch(stem):
        raise MarkerModelError(f"unsafe texture name: {declared!r}")
    return stem


def build_bundle(
    catalog_path: pathlib.Path | str, output_root: pathlib.Path | str
) -> dict:
    """Write the marker-model bundle and return its manifest."""

    reader = CatalogReader(catalog_path)
    output = pathlib.Path(output_root)
    output.mkdir(parents=True, exist_ok=True)

    gaps: list[Gap] = []
    families = read_families(reader, gaps)
    names = model_names(families)
    if len(names) > MAX_MODELS:
        raise MarkerModelError(f"{len(names)} models, over the {MAX_MODELS} limit")

    blob = bytearray()
    models: list[dict[str, Any]] = []
    textures: dict[str, dict[str, Any]] = {}

    def texture_row(declared: str) -> dict[str, Any]:
        """Resolve one declared texture ONCE, and copy retail's bytes verbatim."""
        existing = textures.get(declared)
        if existing is not None:
            return existing
        hit = resolve_texture(reader, declared)
        if hit is None:
            # NOT faked. The Godot side reads resolved=false and draws the mesh
            # untextured with the declared name on screen.
            row: dict[str, Any] = {"declared": declared, "resolved": False}
            textures[declared] = row
            return row
        payload = reader.read(hit)
        if len(payload) > MAX_TEXTURE_BYTES:
            raise MarkerModelError(
                f"{hit.name} is {len(payload)} bytes, over the "
                f"{MAX_TEXTURE_BYTES} limit"
            )
        file_name = _texture_file_name(hit.name)
        (output / TEXTURE_DIRECTORY).mkdir(parents=True, exist_ok=True)
        (output / TEXTURE_DIRECTORY / file_name).write_bytes(payload)
        row = {
            "declared": declared,
            "resolved": True,
            "file": f"{TEXTURE_DIRECTORY}/{file_name}",
            "source": hit.name,
            "archive": hit.archive,
            "bytes": len(payload),
            "sha256": hashlib.sha256(payload).hexdigest(),
        }
        textures[declared] = row
        return row

    for name in names:
        entry = reader.resolve_basename(f"{name}.w3d")
        if entry is None:
            # NAMED, not dropped. The screen keeps this marker's flat plate and
            # says which retail model it is standing in for.
            models.append(
                {
                    "model": name,
                    "resolved": False,
                    "reason": f"no {name}.w3d in the catalog under any archive",
                    "meshes": [],
                }
            )
            continue
        payload = reader.read(entry)
        try:
            sub_objects = decode_livingmap(payload, entry.name)
        except LivingMapError as error:
            models.append(
                {
                    "model": name,
                    "resolved": False,
                    "virtualPath": entry.name,
                    "archive": entry.archive,
                    "reason": f"the W3D scanner refused it: {error}",
                    "meshes": [],
                }
            )
            continue

        mesh_rows: list[dict[str, Any]] = []
        for sub in sub_objects:
            position_offset = len(blob)
            blob.extend(struct.pack(f"<{len(sub.positions)}f", *sub.positions))
            normal_offset = len(blob)
            blob.extend(struct.pack(f"<{len(sub.normals)}f", *sub.normals))
            uv_offset = len(blob)
            blob.extend(struct.pack(f"<{len(sub.uvs)}f", *sub.uvs))
            index_offset = len(blob)
            blob.extend(struct.pack(f"<{len(sub.indices)}I", *sub.indices))

            base = sub.base_texture
            base_row = texture_row(base) if base else None
            declared_rows = [texture_row(t) for t in sub.textures]
            mesh_rows.append(
                {
                    "mesh": sub.name,
                    "vertexCount": sub.vertex_count,
                    "triangleCount": sub.triangle_count,
                    "hasNormals": bool(sub.normals),
                    "hasUVs": bool(sub.uvs),
                    "positionOffset": position_offset,
                    "normalOffset": normal_offset,
                    "uvOffset": uv_offset,
                    "indexOffset": index_offset,
                    "indexCount": len(sub.indices),
                    "boundsMin": list(sub.bounds_min),
                    "boundsMax": list(sub.bounds_max),
                    "textures": list(sub.textures),
                    "baseTexture": base,
                    "baseTextureFile": (
                        base_row.get("file") if base_row and base_row["resolved"] else None
                    ),
                    "textureFiles": [
                        row["file"] for row in declared_rows if row["resolved"]
                    ],
                }
            )
        models.append(
            {
                "model": name,
                "resolved": True,
                "virtualPath": entry.name,
                "archive": entry.archive,
                "bytes": len(payload),
                "sha256": hashlib.sha256(payload).hexdigest(),
                "meshes": mesh_rows,
            }
        )

    (output / MESH_NAME).write_bytes(bytes(blob))

    resolved_models = [row for row in models if row["resolved"]]
    unresolved_models = sorted(row["model"] for row in models if not row["resolved"])
    texture_rows = [textures[key] for key in sorted(textures)]
    unresolved_textures = sorted(
        row["declared"] for row in texture_rows if not row["resolved"]
    )

    # SLOTS WHOSE MODEL DID NOT CONVERT, named by family and slot. A count alone
    # would let one family's hole hide inside another's.
    slots_without_model: list[str] = []
    by_model = {row["model"]: row for row in models}
    for family in families:
        for slot in family["slots"]:
            model = str(slot.get("model", ""))
            row = by_model.get(model)
            if row is None or not row["resolved"]:
                slots_without_model.append(
                    "%s/%s -> %s" % (family["id"], slot.get("slot", "?"), model)
                )
    slots_without_model.sort()

    # SUB-OBJECT NAMES A SLOT ASKS FOR THAT ITS MODEL DOES NOT CARRY. Retail's
    # own link between a slot and a mesh, checked rather than trusted.
    missing_sub_objects: list[str] = []
    for family in families:
        for slot in family["slots"]:
            model = str(slot.get("model", ""))
            row = by_model.get(model)
            if row is None or not row["resolved"]:
                continue
            have = {str(mesh["mesh"]).upper() for mesh in row["meshes"]}
            for wanted in slot["subObjectList"]:
                if wanted.upper() not in have:
                    missing_sub_objects.append(
                        "%s/%s -> %s.%s"
                        % (family["id"], slot.get("slot", "?"), model, wanted)
                    )
    missing_sub_objects.sort()

    manifest = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "catalog": pathlib.Path(catalog_path).name,
        "meshBin": {
            "file": MESH_NAME,
            "bytes": len(blob),
            "sha256": hashlib.sha256(bytes(blob)).hexdigest(),
            "byteOrder": "little",
            "positionType": "float32x3",
            "normalType": "float32x3",
            "uvType": "float32x2",
            "indexType": "uint32",
        },
        "textureDirectory": TEXTURE_DIRECTORY,
        "coordinateSpace": {
            "note": (
                "Mesh vertices are in RETAIL MODEL SPACE with the model's own "
                "HLOD bone transform applied, Z up - the same convention "
                "livingmap.w3d uses. A marker is placed by translating to the "
                "retail world (x, y) it belongs to, adding the slot's ZOffset "
                "to the sampled terrain height, scaling by the slot's Scale and "
                "turning by its OrientAngle about Z. Godot placement is "
                "(x, z, y) -> (x, z_world_height, -y), as livingmap_bundle."
            ),
        },
        "families": families,
        "models": models,
        "textures": texture_rows,
        "gaps": {
            "unresolvedModels": unresolved_models,
            "unresolvedTextures": unresolved_textures,
            "slotsWithoutModel": slots_without_model,
            "missingSubObjects": missing_sub_objects,
            "documents": [
                {
                    "virtualPath": gap.virtual_path,
                    "line": gap.line,
                    "scope": gap.scope,
                    "reason": gap.reason,
                    "detail": gap.detail,
                }
                for gap in gaps
            ],
        },
        "totals": {
            "families": len(families),
            "familiesByKind": {
                key: sum(1 for row in families if row["family"] == key)
                for key in (FAMILY_ARMY, FAMILY_BUILDING, FAMILY_BUILD_PLOT)
            },
            "slots": sum(len(row["slots"]) for row in families),
            "modelsNamed": len(models),
            "modelsConverted": len(resolved_models),
            "meshes": sum(len(row["meshes"]) for row in resolved_models),
            "vertices": sum(
                int(mesh["vertexCount"])
                for row in resolved_models
                for mesh in row["meshes"]
            ),
            "triangles": sum(
                int(mesh["triangleCount"])
                for row in resolved_models
                for mesh in row["meshes"]
            ),
            "texturesDeclared": len(texture_rows),
            "texturesResolved": sum(1 for row in texture_rows if row["resolved"]),
            "textureBytes": sum(int(row.get("bytes", 0)) for row in texture_rows),
        },
    }
    (output / MANIFEST_NAME).write_text(
        json.dumps(manifest, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    return manifest


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(
        prog="python -m openbfme_importer.livingworld_markers",
        description=(
            "Convert retail's War of the Ring 3D marker models - army banners, "
            "structure icons and build-plot decals - into a Godot bundle."
        ),
    )
    parser.add_argument("--catalog", required=True, type=pathlib.Path)
    parser.add_argument("--out", required=True, type=pathlib.Path)
    args = parser.parse_args(argv)

    manifest = build_bundle(args.catalog, args.out)
    totals = manifest["totals"]
    gaps = manifest["gaps"]
    print(
        "wrote %s - %d/%d models converted, %d meshes, %d vertices, %d triangles, "
        "%d bytes of geometry"
        % (
            args.out,
            totals["modelsConverted"],
            totals["modelsNamed"],
            totals["meshes"],
            totals["vertices"],
            totals["triangles"],
            manifest["meshBin"]["bytes"],
        )
    )
    print(
        "  %d families (%s), %d slots"
        % (
            totals["families"],
            ", ".join(
                f"{key} {value}" for key, value in sorted(totals["familiesByKind"].items())
            ),
            totals["slots"],
        )
    )
    print(
        "  %d/%d textures resolved, %d bytes"
        % (totals["texturesResolved"], totals["texturesDeclared"], totals["textureBytes"])
    )
    for key in ("unresolvedModels", "unresolvedTextures", "slotsWithoutModel", "missingSubObjects"):
        rows = gaps[key]
        if rows:
            print("  %s (%d): %s" % (key.upper(), len(rows), ", ".join(rows)))
    return 0


if __name__ == "__main__":  # pragma: no cover - CLI entry point
    raise SystemExit(main())
