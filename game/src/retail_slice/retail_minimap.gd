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
var source_preview: Texture2D
var retail_parchment: Texture2D
var private_parity_mode := false
var parchment_base := Color.WHITE
var parchment_ink := Color.BLACK
var source_geometry_loaded := false
var world_camera: Camera3D
var camera_center := Vector2.ZERO
var radar_zoom := 1.0
var radar_zoom_target := 1.0
var zoom_response_seconds := 0.09
var last_center_request := Vector2.ZERO

signal center_requested(world_position: Vector2)
signal order_requested(world_position: Vector2)


func bind_retail_parchment(texture: Texture2D) -> bool:
	retail_parchment = texture
	private_parity_mode = texture != null
	if texture != null:
		var image := texture.get_image()
		if image != null and not image.is_empty():
			var lightest := Color.BLACK
			var darkest := Color.WHITE
			for y in range(0, image.get_height(), maxi(1, int(image.get_height() / 24.0))):
				for x in range(0, image.get_width(), maxi(1, int(image.get_width() / 24.0))):
					var sample := image.get_pixel(x, y)
					if sample.a < 0.5:
						continue
					if sample.get_luminance() > lightest.get_luminance():
						lightest = sample
					if sample.get_luminance() < darkest.get_luminance():
						darkest = sample
			parchment_base = lightest
			parchment_ink = darkest
	queue_redraw()
	return private_parity_mode


func configure(sim: RefCounted, map_data: RefCounted = null, preview: Texture2D = null) -> void:
	simulation = sim
	source_map_data = map_data
	source_preview = preview
	uses_source_preview_as_background = false
	if source_map_data != null and bool(source_map_data.ready):
		map_bounds = source_map_data.local_bounds
		mapping_mode = "source-derived-local-transform"
		source_geometry_loaded = true
	else:
		mapping_mode = "fallback-schematic"
		source_geometry_loaded = false
		uses_source_preview_as_background = source_preview != null
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
		elif mouse.button_index == MOUSE_BUTTON_RIGHT:
			# Retail: right-click on the radar orders the selection to that
			# world point without moving the camera.
			order_requested.emit(_canvas_to_world(mouse.position, Rect2(Vector2.ZERO, size).grow(-14.0)))
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
	# The retail parchment disk carries its own edge falloff, so it fills the
	# radar circle; the synthetic path keeps the historical inset.
	var arena := rect.grow(-2.0) if retail_parchment != null else rect.grow(-14.0)
	if retail_parchment != null:
		draw_texture_rect(retail_parchment, arena, false, Color.WHITE)
	elif uses_source_preview_as_background:
		draw_texture_rect(source_preview, arena, false, Color.WHITE)
	if source_geometry_loaded:
		_draw_source_geometry(arena)
	else:
		if not private_parity_mode:
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
		for id in simulation.structure_ids():
			var structure: Dictionary = simulation.structure(id)
			if int(structure.get("health", 0)) <= 0:
				continue
			var point := _world_to_canvas(Vector2(structure["position"]), arena)
			var color := Color("56b5ff") if int(structure["team"]) == 0 else Color("ff6259")
			draw_rect(Rect2(point - Vector2(3.5, 3.5), Vector2(7.0, 7.0)), Color("081018"), true)
			draw_rect(Rect2(point - Vector2(2.5, 2.5), Vector2(5.0, 5.0)), color, true)
	_draw_camera_footprint(arena)
	if not private_parity_mode:
		draw_arc(center, radius, 0.0, TAU, 96, Color("d5ba6a"), 2.0, true)
		draw_arc(center, radius - 5.0, 0.0, TAU, 96, Color("625637"), 3.0, true)


func _draw_source_geometry(arena: Rect2) -> void:
	var land_color := parchment_base if private_parity_mode else Color("4f7339")
	var ink_color := parchment_ink if private_parity_mode else Color("2d7998")
	if private_parity_mode:
		land_color.a = 0.0
		# Retail radar ink is a faint brown wash, not a heavy black fill
		# (REF-24/41/52): keep the water/ford reading subtle.
		ink_color.a = 0.30
	var outline := PackedVector2Array()
	for point in source_map_data.map_outline:
		outline.append(_world_to_canvas(point, arena))
	var outline_fill := _sanitized_radar_polygon(outline)
	if not outline_fill.is_empty():
		draw_colored_polygon(outline_fill, land_color)
	for polygon_value in source_map_data.standing_water_polygons:
		var source_polygon: PackedVector3Array = polygon_value
		var polygon := PackedVector2Array()
		for point in source_polygon:
			polygon.append(_world_to_canvas(Vector2(point.x, point.z), arena))
		_draw_radar_water_polygon(polygon, ink_color)
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
			_draw_radar_water_polygon(strip, ink_color)
	for gate in source_map_data.ford_gates:
		var edge_a := _world_to_canvas(Vector2(gate.get("edge_a", Vector2.ZERO)), arena)
		var edge_b := _world_to_canvas(Vector2(gate.get("edge_b", Vector2.ZERO)), arena)
		draw_line(edge_a, edge_b, parchment_base if private_parity_mode else Color("d0b875"), 4.0, true)
	if outline.size() >= 3:
		for index in range(outline.size()):
			draw_line(outline[index], outline[(index + 1) % outline.size()], parchment_ink if private_parity_mode else Color("779257"), 1.0, true)


