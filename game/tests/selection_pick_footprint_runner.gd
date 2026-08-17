extends SceneTree
## Selection picking must hit-test the retail Geometry footprint, not a flat
## world-unit radius.
##
## Retail truth (PURE RETAIL rotwk tree,
## workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini):
##   * object/goodfaction/structures/men/barracks.ini
##       Geometry = CYLINDER 8.0 (rally probe) plus AdditionalGeometry BOX
##       45.0 x 50.0 -> union footprint 45 x 50 SOURCE half-extents.
##   * object/goodfaction/structures/men/fortress.ini
##       Geometry = BOX 64 x 64 main body plus the expansion-plot ring.
##   * object/goodfaction/units/gondor/gondorinfantry.ini
##       Geometry = CYLINDER, GeometryMajorRadius = 8.0, GeometryHeight = 19.2.
##
## Fords of Isen II player starts are 2868.7 source units apart and
## RetailMapData normalises that to LOCAL_START_SEPARATION = 76.0 world units,
## so the map transform scale is 0.026492. A barracks is therefore ~1.32 world
## units of footprint radius; the slice used to pick it anywhere within 5.5.

const Pick = preload("res://src/retail_slice/retail_selection_pick.gd")
## NOT preloaded: retail_battalion.gd names the ContentDB autoload, and a
## --script SceneTree runner compiles its preloads before the autoloads
## register, so a const preload here binds a half-compiled GDScript whose
## `.new()` is missing. Runtime `load()` inside the test compiles lazily,
## after the autoloads exist (same reason the scene runners load the slice
## tscn inside _run, not at parse time).
const BATTALION_SCRIPT_PATH := "res://src/retail_slice/retail_battalion.gd"

# 76.0 / 2868.7 -- see the header. Kept as a literal so the runner does not
# need a cooked map on disk.
const FORDS_SOURCE_SCALE := 0.026492
const BARRACKS_SOURCE_RADIUS := 50.0
## Retail-authored MenFortress main body: `Geometry = BOX`,
## `GeometryMajorRadius = 64` / `GeometryMinorRadius = 64`
## (pure RotWK 2.01 `object/goodfaction/structures/men/fortress.ini:1254-1256`).
## Read from the module so the fallback and this runner cannot drift apart.
const FORTRESS_SOURCE_RADIUS := Pick.DEFAULT_FORTRESS_SOURCE_RADIUS
const INFANTRY_SOURCE_RADIUS := 8.0
## Pure RotWK 2.01 `object/evilfaction/units/mordor/attacktroll.ini:795-797`:
## `Geometry = CYLINDER`, `GeometryMajorRadius = 17.6`, `GeometryHeight = 40.0`.
const TROLL_SOURCE_RADIUS := 17.6

## LIVENESS (rulebook T3): a mid-body script error aborts the enclosing
## function without propagating. Completion is marked only at the END of each
## test body, and the exact check count must match EXPECTED_CHECKS.
const EXPECTED_TESTS := 14
const EXPECTED_CHECKS := 73
## Floor kept so a content-less run still fails closed if it aborts early.
const MINIMUM_CHECKS := 40

var passed := 0
var failed := 0
var _completed: Array[String] = []


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_source_footprint_reads_compiled_geometry()
	_test_world_projection_matches_retail_scale()
	_test_visual_bounds_fallback_is_tight()
	_test_structure_pick_rejects_distant_clicks()
	_test_battalion_pick_rejects_distant_clicks()
	_test_nested_footprints_prefer_the_tighter_volume()
	_test_no_flat_pick_radius_constants_remain()
	_test_horde_pick_is_the_member_union()
	_test_slice_pick_wires_member_candidates()
	_test_order_margin_stays_within_the_hitbox()
	_test_mounted_pack_member_geometry_matches_retail()
	_test_pieces_union_beats_primary_probe()
	_test_visual_aabb_is_a_floor_over_compiled_geometry()
	_test_piece_accurate_structure_hits()

	if _completed.size() != EXPECTED_TESTS:
		failed += 1
		push_error("SELECTION_PICK_FAIL liveness: %d/%d tests reported (%s)" % [
			_completed.size(), EXPECTED_TESTS, ", ".join(_completed)
		])
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		push_error("SELECTION_PICK_FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [
			ran, EXPECTED_CHECKS
		])
	elif ran < MINIMUM_CHECKS:
		failed += 1
		push_error("SELECTION_PICK_FAIL liveness: only %d checks ran, expected at least %d" % [
			ran, MINIMUM_CHECKS
		])
	print("SELECTION_PICK_RESULT passed=%d failed=%d tests=%d checks=%d" % [passed, failed, _completed.size(), ran])
	quit(0 if failed == 0 else 1)


