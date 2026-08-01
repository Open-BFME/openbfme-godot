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
##     MouseoverEffectFlareup   geometry LMR_Fill
##     RegionSelectionEffect    geometry LMR_Edge
##     HomeRegionHighlight      geometry LMR_Highlight
##     UnifiedEffect            geometry LMR_RegFill, LMR_RegEdge
##
## RETAIL SEPARATES ITS EFFECTS BY GEOMETRY, NOT BY COLOUR. Selection is its own
## mesh (`LMR_Edge`) and the home region is its own mesh (`LMR_Highlight`); they
## are not the ownership band in a different tint. A consumer holding only
## `fill` and `border` therefore cannot draw either one with retail's shapes and
## has to recolour the ownership art, which is what this screen was doing. The
## converter now writes the `edge` and `highlight` layers by default, at a
## measured 52/52 region coverage each, and `selection_mesh()` /
## `home_highlight_mesh()` below hand them out.
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

## `"<region>@<outset>" -> ArrayMesh or null`, see `border_shoulder_mesh`.
var _shoulder_cache: Dictionary = {}

var layers_present: PackedStringArray = PackedStringArray()
var total_triangles := 0
var source_summary: Array[Dictionary] = []

## `layer -> {meshes, regionsMatched, territoriesMatched, impassableMeshes,
## regionsWithoutMesh, unmatchedMeshes}` straight from the manifest. Per LAYER,
## because the layers do NOT all cover the same regions: a bundle can carry a
## fill for a region and no selection edge for it, and a caller about to draw
## the selection has to be able to ask about the selection rather than about the
## fill. Empty for a bundle written before the converter recorded coverage.
var layer_coverage: Dictionary = {}
## `name -> text`, retail-geometry gaps the CONVERTER named, carried verbatim so
## this side and the importer say the same sentence instead of two paraphrases
## that drift. See `named_gap_lines`.
var named_gaps: Dictionary = {}

## THE LAYER RETAIL'S `RegionSelectionEffect` DRAWS. Named as a constant because
## the mapping from an effect to a mesh is retail's, stated in
## `livingworldregioneffects.ini`, and must never be re-guessed at a call site.
const SELECTION_LAYER := "edge"
## THE LAYER RETAIL'S `HomeRegionHighlight` DRAWS.
const HOME_HIGHLIGHT_LAYER := "highlight"
## THE LAYER RETAIL'S `BordersEffect` DRAWS.
const BORDER_LAYER := "border"
## THE LAYER RETAIL'S `FilledOwnershipEffect` AND `MouseoverEffectFlareup` DRAW.
## Both effects genuinely use the SAME mesh - hover flares the fill, it does not
## get art of its own - which is why there is no separate mouseover layer.
const FILL_LAYER := "fill"


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

	# Optional, and read defensively: a bundle written before the converter
	# measured per-layer coverage is still a VALID bundle of the same schema
	# version, and refusing it here would break a field install over a field
	# that only makes an existing fact easier to read. What such a bundle cannot
	# do is claim coverage it never measured, so the accessors below fall back to
	# counting the meshes actually loaded rather than to an optimistic default.
	var coverage: Variant = manifest.get("layerCoverage", null)
	if coverage is Dictionary:
		layer_coverage = coverage
	for gap_value in manifest.get("namedGaps", []) as Array:
		var gap := gap_value as Dictionary
		if gap == null:
			continue
		var gap_name := String(gap.get("name", ""))
		if not gap_name.is_empty():
			named_gaps[gap_name] = String(gap.get("text", ""))

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


## THE BOX THAT HOLDS EVERY PLAYABLE REGION, in Godot space, or a zero-size box
## when no fill layer loaded. This is the PLAYABLE BOARD as opposed to retail's
## terrain slab, and the two are not the same rectangle: measured on the shipped
## bundle the fill layer spans godot x -2106.9..3088.0 and z -2537.3..1189.9
## (5195 x 3727), while the slab spans x -2784.1..3236.8 and z -3447.1..1372.1
## (6021 x 4819). The difference is retail's painted seabed column in the west
## and the empty ice north of Forodwaith - ground no region is ever placed on.
##
## WHY IT LIVES HERE. `wotr_map_view.gd` frames its camera on this box (see
## `_framing_box`), and the box is a property of the region bundle rather than of
## the camera: the view must not re-derive it from meshes it happens to have
## instanced, because the fill meshes the view instances are filtered by which
## regions the session named and the framing must not move when a campaign names
## a different set.
##
## MERGED FROM THE MESHES' OWN BAKED AABBs rather than walked vertex by vertex.
## `ArrayMesh.get_aabb()` is the bound the importer wrote, so this is retail's own
## number, and the merge is 52 cheap reads rather than a pass over ~200k vertices.
## Cached: the bundle is immutable once loaded, so the box cannot move.
var _playable_bounds := AABB()
var _playable_bounds_built := false

