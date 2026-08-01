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
## one, RETAIL'S OWN 16:9 (the aspect the oracle capture is at, and the one the
## HUD is compared against - it was never in this list, so nothing held the
## layout to the frame it is judged in), a laptop, and an absurd one on each axis.
## RETAIL'S 16:9 GOES LAST rather than in aspect order, because the three sizes
## already indexed out of this list by the checks below (`SIZES[0]`, `[1]`, `[3]`)
## are transcribed into `wotr_capture_runner.gd` as the windows it photographs.
const SIZES := [
	Vector2(1860.0, 800.0), Vector2(2560.0, 1351.0), Vector2(1366.0, 768.0),
	Vector2(3840.0, 1600.0), Vector2(1280.0, 1024.0), Vector2(2560.0, 1440.0),
]

## RETAIL'S OWN SEAT NAMES, asserted BY VALUE rather than "the lookup returned
## something". Two of the three are the reason the binding is a TABLE and not a
## rule: `PlayerWild` reads "Goblins", not "Wild", and no rule recovers that.
const RETAIL_SEAT_NAMES := {
	"PlayerAngmar": "Angmar", "PlayerDwarves": "Dwarves", "PlayerWild": "Goblins",
}
## The one region whose display-name key retail spells two ways - the region ini
## asks for `LW:DisplayNameBrownLands`, `data/lotr.str` writes
## `LW:DisplayNameBrownlands` - and the text a player must end up reading.
const RETAIL_CASE_SPLIT_REGION := "The_Brown_Lands"
const RETAIL_CASE_SPLIT_NAME := "Old Brown Lands"
## A key whose case-fold lands on TWO authored spellings that disagree
## ("Arnor Archer Battalions" against "Dunedain Archer Battalions"). The resolver
## must refuse it rather than pick one.
const AMBIGUOUS_FOLD_KEY := "CONTROLBAR:LW_UNIT_ARNORARCHERHORDEPLURAL"

## Retail's own HUD slot translations out of `StrategicHUD`, in its authored
## 1024x768 space. Asserted here so a bundle that started composing the HUD
## somewhere else fails loudly rather than moving the palantir quietly.
const RETAIL_HUD_SLOTS := {
	"stats": Vector2(0.0, 0.0), "checklist": Vector2(512.0, 0.0),
	"endTurnButton": Vector2(1024.0, 0.0), "globe": Vector2(0.0, 512.0),
	"selectionDetails": Vector2(430.0, 590.0),
}

## LIVENESS, RAISED. Round 4 added five containment checks
## (`_check_no_island_content_ever_crosses_its_own_panel_edge`) after a blind
## review photographed the region card bleeding off the bottom of the display.
## Round 6 added three more, each one a particular frame violation.
##
## ROUND 7 REPLACES THOSE THREE WITH THE RULE THEY WERE ALL INSTANCES OF and adds
## four checks on top of it - see `_check_nothing_floats` for why containment is
## now stated as an invariant with a coverage guard rather than as a list, and
## `_check_shell_navigation_is_not_on_the_scoreboard` for the product decision a
## future refactor could otherwise undo without failing anything. Net +2 here; the
## structures-well rule needs a bound session and lives in
## `wotr_living_world_ui_runner` instead.
## ROUND 8 ADDS SEVEN, every one of them a property this round s own defects would
## have tripped: the four command capsules as ONE system rather than four call
## sites that each named a subset (which is how CANCEL came to wear two faces at
## once), the two palantir medallions as live controls on retail s own authored
## rectangles, the three view stops and the wrap that keeps the last of them from
## being a trap, and the derivation behind retail s half-turned expander arrows.
## ROUND 9 ADDS FOUR. Three of them are this round's own defects turned into
## properties - the command rail's RANK (one primary, one secondary, one ghost,
## and the three shares still summing to three so ranking cannot move the deck),
## the deck's left edge held to the tray's own visible gilt stile at every window
## size, and the two halves of the map stream's keep-out contract: every visible
## island handed in at the FULL stop, and nothing at all claimed once the HUD is
## down. The fourth is the rank check standing beside the size check it replaced -
## see `_check_the_command_capsules_are_one_button_system` for why "all four the
## same rectangle" was restated rather than deleted.
## AND ONE MORE, ADDED AFTER A MEASURED PERFORMANCE REGRESSION rather than after a
## review: the HUD's animation clock must invalidate its own thin layer and never
## the chrome pass. See `_check_the_animation_clock_repaints_only_its_own_layer`
## for the eighteen milliseconds a frame that bought, and for why the property is
## asserted structurally rather than as a millisecond budget.
const CHECKS_WITH_BUNDLE := 51
const CHECKS_WITHOUT_BUNDLE := 42

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
	_check_no_data_key_reaches_the_player()
	_check_the_hud_is_composed_at_retails_own_slots()
	_check_no_island_content_ever_crosses_its_own_panel_edge()

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


## THE LAYOUT, as PROPERTIES rather than pixels. The screen is retail's island
## layout now - a FULL-BLEED map with HUD islands floating over it - so the
## properties are: the map covers the whole frame at every size, no island
## ever enters the map's CENTRAL FIELD (the middle of Middle-earth, where the
## campaign happens - the successor of the old "no panel over the map" claim,
## restated for a layout where islands legitimately sit on the map's edges),
## nothing is laid out off the panel, and the map grows with the window.
func _check_the_layout_uses_the_window_at_every_size() -> void:
	var screen := ScreenScript.new()
	root.add_child(screen)
	screen.configure(null, [], "layout probe", [])
	var not_full_bleed: Array[String] = []
	var overlaps: Array[String] = []
	var off_panel: Array[String] = []
	for value in SIZES:
		var frame := value as Vector2
		screen.size = frame
		screen._relayout()
		var map_box := Rect2(screen.map3d.position, screen.map3d.size)
		if map_box.position != Vector2.ZERO or not map_box.size.is_equal_approx(frame):
			not_full_bleed.append("map %s in frame %s" % [str(map_box), str(frame)])
		# The central field, in the same fractions the screen states.
		var field: Rect2 = ScreenScript.CENTRAL_FIELD
		var central := Rect2(
			frame.x * field.position.x, frame.y * field.position.y,
			frame.x * field.size.x, frame.y * field.size.y)
		var side: Array[Control] = [
			screen.standings_label, screen.detail_label, screen.attack_button,
			screen.end_turn_button, screen.region_portrait_frame,
			screen.region_portrait_caption, screen.header_label, screen.turn_banner,
			screen.hint_label, screen.message_label,
		]
		# `legend_label` LEFT this list because the control was DELETED, not
		# because it stopped needing to obey: the colour-chip key and the camera
		# cheat-sheet that used to run along the bottom are gone from the player
		# surface (an adversarial blind review named both disqualifying). Every
		# island that still exists is still asserted here, at every window size.
		#
		# `back_button` LEFT IT IN ROUND 7 BECAUSE IT STOPPED BEING AN ISLAND. This
		# rule is about the surfaces that are on screen while the campaign is being
		# played, and MAIN MENU is now on the pause shell - a MODAL, which is the one
		# class of surface that has always been allowed over the central field
		# (`diagnostics_panel` has never been in this list either, for the same
		# stated reason: opening one is asking for it). It is not unasserted. It is
		# asserted harder, by `_check_nothing_floats` - which holds it inside the
		# shell card's inner field at every size - and by
		# `_check_shell_navigation_is_not_on_the_scoreboard`, which holds it OUT of
		# the seat panel it used to be framed inside.
		for control in side:
			var box := Rect2(control.position, control.size)
			if box.intersects(central):
				overlaps.append("%s in the central field at %dx%d (%s vs %s)" % [
					control.name, int(frame.x), int(frame.y), str(box), str(central)])
			if box.end.x > frame.x + 0.5 or box.end.y > frame.y + 0.5 \
					or box.position.x < -0.5 or box.position.y < -0.5:
				off_panel.append("%s off the panel at %dx%d (%s)" % [
					control.name, int(frame.x), int(frame.y), str(box)])
	_check("the_map_is_full_bleed_at_every_size",
		not_full_bleed.is_empty(), ", ".join(not_full_bleed))
	_check("no_hud_island_ever_enters_the_maps_central_field",
		overlaps.is_empty(), ", ".join(overlaps))
	_check("nothing_is_ever_laid_out_off_the_panel",
		off_panel.is_empty(), ", ".join(off_panel))
	# THE POINT OF THE WHOLE EXERCISE: a bigger window means a bigger map. On the
	# owner's own 2560x1351 window it must be substantially bigger than at the
	# authored size, not the same. With the map full-bleed this follows from the
	# frame arithmetic - which is exactly why it stays asserted: a future layout
	# that quietly re-boxed the map would fail here first.
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
	# THE DIAGNOSTICS OVERLAY. The conversion-facing lines (provenance, report,
	# named gaps) moved off the player-facing surface onto a toggled panel, and
	# the move must not have COST anything: the panel starts hidden, opens and
	# closes on the toggle, and the report label keeps being refreshed while
	# the panel is hidden - a capability behind a toggle is only kept if it is
	# still live when nobody is looking at it.
	_check("the_diagnostics_overlay_is_hidden_by_default",
		screen.diagnostics_panel != null and not screen.diagnostics_panel.visible)
	screen.toggle_diagnostics()
	var opened: bool = screen.diagnostics_panel.visible
	screen.toggle_diagnostics(false)
	_check("the_diagnostics_overlay_opens_and_closes_on_the_toggle",
		opened and not screen.diagnostics_panel.visible)
	_check("the_conversion_report_is_still_refreshed_while_the_overlay_is_hidden",
		not screen.map_mode_label.text.is_empty(),
		screen.map_mode_label.text.substr(0, 60))
	screen.queue_free()