func _test_source_footprint_reads_compiled_geometry() -> void:
	## A compiled geometry document supplies the union footprint directly.
	var document := {
		"shape": "CYLINDER",
		"majorRadius": {"authored": "8.0", "value": 8.0},
		"minorRadius": {"authored": "8.0", "value": 8.0},
		"height": {"authored": "10", "value": 10.0},
		"isSmall": false,
		"footprint": {"majorRadius": 45.0, "minorRadius": 50.0, "radius": 50.0},
	}
	_check("compiled_footprint_wins", is_equal_approx(Pick.source_footprint_radius(document), 50.0))
	# Without the union block the primary geometry still narrows the pick.
	var primary_only := document.duplicate(true)
	primary_only.erase("footprint")
	_check("primary_geometry_fallback", is_equal_approx(Pick.source_footprint_radius(primary_only), 8.0))
	_check("empty_geometry_is_zero", is_equal_approx(Pick.source_footprint_radius({}), 0.0))
	_completed.append("source-footprint")


func _test_world_projection_matches_retail_scale() -> void:
	# The fortress fallback must be the AUTHORED half-extent, not an estimate.
	# It was 120.0 - a number retail authors nowhere; the real main body is 64
	# (fortress.ini:1254-1256, and the same 64 in every faction fortress). The
	# fallback only applies when a pack carries no geometry at all, so an
	# invented value silently widens the pick on exactly the packs that can
	# least afford it.
	_check(
		"fortress_fallback_is_the_authored_64",
		is_equal_approx(Pick.DEFAULT_FORTRESS_SOURCE_RADIUS, 64.0)
	)
	var barracks := Pick.world_radius_from_source(BARRACKS_SOURCE_RADIUS, FORDS_SOURCE_SCALE)
	var expected := BARRACKS_SOURCE_RADIUS * FORDS_SOURCE_SCALE + Pick.SELECTION_MARGIN
	_check("barracks_world_radius", absf(barracks - expected) < 0.001)
	# The regression this guards: the old flat constant.
	_check("barracks_world_radius_far_below_legacy_5_5", barracks < 2.0)
	var fortress := Pick.world_radius_from_source(FORTRESS_SOURCE_RADIUS, FORDS_SOURCE_SCALE)
	_check("fortress_world_radius_far_below_legacy_8_0", fortress < 3.5)
	_check("fortress_is_larger_than_barracks", fortress > barracks)
	var infantry := Pick.world_radius_from_source(INFANTRY_SOURCE_RADIUS, FORDS_SOURCE_SCALE)
	_check(
		"single_infantry_body_stays_tiny",
		infantry >= Pick.MINIMUM_SELECTION_RADIUS and infantry < 0.4
	)
	_check("no_scale_yields_no_radius", is_equal_approx(Pick.world_radius_from_source(50.0, 0.0), 0.0))
	_completed.append("world-projection")


func _test_visual_bounds_fallback_is_tight() -> void:
	## Packs published before the geometry projection carry no geometry, so the
	## runtime falls back to the model's own bounds. That must stay tight.
	var bounds := AABB(Vector3(-45.0, 0.0, -50.0), Vector3(90.0, 75.0, 100.0))
	var radius := Pick.world_radius_from_visual_bounds(bounds, FORDS_SOURCE_SCALE)
	var expected := 50.0 * FORDS_SOURCE_SCALE + Pick.SELECTION_MARGIN
	_check("visual_bounds_uses_largest_half_extent", absf(radius - expected) < 0.001)
	_check("visual_bounds_below_legacy_constant", radius < 2.0)
	_check("visual_bounds_without_scale_is_zero", is_equal_approx(Pick.world_radius_from_visual_bounds(bounds, 0.0), 0.0))
	var sliver := AABB(Vector3(-0.5, 0.0, -0.5), Vector3(1.0, 4.0, 1.0))
	_check(
		"tiny_model_stays_clickable",
		Pick.world_radius_from_visual_bounds(sliver, FORDS_SOURCE_SCALE) >= Pick.MINIMUM_SELECTION_RADIUS
	)
	_completed.append("visual-bounds")


