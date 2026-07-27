extends RefCounted

## THE PORTRAITS RETAIL DRAWS ON A REGION CARD, loaded from the bundle
## `openbfme_importer.living_world_region_images` writes.
##
## WHY IT EXISTS. The region card named a region, its treasure, its plots and its
## territory in retail's own words and showed NO PICTURE. Retail's card carries
## one, and the living-world data says which: every region names a
## `RegionPortrait`, and ten of them also name a `Fortress.Portrait`. Both are
## `MappedImage` ids - an atlas name plus a pixel rectangle - and the converter
## resolves them through the SAME resolver the UI bundle uses, asked for a
## different id set.
##
## WHAT IT REFUSES TO DO. It NEVER substitutes a portrait. Retail names three
## fortress portraits it does not define anywhere - `BPCAmonSul`, `BPCCarnDum`
## and `BPCFornost` - and the nearest things in the archives are `BPCFornostGate`
## and `BPCFornostCitadel`, which are DIFFERENT IDS for different pictures.
## Those three draw an empty plate and are named on the card. Bridging them by
## name similarity would be exactly the fabrication this project refuses.
##
## THE CASE FOLD IS THE CONVERTER'S, NOT THIS FILE'S. Retail spells four region
## portraits one way in the region row and another in the image block
## (`LWPBarrowDOwns` vs `LWPBarrowDowns`), and INI ids are case-insensitive; the
## bundle carries the resolved spelling in its own field so nothing here folds
## anything and nothing here can fold its way onto a different picture.

const SCHEMA := "openbfme.living-world-region-images"
const SCHEMA_VERSION := 1

const BUNDLE_ENV := "OPENBFME_LIVING_WORLD_REGION_IMAGES"
const FILE_NAME := "living-world-region-images.json"
const MAX_BYTES := 16 * 1024 * 1024
const MAX_ATLAS_BYTES := 16 * 1024 * 1024

var loaded := false
var source_path := ""
var bundle_root := ""
var errors: PackedStringArray = PackedStringArray()

## `image id -> {atlas, left, top, right, bottom, textureWidth, textureHeight}`.
var images: Dictionary = {}
## `region id -> record`, carrying the requested and the resolved id of both
## portraits.
var regions: Dictionary = {}
var totals: Dictionary = {}
var gaps: Dictionary = {}
## Image ids asked for that produced no texture, and why, in ask order.
var missing_images: Dictionary = {}

var _atlas_directory := ""
var _atlas_cache: Dictionary = {}
var _crop_cache: Dictionary = {}


static func candidate_paths(roots: Array = []) -> Array[Dictionary]:
	var candidates: Array[Dictionary] = []
	var override := OS.get_environment(BUNDLE_ENV).strip_edges()
	if not override.is_empty():
		var direct := override if override.ends_with(FILE_NAME) else override.path_join(FILE_NAME)
		candidates.append({"path": direct, "origin": BUNDLE_ENV})
	for root in roots:
		var text := String(root).strip_edges()
		if text.is_empty():
			continue
		candidates.append({"path": text.path_join(FILE_NAME), "origin": "beside the map bundles"})
	return candidates


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
			"NO REGION-IMAGE BUNDLE, so the region card names a region without "
			+ "showing retail's own portrait of it. Looked at: %s. Produce one "
			+ "with: python -m openbfme_importer.living_world_region_images "
			+ "--catalog <catalog>.json --document <living-world>.json --out "
			+ "<dir>, then point %s at <dir>.") % [
				"; ".join(tried) if not tried.is_empty() else "nowhere", BUNDLE_ENV],
	}


