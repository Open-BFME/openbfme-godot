extends SceneTree
## PROVE, do not infer: run the UNMODIFIED production discovery code against a
## built bundle's pack roots with every War of the Ring environment variable
## EMPTY, and print what it found.
##
## Nothing here reimplements a search. Every path below is produced by the same
## `locate_and_load()` / `locate_document()` the shipped game calls, preloaded
## from res:// - so a PASS here means the player's build finds the data, and a
## FAIL here is the same failure the player would hit. The roots are built the
## way `main_menu.gd` builds them (ModLoader's own pack roots, sorted) and the
## way `wotr_screen.gd::_load_strings` extends them for the bundles that are
## searched BESIDE the region geometry rather than at a path of their own.
##
## Run it through tools/Probe-WotrBundles.ps1, which is what clears the
## environment. Run directly it will still work, but it will REFUSE if any
## OPENBFME_LIVING_* / OPENBFME_WOTR_* variable is set, because a pass with one
## of those set proves nothing about a bundle.

const SessionScript = preload("res://src/wotr/wotr_session.gd")
const MapBundleScript = preload("res://src/wotr/wotr_map_bundle.gd")
const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")
const MarkerModelsScript = preload("res://src/wotr/wotr_marker_models.gd")
const RegionImagesScript = preload("res://src/wotr/wotr_region_images.gd")
const SetupStringsScript = preload("res://src/wotr/wotr_setup_strings.gd")
const StringsScript = preload("res://src/wotr/wotr_strings.gd")
const LivingWorldUiScript = preload("res://src/wotr/wotr_living_world_ui.gd")
const MacrosScript = preload("res://src/wotr/wotr_macros.gd")
const WorldScript = preload("res://src/wotr/wotr_world.gd")
const ModLoaderScript = preload("res://src/content/mod_loader.gd")

## Every environment variable a loader would accept an override from. All of
## them must be empty for this probe to mean anything.
const OVERRIDE_ENVIRONMENT := [
	"OPENBFME_LIVING_WORLD_DOC",
	"OPENBFME_LIVING_MAP",
	"OPENBFME_LIVING_MAP_REGIONS",
	"OPENBFME_LIVING_WORLD_MARKERS",
	"OPENBFME_LIVING_WORLD_REGION_IMAGES",
	"OPENBFME_LIVING_WORLD_UI",
	"OPENBFME_LIVING_WORLD_STRINGS",
	"OPENBFME_LIVING_WORLD_MACROS",
	"OPENBFME_WOTR_SETUP_STRINGS",
]

var _failures: Array[String] = []
var _reported := false


## SAFETY NET, and it earned its place: a SCRIPT ERROR inside `_initialize()`
## aborts that function without stopping the main loop, so a one-line mistake in
## this probe looks exactly like a five-minute hang rather than like a bug. If
## the loop is ever reached without a verdict having been printed, say so and
## exit non-zero instead of running forever.
func _process(_delta: float) -> bool:
	if not _reported:
		print("PROBE-RESULT: REFUSED - the probe aborted before reaching a verdict; read the SCRIPT ERROR above this line")
	return true