func playable_bounds() -> AABB:
	if _playable_bounds_built:
		return _playable_bounds
	_playable_bounds_built = true
	var low := Vector3(INF, INF, INF)
	var high := Vector3(-INF, -INF, -INF)
	var found := false
	for key in by_region.keys():
		var mesh: ArrayMesh = region_mesh(String(key), FILL_LAYER)
		if mesh == null or mesh.get_surface_count() == 0:
			continue
		var box: AABB = mesh.get_aabb()
		found = true
		low.x = minf(low.x, box.position.x)
		low.y = minf(low.y, box.position.y)
		low.z = minf(low.z, box.position.z)
		high.x = maxf(high.x, box.end.x)
		high.y = maxf(high.y, box.end.y)
		high.z = maxf(high.z, box.end.z)
	if not found:
		# NO GUESS. A caller that gets a zero-size box must fall back to something
		# it can name, and `wotr_map_view.gd` does exactly that rather than framing
		# on an invented rectangle.
		_playable_bounds = AABB()
		return _playable_bounds
	_playable_bounds = AABB(low, high - low)
	return _playable_bounds


## RETAIL'S OWN SELECTION MESH for a region - the geometry
## `RegionSelectionEffect` draws, `art/w3d/lm/lmr_edge.w3d`. Returns null when
## the bundle carries no edge layer or no edge mesh for this region, and the
## caller must then draw NOTHING for selection rather than recolour the
## ownership band into a stand-in. `selection_geometry_gap()` says why it is
## null, in a sentence fit to print.
##
## This is a different SHAPE from the border, not a different colour: retail's
## edge is a separate strip authored for this purpose. Substituting the border
## is exactly the confusion this accessor exists to end.
func selection_mesh(region_id: String) -> ArrayMesh:
	return region_mesh(region_id, SELECTION_LAYER)


## RETAIL'S OWN HOME-REGION MESH - the geometry `HomeRegionHighlight` draws,
## `art/w3d/lm/lmr_highlight.w3d`. Null when absent, same discipline as
## `selection_mesh`.
func home_highlight_mesh(region_id: String) -> ArrayMesh:
	return region_mesh(region_id, HOME_HIGHLIGHT_LAYER)


## Does the bundle carry this layer at all? Asked BEFORE a draw loop so a caller
## can say "retail's selection art is not in this bundle" once, rather than
## discovering a null per region and drawing 52 nothings without explaining.
func has_layer(layer: String) -> bool:
	return layers_present.has(layer)


## How many document regions this layer can actually be drawn for. Counted from
## the meshes that LOADED, not from the manifest's claim, so a manifest that
## overstates coverage cannot make this number lie.
func layer_region_count(layer: String) -> int:
	var count := 0
	for key in by_region.keys():
		if (by_region[key] as Dictionary).has(layer):
			count += 1
	return count


## Region ids that have a `fill` (so they are ON the map and get drawn) but no
## mesh in `layer` (so this effect cannot be drawn for them). This is the honest
## hole list for one effect, and it is NOT the same as
## `regions_without_geometry`, which is the union over every layer.
func regions_without_layer(layer: String) -> PackedStringArray:
	var missing: Array[String] = []
	for key in by_region.keys():
		var slot := by_region[key] as Dictionary
		if slot.has(FILL_LAYER) and not slot.has(layer):
			missing.append(String(key))
	missing.sort()
	return PackedStringArray(missing)


## RETAIL'S OWN FILL MESH WITH A DISTANCE-FROM-THE-RIM FIELD BAKED INTO ITS VERTEX
## COLOUR - `COLOR.r` is 0 on the province's outline and 1 at its deepest interior
## point. Null when the region has no fill mesh, and null when it has no BORDER
## mesh either, because the border ribbon is what the rim is measured against.
##
## WHY IT EXISTS. `MouseoverEffectFlareup` was drawn as a flat additive wash over
## the whole fill: uniform alpha right up to the polygon boundary, which ends the
## light on a hard edge and draws the region's silhouette as a sheet of colour.
## The map view could not do better because retail's `LMR_Fill` carries POSITIONS
## AND NOTHING ELSE - no UVs, no colours - so a shader had no field to fall off
## against. This is that field.
##
## IT IS RETAIL'S GEOMETRY ON BOTH SIDES. The vertices are retail's fill vertices,
## untouched. The rim is retail's own `LMR_Border` ribbon for the SAME region -
## the strip the ownership band is drawn on, which by construction traces the
## province's outline - so "distance to the rim" is measured against shipped art
## rather than against a boundary this project inferred. The only computed number
## is a distance, and it is normalised by the region's own deepest value so the
## field means the same thing on Harad and on Buckland.
##
## A DISTANCE TRANSFORM IS WHY THIS IS AFFORDABLE, AND THE FIRST ATTEMPT IS
## RECORDED BECAUSE IT WAS NOT. Measured on the shipped bundle there are 41,943
## fill vertices and 17,614 border vertices over 52 regions, so the obvious nested
## loop is ~14 million distance tests; bucketing the rim into a grid and searching
## outwards ring by ring from each fill vertex sounds like the fix and is not - a
## vertex deep inside Rhun is a dozen rings from any rim cell, and the whole bake
## measured 76 SECONDS. What is cheap is solving the field ONCE PER REGION on a
## fixed grid and then reading it per vertex:
##
##   1. rasterise the rim onto a `FALLOFF_GRID_CELLS` square grid over the
##      region's own extent - every cell holding a rim vertex is distance zero;
##   2. two chamfer passes (forward and backward, four neighbours each) propagate
##      distance across the grid in world units, which is the standard sequential
##      distance transform and is O(cells) rather than O(vertices x rim);
##   3. sample the grid bilinearly at each fill vertex, so the field the shader
##      interpolates is smooth rather than stepped at the cell size.
##
## That is ~1 million simple operations for the whole map instead of ~100 million
## heavy ones, and it measures under a tenth of a second. The chamfer's own error
## is a few percent of a cell, which on a gradient nobody can read to that
## precision is invisible; what it must get right is that the rim reads ZERO, and
## rasterising the rim to exact zeroes is what guarantees that.
##
## Cached for the same reason `border_shoulder_mesh` is: the territory nodes are
## rebuilt whenever the world is, and this must be paid once per LOAD rather than
## once per rebuild.
const FALLOFF_GRID_CELLS := 64
var _falloff_cache: Dictionary = {}
var _falloff_depth_units: Dictionary = {}
## How long the bake has cost so far, in milliseconds, so a load can print it
## rather than a reader having to guess.
var falloff_build_ms := 0.0