func load_from(path: String) -> bool:
	_reset()
	source_path = path
	bundle_root = path.get_base_dir()

	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return _fail("cannot open %s" % path)
	if file.get_length() > MAX_BYTES:
		var oversized := file.get_length()
		file.close()
		return _fail("%s is %d bytes, over the %d limit" % [path, oversized, MAX_BYTES])
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if not (parsed is Dictionary):
		return _fail("%s is not a JSON object" % path)
	var bundle: Dictionary = parsed
	if String(bundle.get("schema", "")) != SCHEMA:
		return _fail("schema is not %s" % SCHEMA)
	if int(bundle.get("schemaVersion", -1)) != SCHEMA_VERSION:
		return _fail("unsupported schemaVersion %d" % int(bundle.get("schemaVersion", -1)))

	_atlas_directory = String(bundle.get("atlasDirectory", "region-atlases"))
	images = bundle.get("images", {}) as Dictionary
	totals = bundle.get("totals", {}) as Dictionary
	gaps = bundle.get("gaps", {}) as Dictionary
	for row_value in bundle.get("regions", []) as Array:
		var row := row_value as Dictionary
		var id := String(row.get("region", ""))
		if id.is_empty():
			return _fail("a region record carries no id")
		regions[id] = row
	if regions.is_empty():
		return _fail("the bundle declares no regions")
	loaded = true
	return true


func describe_load() -> PackedStringArray:
	var lines: Array[String] = []
	if not loaded:
		lines.append("no region-image bundle loaded")
		return PackedStringArray(lines)
	lines.append("%d region(s); %d with a fortress portrait, %d with a region portrait" % [
		int(totals.get("regions", 0)),
		int(totals.get("regionsWithFortressPortrait", 0)),
		int(totals.get("regionsWithRegionPortrait", 0))])
	lines.append("%d of %d image ids resolved across %d atlases (%d bytes)" % [
		int(totals.get("imageIdsResolved", 0)), int(totals.get("imageIdsRequested", 0)),
		int(totals.get("atlases", 0)), int(totals.get("atlasBytes", 0))])
	for key in ["missingImageIds", "ambiguousImageIds", "unresolvedAtlases",
			"cropsWithoutAtlas", "regionsNamingAPortraitRetailDoesNotDefine",
			"regionsDeclaredTwiceWithDifferentArt"]:
		var rows: Array = gaps.get(key, []) as Array
		if not rows.is_empty():
			lines.append("%s (%d): %s" % [key, rows.size(),
				", ".join(rows.map(func(v: Variant) -> String: return String(v)))])
	return PackedStringArray(lines)


# --- what the card asks for -------------------------------------------------------

## The portrait retail draws for a region, and WHICH of its two authored fields
## it came from. Returns `{texture, id, requested, source, reason}`.
##
## The FORTRESS portrait wins when retail authors one, because that is the plate
## retail puts on the card for a region that has a castle; the REGION portrait is
## the one every region carries. Both are the region's own authored fields, in a
## fixed order - never a resemblance, and never another region's picture.
func region_portrait(region_id: String) -> Dictionary:
	var blank := {"texture": null, "id": "", "requested": "", "source": "", "reason": ""}
	if not loaded:
		return blank
	var row: Dictionary = regions.get(region_id, {}) as Dictionary
	if row.is_empty():
		return blank
	for pair in [["fortressPortrait", "Fortress.Portrait"], ["regionPortrait", "RegionPortrait"]]:
		var field := String(pair[0])
		var requested := String(row.get(field, ""))
		if requested.is_empty():
			continue
		var resolved := String(row.get("%sResolved" % field, ""))
		if resolved.is_empty():
			# RETAIL NAMED A PICTURE IT DOES NOT SHIP. Say so and fall through to
			# the next authored field rather than drawing something else.
			blank["requested"] = requested
			blank["source"] = String(pair[1])
			blank["reason"] = (
				"retail's own data names %s here and defines it in none of its "
				+ "MappedImage documents") % requested
			continue
		var texture := image(resolved)
		if texture == null:
			blank["requested"] = requested
			blank["source"] = String(pair[1])
			blank["reason"] = String(missing_images.get(resolved, "its atlas did not load"))
			continue
		return {
			"texture": texture, "id": resolved, "requested": requested,
			"source": String(pair[1]), "reason": "",
		}
	return blank


