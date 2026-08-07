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
const SCHEMA_VERSION := 3

const BUNDLE_ENV := "OPENBFME_LIVING_WORLD_UI"
const FILE_NAME := "living-world-ui.json"
const MAX_BYTES := 16 * 1024 * 1024
const MAX_ATLAS_BYTES := 16 * 1024 * 1024
const MAX_EXPERIENCE_LEVELS := 4096
const MAX_EXPERIENCE_TOKENS := 65536

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
## `playerArmy (casefolded) -> {icon, size}`, retail's own `ArmyToSpawn.Icon` and
## `IconSize` - the authored link from an army to the 3D MARKER FAMILY retail
## stands on the map for it. Built from the same recruit rows the portraits come
## from, so the two can never disagree about which army a row is for.
var army_marker_icons: Dictionary = {}
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
## Source-ordered retail experience rows usable by typed UpgradeTroops nuggets.
var upgrade_experience_levels: Array = []

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


static func _exact_keys(row: Dictionary, expected: Array[String]) -> bool:
	if row.size() != expected.size():
		return false
	for key in expected:
		if not row.has(key):
			return false
	return true


static func _json_integer(value: Variant, minimum: int, positive := false) -> bool:
	# JSON numbers arrive as floats. Accept only an exactly integral finite token;
	# never coerce/truncate 1.5 to 1.
	if not (value is float) or not is_finite(value) or value != floor(value):
		return false
	# JSON numbers cross this boundary as IEEE-754 floats; beyond 2^53-1 an
	# authored integer cannot be carried exactly.
	if absf(value) > 9007199254740991.0:
		return false
	return value > 0.0 if positive else value >= float(minimum)


static func _strings(row: Dictionary, keys: Array[String]) -> bool:
	for key in keys:
		if not (row.get(key) is String) or String(row[key]).length() > 256:
			return false
	return true


static func _validate_army(value: Variant) -> bool:
	if not (value is Dictionary):
		return false
	var row: Dictionary = value
	var keys: Array[String] = ["buildTime", "constructButtonHelp", "constructButtonImage", "constructButtonTitle", "heroTemplateName", "icon", "iconSize", "palantirMovie", "playerArmy"]
	return _exact_keys(row, keys) and _strings(row, keys)


static func _validate_bonus(value: Variant) -> bool:
	if not (value is Dictionary):
		return false
	var row: Dictionary = value
	if not _exact_keys(row, ["armorPct", "armorRaw", "experiencePct", "experienceRaw", "threshold", "weaponPct", "weaponRaw"]):
		return false
	if not _json_integer(row["threshold"], 1, true):
		return false
	for prefix in ["armor", "experience", "weapon"]:
		var pct: Variant = row[prefix + "Pct"]
		var raw: Variant = row[prefix + "Raw"]
		if pct == null or raw == null:
			if pct != null or raw != null:
				return false
			continue
		if not (pct is float) or not is_finite(pct) or not (raw is String):
			return false
		var text := String(raw)
		var colon := text.find(":")
		if text.length() > 256 or colon <= 0 or text.left(colon).to_lower() != prefix 				or not text.ends_with("%") or text.ends_with("%%"):
			return false
		var number := text.substr(text.find(":") + 1, text.length() - text.find(":") - 2)
		var decimal := RegEx.new()
		decimal.compile("^[+-]?([0-9]+(\\.[0-9]*)?|\\.[0-9]+)$")
		if decimal.search(number) == null or not is_finite(number.to_float()) or number.to_float() != pct:
			return false
	return true