## NO DATA KEY REACHES THE PLAYER.
##
## A blind review of this screen against retail's own capture called every one of
## these individually disqualifying: "PLAYERANGMAR" shouted in the turn band,
## "PlayerDwarves" in the seat table, "Amon Sul (Amon_Sul)" and "The Barrow-downs
## (Barrow_Downs)" in the territory list. The defence is not "we removed them" -
## it is that names come from retail's own string tables through ONE resolver
## which records every miss, and that a miss produces a cleaned id rather than a
## key. All four claims are asserted here.
func _check_no_data_key_reaches_the_player() -> void:
	var names = load("res://src/wotr/wotr_display_names.gd").new()
	var strings = load("res://src/wotr/wotr_strings.gd").new()
	var located: Dictionary = strings.locate_and_load(_roots())
	names.locate_and_load(_roots())
	names.bind_living_world(strings if bool(located.get("ok", false)) else null)

	# 1. THE SEATS, by value. A rule cannot produce these; a table can.
	var wrong: Array[String] = []
	for template in RETAIL_SEAT_NAMES.keys():
		var got: String = names.seat_label(String(template))
		if got != String(RETAIL_SEAT_NAMES[template]):
			wrong.append("%s -> %s, wanted %s" % [
				String(template), got, String(RETAIL_SEAT_NAMES[template])])
	_check("every_seat_is_named_in_retails_own_english_not_by_its_template_id",
		wrong.is_empty() or names.strings == null,
		"%s%s" % [", ".join(wrong),
			" (no setup string bundle, so the fallback path is the one under test)"
			if names.strings == null else ""])

	# 2. A MISS IS A CLEANED ID, never the raw key and never a spelled-out name.
	_check("a_name_that_does_not_resolve_becomes_a_cleaned_id_rather_than_a_key",
		names.seat_label("PlayerNoSuchFaction_Test") == "PlayerNoSuchFaction Test"
			and names.gaps.has("PlayerNoSuchFaction_Test")
			and load("res://src/wotr/wotr_display_names.gd").clean_id("Barrow_Downs") == "Barrow Downs")

	# 3. THE ONE KEY RETAIL SPELLS TWO WAYS. Retail's own string manager folds
	#    case; a case-preserving converter loses "Old Brown Lands" entirely.
	var folded: String = names.living_world_text("LW:DisplayNameBrownLands", RETAIL_CASE_SPLIT_REGION)
	_check("a_display_name_key_retail_spells_in_two_cases_still_resolves",
		folded == RETAIL_CASE_SPLIT_NAME or not bool(located.get("ok", false)),
		"%s -> %s" % [RETAIL_CASE_SPLIT_REGION, folded])

	# 4. AND THE FOLD IS REFUSED WHERE IT IS AMBIGUOUS. Two authored spellings of
	#    one folded key disagree in this very table; picking one would be a coin
	#    toss dressed as a lookup.
	var ambiguous: String = names.living_world_text(AMBIGUOUS_FOLD_KEY, "ambiguity probe")
	_check("an_ambiguous_case_fold_is_refused_and_recorded_rather_than_guessed",
		ambiguous.is_empty() and (names.gaps.has("ambiguity probe") or not bool(located.get("ok", false))),
		"%s -> %s" % [AMBIGUOUS_FOLD_KEY, ambiguous])

	# 5. EVERY PLAYABLE REGION RESOLVES. The screen may legitimately stand in for
	#    a name, but it may not do so silently - so a miss must be BOTH shown as a
	#    cleaned id AND present in the gap register.
	var unresolved: Array[String] = []
	var unrecorded: Array[String] = []
	var world = load("res://src/wotr/wotr_world.gd").new()
	var found: Dictionary = load("res://src/wotr/wotr_session.gd").locate_document([])
	if bool(found.get("ok", false)) and world.load_from_dict(found["document"] as Dictionary, ""):
		for region_id in world.region_ids:
			var id := String(region_id)
			var key := String(world.region(id).get("display_name", ""))
			var resolved: String = names.living_world_text(key, id)
			if resolved.is_empty():
				unresolved.append(id)
				if not names.gaps.has(id):
					unrecorded.append(id)
	_check("every_region_name_either_resolves_to_retails_text_or_is_a_recorded_gap",
		unrecorded.is_empty(),
		"%d unresolved, %d of them unrecorded: %s" % [
			unresolved.size(), unrecorded.size(),
			", ".join(unrecorded.slice(0, mini(unrecorded.size(), 6)))])


