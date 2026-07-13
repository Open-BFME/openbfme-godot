class_name Stage3Board
extends Control
## Legal-safe top-down presentation for the Stage 3 fortification lab.

const WorldScript = preload("res://src/proof_stage3/proof_world.gd")
const StructureScript = preload("res://src/proof_stage3/structure_system.gd")
const TopologyScript = preload("res://src/proof_stage3/topology_grid.gd")

signal cell_left_pressed(cell: Vector2i, additive: bool)
signal cell_right_pressed(cell: Vector2i)
signal hover_changed(cell: Vector2i)

var world: WorldScript
var presented_snapshot: Dictionary = {}
var build_definition_id: String = ""
var build_rotation: int = 0
var pending_chain: Array[Vector2i] = []
var hover_cell: Vector2i = Vector2i(-1, -1)
var selected_unit_id: int = 0
var selected_gate_id: int = 0


func _ready() -> void:
	mouse_filter = Control.MOUSE_FILTER_STOP
	clip_contents = true
	resized.connect(queue_redraw)


func configure(p_world: WorldScript) -> void:
	world = p_world
	presented_snapshot = world.team_filtered_snapshot(WorldScript.TEAM_BLUE)
	queue_redraw()


func present(snapshot: Dictionary, definition_id: String, rotation_quarters: int, chain: Array[Vector2i], selected_unit: int, selected_gate: int) -> void:
	presented_snapshot = snapshot
	build_definition_id = definition_id
	build_rotation = rotation_quarters
	pending_chain = chain.duplicate()
	selected_unit_id = selected_unit
	selected_gate_id = selected_gate
	queue_redraw()


func cell_to_screen(cell: Vector2i) -> Vector2:
	var geometry: Dictionary = _grid_geometry()
	var origin: Vector2 = geometry.origin
	var cell_size: float = float(geometry.cell_size)
	return origin + Vector2(float(cell.x) + 0.5, float(cell.y) + 0.5) * cell_size


func screen_to_cell(point: Vector2) -> Vector2i:
	if world == null:
		return Vector2i(-1, -1)
	var geometry: Dictionary = _grid_geometry()
	var origin: Vector2 = geometry.origin
	var cell_size: float = float(geometry.cell_size)
	var local: Vector2 = point - origin
	var cell := Vector2i(floori(local.x / cell_size), floori(local.y / cell_size))
	return cell if world.topology.contains(cell) else Vector2i(-1, -1)


func visible_enemy_count() -> int:
	var count: int = 0
	for raw_unit: Variant in presented_snapshot.get("units", []):
		var unit: Dictionary = raw_unit
		if int(unit.get("team", -1)) == WorldScript.TEAM_RED:
			count += 1
	return count


func _gui_input(event: InputEvent) -> void:
	if event is InputEventMouseMotion:
		var motion := event as InputEventMouseMotion
		var next_hover: Vector2i = screen_to_cell(motion.position)
		if next_hover != hover_cell:
			hover_cell = next_hover
			hover_changed.emit(hover_cell)
			queue_redraw()
		return
	if not event is InputEventMouseButton:
		return
	var mouse := event as InputEventMouseButton
	if not mouse.pressed:
		return
	var cell: Vector2i = screen_to_cell(mouse.position)
	if cell.x < 0:
		return
	if mouse.button_index == MOUSE_BUTTON_LEFT:
		cell_left_pressed.emit(cell, mouse.shift_pressed)
		accept_event()
	elif mouse.button_index == MOUSE_BUTTON_RIGHT:
		cell_right_pressed.emit(cell)
		accept_event()


