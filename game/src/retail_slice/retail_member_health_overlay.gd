class_name RetailMemberHealthOverlay
extends Control
## Screen-space member health bars for the private retail slice: one small bar
## just above each soldier of a SELECTED or DAMAGED battalion.
##
## SAGE computes one UI region per drawable, fixes the bar height at three
## pixels, draws a one-pixel outline, and scales the width by tactical zoom.
## Keeping this in CanvasItem space avoids the large billboard quads that were
## previously used as a gameplay approximation.
##
## Two properties keep a bar attached to its soldier rather than floating over
## him as a slab, and both mirror the structure presenter (retail_structure.gd):
##   * the anchor is the member's MEASURED visual top plus a small gap, supplied
##     by retail_battalion.member_health_overlay_rows(), never a table constant;
##   * the drawn width is bounded by the member's own projected footprint, so it
##     shrinks with him as the camera pulls out instead of holding 40..100px.
##
## Veterancy: a battalion at rank 2+ draws one small chevron pip per earned
## rank above its first rendered bar — placeholder-styled after the retail
## rank badges (reference/INDEX.md REF-24/REF-44), no retail art claims.

const SOURCE_MINIMUM_INFANTRY_WIDTH_PIXELS := 40.0
## Floor for the member-bounded width. Below this a bar carries no readable fill,
## and the distance gate has already dropped anything that far away.
const MINIMUM_DRAWN_WIDTH_PIXELS := 10.0
const SOURCE_HEIGHT_PIXELS := 3.0
const SOURCE_OUTLINE_PIXELS := 1.0
const SOURCE_CLOSE_CAMERA_HEIGHT := 120.0
const SOURCE_FAR_CAMERA_HEIGHT := 300.0
const CHEVRON_WIDTH_PIXELS := 5.0
const CHEVRON_HEIGHT_PIXELS := 3.0
const CHEVRON_SPACING_PIXELS := 2.0
const CHEVRON_LIFT_PIXELS := 2.0
const CHEVRON_COLOR := Color(0.95, 0.85, 0.35, 0.95)
## Distance LOD. Past this range a three-pixel bar is sub-pixel on screen, but
## building its rows still costs one Dictionary allocation per living member per
## frame - the dominant cost of this overlay at large army sizes. Whole
## battalions are therefore rejected before their rows are built, matching the
## tier at which src/view/member_lod_policy.gd drops per-member decoration.
const SOURCE_MAXIMUM_OVERLAY_DISTANCE := 150.0

var tactical_view: Node
var tactical_camera: Camera3D
var battalions: Dictionary
var rendered_bar_count := 0
var rendered_chevron_count := 0
## Diagnostic mirror of the last drawn bars, populated only when a capture or a
## gate asks for it. Rows: {"battalion", "member_index", "region", "anchor"}.
var collect_debug_regions := false
var debug_bar_regions: Array[Dictionary] = []
## Battalions skipped by the distance gate on the last draw. Diagnostic only.
var distance_culled_battalion_count := 0


func configure(view: Node, camera: Camera3D, battalion_nodes: Dictionary) -> void:
	tactical_view = view
	tactical_camera = camera
	battalions = battalion_nodes
	mouse_filter = Control.MOUSE_FILTER_IGNORE
	set_anchors_and_offsets_preset(Control.PRESET_FULL_RECT)
	set_process(true)


func _process(_delta: float) -> void:
	queue_redraw()


