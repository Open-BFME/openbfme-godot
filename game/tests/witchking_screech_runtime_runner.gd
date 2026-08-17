extends SceneTree
## ability:Command_SpecialAbilityScreechWitchKing (SpecialAbilityScreech)
##
## MordorWitchKingOnFellBeast binds SpecialAbilityScreech twice - the fell-beast
## module tag and its own, differing only in TriggerSound - and both author
## EffectRange = 180. The converter used to refuse the duplicate as an
## ambiguity, so Mordor's flagship RotWK hero carried a Screech button with no
## effect. This runner reads both authored ranges plus the TERROR EmotionNugget
## out of the RotWK oracle and proves the sim spends the ability at exactly
## those magnitudes.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const ORACLE_ROOT := "res://../workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini/"
const FELLBEAST := "res://../workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini/object/evilfaction/units/mordor/fellbeast.ini"
const SOURCE_SCALE := 0.1
const WATCHDOG_FRAMES := 1800

var passed := 0
var failed := 0
var _frames := 0
var _reported := false


func _initialize() -> void:
	call_deferred("_run")


func _process(_delta: float) -> bool:
	_frames += 1
	if _frames > WATCHDOG_FRAMES and not _reported:
		failed += 1
		push_error("WITCHKING_SCREECH_FAIL watchdog: the runner aborted before reporting")
		_report()
	return false


func _run() -> void:
	var fellbeast := _read(FELLBEAST)
	var specialpower := _read(ORACLE_ROOT + "specialpower.ini")
	var emotions := _read(ORACLE_ROOT + "emotions.ini")
	if fellbeast.is_empty() or specialpower.is_empty() or emotions.is_empty():
		failed += 1
		push_error("WITCHKING_SCREECH_FAIL oracle ini files are unreadable")
		_report()
		return

	# --- the duplicate the converter used to refuse ------------------------
	var witchking := _block(fellbeast, "ChildObject MordorWitchKingOnFellBeast MordorFellBeastInterface")
	var screech_ranges := _bound_effect_ranges(witchking, "SpecialAbilityScreech")
	_check("oracle_authors_screech_twice", screech_ranges.size() == 2)
	_check("both_screech_modules_author_180", screech_ranges.size() == 2 and screech_ranges[0] == 180.0 and screech_ranges[1] == 180.0)

	var power := _block(specialpower, "SpecialPower SpecialAbilityScreech")
	_check("oracle_power_enum_special_screech", _field(power, "Enum") == "SPECIAL_SCREECH")
	var reload_ms := _field(power, "ReloadTime").split(";")[0].strip_edges().to_float()
	_check("oracle_power_reload_180000", is_equal_approx(reload_ms, 180000.0))

	var terror := _block(emotions, "EmotionNugget Terror_Base")
	_check("oracle_terror_type", _field(terror, "Type") == "TERROR")
	var duration_ms := _field(terror, "Duration").split(";")[0].strip_edges().to_float()
	_check("oracle_terror_duration_10000", is_equal_approx(duration_ms, 10000.0))
	_check("oracle_terror_prevents_player_commands", _field(terror, "PreventPlayerCommands") == "Yes")

	var radius: float = float(screech_ranges[0]) if not screech_ranges.is_empty() else 0.0
	var cooldown_ticks := roundi(reload_ms / (Sim.TICK_SECONDS * 1000.0))
	var duration_ticks := roundi(duration_ms / (Sim.TICK_SECONDS * 1000.0))

	# --- converter shape -> real adapter -> sim rule ------------------------
	var rules := Adapter.ability_rules(_document(radius, duration_ms, reload_ms))
	if rules.size() != 1:
		failed += 1
		push_error("WITCHKING_SCREECH_FAIL the adapter refused the compiled terror row")
		_report()
		return
	var rule := rules[0]
	_check("adapter_ability_is_castable", bool(rule.get("castable", false)))
	_check("adapter_cooldown_is_1800_ticks", int(rule.get("cooldown_ticks", 0)) == cooldown_ticks)
	_check("adapter_keeps_self_targeting", String(rule.get("targeting", "")) == "self")

	# --- magnitudes the sim actually spends ---------------------------------
	var scaled_radius := radius * SOURCE_SCALE
	var sim: RetailSliceSim = _fixture(rule)
	var inside: Dictionary = _spawn(sim, 2, 1, Vector2(scaled_radius - 0.5, 0.0))
	var outside: Dictionary = _spawn(sim, 3, 1, Vector2(scaled_radius + 0.5, 0.0))
	var cast: Dictionary = sim.cast_ability(1, "Command_SpecialAbilityScreechWitchKing", Vector2.ZERO, 0)
	_check("screech_casts", bool(cast.get("ok", false)))
	_check("screech_affects_only_the_enemy_inside_180", int(cast.get("affected", 0)) == 1)
	var key := "fear:Command_SpecialAbilityScreechWitchKing"
	_check("victim_inside_radius_is_terrified", _table(inside).has(key))
	_check("victim_outside_radius_untouched", not _table(outside).has(key))
	_check("terror_zeroes_outgoing_damage", is_equal_approx(sim._ability_outgoing_multiplier(inside), 0.0))
	_check("recharge_blocks_second_cast", String(sim.cast_ability(1, "Command_SpecialAbilityScreechWitchKing", Vector2.ZERO, 0).get("reason", "")) == "cooldown-active")

	var cast_tick: int = sim.tick_index
	sim.advance(duration_ticks - 1)
	_check("terror_holds_through_the_authored_duration", sim.tick_index == cast_tick + duration_ticks - 1 and _table(inside).has(key))
	sim.advance(1)
	_check("terror_expires_on_the_authored_tick", not _table(inside).has(key))
	_report()


# ---------------------------------------------------------------------------


