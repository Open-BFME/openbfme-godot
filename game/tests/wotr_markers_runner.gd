extends SceneTree

## RETAIL'S 3D LIVING-WORLD MARKERS, checked against retail's own documents.
##
## This runner exists because the claim this lane makes - "the flat plates and
## rings are gone and retail's own models are standing on the map" - is a claim
## about RESOLUTION and PLACEMENT, and both can fail into something that still
## photographs well:
##
##   1. a marker family can quietly fall back to another faction's banner;
##   2. a `SubObjects` filter can quietly stop filtering, so a slot that should
##      show a staff and a flag shows the flag, the staff and three size pips at
##      once;
##   3. a model can quietly fail to convert and the screen still draws a marker,
##      because the flat plate is still there underneath;
##   4. the flat plate and the model can BOTH be drawn, which looks like a
##      slightly heavy marker and is actually two markers;
##   5. the whole set can survive at one zoom and be behind the terrain at
##      another.
##
## Every one of those is asserted here, against the bundle and the living-world
## document rather than against a picture.
##
## IT IS A SEPARATE RUNNER ON PURPOSE, for the same reason
## `wotr_living_world_ui_runner` is: `wotr_map3d` has stated floors of 29 and 14
## and `wotr_living_world_ui` of 34 and 4. Folding these in would have moved
## three numbers that other people rely on. This runner carries its own.
##
## LIVENESS. The expected check count is asserted, so a function that aborts
## before its assertions fails the run instead of silently shrinking it.
##
## Usage:
##   Godot_v4.7 --headless --path game --script tests/wotr_markers_runner.gd
## With no bundle present it still runs its unbound checks and says so; that is
## the 5/5 mode.

const MapViewScript = preload("res://src/wotr/wotr_map_view.gd")
const MarkerScript = preload("res://src/wotr/wotr_marker_models.gd")
const UiScript = preload("res://src/wotr/wotr_living_world_ui.gd")
const SessionScript = preload("res://src/wotr/wotr_session.gd")
const ScreenScript = preload("res://src/ui/wotr_screen.gd")
const RegionGeometryScript = preload("res://src/wotr/wotr_region_geometry.gd")

## RETAIL'S OWN COUNTS, measured off the shipped documents rather than chosen:
## `data/ini/livingworldicons/*.ini` carry 40 `LivingWorldArmyIcon` blocks,
## `livingworldbuildingicons.ini` 28 `LivingWorldBuildingIcon` and
## `livingworldbuildploticons.ini` 7 `LivingWorldBuildPlotIcon`. Between them
## they declare 506 `Object` slots naming 81 DISTINCT W3D models (43 named by
## army icons, 31 by building icons, 9 by build-plot icons - three models are
## shared across families, which is why those three sum to 83 and not 81).
const RETAIL_ARMY_FAMILIES := 40
const RETAIL_BUILDING_FAMILIES := 28
const RETAIL_BUILD_PLOT_FAMILIES := 7
const RETAIL_SLOTS := 506
const RETAIL_MODELS := 81

## One family whose whole slot table is asserted field by field, so a converter
## that started dropping `SubObjects` or `OrientAngle` reddens here rather than
## producing a marker that is merely turned the wrong way. Retail's own values,
## from `data/ini/livingworldicons/arnoricons.ini`.
const SPOT_FAMILY := "MoWArmyIcon"
const SPOT_SLOT := "Banner"
const SPOT_MODEL := "LWArmyHMoW"
const SPOT_SUB_OBJECTS := ["LWSTAFF", "LWBANNER"]
const SPOT_Z_OFFSET := 5.0
const SPOT_SCALE := 1.0
const SPOT_ORIENT_DEGREES := 270.0

## The model retail names in the `HilightedRing` slot of all seven
## `LivingWorldBuildPlotIcon` families - `livingworldbuildploticons.ini`, one
## value, no exceptions. Asserted BY NAME so a bundle that started binding that
## slot to some other ring could not pass as "a ring is standing".
const RETAIL_PLOT_RING_MODEL := "ArmyAntsLoc"

## 40 was the count before retail's per-plot hover ring became reachable.
## Nothing was removed and nothing was weakened; two were added, both about the
## ring that had never been on screen, so 40 + 2 = 42. The no-bundle total is
## untouched at 5, because a ring needs a plot to stand on.
const CHECKS_WITH_BUNDLE := 42
const CHECKS_WITHOUT_BUNDLE := 5

var _passed := 0
var _failed := 0


