class_name Stage4Lab
extends Node2D
## Interactive legal-safe lab around the deterministic Stage 4 proof world.

const ProofWorldScript = preload("res://src/proof_stage4/proof_world.gd")
const TICKS_PER_SECOND: int = 10
const FIXED_DT: float = 1.0 / float(TICKS_PER_SECOND)

@onready var board: Node2D = $Board
@onready var hud: CanvasLayer = $Hud

var world: RefCounted
var definition_document: Dictionary = {}
var ability_definitions: Array[Dictionary] = []
var selected_entity_id: int = 0
var inspected_target_id: int = 0
var targeting_ability_code: int = 0
var simulation_paused: bool = false
var accumulator: float = 0.0
var definition_error: String = ""

var blue_squad_id: int = 0
var red_squad_id: int = 0


func _ready() -> void:
	_connect_hud()
	definition_error = _load_definitions()
	if definition_error != "":
		hud.set_feedback("Definition load failed: " + definition_error)
		return
	hud.configure(ability_definitions)
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
		if mouse.pressed and mouse.button_index == MOUSE_BUTTON_RIGHT and targeting_ability_code != 0:
			cancel_targeting("Ability targeting cancelled")
			get_viewport().set_input_as_handled()
			return
		if mouse.pressed and mouse.button_index == MOUSE_BUTTON_LEFT:
			_handle_board_click(mouse.position)
			get_viewport().set_input_as_handled()
			return
	if event is InputEventKey:
		var key := event as InputEventKey
		if not key.pressed or key.echo:
			return
		if key.keycode >= KEY_1 and key.keycode <= KEY_9:
			var code: int = key.keycode - KEY_0
			if _ability_definition(code).is_empty():
				return
			arm_or_cast_ability(code)
			get_viewport().set_input_as_handled()
			return
		match key.keycode:
			KEY_Q: request_stance("balanced")
			KEY_W: request_stance("aggressive")
			KEY_E: request_stance("defensive")
			KEY_D: request_stance("hold")
			KEY_X: grant_selected_xp()
			KEY_F: request_fear(false)
			KEY_T: request_fear(true)
			KEY_SPACE: toggle_pause()
			KEY_R: reset_lab()
			KEY_ESCAPE:
				if targeting_ability_code != 0:
					cancel_targeting("Ability targeting cancelled")
				else:
					_return_to_menu()
			_: return
		get_viewport().set_input_as_handled()


func reset_lab() -> void:
	if definition_error != "":
		return
	world = ProofWorldScript.new()
	var setup_error: String = world.setup_default(
		ability_definitions,
		definition_document["progression"],
		definition_document["status"],
		definition_document["revival"]
	)
	if setup_error != "":
		definition_error = setup_error
		hud.set_feedback("Stage 4 setup rejected: " + setup_error)
		return
	blue_squad_id = int(world.add_squad(0, Vector2i(3, 4), 4, 120))
	red_squad_id = int(world.add_squad(1, Vector2i(5, 6), 3, 400))
	# This short amber barrier is intentionally adjacent to the red cohort so
	# Impact Drive proves knockback stops before a newly blocked cell.
	world.set_blocked(Vector2i(6, 5))
	world.set_blocked(Vector2i(6, 6))
	world.set_blocked(Vector2i(6, 7))
	selected_entity_id = int(world.champion_for(0)["id"])
	inspected_target_id = red_squad_id
	targeting_ability_code = 0
	simulation_paused = false
	accumulator = 0.0
	board.configure(world)
	board.set_selection(selected_entity_id, inspected_target_id)
	board.clear_targeting()
	hud.set_feedback("Ready. Rank up for Impact Drive, or defeat a concrete squad member and use Reform Ranks.", true)
	_refresh_presentation()


func select_entity(entity_id: int) -> bool:
	var row: Dictionary = world.entity(entity_id) if world != null else {}
	if row.is_empty() or int(row.get("team", -1)) != 0:
		return false
	selected_entity_id = entity_id
	cancel_targeting("")
	hud.set_feedback("Selected blue %s #%d" % [String(row.get("kind", "entity")), entity_id], true)
	_refresh_presentation()
	return true


