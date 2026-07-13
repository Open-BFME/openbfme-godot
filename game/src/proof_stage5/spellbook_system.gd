class_name Stage5SpellbookSystem
extends RefCounted
## Data-driven power-point progression, unlock rules, and per-team cooldowns.

const TARGET_MODES: Array[String] = [
	"friendly_entity",
	"hostile_entity",
	"hostile_building",
	"position",
	"global",
]
const EFFECT_TYPES: Array[String] = [
	"heal_entity",
	"damage_entity",
	"damage_building",
	"area_damage",
	"area_heal",
	"weather",
	"global_heal",
]

var rules_version: int = 0
var team_count: int = 0
var starting_power_points: int = 0
var experience_per_point: int = 0
var unit_defeat_power_points: int = 0
var building_defeat_power_points: int = 0
var maximum_queued_commands: int = 0
var weather_policy: String = ""
var tier_minimum_spent: Dictionary = {}
var definitions: Array[Dictionary] = []
var definitions_by_code: Dictionary = {}


func configure(document: Dictionary) -> String:
	if String(document.get("schema", "")) != "openbfme.powers":
		return "powers_schema"
	if int(document.get("schemaVersion", -1)) != 0:
		return "powers_schema_version"
	rules_version = int(document.get("rulesVersion", 0))
	var rules: Dictionary = document.get("rules", {})
	team_count = int(rules.get("teamCount", 0))
	starting_power_points = int(rules.get("startingPowerPoints", -1))
	experience_per_point = int(rules.get("experiencePerPoint", 0))
	unit_defeat_power_points = int(rules.get("unitDefeatPowerPoints", 0))
	building_defeat_power_points = int(rules.get("buildingDefeatPowerPoints", 0))
	maximum_queued_commands = int(rules.get("maximumQueuedCommands", 0))
	weather_policy = String(rules.get("weatherPolicy", ""))
	if rules_version <= 0 or team_count != 2 or starting_power_points < 0 or experience_per_point <= 0 or unit_defeat_power_points <= 0 or building_defeat_power_points <= 0 or maximum_queued_commands <= 0 or weather_policy != "replace":
		return "powers_rules"

	tier_minimum_spent.clear()
	var last_tier: int = 0
	var last_minimum: int = -1
	for raw_tier: Variant in document.get("tiers", []):
		if not raw_tier is Dictionary:
			return "power_tier_shape"
		var tier: Dictionary = raw_tier
		var tier_code: int = int(tier.get("tier", 0))
		var minimum_spent: int = int(tier.get("minimumSpent", -1))
		if tier_code != last_tier + 1 or minimum_spent < 0 or minimum_spent < last_minimum:
			return "power_tier_order"
		tier_minimum_spent[tier_code] = minimum_spent
		last_tier = tier_code
		last_minimum = minimum_spent
	if tier_minimum_spent.is_empty() or int(tier_minimum_spent.get(1, -1)) != 0:
		return "power_tiers"

	definitions.clear()
	definitions_by_code.clear()
	for raw_power: Variant in document.get("powers", []):
		if not raw_power is Dictionary:
			return "power_shape"
		var power: Dictionary = (raw_power as Dictionary).duplicate(true)
		var code: int = int(power.get("code", 0))
		var tier_code: int = int(power.get("tier", 0))
		var target_mode: String = String(power.get("targetMode", ""))
		if code <= 0 or definitions_by_code.has(code) or String(power.get("id", "")) == "" or String(power.get("displayName", "")) == "":
			return "power_identity"
		if not tier_minimum_spent.has(tier_code) or int(power.get("pointCost", 0)) <= 0 or int(power.get("cooldownTicks", 0)) <= 0 or not TARGET_MODES.has(target_mode):
			return "power_rules_%d" % code
		power["minimumSpent"] = int(tier_minimum_spent[tier_code])
		var prerequisites: Array[int] = []
		for raw_prerequisite: Variant in power.get("prerequisites", []):
			var prerequisite: int = int(raw_prerequisite)
			if prerequisite <= 0 or prerequisite == code or prerequisites.has(prerequisite):
				return "power_prerequisite_%d" % code
			prerequisites.append(prerequisite)
		prerequisites.sort()
		power["prerequisites"] = prerequisites
		var effects: Array = power.get("effects", [])
		if effects.is_empty():
			return "power_effects_%d" % code
		for raw_effect: Variant in effects:
			if not raw_effect is Dictionary or _validate_effect(raw_effect as Dictionary, target_mode) != "":
				return "power_effect_%d" % code
		definitions.append(power)
		definitions_by_code[code] = power
	definitions.sort_custom(func(left: Dictionary, right: Dictionary) -> bool: return int(left["code"]) < int(right["code"]))
	if definitions.is_empty():
		return "powers_empty"
	for definition: Dictionary in definitions:
		for prerequisite: int in definition["prerequisites"]:
			if not definitions_by_code.has(prerequisite):
				return "power_prerequisite_missing_%d" % int(definition["code"])
	if _has_prerequisite_cycle():
		return "power_prerequisite_cycle"
	return ""


