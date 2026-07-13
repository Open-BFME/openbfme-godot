class_name RetailMinimap
extends Control
## Schematic projection of the cooked source geometry in the exact local
## transform used by simulation. The separate imported preview remains art,
## never a false coordinate texture.

var simulation: RefCounted
var source_map_data: RefCounted
var map_bounds := Rect2(Vector2(-60.0, -45.0), Vector2(120.0, 90.0))
var mapping_mode := "unconfigured"
var uses_source_preview_as_background := false
var source_geometry_loaded := false
var world_camera: Camera3D
var camera_center := Vector2.ZERO
var radar_zoom := 1.0
var radar_zoom_target := 1.0
var zoom_response_seconds := 0.09
var last_center_request := Vector2.ZERO

signal center_requested(world_position: Vector2)


func configure(sim: RefCounted, map_data: RefCounted = null) -> void:
	simulation = sim
	source_map_data = map_data
	if source_map_data != null and bool(source_map_data.ready):
		map_bounds = source_map_data.local_bounds
		mapping_mode = "source-derived-local-transform"
		source_geometry_loaded = true
	else:
		mapping_mode = "fallback-schematic"
		source_geometry_loaded = false
	camera_center = map_bounds.get_center()
	queue_redraw()


func _process(delta: float) -> void:
	var response := 1.0 - exp(-maxf(delta, 0.0) / maxf(zoom_response_seconds, 0.001))
	radar_zoom = lerpf(radar_zoom, radar_zoom_target, response)
	queue_redraw()


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseButton and (event as InputEventMouseButton).pressed:
		var mouse := event as InputEventMouseButton
		if mouse.button_index == MOUSE_BUTTON_WHEEL_UP:
			nudge_zoom(1)
			accept_event()
		elif mouse.button_index == MOUSE_BUTTON_WHEEL_DOWN:
			nudge_zoom(-1)
			accept_event()
		elif mouse.button_index == MOUSE_BUTTON_LEFT:
			last_center_request = _canvas_to_world(mouse.position, Rect2(Vector2.ZERO, size).grow(-14.0))
			center_requested.emit(last_center_request)
			accept_event()


func nudge_zoom(direction: int) -> void:
	# One notch is deliberately meaningful; convergence is fast enough to feel
	# immediate without a camera-jarring single-frame pop.
	var factor := 1.32 if direction > 0 else (1.0 / 1.32)
	radar_zoom_target = clampf(radar_zoom_target * factor, 1.0, 2.8)


func set_zoom(value: float, immediate: bool = false) -> void:
	radar_zoom_target = clampf(value, 1.0, 2.8)
	if immediate:
		radar_zoom = radar_zoom_target
	queue_redraw()


func _draw() -> void:
	var rect := Rect2(Vector2.ZERO, size)
	var center := rect.get_center()
	var radius := minf(rect.size.x, rect.size.y) * 0.47
	draw_circle(center, radius, Color("0a1118"))
	var arena := rect.grow(-14.0)
	if source_geometry_loaded:
		_draw_source_geometry(arena)
	else:
		draw_circle(center, maxf(1.0, radius - 8.0), Color("4f7339"))
	if simulation != null:
		for id in simulation.entity_ids():
			var entity: Dictionary = simulation.entity(id)
			if int(entity.get("health", 0)) <= 0:
				continue
			var point := _world_to_canvas(Vector2(entity["position"]), arena)
			var color := Color("56b5ff") if int(entity["team"]) == 0 else Color("ff6259")
			draw_circle(point, 4.5, Color("081018"))
			draw_circle(point, 3.2, color)
	_draw_camera_footprint(arena)
	draw_arc(center, radius, 0.0, TAU, 96, Color("d5ba6a"), 2.0, true)
	draw_arc(center, radius - 5.0, 0.0, TAU, 96, Color("625637"), 3.0, true)


