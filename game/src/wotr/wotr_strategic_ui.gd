extends RefCounted

## RETAIL'S WAR OF THE RING STRATEGIC SCREENS, loaded from the bundle
## `openbfme_importer.retail_strategic_apt_convert` writes: 24 flattened APT
## screens (the gold filigree frames, the strategic palantir, the END TURN
## button, the TERRITORY/ARMIES/STRUCTURES tray, the parchment build-slot
## cards, the timeline), their atlases, the retail font winners and the
## living-world support data.
##
## WHY THIS EXISTS. Every visual stream has been hand-drawing placeholders for
## ornament that retail SHIPS: the strategic screens are complete APT movies
## under `apt/strategic*.big` that were simply never imported. This loader
## hands the other streams retail's own triangles and atlas crops.
##
## WHAT A SCREEN IS. Each screen JSON carries exact static flattenings of the
## authored states the APT bytes themselves nominate - frame 0, every authored
## frame LABEL, and every frame whose authored script provably executes STOP -
## plus the exact root timeline, the authored instance names, and the
## exported symbols retail attaches dynamically (LivingWorldUI's RegionPopup
## card lives there, not on its root frame).
##
## WHAT IT REFUSES TO DO. It plays no timelines, executes no ActionScript and
## substitutes no art. A screen or atlas the manifest declares but the disk
## does not carry fails the WHOLE load - a bundle that lies about itself is
## not half-usable. Everything the APT data does not carry is in `named_gaps`
## (nine-slice metrics, script-driven layout, the SachaWynter font binding),
## so a consumer can SAY what is absent instead of quietly faking it.

## WHICH EDITION'S ART THIS IS. Read this before you trust a colour.
##
## This bundle was cooked from the BFME2 asset layer once, and it looked
## right: BFME2 and RotWK author the SAME strategic screens, so all 24 loaded,
## every element id resolved and every runner passed. What silently differed
## was the PIXELS of seventeen sheets - the palantir ring, the stats plaque,
## the details tabs, and above all the shell plate MenuExport draws, which is
## GREEN in BFME2 and BLUE-STEEL in RotWK. Nothing in the old manifest could
## have caught it, because an aggregate digest of the wrong edition is still a
## consistent aggregate digest.
##
## So the edition is now PINNED, and this loader refuses a bundle cooked from
## anything else. `atlas_sources` additionally states, per cooked PNG, the
## retail .tga and digest it came from, so a consumer can prove the edition of
## one specific sheet instead of trusting the whole bundle at once.
const SCHEMA := "openbfme.strategic-ui"
const SCHEMA_VERSION := 0

## The sealed RotWK strategic source closure - the canonical aggregate of the
## 1,337 retail sources under `editions/rotwk/cache/effective-assets`, the same
## constant `retail_strategic_apt_profile.EXPECTED_SOURCE_AGGREGATE_SHA256`
## seals on the importer side. BFME2's closure is 1,076 sources and hashes to
## 98ae0d99...; that bundle is REFUSED here rather than drawn in the wrong
## edition's colours.
const ROTWK_SOURCE_AGGREGATE_SHA256 := \
	"6ab93cb3ea6db0835c62ae029cac8218c2e204cd98ff8305863954ca61b831c7"

const BUNDLE_ENV := "OPENBFME_STRATEGIC_UI"
const FILE_NAME := "strategic-ui.json"
const MAX_MANIFEST_BYTES := 8 * 1024 * 1024
const MAX_SCREEN_BYTES := 32 * 1024 * 1024
const MAX_ATLAS_BYTES := 16 * 1024 * 1024

var loaded := false
var source_path := ""
var bundle_root := ""
var errors: PackedStringArray = PackedStringArray()

