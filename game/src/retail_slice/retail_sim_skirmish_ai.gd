extends RefCounted
## Skirmish-AI interpreter (Q83 phase 2): compiles the raw authored
## `openbfme.skirmish-ai` document (retail aidata.ini + skirmishaidata.ini,
## fact-for-fact with provenance) into typed per-side runtime plans the sim's
## AI can consume. Interpretation lives HERE, at runtime — the importer never
## pre-bakes decisions (owner's raw-fields rule, 2026-08-25).
##
## State stays on the sim (sim.skirmish_ai_*); this module holds logic only.
## configure refuses loudly by name and configures NOTHING on refusal.

var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()

func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)


func configure_skirmish_ai(document: Dictionary) -> Dictionary:
	if document.is_empty():
		return _refuse("skirmish-ai document is empty")
	if String(document.get("schema", "")) != "openbfme.skirmish-ai":
		return _refuse("skirmish-ai schema is not openbfme.skirmish-ai")
	if int(document.get("schemaVersion", -1)) != 0:
		return _refuse("skirmish-ai schemaVersion is not 0")
	for required in ["armies", "combatChains", "difficultyTuning", "census"]:
		if not document.has(required):
			return _refuse("skirmish-ai document is missing '%s'" % required)
	var armies_value: Variant = document.get("armies", {})
	if typeof(armies_value) != TYPE_DICTIONARY or (armies_value as Dictionary).is_empty():
		return _refuse("skirmish-ai armies table is empty")

	var plans_by_side: Dictionary = {}
	for army_name in armies_value:
		var army_value: Variant = (armies_value as Dictionary)[army_name]
		if typeof(army_value) != TYPE_DICTIONARY:
			return _refuse("skirmish-ai army '%s' is not a table" % army_name)
		var plan := _compiled_army_plan(String(army_name), army_value as Dictionary)
		if plan.has("refusal"):
			return _refuse(String(plan["refusal"]))
		var side := String(plan["side"]).to_lower()
		if plans_by_side.has(side):
			return _refuse(
				"skirmish-ai side '%s' has two army definitions (%s, %s)" % [
					side, String((plans_by_side[side] as Dictionary)["army_name"]), army_name,
				]
			)
		plans_by_side[side] = plan

	var difficulty := _compiled_difficulty(document.get("difficultyTuning", {}) as Dictionary)
	if difficulty.has("refusal"):
		return _refuse(String(difficulty["refusal"]))
	var brutal_value: Variant = document.get("brutalDifficultyCheats")
	var brutal: Dictionary = {}
	if typeof(brutal_value) == TYPE_DICTIONARY:
		brutal = _compiled_field_values(brutal_value as Dictionary)

	sim.skirmish_ai_plans_by_side = plans_by_side
	sim.skirmish_ai_difficulty = difficulty
	sim.skirmish_ai_brutal_cheats = brutal
	sim.skirmish_ai_configured = true
	return {"ok": true, "reason": "", "sides": plans_by_side.size()}


func skirmish_ai_plan_for_side(side: String) -> Dictionary:
	if not bool(sim.skirmish_ai_configured):
		return {}
	return sim.skirmish_ai_plans_by_side.get(side.to_lower(), {}) as Dictionary


## ---- production consumption (retail ArmyDefinition phases) -----------------