## THE HUD IS COMPOSED AT RETAIL'S OWN SLOTS, and its islands stay on the panel.
##
## Every island's position comes from a `StrategicHUD` named instance rather than
## from a number in this project's layout code, so the first thing asserted is
## that those translations are what they claim to be. The rest is the property
## the composition has to keep at any window: nothing off the panel, and the
## right-hand islands actually anchored to the right edge rather than stranded in
## the middle of a widescreen frame.
func _check_the_hud_is_composed_at_retails_own_slots() -> void:
	var screen := ScreenScript.new()
	root.add_child(screen)
	screen.configure(null, [], "island probe", [])
	var bound: bool = screen.strategic != null
	var slots: Dictionary = {}
	if bound:
		for row_value in screen.strategic.named_instances("StrategicHUD"):
			var row := row_value as Dictionary
			var translation: Array = row.get("translation", []) as Array
			if RETAIL_HUD_SLOTS.has(String(row.get("name", ""))) and translation.size() == 2:
				slots[String(row.get("name", ""))] = Vector2(
					float(translation[0]), float(translation[1]))
	_check("the_hud_slot_translations_are_retails_own",
		not bound or slots == RETAIL_HUD_SLOTS,
		"%s vs %s" % [str(slots), str(RETAIL_HUD_SLOTS)] if bound
			else "no strategic bundle: %s" % screen.strategic_reason.split(".")[0])

	var off_panel: Array[String] = []
	var not_anchored: Array[String] = []
	for value in SIZES:
		var frame := value as Vector2
		screen.size = frame
		screen._relayout()
		if not bound:
			continue
		for slot in screen._islands.keys():
			var box: Rect2 = (screen._islands[slot] as Dictionary)["rect"] as Rect2
			# The checklist's authored art runs above y=0 by design (its rails);
			# what may never happen is running off an EDGE the layout anchors to.
			if box.position.x < -0.5 or box.end.x > frame.x + 0.5 or box.end.y > frame.y + 0.5:
				off_panel.append("%s %s in %s" % [String(slot), str(box), str(frame)])
		for slot in ["endTurnButton", "selectionDetails"]:
			if not screen._islands.has(slot):
				continue
			var right: Rect2 = (screen._islands[slot] as Dictionary)["rect"] as Rect2
			if right.end.x < frame.x - 8.0:
				not_anchored.append("%s ends at %.0f in a %.0f frame" % [
					slot, right.end.x, frame.x])
	_check("no_retail_island_is_ever_laid_out_off_the_panel",
		off_panel.is_empty(), ", ".join(off_panel))
	_check("the_right_hand_islands_stay_anchored_to_the_right_edge_at_every_width",
		not_anchored.is_empty(), ", ".join(not_anchored))

	# THE RATCHET. `x.size = v` then `x.custom_minimum_size = x.size` reads back
	# the size Godot CLAMPED up to the control's minimum and stores it as the new
	# floor, so a layout that shrinks can never shrink again. It shipped here: the
	# details tray reached 1,202 pixels inside a 737-pixel frame and pushed
	# AUTO-RESOLVE off the screen. Big window first, then small, is the exact
	# order that exposes it.
	screen.size = SIZES[3] as Vector2
	screen._relayout()
	var wide := screen.detail_label.size.x
	screen.size = SIZES[0] as Vector2
	screen._relayout()
	var narrow := screen.detail_label.size.x
	# THE PROPERTY IS THE RATIO, WHICH IS STRICTLY STRONGER than the bound this
	# check used to carry. It read `narrow <= width * 0.5`, which was not a
	# statement about ratcheting at all - it was the old right-hand SIDEBAR's
	# width baked in as a constant, and it started failing the moment the tray
	# became retail's own full-width command bar (retail's bar is ~58% of the
	# frame). What ratcheting actually looks like is a control that comes back
	# WIDER than its share of the smaller frame, so the fraction of the frame is
	# what is asserted, and it is asserted to hold at BOTH sizes rather than to
	# sit under a ceiling at one.
	var wide_share := wide / (SIZES[3] as Vector2).x
	var narrow_share := narrow / screen.size.x
	_check("a_control_shrinks_again_after_a_larger_window_rather_than_ratcheting",
		narrow < wide and absf(narrow_share - wide_share) <= 0.02,
		"%.0f of %.0f (%.3f) then %.0f of %.0f (%.3f)" % [
			wide, (SIZES[3] as Vector2).x, wide_share, narrow, screen.size.x, narrow_share])
	screen.queue_free()


## NO ISLAND'S CONTENT EVER CROSSES ITS OWN PANEL EDGE.
##
## This is the check the round-3 capture needed and did not have. A blind review
## photographed the region card running PAST THE BOTTOM OF THE DISPLAY and
## stopping mid-list on a trailing comma, with a scrollbar sliver hanging outside
## the gold frame line, and called that one defect a 100%-confidence tell on its
## own. Every existing layout check passed on that frame, because all of them ask
## where a CONTROL is and none of them asked whether the control's CONTENT stays
## inside the panel drawn around it.
##
## Three properties, at every window size in `SIZES`:
##
##   1. The tray's own text controls sit INSIDE the tray island's flattened
##      rectangle. A card laid out past retail's own frame is off the panel even
##      when it is still on the screen.
##   2. The card CLIPS and does not SCROLL. `scroll_active` is what put a
##      scrollbar on the frame line and let the last line sit half off; clipping
##      is what makes an over-long card a short card instead of a bleeding one.
##   3. Nothing in the bar reaches the frame's own bottom edge.
func _check_no_island_content_ever_crosses_its_own_panel_edge() -> void:
	var screen := ScreenScript.new()
	root.add_child(screen)
	screen.configure(null, [], "containment probe", [])
	var outside: Array[String] = []
	var past_bottom: Array[String] = []
	for value in SIZES:
		var frame := value as Vector2
		screen.size = frame
		screen._relayout()
		var tray := Rect2()
		if screen._islands.has("selectionDetails"):
			tray = (screen._islands["selectionDetails"] as Dictionary)["rect"] as Rect2
		var inside: Array[Control] = [screen.detail_label, screen.tray_ribbon]
		for key in screen._tab_buttons.keys():
			inside.append(screen._tab_buttons[key] as Control)
		for control in inside:
			var box := Rect2(control.position, control.size)
			if tray.size.x > 0.0 and not tray.grow(1.0).encloses(box):
				outside.append("%s %s outside the tray %s at %dx%d" % [
					control.name, str(box), str(tray), int(frame.x), int(frame.y)])
			if box.end.y > frame.y + 0.5:
				past_bottom.append("%s ends at %.0f in a %.0f frame" % [
					control.name, box.end.y, frame.y])
	_check("no_bar_content_is_ever_laid_out_outside_the_trays_own_frame",
		outside.is_empty(), ", ".join(outside))
	_check("no_bar_content_ever_reaches_past_the_bottom_of_the_frame",
		past_bottom.is_empty(), ", ".join(past_bottom))
	_check("the_region_card_clips_its_content_instead_of_scrolling_it",
		screen.detail_label.clip_contents and not screen.detail_label.scroll_active
			and not screen.detail_label.fit_content,
		"clip=%s scroll=%s fit=%s" % [
			str(screen.detail_label.clip_contents),
			str(screen.detail_label.scroll_active),
			str(screen.detail_label.fit_content)])
	# AND THE CARD IS FITTED BEFORE IT IS SET, so an over-long card ends on a
	# whole line with an ellipsis rather than on whatever the clip happened to cut.
	var long_card: Array[String] = []
	for index in range(60):
		long_card.append("line %d" % index)
	var fitted: Array[String] = screen._fit_card_lines(long_card)
	# THE ELLIPSIS IS THE TYPOGRAPHIC GLYPH, NOT THREE FULL STOPS, and this check
	# now holds BOTH halves of that. `...` is what a string join looks like when it
	# runs out of room - a blind review read `Dark Iron Forge, ...` on the status
	# ribbon as exactly that and it was right - so the card is asserted to end on
	# `RIBBON_ELLIPSIS` AND to contain no run of full stops anywhere.
	var last_line := String(fitted[fitted.size() - 1])
	_check("an_over_long_card_is_trimmed_to_the_well_and_ends_on_an_ellipsis",
		fitted.size() < long_card.size()
			and last_line.ends_with(ScreenScript.RIBBON_ELLIPSIS)
			and not last_line.contains("..."),
		"%d line(s) -> %d, last %s" % [
			long_card.size(), fitted.size(), last_line])
	# THE BAR IS A FLOOR, not an island. Retail's runs from the palantir to the
	# right edge; the reason this check exists is that this screen's ran to ~51%
	# of a 16:9 frame and left 87% of the bottom edge bare map.
	screen.size = Vector2(2560.0, 1440.0)
	screen._relayout()
	var bar := Rect2()
	if screen._islands.has("selectionDetails"):
		bar = (screen._islands["selectionDetails"] as Dictionary)["rect"] as Rect2
	_check("the_command_bar_reaches_the_right_edge_and_covers_most_of_the_bottom",
		bar.size.x <= 0.0 or (bar.end.x >= 2560.0 - 12.0 and bar.size.x >= 2560.0 * 0.66),
		"%s in a 2560x1440 frame%s" % [str(bar),
			"" if bar.size.x > 0.0 else " (no strategic bundle: %s)"
				% screen.strategic_reason.split(".")[0]])
	_check_nothing_floats(screen)
	_check_shell_navigation_is_not_on_the_scoreboard(screen)
	_check_the_command_capsules_are_one_button_system(screen)
	_check_the_medallions_are_controls_on_retails_own_rectangles(screen)
	_check_the_view_has_three_stops_and_never_traps_the_player(screen)
	_check_the_expander_arrows_are_turned_through_retails_own_half_turn(screen)
	_check_the_deck_ends_on_the_trays_own_stile(screen)
	_check_the_running_screen_hands_the_map_its_keep_out(screen)
	_check_the_animation_clock_repaints_only_its_own_layer(screen)
	screen.queue_free()


