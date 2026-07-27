extends RefCounted

## RETAIL'S WAR OF THE RING UI SURFACE, loaded from the bundle
## `openbfme_importer.living_world_ui` writes: the buildable structures, the
## armies each one recruits, the per-faction marker names, and - the point of the
## whole thing - THE ATLAS CROP BEHIND EVERY ICON RETAIL DRAWS.
##
## WHY THIS EXISTS. The strategic screen drew armies as coloured DOTS and offered
## no way to build anything. Retail draws each army stack as a BANNER CARRYING A
## PORTRAIT, and rings a selected build plot with the icons of the structures
## that can go on it.
##
## A prior lane established, correctly, that `data/ini/livingworldicons/*.ini`
## carry NO portraits - they are 3D map-marker definitions naming W3D models and
## nothing else. The portraits are real and they are elsewhere: every strategic
## icon is a `MappedImage`, which is an ATLAS NAME PLUS A PIXEL RECTANGLE, and
## the living-world data names them by id:
##
##     ConstructButtonImage = BGFortress          (a structure)
##     ConstructButtonImage = HIAragorn_wotr      (a hero army)
##     GarrisonSelectionPortraitName = "UPGondor_Army"   (a garrison)
##     FactionIcon = "IconMenStrategic"
##
## So this class is an ATLAS CROPPER. `image()` returns an `AtlasTexture` over
## retail's own bytes, cropped to retail's own rectangle. Nothing is rescaled,
## re-encoded or recoloured.
##
## WHAT IT REFUSES TO DO. It NEVER substitutes a portrait. An id the bundle has no
## crop for, or whose atlas is not in the archives, returns NOTHING and is
## recorded in `missing_images` so the screen can draw the faction colour and
## SAY which portrait is absent. Retail ships exactly one such id and marks it
## itself - `ConstructButtonImage = CPYoungWizardAlpha  // TEMP`, whose texture
## is in no archive - and that is the one the screen will name.
##
## IT ALSO NEVER GUESSES A LINK. An army's portrait is found only through
## retail's own authored fields, in this order: the `ArmyToSpawn` block that
## recruits that same `PlayerArmy`; failing that, the one for that same
## `HeroTemplateName`; failing that, the owning player template's
## `GarrisonSelectionPortraitName`. Resemblance between two names is never used.

const SCHEMA := "openbfme.living-world-ui"
const SCHEMA_VERSION := 1

const BUNDLE_ENV := "OPENBFME_LIVING_WORLD_UI"
const FILE_NAME := "living-world-ui.json"
const MAX_BYTES := 16 * 1024 * 1024
const MAX_ATLAS_BYTES := 16 * 1024 * 1024

## How an army's portrait was found, so the screen can say which rung it stood
## on rather than implying every banner is a hero portrait.
const SOURCE_ARMY := "PlayerArmy"
const SOURCE_HERO := "HeroTemplateName"
const SOURCE_GARRISON := "GarrisonSelectionPortraitName"
const SOURCE_NONE := ""

var loaded := false
var source_path := ""
var bundle_root := ""
var errors: PackedStringArray = PackedStringArray()

## `image id -> {atlas, left, top, right, bottom, textureWidth, textureHeight}`.
var images: Dictionary = {}
## `building id -> record`, and the id list in manifest (sorted) order.
var buildings: Dictionary = {}
var building_ids: PackedStringArray = PackedStringArray()
## `player template name -> record` carrying the faction's marker and icon names.
var player_templates: Dictionary = {}
## `playerArmy (casefolded) -> image id`, retail's own recruit-button link.
var army_portraits: Dictionary = {}
## `heroTemplateName (casefolded) -> image id`, the same link keyed by hero.
var hero_portraits: Dictionary = {}
## `build plot icon id -> record`, carrying the W3D model names retail decals a
## plot with. RECORDED, NOT CONVERTED - the screen says so.
var build_plot_icons: Dictionary = {}
var totals: Dictionary = {}
var gaps: Dictionary = {}
## `player template -> Banner_* image id`. DERIVED, not authored - see
## `faction_banner_derivation` for the exact rule and why it is allowed.
var faction_banners: Dictionary = {}
var faction_banner_derivation := ""
## Retail's own APT sheet for the War of the Ring shell, and the islands of art
## on it. The islands are DERIVED from its alpha channel; nothing in the shipped
## data names them.
var chrome_sheet: Dictionary = {}

## Image ids asked for that produced no texture, and why, in ask order. Public so
## the screen can name every portrait it is standing in for.
var missing_images: Dictionary = {}