func _test_structure_pick_rejects_distant_clicks() -> void:
	var radius := Pick.world_radius_from_source(BARRACKS_SOURCE_RADIUS, FORDS_SOURCE_SCALE)
	var candidates := [{"id": 7, "position": Vector2(10.0, 4.0), "radius": radius}]
	_check(
		"click_on_the_model_selects",
		Pick.closest_hit(Vector2(10.0, 4.0), candidates) == 7
	)
	_check(
		"click_at_the_footprint_edge_selects",
		Pick.closest_hit(Vector2(10.0 + radius * 0.9, 4.0), candidates) == 7
	)
	# Half the visible battlefield at minimum zoom is ~4.35 world units.
	_check(
		"click_halfway_across_the_screen_does_not_select",
		Pick.closest_hit(Vector2(14.35, 4.0), candidates) == 0
	)
	# The legacy 5.5 pick circle would still have hit here.
	_check(
		"click_at_legacy_radius_does_not_select",
		Pick.closest_hit(Vector2(10.0 + 5.0, 4.0), candidates) == 0
	)
	_check(
		"click_just_beyond_the_footprint_does_not_select",
		Pick.closest_hit(Vector2(10.0 + radius + 0.2, 4.0), candidates) == 0
	)
	_completed.append("structure-pick")


func _test_battalion_pick_rejects_distant_clicks() -> void:
	# A 15-strong horde spread over ~1.1 world units of formation plus the
	# member's own 8.0-source-unit body.
	var radius := 1.1 + Pick.world_radius_from_source(INFANTRY_SOURCE_RADIUS, FORDS_SOURCE_SCALE)
	var candidates := [{"id": 3, "position": Vector2.ZERO, "radius": radius}]
	_check("click_on_the_horde_selects", Pick.closest_hit(Vector2(0.4, 0.2), candidates) == 3)
	_check("click_at_horde_edge_selects", Pick.closest_hit(Vector2(radius * 0.95, 0.0), candidates) == 3)
	_check(
		"click_beyond_the_horde_does_not_select",
		Pick.closest_hit(Vector2(radius + 0.3, 0.0), candidates) == 0
	)
	# The legacy battalion constant was 6.0 world units.
	_check("click_at_legacy_battalion_radius_does_not_select", Pick.closest_hit(Vector2(5.5, 0.0), candidates) == 0)
	# Attack orders keep a small extra margin; selection does not. The margin
	# forgives the silhouette edge (radius + 0.1) but must not reach a full
	# third of a world unit past the body (the old 0.45 did - wider than the
	# thinnest authored member body in the slice, 8.0 source = 0.21 world).
	_check(
		"order_margin_is_forgiving_but_bounded",
		Pick.closest_hit(Vector2(radius + 0.1, 0.0), candidates, Pick.ORDER_MARGIN) == 3
			and Pick.closest_hit(Vector2(radius + 0.3, 0.0), candidates, Pick.ORDER_MARGIN) == 0
	)
	_completed.append("battalion-pick")


func _test_nested_footprints_prefer_the_tighter_volume() -> void:
	var fortress_radius := Pick.world_radius_from_source(FORTRESS_SOURCE_RADIUS, FORDS_SOURCE_SCALE)
	var horde_radius := 0.9
	var candidates := [
		{"id": 11, "position": Vector2.ZERO, "radius": fortress_radius},
		{"id": 12, "position": Vector2(1.4, 0.0), "radius": horde_radius},
	]
	_check(
		"horde_standing_on_the_fortress_wins",
		Pick.closest_hit(Vector2(1.4, 0.0), candidates) == 12
	)
	_check(
		"fortress_still_selectable_away_from_the_horde",
		Pick.closest_hit(Vector2(-1.4, 0.0), candidates) == 11
	)
	_check(
		"zero_radius_candidates_are_never_hit",
		Pick.closest_hit(Vector2.ZERO, [{"id": 9, "position": Vector2.ZERO, "radius": 0.0}]) == 0
	)
	_completed.append("nested-footprints")


func _test_no_flat_pick_radius_constants_remain() -> void:
	## The bug was literal world-unit radii in the pick path. Keep them out.
	var source := FileAccess.get_file_as_string("res://src/retail_slice/retail_vertical_slice.gd")
	_check("vertical_slice_source_readable", source.length() > 0)
	for legacy in ["8.0 if String(row.get(\"structure_kind\"", "var best_distance := 9.0"]:
		_check("legacy_flat_radius_absent:%s" % legacy, not source.contains(legacy))
	_check(
		"pick_path_uses_the_footprint_module",
		source.contains("RetailSelectionPick") or source.contains("SelectionPick")
	)
	_completed.append("no-flat-constants")


