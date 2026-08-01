"""Convert retail's War of the Ring strategic map (``livingmap.w3d``) into a
bundle the Godot strategic screen can load at runtime.

WHY THIS MODULE EXISTS
----------------------
The War of the Ring screen used to draw regions as flat 2D markers because
nothing had ever converted retail's actual strategic map. That map is a single
W3D model - ``art/w3d/li/livingmap.w3d`` - carrying the whole of Middle-earth as
64 sub-objects, and it decodes through this project's existing W3D scanner with
zero unsupported chunks. This module turns it into geometry plus verbatim retail
textures.

WHAT IT REFUSES TO DO
---------------------
* It NEVER invents a texture. A material whose texture does not resolve in the
  catalog is recorded with ``"resolved": false`` and the Godot side draws that
  sub-object untextured and says so. No stand-in image is substituted.
* It NEVER invents a coordinate. Every vertex is retail's own, transformed only
  by retail's own HLOD bone binding.
* It reads through the CATALOGS, never through the effective-assets cache (which
  is known stale and serves BFME2 bytes where RotWK should win). Winner for a
  name is the lowest ``(precedence, archive.casefold())``, which is the
  documented resolution rule.

THE COORDINATE SPACE - THE POINT OF THE WHOLE EXERCISE
------------------------------------------------------
Retail authors the living map and the living-world REGION DOCUMENT in the SAME
world-unit space. This is measured, not assumed, and the measurement is written
into the manifest so the Godot side and any reviewer can re-check it:

* Landmark sub-objects are modelled about their own origin and placed by their
  HLOD bone translation. Those bone translations agree with the corresponding
  region ``centerPoint`` in ``livingworld.ini`` to within ~120 world units on a
  map spanning ~6000 units - e.g. ``LM_ORTHANCTOWER`` bone (148.1, 637.1) versus
  region ``Isengard`` centre (140, 640).
* The 20 terrain tiles ``LM_01``..``LM_20``, once their bone transform is
  applied, tile a 5x4 grid of ~1204 x ~1205 unit cells with no overlap and no
  gap, spanning X[-2784, 3237] Y[-1372, 3447]. The document's placed region
  centres span X[-1680, 2620] Y[-900, 2050], strictly inside that.

So the mapping from document space to map space is the IDENTITY, and the bundle
records the grid proof so a test can assert it rather than trust this docstring.

BUNDLE LAYOUT
-------------
``manifest.json``  provenance, sub-objects, materials, the coordinate proof
``mesh.bin``       little-endian float32/uint32 vertex and index blocks
``textures/*``     verbatim retail bytes (DDS/JPG/TGA), unaltered

Godot 4.7 loads the DDS payloads directly with ``Image.load_dds_from_buffer``,
so nothing is transcoded and nothing is re-encoded.
"""

from __future__ import annotations

import hashlib
import json
import pathlib
import struct
from collections import defaultdict
from dataclasses import dataclass, field
from typing import Iterable, Mapping

from .big import BigArchive, BigEntry
from .w3d_metadata import scan_w3d_metadata

SCHEMA = "openbfme.living-map"
SCHEMA_VERSION = 1

LIVINGMAP_W3D = "art/w3d/li/livingmap.w3d"

#: Retail's own byte ceiling for one asset read. The living map is ~6 MiB; the
#: largest texture is ~1.4 MiB. 64 MiB is generous and still bounded.
MAX_ASSET_BYTES = 64 * 1024 * 1024

# --- W3D chunk ids this module touches directly ------------------------------
C_MESH = 0x00
C_VERTICES = 0x02
C_NORMALS = 0x03
C_TRIANGLES = 0x20
C_TEXTURES = 0x30
C_TEXTURE = 0x31
C_TEXTURE_NAME = 0x32
C_MATERIAL_PASS = 0x38
C_TEXTURE_STAGE = 0x48
C_TEXTURE_IDS = 0x49
C_STAGE_TEXCOORDS = 0x4A
C_SHADER_MATERIALS = 0x50
C_SHADER_MATERIAL = 0x51
C_SHADER_MATERIAL_PROPERTY = 0x53
C_PIVOTS = 0x102

