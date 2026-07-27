extends SceneTree

## THE REGION CARD AND THE LAYOUT AROUND IT.
##
## Two claims, both of which can fail into something that photographs fine:
##
##   1. THE CARD SHOWS RETAIL'S OWN PICTURE OF THAT REGION. A card that quietly
##      shows the previous region's portrait, or a neighbour's, looks correct in
##      every screenshot that does not have the right answer beside it. And the
##      three portraits retail itself names and never defines must draw an empty
##      plate rather than the nearest similarly-named picture.
##   2. THE SCREEN USES THE WINDOW IT HAS. It was authored at ~1860x800 with
##      hand-written pixel positions, and on the 2560x1351 window it actually
##      runs in, 40% of the screen was empty. A layout that "works" is one where
##      nothing overlaps and nothing is off the panel at every size the owner
##      might use - which is a property, not a picture.
##
## LIVENESS. The expected check count is asserted, so a function that aborts
## before its assertions fails the run instead of silently shrinking it.
##
## Usage:
##   Godot_v4.7 --headless --path game --script tests/wotr_region_card_runner.gd
## With no bundle the layout half still runs; that is the 7/7 mode.

const RegionImagesScript = preload("res://src/wotr/wotr_region_images.gd")
const ScreenScript = preload("res://src/ui/wotr_screen.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")

## RETAIL'S OWN NUMBERS, measured off the shipped documents rather than chosen.
## The RotWK living-world document declares 90 region rows across its campaigns
## (52 in the playable one and 38 `Region_N` placeholders in the other). Of those,
## 52 name a `RegionPortrait` and TEN name a `Fortress.Portrait`.
##
## THE BRIEF FOR THIS WORK SAID "every region carries fortress.portrait". It does
## not, and the measurement is recorded here rather than quietly worked around:
## ten regions do, and retail defines the picture for only seven of them.
const RETAIL_REGION_ROWS := 90
const RETAIL_WITH_REGION_PORTRAIT := 52
const RETAIL_WITH_FORTRESS_PORTRAIT := 10
const RETAIL_FORTRESS_PORTRAITS_DEFINED := 7

## THE THREE RETAIL NAMES AND NEVER DEFINES, asserted BY NAME rather than by an
## "at most three missing" tolerance, so a fourth cannot hide behind them. The
## nearest ids in the archives are `BPCFornostGate` and `BPCFornostCitadel`,
## which are DIFFERENT pictures of different things; none of the three is bridged
## to either.
const RETAIL_UNDEFINED_PORTRAITS := ["BPCAmonSul", "BPCCarnDum", "BPCFornost"]

## Window sizes the layout must hold at: the authored one, the owner's actual
## one, a laptop, and an absurd one on each axis.
const SIZES := [
	Vector2(1860.0, 800.0), Vector2(2560.0, 1351.0), Vector2(1366.0, 768.0),
	Vector2(3840.0, 1600.0), Vector2(1280.0, 1024.0),
]

const CHECKS_WITH_BUNDLE := 16
const CHECKS_WITHOUT_BUNDLE := 7

var _passed := 0
var _failed := 0


func _initialize() -> void:
	print("== WAR OF THE RING REGION CARD AND LAYOUT ==")
	var images := RegionImagesScript.new()
	var located: Dictionary = images.locate_and_load(_roots())
	var bound := bool(located.get("ok", false))
	print("bundle: %s" % (String(located["path"]) if bound
		else "NONE - " + String(located["reason"]).split(".")[0]))

	_check_the_loader_reports_its_own_absence(located, bound)
	if bound:
		_check_the_census_is_retails_own(images)
		_check_the_portrait_ladder_uses_only_authored_fields(images)
		_check_an_undefined_portrait_draws_nothing_rather_than_a_lookalike(images)
	_check_the_layout_uses_the_window_at_every_size()

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


func _roots() -> Array:
	var roots: Array = []
	var probed: Dictionary = RegionGeometryScript.probe([])
	if bool(probed.get("found", false)):
		roots.append(String(probed["root"]))
	roots.append(RegionGeometryScript.USER_BUNDLE)
	return roots


# --- the checks -----------------------------------------------------------------

func _check_the_loader_reports_its_own_absence(located: Dictionary, bound: bool) -> void:
	var reason := String(located.get("reason", ""))
	_check("a_bound_bundle_reports_no_reason_and_an_absent_one_reports_a_reason",
		(bound and reason.is_empty()) or (not bound and not reason.is_empty()))
	var refused := RegionImagesScript.new()
	_check("a_path_that_is_not_a_bundle_is_refused_rather_than_half_loaded",
		not refused.load_from("res://project.godot") and not refused.loaded
			and not refused.errors.is_empty(),
		", ".join(Array(refused.errors)))
	# A REGION THE BUNDLE HAS NEVER HEARD OF GETS NOTHING, not the last one asked
	# for. The crop cache makes this a real risk rather than a theoretical one.
	var probe := RegionImagesScript.new()
	if bound:
		probe.locate_and_load(_roots())
	var invented: Dictionary = probe.region_portrait("NoSuchRegion_%d" % Time.get_ticks_usec())
	_check("a_region_retail_never_declared_gets_no_portrait_rather_than_the_last_one",
		invented["texture"] == null and String(invented["id"]).is_empty())