func _test_horde_pick_is_the_member_union() -> void:
	## THE BUG. The battalion pick used to be ONE disc centred on the horde
	## root: formation spread + one member body. On a 15-archer line that disc
	## is ~1.5 world units of radius - it lit the attack cursor over the gaps
	## between ranks and past the outermost silhouette ("hovering far from an
	## enemy still lights the attack cursor"). Retail hit-tests each member's
	## authored Geometry cylinder (gondorarcher.ini:796-799, MajorRadius 8.0),
	## so the pickable silhouette is the UNION of member bodies.
	var member_radius := Pick.world_radius_from_source(INFANTRY_SOURCE_RADIUS, FORDS_SOURCE_SCALE)
	var battalion_script: GDScript = load(BATTALION_SCRIPT_PATH)
	_check("battalion_script_compiles", battalion_script != null and battalion_script.can_instantiate())
	if battalion_script == null or not battalion_script.can_instantiate():
		_completed.append("member-union")
		return
	var battalion: Node3D = battalion_script.new()
	root.add_child(battalion)
	# Off-origin: candidates must be WORLD positions, not local ones.
	battalion.position = Vector3(30.0, 0.0, -12.0)
	battalion._member_selection_radius = member_radius
	var offsets := [Vector3(-1.0, 0.0, 0.0), Vector3(1.0, 0.0, 0.0), Vector3(0.0, 0.0, 0.9)]
	for index in offsets.size():
		var visual := Node3D.new()
		battalion.add_child(visual)
		visual.position = offsets[index]
		battalion.member_visuals[index] = visual
	var candidates: Array = battalion.member_pick_candidates(42)
	_check("one_candidate_per_living_member", candidates.size() == offsets.size())
	var all_radius := true
	var all_id := true
	for candidate_value in candidates:
		var candidate: Dictionary = candidate_value
		if absf(float(candidate.get("radius", 0.0)) - member_radius) > 0.001:
			all_radius = false
		if int(candidate.get("id", 0)) != 42:
			all_id = false
	_check("candidates_carry_the_member_geometry_radius", all_radius)
	_check("candidates_carry_the_battalion_id", all_id)
	var found_world_member := false
	for candidate_value in candidates:
		if Vector2((candidate_value as Dictionary).get("position", Vector2.ZERO)).distance_to(Vector2(31.0, -12.0)) < 0.001:
			found_world_member = true
	_check("candidate_positions_are_world_space", found_world_member)
	var origin := Vector2(30.0, -12.0)
	_check("hover_over_a_member_hits", Pick.closest_hit(origin + Vector2(1.0, 0.0), candidates) == 42)
	# The horde centre sits 1.0 world units from every member - deep inside the
	# old bounding disc, outside every authored body.
	_check("formation_centre_gap_does_not_hit", Pick.closest_hit(origin, candidates) == 0)
	var legacy_disc := [{"id": 42, "position": origin, "radius": 1.0 + member_radius}]
	_check("legacy_bounding_disc_would_have_hit_the_gap", Pick.closest_hit(origin, legacy_disc) == 42)
	_check(
		"order_margin_forgives_the_silhouette_edge",
		Pick.closest_hit(origin + Vector2(1.0 + member_radius + 0.05, 0.0), candidates, Pick.ORDER_MARGIN) == 42
	)
	_check("order_margin_does_not_reach_the_gap", Pick.closest_hit(origin, candidates, Pick.ORDER_MARGIN) == 0)
	# Dead members (health already 0, corpse still present) are not pickable.
	battalion.member_health_ratios[2] = 0.0
	_check("dead_members_stop_being_pickable_while_corpse_remains", battalion.member_pick_candidates(42).size() == 2)
	# After the corpse is freed the visual map drops them too.
	battalion.member_visuals.erase(1)
	_check("dead_members_stop_being_pickable", battalion.member_pick_candidates(42).size() == 1)
	battalion.queue_free()
	# No visuals yet (spawn frame, headless seam): empty, so the caller's
	# battalion-level fallback disc takes over. That contract must hold.
	var fresh: Node3D = battalion_script.new()
	root.add_child(fresh)
	_check("no_visuals_yields_no_candidates_for_the_fallback", fresh.member_pick_candidates(7).is_empty())
	fresh.queue_free()
	_completed.append("member-union")