## Retail's own image for an id, cropped to retail's own rectangle, or NULL. A
## null is a RECORDED null: the id and the reason land in `missing_images`.
func image(image_id: String) -> Texture2D:
	if image_id.is_empty() or not loaded:
		return null
	if _crop_cache.has(image_id):
		return _crop_cache[image_id] as AtlasTexture
	var record: Variant = images.get(image_id, null)
	if not (record is Dictionary):
		_miss(image_id, "no MappedImage block in retail's mappedimages documents defines it")
		return null
	var row: Dictionary = record
	var atlas_file := String(row.get("atlas", ""))
	if atlas_file.is_empty():
		_miss(image_id, "its atlas %s is in no archive in the catalog" % String(row.get("texture", "?")))
		return null
	var atlas := _atlas(atlas_file)
	if atlas == null:
		return null
	# Retail authors the rectangle against a DECLARED texture size that can
	# differ from the shipped atlas; scaling by the ratio is derivation from both
	# shipped numbers and is the only way the crop lands in the same place.
	var declared_width := maxi(int(row.get("textureWidth", 0)), 1)
	var declared_height := maxi(int(row.get("textureHeight", 0)), 1)
	var scale_x := float(atlas.get_width()) / float(declared_width)
	var scale_y := float(atlas.get_height()) / float(declared_height)
	var left := int(row.get("left", 0))
	var top := int(row.get("top", 0))
	var rect := Rect2(
		float(left) * scale_x, float(top) * scale_y,
		float(int(row.get("right", 0)) - left) * scale_x,
		float(int(row.get("bottom", 0)) - top) * scale_y)
	if rect.size.x <= 0.0 or rect.size.y <= 0.0:
		_miss(image_id, "its authored rectangle has no area")
		return null
	var crop := AtlasTexture.new()
	crop.atlas = atlas
	crop.region = rect
	crop.filter_clip = true
	crop.resource_name = image_id
	_crop_cache[image_id] = crop
	return crop


# --- internals ---------------------------------------------------------------

func _atlas(file_name: String) -> ImageTexture:
	if _atlas_cache.has(file_name):
		return _atlas_cache[file_name] as ImageTexture
	var path := bundle_root.path_join(_atlas_directory).path_join(file_name)
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		_atlas_cache[file_name] = null
		errors.append("cannot open atlas %s" % path)
		return null
	if file.get_length() > MAX_ATLAS_BYTES:
		file.close()
		_atlas_cache[file_name] = null
		errors.append("atlas %s is over the limit" % path)
		return null
	var bytes := file.get_buffer(file.get_length())
	file.close()
	# RETAIL'S BYTES, DECODED - never re-encoded.
	var picture := Image.new()
	var extension := file_name.get_extension().to_lower()
	var status := ERR_UNAVAILABLE
	if extension == "dds":
		status = picture.load_dds_from_buffer(bytes)
	elif extension == "tga":
		status = picture.load_tga_from_buffer(bytes)
	elif extension == "jpg" or extension == "jpeg":
		status = picture.load_jpg_from_buffer(bytes)
	elif extension == "png":
		status = picture.load_png_from_buffer(bytes)
	else:
		_atlas_cache[file_name] = null
		errors.append("this loader does not read .%s atlases (%s)" % [extension, file_name])
		return null
	if status != OK:
		_atlas_cache[file_name] = null
		errors.append("the .%s decoder rejected %s (error %d)" % [extension, file_name, status])
		return null
	var texture := ImageTexture.create_from_image(picture)
	_atlas_cache[file_name] = texture
	return texture


func _reset() -> void:
	loaded = false
	source_path = ""
	bundle_root = ""
	errors = PackedStringArray()
	images = {}
	regions = {}
	totals = {}
	gaps = {}
	missing_images = {}
	_atlas_directory = ""
	_atlas_cache = {}
	_crop_cache = {}


func _fail(message: String) -> bool:
	errors.append(message)
	loaded = false
	return false


func _miss(image_id: String, reason: String) -> void:
	if not missing_images.has(image_id):
		missing_images[image_id] = reason