func _check_the_census_is_retails_own(images) -> void:
	_check("the_bundle_carries_every_region_row_the_document_declares",
		int(images.totals.get("regions", 0)) == RETAIL_REGION_ROWS,
		"%d of %d" % [int(images.totals.get("regions", 0)), RETAIL_REGION_ROWS])
	var with_region := 0
	var with_fortress := 0
	for key in images.regions.keys():
		var row: Dictionary = images.regions[key] as Dictionary
		if not String(row.get("regionPortrait", "")).is_empty():
			with_region += 1
		if not String(row.get("fortressPortrait", "")).is_empty():
			with_fortress += 1
	_check("retails_own_count_of_regions_naming_a_RegionPortrait",
		with_region == RETAIL_WITH_REGION_PORTRAIT
			and int(images.totals.get("regionsWithRegionPortrait", 0)) == RETAIL_WITH_REGION_PORTRAIT,
		"%d name one, %d resolve" % [
			with_region, int(images.totals.get("regionsWithRegionPortrait", 0))])
	_check("retails_own_count_of_regions_naming_a_Fortress_Portrait",
		with_fortress == RETAIL_WITH_FORTRESS_PORTRAIT
			and int(images.totals.get("regionsWithFortressPortrait", 0)) == RETAIL_FORTRESS_PORTRAITS_DEFINED,
		"%d name one, %d resolve" % [
			with_fortress, int(images.totals.get("regionsWithFortressPortrait", 0))])
	# NO REGION IS DECLARED TWICE WITH DIFFERENT ART. Retail lists some regions in
	# more than one campaign, and two campaigns disagreeing about a region's
	# picture would be a real conflict rather than something to pick a winner for.
	var conflicting: Array = images.gaps.get("regionsDeclaredTwiceWithDifferentArt", []) as Array
	_check("no_region_is_declared_twice_with_two_different_portraits",
		conflicting.is_empty(), "%d conflict(s)" % conflicting.size())


func _check_the_portrait_ladder_uses_only_authored_fields(images) -> void:
	# A region with BOTH fields takes the fortress plate; one with only the
	# region portrait takes that. Both are the region's own authored fields, and
	# the sample regions are found from the bundle rather than named here.
	var with_both := ""
	var with_region_only := ""
	for key in images.regions.keys():
		var row: Dictionary = images.regions[key] as Dictionary
		var fortress := String(row.get("fortressPortraitResolved", ""))
		var region := String(row.get("regionPortraitResolved", ""))
		if with_both.is_empty() and not fortress.is_empty() and not region.is_empty():
			with_both = String(key)
		if with_region_only.is_empty() and fortress.is_empty() and not region.is_empty():
			with_region_only = String(key)
	var top: Dictionary = images.region_portrait(with_both)
	_check("a_region_with_a_fortress_takes_its_own_Fortress_Portrait",
		String(top["source"]) == "Fortress.Portrait" and top["texture"] != null
			and String(top["id"]) == String(
				(images.regions[with_both] as Dictionary).get("fortressPortraitResolved", "")),
		"%s -> %s [%s]" % [with_both, String(top["id"]), String(top["source"])])
	var next: Dictionary = images.region_portrait(with_region_only)
	_check("a_region_without_one_takes_its_own_RegionPortrait",
		String(next["source"]) == "RegionPortrait" and next["texture"] != null,
		"%s -> %s [%s]" % [with_region_only, String(next["id"]), String(next["source"])])
	# EVERY CROP IS A REAL RECTANGLE INSIDE ITS OWN ATLAS. A crop that ran off the
	# edge would draw a band of the neighbouring icon, which reads as art.
	var checked := 0
	var outside: Array[String] = []
	for image_id in images.images.keys():
		var texture: Texture2D = images.image(String(image_id))
		if texture == null:
			continue
		var crop := texture as AtlasTexture
		checked += 1
		var atlas_rect := Rect2(Vector2.ZERO, crop.atlas.get_size())
		if not atlas_rect.encloses(crop.region):
			outside.append(String(image_id))
	_check("every_resolved_region_portrait_crops_a_real_rectangle_inside_its_atlas",
		checked > 0 and outside.is_empty() and images.errors.is_empty(),
		"%d crop(s); outside: %s; errors: %s" % [
			checked, ", ".join(outside), ", ".join(Array(images.errors))])


