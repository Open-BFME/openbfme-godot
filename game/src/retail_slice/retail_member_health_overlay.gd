class_name RetailMemberHealthOverlay
extends Control
## Screen-space member health bars for selected friendly battalions and every
## visible enemy member in the private retail slice.
##
## SAGE computes one UI region per drawable, fixes the bar height at three
## pixels, draws a one-pixel outline, and scales the width by tactical zoom.
## Keeping this in CanvasItem space avoids the large billboard quads that were
## previously used as a gameplay approximation.

const SOURCE_MINIMUM_INFANTRY_WIDTH_PIXELS := 40.0
const SOURCE_HEIGHT_PIXELS := 3.0
const SOURCE_OUTLINE_PIXELS := 1.0
const SOURCE_CLOSE_CAMERA_HEIGHT := 120.0
const SOURCE_FAR_CAMERA_HEIGHT := 300.0

var tactical_view: Node
var tactical_camera: Camera3D
var battalions: Dictionary
var rendered_bar_count := 0


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
	if tactical_camera == null or not is_instance_valid(tactical_camera):
		return
	var zoom := 1.0
	if tactical_view != null and is_instance_valid(tactical_view):
		zoom = float(tactical_view.get("camera_zoom"))
	var width := source_health_width_for_zoom(zoom)
	var viewport_rect := get_viewport_rect()
	for battalion_value in battalions.values():
		if not battalion_value is Node or not (battalion_value as Node).has_method("member_health_overlay_rows"):
			continue
		var battalion := battalion_value as Node
		var battalion_team := int(battalion.get("team"))
		if not should_show_battalion(battalion_team, bool(battalion.get("selected"))):
			continue
		for row_value in battalion.call("member_health_overlay_rows"):
			var row := row_value as Dictionary
			var world_position := row.get("world_position", Vector3.ZERO) as Vector3
			if tactical_camera.is_position_behind(world_position):
				continue
			var screen_center := tactical_camera.unproject_position(world_position)
			# The source region starts at center - 45% of its width rather than
			# exactly centering the bar.
			var origin := Vector2(
				screen_center.x - width * 0.45,
				screen_center.y - SOURCE_HEIGHT_PIXELS * 0.5
			)
			var region := Rect2(origin, Vector2(width, SOURCE_HEIGHT_PIXELS))
			if not viewport_rect.intersects(region):
				continue
			var ratio := clampf(float(row.get("health_ratio", 0.0)), 0.0, 1.0)
			if ratio <= 0.0:
				continue
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


static func should_show_battalion(team: int, is_selected: bool) -> bool:
	return team != 0 or is_selected


static func source_health_width_for_zoom(tactical_zoom: float) -> float:
	# camera_zoom is a normalized interpolation coordinate, not a divisor. Map
	# it through the authored 120..300 SAGE camera-height range so the health
	# region grows at close view but remains bounded.
	var normalized := clampf(tactical_zoom, 0.0, 1.0)
	var close_scale := SOURCE_FAR_CAMERA_HEIGHT / SOURCE_CLOSE_CAMERA_HEIGHT
	return SOURCE_MINIMUM_INFANTRY_WIDTH_PIXELS * lerpf(close_scale, 1.0, normalized)
