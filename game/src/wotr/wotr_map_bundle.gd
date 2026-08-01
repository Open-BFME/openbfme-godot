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
##   missing a third of Middle-earth still looks like a map. And a load that
##   found nothing reports EVERY candidate it looked at, with its origin and its
##   verdict - `describe_search_failure()` is what the log and the player both
##   get, because a silent fall back to the flat 2D graph is only discoverable by
##   noticing the map looks wrong, which is not a diagnosis.
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

## Sub-objects that are retail's ATMOSPHERE rather than its ground, still held
## back, each for a MEASURED reason rather than a general one:
##
## * `LAVAPLANE1`/`LAVAPLANE3` - `EXLavaSeq` is a FRAME-SEQUENCE SHEET; retail
##   steps through sub-rectangles of it on a schedule this bundle does not
##   carry, and playing it as one image shows the whole contact sheet at once.
## * `LM_SMOKE01..05` and `LM_LAVAVENTSMOK` - round 2 TRIED to draw these as a
##   two-frame cross-fade and measured why that cannot be honest: BOTH of
##   retail's frames (`LM_Smoke`, `LM_Smoke2`) are flat mid-grey sheets - the
##   JPEG's 5th and 95th percentile are the same 0.55 grey - so the plume's
##   SHAPE is not in either texture. It lives in the cards' vertex alpha,
##   which this bundle does not carry, and without it every card renders as
##   the grey rectangle it is. Named, not approximated.
##
## `PLANE03` - Mount Doom's smoke column - LEFT this list in round 2: its
## `LM_Doom01` frame is DXT5 with a real authored alpha mask, so the two-frame
## cross-fade CAN be composed from shipped bytes alone. It is in
## `PRESENTATION_SURFACES` below.
const AMBIENT_SUB_OBJECTS := [
	"LAVAPLANE1", "LAVAPLANE3",
	"LM_LAVAVENTSMOK", "LM_SMOKE01", "LM_SMOKE02", "LM_SMOKE03",
	"LM_SMOKE04", "LM_SMOKE05",
]

## Retail surfaces this lane draws with its OWN presentation treatment, keyed by
## sub-object name to the treatment the map view applies:
##
##   * "water" - the WATER plane. Retail renders it with `WaterShader.FX`, a
##     procedural shader with no diffuse texture; the view stands it under this
##     project's procedural water shader, which is the same claim retail makes
##     for it: colour from code, not from a texture.
##   * "cloud" - BORDERCLOUD (the weather bank surrounding Middle-earth) and
##     LM_CLOUDLAYER (the drift over the map), retail's own textures under a
##     slow UV drift in place of retail's texture-coordinate animation.
##   * "text" - TEXT PLANE, retail's engraved province names (`lm_text.dds`),
##     drawn as the static overlay retail authored it as and faded with the
##     camera so the lettering belongs to the strategic framing, not the close
##     one.
##   * "coast" - the six LM_COAST* shoreline strips. Retail authors each as a
##     TWO-STAGE blend: stage 0 is `SkyBoxNightClouds` (the sky-reflection
##     pass) and stage 1 is `WtrWhitecap_01` (the animated foam) on the mesh's
##     own UVs. Round 1 held these back because it composited one stage only,
##     which showed stage 0's night sky as an opaque band; the coast shader
##     now composites BOTH stages - foam dominant, reflection faint - with a
##     drift standing in for retail's texture-coordinate animation.
##   * "smoke" - Mount Doom's smoke column (PLANE03), authored as TWO frames
##     (`LM_Doom01` + `LM_Doom02`) where frame 0 carries a real DXT5 alpha
##     mask; the smoke shader cross-fades the frames inside that mask and
##     drifts them in place of retail's particle-sequence animation. The
##     OTHER smoke cards stay held back - see AMBIENT_SUB_OBJECTS for the
##     measurement that keeps them there.
##
## These carry `surface` in their sub-object row; `ambient`, `collision` and
## `shader_only` are all false for them, so the screen's own NOT-DRAWN census
## - which reads those three flags - stays truthful when they appear.
const PRESENTATION_SURFACES := {
	"WATER": "water",
	"BORDERCLOUD": "cloud",
	"LM_CLOUDLAYER": "cloud",
	"TEXT PLANE": "text",
	"LM_COAST": "coast",
	"LM_COAST01": "coast",
	"LM_COAST02": "coast",
	"LM_COAST03": "coast",
	"LM_COAST04": "coast",
	"LM_COAST05": "coast",
	"PLANE03": "smoke",
}