func _check_an_undefined_portrait_draws_nothing_rather_than_a_lookalike(images) -> void:
	var missing: Array = images.gaps.get("missingImageIds", []) as Array
	var names: Array[String] = []
	for value in missing:
		names.append(String(value))
	names.sort()
	var wanted := RETAIL_UNDEFINED_PORTRAITS.duplicate()
	wanted.sort()
	_check("the_only_portraits_with_no_definition_are_the_three_retail_never_wrote",
		names == wanted, "%s vs %s" % [str(names), str(wanted)])
	# AND THE REGIONS THAT NAME THEM SAY SO, by name, with no picture at all.
	var bridged: Array[String] = []
	var said: Array[String] = []
	for key in images.regions.keys():
		var row: Dictionary = images.regions[key] as Dictionary
		if not RETAIL_UNDEFINED_PORTRAITS.has(String(row.get("fortressPortrait", ""))):
			continue
		said.append(String(key))
		var found: Dictionary = images.region_portrait(String(key))
		# The region portrait may legitimately stand in - it is that region's OWN
		# other authored field - but the FORTRESS id must never have resolved.
		if not String(row.get("fortressPortraitResolved", "")).is_empty():
			bridged.append(String(key))
		if String(found["source"]) == "Fortress.Portrait":
			bridged.append(String(key))
	said.sort()
	_check("a_region_naming_an_undefined_fortress_portrait_is_never_bridged_to_a_lookalike",
		bridged.is_empty() and said.size() == RETAIL_UNDEFINED_PORTRAITS.size(),
		"%d region(s) affected: %s; bridged: %s" % [said.size(), ", ".join(said), str(bridged)])


## THE LAYOUT, as PROPERTIES rather than pixels: at every size, the map is inside
## the panel, the sidebar does not overlap it, nothing runs off the bottom, and
## the map actually grows with the window rather than staying at its authored
## 1240x548.
func _check_the_layout_uses_the_window_at_every_size() -> void:
	var screen := ScreenScript.new()
	root.add_child(screen)
	screen.configure(null, [], "layout probe", [])
	var overlaps: Array[String] = []
	var off_panel: Array[String] = []
	var areas: Array[float] = []
	for value in SIZES:
		var frame := value as Vector2
		screen.size = frame
		screen._relayout()
		var map_box := Rect2(screen.map3d.position, screen.map3d.size)
		areas.append(map_box.size.x * map_box.size.y)
		var side: Array[Control] = [
			screen.standings_label, screen.detail_label, screen.attack_button,
			screen.end_turn_button, screen.back_button, screen.region_portrait_frame,
		]
		for control in side:
			var box := Rect2(control.position, control.size)
			if box.intersects(map_box):
				overlaps.append("%s over the map at %dx%d" % [
					control.name, int(frame.x), int(frame.y)])
			if box.end.x > frame.x + 0.5 or box.end.y > frame.y + 0.5 \
					or box.position.x < -0.5 or box.position.y < -0.5:
				off_panel.append("%s off the panel at %dx%d (%s)" % [
					control.name, int(frame.x), int(frame.y), str(box)])
		if map_box.end.x > frame.x + 0.5 or map_box.end.y > frame.y + 0.5:
			off_panel.append("the map itself off the panel at %dx%d" % [
				int(frame.x), int(frame.y)])
	_check("no_side_panel_or_button_ever_overlaps_the_map",
		overlaps.is_empty(), ", ".join(overlaps))
	_check("nothing_is_ever_laid_out_off_the_panel",
		off_panel.is_empty(), ", ".join(off_panel))
	# THE POINT OF THE WHOLE EXERCISE: a bigger window means a bigger map. On the
	# owner's own 2560x1351 window it must be substantially bigger than at the
	# authored size, not the same.
	#
	# AT the authored size the map is 1264 wide against the hand-written 1240, and
	# 496 tall against 548. The height is DOWN BY 52 PIXELS and that is a trade
	# made on purpose, not a regression that slipped through: the conversion
	# report under the map is what says what is retail's and what is not, and at
	# 1860x800 it was being clipped mid-sentence. The width - the axis the map was
	# actually starved on - is up.
	screen.size = SIZES[1] as Vector2
	screen._relayout()
	var grown := screen.map3d.size.x * screen.map3d.size.y
	screen.size = SIZES[0] as Vector2
	screen._relayout()
	var authored := screen.map3d.size.x * screen.map3d.size.y
	_check("the_map_grows_with_the_window_rather_than_staying_at_its_authored_size",
		grown > authored * 1.8 and screen.map3d.size.x >= 1240.0,
		"authored %.0f px^2 (%.0f wide) -> %.0f px^2 at 2560x1351 (%.2fx)" % [
			authored, screen.map3d.size.x, grown, grown / maxf(authored, 1.0)])
	# AND SHRINKING IS BOUNDED. Below the stated floor the map stops shrinking and
	# the sidebar gives room back, so a small window degrades rather than breaks.
	screen.size = Vector2(1100.0, 700.0)
	screen._relayout()
	_check("the_map_never_shrinks_below_its_stated_floor",
		screen.map3d.size.x >= ScreenScript.MAP_MIN.x - 0.5
			and screen.map3d.size.y >= ScreenScript.MAP_MIN.y - 0.5,
		"%s at 1100x700, floor %s" % [str(screen.map3d.size), str(ScreenScript.MAP_MIN)])
	screen.queue_free()


func _check(what: String, condition: bool, detail: String = "") -> void:
	if condition:
		_passed += 1
		print("  PASS  %s%s" % [what, "" if detail.is_empty() else " (%s)" % detail])
	else:
		_failed += 1
		print("  FAIL  %s%s" % [what, "" if detail.is_empty() else " (%s)" % detail])
