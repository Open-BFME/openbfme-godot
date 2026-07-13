extends SceneTree
## godot --headless --path game -s res://tests/stage4_proof_runner.gd

const Stage4ProofWorld = preload("res://src/proof_stage4/proof_world.gd")

const EXPECTED_TOP_KEYS: Array[String] = [
	"abilities",
	"progression",
	"revival",
	"rulesVersion",
	"schema",
	"schemaVersion",
	"status",
]

var passed: int = 0
var failed: int = 0
var _document: Dictionary = {}
var _abilities: Array[Dictionary] = []
var _progression: Dictionary = {}
var _status: Dictionary = {}
var _revival: Dictionary = {}
var _repeat_hash: String = "00000000"


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_load_external_definitions()
	_test_external_contract_and_strict_rejection()
	if not _definitions_ready():
		_finish()
		return
	_test_default_champions()
	_test_self_and_position_abilities()
	_test_friendly_replenishment()
	_test_hostile_rank_cooldown_and_knockback()
	_test_veterancy_and_stance_modifiers()
	_test_toggle_damage_xp_and_auto_replenishment()
	_test_fear_terror_immunity_and_recovery()
	_test_rule_based_fear_immunity()
	_test_revival_and_uniqueness()
	_test_deterministic_replay()
	_finish()


func _finish() -> void:
	if failed == 0:
		print("STAGE4_METRICS repeat_hash=%s assertions=%d" % [_repeat_hash, passed])
		print("STAGE4_GODOT_PROOF PASS authority=gdscript-proof assertions=%d" % passed)
		quit(0)
	else:
		print("STAGE4_GODOT_PROOF FAIL authority=gdscript-proof assertions=%d failed=%d" % [passed + failed, failed])
		quit(1)


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("PASS %s%s" % [name, " " + detail if detail != "" else ""])
	else:
		failed += 1
		print("FAIL %s%s" % [name, " " + detail if detail != "" else ""])


func _load_external_definitions() -> void:
	var path: String = ProjectSettings.globalize_path("res://../content/openbfme-test/data/champions.json")
	_check("external_champions_file_exists", FileAccess.file_exists(path), path)
	if not FileAccess.file_exists(path):
		return
	var file: FileAccess = FileAccess.open(path, FileAccess.READ)
	_check("external_champions_file_opens", file != null)
	if file == null:
		return
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	_check("external_champions_json_is_dictionary", typeof(parsed) == TYPE_DICTIONARY)
	if typeof(parsed) != TYPE_DICTIONARY:
		return
	_document = parsed
	var raw_abilities: Variant = _document.get("abilities", [])
	if typeof(raw_abilities) == TYPE_ARRAY:
		for value: Variant in raw_abilities:
			if typeof(value) == TYPE_DICTIONARY:
				_abilities.append(value)
	_progression = _document.get("progression", {})
	_status = _document.get("status", {})
	_revival = _document.get("revival", {})