func definition(power_code: int) -> Dictionary:
	return definitions_by_code.get(power_code, {})


func new_team_state() -> Dictionary:
	return {
		"available_points": starting_power_points,
		"spent_points": 0,
		"earned_points": 0,
		"experience_remainder": 0,
		"unlocked": [],
		"cooldowns": {},
	}


func award_experience(state: Dictionary, amount: int) -> Dictionary:
	if amount <= 0:
		return {"ok": false, "reason": "invalid_experience", "points_gained": 0}
	var total: int = int(state.get("experience_remainder", 0)) + amount
	var gained: int = total / experience_per_point
	state["experience_remainder"] = total % experience_per_point
	state["available_points"] = int(state.get("available_points", 0)) + gained
	state["earned_points"] = int(state.get("earned_points", 0)) + gained
	return {
		"ok": true,
		"reason": "",
		"points_gained": gained,
		"available_points": int(state["available_points"]),
		"experience_remainder": int(state["experience_remainder"]),
	}


func grant_points(state: Dictionary, amount: int) -> Dictionary:
	if amount <= 0:
		return {"ok": false, "reason": "invalid_points", "points_gained": 0}
	state["available_points"] = int(state.get("available_points", 0)) + amount
	state["earned_points"] = int(state.get("earned_points", 0)) + amount
	return {"ok": true, "reason": "", "points_gained": amount, "available_points": int(state["available_points"])}


func unlock(state: Dictionary, power_code: int) -> Dictionary:
	var power: Dictionary = definition(power_code)
	if power.is_empty():
		return {"ok": false, "reason": "unknown_power"}
	var unlocked: Array = state.get("unlocked", [])
	if unlocked.has(power_code):
		return {"ok": false, "reason": "already_unlocked"}
	for prerequisite: int in power["prerequisites"]:
		if not unlocked.has(prerequisite):
			return {"ok": false, "reason": "prerequisite_locked", "prerequisite": prerequisite}
	var minimum_spent: int = int(tier_minimum_spent[int(power["tier"])])
	if int(state.get("spent_points", 0)) < minimum_spent:
		return {"ok": false, "reason": "tier_spend_locked", "minimum_spent": minimum_spent}
	var cost: int = int(power["pointCost"])
	if int(state.get("available_points", 0)) < cost:
		return {"ok": false, "reason": "insufficient_points", "cost": cost}
	state["available_points"] = int(state["available_points"]) - cost
	state["spent_points"] = int(state["spent_points"]) + cost
	unlocked.append(power_code)
	unlocked.sort()
	state["unlocked"] = unlocked
	return {
		"ok": true,
		"reason": "",
		"power_code": power_code,
		"available_points": int(state["available_points"]),
		"spent_points": int(state["spent_points"]),
	}


func is_unlocked(state: Dictionary, power_code: int) -> bool:
	return Array(state.get("unlocked", [])).has(power_code)


func cooldown_remaining(state: Dictionary, power_code: int, tick_index: int) -> int:
	return maxi(0, int(Dictionary(state.get("cooldowns", {})).get(power_code, 0)) - tick_index)


