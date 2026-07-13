class_name Stage5Lab
extends Node2D
## Interactive Stage 5 spellbook progression and world-power laboratory.

const ProofWorldScript = preload("res://src/proof_stage5/proof_world.gd")
const TICKS_PER_SECOND: int = 10
const FIXED_DT: float = 1.0 / float(TICKS_PER_SECOND)

@onready var board: Node2D = $Board
@onready var hud: CanvasLayer = $Hud

var world: RefCounted
var definition_document: Dictionary = {}
var power_definitions: Array[Dictionary] = []
var definition_error: String = ""
var armed_power_code: int = 0
var simulation_paused: bool = false
var accumulator: float = 0.0

var blue_unit_id: int = 0
var blue_reserve_id: int = 0
var blue_building_id: int = 0
var red_unit_id: int = 0
var red_reserve_id: int = 0
var red_building_id: int = 0


func _ready() -> void:
	_connect_hud()
	definition_error = _load_definitions()
	if definition_error != "":
		hud.set_feedback("Definition load failed: " + definition_error)
		return
	hud.configure(power_definitions)
	reset_lab()


func _process(delta: float) -> void:
	if world == null:
		return
	if not simulation_paused:
		accumulator += minf(delta, 0.2)
		var steps: int = 0
		while accumulator >= FIXED_DT and steps < 8:
			accumulator -= FIXED_DT
			world.tick()
			steps += 1
	_refresh_presentation()


func _unhandled_input(event: InputEvent) -> void:
	if world == null:
		return
	if event is InputEventMouseMotion:
		board.update_hover((event as InputEventMouseMotion).position)
		return
	if event is InputEventMouseButton:
		var mouse := event as InputEventMouseButton
		if mouse.pressed and mouse.button_index == MOUSE_BUTTON_RIGHT and armed_power_code != 0:
			cancel_targeting("Power targeting cancelled")
			get_viewport().set_input_as_handled()
			return
		if mouse.pressed and mouse.button_index == MOUSE_BUTTON_LEFT:
			var cell: Vector2i = board.cell_from_screen(mouse.position)
			if cell.x >= 0:
				handle_cell_clicked(cell)
				get_viewport().set_input_as_handled()
			return
	if event is InputEventKey:
		var key := event as InputEventKey
		if not key.pressed or key.echo:
			return
		if key.keycode >= KEY_1 and key.keycode <= KEY_7:
			request_power_action(key.keycode - KEY_0)
			get_viewport().set_input_as_handled()
			return
		match key.keycode:
			KEY_X: grant_experience(100)
			KEY_SPACE: toggle_pause()
			KEY_R: reset_lab()
			KEY_ESCAPE:
				if armed_power_code != 0:
					cancel_targeting("Power targeting cancelled")
				else:
					_return_to_menu()
			_: return
		get_viewport().set_input_as_handled()


func reset_lab() -> void:
	if definition_error != "":
		return
	world = ProofWorldScript.new()
	var error: String = world.setup(definition_document)
	if error != "":
		definition_error = error
		hud.set_feedback("Stage 5 setup rejected: " + error)
		return
	blue_unit_id = int(world.add_unit(0, Vector2i(3, 3), 1000, 650))
	blue_reserve_id = int(world.add_unit(0, Vector2i(5, 5), 800, 620))
	red_unit_id = int(world.add_unit(1, Vector2i(11, 3), 1000, 1000))
	red_reserve_id = int(world.add_unit(1, Vector2i(10, 5), 800, 800))
	blue_building_id = int(world.add_building(0, Vector2i(3, 6), 1400, 900))
	red_building_id = int(world.add_building(1, Vector2i(12, 6), 1800, 1800))
	armed_power_code = 0
	simulation_paused = false
	accumulator = 0.0
	board.configure(world)
	board.clear_targeting()
	hud.set_feedback("Ready. Unlock tier-one powers, earn XP, then climb the external prerequisite tree.", true)
	_refresh_presentation()


func request_power_action(power_code: int) -> Dictionary:
	if world == null:
		return {"ok": false, "reason": "world_missing"}
	if not bool(world.is_power_unlocked(0, power_code)):
		return request_unlock(power_code)
	var definition: Dictionary = world.power_definition(power_code)
	if definition.is_empty():
		return _feedback_result({"ok": false, "reason": "unknown_power"})
	armed_power_code = power_code
	var target_mode: String = String(definition["targetMode"])
	board.set_targeting(target_mode)
	if target_mode == "global":
		return resolve_armed_target()
	hud.set_feedback("%s armed. Click a %s on the board; RMB or Esc cancels." % [String(definition["displayName"]), _friendly_mode(String(definition["targetMode"]))])
	_refresh_presentation()
	return {"ok": true, "reason": "", "armed": true, "power_code": power_code}


func request_unlock(power_code: int) -> Dictionary:
	var result: Dictionary = world.unlock_power(0, power_code)
	if bool(result.get("ok", false)):
		var definition: Dictionary = world.power_definition(power_code)
		hud.set_feedback("Unlocked %s for %d point(s)." % [String(definition.get("displayName", "Power")), int(definition.get("pointCost", 0))], true)
	else:
		hud.set_feedback("Unlock rejected: " + _reason_text(String(result.get("reason", "unknown"))))
	_refresh_presentation()
	return result


