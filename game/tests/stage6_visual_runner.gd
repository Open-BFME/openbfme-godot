extends SceneTree
## Exercises the real Stage 6 scene, data-driven controls, and primitive board.

const LabScript = preload("res://src/stage6/stage6_lab.gd")

var passed: int = 0
var failed: int = 0
var presentation_probe_ms: float = 0.0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var packed := load("res://scenes/stage6_lab.tscn") as PackedScene
	_check("real_stage6_scene_loads", packed != null)
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
	_check("real_stage6_scene_ready", lab.world != null and lab.definition_error == "" and board != null and hud != null)
	_check("real_scene_loads_external_schema", String(lab.definition_document.get("schema", "")) == "openbfme.faction-rosters")
	_check("four_data_driven_faction_buttons", hud.faction_buttons.size() == 4)
	_check("default_faction_has_two_roster_buttons", hud.roster_buttons.size() == 2)
	_check("default_faction_has_two_research_buttons", hud.upgrade_buttons.size() == 2)
	_check("art_coverage_is_visible", String(hud.coverage_label.text).contains("8/8") and String(hud.coverage_label.text).contains("missing 0"))
	_check("stage6_uses_no_sprite_or_texture_nodes", _has_no_sprite_nodes(lab))
	_check("board_uses_same_authoritative_world", board.world == lab.world)
	_check("default_matrix_readout_is_visible", String(hud.matrix_label.text).contains("MATRIX") and String(hud.matrix_label.text).contains("damage"))
	_check("default_attacker_and_target_exist", lab.attacker_id > 0 and lab.target_id > 0)

	(hud.faction_buttons["ember_union"] as Button).emit_signal("pressed")
	_check("faction_button_changes_active_faction", lab.active_faction_id == "ember_union")
	_check("context_rebuilds_ember_roster", hud.roster_buttons.has("ember_breaker") and hud.roster_buttons.has("ember_channeler"))
	(hud.roster_buttons["ember_channeler"] as Button).emit_signal("pressed")
	_check("roster_button_selects_deployed_unit", String(lab.world.entity(lab.attacker_id)["unit_id"]) == "ember_channeler")

	(hud.faction_buttons["aurora_compact"] as Button).emit_signal("pressed")
	(hud.roster_buttons["aurora_ranger"] as Button).emit_signal("pressed")
	var hostile: int = lab.find_deployed_unit("ember_breaker")
	_check("hostile_target_selection_is_authoritative", lab.select_target(hostile) and lab.target_id == hostile)
	var old_health: int = int(lab.world.entity(hostile)["health"])
	var attack: Dictionary = lab.request_attack()
	_check("real_scene_attack_resolves_matrix", bool(attack.get("ok", false)) and int(lab.world.entity(hostile)["health"]) < old_health)
	_check("attack_feedback_names_matrix", String(hud.feedback_label.text).contains("Matrix resolved"))
	var after_attack: int = int(lab.world.entity(hostile)["health"])
	_check("real_scene_cooldown_rejects_repeat", String(lab.request_attack().get("reason", "")) == "cooldown" and int(lab.world.entity(hostile)["health"]) == after_attack)

	var resources_before: int = int(lab.world.faction_states["aurora_compact"]["resources"])
	(hud.upgrade_buttons["trueflight_vanes"] as Button).emit_signal("pressed")
	_check("research_button_starts_real_job", not Dictionary(lab.world.faction_states["aurora_compact"]["research"]).is_empty())
	_check("research_button_debits_data_cost", int(lab.world.faction_states["aurora_compact"]["resources"]) == resources_before - 110)
	lab.advance_ticks(2)
	_check("visual_research_does_not_complete_early", lab.world.completed_upgrades("aurora_compact").is_empty())
	lab.advance_ticks(1)
	_check("visual_research_completes_exactly", lab.world.completed_upgrades("aurora_compact") == ["trueflight_vanes"])
	_check("matrix_readout_shows_research_modifier", String(hud.matrix_label.text).contains("attack +300"))
	_check("research_label_returns_idle", String(hud.research_label.text).contains("idle"))

	var target_center: Vector2 = board.global_position + board.cell_center(Vector2i(lab.world.entity(hostile)["position"]))
	var mouse := InputEventMouseButton.new()
	mouse.button_index = MOUSE_BUTTON_LEFT
	mouse.pressed = true
	mouse.position = target_center
	lab._unhandled_input(mouse)
	_check("actual_board_click_inspects_hostile", lab.target_id == hostile)
	_check("primitive_picker_returns_target", board.pick_entity(target_center) == hostile)

	(hud.faction_buttons["verdant_league"] as Button).emit_signal("pressed")
	_check("third_faction_is_playable", lab.active_faction_id == "verdant_league" and hud.roster_buttons.size() == 2)
	_check("third_faction_exposes_its_research", hud.upgrade_buttons.has("woven_wards") and hud.upgrade_buttons.has("trueflight_vanes"))
	(hud.faction_buttons["dusk_accord"] as Button).emit_signal("pressed")
	_check("fourth_faction_is_selectable", lab.active_faction_id == "dusk_accord" and hud.roster_buttons.has("dusk_lancer") and hud.roster_buttons.has("dusk_shade"))
	_check("live_scene_state_is_valid", lab.world.validate_state() == "")
	lab.reset_lab()
	_check("reset_restores_default_faction", lab.active_faction_id == "aurora_compact" and lab.world.tick_index == 0)
	_check("reset_restores_full_art_coverage", bool(lab.world.catalog.art_coverage()["legal_safe"]))
	var probe_start: int = Time.get_ticks_usec()
	_check("playable_probe_loads_eighty_battalions", lab.load_performance_probe(80) == 80)
	board.queue_redraw()
	lab.process_mode = Node.PROCESS_MODE_INHERIT
	await process_frame
	lab.process_mode = Node.PROCESS_MODE_DISABLED
	presentation_probe_ms = float(Time.get_ticks_usec() - probe_start) / 1000.0
	_check("eighty_battalion_board_uses_primitives_only", lab.world.entity_ids().size() == 80 and _has_no_sprite_nodes(lab))
	_check("bounded_presentation_probe_completes", presentation_probe_ms < 500.0)

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
		print("STAGE6_VISUAL_METRICS battalions=80 presentation_ms=%.3f note=legal-safe-proof-not-bfme-parity" % presentation_probe_ms)
		print("STAGE6_VISUAL_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE6_VISUAL_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)