func inspect_target(entity_id: int) -> bool:
	var row: Dictionary = world.entity(entity_id) if world != null else {}
	if row.is_empty() or int(row.get("team", -1)) != 1:
		return false
	inspected_target_id = entity_id
	hud.set_feedback("Inspecting red %s #%d" % [String(row.get("kind", "entity")), entity_id], true)
	_refresh_presentation()
	return true


func arm_or_cast_ability(ability_code: int) -> Dictionary:
	var definition: Dictionary = _ability_definition(ability_code)
	if definition.is_empty():
		return _feedback_result({"ok": false, "reason": "unknown_ability"})
	var selected: Dictionary = world.entity(selected_entity_id)
	if selected.is_empty() or String(selected.get("kind", "")) != "champion":
		return _feedback_result({"ok": false, "reason": "invalid_caster"})
	if String(definition["target_mode"]) == "self":
		return request_ability(ability_code)
	targeting_ability_code = ability_code
	board.set_targeting(String(definition["target_mode"]), int(definition["range_cells"]))
	hud.set_feedback("%s armed: click a %s within %d cells; RMB or Esc cancels" % [
		String(definition["name"]),
		_friendly_target(String(definition["target_mode"])),
		int(definition["range_cells"]),
	])
	_refresh_presentation()
	return {"ok": true, "reason": "", "armed": true, "ability_code": ability_code}


func request_ability(ability_code: int, target_spec: Dictionary = {}) -> Dictionary:
	if world == null:
		return {"ok": false, "reason": "world_missing"}
	var result: Dictionary = world.cast_ability(selected_entity_id, ability_code, target_spec)
	if bool(result.get("ok", false)):
		var definition: Dictionary = _ability_definition(ability_code)
		cancel_targeting("")
		hud.set_feedback("%s succeeded · ready again at tick %d" % [String(definition.get("name", "Ability")), int(result.get("ready_tick", world.tick_index))], true)
	else:
		hud.set_feedback("Ability rejected: " + _reason_text(String(result.get("reason", "unknown"))))
	_refresh_presentation()
	return result


func request_stance(stance_code: String) -> Dictionary:
	var result: Dictionary = world.set_stance(selected_entity_id, stance_code)
	if bool(result.get("ok", false)):
		var definition: Dictionary = world.progression_system.stance_definition(stance_code)
		hud.set_feedback("%s stance · damage %d%% · armor %d%% · speed %d%%" % [
			stance_code.capitalize(),
			int(definition.get("damage_permille", 1000)) / 10,
			int(definition.get("armor_permille", 1000)) / 10,
			int(definition.get("speed_permille", 1000)) / 10,
		], true)
	else:
		hud.set_feedback("Stance rejected: " + _reason_text(String(result.get("reason", "unknown"))))
	_refresh_presentation()
	return result


func grant_selected_xp(amount: int = 100) -> Dictionary:
	var result: Dictionary = world.award_xp(selected_entity_id, amount)
	if bool(result.get("ok", false)):
		hud.set_feedback("Awarded %d XP · rank %d → %d" % [amount, int(result["old_rank"]), int(result["new_rank"])], true)
	else:
		hud.set_feedback("XP rejected: " + _reason_text(String(result.get("reason", "unknown"))))
	_refresh_presentation()
	return result


func request_fear(terror: bool) -> Dictionary:
	var source: Dictionary = world.champion_for(0)
	var target: Dictionary = world.entity(inspected_target_id)
	if source.is_empty() or target.is_empty():
		var missing := {"ok": false, "reason": "invalid_entity"}
		_feedback_result(missing)
		return missing
	var result: Dictionary = world.apply_fear(int(source["id"]), inspected_target_id, 20, 20, terror)
	if bool(result.get("ok", false)):
		hud.set_feedback("%s applied at power %d · target flees for 20 deterministic ticks" % ["Terror" if terror else "Fear", int(result.get("effective_power", 20))], true)
	else:
		hud.set_feedback("%s rejected: %s" % ["Terror" if terror else "Fear", _reason_text(String(result.get("reason", "unknown")))])
	_refresh_presentation()
	return result