func _initialize() -> void:
	print("== WAR OF THE RING 3D MARKERS ==")
	var markers := MarkerScript.new()
	var located: Dictionary = markers.locate_and_load(_roots())
	var bound := bool(located.get("ok", false))
	if bound:
		print("bundle: %s" % String(located["path"]))
	else:
		print("bundle: NONE - %s" % String(located["reason"]).split(".")[0])

	_check_the_loader_reports_its_own_absence(located, bound)
	if bound:
		_check_the_bundle_carries_retails_own_census(markers)
		_check_every_model_converted_or_is_named(markers)
		_check_one_familys_slots_are_retails_own_numbers(markers)
		_check_subobjects_actually_filters(markers)
		_check_visibility_rules_are_retails_own(markers)
		_check_the_army_and_plot_links_are_authored()
		_check_the_map_stands_them_and_drops_the_flat_stand_ins()

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

## A MISSING BUNDLE IS A SENTENCE, NOT A FALSE - and it has to say what it costs
## on screen, or the reader has no way to tell "no models" from "no map".
func _check_the_loader_reports_its_own_absence(located: Dictionary, bound: bool) -> void:
	var reason := String(located.get("reason", ""))
	_check("a_bound_bundle_reports_no_reason_and_an_absent_one_reports_a_reason",
		(bound and reason.is_empty()) or (not bound and not reason.is_empty()))
	var empty := MarkerScript.new()
	var nothing: Dictionary = empty.locate_and_load([])
	if bool(nothing.get("ok", false)):
		# The environment names a bundle, so "no roots" still finds one. A
		# legitimate configuration, and the check still has to mean something.
		_check("loading_with_no_roots_either_finds_the_environments_bundle_or_explains",
			empty.loaded and not empty.families.is_empty())
	else:
		var text := String(nothing.get("reason", ""))
		_check("loading_with_no_roots_explains_itself",
			text.contains("livingworld_markers")
				and text.contains(MarkerScript.BUNDLE_ENV)
				and text.contains("flat"),
			text.split(".")[0])
	var refused := MarkerScript.new()
	_check("a_path_that_is_not_a_bundle_is_refused_rather_than_half_loaded",
		not refused.load_from("res://project.godot") and not refused.loaded)
	_check("a_refusal_carries_at_least_one_stated_error", not refused.errors.is_empty(),
		", ".join(Array(refused.errors)))
	# AN UNKNOWN FAMILY GETS NOTHING, never another family's geometry. Asserted
	# whether or not a bundle is bound, because the substitution this forbids is
	# exactly what a half-loaded bundle would invite.
	var invented := "NoSuchArmyIcon_%d" % Time.get_ticks_usec()
	var loader = MarkerScript.new() if not bound else null
	var probe = loader if loader != null else MarkerScript.new()
	if bound:
		probe.locate_and_load(_roots())
	_check("a_family_retail_never_declared_produces_no_geometry_rather_than_a_lookalike",
		probe.slot_pieces(invented, "Banner").is_empty()
			and probe.slot_model(invented, "Banner").is_empty())


## RETAIL'S OWN CENSUS. If the converter starts dropping blocks or models this is
## where it shows up - not as a thinner map nobody re-counts.
func _check_the_bundle_carries_retails_own_census(markers) -> void:
	var by_kind: Dictionary = markers.totals.get("familiesByKind", {}) as Dictionary
	_check("the_bundle_carries_every_LivingWorldArmyIcon_retail_ships",
		int(by_kind.get("army", 0)) == RETAIL_ARMY_FAMILIES,
		"%d of %d" % [int(by_kind.get("army", 0)), RETAIL_ARMY_FAMILIES])
	_check("the_bundle_carries_every_LivingWorldBuildingIcon_retail_ships",
		int(by_kind.get("building", 0)) == RETAIL_BUILDING_FAMILIES,
		"%d of %d" % [int(by_kind.get("building", 0)), RETAIL_BUILDING_FAMILIES])
	_check("the_bundle_carries_every_LivingWorldBuildPlotIcon_retail_ships",
		int(by_kind.get("buildPlot", 0)) == RETAIL_BUILD_PLOT_FAMILIES,
		"%d of %d" % [int(by_kind.get("buildPlot", 0)), RETAIL_BUILD_PLOT_FAMILIES])
	_check("the_bundle_carries_every_Object_slot_retail_declares",
		int(markers.totals.get("slots", 0)) == RETAIL_SLOTS,
		"%d of %d" % [int(markers.totals.get("slots", 0)), RETAIL_SLOTS])
	_check("the_bundle_names_every_distinct_model_retails_slots_name",
		int(markers.totals.get("modelsNamed", 0)) == RETAIL_MODELS,
		"%d of %d" % [int(markers.totals.get("modelsNamed", 0)), RETAIL_MODELS])
	# THE CONVERTER READ THE DOCUMENTS WHOLE. A key it does not model is a gap it
	# records; a non-empty list here means the manifest is a partial reading.
	var documents: Array = (markers.gaps.get("documents", []) as Array)
	_check("no_key_in_retails_marker_documents_went_unmodelled",
		documents.is_empty(), "%d unmodelled key(s)" % documents.size())