func authored_ai_queue_choice(team: int) -> Dictionary:
	## Composition-driven production: count the team's living combat units per
	## authored member Unit and answer the member whose current share of the
	## army is furthest below its authored PercentageOfArmyPhaseN for the
	## current phase. Deterministic (document order breaks ties); refuses by
	## name, never guesses.
	if not bool(sim.skirmish_ai_configured):
		return {"ok": false, "reason": "skirmish-ai is not configured"}
	var side_row: Dictionary = sim.team_retail_side(team)
	var side := String(side_row.get("side", ""))
	if side == "":
		return {"ok": false, "reason": String(side_row.get("reason", "team %d has no retail side" % team))}
	var plan := skirmish_ai_plan_for_side(side)
	if plan.is_empty():
		return {"ok": false, "reason": "no ArmyDefinition for side '%s'" % side}
	var phase := current_army_phase(plan)
	var team_rules: Dictionary = sim.unit_production_rules_for_team(team)
	var total_percentage := 0.0
	var candidates: Array = []
	var untrainable: Array = []
	for member_value in plan.get("members", []) as Array:
		var member := member_value as Dictionary
		var percentage := float((member.get("phase_percentages", []) as Array)[phase - 1])
		if percentage <= 0.0:
			continue
		var unit_type := String(sim.trainable_unit_type_for(team, String(member.get("unit", ""))))
		if unit_type == "" or not team_rules.has(unit_type):
			# The authored member is not trainable in the mounted pack. A named
			# limitation, not silence: the caller can surface the roster gap.
			untrainable.append(String(member.get("unit", "")))
			continue
		candidates.append({"unit_type": unit_type, "authored_unit": String(member.get("unit", "")), "percentage": percentage})
		total_percentage += percentage
	if candidates.is_empty():
		return {
			"ok": false,
			"reason": "side '%s' phase %d has no trainable authored members" % [side, phase],
			"untrainable": untrainable,
		}
	var counts: Dictionary = {}
	var living_total := 0
	for id in sim.living_ids(team):
		var row: Dictionary = sim.entities[id]
		if bool(row.get("is_builder", false)):
			continue
		var unit_type := String(row.get("unit_type", ""))
		counts[unit_type] = int(counts.get(unit_type, 0)) + 1
		living_total += 1
	var best: Dictionary = {}
	var best_deficit := -1.0e12
	for candidate_value in candidates:
		var candidate := candidate_value as Dictionary
		var desired := float(candidate["percentage"]) / total_percentage
		var current := 0.0
		if living_total > 0:
			current = float(int(counts.get(String(candidate["unit_type"]), 0))) / float(living_total)
		var deficit := desired - current
		if deficit > best_deficit + 0.000001:
			best_deficit = deficit
			best = candidate
	return {
		"ok": true,
		"unit_type": String(best["unit_type"]),
		"authored_unit": String(best["authored_unit"]),
		"phase": phase,
		"untrainable": untrainable,
	}


func current_army_phase(plan: Dictionary) -> int:
	## Phase 1 (Rush) until PhaseDuration_Rush seconds, phase 2 (MidGame)
	## until rush+mid, then phase 3 (EndGame). Unmeasured durations keep the
	## army in phase 1 honestly rather than inventing a schedule.
	var durations := plan.get("phase_durations", {}) as Dictionary
	var rush := float(durations.get("rush", -1.0))
	var mid := float(durations.get("mid_game", -1.0))
	if rush <= 0.0:
		return 1
	var elapsed_seconds := float(sim.tick_index) * float(sim.TICK_SECONDS)
	if elapsed_seconds < rush:
		return 1
	if mid <= 0.0:
		# Only the rush boundary is authored: past it the army is in the NEXT
		# authored phase and stays there — inventing an end-game boundary
		# retail never wrote would be a silent schedule.
		return 2
	if elapsed_seconds < rush + mid:
		return 2
	return 3


## ---- army compilation -----------------------------------------------------

func _compiled_army_plan(army_name: String, army: Dictionary) -> Dictionary:
	var side := String((army.get("side", {}) as Dictionary).get("value", ""))
	if side == "" or side.to_lower() == "unresolved":
		return {"refusal": "skirmish-ai army '%s' has no authored Side" % army_name}
	var members_value: Variant = army.get("armyMembers", [])
	if typeof(members_value) != TYPE_ARRAY:
		return {"refusal": "skirmish-ai army '%s' armyMembers is not a list" % army_name}
	var members: Array = []
	for member_value in members_value:
		var member := member_value as Dictionary
		var fields := member.get("fields", {}) as Dictionary
		var unit := String((fields.get("Unit", {}) as Dictionary).get("value", ""))
		if unit == "":
			return {"refusal": "skirmish-ai army '%s' has a member without a Unit" % army_name}
		var row: Dictionary = {
			"member_name": String((member.get("name", {}) as Dictionary).get("value", "")),
			"unit": unit,
			"phase_percentages": [],
		}
		for phase in [1, 2, 3]:
			var key := "PercentageOfArmyPhase%d" % phase
			var authored: Variant = (fields.get(key, {}) as Dictionary).get("value")
			if authored == null:
				# Retail omits later phases on some members; absent means the
				# member is not requested in that phase. Record the fact.
				(row["phase_percentages"] as Array).append(0.0)
				continue
			var number := _authored_number(String(authored))
			if is_nan(number):
				return {"refusal": "skirmish-ai army '%s' member '%s' %s is unmeasured: %s" % [
					army_name, unit, key, String(authored)]}
			(row["phase_percentages"] as Array).append(number)
		members.append(row)
	if members.is_empty():
		return {"refusal": "skirmish-ai army '%s' has no ArmyMemberDefinitions" % army_name}

	var fields_table := _compiled_field_values(army)
	return {
		"army_name": army_name,
		"side": side,
		"members": members,
		"hero_build_order": _token_values(army.get("heroBuildOrder", {})),
		"offensive_buildings": _token_values(army.get("offensiveBuildings", {})),
		"fields": fields_table,
		"phase_durations": {
			"rush": _field_number(fields_table, "PhaseDuration_Rush", -1.0),
			"mid_game": _field_number(fields_table, "PhaseDuration_MidGame", -1.0),
		},
		"must_use_command_point_percentage": [
			_field_number(fields_table, "MustUseCommandPointPercentage_Phase1", -1.0),
			_field_number(fields_table, "MustUseCommandPointPercentage_Phase2", -1.0),
			_field_number(fields_table, "MustUseCommandPointPercentage_Phase3", -1.0),
		],
	}