## `RIVERS`, still held back, and why the coast fix does not cover it: it is a
## full-map plane (X -2973..3416, Y -1373..3472) parked at z -124.8, a hair
## BELOW the terrain's lowest vertex at -123.3. Retail scrolls two ocean
## textures across it and lets it show through the riverbeds - which requires
## the terrain above it to cut alpha holes along every river, and this lane's
## terrain tiles are opaque. Drawn as authored it is invisible under the
## terrain except where it protrudes past the map edge, where it becomes two
## striped blue bands that are not part of Middle-earth. Not drawn, and named.
const RIVER_OVERLAY_SUB_OBJECTS := [
	"RIVERS",
]

## Sub-objects retail draws with a SHADER this lane does not implement. `WATER`
## was here while no water shader existed, because drawing it as a flat grey
## slab would read as land; `wotr_map_water.gdshader` now implements the same
## procedural claim retail's `WaterShader.FX` makes, so the list is empty. It is
## kept (and the field it feeds is kept) so a future retail surface with an
## unimplemented shader has somewhere honest to go.
const SHADER_ONLY_SUB_OBJECTS: Array[String] = []

## Sub-objects retail uses as INVISIBLE gameplay volumes - the impassable
## barriers armies cannot cross. They carry no texture and no UVs because they
## were never meant to be seen. Hidden, not deleted: they are part of the map and
## a later lane will want them for movement rules.
const COLLISION_PREFIX := "IMPASSABLE"

var loaded := false
var bundle_root := ""
## Every refusal, in order. Never empty on a false return from `load_from()`.
var errors: PackedStringArray = PackedStringArray()
## Everything that went WRONG without stopping the load: a declared texture the
## converter never resolved, a resolved texture whose bytes would not decode.
## A load can succeed with a non-empty `warnings`, and when it does the screen
## and the log both say so - a map missing a third of its textures still looks
## like a map, which is exactly why it has to be said out loud.
var warnings: PackedStringArray = PackedStringArray()
## One row per candidate root `locate_and_load()` actually looked at:
## {root, origin, verdict, detail}. `verdict` is one of "loaded", "absent",
## "unreadable". This is what turns "no map" into an actionable sentence.
var searched_roots: Array[Dictionary] = []

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
## then the user-data workspace location. Each candidate carries WHERE IT CAME
## FROM, because "searched five paths" is only actionable when the owner can see
## which one was his environment variable and which one the game chose itself.
static func candidate_sources(pack_roots: Array = []) -> Array[Dictionary]:
	var candidates: Array[Dictionary] = []
	for root_value in pack_roots:
		var root := String(root_value).strip_edges()
		if not root.is_empty():
			candidates.append({
				"root": root.path_join(PACK_BUNDLE_RELATIVE),
				"origin": "mounted content pack %s" % root.get_file(),
			})
	var override := OS.get_environment(BUNDLE_ENV).strip_edges()
	if not override.is_empty():
		candidates.append({"root": override, "origin": "%s" % BUNDLE_ENV})
	candidates.append({"root": USER_BUNDLE, "origin": "user-data workspace"})
	return candidates