func _test_slice_pick_wires_member_candidates() -> void:
	## The cursor/order picks run through `_battalion_pick_candidates`; it must
	## prefer per-member candidates and keep the horde disc only as the
	## no-visuals fallback. Substring wiring check, same pattern as
	## attack_cursor_runner's slice-wiring test.
	var source := FileAccess.get_file_as_string("res://src/retail_slice/retail_vertical_slice.gd")
	_check("vertical_slice_source_readable_for_wiring", source.length() > 0)
	var start := source.find("func _battalion_pick_candidates")
	_check("battalion_pick_candidates_present", start >= 0)
	if start < 0:
		_completed.append("slice-wiring")
		return
	var body := source.substr(start, source.find("func _battalion_pick_radius") - start)
	_check("slice_picks_per_member_when_visuals_live", body.contains("member_pick_candidates"))
	_check("horde_disc_remains_only_a_fallback", body.contains("_battalion_pick_radius(id)"))
	_completed.append("slice-wiring")


func _test_order_margin_stays_within_the_hitbox() -> void:
	## Retail's attack cursor is slightly more generous than the selection
	## hit-test - slightly. The thinnest authored member body in the slice is
	## the archer's MajorRadius 8.0 (gondorarcher.ini:796-799), 0.21 world at
	## the Fords scale; an order margin wider than that is not a margin, it is
	## a second hitbox.
	var archer_body_world := INFANTRY_SOURCE_RADIUS * FORDS_SOURCE_SCALE
	_check("order_margin_exceeds_selection_margin", Pick.ORDER_MARGIN > Pick.SELECTION_MARGIN)
	_check("order_margin_no_wider_than_the_thinnest_body", Pick.ORDER_MARGIN <= archer_body_world)
	_completed.append("order-margin")


