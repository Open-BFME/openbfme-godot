extends RefCounted

## RETAIL'S REGION TERRITORY GEOMETRY, loaded from the bundle
## `openbfme_importer.livingmap_regions` writes.
##
## WHY THIS EXISTS. The strategic screen drew each region as a coloured DOT on
## retail's 3D map. Retail draws FILLED TERRITORIES with visible borders, and the
## difference is the single largest visual gap between this screen and the real
## game.
##
## A prior lane established, correctly, that `livingmap.w3d` carries no
## per-region sub-objects - its 64 meshes are terrain tiles, coast, water, the
## impassable volumes, the ambient cards and eleven landmarks. The conclusion
## drawn from that (that retail's territory shapes are not in the files we have)
## was wrong, and retail's own data says where they are. The living-world
## document's `regionEffects` block - converted from
## `data/ini/livingworldregioneffects.ini` - names the geometry by name:
##
##     regionObject = LMR_Fill
##     BordersEffect            geometry LMR_Border
##     FilledOwnershipEffect    geometry LMR_Fill
##     UnifiedEffect            geometry LMR_RegFill, LMR_RegEdge
##
## Those are separate W3D models under `art/w3d/lm/`, each carrying ONE MESH PER
## REGION whose mesh name is the region's `SubObject` id. `lmr_fill.w3d` carries
## 62 (51 regions plus 11 impassable land masses), `lmr_border.w3d` 52 outlines,
## `lmr_regfill.w3d` the seven TERRITORY groups. They land in the SAME world-unit
## space as the living map, so nothing here fits, scales or registers anything.
##
## WHAT IT REFUSES TO DO. It never invents geometry. A region the bundle carries
## no mesh for is reported in `regions_without_geometry` and DRAWN AS NOTHING,
## and the screen names it. It never substitutes one region's shape for another,
## however close the names look: retail spells a region `Rhudaur` and its mesh
## `Rhudaul`, and only the document's own `subObject` link is allowed to bridge
## that - never a guess made here.

const SCHEMA := "openbfme.living-map-regions"
const SCHEMA_VERSION := 1

## Where to look for a bundle, in the order tried. Same shape as the living
## map's search so the two are configured the same way.
const BUNDLE_ENV := "OPENBFME_LIVING_MAP_REGIONS"
const PACK_BUNDLE_RELATIVE := "data/living-map-regions"
const USER_BUNDLE := "user://livingmap-regions"
## The living-map bundle's own directory usually has this one beside it, which is
## how a single `OPENBFME_LIVING_MAP` setting brings both.
const SIBLING_SUFFIX := "-regions"

const MANIFEST_MAX_BYTES := 16 * 1024 * 1024
const MESH_MAX_BYTES := 256 * 1024 * 1024

var loaded := false
var bundle_root := ""
var errors: PackedStringArray = PackedStringArray()
var searched_roots: Array[Dictionary] = []

## Every mesh record, in manifest order.
var records: Array[Dictionary] = []
## `region id -> {fill: ArrayMesh, border: ArrayMesh, ...}` for regions the
## bundle carries geometry for.
var by_region: Dictionary = {}
## `territory name -> ArrayMesh` from `lmr_regfill.w3d`.
var by_territory: Dictionary = {}
## Region ids the DOCUMENT declares that the bundle has no mesh for. Reported so
## the screen can say which regions are unshaded rather than leaving a silent
## hole in Middle-earth.
var regions_without_geometry: PackedStringArray = PackedStringArray()
## `region id -> Vector2`, the AREA-WEIGHTED CENTROID of that region's fill mesh,
## derived by the converter from retail's own triangles. This is what lets a
## region retail never gave a `CustomCenterPoint` be placed from shipped geometry
## instead of being listed as unplaceable.
var derived_centroids: Dictionary = {}

var layers_present: PackedStringArray = PackedStringArray()
var total_triangles := 0
var source_summary: Array[Dictionary] = []


static func candidate_sources(pack_roots: Array = []) -> Array[Dictionary]:
	var candidates: Array[Dictionary] = []
	var override := OS.get_environment(BUNDLE_ENV).strip_edges()
	if not override.is_empty():
		candidates.append({"root": override, "origin": BUNDLE_ENV})
	# Beside the living map, wherever that was found. One setting, both bundles.
	var map_override := OS.get_environment("OPENBFME_LIVING_MAP").strip_edges()
	if not map_override.is_empty():
		candidates.append({
			"root": map_override.trim_suffix("/").trim_suffix("\\") + SIBLING_SUFFIX,
			"origin": "OPENBFME_LIVING_MAP + %s" % SIBLING_SUFFIX,
		})
	for root in pack_roots:
		candidates.append({
			"root": String(root).path_join(PACK_BUNDLE_RELATIVE),
			"origin": "mounted pack",
		})
	candidates.append({"root": USER_BUNDLE, "origin": "user data"})
	return candidates


