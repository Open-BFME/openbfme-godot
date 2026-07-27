extends RefCounted

## RETAIL'S STRATEGIC MAP, LOADED. This reads the `openbfme.living-map` bundle
## produced by `openbfme_importer.livingmap_bundle` from retail's own
## `art/w3d/li/livingmap.w3d` and turns it into Godot meshes and materials.
##
## WHAT IT IS NOT: a model format, a converter, or a guess. Every vertex in
## `mesh.bin` is a retail vertex with retail's own HLOD bone transform applied,
## and every texture is retail's compiled payload byte-for-byte. Godot 4.7 reads
## the DDS payloads directly, so nothing is transcoded on the way in either.
##
## THE RULES IT KEEPS:
##
## * FAIL LOUDLY, NEVER PARTIALLY. `load_from()` returns false and fills
##   `errors` the moment the manifest, the mesh block or a declared sub-object
##   does not add up. A half-loaded map is never handed back, because a map
##   missing a third of Middle-earth still looks like a map.
##
## * A TEXTURE THAT DID NOT RESOLVE IS SAID, NOT SUBSTITUTED. The converter
##   marks unresolved textures in the manifest; this file carries that through to
##   `untextured_sub_objects` and gives the sub-object a plain material. Nothing
##   stands in for retail art.
##
## * IT LOADS NOTHING STRATEGIC. This file knows about geometry and images. It
##   has no idea what a region, an army or a commitment is, and cannot reach one.

## The bundle schema this loader understands, and nothing else.
const SCHEMA := "openbfme.living-map"
const SCHEMA_VERSION := 1

## The environment override that points at a bundle, mirroring the
## `OPENBFME_LIVING_WORLD_DOC` convention the strategic lane already documents.
const BUNDLE_ENV := "OPENBFME_LIVING_MAP"
## The bundle location inside a content pack, once one ships it.
const PACK_BUNDLE_RELATIVE := "data/living-map"
## Where a workspace bundle lands when the owner has not set the variable.
const USER_BUNDLE := "user://livingmap"

const MANIFEST_MAX_BYTES := 8 * 1024 * 1024
const MESH_MAX_BYTES := 256 * 1024 * 1024
const TEXTURE_MAX_BYTES := 64 * 1024 * 1024

## Sub-objects that are retail's ATMOSPHERE rather than its ground: smoke cards,
## the cloud layer, the border cloud, the lava planes and the text plane. They
## are loaded and reported like everything else, but the strategic view leaves
## them hidden by default because each one is a camera-facing card that retail
## animates and this lane does not animate anything yet. Drawing a static smoke
## card over Orthanc would be worse than drawing none.
const AMBIENT_SUB_OBJECTS := [
	"BORDERCLOUD", "LAVAPLANE1", "LAVAPLANE3", "LM_CLOUDLAYER",
	"LM_LAVAVENTSMOK", "LM_SMOKE01", "LM_SMOKE02", "LM_SMOKE03",
	"LM_SMOKE04", "LM_SMOKE05", "PLANE03", "TEXT PLANE",
]

## Retail's shoreline overlay strips. Each one is a MULTI-STAGE blend - a
## reflection map in stage 0 and an animated whitecap in stage 1 - laid over a
## coastline the terrain textures already draw in full. This lane composites one
## stage, so drawing them puts stage 0's night-sky reflection along the shore as
## an opaque band: a surface retail never shows anyone. Not drawn, and named.
## `RIVERS` belongs here too, for the same reason and one more: it is a full-map
## plane (X -2973..3416, Y -1373..3472) parked at z -124.8, a hair BELOW the
## terrain's lowest vertex at -123.3. Retail scrolls two ocean textures across it
## and lets it show through the riverbeds. Drawn opaque and unanimated it is
## invisible under the terrain except where it protrudes past the map edge, where
## it becomes two striped blue bands that are not part of Middle-earth.
const COAST_OVERLAY_SUB_OBJECTS := [
	"LM_COAST", "LM_COAST01", "LM_COAST02", "LM_COAST03", "LM_COAST04",
	"LM_COAST05", "RIVERS",
]

## Sub-objects retail draws with a SHADER this lane does not implement, and which
## therefore have no colour map to bind at all. `WATER` is the whole ocean plane
## and retail renders it with `WaterShader.FX` - a procedural shader whose only
## authored property is a dimming factor, with no diffuse texture anywhere in the
## model. Drawing it as a flat grey slab would read as land, which is precisely
## the kind of confident wrong answer this project refuses. It is loaded and
## named; it is not drawn.
const SHADER_ONLY_SUB_OBJECTS := ["WATER"]

