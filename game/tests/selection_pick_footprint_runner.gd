extends SceneTree
## Selection picking must hit-test the retail Geometry footprint, not a flat
## world-unit radius.
##
## Retail truth (PURE RETAIL rotwk tree,
## .private/retail-work/editions/rotwk/cache/effective-assets/data/ini):
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

var passed := 0
var failed := 0


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

	print("SELECTION_PICK_RESULT passed=%d failed=%d" % [passed, failed])
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
	# Attack orders keep a small extra margin; selection does not.
	_check(
		"order_margin_is_forgiving_but_bounded",
		Pick.closest_hit(Vector2(radius + 0.3, 0.0), candidates, Pick.ORDER_MARGIN) == 3
			and Pick.closest_hit(Vector2(5.5, 0.0), candidates, Pick.ORDER_MARGIN) == 0
	)


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


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("SELECTION_PICK_FAIL %s" % label)
