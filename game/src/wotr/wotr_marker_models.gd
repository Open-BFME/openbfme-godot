extends RefCounted

## RETAIL'S 3D LIVING-WORLD MARKERS, LOADED. This reads the
## `openbfme.living-world-markers` bundle produced by
## `openbfme_importer.livingworld_markers` from retail's own marker models - the
## army banners, the marching "army ant" columns, the structure icons and the
## faction foundation decals - and turns them into Godot meshes and materials.
##
## WHY IT EXISTS. The strategic screen drew every army as a FLAT PLATE and every
## build plot as a FLAT RING, and said so on screen. Retail stands MODELS on the
## map. Retail's own marker documents name 81 distinct W3D models across 75
## families and 506 slots, and every one of them is in the archives.
##
## WHAT IT IS NOT: a model format, a converter, or a guess. Every vertex in
## `markers.bin` is a retail vertex with retail's own HLOD bone transform
## applied, and every texture is retail's payload byte-for-byte.
##
## THE RULES IT KEEPS:
##
## * FAIL LOUDLY, NEVER PARTIALLY. `load_from()` returns false and fills `errors`
##   the moment the manifest or the mesh block does not add up.
##
## * A MODEL THAT DID NOT CONVERT IS SAID, NOT SUBSTITUTED. The converter marks
##   it `resolved: false` with a reason; this file carries that through to
##   `unresolved_models` and returns NO mesh for it, so the map view keeps that
##   marker's flat plate or ring and names the retail model on screen. Nothing
##   else is ever stood up in its place.
##
## * A TEXTURE THAT DID NOT RESOLVE IS SAID, NOT SUBSTITUTED. The mesh is drawn
##   with a flat, obviously-untextured material and the declared name is listed.
##
## * IT PLACES NOTHING AND DECIDES NOTHING. It knows about geometry and retail's
##   authored slot table. It has no idea what a region, an army or a commitment
##   is, and cannot reach one.

const SCHEMA := "openbfme.living-world-markers"
const SCHEMA_VERSION := 1

## The environment override that points at a bundle, mirroring the
## `OPENBFME_LIVING_MAP` convention the strategic lane already documents.
const BUNDLE_ENV := "OPENBFME_LIVING_WORLD_MARKERS"
const FILE_NAME := "living-world-markers.json"

const MANIFEST_MAX_BYTES := 32 * 1024 * 1024
const MESH_MAX_BYTES := 128 * 1024 * 1024
const TEXTURE_MAX_BYTES := 64 * 1024 * 1024

var loaded := false
var bundle_root := ""
var source_path := ""
var errors: PackedStringArray = PackedStringArray()
## Everything that went wrong WITHOUT stopping the load. A load can succeed with
## a non-empty `warnings`, and when it does the screen and the log both say so.
var warnings: PackedStringArray = PackedStringArray()

## `family id -> record`, retail's own block with its authored slots in order.
var families: Dictionary = {}
## `model name -> {meshes: {MESH NAME -> {mesh: ArrayMesh, material, textured}},
## order: PackedStringArray}`. Only converted models appear.
var models: Dictionary = {}
## Models retail names that this bundle does NOT carry geometry for, with the
## converter's reason. Public so the screen can name every flat stand-in.
var unresolved_models: Dictionary = {}
## Declared textures the converter could not resolve, by name.
var unresolved_textures: PackedStringArray = PackedStringArray()
var totals: Dictionary = {}
var gaps: Dictionary = {}

var _texture_cache: Dictionary = {}


static func candidate_paths(roots: Array = []) -> Array[Dictionary]:
	var candidates: Array[Dictionary] = []
	var override := OS.get_environment(BUNDLE_ENV).strip_edges()
	if not override.is_empty():
		# The environment may name the directory or the file itself; both are
		# accepted so one setting cannot be "right but spelled the other way".
		var direct := override if override.ends_with(FILE_NAME) else override.path_join(FILE_NAME)
		candidates.append({"path": direct, "origin": BUNDLE_ENV})
	for root in roots:
		var text := String(root).strip_edges()
		if text.is_empty():
			continue
		candidates.append({
			"path": text.path_join(FILE_NAME),
			"origin": "beside the map bundles",
		})
	return candidates


