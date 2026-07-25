extends SceneTree
## godot --headless --path game -s res://tests/stage5_proof_runner.gd

const WorldScript = preload("res://src/proof_stage5/proof_world.gd")

var passed: int = 0
var failed: int = 0
var document: Dictionary = {}
var replay_hash: String = "00000000"


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "STAGE5_PROOF_RUNNER")
	call_deferred("_run")


func _run() -> void:
	document = _load_document()
	_test_contract_validation()
	_test_progression_prerequisites_and_tiers()
	_test_defeat_power_point_rewards()
	_test_targeted_area_and_cooldown_effects()
	_test_building_global_and_weather_effects()
	_test_data_driven_values_and_hash_coverage()
	_test_scheduled_replay()
	if failed == 0:
		print("STAGE5_GODOT_PROOF PASS authority=gdscript-proof assertions=%d hash=%s" % [passed, replay_hash])
		quit(0)
	else:
		print("STAGE5_GODOT_PROOF FAIL authority=gdscript-proof assertions=%d failed=%d hash=%s" % [passed + failed, failed, replay_hash])
		quit(1)


func _test_contract_validation() -> void:
	_check("external_powers_json_parses", not document.is_empty())
	var world: WorldScript = _new_world()
	_check("powers_contract_configures", world != null and world.power_definitions().size() == 7)
	if world == null:
		return
	var modes: Array[String] = []
	for definition: Dictionary in world.power_definitions():
		modes.append(String(definition["targetMode"]))
	modes.sort()
	_check("powers_cover_targeted_area_global_modes", modes == ["friendly_entity", "global", "global", "hostile_building", "hostile_entity", "position", "position"])

	var duplicate: Dictionary = document.duplicate(true)
	var duplicate_powers: Array = duplicate["powers"]
	(duplicate_powers[1] as Dictionary)["code"] = 1
	var duplicate_world := WorldScript.new()
	_check("duplicate_power_code_rejected", duplicate_world.setup(duplicate) == "power_identity")

	var cycle: Dictionary = document.duplicate(true)
	var cycle_powers: Array = cycle["powers"]
	(cycle_powers[0] as Dictionary)["prerequisites"] = [2]
	(cycle_powers[1] as Dictionary)["prerequisites"] = [1]
	var cycle_world := WorldScript.new()
	_check("prerequisite_cycle_rejected", cycle_world.setup(cycle) == "power_prerequisite_cycle")

	var invalid_effect: Dictionary = document.duplicate(true)
	var invalid_powers: Array = invalid_effect["powers"]
	var effects: Array = (invalid_powers[0] as Dictionary)["effects"]
	(effects[0] as Dictionary)["amount"] = 0
	var invalid_world := WorldScript.new()
	_check("invalid_effect_magnitude_rejected", invalid_world.setup(invalid_effect) == "power_effect_1")


func _test_progression_prerequisites_and_tiers() -> void:
	var world: WorldScript = _new_world()
	if world == null:
		return
	var blue: Dictionary = world.team_state(WorldScript.TEAM_BLUE)
	var red: Dictionary = world.team_state(WorldScript.TEAM_RED)
	_check("spellbook_starts_with_external_points", int(blue["available_points"]) == 2 and int(blue["spent_points"]) == 0)
	_check("prerequisite_unlock_rejected", _reason(world.unlock_power(0, 3)) == "prerequisite_locked")
	_check("tier_one_unlock_spends_exact_cost", bool(world.unlock_power(0, 1).get("ok", false)) and bool(world.unlock_power(0, 2).get("ok", false)) and int(blue["available_points"]) == 0 and int(blue["spent_points"]) == 2)
	world.grant_power_points(0, 8)
	_check("tier_spend_gate_rejects_even_with_points", _reason(world.unlock_power(0, 6)) == "tier_spend_locked" and int(blue["available_points"]) == 8)
	# Return to an exact zero-point state so XP conversion and remainder behavior are isolated.
	blue["available_points"] = 0
	blue["earned_points"] = 0
	_check("spellbook_accounting_fixture_valid", world.validate_state() == "")
	var first_xp: Dictionary = world.award_spellbook_experience(0, 199)
	_check("experience_uses_integer_point_conversion", int(first_xp.get("points_gained", -1)) == 1 and int(blue["experience_remainder"]) == 99 and int(blue["available_points"]) == 1)
	_check("unlock_rejects_insufficient_points_transactionally", _reason(world.unlock_power(0, 3)) == "insufficient_points" and int(blue["spent_points"]) == 2 and Array(blue["unlocked"]) == [1, 2])
	var boundary_xp: Dictionary = world.award_spellbook_experience(0, 1)
	_check("experience_boundary_awards_exact_point", int(boundary_xp.get("points_gained", -1)) == 1 and int(blue["experience_remainder"]) == 0)
	_check("tier_two_unlock_succeeds_after_spend_and_prerequisite", bool(world.unlock_power(0, 3).get("ok", false)) and int(blue["spent_points"]) == 4 and int(blue["available_points"]) == 0)
	world.award_spellbook_experience(0, 300)
	_check("tier_three_unlock_succeeds_after_threshold", bool(world.unlock_power(0, 6).get("ok", false)) and int(blue["spent_points"]) == 7)
	world.grant_power_points(0, 9)
	var remaining_tree_ok: bool = bool(world.unlock_power(0, 4).get("ok", false)) and bool(world.unlock_power(0, 5).get("ok", false)) and bool(world.unlock_power(0, 7).get("ok", false))
	_check("tier_tree_has_no_softlock", remaining_tree_ok and Array(blue["unlocked"]) == [1, 2, 3, 4, 5, 6, 7] and int(blue["spent_points"]) == 16 and int(blue["available_points"]) == 0)
	_check("progression_is_team_local", int(red["available_points"]) == 2 and int(red["spent_points"]) == 0 and Array(red["unlocked"]).is_empty())
	_check("progression_state_valid", world.validate_state() == "", world.validate_state())