func toggle_target_immunity() -> Dictionary:
	var target: Dictionary = world.entity(inspected_target_id)
	if target.is_empty() or int(target.get("team", -1)) != 1:
		return _feedback_result({"ok": false, "reason": "invalid_entity"})
	var next_immunity: bool = not bool(target.get("fear_immune", false))
	var result: Dictionary = world.configure_fear_profile(inspected_target_id, int(target.get("fear_resistance", 0)), next_immunity)
	var effective_reason: String = String(world.fear_immunity_reason(inspected_target_id))
	var effective_text: String = "OFF" if effective_reason == "" else "ON via %s" % effective_reason.replace("_", " ")
	hud.set_feedback("Target explicit immunity flag %s · effective immunity %s" % ["ON" if next_immunity else "OFF", effective_text], true)
	_refresh_presentation()
	return result


func request_casualty(squad_id: int = 0) -> Dictionary:
	var resolved_id: int = squad_id
	if resolved_id == 0:
		var selected: Dictionary = world.entity(selected_entity_id)
		resolved_id = selected_entity_id if String(selected.get("kind", "")) == "squad" and int(selected.get("team", -1)) == 0 else blue_squad_id
	var living_ids: Array[int] = world.living_member_ids(resolved_id)
	if living_ids.is_empty():
		return _feedback_result({"ok": false, "reason": "squad_defeated"})
	var member_id: int = living_ids[0]
	var result: Dictionary = world.defeat_member(resolved_id, member_id)
	if bool(result.get("ok", false)):
		hud.set_feedback("Concrete member #%d defeated; Reform Ranks restores this ID now, or auto-replenishment starts after 30 quiet ticks" % member_id)
	else:
		hud.set_feedback("Casualty rejected: " + _reason_text(String(result.get("reason", "unknown"))))
	_refresh_presentation()
	result["member_id"] = member_id
	return result


func request_kill_blue_champion() -> Dictionary:
	var champion: Dictionary = world.champion_for(0)
	if champion.is_empty():
		return _feedback_result({"ok": false, "reason": "invalid_champion"})
	var result: Dictionary = world.kill_champion(int(champion["id"]))
	if bool(result.get("ok", false)):
		selected_entity_id = int(champion["id"])
		hud.set_feedback("Blue Champion recorded dead locally · revival cost %d" % int(result.get("revival_cost", 0)))
	else:
		hud.set_feedback("Death rejected: " + _reason_text(String(result.get("reason", "unknown"))))
	_refresh_presentation()
	return result


func request_revive_blue_champion() -> Dictionary:
	var result: Dictionary = world.revive_champion(0)
	if bool(result.get("ok", false)):
		selected_entity_id = int(result["champion_id"])
		hud.set_feedback("Champion revived for %d supplies at %d HP · same unique entity #%d" % [int(result["cost"]), int(result["health"]), int(result["champion_id"])], true)
	else:
		hud.set_feedback("Revival rejected: " + _reason_text(String(result.get("reason", "unknown"))))
	_refresh_presentation()
	return result


func request_train_champion() -> Dictionary:
	var result: Dictionary = world.train_champion(0)
	hud.set_feedback("Training %s: %s" % ["accepted" if bool(result.get("ok", false)) else "rejected", _reason_text(String(result.get("reason", "unique champion preserved")))], bool(result.get("ok", false)))
	_refresh_presentation()
	return result


func toggle_pause() -> void:
	simulation_paused = not simulation_paused
	accumulator = 0.0
	hud.set_feedback("Simulation %s" % ("paused for inspection" if simulation_paused else "resumed at 10 ticks per second"), true)
	_refresh_presentation()


func set_simulation_paused(paused: bool) -> void:
	simulation_paused = paused
	accumulator = 0.0
	_refresh_presentation()


func advance_ticks(ticks: int) -> void:
	if world == null:
		return
	world.advance(ticks)
	_refresh_presentation()


func cancel_targeting(message: String = "") -> void:
	targeting_ability_code = 0
	if board != null:
		board.clear_targeting()
	if message != "" and hud != null:
		hud.set_feedback(message)