## Load from the first candidate that parses. Returns `{ok, path, reason}`, and a
## failure is always a REASON naming every path tried and the command that makes
## a bundle - "no models" alone sends nobody anywhere.
func locate_and_load(roots: Array = []) -> Dictionary:
	var tried: Array[String] = []
	for candidate in candidate_paths(roots):
		var path := String(candidate["path"])
		tried.append("%s [%s]" % [path, String(candidate["origin"])])
		if not FileAccess.file_exists(path):
			continue
		if load_from(path):
			return {"ok": true, "path": path, "reason": ""}
	return {
		"ok": false, "path": "",
		"reason": (
			"NO LIVING-WORLD MARKER BUNDLE, so army stacks are drawn as flat "
			+ "portrait plates and build plots as flat rings instead of retail's "
			+ "own 3D banners and foundation decals. Looked at: %s. Produce one "
			+ "with: python -m openbfme_importer.livingworld_markers --catalog "
			+ "<catalog>.json --out <dir>, then point %s at <dir>.") % [
				"; ".join(tried) if not tried.is_empty() else "nowhere", BUNDLE_ENV],
	}


func load_from(path: String) -> bool:
	_reset()
	source_path = path
	bundle_root = path.get_base_dir()

	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return _fail("cannot open %s" % path)
	if file.get_length() > MANIFEST_MAX_BYTES:
		var oversized := file.get_length()
		file.close()
		return _fail("%s is %d bytes, over the %d limit" % [path, oversized, MANIFEST_MAX_BYTES])
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if not (parsed is Dictionary):
		return _fail("%s is not a JSON object" % path)
	var manifest: Dictionary = parsed
	if String(manifest.get("schema", "")) != SCHEMA:
		return _fail("schema is not %s" % SCHEMA)
	if int(manifest.get("schemaVersion", -1)) != SCHEMA_VERSION:
		return _fail("unsupported schemaVersion %d" % int(manifest.get("schemaVersion", -1)))

	totals = manifest.get("totals", {}) as Dictionary
	gaps = manifest.get("gaps", {}) as Dictionary

	# --- mesh block ------------------------------------------------------
	var mesh_info: Dictionary = manifest.get("meshBin", {}) as Dictionary
	var mesh_path := bundle_root.path_join(String(mesh_info.get("file", "markers.bin")))
	var mesh_file := FileAccess.open(mesh_path, FileAccess.READ)
	if mesh_file == null:
		return _fail("cannot open %s" % mesh_path)
	if mesh_file.get_length() > MESH_MAX_BYTES:
		var big := mesh_file.get_length()
		mesh_file.close()
		return _fail("mesh block is %d bytes, over the %d limit" % [big, MESH_MAX_BYTES])
	var declared_bytes := int(mesh_info.get("bytes", -1))
	if declared_bytes >= 0 and mesh_file.get_length() != declared_bytes:
		var actual := mesh_file.get_length()
		mesh_file.close()
		return _fail("mesh block is %d bytes, manifest declares %d" % [actual, declared_bytes])
	var blob := mesh_file.get_buffer(mesh_file.get_length())
	mesh_file.close()

	# --- textures --------------------------------------------------------
	var texture_directory := String(manifest.get("textureDirectory", "marker-textures"))
	var missing: Array[String] = []
	for row_value in manifest.get("textures", []) as Array:
		var row := row_value as Dictionary
		if not bool(row.get("resolved", false)):
			missing.append(String(row.get("declared", "?")))
			warnings.append("%s: the converter never resolved it in the catalog"
				% String(row.get("declared", "?")))
	missing.sort()
	unresolved_textures = PackedStringArray(missing)

	# --- models ----------------------------------------------------------
	for row_value in manifest.get("models", []) as Array:
		var row := row_value as Dictionary
		var name := String(row.get("model", ""))
		if name.is_empty():
			return _fail("a model record carries no name")
		if not bool(row.get("resolved", false)):
			unresolved_models[name] = String(row.get("reason", "no reason was recorded, which is itself a defect"))
			continue
		var built := _build_model(name, row, blob, texture_directory)
		if built.is_empty():
			return false
		models[name] = built

	if models.is_empty() and unresolved_models.is_empty():
		return _fail("the bundle declares no models at all")

	for row_value in manifest.get("families", []) as Array:
		var row := row_value as Dictionary
		var id := String(row.get("id", ""))
		if id.is_empty():
			return _fail("a marker family carries no id")
		families[id] = row
	if families.is_empty():
		return _fail("the bundle declares no marker families")

	loaded = true
	return true