func request_cast(power_code: int, target_spec: Dictionary = {}) -> Dictionary:
	var result: Dictionary = world.cast_power(0, power_code, target_spec)
	if bool(result.get("ok", false)):
		var definition: Dictionary = world.power_definition(power_code)
		hud.set_feedback("%s resolved: %d damage, %d healed, ready at tick %d." % [
			String(definition.get("displayName", "Power")), int(result.get("damage", 0)), int(result.get("healed", 0)), int(result.get("ready_tick", world.tick_index)),
		], true)
		cancel_targeting("")
	else:
		hud.set_feedback("Cast rejected: " + _reason_text(String(result.get("reason", "unknown"))))
	_refresh_presentation()
	return result


func handle_cell_clicked(cell: Vector2i) -> Dictionary:
	if armed_power_code == 0:
		var entity_id: int = int(world.entity_at(cell))
		if entity_id == 0:
			return _feedback_result({"ok": false, "reason": "no_entity"})
		var row: Dictionary = world.entity(entity_id)
		hud.set_feedback("Inspected %s %s #%d at %s, HP %d/%d." % ["blue" if int(row["team"]) == 0 else "red", String(row["kind"]), entity_id, str(cell), int(row["health"]), int(row["max_health"])], true)
		return {"ok": true, "reason": "", "entity_id": entity_id}
	return resolve_armed_target(cell)


func resolve_armed_target(cell: Vector2i = Vector2i(-1, -1)) -> Dictionary:
	if armed_power_code == 0:
		return _feedback_result({"ok": false, "reason": "no_power_armed"})
	var definition: Dictionary = world.power_definition(armed_power_code)
	var mode: String = String(definition.get("targetMode", ""))
	var target_spec: Dictionary
	match mode:
		"global": target_spec = {}
		"position": target_spec = {"position": cell}
		"friendly_entity":
			var friendly_id: int = int(world.entity_at(cell, "", 0))
			if friendly_id == 0:
				return _feedback_result({"ok": false, "reason": "no_friendly_entity"})
			target_spec = {"entity_id": friendly_id}
		"hostile_entity":
			var hostile_id: int = int(world.entity_at(cell, "unit", 1))
			if hostile_id == 0:
				return _feedback_result({"ok": false, "reason": "no_hostile_unit"})
			target_spec = {"entity_id": hostile_id}
		"hostile_building":
			var building_id: int = int(world.entity_at(cell, "building", 1))
			if building_id == 0:
				return _feedback_result({"ok": false, "reason": "no_hostile_building"})
			target_spec = {"entity_id": building_id}
		_:
			return _feedback_result({"ok": false, "reason": "unsupported_target_mode"})
	return request_cast(armed_power_code, target_spec)


func grant_experience(amount: int = 100) -> Dictionary:
	var result: Dictionary = world.award_spellbook_experience(0, amount)
	if bool(result.get("ok", false)):
		hud.set_feedback("Granted %d spellbook XP: +%d point(s), remainder %d/100." % [amount, int(result.get("points_gained", 0)), int(result.get("experience_remainder", 0))], true)
	else:
		hud.set_feedback("XP rejected: " + _reason_text(String(result.get("reason", "unknown"))))
	_refresh_presentation()
	return result


func advance_ticks(ticks: int) -> void:
	if world == null:
		return
	world.advance(ticks)
	_refresh_presentation()


func set_simulation_paused(paused: bool) -> void:
	simulation_paused = paused
	accumulator = 0.0
	_refresh_presentation()


func toggle_pause() -> void:
	set_simulation_paused(not simulation_paused)


func cancel_targeting(message: String = "") -> void:
	armed_power_code = 0
	board.clear_targeting()
	if message != "":
		hud.set_feedback(message)
	_refresh_presentation()


func _load_definitions() -> String:
	var path: String = ProjectSettings.globalize_path("res://../content/openbfme-test/data/powers.json")
	if not FileAccess.file_exists(path):
		return "powers_file_missing"
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	if not parsed is Dictionary:
		return "powers_json"
	definition_document = (parsed as Dictionary).duplicate(true)
	var probe := ProofWorldScript.new()
	var error: String = probe.configure(definition_document)
	if error != "":
		return error
	power_definitions = probe.power_definitions()
	return ""


func _connect_hud() -> void:
	hud.power_requested.connect(request_power_action)
	hud.experience_requested.connect(grant_experience)
	hud.advance_requested.connect(func() -> void: advance_ticks(1))
	hud.pause_requested.connect(toggle_pause)
	hud.reset_requested.connect(reset_lab)
	hud.menu_requested.connect(_return_to_menu)


func _refresh_presentation() -> void:
	if world == null:
		return
	board.queue_redraw()
	hud.refresh(world, armed_power_code, simulation_paused)


func _feedback_result(result: Dictionary) -> Dictionary:
	if bool(result.get("ok", false)):
		hud.set_feedback("Action completed.", true)
	else:
		hud.set_feedback("Action rejected: " + _reason_text(String(result.get("reason", "unknown"))))
	_refresh_presentation()
	return result


func _friendly_mode(mode: String) -> String:
	return mode.replace("_", " ")


func _reason_text(reason: String) -> String:
	return reason.replace("_", " ")


func _return_to_menu() -> void:
	get_tree().change_scene_to_file("res://scenes/boot.tscn")