## Sub-objects retail uses as INVISIBLE gameplay volumes - the impassable
## barriers armies cannot cross. They carry no texture and no UVs because they
## were never meant to be seen. Hidden, not deleted: they are part of the map and
## a later lane will want them for movement rules.
const COLLISION_PREFIX := "IMPASSABLE"

var loaded := false
var bundle_root := ""
## Every refusal, in order. Never empty on a false return from `load_from()`.
var errors: PackedStringArray = PackedStringArray()

## Sub-object rows: {name, mesh: ArrayMesh, material, textured, ambient,
## collision, vertex_count, triangle_count, bounds_min, bounds_max}.
var sub_objects: Array[Dictionary] = []
var _by_name: Dictionary = {}

## Declared textures the converter could not resolve in the catalog. Non-empty
## means those sub-objects are drawn untextured and the screen SAYS so.
var untextured_sub_objects: PackedStringArray = PackedStringArray()
## Sub-objects retail draws with a shader this lane does not implement, so they
## are not drawn at all. Named on screen rather than silently missing.
var shader_only_sub_objects: PackedStringArray = PackedStringArray()
var unresolved_textures: PackedStringArray = PackedStringArray()

## The terrain extent in RETAIL WORLD UNITS: {x_min, x_max, y_min, y_max,
## z_min, z_max}. This is the frame the camera is fitted to and the space the
## living-world document's region centre points live in.
var terrain_extent: Dictionary = {}
## The converter's own measurement that the grid tiles exactly. Carried through
## so a test can assert it without re-deriving anything.
var terrain_grid_proof: Dictionary = {}
var landmark_agreement: Dictionary = {}
var source_sha256 := ""

## Terrain triangles, kept for height sampling. Grouped by the tile they came
## from so a lookup touches one tile rather than all twenty.
var _terrain_tiles: Array[Dictionary] = []


## Where to look for a bundle, in the order the strategic lane searches: the
## packs the game actually mounted, then the documented environment override,
## then the user-data workspace location.
static func candidate_roots(pack_roots: Array = []) -> PackedStringArray:
	var candidates := PackedStringArray()
	for root_value in pack_roots:
		var root := String(root_value).strip_edges()
		if not root.is_empty():
			candidates.append(root.path_join(PACK_BUNDLE_RELATIVE))
	var override := OS.get_environment(BUNDLE_ENV).strip_edges()
	if not override.is_empty():
		candidates.append(override)
	candidates.append(USER_BUNDLE)
	return candidates


## Load the first candidate that carries a manifest. Returns `{ok, root, reason}`
## and names every place it looked when it finds nothing - "no map" alone sends
## nobody anywhere.
func locate_and_load(pack_roots: Array = []) -> Dictionary:
	var searched: Array[String] = []
	for root in candidate_roots(pack_roots):
		searched.append(root)
		if not FileAccess.file_exists(root.path_join("manifest.json")):
			continue
		if load_from(root):
			return {"ok": true, "root": root, "reason": ""}
		return {
			"ok": false,
			"root": root,
			"reason": "The living-map bundle at %s could not be read: %s" % [
				root, ", ".join(Array(errors))],
		}
	return {
		"ok": false,
		"root": "",
		"reason": (
			"No living-map bundle was found. Retail's 3D strategic map is "
			+ "converted by `python -m openbfme_importer.livingmap_bundle "
			+ "<catalog>/rotwk.json <bundle-dir>`; point %s at the result. "
			+ "Searched: %s"
		) % [BUNDLE_ENV, ", ".join(searched)],
	}