## One line per fact worth printing at load. Never silent: a bundle that loaded
## with three models unresolved says so here, by name.
func describe_load() -> PackedStringArray:
	var lines: Array[String] = []
	if not loaded:
		lines.append("no marker bundle loaded")
		return PackedStringArray(lines)
	lines.append("root %s" % bundle_root)
	lines.append("%d of %d retail marker models converted: %d meshes, %d triangles" % [
		int(totals.get("modelsConverted", 0)), int(totals.get("modelsNamed", 0)),
		int(totals.get("meshes", 0)), int(totals.get("triangles", 0))])
	lines.append("%d families (%s), %d slots" % [
		int(totals.get("families", 0)),
		_kinds_line(), int(totals.get("slots", 0))])
	if unresolved_models.is_empty():
		lines.append("every model retail names converted")
	else:
		var named: Array[String] = []
		for key in unresolved_models.keys():
			named.append("%s (%s)" % [String(key), String(unresolved_models[key])])
		named.sort()
		lines.append("MODELS NOT CONVERTED (%d), drawn as a flat stand-in and named on screen: %s" % [
			named.size(), ", ".join(named)])
	if unresolved_textures.is_empty():
		lines.append("%d of %d textures resolved" % [
			int(totals.get("texturesResolved", 0)), int(totals.get("texturesDeclared", 0))])
	else:
		lines.append("TEXTURES NOT RESOLVED (%d): %s" % [
			unresolved_textures.size(), ", ".join(Array(unresolved_textures))])
	# THE ONE RETAIL FIELD THIS LANE READS AND CANNOT ACT ON, counted rather than
	# asserted in prose. See `HOUSE_COLOUR_GAP`.
	var wanting := slots_wanting_house_color()
	if not wanting.is_empty():
		var slot_total := 0
		for key in wanting.keys():
			slot_total += (wanting[key] as Array).size()
		lines.append("GAP: %d slot(s) across %d famil(ies) ask for a house colour. %s" % [
			slot_total, wanting.size(), HOUSE_COLOUR_GAP])
	return PackedStringArray(lines)


func _kinds_line() -> String:
	var by_kind: Dictionary = totals.get("familiesByKind", {}) as Dictionary
	var keys: Array[String] = []
	for key in by_kind.keys():
		keys.append(String(key))
	keys.sort()
	var parts: Array[String] = []
	for key in keys:
		parts.append("%s %d" % [key, int(by_kind[key])])
	return ", ".join(parts)


# --- what the map view asks for -------------------------------------------------

func has_family(family_id: String) -> bool:
	return loaded and families.has(family_id)


## One slot of one family, exactly as retail authored it, or `{}`.
func slot(family_id: String, slot_name: String) -> Dictionary:
	var family: Dictionary = families.get(family_id, {}) as Dictionary
	for row_value in family.get("slots", []) as Array:
		var row := row_value as Dictionary
		if String(row.get("slot", "")) == slot_name:
			return row
	return {}


## The slot retail reads a marker of this family off the map by - `Banner` for an
## army, `Building` for a structure, `Decal` for a build plot. RETAIL'S OWN NAME,
## carried in the manifest rather than restated here.
func body_slot(family_id: String) -> Dictionary:
	var family: Dictionary = families.get(family_id, {}) as Dictionary
	var name := String(family.get("bodySlot", ""))
	if name.is_empty():
		return {}
	return slot(family_id, name)


## The drawable pieces of one slot: `[{mesh: ArrayMesh, material, textured,
## name}]`, in the model's own mesh order, filtered to the slot's `SubObjects`
## when it names any and the whole model when it does not.
##
## An EMPTY ARRAY means retail's model for this slot is not in the bundle. The
## caller MUST then draw its own stand-in and say which model is absent - it must
## never fall back to another model's geometry.
func slot_pieces(family_id: String, slot_name: String) -> Array[Dictionary]:
	return pieces_of(slot(family_id, slot_name))


## The same, for a slot record the caller already has.
##
## RETAIL'S OWN `FadeMethod` IS HONOURED HERE. Every `Selected` slot retail
## authors - the SFE_ArmyShine / SFE_HeroShine light-streak cylinders - carries
## `FadeMethod = Additive`, retail's statement that the shine RENDERS
## additively. Built with the shared lit alpha-scissor material those models
## came out as solid grey plastic tubes standing on a selection, because a
## light streak's texture is glow painted on black and scissoring it at 0.5
## keeps the black. An additive slot gets an ADDITIVE variant of its meshes'
## materials: same retail texture, unshaded, summed into the scene, which is
## what the field says and what makes the shine SHINE. The variants are cached
## per model so a rebuild does not mint materials every frame.
func pieces_of(slot_row: Dictionary) -> Array[Dictionary]:
	var pieces: Array[Dictionary] = []
	if slot_row.is_empty():
		return pieces
	var model_name := String(slot_row.get("model", ""))
	var model: Dictionary = models.get(model_name, {}) as Dictionary
	if model.is_empty():
		return pieces
	var additive := slot_fade_additive(slot_row)
	var wanted: Array[String] = []
	for value in slot_row.get("subObjectList", []) as Array:
		wanted.append(String(value).to_upper())
	var by_mesh: Dictionary = model["meshes"] as Dictionary
	for mesh_name in model["order"] as PackedStringArray:
		if not wanted.is_empty() and not wanted.has(String(mesh_name).to_upper()):
			continue
		var piece := by_mesh[mesh_name] as Dictionary
		if additive:
			piece = piece.duplicate()
			piece["material"] = _additive_material_of(model_name, piece)
		pieces.append(piece)
	return pieces