#: Shader-material property that names the base colour map. Retail's living map
#: uses ``NormalMapped.fx`` for three landmarks (Amon Sul, Carn Dum, Erebor) and
#: ``WaterShader.FX`` for the ocean; only the first kind names a diffuse texture,
#: which is why the ocean legitimately has none to bind.
SHADER_DIFFUSE_PROPERTY = "DiffuseTexture"

#: ``W3dPivotStruct``: char Name[16]; uint32 ParentIdx; Vector3 Translation;
#: Vector3 EulerAngles; Quaternion Rotation.
PIVOT_STRIDE = 60

#: Directories a compiled texture may live in, tried in order. Retail buckets
#: compiled textures by the first two characters of the file name.
_TEXTURE_EXTENSIONS = (".dds", ".tga", ".jpg")


class LivingMapError(RuntimeError):
    """The bundle cannot be produced, with the exact reason."""


# --- catalog access -----------------------------------------------------------


@dataclass(frozen=True, slots=True)
class _Resolved:
    name: str
    archive: str
    offset: int
    size: int
    precedence: int


class CatalogReader:
    """Reads assets out of a retail install catalog by the documented winner rule.

    Deliberately NOT ``InstallCatalog`` - that class is being edited by another
    lane, and this module only needs the read half. The winner rule is the one
    the project documents: lowest ``(precedence, archive.casefold())`` per name.
    """

    def __init__(self, catalog_path: pathlib.Path | str) -> None:
        self.path = pathlib.Path(catalog_path)
        document = json.loads(self.path.read_text(encoding="utf-8"))
        self.install_root = pathlib.Path(document["install_root"])
        self._winners: dict[str, _Resolved] = {}
        self._by_basename: dict[str, _Resolved] = {}
        for row in document["entries"]:
            entry = _Resolved(
                name=str(row["name"]),
                archive=str(row["archive"]),
                offset=int(row["offset"]),
                size=int(row["size"]),
                precedence=int(row["precedence"]),
            )
            key = entry.name.casefold()
            rank = (entry.precedence, entry.archive.casefold())
            current = self._winners.get(key)
            if current is None or rank < (current.precedence, current.archive.casefold()):
                self._winners[key] = entry
        for entry in self._winners.values():
            base = entry.name.rsplit("/", 1)[-1].casefold()
            current = self._by_basename.get(base)
            rank = (entry.precedence, entry.archive.casefold())
            if current is None or rank < (current.precedence, current.archive.casefold()):
                self._by_basename[base] = entry
        self._archives: dict[str, BigArchive] = {}

    def resolve(self, virtual_path: str) -> _Resolved | None:
        return self._winners.get(virtual_path.casefold())

    def resolve_basename(self, basename: str) -> _Resolved | None:
        return self._by_basename.get(basename.casefold())

    def read(self, entry: _Resolved) -> bytes:
        archive_path = pathlib.Path(entry.archive)
        if not archive_path.is_absolute():
            archive_path = self.install_root / archive_path
        key = str(archive_path).casefold()
        archive = self._archives.get(key)
        if archive is None:
            archive = BigArchive.open(archive_path)
            self._archives[key] = archive
        return archive.read_entry(
            BigEntry(name=entry.name, offset=entry.offset, size=entry.size),
            max_bytes=MAX_ASSET_BYTES,
        )


def resolve_texture(reader: CatalogReader, declared: str) -> _Resolved | None:
    """Find the shipped file behind a W3D texture reference.

    W3D records authoring-time names like ``LM_00000.tga``; retail ships the
    compiled payload as ``art/compiledtextures/lm/lm_00000.dds``. The bucket is
    the first two characters of the file name. Returns ``None`` when nothing in
    the catalog carries that name - the caller MUST record that as unresolved
    rather than substituting anything.
    """
    stem = declared.rsplit("/", 1)[-1]
    stem = stem.rsplit(".", 1)[0]
    bucket = stem[:2].casefold()
    for extension in _TEXTURE_EXTENSIONS:
        hit = reader.resolve(f"art/compiledtextures/{bucket}/{stem}{extension}".casefold())
        if hit is not None:
            return hit
    for extension in _TEXTURE_EXTENSIONS:
        hit = reader.resolve(f"art/textures/{stem}{extension}".casefold())
        if hit is not None:
            return hit
    # Last resort: any catalog entry whose basename stem matches exactly.
    for extension in _TEXTURE_EXTENSIONS:
        hit = reader.resolve_basename(f"{stem}{extension}")
        if hit is not None:
            return hit
    return None