func _test_external_contract_and_strict_rejection() -> void:
	var actual_keys: Array[String] = []
	for key: Variant in _document.keys():
		actual_keys.append(String(key))
	actual_keys.sort()
	_check("external_top_level_keys_are_exact", actual_keys == EXPECTED_TOP_KEYS, str(actual_keys))
	_check("external_schema_identity", String(_document.get("schema", "")) == "openbfme.champions")
	_check("external_schema_version", int(_document.get("schemaVersion", -1)) == 0 and int(_document.get("rulesVersion", -1)) == 2)
	_check("external_four_target_modes", _abilities.size() == 5 and _ability_modes() == ["friendly_entity", "hostile_entity", "position", "self", "self"])
	_check("external_progression_values", int(Array(_progression.get("rank_thresholds", []))[1]) == 100 and int(_progression.get("replenish_health_permille", 0)) == 500 and int(_progression.get("xp_damage_permille", 0)) == 100 and int(_progression.get("replenish_delay_ticks", 0)) == 30)
	_check("external_revival_values", int(_revival.get("base_cost", -1)) == 300 and int(_revival.get("death_cost", -1)) == 200)
	if not _definitions_ready():
		return
	var valid_world: Stage4ProofWorld = Stage4ProofWorld.new()
	_check("external_definitions_configure", valid_world.setup_default(_abilities, _progression, _status, _revival) == "")
	var extra_field_abilities: Array[Dictionary] = _abilities.duplicate(true)
	extra_field_abilities[0]["unexpected"] = true
	var strict_world: Stage4ProofWorld = Stage4ProofWorld.new()
	var extra_error: String = strict_world.configure(extra_field_abilities, _progression, _status, _revival)
	_check("ability_unknown_field_rejected", extra_error.contains("exactly"), extra_error)
	var missing_mode_abilities: Array[Dictionary] = _abilities.duplicate(true)
	missing_mode_abilities.remove_at(3)
	var mode_world: Stage4ProofWorld = Stage4ProofWorld.new()
	var mode_error: String = mode_world.configure(missing_mode_abilities, _progression, _status, _revival)
	_check("missing_target_mode_rejected", mode_error.contains("target mode"), mode_error)
	var fractional_abilities: Array[Dictionary] = _abilities.duplicate(true)
	fractional_abilities[0]["code"] = 1.5
	var fractional_world: Stage4ProofWorld = Stage4ProofWorld.new()
	var fractional_error: String = fractional_world.configure(fractional_abilities, _progression, _status, _revival)
	_check("fractional_integer_field_rejected", fractional_error.contains("integer"), fractional_error)
	var invalid_activation: Array[Dictionary] = _abilities.duplicate(true)
	invalid_activation[0]["activation_mode"] = "continuous"
	var activation_world: Stage4ProofWorld = Stage4ProofWorld.new()
	var activation_error: String = activation_world.configure(invalid_activation, _progression, _status, _revival)
	_check("invalid_activation_mode_rejected", activation_error.contains("activation_mode"), activation_error)
	var invalid_revival: Dictionary = _revival.duplicate(true)
	invalid_revival["extra"] = 1
	var revival_world: Stage4ProofWorld = Stage4ProofWorld.new()
	var revival_error: String = revival_world.configure(_abilities, _progression, _status, invalid_revival)
	_check("revival_unknown_field_rejected", revival_error.contains("exactly"), revival_error)


func _test_default_champions() -> void:
	var world: Stage4ProofWorld = _new_world()
	var blue: Dictionary = world.champion_for(Stage4ProofWorld.TEAM_BLUE)
	var red: Dictionary = world.champion_for(Stage4ProofWorld.TEAM_RED)
	_check("one_blue_neutral_champion", not blue.is_empty() and int(blue["formation_size"]) == 1 and String(blue["display_name"]) == "Neutral Champion")
	_check("one_red_neutral_champion", not red.is_empty() and int(red["formation_size"]) == 1 and int(red["id"]) != int(blue["id"]))
	_check("champions_start_rank_one", int(blue["rank"]) == 1 and int(red["rank"]) == 1)
	_check("champion_training_duplicate_rejected", _reason(world.train_champion(Stage4ProofWorld.TEAM_BLUE)) == "champion_exists")
	var snapshot: Dictionary = world.snapshot()
	_check("snapshot_declares_gdscript_authority", String(snapshot.get("authority", "")) == "gdscript-proof" and int(snapshot.get("rules_version", 0)) == 4)
	_check("default_world_valid", world.validate_state() == "", world.validate_state())