func _draw_radar_water_polygon(polygon: PackedVector2Array, ink_color: Color) -> void:
	## Water shapes projected to radar scale can collapse (points merge, runs
	## go collinear) or self-intersect (river strips whose section orientation
	## flips form bowtie quads). Handing those to draw_colored_polygon fails
	## triangulation inside the renderer and spams an error EVERY redraw, so
	## fills are pre-validated; a shape that cannot fill still reads as an ink
	## outline instead of vanishing.
	var filled := _sanitized_radar_polygon(polygon)
	if not filled.is_empty():
		draw_colored_polygon(filled, ink_color)
	elif polygon.size() >= 2:
		var closed := polygon.duplicate()
		closed.append(polygon[0])
		draw_polyline(closed, ink_color, 1.0, true)


static func _sanitized_radar_polygon(polygon: PackedVector2Array) -> PackedVector2Array:
	## Returns a fill-safe copy (consecutive duplicates and the redundant
	## closing point removed), or an EMPTY array when the polygon cannot be
	## triangulated (fewer than 3 distinct points, collinear, self-crossing).
	## Callers must not fill an empty result. Pinned by
	## game/tests/minimap_geometry_guard_runner.gd.
	if polygon.size() < 3:
		return PackedVector2Array()
	var cleaned := PackedVector2Array()
	for point in polygon:
		if cleaned.is_empty() or not cleaned[cleaned.size() - 1].is_equal_approx(point):
			cleaned.append(point)
	while cleaned.size() >= 2 and cleaned[0].is_equal_approx(cleaned[cleaned.size() - 1]):
		cleaned.remove_at(cleaned.size() - 1)
	if cleaned.size() < 3:
		return PackedVector2Array()
	# Godot's ear-clipper ACCEPTS exactly-collinear rings (a zero-area fill
	# that renders nothing), so triangulability alone is not enough — reject
	# zero-area shapes too and let the caller's outline fallback draw them.
	var doubled_area := 0.0
	for index in range(cleaned.size()):
		var current := cleaned[index]
		var next := cleaned[(index + 1) % cleaned.size()]
		doubled_area += current.x * next.y - next.x * current.y
	if absf(doubled_area) <= 0.001:
		return PackedVector2Array()
	if Geometry2D.triangulate_polygon(cleaned).is_empty():
		return PackedVector2Array()
	return cleaned


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
	if world_camera != null and is_instance_valid(world_camera):
		var viewport_rect := world_camera.get_viewport().get_visible_rect()
		var screen_corners := [
			viewport_rect.position,
			Vector2(viewport_rect.end.x, viewport_rect.position.y),
			viewport_rect.end,
			Vector2(viewport_rect.position.x, viewport_rect.end.y),
		]
		var projected := PackedVector2Array()
		for screen_corner in screen_corners:
			var origin := world_camera.project_ray_origin(screen_corner)
			var direction := world_camera.project_ray_normal(screen_corner)
			var hit: Variant = Plane(Vector3.UP, 0.35).intersects_ray(origin, direction)
			if hit != null:
				# The camera frustum regularly spills past the playable edge;
				# retail clips the wedge at the map boundary, so the footprint
				# never escapes the parchment disk.
				var world_hit := Vector2((hit as Vector3).x, (hit as Vector3).z)
				world_hit.x = clampf(world_hit.x, map_bounds.position.x, map_bounds.end.x)
				world_hit.y = clampf(world_hit.y, map_bounds.position.y, map_bounds.end.y)
				projected.append(_world_to_canvas(world_hit, arena))
		if projected.size() == 4:
			projected.append(projected[0])
			draw_polyline(projected, Color(0.92, 0.84, 0.55, 0.9), 1.6, true)
			draw_circle(center, 1.8, Color("f0d47c"))
			return
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