# --- geometry ----------------------------------------------------------------


@dataclass
class SubObject:
    name: str
    bone_index: int
    bone_name: str
    translation: tuple[float, float, float]
    rotation: tuple[float, float, float, float]
    vertex_count: int
    triangle_count: int
    positions: list[float] = field(default_factory=list)
    normals: list[float] = field(default_factory=list)
    uvs: list[float] = field(default_factory=list)
    indices: list[int] = field(default_factory=list)
    textures: list[str] = field(default_factory=list)
    #: The surface's own colour map: first stage of the first pass, or the
    #: shader material's DiffuseTexture. None when retail binds neither.
    base_texture: str | None = None
    bounds_min: tuple[float, float, float] = (0.0, 0.0, 0.0)
    bounds_max: tuple[float, float, float] = (0.0, 0.0, 0.0)


def _quaternion_matrix(q: tuple[float, float, float, float]):
    x, y, z, w = q
    return (
        (1 - 2 * (y * y + z * z), 2 * (x * y - z * w), 2 * (x * z + y * w)),
        (2 * (x * y + z * w), 1 - 2 * (x * x + z * z), 2 * (y * z - x * w)),
        (2 * (x * z - y * w), 2 * (y * z + x * w), 1 - 2 * (x * x + y * y)),
    )


def _apply(matrix, translation, x: float, y: float, z: float):
    tx, ty, tz = translation
    return (
        matrix[0][0] * x + matrix[0][1] * y + matrix[0][2] * z + tx,
        matrix[1][0] * x + matrix[1][1] * y + matrix[1][2] * z + ty,
        matrix[2][0] * x + matrix[2][1] * y + matrix[2][2] * z + tz,
    )


def _rotate(matrix, x: float, y: float, z: float):
    return (
        matrix[0][0] * x + matrix[0][1] * y + matrix[0][2] * z,
        matrix[1][0] * x + matrix[1][1] * y + matrix[1][2] * z,
        matrix[2][0] * x + matrix[2][1] * y + matrix[2][2] * z,
    )