## True when retail's own `FadeMethod` for a slot says the art is additive.
static func slot_fade_additive(slot_row: Dictionary) -> bool:
	return String(slot_row.get("fadeMethod", "")).to_lower() == "additive"


## RETAIL'S OWN `UseHouseColor` FIELD, AND THE HALF OF IT THAT IS NOW APPLIED.
##
## Every army banner slot in `data/ini/livingworldicons/*.ini` carries
## `UseHouseColor = Yes` - `LWArmyHAng`'s `Banner`, `SmallPip` and `MediumPip`
## slots all do - which is SAGE's instruction to recolour part of the model with
## the owning player's house colour. Round 5 recorded that as wholly
## undeterminable, on the reading that the mask "lives in the renderer".
##
## ROUND 6 MEASURED THE MESHES AND HALF OF THAT WAS WRONG. Read `LWArmyHAng`'s
## four sub-objects' own texture coordinates out of `markers.bin`:
##
##   LWBANNER     LWArmyHAng.tga        u 7..125,  v 2..126  of 128   (94 tris)
##   LWSTAFF      LWBanHWitckKing.tga   u 1..12,   v 69..127 of 128  (275 tris)
##   LWBANSMALL   LWBanHWitckKing.tga   u 126..127, v 126..127       ( 12 tris)
##   LWBANMEDIUM  LWBanHWitckKing.tga   u 126..127, v 125..127       ( 24 tris)
##   LWBANLARGE   LWBanHWitckKing.tga   u 126..127, v 126..128       ( 36 tris)
##
## The three PIP meshes have their UVs COLLAPSED onto a one-to-two texel block in
## the corner of the sheet, and every texel in that block is bit-identical
## rgb(244, 166, 21) - a saturated orange. `LWArmyHMoW`, a different faction on a
## different sheet, does the same thing at a different address: its pips sample
## texels 106..118 x 104..123, and every one of those 228 texels is the SAME
## rgb(244, 166, 21). One flat colour, shared across factions, sampled only by
## the geometry whose slot asks for a house colour.
##
## That is not a texture, it is a SWATCH: retail authored the house-coloured
## geometry so that its whole surface samples one flat colour, which is all a
## renderer needs in order to replace it. A blind review of round 6 reported the
## army markers as "flat orange rectangles ... untextured", and that is exactly
## what it was looking at: retail's own pips, flying the unreplaced swatch.
##
## SO THE PIPS ARE RECOLOURED AND THE CLOTH IS NOT, and neither half is invented.
## `_flat_swatch()` measures the property from the shipped mesh and the shipped
## texture - a mesh whose whole UV footprint lands on a small patch of
## identical texels - and `house_coloured_material_of()` replaces that colour
## with the seat's. `LWBANNER` fails that test by a mile (it covers 118x124
## texels of painted crest), so the cloth keeps retail's own art untinted and the
## gap below is narrowed to it rather than closed.
const HOUSE_COLOUR_GAP := "LivingWorldArmyIcon slots author UseHouseColor = Yes (LWArmyHAng Banner/SmallPip/MediumPip and their siblings). The PIP meshes are recoloured from shipped evidence: their UVs collapse onto a small block of identical rgb(244,166,21) - 2x2 texels for LWArmyHAng, 12x19 for LWArmyHMoW, the same colour on both sheets - which is retail's house-colour swatch, so the seat's colour replaces it exactly. The BANNER CLOTH is not: LWBANNER covers 118x124 texels of painted faction crest, nothing in the .ini or the converted W3D says which of those texels or which channel SAGE replaces, and no tint is invented over retail's art. 23 of the 36 families that ask for a house colour carry a swatch clean enough to measure, and every one of them lands on the SAME rgb(244,166,22) with a per-channel spread of exactly zero - the swatch is one authored colour across retail's whole marker set, not per-family art. The other 13 do NOT share it and were re-measured rather than assumed: LWBanHDale / LWBanHDain / LWBanHGimli / LWBanHGloin spread 0.84 of the red channel over their patch, LWBanHElrond / LWBanHGlorf / LWBanHHaldir / LWBanHThranduil spread 0.97, and LWBanHAragorn / LWBanHBoromir / LWBanHEomer / LWBanHTheoden spread 0.41 of the blue. A patch spanning most of a channel is PAINTED ART under those UVs, not codec noise on a flat colour, and replacing it wholesale would destroy retail's own crest - so twelve of the thirteen are not a tolerance problem at all and no tolerance can honestly admit them. The thirteenth, LWArmyHDwf, spreads 0.16 of green about rgb(254,164,16); it fails by less, and it is still the only one that would be bought by loosening the guard, which is not a trade worth making for one family. All thirteen therefore keep retail's texture rather than have a value guessed at as the house colour. Every seat's cloth is therefore still the same, and 23 families' pips are not."