func fill_falloff_mesh(region_id: String) -> ArrayMesh:
	if _falloff_cache.has(region_id):
		return _falloff_cache[region_id] as ArrayMesh
	var source: ArrayMesh = region_mesh(region_id, FILL_LAYER)
	var rim: ArrayMesh = region_mesh(region_id, BORDER_LAYER)
	if source == null or rim == null or source.get_surface_count() == 0 			or rim.get_surface_count() == 0:
		# NO GUESS. The caller draws the flat flare it always drew and the region is
		# named, rather than being given an invented falloff.
		_falloff_cache[region_id] = null
		return null
	var started := Time.get_ticks_usec()
	var arrays: Array = source.surface_get_arrays(0)
	var vertices := arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array
	var rim_vertices := (rim.surface_get_arrays(0)[Mesh.ARRAY_VERTEX]) as PackedVector3Array
	if vertices.is_empty() or rim_vertices.is_empty():
		_falloff_cache[region_id] = null
		return null

	# THE GRID, over the region's own extent - the union of the fill's and the
	# rim's, so no fill vertex can fall outside the field that is solved for it.
	var low := Vector2(INF, INF)
	var high := Vector2(-INF, -INF)
	for point in rim_vertices:
		low.x = minf(low.x, point.x)
		low.y = minf(low.y, point.z)
		high.x = maxf(high.x, point.x)
		high.y = maxf(high.y, point.z)
	for point in vertices:
		low.x = minf(low.x, point.x)
		low.y = minf(low.y, point.z)
		high.x = maxf(high.x, point.x)
		high.y = maxf(high.y, point.z)
	var span := Vector2(maxf(high.x - low.x, 0.001), maxf(high.y - low.y, 0.001))
	var cells := FALLOFF_GRID_CELLS
	var cell := span / float(cells - 1)
	var field := PackedFloat32Array()
	field.resize(cells * cells)
	field.fill(INF)
	for point in rim_vertices:
		var col := clampi(int(round((point.x - low.x) / cell.x)), 0, cells - 1)
		var row := clampi(int(round((point.z - low.y) / cell.y)), 0, cells - 1)
		field[row * cells + col] = 0.0

	# TWO CHAMFER PASSES. The four neighbours each pass are the causal half of a
	# 3x3 stencil, which is what makes one forward and one backward sweep enough.
	var dx := cell.x
	var dy := cell.y
	var dd := sqrt(dx * dx + dy * dy)
	for row in range(cells):
		for col in range(cells):
			var here := row * cells + col
			var best := field[here]
			if col > 0:
				best = minf(best, field[here - 1] + dx)
			if row > 0:
				best = minf(best, field[here - cells] + dy)
				if col > 0:
					best = minf(best, field[here - cells - 1] + dd)
				if col < cells - 1:
					best = minf(best, field[here - cells + 1] + dd)
			field[here] = best
	for row in range(cells - 1, -1, -1):
		for col in range(cells - 1, -1, -1):
			var here := row * cells + col
			var best := field[here]
			if col < cells - 1:
				best = minf(best, field[here + 1] + dx)
			if row < cells - 1:
				best = minf(best, field[here + cells] + dy)
				if col < cells - 1:
					best = minf(best, field[here + cells + 1] + dd)
				if col > 0:
					best = minf(best, field[here + cells - 1] + dd)
			field[here] = best

	# SAMPLED BILINEARLY, so the shader interpolates a smooth field rather than a
	# staircase at the cell size.
	var shortest := PackedFloat32Array()
	shortest.resize(vertices.size())
	var deepest := 0.0
	for index in vertices.size():
		var vertex := vertices[index]
		var fx := clampf((vertex.x - low.x) / cell.x, 0.0, float(cells - 1))
		var fy := clampf((vertex.z - low.y) / cell.y, 0.0, float(cells - 1))
		var col0 := int(fx)
		var row0 := int(fy)
		var col1 := mini(col0 + 1, cells - 1)
		var row1 := mini(row0 + 1, cells - 1)
		var tx := fx - float(col0)
		var ty := fy - float(row0)
		var top := lerpf(field[row0 * cells + col0], field[row0 * cells + col1], tx)
		var bottom := lerpf(field[row1 * cells + col0], field[row1 * cells + col1], tx)
		var distance := lerpf(top, bottom, ty)
		if not is_finite(distance):
			distance = 0.0
		shortest[index] = distance
		deepest = maxf(deepest, distance)

	var colors := PackedColorArray()
	colors.resize(vertices.size())
	var scale := 1.0 / maxf(deepest, 0.001)
	for index in vertices.size():
		var depth := clampf(shortest[index] * scale, 0.0, 1.0)
		colors[index] = Color(depth, depth, depth, 1.0)
	var built: Array = []
	built.resize(Mesh.ARRAY_MAX)
	built[Mesh.ARRAY_VERTEX] = vertices
	built[Mesh.ARRAY_COLOR] = colors
	built[Mesh.ARRAY_INDEX] = arrays[Mesh.ARRAY_INDEX]
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, built)
	mesh.resource_name = "%s_flare_falloff" % source.resource_name
	falloff_build_ms += float(Time.get_ticks_usec() - started) / 1000.0
	_falloff_cache[region_id] = mesh
	_falloff_depth_units[region_id] = deepest
	return mesh