func _test_defeat_power_point_rewards() -> void:
	var unit_world: WorldScript = _new_world()
	if unit_world == null:
		return
	unit_world.unlock_power(0, 2)
	var red_unit: int = unit_world.add_unit(1, Vector2i(5, 4), 100, 100)
	var unit_points_before: int = int(unit_world.team_state(0)["available_points"])
	var unit_defeat: Dictionary = unit_world.cast_power(0, 2, {"entity_id": red_unit})
	_check("unit_defeat_awards_external_power_points", bool(unit_defeat.get("ok", false)) and int(unit_world.entity(red_unit)["health"]) == 0 and int(unit_defeat.get("power_points_earned", 0)) == 1 and int(unit_world.team_state(0)["available_points"]) == unit_points_before + 1)

	var building_world: WorldScript = _new_world()
	if building_world == null:
		return
	building_world.grant_power_points(0, 20)
	for code: int in [1, 2, 3, 4, 5, 6, 7]:
		building_world.unlock_power(0, code)
	var red_building: int = building_world.add_building(1, Vector2i(7, 5), 300, 300)
	var building_points_before: int = int(building_world.team_state(0)["available_points"])
	var building_defeat: Dictionary = building_world.cast_power(0, 6, {"entity_id": red_building})
	_check("building_defeat_awards_larger_external_power_points", bool(building_defeat.get("ok", false)) and int(building_world.entity(red_building)["health"]) == 0 and int(building_defeat.get("power_points_earned", 0)) == 2 and int(building_world.team_state(0)["available_points"]) == building_points_before + 2)
	_check("defeat_rewards_preserve_spellbook_accounting", unit_world.validate_state() == "" and building_world.validate_state() == "")


