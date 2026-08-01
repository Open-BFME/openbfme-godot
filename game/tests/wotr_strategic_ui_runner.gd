extends SceneTree

## RETAIL'S STRATEGIC SCREENS, checked against what the bundle ACTUALLY
## RESOLVED - not against a screenshot.
##
## This runner exists because the strategic-ui import makes claims about
## RESOLUTION that can all fail silently into something that photographs
## fine: a palantir whose stop frame flattened to nothing, an END TURN button
## that lost its authored states, a textured draw pointing at an atlas that
## never landed, or a named gap quietly dropped so the bundle looks more
## complete than the APT data is. Each is asserted here by name.
##
## LIVENESS. The expected check count is asserted, so a function that aborts
## before its assertions fails the run instead of silently shrinking it.
##
## Usage:
##   Godot_v4.7 --headless --path game --script res://tests/wotr_strategic_ui_runner.gd
## With no bundle present it still runs its unbound checks and says so; that
## is the 4/4 mode.

const UiScript = preload("res://src/wotr/wotr_strategic_ui.gd")
const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")

## MEASUREMENTS of the converted retail closure, not targets: 24 screens, 79
## cooked atlases, 3 retail font winners, and the 6 named gaps the converter
## must keep stating. If the converter starts losing screens or gaps, these
## are where it shows up.
##
## The atlas census was 71 while this lane was (wrongly) cooked from the BFME2
## asset layer; RotWK ships eight sheets BFME2 does not - a third MenuExport
## page, StrategicVeterancy's own 18/19/20, and the extra PlayerPage, Armies
## and Structures pages - so 79 is the RotWK number.
const RETAIL_SCREENS := 24
const RETAIL_ATLASES := 79
const RETAIL_FONTS := 3

## PER-SHEET EDITION EVIDENCE. Each row is `[retail .tga, RotWK sha256 prefix,
## BFME2 sha256 prefix]`, measured from the two effective-assets views. These
## are the sheets whose PIXELS differ between the editions, so they are the
## only way to catch a wrong-edition bundle that otherwise loads perfectly:
## the palantir ring, the stats plaque, and the shell plate that is BLUE-STEEL
## in RotWK and GREEN in BFME2.
const EDITION_WITNESS_SHEETS := [
	["art/Textures/apt_StrategicPalantir_1.tga", "cd13947c49ba", "cc4ce1696fed"],
	["art/Textures/apt_StrategicStats_1.tga", "305a827291be", "88cd052ba75a"],
	["art/Textures/apt_MenuExport_1.tga", "68d4eadab9cc", "83db043843ce"],
	["art/Textures/apt_MenuExport_2.tga", "3b51973b8e25", "268de9714542"],
	["art/Textures/apt_StrategicDetailsTerritory_1.tga", "2e9dea79fb58", "38746ddfd787"],
]
const RETAIL_NAMED_GAPS := [
	"action-scripts-not-executed",
	"dynamic-content-slots-are-empty",
	"nine-slice-metrics-not-authored",
	"sachawynter-font-binding-unproven",
	"strategic-text-values-are-live",
	"timeline-playback-not-bound",
]
## Every strategic screen the brief names, by exact movie identity.
const RETAIL_SCREEN_NAMES := [
	"LivingWorldUI", "StrategicHUD", "StrategicPalantir", "StrategicDetailsTray",
	"StrategicDetailsTerritory", "StrategicDetailsArmies", "StrategicDetailsStructures",
	"StrategicDetailsBuildQueue", "StrategicDetailsRegion", "StrategicDetailsArmyRetinue",
	"StrategicEndTurnButton", "StrategicNextTurnInd", "StrategicPlayerStatus",
	"StrategicBattlePrompt", "StrategicBattlePromptArmyPanel", "StrategicBattlePromptPlayerPage",
	"StrategicConflictResults", "StrategicChecklist", "StrategicVeterancy", "StrategicStats",
	"StrategicDynamicAutoResolve", "StrategicArmyUnitSwapper", "StrategicRegionAward", "TimeLine",
]

## 4 absence checks + 27 bound checks (23 originals plus the four edition
## checks added when this lane was corrected off the BFME2 asset layer).
## Stated rather than counted by hand at the bottom: the liveness assertion
## compares against this, so a function that aborts before its assertions
## reddens the run.
const CHECKS_WITH_BUNDLE := 31
const CHECKS_WITHOUT_BUNDLE := 4