## `movie name -> manifest screen row` ({file, labels, summary, aggregateSha256}).
var screens: Dictionary = {}
## Manifest order, so iteration is deterministic.
var screen_names: PackedStringArray = PackedStringArray()
## Bundle-relative cooked atlas paths, as the draws reference them.
var atlases: PackedStringArray = PackedStringArray()
## `cooked atlas path -> {sourceVirtualPath, sha256, byteLength, width, height}`.
## THE PER-SHEET EDITION RECEIPT: the retail .tga each cooked PNG was decoded
## from, and its digest. `apt_StrategicPalantir_1.tga` is cd13947c... in RotWK
## and cc4ce169... in BFME2, so one lookup here settles which you are drawing.
var atlas_sources: Dictionary = {}
## `{sourceAggregateSha256, sourceCount, sourcePayloadBytes, effectiveAssets}` -
## where the retail bytes came from, as the converter measured it.
var source_provenance: Dictionary = {}
## The retail font winners: `[{sourceVirtualPath, cookedFont, embeddedFamily,
## aptFontName, bindingProven, ...}]`. Only Albertus MT is a PROVEN binding.
var fonts: Array = []
## `[{virtualPath, byteLength, sha256}]` - the living-world ini documents and
## the `art/compiledtextures/lm/*` strategic-map texture set, byte-preserved.
var supporting_data: Array = []
## `gap name -> detail`. THE HONEST BOUNDARY - read it before drawing.
var named_gaps: Dictionary = {}
var summary: Dictionary = {}

## Screens asked for that produced nothing, and why, in ask order.
var missing_screens: Dictionary = {}

var _screen_cache: Dictionary = {}
var _atlas_cache: Dictionary = {}


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


## Load from the first candidate that parses. Returns `{ok, path, reason}`, and
## a failure is always a REASON naming every path tried.
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
			"NO STRATEGIC-UI BUNDLE, so the living-world HUD keeps its diagnostic "
			+ "chrome instead of retail's gold frames, palantir, END TURN button "
			+ "and tray cards. Retail ships all of it as APT movies; nothing has "
			+ "converted them here. Looked at: %s. Produce one with: python -m "
			+ "openbfme_importer.retail_strategic_apt_convert --effective-assets "
			+ "<view> --output <dir>, then point %s at <dir>.") % [
				"; ".join(tried) if not tried.is_empty() else "nowhere", BUNDLE_ENV],
	}