## EVERY MODEL EITHER CONVERTED OR IS NAMED. There is no third state, and the
## assertion is BY NAME rather than by a count tolerance so a second failure
## cannot hide behind the first.
func _check_every_model_converted_or_is_named(markers) -> void:
	var named := int(markers.totals.get("modelsNamed", 0))
	var converted := int(markers.totals.get("modelsConverted", 0))
	var absent: Array[String] = []
	for key in markers.unresolved_models.keys():
		absent.append(String(key))
	absent.sort()
	_check("every_model_is_either_converted_or_listed_as_unconverted",
		converted + absent.size() == named,
		"%d converted + %d named = %d" % [converted, absent.size(), named])
	# RETAIL SHIPS ALL 81. This asserts the exact list is EMPTY rather than
	# "small": if a model stops converting, this reddens with its name in it.
	_check("retail_names_no_marker_model_this_project_cannot_convert",
		absent.is_empty(), "unconverted: %s" % ", ".join(absent))
	var unresolved_textures: Array[String] = []
	for value in markers.unresolved_textures:
		unresolved_textures.append(String(value))
	_check("every_texture_the_marker_models_declare_resolved",
		unresolved_textures.is_empty(),
		"%d of %d resolved; unresolved: %s" % [
			int(markers.totals.get("texturesResolved", 0)),
			int(markers.totals.get("texturesDeclared", 0)),
			", ".join(unresolved_textures)])
	# EVERY SLOT REACHES A MODEL, and every `SubObjects` name reaches a mesh.
	# These are retail's own links, checked rather than trusted.
	var slots_without: Array = markers.gaps.get("slotsWithoutModel", []) as Array
	_check("every_slot_retail_declares_reaches_a_converted_model",
		slots_without.is_empty(), "%d slot(s) without: %s" % [
			slots_without.size(),
			", ".join(slots_without.map(func(v: Variant) -> String: return String(v)))])
	var missing_subs: Array = markers.gaps.get("missingSubObjects", []) as Array
	_check("every_SubObjects_name_a_slot_asks_for_exists_in_its_own_model",
		missing_subs.is_empty(), "%d missing: %s" % [
			missing_subs.size(),
			", ".join(missing_subs.map(func(v: Variant) -> String: return String(v)))])
	# EVERY DRAWABLE PIECE CARRIES RETAIL'S OWN PIXELS AND RETAIL'S OWN UVS. An
	# untextured or unmapped mesh would draw as flat grey, which is a stand-in.
	var pieces := 0
	var untextured: Array[String] = []
	var unmapped: Array[String] = []
	for model_name in markers.models.keys():
		var model: Dictionary = markers.models[model_name] as Dictionary
		for mesh_name in model["order"] as PackedStringArray:
			var piece := (model["meshes"] as Dictionary)[mesh_name] as Dictionary
			pieces += 1
			if not bool(piece["textured"]):
				untextured.append("%s.%s" % [String(model_name), String(mesh_name)])
			var mesh := piece["mesh"] as ArrayMesh
			if (mesh.surface_get_format(0) & Mesh.ARRAY_FORMAT_TEX_UV) == 0:
				unmapped.append("%s.%s" % [String(model_name), String(mesh_name)])
	untextured.sort()
	unmapped.sort()
	_check("every_converted_mesh_binds_retails_own_texture",
		untextured.is_empty() and pieces == int(markers.totals.get("meshes", 0)),
		"%d mesh(es); untextured: %s" % [pieces, ", ".join(untextured)])
	_check("every_converted_mesh_carries_retails_own_texture_coordinates",
		unmapped.is_empty(), "unmapped: %s" % ", ".join(unmapped))