func _test_mounted_pack_member_geometry_matches_retail() -> void:
	## Oracle check against the mounted packs: the compiled geometry index must
	## reproduce the authored radii this whole runner cites - a thin unit (Men
	## archer, 8.0) and a fat one (Mordor attack troll, 17.6). Skips loudly
	## with no content; FAILS when OPENBFME_CONTENT is set and the document is
	## missing (same rule as attack_cursor_runner's mounted-pack test).
	var db = root.get_node_or_null("ContentDB")
	_check("content_db_autoload_available", db != null)
	if db == null:
		_completed.append("mounted-geometry")
		return
	db.reload()
	if not db.has_method("get_playable_unit_runtime_for_member"):
		_check("content_db_indexes_geometry_per_member", false)
		_completed.append("mounted-geometry")
		return
	var content_requested := OS.get_environment("OPENBFME_CONTENT").strip_edges() != ""
	var cases := {
		"GondorArcher": INFANTRY_SOURCE_RADIUS,
		"MordorAttackTroll": TROLL_SOURCE_RADIUS,
	}
	for member_id in cases.keys():
		var expected := float(cases[member_id])
		var document: Dictionary = db.get_playable_unit_runtime_for_member(String(member_id))
		if document.is_empty():
			if content_requested:
				_check("mounted_pack_carries_%s_geometry" % member_id, false)
			else:
				print("SELECTION_PICK_SKIP no_pack_for_%s" % member_id)
			continue
		var registration: Dictionary = document.get("registration", {})
		var geometry: Dictionary = (registration.get("gameplay", {}) as Dictionary).get("geometry", {})
		var resolved := Pick.source_footprint_radius(geometry)
		_check(
			"%s_geometry_radius_is_the_authored_value" % member_id,
			absf(resolved - expected) < 0.05
		)
	# The fat/thin contrast is the owner's ask: pick bounds track the AUTHORED
	# geometry per unit, not one shared constant.
	var archer_doc: Dictionary = db.get_playable_unit_runtime_for_member("GondorArcher")
	var troll_doc: Dictionary = db.get_playable_unit_runtime_for_member("MordorAttackTroll")
	if not archer_doc.is_empty() and not troll_doc.is_empty():
		var archer_radius := Pick.source_footprint_radius(
			((archer_doc.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary).get("geometry", {})
		)
		var troll_radius := Pick.source_footprint_radius(
			((troll_doc.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary).get("geometry", {})
		)
		_check("troll_pick_is_fatter_than_archer_pick", troll_radius > 2.0 * archer_radius)
	_completed.append("mounted-geometry")


func _barracks_pieces_geometry() -> Dictionary:
	## Retail barracks.ini:434-452 — CYLINDER 8.0 rally probe plus Geom_Orig
	## BOX 20x20 at X:-22 Y:-30 and BOX 45x50 at the origin. Union is 45x50.
	return {
		"shape": "CYLINDER",
		"majorRadius": {"authored": "8.0", "value": 8.0},
		"minorRadius": {"authored": "8.0", "value": 8.0},
		"height": {"authored": "10", "value": 10.0},
		"pieces": [
			{
				"role": "primary",
				"shape": "CYLINDER",
				"majorRadius": {"authored": "8.0", "value": 8.0},
				"minorRadius": {"authored": "8.0", "value": 8.0},
			},
			{
				"role": "additional",
				"name": "Geom_Orig",
				"shape": "BOX",
				"majorRadius": {"authored": "20.0", "value": 20.0},
				"minorRadius": {"authored": "20.0", "value": 20.0},
				"offset": {"x": -22.0, "y": -30.0, "z": 0.0},
			},
			{
				"role": "additional",
				"name": "Geom_Orig",
				"shape": "BOX",
				"majorRadius": {"authored": "45.0", "value": 45.0},
				"minorRadius": {"authored": "50.0", "value": 50.0},
				"offset": {"x": 0.0, "y": 0.0, "z": 0.0},
			},
		],
	}


func _fortress_pieces_geometry() -> Dictionary:
	## Retail fortress.ini:1254-1300 — BOX 64 main body plus plot pads at
	## GeometryOffset X:64 Y:-64 (and the other five). Union radius is 74.
	return {
		"shape": "BOX",
		"majorRadius": {"authored": "64", "value": 64.0},
		"minorRadius": {"authored": "64", "value": 64.0},
		"pieces": [
			{
				"role": "primary",
				"shape": "BOX",
				"majorRadius": {"authored": "64", "value": 64.0},
				"minorRadius": {"authored": "64", "value": 64.0},
			},
			{
				"role": "additional",
				"name": "Plots",
				"shape": "BOX",
				"majorRadius": {"authored": "10.0", "value": 10.0},
				"minorRadius": {"authored": "10.0", "value": 10.0},
				"offset": {"x": 64.0, "y": -64.0, "z": 0.0},
			},
		],
	}


func _test_pieces_union_beats_primary_probe() -> void:
	## THE BUG. A pack document can carry geometry.pieces without a precomputed
	## footprint row. source_footprint_radius used to fall back to the primary
	## 8.0 rally probe and the barracks was barely clickable.
	var barracks := _barracks_pieces_geometry()
	_check("barracks_pieces_union_is_50_not_the_8_probe", is_equal_approx(Pick.source_footprint_radius(barracks), 50.0))
	var with_footprint := barracks.duplicate(true)
	with_footprint["footprint"] = {"majorRadius": 45.0, "minorRadius": 50.0, "radius": 50.0}
	_check("barracks_footprint_row_still_wins", is_equal_approx(Pick.source_footprint_radius(with_footprint), 50.0))
	var fortress := _fortress_pieces_geometry()
	_check("fortress_pieces_union_is_74", is_equal_approx(Pick.source_footprint_radius(fortress), 74.0))
	_completed.append("pieces-union")


func _test_visual_aabb_is_a_floor_over_compiled_geometry() -> void:
	## Live keep mesh (construction-intact-gbfortress.glb accessor AABB) is
	## ~63.36 x 66.85 source half-extents — NOT the repair contact points at
	## X:-90 Y:82 (fortress.ini:1305-1306). MenFortress compiled union is 74
	## (64-box + plots), so the visual does not grow that object. The
	## load-bearing case is MenFortressCitadel: primary 49 + towers = 52
	## (fortress.ini:996-1033), which the 66.85 mesh does outrun.
	var compiled_52 := Pick.world_radius_from_source(52.0, FORDS_SOURCE_SCALE)
	var compiled_74 := Pick.world_radius_from_source(74.0, FORDS_SOURCE_SCALE)
	var keep_mesh := AABB(Vector3(-63.36, 0.0, -66.85), Vector3(126.72, 75.0, 133.70))
	var visual := Pick.world_radius_from_visual_bounds(keep_mesh, FORDS_SOURCE_SCALE)
	_check("live_keep_mesh_does_not_outrun_menfortress_74", visual < compiled_74)
	_check(
		"effective_menfortress_stays_on_compiled_74",
		is_equal_approx(Pick.effective_world_pick_radius(compiled_74, visual), compiled_74)
	)
	_check("citadel_visual_outruns_compiled_52", visual > compiled_52)
	_check(
		"effective_citadel_takes_the_visual_floor",
		is_equal_approx(Pick.effective_world_pick_radius(compiled_52, visual), visual)
	)
	_check(
		"compiled_stays_the_floor_when_visual_is_smaller",
		is_equal_approx(Pick.effective_world_pick_radius(compiled_74, compiled_52), compiled_74)
	)
	var structure_script: GDScript = load("res://src/retail_slice/retail_structure.gd")
	_check("structure_script_compiles", structure_script != null and structure_script.can_instantiate())
	if structure_script == null or not structure_script.can_instantiate():
		_completed.append("visual-floor")
		return
	var structure: Node3D = structure_script.new()
	root.add_child(structure)
	structure.structure_kind = "fortress"
	structure.selection_radius_source = "compiled-retail-geometry"
	structure.pick_radius = compiled_52
	structure._source_unit_scale = FORDS_SOURCE_SCALE
	structure._apply_visual_bounds_selection_radius(keep_mesh, FORDS_SOURCE_SCALE)
	_check("citadel_structure_pick_grows_to_the_mesh", structure.pick_radius >= visual - 0.001)
	var origin := Vector2(20.0, -8.0)
	structure.global_position = Vector3(origin.x, 0.0, origin.y)
	var candidates: Array = structure.structure_pick_candidates(11)
	_check("structure_emits_pick_candidates", not candidates.is_empty())
	var visual_edge := origin + Vector2(visual * 0.92, 0.0)
	_check("click_on_the_citadel_visual_edge_selects", Pick.closest_hit(visual_edge, candidates) == 11)
	var compiled_only := [{"id": 11, "position": origin, "radius": compiled_52}]
	_check("compiled_52_circle_alone_misses_the_citadel_edge", Pick.closest_hit(visual_edge, compiled_only) == 0)
	structure.queue_free()
	_completed.append("visual-floor")


func _test_piece_accurate_structure_hits() -> void:
	## A single circle at the entity origin leaves offset pieces unclickable.
	## Barracks Geom_Orig sits at X:-22 Y:-30 (barracks.ini:440-445). Fortress
	## plot pads sit at X:64 Y:-64 (fortress.ini:1260-1265).
	var origin := Vector2(10.0, 4.0)
	var barracks := Pick.geometry_piece_candidates(7, origin, _barracks_pieces_geometry(), FORDS_SOURCE_SCALE)
	_check("barracks_emits_one_candidate_per_piece", barracks.size() == 3)
	var union_radius := Pick.world_radius_from_source(50.0, FORDS_SOURCE_SCALE)
	_check(
		"click_on_the_45x50_body_selects",
		Pick.closest_hit(origin + Vector2(union_radius * 0.85, 0.0), barracks) == 7
	)
	var probe_only := [{"id": 7, "position": origin, "radius": Pick.world_radius_from_source(8.0, FORDS_SOURCE_SCALE)}]
	_check(
		"eight_unit_probe_alone_misses_the_45x50_body",
		Pick.closest_hit(origin + Vector2(union_radius * 0.85, 0.0), probe_only) == 0
	)
	var geom_orig := origin + Vector2(-22.0, -30.0) * FORDS_SOURCE_SCALE
	_check("click_on_offset_geom_orig_selects", Pick.closest_hit(geom_orig, barracks) == 7)
	var fortress := Pick.geometry_piece_candidates(11, origin, _fortress_pieces_geometry(), FORDS_SOURCE_SCALE)
	var plot := origin + Vector2(64.0, -64.0) * FORDS_SOURCE_SCALE
	_check("click_on_offset_fortress_plot_selects", Pick.closest_hit(plot, fortress) == 11)
	var body_only := [{"id": 11, "position": origin, "radius": Pick.world_radius_from_source(64.0, FORDS_SOURCE_SCALE)}]
	_check("sixty_four_box_alone_misses_the_plot_pad", Pick.closest_hit(plot, body_only) == 0)
	_completed.append("piece-hits")


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("SELECTION_PICK_FAIL %s" % label)