## ------------------------------------------------------------------------------
## THE ANIMATION CLOCK REPAINTS ONE THIN LAYER, AND NOT THE WHOLE SCREEN
## ------------------------------------------------------------------------------
##
## THE REGRESSION THIS PINS, in numbers, because it is invisible in every picture:
## the round that gave this HUD a pulse also gave it a `_process` that called
## `chrome_layer.queue_redraw()` sixty times a second. `chrome_layer` is where
## retail's five flattened APT islands and every drawn plate, roster row and
## engraved caption on the screen are painted. The frame budget runner measured
## idle going from 1.00 ms to 18.33 ms against a 4 ms budget and pan from 7.78 to
## 26.45 against 12, and a companion runner isolated it exactly - `idle-hud-hidden`
## at 6.90 ms against `idle` at 24.80 ms on the same map, an 18 ms delta that
## appeared and disappeared with this screen.
##
## The animation is a handful of outlines. It cost eighteen milliseconds because it
## was invalidating a surface eighteen milliseconds wide.
##
## WHY THIS IS A STRUCTURAL CHECK RATHER THAN A TIMED ONE. A millisecond budget
## asserted from this runner would be asserted headless, on whatever machine and
## whatever display sync the runner happens to meet - and the frame budget runner's
## own idle figure landed on 16.61 ms, which is 1000/60 to two decimal places and
## therefore cannot be distinguished from a vsync-locked frame without turning
## vsync off first. A timing check that can pass or fail on display settings is not
## a check. THE CAUSE, on the other hand, is exact and machine-independent: the
## clock must name the surfaces it invalidates, and `chrome_layer` must not be one
## of them. That property is what actually broke, and it is checkable anywhere.
func _check_the_animation_clock_repaints_only_its_own_layer(screen) -> void:
	var wrong: Array[String] = []
	var animated: Array = screen.animated_layers()
	if animated.is_empty():
		wrong.append("the clock names no layer at all, so nothing can be held to it")
	for value in animated:
		var layer := value as CanvasItem
		if layer == screen.chrome_layer:
			wrong.append("the clock invalidates the chrome pass, which repaints "
				+ "retail's five islands and every drawn caption on the screen")
		elif layer != screen.pulse_layer:
			wrong.append("the clock invalidates %s, which is not the pulse layer" % [
				String(layer.name)])
	# AND THE CLOCK IS OFF WHEN THERE IS NOTHING TO SAY. A pulse that runs on an
	# opponent's turn or over a bare map is a per-frame repaint bought for a light
	# nobody can see, which is the same defect at a smaller size.
	screen.set_hud_hidden(true)
	if screen._pulse_wanted:
		wrong.append("the clock still runs with the HUD down")
	screen.set_hud_hidden(false)
	_check("the_animation_clock_repaints_only_the_pulse_layer",
		wrong.is_empty(), "; ".join(wrong) if not wrong.is_empty()
			else "%d animated layer(s), none of them the chrome pass" % animated.size())


## ------------------------------------------------------------------------------
## THE DECK ENDS WHERE THE TRAY ENDS
## ------------------------------------------------------------------------------
##
## An adversarial art-direction review on the command deck: its "left terminus is
## an arbitrary diagonal cut that aligns to nothing, and ... reads as a separate,
## later-added layer."
##
## It aligned to `TRAY_FIELD`, the tray panel's own quad - which is not what a
## player can see, because 62 authored pixels of that quad run behind the palantir.
## `ScreenScript.TRAY_STILE` records the flattening that finds the tray's actual
## visible left-hand gilt member and why -73.5 is the number. This holds the deck
## to it, at every window size, so the alignment cannot quietly come apart the next
## time either rectangle is touched - which is exactly what happened twice before.
##
## THE TOLERANCE IS A PIXEL, not a percentage: these are two edges that must be the
## SAME LINE, and "close" is the failure being asserted against.
func _check_the_deck_ends_on_the_trays_own_stile(screen) -> void:
	var adrift: Array[String] = []
	var measured := 0
	for value in SIZES:
		var frame := value as Vector2
		screen.size = frame
		screen._relayout()
		if not screen._islands.has("selectionDetails"):
			continue
		var stile: Rect2 = screen._island_rect("selectionDetails", ScreenScript.TRAY_STILE)
		if stile.size.x <= 0.0:
			continue
		var deck: Rect2 = screen.command_deck_rect()
		if deck.size.x <= 0.0:
			continue
		measured += 1
		if absf(deck.position.x - stile.position.x) > 1.0:
			adrift.append("deck starts at %.1f, the tray's stile at %.1f at %dx%d" % [
				deck.position.x, stile.position.x, int(frame.x), int(frame.y)])
	_check("the_command_decks_left_edge_is_the_trays_own_visible_stile",
		adrift.is_empty(), "; ".join(adrift) if not adrift.is_empty()
			else "%d window size(s) held to one vertical line" % measured)


## ------------------------------------------------------------------------------
## THE RUNNING SCREEN HANDS THE MAP ITS KEEP-OUT
## ------------------------------------------------------------------------------
##
## The map stream's contract: `map3d.set_hud_keep_out(rects)` slides the build ring
## clear of anything the HUD says it occupies, and the map view "does not guess
## where the HUD is ... the list is empty until somebody hands it in."
##
## `wotr_map3d_runner` proves the escape ARITHMETIC by driving `set_hud_keep_out`
## from the test itself. It cannot prove the running screen ever calls it, and that
## half is the half that actually failed: the build ring opened over the Treasury
## plate with two icons off the screen edge in this stream's own capture set,
## because nobody was calling it. So this is the other side of that contract.
##
## THREE PROPERTIES, and the second and third are the ones a future edit would
## break silently:
##
##   * at the FULL stop the map has been handed a rectangle for every island that
##     is on the glass;
##   * at the bare-map stop it has been handed NOTHING - the ring is supposed to
##     reclaim the whole board when the chrome gets out of the way, which is the
##     entire point of the stops;
##   * the rectangles are the ISLANDS' OWN, not a second copy of the arithmetic -
##     each one is checked against `_islands`, so a keep-out that drifts away from
##     the chrome it is protecting is a failure rather than a near miss.
func _check_the_running_screen_hands_the_map_its_keep_out(screen) -> void:
	screen.size = Vector2(2560.0, 1440.0)
	screen.set_view_mode(ScreenScript.VIEW_FULL)
	screen._relayout()
	var handed: Array = screen.map3d.hud_keep_out
	var wrong: Array[String] = []
	var shown := 0
	for entry_value in ScreenScript.STRATEGIC_ISLANDS:
		var slot := String((entry_value as Dictionary)["slot"])
		if not screen._islands.has(slot) or not screen.island_is_shown(slot):
			continue
		shown += 1
		var rect := (screen._islands[slot] as Dictionary)["rect"] as Rect2
		var found := false
		for handed_value in handed:
			if (handed_value as Rect2).is_equal_approx(rect):
				found = true
				break
		if not found:
			wrong.append("%s is on the glass at %s and was not handed to the map" % [
				slot, str(rect)])
	if shown > 0 and handed.is_empty():
		wrong.append("%d island(s) are on the glass and the map was handed nothing" % shown)
	_check("the_running_screen_tells_the_map_where_every_visible_hud_island_is",
		wrong.is_empty(), "; ".join(wrong) if not wrong.is_empty()
			else "%d island(s) on the glass, %d rectangle(s) handed in" % [shown, handed.size()])
	# AND THE BOARD COMES BACK when the chrome goes away.
	screen.set_hud_hidden(true)
	var cleared: Array = screen.map3d.hud_keep_out
	screen.set_hud_hidden(false)
	_check("the_build_ring_gets_the_whole_board_back_when_the_hud_goes_down",
		cleared.is_empty(), "%d rectangle(s) still claimed with the HUD down" % cleared.size())