func load_from(path: String) -> bool:
	_reset()
	source_path = path
	bundle_root = path.get_base_dir()

	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return _fail("cannot open %s" % path)
	if file.get_length() > MAX_MANIFEST_BYTES:
		return _fail("%s is %d bytes, over the %d limit" % [path, file.get_length(), MAX_MANIFEST_BYTES])
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if not (parsed is Dictionary):
		return _fail("%s is not a JSON object" % path)
	var manifest: Dictionary = parsed
	if String(manifest.get("schema", "")) != SCHEMA:
		return _fail("schema is not %s" % SCHEMA)
	if int(manifest.get("schemaVersion", -1)) != SCHEMA_VERSION:
		return _fail("unsupported schemaVersion %d" % int(manifest.get("schemaVersion", -1)))

	summary = manifest.get("summary", {}) as Dictionary
	fonts = manifest.get("fonts", []) as Array
	supporting_data = manifest.get("supportingData", []) as Array
	for row_value in manifest.get("namedGaps", []) as Array:
		var row := row_value as Dictionary
		named_gaps[String(row.get("gap", ""))] = String(row.get("detail", ""))

	var names: Array[String] = []
	for row_value in manifest.get("screens", []) as Array:
		var row := row_value as Dictionary
		var movie := String(row.get("movie", ""))
		if movie.is_empty():
			return _fail("a screen record carries no movie name")
		screens[movie] = row
		names.append(movie)
	screen_names = PackedStringArray(names)

	var atlas_paths: Array[String] = []
	for value in manifest.get("atlases", []) as Array:
		atlas_paths.append(String(value))
	atlases = PackedStringArray(atlas_paths)
	for row_value in manifest.get("atlasSources", []) as Array:
		var row := row_value as Dictionary
		atlas_sources[String(row.get("cookedPng", ""))] = row

	# THE EDITION GATE. A bundle cooked from the wrong retail edition loads
	# perfectly and draws the wrong colours, so it is refused here by the one
	# identity that distinguishes them: the canonical aggregate of the retail
	# sources it was converted from. This is not a version check that can be
	# relaxed for convenience - a bundle that fails it IS BFME2's strategic UI
	# (or an unknown third thing), and drawing it would be exactly the silent
	# substitution this project forbids.
	source_provenance = manifest.get("source", {}) as Dictionary
	var aggregate := String(source_provenance.get("sourceAggregateSha256", ""))
	if aggregate != ROTWK_SOURCE_AGGREGATE_SHA256:
		return _fail(
			("this bundle was NOT cooked from RotWK's asset layer: its retail "
			+ "source aggregate is %s, and RotWK's is %s. BFME2 authors the "
			+ "same 24 strategic screens with DIFFERENT art (the palantir ring, "
			+ "the stats plaque and the green-instead-of-blue-steel shell "
			+ "plate), so this would draw the wrong edition. Reconvert with "
			+ "--effective-assets .private/retail-work/editions/rotwk/cache/"
			+ "effective-assets.") % [
				aggregate if not aggregate.is_empty() else "(unstated)",
				ROTWK_SOURCE_AGGREGATE_SHA256])
	if atlas_sources.size() != atlases.size():
		# Every cooked atlas must carry its retail source receipt, or the
		# per-sheet edition claim above is only half checkable.
		return _fail("the bundle declares %d atlases but only %d atlas sources" % [
			atlases.size(), atlas_sources.size()])

	if screens.is_empty():
		return _fail("the bundle declares no screens")
	if named_gaps.is_empty():
		# A converter that stopped stating its gaps did not become complete;
		# it became dishonest. Refuse rather than pretend.
		return _fail("the bundle states no named gaps, which this data cannot honestly claim")

	# EVERY DECLARED ARTEFACT MUST BE ON DISK. A bundle that lies about itself
	# would otherwise fail one screen at a time, mid-frame, at draw time.
	for movie in screen_names:
		var screen_file := bundle_root.path_join(String((screens[movie] as Dictionary).get("file", "")))
		if not FileAccess.file_exists(screen_file):
			return _fail("declared screen file is missing: %s" % screen_file)
	for atlas_path in atlases:
		if not FileAccess.file_exists(bundle_root.path_join(atlas_path)):
			return _fail("declared atlas is missing: %s" % atlas_path)
	for row_value in fonts:
		var row := row_value as Dictionary
		if not FileAccess.file_exists(bundle_root.path_join(String(row.get("cookedFont", "")))):
			return _fail("declared font is missing: %s" % String(row.get("cookedFont", "")))
	for row_value in supporting_data:
		var row := row_value as Dictionary
		if not FileAccess.file_exists(bundle_root.path_join(String(row.get("virtualPath", "")))):
			return _fail("declared supporting file is missing: %s" % String(row.get("virtualPath", "")))

	loaded = true
	return true


## One line per fact worth printing at load, for the launch log.
func describe_load() -> PackedStringArray:
	var lines: Array[String] = []
	lines.append("%d screens, %d draws (%d textured), %d named instances" % [
		int(summary.get("screenCount", 0)), int(summary.get("drawCount", 0)),
		int(summary.get("texturedTriangleCount", 0)), int(summary.get("namedInstanceCount", 0))])
	lines.append("%d atlases, %d fonts, %d supporting files" % [
		int(summary.get("atlasCount", 0)), int(summary.get("fontCount", 0)),
		int(summary.get("supportingFileCount", 0))])
	var gap_names: Array[String] = []
	for key in named_gaps.keys():
		gap_names.append(String(key))
	gap_names.sort()
	lines.append("named gaps: %s" % ", ".join(gap_names))
	lines.append("edition: RotWK (source aggregate %s...)" % \
		ROTWK_SOURCE_AGGREGATE_SHA256.substr(0, 12))
	return PackedStringArray(lines)