static func _validate_nugget(value: Variant) -> bool:
	if not (value is Dictionary):
		return false
	var row: Dictionary = value
	if not _strings(row, ["kind", "tag"]) or String(row.get("tag", "")).is_empty():
		return false
	match String(row.get("kind", "")):
		"strengthen_army":
			if not _exact_keys(row, ["bonusKey", "bonuses", "kind", "strengtheningRange", "tag"]): return false
			if not _strings(row, ["bonusKey", "strengtheningRange"]) or String(row["bonusKey"]).is_empty() or String(row["strengtheningRange"]).is_empty(): return false
			if not (row["bonuses"] is Array) or row["bonuses"].is_empty() or row["bonuses"].size() > 16: return false
			for bonus in row["bonuses"]:
				if not _validate_bonus(bonus): return false
		"increase_treasury":
			if not _exact_keys(row, ["kind", "tag", "treasureAmount"]): return false
			if not _strings(row, ["treasureAmount"]) or String(row["treasureAmount"]).is_empty(): return false
		"spawn_army":
			if not _exact_keys(row, ["armies", "kind", "queueSize", "tag"]): return false
			if not _json_integer(row["queueSize"], 0) or not (row["armies"] is Array) or row["armies"].size() > 16: return false
			for army in row["armies"]:
				if not _validate_army(army): return false
		"upgrade_troops":
			if not _exact_keys(row, ["kind", "numUpgradesPerTurn", "tag", "upgradeableUnits"]): return false
			if not _json_integer(row["numUpgradesPerTurn"], 1, true) or not (row["upgradeableUnits"] is Array) or row["upgradeableUnits"].is_empty() or row["upgradeableUnits"].size() > 64: return false
			for unit in row["upgradeableUnits"]:
				if not (unit is String) or String(unit).is_empty() or String(unit).length() > 256: return false
		"increase_command_points":
			if not _exact_keys(row, ["amount", "kind", "tag", "type"]): return false
			if not _strings(row, ["type"]) or String(row["type"]).is_empty() or not _json_integer(row["amount"], -9223372036854775808): return false
		_:
			return false
	return true


static func _validate_buildings(value: Variant) -> String:
	if not (value is Array):
		return "buildings is not an array"
	for building_value in value:
		if not (building_value is Dictionary): return "a building is not an object"
		var row: Dictionary = building_value
		var id: Variant = row.get("id")
		if not (id is String) or String(id).is_empty(): return "a building record carries no id"
		if not (row.get("nuggetsStatus") is String) or not (row.get("nuggets") is Array): return "building %s has no typed nugget status/list" % id
		var status := String(row["nuggetsStatus"])
		var nuggets: Array = row["nuggets"]
		if status != "ok" and status != "refused": return "building %s has invalid nuggetsStatus" % id
		if nuggets.size() > 32 or (status == "refused" and not nuggets.is_empty()): return "building %s violates nugget status invariant" % id
		if status == "ok":
			for nugget in nuggets:
				if not _validate_nugget(nugget): return "building %s has malformed typed nugget" % id
	return ""