## ---- difficulty compilation -----------------------------------------------

func _compiled_difficulty(rows: Dictionary) -> Dictionary:
	if rows.is_empty():
		return {"refusal": "skirmish-ai difficultyTuning table is empty"}
	var compiled: Dictionary = {}
	for row_name in rows:
		var row := rows[row_name] as Dictionary
		var fields := _compiled_field_values(row)
		var label: Variant = fields.get("Difficulty")
		if label == null:
			return {"refusal": "skirmish-ai difficulty row '%s' names no Difficulty" % row_name}
		var entry: Dictionary = {"row_name": String(row_name), "fields": fields}
		for probability_key in [
			"EconomyUpgradeProbability",
			"OffensiveTacticActivationProbability",
			"SpecialPowerActivationProbability",
		]:
			var authored: Variant = fields.get(probability_key)
			if authored == null:
				continue
			var pair := _authored_ratio(String(authored))
			if pair.is_empty():
				return {"refusal": "skirmish-ai difficulty '%s' %s is unmeasured: %s" % [
					row_name, probability_key, String(authored)]}
			entry[probability_key] = pair
		compiled[String(label).to_upper()] = entry
	return compiled


## ---- authored-value parsing ------------------------------------------------

func _compiled_field_values(row: Dictionary) -> Dictionary:
	## Flatten {Field: {value, sourceIni, line}} into {Field: value-string}.
	var fields_value: Variant = row.get("fields", row)
	if typeof(fields_value) != TYPE_DICTIONARY:
		return {}
	var out: Dictionary = {}
	for key in fields_value:
		var cell: Variant = (fields_value as Dictionary)[key]
		if typeof(cell) == TYPE_DICTIONARY and (cell as Dictionary).has("value"):
			out[key] = (cell as Dictionary)["value"]
	return out


func _token_values(fact: Variant) -> Array:
	## A compiled token-list fact: {value: [tokens...]} or {value: "a b c"}.
	if typeof(fact) != TYPE_DICTIONARY:
		return []
	var value: Variant = (fact as Dictionary).get("value", [])
	if typeof(value) == TYPE_ARRAY:
		return (value as Array).duplicate()
	if typeof(value) == TYPE_STRING:
		var tokens: Array = []
		for token in String(value).split(" ", false):
			tokens.append(token)
		return tokens
	return []


func _field_number(fields: Dictionary, key: String, fallback: float) -> float:
	var authored: Variant = fields.get(key)
	if authored == null:
		return fallback
	var number := _authored_number(String(authored))
	return fallback if is_nan(number) else number


func _authored_number(text: String) -> float:
	## Retail numeric spellings: "40.0", "90%", "50.0f", "1950.0f". A percent
	## sign keeps the authored magnitude (90% -> 90.0) — consumers decide the
	## scale, this parser only proves measurability. NAN = unmeasured.
	var cleaned := text.strip_edges()
	if cleaned.ends_with("%"):
		cleaned = cleaned.substr(0, cleaned.length() - 1).strip_edges()
	if cleaned.ends_with("f") or cleaned.ends_with("F"):
		cleaned = cleaned.substr(0, cleaned.length() - 1)
	if not cleaned.is_valid_float():
		return NAN
	return cleaned.to_float()


func _authored_ratio(text: String) -> Array:
	## "10 : 150" probability pair -> [10.0, 150.0]; empty = unmeasured.
	var parts := text.split(":")
	if parts.size() != 2:
		return []
	var low := _authored_number(parts[0])
	var high := _authored_number(parts[1])
	if is_nan(low) or is_nan(high):
		return []
	return [low, high]


func _refuse(reason: String) -> Dictionary:
	push_error("configure_skirmish_ai refused: %s" % reason)
	sim.skirmish_ai_configured = false
	sim.skirmish_ai_plans_by_side = {}
	sim.skirmish_ai_difficulty = {}
	sim.skirmish_ai_brutal_cheats = {}
	return {"ok": false, "reason": reason}
