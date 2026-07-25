extends SceneTree
## godot --headless --path game -s res://tests/stage4_visual_runner.gd

var passed: int = 0
var failed: int = 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "STAGE4_VISUAL_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var packed: PackedScene = load("res://scenes/stage4_lab.tscn")
	_check("stage4_scene_loads", packed != null)
	if packed == null:
		_finish()
		return
	var arena: Node = packed.instantiate()
	root.add_child(arena)
	current_scene = arena
	await process_frame
	await process_frame
	var world: RefCounted = arena.get("world")
	var board: Node2D = arena.get_node("Board")
	var hud: CanvasLayer = arena.get_node("Hud")
	_check("stage4_scene_instantiates_real_lab", world != null and board != null and hud != null and String(arena.get("definition_error")) == "")
	if world == null:
		_finish()
		return
	arena.call("set_simulation_paused", true)
	_test_external_hud_and_board(arena, world, board, hud)
	_test_four_target_modes_toggle_and_rejections(arena, hud)
	_test_stances_and_xp(arena, hud)
	_test_fear_immunity_and_recovery(arena, hud)
	_test_auto_replenishment(arena, hud)
	_test_team_local_revival_and_uniqueness(arena, hud)

	var prior_arena: WeakRef = weakref(arena)
	arena.call("_return_to_menu")
	await process_frame
	await process_frame
	await process_frame
	_check("menu_return_loads_boot_scene", current_scene != null and current_scene.scene_file_path == "res://scenes/boot.tscn")
	_check("menu_return_releases_stage4_scene", prior_arena.get_ref() == null)
	if current_scene != null:
		_check("boot_menu_preserves_stage1_to_stage4_entries", current_scene.has_node("Center/Stage1") and current_scene.has_node("Center/Stage2") and current_scene.has_node("Center/Stage3") and current_scene.has_node("Center/Stage4"))
		var stage4_button := current_scene.get_node("Center/Stage4") as Button
		_check("boot_stage4_button_is_readable", stage4_button != null and stage4_button.text.contains("Champion") and stage4_button.text.contains("Status"))
		stage4_button.emit_signal("pressed")
		await process_frame
		await process_frame
		await process_frame
		_check("boot_stage4_button_launches_real_lab", current_scene != null and current_scene.scene_file_path == "res://scenes/stage4_lab.tscn" and current_scene.get("world") != null)
	if current_scene != null:
		current_scene.queue_free()
		current_scene = null
	await process_frame
	await process_frame
	_finish()


func _test_external_hud_and_board(arena: Node, world: RefCounted, board: Node2D, hud: CanvasLayer) -> void:
	var document: Dictionary = arena.get("definition_document")
	var definitions: Array = arena.get("ability_definitions")
	_check("visual_uses_external_rules_v2", String(document.get("schema", "")) == "openbfme.champions" and int(document.get("rulesVersion", 0)) == 2)
	_check("visual_binds_all_five_external_abilities", definitions.size() == 5 and Dictionary(hud.get("ability_buttons")).size() == 5)
	var ability_layout_readable := true
	for button_value: Variant in Dictionary(hud.get("ability_buttons")).values():
		var ability_button := button_value as Button
		ability_layout_readable = ability_layout_readable and ability_button != null and ability_button.size.x >= 250.0 and ability_button.size.y >= 60.0
	_check("ability_buttons_have_readable_live_layout", ability_layout_readable)
	var modes: Array[String] = []
	for definition_value: Variant in definitions:
		modes.append(String(Dictionary(definition_value).get("target_mode", "")))
	modes.sort()
	_check("visual_covers_all_four_target_modes", modes == ["friendly_entity", "hostile_entity", "position", "self", "self"])
	var ability_buttons: Dictionary = hud.get("ability_buttons")
	var all_bound: bool = true
	for code: int in range(1, 6):
		all_bound = all_bound and ability_buttons.has(code) and ability_buttons[code] is Button
	_check("ability_buttons_preserve_external_codes", all_bound)
	_check("guard_channel_starts_readable_off", String((ability_buttons[5] as Button).text).contains("OFF") or String((ability_buttons[5] as Button).text).contains("LOCKED"))
	_check("board_renders_full_deterministic_grid", board.call("board_size") == Vector2(800, 600) and world.width == 16 and world.height == 12)
	_check("board_has_collision_test_barrier", world.is_blocked(Vector2i(6, 5)) and world.is_blocked(Vector2i(6, 6)) and world.is_blocked(Vector2i(6, 7)))
	var blue_squad_id: int = int(arena.get("blue_squad_id"))
	_check("visual_selects_blue_squad", bool(arena.call("select_entity", blue_squad_id)) and int(arena.get("selected_entity_id")) == blue_squad_id)
	_check("hud_shows_concrete_member_ids", String((hud.get("roster_label") as Label).text).contains("#1000"))
	var red_squad_id: int = int(arena.get("red_squad_id"))
	_check("visual_inspects_red_target_separately", bool(arena.call("inspect_target", red_squad_id)) and int(arena.get("inspected_target_id")) == red_squad_id and int(arena.get("selected_entity_id")) == blue_squad_id)
	var combined_text: String = String((hud.get("selection_label") as Label).text) + String((hud.get("target_label") as Label).text) + String((hud.get("feedback_label") as Label).text)
	_check("visual_text_has_no_mojibake", not combined_text.contains("Â") and not combined_text.contains("â"))


