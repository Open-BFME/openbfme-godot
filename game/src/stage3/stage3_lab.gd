class_name Stage3Lab
extends Control
## Interactive Stage 3 lab: real definitions -> proof world -> legal-safe presentation.

const GRID_WIDTH: int = 24
const GRID_HEIGHT: int = 15
const SIM_STEP_SECONDS: float = 0.16
const WorldScript = preload("res://src/proof_stage3/proof_world.gd")
const TopologyScript = preload("res://src/proof_stage3/topology_grid.gd")
const StructureScript = preload("res://src/proof_stage3/structure_system.gd")
const BoardScript = preload("res://src/stage3/stage3_board.gd")
const HudScript = preload("res://src/stage3/stage3_hud.gd")

@onready var board: BoardScript = $Board
@onready var hud: HudScript = $Stage3Hud

var world: WorldScript
var definitions_root: Dictionary = {}
var build_definition_id: String = ""
var build_rotation: int = 0
var pending_chain: Array[Vector2i] = []
var selected_unit_id: int = 0
var selected_gate_id: int = 0
var demonstration_gate_id: int = 0
var blue_unit_id: int = 0
var red_unit_id: int = 0
var red_tower_id: int = 0
var last_local_edit_ms: float = 0.0
var accumulator: float = 0.0


func _ready() -> void:
	definitions_root = _load_definitions()
	board.cell_left_pressed.connect(_on_cell_left)
	board.cell_right_pressed.connect(_on_cell_right)
	hud.build_selected.connect(select_build)
	hud.rotate_requested.connect(rotate_build)
	hud.chain_commit_requested.connect(commit_chain)
	hud.chain_cancel_requested.connect(cancel_pending_chain)
	hud.gate_toggle_requested.connect(toggle_selected_gate)
	hud.reset_requested.connect(reset_lab)
	hud.menu_requested.connect(_return_to_menu)
	hud.configure(definitions_root)
	reset_lab()


func _process(delta: float) -> void:
	if world == null:
		return
	accumulator += minf(delta, 0.25)
	var steps: int = 0
	while accumulator >= SIM_STEP_SECONDS and steps < 4:
		accumulator -= SIM_STEP_SECONDS
		world.tick()
		steps += 1
	refresh_presentation()


func reset_lab() -> void:
	world = WorldScript.new(GRID_WIDTH, GRID_HEIGHT, definitions_root)
	world.structures.set_team_resources(WorldScript.TEAM_BLUE, 20000)
	world.structures.set_team_resources(WorldScript.TEAM_RED, 20000)
	var barrier: Array[Dictionary] = []
	for y: int in range(GRID_HEIGHT):
		barrier.append({
			"definition": "gate" if y == 7 else "wall",
			"owner": WorldScript.TEAM_BLUE,
			"position": TopologyScript.cell_center(Vector2i(12, y)),
			"rotation_quarters": 1,
		})
	var barrier_result: Dictionary = world.place_chain(barrier)
	if bool(barrier_result.get("ok", false)):
		demonstration_gate_id = int((barrier_result.get("ids", []) as Array)[7])
		world.structures.set_gate_open(demonstration_gate_id, true)
	else:
		demonstration_gate_id = 0
	var tower_result: Dictionary = world.place_structure("tower", WorldScript.TEAM_RED, TopologyScript.cell_center(Vector2i(17, 5)))
	red_tower_id = int(tower_result.get("id", 0))
	var blue: WorldScript.UnitRecord = world.add_unit(WorldScript.TEAM_BLUE, TopologyScript.cell_center(Vector2i(5, 7)), 180, 4, 1100)
	var red: WorldScript.UnitRecord = world.add_unit(WorldScript.TEAM_RED, TopologyScript.cell_center(Vector2i(18, 7)), 180, 4, 2100)
	blue_unit_id = blue.id
	red_unit_id = red.id
	world.order_move(red_unit_id, Vector2i(5, 7)) # Expected to fail: an open owner gate still blocks red.
	world.structures.set_team_resources(WorldScript.TEAM_BLUE, 1600)
	world.structures.set_team_resources(WorldScript.TEAM_RED, 0)
	world.recompute_visibility()
	build_definition_id = ""
	build_rotation = 0
	pending_chain.clear()
	selected_unit_id = blue_unit_id
	selected_gate_id = 0
	last_local_edit_ms = 0.0
	accumulator = 0.0
	board.configure(world)
	hud.set_status("Blue scout selected. Right-click beyond the open gate; red cannot use it.")
	refresh_presentation()


func select_build(definition_id: String) -> void:
	if not world.structures.definitions.has(definition_id):
		hud.set_status("Unknown defense definition: %s" % definition_id, true)
		return
	if build_definition_id == definition_id:
		cancel_pending_chain()
		build_definition_id = ""
		hud.set_status("Build mode cancelled. Click the blue scout or a friendly gate to command it.")
	else:
		if not pending_chain.is_empty():
			pending_chain.clear()
		build_definition_id = definition_id
		hud.set_status("%s selected. Click a snapped grid cell." % definition_id.replace("_", " ").capitalize())
	selected_gate_id = 0
	refresh_presentation()