## THE WORLD-UNIT DISTANCE `COLOR.r == 1.0` STANDS FOR in one region - the deepest
## point of that province, measured from retail's own border ribbon. The field in
## the mesh is normalised per region so it means the same thing on Harad and on
## Buckland; this is the scale that was divided out, and it is what lets a test
## state a tolerance in world units rather than in a fraction that means something
## different on every province. Zero until the region's falloff has been baked.
func falloff_depth_units(region_id: String) -> float:
	return float(_falloff_depth_units.get(region_id, 0.0))


## Every region whose flare falloff could NOT be baked, so the screen can name
## them rather than quietly drawing the flat wash. Only meaningful after the
## meshes have been asked for.
func regions_without_flare_falloff() -> PackedStringArray:
	var missing: Array[String] = []
	for key in _falloff_cache.keys():
		if _falloff_cache[key] == null:
			missing.append(String(key))
	missing.sort()
	return PackedStringArray(missing)


## Empty when retail's selection geometry is available for every drawn region.
## Otherwise a NAMED GAP sentence saying exactly what is missing and what the
## consequence is - the shape `wotr_handoff.gd:UNSUPPORTED_BY_TACTICAL_SIM`
## uses. A caller prints this instead of inventing a substitute.
func selection_geometry_gap() -> String:
	return _layer_gap(SELECTION_LAYER, "RegionSelectionEffect", "art/w3d/lm/lmr_edge.w3d")


## Empty when retail's home-region geometry is available for every drawn region.
func home_highlight_gap() -> String:
	return _layer_gap(
		HOME_HIGHLIGHT_LAYER, "HomeRegionHighlight", "art/w3d/lm/lmr_highlight.w3d")


## Every gap the CONVERTER named, one line each, for the launch log. These are
## facts about retail's shipped art (e.g. that `lmr_seledge.w3d` is stale BFME2
## geometry no RotWK effect references), not about this load.
func named_gap_lines() -> PackedStringArray:
	var lines: Array[String] = []
	var keys: Array = named_gaps.keys()
	keys.sort()
	for key in keys:
		lines.append("%s: %s" % [String(key), String(named_gaps[key])])
	return PackedStringArray(lines)