func _draw() -> void:
	if world == null:
		return
	var geometry: Dictionary = _grid_geometry()
	var origin: Vector2 = geometry.origin
	var cell_size: float = float(geometry.cell_size)
	var board_size := Vector2(float(world.topology.width), float(world.topology.height)) * cell_size
	draw_rect(Rect2(origin - Vector2(9.0, 9.0), board_size + Vector2(18.0, 18.0)), Color("263449"), true)
	draw_rect(Rect2(origin - Vector2(4.0, 4.0), board_size + Vector2(8.0, 8.0)), Color("8ba2b8"), false, 2.0)
	for y: int in range(world.topology.height):
		for x: int in range(world.topology.width):
			var cell := Vector2i(x, y)
			var color := Color("101822")
			if world.vision.is_visible(WorldScript.TEAM_BLUE, cell):
				color = Color("284c42") if (x + y) % 2 == 0 else Color("24463e")
			elif world.vision.is_explored(WorldScript.TEAM_BLUE, cell):
				color = Color("1b2b32") if (x + y) % 2 == 0 else Color("19272e")
			draw_rect(Rect2(origin + Vector2(x, y) * cell_size, Vector2.ONE * cell_size), color, true)
	for x: int in range(world.topology.width + 1):
		var line_x: float = origin.x + float(x) * cell_size
		draw_line(Vector2(line_x, origin.y), Vector2(line_x, origin.y + board_size.y), Color(0.35, 0.48, 0.5, 0.28), 1.0)
	for y: int in range(world.topology.height + 1):
		var line_y: float = origin.y + float(y) * cell_size
		draw_line(Vector2(origin.x, line_y), Vector2(origin.x + board_size.x, line_y), Color(0.35, 0.48, 0.5, 0.28), 1.0)
	_draw_selected_path(cell_size)
	_draw_structures(cell_size)
	_draw_units(cell_size)
	_draw_projectiles(cell_size)
	_draw_build_preview(cell_size)


func _draw_selected_path(cell_size: float) -> void:
	if selected_unit_id == 0:
		return
	var unit: WorldScript.UnitRecord = world.get_unit(selected_unit_id)
	if unit == null or unit.path_index >= unit.path.size():
		return
	var points := PackedVector2Array([cell_to_screen(unit.cell)])
	for i: int in range(unit.path_index, unit.path.size()):
		points.append(cell_to_screen(unit.path[i]))
	if points.size() >= 2:
		draw_polyline(points, Color(0.32, 0.78, 1.0, 0.86), maxf(2.0, cell_size * 0.08), true)


func _draw_structures(cell_size: float) -> void:
	for raw_structure: Variant in presented_snapshot.get("structures", []):
		var row: Dictionary = raw_structure
		var id: int = int(row.get("id", 0))
		var definition_id: String = String(row.get("definition", ""))
		var cell: Vector2i = row.get("cell", Vector2i.ZERO)
		var center: Vector2 = cell_to_screen(cell)
		var record: StructureScript.StructureRecord = world.structures.get_structure(id)
		var team: int = int(row.get("team", 0))
		var team_color := Color("51a8ff") if team == WorldScript.TEAM_BLUE else Color("f05a55")
		match definition_id:
			"wall":
				var horizontal: bool = record == null or record.rotation_quarters % 2 == 0
				var wall_size := Vector2(cell_size * (0.86 if horizontal else 0.30), cell_size * (0.30 if horizontal else 0.86))
				draw_rect(Rect2(center - wall_size * 0.5, wall_size), team_color.darkened(0.25), true)
				draw_rect(Rect2(center - wall_size * 0.5, wall_size), team_color.lightened(0.2), false, 2.0)
			"gate":
				var opened: bool = bool(row.get("gate_open", false))
				var half: float = cell_size * 0.43
				draw_line(center + Vector2(-half, -half), center + Vector2(-half, half), team_color, 4.0)
				draw_line(center + Vector2(half, -half), center + Vector2(half, half), team_color, 4.0)
				if not opened:
					draw_line(center + Vector2(-half, 0.0), center + Vector2(half, 0.0), team_color, 5.0)
				else:
					draw_arc(center, half * 0.8, PI, TAU, 12, team_color, 2.0)
				_draw_centered_text(center + Vector2(0.0, -cell_size * 0.31), "OPEN" if opened else "CLOSED", Color.WHITE, maxi(9, int(cell_size * 0.19)))
			"tower":
				draw_circle(center, cell_size * 0.35, team_color.darkened(0.28))
				draw_arc(center, cell_size * 0.35, 0.0, TAU, 20, team_color.lightened(0.2), 2.0)
				draw_line(center, center + Vector2(cell_size * 0.28, 0.0), Color("ffd36b"), 3.0)
			"wall_tower":
				draw_circle(center, cell_size * 0.22, Color("ffcc66"))
				draw_arc(center, cell_size * 0.22, 0.0, TAU, 16, Color.WHITE, 1.5)
		if id == selected_gate_id:
			draw_arc(center, cell_size * 0.55, 0.0, TAU, 28, Color("ffe275"), 3.0)