static func _validate_upgrade_experience_levels(value: Variant, building_rows: Variant) -> String:
	if not (value is Array):
		return "upgradeExperienceLevels is not an array"
	var rows: Array = value
	if rows.size() > MAX_EXPERIENCE_LEVELS:
		return "upgradeExperienceLevels exceeds its row cap"
	var active: Dictionary = {}
	for building_value in building_rows as Array:
		var building := building_value as Dictionary
		if String(building.get("nuggetsStatus", "")) != "ok":
			continue
		for nugget_value in building.get("nuggets", []) as Array:
			var nugget := nugget_value as Dictionary
			if String(nugget.get("kind", "")) != "upgrade_troops":
				continue
			for unit_value in nugget.get("upgradeableUnits", []) as Array:
				active[String(unit_value)] = true
	var names: Dictionary = {}
	var progress: Dictionary = {}
	var token_count := 0
	for value_row in rows:
		if not (value_row is Dictionary):
			return "an upgrade experience row is not an object"
		var row := value_row as Dictionary
		if not _exact_keys(row, ["experienceAward", "name", "rank", "requiredExperience", "targetNames", "upgrades"]):
			return "an upgrade experience row has malformed shape"
		var name_value: Variant = row.get("name")
		if not (name_value is String) or String(name_value).is_empty() or String(name_value).length() > 256:
			return "an upgrade experience row has no valid name"
		var name := String(name_value)
		if names.has(name):
			return "duplicate upgrade experience name %s" % name
		names[name] = true
		if not _json_integer(row.get("requiredExperience"), 1, true) 				or not _json_integer(row.get("experienceAward"), 0) 				or not _json_integer(row.get("rank"), 1, true):
			return "upgrade experience row %s has a malformed integer" % name
		if not (row.get("targetNames") is Array) or not (row.get("upgrades") is Array):
			return "upgrade experience row %s has malformed token lists" % name
		var targets: Array = row["targetNames"]
		if targets.is_empty():
			return "upgrade experience row %s has no targets" % name
		var seen_in_row: Dictionary = {}
		for target_value in targets:
			if not (target_value is String) or String(target_value).is_empty() or String(target_value).length() > 256:
				return "upgrade experience row %s has a malformed target" % name
			var target := String(target_value)
			if not active.has(target):
				return "upgrade experience row %s targets an inactive unit" % name
			token_count += 1
			if seen_in_row.has(target):
				continue
			seen_in_row[target] = true
			var prior: Dictionary = progress.get(target, {"rank": 0.0, "threshold": 0.0}) as Dictionary
			if float(row["rank"]) <= float(prior["rank"]) 					or float(row["requiredExperience"]) <= float(prior["threshold"]):
				return "non-increasing upgrade experience progression for %s" % target
			progress[target] = {"rank": row["rank"], "threshold": row["requiredExperience"]}
		for upgrade_value in row["upgrades"] as Array:
			if not (upgrade_value is String) or String(upgrade_value).is_empty() or String(upgrade_value).length() > 256:
				return "upgrade experience row %s has a malformed upgrade" % name
			token_count += 1
		if token_count > MAX_EXPERIENCE_TOKENS:
			return "upgrade experience token cap exceeded"
	for unit in active:
		if not progress.has(unit):
			return "active upgrade unit %s has no experience rows" % String(unit)
	return ""


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
	var version_value: Variant = bundle.get("schemaVersion", null)
	if not (version_value is float) or not is_finite(version_value) or version_value != floor(version_value):
		return _fail("schemaVersion must be the JSON integer %d" % SCHEMA_VERSION)
	var version := int(version_value)
	if version != SCHEMA_VERSION:
		if version == 1 or version == 2:
			return _fail("schemaVersion %d is stale; regenerate the living-world UI bundle for schemaVersion 3" % version)
		return _fail("unsupported schemaVersion %d" % version)
	var building_error := _validate_buildings(bundle.get("buildings", null))
	if not building_error.is_empty():
		return _fail(building_error)
	var experience_error := _validate_upgrade_experience_levels(
		bundle.get("upgradeExperienceLevels", null), bundle.get("buildings", null))
	if not experience_error.is_empty():
		return _fail(experience_error)

	_atlas_directory = String(bundle.get("atlasDirectory", "ui-atlases"))
	images = bundle.get("images", {}) as Dictionary
	army_portraits = bundle.get("armyPortraits", {}) as Dictionary
	hero_portraits = bundle.get("heroPortraits", {}) as Dictionary
	totals = bundle.get("totals", {}) as Dictionary
	gaps = bundle.get("gaps", {}) as Dictionary
	faction_banners = bundle.get("factionBanners", {}) as Dictionary
	faction_banner_derivation = String(bundle.get("factionBannerDerivation", ""))
	chrome_sheet = bundle.get("chromeSheet", {}) as Dictionary
	upgrade_experience_levels = (bundle.get("upgradeExperienceLevels", []) as Array).duplicate(true)

	var ids: Array[String] = []
	for row_value in bundle.get("buildings", []) as Array:
		var row := row_value as Dictionary
		var id := String(row.get("id", ""))
		if id.is_empty():
			return _fail("a building record carries no id")
		buildings[id] = row
		ids.append(id)
		# RETAIL'S OWN AUTHORED LINK from an army to its 3D marker family, read
		# off the same `ArmyToSpawn` block that carries its portrait. Never a
		# resemblance between two names.
		for recruit_value in row.get("recruits", []) as Array:
			var recruit := recruit_value as Dictionary
			var army := String(recruit.get("playerArmy", "")).to_lower()
			var icon := String(recruit.get("icon", ""))
			if army.is_empty() or icon.is_empty():
				continue
			army_marker_icons[army] = {
				"icon": icon,
				"size": String(recruit.get("iconSize", "")),
			}
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