func _test_four_target_modes_toggle_and_rejections(arena: Node, hud: CanvasLayer) -> void:
	_reset(arena)
	var world: RefCounted = arena.get("world")
	var blue_id: int = int(world.champion_for(0)["id"])
	var blue_squad_id: int = int(arena.get("blue_squad_id"))
	var red_squad_id: int = int(arena.get("red_squad_id"))
	var red_id: int = int(world.champion_for(1)["id"])
	_check("visual_self_target_rejection", _reason(arena.call("request_ability", 1, {"entity_id": blue_id})) == "unexpected_target")
	world.damage_entity(blue_id, 400)
	var heal: Dictionary = arena.call("request_ability", 1)
	_check("visual_self_ability_applies", bool(heal.get("ok", false)) and int(world.entity(blue_id)["health"]) == 860)
	_check("visual_self_cooldown_feedback", _reason(arena.call("request_ability", 1)) == "cooldown" and String((hud.get("feedback_label") as Label).text).contains("Cooldown"))

	_check("visual_position_target_type_rejection", _reason(arena.call("request_ability", 2, {"position": [4, 6]})) == "invalid_position_target")
	var ability_buttons: Dictionary = hud.get("ability_buttons")
	(ability_buttons[2] as Button).emit_signal("pressed")
	_check("bound_position_button_arms_correct_code", int(arena.get("targeting_ability_code")) == 2 and String((hud.get("targeting_label") as Label).text).contains("POSITION"))
	var dash: Dictionary = arena.call("request_ability", 2, {"position": Vector2i(4, 6)})
	_check("visual_position_ability_moves_on_clear_cells", bool(dash.get("ok", false)) and Vector2i(world.entity(blue_id)["position"]) == Vector2i(4, 6))

	var casualty: Dictionary = arena.call("request_casualty", blue_squad_id)
	var casualty_id: int = int(casualty.get("member_id", 0))
	_check("visual_defeats_one_concrete_member", bool(casualty.get("ok", false)) and not world.living_member_ids(blue_squad_id).has(casualty_id))
	_check("visual_friendly_mode_rejects_hostile", _reason(arena.call("request_ability", 3, {"entity_id": red_squad_id})) == "target_not_friendly")
	var reform: Dictionary = arena.call("request_ability", 3, {"entity_id": blue_squad_id})
	_check("visual_friendly_ability_restores_same_id", bool(reform.get("ok", false)) and Array(reform.get("restored_ids", [])).has(casualty_id) and world.living_member_ids(blue_squad_id).has(casualty_id))
	_check("visual_replenishment_uses_half_health", _member_health(world.entity(blue_squad_id), casualty_id) == 60)

	_check("visual_hostile_rank_rejection", _reason(arena.call("request_ability", 4, {"entity_id": red_squad_id})) == "rank_locked")
	arena.call("grant_selected_xp", 100)
	_check("visual_hostile_team_rejection", _reason(arena.call("request_ability", 4, {"entity_id": blue_squad_id})) == "target_not_hostile")
	_check("visual_hostile_range_rejection", _reason(arena.call("request_ability", 4, {"entity_id": red_id})) == "target_out_of_range")
	var red_before: Vector2i = world.entity(red_squad_id)["position"]
	var impact: Dictionary = arena.call("request_ability", 4, {"entity_id": red_squad_id})
	_check("visual_hostile_damage_applies_external_value", bool(impact.get("ok", false)) and int(impact.get("damage", 0)) == 180)
	_check("visual_knockback_stops_at_drawn_blocker", int(impact.get("knockback_cells", -1)) == 0 and Vector2i(world.entity(red_squad_id)["position"]) == red_before and world.is_blocked(Vector2i(6, 6)))
	_check("visual_combat_awards_damage_xp", int(world.entity(blue_id)["xp"]) == 118 and String((hud.get("selection_label") as Label).text).contains("XP 118"))

	var toggle_on: Dictionary = arena.call("request_ability", 5)
	_check("visual_toggle_activates_persistently", bool(toggle_on.get("ok", false)) and world.is_toggle_active(blue_id, 5))
	_check("visual_toggle_active_state_is_readable", String((hud.get("ability_buttons") as Dictionary)[5].text).contains("ACTIVE") and String((hud.get("roster_label") as Label).text).contains("ACTIVE"))
	_check("visual_toggle_cooldown_rejection", _reason(arena.call("request_ability", 5)) == "cooldown")
	arena.call("advance_ticks", 12)
	var toggle_off: Dictionary = arena.call("request_ability", 5)
	_check("visual_toggle_deactivates_after_cooldown", bool(toggle_off.get("ok", false)) and not world.is_toggle_active(blue_id, 5) and String((hud.get("ability_buttons") as Dictionary)[5].text).contains("OFF"))
	_check("visual_ability_sequence_state_valid", world.validate_state() == "", world.validate_state())