## Every marker slot that asks for a house colour this lane cannot apply, as
## `family -> [slot, ...]`. Counted from the loaded bundle rather than from
## `HOUSE_COLOUR_GAP`'s prose, so the sentence can never outlive the fact.
func slots_wanting_house_color() -> Dictionary:
	var wanting: Dictionary = {}
	var keys: Array = families.keys()
	keys.sort()
	for key in keys:
		var rows: Array[String] = []
		for slot_value in (families[key] as Dictionary).get("slots", []) as Array:
			var slot_row := slot_value as Dictionary
			if String(slot_row.get("useHouseColor", "")).to_lower() == "yes":
				rows.append(String(slot_row.get("slot", "")))
		if not rows.is_empty():
			wanting[String(key)] = rows
	return wanting


## The additive variant of one mesh's material, cached. The texture reference
## is the same retail texture the lit material binds; only the blend and the
## shading change, exactly what `FadeMethod = Additive` authorises.
var _additive_material_cache: Dictionary = {}

func _additive_material_of(model_name: String, piece: Dictionary) -> StandardMaterial3D:
	var key := "%s/%s" % [model_name, String(piece["name"])]
	if _additive_material_cache.has(key):
		return _additive_material_cache[key]
	var source := piece["material"] as StandardMaterial3D
	var material := StandardMaterial3D.new()
	material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	material.blend_mode = BaseMaterial3D.BLEND_MODE_ADD
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	# Additive art must not write depth or its invisible black regions would
	# hole out everything drawn behind them.
	material.depth_draw_mode = BaseMaterial3D.DEPTH_DRAW_DISABLED
	material.albedo_texture = source.albedo_texture
	material.albedo_color = source.albedo_color
	_additive_material_cache[key] = material
	return material


## The retail model a slot names, whether or not it converted. Used by the screen
## to NAME what a flat stand-in is standing in for.
func slot_model(family_id: String, slot_name: String) -> String:
	return String(slot(family_id, slot_name).get("model", ""))


## Retail's `Scale` for a slot, or 1.0 when it authors none.
static func slot_scale(slot_row: Dictionary) -> float:
	var text := String(slot_row.get("scale", ""))
	return float(text) if text.is_valid_float() else 1.0


## Retail's `ZOffset` for a slot, in retail world units, or 0.0.
static func slot_z_offset(slot_row: Dictionary) -> float:
	var text := String(slot_row.get("zOffset", ""))
	return float(text) if text.is_valid_float() else 0.0


## Retail's `OrientAngle` for a slot, in RADIANS about the map's up axis, or 0.
static func slot_orient_radians(slot_row: Dictionary) -> float:
	var text := String(slot_row.get("orientAngle", ""))
	return deg_to_rad(float(text)) if text.is_valid_float() else 0.0


## True when retail marks a slot as only visible while the marker is hovered.
static func slot_hover_only(slot_row: Dictionary) -> bool:
	return String(slot_row.get("hideWhenUnhilighted", "")).to_lower() == "yes"


## True when retail marks a slot as only visible while the marker is selected.
static func slot_selection_only(slot_row: Dictionary) -> bool:
	return String(slot_row.get("hideWhenUnselected", "")).to_lower() == "yes"


## Retail's `VisibleArmySizes` for a slot as an upper-case list. An EMPTY list is
## retail saying "always", not "never".
static func slot_army_sizes(slot_row: Dictionary) -> PackedStringArray:
	var sizes := PackedStringArray()
	for token in String(slot_row.get("visibleArmySizes", "")).split(" ", false):
		var text := String(token).strip_edges().to_upper()
		if not text.is_empty():
			sizes.append(text)
	return sizes


# --- internals ---------------------------------------------------------------

func _reset() -> void:
	loaded = false
	bundle_root = ""
	source_path = ""
	errors = PackedStringArray()
	warnings = PackedStringArray()
	families = {}
	models = {}
	unresolved_models = {}
	unresolved_textures = PackedStringArray()
	totals = {}
	gaps = {}
	_texture_cache = {}
	_additive_material_cache = {}


func _fail(message: String) -> bool:
	errors.append(message)
	loaded = false
	return false


func _build_model(
	name: String, row: Dictionary, blob: PackedByteArray, texture_directory: String
) -> Dictionary:
	var by_mesh: Dictionary = {}
	var order: Array[String] = []
	for mesh_value in row.get("meshes", []) as Array:
		var mesh_row := mesh_value as Dictionary
		var built := _build_mesh(name, mesh_row, blob, texture_directory)
		if built.is_empty():
			return {}
		by_mesh[String(built["name"])] = built
		order.append(String(built["name"]))
	if by_mesh.is_empty():
		_fail("model %s declares no meshes" % name)
		return {}
	return {"meshes": by_mesh, "order": PackedStringArray(order)}