## ------------------------------------------------------------------------------
## NOTHING FLOATS.
## ------------------------------------------------------------------------------
##
## THIS IS THE RULE THE WHOLE ROUND TURNS ON, and it is stated here as one property
## rather than as a growing list of the particular violations somebody happened to
## photograph.
##
## A blind adversarial review, shown this screen and retail's side by side without
## being told which was which, initially picked THIS ONE as the shipped commercial
## product and held that reading for its whole first pass - on copy, on labelled
## counters, on information architecture. What resolved it against us was one
## audit and one audit only, and the review said so in as many words: "switching
## from what the UI says to how the UI is assembled. Once I audited containment -
## does anything float, does anything overlap, does anything sit outside its frame
## - the two builds separated instantly and did not converge again." Its summary of
## retail was five words: "P's rule is absolute: nothing floats."
##
## So containment stops being a checklist and becomes an INVARIANT. Two halves,
## and they only work together:
##
##   1. EVERY DRAWN ELEMENT IS INSIDE THE FRAME IT BELONGS TO, at every window
##      size in `SIZES`. The pairing is a table below, and each frame is read from
##      the SAME function the drawing reads it from (`tray_tab_cell`,
##      `command_deck_rect`, `_standings_card_rect`, `_pause_card_rect`, the
##      island rectangles) - never recomputed here, because a containment property
##      computed twice is one that can be true in the runner and false on the
##      glass. That is exactly how round 6 clamped the painted tab plate and left
##      the tab BUTTON at the raw pitch, and the button is what a blind review then
##      photographed hanging past the gold border.
##
##   2. EVERY CONTROL IS IN THAT TABLE. A rule that only covers the controls
##      somebody remembered is a rule that a new floating control walks straight
##      through. So the second check walks the screen's own children and fails on
##      any visible one that is neither given a frame above nor named in
##      `UNFRAMED_BY_CONSTRUCTION` with the reason it needs none. Adding a control
##      to this screen now MEANS deciding what frames it, and the build says so.
##
## The three round-6 checks (the lit tab plate off the tray's right edge, MAIN MENU
## overhanging the seat panel, the command deck lying across the tab strip) are all
## instances of half 1 and are subsumed by it. The deck's STACKING against the tab
## strip is not a containment property - the deck is deliberately welded INTO the
## tray - so it keeps its own check below.
##
## WHAT THE RULE CANNOT SAY WITHOUT RETAIL'S ART, said plainly rather than hidden:
## with no strategic-UI bundle converted there are no retail frames to be inside,
## so those pairs fall back to the window and the check degrades to "on the panel"
## (which `nothing_is_ever_laid_out_off_the_panel` already holds). The number of
## pairs measured against a REAL frame is reported in the check's own detail, so a
## run that proved nothing cannot look like a run that proved something.
##
## The controls that are not inside a frame, and why each one needs none. This is
## the whitelist half 2 checks against, and every entry is a decision.
const UNFRAMED_BY_CONSTRUCTION := {
	# The map is the ground everything else sits on; it IS the frame.
	"MapView": "the full-bleed map",
	"Map3D": "the full-bleed map",
	# The chrome pass is where the frames themselves are painted.
	"Chrome": "the layer the frames are drawn on",
	# The animation layer is the chrome pass split in two for cost, and covers the
	# window for the same reason the chrome pass does.
	"Pulse": "the layer the animated highlights are drawn on",
	# Modal overlays are frames in their own right and cover the whole window on
	# purpose - the diagnostics panel, the battle report, the pause shell. They are
	# the one class allowed over the map's central field, because opening one is
	# asking for it.
	"Diagnostics": "a modal overlay, and a frame in its own right",
	"PauseShell": "a modal overlay, and the frame the shell capsules sit in",
	"BattleReportBackdrop": "a modal overlay",
	"BattleReport": "inside the battle report's own backdrop",
	"BattleReportClose": "inside the battle report's own backdrop",
	# The masthead and the conversion report are not on the player's surface at
	# all: `Heading` is never shown and `Status` lives inside the diagnostics panel.
	"Heading": "not shown on the player surface",
	"Status": "inside the diagnostics panel",
	"MapMode": "inside the diagnostics panel",
}


func _check_nothing_floats(screen) -> void:
	var loose: Array[String] = []
	var deck_overlap: Array[String] = []
	var framed := 0
	var measured := 0
	var unframed: Array[String] = []
	for value in SIZES:
		var frame := value as Vector2
		screen.size = frame
		screen._relayout()
		var pairs := _containment_pairs(screen, frame)
		for pair_value in pairs:
			var pair := pair_value as Array
			var box := pair[1] as Rect2
			var host := pair[2] as Rect2
			framed += 1
			# A frame with no size is an island retail's art did not supply; the pair
			# falls back to the window and is counted separately rather than skipped.
			if host.size.x > 0.0 and host.size.y > 0.0:
				measured += 1
			else:
				host = Rect2(Vector2.ZERO, frame)
			if not host.grow(1.0).encloses(box):
				loose.append("%s %s outside %s at %dx%d" % [
					String(pair[0]), str(box), str(host), int(frame.x), int(frame.y)])
		# THE DECK'S STACKING against the tab strip's own top edge. Not containment:
		# the deck runs INTO the tray on purpose, and what it may not do is reach the
		# tray's first row of content.
		if screen._islands.has("selectionDetails"):
			var island := screen._islands["selectionDetails"] as Dictionary
			var origin := island["origin"] as Vector2
			var scale := island["scale"] as Vector2
			var strip_top := origin.y + ScreenScript.TRAY_TAB_STRIP.position.y * scale.y
			var deck_bottom := 0.0
			if screen.attack_button != null:
				deck_bottom = screen.attack_button.position.y + screen.attack_button.size.y
			if deck_bottom > strip_top + 0.5:
				deck_overlap.append("deck reaches %.0f, strip starts %.0f at %dx%d" % [
					deck_bottom, strip_top, int(frame.x), int(frame.y)])
	# HALF TWO: every control is accounted for. Run once, at the last size, because
	# the roster of controls does not change with the window.
	var named: Dictionary = {}
	for pair_value in _containment_pairs(screen, screen.size):
		named[String((pair_value as Array)[0])] = true
	for child in screen.get_children():
		var control := child as Control
		if control == null or not control.visible:
			continue
		var control_name := String(control.name)
		if named.has(control_name) or UNFRAMED_BY_CONSTRUCTION.has(control_name):
			continue
		unframed.append(control_name)
	_check("nothing_this_screen_draws_renders_outside_the_frame_it_belongs_to",
		loose.is_empty(), "%d pair(s) held, %d of them against a real frame%s" % [
			framed, measured, "" if loose.is_empty() else "; " + "; ".join(loose)])
	_check("every_control_on_this_screen_is_either_framed_or_named_as_needing_no_frame",
		unframed.is_empty(),
		"unaccounted: %s" % ", ".join(unframed) if not unframed.is_empty()
			else "%d framed, %d exempt by construction" % [
				named.size(), UNFRAMED_BY_CONSTRUCTION.size()])
	_check("the_command_deck_stops_above_the_trays_tab_strip",
		deck_overlap.is_empty(), "; ".join(deck_overlap))