func load_from(root: String) -> bool:
	_reset()
	bundle_root = root

	var manifest_path := root.path_join("manifest.json")
	var manifest_file := FileAccess.open(manifest_path, FileAccess.READ)
	if manifest_file == null:
		return _fail("cannot open %s" % manifest_path)
	if manifest_file.get_length() > MANIFEST_MAX_BYTES:
		return _fail("manifest is %d bytes, over the %d limit" % [
			manifest_file.get_length(), MANIFEST_MAX_BYTES])
	var parsed: Variant = JSON.parse_string(manifest_file.get_as_text())
	manifest_file.close()
	if not (parsed is Dictionary):
		return _fail("manifest is not a JSON object")
	var manifest: Dictionary = parsed

	if String(manifest.get("schema", "")) != SCHEMA:
		return _fail("manifest schema is not %s" % SCHEMA)
	if int(manifest.get("schemaVersion", -1)) != SCHEMA_VERSION:
		return _fail("unsupported schemaVersion %d" % int(manifest.get("schemaVersion", -1)))

	var source: Dictionary = manifest.get("source", {}) as Dictionary
	source_sha256 = String(source.get("sha256", ""))

	var coordinate_space: Dictionary = manifest.get("coordinateSpace", {}) as Dictionary
	terrain_grid_proof = coordinate_space.get("terrainGridProof", {}) as Dictionary
	landmark_agreement = coordinate_space.get("landmarkAgreement", {}) as Dictionary
	var extent: Dictionary = terrain_grid_proof.get("extent", {}) as Dictionary
	if extent.is_empty():
		return _fail("manifest carries no terrain extent")
	terrain_extent = {
		"x_min": float(extent.get("xMin", 0.0)), "x_max": float(extent.get("xMax", 0.0)),
		"y_min": float(extent.get("yMin", 0.0)), "y_max": float(extent.get("yMax", 0.0)),
		"z_min": float(extent.get("zMin", 0.0)), "z_max": float(extent.get("zMax", 0.0)),
	}
	if terrain_extent["x_max"] <= terrain_extent["x_min"]:
		return _fail("terrain extent has no width")

	# --- mesh block ------------------------------------------------------
	var mesh_info: Dictionary = manifest.get("meshBin", {}) as Dictionary
	var mesh_path := root.path_join(String(mesh_info.get("file", "mesh.bin")))
	var mesh_file := FileAccess.open(mesh_path, FileAccess.READ)
	if mesh_file == null:
		return _fail("cannot open %s" % mesh_path)
	var declared_bytes := int(mesh_info.get("bytes", -1))
	if mesh_file.get_length() > MESH_MAX_BYTES:
		return _fail("mesh block is %d bytes, over the %d limit" % [
			mesh_file.get_length(), MESH_MAX_BYTES])
	if declared_bytes >= 0 and mesh_file.get_length() != declared_bytes:
		return _fail("mesh block is %d bytes, manifest declares %d" % [
			mesh_file.get_length(), declared_bytes])
	var blob := mesh_file.get_buffer(mesh_file.get_length())
	mesh_file.close()

	# --- textures --------------------------------------------------------
	var texture_by_declared: Dictionary = {}
	var missing: Array[String] = []
	for row_value in manifest.get("textures", []) as Array:
		var row := row_value as Dictionary
		var declared := String(row.get("declared", ""))
		if not bool(row.get("resolved", false)):
			missing.append(declared)
			continue
		var image := _load_image(root.path_join(String(row.get("file", ""))))
		if image == null:
			# A texture the converter DID resolve but this loader cannot decode is
			# a real defect, not a missing asset. Say which, and carry on
			# untextured rather than pretending the pixel data exists.
			missing.append("%s (present but undecodable)" % declared)
			continue
		texture_by_declared[declared] = ImageTexture.create_from_image(image)
	missing.sort()
	unresolved_textures = PackedStringArray(missing)

	# --- sub-objects -----------------------------------------------------
	var rows: Array = manifest.get("subObjects", []) as Array
	if rows.is_empty():
		return _fail("manifest declares no sub-objects")
	var untextured: Array[String] = []
	for row_value in rows:
		var row := row_value as Dictionary
		var built := _build_sub_object(row, blob, texture_by_declared)
		if built.is_empty():
			return false
		sub_objects.append(built)
		_by_name[String(built["name"])] = built
		var reportable := not bool(built["collision"]) and not bool(built["shader_only"])
		if reportable and not bool(built["textured"]):
			untextured.append(String(built["name"]))
	untextured.sort()
	untextured_sub_objects = PackedStringArray(untextured)
	var shader_only: Array[String] = []
	for entry in sub_objects:
		if bool(entry["shader_only"]):
			shader_only.append(entry["name"] as String)
	shader_only.sort()
	shader_only_sub_objects = PackedStringArray(shader_only)

	loaded = true
	return true


func sub_object(name: String) -> Dictionary:
	return _by_name.get(name, {}) as Dictionary


## Retail world (x, y, z) to Godot 3D. Retail's strategic map is Z-up with Y
## growing north; Godot is Y-up with -Z forward. ONE conversion, stated once,
## used by every caller so a marker and the ground it stands on cannot disagree.
static func world_to_godot(x: float, y: float, z: float) -> Vector3:
	return Vector3(x, z, -y)