func _build_mesh(
	model: String, row: Dictionary, blob: PackedByteArray, texture_directory: String
) -> Dictionary:
	var name := String(row.get("mesh", ""))
	if name.is_empty():
		_fail("model %s carries a mesh with no name" % model)
		return {}
	var vertex_count := int(row.get("vertexCount", 0))
	var index_count := int(row.get("indexCount", 0))
	if vertex_count <= 0 or index_count <= 0:
		_fail("%s.%s declares %d vertices and %d indices" % [
			model, name, vertex_count, index_count])
		return {}

	var position_offset := int(row.get("positionOffset", -1))
	var position_bytes := vertex_count * 3 * 4
	var index_offset := int(row.get("indexOffset", -1))
	var index_bytes := index_count * 4
	if position_offset < 0 or position_offset + position_bytes > blob.size():
		_fail("%s.%s positions run past the mesh block" % [model, name])
		return {}
	if index_offset < 0 or index_offset + index_bytes > blob.size():
		_fail("%s.%s indices run past the mesh block" % [model, name])
		return {}

	var positions := blob.slice(position_offset, position_offset + position_bytes).to_float32_array()
	var indices := blob.slice(index_offset, index_offset + index_bytes).to_int32_array()
	for index in indices:
		if index < 0 or index >= vertex_count:
			_fail("%s.%s references vertex %d of %d" % [model, name, index, vertex_count])
			return {}

	# RETAIL MODEL SPACE IS Z-UP, exactly as the living map's is, so the SAME one
	# conversion is used - imported from the map bundle rather than restated, so a
	# marker and the ground it stands on can never disagree about which way is up.
	var vertices := PackedVector3Array()
	vertices.resize(vertex_count)
	for i in vertex_count:
		vertices[i] = Vector3(
			positions[i * 3], positions[i * 3 + 2], -positions[i * 3 + 1])

	var arrays := []
	arrays.resize(Mesh.ARRAY_MAX)
	arrays[Mesh.ARRAY_VERTEX] = vertices

	if bool(row.get("hasNormals", false)):
		var normal_offset := int(row.get("normalOffset", -1))
		var normal_bytes := vertex_count * 3 * 4
		if normal_offset < 0 or normal_offset + normal_bytes > blob.size():
			_fail("%s.%s normals run past the mesh block" % [model, name])
			return {}
		var raw := blob.slice(normal_offset, normal_offset + normal_bytes).to_float32_array()
		var normals := PackedVector3Array()
		normals.resize(vertex_count)
		for i in vertex_count:
			normals[i] = Vector3(raw[i * 3], raw[i * 3 + 2], -raw[i * 3 + 1]).normalized()
		arrays[Mesh.ARRAY_NORMAL] = normals

	if bool(row.get("hasUVs", false)):
		var uv_offset := int(row.get("uvOffset", -1))
		var uv_bytes := vertex_count * 2 * 4
		if uv_offset < 0 or uv_offset + uv_bytes > blob.size():
			_fail("%s.%s texture coordinates run past the mesh block" % [model, name])
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
	# LIT, in step with the living map: the map view now renders the strategic
	# world under a warm key light, and a marker left unshaded would float over
	# it like a sticker with no relationship to the sun everything under it
	# obeys. Fully rough with no metallic response, so the light models the
	# banner cloth rather than putting a sheen on painted art. A mesh retail
	# authored no normals for falls back to unshaded rather than lighting to
	# black - see below.
	material.roughness = 1.0
	material.metallic = 0.0
	material.metallic_specular = 0.1
	material.cull_mode = BaseMaterial3D.CULL_DISABLED
	if not bool(row.get("hasNormals", false)):
		material.shading_mode = BaseMaterial3D.SHADING_MODE_UNSHADED
	# ALPHA TEST, NOT ALPHA BLEND, and the difference was visible: retail's
	# marker textures are CUTOUTS - the army-ant columns and the foundation pads
	# carry a hard 0/255 alpha mask (mean alpha 75 and 82 over a fully-opaque
	# sigil), not a gradient. Blending them put every marker in the transparent
	# queue at render_priority 0, BEHIND the territory fills, which are
	# transparent at priority 1 with depth writing off - so every banner came out
	# as a pale blue ghost with Middle-earth painted over it. Alpha-tested, they
	# are in the opaque pass and write depth, which is both what retail's masks
	# mean and what puts a banner in front of the ground it stands on.
	material.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	material.alpha_scissor_threshold = 0.5
	var textured := false
	var candidates: Array[String] = []
	var base_value: Variant = row.get("baseTextureFile", null)
	if base_value != null and not String(base_value).is_empty():
		candidates.append(String(base_value))
	for value in row.get("textureFiles", []) as Array:
		if not candidates.has(String(value)):
			candidates.append(String(value))
	for relative in candidates:
		var texture := _texture(texture_directory, relative)
		if texture != null:
			material.albedo_texture = texture
			textured = true
			break
	if not textured:
		# NOT a stand-in for retail art: a flat, obviously-untextured grey that
		# reads as "this surface has no shipped texture bound".
		material.albedo_color = Color(0.32, 0.34, 0.30)

	# THE HOUSE-COLOUR SWATCH, measured off this mesh's own UVs. See
	# `HOUSE_COLOUR_GAP` for what it is and what it is not.
	var swatch := Color(0.0, 0.0, 0.0, 0.0)
	if textured and arrays[Mesh.ARRAY_TEX_UV] != null:
		swatch = _flat_swatch(
			material.albedo_texture, arrays[Mesh.ARRAY_TEX_UV] as PackedVector2Array)

	return {
		"name": name,
		"mesh": mesh,
		"material": material,
		"textured": textured,
		"house_swatch": swatch,
		"vertex_count": vertex_count,
		"triangle_count": int(row.get("triangleCount", 0)),
	}


