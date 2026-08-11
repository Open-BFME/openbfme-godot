extends SceneTree

const OverlayScript = preload("res://src/retail_slice/retail_member_health_overlay.gd")
const UserSettingsScript = preload("res://src/ui/user_settings.gd")

## Every row this runner must reach on the non-parity (world-quad) branch, which
## is the branch every currently published pack takes. A whole section that
## unwinds on a runtime error still prints a green RESULT line, so the row count
## is asserted as well as the per-section sentinels below.
const MINIMUM_EXPECTED_ROWS := 33

var passed := 0
var failed := 0
var _chevron_section_completed := false
var _visibility_section_completed := false
var _geometry_section_completed := false


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_MEMBER_HEALTH_OVERLAY_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_check("source_minimum_infantry_width", is_equal_approx(OverlayScript.SOURCE_MINIMUM_INFANTRY_WIDTH_PIXELS, 40.0))
	_check("source_fixed_height", is_equal_approx(OverlayScript.SOURCE_HEIGHT_PIXELS, 3.0))
	_check("source_outline_width", is_equal_approx(OverlayScript.SOURCE_OUTLINE_PIXELS, 1.0))
	_check("far_zoom_uses_source_minimum_width", is_equal_approx(OverlayScript.source_health_width_for_zoom(1.0), 40.0))
	_check("close_zoom_uses_source_camera_height_ratio", is_equal_approx(OverlayScript.source_health_width_for_zoom(0.0), 100.0))
	_check("health_width_is_bounded_outside_zoom_range", is_equal_approx(OverlayScript.source_health_width_for_zoom(-1.0), 100.0) and is_equal_approx(OverlayScript.source_health_width_for_zoom(2.0), 40.0))
	# RETAIL'S RULE: selected-only, plus the authored opt-in. REF-45 (a selected
	# horde bars every member, full health included), REF-40 (a mass melee shows
	# no bars over unselected damaged enemies), lotr.str:2616-2622
	# (APT:EnableHealthBars, the "Show All Health Bars" option that only exists
	# because the default is selected-only).
	_check("unselected_battalion_draws_no_bar", not OverlayScript.should_show_battalion(false, false))
	_check("selected_battalion_draws_bars", OverlayScript.should_show_battalion(true, false))
	_check("show_all_setting_bars_the_unselected", OverlayScript.should_show_battalion(false, true))
	# The SHIPPED default is off. The value stored on the machine running this
	# runner is deliberately NOT asserted - it is a player setting, and pinning it
	# would make the gate pass or fail on whatever the last person clicked.
	_check("show_all_health_bars_defaults_off", not UserSettingsScript.DEFAULT_SHOW_ALL_HEALTH_BARS)
	_check_colors("full_health", 1.0, Color(0.0, 1.0, 0.0, 1.0), Color(0.0, 0.5, 0.0, 1.0))
	_check_colors("three_quarter_health", 0.75, Color(0.25, 1.0, 0.0, 1.0), Color(0.25, 0.5, 0.0, 1.0))
	_check_colors("half_health", 0.5, Color(0.5, 1.0, 0.0, 1.0), Color(0.5, 0.5, 0.0, 1.0))
	_check_colors("damaged_health", 0.4, Color(1.0, 0.8, 0.0, 1.0), Color(0.5, 0.4, 0.0, 1.0))
	_check_colors("really_damaged_health", 0.25, Color(1.0, 0.25, 0.0, 1.0), Color(0.5, 0.25, 0.0, 1.0))
	await _check_rank_chevrons()
	await _check_selected_only_visibility()
	await _check_member_bar_geometry()
	# LIVENESS GUARD. A GDScript runtime error inside an awaited section unwinds
	# that section and hands control straight back here, so a runner without these
	# sentinels prints a clean green result for work it never did. One sentinel
	# per awaited section, plus a floor on the total row count that catches an
	# abort part-way through any of them.
	_check("chevron_section_ran_to_completion", _chevron_section_completed)
	_check("visibility_section_ran_to_completion", _visibility_section_completed)
	_check("geometry_section_ran_to_completion", _geometry_section_completed)
	_check(
		"every_expected_row_ran",
		passed + failed >= MINIMUM_EXPECTED_ROWS,
		"rows=%d expected at least %d" % [passed + failed, MINIMUM_EXPECTED_ROWS]
	)

	print("RETAIL_MEMBER_HEALTH_OVERLAY_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check_rank_chevrons() -> void:
	## Veterancy pips: the battalion's live level rides the overlay rows, and
	## rank 2+ draws one placeholder chevron per earned rank above the bar.
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.global_position = Vector3(0.0, 30.0, 30.0)
	camera.look_at(Vector3.ZERO, Vector3.UP)
	camera.current = true
	var veteran := FakeBattalion.new(3, 2, true)
	var rookie := FakeBattalion.new(1, 1, true)
	var overlay := OverlayScript.new()
	root.add_child(overlay)
	overlay.configure(null, camera, {"veteran": veteran, "rookie": rookie})
	await process_frame
	await process_frame
	overlay._draw()
	_check(
		"rank_chevrons_track_experience_level",
		overlay.rendered_chevron_count == 2,
		"chevrons=%d" % overlay.rendered_chevron_count
	)
	_check("rank_one_draws_no_chevrons", overlay.rendered_bar_count == 3, "bars=%d" % overlay.rendered_bar_count)
	veteran.free()
	rookie.free()
	overlay.free()
	camera.free()
	_chevron_section_completed = true


func _check_selected_only_visibility() -> void:
	## THE WALL OF SLABS, AND WHAT REPLACED IT. The overlay used to draw a bar
	## over every visible ENEMY whatever his health (`team != 0 or is_selected`).
	## Retail's rule is selection, not damage: a selected horde bars every member
	## including full-health ones (REF-45), a mass melee shows nothing over
	## unselected damaged enemies (REF-40), and the player may opt into all of
	## them (lotr.str APT:EnableHealthBars).
	var camera := Camera3D.new()
	root.add_child(camera)
	camera.global_position = Vector3(0.0, 30.0, 30.0)
	camera.look_at(Vector3.ZERO, Vector3.UP)
	camera.current = true
	var selected_full_health := FakeBattalion.new(1, 4, true)
	var unselected_damaged := FakeBattalion.new(1, 4, false)
	unselected_damaged.team = 1
	unselected_damaged.damaged = true
	var overlay := OverlayScript.new()
	root.add_child(overlay)
	overlay.configure(null, camera, {"selected": selected_full_health})
	overlay.show_all_health_bars = false
	await process_frame
	await process_frame
	overlay._draw()
	_check(
		"selected_full_health_horde_bars_every_member",
		overlay.rendered_bar_count == 4,
		"bars=%d" % overlay.rendered_bar_count
	)
	overlay.battalions = {"damaged": unselected_damaged}
	overlay._draw()
	_check(
		"unselected_damaged_battalion_draws_no_bars",
		overlay.rendered_bar_count == 0,
		"bars=%d" % overlay.rendered_bar_count
	)
	overlay.show_all_health_bars = true
	overlay._draw()
	_check(
		"show_all_setting_bars_the_unselected_battalion",
		overlay.rendered_bar_count == 4,
		"bars=%d" % overlay.rendered_bar_count
	)
	selected_full_health.free()
	unselected_damaged.free()
	overlay.free()
	camera.free()
	_visibility_section_completed = true


const GEOMETRY_OBJECT_ID := "bfme2.object.gondor-fighter"
## The slice's validated source-to-local scale (retail_slice_sim.gd rules
## default `source_unit_scale`). A Gondor fighter is 19.2 SAGE units tall, so a
## member stands about 1.92 world units.
const GEOMETRY_SOURCE_UNIT_SCALE := 0.1
## World-unit clearance permitted between a member's real visual top and his
## health-bar anchor. The old anchor - (19.2 + 10) * scale above the member's
## ORIGIN - cleared his head by about one world unit, half a body height.
## BOTH ends are asserted per member: a max-only bound passed a battalion in
## which two of three bars were buried inside their soldiers as long as one
## cleared, and a min-only bound would pass the old floating slab.
const MAXIMUM_HEAD_CLEARANCE := 0.25
const MINIMUM_HEAD_CLEARANCE := 0.0
## The drawn bar must be a real, member-sized bar. Without a lower bound a
## 0.001-unit invisible sliver satisfied "no wider than the member" perfectly.
const MINIMUM_WIDTH_FRACTION_OF_MEMBER := 0.5


func _check_member_bar_geometry() -> void:
	## THE BAR MUST RIDE THE MEMBER'S OWN MEASURED GEOMETRY.
	##
	## Oracle: the member visual actually built from the mounted pack. Its AABB is
	## measured here independently of retail_battalion's own helper, in world
	## space, so this cannot pass by agreeing with the code under test.
	var content_db = root.get_node_or_null("ContentDB")
	if not _check("geometry_content_db_available", content_db != null):
		return
	# ContentDB resolves pack content over its first frames; no member GLB exists
	# before that.
	await process_frame
	await process_frame
	var definition: Dictionary = content_db.get_bundle_object(GEOMETRY_OBJECT_ID)
	var pack_root := String(definition.get("_pack_root", ""))
	if not _check("geometry_member_object_is_mounted", pack_root != "", str(definition.keys())):
		return
	print("RETAIL_MEMBER_HEALTH_OVERLAY geometry_pack_root=%s" % pack_root)
	var battalion_script = load("res://src/retail_slice/retail_battalion.gd")
	var battalion = battalion_script.new()
	root.add_child(battalion)
	battalion.configure(
		4242, 0, GEOMETRY_OBJECT_ID, {}, 3, GEOMETRY_SOURCE_UNIT_SCALE,
		[Vector3.ZERO, Vector3(1.2, 0.0, 0.0), Vector3(2.4, 0.0, 0.0)]
	)
	await process_frame
	# Which presenter is live is a property of the MOUNTED pack, not of this
	# runner: a pack declaring the full retail presentation surfaces uses the
	# screen-space overlay, every other pack uses the world-space billboard
	# quads. Both are gated, and the log says which one ran.
	print("RETAIL_MEMBER_HEALTH_OVERLAY geometry_parity_mode=%s" % str(battalion.private_parity_mode_active))
	# The presenter must CONSUME the player's stored opt-in. Asserted as "equals
	# whatever the store says", never as a fixed value, so this holds on a machine
	# where the option has been turned on.
	_check(
		"battalion_reads_the_stored_show_all_health_bars_setting",
		bool(battalion.get("show_all_health_bars"))
			== bool(UserSettingsScript.load_controls().get("show_all_health_bars", false)),
		"battalion=%s store=%s" % [
			str(battalion.get("show_all_health_bars")),
			str(UserSettingsScript.load_controls().get("show_all_health_bars", false)),
		]
	)
	# Everything below states the retail rule, so the runner owns the setting from
	# here rather than inheriting this machine's.
	battalion.show_all_health_bars = false
	# `get()` rather than a property access: a build that never introduced the
	# field must FAIL this row, not abort the whole section.
	var anchor_source := str(battalion.get("member_health_anchor_source"))
	_check(
		"geometry_anchor_comes_from_the_measured_visual",
		anchor_source == "measured-visual-bounds",
		anchor_source
	)
	if not bool(battalion.private_parity_mode_active):
		await _check_world_quad_bar_geometry(battalion)
		battalion.queue_free()
		await process_frame
		_geometry_section_completed = true
		return
	var rows: Array = battalion.member_health_overlay_rows()
	if not _check("geometry_rows_cover_every_member", rows.size() == 3, str(rows.size())):
		battalion.queue_free()
		return
	var clearances := _ClearanceSpread.new()
	var widths := _WidthSpread.new()
	var measured_members := 0
	for row_value in rows:
		var row: Dictionary = row_value
		var member_index := int(row.get("member_index", -1))
		var visual: Node3D = battalion.member_visuals.get(member_index) as Node3D
		var bounds := _world_mesh_bounds(visual)
		if bounds.size.y <= 0.0:
			_check("geometry_member_%d_has_measurable_geometry" % member_index, false)
			continue
		measured_members += 1
		var anchor_y := (row.get("world_position", Vector3.ZERO) as Vector3).y
		clearances.observe(
			anchor_y - bounds.end.y,
			"member %d anchor_y=%.4f visual_top=%.4f height=%.4f" % [
				member_index, anchor_y, bounds.end.y, bounds.size.y
			]
		)
		var measured_half := maxf(bounds.size.x, bounds.size.z) * 0.5
		widths.observe(
			float(row.get("half_width", 0.0)),
			measured_half,
			"member %d measured_half=%.4f" % [member_index, measured_half]
		)
	_check("geometry_every_member_was_measured", measured_members == rows.size(), "%d of %d" % [measured_members, rows.size()])
	_report_bar_geometry(clearances, widths)
	battalion.queue_free()
	await process_frame
	_geometry_section_completed = true


func _report_bar_geometry(clearances: _ClearanceSpread, widths: _WidthSpread) -> void:
	print("RETAIL_MEMBER_HEALTH_OVERLAY clearance_min=%.4f (%s) clearance_max=%.4f (%s)" % [
		clearances.minimum, clearances.minimum_detail, clearances.maximum, clearances.maximum_detail
	])
	print("RETAIL_MEMBER_HEALTH_OVERLAY width_ratio_min=%.4f (%s) width_ratio_max=%.4f (%s)" % [
		widths.minimum_ratio, widths.minimum_detail, widths.maximum_ratio, widths.maximum_detail
	])
	# EVERY member, both ends. Reported as one row per property with the extreme
	# member named, so a single buried or floating bar in a battalion of three
	# cannot hide behind its neighbours.
	_check(
		"member_bar_never_sinks_into_the_member",
		clearances.observed and clearances.minimum >= MINIMUM_HEAD_CLEARANCE,
		"worst member: %s clearance=%.4f" % [clearances.minimum_detail, clearances.minimum]
	)
	_check(
		"member_bar_never_floats_above_the_member",
		clearances.observed and clearances.maximum <= MAXIMUM_HEAD_CLEARANCE,
		"worst member: %s clearance=%.4f" % [clearances.maximum_detail, clearances.maximum]
	)
	_check(
		"member_bar_is_no_wider_than_the_member",
		widths.observed and widths.maximum_ratio <= 1.0 + 0.02,
		"worst member: %s ratio=%.4f" % [widths.maximum_detail, widths.maximum_ratio]
	)
	_check(
		"member_bar_is_wide_enough_to_see",
		widths.observed and widths.minimum_ratio >= MINIMUM_WIDTH_FRACTION_OF_MEMBER,
		"worst member: %s ratio=%.4f" % [widths.minimum_detail, widths.minimum_ratio]
	)


class _ClearanceSpread:
	extends RefCounted
	var observed := false
	var minimum := INF
	var maximum := -INF
	var minimum_detail := "<none>"
	var maximum_detail := "<none>"

	func observe(clearance: float, detail: String) -> void:
		observed = true
		if clearance < minimum:
			minimum = clearance
			minimum_detail = detail
		if clearance > maximum:
			maximum = clearance
			maximum_detail = detail


class _WidthSpread:
	extends RefCounted
	var observed := false
	var minimum_ratio := INF
	var maximum_ratio := -INF
	var minimum_detail := "<none>"
	var maximum_detail := "<none>"

	func observe(drawn: float, member_extent: float, detail: String) -> void:
		observed = true
		var ratio := (drawn / member_extent) if member_extent > 0.0 else -1.0
		if ratio < minimum_ratio:
			minimum_ratio = ratio
			minimum_detail = detail
		if ratio > maximum_ratio:
			maximum_ratio = ratio
			maximum_detail = detail


func _check_world_quad_bar_geometry(battalion) -> void:
	## The presenter every non-parity pack actually renders: one billboarded quad
	## per member. Same properties as the screen-space overlay - the bar clears
	## the measured head of EVERY member by a hair and no more, is neither wider
	## than him nor an invisible sliver, travels with him, and obeys retail's
	## selected-only visibility rule.
	var clearances := _ClearanceSpread.new()
	var widths := _WidthSpread.new()
	var measured_members := 0
	for member_index_value in battalion.member_visuals.keys():
		var member_index := int(member_index_value)
		var visual: Node3D = battalion.member_visuals[member_index] as Node3D
		var back: MeshInstance3D = battalion.member_health_backs.get(member_index) as MeshInstance3D
		if not _check("quad_member_%d_has_a_bar" % member_index, back != null):
			return
		var bounds := _world_mesh_bounds(visual)
		if bounds.size.y <= 0.0:
			_check("quad_member_%d_has_measurable_geometry" % member_index, false)
			continue
		measured_members += 1
		clearances.observe(
			back.global_position.y - bounds.end.y,
			"member %d bar_y=%.4f visual_top=%.4f height=%.4f" % [
				member_index, back.global_position.y, bounds.end.y, bounds.size.y
			]
		)
		var quad := back.mesh as QuadMesh
		widths.observe(
			quad.size.x if quad != null else -1.0,
			maxf(bounds.size.x, bounds.size.z),
			"member %d measured_width=%.4f" % [member_index, maxf(bounds.size.x, bounds.size.z)]
		)
	_check(
		"quad_every_member_was_measured",
		measured_members == battalion.member_visuals.size(),
		"%d of %d" % [measured_members, battalion.member_visuals.size()]
	)
	_report_bar_geometry(clearances, widths)

	# The bar follows the soldier. A battalion that has walked or wheeled moves
	# every member off its authored formation slot.
	var moved_index := int(battalion.member_visuals.keys()[0])
	var moved_visual: Node3D = battalion.member_visuals[moved_index] as Node3D
	moved_visual.position += Vector3(3.0, 0.0, 4.0)
	battalion.selected = true
	battalion._refresh_member_overlays()
	await process_frame
	var moved_back: MeshInstance3D = battalion.member_health_backs.get(moved_index) as MeshInstance3D
	var horizontal_drift := Vector2(
		moved_back.position.x - moved_visual.position.x,
		moved_back.position.z - moved_visual.position.z
	).length()
	_check(
		"member_bar_follows_its_member",
		horizontal_drift <= 0.01,
		"bar=%s member=%s drift=%.4f" % [str(moved_back.position), str(moved_visual.position), horizontal_drift]
	)
	# Retail's rule in the live presenter: selection shows the bar, damage alone
	# does not, and the authored opt-in overrides.
	_check("quad_selected_member_bar_is_visible", moved_back.visible)
	battalion.selected = false
	battalion.member_health_ratios[moved_index] = 0.5
	battalion._refresh_member_overlays()
	_check(
		"quad_unselected_damaged_member_draws_no_bar",
		not moved_back.visible,
		"damage alone must not summon a bar (REF-40)"
	)
	battalion.show_all_health_bars = true
	battalion._refresh_member_overlays()
	_check("quad_show_all_setting_bars_the_unselected_member", moved_back.visible)
	battalion.show_all_health_bars = false


func _world_mesh_bounds(visual: Node3D) -> AABB:
	## Independent world-space AABB of everything a member actually draws.
	var bounds := AABB()
	var measured := false
	if visual == null or not is_instance_valid(visual):
		return bounds
	var pending: Array[Node] = [visual]
	while not pending.is_empty():
		var node: Node = pending.pop_back()
		if node is MeshInstance3D and (node as MeshInstance3D).mesh != null:
			var mesh_instance := node as MeshInstance3D
			var mesh_bounds := mesh_instance.global_transform * mesh_instance.get_aabb()
			bounds = bounds.merge(mesh_bounds) if measured else mesh_bounds
			measured = true
		for child in node.get_children():
			if child is Node:
				pending.append(child as Node)
	return bounds if measured else AABB()


class FakeBattalion:
	extends Node
	var team := 0
	var selected := true
	var damaged := false
	var _level := 1
	var _members := 1


	func _init(level: int, members: int, is_selected: bool) -> void:
		_level = level
		_members = members
		selected = is_selected


	func member_health_overlay_rows() -> Array[Dictionary]:
		var rows: Array[Dictionary] = []
		for index in range(_members):
			rows.append({
				"member_index": index,
				"health_ratio": 0.5 if damaged else 1.0,
				"world_position": Vector3(float(index), 0.0, 0.0),
				"experience_level": _level,
			})
		return rows


func _check_colors(name: String, ratio: float, expected_fill: Color, expected_outline: Color) -> void:
	var colors: Dictionary = OverlayScript.source_health_colors(ratio)
	_check(name, (colors.fill as Color).is_equal_approx(expected_fill) and (colors.outline as Color).is_equal_approx(expected_outline), str(colors))


func _check(name: String, condition: bool, detail: String = "") -> bool:
	if condition:
		passed += 1
		print("RETAIL_MEMBER_HEALTH_OVERLAY PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_MEMBER_HEALTH_OVERLAY FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
	return condition