var _atlas_directory := ""
## `atlas file -> ImageTexture`, decoded once and shared by every crop on it.
var _atlas_cache: Dictionary = {}
## `image id -> AtlasTexture`, so a banner drawn every frame does not re-crop.
var _crop_cache: Dictionary = {}


static func candidate_paths(roots: Array = []) -> Array[Dictionary]:
	var candidates: Array[Dictionary] = []
	var override := OS.get_environment(BUNDLE_ENV).strip_edges()
	if not override.is_empty():
		# The environment may name the directory or the file itself; both are
		# accepted so one setting cannot be "right but spelled the other way".
		var direct := override if override.ends_with(FILE_NAME) else override.path_join(FILE_NAME)
		candidates.append({"path": direct, "origin": BUNDLE_ENV})
	for root in roots:
		candidates.append({
			"path": String(root).path_join(FILE_NAME),
			"origin": "beside the region geometry",
		})
	return candidates


## Load from the first candidate that parses. Returns `{ok, path, reason}`, and a
## failure is always a REASON naming every path tried.
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
			"NO LIVING-WORLD UI BUNDLE, so army stacks are drawn as plain markers "
			+ "instead of banners carrying retail's portraits, and there is no "
			+ "build menu. Retail's icons are MappedImage atlas crops; nothing has "
			+ "converted them here. Looked at: %s. Produce one with: python -m "
			+ "openbfme_importer.living_world_ui --catalog <catalog>.json --out "
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
		return _fail("%s is %d bytes, over the %d limit" % [path, file.get_length(), MAX_BYTES])
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if not (parsed is Dictionary):
		return _fail("%s is not a JSON object" % path)
	var bundle: Dictionary = parsed
	if String(bundle.get("schema", "")) != SCHEMA:
		return _fail("schema is not %s" % SCHEMA)
	if int(bundle.get("schemaVersion", -1)) != SCHEMA_VERSION:
		return _fail("unsupported schemaVersion %d" % int(bundle.get("schemaVersion", -1)))

	_atlas_directory = String(bundle.get("atlasDirectory", "ui-atlases"))
	images = bundle.get("images", {}) as Dictionary
	army_portraits = bundle.get("armyPortraits", {}) as Dictionary
	hero_portraits = bundle.get("heroPortraits", {}) as Dictionary
	totals = bundle.get("totals", {}) as Dictionary
	gaps = bundle.get("gaps", {}) as Dictionary
	faction_banners = bundle.get("factionBanners", {}) as Dictionary
	faction_banner_derivation = String(bundle.get("factionBannerDerivation", ""))
	chrome_sheet = bundle.get("chromeSheet", {}) as Dictionary

	var ids: Array[String] = []
	for row_value in bundle.get("buildings", []) as Array:
		var row := row_value as Dictionary
		var id := String(row.get("id", ""))
		if id.is_empty():
			return _fail("a building record carries no id")
		buildings[id] = row
		ids.append(id)
	ids.sort()
	building_ids = PackedStringArray(ids)

	for row_value in bundle.get("playerTemplates", []) as Array:
		var row := row_value as Dictionary
		player_templates[String(row.get("name", ""))] = row
	for row_value in bundle.get("buildPlotIcons", []) as Array:
		var row := row_value as Dictionary
		build_plot_icons[String(row.get("id", ""))] = row

	if buildings.is_empty():
		return _fail("the bundle declares no buildings")
	loaded = true
	return true


## One line per fact worth printing at load, for the launch log.
func describe_load() -> PackedStringArray:
	var lines: Array[String] = []
	lines.append("%d buildings, %d recruit entries, %d player templates" % [
		int(totals.get("buildings", 0)), int(totals.get("recruits", 0)),
		int(totals.get("playerTemplates", 0))])
	lines.append("%d of %d image ids resolved to an atlas crop, across %d atlases (%d bytes)" % [
		int(totals.get("imageIdsResolved", 0)), int(totals.get("imageIdsRequested", 0)),
		int(totals.get("atlases", 0)), int(totals.get("atlasBytes", 0))])
	for key in ["missingImageIds", "ambiguousImageIds", "unresolvedAtlases", "cropsWithoutAtlas"]:
		var rows: Array = gaps.get(key, []) as Array
		if not rows.is_empty():
			lines.append("%s (%d): %s" % [key, rows.size(), ", ".join(rows.map(func(v: Variant) -> String: return String(v)))])
	return PackedStringArray(lines)


# --- the atlas cropper ---------------------------------------------------------

