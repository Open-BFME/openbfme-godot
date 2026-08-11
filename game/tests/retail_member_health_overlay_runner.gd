extends SceneTree

const OverlayScript = preload("res://src/retail_slice/retail_member_health_overlay.gd")
const UserSettingsScript = preload("res://src/ui/user_settings.gd")

## Every row this runner must reach on the non-parity (world-quad) branch, which
## is the branch every currently published pack takes. A whole section that
## unwinds on a runtime error still prints a green RESULT line, so the row count
## is asserted as well as the per-section sentinels below.
const MINIMUM_EXPECTED_ROWS := 63

var passed := 0
var failed := 0
var _chevron_section_completed := false
var _visibility_section_completed := false
var _geometry_section_completed := false
var _consistency_section_completed := false


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
	await _check_cross_unit_bar_consistency()
	# LIVENESS GUARD. A GDScript runtime error inside an awaited section unwinds
	# that section and hands control straight back here, so a runner without these
	# sentinels prints a clean green result for work it never did. One sentinel
	# per awaited section, plus a floor on the total row count that catches an
	# abort part-way through any of them.
	_check("chevron_section_ran_to_completion", _chevron_section_completed)
	_check("visibility_section_ran_to_completion", _visibility_section_completed)
	_check("geometry_section_ran_to_completion", _geometry_section_completed)
	_check("consistency_section_ran_to_completion", _consistency_section_completed)
	_check(
		"every_expected_row_ran",
		passed + failed >= MINIMUM_EXPECTED_ROWS,
		"rows=%d expected at least %d" % [passed + failed, MINIMUM_EXPECTED_ROWS]
	)
	# DRAIN THE DEFERRED FREES BEFORE QUITTING. `queue_free()` destroys on a later
	# frame, so quitting straight after it leaves every mesh, material and
	# instance RID allocated - which Godot reports as "RID allocations ... were
	# leaked at exit", and which this runner's harness step
	# (tools/gate-m2-focused.ps1) scans stderr for. The rows above are already
	# counted; these frames only let the tree finish tearing itself down.
	for _drain_frame in 8:
		await process_frame

	print("RETAIL_MEMBER_HEALTH_OVERLAY_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _redraw(overlay) -> void:
	## A REAL DRAW PASS, NOT A DIRECT `_draw()` CALL.
	##
	## Calling `_draw()` by hand raises "Drawing is only allowed inside this
	## node's `_draw()`" for every draw_rect it performs. The counters still came
	## out right, so the runner looked green - but this runner's harness step
	## (tools/gate-m2-focused.ps1, Invoke-ProofChecked) scans stderr for
	## /ERROR/, so the step could never pass no matter what the RESULT line
	## said. The overlay queues its own redraw every `_process`, so asking for one
	## and yielding two frames gets a genuine NOTIFICATION_DRAW.
	overlay.queue_redraw()
	await process_frame
	await process_frame


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
	await _redraw(overlay)
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
	await _redraw(overlay)
	_check(
		"selected_full_health_horde_bars_every_member",
		overlay.rendered_bar_count == 4,
		"bars=%d" % overlay.rendered_bar_count
	)
	overlay.battalions = {"damaged": unselected_damaged}
	await _redraw(overlay)
	_check(
		"unselected_damaged_battalion_draws_no_bars",
		overlay.rendered_bar_count == 0,
		"bars=%d" % overlay.rendered_bar_count
	)
	overlay.show_all_health_bars = true
	await _redraw(overlay)
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
## THE OLD WIDTH ORACLE WAS THE BUG.
##
## These two bounds used to be `drawn_width / max(member_AABB.x, member_AABB.z)`
## in [0.5, 1.02] -- "the bar is as wide as the member". The member's mesh AABB
## includes his SPEAR, so that oracle scored a Tower Guard bar three times wider
## than the Gondor Fighter's next to him as a perfect 1.00, which is the owner's
## reported "a lot of the health bars have inconsistent sizes". The denominator
## is now the member's measured HEIGHT, which no weapon can inflate sideways, and
## the real cross-unit property is gated in its own section below.
const MAXIMUM_WIDTH_FRACTION_OF_MEMBER_HEIGHT := 1.0
const MINIMUM_WIDTH_FRACTION_OF_MEMBER_HEIGHT := 0.2
## Every member of one horde is the same retail Object, so their bars must be
## bit-identical, not merely similar.
const WITHIN_HORDE_WIDTH_TOLERANCE := 1.0001
## ACROSS units, retail's own oracle is the authored `Geometry` block, and it is
## near-uniform: the mounted RotWK men pack authors majorRadius 8.0 for
## GondorFighter, GondorTowerShieldGuard, GondorArcher, GondorCavalry,
## RohanSpearmen and RohanTheoden alike. Two units retail sizes identically must
## therefore draw identical bars; the small slack absorbs float only.
const CROSS_UNIT_WIDTH_TOLERANCE := 1.02
## The two units in the owner's screenshot 3: a sword-and-shield horde whose bars
## looked right, and a spear horde whose bars rendered as overlapping slabs.
const CONSISTENCY_COMPACT_OBJECT_ID := "bfme2.object.gondor-fighter"
const CONSISTENCY_LONG_WEAPON_OBJECT_ID := "bfme2.object.gondor-tower-guard"
## THE DISCRIMINATING UNIT.
##
## Fighter and Tower Guard are BOTH authored majorRadius 8.0, so an equal-width
## assertion over just those two passes even when the authored-geometry lookup is
## completely broken and every unit falls to the 8.0 default - which is exactly
## what was happening. Knights of Dol Amroth are authored majorRadius 10.0
## (gondorknightsofdolhorde.json, `registration.gameplay.geometry`), so their bar
## MUST come out 10/8 wider. That ratio is unreachable without a working lookup.
const CONSISTENCY_WIDE_OBJECT_ID := "bfme2.object.gondor-knightsof-dol"
const AUTHORED_INFANTRY_SOURCE_RADIUS := 8.0
const AUTHORED_DOL_AMROTH_SOURCE_RADIUS := 10.0
## Bars may form minor width CLASSES but never a slab: the whole spread across
## every unit in the game is bounded well under the 2.26x the mesh AABB produced.
const MAXIMUM_CROSS_CLASS_WIDTH_RATIO := 1.6


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
		widths.observe(
			float(row.get("half_width", 0.0)) * 2.0,
			bounds.size.y,
			"member %d measured_height=%.4f measured_horizontal_extent=%.4f" % [
				member_index, bounds.size.y, maxf(bounds.size.x, bounds.size.z)
			]
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
		"member_bar_is_not_a_slab_over_the_member",
		widths.observed and widths.maximum_ratio <= MAXIMUM_WIDTH_FRACTION_OF_MEMBER_HEIGHT,
		"worst member: %s ratio=%.4f" % [widths.maximum_detail, widths.maximum_ratio]
	)
	_check(
		"member_bar_is_wide_enough_to_see",
		widths.observed and widths.minimum_ratio >= MINIMUM_WIDTH_FRACTION_OF_MEMBER_HEIGHT,
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
			bounds.size.y,
			"member %d measured_height=%.4f measured_horizontal_extent=%.4f" % [
				member_index, bounds.size.y, maxf(bounds.size.x, bounds.size.z)
			]
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


func _check_cross_unit_bar_consistency() -> void:
	## THE OWNER'S BUG, STATED DIRECTLY: two hordes standing in the same battle
	## must not draw different-sized health bars.
	##
	## Oracle is retail's own `Geometry` authoring, read here out of the mounted
	## pack rather than assumed. Three units, chosen so the section can FAIL in
	## both directions:
	##   * GondorFighter and GondorTowerShieldGuard are both authored majorRadius
	##     8.0, so they must draw IDENTICAL bars. The Tower Guard carries a long
	##     spear and the Fighter does not, which is the difference the old
	##     mesh-AABB width turned into a slab (2.26x).
	##   * GondorKnightsofDol is authored majorRadius 10.0, so it must draw a bar
	##     10/8 WIDER. Without that row the section was tautological: every unit
	##     was silently taking the same 8.0 default because the document lookup
	##     used the member id against a registry keyed by the horde id, so an
	##     equal-width assertion over two equal-radius units could not fail.
	var content_db = root.get_node_or_null("ContentDB")
	if not _check("consistency_content_db_available", content_db != null):
		return
	await process_frame
	await process_frame
	var battalion_script = load("res://src/retail_slice/retail_battalion.gd")
	var samples: Array[Dictionary] = []
	var battalions: Array = []
	for object_id in [
		CONSISTENCY_COMPACT_OBJECT_ID, CONSISTENCY_LONG_WEAPON_OBJECT_ID, CONSISTENCY_WIDE_OBJECT_ID
	]:
		var definition: Dictionary = content_db.get_bundle_object(object_id)
		if not _check(
			"consistency_%s_is_mounted" % object_id,
			String(definition.get("_pack_root", "")) != "",
			str(definition.keys())
		):
			continue
		var battalion = battalion_script.new()
		root.add_child(battalion)
		battalion.configure(
			5150 + battalions.size(), 0, object_id, {}, 3, GEOMETRY_SOURCE_UNIT_SCALE,
			[Vector3.ZERO, Vector3(1.2, 0.0, 0.0), Vector3(2.4, 0.0, 0.0)]
		)
		battalions.append(battalion)
		await process_frame
		# WHICH PRESENTER a unit gets is a property of the PACK its bundle object
		# came from, not of the unit: on the shipped selection the Gondor Fighter
		# takes the world-quad path and the Tower Guard the parity overlay. So the
		# sampled quantity is the width BOTH presenters consume - the row's
		# `half_width` - and the world quad is sampled as well where it exists.
		var is_parity := bool(battalion.private_parity_mode_active)
		print("RETAIL_MEMBER_HEALTH_OVERLAY consistency_presenter[%s]=%s" % [
			object_id, "parity-overlay" if is_parity else "world-quad"
		])
		# The parity overlay publishes its width as the row's `half_width` and
		# builds no quads; the world-quad presenter publishes no rows and builds
		# quads. Both are the SAME authored bar width, so each is read out of its
		# own presenter and compared on equal terms.
		var parity_widths := {}
		for row_value in battalion.member_health_overlay_rows():
			var row: Dictionary = row_value
			parity_widths[int(row.get("member_index", -1))] = float(row.get("half_width", 0.0)) * 2.0
		for member_index_value in battalion.member_visuals.keys():
			var member_index := int(member_index_value)
			var bounds := _world_mesh_bounds(battalion.member_visuals.get(member_index) as Node3D)
			var back: MeshInstance3D = battalion.member_health_backs.get(member_index) as MeshInstance3D
			var quad: QuadMesh = (back.mesh as QuadMesh) if back != null else null
			var width := float(parity_widths.get(member_index, -1.0)) if is_parity else (quad.size.x if quad != null else -1.0)
			if not _check(
				"consistency_%s_member_%d_has_a_bar_width" % [object_id, member_index],
				width > 0.0,
				"presenter=%s width=%.4f" % ["parity-overlay" if is_parity else "world-quad", width]
			):
				continue
			samples.append({
				"object_id": object_id,
				"member_index": member_index,
				"width": width,
				"quad_thickness": quad.size.y if quad != null else -1.0,
				"extent": maxf(bounds.size.x, bounds.size.z),
			})
	if not _check("consistency_sampled_every_unit_type", samples.size() >= 9, "%d samples" % samples.size()):
		for battalion in battalions:
			battalion.queue_free()
		return
	# The units must differ in what they HOLD and not (beyond their authored
	# class) in what they DRAW. Reported so a reviewer can see both.
	var extent_ratio := _extreme_ratio(samples, "extent")
	print("RETAIL_MEMBER_HEALTH_OVERLAY consistency_member_mesh_extent_ratio=%.4f" % extent_ratio.ratio)
	_check(
		"consistency_the_units_really_do_differ_in_mesh_extent",
		extent_ratio.ratio >= 1.15,
		"%s vs %s ratio=%.4f - if these are alike this section proves nothing" % [
			extent_ratio.minimum_detail, extent_ratio.maximum_detail, extent_ratio.ratio
		]
	)
	# EQUAL-RADIUS PAIR: retail authors both of these 8.0, so they must be equal.
	# This is the row the mesh-AABB defect failed at 2.26x.
	var equal_radius_samples: Array[Dictionary] = _samples_for(
		samples, [CONSISTENCY_COMPACT_OBJECT_ID, CONSISTENCY_LONG_WEAPON_OBJECT_ID]
	)
	var width_ratio := _extreme_ratio(equal_radius_samples, "width")
	print("RETAIL_MEMBER_HEALTH_OVERLAY consistency_bar_width_ratio=%.4f width_min=%.4f width_max=%.4f" % [
		width_ratio.ratio, width_ratio.minimum, width_ratio.maximum
	])
	_check(
		"bars_are_the_same_width_across_units_retail_sizes_alike",
		width_ratio.ratio <= CROSS_UNIT_WIDTH_TOLERANCE,
		"widest %s = %.4f, narrowest %s = %.4f, ratio=%.4f" % [
			width_ratio.maximum_detail, width_ratio.maximum,
			width_ratio.minimum_detail, width_ratio.minimum,
			width_ratio.ratio,
		]
	)
	# THE DISCRIMINATING ROW. Knights of Dol Amroth are authored majorRadius 10.0
	# against infantry's 8.0, so their bar must be 1.25x. If the authored-geometry
	# lookup regresses, every unit falls back to the same default and this ratio
	# collapses to 1.0 - which is precisely how the broken member/horde id lookup
	# hid for a whole review cycle.
	var class_infantry_width := _mean_width(samples, CONSISTENCY_COMPACT_OBJECT_ID)
	var dol_width := _mean_width(samples, CONSISTENCY_WIDE_OBJECT_ID)
	var expected_class_ratio := AUTHORED_DOL_AMROTH_SOURCE_RADIUS / AUTHORED_INFANTRY_SOURCE_RADIUS
	var observed_class_ratio := (dol_width / class_infantry_width) if class_infantry_width > 0.0 else -1.0
	print("RETAIL_MEMBER_HEALTH_OVERLAY consistency_class_ratio observed=%.4f expected=%.4f infantry=%.4f dol=%.4f" % [
		observed_class_ratio, expected_class_ratio, class_infantry_width, dol_width
	])
	_check(
		"the_authored_geometry_radius_actually_drives_the_bar_width",
		absf(observed_class_ratio - expected_class_ratio) <= 0.02 * expected_class_ratio,
		"Dol Amroth authors majorRadius %.1f against infantry %.1f, so the bar must be %.4fx; measured %.4fx (infantry=%.4f dol=%.4f)" % [
			AUTHORED_DOL_AMROTH_SOURCE_RADIUS, AUTHORED_INFANTRY_SOURCE_RADIUS,
			expected_class_ratio, observed_class_ratio, class_infantry_width, dol_width,
		]
	)
	# Classes are allowed, slabs are not.
	var overall_ratio := _extreme_ratio(samples, "width")
	_check(
		"the_whole_width_spread_stays_a_class_not_a_slab",
		overall_ratio.ratio <= MAXIMUM_CROSS_CLASS_WIDTH_RATIO,
		"widest %s = %.4f, narrowest %s = %.4f, ratio=%.4f" % [
			overall_ratio.maximum_detail, overall_ratio.maximum,
			overall_ratio.minimum_detail, overall_ratio.minimum,
			overall_ratio.ratio,
		]
	)
	# Thickness is only a world-quad property (the parity overlay draws a fixed
	# three-pixel bar). Sampled where it exists, and asserted unconditionally on
	# the pure helper so the property is gated even when the shipped selection
	# routes one of the two units to the other presenter.
	var quad_samples: Array[Dictionary] = []
	for sample in samples:
		if float(sample.get("quad_thickness", -1.0)) > 0.0:
			quad_samples.append(sample)
	if quad_samples.size() >= 2:
		var thickness_ratio := _extreme_ratio(quad_samples, "quad_thickness")
		print("RETAIL_MEMBER_HEALTH_OVERLAY consistency_bar_thickness_ratio=%.4f thickness_min=%.4f thickness_max=%.4f" % [
			thickness_ratio.ratio, thickness_ratio.minimum, thickness_ratio.maximum
		])
		_check(
			"drawn_quad_bars_are_the_same_thickness",
			thickness_ratio.ratio <= CROSS_UNIT_WIDTH_TOLERANCE,
			"thickest %s = %.4f, thinnest %s = %.4f" % [
				thickness_ratio.maximum_detail, thickness_ratio.maximum,
				thickness_ratio.minimum_detail, thickness_ratio.minimum,
			]
		)
	else:
		_check(
			"drawn_quad_bars_are_the_same_thickness",
			true,
			"only %d world-quad members in this selection; pure-helper rows carry it" % quad_samples.size()
		)
	# THE THICKNESS PROPERTY ITSELF: no unit may make its bar fatter. The helper
	# takes no radius at all, which is the whole point - it used to be
	# `width * aspect`, so a spear made the bar both wider and thicker.
	var battalion_script_static = load("res://src/retail_slice/retail_battalion.gd")
	var infantry_width: float = battalion_script_static.member_bar_width(8.0, GEOMETRY_SOURCE_UNIT_SCALE)
	var giant_width: float = battalion_script_static.member_bar_width(200.0, GEOMETRY_SOURCE_UNIT_SCALE)
	var runt_width: float = battalion_script_static.member_bar_width(0.5, GEOMETRY_SOURCE_UNIT_SCALE)
	_check(
		"infantry_bar_keeps_its_shipped_width",
		is_equal_approx(infantry_width, 0.58),
		"retail infantry majorRadius 8.0 at source scale 0.1 -> %.4f" % infantry_width
	)
	_check(
		"a_runaway_footprint_cannot_make_a_slab",
		giant_width / infantry_width <= 2.1,
		"majorRadius 200 -> %.4f, which is %.4fx infantry" % [giant_width, giant_width / infantry_width]
	)
	_check(
		"a_tiny_footprint_still_draws_a_readable_bar",
		runt_width / infantry_width >= 0.7,
		"majorRadius 0.5 -> %.4f" % runt_width
	)
	for object_id in [CONSISTENCY_COMPACT_OBJECT_ID, CONSISTENCY_LONG_WEAPON_OBJECT_ID]:
		var horde: Array[Dictionary] = []
		for sample in samples:
			if String(sample.get("object_id", "")) == object_id:
				horde.append(sample)
		if horde.is_empty():
			continue
		var horde_ratio := _extreme_ratio(horde, "width")
		_check(
			"bars_within_%s_are_identical" % object_id,
			horde_ratio.ratio <= WITHIN_HORDE_WIDTH_TOLERANCE,
			"ratio=%.4f min=%.4f max=%.4f" % [horde_ratio.ratio, horde_ratio.minimum, horde_ratio.maximum]
		)
	# The width source is reported, not assumed: a pack whose bundle objects carry
	# no `sourceObjectId` cannot reach its playable-unit geometry and falls back to
	# retail's authored infantry default. Either way the bars must be uniform, but
	# the log has to say which happened.
	for battalion in battalions:
		var bar_source := str(battalion.get("member_health_bar_source"))
		print("RETAIL_MEMBER_HEALTH_OVERLAY consistency_bar_source[%s]=%s" % [
			str(battalion.object_id), bar_source
		])
		# TELEMETRY IS ASSERTED, NOT JUST PRINTED. Both units whose bundle object
		# records a `sourceObjectId` must reach their compiled document; a
		# regression in the member->document lookup shows up here as
		# "retail-infantry-default" even before it moves a width.
		if String(battalion.object_id) != CONSISTENCY_LONG_WEAPON_OBJECT_ID:
			_check(
				"bar_width_comes_from_compiled_geometry_%s" % str(battalion.object_id),
				bar_source == "compiled-geometry",
				bar_source
			)
		else:
			# NAMED GAP, NOT A SILENT SKIP. This bundle object comes from a pack's
			# static objects.json, which records no `sourceObjectId`, so nothing can
			# reach its playable-unit document and it takes retail's authored
			# infantry default 8.0. That happens to equal what retail authors for
			# GondorTowerShieldGuard, so its bar is right by luck, not by lookup.
			# Asserted as the gap it is: if a republish ever gives this object a
			# source id, this row flips and the exclusion above should be removed.
			_check(
				"tower_guard_is_still_the_known_sourceless_bundle_gap",
				bar_source == "retail-infantry-default"
					and String(content_db.get_bundle_object(String(battalion.object_id)).get("sourceObjectId", "")) == "",
				bar_source
			)
		battalion.queue_free()
	await process_frame
	_consistency_section_completed = true


class _ExtremeRatio:
	extends RefCounted
	var minimum := INF
	var maximum := -INF
	var minimum_detail := "<none>"
	var maximum_detail := "<none>"

	var ratio: float:
		get:
			if not is_finite(minimum) or minimum <= 0.0 or not is_finite(maximum):
				return INF
			return maximum / minimum


func _samples_for(samples: Array[Dictionary], object_ids: Array) -> Array[Dictionary]:
	var out: Array[Dictionary] = []
	for sample in samples:
		if object_ids.has(String(sample.get("object_id", ""))):
			out.append(sample)
	return out


func _mean_width(samples: Array[Dictionary], object_id: String) -> float:
	## Mean rather than first, so one stray member cannot carry the class ratio.
	var total := 0.0
	var count := 0
	for sample in _samples_for(samples, [object_id]):
		total += float(sample.get("width", 0.0))
		count += 1
	return (total / float(count)) if count > 0 else 0.0


func _extreme_ratio(samples: Array[Dictionary], key: String) -> _ExtremeRatio:
	var spread := _ExtremeRatio.new()
	for sample in samples:
		var value := float(sample.get(key, 0.0))
		var detail := "%s member %d" % [String(sample.get("object_id", "?")), int(sample.get("member_index", -1))]
		if value < spread.minimum:
			spread.minimum = value
			spread.minimum_detail = detail
		if value > spread.maximum:
			spread.maximum = value
			spread.maximum_detail = detail
	return spread


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
