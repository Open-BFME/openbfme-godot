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

## 28 checks over the UI bundle plus 6 over the camera. Stated rather than
## counted by hand at the bottom: the liveness assertion below compares against
## this, so a function that aborts before its assertions reddens the run.
const CHECKS_WITH_BUNDLE := 34
const CHECKS_WITHOUT_BUNDLE := 4

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
		_check_the_screen_binds_plots_banners_and_a_ring(ui)

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


## THE SCREEN ACTUALLY BINDS ALL THREE, on a real session over the real document.
## Without this the checks above prove a loader works and prove nothing about the
## screen that is supposed to be using it.
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
	_check_the_camera_is_free_and_bounded(screen.map3d)
	screen.queue_free()


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
	_check("zoom_is_clamped_to_its_stated_range",
		is_equal_approx(floored, MapViewScript.MIN_ZOOM)
			and is_equal_approx(ceiled, MapViewScript.MAX_ZOOM),
		"%.3f and %.3f" % [floored, ceiled])

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
	view.reset_camera()
	view.focus_region("", 0.31)
	view.set_orbit(1.1, -21.0)
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