## The same candidate list as paths alone, kept for callers that only need the
## order.
static func candidate_roots(pack_roots: Array = []) -> PackedStringArray:
	var roots := PackedStringArray()
	for row in candidate_sources(pack_roots):
		roots.append(String(row["root"]))
	return roots


## A CHEAP look: which candidate roots carry a manifest at all, without reading
## 2.5 MB of mesh or 48 textures. Returns `{found, root, origin, rows}` where
## each row is {root, origin, present}. This is what lets the game say at STARTUP
## whether retail's 3D map will be there, instead of the owner discovering it by
## noticing the map looks wrong.
static func probe(pack_roots: Array = []) -> Dictionary:
	var rows: Array[Dictionary] = []
	var found_root := ""
	var found_origin := ""
	for row in candidate_sources(pack_roots):
		var root := String(row["root"])
		var present := FileAccess.file_exists(root.path_join("manifest.json"))
		rows.append({"root": root, "origin": String(row["origin"]), "present": present})
		if present and found_root.is_empty():
			found_root = root
			found_origin = String(row["origin"])
	return {
		"found": not found_root.is_empty(),
		"root": found_root,
		"origin": found_origin,
		"rows": rows,
	}


## Load the first candidate that carries a READABLE bundle. Returns
## `{ok, root, origin, reason}` and, when it finds nothing, names every place it
## looked, what was wrong with each, and the command that produces one - because
## "no map" alone sends nobody anywhere.
##
## A candidate that carries a manifest but does not load NO LONGER ENDS THE
## SEARCH. It used to, and that made a single stale bundle in a mounted pack able
## to hide a perfectly good one behind it with no way for the owner to tell.
## Every candidate is now tried and every verdict is reported.
func locate_and_load(pack_roots: Array = []) -> Dictionary:
	searched_roots = []
	for row in candidate_sources(pack_roots):
		var root := String(row["root"])
		var origin := String(row["origin"])
		if not FileAccess.file_exists(root.path_join("manifest.json")):
			searched_roots.append({
				"root": root, "origin": origin,
				"verdict": "absent", "detail": "no manifest.json here",
			})
			continue
		if load_from(root):
			searched_roots.append({
				"root": root, "origin": origin,
				"verdict": "loaded", "detail": "",
			})
			return {"ok": true, "root": root, "origin": origin, "reason": ""}
		searched_roots.append({
			"root": root, "origin": origin, "verdict": "unreadable",
			"errors": ", ".join(Array(errors)),
			"detail": "manifest is present but the bundle did not load: %s"
				% ", ".join(Array(errors)),
		})
	return {
		"ok": false,
		"root": "",
		"origin": "",
		"reason": describe_search_failure(),
	}


## The player-facing and log-facing sentence for "there is no 3D map". It names
## every candidate, its origin, its verdict, the environment variable and the
## command that produces a bundle - the same shape as
## `wotr_session.locate_document`'s refusal, which is the message that let the
## owner diagnose the document problem himself.
func describe_search_failure() -> String:
	var lines: Array[String] = []
	lines.append(
		"Retail's 3D strategic map is NOT loaded, so the strategic screen is "
		+ "drawing its flat 2D region graph instead.")
	# THE ACTIONABLE LINES COME FIRST, deliberately. A bundle that is PRESENT and
	# broken is a different problem from one that was never converted, and it is
	# the one the owner can fix in a minute - so it goes above the audit trail
	# rather than under a list of paths that pushes it off the panel.
	for row in searched_roots:
		if String(row["verdict"]) != "unreadable":
			continue
		lines.append("A bundle IS present at %s [%s], and it did NOT load: %s" % [
			String(row["root"]), String(row["origin"]), String(row.get("errors", ""))])
	lines.append(
		"Convert one with `python -m openbfme_importer.livingmap_bundle "
		+ "<catalog>/rotwk.json <bundle-dir>` and point %s at <bundle-dir>."
		% BUNDLE_ENV)
	lines.append("Looked in %d place(s), in this order:" % searched_roots.size())
	for row in searched_roots:
		var detail := String(row["detail"])
		lines.append("  - %s  [%s]  %s" % [
			String(row["root"]), String(row["origin"]),
			detail if not detail.is_empty() else String(row["verdict"])])
	return "\n".join(lines)