func _test_self_and_position_abilities() -> void:
	var self_world: Stage4ProofWorld = _new_world()
	var blue_id: int = int(self_world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	_check("self_mode_rejects_target", _reason(self_world.cast_ability(blue_id, 1, {"entity_id": blue_id})) == "unexpected_target")
	self_world.damage_entity(blue_id, 400)
	var self_cast: Dictionary = self_world.cast_ability(blue_id, 1)
	_check("self_heal_applies_external_magnitude", bool(self_cast.get("ok", false)) and int(self_cast.get("healed", 0)) == 260 and int(self_world.entity(blue_id)["health"]) == 860)
	_check("self_ability_cooldown_rejects", _reason(self_world.cast_ability(blue_id, 1)) == "cooldown")
	self_world.advance(30)
	var second_heal: Dictionary = self_world.cast_ability(blue_id, 1)
	_check("self_cooldown_expires_at_exact_tick", bool(second_heal.get("ok", false)) and int(second_heal.get("healed", 0)) == 140)
	self_world.advance(30)
	_check("self_heal_full_health_rejected", _reason(self_world.cast_ability(blue_id, 1)) == "already_full_health")

	var position_world: Stage4ProofWorld = _new_world()
	blue_id = int(position_world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	_check("position_mode_rejects_wrong_type", _reason(position_world.cast_ability(blue_id, 2, {"position": [4, 6]})) == "invalid_position_target")
	_check("position_mode_rejects_range", _reason(position_world.cast_ability(blue_id, 2, {"position": Vector2i(8, 6)})) == "target_out_of_range")
	position_world.set_blocked(Vector2i(3, 6))
	var blocked_dash: Dictionary = position_world.cast_ability(blue_id, 2, {"position": Vector2i(4, 6)})
	_check("position_dash_rejects_blocked_path_atomically", _reason(blocked_dash) == "path_blocked" and Vector2i(position_world.entity(blue_id)["position"]) == Vector2i(2, 6))
	position_world.set_blocked(Vector2i(3, 6), false)
	var dash: Dictionary = position_world.cast_ability(blue_id, 2, {"position": Vector2i(4, 6)})
	_check("position_dash_succeeds", bool(dash.get("ok", false)) and int(dash.get("moved_cells", 0)) == 2 and Vector2i(position_world.entity(blue_id)["position"]) == Vector2i(4, 6))
	_check("position_ability_cooldown_rejects", _reason(position_world.cast_ability(blue_id, 2, {"position": Vector2i(5, 6)})) == "cooldown")

	var stance_speed_world: Stage4ProofWorld = _new_world()
	blue_id = int(stance_speed_world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	stance_speed_world.set_stance(blue_id, "hold")
	_check("hold_stance_reduces_dash_allowance", _reason(stance_speed_world.cast_ability(blue_id, 2, {"position": Vector2i(3, 6)})) == "movement_limit")
	stance_speed_world.set_stance(blue_id, "aggressive")
	_check("aggressive_stance_allows_dash", bool(stance_speed_world.cast_ability(blue_id, 2, {"position": Vector2i(3, 6)}).get("ok", false)))


func _test_friendly_replenishment() -> void:
	var world: Stage4ProofWorld = _new_world()
	var blue_id: int = int(world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	var blue_squad_id: int = world.add_squad(Stage4ProofWorld.TEAM_BLUE, Vector2i(3, 6), 3, 100)
	var red_squad_id: int = world.add_squad(Stage4ProofWorld.TEAM_RED, Vector2i(3, 7), 2, 100)
	_check("friendly_mode_rejects_hostile", _reason(world.cast_ability(blue_id, 3, {"entity_id": red_squad_id})) == "target_not_friendly")
	var roster_before: Array = world.entity(blue_squad_id)["members"]
	var first_id: int = int(Dictionary(roster_before[0])["id"])
	var third_id: int = int(Dictionary(roster_before[2])["id"])
	world.defeat_member(blue_squad_id, third_id)
	world.defeat_member(blue_squad_id, first_id)
	_check("concrete_casualties_remove_exact_ids", world.living_member_ids(blue_squad_id).size() == 1 and not world.living_member_ids(blue_squad_id).has(first_id) and not world.living_member_ids(blue_squad_id).has(third_id))
	var reform: Dictionary = world.cast_ability(blue_id, 3, {"entity_id": blue_squad_id})
	var restored_ids: Array = reform.get("restored_ids", [])
	_check("replenishment_restores_original_member_ids", bool(reform.get("ok", false)) and restored_ids == [first_id, third_id], str(restored_ids))
	_check("replenishment_restores_real_count", world.living_member_ids(blue_squad_id).size() == 3)
	_check("replenishment_uses_external_half_health", _member_health(world.entity(blue_squad_id), first_id) == 50 and _member_health(world.entity(blue_squad_id), third_id) == 50)
	_check("friendly_ability_cooldown_rejects", _reason(world.cast_ability(blue_id, 3, {"entity_id": blue_squad_id})) == "cooldown")
	_check("replenished_world_valid", world.validate_state() == "", world.validate_state())


func _test_hostile_rank_cooldown_and_knockback() -> void:
	var world: Stage4ProofWorld = _new_world()
	var blue_id: int = int(world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	var red_squad_id: int = world.add_squad(Stage4ProofWorld.TEAM_RED, Vector2i(4, 6), 2, 400)
	var blue_squad_id: int = world.add_squad(Stage4ProofWorld.TEAM_BLUE, Vector2i(3, 6), 2, 100)
	_check("hostile_ability_rank_gate_rejects", _reason(world.cast_ability(blue_id, 4, {"entity_id": red_squad_id})) == "rank_locked")
	world.award_xp(blue_id, 100)
	_check("hostile_mode_rejects_friendly", _reason(world.cast_ability(blue_id, 4, {"entity_id": blue_squad_id})) == "target_not_hostile")
	var red_champion_id: int = int(world.champion_for(Stage4ProofWorld.TEAM_RED)["id"])
	_check("hostile_mode_rejects_out_of_range", _reason(world.cast_ability(blue_id, 4, {"entity_id": red_champion_id})) == "target_out_of_range")
	world.set_blocked(Vector2i(5, 6))
	var impact: Dictionary = world.cast_ability(blue_id, 4, {"entity_id": red_squad_id})
	_check("hostile_damage_applies", bool(impact.get("ok", false)) and int(impact.get("damage", 0)) == 180)
	var damaged_member_id: int = int(Dictionary(Array(world.entity(red_squad_id)["members"])[0])["id"])
	_check("hostile_damage_hits_concrete_member", _member_health(world.entity(red_squad_id), damaged_member_id) == 220)
	_check("knockback_stops_before_blocker", int(impact.get("knockback_cells", -1)) == 0 and Vector2i(world.entity(red_squad_id)["position"]) == Vector2i(4, 6))
	_check("hostile_ability_cooldown_rejects", _reason(world.cast_ability(blue_id, 4, {"entity_id": red_squad_id})) == "cooldown")
	world.set_blocked(Vector2i(5, 6), false)
	var direct_knockback: Dictionary = world.apply_knockback(red_squad_id, Vector2i(2, 6), 3)
	_check("knockback_moves_cell_by_cell_when_clear", int(direct_knockback.get("moved_cells", 0)) == 3 and Vector2i(world.entity(red_squad_id)["position"]) == Vector2i(7, 6))


func _test_veterancy_and_stance_modifiers() -> void:
	var xp_world: Stage4ProofWorld = _new_world()
	var blue_id: int = int(xp_world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	_check("negative_xp_rejected", _reason(xp_world.award_xp(blue_id, -1)) == "invalid_xp_award")
	var rank_two: Dictionary = xp_world.award_xp(blue_id, 100)
	_check("xp_reaches_external_rank_two", bool(rank_two.get("ok", false)) and int(rank_two.get("new_rank", 0)) == 2)
	var rank_three: Dictionary = xp_world.award_xp(blue_id, 200)
	_check("xp_reaches_external_rank_three", bool(rank_three.get("ok", false)) and int(rank_three.get("new_rank", 0)) == 3)
	_check("unknown_stance_rejected", _reason(xp_world.set_stance(blue_id, "invented")) == "unknown_stance")

	var aggressive_world: Stage4ProofWorld = _new_world()
	blue_id = int(aggressive_world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	var red_id: int = int(aggressive_world.champion_for(Stage4ProofWorld.TEAM_RED)["id"])
	aggressive_world.set_stance(blue_id, "aggressive")
	var aggressive_hit: Dictionary = aggressive_world.damage_entity(red_id, 100, blue_id)
	_check("aggressive_stance_changes_damage", int(aggressive_hit.get("damage", 0)) == 125)

	var defensive_world: Stage4ProofWorld = _new_world()
	blue_id = int(defensive_world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	red_id = int(defensive_world.champion_for(Stage4ProofWorld.TEAM_RED)["id"])
	defensive_world.set_stance(red_id, "defensive")
	var defensive_hit: Dictionary = defensive_world.damage_entity(red_id, 100, blue_id)
	_check("defensive_stance_changes_armor", int(defensive_hit.get("damage", 0)) == 80)


func _test_toggle_damage_xp_and_auto_replenishment() -> void:
	var toggle_world: Stage4ProofWorld = _new_world()
	var blue_id: int = int(toggle_world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	var red_id: int = int(toggle_world.champion_for(Stage4ProofWorld.TEAM_RED)["id"])
	_check("guard_toggle_rank_gate_rejects", _reason(toggle_world.cast_ability(blue_id, 5)) == "rank_locked")
	toggle_world.award_xp(blue_id, 100)
	var toggle_on: Dictionary = toggle_world.cast_ability(blue_id, 5)
	_check("persistent_guard_toggle_activates", bool(toggle_on.get("ok", false)) and bool(toggle_on.get("active", false)) and toggle_world.is_toggle_active(blue_id, 5))
	_check("guard_toggle_persists_external_modifiers", int(toggle_world.entity(blue_id)["toggle_armor_bonus_permille"]) == 300 and int(toggle_world.entity(blue_id)["toggle_speed_penalty_permille"]) == 250)
	var guarded_hit: Dictionary = toggle_world.damage_entity(blue_id, 100, red_id)
	_check("guard_toggle_changes_actual_armor", int(guarded_hit.get("damage", 0)) == 76)
	_check("guard_toggle_obeys_cooldown", _reason(toggle_world.cast_ability(blue_id, 5)) == "cooldown")
	toggle_world.advance(12)
	var toggle_off: Dictionary = toggle_world.cast_ability(blue_id, 5)
	_check("persistent_guard_toggle_deactivates", bool(toggle_off.get("ok", false)) and not bool(toggle_off.get("active", true)) and not toggle_world.is_toggle_active(blue_id, 5))
	_check("guard_toggle_clears_modifiers", int(toggle_world.entity(blue_id)["toggle_armor_bonus_permille"]) == 0 and int(toggle_world.entity(blue_id)["toggle_speed_penalty_permille"]) == 0)

	var xp_world: Stage4ProofWorld = _new_world()
	blue_id = int(xp_world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	var red_squad_id: int = xp_world.add_squad(Stage4ProofWorld.TEAM_RED, Vector2i(4, 6), 2, 400)
	var nonlethal: Dictionary = xp_world.damage_entity(red_squad_id, 100, blue_id)
	_check("nonlethal_damage_awards_scaled_xp", int(nonlethal.get("damage", 0)) == 100 and int(xp_world.entity(blue_id)["xp"]) == 10)
	var member_kill: Dictionary = xp_world.damage_entity(red_squad_id, 500, blue_id)
	_check("concrete_member_kill_adds_external_bonus", int(member_kill.get("damage", 0)) == 300 and int(xp_world.entity(blue_id)["xp"]) == 65)

	var squad_source_world: Stage4ProofWorld = _new_world()
	var source_squad_id: int = squad_source_world.add_squad(Stage4ProofWorld.TEAM_BLUE, Vector2i(4, 6), 2, 100)
	red_id = int(squad_source_world.champion_for(Stage4ProofWorld.TEAM_RED)["id"])
	var champion_kill: Dictionary = squad_source_world.damage_entity(red_id, 2000, source_squad_id)
	_check("source_squad_gains_damage_and_champion_kill_xp", int(champion_kill.get("damage", 0)) == 1000 and int(squad_source_world.entity(source_squad_id)["xp"]) == 250 and int(squad_source_world.entity(source_squad_id)["rank"]) == 2)
	_check("squad_veterancy_state_validates", squad_source_world.validate_state() == "", squad_source_world.validate_state())

	var replenish_world: Stage4ProofWorld = _new_world()
	var squad_id: int = replenish_world.add_squad(Stage4ProofWorld.TEAM_BLUE, Vector2i(4, 6), 3, 100)
	red_id = int(replenish_world.champion_for(Stage4ProofWorld.TEAM_RED)["id"])
	var original_ids: Array[int] = replenish_world.living_member_ids(squad_id)
	replenish_world.defeat_member(squad_id, original_ids[0])
	replenish_world.advance(29)
	_check("auto_replenishment_waits_external_delay", replenish_world.living_member_ids(squad_id).size() == 2 and int(replenish_world.entity(squad_id)["next_replenish_tick"]) == 30)
	replenish_world.damage_entity(squad_id, 10, red_id)
	_check("nonlethal_hostile_damage_resets_replenishment_delay", int(replenish_world.entity(squad_id)["last_damage_tick"]) == 29 and int(replenish_world.entity(squad_id)["next_replenish_tick"]) == 59)
	replenish_world.advance(29)
	_check("auto_replenishment_still_waits_after_damage", replenish_world.living_member_ids(squad_id).size() == 2)
	replenish_world.advance(1)
	_check("auto_replenishment_restores_same_member_id", replenish_world.living_member_ids(squad_id).has(original_ids[0]) and _member_health(replenish_world.entity(squad_id), original_ids[0]) == 50)

	var cadence_world: Stage4ProofWorld = _new_world()
	squad_id = cadence_world.add_squad(Stage4ProofWorld.TEAM_BLUE, Vector2i(4, 6), 3, 100)
	original_ids = cadence_world.living_member_ids(squad_id)
	cadence_world.defeat_member(squad_id, original_ids[1])
	cadence_world.defeat_member(squad_id, original_ids[0])
	cadence_world.advance(30)
	_check("auto_replenishment_restores_one_per_external_cadence", cadence_world.living_member_ids(squad_id).size() == 2 and cadence_world.living_member_ids(squad_id).has(original_ids[0]))
	cadence_world.advance(11)
	_check("auto_replenishment_interval_not_early", cadence_world.living_member_ids(squad_id).size() == 2)
	cadence_world.advance(1)
	_check("auto_replenishment_interval_restores_next_id", cadence_world.living_member_ids(squad_id).size() == 3 and cadence_world.living_member_ids(squad_id).has(original_ids[1]))


func _test_fear_terror_immunity_and_recovery() -> void:
	var world: Stage4ProofWorld = _new_world()
	var blue_id: int = int(world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	var red_squad_id: int = world.add_squad(Stage4ProofWorld.TEAM_RED, Vector2i(4, 6), 2, 100)
	var blue_squad_id: int = world.add_squad(Stage4ProofWorld.TEAM_BLUE, Vector2i(3, 6), 2, 100)
	world.configure_fear_profile(red_squad_id, 25, false)
	_check("fear_below_resistance_rejected", _reason(world.apply_fear(blue_id, red_squad_id, 20, 2)) == "fear_resisted")
	var terror: Dictionary = world.apply_fear(blue_id, red_squad_id, 20, 2, true)
	_check("terror_bonus_deterministically_overcomes_resistance", bool(terror.get("ok", false)) and int(terror.get("effective_power", 0)) == 40)
	world.tick()
	_check("flee_moves_away_deterministically", Vector2i(world.entity(red_squad_id)["position"]) == Vector2i(5, 6) and String(world.entity(red_squad_id)["status"]) == "flee")
	world.tick()
	_check("flee_duration_moves_exact_ticks", Vector2i(world.entity(red_squad_id)["position"]) == Vector2i(6, 6))
	world.tick()
	_check("fear_recovers_at_exact_tick", String(world.entity(red_squad_id)["status"]) == "normal" and Vector2i(world.entity(red_squad_id)["position"]) == Vector2i(6, 6))
	world.configure_fear_profile(red_squad_id, 0, true)
	var explicit_fear: Dictionary = world.apply_fear(blue_id, red_squad_id, 999, 3, true)
	_check("fear_immunity_rejects_terror", _reason(explicit_fear) == "fear_immune" and String(explicit_fear.get("immunity", "")) == "explicit" and world.fear_immunity_reason(red_squad_id) == "explicit")
	_check("fear_rejects_friendly_target", _reason(world.apply_fear(blue_id, blue_squad_id, 999, 3, true)) == "friendly_target")


func _test_rule_based_fear_immunity() -> void:
	var world: Stage4ProofWorld = _new_world()
	var blue_id: int = int(world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	var red_id: int = int(world.champion_for(Stage4ProofWorld.TEAM_RED)["id"])
	_check("champion_has_external_rule_immunity", world.is_fear_immune(red_id) and world.fear_immunity_reason(red_id) == "champion_rule")
	var champion_fear: Dictionary = world.apply_fear(blue_id, red_id, 999, 3, true)
	_check("champion_rule_immunity_rejects_terror", _reason(champion_fear) == "fear_immune" and String(champion_fear.get("immunity", "")) == "champion_rule")
	var red_squad_id: int = world.add_squad(Stage4ProofWorld.TEAM_RED, Vector2i(4, 6), 2, 100)
	world.award_xp(red_squad_id, 700)
	_check("high_rank_entity_has_external_rule_immunity", int(world.entity(red_squad_id)["rank"]) == 4 and world.fear_immunity_reason(red_squad_id) == "rank_rule")
	var rank_fear: Dictionary = world.apply_fear(blue_id, red_squad_id, 999, 3, true)
	_check("rank_rule_immunity_rejects_terror", _reason(rank_fear) == "fear_immune" and String(rank_fear.get("immunity", "")) == "rank_rule")


func _test_revival_and_uniqueness() -> void:
	var world: Stage4ProofWorld = _new_world()
	var blue_id: int = int(world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	var red_id: int = int(world.champion_for(Stage4ProofWorld.TEAM_RED)["id"])
	world.award_xp(blue_id, 300)
	var death: Dictionary = world.kill_champion(blue_id)
	_check("champion_death_records_team_locally", bool(death.get("ok", false)) and int(world.death_record(Stage4ProofWorld.TEAM_BLUE).get("hero_id", 0)) == blue_id and world.death_record(Stage4ProofWorld.TEAM_RED).is_empty())
	_check("other_team_champion_survives", bool(world.entity(red_id).get("alive", false)))
	_check("rank_and_death_scaled_revival_cost", world.revival_cost(Stage4ProofWorld.TEAM_BLUE) == 700, "cost=%d" % world.revival_cost(Stage4ProofWorld.TEAM_BLUE))
	_check("dead_champion_cannot_be_retrained", _reason(world.train_champion(Stage4ProofWorld.TEAM_BLUE)) == "must_revive")
	_check("living_other_team_cannot_duplicate", _reason(world.train_champion(Stage4ProofWorld.TEAM_RED)) == "champion_exists")
	world.set_team_resources(Stage4ProofWorld.TEAM_BLUE, 699)
	_check("revival_rejects_insufficient_resources", _reason(world.revive_champion(Stage4ProofWorld.TEAM_BLUE)) == "insufficient_resources")
	_check("other_team_cannot_consume_death_record", _reason(world.revive_champion(Stage4ProofWorld.TEAM_RED)) == "no_death_record")
	world.set_team_resources(Stage4ProofWorld.TEAM_BLUE, 1000)
	var revival: Dictionary = world.revive_champion(Stage4ProofWorld.TEAM_BLUE)
	var revived: Dictionary = world.champion_for(Stage4ProofWorld.TEAM_BLUE)
	_check("revival_restores_same_unique_champion", bool(revival.get("ok", false)) and int(revived["id"]) == blue_id and int(revived["rank"]) == 3)
	_check("revival_deducts_cost_and_scales_health", int(revived["health"]) == 750 and world.resources[Stage4ProofWorld.TEAM_BLUE] == 300)
	_check("revival_clears_only_local_pending_record", world.death_record(Stage4ProofWorld.TEAM_BLUE).is_empty() and world.death_record(Stage4ProofWorld.TEAM_RED).is_empty())
	world.kill_champion(blue_id)
	_check("repeat_death_increases_revival_cost", world.revival_cost(Stage4ProofWorld.TEAM_BLUE) == 900, "cost=%d" % world.revival_cost(Stage4ProofWorld.TEAM_BLUE))
	_check("death_world_remains_valid", world.validate_state() == "", world.validate_state())


func _test_deterministic_replay() -> void:
	var first: Stage4ProofWorld = _run_replay()
	var second: Stage4ProofWorld = _run_replay()
	_repeat_hash = first.state_hash_text()
	_check("repeat_replay_hash_equal", first.state_hash() == second.state_hash(), _repeat_hash)
	_check("repeat_replay_snapshot_equal", JSON.stringify(first.snapshot()) == JSON.stringify(second.snapshot()))
	_check("repeat_replay_world_valid", first.validate_state() == "" and second.validate_state() == "", first.validate_state())
	second.set_blocked(Vector2i(9, 9))
	_check("state_hash_changes_with_state", first.state_hash() != second.state_hash())


func _run_replay() -> Stage4ProofWorld:
	var world: Stage4ProofWorld = _new_world()
	var blue_id: int = int(world.champion_for(Stage4ProofWorld.TEAM_BLUE)["id"])
	var red_id: int = int(world.champion_for(Stage4ProofWorld.TEAM_RED)["id"])
	var blue_squad_id: int = world.add_squad(Stage4ProofWorld.TEAM_BLUE, Vector2i(3, 6), 4, 120)
	var red_squad_id: int = world.add_squad(Stage4ProofWorld.TEAM_RED, Vector2i(4, 6), 3, 300)
	var casualty_id: int = int(Dictionary(Array(world.entity(blue_squad_id)["members"])[1])["id"])
	world.defeat_member(blue_squad_id, casualty_id)
	world.cast_ability(blue_id, 3, {"entity_id": blue_squad_id})
	world.award_xp(blue_id, 300)
	world.set_stance(blue_id, "aggressive")
	world.set_stance(red_squad_id, "defensive")
	world.set_blocked(Vector2i(5, 6))
	world.cast_ability(blue_id, 4, {"entity_id": red_squad_id})
	world.configure_fear_profile(red_squad_id, 10, false)
	world.apply_fear(blue_id, red_squad_id, 20, 2, true)
	world.advance(3)
	world.damage_entity(blue_id, 350, red_id)
	world.cast_ability(blue_id, 1)
	world.award_xp(red_id, 100)
	world.kill_champion(red_id)
	world.set_team_resources(Stage4ProofWorld.TEAM_RED, 1000)
	world.revive_champion(Stage4ProofWorld.TEAM_RED)
	world.advance(7)
	return world


func _new_world() -> Stage4ProofWorld:
	var world: Stage4ProofWorld = Stage4ProofWorld.new()
	var error: String = world.setup_default(_abilities, _progression, _status, _revival)
	if error != "":
		_check("world_setup", false, error)
	return world


func _definitions_ready() -> bool:
	return _abilities.size() == 5 and not _progression.is_empty() and not _status.is_empty() and not _revival.is_empty()


func _ability_modes() -> Array[String]:
	var modes: Array[String] = []
	for definition: Dictionary in _abilities:
		modes.append(String(definition.get("target_mode", "")))
	modes.sort()
	return modes


func _member_health(squad: Dictionary, member_id: int) -> int:
	for member_value: Variant in Array(squad.get("members", [])):
		var member: Dictionary = member_value
		if int(member.get("id", 0)) == member_id:
			return int(member.get("health", -1))
	return -1


func _reason(result: Dictionary) -> String:
	return String(result.get("reason", ""))