## Terrain height at a retail world (x, y), sampled from retail's own terrain
## triangles. Returns `{ok, height}`. This DERIVES a value from shipped geometry;
## it does not invent one, and when no triangle covers the point it says so
## rather than returning a plausible number.
func sample_height(x: float, y: float) -> Dictionary:
	for tile in _terrain_tiles:
		var bounds_min: Vector3 = tile["bounds_min"]
		var bounds_max: Vector3 = tile["bounds_max"]
		if x < bounds_min.x or x > bounds_max.x or y < bounds_min.y or y > bounds_max.y:
			continue
		var positions: PackedFloat32Array = tile["positions"]
		var indices: PackedInt32Array = tile["indices"]
		var count := indices.size()
		var i := 0
		while i < count:
			var a := indices[i] * 3
			var b := indices[i + 1] * 3
			var c := indices[i + 2] * 3
			var ax := positions[a]
			var ay := positions[a + 1]
			var bx := positions[b]
			var by := positions[b + 1]
			var cx := positions[c]
			var cy := positions[c + 1]
			var area := (by - cy) * (ax - cx) + (cx - bx) * (ay - cy)
			if absf(area) > 0.000001:
				var w0 := ((by - cy) * (x - cx) + (cx - bx) * (y - cy)) / area
				var w1 := ((cy - ay) * (x - cx) + (ax - cx) * (y - cy)) / area
				var w2 := 1.0 - w0 - w1
				if w0 >= 0.0 and w1 >= 0.0 and w2 >= 0.0:
					return {
						"ok": true,
						"height": w0 * positions[a + 2] + w1 * positions[b + 2] + w2 * positions[c + 2],
					}
			i += 3
	return {"ok": false, "height": 0.0}


# --- internals ---------------------------------------------------------------

func _reset() -> void:
	loaded = false
	bundle_root = ""
	errors = PackedStringArray()
	sub_objects = []
	_by_name = {}
	_terrain_tiles = []
	untextured_sub_objects = PackedStringArray()
	shader_only_sub_objects = PackedStringArray()
	unresolved_textures = PackedStringArray()
	terrain_extent = {}
	terrain_grid_proof = {}
	landmark_agreement = {}
	source_sha256 = ""


func _fail(message: String) -> bool:
	errors.append(message)
	return false


func _load_image(path: String) -> Image:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return null
	if file.get_length() > TEXTURE_MAX_BYTES:
		file.close()
		return null
	var bytes := file.get_buffer(file.get_length())
	file.close()
	var image := Image.new()
	var extension := path.get_extension().to_lower()
	var status := ERR_FILE_UNRECOGNIZED
	# Retail ships compiled textures as DXT-compressed DDS with a handful of
	# JPEGs. Godot 4.7 reads both from a buffer, so the bytes are never rewritten.
	if extension == "dds":
		status = image.load_dds_from_buffer(bytes)
	elif extension == "jpg" or extension == "jpeg":
		status = image.load_jpg_from_buffer(bytes)
	elif extension == "tga":
		status = image.load_tga_from_buffer(bytes)
	elif extension == "png":
		status = image.load_png_from_buffer(bytes)
	if status != OK:
		return null
	return image