## HOW BIG A PATCH A MESH MAY SAMPLE AND STILL COUNT AS SAMPLING A FLAT SWATCH,
## as a fraction of the texture on each axis. The property that matters is that
## every texel under the mesh is the SAME COLOUR; this is only the guard that
## stops a texture which happens to be flat overall from qualifying, so the patch
## has to be a small part of the sheet rather than the sheet.
##
## Measured, not chosen: retail's own house-coloured meshes range from
## `LWArmyHAng`'s pips (a 2x2 texel corner block, 1.6% of a 128-texel axis) to
## `LWArmyHMoW`'s (a 12x19 block, 15%). 0.25 clears the largest of them with room
## and is nowhere near the 92%-of-both-axes a painted banner covers.
const HOUSE_SWATCH_MAX_FRACTION := 0.25
## HOW FAR APART THE TEXELS IN THAT PATCH MAY SPREAD, per channel, and still be
## "one colour". Measured off the shipped sheets rather than chosen: on most
## faction textures the patch is BIT-IDENTICAL - `LWArmyHAng` and `LWArmyHMoW`
## both carry exactly rgb(244, 166, 21) across every texel the pips touch - but
## `LWArmyHDwf`'s is DXT-compressed at a block boundary and comes back as 28
## values spanning R 244..255, G 146..170, B 16..16. That is codec noise on one
## flat colour, and a threshold tight enough to reject it would have thrown away
## the Dwarven banner for a reason that has nothing to do with the art. 0.10 -
## 25 levels - clears the worst of it and is still an order of magnitude below
## anything painted: `LWBANNER`'s crest spans the full range on every channel.
const HOUSE_SWATCH_TOLERANCE := 0.10


## THE ONE COLOUR A MESH SAMPLES, when its whole UV footprint lands on a block
## of identical texels - and `Color(0, 0, 0, 0)` when it does not.
##
## This is the measurement `HOUSE_COLOUR_GAP` used to say could not be made. It
## is not a guess at a mask: it is the observation that retail authored the
## house-coloured geometry with its texture coordinates COLLAPSED onto a flat
## patch, which is the only thing a renderer needs in order to replace that
## colour wholesale.
func _flat_swatch(texture: ImageTexture, uvs: PackedVector2Array) -> Color:
	if texture == null or uvs.is_empty():
		return Color(0.0, 0.0, 0.0, 0.0)
	var image := texture.get_image()
	if image == null or image.get_width() <= 0 or image.get_height() <= 0:
		return Color(0.0, 0.0, 0.0, 0.0)
	# Retail's textures are DXT, and `get_pixel` refuses a compressed image. The
	# copy is decompressed rather than the texture's own image, so nothing the
	# renderer is holding is disturbed.
	if image.is_compressed():
		image = image.duplicate() as Image
		if image.decompress() != OK:
			return Color(0.0, 0.0, 0.0, 0.0)
	var low := Vector2(INF, INF)
	var high := Vector2(-INF, -INF)
	for uv in uvs:
		low = Vector2(minf(low.x, uv.x), minf(low.y, uv.y))
		high = Vector2(maxf(high.x, uv.x), maxf(high.y, uv.y))
	var width := image.get_width()
	var height := image.get_height()
	var x0 := clampi(int(floor(low.x * width)), 0, width - 1)
	var x1 := clampi(int(ceil(high.x * width)) - 1, 0, width - 1)
	var y0 := clampi(int(floor(low.y * height)), 0, height - 1)
	var y1 := clampi(int(ceil(high.y * height)) - 1, 0, height - 1)
	if float(x1 - x0 + 1) > float(width) * HOUSE_SWATCH_MAX_FRACTION 			or float(y1 - y0 + 1) > float(height) * HOUSE_SWATCH_MAX_FRACTION:
		return Color(0.0, 0.0, 0.0, 0.0)
	var low_channel := Vector3(INF, INF, INF)
	var high_channel := Vector3(-INF, -INF, -INF)
	var total := Vector3.ZERO
	var count := 0
	for x in range(x0, x1 + 1):
		for y in range(y0, y1 + 1):
			var here := image.get_pixel(x, y)
			var rgb := Vector3(here.r, here.g, here.b)
			low_channel = low_channel.min(rgb)
			high_channel = high_channel.max(rgb)
			total += rgb
			count += 1
	if count <= 0:
		return Color(0.0, 0.0, 0.0, 0.0)
	var spread := high_channel - low_channel
	if spread.x > HOUSE_SWATCH_TOLERANCE or spread.y > HOUSE_SWATCH_TOLERANCE 			or spread.z > HOUSE_SWATCH_TOLERANCE:
		return Color(0.0, 0.0, 0.0, 0.0)
	var mean := total / float(count)
	return Color(mean.x, mean.y, mean.z, 1.0)