## RETAIL'S OWN BORDER RIBBON, PUSHED OUTWARDS - the outer shoulder of the
## ownership outline. Returns null when the region has no border mesh or no
## derived centroid, and the caller then draws no shoulder rather than a
## substitute.
##
## WHAT IS AND IS NOT DERIVED HERE. Every vertex is retail's own border vertex;
## the only thing computed is a displacement of `outset` world units along the
## direction from the region's AREA-WEIGHTED CENTROID of retail's own fill
## triangles (`derivedCentroid`, which the converter measures and which this
## project already places unauthored region markers from) to that vertex. The
## ribbon therefore keeps retail's shape and its ~6.5-unit width and simply sits
## a few units further out, which is the concentric shoulder a bloomed outline
## needs and which retail gets from its own renderer's bloom.
##
## It is NOT a widened border and must never be drawn as one: it is an additive
## halo under the solid band, and the band itself is always retail's strip at
## retail's own coordinates.
##
## Cached per (region, outset) because the territory nodes are rebuilt whenever
## the world is, and re-deriving a few hundred ribbons per rebuild would be paid
## for every time the map bundle is bound.
func border_shoulder_mesh(region_id: String, outset: float) -> ArrayMesh:
	var key := "%s@%.3f" % [region_id, outset]
	if _shoulder_cache.has(key):
		return _shoulder_cache[key] as ArrayMesh
	var source: ArrayMesh = region_mesh(region_id, "border")
	if source == null or not derived_centroids.has(region_id):
		_shoulder_cache[key] = null
		return null
	var centre := derived_centroids[region_id] as Vector2
	# The centroid is authored in RETAIL world x/y; the mesh is already in Godot
	# space, so the centre goes through the same one conversion the vertices did.
	var godot_centre := Vector3(centre.x, 0.0, -centre.y)
	var arrays: Array = source.surface_get_arrays(0)
	var vertices := arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array
	var moved := PackedVector3Array()
	moved.resize(vertices.size())
	for index in vertices.size():
		var vertex := vertices[index]
		var radial := Vector2(vertex.x - godot_centre.x, vertex.z - godot_centre.z)
		if radial.length() < 0.001:
			# A vertex sitting exactly on the centroid has no outward direction.
			# Left where retail put it rather than pushed in an invented one.
			moved[index] = vertex
			continue
		radial = radial.normalized() * outset
		moved[index] = Vector3(vertex.x + radial.x, vertex.y, vertex.z + radial.y)
	var built: Array = []
	built.resize(Mesh.ARRAY_MAX)
	built[Mesh.ARRAY_VERTEX] = moved
	built[Mesh.ARRAY_INDEX] = arrays[Mesh.ARRAY_INDEX]
	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, built)
	mesh.resource_name = "%s_shoulder_%.1f" % [source.resource_name, outset]
	_shoulder_cache[key] = mesh
	return mesh


## ==============================================================================
## PICKING BY THE REGION'S OWN POLYGON
## ==============================================================================
##
## WHY THIS IS HERE AND NOT IN THE VIEW. The shape a region occupies is a fact
## about retail's geometry, not about a camera, so the structure that answers
## "which region is this point in" belongs beside the meshes it is built from.
## The view supplies a ray; nothing about the camera leaks in here.
##
## WHAT IT REPLACES. `wotr_map_view.region_at()` used to measure the distance to
## the region's MARKER - a 16-pixel disc around a node point - so 99% of a
## province was dead to the pointer and two neighbouring capitals were easier to
## confuse than to hit. The owner's words: "when I hover over an area it should
## light up and allow me to select the ENTIRE area instead of just the node point
## that has a lower selection." The whole area is `LMR_Fill`, and this tests
## against it directly. PROJECT-AUTHORED structure over RETAIL-DERIVED geometry:
## retail's own picking code is not in the archives and none is claimed here -
## every triangle tested is retail's, the acceleration around them is ours.
##
## IT IS AN EXACT 3D RAY TEST, NOT A FLATTENED ONE, and that is deliberate. The
## cheap version intersects the pointer's ray with one horizontal plane and looks
## the resulting x/z up in a 2D map of the regions. Middle-earth's relief spans
## hundreds of world units and the camera looks down it at 47 degrees, so a
## point sampled on a single plane lands up to ~0.93 * (height error) away from
## the ground the player is actually looking at - most of a province wide, in the
## Misty Mountains. The rays here are intersected with retail's own triangles at
## retail's own heights, so hover is exactly what the eye sees.
##
## THE ACCELERATION IS A UNIFORM GRID over the map's x/z footprint, traversed
## front-to-back (an Amanatides-Woo DDA), with the nearest hit winning. The grid
## is built ONCE, lazily, on the first pick - a headless run that never picks
## never pays for it - and holds ~55k triangles at about four per cell.
##
## COST, measured rather than assumed (see `wotr_map3d_runner`): the build is a
## one-off, and a pick walks a few tens of cells and tests a few tens of
## triangles. Picking runs on pointer MOTION and on clicks, never in `_process`,
## so it is not in the per-frame budget at all.

## TARGET TRIANGLES PER GRID CELL. Four is the usual sweet spot for a uniform
## grid over triangle soup: fewer and the cell bookkeeping dominates, more and
## the per-cell scan does. Both ends are cheap here; the number is not delicate.
const PICK_TRIANGLES_PER_CELL := 4.0
## Bounds on the grid's resolution so a degenerate bundle cannot ask for a
## million cells or for one.
const PICK_MIN_CELLS_PER_AXIS := 4
const PICK_MAX_CELLS_PER_AXIS := 256

var _pick_built := false
## Region id per grid triangle, as an index into `_pick_region_ids`.
var _pick_region_ids: PackedStringArray = PackedStringArray()
var _pick_tri_region: PackedInt32Array = PackedInt32Array()
## Three vertices per triangle, in Godot space, exactly as the meshes carry them.
var _pick_tri: PackedVector3Array = PackedVector3Array()
## The grid: `_pick_start[cell]`..`_pick_start[cell + 1]` indexes `_pick_items`,
## which holds triangle indices. A counting sort, so there is one flat array
## rather than a dictionary of arrays per cell.
var _pick_start: PackedInt32Array = PackedInt32Array()
var _pick_items: PackedInt32Array = PackedInt32Array()
var _pick_origin := Vector2.ZERO
var _pick_cell := Vector2.ONE
var _pick_cols := 0
var _pick_rows := 0
var _pick_y_min := 0.0
var _pick_y_max := 0.0
## How long the one-off build took, in milliseconds, so the cost can be reported
## rather than guessed at.
var pick_index_build_ms := 0.0