func _draw() -> void:
	rendered_bar_count = 0
	rendered_chevron_count = 0
	distance_culled_battalion_count = 0
	if collect_debug_regions:
		debug_bar_regions.clear()
	if tactical_camera == null or not is_instance_valid(tactical_camera):
		return
	var camera_position := tactical_camera.global_position
	var zoom := 1.0
	if tactical_view != null and is_instance_valid(tactical_view):
		zoom = float(tactical_view.get("camera_zoom"))
	var width_bound := source_health_width_for_zoom(zoom)
	# The screen-space offset of one world unit measured across the camera's own
	# right axis: how a member's measured half-width becomes a pixel width.
	var camera_right := tactical_camera.global_transform.basis.x.normalized()
	var viewport_rect := get_viewport_rect()
	for battalion_value in battalions.values():
		if not battalion_value is Node or not (battalion_value as Node).has_method("member_health_overlay_rows"):
			continue
		var battalion := battalion_value as Node
		var battalion_selected := bool(battalion.get("selected"))
		var battalion_damaged := (
			bool(battalion.call("has_damaged_member"))
			if battalion.has_method("has_damaged_member")
			else false
		)
		if not should_show_battalion(battalion_selected, battalion_damaged):
			continue
		# Distance gate before member_health_overlay_rows(), which allocates one
		# Dictionary per living member each frame.
		if battalion is Node3D:
			var battalion_origin := (battalion as Node3D).global_position
			if not should_draw_battalion_at_distance(camera_position.distance_to(battalion_origin)):
				distance_culled_battalion_count += 1
				continue
		var chevron_anchor := Vector2.INF
		var chevron_pips := 0
		for row_value in battalion.call("member_health_overlay_rows"):
			var row := row_value as Dictionary
			var ratio := clampf(float(row.get("health_ratio", 0.0)), 0.0, 1.0)
			if ratio <= 0.0:
				continue
			# Retail shows a soldier's bar while his horde is selected or he is
			# hurt. A full-health idle army - friendly or enemy - carries none.
			if not battalion_selected and ratio >= 0.999:
				continue
			var world_position := row.get("world_position", Vector3.ZERO) as Vector3
			if tactical_camera.is_position_behind(world_position):
				continue
			var screen_center := tactical_camera.unproject_position(world_position)
			# FOOTPRINT-BOUNDED, LIKE THE STRUCTURE BAR.
			#
			# The zoom curve alone gave every soldier a 40..100px bar whatever his
			# apparent size, so a zoomed-out army rendered as a wall of slabs wider
			# than the men under them. The member's own measured half-width is
			# projected through the camera and caps the source width.
			var width := width_bound
			var half_width := float(row.get("half_width", 0.0))
			if half_width > 0.0:
				var edge := tactical_camera.unproject_position(world_position + camera_right * half_width)
				var projected := absf(edge.x - screen_center.x) * 2.0
				width = clampf(projected, MINIMUM_DRAWN_WIDTH_PIXELS, width_bound)
			# The source region starts at center - 45% of its width rather than
			# exactly centering the bar, and sits entirely ABOVE the anchor the
			# battalion measured at the member's visual top.
			var origin := Vector2(
				screen_center.x - width * 0.45,
				screen_center.y - SOURCE_HEIGHT_PIXELS
			)
			var region := Rect2(origin, Vector2(width, SOURCE_HEIGHT_PIXELS))
			if not viewport_rect.intersects(region):
				continue
			if collect_debug_regions:
				debug_bar_regions.append({
					"battalion": battalion.name,
					"member_index": int(row.get("member_index", -1)),
					"region": region,
					"anchor": screen_center,
				})
			if chevron_anchor == Vector2.INF:
				chevron_anchor = origin
				chevron_pips = maxi(0, int(row.get("experience_level", 1)) - 1)
			var colors := source_health_colors(ratio)
			draw_rect(region, colors.outline, false, SOURCE_OUTLINE_PIXELS, false)
			var fill_width := maxf(0.0, (width - 2.0) * ratio)
			if fill_width > 0.0:
				draw_rect(
					Rect2(origin + Vector2.ONE, Vector2(fill_width, SOURCE_HEIGHT_PIXELS - 2.0)),
					colors.fill,
					true
				)
			rendered_bar_count += 1
		if chevron_pips > 0 and chevron_anchor != Vector2.INF:
			_draw_rank_chevrons(chevron_anchor, chevron_pips)


func _draw_rank_chevrons(origin: Vector2, pip_count: int) -> void:
	## Placeholder-styled rank pips: one small chevron per earned rank above
	## the first visible member's bar, matching the retail badge count.
	for pip_index in range(pip_count):
		var x := origin.x + float(pip_index) * (CHEVRON_WIDTH_PIXELS + CHEVRON_SPACING_PIXELS)
		var top := origin.y - CHEVRON_LIFT_PIXELS - CHEVRON_HEIGHT_PIXELS
		draw_colored_polygon(
			PackedVector2Array([
				Vector2(x, top + CHEVRON_HEIGHT_PIXELS),
				Vector2(x + CHEVRON_WIDTH_PIXELS * 0.5, top),
				Vector2(x + CHEVRON_WIDTH_PIXELS, top + CHEVRON_HEIGHT_PIXELS),
			]),
			CHEVRON_COLOR
		)
		rendered_chevron_count += 1


static func source_health_colors(health_ratio: float) -> Dictionary:
	var ratio := clampf(health_ratio, 0.0, 1.0)
	var red := 1.0
	var green := 0.0
	if ratio >= 0.5:
		red = 1.0 - ((ratio - 0.5) / 0.5)
		green = 1.0
	else:
		red = 1.0
		green = 1.0 - ((0.5 - ratio) / 0.5)
	var outline := Color(red * 0.5, green * 0.5, 0.0, 1.0)
	# The drawable condition tint is applied after the outline is derived.
	# Infantry above half health is undamaged, below one quarter is really
	# damaged, and the interval between retains the base gradient.
	if ratio >= 0.5:
		green = (1.0 + green) * 0.5
		red *= 0.5
	elif ratio <= 0.25:
		red = (1.0 + red) * 0.5
		green *= 0.5
	return {
		"fill": Color(red, green, 0.0, 1.0),
		"outline": outline,
	}


## Retail's rule, and the one the structure presenter already uses
## (retail_structure.gd: `selected or health_ratio < 1.0`): a health bar belongs
## to a unit the player has selected or to a unit that has been hurt. The old
## `team != 0 or is_selected` painted a bar over every enemy soldier on screen at
## full health, which is what made a zoomed-out battle line read as a wall of
## floating slabs.
static func should_show_battalion(is_selected: bool, is_damaged: bool) -> bool:
	return is_selected or is_damaged


## Distance LOD gate. An unknown/negative distance draws, so a camera that has
## not produced a usable position yet never silently blanks the overlay.
static func should_draw_battalion_at_distance(distance: float) -> bool:
	if not is_finite(distance) or distance < 0.0:
		return true
	return distance <= SOURCE_MAXIMUM_OVERLAY_DISTANCE


static func source_health_width_for_zoom(tactical_zoom: float) -> float:
	# camera_zoom is a normalized interpolation coordinate, not a divisor. Map
	# it through the authored 120..300 SAGE camera-height range so the health
	# region grows at close view but remains bounded.
	var normalized := clampf(tactical_zoom, 0.0, 1.0)
	var close_scale := SOURCE_FAR_CAMERA_HEIGHT / SOURCE_CLOSE_CAMERA_HEIGHT
	return SOURCE_MINIMUM_INFANTRY_WIDTH_PIXELS * lerpf(close_scale, 1.0, normalized)
