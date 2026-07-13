extends SceneTree
## Exercises the real Stage 7 scene and all difficulty/no-softlock controls.

const LabScript = preload("res://src/stage7/stage7_lab.gd")

var passed: int = 0
var failed: int = 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var packed := load("res://scenes/stage7_lab.tscn") as PackedScene
	_check("real_stage7_scene_loads", packed != null)
	if packed == null:
		_finish()
		return
	var lab := packed.instantiate() as LabScript
	root.add_child(lab)
	current_scene = lab
	await process_frame
	lab.process_mode = Node.PROCESS_MODE_DISABLED
	var board: Node2D = lab.get_node("Board")
	var hud: CanvasLayer = lab.get_node("Hud")
	_check("real_stage7_scene_ready", lab.world != null and lab.definition_error == "" and board != null and hud != null)
	_check("real_scene_loads_external_ai_schema", String(lab.definition_document.get("schema", "")) == "openbfme.ai-strategies")
	_check("three_data_driven_difficulty_buttons", hud.difficulty_buttons.size() == 3)
	_check("difficulty_buttons_expose_distinct_rules", String((hud.difficulty_buttons["easy"] as Button).text).contains("think 3") and String((hud.difficulty_buttons["normal"] as Button).text).contains("think 2") and String((hud.difficulty_buttons["hard"] as Button).text).contains("think 1"))
	_check("normal_is_default_playable_difficulty", lab.selected_difficulty_id == "normal" and lab.world.difficulty_id == "normal")
	_check("finite_deposit_is_visible", String(hud.economy_label.text).contains("FINITE DEPOSIT 240/240"))
	_check("plan_is_visible", String(hud.plan_label.text).contains("Measured Opening") and String(hud.plan_label.text).contains("BUILD"))
	_check("stage7_uses_no_sprite_or_texture_nodes", _has_no_sprite_nodes(lab))
	_check("board_uses_authoritative_ai_world", board.world == lab.world)

	(hud.difficulty_buttons["hard"] as Button).emit_signal("pressed")
	_check("hard_button_resets_real_world", lab.selected_difficulty_id == "hard" and lab.world.difficulty_id == "hard" and lab.world.tick_index == 0)
	_check("hard_rule_readout_is_distinct", String(hud.plan_label.text).contains("think every 1 ticks"))
	(hud.get("difficulty_buttons")["hard"] as Button).disabled = true
	hud.step_requested.emit()
	_check("step_control_advances_one_tick", lab.world.tick_index == 1)
	_check("first_visual_tick_starts_build_job", String(lab.world.active_job.get("action", "")) == "build")
	_check("event_log_shows_build_start", String(hud.event_label.text).contains("job_started"))
	var hard_result: Dictionary = lab.run_to_victory()
	_check("hard_real_scene_reaches_victory", bool(hard_result.get("victory", false)) and lab.world.enemy_fortress_health == 0)
	_check("hard_real_scene_uses_planned_attack", not lab.world.fallback_attack and lab.world.army_size == 3)
	_check("victory_hud_is_readable", String(hud.victory_label.text).contains("VICTORY") and String(hud.victory_label.text).contains("PLANNED"))
	_check("victory_event_is_visible", String(hud.event_label.text).contains("victory"))
	_check("hard_real_scene_state_is_valid", lab.world.validate_state() == "")

	(hud.difficulty_buttons["easy"] as Button).emit_signal("pressed")
	_check("easy_button_is_independently_playable", lab.world.difficulty_id == "easy" and lab.world.tick_index == 0)
	var starved_result: Dictionary = lab.run_starvation_probe()
	_check("zero_resource_control_reaches_victory", bool(starved_result.get("victory", false)))
	_check("zero_resource_control_uses_fallback", lab.world.fallback_attack and lab.world.army_size == 1)
	_check("zero_resource_control_skips_unaffordable_plan", lab.world.events_of_type("skip").size() == 3)
	_check("zero_resource_control_has_no_phantom_harvest", lab.world.events_of_type("harvest").is_empty() and lab.world.resources == 0 and lab.world.finite_resource_remaining == 0)
	_check("fallback_victory_is_readable", String(hud.victory_label.text).contains("FALLBACK") and String(hud.feedback_label.text).contains("avoided softlock"))
	_check("fallback_event_log_is_visible", String(hud.event_label.text).contains("finite_resources_exhausted") and String(hud.event_label.text).contains("attack_order"))
	_check("starved_real_scene_state_is_valid", lab.world.validate_state() == "")

	(hud.difficulty_buttons["normal"] as Button).emit_signal("pressed")
	_check("normal_button_restores_distinct_rules", lab.world.difficulty_id == "normal" and int(lab.world.difficulty["incomePermille"]) == 1000)
	lab.toggle_pause()
	_check("pause_control_changes_live_state", not lab.simulation_paused and String(hud.sim_label.text).contains("LIVE"))
	lab.toggle_pause()
	_check("pause_control_restores_inspection", lab.simulation_paused and String(hud.sim_label.text).contains("PAUSED"))
	_check("reset_world_stays_valid", lab.world.validate_state() == "")

	current_scene = null
	lab.free()
	await process_frame
	_finish()


func _has_no_sprite_nodes(node: Node) -> bool:
	if node is Sprite2D or node is Sprite3D or node is TextureRect:
		return false
	for child: Node in node.get_children():
		if not _has_no_sprite_nodes(child):
			return false
	return true


func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
		print("PASS " + name)
	else:
		failed += 1
		print("FAIL " + name)


func _finish() -> void:
	if failed == 0:
		print("STAGE7_VISUAL_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE7_VISUAL_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)