## ONE FAMILY, FIELD BY FIELD. A marker turned the wrong way or lifted by the
## wrong amount is invisible in a count and obvious in retail's own numbers.
func _check_one_familys_slots_are_retails_own_numbers(markers) -> void:
	var slot: Dictionary = markers.slot(SPOT_FAMILY, SPOT_SLOT)
	_check("retails_own_Model_travels_into_the_bundle",
		String(slot.get("model", "")) == SPOT_MODEL,
		"%s vs %s" % [String(slot.get("model", "")), SPOT_MODEL])
	var subs: Array = slot.get("subObjectList", []) as Array
	_check("retails_own_SubObjects_list_travels_into_the_bundle_in_order",
		subs.size() == SPOT_SUB_OBJECTS.size()
			and String(subs[0]) == SPOT_SUB_OBJECTS[0]
			and String(subs[1]) == SPOT_SUB_OBJECTS[1],
		str(subs))
	_check("retails_own_placement_numbers_travel_into_the_bundle",
		is_equal_approx(MarkerScript.slot_z_offset(slot), SPOT_Z_OFFSET)
			and is_equal_approx(MarkerScript.slot_scale(slot), SPOT_SCALE)
			and is_equal_approx(MarkerScript.slot_orient_radians(slot),
				deg_to_rad(SPOT_ORIENT_DEGREES)),
		"z %.1f scale %.2f orient %.1f deg" % [
			MarkerScript.slot_z_offset(slot), MarkerScript.slot_scale(slot),
			rad_to_deg(MarkerScript.slot_orient_radians(slot))])
	# THE BODY SLOT IS RETAIL'S NAME, carried in the manifest so the Godot side
	# does not restate which slot an army is read off the map by.
	_check("each_family_declares_which_of_its_slots_is_the_marker_body",
		String(markers.body_slot(SPOT_FAMILY).get("slot", "")) == SPOT_SLOT,
		String(markers.body_slot(SPOT_FAMILY).get("slot", "")))


## `SubObjects` HAS TO ACTUALLY FILTER. `LWArmyHMoW` carries four meshes; the
## `Banner` slot names two of them and the pip slots one each. A filter that
## stopped filtering would draw the small, medium and large pips simultaneously
## on every banner, which reads as a slightly fussy marker rather than a bug.
func _check_subobjects_actually_filters(markers) -> void:
	var model: Dictionary = markers.models.get(SPOT_MODEL, {}) as Dictionary
	var whole := (model["order"] as PackedStringArray).size()
	var banner: int = markers.slot_pieces(SPOT_FAMILY, SPOT_SLOT).size()
	var pip: int = markers.slot_pieces(SPOT_FAMILY, "SmallPip").size()
	_check("a_slot_that_names_SubObjects_draws_only_those_meshes",
		banner == SPOT_SUB_OBJECTS.size() and pip == 1 and whole > banner,
		"model %d meshes, Banner %d, SmallPip %d" % [whole, banner, pip])
	var names: Array[String] = []
	for piece in markers.slot_pieces(SPOT_FAMILY, SPOT_SLOT):
		names.append(String(piece["name"]).to_upper())
	names.sort()
	var wanted := SPOT_SUB_OBJECTS.duplicate()
	wanted.sort()
	_check("and_they_are_the_meshes_retail_named_not_the_first_two",
		names == wanted, "%s vs %s" % [str(names), str(wanted)])
	# A SLOT THAT NAMES NONE DRAWS THE WHOLE MODEL, which is retail's own meaning
	# of an absent `SubObjects` and not a special case invented here.
	var soldiers: Array = markers.slot_pieces(SPOT_FAMILY, "Soldiers_Small")
	var soldier_model: Dictionary = markers.models.get(
		markers.slot_model(SPOT_FAMILY, "Soldiers_Small"), {}) as Dictionary
	_check("a_slot_that_names_no_SubObjects_draws_the_whole_model",
		not soldiers.is_empty()
			and soldiers.size() == (soldier_model["order"] as PackedStringArray).size(),
		"%d piece(s)" % soldiers.size())


## RETAIL'S OWN VISIBILITY FIELDS, read as retail wrote them. The one that
## matters most is the empty `VisibleArmySizes`: it means ALWAYS, and reading it
## as NEVER would silently delete every banner on the map.
func _check_visibility_rules_are_retails_own(markers) -> void:
	var banner: Dictionary = markers.slot(SPOT_FAMILY, SPOT_SLOT)
	var small: Dictionary = markers.slot(SPOT_FAMILY, "SmallPip")
	var hilighted: Dictionary = markers.slot(SPOT_FAMILY, "Hilighted")
	_check("an_absent_VisibleArmySizes_means_always_not_never",
		MarkerScript.slot_army_sizes(banner).is_empty()
			and not MarkerScript.slot_army_sizes(small).is_empty(),
		"Banner %s, SmallPip %s" % [
			str(Array(MarkerScript.slot_army_sizes(banner))),
			str(Array(MarkerScript.slot_army_sizes(small)))])
	_check("retails_hover_and_selection_gates_travel_into_the_bundle",
		MarkerScript.slot_hover_only(hilighted)
			and not MarkerScript.slot_hover_only(banner)
			and MarkerScript.slot_selection_only(markers.slot(SPOT_FAMILY, "Selected")),
		"Hilighted hover-only=%s" % MarkerScript.slot_hover_only(hilighted))