## `[what, element, frame]` for every drawn element on the screen, at the layout
## the screen is currently in. Every frame is read from the screen's own
## definition of it; nothing here recomputes one.
func _containment_pairs(screen, frame: Vector2) -> Array:
	var pairs: Array = []
	var stats: Rect2 = screen._island_rect("stats", ScreenScript.STATS_FIELD)
	# THE STATS ISLAND'S WHOLE RECTANGLE, as well as the black field inside it that
	# `STATS_FIELD` names. Retail's `Expand` button sits BESIDE that field rather
	# than in it, so the field is the wrong frame to hold it against.
	var stats_island := Rect2()
	if screen._islands.has("stats"):
		stats_island = (screen._islands["stats"] as Dictionary)["rect"] as Rect2
	var checklist := Rect2()
	var tray := Rect2()
	var globe := Rect2()
	var end_turn := Rect2()
	if screen._islands.has("checklist"):
		checklist = (screen._islands["checklist"] as Dictionary)["rect"] as Rect2
	if screen._islands.has("selectionDetails"):
		tray = (screen._islands["selectionDetails"] as Dictionary)["rect"] as Rect2
	if screen._islands.has("globe"):
		globe = (screen._islands["globe"] as Dictionary)["rect"] as Rect2
	if screen._islands.has("endTurnButton"):
		end_turn = (screen._islands["endTurnButton"] as Dictionary)["rect"] as Rect2
	for entry in [
		["Header", screen.header_label, stats],
		["TurnBanner", screen.turn_banner, checklist],
		["PhaseBanner", screen.phase_banner, checklist],
		["Hint", screen.hint_label, checklist],
		["Message", screen.message_label, checklist],
		["EndTurn", screen.end_turn_button, end_turn],
		["RegionPortraitFrame", screen.region_portrait_frame, globe],
		["RegionPortraitCaption", screen.region_portrait_caption, globe],
		# The command ring's hover target. It is a transparent Control whose whole
		# job is to carry the "this ring is a readout, not a build menu" tooltip, and
		# a tooltip target that has drifted off the icons it is about is a tooltip
		# that fires over the wrong thing - so it is held inside the palantir island
		# exactly like the portrait and its caption are.
		["CommandDialAffordance", screen.dial_affordance, globe],
		# AND THE TWO MEDALLIONS, which are REAL CONTROLS this round rather than a
		# hover band over an ornament. The containment rule is if anything stronger
		# now: a button whose hit area has drifted off the disc it is painted under
		# takes a click for a control the player cannot see. Their rectangles are
		# derived from retail's own `namedInstances` depths (`_medallion_rect`), so
		# this holds that derivation to the island at every window size.
		["PalantirKey", screen.medallion_key, globe],
		["PalantirBanner", screen.medallion_banner, globe],
		# RETAIL'S TWO EXPANDER BUTTONS, held to the islands whose art draws them.
		# Their rectangles are derived from retail's own instance depths
		# (`_expander_rect`), so this is the check that the derivation lands on the
		# art rather than beside it - a red arrow the player can see and a hit area
		# somewhere else is the worst of both.
		["StatsExpander", screen.stats_expander, stats_island],
		["ObjectivesExpander", screen.objectives_expander, checklist],
		["Detail", screen.detail_label, tray],
		["TrayRibbon", screen.tray_ribbon, tray],
	]:
		var control := (entry as Array)[1] as Control
		if control == null or not control.visible:
			continue
		pairs.append([String((entry as Array)[0]),
			Rect2(control.position, control.size), (entry as Array)[2] as Rect2])
	# THE TAB CELLS. The button AND the plate the chrome pass lights under it are
	# the same rectangle by construction (`tray_tab_cell`), so holding the button
	# holds both - which is the point of there being one definition.
	for entry_value in ScreenScript.TRAY_TABS:
		var entry := entry_value as Dictionary
		var key := String(entry["key"])
		if not screen._tab_buttons.has(key):
			continue
		var tab := screen._tab_buttons[key] as Button
		pairs.append(["Tab%s" % key.capitalize(),
			Rect2(tab.position, tab.size),
			screen._island_rect("selectionDetails", ScreenScript.tray_tab_cell(entry))])
	# THE THREE POOLS OF BUILD CONTROLS THIS ROUND ADDED, each inside the frame its
	# own drawing is inside. This is the "extend the rule to anything new" half of
	# `_check_nothing_floats`: a build button whose hit area has drifted off the
	# icon it is painted under takes a click for a control the player cannot see,
	# and the whole point of this round is that those clicks now SPEND TREASURE.
	#
	# The roster rows are held against the tray's own card control rather than
	# against the tray island, because that is the rectangle `structure_roster_rects`
	# solves inside and the one `_place_detail_well` already fits between retail's
	# card rail and retail's scroll. Same for the foundation cards, which live on
	# retail's authored card slots inside the tray. The palantir's command wells are
	# held to the palantir island for the same reason its medallions are.
	var detail_well := Rect2()
	if screen.detail_label != null:
		detail_well = Rect2(screen.detail_label.position, screen.detail_label.size)
	for entry_value in [
		["BuildRow", screen._build_row_buttons, detail_well],
		["PlotCard", screen._plot_card_buttons, tray],
		["DialWell", screen._dial_buttons, globe],
	]:
		var entry := entry_value as Array
		var pool := entry[1] as Array
		for index in range(pool.size()):
			var pooled := pool[index] as Button
			if pooled == null or not pooled.visible:
				continue
			pairs.append(["%s%d" % [String(entry[0]), index],
				Rect2(pooled.position, pooled.size), entry[2] as Rect2])
	# THE COMMAND CAPSULES, inside the deck they are seated on.
	var deck: Rect2 = screen.command_deck_rect()
	for capsule in [screen.attack_button, screen.cancel_button, screen.auto_resolve_button]:
		var button := capsule as Button
		if button == null or not button.visible:
			continue
		pairs.append([String(button.name), Rect2(button.position, button.size), deck])
	# THE SEAT PLAQUES, inside the seat panel's INNER field - clear of the bevel,
	# the fillet and the corner scrolls `draw_card` turns every corner in. 2.9
	# weights is the frame's own construction: an elbow 1.6 weights in, an arm up to
	# nine weights long, and a curl of nearly one more weight at its end.
	if screen.standings_label != null and screen.standings_label.visible:
		pairs.append(["Standings",
			Rect2(screen.standings_label.position, screen.standings_label.size),
			_inner_field(screen._standings_card_rect())])
	# THE PAUSE SHELL'S CAPSULES, inside the shell card's inner field. They are
	# hidden until ESC asks for them, so the pair is added from the CARD rather than
	# from visibility - a shell that only contained its buttons while it was open
	# would be a shell nobody had laid out.
	var shell_field := _inner_field(screen._pause_card_rect())
	for capsule in [screen.pause_resume, screen.back_button]:
		var button := capsule as Button
		if button == null:
			continue
		pairs.append([String(button.name), Rect2(button.position, button.size), shell_field])
	# THE UNPLACED BLOCK, inside the card `_draw_chrome` cuts around it.
	if screen.unplaced_label != null and screen.unplaced_label.visible:
		var block := Rect2(screen.unplaced_label.position - Vector2(8.0, 6.0),
			Vector2(screen.unplaced_label.size.x + 16.0,
				screen.unplaced_label.size.y + screen.unplaced_host.size.y + 12.0))
		pairs.append(["UnplacedHeading",
			Rect2(screen.unplaced_label.position, screen.unplaced_label.size), block])
		pairs.append(["UnplacedRegions",
			Rect2(screen.unplaced_host.position, screen.unplaced_host.size), block])
	return pairs