## The retail .tga a cooked atlas was decoded from, or {} - the per-sheet
## edition receipt. Use it to SAY which edition a sheet is instead of
## assuming: `atlas_source(path)["sourceVirtualPath"]` and its `sha256`.
func atlas_source(atlas_path: String) -> Dictionary:
	var row: Variant = atlas_sources.get(atlas_path, null)
	return row as Dictionary if row is Dictionary else {}


# --- screens ---------------------------------------------------------------------

## One screen's full contract, parsed once and cached, or {} RECORDED in
## `missing_screens`. A screen whose file drifted from its manifest identity
## is refused, not half-read.
func screen(movie: String) -> Dictionary:
	if not loaded or movie.is_empty():
		return {}
	if _screen_cache.has(movie):
		return _screen_cache[movie] as Dictionary
	var row: Variant = screens.get(movie, null)
	if not (row is Dictionary):
		_miss(movie, "the manifest declares no screen with this movie name")
		return {}
	var path := bundle_root.path_join(String((row as Dictionary).get("file", "")))
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		_miss(movie, "cannot open %s" % path)
		return {}
	if file.get_length() > MAX_SCREEN_BYTES:
		file.close()
		_miss(movie, "%s is %d bytes, over the %d limit" % [path, file.get_length(), MAX_SCREEN_BYTES])
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	if not (parsed is Dictionary):
		_miss(movie, "%s is not a JSON object" % path)
		return {}
	var contract: Dictionary = parsed
	if String(contract.get("schema", "")) != "openbfme.strategic-ui-screen" \
			or String(contract.get("movie", "")) != movie:
		_miss(movie, "%s does not declare screen schema and movie %s" % [path, movie])
		return {}
	_screen_cache[movie] = contract
	return contract


## The flattened frame carrying an authored LABEL, or {} - never a guess.
## An empty label asks for frame 0, the authored initial state.
func frame_by_label(movie: String, label: String = "") -> Dictionary:
	var contract := screen(movie)
	if contract.is_empty():
		return {}
	for frame_value in contract.get("flattenedFrames", []) as Array:
		var frame := frame_value as Dictionary
		if label.is_empty():
			if int(frame.get("frameIndex", -1)) == 0:
				return frame
		elif (frame.get("labels", []) as Array).has(label):
			return frame
	return {}


## The flattened frame with the MOST draws - a STATED derivation, not a
## playback claim: many screens author an empty frame 0 and park their full
## composition on a scripted stop frame (the strategic palantir's oval and
## swirls live on its frame 1). Callers that need an exact authored state
## should ask `frame_by_label`; callers that need "show me this screen's art"
## get the fullest static state retail authored, by a rule they can restate.
func richest_frame(movie: String) -> Dictionary:
	var contract := screen(movie)
	if contract.is_empty():
		return {}
	var best: Dictionary = {}
	var best_draws := -1
	for frame_value in contract.get("flattenedFrames", []) as Array:
		var frame := frame_value as Dictionary
		if int(frame.get("drawCount", 0)) > best_draws:
			best_draws = int(frame.get("drawCount", 0))
			best = frame
	return best


## An exported symbol's contract (LivingWorldUI's RegionPopup card and the
## conquest notices ship this way), or {}.
func export_symbol(movie: String, symbol: String) -> Dictionary:
	var contract := screen(movie)
	for row_value in contract.get("exports", []) as Array:
		var row := row_value as Dictionary
		if String(row.get("symbol", "")) == symbol:
			return row
	return {}