func _draw_units(cell_size: float) -> void:
	for raw_unit: Variant in presented_snapshot.get("units", []):
		var unit: Dictionary = raw_unit
		var id: int = int(unit.get("id", 0))
		var team: int = int(unit.get("team", -1))
		var center: Vector2 = cell_to_screen(unit.get("cell", Vector2i.ZERO))
		var color := Color("68b8ff") if team == WorldScript.TEAM_BLUE else Color("ff6b64")
		draw_circle(center, cell_size * 0.23, color)
		draw_arc(center, cell_size * 0.23, 0.0, TAU, 16, Color.WHITE if id == selected_unit_id else color.lightened(0.2), 3.0 if id == selected_unit_id else 1.5)
		var health: int = int(unit.get("health", 0))
		_draw_centered_text(center + Vector2(0.0, cell_size * 0.08), str(health), Color("102030"), maxi(9, int(cell_size * 0.18)))


func _draw_projectiles(cell_size: float) -> void:
	for raw_projectile: Variant in presented_snapshot.get("projectiles", []):
		var projectile: Dictionary = raw_projectile
		var center: Vector2 = cell_to_screen(projectile.get("cell", Vector2i.ZERO))
		draw_circle(center, maxf(3.0, cell_size * 0.10), Color("ffe477"))
		draw_arc(center, maxf(4.0, cell_size * 0.13), 0.0, TAU, 12, Color("ff8736"), 2.0)


func _draw_build_preview(cell_size: float) -> void:
	for cell: Vector2i in pending_chain:
		var center: Vector2 = cell_to_screen(cell)
		draw_rect(Rect2(center - Vector2.ONE * cell_size * 0.35, Vector2.ONE * cell_size * 0.7), Color(0.95, 0.72, 0.22, 0.32), true)
		draw_rect(Rect2(center - Vector2.ONE * cell_size * 0.35, Vector2.ONE * cell_size * 0.7), Color("ffd568"), false, 2.0)
	if build_definition_id == "" or hover_cell.x < 0:
		return
	var valid: bool = _preview_valid(hover_cell)
	var center: Vector2 = cell_to_screen(hover_cell)
	var color := Color(0.28, 0.95, 0.55, 0.42) if valid else Color(1.0, 0.24, 0.22, 0.42)
	draw_rect(Rect2(center - Vector2.ONE * cell_size * 0.42, Vector2.ONE * cell_size * 0.84), color, true)
	var direction: Vector2i = TopologyScript.quarter_direction(build_rotation)
	draw_line(center, center + Vector2(direction) * cell_size * 0.34, Color.WHITE, 3.0)


func _preview_valid(cell: Vector2i) -> bool:
	if build_definition_id == "wall_tower":
		var base: StructureScript.StructureRecord = world.structures.structure_at(cell)
		return base != null and base.kind == "wall" and base.owner == WorldScript.TEAM_BLUE and world.structures.attachment_at(cell) == null
	return world.structures.structure_at(cell) == null and world.topology.mask_at(cell) == 0


func _draw_centered_text(center: Vector2, text: String, color: Color, font_size: int) -> void:
	var font: Font = ThemeDB.fallback_font
	var width: float = font.get_string_size(text, HORIZONTAL_ALIGNMENT_LEFT, -1, font_size).x
	draw_string(font, center - Vector2(width * 0.5, 0.0), text, HORIZONTAL_ALIGNMENT_LEFT, -1.0, font_size, color)


func _grid_geometry() -> Dictionary:
	var left_margin: float = 282.0
	var right_margin: float = 322.0
	var top_margin: float = 82.0
	var bottom_margin: float = 92.0
	var available := Vector2(maxf(120.0, size.x - left_margin - right_margin), maxf(120.0, size.y - top_margin - bottom_margin))
	var cell_size: float = maxf(8.0, floorf(minf(available.x / float(world.topology.width), available.y / float(world.topology.height))))
	var board_size := Vector2(float(world.topology.width), float(world.topology.height)) * cell_size
	var origin := Vector2(left_margin, top_margin) + (available - board_size) * 0.5
	return {"origin": origin, "cell_size": cell_size}