## A `draw_card` frame's inner field - the rectangle a control may occupy without
## touching the bevel, the fillet or a corner scroll. The weights are the ones
## `WotrHudChrome.draw_card` actually cuts.
func _inner_field(card: Rect2) -> Rect2:
	var weight := clampf(minf(card.size.x, card.size.y) * 0.035, 3.0, 9.0)
	return card.grow(-weight * 2.9)


## SHELL NAVIGATION IS NOT ON THE SCOREBOARD, held as a property.
##
## A blind review's answer to "what would a show-floor audience photograph" was
## "the MAIN MENU button in the scoreboard". It is on the pause shell now, and this
## is what stops it coming back: MAIN MENU may not intersect the seat panel, and it
## may not be reachable without opening the shell.
func _check_shell_navigation_is_not_on_the_scoreboard(screen) -> void:
	var inside: Array[String] = []
	for value in SIZES:
		var frame := value as Vector2
		screen.size = frame
		screen._relayout()
		var card: Rect2 = screen._standings_card_rect()
		var pill := Rect2(screen.back_button.position, screen.back_button.size)
		if card.intersects(pill):
			inside.append("%s intersects the seat panel %s at %dx%d" % [
				str(pill), str(card), int(frame.x), int(frame.y)])
	_check("shell_navigation_is_never_laid_out_inside_the_live_standings_panel",
		inside.is_empty(), "; ".join(inside))
	# AND IT IS BEHIND ESCAPE, not merely moved. A shell nobody can open is worse
	# than a shell in the wrong place.
	screen.toggle_pause_shell(false)
	var hidden: bool = not screen.back_button.visible and not screen.pause_shell.visible
	screen.toggle_pause_shell(true)
	var opened: bool = screen.back_button.visible and screen.pause_resume.visible \
		and screen.pause_shell.visible
	screen.toggle_pause_shell(false)
	var closed_again: bool = not screen.back_button.visible
	_check("the_pause_shell_opens_and_closes_and_carries_both_of_its_controls",
		hidden and opened and closed_again,
		"hidden=%s opened=%s closed=%s" % [str(hidden), str(opened), str(closed_again)])


func _check(what: String, condition: bool, detail: String = "") -> void:
	if condition:
		_passed += 1
		print("  PASS  %s%s" % [what, "" if detail.is_empty() else " (%s)" % detail])
	else:
		_failed += 1
		print("  FAIL  %s%s" % [what, "" if detail.is_empty() else " (%s)" % detail])


## ONE BUTTON SYSTEM, ASSERTED AS ONE.
##
## The owner asked why END TURN "looks good but is not the same for ATTACK, CANCEL
## or AUTO-RESOLVE", and the cause was that four different passes each named a
## SUBSET of the four capsules - CANCEL was missing from the loop that strips a
## capsule's own stylebox when retail's art is bound, so retail's capsule was
## painted under it and this project's drawn pill was painted on top of it.
##
## A comment saying "keep these four in step" would not have caught that and did
## not. These properties would have: same dress, same size, same states.
##
## ------------------------------------------------------------------------------
## THE "SAME SIZE" HALF IS RESTATED, NOT DELETED, AND HERE IS WHY IT NO LONGER HOLDS
## ------------------------------------------------------------------------------
##
## The size clause used to demand that all four capsules be the SAME RECTANGLE, and
## it was correct for the defect it was written against: four passes each naming a
## subset of the four had left them visibly unlike each other by accident.
##
## An adversarial art-direction review then judged the assembled frame against a
## modern bar and named the OPPOSITE property as a defect: "ATTACK, CANCEL and
## AUTO-RESOLVE are rendered at identical weight, identical fill, identical width
## class... ATTACK must be the loudest control on screen and it is currently tied
## for third." Both readings are right, about different things. What must be
## identical is the MATERIAL - one button system, retail's own capsule, four
## states, no control painting a face of its own. What must NOT be identical is the
## RANK, and a rail whose three controls are the same width cannot express one.
##
## So the clause is replaced rather than dropped, and the replacement is strictly
## the harder property, because "all equal" is one arithmetic relation and this is
## three:
##
##   * every capsule's width is its declared class share of the rail's own unit
##     (`ScreenScript.COMMAND_WIDTH_CLASS`), so a rank cannot drift into a
##     coincidence;
##   * the three shares still sum to 3.00, so ranking the rail cannot move the deck
##     or the tray under it;
##   * every capsule is still the same HEIGHT and still paints no face of its own,
##     which is the whole of what "one button system" ever meant.
##
## END TURN is measured against the authored face rather than against the rail: it
## is not in the rail at all (it lives on retail's own `endTurnButton` island) and
## its width is retail's, not a class share.
func _check_the_command_capsules_are_one_button_system(screen) -> void:
	var capsules: Array = screen.command_capsules()
	_check("the_command_rail_is_the_four_capsules_and_only_them",
		capsules.size() == 4,
		"%d capsule(s)" % capsules.size())
	var art_bound: bool = screen.strategic != null
	var wrong: Array[String] = []
	var reference := 0.0
	for value in capsules:
		var button := value as Button
		if art_bound:
			# WITH RETAIL'S CAPSULE UNDER IT, a button must paint nothing of its own.
			# A StyleBoxFlat here is this project's pill on top of retail's art, which
			# is precisely what CANCEL was doing.
			for state in ["normal", "hover", "pressed", "focus", "disabled"]:
				if not (button.get_theme_stylebox(state) is StyleBoxEmpty):
					wrong.append("%s paints its own %s face over retail's capsule" % [
						String(button.name), state])
		# ONE FACE HEIGHT. The rank is carried entirely by width and by what is drawn
		# INSIDE the capsule; a difference in height would be a second kind of button
		# rather than a second weight of the same one.
		if reference == 0.0:
			reference = button.size.y
		elif absf(button.size.y - reference) > 0.5:
			wrong.append("%s stands %.1f tall where the rail stands %.1f" % [
				String(button.name), button.size.y, reference])
	_check("every_command_capsule_wears_the_same_face_in_the_same_states",
		wrong.is_empty(), "; ".join(wrong) if not wrong.is_empty()
			else "4 capsule(s) at %.1f tall%s" % [reference,
				", all stripped to retail's own art" if art_bound else ", on the drawn pill"])
	# THE RANK ITSELF, as the three properties the block above sets out.
	var ranked: Array[String] = []
	var shares: Array = ScreenScript.COMMAND_WIDTH_CLASS
	var share_total := 0.0
	for share_value in shares:
		share_total += float(share_value)
	if absf(share_total - float(shares.size())) > 0.001:
		ranked.append("the %d width classes sum to %.3f, not to %d - ranking the rail would move the deck" % [
			shares.size(), share_total, shares.size()])
	var rail: Array = [screen.attack_button, screen.cancel_button, screen.auto_resolve_button]
	# The unit is solved from the row rather than restated: whatever the rail's own
	# cell unit is, each cell must be its class share OF IT.
	var unit := 0.0
	for index in range(rail.size()):
		var button := rail[index] as Button
		if button == null:
			continue
		var cell_unit := button.size.x / float(shares[index])
		if unit == 0.0:
			unit = cell_unit
		elif absf(cell_unit - unit) > 1.5:
			ranked.append("%s is %.1f wide, which is class %.2f of a %.1f unit and the rail's unit is %.1f" % [
				String(button.name), button.size.x, float(shares[index]), cell_unit, unit])
	# AND THE PRIMARY IS ACTUALLY THE WIDEST. The shares could all be satisfied and
	# still rank the wrong control if the table were ever reordered.
	if screen.attack_button != null and screen.cancel_button != null \
			and screen.auto_resolve_button != null:
		if screen.attack_button.size.x <= screen.auto_resolve_button.size.x \
				or screen.auto_resolve_button.size.x <= screen.cancel_button.size.x:
			ranked.append("the rail is not ranked widest-to-narrowest: ATTACK %.0f, AUTO-RESOLVE %.0f, CANCEL %.0f" % [
				screen.attack_button.size.x, screen.auto_resolve_button.size.x,
				screen.cancel_button.size.x])
	_check("the_command_rail_ranks_one_primary_one_secondary_and_one_ghost",
		ranked.is_empty(), "; ".join(ranked) if not ranked.is_empty()
			else "ATTACK %.0f > AUTO-RESOLVE %.0f > CANCEL %.0f on a %.1f unit" % [
				screen.attack_button.size.x, screen.auto_resolve_button.size.x,
				screen.cancel_button.size.x, unit])