## THE 3D MARKER FAMILY retail stands on the map for one army, and WHICH authored
## link found it.
##
## Returns `{icon, size, source}`. Exactly two rungs, both retail's own fields
## and neither of them a name match: the `ArmyToSpawn` block that recruits this
## same `PlayerArmy` (which is where every hero army's own banner comes from),
## failing that the owning `LivingWorldPlayerTemplate`'s `DefaultArmyIconName`.
## An army neither reaches gets an EMPTY icon, and the map keeps its flat plate
## and says so - it never borrows another faction's banner.
func army_marker(roster: String, player_template: String) -> Dictionary:
	if not loaded:
		return {"icon": "", "size": "", "source": SOURCE_NONE}
	var by_army: Variant = army_marker_icons.get(roster.to_lower(), null)
	if by_army is Dictionary and not String((by_army as Dictionary).get("icon", "")).is_empty():
		var row := by_army as Dictionary
		return {
			"icon": String(row.get("icon", "")),
			"size": String(row.get("size", "")),
			"source": SOURCE_ARMY,
		}
	var template: Dictionary = player_templates.get(player_template, {}) as Dictionary
	var default_icon := String(template.get("defaultArmyIconName", ""))
	if not default_icon.is_empty():
		return {"icon": default_icon, "size": "", "source": "DefaultArmyIconName"}
	return {"icon": "", "size": "", "source": SOURCE_NONE}


## The BUILD-PLOT marker family retail decals a seat's plots with - retail's own
## `BuildPlotIconName` on the player template, or "" when it authors none.
func build_plot_icon_id(player_template: String) -> String:
	var template: Dictionary = player_templates.get(player_template, {}) as Dictionary
	return String(template.get("buildPlotIconName", ""))


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


## THE PHASE-BAND STRIPS off the same APT sheet - the two wide dark bands with
## a lit hairline that retail's strategic HUD hangs its phase banner on. Chosen,
## like the rings, by a STATED RULE over the sheet rather than by an index
## someone wrote down: a band candidate is an island whose opaque pixels cover
## MORE than 60% of its own box (a filled bar; every ring on the sheet covers
## 33-43%). Retail's sheet carries exactly one such island, and it holds BOTH
## strips stacked with fully-transparent rows between them, so the island is
## split at those rows - a derivation from retail's own alpha channel, exactly
## the way the converter derived the islands in the first place.
##
## `which` is "top" or "bottom". Returns null (no substitute) when the sheet is
## not converted or carries no bar-shaped island; the caller names the gap.
func chrome_band(which: String = "top") -> Texture2D:
	var components: Array = chrome_sheet.get("components", []) as Array
	var file := String(chrome_sheet.get("file", ""))
	if components.is_empty() or file.is_empty():
		return null
	var key := "%s:band:%s" % [file, which]
	if _crop_cache.has(key):
		return _crop_cache[key] as AtlasTexture
	var bar: Dictionary = {}
	for value in components:
		var row := value as Dictionary
		if float(row.get("coverage", 0.0)) > 0.6:
			bar = row
			break
	if bar.is_empty():
		return null
	var atlas := _atlas(file)
	if atlas == null:
		return null
	var picture := atlas.get_image()
	if picture == null:
		return null
	var left := int(bar.get("left", 0))
	var top := int(bar.get("top", 0))
	var right := int(bar.get("right", 0))
	var bottom := mini(int(bar.get("bottom", 0)), picture.get_height())
	# Runs of rows that carry any opaque pixel (alpha > 8, the converter's own
	# threshold), sampled on the same 4-pixel grid the converter used.
	var runs: Array[Vector2i] = []
	var run_start := -1
	for y in range(top, bottom):
		var occupied := false
		var x := left
		while x < right:
			if picture.get_pixel(x, y).a > 8.0 / 255.0:
				occupied = true
				break
			x += 4
		if occupied and run_start < 0:
			run_start = y
		elif not occupied and run_start >= 0:
			runs.append(Vector2i(run_start, y))
			run_start = -1
	if run_start >= 0:
		runs.append(Vector2i(run_start, bottom))
	if runs.is_empty():
		return null
	var chosen: Vector2i = runs[0] if which == "top" else runs[runs.size() - 1]
	return _sheet_crop(key, {
		"left": left, "top": chosen.x, "right": right, "bottom": chosen.y,
	})


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