## THE LINK FROM AN ARMY TO ITS MARKER IS RETAIL'S, never a resemblance. Same
## discipline, and the same two rungs, as the portrait ladder.
func _check_the_army_and_plot_links_are_authored() -> void:
	var ui := UiScript.new()
	if not bool(ui.locate_and_load(_roots()).get("ok", false)):
		_fail("a_recruited_army_takes_the_marker_off_its_own_ArmyToSpawn_Icon",
			"no living-world UI bundle")
		_fail("a_seat_with_no_recruit_row_falls_back_to_its_own_DefaultArmyIconName",
			"no living-world UI bundle")
		_fail("an_army_retail_binds_to_no_icon_gets_none_rather_than_a_lookalike",
			"no living-world UI bundle")
		_fail("every_playable_template_names_retails_own_BuildPlotIconName",
			"no living-world UI bundle")
		return
	# Take a real recruited army out of the bundle rather than naming one here.
	var sample_army := ""
	var sample_icon := ""
	for key in ui.army_marker_icons.keys():
		sample_army = String(key)
		sample_icon = String((ui.army_marker_icons[key] as Dictionary).get("icon", ""))
		break
	var found: Dictionary = ui.army_marker(sample_army, "PlayerMen")
	_check("a_recruited_army_takes_the_marker_off_its_own_ArmyToSpawn_Icon",
		String(found.get("icon", "")) == sample_icon
			and String(found.get("source", "")) == UiScript.SOURCE_ARMY,
		"%s -> %s [%s]" % [sample_army, String(found.get("icon", "")),
			String(found.get("source", ""))])
	var fallback: Dictionary = ui.army_marker(
		"NoSuchArmy_%d" % Time.get_ticks_usec(), "PlayerMen")
	var template: Dictionary = ui.player_templates.get("PlayerMen", {}) as Dictionary
	_check("a_seat_with_no_recruit_row_falls_back_to_its_own_DefaultArmyIconName",
		String(fallback.get("icon", "")) == String(template.get("defaultArmyIconName", ""))
			and not String(fallback.get("icon", "")).is_empty(),
		String(fallback.get("icon", "")))
	var nothing: Dictionary = ui.army_marker(
		"NoSuchArmy_%d" % Time.get_ticks_usec(), "NoSuchTemplate")
	_check("an_army_retail_binds_to_no_icon_gets_none_rather_than_a_lookalike",
		String(nothing.get("icon", "")).is_empty(), str(nothing))
	var plot_icons := 0
	for name in ui.player_templates.keys():
		if not ui.build_plot_icon_id(String(name)).is_empty():
			plot_icons += 1
	_check("every_playable_template_names_retails_own_BuildPlotIconName",
		plot_icons == RETAIL_BUILD_PLOT_FAMILIES,
		"%d of %d template(s)" % [plot_icons, RETAIL_BUILD_PLOT_FAMILIES])