static func probe(pack_roots: Array = []) -> Dictionary:
	var rows: Array[Dictionary] = []
	for candidate in candidate_sources(pack_roots):
		var root := String(candidate["root"])
		var present := FileAccess.file_exists(root.path_join("manifest.json"))
		rows.append({"root": root, "origin": String(candidate["origin"]), "present": present})
		if present:
			return {"found": true, "root": root, "origin": String(candidate["origin"]), "rows": rows}
	return {"found": false, "root": "", "origin": "", "rows": rows}


## Find and load a bundle. Returns `{ok, root, origin, reason}`. A failure is
## always a REASON naming every path tried, never a bare false.
func locate_and_load(pack_roots: Array = []) -> Dictionary:
	searched_roots = []
	for candidate in candidate_sources(pack_roots):
		var root := String(candidate["root"])
		var origin := String(candidate["origin"])
		if not FileAccess.file_exists(root.path_join("manifest.json")):
			searched_roots.append({"root": root, "origin": origin, "present": false, "loaded": false})
			continue
		if load_from(root):
			searched_roots.append({"root": root, "origin": origin, "present": true, "loaded": true})
			return {"ok": true, "root": root, "origin": origin, "reason": ""}
		searched_roots.append({
			"root": root, "origin": origin, "present": true, "loaded": false,
			"errors": errors.duplicate(),
		})
	return {"ok": false, "root": "", "origin": "", "reason": describe_search_failure()}


func describe_search_failure() -> String:
	var lines: Array[String] = []
	lines.append(
		"NO REGION TERRITORY GEOMETRY, so regions are drawn as markers instead of "
		+ "filled territories. Retail fills each region with its owner's colour "
		+ "using art/w3d/lm/lmr_fill.w3d; nothing has converted it here.")
	for row in searched_roots:
		var root := String(row["root"])
		var origin := String(row["origin"])
		if not bool(row["present"]):
			lines.append("  %s [%s] - no manifest.json" % [root, origin])
			continue
		lines.append("  %s [%s] - PRESENT BUT REFUSED: %s" % [
			root, origin, ", ".join(Array(row.get("errors", PackedStringArray())))])
	lines.append(
		"Produce one with: python -m openbfme_importer.livingmap_regions "
		+ "--catalog <catalog>.json --out <dir> --document <living-world>.json, "
		+ "then point %s at <dir>." % BUNDLE_ENV)
	return "\n".join(lines)


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

	var mesh_info: Dictionary = manifest.get("meshBin", {}) as Dictionary
	var mesh_path := root.path_join(String(mesh_info.get("file", "regions.bin")))
	var mesh_file := FileAccess.open(mesh_path, FileAccess.READ)
	if mesh_file == null:
		return _fail("cannot open %s" % mesh_path)
	if mesh_file.get_length() > MESH_MAX_BYTES:
		return _fail("geometry block is %d bytes, over the %d limit" % [
			mesh_file.get_length(), MESH_MAX_BYTES])
	var declared := int(mesh_info.get("bytes", -1))
	if declared >= 0 and mesh_file.get_length() != declared:
		return _fail("geometry block is %d bytes, manifest declares %d" % [
			mesh_file.get_length(), declared])
	var blob := mesh_file.get_buffer(mesh_file.get_length())
	mesh_file.close()

	var rows: Array = manifest.get("records", []) as Array
	if rows.is_empty():
		return _fail("manifest declares no meshes")
	var layers: Dictionary = {}
	for row_value in rows:
		var row := row_value as Dictionary
		var built := _build_mesh(row, blob)
		if built == null:
			return false
		var layer := String(row.get("layer", ""))
		layers[layer] = true
		records.append(row)
		total_triangles += int(row.get("triangleCount", 0))

		if layer == "territory":
			var territory := _text(row, "territoryName")
			if not territory.is_empty():
				by_territory[territory] = built
			continue
		var region_id := _text(row, "regionId")
		if region_id.is_empty():
			# An impassable land mass, or a mesh no document region claims. Kept
			# in `records` (so the count is honest) and bound to no region.
			continue
		var slot: Dictionary = by_region.get(region_id, {}) as Dictionary
		slot[layer] = built
		by_region[region_id] = slot
		var centroid_value: Variant = row.get("derivedCentroid", null)
		if layer == "fill" and centroid_value is Array and (centroid_value as Array).size() >= 2:
			derived_centroids[region_id] = Vector2(
				float((centroid_value as Array)[0]), float((centroid_value as Array)[1]))

	var layer_names: Array[String] = []
	for key in layers.keys():
		layer_names.append(String(key))
	layer_names.sort()
	layers_present = PackedStringArray(layer_names)

	var document: Dictionary = manifest.get("document", {}) as Dictionary
	var missing: Array[String] = []
	for value in document.get("unmatchedRegions", []) as Array:
		missing.append(String(value))
	missing.sort()
	regions_without_geometry = PackedStringArray(missing)

	for value in manifest.get("sources", []) as Array:
		source_summary.append(value as Dictionary)

	loaded = true
	return true