## One line per thing that is true about the loaded map, for the log. Never
## silent: a map that loaded with three textures missing says so here.
func describe_load() -> PackedStringArray:
	var lines := PackedStringArray()
	if not loaded:
		lines.append("no bundle loaded")
		return lines
	lines.append("root %s" % bundle_root)
	lines.append("source livingmap.w3d sha256 %s" % source_sha256)
	lines.append("%d sub-objects" % sub_objects.size())
	if warnings.is_empty():
		lines.append("every declared texture resolved and decoded")
	else:
		lines.append("%d texture problem(s): %s" % [
			warnings.size(), ", ".join(Array(warnings))])
	if not untextured_sub_objects.is_empty():
		lines.append("%d sub-object(s) drawn UNTEXTURED: %s" % [
			untextured_sub_objects.size(),
			", ".join(Array(untextured_sub_objects))])
	return lines


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
			warnings.append("%s: the converter never resolved it in the catalog" % declared)
			continue
		var texture_path := root.path_join(String(row.get("file", "")))
		var attempt := _load_image(texture_path)
		var image: Image = attempt["image"]
		if image == null:
			# A texture the converter DID resolve but this loader cannot decode is
			# a real defect, not a missing asset. Say which, and carry on
			# untextured rather than pretending the pixel data exists.
			missing.append("%s (present but undecodable)" % declared)
			warnings.append("%s: %s (%s)" % [declared, String(attempt["reason"]), texture_path])
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
		# A presentation surface is not "untextured" in the defect sense: WATER
		# binds no texture because RETAIL binds none - its colour is procedural
		# there and procedural here - so listing it beside a landmark that lost
		# its art would be a false alarm.
		var reportable := (not bool(built["collision"])
			and not bool(built["shader_only"])
			and String(built["surface"]).is_empty())
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
	warnings = PackedStringArray()
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


