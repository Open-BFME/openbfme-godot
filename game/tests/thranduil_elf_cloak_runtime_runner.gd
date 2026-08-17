extends SceneTree
## ability:Command_SpecialAbilityElfCloakThranduil (SpecialAbilityElfCloak)
##
## Thranduil's Elven Cloak authors a bare ToggleHiddenSpecialAbilityUpdate: no
## EffectDuration anywhere in the module. The converter demanded one and left
## the row unimplemented, so an Elven skirmish hero shipped a cloak button with
## no effect. RotWK authors EffectDuration on exactly one ToggleHidden module in
## the whole game (Wormtongue's), which this runner re-proves against the oracle
## before driving the untimed toggle through the sim.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const OBJECT_ROOT := "res://../workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini/object/"
const ORACLE_ROOT := "res://../workspace/retail-work/editions/rotwk/cache/effective-assets/data/ini/"
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
		push_error("THRANDUIL_ELF_CLOAK_FAIL watchdog: the runner aborted before reporting")
		_report()
	return false


func _run() -> void:
	var thranduil := _read(OBJECT_ROOT + "goodfaction/units/elven/thranduil.ini")
	var wormtongue := _read(OBJECT_ROOT + "evilfaction/units/isengard/wormtongue.ini")
	var specialpower := _read(ORACLE_ROOT + "specialpower.ini")
	if thranduil.is_empty() or wormtongue.is_empty() or specialpower.is_empty():
		failed += 1
		push_error("THRANDUIL_ELF_CLOAK_FAIL oracle ini files are unreadable")
		_report()
		return

	# --- the authored shape the converter used to refuse --------------------
	var cloak := _module(thranduil, "ToggleHiddenSpecialAbilityUpdate", "SpecialAbilityElfCloak")
	_check("oracle_authors_thranduil_toggle_hidden", not cloak.is_empty())
	_check("oracle_cloak_authors_no_effect_duration", _field(cloak, "EffectDuration") == "")
	var timed := _module(wormtongue, "ToggleHiddenSpecialAbilityUpdate", "SpecialAbilityWormtongueEscape")
	_check("oracle_wormtongue_is_the_timed_counterexample", _field(timed, "EffectDuration") == "15000")

	var forbidden := _cloak_forbidden_conditions(thranduil)
	_check("oracle_cloak_breaks_on_firing", forbidden.has("FIRING_ANY"))

	var power := _block(specialpower, "SpecialPower SpecialAbilityElfCloak")
	var reload_ms := _field(power, "ReloadTime").to_float()
	_check("oracle_power_authors_a_recharge", reload_ms > 0.0)

	# --- converter shape -> real adapter -> sim rule ------------------------
	var rules := Adapter.ability_rules(_document(forbidden, reload_ms))
	if rules.size() != 1:
		failed += 1
		push_error("THRANDUIL_ELF_CLOAK_FAIL the adapter refused the untimed cloak row")
		_report()
		return
	var rule := rules[0]
	_check("adapter_ability_is_castable", bool(rule.get("castable", false)))
	_check("adapter_cooldown_matches_the_authored_recharge", int(rule.get("cooldown_ticks", 0)) == roundi(reload_ms / (Sim.TICK_SECONDS * 1000.0)))
	var scaled := (rule.get("effect", {}) as Dictionary)
	_check("scaled_rule_carries_no_timer", int(scaled.get("duration_ticks", -1)) == 0 or not scaled.has("duration_ticks"))

	# --- the toggle the sim actually runs -----------------------------------
	var sim: RetailSliceSim = _fixture(rule)
	var hero := sim.entities[1] as Dictionary
	var cast: Dictionary = sim.cast_ability(1, "Command_SpecialAbilityElfCloakThranduil", Vector2.ZERO, 0)
	_check("cloak_engages", bool(cast.get("ok", false)) and bool(cast.get("engaged", false)))
	_check("hero_is_stealthed", sim._stealth_active(hero))
	sim.advance(2000)
	_check("cloak_outlives_any_authored_timer", sim._stealth_active(hero))

	# Recasting the toggle drops it, which is the only clock retail gives it.
	(sim.entities[1] as Dictionary)["ability_states"] = {}
	sim._attach_hero_ability_state(hero)
	var recast: Dictionary = sim.cast_ability(1, "Command_SpecialAbilityElfCloakThranduil", Vector2.ZERO, 0)
	_check("recast_drops_the_cloak", bool(recast.get("ok", false)) and not bool(recast.get("engaged", true)))
	_check("hero_is_visible_again", not sim._stealth_active(hero))

	# The authored ForbiddenCondition is the other way out.
	var broken: RetailSliceSim = _fixture(rule)
	var broken_hero := broken.entities[1] as Dictionary
	broken.cast_ability(1, "Command_SpecialAbilityElfCloakThranduil", Vector2.ZERO, 0)
	_check("cloak_engages_before_the_break", broken._stealth_active(broken_hero))
	broken._break_stealth(broken_hero, "FIRING_ANY")
	_check("authored_forbidden_condition_breaks_the_cloak", not broken._stealth_active(broken_hero))

	var unrelated: RetailSliceSim = _fixture(rule)
	var unrelated_hero := unrelated.entities[1] as Dictionary
	unrelated.cast_ability(1, "Command_SpecialAbilityElfCloakThranduil", Vector2.ZERO, 0)
	unrelated._break_stealth(unrelated_hero, "TAKING_DAMAGE")
	_check("an_unauthored_condition_does_not_break_it", unrelated._stealth_active(unrelated_hero))
	_report()