## How many triangles the pick index holds, and how many cells over them. Public
## because "picking is by polygon now" and "picking is by polygon over retail's
## own 55k fill triangles" are different claims and only the second is this one.
func pick_index_stats() -> Dictionary:
	_ensure_pick_index()
	return {
		"triangles": _pick_tri_region.size(),
		"cells": _pick_cols * _pick_rows,
		"cols": _pick_cols,
		"rows": _pick_rows,
		"regions": _pick_region_ids.size(),
		"build_ms": pick_index_build_ms,
	}


## THE REGION A RAY STRIKES, or "". `origin` and `direction` are in Godot space;
## `direction` need not be normalised. The NEAREST region along the ray wins, so
## a ray that grazes a far province before entering a near one picks the near
## one - which is what the player sees.
func region_at_ray(origin: Vector3, direction: Vector3) -> String:
	var hit := _pick_ray(origin, direction)
	return "" if hit < 0 else String(_pick_region_ids[_pick_tri_region[hit]])


## THE REGION DIRECTLY BELOW OR ABOVE A MAP POSITION, or "". A vertical drop
## through the fill layer, which is the question a test asks ("is this point
## inside this region's polygon?") without having to build a camera to ask it.
func region_at_position(x: float, z: float) -> String:
	_ensure_pick_index()
	if not _pick_built:
		return ""
	# Started above everything and dropped, so the answer cannot depend on which
	# side of the terrain the caller happened to pick.
	return region_at_ray(
		Vector3(x, _pick_y_max + 1000.0, z), Vector3(0.0, -1.0, 0.0))


## The triangle index a ray strikes first, or -1. Split from `region_at_ray` so
## the traversal can be exercised on its own.
func _pick_ray(origin: Vector3, direction: Vector3) -> int:
	_ensure_pick_index()
	if not _pick_built:
		return -1
	if direction.length_squared() < 0.000001:
		return -1
	var dir := direction.normalized()

	# THE SLAB THE ANSWER CAN POSSIBLY BE IN, on all three axes. Clipping first
	# means a ray aimed at the sky or at the ocean beyond the map costs three
	# comparisons instead of a walk across the whole grid.
	var enter := 0.0
	var exit := INF
	var low := Vector3(_pick_origin.x, _pick_y_min, _pick_origin.y)
	var high := Vector3(
		_pick_origin.x + _pick_cell.x * float(_pick_cols),
		_pick_y_max,
		_pick_origin.y + _pick_cell.y * float(_pick_rows))
	for axis in 3:
		var d := dir[axis]
		var o := origin[axis]
		if absf(d) < 0.000001:
			if o < low[axis] or o > high[axis]:
				return -1
			continue
		var t0 := (low[axis] - o) / d
		var t1 := (high[axis] - o) / d
		if t0 > t1:
			var swap := t0
			t0 = t1
			t1 = swap
		enter = maxf(enter, t0)
		exit = minf(exit, t1)
		if enter > exit:
			return -1
	if not is_finite(exit):
		return -1

	# AMANATIDES-WOO, on the x/z plane only: the grid has no vertical division,
	# so a ray's cell column is a function of its horizontal travel alone.
	var at := origin + dir * enter
	var col := clampi(int(floor((at.x - _pick_origin.x) / _pick_cell.x)), 0, _pick_cols - 1)
	var row := clampi(int(floor((at.z - _pick_origin.y) / _pick_cell.y)), 0, _pick_rows - 1)
	var step_col := 0 if absf(dir.x) < 0.000001 else (1 if dir.x > 0.0 else -1)
	var step_row := 0 if absf(dir.z) < 0.000001 else (1 if dir.z > 0.0 else -1)
	var next_col := INF
	var delta_col := INF
	if step_col != 0:
		var boundary_x := _pick_origin.x + float(col + (1 if step_col > 0 else 0)) * _pick_cell.x
		next_col = (boundary_x - origin.x) / dir.x
		delta_col = absf(_pick_cell.x / dir.x)
	var next_row := INF
	var delta_row := INF
	if step_row != 0:
		var boundary_z := _pick_origin.y + float(row + (1 if step_row > 0 else 0)) * _pick_cell.y
		next_row = (boundary_z - origin.z) / dir.z
		delta_row = absf(_pick_cell.y / dir.z)

	var best := -1
	var best_t := INF
	while true:
		# THE CELL'S OWN EXIT PARAMETER. Once a hit has been found nearer than
		# where this cell ENDS, no later cell can beat it and the walk stops -
		# which is the whole reason the traversal is front-to-back.
		var cell_exit := minf(next_col, next_row)
		var cell := row * _pick_cols + col
		var from := _pick_start[cell]
		var to := _pick_start[cell + 1]
		for slot in range(from, to):
			var index := _pick_items[slot]
			var t := _ray_hits_triangle(origin, dir, index)
			if t >= 0.0 and t < best_t:
				best_t = t
				best = index
		if best >= 0 and best_t <= cell_exit:
			return best
		if cell_exit > exit:
			break
		if next_col < next_row:
			col += step_col
			if col < 0 or col >= _pick_cols:
				break
			next_col += delta_col
		else:
			row += step_row
			if row < 0 or row >= _pick_rows:
				break
			next_row += delta_row
	return best