func rotate_build() -> void:
	build_rotation = TopologyScript.normalize_quarter_rotation(build_rotation + 1)
	hud.set_status("Placement rotated to %d degrees." % (build_rotation * 90))
	refresh_presentation()


func place_at_cell(cell: Vector2i) -> Dictionary:
	if world == null or not world.topology.contains(cell):
		return {"ok": false, "error": "out_of_bounds"}
	if build_definition_id == "":
		_inspect_cell(cell)
		return {"ok": true, "action": "inspect"}
	if build_definition_id == "wall":
		return _queue_wall_cell(cell)
	var attach_to_id: int = 0
	if build_definition_id == "wall_tower":
		var base: StructureScript.StructureRecord = world.structures.structure_at(cell)
		if base == null:
			hud.set_status("Barrier Turret requires a friendly wall at that cell.", true)
			return {"ok": false, "error": "attachment_requires_wall"}
		attach_to_id = base.id
	var start_usec: int = Time.get_ticks_usec()
	var result: Dictionary = world.place_structure(
		build_definition_id,
		WorldScript.TEAM_BLUE,
		TopologyScript.cell_center(cell),
		build_rotation,
		attach_to_id
	)
	last_local_edit_ms = float(Time.get_ticks_usec() - start_usec) / 1000.0
	if bool(result.get("ok", false)):
		var id: int = int(result.get("id", 0))
		if build_definition_id == "gate":
			selected_gate_id = id
		hud.set_status("Placed %s at %s in %.3f ms." % [build_definition_id.replace("_", " "), str(cell), last_local_edit_ms])
	else:
		hud.set_status("Placement rejected: %s" % String(result.get("error", "unknown")), true)
	world.recompute_visibility()
	refresh_presentation()
	return result


func commit_chain() -> Dictionary:
	if pending_chain.is_empty():
		return {"ok": false, "error": "chain_empty"}
	var requests: Array[Dictionary] = []
	for cell: Vector2i in pending_chain:
		requests.append({
			"definition": "wall",
			"owner": WorldScript.TEAM_BLUE,
			"position": TopologyScript.cell_center(cell),
			"rotation_quarters": build_rotation,
		})
	var start_usec: int = Time.get_ticks_usec()
	var result: Dictionary = world.place_chain(requests)
	last_local_edit_ms = float(Time.get_ticks_usec() - start_usec) / 1000.0
	if bool(result.get("ok", false)):
		hud.set_status("Committed %d snapped wall segments in %.3f ms." % [pending_chain.size(), last_local_edit_ms])
		pending_chain.clear()
	else:
		hud.set_status("Wall chain rejected atomically: %s" % String(result.get("error", "unknown")), true)
	world.recompute_visibility()
	refresh_presentation()
	return result


func cancel_pending_chain() -> void:
	pending_chain.clear()
	hud.set_status("Pending wall chain cleared.")
	refresh_presentation()


func toggle_selected_gate() -> bool:
	var gate: StructureScript.StructureRecord = world.structures.get_structure(selected_gate_id)
	if gate == null or gate.kind != "gate" or gate.owner != WorldScript.TEAM_BLUE or not gate.is_alive():
		hud.set_status("Select a living blue gate first.", true)
		return false
	var start_usec: int = Time.get_ticks_usec()
	var changed: bool = world.structures.set_gate_open(gate.id, not gate.gate_open)
	last_local_edit_ms = float(Time.get_ticks_usec() - start_usec) / 1000.0
	if changed:
		hud.set_status("Gate %d is now %s. Red remains blocked when it is open." % [gate.id, "OPEN" if gate.gate_open else "CLOSED"])
	world.recompute_visibility()
	refresh_presentation()
	return changed


func move_selected_to(cell: Vector2i) -> bool:
	var unit: WorldScript.UnitRecord = world.get_unit(selected_unit_id)
	if unit == null or unit.team != WorldScript.TEAM_BLUE or not unit.is_alive():
		hud.set_status("Select the blue scout before issuing a move.", true)
		return false
	if not pending_chain.is_empty():
		pending_chain.clear()
	build_definition_id = ""
	var accepted: bool = world.order_move(unit.id, cell)
	if accepted:
		hud.set_status("Blue route accepted at topology revision %d." % world.topology.revision)
	else:
		hud.set_status("No blue route to %s under the current topology." % str(cell), true)
	refresh_presentation()
	return accepted


func step_sim(ticks: int = 1) -> void:
	if world == null:
		return
	world.advance(maxi(0, ticks))
	refresh_presentation()