func _document(radius: float, duration_ms: float, reload_ms: float) -> Dictionary:
	## The converter-emitted ability row for Command_SpecialAbilityScreechWitchKing,
	## with the magnitudes read out of the oracle above rather than restated here.
	return {
		"registration": {
			"abilities": [{
				"id": "Command_SpecialAbilityScreechWitchKing",
				"slot": 4,
				"targeting": "self",
				"specialPowerId": "SpecialAbilityScreech",
				"enum": "SPECIAL_SCREECH",
				"cooldownMs": reload_ms,
				"button": {
					"commandId": "Command_SpecialAbilityScreechWitchKing",
					"iconIds": ["HSNazgulScreech"],
					"labelIds": ["CONTROLBAR:Screech"],
					"options": [],
				},
				"effect": {
					"kind": "terror",
					"radius": radius,
					"durationMs": duration_ms,
					"emotionNuggetId": "Terror_Base",
					"engineEnum": "SPECIAL_SCREECH",
					"modifiers": [{
						"kind": "DAMAGE_MULT",
						"value": 0.0,
						"application": "multiplicative",
						"semantic": "SPECIAL_SCREECH TERROR victims cannot fight while RUN_AWAY_PANIC / PreventPlayerCommands holds",
					}],
				},
				"implementation": {"status": "implemented", "reason": "", "limitations": []},
				"levelGate": {"requiredLevel": 1, "upgradeIds": []},
			}],
			"stringBindings": {},
		},
	}


func _fixture(rule: Dictionary) -> RetailSliceSim:
	var unit_rules := {}
	for object_id in [Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, Sim.ARCHER_OBJECT_ID, Sim.TOWER_GUARD_OBJECT_ID, Sim.KNIGHT_OBJECT_ID]:
		unit_rules[object_id] = _unit_rule()
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {"unit_rules": unit_rules, "source_map_transform_scale": SOURCE_SCALE})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	sim._unit_ability_rules[Sim.SOLDIER_HORDE_ID] = sim._scaled_ability_rules([rule], SOURCE_SCALE)
	_spawn(sim, 1, 0, Vector2.ZERO)
	return sim


func _spawn(sim: RetailSliceSim, id: int, team: int, point: Vector2) -> Dictionary:
	sim._add_battalion(id, team, point, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _unit_rule())
	sim._attach_hero_ability_state(sim.entities[id] as Dictionary)
	return sim.entities[id] as Dictionary


func _table(row: Dictionary) -> Dictionary:
	return row.get("timed_modifiers", {}) as Dictionary


func _unit_rule() -> Dictionary:
	return {
		"horde_id": Sim.SOLDIER_HORDE_ID, "category": "hero",
		"speed": 1.0, "speed_source": 10.0,
		"acceleration": 1.0, "acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0, "braking_source": 10.0,
		"attack_range": 1.0, "attack_range_source": 1.0,
		"minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0,
		"vision_range": 10.0, "vision_range_source": 10.0,
		"delay_between_shots_ms": 1000.0, "pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0, "attack_period_ticks": 10,
		"pre_attack_ticks": 0, "firing_duration_ticks": 0,
		"member_damage": 10, "member_health": 400, "member_count": 1,
		"formation_positions": [Vector3.ZERO], "provenance": {},
	}


func _read(path: String) -> String:
	return FileAccess.get_file_as_string(ProjectSettings.globalize_path(path))


func _block(text: String, header: String) -> String:
	var collecting := false
	var body := ""
	for line in text.split("\n"):
		var raw := String(line).replace("\r", "")
		var trimmed := raw.strip_edges()
		if not collecting:
			if _normalized(_strip_comment(trimmed)) == _normalized(header):
				collecting = true
				body = ""
			continue
		if raw.strip_edges(false, true) == "End":
			return body
		body += trimmed + "\n"
	return body


func _bound_effect_ranges(block: String, power_id: String) -> Array:
	## Every EffectRange authored by a SpecialAbilityUpdate bound to this power,
	## in source order - the duplicate module tags retail actually authors.
	var ranges: Array = []
	var in_module := false
	var bound := false
	var pending := -1.0
	for line in block.split("\n"):
		var trimmed := _normalized(_strip_comment(String(line)))
		if trimmed.begins_with("Behavior = "):
			if in_module and bound and pending > 0.0:
				ranges.append(pending)
			in_module = trimmed.begins_with("Behavior = SpecialAbilityUpdate ")
			bound = false
			pending = -1.0
			continue
		if not in_module:
			continue
		if trimmed.begins_with("SpecialPowerTemplate =") and trimmed.ends_with(power_id):
			bound = true
		elif trimmed.begins_with("EffectRange ="):
			pending = trimmed.substr("EffectRange =".length()).strip_edges().to_float()
	if in_module and bound and pending > 0.0:
		ranges.append(pending)
	return ranges


func _field(block: String, key: String) -> String:
	for line in block.split("\n"):
		var trimmed := _normalized(String(line))
		if trimmed.begins_with(key + " ="):
			return _strip_comment(trimmed.substr((key + " =").length()).strip_edges())
	return ""


func _strip_comment(value: String) -> String:
	var text := value
	var marker := text.find(";")
	if marker >= 0:
		text = text.substr(0, marker)
	return text.strip_edges()


func _normalized(value: String) -> String:
	var text := value.replace("\t", " ")
	while text.contains("  "):
		text = text.replace("  ", " ")
	return text.strip_edges()


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("WITCHKING_SCREECH_FAIL %s" % label)


func _report() -> void:
	if _reported:
		return
	_reported = true
	var ran := passed + failed
	if ran < 18:
		failed += 1
		push_error("WITCHKING_SCREECH_FAIL liveness ran=%d expected>=18" % ran)
	print("WITCHKING_SCREECH_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
