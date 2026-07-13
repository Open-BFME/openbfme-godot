class_name Stage6Lab
extends Node2D
## Interactive legal-safe Stage 6 faction and research laboratory.

const ProofWorldScript = preload("res://src/proof_stage6/proof_world.gd")
const FIXED_DT: float = 0.125

@onready var board: Node2D = $Board
@onready var hud: CanvasLayer = $Hud

var world: RefCounted
var definition_document: Dictionary = {}
var definition_error: String = ""
var active_faction_id: String = ""
var attacker_id: int = 0
var target_id: int = 0
var simulation_paused: bool = true
var accumulator: float = 0.0


func _ready() -> void:
	_connect_hud()
	definition_error = _load_definitions()
	if definition_error != "":
		hud.set_feedback("Definition load failed: " + definition_error)
		return
	reset_lab()


func _process(delta: float) -> void:
	if world == null or simulation_paused:
		return
	accumulator += minf(delta, 0.25)
	while accumulator >= FIXED_DT:
		accumulator -= FIXED_DT
		world.tick()
	_refresh_presentation()


func _unhandled_input(event: InputEvent) -> void:
	if world == null:
		return
	if event is InputEventMouseButton:
		var mouse := event as InputEventMouseButton
		if mouse.pressed and mouse.button_index == MOUSE_BUTTON_LEFT:
			var picked: int = board.pick_entity(mouse.position)
			if picked != 0:
				var row: Dictionary = world.entity(picked)
				if String(row["faction_id"]) == active_faction_id:
					attacker_id = picked
					hud.set_feedback("Selected attacker #%d" % picked, true)
				else:
					target_id = picked
					hud.set_feedback("Inspected hostile target #%d" % picked, true)
				_refresh_presentation()
			get_viewport().set_input_as_handled()
			return
	if event is InputEventKey:
		var key := event as InputEventKey
		if not key.pressed or key.echo:
			return
		match key.keycode:
			KEY_A: request_attack()
			KEY_SPACE: toggle_pause()
			KEY_N: advance_ticks(1)
			KEY_R: reset_lab()
			KEY_ESCAPE: _return_to_menu()
			_: return
		get_viewport().set_input_as_handled()


func reset_lab() -> void:
	world = ProofWorldScript.new()
	definition_error = world.configure(definition_document)
	if definition_error != "":
		hud.set_feedback("Stage 6 setup rejected: " + definition_error)
		return
	world.setup_showcase()
	active_faction_id = String(world.catalog.faction_ids()[0])
	attacker_id = find_deployed_unit("aurora_ranger")
	target_id = find_deployed_unit("ember_breaker")
	simulation_paused = true
	accumulator = 0.0
	board.configure(world)
	hud.configure(world.catalog)
	hud.set_feedback("All six roster entries resolve to committed primitive recipes. Research an upgrade, then resolve the integer matrix.", true)
	_refresh_presentation()


func select_faction(faction_id: String) -> bool:
	if world == null or world.catalog.faction(faction_id).is_empty():
		return false
	active_faction_id = faction_id
	attacker_id = 0
	for entity_id: int in world.entity_ids():
		if String(world.entity(entity_id)["faction_id"]) == faction_id and bool(world.entity(entity_id)["alive"]):
			attacker_id = entity_id
			break
	for entity_id: int in world.entity_ids():
		if String(world.entity(entity_id)["faction_id"]) != faction_id and bool(world.entity(entity_id)["alive"]):
			target_id = entity_id
			break
	hud.set_feedback("Active faction: %s" % String(world.catalog.faction(faction_id)["displayName"]), true)
	_refresh_presentation()
	return true


func select_roster_unit(unit_id: String) -> bool:
	var entity_id := find_deployed_unit(unit_id)
	if entity_id == 0 or String(world.entity(entity_id)["faction_id"]) != active_faction_id:
		return false
	attacker_id = entity_id
	_refresh_presentation()
	return true


func select_target(entity_id: int) -> bool:
	var row: Dictionary = world.entity(entity_id) if world != null else {}
	if row.is_empty() or String(row.get("faction_id", "")) == active_faction_id:
		return false
	target_id = entity_id
	_refresh_presentation()
	return true


func request_research(upgrade_id: String) -> Dictionary:
	var result: Dictionary = world.begin_research(active_faction_id, upgrade_id)
	if bool(result.get("ok", false)):
		hud.set_feedback("Research started: %s completes at tick %d" % [upgrade_id, int(result["complete_tick"])], true)
	else:
		hud.set_feedback("Research rejected: %s" % String(result.get("reason", "unknown")).replace("_", " "))
	_refresh_presentation()
	return result


func request_attack() -> Dictionary:
	var result: Dictionary = world.attack(attacker_id, target_id)
	if bool(result.get("ok", false)):
		hud.set_feedback("Matrix resolved %d damage (%s vs %s)" % [int(result["damage"]), String(result["damage_type"]), String(result["armor_class"])], true)
	else:
		hud.set_feedback("Attack rejected: %s" % String(result.get("reason", "unknown")).replace("_", " "))
	_refresh_presentation()
	return result


func advance_ticks(ticks: int) -> void:
	if world != null:
		world.advance(ticks)
	_refresh_presentation()


func toggle_pause() -> void:
	simulation_paused = not simulation_paused
	accumulator = 0.0
	_refresh_presentation()


func load_performance_probe(count: int = 80) -> int:
	if world == null:
		return 0
	var loaded: int = int(world.setup_battalion_probe(count))
	active_faction_id = String(world.catalog.faction_ids()[0])
	attacker_id = int(world.entity_ids()[0]) if loaded > 0 else 0
	target_id = 0
	for entity_id: int in world.entity_ids():
		if String(world.entity(entity_id)["faction_id"]) != active_faction_id:
			target_id = entity_id
			break
	board.configure(world)
	hud.set_feedback("Loaded %d legal-safe primitive battalions for a bounded presentation probe." % loaded, true)
	_refresh_presentation()
	return loaded


func find_deployed_unit(unit_id: String) -> int:
	if world == null:
		return 0
	for entity_id: int in world.entity_ids():
		if String(world.entity(entity_id).get("unit_id", "")) == unit_id:
			return entity_id
	return 0


func _refresh_presentation() -> void:
	if world == null:
		return
	board.set_selection(attacker_id, target_id)
	hud.refresh(world, active_faction_id, attacker_id, target_id, simulation_paused)


func _load_definitions() -> String:
	var path := ProjectSettings.globalize_path("res://../content/openbfme-test/data/faction_rosters.json")
	if not FileAccess.file_exists(path):
		return "faction_rosters.json missing"
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return "faction_rosters.json could not be opened"
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	if typeof(parsed) != TYPE_DICTIONARY:
		return "faction_rosters.json root must be an object"
	definition_document = parsed
	return ""


func _connect_hud() -> void:
	hud.faction_requested.connect(select_faction)
	hud.roster_requested.connect(select_roster_unit)
	hud.research_requested.connect(request_research)
	hud.attack_requested.connect(request_attack)
	hud.step_requested.connect(func() -> void: advance_ticks(1))
	hud.pause_requested.connect(toggle_pause)
	hud.performance_probe_requested.connect(load_performance_probe)
	hud.reset_requested.connect(reset_lab)
	hud.menu_requested.connect(_return_to_menu)


func _return_to_menu() -> void:
	get_tree().change_scene_to_file("res://scenes/boot.tscn")