## Whether one piece is a mesh retail flat-shades from a single texel, i.e. one
## a `UseHouseColor` slot can be recoloured wholesale.
static func piece_is_flat_swatch(piece: Dictionary) -> bool:
	return (piece.get("house_swatch", Color(0, 0, 0, 0)) as Color).a > 0.0


## THE HOUSE-COLOURED VARIANT of one flat-swatch mesh, cached per model, mesh and
## colour. The texture is DROPPED rather than tinted, and that is exact rather
## than approximate: the mesh samples one uniform texel, so a flat colour over
## the whole mesh is the same surface with a different colour in it, which is
## precisely what `UseHouseColor` asks for.
var _house_material_cache: Dictionary = {}

func house_coloured_material_of(
	model_name: String, piece: Dictionary, color: Color
) -> StandardMaterial3D:
	var key := "%s/%s/%s" % [model_name, String(piece["name"]), color.to_html(false)]
	if _house_material_cache.has(key):
		return _house_material_cache[key]
	var source := piece["material"] as StandardMaterial3D
	var material := StandardMaterial3D.new()
	material.shading_mode = source.shading_mode
	material.transparency = source.transparency
	material.alpha_scissor_threshold = source.alpha_scissor_threshold
	material.cull_mode = source.cull_mode
	material.roughness = source.roughness
	material.metallic = source.metallic
	material.metallic_specular = source.metallic_specular
	material.albedo_texture = null
	material.albedo_color = Color(color.r, color.g, color.b, 1.0)
	_house_material_cache[key] = material
	return material


func _texture(texture_directory: String, relative: String) -> ImageTexture:
	if _texture_cache.has(relative):
		return _texture_cache[relative] as ImageTexture
	# The manifest stores the path relative to the bundle root already, with the
	# directory in it; `texture_directory` is only used when it does not.
	var path := bundle_root.path_join(relative)
	if not FileAccess.file_exists(path):
		path = bundle_root.path_join(texture_directory).path_join(relative.get_file())
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		_texture_cache[relative] = null
		warnings.append("cannot open marker texture %s" % path)
		return null
	if file.get_length() > TEXTURE_MAX_BYTES:
		var oversized := file.get_length()
		file.close()
		_texture_cache[relative] = null
		warnings.append("marker texture %s is %d bytes, over the limit" % [path, oversized])
		return null
	var bytes := file.get_buffer(file.get_length())
	file.close()
	var picture := Image.new()
	var extension := path.get_extension().to_lower()
	var status := ERR_FILE_UNRECOGNIZED
	if extension == "dds":
		status = picture.load_dds_from_buffer(bytes)
	elif extension == "tga":
		status = picture.load_tga_from_buffer(bytes)
	elif extension == "jpg" or extension == "jpeg":
		status = picture.load_jpg_from_buffer(bytes)
	elif extension == "png":
		status = picture.load_png_from_buffer(bytes)
	else:
		_texture_cache[relative] = null
		warnings.append("this loader does not read .%s marker textures (%s)" % [extension, relative])
		return null
	if status != OK:
		_texture_cache[relative] = null
		warnings.append("the .%s decoder rejected %s (error %d)" % [extension, relative, status])
		return null
	var texture := ImageTexture.create_from_image(picture)
	_texture_cache[relative] = texture
	return texture