## RETAIL'S OWN PICTURE OF AN EMPTY BUILD PLOT, for one seat.
##
## `LivingWorldPlayerTemplate` authors `BuildPlotSelectionPortraitName` beside
## `BuildPlotIconName`, and it is a MappedImage id like every other portrait here
## (`BPMFortress_BuildPlot` for Mordor, `KUFortressBuildPlot` for Angmar). It is
## the engraved stone tile carrying the faction's device - the Lidless Eye for
## Mordor, the crown for Angmar - and RETAIL DRAWS IT IN TWO PLACES AT ONCE:
##
##   * inside the palantir's right-hand lens, which is the SELECTION portrait
##     slot, whenever the thing selected is a build plot; and
##   * inside every empty card on the build-queue rail, whose authored picture
##     host `StrategicDetailsBuildQueue` ships filled with the Men-of-the-West
##     default (`5/4/34/*/8`, the navy White Tree plate) for the artist's benefit
##     and the engine replaces per seat.
##
## THAT IT IS ONE ASSET IN BOTH PLACES WAS MEASURED, not assumed, against the
## oracle capture `game.dat_l1eJcM0zCw.jpg`: the engraving inside the tray card
## (x 1290..1400, y 1215..1272) template-matches the engraving inside the palantir
## lens (x 560..920, y 1120..1360) at a normalised cross-correlation of 0.625 on
## gradient magnitude, at a scale ratio of 2.4, against 0.175 for the same
## template over an unrelated piece of that frame's gold chrome. Of the six
## shipped `BP*Fortress_BuildPlot` tiles, Mordor's is the best match at BOTH
## sites; the other five score lower at both. Same id, two hosts.
##
## A PRIOR ROUND OF THIS LANE CONCLUDED THE FACE WAS NOT IN THE DATA and drew a
## blank stone plate instead. That search was for a compass, a dial, a rosette or
## a medallion, and it failed because retail names this art after what it MEANS -
## a fortress build plot - and not after what it looks like. The face was in the
## living-world UI bundle the whole time, one field away from `build_plot_icon`.
func build_plot_portrait_id(player_template: String) -> String:
	var template: Dictionary = player_templates.get(player_template, {}) as Dictionary
	return String(template.get("buildPlotSelectionPortraitName", ""))


## The same tile as a texture, or NULL when the seat authors none or its atlas is
## in no archive. A null is recorded in `missing_images` by `image()`; callers
## must not substitute another faction's tile.
func build_plot_portrait(player_template: String) -> Texture2D:
	return image(build_plot_portrait_id(player_template))


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
	army_marker_icons = {}
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
	upgrade_experience_levels = []
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