## The authored instance names of one screen - the element ids other streams
## address retail's layout by ("endTurnButton", "globe", "regionUI", ...).
func named_instances(movie: String) -> Array:
	return screen(movie).get("namedInstances", []) as Array


func labels(movie: String) -> Dictionary:
	var row: Variant = screens.get(movie, null)
	if row is Dictionary:
		return (row as Dictionary).get("labels", {}) as Dictionary
	return {}


# --- atlases and support files ---------------------------------------------------

## The cooked PNG a textured draw references, decoded once, or NULL with the
## error recorded. Never a substitute texture.
func atlas_texture(atlas_path: String) -> ImageTexture:
	if _atlas_cache.has(atlas_path):
		return _atlas_cache[atlas_path] as ImageTexture
	if not loaded or not atlases.has(atlas_path):
		errors.append("draw references an undeclared atlas: %s" % atlas_path)
		_atlas_cache[atlas_path] = null
		return null
	var texture := _decode_image(bundle_root.path_join(atlas_path))
	_atlas_cache[atlas_path] = texture
	return texture


## One of the byte-preserved `art/compiledtextures/lm/*` strategic-map
## textures by file name, or NULL recorded. These are RETAIL'S BYTES; the
## bundle copies them verbatim and this decodes that copy.
func lm_texture(file_name: String) -> ImageTexture:
	var relative := "art/compiledtextures/lm/%s" % file_name
	if _atlas_cache.has(relative):
		return _atlas_cache[relative] as ImageTexture
	var declared := false
	for row_value in supporting_data:
		if String((row_value as Dictionary).get("virtualPath", "")) == relative:
			declared = true
			break
	if not loaded or not declared:
		errors.append("no supporting lm texture named %s" % file_name)
		_atlas_cache[relative] = null
		return null
	var texture := _decode_image(bundle_root.path_join(relative))
	_atlas_cache[relative] = texture
	return texture


## Absolute path of a byte-preserved supporting file (the livingworld*.ini
## documents), or "" when the bundle does not declare it.
func supporting_file_path(relative: String) -> String:
	for row_value in supporting_data:
		if String((row_value as Dictionary).get("virtualPath", "")) == relative:
			return bundle_root.path_join(relative)
	return ""


func _decode_image(path: String) -> ImageTexture:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		errors.append("cannot open %s" % path)
		return null
	if file.get_length() > MAX_ATLAS_BYTES:
		file.close()
		errors.append("%s is %d bytes, over the limit" % [path, file.get_length()])
		return null
	var bytes := file.get_buffer(file.get_length())
	file.close()
	var picture := Image.new()
	var extension := path.get_extension().to_lower()
	var status := ERR_UNAVAILABLE
	if extension == "png":
		status = picture.load_png_from_buffer(bytes)
	elif extension == "dds":
		status = picture.load_dds_from_buffer(bytes)
	elif extension == "tga":
		status = picture.load_tga_from_buffer(bytes)
	elif extension == "jpg" or extension == "jpeg":
		status = picture.load_jpg_from_buffer(bytes)
	else:
		errors.append("this loader does not read .%s images (%s)" % [extension, path])
		return null
	if status != OK:
		errors.append("the .%s decoder rejected %s (error %d)" % [extension, path, status])
		return null
	return ImageTexture.create_from_image(picture)


# --- internals -------------------------------------------------------------------

func _reset() -> void:
	loaded = false
	errors = PackedStringArray()
	screens = {}
	screen_names = PackedStringArray()
	atlases = PackedStringArray()
	atlas_sources = {}
	source_provenance = {}
	fonts = []
	supporting_data = []
	named_gaps = {}
	summary = {}
	missing_screens = {}
	_screen_cache = {}
	_atlas_cache = {}


func _fail(message: String) -> bool:
	errors.append(message)
	loaded = false
	return false


func _miss(movie: String, reason: String) -> void:
	if not missing_screens.has(movie):
		missing_screens[movie] = reason