func start_cooldown(state: Dictionary, power_code: int, tick_index: int) -> int:
	var power: Dictionary = definition(power_code)
	var ready_tick: int = tick_index + int(power.get("cooldownTicks", 0))
	var cooldowns: Dictionary = state.get("cooldowns", {})
	cooldowns[power_code] = ready_tick
	state["cooldowns"] = cooldowns
	return ready_tick


func validate_team_state(state: Dictionary) -> String:
	var available: int = int(state.get("available_points", -1))
	var spent: int = int(state.get("spent_points", -1))
	var earned: int = int(state.get("earned_points", -1))
	var remainder: int = int(state.get("experience_remainder", -1))
	if available < 0 or spent < 0 or earned < 0 or remainder < 0 or remainder >= experience_per_point:
		return "spellbook_values"
	if available + spent != starting_power_points + earned:
		return "spellbook_accounting"
	var expected_spent: int = 0
	var prior_code: int = 0
	var unlocked_codes: Array = state.get("unlocked", [])
	for raw_code: Variant in unlocked_codes:
		var code: int = int(raw_code)
		if code <= prior_code or not definitions_by_code.has(code):
			return "spellbook_unlocks"
		var power: Dictionary = definition(code)
		for prerequisite: int in power["prerequisites"]:
			if not unlocked_codes.has(prerequisite):
				return "spellbook_prerequisite"
		if spent < int(power.get("minimumSpent", 0)):
			return "spellbook_tier"
		expected_spent += int(power["pointCost"])
		prior_code = code
	if expected_spent != spent:
		return "spellbook_spent"
	for raw_code: Variant in Dictionary(state.get("cooldowns", {})).keys():
		if not definitions_by_code.has(int(raw_code)) or not unlocked_codes.has(int(raw_code)) or int(Dictionary(state["cooldowns"])[raw_code]) < 0:
			return "spellbook_cooldowns"
	return ""


func _validate_effect(effect: Dictionary, target_mode: String) -> String:
	var effect_type: String = String(effect.get("type", ""))
	if not EFFECT_TYPES.has(effect_type):
		return "effect_type"
	match effect_type:
		"heal_entity":
			if target_mode != "friendly_entity" or int(effect.get("amount", 0)) <= 0:
				return "heal_entity"
		"damage_entity":
			if target_mode != "hostile_entity" or int(effect.get("amount", 0)) <= 0:
				return "damage_entity"
		"damage_building":
			if target_mode != "hostile_building" or int(effect.get("amount", 0)) <= 0:
				return "damage_building"
		"area_damage":
			if target_mode != "position" or int(effect.get("amount", 0)) <= 0 or int(effect.get("radiusCells", 0)) <= 0:
				return "area_damage"
		"area_heal":
			if target_mode != "position" or int(effect.get("amount", 0)) <= 0 or int(effect.get("buildingAmount", 0)) <= 0 or int(effect.get("radiusCells", 0)) <= 0:
				return "area_heal"
		"weather":
			if target_mode != "global" or String(effect.get("weatherCode", "")) == "" or int(effect.get("durationTicks", 0)) <= 0 or int(effect.get("unitDamagePerTick", -1)) < 0 or int(effect.get("buildingDamagePerTick", -1)) < 0:
				return "weather"
		"global_heal":
			if target_mode != "global" or int(effect.get("amount", 0)) <= 0 or int(effect.get("buildingAmount", 0)) <= 0:
				return "global_heal"
	return ""


func _has_prerequisite_cycle() -> bool:
	var visiting: Dictionary = {}
	var visited: Dictionary = {}
	for definition_value: Dictionary in definitions:
		if _visit_for_cycle(int(definition_value["code"]), visiting, visited):
			return true
	return false


func _visit_for_cycle(code: int, visiting: Dictionary, visited: Dictionary) -> bool:
	if visited.has(code):
		return false
	if visiting.has(code):
		return true
	visiting[code] = true
	for prerequisite: int in definition(code)["prerequisites"]:
		if _visit_for_cycle(prerequisite, visiting, visited):
			return true
	visiting.erase(code)
	visited[code] = true
	return false