## Retail's own image for an id, cropped to retail's own rectangle, or NULL.
##
## A null is a RECORDED null: the id and the reason land in `missing_images`, and
## the caller must draw something that is visibly not a portrait and say which
## one is absent. Substituting any other image here would be inventing retail art.
func image(image_id: String) -> Texture2D:
	if image_id.is_empty() or not loaded:
		return null
	if _crop_cache.has(image_id):
		return _crop_cache[image_id] as AtlasTexture
	var record: Variant = images.get(image_id, null)
	if not (record is Dictionary):
		_miss(image_id, "no MappedImage block in retail's 37 mappedimages documents defines it")
		return null
	var row: Dictionary = record
	var atlas_file := String(row.get("atlas", ""))
	if atlas_file.is_empty():
		_miss(image_id, "its atlas %s is in no archive in the catalog" % String(row.get("texture", "?")))
		return null
	var atlas := _atlas(atlas_file)
	if atlas == null:
		return null
	var left := int(row.get("left", 0))
	var top := int(row.get("top", 0))
	var right := int(row.get("right", 0))
	var bottom := int(row.get("bottom", 0))
	# Retail authors the rectangle against a DECLARED texture size that can differ
	# from the shipped atlas (an atlas may be downsampled). Scaling by the ratio
	# is derivation from both shipped numbers, not invention, and it is the only
	# way the crop lands in the same place on either.
	var declared_width := maxi(int(row.get("textureWidth", 0)), 1)
	var declared_height := maxi(int(row.get("textureHeight", 0)), 1)
	var scale_x := float(atlas.get_width()) / float(declared_width)
	var scale_y := float(atlas.get_height()) / float(declared_height)
	var rect := Rect2(
		float(left) * scale_x, float(top) * scale_y,
		float(right - left) * scale_x, float(bottom - top) * scale_y)
	if rect.size.x <= 0.0 or rect.size.y <= 0.0:
		_miss(image_id, "its authored rectangle %d,%d..%d,%d has no area" % [left, top, right, bottom])
		return null
	var crop := AtlasTexture.new()
	crop.atlas = atlas
	crop.region = rect
	crop.filter_clip = true
	crop.resource_name = image_id
	_crop_cache[image_id] = crop
	return crop


func has_image(image_id: String) -> bool:
	return loaded and images.has(image_id) and not String(
		(images[image_id] as Dictionary).get("atlas", "")).is_empty()


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
		errors.append("atlas %s is %d bytes, over the limit" % [path, file.get_length()])
		return null
	var bytes := file.get_buffer(file.get_length())
	file.close()
	# RETAIL'S BYTES, DECODED - never re-encoded. The bundle copies the compiled
	# payload out of the BIG verbatim and this reads that payload directly.
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


# --- what the screen asks for --------------------------------------------------

## The portrait retail draws for one army, and WHICH authored link found it.
##
## Returns `{id, source}`. `source` is one of the `SOURCE_*` constants, and
## `SOURCE_NONE` with an empty id means retail authors no portrait for this army
## at all - which is true of its neutral map cities, whose markers retail draws
## as 3D city models with no portrait anywhere in the data.
func army_portrait(roster: String, hero_template: String, player_template: String) -> Dictionary:
	if not loaded:
		return {"id": "", "source": SOURCE_NONE}
	var by_army: Variant = army_portraits.get(roster.to_lower(), null)
	if by_army != null and not String(by_army).is_empty():
		return {"id": String(by_army), "source": SOURCE_ARMY}
	var by_hero: Variant = hero_portraits.get(hero_template.to_lower(), null)
	if by_hero != null and not String(by_hero).is_empty():
		return {"id": String(by_hero), "source": SOURCE_HERO}
	var template: Dictionary = player_templates.get(player_template, {}) as Dictionary
	var garrison := String(template.get("garrisonSelectionPortraitName", ""))
	if not garrison.is_empty():
		return {"id": garrison, "source": SOURCE_GARRISON}
	return {"id": "", "source": SOURCE_NONE}


## The structures retail lets one player template build, in manifest order.
##
## `AvailableTo` is retail's own field and the ONLY filter applied here. A region
## may restrict further (`restrictBuildings`), and that is the caller's to apply
## because it is a property of the region, not of the faction.
func buildings_for(player_template: String) -> Array[Dictionary]:
	var rows: Array[Dictionary] = []
	if not loaded or player_template.is_empty():
		return rows
	for id in building_ids:
		var row := buildings[id] as Dictionary
		if String(row.get("availableTo", "")) == player_template:
			rows.append(row)
	return rows