func _draw_source_geometry(arena: Rect2) -> void:
	var outline := PackedVector2Array()
	for point in source_map_data.map_outline:
		outline.append(_world_to_canvas(point, arena))
	if outline.size() >= 3:
		draw_colored_polygon(outline, Color("4f7339"))
	for polygon_value in source_map_data.standing_water_polygons:
		var source_polygon: PackedVector3Array = polygon_value
		var polygon := PackedVector2Array()
		for point in source_polygon:
			polygon.append(_world_to_canvas(Vector2(point.x, point.z), arena))
		if polygon.size() >= 3:
			draw_colored_polygon(polygon, Color("2d7998"))
	for river in source_map_data.river_strips:
		var sections: Array = river.get("sections", [])
		for index in range(sections.size() - 1):
			var first: PackedVector3Array = sections[index]
			var second: PackedVector3Array = sections[index + 1]
			var strip := PackedVector2Array([
				_world_to_canvas(Vector2(first[0].x, first[0].z), arena),
				_world_to_canvas(Vector2(second[0].x, second[0].z), arena),
				_world_to_canvas(Vector2(second[1].x, second[1].z), arena),
				_world_to_canvas(Vector2(first[1].x, first[1].z), arena),
			])
			draw_colored_polygon(strip, Color("2f7e9b"))
	for gate in source_map_data.ford_gates:
		var edge_a := _world_to_canvas(Vector2(gate.get("edge_a", Vector2.ZERO)), arena)
		var edge_b := _world_to_canvas(Vector2(gate.get("edge_b", Vector2.ZERO)), arena)
		draw_line(edge_a, edge_b, Color("d0b875"), 4.0, true)
	if outline.size() >= 3:
		for index in range(outline.size()):
			draw_line(outline[index], outline[(index + 1) % outline.size()], Color("779257"), 1.0, true)


func _world_to_canvas(world: Vector2, arena: Rect2) -> Vector2:
	var visible_bounds := _visible_bounds()
	var safe_size := Vector2(maxf(visible_bounds.size.x, 1.0), maxf(visible_bounds.size.y, 1.0))
	var scale := minf(arena.size.x / safe_size.x, arena.size.y / safe_size.y)
	var rendered_size := safe_size * scale
	var origin := arena.position + (arena.size - rendered_size) * 0.5
	var normalized := (world - visible_bounds.position) / safe_size
	return origin + Vector2(normalized.x * rendered_size.x, normalized.y * rendered_size.y)


func _canvas_to_world(canvas: Vector2, arena: Rect2) -> Vector2:
	var visible_bounds := _visible_bounds()
	var safe_size := Vector2(maxf(visible_bounds.size.x, 1.0), maxf(visible_bounds.size.y, 1.0))
	var scale := minf(arena.size.x / safe_size.x, arena.size.y / safe_size.y)
	var rendered_size := safe_size * scale
	var origin := arena.position + (arena.size - rendered_size) * 0.5
	var normalized := (canvas - origin) / rendered_size
	normalized.x = clampf(normalized.x, 0.0, 1.0)
	normalized.y = clampf(normalized.y, 0.0, 1.0)
	return visible_bounds.position + normalized * safe_size


func _visible_bounds() -> Rect2:
	if radar_zoom <= 1.001:
		return map_bounds
	var visible_size := map_bounds.size / radar_zoom
	var half := visible_size * 0.5
	var center := camera_center
	center.x = clampf(center.x, map_bounds.position.x + half.x, map_bounds.end.x - half.x)
	center.y = clampf(center.y, map_bounds.position.y + half.y, map_bounds.end.y - half.y)
	return Rect2(center - half, visible_size)


func _draw_camera_footprint(arena: Rect2) -> void:
	var center := _world_to_canvas(camera_center, arena)
	var visible_bounds := _visible_bounds()
	var footprint_world := Vector2(visible_bounds.size.x * 0.17, visible_bounds.size.y * 0.14)
	var half := footprint_world * 0.5
	var points := PackedVector2Array([
		_world_to_canvas(camera_center + Vector2(-half.x, -half.y), arena),
		_world_to_canvas(camera_center + Vector2(half.x, -half.y), arena),
		_world_to_canvas(camera_center + Vector2(half.x, half.y), arena),
		_world_to_canvas(camera_center + Vector2(-half.x, half.y), arena),
		_world_to_canvas(camera_center + Vector2(-half.x, -half.y), arena),
	])
	if points.size() == 5:
		draw_polyline(points, Color(0.92, 0.84, 0.55, 0.82), 1.6, true)
		draw_circle(center, 1.8, Color("f0d47c"))