var _passed := 0
var _failed := 0


func _initialize() -> void:
	print("== WAR OF THE RING STRATEGIC UI BUNDLE ==")
	var ui := UiScript.new()
	var located: Dictionary = ui.locate_and_load(_roots())
	var bound := bool(located.get("ok", false))
	if bound:
		print("bundle: %s" % String(located["path"]))
		for line in ui.describe_load():
			print("  %s" % line)
	else:
		print("bundle: NONE - %s" % String(located["reason"]).split(".")[0])

	_check_the_loader_reports_its_own_absence(located, bound)
	if bound:
		_check_the_bundle_carries_every_screen(ui)
		_check_the_art_is_rotwks_and_not_bfme2s(ui)
		_check_the_named_gaps_are_stated(ui)
		_check_the_palantir_authored_stop_frame_carries_its_art(ui)
		_check_the_end_turn_button_ships_every_authored_state(ui)
		_check_the_tray_and_details_screens_carry_textured_retail_art(ui)
		_check_textured_draws_resolve_to_real_decoded_atlases(ui)
		_check_the_named_element_ids_are_authored(ui)
		_check_livingworldui_exports_the_region_popup(ui)
		_check_the_fonts_are_the_retail_winners_and_only_albertus_binds(ui)
		_check_the_supporting_living_world_data_travelled(ui)
		_check_an_unknown_screen_produces_nothing_and_is_recorded(ui)

	var expected := CHECKS_WITH_BUNDLE if bound else CHECKS_WITHOUT_BUNDLE
	var total := _passed + _failed
	print("\nchecks run %d (expected %d), passed %d, failed %d" % [
		total, expected, _passed, _failed])
	if total != expected:
		print("LIVENESS FAILURE: %d checks ran, %d expected. A check was skipped." % [
			total, expected])
		quit(1)
		return
	quit(1 if _failed > 0 else 0)


## Where a bundle might be, mirroring what the screen itself searches: whatever
## `OPENBFME_LIVING_MAP_REGIONS` names, and its sibling by the living map.
func _roots() -> Array:
	var roots: Array = []
	var probed: Dictionary = RegionGeometryScript.probe([])
	if bool(probed.get("found", false)):
		roots.append(String(probed["root"]))
	roots.append(RegionGeometryScript.USER_BUNDLE)
	return roots


# --- the checks ------------------------------------------------------------------

## A MISSING BUNDLE IS A SENTENCE, NOT A FALSE. The reason must name what is
## absent, what it costs on screen, and the command that produces one.
func _check_the_loader_reports_its_own_absence(located: Dictionary, bound: bool) -> void:
	var reason := String(located.get("reason", ""))
	_check("a_bound_bundle_reports_no_reason_and_an_absent_one_reports_a_reason",
		(bound and reason.is_empty()) or (not bound and not reason.is_empty()))
	var empty := UiScript.new()
	var nothing: Dictionary = empty.locate_and_load([])
	if bool(nothing.get("ok", false)):
		_check("loading_with_no_roots_either_finds_the_environments_bundle_or_explains",
			empty.loaded and not empty.screen_names.is_empty())
	else:
		var text := String(nothing.get("reason", ""))
		_check("loading_with_no_roots_explains_itself",
			text.contains("retail_strategic_apt_convert") and text.contains(UiScript.BUNDLE_ENV),
			text.split(".")[0])
	var refused := UiScript.new()
	_check("a_path_that_is_not_a_bundle_is_refused_rather_than_half_loaded",
		not refused.load_from("res://project.godot") and not refused.loaded)
	_check("a_refusal_carries_at_least_one_stated_error", not refused.errors.is_empty(),
		", ".join(Array(refused.errors)))


## ALL 24 SCREENS, BY EXACT NAME. "Most of them" is how a lost screen hides.
func _check_the_bundle_carries_every_screen(ui) -> void:
	var missing: Array[String] = []
	for name in RETAIL_SCREEN_NAMES:
		if not ui.screens.has(name):
			missing.append(String(name))
	_check("the_bundle_carries_every_strategic_screen_retail_ships",
		ui.screen_names.size() == RETAIL_SCREENS and missing.is_empty(),
		"%d of %d, missing: %s" % [ui.screen_names.size(), RETAIL_SCREENS,
			", ".join(missing) if not missing.is_empty() else "none"])
	var unparsed: Array[String] = []
	for name in ui.screen_names:
		if ui.screen(String(name)).is_empty():
			unparsed.append(String(name))
	_check("every_declared_screen_file_parses_and_declares_its_own_identity",
		unparsed.is_empty(), ", ".join(unparsed))