func refresh_presentation() -> void:
	if world == null or board == null or hud == null:
		return
	var snapshot: Dictionary = world.team_filtered_snapshot(WorldScript.TEAM_BLUE)
	board.present(snapshot, build_definition_id, build_rotation, pending_chain, selected_unit_id, selected_gate_id)
	hud.set_resources(world.structures.resources_for(WorldScript.TEAM_BLUE))
	hud.set_build_mode(build_definition_id, build_rotation)
	hud.set_chain_state(pending_chain.size())
	var replans: int = 0
	var unit: WorldScript.UnitRecord = world.get_unit(selected_unit_id)
	if unit != null:
		replans = unit.replan_count
	var selection_text: String = "Nothing selected"
	var gate: StructureScript.StructureRecord = world.structures.get_structure(selected_gate_id)
	if gate != null and gate.kind == "gate":
		selection_text = "Blue gate %d\n%s  /  HP %d\nOpen: blue passes, red blocked" % [gate.id, "OPEN" if gate.gate_open else "CLOSED", gate.health]
	elif unit != null:
		selection_text = "Blue scout %d\nCell %s  /  HP %d\nDestination %s" % [unit.id, str(unit.cell), unit.health, str(unit.destination)]
	hud.set_selection(selection_text, gate != null and gate.kind == "gate")
	hud.set_metrics(world.tick_index, world.topology.revision, replans, last_local_edit_ms, board.visible_enemy_count(), world.structures.active_projectile_count())


func _queue_wall_cell(cell: Vector2i) -> Dictionary:
	if world.structures.structure_at(cell) != null or world.topology.mask_at(cell) != 0 or pending_chain.has(cell):
		hud.set_status("Wall cell is occupied.", true)
		return {"ok": false, "error": "cell_occupied"}
	if not pending_chain.is_empty():
		var previous: Vector2i = pending_chain[-1]
		if absi(previous.x - cell.x) + absi(previous.y - cell.y) != 1:
			hud.set_status("Wall chains must continue into an adjacent cell.", true)
			return {"ok": false, "error": "chain_not_contiguous"}
	pending_chain.append(cell)
	hud.set_status("Wall segment %d queued. Continue clicking, then press Enter." % pending_chain.size())
	refresh_presentation()
	return {"ok": true, "action": "queued", "count": pending_chain.size()}


func _inspect_cell(cell: Vector2i) -> void:
	var base: StructureScript.StructureRecord = world.structures.structure_at(cell)
	if base != null and base.kind == "gate" and base.owner == WorldScript.TEAM_BLUE and base.is_alive():
		selected_gate_id = base.id
		selected_unit_id = 0
		hud.set_status("Blue gate selected. Press Space or use the operations button.")
		refresh_presentation()
		return
	var unit_ids: Array = world.units.keys()
	unit_ids.sort()
	for raw_id: Variant in unit_ids:
		var unit: WorldScript.UnitRecord = world.get_unit(int(raw_id))
		if unit != null and unit.team == WorldScript.TEAM_BLUE and unit.is_alive() and unit.cell == cell:
			selected_unit_id = unit.id
			selected_gate_id = 0
			hud.set_status("Blue scout selected. Right-click a visible or explored destination.")
			refresh_presentation()
			return
	selected_gate_id = 0
	selected_unit_id = 0
	hud.set_status("No selectable blue entity at %s." % str(cell))
	refresh_presentation()


func _on_cell_left(cell: Vector2i, _additive: bool) -> void:
	place_at_cell(cell)


func _on_cell_right(cell: Vector2i) -> void:
	move_selected_to(cell)


func _unhandled_input(event: InputEvent) -> void:
	if not event is InputEventKey:
		return
	var key := event as InputEventKey
	if not key.pressed or key.echo:
		return
	match key.keycode:
		KEY_R:
			rotate_build()
		KEY_ENTER, KEY_KP_ENTER:
			commit_chain()
		KEY_SPACE:
			toggle_selected_gate()
		KEY_ESCAPE:
			_return_to_menu()
		_:
			return
	get_viewport().set_input_as_handled()


func _return_to_menu() -> void:
	get_tree().change_scene_to_file("res://scenes/boot.tscn")


func _load_definitions() -> Dictionary:
	var path: String = ProjectSettings.globalize_path("res://../content/openbfme-test/data/defenses.json")
	if not FileAccess.file_exists(path):
		push_error("Stage 3 defenses file missing: %s" % path)
		return {"schema": "openbfme.defenses", "schemaVersion": 0, "structures": []}
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not parsed is Dictionary:
		push_error("Stage 3 defenses file is not a JSON object")
		return {"schema": "openbfme.defenses", "schemaVersion": 0, "structures": []}
	return parsed as Dictionary


func _exit_tree() -> void:
	pending_chain.clear()
	if board != null:
		board.world = null
	world = null