## RETAIL'S STANDARD FOR ONE FACTION, or null.
##
## The link from a `LivingWorldPlayerTemplate` to a `Banner_*` image is DERIVED,
## not authored - retail makes it inside the APT movie, which is not converted.
## The converter emits it only when `Faction<X> -> Banner_<X>` is a TOTAL
## BIJECTION over retail's own seven playable factions with nothing unbound on
## either side, and it records that reasoning in `faction_banner_derivation` so
## the screen can say the art is bound by derivation rather than by an authored
## field. When the correspondence is not total the converter emits NOTHING and
## this returns null for every template.
func faction_banner(player_template: String) -> Texture2D:
	var id := String(faction_banners.get(player_template, ""))
	if id.is_empty():
		return null
	return image(id)


## The two elvish rings retail draws its strategic UI with, off retail's own APT
## sheet, chosen by a STATED RULE over the pixels rather than by an index someone
## wrote down after opening the file in an image editor.
##
## `which` is "gold" or "red". A ring candidate is an island whose opaque pixels
## cover 20-60% of its own box (a ring, not a filled bar) at a mean alpha above
## 100 (retail's sheet also carries two nearly-transparent rings at alpha 66 and
## 77, which are not these). Among candidates the gold one maximises green minus
## blue and the red one maximises red minus green - which on retail's sheet is
## 107 against 14, and 144 against 54: not a close call in either direction.
func chrome_ring(which: String) -> Texture2D:
	var components: Array = chrome_sheet.get("components", []) as Array
	var file := String(chrome_sheet.get("file", ""))
	if components.is_empty() or file.is_empty():
		return null
	var best: Dictionary = {}
	var best_score := -1.0
	for value in components:
		var row := value as Dictionary
		var coverage := float(row.get("coverage", 0.0))
		var mean_alpha := float(row.get("meanAlpha", 0))
		if coverage < 0.2 or coverage > 0.6 or mean_alpha <= 100.0:
			continue
		var colour: Array = row.get("meanColor", []) as Array
		if colour.size() < 3:
			continue
		var r := float(colour[0])
		var g := float(colour[1])
		var b := float(colour[2])
		var score := (g - b) if which == "gold" else (r - g)
		if score > best_score:
			best_score = score
			best = row
	if best.is_empty():
		return null
	return _sheet_crop("%s:%s" % [file, which], best)


func _sheet_crop(key: String, box: Dictionary) -> Texture2D:
	if _crop_cache.has(key):
		return _crop_cache[key] as AtlasTexture
	var atlas := _atlas(String(chrome_sheet.get("file", "")))
	if atlas == null:
		return null
	var rect := Rect2(
		float(box.get("left", 0)), float(box.get("top", 0)),
		float(int(box.get("right", 0)) - int(box.get("left", 0))),
		float(int(box.get("bottom", 0)) - int(box.get("top", 0))))
	if rect.size.x <= 0.0 or rect.size.y <= 0.0:
		return null
	var crop := AtlasTexture.new()
	crop.atlas = atlas
	crop.region = rect
	crop.filter_clip = true
	crop.resource_name = key
	_crop_cache[key] = crop
	return crop


## The faction icon id for a template, or "" when it authors none.
func faction_icon(player_template: String) -> String:
	var template: Dictionary = player_templates.get(player_template, {}) as Dictionary
	return String(template.get("factionIcon", ""))


## The build-plot marker family a template uses, and the W3D models it names.
## RECORDED, NOT CONVERTED: nothing here stands a model up, and the screen says
## which decal it is drawing a flat ring in place of.
func build_plot_icon(player_template: String) -> Dictionary:
	var template: Dictionary = player_templates.get(player_template, {}) as Dictionary
	var id := String(template.get("buildPlotIconName", ""))
	if id.is_empty():
		return {}
	return build_plot_icons.get(id, {}) as Dictionary


# --- internals ------------------------------------------------------------------

func _reset() -> void:
	loaded = false
	errors = PackedStringArray()
	images = {}
	buildings = {}
	building_ids = PackedStringArray()
	player_templates = {}
	army_portraits = {}
	hero_portraits = {}
	build_plot_icons = {}
	totals = {}
	gaps = {}
	missing_images = {}
	faction_banners = {}
	faction_banner_derivation = ""
	chrome_sheet = {}
	_atlas_cache = {}
	_crop_cache = {}
	_atlas_directory = ""


func _fail(message: String) -> bool:
	errors.append(message)
	loaded = false
	return false


func _miss(image_id: String, reason: String) -> void:
	if not missing_images.has(image_id):
		missing_images[image_id] = reason