## THE TWO MEDALLIONS ARE CONTROLS, AND THEIR ROLES ARE RETAIL'S RECORD.
##
## A previous round left them inert on the grounds that retail "carries the art and
## not the mapping". It does carry the mapping: StrategicPalantir's namedInstances
## has optionsButton at root depth 100 and objectivesButton at root depth 96, and
## the flattened triangles put depth 100 at authored x 81..118 and depth 96 at
## x 140..177 - so the KEY, the left disc, is the options one.
##
## This holds the DERIVATION rather than the conclusion: the key's rectangle must
## be left of the banner's and both must be real, which is what would break if the
## instance table were ever read the other way round.
func _check_the_medallions_are_controls_on_retails_own_rectangles(screen) -> void:
	screen.size = SIZES[SIZES.size() - 1]
	screen._relayout()
	var key: Button = screen.medallion_key
	var banner: Button = screen.medallion_banner
	var wired: bool = key != null and banner != null \
		and key.pressed.get_connections().size() > 0 \
		and banner.pressed.get_connections().size() > 0
	_check("both_palantir_medallions_are_real_buttons", wired,
		"key and banner are %s" % ("wired" if wired else "INERT"))
	if screen.strategic == null:
		# No bundle: the discs are not painted, so there is nothing to sit on and the
		# controls are parked. That is the designed state, and it is checked.
		_check("with_no_strategic_art_the_medallions_are_parked_rather_than_floating",
			not key.visible and not banner.visible,
			"key=%s banner=%s" % [str(key.visible), str(banner.visible)])
		return
	var key_box := Rect2(key.position, key.size)
	var banner_box := Rect2(banner.position, banner.size)
	_check("the_key_is_retails_optionsButton_and_sits_left_of_the_banner",
		key_box.size.x > 4.0 and banner_box.size.x > 4.0
			and key_box.position.x < banner_box.position.x,
		"key %s, banner %s" % [str(key_box), str(banner_box)])


## THREE STOPS, AND NONE OF THEM A TRAP.
##
## F2 used to be a switch, and the owner's complaint was that a switch is not a way
## to look at the map WHILE PLAYING. This holds the properties that make the
## replacement safe rather than merely different: the middle stop actually removes
## something (a cinematic mode that removes nothing is a rename), and one more press
## from the last stop comes all the way back.
func _check_the_view_has_three_stops_and_never_traps_the_player(screen) -> void:
	screen.size = SIZES[SIZES.size() - 1]
	screen._relayout()
	screen.set_view_mode(ScreenScript.VIEW_FULL)
	var slots := ["stats", "checklist", "endTurnButton", "selectionDetails", "globe"]
	var full_islands := 0
	for slot in slots:
		if screen.island_is_shown(String(slot)):
			full_islands += 1
	screen.set_view_mode(ScreenScript.VIEW_FOCUSED)
	var focused_islands := 0
	for slot in slots:
		if screen.island_is_shown(String(slot)):
			focused_islands += 1
	_check("the_focused_stop_actually_takes_chrome_off_the_glass",
		focused_islands > 0 and focused_islands < full_islands,
		"%d island(s) at FULL, %d at FOCUSED" % [full_islands, focused_islands])
	screen.set_view_mode(ScreenScript.VIEW_MAP)
	var bare: bool = screen.hud_hidden
	# ONE MORE STEP WRAPS. This is the property that makes one key enough.
	screen.set_view_mode(ScreenScript.VIEW_MAP + 1)
	_check("one_more_step_from_the_bare_map_wraps_back_to_the_whole_hud",
		bare and screen.view_mode == ScreenScript.VIEW_FULL and not screen.hud_hidden,
		"bare=%s, wrapped to %d, hidden=%s" % [
			str(bare), screen.view_mode, str(screen.hud_hidden)])


## RETAIL'S OWN HALF TURN, ON RETAIL'S OWN ARROW.
##
## Both expander arrows were flattened pointing DOWN because the flattening parked
## their Arrow sprite on retail's _rotateDown frame, whose matrix is [-1, 0, 0, -1].
## The turn applied to put them back up is therefore a reading of the movie, and
## this asserts the READING rather than the picture: the path the turn is applied at
## has to be a path the flattened frame actually carries, derived from retail's own
## instance depths. A path that matched nothing would turn nothing and would fail
## silently, which is the only way this could go wrong.
func _check_the_expander_arrows_are_turned_through_retails_own_half_turn(screen) -> void:
	if screen.strategic == null:
		_check("with_no_strategic_art_there_is_no_expander_arrow_to_turn",
			screen._expander_arrow_paths("StrategicStats", "Expand", {}).is_empty(),
			"no bundle, no arrow")
		return
	screen.size = SIZES[SIZES.size() - 1]
	screen._relayout()
	var missed: Array[String] = []
	for entry_value in [
		["StrategicStats", "Expand", "stats"],
		["StrategicChecklist", "expandButton", "checklist"],
	]:
		var entry := entry_value as Array
		var island: Dictionary = screen._islands.get(String(entry[2]), {}) as Dictionary
		if island.is_empty():
			missed.append("%s has no island" % String(entry[0]))
			continue
		var frame: Dictionary = island["frame"] as Dictionary
		var paths: Array = screen._expander_arrow_paths(
			String(entry[0]), String(entry[1]), frame)
		if paths.is_empty():
			missed.append("%s/%s derived no arrow path" % [String(entry[0]), String(entry[1])])
			continue
		var wanted := String(paths[0])
		var hit := 0
		for draw_value in frame.get("draws", []) as Array:
			var path := String((draw_value as Dictionary).get("path", ""))
			if path == wanted or path.begins_with(wanted + "/"):
				hit += 1
		if hit == 0:
			missed.append("%s matches no draw in the flattened frame" % wanted)
	_check("both_expander_arrows_resolve_to_draws_retails_own_frame_carries",
		missed.is_empty(), "; ".join(missed) if not missed.is_empty()
			else "2 arrow path(s), both matched")