## Moller-Trumbore. Returns the ray parameter of the hit, or -1. Double-sided:
## `lmr_fill.w3d`'s winding is retail's and this is a query, not a render, so a
## triangle facing away from the camera must still be pickable.
func _ray_hits_triangle(origin: Vector3, dir: Vector3, triangle: int) -> float:
	var base := triangle * 3
	var a := _pick_tri[base]
	var edge1 := _pick_tri[base + 1] - a
	var edge2 := _pick_tri[base + 2] - a
	var pvec := dir.cross(edge2)
	var det := edge1.dot(pvec)
	if absf(det) < 0.000000001:
		return -1.0
	var inv_det := 1.0 / det
	var tvec := origin - a
	var u := tvec.dot(pvec) * inv_det
	if u < 0.0 or u > 1.0:
		return -1.0
	var qvec := tvec.cross(edge1)
	var v := dir.dot(qvec) * inv_det
	if v < 0.0 or u + v > 1.0:
		return -1.0
	var t := edge2.dot(qvec) * inv_det
	return t if t >= 0.0 else -1.0


## Build the grid once. Every triangle of every REGION's fill mesh goes in;
## the impassable land masses do not, because they are bound to no region and a
## pointer over one is over no region.
func _ensure_pick_index() -> void:
	if _pick_built:
		return
	_pick_built = true
	var started := Time.get_ticks_usec()
	_pick_region_ids = PackedStringArray()
	_pick_tri_region = PackedInt32Array()
	_pick_tri = PackedVector3Array()
	_pick_start = PackedInt32Array()
	_pick_items = PackedInt32Array()
	_pick_cols = 0
	_pick_rows = 0
	if not loaded:
		_pick_built = false
		return

	var region_ids: Array[String] = []
	for key in by_region.keys():
		if (by_region[key] as Dictionary).has(FILL_LAYER):
			region_ids.append(String(key))
	region_ids.sort()
	if region_ids.is_empty():
		_pick_built = false
		return
	_pick_region_ids = PackedStringArray(region_ids)

	var low := Vector2(INF, INF)
	var high := Vector2(-INF, -INF)
	_pick_y_min = INF
	_pick_y_max = -INF
	for region_index in region_ids.size():
		var mesh: ArrayMesh = region_mesh(region_ids[region_index], FILL_LAYER)
		if mesh == null or mesh.get_surface_count() == 0:
			continue
		var arrays: Array = mesh.surface_get_arrays(0)
		var vertices := arrays[Mesh.ARRAY_VERTEX] as PackedVector3Array
		var indices := arrays[Mesh.ARRAY_INDEX] as PackedInt32Array
		for i in range(0, indices.size() - 2, 3):
			for corner in 3:
				var vertex := vertices[indices[i + corner]]
				_pick_tri.append(vertex)
				low.x = minf(low.x, vertex.x)
				low.y = minf(low.y, vertex.z)
				high.x = maxf(high.x, vertex.x)
				high.y = maxf(high.y, vertex.z)
				_pick_y_min = minf(_pick_y_min, vertex.y)
				_pick_y_max = maxf(_pick_y_max, vertex.y)
			_pick_tri_region.append(region_index)
	var triangles := _pick_tri_region.size()
	if triangles == 0 or not is_finite(low.x) or not is_finite(high.x):
		_pick_built = false
		return
	# A hair of slack so a vertex exactly on the outer boundary still lands in a
	# cell rather than one past the last one.
	var span := Vector2(maxf(high.x - low.x, 0.001), maxf(high.y - low.y, 0.001))
	_pick_origin = low - span * 0.001
	span *= 1.002
	if _pick_y_max - _pick_y_min < 1.0:
		_pick_y_min -= 0.5
		_pick_y_max += 0.5

	# The cell count that puts roughly `PICK_TRIANGLES_PER_CELL` in each cell,
	# split between the axes in the map's own proportion so the cells stay square.
	var wanted_cells := maxf(float(triangles) / PICK_TRIANGLES_PER_CELL, 1.0)
	var side := sqrt(wanted_cells * span.x / span.y)
	_pick_cols = clampi(int(round(side)), PICK_MIN_CELLS_PER_AXIS, PICK_MAX_CELLS_PER_AXIS)
	_pick_rows = clampi(
		int(round(wanted_cells / float(_pick_cols))),
		PICK_MIN_CELLS_PER_AXIS, PICK_MAX_CELLS_PER_AXIS)
	_pick_cell = Vector2(span.x / float(_pick_cols), span.y / float(_pick_rows))

	# COUNTING SORT, two passes. Pass one counts how many triangles each cell
	# holds; the prefix sum turns those counts into starts; pass two writes the
	# triangle indices into the slots the starts hand out. One flat array, no
	# per-cell allocation, and the result is deterministic.
	var cells := _pick_cols * _pick_rows
	_pick_start.resize(cells + 1)
	_pick_start.fill(0)
	var spans := PackedInt32Array()
	spans.resize(triangles * 4)
	for triangle in triangles:
		var base := triangle * 3
		var a := _pick_tri[base]
		var b := _pick_tri[base + 1]
		var c := _pick_tri[base + 2]
		var col_from := _pick_col(minf(a.x, minf(b.x, c.x)))
		var col_to := _pick_col(maxf(a.x, maxf(b.x, c.x)))
		var row_from := _pick_row(minf(a.z, minf(b.z, c.z)))
		var row_to := _pick_row(maxf(a.z, maxf(b.z, c.z)))
		spans[triangle * 4] = col_from
		spans[triangle * 4 + 1] = col_to
		spans[triangle * 4 + 2] = row_from
		spans[triangle * 4 + 3] = row_to
		for row in range(row_from, row_to + 1):
			for col in range(col_from, col_to + 1):
				_pick_start[row * _pick_cols + col] += 1
	var running := 0
	for cell in range(cells + 1):
		var count := _pick_start[cell]
		_pick_start[cell] = running
		running += count
	_pick_items.resize(running)
	var cursor := _pick_start.duplicate()
	for triangle in triangles:
		for row in range(spans[triangle * 4 + 2], spans[triangle * 4 + 3] + 1):
			for col in range(spans[triangle * 4], spans[triangle * 4 + 1] + 1):
				var cell := row * _pick_cols + col
				_pick_items[cursor[cell]] = triangle
				cursor[cell] += 1
	pick_index_build_ms = float(Time.get_ticks_usec() - started) / 1000.0