func _handle_board_click(screen_position: Vector2) -> void:
	if targeting_ability_code != 0:
		var definition: Dictionary = _ability_definition(targeting_ability_code)
		var mode: String = String(definition.get("target_mode", ""))
		if mode == "position":
			var cell: Vector2i = board.cell_from_screen(screen_position)
			if cell.x < 0:
				hud.set_feedback("Position target rejected: click inside the board")
				return
			request_ability(targeting_ability_code, {"position": cell})
			return
		var picked: int = int(board.pick_entity(screen_position))
		if picked == 0:
			hud.set_feedback("Entity target rejected: click a visible formation")
			return
		request_ability(targeting_ability_code, {"entity_id": picked})
		return
	var picked: int = int(board.pick_entity(screen_position))
	if picked == 0:
		hud.set_feedback("No formation at pointer")
		return
	var row: Dictionary = world.entity(picked)
	if int(row.get("team", -1)) == 0:
		select_entity(picked)
	else:
		inspect_target(picked)


func _refresh_presentation() -> void:
	if world == null or board == null or hud == null:
		return
	board.set_selection(selected_entity_id, inspected_target_id)
	if targeting_ability_code != 0:
		var definition: Dictionary = _ability_definition(targeting_ability_code)
		board.set_targeting(String(definition.get("target_mode", "")), int(definition.get("range_cells", 0)))
	board.queue_redraw()
	var target_mode: String = String(_ability_definition(targeting_ability_code).get("target_mode", "")) if targeting_ability_code != 0 else ""
	hud.refresh(world, selected_entity_id, inspected_target_id, target_mode, simulation_paused)


func _load_definitions() -> String:
	var path: String = ProjectSettings.globalize_path("res://../content/openbfme-test/data/champions.json")
	if not FileAccess.file_exists(path):
		return "champions.json missing"
	var file: FileAccess = FileAccess.open(path, FileAccess.READ)
	if file == null:
		return "champions.json could not be opened"
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	if typeof(parsed) != TYPE_DICTIONARY:
		return "champions.json root must be a dictionary"
	definition_document = parsed
	for key: String in ["abilities", "progression", "status", "revival"]:
		if not definition_document.has(key):
			return "champions.json missing %s" % key
	ability_definitions.clear()
	if typeof(definition_document["abilities"]) != TYPE_ARRAY:
		return "champions.json abilities must be an array"
	for value: Variant in Array(definition_document["abilities"]):
		if typeof(value) != TYPE_DICTIONARY:
			return "champions.json ability must be a dictionary"
		ability_definitions.append(value)
	return ""


func _ability_definition(ability_code: int) -> Dictionary:
	for definition: Dictionary in ability_definitions:
		if int(definition.get("code", 0)) == ability_code:
			return definition
	return {}


func _feedback_result(result: Dictionary) -> Dictionary:
	hud.set_feedback("Rejected: " + _reason_text(String(result.get("reason", "unknown"))))
	_refresh_presentation()
	return result


func _reason_text(reason: String) -> String:
	return reason.replace("_", " ").capitalize()


func _friendly_target(mode: String) -> String:
	match mode:
		"position": return "highlighted ground cell"
		"friendly_entity": return "blue formation"
		"hostile_entity": return "red formation"
		_: return mode.replace("_", " ")


func _connect_hud() -> void:
	hud.ability_requested.connect(arm_or_cast_ability)
	hud.stance_requested.connect(request_stance)
	hud.xp_requested.connect(grant_selected_xp)
	hud.fear_requested.connect(request_fear)
	hud.immunity_requested.connect(toggle_target_immunity)
	hud.casualty_requested.connect(request_casualty)
	hud.kill_requested.connect(request_kill_blue_champion)
	hud.revive_requested.connect(request_revive_blue_champion)
	hud.train_requested.connect(request_train_champion)
	hud.pause_requested.connect(toggle_pause)
	hud.reset_requested.connect(reset_lab)
	hud.menu_requested.connect(_return_to_menu)


func _return_to_menu() -> void:
	get_tree().change_scene_to_file("res://scenes/boot.tscn")
