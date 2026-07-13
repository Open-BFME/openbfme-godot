class_name Stage7Lab
extends Node2D
## Interactive deterministic AI strategy and no-softlock proof lab.

const WorldScript = preload("res://src/proof_stage7/proof_world.gd")
const FIXED_DT: float = 0.125

@onready var board: Node2D = $Board
@onready var hud: CanvasLayer = $Hud

var world: RefCounted
var definition_document: Dictionary = {}
var definition_error: String = ""
var selected_difficulty_id: String = "normal"
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
	if world == null or simulation_paused or world.victory:
		return
	accumulator += minf(delta, 0.25)
	while accumulator >= FIXED_DT:
		accumulator -= FIXED_DT
		world.tick()
	_refresh_presentation()


func _unhandled_input(event: InputEvent) -> void:
	if not event is InputEventKey:
		return
	var key := event as InputEventKey
	if not key.pressed or key.echo:
		return
	match key.keycode:
		KEY_N: advance_ticks(1)
		KEY_V: run_to_victory()
		KEY_F: run_starvation_probe()
		KEY_SPACE: toggle_pause()
		KEY_R: reset_lab()
		KEY_ESCAPE: _return_to_menu()
		_: return
	get_viewport().set_input_as_handled()


func reset_lab() -> void:
	world = WorldScript.new()
	definition_error = world.configure(definition_document, selected_difficulty_id)
	if definition_error != "":
		hud.set_feedback("Stage 7 setup rejected: " + definition_error)
		return
	simulation_paused = true
	accumulator = 0.0
	board.configure(world)
	hud.configure(world.catalog)
	hud.set_feedback("Ready. The plan builds, trains twice, and attacks using only a finite deposit.", true)
	_refresh_presentation()


func select_difficulty(difficulty_id: String) -> bool:
	if world == null or world.catalog.difficulty(difficulty_id).is_empty():
		return false
	selected_difficulty_id = difficulty_id
	reset_lab()
	hud.set_feedback("Difficulty set to %s; timing, income, attack strength, and replay hash are data-driven." % difficulty_id.capitalize(), true)
	return true


func advance_ticks(ticks: int) -> void:
	if world != null:
		world.advance(ticks)
	_refresh_presentation()


func run_to_victory() -> Dictionary:
	if world == null:
		return {"ok": false, "reason": "world_missing"}
	simulation_paused = true
	var result: Dictionary = world.run_until_terminal()
	if bool(result.get("victory", false)):
		hud.set_feedback("Victory loop completed at tick %d with %s attack." % [int(result["tick"]), "fallback" if world.fallback_attack else "planned"], true)
	else:
		hud.set_feedback("AI did not terminate inside the declared proof budget.")
	_refresh_presentation()
	return result


func run_starvation_probe() -> Dictionary:
	reset_lab()
	world.force_starvation_probe()
	var result: Dictionary = run_to_victory()
	if bool(result.get("victory", false)):
		hud.set_feedback("Zero-resource probe avoided softlock: unaffordable steps skipped, fallback garrison won at tick %d." % int(result["tick"]), true)
	_refresh_presentation()
	return result


func toggle_pause() -> void:
	simulation_paused = not simulation_paused
	accumulator = 0.0
	_refresh_presentation()


func _refresh_presentation() -> void:
	if world == null:
		return
	board.queue_redraw()
	hud.refresh(world, simulation_paused)


func _load_definitions() -> String:
	var path := ProjectSettings.globalize_path("res://../content/openbfme-test/data/ai_strategies.json")
	if not FileAccess.file_exists(path):
		return "ai_strategies.json missing"
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return "ai_strategies.json could not be opened"
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	if typeof(parsed) != TYPE_DICTIONARY:
		return "ai_strategies.json root must be an object"
	definition_document = parsed
	return ""


func _connect_hud() -> void:
	hud.difficulty_requested.connect(select_difficulty)
	hud.step_requested.connect(func() -> void: advance_ticks(1))
	hud.run_requested.connect(run_to_victory)
	hud.starvation_requested.connect(run_starvation_probe)
	hud.pause_requested.connect(toggle_pause)
	hud.reset_requested.connect(reset_lab)
	hud.menu_requested.connect(_return_to_menu)


func _return_to_menu() -> void:
	get_tree().change_scene_to_file("res://scenes/boot.tscn")