## Returns `{image, reason}`. `image` is null exactly when the load failed, and
## `reason` then says WHICH failure it was - a missing file, an oversized one, a
## format this loader does not read, or bytes the decoder rejected. A null with
## no reason is how a texture goes missing without anyone finding out.
func _load_image(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {"image": null, "reason": "cannot open the texture file (error %d)" % FileAccess.get_open_error()}
	if file.get_length() > TEXTURE_MAX_BYTES:
		var oversized := file.get_length()
		file.close()
		return {"image": null, "reason": "texture is %d bytes, over the %d limit" % [oversized, TEXTURE_MAX_BYTES]}
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
	else:
		return {"image": null, "reason": "this loader does not read .%s textures" % extension}
	if status != OK:
		return {"image": null, "reason": "the .%s decoder rejected %d bytes (error %d)" % [
			extension, bytes.size(), status]}
	return {"image": image, "reason": ""}


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

	# NORMALS ARE DERIVED FROM RETAIL'S OWN TRIANGLES, not read from the file,
	# and the authored ones are deliberately not used. The living map is drawn
	# LIT now - a real key light is what gives the Misty Mountains and Mordor
	# their relief - and under a light the authored normals showed whole terrain
	# tiles black, because W3D strategic-map normals were authored for a renderer
	# that never lit them. An area-weighted accumulation over the exact triangles
	# retail shipped is derivation, not invention: every input is a retail vertex.
	# (`normal_offset` stays validated so a truncated bundle still fails loudly.)
	if bool(row.get("hasNormals", false)):
		var normal_bytes := vertex_count * 3 * 4
		if normal_offset < 0 or normal_offset + normal_bytes > blob.size():
			_fail("sub-object %s normals run past the mesh block" % name)
			return {}

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
	# The derived normals follow the FLIPPED winding, so they face the same way
	# the visible faces do by construction.
	arrays[Mesh.ARRAY_NORMAL] = _derive_normals(vertices, flipped)

	var mesh := ArrayMesh.new()
	mesh.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)

	var material := StandardMaterial3D.new()
	# LIT, deliberately, and the reversal of the old "unshaded" rule is stated:
	# retail's terrain textures do carry painted light, but the retail screen
	# still renders the map under a warm strategic key light - the reference
	# captures show relief shading and a sun direction the flat blit never had.
	# The material is fully rough with no metallic response, so the light MODELS
	# the ground rather than putting a plastic sheen on painted art.
	material.roughness = 1.0
	material.metallic = 0.0
	material.metallic_specular = 0.05
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

	# EVERY RESOLVED STAGE TEXTURE, by retail's declared name, not only the base.
	# The coast and smoke presentation surfaces are MULTI-STAGE materials -
	# stage 0 and stage 1 are both retail's - and the shader that composites
	# them needs both by name. Rows that are not presentation surfaces simply
	# carry the same references; nothing is duplicated.
	var stage_textures: Dictionary = {}
	var stage_order: Array[String] = []
	for declared_value in row.get("textures", []) as Array:
		stage_order.append(String(declared_value))
		var stage_texture: ImageTexture = textures.get(String(declared_value), null)
		if stage_texture != null:
			stage_textures[String(declared_value)] = stage_texture

	var collision := name.begins_with(COLLISION_PREFIX)
	var built := {
		"name": name,
		"mesh": mesh,
		"material": material,
		"textured": textured,
		"stage_textures": stage_textures,
		# Retail's declared stage order, kept even for stages that did not
		# resolve, so a refusal can name exactly which stage is missing.
		"stage_order": stage_order,
		"ambient": AMBIENT_SUB_OBJECTS.has(name) or RIVER_OVERLAY_SUB_OBJECTS.has(name),
		"collision": collision,
		"shader_only": SHADER_ONLY_SUB_OBJECTS.has(name),
		# Which presentation treatment the map view stands this surface under,
		# or "" for ordinary textured geometry. See PRESENTATION_SURFACES.
		"surface": String(PRESENTATION_SURFACES.get(name, "")),
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


## Area-weighted vertex normals over the exact triangles retail shipped. The
## cross product of each triangle's edges is proportional to its area, so simply
## SUMMING the unnormalised face normals into each corner and normalising at the
## end weights big faces more than slivers - the standard smooth-normal
## derivation, over retail's own geometry, with nothing chosen by eye.
static func _derive_normals(
	vertices: PackedVector3Array, indices: PackedInt32Array
) -> PackedVector3Array:
	var normals := PackedVector3Array()
	normals.resize(vertices.size())
	var i := 0
	while i + 2 < indices.size():
		var a := vertices[indices[i]]
		var b := vertices[indices[i + 1]]
		var c := vertices[indices[i + 2]]
		# (c-a) x (b-a), NOT the other order: measured on the shipped bundle,
		# this is the order that puts the terrain's derived normals UP under the
		# flipped winding - the other one lit Middle-earth by ambient alone and
		# read as dusk everywhere.
		var face := (c - a).cross(b - a)
		normals[indices[i]] += face
		normals[indices[i + 1]] += face
		normals[indices[i + 2]] += face
		i += 3
	for v in normals.size():
		var length := normals[v].length()
		# A vertex only degenerate triangles touch has no derivable direction;
		# straight up is the one direction that cannot invert terrain lighting.
		normals[v] = normals[v] / length if length > 0.000001 else Vector3.UP
	return normals


func _vector_of(value: Variant) -> Vector3:
	var list := value as Array
	if list == null or list.size() < 3:
		return Vector3.ZERO
	return Vector3(float(list[0]), float(list[1]), float(list[2]))
