extends SceneTree
## Pins the retail radar's authored house-color seam and absence of invented
## dark marker halos. This is deliberately a headless contract runner: Godot's
## headless SubViewport does not present reliably, so a pixel wait can hang.

const MinimapScript := preload("res://src/retail_slice/retail_minimap.gd")
const HouseColorScript := preload("res://src/retail_slice/retail_house_color.gd")
const SessionScript := preload("res://src/retail_slice/retail_lockstep_session.gd")
const SELECTED_BLUE := Color8(70, 91, 156)
const AUTHORED_PALETTE: Array[Color] = [
	Color8(70, 91, 156),
	Color8(158, 56, 42),
	Color8(175, 189, 76),
	Color8(62, 152, 100),
	Color8(206, 135, 69),
	Color8(122, 168, 204),
	Color8(148, 116, 183),
	Color8(204, 159, 188),
	Color8(100, 100, 100),
	Color8(255, 255, 255),
]

var passed := 0
var failed := 0


func _check(name: String, ok: bool, detail: String = "") -> void:
	if ok:
		passed += 1
		print("RADAR_LOOK PASS %s" % name)
	else:
		failed += 1
		print("RADAR_LOOK FAIL %s | %s" % [name, detail])


func _close(actual: Color, expected: Color, tolerance := 0.0001) -> bool:
	return (
		absf(actual.r - expected.r) <= tolerance
		and absf(actual.g - expected.g) <= tolerance
		and absf(actual.b - expected.b) <= tolerance
	)


func _palette_close(actual: Array, expected: Array[Color]) -> bool:
	if actual.size() != expected.size():
		return false
	for index in expected.size():
		if not _close(Color(actual[index]), expected[index]):
			return false
	return true


func _polygon_area(polygon: PackedVector2Array) -> float:
	var doubled := 0.0
	for index in polygon.size():
		var current := polygon[index]
		var next := polygon[(index + 1) % polygon.size()]
		doubled += current.x * next.y - next.x * current.y
	return absf(doubled) * 0.5


func _distinct_points(polygon: PackedVector2Array) -> int:
	var distinct := PackedVector2Array()
	for point in polygon:
		var already_present := false
		for existing in distinct:
			if existing.is_equal_approx(point):
				already_present = true
				break
		if not already_present:
			distinct.append(point)
	return distinct.size()