## THE EDITION. This lane shipped BFME2's strategic art once, and every other
## check in this file passed while it did - the two editions author the same
## 24 screens, the same element ids, the same labels. Only the PIXELS differ.
## So the edition is asserted three ways: the sealed source aggregate, the
## per-sheet receipts, and the digests of the sheets that actually differ.
func _check_the_art_is_rotwks_and_not_bfme2s(ui) -> void:
	_check("the_bundle_states_the_rotwk_retail_source_aggregate",
		String(ui.source_provenance.get("sourceAggregateSha256", ""))
			== UiScript.ROTWK_SOURCE_AGGREGATE_SHA256,
		String(ui.source_provenance.get("sourceAggregateSha256", "(unstated)")))
	_check("every_cooked_atlas_carries_the_retail_sheet_it_came_from",
		ui.atlas_sources.size() == ui.atlases.size() and ui.atlases.size() > 0,
		"%d receipts for %d atlases" % [ui.atlas_sources.size(), ui.atlases.size()])

	# Index the receipts by their retail source path so a sheet can be named
	# the way retail names it rather than by its hash-addressed cooked name.
	var by_source: Dictionary = {}
	for atlas_path in ui.atlases:
		var row: Dictionary = ui.atlas_source(String(atlas_path))
		by_source[String(row.get("sourceVirtualPath", ""))] = row
	var wrong_edition: Array[String] = []
	var absent: Array[String] = []
	for witness in EDITION_WITNESS_SHEETS:
		var source_path := String(witness[0])
		if not by_source.has(source_path):
			absent.append(source_path)
			continue
		var digest := String((by_source[source_path] as Dictionary).get("sha256", ""))
		if digest.begins_with(String(witness[2])):
			wrong_edition.append("%s is BFME2's" % source_path)
		elif not digest.begins_with(String(witness[1])):
			wrong_edition.append("%s is neither edition's (%s)" % [source_path, digest.substr(0, 12)])
	_check("every_sheet_that_differs_between_editions_is_present",
		absent.is_empty(), ", ".join(absent))
	_check("and_every_one_of_them_is_rotwks_sheet_not_bfme2s",
		wrong_edition.is_empty(), ", ".join(wrong_edition))


## THE HONEST BOUNDARY: the six gaps the APT data genuinely has must stay
## stated. A converter that dropped one did not close it; it hid it.
func _check_the_named_gaps_are_stated(ui) -> void:
	var names: Array[String] = []
	for key in ui.named_gaps.keys():
		names.append(String(key))
	names.sort()
	_check("every_named_gap_the_apt_data_actually_has_is_stated",
		names == RETAIL_NAMED_GAPS, ", ".join(names))


## THE PALANTIR. Frame 0 is authored empty; the full oval, swirls and command
## ring sit on its scripted stop frame. If that flattening broke, the single
## most recognisable piece of the strategic HUD silently vanishes.
func _check_the_palantir_authored_stop_frame_carries_its_art(ui) -> void:
	var contract: Dictionary = ui.screen("StrategicPalantir")
	var stops: Array = contract.get("authoredStopFrames", []) as Array
	var richest: Dictionary = ui.richest_frame("StrategicPalantir")
	_check("the_palantir_authors_a_scripted_stop_frame",
		not stops.is_empty(), str(stops))
	_check("the_palantir_stop_frame_carries_over_a_thousand_retail_draws",
		bool(richest.get("authoredStop", false)) and int(richest.get("drawCount", 0)) > 1000,
		"%d draws on frame %d" % [int(richest.get("drawCount", 0)), int(richest.get("frameIndex", -1))])


## THE END TURN BUTTON ships every authored interaction state under its own
## authored label, and each state actually flattened to visible triangles.
func _check_the_end_turn_button_ships_every_authored_state(ui) -> void:
	var labels: Dictionary = ui.labels("StrategicEndTurnButton")
	var required := ["_up", "_over", "_down", "_disabled"]
	var missing: Array[String] = []
	for label in required:
		if not labels.has(label):
			missing.append(String(label))
	_check("the_end_turn_button_authors_up_over_down_and_disabled_states",
		missing.is_empty(), ", ".join(missing))
	var empty_states: Array[String] = []
	for label in required:
		var frame: Dictionary = ui.frame_by_label("StrategicEndTurnButton", String(label))
		if int(frame.get("drawCount", 0)) <= 0:
			empty_states.append(String(label))
	_check("every_end_turn_state_flattened_to_real_draws",
		empty_states.is_empty(), ", ".join(empty_states))