func _test_targeted_area_and_cooldown_effects() -> void:
	var world: WorldScript = _effect_world()
	if world == null:
		return
	var blue_unit: int = 100
	var blue_unit_two: int = 101
	var blue_building: int = 1000
	var red_close: int = 102
	var red_near: int = 103
	var red_far: int = 104

	_check("friendly_heal_rejects_hostile_target", _reason(world.cast_power(0, 1, {"entity_id": red_close})) == "target_not_friendly")
	var heal: Dictionary = world.cast_power(0, 1, {"entity_id": blue_unit})
	_check("targeted_heal_applies_external_amount", bool(heal.get("ok", false)) and int(heal.get("healed", 0)) == 240 and int(world.entity(blue_unit)["health"]) == 840)
	_check("targeted_heal_starts_exact_cooldown", world.cooldown_remaining(0, 1) == 6 and _reason(world.cast_power(0, 1, {"entity_id": blue_unit})) == "cooldown")

	_check("targeted_damage_rejects_friendly", _reason(world.cast_power(0, 2, {"entity_id": blue_unit})) == "target_not_hostile_unit")
	var strike: Dictionary = world.cast_power(0, 2, {"entity_id": red_close})
	_check("targeted_damage_applies_external_amount", bool(strike.get("ok", false)) and int(strike.get("damage", 0)) == 180 and int(world.entity(red_close)["health"]) == 720)
	world.advance(4)
	_check("cooldown_rejects_until_exact_ready_tick", world.cooldown_remaining(0, 2) == 1 and _reason(world.cast_power(0, 2, {"entity_id": red_close})) == "cooldown")
	world.advance(1)
	_check("cooldown_releases_on_exact_tick", world.cooldown_remaining(0, 2) == 0 and bool(world.cast_power(0, 2, {"entity_id": red_close}).get("ok", false)))

	_check("position_power_rejects_non_vector", _reason(world.cast_power(0, 3, {"position": [8, 4]})) == "invalid_position_target")
	var area: Dictionary = world.cast_power(0, 3, {"position": Vector2i(8, 4)})
	_check("area_damage_uses_stable_radius_membership", bool(area.get("ok", false)) and Array(area.get("affected_ids", [])) == [red_close, red_near])
	_check("area_damage_hits_units_not_far_or_buildings", int(world.entity(red_close)["health"]) == 420 and int(world.entity(red_near)["health"]) == 680 and int(world.entity(red_far)["health"]) == 700 and int(world.entity(1001)["health"]) == 1600)

	var repair: Dictionary = world.cast_power(0, 4, {"position": Vector2i(3, 4)})
	_check("area_heal_covers_friendly_units_and_building", bool(repair.get("ok", false)) and Array(repair.get("affected_ids", [])) == [blue_unit, blue_unit_two, blue_building])
	_check("area_heal_uses_unit_and_building_values", int(world.entity(blue_unit)["health"]) == 990 and int(world.entity(blue_unit_two)["health"]) == 650 and int(world.entity(blue_building)["health"]) == 980 and int(repair.get("healed", 0)) == 480)
	_check("targeted_area_state_valid", world.validate_state() == "", world.validate_state())


func _test_building_global_and_weather_effects() -> void:
	var world: WorldScript = _effect_world()
	if world == null:
		return
	var red_unit: int = 102
	var red_building: int = 1001
	_check("building_power_rejects_unit", _reason(world.cast_power(0, 6, {"entity_id": red_unit})) == "target_not_hostile_building")
	var siege: Dictionary = world.cast_power(0, 6, {"entity_id": red_building})
	_check("building_damage_applies_external_amount", bool(siege.get("ok", false)) and int(siege.get("damage", 0)) == 350 and int(world.entity(red_building)["health"]) == 1250)

	_check("global_weather_rejects_target_payload", _reason(world.cast_power(0, 5, {"entity_id": red_unit})) == "unexpected_target")
	var blue_health: int = int(world.entity(100)["health"])
	var weather_cast: Dictionary = world.cast_power(0, 5)
	_check("global_weather_activates_from_definition", bool(weather_cast.get("ok", false)) and String(world.weather.get("code", "")) == "tempest" and int(world.weather.get("remaining_ticks", 0)) == 4)
	world.advance(1)
	_check("weather_tick_damages_only_hostile_kinds", int(world.entity(red_unit)["health"]) == 875 and int(world.entity(red_building)["health"]) == 1240 and int(world.entity(100)["health"]) == blue_health)
	_check("weather_duration_decrements_exactly", int(world.weather.get("remaining_ticks", 0)) == 3)
	world.advance(3)
	_check("weather_expires_on_exact_tick", world.weather.is_empty() and int(world.entity(red_unit)["health"]) == 800 and int(world.entity(red_building)["health"]) == 1210)

	world.apply_damage(100, 300)
	world.apply_damage(101, 200)
	world.apply_damage(1000, 300)
	var renewal: Dictionary = world.cast_power(0, 7)
	_check("global_heal_affects_all_friendly_entities", bool(renewal.get("ok", false)) and Array(renewal.get("affected_ids", [])) == [100, 101, 1000])
	_check("global_heal_uses_separate_building_amount", int(world.entity(100)["health"]) == 480 and int(world.entity(101)["health"]) == 480 and int(world.entity(1000)["health"]) == 720 and int(renewal.get("healed", 0)) == 580)
	_check("weather_global_state_valid", world.validate_state() == "", world.validate_state())