# ---------------------------------------------------------------------------


func _document(forbidden: Array, reload_ms: float) -> Dictionary:
	return {
		"registration": {
			"abilities": [{
				"id": "Command_SpecialAbilityElfCloakThranduil",
				"slot": 4,
				"targeting": "self",
				"specialPowerId": "SpecialAbilityElfCloak",
				"cooldownMs": reload_ms,
				"button": {
					"commandId": "Command_SpecialAbilityElfCloakThranduil",
					"iconIds": ["HSElvenCloak"],
					"labelIds": ["CONTROLBAR:ElvenCloak"],
					"options": [],
				},
				"effect": {
					"kind": "stealth-toggle",
					"untimed": true,
					"forbiddenConditions": forbidden,
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
	sim._add_battalion(1, 0, Vector2.ZERO, "Fixture", Sim.SOLDIER_OBJECT_ID, Sim.SOLDIER_HORDE_ID, 0, _unit_rule())
	sim._attach_hero_ability_state(sim.entities[1] as Dictionary)
	return sim


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


func _module(text: String, kind: String, power_id: String) -> String:
	## The body of the first `Behavior = <kind>` block bound to <power_id>.
	var body := ""
	var collecting := false
	var bound := false
	for line in text.split("\n"):
		var trimmed := _normalized(_strip_comment(String(line)))
		if trimmed.begins_with("Behavior =") or trimmed.begins_with("Body =") or trimmed.begins_with("Draw ="):
			if collecting and bound:
				return body
			collecting = trimmed.begins_with("Behavior = " + kind + " ")
			bound = false
			body = ""
			continue
		if not collecting:
			continue
		if trimmed == "End":
			if bound:
				return body
			collecting = false
			continue
		if trimmed.begins_with("SpecialPowerTemplate =") and trimmed.ends_with(power_id):
			bound = true
		body += trimmed + "\n"
	return body if bound else ""


func _cloak_forbidden_conditions(text: String) -> Array:
	## The ForbiddenConditions of the object's first InvisibilityNugget - the
	## nugget retail's toggle binds to.
	for line in text.split("\n"):
		var trimmed := _normalized(_strip_comment(String(line)))
		if trimmed.begins_with("ForbiddenConditions ="):
			var tokens: Array = []
			for token in trimmed.substr("ForbiddenConditions =".length()).strip_edges().split(" ", false):
				tokens.append(String(token))
			return tokens
	return []


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


func _field(block: String, key: String) -> String:
	for line in block.split("\n"):
		var trimmed := _normalized(String(line))
		if trimmed.begins_with(key + " ="):
			return _strip_comment(trimmed.substr((key + " =").length()).strip_edges())
	return ""


func _strip_comment(value: String) -> String:
	var text := value
	for marker in [";", "//"]:
		var index := text.find(marker)
		if index >= 0:
			text = text.substr(0, index)
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
		push_error("THRANDUIL_ELF_CLOAK_FAIL %s" % label)


func _report() -> void:
	if _reported:
		return
	_reported = true
	var ran := passed + failed
	if ran < 15:
		failed += 1
		push_error("THRANDUIL_ELF_CLOAK_FAIL liveness ran=%d expected>=15" % ran)
	print("THRANDUIL_ELF_CLOAK_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