func _build_sub_object(row: Dictionary, blob: PackedByteArray, textures: Dictionary) -> Dictionary:
	var name := String(row.get("name", ""))
	if name.is_empty():
		_fail("sub-object without a name")
		return {}
	var vertex_count := int(row.get("vertexCount", 0))
	var index_count := int(row.get("indexCount", 0))
	if vertex_count <= 0 or index_count <= 0:
		_fail("sub-object %s declares %d vertices and %d indices" % [
			name, vertex_count, index_count])
		return {}

	var position_offset := int(row.get("positionOffset", -1))
	var normal_offset := int(row.get("normalOffset", -1))
	var uv_offset := int(row.get("uvOffset", -1))
	var index_offset := int(row.get("indexOffset", -1))
	var position_bytes := vertex_count * 3 * 4
	var index_bytes := index_count * 4
	if position_offset < 0 or position_offset + position_bytes > blob.size():
		_fail("sub-object %s positions run past the mesh block" % name)
		return {}
	if index_offset < 0 or index_offset + index_bytes > blob.size():
		_fail("sub-object %s indices run past the mesh block" % name)
		return {}

	var positions := blob.slice(position_offset, position_offset + position_bytes).to_float32_array()
	var indices := blob.slice(index_offset, index_offset + index_bytes).to_int32_array()
	for index in indices:
		if index < 0 or index >= vertex_count:
			_fail("sub-object %s references vertex %d of %d" % [name, index, vertex_count])
			return {}

	var vertices := PackedVector3Array()
	vertices.resize(vertex_count)
	for i in vertex_count:
		vertices[i] = world_to_godot(
			positions[i * 3], positions[i * 3 + 1], positions[i * 3 + 2])

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices

	if bool(row.get("hasNormals", false)):
		var normal_bytes := vertex_count * 3 * 4
		if normal_offset < 0 or normal_offset + normal_bytes > blob.size():
			_fail("sub-object %s normals run past the mesh block" % name)
			return {}
		var raw := blob.slice(normal_offset, normal_offset + normal_bytes).to_float32_array()
		var normals := PackedVector3Array()
		normals.resize(vertex_count)
		for i in vertex_count:
			normals[i] = world_to_godot(raw[i * 3], raw[i * 3 + 1], raw[i * 3 + 2]).normalized()
		arrays[Mesh.ARRAY_NORMAL] = normals

	var textured := false
	if bool(row.get("hasUVs", false)):
		var uv_bytes := vertex_count * 2 * 4
		if uv_offset < 0 or uv_offset + uv_bytes > blob.size():
			_fail("sub-object %s texture coordinates run past the mesh block" % name)
			return {}
		var raw_uv := blob.slice(uv_offset, uv_offset + uv_bytes).to_float32_array()
		var uvs := PackedVector2Array()
		uvs.resize(vertex_count)
		for i in vertex_count:
			uvs[i] = Vector2(raw_uv[i * 2], raw_uv[i * 2 + 1])
		arrays[Mesh.ARRAY_TEX_UV] = uvs

	# Retail winds its triangles the other way round from Godot's front face.
	var flipped := PackedInt32Array()
	flipped.resize(index_count)
	var triangle := 0
	while triangle * 3 + 2 < index_count:
		flipped[triangle * 3] = indices[triangle * 3]
		flipped[triangle * 3 + 1] = indices[triangle * 3 + 2]
		flipped[triangle * 3 + 2] = indices[triangle * 3 + 1]
		triangle += 1
	arrays[Mesh.ARRAY_INDEX] = flipped

	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

	var material := StandardMaterial3D.new()
	# UNSHADED, deliberately. Retail's living map is painted art: the terrain
	# textures already carry their own light, shadow and relief, baked in by the
	# artist. Lighting them again would show the map under a lighting rig retail
	# never had and this lane would have invented - and it made whole terrain
	# tiles read as black where their authored normals point away from a sun that
	# does not exist. Unshaded shows retail's pixels as retail authored them.
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	# The BASE texture is the surface's own colour map - the first stage of the
	# first material pass, or a shader material's DiffuseTexture. Retail's later
	# stages are blend layers this lane does not composite; binding one of those
	# as if it were the base is what put night-sky clouds on a coastline.
	var candidates: Array = []
	# `baseTexture` is JSON null for a sub-object retail binds no colour map to -
	# the impassable volumes and the ocean. Null is the honest value there, so it
	# is read as "none" rather than coerced.
	var base_value: Variant = row.get("baseTexture", null)
	var base_texture := "" if base_value == null else String(base_value)
	if not base_texture.is_empty():
		candidates.append(base_texture)
	for declared_value in row.get("textures", []) as Array:
		if String(declared_value) != base_texture:
			candidates.append(String(declared_value))
	for declared_value in candidates:
		var texture: ImageTexture = textures.get(String(declared_value), null)
		if texture != null:
			material.albedo_texture = texture
			textured = true
			break
	if not textured:
		# NOT a stand-in for retail art: a flat, obviously-untextured grey that
		# reads as "this surface has no shipped texture bound", which is the truth
		# for retail's shader-material and collision sub-objects.
		material.albedo_color = Color(0.32, 0.34, 0.30)

	var collision := name.begins_with(COLLISION_PREFIX)
	var built := {
		"name": name,
		"mesh": mesh,
		"material": material,
		"textured": textured,
		"ambient": AMBIENT_SUB_OBJECTS.has(name) or COAST_OVERLAY_SUB_OBJECTS.has(name),
		"collision": collision,
		"shader_only": SHADER_ONLY_SUB_OBJECTS.has(name),
		"vertex_count": vertex_count,
		"triangle_count": int(row.get("triangleCount", 0)),
		"bounds_min": _vector_of(row.get("boundsMin", [])),
		"bounds_max": _vector_of(row.get("boundsMax", [])),
	}

	# Terrain tiles are kept in retail world coordinates for height sampling.
	if name.begins_with("LM_") and name.substr(3).is_valid_int():
		_terrain_tiles.append({
			"name": name,
			"positions": positions,
			"indices": indices,
			"bounds_min": built["bounds_min"],
			"bounds_max": built["bounds_max"],
		})
	return built


func _vector_of(value: Variant) -> Vector3:
	var list := value as Array
	if list == null or list.size() < 3:
		return Vector3.ZERO
	return Vector3(float(list[0]), float(list[1]), float(list[2]))