## THE MAP ACTUALLY STANDS THEM, THE FLAT STAND-INS ACTUALLY GO, AND NOTHING
## MOVES. The third is the one that would be expensive to get wrong: a marker
## that wrote anything would desync every other seat.
func _check_the_map_stands_them_and_drops_the_flat_stand_ins() -> void:
	var found: Dictionary = SessionScript.locate_document([])
	if not bool(found.get("ok", false)):
		_fail("the_map_stands_a_model_for_every_army_stack_it_can_reach", "no document")
		_fail("a_stack_that_stands_as_a_model_is_not_also_drawn_as_a_flat_plate", "no document")
		_fail("retails_structure_models_are_converted_and_deliberately_not_placed", "no document")
		_fail("every_standing_marker_is_the_family_that_armys_own_authored_field_names", "no document")
		_fail("no_marker_stands_a_slot_retail_gates_on_a_state_this_screen_does_not_simulate", "no document")
		_fail("standing_the_markers_changes_no_state_at_all", "no document")
		_fail("the_standing_markers_seed_the_label_placer", "no document")
		_fail("the_markers_survive_the_whole_zoom_range_and_an_orbit", "no document")
		_fail("a_marker_stands_at_retails_exact_size_when_close_in_and_is_capped_when_far_out",
			"no document")
		_fail("retails_hilighted_ring_stands_on_the_plot_under_the_pointer_and_no_other",
			"no document")
		_fail("the_plot_ring_is_retails_own_model_and_leaves_with_the_pointer",
			"no document")
		return
	var session = _seat(found)
	var screen := ScreenScript.new()
	root.add_child(screen)
	var before: String = session.state.state_hash()
	screen.configure(session, [], "", [])
	var view = screen.map3d
	if view == null or not view.has_map() or not view.has_markers():
		_fail("the_map_stands_a_model_for_every_army_stack_it_can_reach",
			"no map bundle or no marker bundle")
		_fail("a_stack_that_stands_as_a_model_is_not_also_drawn_as_a_flat_plate", "no map")
		_fail("retails_structure_models_are_converted_and_deliberately_not_placed", "no map")
		_fail("every_standing_marker_is_the_family_that_armys_own_authored_field_names", "no map")
		_fail("no_marker_stands_a_slot_retail_gates_on_a_state_this_screen_does_not_simulate", "no map")
		_fail("standing_the_markers_changes_no_state_at_all", "no map")
		_fail("the_standing_markers_seed_the_label_placer", "no map")
		_fail("the_markers_survive_the_whole_zoom_range_and_an_orbit", "no map")
		_fail("a_marker_stands_at_retails_exact_size_when_close_in_and_is_capped_when_far_out",
			"no map")
		_fail("retails_hilighted_ring_stands_on_the_plot_under_the_pointer_and_no_other",
			"no map")
		_fail("the_plot_ring_is_retails_own_model_and_leaves_with_the_pointer",
			"no map")
		screen.queue_free()
		return

	# HOW MANY STACKS THE MAP COULD REACH, computed from the state and the
	# authored links rather than read back off the view it is checking.
	var reachable := 0
	for region_id in view.armies_by_region.keys():
		var stacks: Array = view.armies_by_region[region_id] as Array
		var shown: int = mini(stacks.size(), view.MAX_BANNERS_PER_REGION)
		for index in range(shown):
			var stack := stacks[index] as Dictionary
			if String(stack.get("icon", "")).is_empty():
				continue
			if not view.markers.has_family(String(stack.get("icon", ""))):
				continue
			reachable += 1
	_check("the_map_stands_a_model_for_every_army_stack_it_can_reach",
		view.army_markers_standing == reachable and reachable > 0,
		"%d standing, %d reachable, %d flat" % [
			view.army_markers_standing, reachable, view.army_markers_flat.size()])
	# THE FLAT PLATE IS THE STAND-IN, NOT A SECOND MARKER. Drawing both would look
	# like a heavy banner and would be two things claiming to be one army.
	view._draw_overlay()
	_check("a_stack_that_stands_as_a_model_is_not_also_drawn_as_a_flat_plate",
		view.banners_drawn == 0 and view.army_markers_standing > 0,
		"%d flat plate(s) over %d standing model(s)" % [
			view.banners_drawn, view.army_markers_standing])
	# STRUCTURES: converted, and deliberately absent from the world because no
	# structure exists to place. Asserted so the absence is a decision on record
	# rather than something that could quietly become a defect.
	var building_families := int(
		(view.markers.totals.get("familiesByKind", {}) as Dictionary).get("building", 0))
	var building_models_placed := 0
	for row in view._standing_markers:
		if String(row["kind"]) == "building":
			building_models_placed += 1
	_check("retails_structure_models_are_converted_and_deliberately_not_placed",
		building_families == RETAIL_BUILDING_FAMILIES and building_models_placed == 0,
		"%d famil(ies) converted, %d placed" % [building_families, building_models_placed])
	# EVERY STANDING MARKER IS THE FAMILY THAT ARMY'S OWN AUTHORED FIELD NAMES.
	# The count check above cannot catch a BORROW - it counts both sides by the
	# same rule - so the binding is checked marker by marker against the stack it
	# belongs to, which is where a fallback to "the first family in the bundle"
	# would show up.
	var borrowed: Array[String] = []
	var by_key: Dictionary = {}
	for row in view._standing_markers:
		by_key[String(row["key"])] = row
	for region_id in view.armies_by_region.keys():
		var stacks: Array = view.armies_by_region[region_id] as Array
		for index in range(mini(stacks.size(), view.MAX_BANNERS_PER_REGION)):
			var row: Variant = by_key.get("%s#%d" % [String(region_id), index], null)
			if row == null:
				continue
			var authored := String((stacks[index] as Dictionary).get("icon", ""))
			if String((row as Dictionary)["family"]) != authored:
				borrowed.append("%s#%d stands %s, retail names %s" % [
					String(region_id), index, String((row as Dictionary)["family"]), authored])
	_check("every_standing_marker_is_the_family_that_armys_own_authored_field_names",
		borrowed.is_empty() and not by_key.is_empty(),
		"%d marker(s); borrowed: %s" % [by_key.size(), ", ".join(borrowed)])
	# AND NO MARKER STANDS A SLOT RETAIL GATES ON A STATE THIS SCREEN DOES NOT
	# SIMULATE. Retail hides the rally flag until a march is ordered and the
	# scaffold until something is under construction; neither state exists here,
	# so a marker showing either would be asserting a game state that is not in
	# the simulation.
	var ungated: Array[String] = []
	for row in view._standing_markers:
		for slot_name in (row["slots"] as PackedStringArray):
			var slot_row: Dictionary = view.markers.slot(String(row["family"]), String(slot_name))
			for field in ["displayAtRallyPoint", "showOnlyAfterMoveOrder",
					"hideWhenNotUnderConstruction", "hideWhenNotProducing"]:
				if String(slot_row.get(field, "")).to_lower() == "yes":
					ungated.append("%s/%s (%s)" % [String(row["family"]), String(slot_name), field])
	_check("no_marker_stands_a_slot_retail_gates_on_a_state_this_screen_does_not_simulate",
		ungated.is_empty(), "%d: %s" % [ungated.size(), ", ".join(ungated)])
	_check("standing_the_markers_changes_no_state_at_all",
		session.state.state_hash() == before,
		"hash %s" % ("unchanged" if session.state.state_hash() == before else "MOVED"))
	# THE LABEL PLACER IS SEEDED WITH THE MARKERS. Commit 83b1959 established that
	# a region name written across a portrait costs both; a banner that became a
	# model must not lose that.
	#
	# ASSERTED IN WORLD SPACE, and here is why rather than as a convenience:
	# headless Godot's `Camera3D.unproject_position` returns the ORIGIN for every
	# point - the whole strategic screen's 44 region markers project to (0, 0) in
	# this process, which is a pre-existing property of running without a
	# rendering server and not something this lane introduced. So a screen-space
	# assertion here would be vacuously true. What IS checkable headless is that
	# every standing marker carries a real, non-degenerate world footprint and
	# that the overlay seeded itself from exactly that set; the screen-space
	# result is what the captures show.
	var boxes: Array[Rect2] = view.project_marker_boxes()
	var footprints := 0
	for row in view._standing_markers:
		var aabb := row["aabb"] as AABB
		if aabb.size.x > 0.0 and aabb.size.y > 0.0 and aabb.size.z > 0.0:
			footprints += 1
	_check("the_standing_markers_seed_the_label_placer_from_a_real_world_footprint",
		footprints == view._standing_markers.size()
			and footprints == view.army_markers_standing + view.plot_markers_standing
			and view._banner_boxes.size() == boxes.size(),
		"%d footprint(s) over %d standing, %d seeded box(es)" % [
			footprints, view.army_markers_standing + view.plot_markers_standing,
			view._banner_boxes.size()])
	# THE CAMERA IS FREE OVER 33.8x AND A FULL ORBIT. A marker set that only
	# survives the opening framing is a marker set nobody checked.
	var standing_at_default: int = view.army_markers_standing
	var survived := true
	var detail: Array[String] = []
	for framing in [[0.04, 0.0, -52.0], [1.35, 0.0, -52.0], [0.30, 2.4, -12.0]]:
		view.focus_region("", float(framing[0]))
		view.set_orbit(float(framing[1]), float(framing[2]))
		view._draw_overlay()
		detail.append("zoom %.2f -> %d standing" % [
			float(framing[0]), view.army_markers_standing])
		if view.army_markers_standing != standing_at_default:
			survived = false
	_check("the_markers_survive_the_whole_zoom_range_and_an_orbit",
		survived and standing_at_default > 0, "; ".join(detail))
	# THE ONE PRESENTATION NUMBER IN THE MARKERS, asserted at both ends rather
	# than trusted. At or below the stated framing a marker stands at RETAIL'S
	# EXACT authored size with no multiplier at all; at the far end it is capped,
	# so it can neither vanish nor grow without bound.
	view.focus_region("", MapViewScript.MARKER_TRUE_ZOOM)
	var at_true: float = view.marker_magnification()
	view.focus_region("", MapViewScript.MIN_ZOOM)
	var at_near: float = view.marker_magnification()
	view.focus_region("", MapViewScript.MAX_ZOOM)
	var at_far: float = view.marker_magnification()
	_check("a_marker_stands_at_retails_exact_size_when_close_in_and_is_capped_when_far_out",
		is_equal_approx(at_true, 1.0) and is_equal_approx(at_near, 1.0)
			and at_far > 1.0 and at_far <= MapViewScript.MARKER_MAX_MAGNIFICATION + 0.001,
		"x%.2f at zoom %.2f, x%.2f at %.2f, x%.2f at %.2f" % [
			at_near, MapViewScript.MIN_ZOOM, at_true, MapViewScript.MARKER_TRUE_ZOOM,
			at_far, MapViewScript.MAX_ZOOM])
	_check_a_plot_ring_follows_the_pointer(view)
	screen.queue_free()


