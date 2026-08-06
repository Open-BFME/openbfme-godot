extends SceneTree

## RETAIL'S WAR OF THE RING UI SURFACE AND ITS CAMERA, checked against retail's
## own numbers and against the properties the camera has to keep.
##
## This runner exists because the three claims this lane makes are all claims
## about RESOLUTION, and every one of them can fail silently into something that
## looks fine:
##
##   1. an army banner carries the portrait retail draws for THAT army;
##   2. a build plot sits where retail authored it;
##   3. a build ring offers the structures retail marks AvailableTo that seat.
##
## A portrait that quietly falls back to another faction's plate, a plot that
## quietly lands at the origin, or a ring that quietly offers every structure in
## the game would all still produce a screen that photographs well. So each is
## asserted against the bundle and the living-world document rather than against
## a picture.
##
## IT IS A SEPARATE RUNNER ON PURPOSE. `wotr_map3d_runner` has stated floors of
## 29 and 14 checks; folding these in would have moved both numbers and made the
## floor arithmetic a thing to argue about. This runner carries its own.
##
## LIVENESS. The expected check count is asserted, so a function that aborts
## before its assertions fails the run instead of silently shrinking it.
##
## Usage:
##   Godot_v4.7 --headless --path game --script tests/wotr_living_world_ui_runner.gd
## With no bundle and no document present it still runs its unbound checks and
## says so; that is the 4/4 mode.