## THE TRAY AND DETAILS CARDS - the parchment panels the living-world HUD
## hangs its TERRITORY/ARMIES/STRUCTURES content on - must carry TEXTURED
## retail triangles, not just solid fills.
func _check_the_tray_and_details_screens_carry_textured_retail_art(ui) -> void:
	var flat: Array[String] = []
	for name in ["StrategicDetailsTray", "StrategicDetailsTerritory",
			"StrategicDetailsArmies", "StrategicDetailsStructures",
			"StrategicDetailsBuildQueue", "TimeLine"]:
		var row: Dictionary = ui.screens.get(String(name), {}) as Dictionary
		var summary: Dictionary = row.get("summary", {}) as Dictionary
		if int(summary.get("texturedTriangleCount", 0)) <= 0:
			flat.append(String(name))
	_check("every_tray_details_and_timeline_screen_carries_textured_retail_art",
		flat.is_empty(), ", ".join(flat))


## A TEXTURED DRAW THAT NAMES A MISSING ATLAS would render as nothing. Every
## atlas a sampled screen references must be declared, on disk, and decodable.
func _check_textured_draws_resolve_to_real_decoded_atlases(ui) -> void:
	var referenced: Dictionary = {}
	for name in ["StrategicPalantir", "StrategicDetailsTray", "StrategicEndTurnButton"]:
		var contract: Dictionary = ui.screen(String(name))
		for frame_value in contract.get("flattenedFrames", []) as Array:
			for draw_value in (frame_value as Dictionary).get("draws", []) as Array:
				var draw := draw_value as Dictionary
				if String(draw.get("kind", "")) == "textured-triangle":
					referenced[String(draw.get("atlas", ""))] = true
	var bad: Array[String] = []
	for atlas_path in referenced.keys():
		var texture: ImageTexture = ui.atlas_texture(String(atlas_path))
		if texture == null or texture.get_width() <= 0:
			bad.append(String(atlas_path))
	_check("sampled_screens_reference_at_least_one_atlas", referenced.size() > 0,
		"%d atlas(es)" % referenced.size())
	_check("every_referenced_atlas_is_declared_on_disk_and_decodes",
		bad.is_empty() and ui.errors.is_empty(),
		", ".join(bad) + " errors: " + ", ".join(Array(ui.errors)))
	_check("the_bundle_declares_the_measured_atlas_census",
		ui.atlases.size() == RETAIL_ATLASES,
		"%d of %d" % [ui.atlases.size(), RETAIL_ATLASES])


## THE ELEMENT IDS other streams address retail's layout by are AUTHORED
## PlaceObject names, and the load-bearing ones must be present by name.
func _check_the_named_element_ids_are_authored(ui) -> void:
	var hud_names: Dictionary = {}
	for row_value in ui.named_instances("StrategicHUD"):
		hud_names[String((row_value as Dictionary).get("name", ""))] = true
	var missing: Array[String] = []
	for required in ["endTurnButton", "globe", "stats", "checklist", "selectionDetails"]:
		if not hud_names.has(required):
			missing.append(String(required))
	_check("the_strategic_hud_authors_its_load_bearing_element_names",
		missing.is_empty(), ", ".join(missing))
	var palantir_names: Dictionary = {}
	for row_value in ui.named_instances("StrategicPalantir"):
		palantir_names[String((row_value as Dictionary).get("name", ""))] = true
	var missing_palantir: Array[String] = []
	for required in ["commandUI", "regionUI", "selectionUI", "bigSwirl", "smallSwirl"]:
		if not palantir_names.has(required):
			missing_palantir.append(String(required))
	_check("the_palantir_authors_its_command_region_and_swirl_names",
		missing_palantir.is_empty(), ", ".join(missing_palantir))