## One line per converted model, for the screen's provenance line.
func describe_load() -> PackedStringArray:
	var lines: Array[String] = []
	for row in source_summary:
		lines.append("%s <- %s (%d meshes, %d bytes)" % [
			String(row.get("layer", "?")), String(row.get("virtualPath", "?")),
			int(row.get("meshes", 0)), int(row.get("bytes", 0))])
	return PackedStringArray(lines)


func region_mesh(region_id: String, layer: String) -> ArrayMesh:
	var slot: Dictionary = by_region.get(region_id, {}) as Dictionary
	return slot.get(layer, null) as ArrayMesh


func has_region(region_id: String) -> bool:
	return by_region.has(region_id)


func shaded_region_count() -> int:
	var count := 0
	for key in by_region.keys():
		if (by_region[key] as Dictionary).has("fill"):
			count += 1
	return count


# --- internals ----------------------------------------------------------------

func _reset() -> void:
	loaded = false
	errors = PackedStringArray()
	records = []
	by_region = {}
	by_territory = {}
	derived_centroids = {}
	regions_without_geometry = PackedStringArray()
	layers_present = PackedStringArray()
	source_summary = []
	total_triangles = 0


## A manifest string field, treating JSON `null` as absent. The converter writes
## `"regionId": null` for a mesh no document region claims - an impassable land
## mass, or a region set this document does not carry - and `String(null)` is a
## runtime error in GDScript, not an empty string.
func _text(row: Dictionary, key: String) -> String:
	var value: Variant = row.get(key, null)
	return "" if value == null else String(value)


func _fail(message: String) -> bool:
	errors.append(message)
	loaded = false
	return false


## Build one ArrayMesh from its slice of the geometry block. Retail's world
## coordinates go through the SAME `world_to_godot` the living map uses, so a
## territory and the ground under it cannot disagree about where north is.
func _build_mesh(row: Dictionary, blob: PackedByteArray) -> ArrayMesh:
	var name := String(row.get("mesh", "?"))
	var vertex_count := int(row.get("vertexCount", 0))
	var index_count := int(row.get("indexCount", 0))
	var position_offset := int(row.get("positionOffset", 0))
	var position_bytes := int(row.get("positionBytes", 0))
	var index_offset := int(row.get("indexOffset", 0))
	if vertex_count <= 0 or index_count <= 0:
		_fail("mesh %s declares %d vertices and %d indices" % [name, vertex_count, index_count])
		return null
	if position_bytes != vertex_count * 12:
		_fail("mesh %s declares %d position bytes for %d vertices" % [
			name, position_bytes, vertex_count])
		return null
	if position_offset + position_bytes > blob.size():
		_fail("mesh %s positions run past the end of the geometry block" % name)
		return null
	if index_offset + index_count * 4 > blob.size():
		_fail("mesh %s indices run past the end of the geometry block" % name)
		return null

	var floats := blob.slice(position_offset, position_offset + position_bytes).to_float32_array()
	var vertices := PackedVector3Array()
	vertices.resize(vertex_count)
	for i in range(vertex_count):
		# Retail world (x, y, z) -> Godot. Stated once, in the living-map bundle,
		# and used verbatim here.
		vertices[i] = Vector3(floats[i * 3], floats[i * 3 + 2], -floats[i * 3 + 1])

	var raw_indices := blob.slice(index_offset, index_offset + index_count * 4).to_int32_array()
	var indices := PackedInt32Array()
	indices.resize(index_count)
	for i in range(index_count):
		var value := raw_indices[i]
		if value < 0 or value >= vertex_count:
			_fail("mesh %s index %d is %d, outside 0..%d" % [name, i, value, vertex_count - 1])
			return null
		indices[i] = value

	var arrays: Array = []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices
	arrays[Mesh.ARRAY_INDEX] = indices
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
	mesh.resource_name = name
	return mesh