func _test_stances_and_xp(arena: Node, hud: CanvasLayer) -> void:
	_reset(arena)
	var world: RefCounted = arena.get("world")
	var blue_id: int = int(world.champion_for(0)["id"])
	var red_id: int = int(world.champion_for(1)["id"])
	var red_squad_id: int = int(arena.get("red_squad_id"))
	var blue_squad_id: int = int(arena.get("blue_squad_id"))
	var stance_buttons: Dictionary = hud.get("stance_buttons")
	_check("visual_renders_all_external_stances", stance_buttons.size() == 4)
	(stance_buttons["aggressive"] as Button).emit_signal("pressed")
	var aggressive_hit: Dictionary = world.damage_entity(red_squad_id, 100, blue_id)
	_check("bound_aggressive_stance_changes_damage", int(aggressive_hit.get("damage", 0)) == 125 and String(world.entity(blue_id)["stance"]) == "aggressive")
	arena.call("select_entity", blue_squad_id)
	(stance_buttons["defensive"] as Button).emit_signal("pressed")
	var defensive_hit: Dictionary = world.damage_entity(blue_squad_id, 100, red_id)
	_check("bound_defensive_stance_changes_armor", int(defensive_hit.get("damage", 0)) == 80 and String(world.entity(blue_squad_id)["stance"]) == "defensive")
	var xp_before: int = int(world.entity(blue_squad_id)["xp"])
	world.damage_entity(red_id, 2000, blue_squad_id)
	_check("visual_squad_source_gains_damage_and_kill_xp", int(world.entity(blue_squad_id)["xp"]) > xp_before and int(world.entity(blue_squad_id)["rank"]) >= 2)


func _test_fear_immunity_and_recovery(arena: Node, hud: CanvasLayer) -> void:
	_reset(arena)
	var world: RefCounted = arena.get("world")
	var red_squad_id: int = int(arena.get("red_squad_id"))
	_check("visual_fear_resistance_rejection", _reason(arena.call("request_fear", false)) == "fear_resisted")
	world.set_blocked(Vector2i(6, 6), false)
	var terror: Dictionary = arena.call("request_fear", true)
	_check("visual_terror_overcomes_resistance", bool(terror.get("ok", false)) and String(world.entity(red_squad_id)["status"]) == "flee")
	arena.call("advance_ticks", 1)
	_check("visual_flee_moves_one_safe_cell", Vector2i(world.entity(red_squad_id)["position"]) == Vector2i(6, 6))
	arena.call("advance_ticks", 20)
	_check("visual_flee_recovers_exactly", String(world.entity(red_squad_id)["status"]) == "normal")

	_reset(arena)
	world = arena.get("world")
	red_squad_id = int(arena.get("red_squad_id"))
	arena.call("toggle_target_immunity")
	_check("visual_explicit_immunity_is_readable", world.fear_immunity_reason(red_squad_id) == "explicit" and String((hud.get("target_label") as Label).text).contains("explicit"))
	_check("visual_explicit_immunity_rejects_terror", _reason(arena.call("request_fear", true)) == "fear_immune")
	var red_id: int = int(world.champion_for(1)["id"])
	arena.call("inspect_target", red_id)
	_check("visual_champion_rule_immunity_is_readable", String((hud.get("target_label") as Label).text).contains("champion rule"))
	_check("visual_champion_rule_rejects_terror", _reason(arena.call("request_fear", true)) == "fear_immune")
	arena.call("inspect_target", red_squad_id)
	world.configure_fear_profile(red_squad_id, 20, false)
	world.award_xp(red_squad_id, 700)
	arena.call("advance_ticks", 0)
	_check("visual_rank_rule_immunity_is_readable", world.fear_immunity_reason(red_squad_id) == "rank_rule" and String((hud.get("target_label") as Label).text).contains("rank rule"))
	_check("visual_rank_rule_rejects_terror", _reason(arena.call("request_fear", true)) == "fear_immune")