def decode_livingmap(source: bytes, virtual_path: str = LIVINGMAP_W3D) -> list[SubObject]:
    """Decode every sub-object of the living map into WORLD space.

    Raises ``LivingMapError`` when the model carries a chunk the scanner cannot
    classify, or when a mesh is bound to a bone the hierarchy does not declare.
    A partially-understood map is never returned.
    """
    metadata = scan_w3d_metadata(source, virtual_path)

    flagged = [c for c in metadata.chunks if c.classification in ("unsupported", "unknown")]
    if flagged:
        ids = sorted({hex(c.chunk_id) for c in flagged})
        raise LivingMapError(
            f"{virtual_path} carries {len(flagged)} unsupported/unknown chunks: {ids}"
        )
    truncated = [c for c in metadata.chunks if c.truncated]
    if truncated:
        raise LivingMapError(f"{virtual_path} carries {len(truncated)} truncated chunks")

    children: dict[int, list] = defaultdict(list)
    for chunk in metadata.chunks:
        if chunk.parent_header_offset is not None:
            children[chunk.parent_header_offset].append(chunk)

    def payload(chunk) -> bytes:
        return source[chunk.payload_offset : chunk.payload_offset + chunk.available_payload_size]

    def find(parent_offset: int, chunk_id: int) -> list:
        return [c for c in children.get(parent_offset, []) if c.chunk_id == chunk_id]

    # --- hierarchy pivots ----------------------------------------------------
    pivots: list[tuple[str, int, tuple[float, float, float], tuple[float, float, float, float]]] = []
    for chunk in metadata.chunks:
        if chunk.chunk_id != C_PIVOTS:
            continue
        blob = payload(chunk)
        if len(blob) % PIVOT_STRIDE:
            raise LivingMapError(
                f"pivot chunk is {len(blob)} bytes, not a multiple of {PIVOT_STRIDE}"
            )
        for index in range(len(blob) // PIVOT_STRIDE):
            record = blob[index * PIVOT_STRIDE : (index + 1) * PIVOT_STRIDE]
            name = record[:16].split(b"\x00")[0].decode("ascii", "replace")
            parent = struct.unpack_from("<I", record, 16)[0]
            translation = struct.unpack_from("<3f", record, 20)
            rotation = struct.unpack_from("<4f", record, 44)
            pivots.append((name, parent, translation, rotation))

    bone_of: dict[str, int] = {}
    for reference in metadata.model_references:
        bone_of[reference.identifier.split(".", 1)[-1].upper()] = reference.bone_index

    # --- meshes --------------------------------------------------------------
    result: list[SubObject] = []
    for chunk in metadata.chunks:
        if chunk.chunk_id != C_MESH or not chunk.scanned_as_container:
            continue
        header = next(
            (
                m
                for m in metadata.mesh_headers
                if m.provenance.parent_chunk_header_offset == chunk.header_offset
            ),
            None,
        )
        if header is None:
            raise LivingMapError(f"mesh container at {chunk.header_offset} has no header")

        name = header.mesh_name
        bone_index = bone_of.get(name.upper())
        if bone_index is None:
            raise LivingMapError(f"mesh {name} is not bound by any HLOD sub-object")
        if not 0 <= bone_index < len(pivots):
            raise LivingMapError(
                f"mesh {name} is bound to bone {bone_index}, "
                f"but the hierarchy declares only {len(pivots)} pivots"
            )
        bone_name, _parent, translation, rotation = pivots[bone_index]
        matrix = _quaternion_matrix(rotation)

        vertex_count = header.vertex_count
        triangle_count = header.face_count

        vertex_chunks = find(chunk.header_offset, C_VERTICES)
        if not vertex_chunks:
            raise LivingMapError(f"mesh {name} carries no vertex chunk")
        raw = payload(vertex_chunks[0])
        flat = struct.unpack_from(f"<{vertex_count * 3}f", raw, 0)

        positions: list[float] = []
        minimum = [float("inf")] * 3
        maximum = [float("-inf")] * 3
        for i in range(vertex_count):
            wx, wy, wz = _apply(
                matrix, translation, flat[i * 3], flat[i * 3 + 1], flat[i * 3 + 2]
            )
            positions.extend((wx, wy, wz))
            for axis, value in enumerate((wx, wy, wz)):
                minimum[axis] = min(minimum[axis], value)
                maximum[axis] = max(maximum[axis], value)

        normals: list[float] = []
        normal_chunks = find(chunk.header_offset, C_NORMALS)
        if normal_chunks:
            raw_n = payload(normal_chunks[0])
            flat_n = struct.unpack_from(f"<{vertex_count * 3}f", raw_n, 0)
            for i in range(vertex_count):
                nx, ny, nz = _rotate(
                    matrix, flat_n[i * 3], flat_n[i * 3 + 1], flat_n[i * 3 + 2]
                )
                normals.extend((nx, ny, nz))

        triangle_chunks = find(chunk.header_offset, C_TRIANGLES)
        if not triangle_chunks:
            raise LivingMapError(f"mesh {name} carries no triangle chunk")
        raw_t = payload(triangle_chunks[0])
        # W3dTriStruct: uint32 Vindex[3]; uint32 Attributes; Vector3 Normal;
        # float Dist  => 12 + 4 + 12 + 4 = 32
        stride = 32
        if len(raw_t) < triangle_count * stride:
            raise LivingMapError(
                f"mesh {name} triangle chunk is {len(raw_t)} bytes, "
                f"expected at least {triangle_count * stride}"
            )
        indices: list[int] = []
        for i in range(triangle_count):
            a, b, c = struct.unpack_from("<3I", raw_t, i * stride)
            if max(a, b, c) >= vertex_count:
                raise LivingMapError(
                    f"mesh {name} triangle {i} references vertex "
                    f"{max(a, b, c)} of {vertex_count}"
                )
            indices.extend((a, b, c))

        # --- material pass: texture names and stage-0 texcoords --------------
        table: list[str] = []
        for textures_chunk in find(chunk.header_offset, C_TEXTURES):
            for texture_chunk in find(textures_chunk.header_offset, C_TEXTURE):
                for name_chunk in find(texture_chunk.header_offset, C_TEXTURE_NAME):
                    table.append(
                        payload(name_chunk).split(b"\x00")[0].decode("ascii", "replace")
                    )

        # Retail's living map uses BOTH material paths, and a mesh that only
        # understood one of them would come out untextured for no reason:
        #
        #   legacy pass:   material-pass -> texture-stage -> texture-ids
        #                                                 -> stage-texcoords
        #   shader material: material-pass -> stage-texcoords  (no texture stage)
        #                    shader-materials -> shader-material -> properties
        #
        # `base` is the FIRST texture of the FIRST stage of the FIRST pass, which
        # is the surface's own colour map. Later stages are retail's blend layers;
        # they are recorded but not composited, because this lane does not
        # implement multi-stage blending and faking the blend would be inventing
        # a surface retail never authored that way.
        used: list[str] = []
        base: str | None = None
        uvs: list[float] = []

        def read_texcoords(container_offset: int) -> None:
            nonlocal uvs
            if uvs:
                return
            for texcoord_chunk in find(container_offset, C_STAGE_TEXCOORDS):
                blob_uv = payload(texcoord_chunk)
                if len(blob_uv) // 8 != vertex_count:
                    continue
                flat_uv = struct.unpack_from(f"<{vertex_count * 2}f", blob_uv, 0)
                # W3D V grows upward; Godot/glTF V grows downward.
                for i in range(vertex_count):
                    uvs.extend((flat_uv[i * 2], 1.0 - flat_uv[i * 2 + 1]))
                return

        for material_pass in find(chunk.header_offset, C_MATERIAL_PASS):
            # Shader-material meshes hang their texcoords straight off the pass.
            read_texcoords(material_pass.header_offset)
            for stage in find(material_pass.header_offset, C_TEXTURE_STAGE):
                for ids_chunk in find(stage.header_offset, C_TEXTURE_IDS):
                    blob_ids = payload(ids_chunk)
                    for value in struct.unpack_from(f"<{len(blob_ids) // 4}I", blob_ids, 0):
                        if 0 <= value < len(table):
                            if base is None:
                                base = table[value]
                            if table[value] not in used:
                                used.append(table[value])
                read_texcoords(stage.header_offset)

        # Shader-material diffuse map, when the mesh uses that path.
        for materials_chunk in find(chunk.header_offset, C_SHADER_MATERIALS):
            for material_chunk in find(materials_chunk.header_offset, C_SHADER_MATERIAL):
                for property_chunk in find(
                    material_chunk.header_offset, C_SHADER_MATERIAL_PROPERTY
                ):
                    blob_property = payload(property_chunk)
                    if len(blob_property) < 8:
                        continue
                    # int32 propertyType; uint32 nameLength; char name[];
                    # uint32 valueLength; char value[]
                    name_length = struct.unpack_from("<I", blob_property, 4)[0]
                    if 8 + name_length > len(blob_property):
                        continue
                    property_name = blob_property[8 : 8 + name_length].split(b"\x00")[0]
                    if property_name.decode("ascii", "replace") != SHADER_DIFFUSE_PROPERTY:
                        continue
                    tail = 8 + name_length
                    if tail + 4 > len(blob_property):
                        continue
                    value_length = struct.unpack_from("<I", blob_property, tail)[0]
                    raw_value = blob_property[tail + 4 : tail + 4 + value_length]
                    value = raw_value.split(b"\x00")[0].decode("ascii", "replace")
                    # Retail writes the literal string "None" when a slot is empty.
                    if not value or value == "None":
                        continue
                    if base is None:
                        base = value
                    if value not in used:
                        used.append(value)

        result.append(
            SubObject(
                name=name,
                bone_index=bone_index,
                bone_name=bone_name,
                translation=tuple(translation),
                rotation=tuple(rotation),
                vertex_count=vertex_count,
                triangle_count=triangle_count,
                positions=positions,
                normals=normals,
                uvs=uvs,
                indices=indices,
                textures=used,
                base_texture=base,
                bounds_min=tuple(minimum),
                bounds_max=tuple(maximum),
            )
        )

    result.sort(key=lambda s: s.name)
    return result


# --- the terrain-grid proof ---------------------------------------------------


def terrain_grid_proof(sub_objects: Iterable[SubObject]) -> dict:
    """Measure the terrain tiles' world layout.

    ``LM_01``..``LM_20`` are retail's terrain. If the bone transform above is
    applied correctly they tile a rectangular grid exactly once each; if it is
    applied wrongly they pile up or scatter. This returns the measurement so a
    test can assert on it instead of trusting a comment.
    """
    tiles = [s for s in sub_objects if s.name.startswith("LM_") and s.name[3:].isdigit()]
    if not tiles:
        return {"tileCount": 0, "exact": False, "reason": "no LM_nn terrain tiles found"}

    def cluster(values: Iterable[float], tolerance: float = 2.0) -> list[float]:
        """Collapse tile edges that differ only by authoring noise.

        Adjacent tiles share an edge, but retail's vertices put the two copies a
        fraction of a unit apart (e.g. 828.3 against 828.4). Without clustering
        each shared edge would split into two and the grid would look twice as
        wide as it is.
        """
        ordered = sorted(values)
        grouped: list[float] = []
        for value in ordered:
            if grouped and value - grouped[-1] <= tolerance:
                continue
            grouped.append(value)
        return grouped

    xs = cluster([s.bounds_min[0] for s in tiles] + [s.bounds_max[0] for s in tiles])
    ys = cluster([s.bounds_min[1] for s in tiles] + [s.bounds_max[1] for s in tiles])
    columns = len(xs) - 1
    rows = len(ys) - 1

    # Assign each tile to a cell by its centre.
    occupancy: dict[tuple[int, int], list[str]] = defaultdict(list)
    for tile in tiles:
        cx = (tile.bounds_min[0] + tile.bounds_max[0]) / 2.0
        cy = (tile.bounds_min[1] + tile.bounds_max[1]) / 2.0
        column = max(0, min(columns - 1, sum(1 for v in xs[1:-1] if cx >= v)))
        row = max(0, min(rows - 1, sum(1 for v in ys[1:-1] if cy >= v)))
        occupancy[(column, row)].append(tile.name)

    filled = sum(1 for cell in occupancy.values() if len(cell) == 1)
    collisions = {f"{k[0]},{k[1]}": v for k, v in occupancy.items() if len(v) > 1}
    return {
        "tileCount": len(tiles),
        "columns": columns,
        "rows": rows,
        "columnEdges": xs,
        "rowEdges": ys,
        "cellsOccupiedExactlyOnce": filled,
        "collisions": collisions,
        "exact": filled == len(tiles) and not collisions,
        "extent": {
            "xMin": min(s.bounds_min[0] for s in tiles),
            "xMax": max(s.bounds_max[0] for s in tiles),
            "yMin": min(s.bounds_min[1] for s in tiles),
            "yMax": max(s.bounds_max[1] for s in tiles),
            "zMin": min(s.bounds_min[2] for s in tiles),
            "zMax": max(s.bounds_max[2] for s in tiles),
        },
    }


#: Landmark sub-object -> the living-world region it stands in. Retail states
#: this correspondence nowhere machine-readable, so it is asserted here from the
#: model's own name and checked NUMERICALLY against the document: if a pairing
#: were wrong the measured separation would be thousands of units, not tens.
LANDMARK_REGIONS: Mapping[str, str] = {
    "LM_BLACKGATE": "The_Black_Gate",
    "LM_CIRITHONGUL": "Minas_Morgul",
    "LM_DOLGULDUR": "Dol_Guldur",
    "LM_EREBOR": "Erebor",
    "LM_HELMSDEEP": "Helms_Deep",
    "LM_MINASTIRITH": "Minas_Tirith",
    "LM_ORTHANCTOWER": "Isengard",
    "LM_OSGILLIATH": "Osgiliath",
    "LM_RIVENDELL": "Rivendell",
}


def landmark_agreement(
    sub_objects: Iterable[SubObject], living_world_document: Mapping
) -> dict:
    """Measure how far each landmark's bone sits from its region's centre point.

    This is the evidence that the model and the region document share ONE
    coordinate space. A small separation on a ~6000-unit map means identity; a
    large one would mean a scale or offset this module has not accounted for.
    """
    campaigns = living_world_document.get("regionCampaigns", [])
    regions: dict[str, dict] = {}
    for campaign in campaigns:
        for region in campaign.get("regions", []):
            if region.get("centerPoint"):
                regions.setdefault(str(region["id"]), region)

    by_name = {s.name: s for s in sub_objects}
    rows = []
    worst = 0.0
    for landmark, region_id in sorted(LANDMARK_REGIONS.items()):
        sub_object = by_name.get(landmark)
        region = regions.get(region_id)
        if sub_object is None or region is None:
            rows.append(
                {"landmark": landmark, "region": region_id, "measured": False}
            )
            continue
        centre = region["centerPoint"]
        dx = sub_object.translation[0] - float(centre["x"])
        dy = sub_object.translation[1] - float(centre["y"])
        separation = (dx * dx + dy * dy) ** 0.5
        worst = max(worst, separation)
        rows.append(
            {
                "landmark": landmark,
                "region": region_id,
                "measured": True,
                "bone": [sub_object.translation[0], sub_object.translation[1]],
                "centerPoint": [float(centre["x"]), float(centre["y"])],
                "separation": separation,
            }
        )
    return {
        "pairs": rows,
        "worstSeparation": worst,
        "measuredPairs": sum(1 for r in rows if r.get("measured")),
    }


# --- bundle emission ----------------------------------------------------------


def build_bundle(
    catalog_path: pathlib.Path | str,
    output_root: pathlib.Path | str,
    living_world_document_path: pathlib.Path | str | None = None,
) -> dict:
    """Produce the bundle and return its manifest."""
    reader = CatalogReader(catalog_path)
    output_root = pathlib.Path(output_root)

    entry = reader.resolve(LIVINGMAP_W3D)
    if entry is None:
        raise LivingMapError(f"{LIVINGMAP_W3D} is not in {catalog_path}")
    source = reader.read(entry)

    sub_objects = decode_livingmap(source, entry.name)

    # --- textures --------------------------------------------------------
    declared: list[str] = []
    for sub_object in sub_objects:
        for name in sub_object.textures:
            if name not in declared:
                declared.append(name)
    declared.sort()

    textures_dir = output_root / "textures"
    textures_dir.mkdir(parents=True, exist_ok=True)
    texture_rows = []
    for name in declared:
        hit = resolve_texture(reader, name)
        if hit is None:
            # NOT faked. The Godot side reads resolved=false and draws the
            # sub-object untextured with the reason on screen.
            texture_rows.append({"declared": name, "resolved": False})
            continue
        blob = reader.read(hit)
        file_name = hit.name.rsplit("/", 1)[-1].casefold()
        (textures_dir / file_name).write_bytes(blob)
        texture_rows.append(
            {
                "declared": name,
                "resolved": True,
                "file": f"textures/{file_name}",
                "source": hit.name,
                "archive": hit.archive,
                "bytes": len(blob),
                "sha256": hashlib.sha256(blob).hexdigest(),
                "format": "dds" if blob[:4] == b"DDS " else file_name.rsplit(".", 1)[-1],
            }
        )
    texture_file_by_declared = {
        row["declared"]: row.get("file") for row in texture_rows if row["resolved"]
    }

    # --- mesh.bin --------------------------------------------------------
    blocks = bytearray()
    sub_object_rows = []
    for sub_object in sub_objects:
        position_offset = len(blocks)
        blocks.extend(struct.pack(f"<{len(sub_object.positions)}f", *sub_object.positions))
        normal_offset = len(blocks)
        blocks.extend(struct.pack(f"<{len(sub_object.normals)}f", *sub_object.normals))
        uv_offset = len(blocks)
        blocks.extend(struct.pack(f"<{len(sub_object.uvs)}f", *sub_object.uvs))
        index_offset = len(blocks)
        blocks.extend(struct.pack(f"<{len(sub_object.indices)}I", *sub_object.indices))
        sub_object_rows.append(
            {
                "name": sub_object.name,
                "boneIndex": sub_object.bone_index,
                "boneName": sub_object.bone_name,
                "boneTranslation": list(sub_object.translation),
                "boneRotation": list(sub_object.rotation),
                "vertexCount": sub_object.vertex_count,
                "triangleCount": sub_object.triangle_count,
                "hasNormals": bool(sub_object.normals),
                "hasUVs": bool(sub_object.uvs),
                "positionOffset": position_offset,
                "normalOffset": normal_offset,
                "uvOffset": uv_offset,
                "indexOffset": index_offset,
                "indexCount": len(sub_object.indices),
                "textures": sub_object.textures,
                "baseTexture": sub_object.base_texture,
                "baseTextureFile": texture_file_by_declared.get(sub_object.base_texture),
                "textureFiles": [
                    texture_file_by_declared[t]
                    for t in sub_object.textures
                    if t in texture_file_by_declared
                ],
                "boundsMin": list(sub_object.bounds_min),
                "boundsMax": list(sub_object.bounds_max),
            }
        )

    output_root.mkdir(parents=True, exist_ok=True)
    (output_root / "mesh.bin").write_bytes(bytes(blocks))

    document = {}
    if living_world_document_path is not None:
        document = json.loads(
            pathlib.Path(living_world_document_path).read_text(encoding="utf-8")
        )

    manifest = {
        "schema": SCHEMA,
        "schemaVersion": SCHEMA_VERSION,
        "source": {
            "virtualPath": entry.name,
            "archive": entry.archive,
            "bytes": len(source),
            "sha256": hashlib.sha256(source).hexdigest(),
            "catalog": str(pathlib.Path(catalog_path).name),
        },
        "meshBin": {
            "file": "mesh.bin",
            "bytes": len(blocks),
            "sha256": hashlib.sha256(bytes(blocks)).hexdigest(),
            "byteOrder": "little",
            "positionType": "float32x3",
            "normalType": "float32x3",
            "uvType": "float32x2",
            "indexType": "uint32",
        },
        "coordinateSpace": {
            "note": (
                "Sub-object vertices are stored in RETAIL WORLD UNITS with the "
                "HLOD bone transform already applied. This is the SAME space as "
                "the living-world document's region centerPoint values; the "
                "mapping is the identity. Godot placement is "
                "(x, z, y) -> (x, z_world_height, -y)."
            ),
            "terrainGridProof": terrain_grid_proof(sub_objects),
            "landmarkAgreement": landmark_agreement(sub_objects, document)
            if document
            else {"measuredPairs": 0},
        },
        "subObjects": sub_object_rows,
        "textures": texture_rows,
        "totals": {
            "subObjects": len(sub_objects),
            "vertices": sum(s.vertex_count for s in sub_objects),
            "triangles": sum(s.triangle_count for s in sub_objects),
            "texturesDeclared": len(texture_rows),
            "texturesResolved": sum(1 for r in texture_rows if r["resolved"]),
        },
    }
    (output_root / "manifest.json").write_text(
        json.dumps(manifest, indent=2, sort_keys=True), encoding="utf-8"
    )
    return manifest


def main(argv: list[str] | None = None) -> int:
    import argparse

    parser = argparse.ArgumentParser(
        prog="python -m openbfme_importer.livingmap_bundle",
        description="Convert retail's living map W3D into a Godot-loadable bundle.",
    )
    parser.add_argument("catalog", type=pathlib.Path, help="path to catalog/<game>.json")
    parser.add_argument("output", type=pathlib.Path, help="bundle output directory")
    parser.add_argument(
        "--living-world-document",
        type=pathlib.Path,
        default=None,
        help="living-world JSON, used to measure landmark/region agreement",
    )
    args = parser.parse_args(argv)

    manifest = build_bundle(args.catalog, args.output, args.living_world_document)
    totals = manifest["totals"]
    proof = manifest["coordinateSpace"]["terrainGridProof"]
    agreement = manifest["coordinateSpace"]["landmarkAgreement"]
    print(f"living map bundle -> {args.output}")
    print(
        f"  sub-objects {totals['subObjects']}  vertices {totals['vertices']}  "
        f"triangles {totals['triangles']}"
    )
    print(
        f"  textures {totals['texturesResolved']}/{totals['texturesDeclared']} resolved"
    )
    print(
        f"  terrain grid {proof.get('columns')}x{proof.get('rows')} "
        f"tiles={proof.get('tileCount')} exact={proof.get('exact')}"
    )
    if agreement.get("measuredPairs"):
        print(
            f"  landmark agreement: {agreement['measuredPairs']} pairs, "
            f"worst separation {agreement['worstSeparation']:.1f} world units"
        )
    unresolved = [r["declared"] for r in manifest["textures"] if not r["resolved"]]
    if unresolved:
        print(f"  UNRESOLVED textures (drawn untextured, not faked): {unresolved}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