func _test_data_driven_values_and_hash_coverage() -> void:
	var modified: Dictionary = document.duplicate(true)
	var powers: Array = modified["powers"]
	var effects: Array = (powers[1] as Dictionary)["effects"]
	(effects[0] as Dictionary)["amount"] = 333
	var modified_world := WorldScript.new()
	var setup_error: String = modified_world.setup(modified)
	modified_world.add_unit(0, Vector2i(2, 2), 500)
	modified_world.add_unit(1, Vector2i(4, 2), 500)
	modified_world.unlock_power(0, 2)
	var modified_hit: Dictionary = modified_world.cast_power(0, 2, {"entity_id": 101})
	_check("modified_external_damage_drives_runtime", setup_error == "" and int(modified_hit.get("damage", 0)) == 333 and int(modified_world.entity(101)["health"]) == 167)

	var normal_world := WorldScript.new()
	normal_world.setup(document)
	normal_world.add_unit(0, Vector2i(2, 2), 500)
	normal_world.add_unit(1, Vector2i(4, 2), 500)
	normal_world.unlock_power(0, 2)
	normal_world.cast_power(0, 2, {"entity_id": 101})
	_check("state_hash_covers_external_definitions", normal_world.state_hash() != modified_world.state_hash())
	var base_hash: int = normal_world.state_hash()
	var blue: Dictionary = normal_world.team_state(0)
	blue["available_points"] = int(blue["available_points"]) + 1
	blue["earned_points"] = int(blue["earned_points"]) + 1
	var progression_changed: bool = normal_world.state_hash() != base_hash
	blue["available_points"] = int(blue["available_points"]) - 1
	blue["earned_points"] = int(blue["earned_points"]) - 1
	_check("state_hash_covers_progression_state", progression_changed and normal_world.state_hash() == base_hash)
	normal_world.weather = {"code": "test", "owner": 0, "remaining_ticks": 1, "unit_damage_per_tick": 1, "building_damage_per_tick": 1}
	_check("state_hash_covers_weather_state", normal_world.state_hash() != base_hash)


func _test_scheduled_replay() -> void:
	var first: WorldScript = _build_replay_world()
	var second: WorldScript = _build_replay_world()
	first.advance(14)
	second.advance(14)
	replay_hash = first.state_hash_text()
	_check("scheduled_commands_replay_equal", first.state_hash() == second.state_hash(), "hash=%s" % replay_hash)
	_check("scheduled_replay_effects_resolve", int(first.entity(101)["health"]) < 900 and first.is_power_unlocked(0, 5) and first.weather.is_empty())
	_check("scheduled_replay_state_valid", first.validate_state() == "" and second.validate_state() == "", first.validate_state())


func _build_replay_world() -> WorldScript:
	var world: WorldScript = _new_world()
	if world == null:
		return null
	world.add_unit(0, Vector2i(2, 4), 1000, 900)
	world.add_unit(1, Vector2i(9, 4), 1000, 900)
	world.add_building(1, Vector2i(10, 6), 1600)
	world.schedule_command("unlock", 0, 0, 0, 1)
	world.schedule_command("unlock", 0, 0, 1, 2)
	world.schedule_command("experience", 0, 1, 2, 0, {}, 500)
	world.schedule_command("unlock", 0, 2, 3, 3)
	world.schedule_command("cast", 0, 3, 4, 2, {"entity_id": 101})
	world.schedule_command("cast", 0, 4, 5, 3, {"position": Vector2i(9, 4)})
	world.schedule_command("unlock", 0, 5, 6, 5)
	world.schedule_command("cast", 0, 6, 7, 5)
	return world


func _effect_world() -> WorldScript:
	var world: WorldScript = _new_world()
	if world == null:
		return null
	world.grant_power_points(0, 20)
	for code: int in [1, 2, 3, 4, 5, 6, 7]:
		var result: Dictionary = world.unlock_power(0, code)
		if not bool(result.get("ok", false)):
			_check("effect_fixture_unlock_%d" % code, false, _reason(result))
			return null
	world.add_unit(0, Vector2i(3, 3), 1000, 600)
	world.add_unit(0, Vector2i(4, 4), 1000, 500)
	world.add_unit(1, Vector2i(8, 4), 900, 900)
	world.add_unit(1, Vector2i(9, 5), 800, 800)
	world.add_unit(1, Vector2i(14, 8), 700, 700)
	world.add_building(0, Vector2i(3, 5), 1200, 800)
	world.add_building(1, Vector2i(12, 5), 1600, 1600)
	return world


func _new_world() -> WorldScript:
	var world := WorldScript.new()
	var error: String = world.setup(document)
	if error != "":
		_check("world_setup", false, error)
		return null
	return world


func _load_document() -> Dictionary:
	var path: String = ProjectSettings.globalize_path("res://../content/openbfme-test/data/powers.json")
	var parsed: Variant = JSON.parse_string(FileAccess.get_file_as_string(path))
	return parsed as Dictionary if parsed is Dictionary else {}


func _reason(result: Dictionary) -> String:
	return String(result.get("reason", ""))


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])