func _has_radar_fallback_diagnostic(rows: Array[String]) -> bool:
	for row in rows:
		if row.begins_with("authored-radar-fallback:") and row.contains("RadarViewBoxEdge"):
			return true
	return false


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	HouseColorScript.team_color_overrides.clear()
	var minimap = MinimapScript.new()
	var team_defaults: Array = []
	for index in AUTHORED_PALETTE.size():
		team_defaults.append(HouseColorScript.color_for_team(index, Color.MAGENTA))
	_check(
		"default_house_color_palette_matches_multiplayer_ini",
		_palette_close(team_defaults, AUTHORED_PALETTE),
		"actual=%s expected=%s" % [team_defaults, AUTHORED_PALETTE]
	)
	var game_state_script = load("res://src/core/game_state.gd")
	var game_state = game_state_script.new()
	_check(
		"default_game_state_colors_use_authored_palette",
		_close(Color(game_state.retail_player_color), AUTHORED_PALETTE[0])
			and _close(Color(game_state.retail_enemy_color), AUTHORED_PALETTE[1]),
		"player=%s enemy=%s" % [game_state.retail_player_color, game_state.retail_enemy_color]
	)
	_check(
		"default_lobby_colors_use_authored_palette",
		_palette_close(Array(SessionScript.LOBBY_HOUSE_COLORS), AUTHORED_PALETTE),
		"actual=%s" % [SessionScript.LOBBY_HOUSE_COLORS]
	)
	_check(
		"default_radar_fallback_uses_authored_palette",
		_close(minimap.blip_color_for_team(0), AUTHORED_PALETTE[0])
			and _close(minimap.blip_color_for_team(1), AUTHORED_PALETTE[1]),
		"team0=%s team1=%s" % [minimap.blip_color_for_team(0), minimap.blip_color_for_team(1)]
	)

	HouseColorScript.team_color_overrides[0] = SELECTED_BLUE
	var has_color_seam := minimap.has_method("blip_color_for_team")
	_check(
		"radar_exposes_the_house_color_seam",
		has_color_seam,
		"RetailMinimap.blip_color_for_team is absent"
	)
	if has_color_seam:
		var actual := Color(minimap.call("blip_color_for_team", 0))
		_check(
			"radar_blip_uses_selected_house_color",
			_close(actual, SELECTED_BLUE),
			"actual=%s expected=%s" % [actual, SELECTED_BLUE]
		)
	else:
		_check("radar_blip_uses_selected_house_color", false, "color seam unavailable")

	var unit_layers: Array = minimap.blip_layers_for("unit", 0)
	_check(
		"unit_blip_has_no_invented_dark_halo",
		unit_layers.size() == 1
			and String(unit_layers[0].get("shape", "")) == "circle"
			and is_equal_approx(float(unit_layers[0].get("radius", 0.0)), 2.3)
			and _close(Color(unit_layers[0].get("color", Color.MAGENTA)), SELECTED_BLUE),
		"layers=%s" % [unit_layers]
	)
	var structure_layers: Array = minimap.blip_layers_for("structure", 0)
	_check(
		"structure_blip_has_no_invented_dark_halo",
		structure_layers.size() == 1
			and String(structure_layers[0].get("shape", "")) == "square"
			and is_equal_approx(float(structure_layers[0].get("half_size", 0.0)), 2.0)
			and _close(Color(structure_layers[0].get("color", Color.MAGENTA)), SELECTED_BLUE),
		"layers=%s" % [structure_layers]
	)

	var viewport := SubViewport.new()
	viewport.size = Vector2i(1024, 768)
	root.add_child(viewport)
	var camera := Camera3D.new()
	camera.fov = 50.0
	viewport.add_child(camera)
	var target := Vector3(52.0, 0.35, 39.0)
	camera.look_at_from_position(Vector3(35.4, 18.0, 22.4), target, Vector3.UP)
	await process_frame
	minimap.map_bounds = Rect2(Vector2(-60.0, -45.0), Vector2(120.0, 90.0))
	minimap.camera_center = Vector2(target.x, target.z)
	var raw_hits := PackedVector2Array()
	var viewport_rect := viewport.get_visible_rect()
	for screen_corner in [viewport_rect.position, Vector2(viewport_rect.end.x, viewport_rect.position.y), viewport_rect.end, Vector2(viewport_rect.position.x, viewport_rect.end.y)]:
		var hit: Variant = Plane(Vector3.UP, 0.35).intersects_ray(
			camera.project_ray_origin(screen_corner), camera.project_ray_normal(screen_corner)
		)
		if hit != null:
			raw_hits.append(Vector2((hit as Vector3).x, (hit as Vector3).z))
	var footprint: PackedVector2Array = minimap.camera_footprint_radar_polygon(camera)
	var all_inside := footprint.size() == 4
	for point in footprint:
		all_inside = all_inside and minimap.map_bounds.has_point(point)
	var area_ratio: float = _polygon_area(footprint) / minimap.map_bounds.get_area()
	var raw_hits_escape := false
	for point in raw_hits:
		if not minimap.map_bounds.has_point(point):
			raw_hits_escape = true
			break
	_check(
		"off_map_camera_pose_reaches_beyond_map_bounds",
		raw_hits_escape,
		"raw_hits=%s" % [raw_hits]
	)
	_check(
		"off_map_camera_footprint_stays_inside_map_bounds",
		all_inside,
		"footprint=%s" % [footprint]
	)
	_check(
		"off_map_camera_footprint_area_stays_below_eight_percent",
		area_ratio > 0.0 and area_ratio < 0.08,
		"area_ratio=%.6f footprint=%s" % [area_ratio, footprint]
	)
	_check(
		"off_map_camera_footprint_remains_a_proper_quad",
		footprint.size() == 4 and _distinct_points(footprint) == 4,
		"distinct=%d footprint=%s" % [_distinct_points(footprint), footprint]
	)

	var has_edge_seam := minimap.has_method("bind_retail_view_box_edge")
	_check(
		"radar_exposes_the_authored_view_box_edge_seam",
		has_edge_seam,
		"RetailMinimap.bind_retail_view_box_edge is absent"
	)
	if has_edge_seam:
		var wrong_size := ImageTexture.create_from_image(
			Image.create(8, 8, false, Image.FORMAT_RGBA8)
		)
		var authored_crop := ImageTexture.create_from_image(
			Image.create(7, 8, false, Image.FORMAT_RGBA8)
		)
		_check(
			"view_box_edge_rejects_wrong_crop_dimensions",
			not bool(minimap.call("bind_retail_view_box_edge", wrong_size))
		)
		_check(
			"view_box_edge_accepts_the_authored_7_by_8_crop",
			bool(minimap.call("bind_retail_view_box_edge", authored_crop))
				and bool(minimap.get("uses_retail_view_box_edge"))
				and String(minimap.get("view_box_edge_source")) == "RadarViewBoxEdge"
		)
	else:
		_check("view_box_edge_rejects_wrong_crop_dimensions", false, "binding seam unavailable")
		_check("view_box_edge_accepts_the_authored_7_by_8_crop", false, "binding seam unavailable")

	var hud_script = load("res://src/retail_slice/retail_hud.gd")
	var fallback_hud = hud_script.new()
	var fallback_minimap = MinimapScript.new()
	fallback_hud.minimap = fallback_minimap
	var fallback_bound := bool(fallback_hud.call("_bind_retail_radar_view_box_edge", null, ""))
	_check(
		"missing_radar_view_box_edge_records_named_bind_diagnostic",
		not fallback_bound and _has_radar_fallback_diagnostic(fallback_hud.retail_bind_diagnostics),
		"diagnostics=%s" % [fallback_hud.retail_bind_diagnostics]
	)

	HouseColorScript.team_color_overrides.clear()
	game_state.free()
	fallback_minimap.free()
	fallback_hud.free()
	viewport.free()
	minimap.free()
	print("RADAR_LOOK_RESULT passed=%d failed=%d" % [passed, failed])
	quit(1 if failed > 0 else 0)