## RETAIL'S OWN PER-PLOT HOVER ART, and the reason it was invisible.
##
## Every one of retail's seven `LivingWorldBuildPlotIcon` families authors a
## `HilightedRing` slot carrying the model `ArmyAntsLoc` with
## `HideWhenUnhilighted = Yes`. All seven converted, `_slot_is_showing()` already
## honoured the field, and the ring had never once been on screen - because the
## view tracked hover per REGION and handed the plot stand a flat `false`. The
## art, the rule and the data were all present and the pointer could not reach
## them.
##
## So this asserts the thing that was missing rather than the thing that worked:
## the ring stands on the plot under the pointer, on THAT plot and no other -
## including no other plot in the same region, which is the case a per-region
## hover cannot distinguish - and it is gone when the pointer leaves.
func _check_a_plot_ring_follows_the_pointer(view) -> void:
	# A region the pointer could actually be over: it has to carry at least TWO
	# authored plots, so "this plot and not its neighbour" is a real distinction,
	# and a seat has to own it or retail's data binds its plots to no icon family.
	var region_id := ""
	var region_ids: Array[String] = []
	for key in view.plots_by_region.keys():
		region_ids.append(String(key))
	region_ids.sort()
	for candidate in region_ids:
		if (view.plots_by_region[candidate] as Array).size() < 2:
			continue
		if String(view.plot_icons_by_region.get(candidate, "")).is_empty():
			continue
		region_id = candidate
		break
	if region_id.is_empty():
		_fail("retails_hilighted_ring_stands_on_the_plot_under_the_pointer_and_no_other",
			"the state owns no region with two authored plots")
		_fail("the_plot_ring_is_retails_own_model_and_leaves_with_the_pointer",
			"the state owns no region with two authored plots")
		return

	# The plots are only drawn for a region that is selected or hovered, so the
	# pointer has to be over the region before it can be over one of its plots.
	view.hover_region = region_id
	view.hover_plot_at(region_id, 0)
	var on_first := _slots_standing(view, "%s.plot0" % region_id)
	var on_second := _slots_standing(view, "%s.plot1" % region_id)
	_check("retails_hilighted_ring_stands_on_the_plot_under_the_pointer_and_no_other",
		on_first.has("HilightedRing") and not on_second.has("HilightedRing"),
		"plot 0 of %s stands %s, plot 1 stands %s" % [
			region_id, str(on_first), str(on_second)])

	# THE MODEL IS RETAIL'S, NAMED, not "a ring". If the bundle ever binds that
	# slot to something else this says which model it actually stood.
	var family := String(view.plot_icons_by_region.get(region_id, ""))
	var ring: Dictionary = view.markers.slot(family, "HilightedRing")
	var model := String(ring.get("model", ""))
	view.hover_plot_at("", -1)
	var after_leaving := _slots_standing(view, "%s.plot0" % region_id)
	_check("the_plot_ring_is_retails_own_model_and_leaves_with_the_pointer",
		model == RETAIL_PLOT_RING_MODEL and not after_leaving.has("HilightedRing"),
		"%s/HilightedRing names %s (retail: %s); with the pointer away plot 0 stands %s" % [
			family, model if not model.is_empty() else "NOTHING",
			RETAIL_PLOT_RING_MODEL, str(after_leaving)])
	view.hover_region = ""
	view._rebuild_markers()


## The slot names standing for one marker key, as an Array of String. Read off
## the view's own record of what it stood, which is the same record the other
## checks in this file read.
func _slots_standing(view, key: String) -> Array:
	for row in view._standing_markers:
		if String(row["key"]) == key:
			return Array(row["slots"] as PackedStringArray)
	return []


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
	var scenarios := probe.startable_scenarios(2)
	var session := SessionScript.new()
	session.begin(document, world.campaign_name, String(scenarios[0]), seats)
	session.document_path = String(found["path"])
	session.document_source = String(found["source"])
	return session


func _check(what: String, condition: bool, detail: String = "") -> void:
	if condition:
		_passed += 1
		print("  PASS  %s%s" % [what, "" if detail.is_empty() else " (%s)" % detail])
	else:
		_fail(what, detail)


func _fail(what: String, detail: String) -> void:
	_failed += 1
	print("  FAIL  %s%s" % [what, "" if detail.is_empty() else " (%s)" % detail])