func _pick_col(x: float) -> int:
	return clampi(int(floor((x - _pick_origin.x) / _pick_cell.x)), 0, _pick_cols - 1)


func _pick_row(z: float) -> int:
	return clampi(int(floor((z - _pick_origin.y) / _pick_cell.y)), 0, _pick_rows - 1)


func has_region(region_id: String) -> bool:
	return by_region.has(region_id)


func shaded_region_count() -> int:
	var count := 0
	for key in by_region.keys():
		if (by_region[key] as Dictionary).has("fill"):
			count += 1
	return count


# --- internals ----------------------------------------------------------------

## The one place a "this effect cannot be drawn" sentence is written. Both
## public gap accessors go through it so the two can never drift into saying the
## same thing two different ways.
func _layer_gap(layer: String, effect: String, virtual_path: String) -> String:
	if not loaded:
		return (
			"%s cannot be drawn: no region-geometry bundle is loaded." % effect)
	if not has_layer(layer):
		return (
			"%s draws %s (%s) and this bundle carries no %s layer, so retail's "
			+ "own mesh is NOT drawn and no substitute is invented. Rebuild with "
			+ "python -m openbfme_importer.livingmap_regions (the %s layer is in "
			+ "its default set)."
		) % [effect, layer.to_upper(), virtual_path, layer, layer]
	var missing := regions_without_layer(layer)
	if missing.is_empty():
		return ""
	return (
		"%s draws %s (%s) and this bundle carries no %s mesh for %d of the %d "
		+ "drawn regions, so those regions get NOTHING for this effect rather "
		+ "than a substitute: %s"
	) % [
		effect, layer.to_upper(), virtual_path, layer, missing.size(),
		shaded_region_count(), ", ".join(Array(missing)),
	]


func _reset() -> void:
	loaded = false
	errors = PackedStringArray()
	records = []
	by_region = {}
	by_territory = {}
	derived_centroids = {}
	_shoulder_cache = {}
	regions_without_geometry = PackedStringArray()
	layers_present = PackedStringArray()
	source_summary = []
	layer_coverage = {}
	named_gaps = {}
	total_triangles = 0
	# The pick index is derived from the meshes above, so a reload invalidates it.
	# Dropped rather than rebuilt: it is built lazily on the first pick and a load
	# that is never picked against must not pay for one.
	_pick_built = false
	_pick_region_ids = PackedStringArray()
	_pick_tri_region = PackedInt32Array()
	_pick_tri = PackedVector3Array()
	_pick_start = PackedInt32Array()
	_pick_items = PackedInt32Array()
	_pick_cols = 0
	_pick_rows = 0
	pick_index_build_ms = 0.0
	# Same rule for the playable box: it is derived from the fill meshes, so a
	# reload invalidates it and it is rebuilt on the next ask rather than here.
	_playable_bounds = AABB()
	_playable_bounds_built = false
	_falloff_cache = {}
	_falloff_depth_units = {}
	falloff_build_ms = 0.0


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