func _initialize() -> void:
	print("PROBE-BEGIN wotr-bundle-probe")

	var dirty: Array[String] = []
	for name in OVERRIDE_ENVIRONMENT:
		var value := OS.get_environment(name).strip_edges()
		if not value.is_empty():
			dirty.append("%s=%s" % [name, value])
	if not dirty.is_empty():
		print("PROBE-RESULT: REFUSED - these overrides are set, so a pass would prove nothing: %s"
			% ", ".join(dirty))
		_reported = true
		quit(2)
		return
	print("PROBE-ENV: all %d override variables are empty" % OVERRIDE_ENVIRONMENT.size())

	var content := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	print("PROBE-ENV: OPENBFME_CONTENT=%s" % ("<unset>" if content.is_empty() else content))

	# EXACTLY main_menu.gd's roots: whatever ModLoader mounted, sorted.
	var loader = ModLoaderScript.new()
	var pack_roots: Array = []
	for root in loader.list_pack_roots():
		pack_roots.append(String(root))
	pack_roots.sort()
	print("PROBE-ROOTS: %d mounted pack root(s)" % pack_roots.size())
	for root in pack_roots:
		print("PROBE-ROOT:   %s" % String(root))
	if pack_roots.is_empty():
		print("PROBE-RESULT: REFUSED - no pack roots were mounted, so nothing could be searched")
		_reported = true
		quit(2)
		return

	# ---- the living-world document -----------------------------------------
	var document := SessionScript.locate_document(pack_roots)
	if bool(document.get("ok", false)):
		var world = WorldScript.new()
		var parsed := bool(world.load_from_dict(document.get("document", {}), ""))
		var regions := 0
		var templates := 0
		if parsed:
			regions = _count(world, ["regions", "region_by_id", "regions_by_id"])
			templates = _count(world, ["player_templates", "player_templates_by_id"])
		_pass("WAR-OF-THE-RING-AVAILABLE", "%s [source=%s] parsed=%s regions=%d playerTemplates=%d" % [
			String(document.get("path", "")), String(document.get("source", "")),
			str(parsed), regions, templates])
		if not parsed:
			_fail("WAR-OF-THE-RING-DOCUMENT-PARSE", "the located document did not load into WotrWorld")
	else:
		_fail("WAR-OF-THE-RING-AVAILABLE", String(document.get("reason", "")))

	# ---- the 3D map ---------------------------------------------------------
	var map_bundle = MapBundleScript.new()
	var map_found: Dictionary = map_bundle.locate_and_load(pack_roots)
	if bool(map_found.get("ok", false)):
		_pass("LIVING-MAP-AVAILABLE", "%s [%s] subObjects=%d untextured=%d unresolvedTextures=%d" % [
			String(map_found.get("root", "")), String(map_found.get("origin", "")),
			map_bundle.sub_objects.size(), map_bundle.untextured_sub_objects.size(),
			map_bundle.unresolved_textures.size()])
	else:
		_fail("LIVING-MAP-AVAILABLE", String(map_found.get("reason", "")))

	# ---- the filled territories --------------------------------------------
	var geometry = RegionGeometryScript.new()
	var geometry_found: Dictionary = geometry.locate_and_load(pack_roots)
	if bool(geometry_found.get("ok", false)):
		_pass("REGION-GEOMETRY-AVAILABLE", "%s [%s] shadedRegions=%d territories=%d triangles=%d unshaded=%d" % [
			String(geometry_found.get("root", "")), String(geometry_found.get("origin", "")),
			geometry.by_region.size(), geometry.by_territory.size(),
			geometry.total_triangles, geometry.regions_without_geometry.size()])
	else:
		_fail("REGION-GEOMETRY-AVAILABLE", String(geometry_found.get("reason", "")))

	# ---- everything searched BESIDE the region geometry ---------------------
	# Built exactly as wotr_screen.gd::_load_strings builds it.
	var roots: Array = []
	var geometry_root := String(geometry_found.get("root", ""))
	if not geometry_root.is_empty():
		roots.append(geometry_root)
	for root in pack_roots:
		roots.append(String(root).path_join(RegionGeometryScript.PACK_BUNDLE_RELATIVE))
	roots.append(RegionGeometryScript.USER_BUNDLE)

	var markers = MarkerModelsScript.new()
	var markers_found: Dictionary = markers.locate_and_load(roots)
	if bool(markers_found.get("ok", false)):
		_pass("MARKER-MODELS-AVAILABLE", "%s models=%d/%d meshes=%s texturesDeclared=%s texturesResolved=%s unresolvedModels=%d" % [
			String(markers_found.get("path", "")), markers.models.size(),
			int(markers.totals.get("modelsNamed", 0)), str(markers.totals.get("meshes", "?")),
			str(markers.totals.get("texturesDeclared", "?")), str(markers.totals.get("texturesResolved", "?")),
			markers.unresolved_models.size()])
	else:
		_fail("MARKER-MODELS-AVAILABLE", String(markers_found.get("reason", "")))

	var region_images = RegionImagesScript.new()
	var images_found: Dictionary = region_images.locate_and_load(roots)
	if bool(images_found.get("ok", false)):
		_pass("REGION-IMAGES-AVAILABLE", "%s regions=%d regionPortraits=%s fortressPortraits=%s atlases=%s" % [
			String(images_found.get("path", "")), region_images.regions.size(),
			str(region_images.totals.get("regionsWithRegionPortrait", "?")),
			str(region_images.totals.get("regionsWithFortressPortrait", "?")),
			str(region_images.totals.get("atlases", "?"))])
	else:
		_fail("REGION-IMAGES-AVAILABLE", String(images_found.get("reason", "")))

	var setup_strings = SetupStringsScript.new()
	var setup_found: Dictionary = setup_strings.locate_and_load(roots)
	if bool(setup_found.get("ok", false)):
		_pass("SETUP-STRINGS-AVAILABLE", "%s strings=%d" % [
			String(setup_found.get("path", "")), setup_strings.strings.size()])
	else:
		_fail("SETUP-STRINGS-AVAILABLE", String(setup_found.get("reason", "")))

	var strings = StringsScript.new()
	var strings_found: Dictionary = strings.locate_and_load(roots)
	if bool(strings_found.get("ok", false)):
		_pass("LIVING-WORLD-STRINGS-AVAILABLE", "%s strings=%d" % [
			String(strings_found.get("path", "")), strings.count()])
	else:
		_fail("LIVING-WORLD-STRINGS-AVAILABLE", String(strings_found.get("reason", "")))

	var ui = LivingWorldUiScript.new()
	var ui_found: Dictionary = ui.locate_and_load(roots)
	if bool(ui_found.get("ok", false)):
		_pass("LIVING-WORLD-UI-AVAILABLE", "%s iconsRequested=%s iconsResolved=%s atlases=%s" % [
			String(ui_found.get("path", "")), str(ui.totals.get("imageIdsRequested", "?")),
			str(ui.totals.get("imageIdsResolved", "?")), str(ui.totals.get("atlases", "?"))])
	else:
		_fail("LIVING-WORLD-UI-AVAILABLE", String(ui_found.get("reason", "")))

	var macros = MacrosScript.new()
	var macros_found: Dictionary = macros.locate_and_load(roots)
	if bool(macros_found.get("ok", false)):
		_pass("LIVING-WORLD-MACROS-AVAILABLE", "%s defines=%d" % [
			String(macros_found.get("path", "")), macros.defines.size()])
	else:
		_fail("LIVING-WORLD-MACROS-AVAILABLE", String(macros_found.get("reason", "")))

	_reported = true
	if _failures.is_empty():
		print("PROBE-RESULT: ALL-WOTR-BUNDLES-REACHABLE-WITHOUT-ENVIRONMENT")
		quit(0)
	else:
		print("PROBE-RESULT: %d BUNDLE(S) UNREACHABLE - %s" % [_failures.size(), ", ".join(_failures)])
		quit(1)


func _pass(label: String, detail: String) -> void:
	print("PROBE-RESULT: %s | %s" % [label, detail])


func _fail(label: String, reason: String) -> void:
	_failures.append(label)
	print("PROBE-RESULT: %s FAILED" % label)
	for line in reason.split("\n"):
		print("PROBE-REASON:   %s" % line)


## The world object's region table has been spelled differently over time. Ask
## for a count without asserting which spelling is current - a probe that
## crashed on a renamed member would look like a missing bundle.
func _count(object, candidates: Array) -> int:
	for name in candidates:
		if not (name in object):
			continue
		var value = object.get(name)
		if value is Dictionary or value is Array:
			return value.size()
	return -1