func _test_auto_replenishment(arena: Node, hud: CanvasLayer) -> void:
	_reset(arena)
	var world: RefCounted = arena.get("world")
	var blue_squad_id: int = int(arena.get("blue_squad_id"))
	var casualty: Dictionary = arena.call("request_casualty", blue_squad_id)
	var member_id: int = int(casualty.get("member_id", 0))
	arena.call("advance_ticks", 29)
	_check("visual_auto_replenishment_waits_30_ticks", not world.living_member_ids(blue_squad_id).has(member_id))
	var red_id: int = int(world.champion_for(1)["id"])
	world.damage_entity(blue_squad_id, 10, red_id)
	var reset_tick: int = int(world.entity(blue_squad_id)["next_replenish_tick"])
	_check("visual_nonlethal_damage_resets_replenishment", reset_tick == int(world.tick_index) + 30)
	arena.call("advance_ticks", 29)
	_check("visual_replenishment_reset_is_not_early", not world.living_member_ids(blue_squad_id).has(member_id))
	arena.call("advance_ticks", 1)
	_check("visual_auto_replenishment_restores_exact_id", world.living_member_ids(blue_squad_id).has(member_id) and _member_health(world.entity(blue_squad_id), member_id) == 60)
	arena.call("select_entity", blue_squad_id)
	_check("visual_auto_replenishment_updates_hud_roster", String((hud.get("roster_label") as Label).text).contains("#%d 60/120" % member_id))


func _test_team_local_revival_and_uniqueness(arena: Node, hud: CanvasLayer) -> void:
	_reset(arena)
	var world: RefCounted = arena.get("world")
	var blue_id: int = int(world.champion_for(0)["id"])
	var red_id: int = int(world.champion_for(1)["id"])
	arena.call("grant_selected_xp", 100)
	var death: Dictionary = arena.call("request_kill_blue_champion")
	_check("visual_death_is_team_local", bool(death.get("ok", false)) and int(world.death_record(0).get("hero_id", 0)) == blue_id and world.death_record(1).is_empty() and bool(world.entity(red_id)["alive"]))
	_check("visual_exact_rank_scaled_revival_cost", world.revival_cost(0) == 600 and String((hud.get("resources_label") as Label).text).contains("600"))
	_check("visual_duplicate_training_rejected", _reason(arena.call("request_train_champion")) == "must_revive")
	world.set_team_resources(0, 599)
	_check("visual_insufficient_revival_rejected", _reason(arena.call("request_revive_blue_champion")) == "insufficient_resources")
	world.set_team_resources(0, 1000)
	var revival: Dictionary = arena.call("request_revive_blue_champion")
	_check("visual_revival_restores_same_unique_entity", bool(revival.get("ok", false)) and int(revival.get("champion_id", 0)) == blue_id and int(world.entity(blue_id)["health"]) == 750 and int(world.entity(blue_id)["rank"]) == 2)
	_check("visual_revival_deducts_exact_resources", int(world.resources[0]) == 400)
	arena.call("request_kill_blue_champion")
	_check("visual_repeat_death_escalates_cost", world.revival_cost(0) == 800)
	var blue_champion_count: int = 0
	for entity_id: int in world.entity_ids():
		var row: Dictionary = world.entity(entity_id)
		if int(row.get("team", -1)) == 0 and String(row.get("kind", "")) == "champion":
			blue_champion_count += 1
	_check("visual_never_duplicates_size_one_champion", blue_champion_count == 1 and int(world.entity(blue_id)["formation_size"]) == 1)
	_check("visual_lifecycle_state_valid", world.validate_state() == "", world.validate_state())


func _reset(arena: Node) -> void:
	arena.call("reset_lab")
	arena.call("set_simulation_paused", true)


func _member_health(squad: Dictionary, member_id: int) -> int:
	for member_value: Variant in Array(squad.get("members", [])):
		var member: Dictionary = member_value
		if int(member.get("id", 0)) == member_id:
			return int(member.get("health", -1))
	return -1


func _reason(result: Dictionary) -> String:
	return String(result.get("reason", ""))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _finish() -> void:
	if failed == 0:
		print("STAGE4_VISUAL_PROOF PASS assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE4_VISUAL_PROOF FAIL assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)