const UiScript = preload("res://src/wotr/wotr_living_world_ui.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const ScreenScript = preload("res://src/ui/wotr_screen.gd")
const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")

## Retail's own totals, from `data/ini/livingworldbuildings.ini`. These are
## MEASUREMENTS of the shipped file, not targets: 28 `LivingWorldBuilding`
## blocks, 99 `ArmyToSpawn` blocks inside them, 7 playable
## `LivingWorldPlayerTemplate` families with a build-plot icon plus the observer
## template that has none.
const RETAIL_BUILDINGS := 28
const RETAIL_RECRUITS := 99
const RETAIL_BUILD_PLOT_ICONS := 7

## The ONE image retail names and ships no texture for. `livingworldbuildings.ini`
## marks it itself: `ConstructButtonImage = CPYoungWizardAlpha  // TEMP`, and
## `CPYoungWizardAlpha.tga` is in no archive under any name. It is asserted BY
## NAME so that a bundle which starts losing other atlases cannot hide inside a
## "one or two missing is fine" tolerance.
const RETAIL_ATLAS_GAP := "CPYoungWizardAlpha"

## 31 checks over the UI bundle plus 6 over the camera, plus the four the STRING
## AUDIT adds (the register of every player-visible surface, the same sweep proved
## non-vacuous against the diagnostics panel, every moved gap arriving there, and
## the ribbon's trim), plus the APT renderer's host-suppression probe. Stated
## rather than counted by hand at the bottom: the liveness assertion below compares
## against this, so a function that aborts before its assertions reddens the run.
## Round six adds four: the empty-foundation tile resolves for every seat that
## authors one, the tile the palantir's lens shows is the SAME id the build cards
## show, no two seats share one, and the screen actually reaches it for a region
## it holds. See `_check_the_empty_foundation_tile_is_one_retail_asset`.
## ROUND SEVEN ADDS THREE, all on the structures well - the surface a blind
## review named the single most damaging element on the screen. It is a drawn
## table now rather than a run of lines in the tray's text control, so the three
## are: the card carries no text on that tab, the roster is retail's own offerings
## with retail's own icons, and drawing it instead of setting it did not take it
## out of the player-string audit. See the block at the end of
## `_check_the_players_surface_speaks_about_the_world`.
## ROUND EIGHT ADDS EIGHT, on the one control the owner's biggest complaint was
## about: taking neutral ground. Most of retail's 52-region board starts held by
## nobody, and the command rail's first button used to be DISABLED on every one
## of those regions, so a human could not expand at all while the opponent did.
## The eight assert the whole path end to end on a real session and a real screen
## - the button is live, it renames itself, it commits a CLAIM and not a battle
## commitment, the ground changes hands, retail's own "%s taken!" notice is what
## the player is shown (and not the "%s conquered!" a battle raises), no battle
## report is opened over an event where nobody fought, and the record names itself
## a claim with no seed because nothing was rolled. See `_check_the_screen_can_take_neutral_ground`.
## ROUND NINE ADDS EIGHT, on the path the owner's very first defect names: the
## build ring. Construction became real in the strategic layer this round and
## this screen is the only thing between it and the player, so the crossing is
## pinned end to end - the ring offers what the session offers, a refused
## offering is still drawn and names itself in the player's register, a click
## spends the treasury and stands a structure, it does NOT end the turn, the
## treasure plate tracks the live purse rather than the seat template's frozen
## figure, a refusal moves nothing and is shown, both plot counters count what
## stands, and the diagnosis has stopped reporting a gap that is closed. See
## `_check_the_screen_can_raise_a_structure`.
const CHECKS_WITH_BUNDLE := 75
const CHECKS_WITHOUT_BUNDLE := 8

var _passed := 0
var _failed := 0


func _initialize() -> void:
	print("== WAR OF THE RING UI SURFACE ==")
	var ui := UiScript.new()
	var roots := _roots()
	var located: Dictionary = ui.locate_and_load(roots)
	var bound := bool(located.get("ok", false))
	if bound:
		print("bundle: %s" % String(located["path"]))
	else:
		print("bundle: NONE - %s" % String(located["reason"]).split("\n")[0])

	_check_the_loader_reports_its_own_absence(located, bound)
	if bound:
		_check_the_bundle_carries_retails_own_totals(ui)
		_check_every_image_id_resolves_or_is_named(ui)
		_check_a_crop_is_a_real_rectangle_inside_its_atlas(ui)
		_check_an_unknown_id_produces_nothing_and_is_recorded(ui)
		_check_the_portrait_ladder_uses_only_authored_links(ui)
		_check_buildings_are_filtered_by_retails_own_availableto(ui)
		_check_the_chrome_sheet_yields_its_rings_and_bands(ui)
		_check_the_empty_foundation_tile_is_one_retail_asset(ui)
		_check_the_screen_binds_plots_banners_and_a_ring(ui)
		_check_the_screen_can_take_neutral_ground()
		_check_the_screen_can_raise_a_structure()
		_check_live_phase_resume_and_abandon()
	_check_the_apt_renderer_honours_retails_masks_and_substitutes_nothing()

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


## ------------------------------------------------------------------------------
## THE BUILD PATH, END TO END, ON A REAL SESSION AND A REAL SCREEN
## ------------------------------------------------------------------------------
##
## The owner's first defect was "I cannot click on the buildings or build them
## with the icons as they don't light up and do not allow me to build them". The
## strategic layer grew a full construction simulation, and the ONLY thing between
## that and the player is this screen. Every step of the crossing is asserted here,
## because every one of them can fail into something that photographs perfectly:
##
##   1. THE RING OFFERS WHAT THE SESSION OFFERS. A ring built off the old UI
##      bundle instead of `session.build_options()` looks identical and cannot be
##      clicked into a build.
##   2. A REFUSED OFFERING IS STILL OFFERED AND NAMES ITSELF, in the player's
##      register. A refusal worded by the strategic layer would read "LWB_MenFortress
##      costs 1500 and seat 0 has 900 in the treasury" on the glass.
##   3. A CLICK SPENDS THE TREASURY AND STANDS A STRUCTURE. The whole point.
##   4. AND DOES NOT END THE TURN - retail's own rule, stated in its tutorial:
##      construction, training and movement are all decided in one phase.
##   5. THE TREASURE PLATE TRACKS THE SESSION, not the template. It read a frozen
##      3000 for the whole campaign, and a frozen number beside a live one is the
##      most expensive kind of wrong: it is right at the start of every playtest.
##   6. A REFUSAL MOVES NOTHING and is SHOWN.
##   7. THE PLOT COUNT COUNTS WHAT STANDS, AND IS STATED ONCE. It used to be three
##      surfaces with a structural zero in them; it is now one surface with a live
##      numerator, and the other two are asserted silent. See the block at step 7.
##
## IT RUNS ON ITS OWN SESSION, last, because four of these MUTATE the strategic
## state. A build committed inside the string-audit block would leave every check
## after it looking at a board that had moved.
func _check_the_screen_can_raise_a_structure() -> void:
	var names := [
		"the_build_ring_offers_exactly_what_the_session_offers",
		"every_refused_offering_is_still_drawn_and_names_itself_in_the_players_register",
		"a_click_on_a_build_entry_spends_the_treasury_and_stands_a_structure",
		"raising_a_structure_does_not_end_the_turn",
		"the_treasure_plate_tracks_the_live_treasury_and_not_the_seat_template",
		"a_refused_build_moves_nothing_and_the_player_is_told_why",
		"the_build_plot_count_is_stated_once_and_counts_what_stands",
		"the_diagnosis_no_longer_reports_construction_as_an_absent_system",
	]
	var found: Dictionary = SessionScript.locate_document([])
	if not bool(found.get("ok", false)):
		for what in names:
			_fail(String(what), "no document")
		return
	var session = _seat(found)
	var screen := ScreenScript.new()
	root.add_child(screen)
	screen.configure(session, [], "", [])
	screen.size = Vector2(2560.0, 1440.0)
	screen._relayout()
	screen.refresh()

	# A REGION THIS SEAT HOLDS THAT AUTHORS FOUNDATIONS AND HAS A FREE ONE.
	var seat: int = session.state.active_player()
	var region_id := ""
	for candidate in session.state.regions_owned_by(seat):
		var plots: Dictionary = session.build_plots(String(candidate))
		if int(plots.get("free", 0)) > 0 and not session.build_options(String(candidate)).is_empty():
			region_id = String(candidate)
			break
	if region_id.is_empty():
		for what in names:
			_fail(String(what), "no held region offers a free foundation: %s"
				% session.build_reason())
		screen.queue_free()
		return
	var plot: int = session.state.free_plot(region_id)
	screen._on_region_hovered(region_id)
	screen._on_plot_clicked(region_id, plot)
	screen.refresh()

	# 1. THE RING IS THE SESSION'S OWN OFFER, id for id and in the same order.
	var offered: Array[String] = []
	for row_value in session.build_options(region_id, plot):
		offered.append(String((row_value as Dictionary)["building"]))
	var ringed: Array[String] = []
	for entry_value in screen._radial_entries():
		ringed.append(String((entry_value as Dictionary)["id"]))
	_check(String(names[0]), not offered.is_empty() and ringed == offered,
		"%d offered, ring carries %d: %s" % [offered.size(), ringed.size(),
			", ".join(ringed)])

	# 2. THE REFUSALS ARE IN THE PLAYER'S REGISTER. Every entry that cannot be
	#    built is STILL in the ring (nothing is filtered out) and carries a
	#    sentence, and no sentence carries the banned vocabulary.
	var refused := 0
	var silent: Array[String] = []
	var leaked: Array[String] = []
	for entry_value in screen._radial_entries():
		var entry := entry_value as Dictionary
		if bool(entry["can_build"]):
			continue
		refused += 1
		var sentence := String(entry["refusal"])
		if sentence.strip_edges().is_empty():
			silent.append(String(entry["id"]))
			continue
		for banned_value in screen.IMPLEMENTATION_VOCABULARY:
			if sentence.to_lower().contains(String(banned_value)):
				leaked.append("%s :: %s" % [String(banned_value), sentence])
	_check(String(names[1]), silent.is_empty() and leaked.is_empty(),
		"%d refused offering(s), %d silent, %d leaking%s" % [
			refused, silent.size(), leaked.size(),
			"" if leaked.is_empty() else ": " + "; ".join(leaked)])

	# 3. THE CLICK. Driven through the SIGNAL HANDLER the map view calls, not
	#    through the session, so what is asserted is the path the player uses.
	var buildable := ""
	var price := 0
	for entry_value in screen._radial_entries():
		var entry := entry_value as Dictionary
		if bool(entry["can_build"]):
			buildable = String(entry["id"])
			price = int(entry["cost_value"])
			break
	var purse_before: int = session.treasure()
	var hash_before: String = session.state.state_hash()
	var turn_before: int = session.state.turn_index
	var seat_before: int = session.state.active_player()
	var used_before: int = int(session.build_plots(region_id).get("used", 0))
	if buildable.is_empty():
		for what in names.slice(2):
			_fail(String(what), "nothing in %s was buildable at all" % region_id)
		screen.queue_free()
		return
	screen._on_build_entry_clicked(region_id, plot, buildable)
	var plots_after: Dictionary = session.build_plots(region_id)
	var standing := ""
	for row_value in plots_after.get("plots", []) as Array:
		if int((row_value as Dictionary).get("plot", -1)) == plot:
			standing = String((row_value as Dictionary).get("building", ""))
	_check(String(names[2]),
		standing == buildable
			and int(plots_after.get("used", 0)) == used_before + 1
			and session.treasure() == purse_before - price
			and session.state.state_hash() != hash_before,
		"%s stands on plot %d of %s; treasury %d -> %d for %d; used %d -> %d" % [
			standing, plot, region_id, purse_before, session.treasure(), price,
			used_before, int(plots_after.get("used", 0))])

	# 4. AND THE TURN IS STILL THIS SEAT'S. Retail's own rule.
	_check(String(names[3]),
		session.state.turn_index == turn_before
			and session.state.active_player() == seat_before,
		"turn %d -> %d, seat %d -> %d" % [turn_before, session.state.turn_index,
			seat_before, session.state.active_player()])

	# 5. THE PLATE. It reads the LIVE treasury, which after a purchase is no longer
	#    the template's `ScenarioStartResources` - the number it used to be frozen
	#    at. Both halves are asserted: the plate matches the session AND, when the
	#    price was not zero, it has moved off the template's figure.
	screen.refresh()
	var plate := ""
	for fact_value in screen._header_facts:
		var fact := fact_value as Dictionary
		if String(fact.get("label", "")).to_lower().contains("treasur") \
				and not String(fact.get("label", "")).to_lower().contains("income"):
			plate = String(fact.get("value", ""))
	var template_purse := int((session.world.player_templates.get(
		String((session.state.players[seat] as Dictionary).get("template", "")), {})
		as Dictionary).get("scenario_start_resources", -1))
	_check(String(names[4]),
		plate == str(session.treasure())
			and (price == 0 or plate != str(template_purse)),
		"plate reads %s, session says %d, the template says %d (price %d)" % [
			plate, session.treasure(), template_purse, price])

	# 6. A REFUSAL MOVES NOTHING AND IS SHOWN. The same foundation, a second time:
	#    it is occupied now, so every offering on it is refused.
	var hash_held: String = session.state.state_hash()
	var purse_held: int = session.treasure()
	screen.show_message("")
	screen._on_build_entry_clicked(region_id, plot, buildable)
	var told := String(screen.message_label.text).strip_edges()
	var told_clean := not told.is_empty()
	for banned_value in screen.IMPLEMENTATION_VOCABULARY:
		if told.to_lower().contains(String(banned_value)):
			told_clean = false
	_check(String(names[5]),
		session.state.state_hash() == hash_held
			and session.treasure() == purse_held and told_clean,
		"hash %s, treasury %d -> %d, message \"%s\"" % [
			"held" if session.state.state_hash() == hash_held else "MOVED",
			purse_held, session.treasure(), told])

	# ------------------------------------------------------------------------------
	# 7. THE COUNTER. RESTATED, NOT DELETED - AND IT IS NOW A STRONGER CHECK.
	# ------------------------------------------------------------------------------
	#
	# This used to assert that the tray's STATUS RIBBON carried "N of M built", and
	# it was written to catch a real defect: the numerator was a structural literal
	# zero for several rounds, so a screen that had just raised a structure still
	# reported none standing.
	#
	# The ribbon no longer carries that phrase, and its absence is deliberate rather
	# than a regression. An adversarial art-direction review counted the build-plot
	# count being stated THREE TIMES in this one tray - "Territory Build Plots 0/3"
	# in the middle column, "Plots 0/3" in the palantir footer, "0 of 3 built" in the
	# status strip - and named the redundancy as a defect in its own right: "saying
	# the same number three times does not make it clearer, it makes the tray look
	# like three teams shipped independently." Two of the three are gone.
	#
	# SO THE PROPERTY MOVES TO THE SURVIVOR AND GETS HARDER. It is asserted on the
	# tray column that actually carries the count now - through
	# `build_plot_counter_text`, the single definition the draw pass itself reads, so
	# the check is on the number the player sees rather than on a second copy of the
	# arithmetic - AND the two retired statements are asserted ABSENT, which the old
	# check could not have expressed at all. A future edit that quietly puts either
	# back fails here.
	screen._on_region_hovered(region_id)
	screen._on_tab_pressed("structures")
	screen.refresh()
	var used: int = int(session.build_plots(region_id).get("used", 0))
	var total: int = int(session.build_plots(region_id).get("total", 0))
	var counter := String(screen.build_plot_counter_text(region_id))
	var ribbon := String(screen.tray_ribbon_text)
	var faults: Array[String] = []
	if used <= 0:
		faults.append("nothing stands after a committed build")
	if counter != "%d/%d" % [used, total]:
		faults.append("the tray column reads \"%s\" where the state says %d/%d" % [
			counter, used, total])
	if ribbon.contains("of %d built" % total):
		faults.append("the status ribbon states the plot count a second time: \"%s\"" % ribbon)
	_check(String(names[6]), faults.is_empty(),
		"; ".join(faults) if not faults.is_empty()
			else "the tray column reads %s, and it is the only surface that states it" % counter)

	# 8. AND THE DIAGNOSIS HAS STOPPED CLAIMING THE GAP IS OPEN.
	#
	# THIS IS THE RESTATEMENT OF AN ASSERTION RATHER THAN ITS DELETION, and the
	# reason is on the record: `every_gap_this_screen_took_off_the_hud_is_stated_in
	# _full_in_the_diagnosis` used to require the diagnosis to contain "construction
	# is not simulated" and "nothing ever gets built". Both are now FALSE, and an
	# absence list that keeps reporting a closed gap is a diagnosis lying in the
	# safe direction. So the assertion is inverted and kept: those phrases must be
	# GONE, and what took their place - the verbatim refusal ledger and the
	# statement of which refusal wording is this project's rather than retail's -
	# must be there.
	var diagnosis := String(screen.diagnostics_text()).to_lower()
	var stale: Array[String] = []
	for phrase_value in ["construction is not simulated", "nothing ever gets built",
			"no building system exists"]:
		if diagnosis.contains(String(phrase_value)):
			stale.append(String(phrase_value))
	var carries := diagnosis.contains("project-authored wording") \
		and diagnosis.contains("controlbar:lw_fortrestricted")
	_check(String(names[7]), stale.is_empty() and carries,
		"stale claim(s): %s; replacement present: %s" % [
			"none" if stale.is_empty() else "; ".join(stale), str(carries)])
	screen.queue_free()


## Where a bundle might be, mirroring what the screen itself searches: whatever
## `OPENBFME_LIVING_MAP_REGIONS` names, and its sibling by the living map.
func _roots() -> Array:
	var roots: Array = []
	var probed: Dictionary = RegionGeometryScript.probe([])
	if bool(probed.get("found", false)):
		roots.append(String(probed["root"]))
	roots.append(RegionGeometryScript.USER_BUNDLE)
	return roots


# --- the checks ----------------------------------------------------------------

## A MISSING BUNDLE IS A SENTENCE, NOT A FALSE. The reason must name what is
## absent, what it costs on screen, and the command that produces one - the same
## contract the map and region-geometry loaders keep.
func _check_the_loader_reports_its_own_absence(located: Dictionary, bound: bool) -> void:
	var reason := String(located.get("reason", ""))
	_check("a_bound_bundle_reports_no_reason_and_an_absent_one_reports_a_reason",
		(bound and reason.is_empty()) or (not bound and not reason.is_empty()))
	var empty := UiScript.new()
	var nothing: Dictionary = empty.locate_and_load([])
	if bool(nothing.get("ok", false)):
		# The environment names a bundle, so "no roots" still finds one. That is
		# a legitimate configuration and the check below still has to mean
		# something, so it asserts the loaded state instead.
		_check("loading_with_no_roots_either_finds_the_environments_bundle_or_explains",
			empty.loaded and not empty.building_ids.is_empty())
	else:
		var text := String(nothing.get("reason", ""))
		_check("loading_with_no_roots_explains_itself",
			text.contains("living_world_ui") and text.contains(UiScript.BUNDLE_ENV),
			text.split(".")[0])
	var refused := UiScript.new()
	_check("a_path_that_is_not_a_bundle_is_refused_rather_than_half_loaded",
		not refused.load_from("res://project.godot") and not refused.loaded)
	_check("a_refusal_carries_at_least_one_stated_error", not refused.errors.is_empty(),
		", ".join(Array(refused.errors)))


## RETAIL'S OWN COUNTS. If the converter starts dropping blocks, this is where it
## shows up - not as a smaller ring on a screenshot nobody re-reads.
func _check_the_bundle_carries_retails_own_totals(ui) -> void:
	_check("the_bundle_carries_every_livingworldbuilding_retail_ships",
		ui.building_ids.size() == RETAIL_BUILDINGS,
		"%d of %d" % [ui.building_ids.size(), RETAIL_BUILDINGS])
	var recruits := 0
	for id in ui.building_ids:
		recruits += (ui.buildings[id] as Dictionary).get("recruits", []).size()
	_check("the_bundle_carries_every_armytospawn_retail_ships",
		recruits == RETAIL_RECRUITS, "%d of %d" % [recruits, RETAIL_RECRUITS])
	_check("the_bundle_carries_every_build_plot_icon_family",
		ui.build_plot_icons.size() == RETAIL_BUILD_PLOT_ICONS,
		"%d of %d" % [ui.build_plot_icons.size(), RETAIL_BUILD_PLOT_ICONS])
	# The plot decal MODELS are recorded and NOT converted; the screen says so,
	# and the names have to actually be there for it to be able to.
	var named := 0
	for key in ui.build_plot_icons.keys():
		for object in (ui.build_plot_icons[key] as Dictionary).get("objects", []) as Array:
			if not String((object as Dictionary).get("model", "")).is_empty():
				named += 1
	_check("every_plot_icon_family_names_the_w3d_models_it_is_standing_in_for",
		named >= RETAIL_BUILD_PLOT_ICONS, "%d model name(s)" % named)


## EVERY ID THE LIVING WORLD NAMES EITHER RESOLVES OR IS LISTED. There is no
## third state, and the one retail itself leaves broken is asserted by name.
func _check_every_image_id_resolves_or_is_named(ui) -> void:
	var requested := int(ui.totals.get("imageIdsRequested", 0))
	var resolved := int(ui.totals.get("imageIdsResolved", 0))
	var missing: Array = ui.gaps.get("missingImageIds", []) as Array
	var ambiguous: Array = ui.gaps.get("ambiguousImageIds", []) as Array
	_check("every_requested_image_id_is_either_resolved_or_named_as_a_gap",
		resolved + missing.size() + ambiguous.size() == requested,
		"%d resolved + %d missing + %d ambiguous = %d requested" % [
			resolved, missing.size(), ambiguous.size(), requested])
	_check("retail_names_no_image_this_project_cannot_find_a_definition_for",
		missing.is_empty(), ", ".join(missing.map(func(v: Variant) -> String: return String(v))))
	_check("no_requested_image_id_is_defined_twice",
		ambiguous.is_empty(), ", ".join(ambiguous.map(func(v: Variant) -> String: return String(v))))
	var without_atlas: Array = ui.gaps.get("cropsWithoutAtlas", []) as Array
	var names: Array[String] = []
	for value in without_atlas:
		names.append(String(value))
	names.sort()
	# EXACTLY retail's own gap, by name. Not "at most one".
	_check("the_only_image_with_no_shipped_atlas_is_the_one_retail_marks_TEMP",
		names == [RETAIL_ATLAS_GAP], ", ".join(names))
	_check("an_image_with_no_atlas_draws_nothing_rather_than_something_else",
		ui.image(RETAIL_ATLAS_GAP) == null and ui.missing_images.has(RETAIL_ATLAS_GAP),
		String(ui.missing_images.get(RETAIL_ATLAS_GAP, "not recorded")))


## A CROP MUST BE A REAL RECTANGLE INSIDE ITS ATLAS. An `AtlasTexture` whose
## region ran off the edge would silently draw transparent - a portrait that is
## present, resolved, and invisible.
func _check_a_crop_is_a_real_rectangle_inside_its_atlas(ui) -> void:
	var ids: Array[String] = []
	for key in ui.images.keys():
		ids.append(String(key))
	ids.sort()
	var checked := 0
	var bad: Array[String] = []
	for image_id in ids:
		if not ui.has_image(image_id):
			continue
		var texture: Texture2D = ui.image(image_id)
		if texture == null:
			bad.append("%s did not produce a texture" % image_id)
			continue
		var atlas := texture as AtlasTexture
		var region := atlas.region
		var whole := Rect2(Vector2.ZERO, atlas.atlas.get_size())
		if region.size.x <= 0.0 or region.size.y <= 0.0 or not whole.encloses(region):
			bad.append("%s crops %s out of %s" % [image_id, str(region), str(whole)])
		checked += 1
	_check("every_resolved_image_crops_a_real_rectangle_inside_its_own_atlas",
		bad.is_empty(), ", ".join(bad))
	_check("the_atlases_actually_decoded", checked > 0 and ui.errors.is_empty(),
		"%d crop(s), errors: %s" % [checked, ", ".join(Array(ui.errors))])


## AN UNKNOWN ID GETS NOTHING, AND THE MISS IS RECORDED. This is the property
## that makes "no fabrication" checkable rather than a promise in a comment.
func _check_an_unknown_id_produces_nothing_and_is_recorded(ui) -> void:
	var invented := "NotARetailImageId_OpenBfmeProbe"
	var before: int = ui.missing_images.size()
	_check("an_id_retail_never_defined_produces_no_texture", ui.image(invented) == null)
	_check("and_the_miss_is_recorded_by_name_rather_than_swallowed",
		ui.missing_images.has(invented) and ui.missing_images.size() == before + 1,
		String(ui.missing_images.get(invented, "not recorded")))


## THE PORTRAIT LADDER FOLLOWS RETAIL'S AUTHORED FIELDS AND NOTHING ELSE.
func _check_the_portrait_ladder_uses_only_authored_links(ui) -> void:
	# Rung 1: a PlayerArmy retail recruits gets the image off THAT button.
	var army_key := ""
	var army_image := ""
	var keys: Array[String] = []
	for key in ui.army_portraits.keys():
		keys.append(String(key))
	keys.sort()
	if not keys.is_empty():
		army_key = keys[0]
		army_image = String(ui.army_portraits[army_key])
	var by_army: Dictionary = ui.army_portrait(army_key, "", "")
	_check("an_army_retail_recruits_takes_the_portrait_off_its_own_recruit_button",
		String(by_army.get("id", "")) == army_image
			and String(by_army.get("source", "")) == UiScript.SOURCE_ARMY,
		"%s -> %s [%s]" % [army_key, String(by_army.get("id", "")), String(by_army.get("source", ""))])
	# Rung 2: a hero template retail names gets the image off that hero's button.
	var hero_keys: Array[String] = []
	for key in ui.hero_portraits.keys():
		hero_keys.append(String(key))
	hero_keys.sort()
	var hero_key := hero_keys[0] if not hero_keys.is_empty() else ""
	var by_hero: Dictionary = ui.army_portrait("no such roster", hero_key, "")
	_check("a_hero_army_falls_back_to_the_button_for_its_own_HeroTemplateName",
		String(by_hero.get("source", "")) == UiScript.SOURCE_HERO
			and String(by_hero.get("id", "")) == String(ui.hero_portraits[hero_key]),
		"%s -> %s" % [hero_key, String(by_hero.get("id", ""))])
	# Rung 3: a garrison takes its own template's authored garrison portrait.
	var template := "PlayerMen"
	var garrison := String((ui.player_templates.get(template, {}) as Dictionary).get(
		"garrisonSelectionPortraitName", ""))
	var by_garrison: Dictionary = ui.army_portrait("no such roster", "no such hero", template)
	_check("a_garrison_takes_its_templates_own_GarrisonSelectionPortraitName",
		String(by_garrison.get("id", "")) == garrison
			and String(by_garrison.get("source", "")) == UiScript.SOURCE_GARRISON,
		"%s -> %s" % [template, String(by_garrison.get("id", ""))])
	# Rung 4: nothing authored means NOTHING, not a nearest match.
	var nothing: Dictionary = ui.army_portrait("no such roster", "no such hero", "no such template")
	_check("an_army_retail_authors_no_portrait_for_gets_none_rather_than_a_lookalike",
		String(nothing.get("id", "")).is_empty()
			and String(nothing.get("source", "")) == UiScript.SOURCE_NONE)


## THE RING OFFERS RETAIL'S OWN `AvailableTo` SET AND NOT THE WHOLE GAME.
func _check_buildings_are_filtered_by_retails_own_availableto(ui) -> void:
	var templates: Array[String] = []
	for key in ui.player_templates.keys():
		templates.append(String(key))
	templates.sort()
	var total := 0
	var leaked: Array[String] = []
	for template in templates:
		var rows: Array = ui.buildings_for(template)
		total += rows.size()
		for row in rows:
			if String(row.get("availableTo", "")) != template:
				leaked.append("%s offered %s" % [template, String(row.get("id", ""))])
	_check("no_template_is_offered_a_structure_retail_did_not_mark_available_to_it",
		leaked.is_empty(), ", ".join(leaked))
	_check("every_structure_belongs_to_exactly_one_template_so_the_split_is_a_partition",
		total == ui.building_ids.size(), "%d offered, %d exist" % [total, ui.building_ids.size()])
	_check("a_template_retail_never_declared_is_offered_nothing",
		ui.buildings_for("PlayerNotARetailTemplate").is_empty())


## THE CHROME SHEET'S DERIVED CROPS - the two elvish rings and the two phase
## band strips the strategic HUD draws with. Both accessors work by a STATED
## RULE over the sheet's alpha islands rather than by a hand-written index, so
## what has to hold is that the rule lands on real, distinct rectangles inside
## the sheet - a rule that quietly matched nothing, or matched the same strip
## twice, would photograph as missing or doubled chrome.
func _check_the_chrome_sheet_yields_its_rings_and_bands(ui) -> void:
	var sheet_ok := true
	var detail: Array[String] = []
	for which in ["gold", "red"]:
		var ring: Texture2D = ui.chrome_ring(which)
		var crop := ring as AtlasTexture
		if crop == null or crop.region.size.x <= 0.0 or crop.region.size.y <= 0.0 \
				or not Rect2(Vector2.ZERO, crop.atlas.get_size()).encloses(crop.region):
			sheet_ok = false
			detail.append("%s ring missing or out of the sheet" % which)
	_check("both_elvish_rings_crop_real_rectangles_inside_retails_own_sheet",
		sheet_ok, ", ".join(detail))
	var top := ui.chrome_band("top") as AtlasTexture
	var bottom := ui.chrome_band("bottom") as AtlasTexture
	_check("both_phase_band_strips_crop_real_rectangles_inside_the_sheet",
		top != null and bottom != null
			and top.region.size.x > 0.0 and top.region.size.y > 0.0
			and bottom.region.size.x > 0.0 and bottom.region.size.y > 0.0
			and Rect2(Vector2.ZERO, top.atlas.get_size()).encloses(top.region)
			and Rect2(Vector2.ZERO, bottom.atlas.get_size()).encloses(bottom.region),
		"top %s bottom %s" % [
			str(top.region) if top != null else "null",
			str(bottom.region) if bottom != null else "null"])
	# TWO strips, not one strip twice: the sheet stacks them with transparent
	# rows between, so the split must produce disjoint row ranges.
	_check("the_top_and_bottom_band_strips_are_disjoint_rows_of_the_same_island",
		top != null and bottom != null
			and top.region.position.x == bottom.region.position.x
			and top.region.end.y <= bottom.region.position.y,
		"top %s bottom %s" % [
			str(top.region) if top != null else "null",
			str(bottom.region) if bottom != null else "null"])


## THE SCREEN ACTUALLY BINDS ALL THREE, on a real session over the real document.
## Without this the checks above prove a loader works and prove nothing about the
## screen that is supposed to be using it.
## THE EMPTY-FOUNDATION TILE, WHICH IS ONE ASSET IN TWO HOSTS.
##
## Retail's palantir shows the selected object's portrait in its right-hand lens
## and its build-queue rail shows one card per empty plot, and in retail's own
## oracle capture BOTH carry the same engraved stone tile with the faction's
## device cut into it. A round of this project concluded that face was not in the
## data and drew a blank stone plate in its place; it was in the data, one field
## away from `build_plot_icon`, named for what it MEANS
## (`BuildPlotSelectionPortraitName`) rather than for what it looks like.
##
## These four assertions are what stops that being re-litigated by eye:
##
##   1. Every seat that authors the field resolves it to a REAL CROP. A tile that
##      silently returned null would put the blank disc back with no warning.
##   2. The id the palantir's lens asks for and the id the build cards ask for are
##      the SAME id, taken from the same call, so the two hosts cannot drift.
##   3. No two seats share a tile. Mordor's Eye appearing on Angmar's plots would
##      be exactly the substitution this project refuses, and it would look right.
##   4. The screen reaches it for a region its own active seat holds - i.e. the
##      binding is live, not merely available on the loader.
func _check_the_empty_foundation_tile_is_one_retail_asset(ui) -> void:
	var resolved := 0
	var authored := 0
	var unresolved: Array[String] = []
	var by_id: Dictionary = {}
	var shared: Array[String] = []
	for template_value in ui.player_templates.keys():
		var template := String(template_value)
		var id: String = ui.build_plot_portrait_id(template)
		if id.is_empty():
			continue
		authored += 1
		if by_id.has(id):
			shared.append("%s and %s both take %s" % [String(by_id[id]), template, id])
		by_id[id] = template
		if ui.build_plot_portrait(template) != null:
			resolved += 1
		else:
			unresolved.append("%s -> %s" % [template, id])
	_check("every_seat_that_authors_an_empty_foundation_tile_resolves_it_to_a_real_crop",
		authored >= 7 and resolved == authored,
		"%d of %d authored tile(s) resolved%s" % [resolved, authored,
			"" if unresolved.is_empty() else " :: " + "; ".join(unresolved)])
	_check("no_two_seats_are_ever_given_the_same_empty_foundation_tile",
		shared.is_empty() and by_id.size() == authored,
		"%d distinct tile(s) over %d seat(s)%s" % [by_id.size(), authored,
			"" if shared.is_empty() else " :: " + "; ".join(shared)])

	var found: Dictionary = SessionScript.locate_document([])
	if not bool(found.get("ok", false)):
		_fail("the_palantirs_lens_and_the_build_cards_ask_for_the_same_tile", "no document")
		_fail("the_screen_reaches_that_tile_for_a_region_its_own_seat_holds", "no document")
		return
	var session = _seat(found)
	var screen := ScreenScript.new()
	root.add_child(screen)
	screen.configure(session, [], "", [])
	# ONE CALL SERVES BOTH HOSTS. `_draw_palantir` and `_draw_structure_cards` both
	# take their texture from `_build_plot_tile()`, so asserting that the call is
	# the single source is asserting the two can never disagree.
	var held := ""
	for region_id in session.state.regions_owned_by(session.state.active_player()):
		if int(session.world.region(String(region_id)).get("building_spot_count", 0)) > 0:
			held = String(region_id)
			break
	if not held.is_empty():
		screen.session.hover_region = held
	var tile: Texture2D = screen._build_plot_tile()
	var seat_template := ""
	if not held.is_empty():
		var owner: int = session.state.owner_of(held)
		if owner != session.state.NEUTRAL:
			seat_template = String((session.state.players[owner] as Dictionary).get("template", ""))
	# The crop is compared against the SCREEN'S OWN loader, not this runner's: two
	# loaders over the same bundle mint two AtlasTextures over the same rectangle,
	# and comparing across them would prove nothing about the screen. What is being
	# asserted is that the one call both hosts use returns the seat's own authored
	# id and returns the SAME cached crop every time it is asked.
	var wanted := ""
	if not seat_template.is_empty() and screen.ui != null:
		wanted = String(screen.ui.build_plot_portrait_id(seat_template))
	_check("the_palantirs_lens_and_the_build_cards_ask_for_the_same_tile",
		tile != null and not wanted.is_empty()
			and tile == screen.ui.build_plot_portrait(seat_template)
			and String(tile.resource_name) == wanted
			and wanted == ui.build_plot_portrait_id(seat_template),
		"%s -> %s (crop %s)" % [seat_template, wanted,
			"" if tile == null else String(tile.resource_name)])
	_check("the_screen_reaches_that_tile_for_a_region_its_own_seat_holds",
		not held.is_empty() and tile != null and tile.get_width() > 8 and tile.get_height() > 8,
		"%s, %s" % [held, "no tile" if tile == null else "%dx%d" % [
			tile.get_width(), tile.get_height()]])
	screen.queue_free()


func _check_the_screen_binds_plots_banners_and_a_ring(_ui) -> void:
	var found: Dictionary = SessionScript.locate_document([])
	if not bool(found.get("ok", false)):
		# NOT a silent skip: the liveness count below would catch a skip, so this
		# has to fail loudly instead of quietly running fewer checks.
		_fail("a_living_world_document_is_available_to_bind_the_screen_to",
			String(found.get("reason", "")))
		_fail("the_screen_binds_retails_authored_build_plots", "no document")
		_fail("the_screen_binds_one_banner_per_army_in_the_state", "no document")
		_fail("opening_a_plot_offers_retails_own_structures_and_changes_no_state", "no document")
		return
	_check("a_living_world_document_is_available_to_bind_the_screen_to", true,
		String(found.get("path", "")))
	var session = _seat(found)
	var screen := ScreenScript.new()
	root.add_child(screen)
	screen.configure(session, [], "", [])

	# PLOTS: every region the document authors `BuildingSpot` lines for, and the
	# exact points it authors, with nothing added and nothing moved.
	var plots: Dictionary = screen._plots_by_region()
	var expected_regions := 0
	var mismatched: Array[String] = []
	for region_id in session.world.region_ids:
		var authored: Array = session.world.region(String(region_id)).get("building_spots", []) as Array
		if authored.is_empty():
			continue
		expected_regions += 1
		var bound: Array = plots.get(String(region_id), []) as Array
		if bound.size() != authored.size():
			mismatched.append("%s has %d authored and %d bound" % [
				String(region_id), authored.size(), bound.size()])
			continue
		for index in range(authored.size()):
			var point := bound[index] as Vector2
			var source := authored[index] as Dictionary
			if point.x != float(source.get("x", 0)) or point.y != float(source.get("y", 0)):
				mismatched.append("%s plot %d moved" % [String(region_id), index])
	_check("the_screen_binds_retails_authored_build_plots",
		mismatched.is_empty() and plots.size() == expected_regions,
		"%d region(s) with plots, %s" % [plots.size(),
			"none moved" if mismatched.is_empty() else ", ".join(mismatched)])

	# BANNERS: exactly one stack per army the authoritative state carries.
	var stacks: Dictionary = screen._army_stacks_by_region()
	var counted := 0
	for key in stacks.keys():
		counted += (stacks[key] as Array).size()
	_check("the_screen_binds_one_banner_per_army_in_the_state",
		counted == session.state.armies.size(),
		"%d banner(s), %d army/armies" % [counted, session.state.armies.size()])

	# THE RING: retail's own offer, and NO state change from opening it. The
	# second half is the one that matters - a build menu that quietly mutated the
	# strategic state would desync every other seat.
	var before: String = session.state.state_hash()
	var opened := false
	var offered := 0
	for region_id in session.state.regions_owned_by(session.state.active_player()):
		if int(session.world.region(String(region_id)).get("building_spot_count", 0)) <= 0:
			continue
		screen._on_plot_clicked(String(region_id), 0)
		opened = not screen.selected_plot.is_empty()
		offered = screen._radial_entries().size()
		break
	_check("opening_a_plot_offers_retails_own_structures_and_changes_no_state",
		opened and offered > 0 and session.state.state_hash() == before,
		"opened=%s offered=%d hash %s" % [opened, offered,
			"unchanged" if session.state.state_hash() == before else "MOVED"])
	_check_the_players_surface_speaks_about_the_world(screen, session)
	_check_the_camera_is_free_and_bounded(screen.map3d)
	screen.queue_free()


## THE STRING AUDIT, AS AN ASSERTION RATHER THAN AS A HABIT.
##
## An adversarial blind review identified this screen as the reimplementation
## before it had looked at any art, off ONE string on the status ribbon:
## `nothing here builds, construction is not simulated`. That sentence had
## survived four rounds of people reading this screen, because reading is how it
## was being checked. So the register is checked by a runner now.
##
## THE TWO HALVES ONLY MEAN SOMETHING TOGETHER, and both are asserted here:
##
##   1. NO player-visible string may contain implementation vocabulary. The
##      screen collects them itself (`player_visible_strings()`), including the
##      drawn surfaces that are not Labels and every tooltip, so the sweep cannot
##      miss a surface by forgetting to list it.
##   2. EVERY phrase taken off the glass must be present in the diagnostics panel.
##      Without this half, "pass the audit" would be satisfiable by deleting the
##      honesty `AGENTS.md` requires instead of moving it.
##
## And a third, which is what keeps the first from being vacuous: the diagnostics
## panel MUST contain the banned register. If it did not, the matcher would be
## proving nothing about anything.
##
## The sweep is run over every tab and both selection states, because the ribbon
## and the card change wording per tab and the round-four defect lived on exactly
## one of the three.
func _check_the_players_surface_speaks_about_the_world(screen, session) -> void:
	var offences: Array[String] = []
	var surfaces_seen := 0
	var strings_seen := 0
	# Point at a region the seat can act on, so the ribbon, the card and the
	# palantir captions are all populated rather than in their empty state.
	var staging: PackedStringArray = session.staging_regions()
	for selection in ["", String(staging[0]) if not staging.is_empty() else ""]:
		if not selection.is_empty():
			screen._on_region_hovered(selection)
			screen.select_region(selection)
		for tab in ["territory", "armies", "structures"]:
			screen._on_tab_pressed(tab)
			screen.refresh()
			var surfaces: Dictionary = screen.player_visible_strings()
			surfaces_seen = maxi(surfaces_seen, surfaces.size())
			for key in surfaces.keys():
				var text := String(surfaces[key]).to_lower()
				if text.strip_edges().is_empty():
					continue
				strings_seen += 1
				for banned_value in screen.IMPLEMENTATION_VOCABULARY:
					var banned := String(banned_value)
					if text.contains(banned):
						var offence := "[%s tab, %s] %s :: \"%s\"" % [
							tab, key, banned, String(surfaces[key])]
						if not offences.has(offence):
							offences.append(offence)
	_check("no_player_visible_string_on_this_screen_carries_implementation_vocabulary",
		offences.is_empty() and surfaces_seen >= 12 and strings_seen > 0,
		"%d surface(s), %d non-empty string(s)%s" % [surfaces_seen, strings_seen,
			"" if offences.is_empty() else " :: " + "; ".join(offences)])

	# THE MATCHER IS NOT VACUOUS. The diagnosis is written in exactly the register
	# the HUD is forbidden, so if the sweep above cannot find a banned word HERE,
	# it was not capable of finding one anywhere.
	var diagnosis := String(screen.diagnostics_text()).to_lower()
	var found_in_diagnosis: Array[String] = []
	for banned_value in screen.IMPLEMENTATION_VOCABULARY:
		if diagnosis.contains(String(banned_value)):
			found_in_diagnosis.append(String(banned_value))
	_check("the_same_sweep_finds_that_register_all_over_the_diagnostics_panel",
		found_in_diagnosis.size() >= 8,
		"%d banned term(s) present in the diagnosis: %s" % [
			found_in_diagnosis.size(), ", ".join(found_in_diagnosis.slice(0, 8))])

	# EVERY GAP TAKEN OFF THE GLASS ARRIVED. These are the exact phrases this
	# round removed from the HUD, asserted by their anchor wording rather than by
	# a count, so deleting one from `_strings_taken_off_the_glass` reddens the run.
	# THIS LIST IS RESTATED, NOT SHORTENED, and the reason is on the record.
	#
	# It used to require the diagnosis to carry "construction is not simulated",
	# "nothing ever gets built" and "the status ribbon no longer says it". All three
	# were true and none of them is any more: construction IS simulated, structures
	# stand on numbered foundations, and the `0 of N built` counter's numerator is
	# live. An absence list that keeps reporting a closed gap is a diagnosis lying in
	# the safe direction, so those three were deleted from
	# `_strings_taken_off_the_glass` - and the assertion is REPLACED with the one
	# that guards what took their place rather than removed, because the bargain
	# itself has not changed: what comes OFF the glass has to arrive here.
	#
	# What comes off the glass now is the strategic layer's own wording of every
	# build refusal - it names seat indices, `LWB_*` ids and the macro table - and
	# the statement of which refusal English is this project's rather than retail's.
	# `_check_the_screen_can_raise_a_structure` holds the other side of this: that
	# the three retired phrases are GONE.
	var must_carry := [
		"project-authored wording",
		"controlbar:lw_fortrestricted",
		"which no converted bundle carries",
	]
	var missing: Array[String] = []
	for phrase_value in must_carry:
		if not diagnosis.contains(String(phrase_value)):
			missing.append(String(phrase_value))
	_check("every_gap_this_screen_took_off_the_hud_is_stated_in_full_in_the_diagnosis",
		missing.is_empty(), "missing: %s" % "; ".join(missing))

	# THE RIBBON'S TRIM NEVER LEAVES A LIST JOIN HANGING. The round-four capture
	# ended `Dark Iron Forge, ...` - a comma from `", ".join(...)` sitting in front
	# of three full stops, which reads as a raw join leaking into the UI. The
	# ribbon is driven to a rail far too narrow for its line and the tail is read
	# back off what was actually drawn.
	screen._on_tab_pressed("structures")
	screen.refresh()
	var trimmed := String(screen.trimmed_ribbon_text(90.0))
	_check("a_ribbon_too_long_for_its_rail_is_cut_to_a_word_and_never_to_a_separator",
		trimmed.ends_with(screen.RIBBON_ELLIPSIS)
			and not screen.RIBBON_TAIL_TRIM.contains(
				trimmed.substr(maxi(trimmed.length() - 1 - screen.RIBBON_ELLIPSIS.length(), 0), 1))
			and not trimmed.contains("..."),
		"\"%s\"" % trimmed)

	# ------------------------------------------------------------------------------
	# THE STRUCTURES WELL IS A TABLE, NOT A RUN OF LINES
	# ------------------------------------------------------------------------------
	#
	# The single most damaging element a blind adversarial review found on this
	# screen was the structures roster rendered as a bare left-aligned list of names
	# in the tray's text control: "flat white sans, no frame, no icons, no baseline
	# grid, no relationship to the cards it sits beside ... that is a data structure
	# being printed, not a UI being drawn". One second on it and the review's round
	# was, in its own words, effectively over.
	#
	# The fix is not a style, which is why it is worth a check: the roster is no
	# longer TEXT AT ALL. It is rows (`structure_roster`) drawn by
	# `_draw_structure_roster` as a framed table with retail's own
	# `ConstructButtonImage` per row and its cost right-aligned in its own column,
	# and the tray's card carries nothing on that tab. A future refactor that
	# quietly put the names back into the card would restore the whole defect
	# without failing any other assertion here.
	#
	# It lives in THIS runner rather than beside the other containment rules in
	# `wotr_region_card_runner` because it needs a bound session: with no session
	# the card legitimately carries the "War of the Ring is unavailable" notice, and
	# a check that passed on that would be proving nothing.
	screen._on_tab_pressed("structures")
	if not staging.is_empty():
		screen._on_region_hovered(String(staging[0]))
		screen.select_region(String(staging[0]))
	screen.refresh()
	_check("the_structures_tab_sets_no_text_in_the_trays_own_card",
		String(screen.detail_label.text).strip_edges().is_empty(),
		"card carries \"%s\"" % String(screen.detail_label.text).left(60))
	# AND THE ROSTER IS REAL - retail's own offerings, with retail's own icons.
	var roster: Array = screen.structure_roster
	var with_icons := 0
	for row_value in roster:
		if not String((row_value as Dictionary)["image_id"]).is_empty():
			with_icons += 1
	_check("the_drawn_roster_carries_retails_own_offerings_and_their_own_icons",
		not roster.is_empty() and with_icons == roster.size(),
		"%d offering(s), %d with an authored icon" % [roster.size(), with_icons])
	# AND IT IS STILL A PLAYER SURFACE THE AUDIT CAN READ. Drawing a surface instead
	# of setting it on a control must never take it out of the sweep that guards
	# this screen's register - that would be trading one defect for a blind spot.
	var drawn_surfaces: Dictionary = screen.player_visible_strings()
	var roster_titles := 0
	for key in drawn_surfaces.keys():
		if String(key).begins_with("structure roster "):
			roster_titles += 1
	_check("the_drawn_roster_is_still_visible_to_the_player_string_audit",
		roster_titles >= roster.size() + 1,
		"%d roster surface(s) for %d row(s)" % [roster_titles, roster.size()])


## THE CAMERA THE OWNER ASKED FOR: "zoom around the 3d map and zoom way in and
## out like in a regular skirmish match".
##
## Four of these are the freedoms, and two are the properties commit a1e7b2e
## established that widening the camera must not break - the fit is against the
## viewport's own aspect AND the pitched depth, and a resize re-fits WITHOUT
## discarding where the player had panned, zoomed or turned to. Those two are the
## reason this section exists at all: a camera change that silently reverted the
## framing on the next resize would look fine in every screenshot.
const MapViewScript = preload("res://src/wotr/wotr_map_view.gd")

## THE APT RENDERER'S THREE HONESTY RULES, on synthetic frames rather than on the
## bundle, so they hold whether or not a strategic bundle is converted here.
##
## 1. A `clipDepth` display-list entry is a MASK. Retail never draws one; it
##    clips with it. The flattener emits masks as triangles anyway (its own
##    `clip-mask-not-converted` gap), and they are authored in Flash's mask
##    palette - flat green, flat purple - so a renderer that drew them verbatim
##    would paint bright green rectangles over retail's gold. It must drop them
##    AND keep them out of the frame's measured bounds.
## 2. The draws a mask governs are SCISSORED to it, not passed through.
## 3. A textured draw whose atlas is unavailable draws NOTHING and is counted. A
##    substitute texture would be procedural art wearing retail's UVs.
func _check_the_apt_renderer_honours_retails_masks_and_substitutes_nothing() -> void:
	var HudScript = load("res://src/wotr/wotr_hud_chrome.gd")
	# A mask at depth 2 governing depths 3..5, a masked art draw at depth 4 that
	# runs well outside it, and an unmasked art draw at depth 7.
	var frame := {
		"frameIndex": 0,
		"displayList": [
			{"depth": 2, "clipDepth": 5}, {"depth": 4, "clipDepth": 0},
			{"depth": 7, "clipDepth": 0},
		],
		"draws": [
			{"kind": "solid-triangle", "path": "screen:Probe:frame:0/2/1",
				"color": [0.2, 0.8, 0.2, 1.0],
				"points": [[0.0, 0.0], [100.0, 0.0], [100.0, 100.0]]},
			{"kind": "solid-triangle", "path": "screen:Probe:frame:0/4/1",
				"color": [1.0, 1.0, 1.0, 1.0],
				"points": [[0.0, 0.0], [400.0, 0.0], [0.0, 400.0]]},
			{"kind": "textured-triangle", "path": "screen:Probe:frame:0/7/1",
				"atlas": "no/such/atlas.png", "color": [1.0, 1.0, 1.0, 1.0],
				"points": [[10.0, 10.0], [20.0, 10.0], [10.0, 20.0]],
				"uvs": [[0.0, 0.0], [1.0, 0.0], [0.0, 1.0]]},
		],
	}
	var masks: Dictionary = HudScript.apt_clip_masks(frame)
	_check("a_clipDepth_entry_is_read_as_a_mask_with_the_rectangle_its_own_shape_covers",
		masks.size() == 1 and masks.has(2)
			and int((masks[2] as Dictionary)["to"]) == 5
			and ((masks[2] as Dictionary)["rect"] as Rect2).is_equal_approx(
				Rect2(0.0, 0.0, 100.0, 100.0)),
		str(masks))
	var bounds: Rect2 = HudScript.apt_frame_bounds(frame)
	_check("a_masks_own_triangles_are_excluded_from_the_frames_measured_bounds",
		bounds.is_equal_approx(Rect2(0.0, 0.0, 400.0, 400.0)), str(bounds))
	# The renderer needs a real CanvasItem to emit into; a bare Control in the
	# tree is enough and it is freed straight after.
	var canvas := Control.new()
	root.add_child(canvas)
	# The scale is PER-AXIS now, because retail's own composition is (see
	# `wotr_screen.APT_STRETCH_MEASUREMENT`); `Vector2.ONE` is this probe asking
	# for retail's authored coordinates unchanged, which is what it was asking for
	# before with `1.0`.
	var report: Dictionary = HudScript.draw_apt_frame(
		canvas, frame, Vector2.ZERO, Vector2.ONE, null)
	canvas.queue_free()
	_check("the_mask_is_dropped_the_draw_it_governs_is_scissored_and_a_missing_atlas_draws_nothing",
		int(report["masked"]) == 1 and int(report["clipped"]) == 1
			and int(report["skipped"]) == 1 and int(report["drawn"]) == 1,
		str(report))

	# SUPPRESSING AN EMPTY RUNTIME HOST TAKES THAT HOST AND NOTHING NEXT TO IT.
	#
	# The palantir suppresses two of retail's engine-filled hosts by their authored
	# paths - `...frame:1/3` (the sub-glass) and `...frame:1/35/9` (commandUI's
	# backdrop) - so that a carved seat can be drawn where retail composites a
	# picture. A SUBSTRING rule would be catastrophic there and quietly so: `1/3`
	# is a prefix of `1/35/9/1`, so asking for the sub-glass alone would silently
	# take the whole command host with it and the button chain would lose its
	# backing. The rule is therefore whole-path-segment, and this is the probe that
	# holds it: two draws whose paths are exactly that pair, one suppressed.
	var siblings := {
		"frameIndex": 0,
		"displayList": [{"depth": 3, "clipDepth": 0}, {"depth": 35, "clipDepth": 0}],
		"draws": [
			{"kind": "solid-triangle", "path": "screen:Probe:frame:1/3",
				"color": [0.0, 0.0, 0.0, 1.0],
				"points": [[0.0, 0.0], [10.0, 0.0], [0.0, 10.0]]},
			{"kind": "solid-triangle", "path": "screen:Probe:frame:1/35/9/1",
				"color": [0.0, 0.0, 0.0, 1.0],
				"points": [[0.0, 0.0], [20.0, 0.0], [0.0, 20.0]]},
		],
	}
	var probe := Control.new()
	root.add_child(probe)
	var only_sub_glass: Dictionary = HudScript.draw_apt_frame(
		probe, siblings, Vector2.ZERO, Vector2.ONE, null, Vector2i(0, 0),
		["screen:Probe:frame:1/3"])
	var both: Dictionary = HudScript.draw_apt_frame(
		probe, siblings, Vector2.ZERO, Vector2.ONE, null, Vector2i(0, 0),
		["screen:Probe:frame:1/3", "screen:Probe:frame:1/35/9"])
	probe.queue_free()
	_check("suppressing_one_authored_host_never_takes_the_sibling_whose_path_it_prefixes",
		int(only_sub_glass["suppressed"]) == 1 and int(only_sub_glass["drawn"]) == 1
			and int(both["suppressed"]) == 2 and int(both["drawn"]) == 0,
		"one path -> %s, two paths -> %s" % [str(only_sub_glass), str(both)])


func _check_the_camera_is_free_and_bounded(view) -> void:
	if view == null or not view.has_map():
		_fail("the_zoom_range_spans_at_least_twenty_times", "no map bundle")
		_fail("zoom_is_clamped_to_its_stated_range", "no map bundle")
		_fail("pitch_is_adjustable_and_clamped", "no map bundle")
		_fail("yaw_turns_the_camera_and_wraps", "no map bundle")
		_fail("panning_cannot_carry_the_map_off_the_panel", "no map bundle")
		_fail("a_resize_refits_the_distance_without_discarding_the_players_framing",
			"no map bundle")
		return
	var span := MapViewScript.MAX_ZOOM / MapViewScript.MIN_ZOOM
	_check("the_zoom_range_spans_at_least_twenty_times", span >= 20.0,
		"%.1fx (%.2f..%.2f)" % [span, MapViewScript.MIN_ZOOM, MapViewScript.MAX_ZOOM])

	view.focus_region("", 0.0001)
	var floored := float(view.camera_state()["zoom"])
	view.focus_region("", 99.0)
	var ceiled := float(view.camera_state()["zoom"])
	# THE CEILING IS SOLVED PER FRAME, NOT A CONSTANT, and this check now pins BOTH
	# halves of that. `MAX_ZOOM` alone was wrong at every aspect at or above about
	# 1.35: at the 1860x800 shape this lane photographs, pulling back to the flat
	# constant put retail's own terrain slab edge (x = -2784, its
	# `terrain_extent.x_min`) on screen. `zoom_ceiling()` solves for the furthest
	# pull-back whose four panel corners all still land inside retail's footprint
	# at the LIVE aspect, pitch, yaw and pan. So the clamp must land on that
	# computed ceiling AND the ceiling must never escape the advertised constant -
	# strictly more than "the clamp equals MAX_ZOOM" asserted.
	var ceiling := float(view.zoom_ceiling())
	_check("zoom_is_clamped_to_its_stated_range",
		is_equal_approx(floored, MapViewScript.MIN_ZOOM)
			and is_equal_approx(ceiled, ceiling)
			and ceiling <= MapViewScript.MAX_ZOOM + 0.0001,
		"%.3f and %.3f (computed ceiling %.3f, stated MAX_ZOOM %.3f)" % [
			floored, ceiled, ceiling, MapViewScript.MAX_ZOOM])

	view.set_orbit(0.0, -1000.0)
	var low := float(view.camera_state()["pitch"])
	view.set_orbit(0.0, 1000.0)
	var high := float(view.camera_state()["pitch"])
	_check("pitch_is_adjustable_and_clamped",
		is_equal_approx(low, MapViewScript.MIN_PITCH_DEGREES)
			and is_equal_approx(high, MapViewScript.MAX_PITCH_DEGREES)
			and low != high,
		"%.1f..%.1f degrees" % [low, high])

	view.set_orbit(2.0, MapViewScript.DEFAULT_PITCH_DEGREES)
	var turned := float(view.camera_state()["yaw"])
	view.set_orbit(TAU + 0.25, MapViewScript.DEFAULT_PITCH_DEGREES)
	var wrapped := float(view.camera_state()["yaw"])
	_check("yaw_turns_the_camera_and_wraps",
		is_equal_approx(turned, 2.0) and absf(wrapped - 0.25) < 0.001,
		"%.3f then %.3f" % [turned, wrapped])

	# PAN IS BOUNDED. A drag long enough to cross the map several times must not
	# be able to leave the map's own footprint plus its stated margin behind.
	var extent: Dictionary = view.bundle.terrain_extent
	var width := float(extent["x_max"]) - float(extent["x_min"])
	var depth := float(extent["y_max"]) - float(extent["y_min"])
	for _step in range(60):
		view._pan(Vector2(-4000.0, -4000.0))
	var runaway: Vector3 = view.camera_state()["target"]
	var margin_x := width * MapViewScript.PAN_MARGIN_FRACTION
	var margin_y := depth * MapViewScript.PAN_MARGIN_FRACTION
	var low_corner: Vector3 = view.BundleScript.world_to_godot(
		float(extent["x_min"]) - margin_x, float(extent["y_min"]) - margin_y, 0.0)
	var high_corner: Vector3 = view.BundleScript.world_to_godot(
		float(extent["x_max"]) + margin_x, float(extent["y_max"]) + margin_y, 0.0)
	var inside := (
		runaway.x >= minf(low_corner.x, high_corner.x) - 0.001
		and runaway.x <= maxf(low_corner.x, high_corner.x) + 0.001
		and runaway.z >= minf(low_corner.z, high_corner.z) - 0.001
		and runaway.z <= maxf(low_corner.z, high_corner.z) + 0.001)
	_check("panning_cannot_carry_the_map_off_the_panel", inside, str(runaway))

	# THE PROPERTY a1e7b2e ESTABLISHED, at a framing that is NOT the default one.
	#
	# 0.31 WAS 0.20 BECAUSE THE CEILING IS NOW SOLVED PER ASPECT. The map view's
	# reachable pull-back is `min(MAX_ZOOM, zoom_ceiling())`, and the ceiling is
	# computed against the LIVE aspect, pitch, yaw and pan. At pitch -21 the
	# ceiling is 0.291 on this panel and 0.237 on the halved one, so a request of
	# 0.31 is clamped to the ceiling at BOTH - and a zoom sitting ON an
	# aspect-dependent ceiling MUST move when the aspect changes. That is the
	# clamp doing its job, not the player's framing being discarded, so asserting
	# it here was asserting the wrong thing. 0.20 is inside the reachable range at
	# both shapes, which is what makes "the framing was kept" a real claim.
	#
	# The half this weakened is restored immediately below rather than dropped.
	view.reset_camera()
	view.set_orbit(1.1, -21.0)
	view.focus_region("", 0.20)
	var before: Dictionary = view.camera_state()
	view.size = Vector2(view.size.x * 0.5, view.size.y * 0.75)
	view._on_resized()
	var after: Dictionary = view.camera_state()
	var kept := (
		is_equal_approx(float(before["zoom"]), float(after["zoom"]))
		and is_equal_approx(float(before["yaw"]), float(after["yaw"]))
		and is_equal_approx(float(before["pitch"]), float(after["pitch"]))
		and (before["target"] as Vector3).is_equal_approx(after["target"] as Vector3))
	var refitted := not is_equal_approx(
		float(before["distance"]), float(after["distance"]))
	_check("a_resize_refits_the_distance_without_discarding_the_players_framing",
		kept and refitted,
		"zoom/yaw/pitch/target %s, distance %.1f -> %.1f" % [
			"kept" if kept else "LOST", float(before["distance"]), float(after["distance"])])

	# AND THE OTHER HALF, which the check above used to cover by accident and now
	# covers deliberately: a zoom that IS pinned to the ceiling must resolve to
	# the NEW aspect's ceiling after a resize - not stay at the old value, and not
	# land on some arbitrary number. Without this, moving the framing case to 0.20
	# would have left the clamped path untested.
	view.focus_region("", 99.0)
	var at_ceiling := float(view.camera_state()["zoom"])
	view.size = Vector2(view.size.x * 2.0, view.size.y / 0.75)
	view._on_resized()
	var resolved := float(view.camera_state()["zoom"])
	_check("a_zoom_pinned_to_the_ceiling_resolves_to_the_new_aspects_ceiling",
		is_equal_approx(resolved, float(view.zoom_ceiling()))
			and not is_equal_approx(resolved, at_ceiling),
		"%.4f -> %.4f (ceiling %.4f)" % [
			at_ceiling, resolved, float(view.zoom_ceiling())])


## ------------------------------------------------------------------------------
## THE HUMAN CAN TAKE NEUTRAL GROUND, WITH THE BUTTON, ON A REAL SESSION
## ------------------------------------------------------------------------------
##
## THE BUG THIS PINS was the owner's loudest complaint: 49 of 52 regions still
## neutral at turn 14 and no way to expand. The strategic layer learned to model
## retail's claim - march a hero army into unowned country
## (`STRATEGICHUD:MoveHeroArmiesChecklistItem`: "Conquer new territories by moving
## Hero armies into adjacent territories") - but `wotr_screen.can_attack_now()`
## still ended with `return not _target_is_unclaimed()`, so the one control that
## reaches it stayed grey on every neutral region on the board.
##
## IT IS ASSERTED THROUGH THE SCREEN, not through the session. The session's own
## runners already prove the claim rule; what could regress here is the SURFACE -
## a re-added guard, a caption that lies about its verb, a `battle_committed`
## signal fired for a battle that does not exist, or the conquered string being
## reused for an event retail gives its own string to.
##
## THE SESSION DELIBERATELY HAS NO AUTO-RESOLVE BUNDLES. `_seat()` never calls
## `load_auto_resolve()`, so every army in it fields zero units. Taking unowned
## ground must still work: nothing defends it, nothing rolls, and a claim that
## quietly needed combat data would fail on exactly the checkouts that have none.
func _check_the_screen_can_take_neutral_ground() -> void:
	var found: Dictionary = SessionScript.locate_document([])
	if not bool(found.get("ok", false)):
		for what in ["neutral_ground_is_reachable_from_the_human_seats_own_regions",
				"the_command_rail_is_live_on_neutral_ground",
				"the_button_renames_itself_from_attack_to_take",
				"committing_neutral_ground_returns_a_claim_and_no_battle_commitment",
				"the_region_changes_hands",
				"the_player_is_shown_retails_own_taken_notice_and_not_its_conquered_one",
				"no_battle_report_is_opened_for_an_event_where_nobody_fought",
				"the_resolution_names_itself_a_claim_and_rolled_no_dice"]:
			_fail(what, "no document")
		return
	var session = _seat(found)
	var screen := ScreenScript.new()
	root.add_child(screen)
	screen.configure(session, [], "", [])

	# WALK THE WAR FORWARD UNTIL A CLAIM IS ON OFFER, exactly as the probe run did
	# with the mouse: retail seats both sides deep inside their own territory, so
	# on turn one the human's only armed region may touch nothing but the enemy.
	# Marching one region a turn is what a player does about that. BOUNDED, and a
	# failure to find one in the bound is a failure rather than a skip.
	var stage_id := ""
	var claim_id := ""
	for _round in range(12):
		for region_id in session.staging_regions():
			var claims: PackedStringArray = session.claim_targets(String(region_id))
			if not claims.is_empty():
				stage_id = String(region_id)
				claim_id = String(claims[0])
				break
		if not claim_id.is_empty():
			break
		var march: Dictionary = _step_towards_unowned_ground(session)
		var stepped := not march.is_empty() and bool(session.move_armies(
			String(march["from"]), String(march["to"])).get("ok", false))
		# Past the AI seat and back to the human, without running the opponent: this
		# check is about the human's control, and an opponent moving the board under
		# it would make the assertions below depend on its dice.
		session.state.advance_turn()
		while session.active_seat_is_ai():
			session.state.advance_turn()
		if not stepped:
			break
	_check("neutral_ground_is_reachable_from_the_human_seats_own_regions",
		not claim_id.is_empty(),
		"%s can take %s" % [stage_id, claim_id] if not claim_id.is_empty()
			else "no seat-owned region ever touched unowned ground")
	if claim_id.is_empty():
		for what in ["the_command_rail_is_live_on_neutral_ground",
				"the_button_renames_itself_from_attack_to_take",
				"committing_neutral_ground_returns_a_claim_and_no_battle_commitment",
				"the_region_changes_hands",
				"the_player_is_shown_retails_own_taken_notice_and_not_its_conquered_one",
				"no_battle_report_is_opened_for_an_event_where_nobody_fought",
				"the_resolution_names_itself_a_claim_and_rolled_no_dice"]:
			_fail(what, "no claimable neutral region was reached")
		return

	var seat: int = session.state.active_player()
	# THE SCREEN IS REFRESHED FIRST. `select_region()` tests the region against the
	# staging list the LAST refresh built, and the walk above moved the armies
	# several times since `configure()` ran.
	screen.refresh()
	screen.select_region(stage_id)
	screen.select_target(claim_id)
	screen.refresh()
	_check("the_command_rail_is_live_on_neutral_ground",
		screen.can_attack_now() and not screen.attack_button.disabled,
		"can_attack_now=%s disabled=%s" % [
			str(screen.can_attack_now()), str(screen.attack_button.disabled)])
	# THE VERB. Retail ships no control text for either state - its living world
	# has no ATTACK button, a move is a drag - so both words are this project's,
	# and TAKE is retail's own verb for the event from the notice it raises.
	_check("the_button_renames_itself_from_attack_to_take",
		screen.attack_button.text == "TAKE",
		"caption reads '%s'" % screen.attack_button.text)

	var committed: Dictionary = screen.commit_selected_attack()
	_check("committing_neutral_ground_returns_a_claim_and_no_battle_commitment",
		bool(committed.get("ok", false)) and session.state.pending_battle.is_empty()
			and session.state.pending_claim.is_empty(),
		"ok=%s pending_battle=%d pending_claim=%d" % [
			str(committed.get("ok", false)), session.state.pending_battle.size(),
			session.state.pending_claim.size()])
	_check("the_region_changes_hands",
		session.state.owner_of(claim_id) == seat,
		"%s is held by seat %d (was nobody)" % [claim_id, session.state.owner_of(claim_id)])

	# RETAIL'S OWN NOTICE, and the RIGHT one. `data/lotr.str` ships
	# `APT:LivingWorldRegionTakenNotice` = "%s taken!" as a DIFFERENT string from
	# `APT:LivingWorldRegionConqueredNotice` = "%s conquered!"; reusing one word for
	# both would lose a distinction retail itself authored.
	var strip: String = screen.message_label.text
	var region_name: String = screen._display_of(claim_id)
	_check("the_player_is_shown_retails_own_taken_notice_and_not_its_conquered_one",
		strip.contains(region_name) and strip.contains("taken")
			and not strip.contains("conquered"),
		"strip reads '%s'" % strip)
	_check("no_battle_report_is_opened_for_an_event_where_nobody_fought",
		not screen.report_backdrop.visible and screen.last_auto_resolve.is_empty(),
		"report visible=%s, last_auto_resolve %s" % [
			str(screen.report_backdrop.visible),
			"empty" if screen.last_auto_resolve.is_empty() else "SET"])
	# NOTHING WAS ROLLED. The resolution names itself a claim and carries no seed,
	# which is how a reader tells "marched into empty country" from "won a fight"
	# in the record rather than in the prose.
	var outcome: Dictionary = committed.get("outcome", {}) as Dictionary
	_check("the_resolution_names_itself_a_claim_and_rolled_no_dice",
		String(outcome.get("kind", "")) == "region_claimed"
			and String(committed.get("seed", "x")).is_empty(),
		"kind '%s', seed '%s'" % [
			String(outcome.get("kind", "")), String(committed.get("seed", "x"))])
	screen.queue_free()


## THE FIRST STEP OF THE SHORTEST PATH, over regions the active seat owns, from
## an armed region to one that touches unowned ground. Breadth-first, so it is the
## SHORTEST such path and the walk above terminates; picking the first movement
## target instead makes the armies wander between two interior regions forever,
## which is how this check first failed.
func _step_towards_unowned_ground(session) -> Dictionary:
	var seat: int = session.state.active_player()
	for start in session.staging_regions():
		var from_id := String(start)
		var frontier: Array = [[from_id, ""]]
		var seen: Dictionary = {from_id: true}
		while not frontier.is_empty():
			var entry: Array = frontier.pop_front()
			var here := String(entry[0])
			var first := String(entry[1])
			for neighbour in session.world.neighbours(here):
				var next_id := String(neighbour)
				if session.state.owner_of(next_id) == session.state.NEUTRAL and not first.is_empty():
					return {"from": from_id, "to": first}
				if seen.has(next_id) or session.state.owner_of(next_id) != seat:
					continue
				seen[next_id] = true
				frontier.append([next_id, first if not first.is_empty() else next_id])
	return {}


func _seat(found: Dictionary):
	var document: Dictionary = found["document"] as Dictionary
	var world = load("res://src/wotr/wotr_world.gd").new()
	world.load_from_dict(document, "")
	var probe := SessionScript.new()
	probe.world = world
	var availability: Dictionary = {}
	for pack_faction in SessionScript.FACTION_BINDINGS.values():
		availability[String(pack_faction)] = ""
	var seats: Array = []
	for option in probe.seat_options(availability):
		seats.append({
			"template": String(option["template"]),
			"team": seats.size() + 1,
			"controller": "human" if seats.is_empty() else "ai",
		})
		if seats.size() == 2:
			break
	var session := SessionScript.new()
	session.begin(document, world.campaign_name, String(probe.startable_scenarios(2)[0]), seats)
	session.document_path = String(found["path"])
	session.document_source = String(found["source"])
	return session


func _check_live_phase_resume_and_abandon() -> void:
	var menu_source := FileAccess.get_file_as_string("res://src/ui/main_menu.gd")
	_check("main_menu_resume_seam_gates_opponents_on_authoritative_clean_tactical",
		menu_source.contains("The result was refused and rolled back; the battle remains")
			and menu_source.contains("if clean_tactical:")
			and menu_source.contains("session.state.pending_retreats.is_empty()"))
	var found: Dictionary = SessionScript.locate_document([])
	if not bool(found.get("ok", false)):
		var reason := String(found.get("reason", "living-world document unavailable"))
		for label in [
			"live_phase_clock_reads_authoritative_tactical",
			"main_menu_resume_seam_waits_for_clean_tactical",
			"battle_cancel_surface_becomes_abandon",
			"abandon_handler_returns_authoritative_clean_tactical",
			"abandon_handler_names_its_result",
		]:
			_check(String(label), false, reason)
		return
	var session = _seat(found)
	# Keep the handler observation local: an AI turn would replace the named
	# abandon message with its narrative after the authoritative transition.
	for row in session.state.players:
		(row as Dictionary)["controller"] = session.state.CONTROLLER_HUMAN
	var screen := ScreenScript.new()
	root.add_child(screen)
	screen.configure(session, [], "", [])
	_check("live_phase_clock_reads_authoritative_tactical",
		screen.current_phase() == screen.PHASE_TACTICAL)
	session.state.phase = session.state.PHASE_RETREAT
	screen.run_opponent_turns()
	_check("main_menu_resume_seam_waits_for_clean_tactical",
		screen.message_label.text.contains("Finish the current phase"))
	session.state.phase = session.state.PHASE_BATTLE
	session.state.pending_battle = {"region": "UIAbandonProbe"}
	screen.refresh()
	_check("battle_cancel_surface_becomes_abandon",
		screen.current_phase() == screen.PHASE_BATTLE and screen.cancel_button.text == "ABANDON")
	screen._on_cancel_pressed()
	_check("abandon_handler_returns_authoritative_clean_tactical",
		session.state.phase == session.state.PHASE_TACTICAL and session.state.pending_battle.is_empty())
	_check("abandon_handler_names_its_result",
		screen.message_label.text.contains("Battle abandoned"))
	screen.queue_free()


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
