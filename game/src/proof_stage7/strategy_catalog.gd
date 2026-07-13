class_name Stage7StrategyCatalog
extends RefCounted
## Strict schema and deterministic indexes for finite-resource AI strategies.

const ROOT_KEYS: Array[String] = ["difficulties", "plans", "rulesVersion", "scenario", "schema", "schemaVersion"]
const SCENARIO_KEYS: Array[String] = ["attackIntervalTicks", "baseAttackDamage", "enemyFortressHealth", "finiteResourceAmount", "harvestAmount", "harvestIntervalTicks", "maximumProofTicks", "startingArmy", "startingResources"]
const DIFFICULTY_KEYS: Array[String] = ["attackPermille", "displayName", "id", "incomePermille", "thinkIntervalTicks"]
const PLAN_KEYS: Array[String] = ["displayName", "id", "steps"]
const JOB_STEP_KEYS: Array[String] = ["action", "cost", "durationTicks", "objectId"]
const ATTACK_STEP_KEYS: Array[String] = ["action", "minimumArmy"]

var scenario: Dictionary = {}
var difficulties: Dictionary = {}
var plans: Dictionary = {}


func configure(document: Dictionary) -> String:
	scenario.clear()
	difficulties.clear()
	plans.clear()
	var error := _require_exact_keys(document, ROOT_KEYS, "root")
	if error != "":
		return error
	if String(document.get("schema", "")) != "openbfme.ai-strategies" or int(document.get("schemaVersion", -1)) != 0 or int(document.get("rulesVersion", -1)) != 1:
		return "unsupported AI strategy schema"
	if typeof(document.get("scenario")) != TYPE_DICTIONARY or typeof(document.get("difficulties")) != TYPE_ARRAY or typeof(document.get("plans")) != TYPE_ARRAY:
		return "AI strategy collections have invalid types"
	error = _parse_scenario(document["scenario"])
	if error != "":
		return error
	error = _parse_difficulties(Array(document["difficulties"]))
	if error != "":
		return error
	error = _parse_plans(Array(document["plans"]))
	if error != "":
		return error
	if difficulties.size() < 3:
		return "easy, normal, and hard difficulty evidence is required"
	return ""


func difficulty_ids() -> Array[String]:
	return _sorted_keys(difficulties)


func plan_ids() -> Array[String]:
	return _sorted_keys(plans)


func difficulty(difficulty_id: String) -> Dictionary:
	return Dictionary(difficulties.get(difficulty_id, {})).duplicate(true)


func plan(plan_id: String) -> Dictionary:
	return Dictionary(plans.get(plan_id, {})).duplicate(true)


func definitions_snapshot() -> Dictionary:
	var difficulty_rows: Array[Dictionary] = []
	for difficulty_id: String in difficulty_ids():
		difficulty_rows.append(difficulty(difficulty_id))
	var plan_rows: Array[Dictionary] = []
	for plan_id: String in plan_ids():
		plan_rows.append(plan(plan_id))
	return {"scenario": scenario.duplicate(true), "difficulties": difficulty_rows, "plans": plan_rows}


func _parse_scenario(row: Dictionary) -> String:
	var error := _require_exact_keys(row, SCENARIO_KEYS, "scenario")
	if error != "":
		return error
	for key: String in SCENARIO_KEYS:
		if not _is_integer(row.get(key)) or int(row[key]) <= 0:
			return "scenario.%s must be a positive integer" % key
	if int(row["harvestAmount"]) > int(row["finiteResourceAmount"]):
		return "harvest amount exceeds the finite deposit"
	scenario = row.duplicate(true)
	return ""


func _parse_difficulties(rows: Array) -> String:
	for index: int in range(rows.size()):
		if typeof(rows[index]) != TYPE_DICTIONARY:
			return "difficulties[%d] must be an object" % index
		var row: Dictionary = rows[index]
		var error := _require_exact_keys(row, DIFFICULTY_KEYS, "difficulty")
		if error != "":
			return error
		var difficulty_id := String(row.get("id", ""))
		if difficulty_id.is_empty() or difficulties.has(difficulty_id) or String(row.get("displayName", "")).is_empty():
			return "difficulty identity is invalid"
		for field: String in ["thinkIntervalTicks", "incomePermille", "attackPermille"]:
			if not _is_integer(row.get(field)) or int(row[field]) <= 0:
				return "difficulty %s field %s must be positive" % [difficulty_id, field]
		difficulties[difficulty_id] = row.duplicate(true)
	for required: String in ["easy", "normal", "hard"]:
		if not difficulties.has(required):
			return "missing %s difficulty" % required
	return ""


func _parse_plans(rows: Array) -> String:
	for index: int in range(rows.size()):
		if typeof(rows[index]) != TYPE_DICTIONARY:
			return "plans[%d] must be an object" % index
		var row: Dictionary = rows[index]
		var error := _require_exact_keys(row, PLAN_KEYS, "plan")
		if error != "":
			return error
		var plan_id := String(row.get("id", ""))
		if plan_id.is_empty() or plans.has(plan_id) or String(row.get("displayName", "")).is_empty() or typeof(row.get("steps")) != TYPE_ARRAY or Array(row["steps"]).is_empty():
			return "plan identity or steps are invalid"
		var normalized_steps: Array[Dictionary] = []
		var attack_seen: bool = false
		for step_index: int in range(Array(row["steps"]).size()):
			var raw_step: Variant = Array(row["steps"])[step_index]
			if typeof(raw_step) != TYPE_DICTIONARY:
				return "plan step must be an object"
			var step: Dictionary = raw_step
			var action := String(step.get("action", ""))
			if action == "build" or action == "train":
				error = _require_exact_keys(step, JOB_STEP_KEYS, "job step")
				if error != "" or attack_seen or String(step.get("objectId", "")).is_empty():
					return "build/train step is invalid or follows attack"
				for field: String in ["cost", "durationTicks"]:
					if not _is_integer(step.get(field)) or int(step[field]) <= 0:
						return "job step %s must be positive" % field
			elif action == "attack":
				error = _require_exact_keys(step, ATTACK_STEP_KEYS, "attack step")
				if error != "" or attack_seen or not _is_integer(step.get("minimumArmy")) or int(step["minimumArmy"]) <= 0:
					return "attack step is invalid"
				attack_seen = true
			else:
				return "unsupported AI plan action"
			normalized_steps.append(step.duplicate(true))
		if not attack_seen or String(normalized_steps[-1]["action"]) != "attack":
			return "plan must end in one attack step"
		var normalized := row.duplicate(true)
		normalized["steps"] = normalized_steps
		plans[plan_id] = normalized
	return ""


static func _sorted_keys(dictionary: Dictionary) -> Array[String]:
	var result: Array[String] = []
	for key: Variant in dictionary.keys():
		result.append(String(key))
	result.sort()
	return result


static func _require_exact_keys(value: Dictionary, expected: Array[String], path: String) -> String:
	if value.size() != expected.size():
		return "%s must contain exactly: %s" % [path, ",".join(expected)]
	for key: String in expected:
		if not value.has(key):
			return "%s is missing %s" % [path, key]
	return ""


static func _is_integer(value: Variant) -> bool:
	return typeof(value) == TYPE_INT or (typeof(value) == TYPE_FLOAT and float(value) == floorf(float(value)))