## THE REGION POPUP - the gold filigree card retail floats over a region -
## lives in LivingWorldUI's EXPORTS, not on its root frame. If the export
## flattening broke, that card is gone while the screen count still reads 24.
func _check_livingworldui_exports_the_region_popup(ui) -> void:
	var popup: Dictionary = ui.export_symbol("LivingWorldUI", "RegionPopup")
	var frames: Array = popup.get("flattenedFrames", []) as Array
	var drew := false
	for frame_value in frames:
		if int((frame_value as Dictionary).get("drawCount", 0)) > 0:
			drew = true
	_check("livingworldui_exports_the_region_popup_with_real_draws",
		not popup.is_empty() and drew, "%d flattened frame(s)" % frames.size())
	var notices := 0
	for symbol in ["RegionConqueredNoticeGood", "RegionConqueredNoticeEvil"]:
		if not ui.export_symbol("LivingWorldUI", String(symbol)).is_empty():
			notices += 1
	_check("both_region_conquered_notices_are_exported", notices == 2, str(notices))


## THE FONTS. Three retail winners travel byte-for-byte; exactly ONE authored
## APT binding is proven (Albertus MT). A bundle claiming a SachaWynter
## binding would be claiming something the shipped bytes do not prove.
func _check_the_fonts_are_the_retail_winners_and_only_albertus_binds(ui) -> void:
	_check("the_bundle_carries_the_three_retail_font_winners",
		ui.fonts.size() == RETAIL_FONTS, "%d of %d" % [ui.fonts.size(), RETAIL_FONTS])
	var proven: Array[String] = []
	for row_value in ui.fonts:
		var row := row_value as Dictionary
		if bool(row.get("bindingProven", false)):
			proven.append(String(row.get("aptFontName", "")))
	_check("only_albertus_mt_claims_a_proven_apt_binding",
		proven == ["Albertus MT"], ", ".join(proven))
	_check("the_sachawynter_gap_is_stated_rather_than_silently_bound",
		ui.named_gaps.has("sachawynter-font-binding-unproven"))


## THE SUPPORTING DATA the other streams read - the livingworld ini documents
## and the lm strategic-map texture set - travelled byte-for-byte.
func _check_the_supporting_living_world_data_travelled(ui) -> void:
	var missing: Array[String] = []
	for relative in ["data/ini/livingworldbuildingicons.ini",
			"data/ini/livingworldbuildploticons.ini", "data/ini/livingworldbuildings.ini",
			"data/ini/livingworldlogic.ini", "data/ini/livingworldplayers.ini",
			"data/ini/livingworldregioneffects.ini", "data/ini/fontsubstitution.ini"]:
		if String(ui.supporting_file_path(String(relative))).is_empty():
			missing.append(String(relative))
	_check("every_named_living_world_ini_travelled", missing.is_empty(), ", ".join(missing))
	var lm := 0
	for row_value in ui.supporting_data:
		if String((row_value as Dictionary).get("virtualPath", "")).begins_with("art/compiledtextures/lm/"):
			lm += 1
	_check("the_lm_strategic_map_texture_set_travelled", lm >= 55, "%d file(s)" % lm)
	var good_pb: ImageTexture = ui.lm_texture("lm_goodpb.dds")
	_check("a_sampled_lm_texture_decodes_from_retails_own_bytes",
		good_pb != null and good_pb.get_width() > 0,
		"null" if good_pb == null else "%dx%d" % [good_pb.get_width(), good_pb.get_height()])


## AN UNKNOWN SCREEN GETS NOTHING, AND THE MISS IS RECORDED. This is the
## property that makes "no fabrication" checkable rather than a promise.
func _check_an_unknown_screen_produces_nothing_and_is_recorded(ui) -> void:
	var invented := "NotARetailScreen_OpenBfmeProbe"
	var before: int = ui.missing_screens.size()
	_check("a_screen_retail_never_authored_produces_nothing",
		ui.screen(invented).is_empty())
	_check("and_the_miss_is_recorded_by_name_rather_than_swallowed",
		ui.missing_screens.has(invented) and ui.missing_screens.size() == before + 1,
		String(ui.missing_screens.get(invented, "not recorded")))


# --- reporting ------------------------------------------------------------------

func _check(what: String, condition: bool, detail: String = "") -> void:
	if not condition:
		_fail(what, detail)
		return
	_passed += 1
	if detail.is_empty():
		print("  PASS  %s" % what)
	else:
		print("  PASS  %s (%s)" % [what, detail])


func _fail(what: String, detail: String) -> void:
	_failed += 1
	print("  FAIL  %s :: %s" % [what, detail])
