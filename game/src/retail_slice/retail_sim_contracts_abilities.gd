extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Ability/upgrade contracts carved out of retail_sim_module_contracts.gd (crack-2 finish): pickups, auto abilities, AI special powers, respawn, fire-weapon/deletion/production updates, rebuild holes, banners, give-upgrade, gates, stop/unleash powers, enemy sense, invisibility, stealth, slaved updates, spawn behaviors, object-creation/attribute/geometry upgrades, emotions.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func _attach_pickup_stuff_update_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("pickup_stuff_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var interval_value: Variant = fields.get("ScanIntervalSeconds")
	var interval_ms := 0.0
	if typeof(interval_value) == TYPE_DICTIONARY:
		var interval_field := interval_value as Dictionary
		interval_ms = float(interval_field.get("milliseconds", float(interval_field.get("seconds", 0.0)) * 1000.0))
	row["pickup_stuff_update"] = {
		"skirmish_ai_only": bool(_sim._module_contract_value(fields, "SkirmishAIOnly", false)),
		"filter": _sim._typed_contract_tokens(fields, "StuffToPickUp"),
		"scan_range_source": maxf(0.0, float(_sim._module_contract_value(fields, "ScanRange", 0.0))),
		"scan_interval_ticks": maxi(1, _sim._ship_contract_delay_ticks(interval_ms)),
		"next_scan_tick": _sim.tick_index,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func register_pickup_object(kind_of: Array, position: Vector2, object_id: String = "") -> int:
	var _sim = sim
	var id = _sim._next_pickup_object_id
	_sim._next_pickup_object_id += 1
	_sim.pickup_objects[id] = {
		"id": id,
		"object_id": object_id,
		"kind_of": kind_of.duplicate(),
		"position": position,
		"available": true,
	}
	return id


func remove_pickup_object(pickup_id: int) -> void:
	sim.pickup_objects.erase(pickup_id)


func _step_pickup_stuff_updates() -> void:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		var row := _sim.entities[entity_id] as Dictionary
		if not row.has("pickup_stuff_update") and not row.has("module_contracts"):
			_sim._attach_module_contracts(row)
		var policy := row.get("pickup_stuff_update", {}) as Dictionary
		if policy.is_empty() or int(row.get("health", 0)) <= 0:
			continue
		if bool(policy.get("skirmish_ai_only", false)) and (not _sim.ai_enabled or not _sim.team_is_ai(int(row.get("team", -1)))):
			continue
		if _sim.tick_index < int(policy.get("next_scan_tick", _sim.tick_index)):
			continue
		policy["next_scan_tick"] = _sim.tick_index + maxi(1, int(policy.get("scan_interval_ticks", 1)))
		row["pickup_stuff_update"] = policy
		var nearest := _nearest_pickup_for(row, policy)
		if nearest == 0:
			continue
		var destination := Vector2((_sim.pickup_objects[nearest] as Dictionary).get("position", row.get("position", Vector2.ZERO)))
		if _sim._assign_route(row, destination):
			row["pickup_target_id"] = nearest
			row["order_kind"] = "pickup"
			row["state"] = "run"
			_sim._emit_event("pickup_stuff.targeted", entity_id, nearest, {"destination": [destination.x, destination.y]})


func _nearest_pickup_for(row: Dictionary, policy: Dictionary) -> int:
	var _sim = sim
	var origin := Vector2(row.get("position", Vector2.ZERO))
	var scale := float(_sim._rules.get("source_map_transform_scale", 1.0))
	var maximum := float(policy.get("scan_range_source", 0.0)) * (scale if scale > 0.0 else 1.0)
	var best_id := 0
	var best_distance := maximum
	for pickup_id_value in _sim.pickup_objects.keys():
		var pickup_id = int(pickup_id_value)
		var pickup := _sim.pickup_objects[pickup_id] as Dictionary
		if not bool(pickup.get("available", true)):
			continue
		var probe := {"kind_of": pickup.get("kind_of", [])}
		if not _sim._transport_filter_accepts(probe, policy.get("filter", []) as Array):
			continue
		var distance := origin.distance_to(Vector2(pickup.get("position", origin)))
		if distance < best_distance or (is_equal_approx(distance, best_distance) and (best_id == 0 or pickup_id < best_id)):
			best_distance = distance
			best_id = pickup_id
	return best_id


func _attach_auto_ability_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	var rows: Array = row.get("auto_ability_behaviors", []) as Array
	var key := "%s:%d" % [String(contract.get("tag", "")), int(contract.get("line", 0))]
	for existing_value in rows:
		if String((existing_value as Dictionary).get("key", "")) == key:
			return
	var idle_ms := 0.0
	var idle_field: Variant = fields.get("IdleTimeSeconds")
	if typeof(idle_field) == TYPE_DICTIONARY:
		idle_ms = float((idle_field as Dictionary).get("milliseconds", float((idle_field as Dictionary).get("seconds", 0.0)) * 1000.0))
	rows.append({
		"key": key,
		"special_ability": String(_sim._module_contract_value(fields, "SpecialAbility", "")),
		"active": bool(_sim._module_contract_value(fields, "StartsActive", false)),
		"base_max_range_from_start": bool(_sim._module_contract_value(fields, "BaseMaxRangeFromStartPos", false)),
		"adjust_melee_position": bool(_sim._module_contract_value(fields, "AdjustAttackMeleePosition", false)),
		"allow_self": bool(_sim._module_contract_value(fields, "AllowSelf", false)),
		"maximum_range_source": _resolve_auto_ability_range(fields.get("MaxScanRange")),
		"minimum_range_source": _resolve_auto_ability_range(fields.get("MinScanRange")),
		"idle_ticks": _sim._ship_contract_delay_ticks(idle_ms),
		"forbidden_status": _sim._typed_contract_tokens(fields, "ForbiddenStatus"),
		"queries": (fields.get("Query", []) as Array).duplicate(true),
		"next_check_tick": _sim.tick_index,
		"unsupported_semantics": ["melee_position_solver:AdjustAttackMeleePosition"] if bool(_sim._module_contract_value(fields, "AdjustAttackMeleePosition", false)) else [],
	})
	row["auto_ability_behaviors"] = rows
	if not row.has("auto_ability_start_position"):
		row["auto_ability_start_position"] = row.get("position", Vector2.ZERO)


func _resolve_auto_ability_range(value: Variant) -> float:
	var _sim = sim
	if typeof(value) != TYPE_DICTIONARY:
		return 0.0
	var field := value as Dictionary
	match String(field.get("kind", "literal")):
		"literal":
			return maxf(0.0, float(field.get("value", 0.0)))
		"define":
			return maxf(0.0, float((_sim._rules.get("auto_ability_range_defines", {}) as Dictionary).get(String(field.get("name", "")), 0.0)))
		"subtract":
			return maxf(0.0, float((_sim._rules.get("auto_ability_range_defines", {}) as Dictionary).get(String(field.get("name", "")), 0.0)) - float(field.get("amount", 0.0)))
	return 0.0


func set_auto_ability_active(entity_id: int, special_ability: String, active: bool) -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row := _sim.entities[entity_id] as Dictionary
	if not row.has("auto_ability_behaviors"):
		_sim._attach_module_contracts(row)
	var behaviors: Array = row.get("auto_ability_behaviors", []) as Array
	for index in behaviors.size():
		var behavior := behaviors[index] as Dictionary
		if String(behavior.get("special_ability", "")) == special_ability:
			behavior["active"] = active
			behavior["next_check_tick"] = _sim.tick_index
			behaviors[index] = behavior
			row["auto_ability_behaviors"] = behaviors
			return {"ok": true, "reason": ""}
	return {"ok": false, "reason": "ability-contract-missing"}


func _step_auto_abilities() -> void:
	var _sim = sim
	var scale := float(_sim._rules.get("source_map_transform_scale", 1.0))
	if scale <= 0.0:
		scale = 1.0
	for entity_id in _sim.entity_ids():
		var row := _sim.entities[entity_id] as Dictionary
		if not row.has("auto_ability_behaviors") and not row.has("module_contracts"):
			_sim._attach_module_contracts(row)
		if int(row.get("health", 0)) <= 0:
			continue
		var behaviors: Array = row.get("auto_ability_behaviors", []) as Array
		for index in behaviors.size():
			var behavior := behaviors[index] as Dictionary
			if not bool(behavior.get("active", false)) or _sim.tick_index < int(behavior.get("next_check_tick", 0)):
				continue
			behavior["next_check_tick"] = _sim.tick_index + 1
			behaviors[index] = behavior
			var blocked := false
			for status_value in behavior.get("forbidden_status", []) as Array:
				if bool((row.get("object_status", {}) as Dictionary).get(String(status_value), false)):
					blocked = true
					break
			if blocked or _sim.tick_index - int(row.get("last_action_tick", 0)) < int(behavior.get("idle_ticks", 0)):
				continue
			var max_range := float(behavior.get("maximum_range_source", 0.0)) * scale
			var min_range := float(behavior.get("minimum_range_source", 0.0)) * scale
			if bool(behavior.get("base_max_range_from_start", false)) and Vector2(row.get("position", Vector2.ZERO)).distance_to(Vector2(row.get("auto_ability_start_position", row.get("position", Vector2.ZERO)))) > max_range:
				continue
			var target := _auto_ability_query_target(row, behavior, min_range, max_range)
			if target < 0:
				continue
			var ability_id := _ability_id_for_special_power(row, String(behavior.get("special_ability", "")))
			if ability_id == "":
				continue
			var point = Vector2(row.get("position", Vector2.ZERO)) if target == int(row.get("id", 0)) else Vector2((_sim.entities[target] as Dictionary).get("position", Vector2.ZERO))
			var result = _sim.cast_ability(entity_id, ability_id, point, int(row.get("team", -1)))
			if bool(result.get("ok", false)):
				row["last_action_tick"] = _sim.tick_index
		# Keep optional module state absent when this object has no authored
		# AutoAbilityBehavior.  Materializing an empty array mutates save/hash
		# state for every legacy battalion even though no behavior exists.
		if behaviors.is_empty():
			row.erase("auto_ability_behaviors")
		else:
			row["auto_ability_behaviors"] = behaviors


func _ability_id_for_special_power(row: Dictionary, special_power: String) -> String:
	for rule_value in sim._unit_ability_rules.get(String(row.get("unit_type", "")), []) as Array:
		var rule := rule_value as Dictionary
		if String(rule.get("special_power_id", "")) == special_power:
			return String(rule.get("ability_id", ""))
	return ""


func _auto_ability_query_target(source: Dictionary, behavior: Dictionary, minimum: float, maximum: float) -> int:
	var _sim = sim
	var origin := Vector2(source.get("position", Vector2.ZERO))
	var queries: Array = behavior.get("queries", []) as Array
	if queries.is_empty():
		return int(source.get("id", 0)) if bool(behavior.get("allow_self", false)) else -1
	var chosen := -1
	for query_value in queries:
		var query := query_value as Dictionary
		var tokens: Array = query.get("filterTokens", []) as Array
		var matches: Array[int] = []
		for candidate_id in _sim.entity_ids():
			if candidate_id == int(source.get("id", 0)) and not bool(behavior.get("allow_self", false)):
				continue
			var candidate = _sim.entities[candidate_id] as Dictionary
			var distance := origin.distance_to(Vector2(candidate.get("position", origin)))
			if distance < minimum or (maximum > 0.0 and distance > maximum):
				continue
			if _auto_ability_filter_accepts(source, candidate, tokens):
				matches.append(candidate_id)
		if matches.size() < int(query.get("minimumMatches", 1)):
			return -1
		if chosen < 0 and not matches.is_empty():
			chosen = matches[0]
	return chosen


func _auto_ability_filter_accepts(source: Dictionary, candidate: Dictionary, tokens: Array) -> bool:
	var _sim = sim
	var relation_filtered: Array = []
	for token_value in tokens:
		var token := String(token_value).to_upper()
		if token == "ENEMIES" and not _sim._is_hostile(int(source.get("team", -1)), int(candidate.get("team", -1))):
			return false
		if token == "ALLIES" and int(source.get("team", -1)) != int(candidate.get("team", -1)):
			return false
		if token not in ["ENEMIES", "ALLIES"]:
			relation_filtered.append(token)
	return _sim._ability_token_filter_accepts(candidate, relation_filtered)


# AISpecialPowerUpdate is an AI command router, not a second implementation of
# any power. It deterministically chooses a target from live simulation state,
# then enters the same sim.cast_ability()/sim.cast_power() path used by player commands.
# Consequently level, cooldown, ownership, activation filters, costs and the
# effect itself remain single-source-of-truth gameplay rules.


func _attach_ai_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	var command = String(_sim._module_contract_value(fields, "CommandButtonName", "")).strip_edges()
	var ai_type = String(_sim._module_contract_value(fields, "SpecialPowerAIType", "")).strip_edges().to_upper()
	if command == "" or ai_type == "":
		return
	var policies: Array = row.get("ai_special_power_updates", []) as Array
	var key := "%s:%d" % [String(contract.get("tag", "")), int(contract.get("line", 0))]
	for existing_value in policies:
		if String((existing_value as Dictionary).get("key", "")) == key:
			return
	var radius = _resolve_ai_special_power_expression(fields.get("SpecialPowerRadius"))
	var cast_range := _resolve_ai_special_power_expression(fields.get("SpecialPowerRange"))
	var unsupported: Array[String] = []
	if bool(radius.get("authored", false)) and not bool(radius.get("resolved", false)):
		unsupported.append("unresolved_radius_define:%s" % String(radius.get("expression", "")))
	if bool(cast_range.get("authored", false)) and not bool(cast_range.get("resolved", false)):
		unsupported.append("unresolved_range_define:%s" % String(cast_range.get("expression", "")))
	if not _ai_special_power_type_supported(ai_type):
		unsupported.append("unsupported_ai_type:%s" % ai_type)
	if ai_type == "AI_SPELLBOOK_TREE_KILLER":
		unsupported.append("terrain_tree_target_registry:unavailable")
	policies.append({
		"key": key,
		"command_button": command,
		"ai_type": ai_type,
		"radius_source": float(radius.get("value", 0.0)),
		"radius_authored": bool(radius.get("authored", false)),
		"range_source": float(cast_range.get("value", 0.0)),
		"range_authored": bool(cast_range.get("authored", false)),
		"spell_makes_structure": bool(_sim._module_contract_value(fields, "SpellMakesAStructure", false)),
		"randomize_target_location": bool(_sim._module_contract_value(fields, "RandomizeTargetLocation", false)),
		# No cadence field exists in the retail grammar. UpdateModule therefore
		# participates once per authoritative simulation update (one tick here).
		"next_check_tick": _sim.tick_index,
		"attempt_count": 0,
		"cast_count": 0,
		"last_target_id": 0,
		"last_target_kind": "none",
		"last_target_point": Vector2(row.get("position", Vector2.ZERO)),
		"last_result": "never-attempted",
		"unsupported_semantics": unsupported,
	})
	row["ai_special_power_updates"] = policies


func _resolve_ai_special_power_expression(value: Variant) -> Dictionary:
	if typeof(value) != TYPE_DICTIONARY:
		return {"authored": false, "resolved": true, "value": 0.0, "expression": ""}
	var field := value as Dictionary
	var expression := String(field.get("expression", "")).strip_edges()
	if field.has("value"):
		return {"authored": true, "resolved": true, "value": maxf(0.0, float(field.get("value", 0.0))), "expression": expression}
	var defines := sim._rules.get("ai_special_power_defines", {}) as Dictionary
	if defines.has(expression) and typeof(defines[expression]) in [TYPE_INT, TYPE_FLOAT] and is_finite(float(defines[expression])) and float(defines[expression]) >= 0.0:
		return {"authored": true, "resolved": true, "value": float(defines[expression]), "expression": expression}
	return {"authored": true, "resolved": false, "value": 0.0, "expression": expression}


# De-staticed on extraction (instance sim access).
func _ai_special_power_type_supported(ai_type: String) -> bool:
	return ai_type in [
		"AI_SPECIAL_POWER_BASIC_SELF_BUFF", "AI_SPECIAL_POWER_CAPTURE_BUILDING",
		"AI_SPECIAL_POWER_CHARGE", "AI_SPECIAL_POWER_ELENDIL",
		"AI_SPECIAL_POWER_ENEMY_TYPE_KILLER", "AI_SPECIAL_POWER_ENEMY_TYPE_KILLER_RANGED",
		"AI_SPECIAL_POWER_ENEMY_TYPE_KILLER_STRUCTURES", "AI_SPECIAL_POWER_GANDALF_WIZARD_BLAST",
		"AI_SPECIAL_POWER_GIVEXP_AOE", "AI_SPECIAL_POWER_GOBLINKING_CALLOFTHEDEEP",
		"AI_SPECIAL_POWER_GOBLINKING_MOUNTED", "AI_SPECIAL_POWER_HEAL_AOE",
		"AI_SPECIAL_POWER_LEGOLAS_ARROWWIND", "AI_SPECIAL_POWER_LEGOLAS_TRAINARCHERS",
		"AI_SPECIAL_POWER_RANGED_AOE_ATTACK", "AI_SPECIAL_POWER_SELFAOEHEALHEROS",
		"AI_SPECIAL_POWER_STANCEAGGRESSIVE", "AI_SPECIAL_POWER_STANCEBATTLE",
		"AI_SPECIAL_POWER_STANCEHOLDGROUND", "AI_SPECIAL_POWER_TARGETAOE_SUMMON",
		"AI_SPECIAL_POWER_TOGGLE_MOUNTED", "AI_SPECIAL_POWER_TOGGLE_SIEGE",
		"AI_SPELLBOOK_ALWAYS_FIRE", "AI_SPELLBOOK_ARMY_BREAKER",
		"AI_SPELLBOOK_ASSIST_BATTLE_BUFF", "AI_SPELLBOOK_ASSIST_BATTLE_DEBUFF",
		"AI_SPELLBOOK_BUFFECONOMYBUILDING", "AI_SPELLBOOK_BUFFTERRAIN",
		"AI_SPELLBOOK_CALLTHEHORDE", "AI_SPELLBOOK_CAPTURE_CREEP", "AI_SPELLBOOK_CITADEL",
		"AI_SPELLBOOK_ENSHROUDINGMIST", "AI_SPELLBOOK_HEAL", "AI_SPELLBOOK_REBUILD",
		"AI_SPELLBOOK_SHROUD_REVEAL", "AI_SPELLBOOK_STRUCTURE_BASEKILL",
		"AI_SPELLBOOK_STRUCTURE_BREAKER", "AI_SPELLBOOK_STRUCTURE_BREAKER_PREF_WALLS",
		"AI_SPELLBOOK_TREE_KILLER",
	]


func _step_ai_special_power_updates() -> void:
	var _sim = sim
	if not _sim.ai_enabled:
		return
	for entity_id in _sim.entity_ids():
		var row := _sim.entities[entity_id] as Dictionary
		if not _sim.team_is_ai(int(row.get("team", -1))) or int(row.get("health", 0)) <= 0:
			continue
		if not row.has("ai_special_power_updates") and row.has("module_contracts"):
			_sim._attach_module_contracts(row)
		var policies: Array = row.get("ai_special_power_updates", []) as Array
		for index in policies.size():
			var policy := policies[index] as Dictionary
			if _sim.tick_index < int(policy.get("next_check_tick", 0)):
				continue
			policy["next_check_tick"] = _sim.tick_index + 1
			if not (policy.get("unsupported_semantics", []) as Array).is_empty():
				policy["last_result"] = String((policy.get("unsupported_semantics", []) as Array)[0])
				policies[index] = policy
				continue
			var ability_rule := _ai_special_power_ability_rule(
				row, String(policy.get("command_button", ""))
			)
			var blocked_condition := _ability_auto_blocked_model_condition(row, ability_rule)
			if blocked_condition != "":
				policy["last_result"] = "auto-ability-model-condition:%s" % blocked_condition
				policies[index] = policy
				continue
			var stance := _ai_special_power_stance(String(policy.get("ai_type", "")))
			if stance != "":
				if stance != _ai_special_power_desired_stance(row):
					policy["last_result"] = "stance-condition-not-met"
					policies[index] = policy
					continue
				policy["attempt_count"] = int(policy.get("attempt_count", 0)) + 1
				var stance_result = _sim.set_entity_stance(entity_id, stance)
				policy["last_result"] = String(stance_result.get("reason", ""))
				if bool(stance_result.get("ok", false)):
					policy["cast_count"] = int(policy.get("cast_count", 0)) + 1
				policies[index] = policy
				continue
			var target := _ai_special_power_target(row, policy)
			if not bool(target.get("ok", false)):
				policy["last_result"] = String(target.get("reason", "no-eligible-target"))
				policies[index] = policy
				continue
			policy["attempt_count"] = int(policy.get("attempt_count", 0)) + 1
			policy["last_target_id"] = int(target.get("id", 0))
			policy["last_target_kind"] = String(target.get("kind", "point"))
			policy["last_target_point"] = Vector2(target.get("point", row.get("position", Vector2.ZERO)))
			var result = _ai_special_power_cast(entity_id, row, policy, Vector2(policy["last_target_point"]))
			policy["last_result"] = String(result.get("reason", ""))
			if bool(result.get("ok", false)):
				policy["cast_count"] = int(policy.get("cast_count", 0)) + 1
				_sim._emit_event("ai_special_power.cast", entity_id, int(policy.get("last_target_id", 0)), {"command": policy.get("command_button"), "ai_type": policy.get("ai_type"), "target_kind": policy.get("last_target_kind")})
			policies[index] = policy
			# Retail modules are authored in priority order; once one actionable
			# power succeeds, lower rows wait for the next UpdateModule pass.
			if bool(result.get("ok", false)):
				break
		row["ai_special_power_updates"] = policies


func _ability_auto_blocked_model_condition(row: Dictionary, rule: Dictionary) -> String:
	var effect := rule.get("effect", {}) as Dictionary
	if not bool(effect.get("autoAbility", false)):
		return ""
	var active: Dictionary = {}
	for condition_value in row.get("model_conditions", []) as Array:
		active[String(condition_value).to_upper()] = true
	for condition_value in (row.get("object_status", {}) as Dictionary).keys():
		if bool((row.get("object_status", {}) as Dictionary).get(condition_value, false)):
			active[String(condition_value).to_upper()] = true
	if not (row.get("route", []) as Array).is_empty() or float(row.get("current_speed", 0.0)) > 0.0:
		active["MOVING"] = true
	for blocked_value in effect.get("autoAbilityBlockedModelConditions", []) as Array:
		var blocked := String(blocked_value).to_upper()
		if active.has(blocked):
			return blocked
	return ""


func _ai_special_power_cast(entity_id: int, row: Dictionary, policy: Dictionary, point: Vector2) -> Dictionary:
	var _sim = sim
	var command := String(policy.get("command_button", ""))
	if command.begins_with("Command_SpellBook"):
		return _sim.cast_power(int(row.get("team", -1)), command.trim_prefix("Command_"), point)
	return _sim.cast_ability(entity_id, command, point, int(row.get("team", -1)))


# De-staticed on extraction (instance sim access).
func _ai_special_power_stance(ai_type: String) -> String:
	match ai_type:
		"AI_SPECIAL_POWER_STANCEAGGRESSIVE": return "Aggressive"
		"AI_SPECIAL_POWER_STANCEBATTLE": return "Battle"
		"AI_SPECIAL_POWER_STANCEHOLDGROUND": return "HoldGround"
	return ""


# De-staticed on extraction (instance sim access).
func _ai_special_power_desired_stance(row: Dictionary) -> String:
	if bool(row.get("hold_ground", false)):
		return "HoldGround"
	if int(row.get("target_id", 0)) != 0 or String(row.get("state", "")) == "attack":
		return "Aggressive"
	return "Battle"


func _ai_special_power_target(source: Dictionary, policy: Dictionary) -> Dictionary:
	var _sim = sim
	var ai_type := String(policy.get("ai_type", ""))
	var origin := Vector2(source.get("position", Vector2.ZERO))
	if _ai_special_power_is_self(ai_type):
		return {"ok": true, "id": int(source.get("id", 0)), "kind": "self", "point": origin}
	var scale := maxf(0.000001, float(_sim._rules.get("source_map_transform_scale", 1.0)))
	var command_rule := _ai_special_power_ability_rule(source, String(policy.get("command_button", "")))
	var effect := command_rule.get("effect", {}) as Dictionary
	var search_range := float(policy.get("range_source", 0.0)) * scale
	if not bool(policy.get("range_authored", false)):
		search_range = float(effect.get("range", source.get("vision_range", 0.0)))
		if search_range <= 0.0:
			search_range = float(source.get("vision_range", 0.0))
	var radius = float(policy.get("radius_source", 0.0)) * scale
	if not bool(policy.get("radius_authored", false)):
		for radius_key in ["damage_radius", "radius_scaled", "target_radius_scaled"]:
			radius = maxf(radius, float(effect.get(radius_key, 0.0)))
	var candidates: Array[Dictionary] = []
	if _ai_special_power_targets_structure(ai_type):
		var allied_structure := ai_type in ["AI_SPELLBOOK_BUFFECONOMYBUILDING", "AI_SPELLBOOK_REBUILD", "AI_SPELLBOOK_CITADEL"]
		for structure_id in _sim.structure_ids():
			var structure := _sim.structures[structure_id] as Dictionary
			if int(structure.get("health", 0)) <= 0:
				continue
			var capture := ai_type == "AI_SPECIAL_POWER_CAPTURE_BUILDING"
			if capture and (int(structure.get("team", -1)) != _sim.NEUTRAL_TEAM or not bool(structure.get("capturable", false))):
				continue
			if not capture and allied_structure != (int(source.get("team", -1)) == int(structure.get("team", -1))):
				continue
			if not capture and not allied_structure and not _sim._is_hostile(int(source.get("team", -1)), int(structure.get("team", -1))):
				continue
			if ai_type == "AI_SPELLBOOK_REBUILD" and int(structure.get("health", 0)) >= int(structure.get("maximum_health", structure.get("health", 0))):
				continue
			if ai_type == "AI_SPELLBOOK_BUFFECONOMYBUILDING" and int(structure.get("income_per_payout", 0)) <= 0:
				continue
			if ai_type == "AI_SPELLBOOK_CITADEL" and String(structure.get("structure_kind", "")).to_lower() not in ["fortress", "citadel"]:
				continue
			var point = Vector2(structure.get("position", origin))
			var distance := origin.distance_to(point)
			if search_range > 0.0 and distance > search_range:
				continue
			var structure_score := _ai_special_power_cluster_score(source, point, radius, allied_structure)
			var structure_kind := String(structure.get("structure_kind", "")).to_lower()
			if ai_type == "AI_SPELLBOOK_STRUCTURE_BASEKILL" and structure_kind in ["fortress", "citadel"]:
				structure_score += 10000
			if ai_type == "AI_SPELLBOOK_STRUCTURE_BREAKER_PREF_WALLS" and "wall" in structure_kind:
				structure_score += 10000
			candidates.append({"id": structure_id, "kind": "structure", "point": point, "distance": distance, "score": structure_score})
	else:
		var allies := _ai_special_power_targets_allies(ai_type)
		for candidate_id in _sim.entity_ids():
			var candidate = _sim.entities[candidate_id] as Dictionary
			if int(candidate.get("health", 0)) <= 0:
				continue
			var same_team := int(candidate.get("team", -1)) == int(source.get("team", -1))
			if allies != same_team or (not allies and not _sim._is_hostile(int(source.get("team", -1)), int(candidate.get("team", -1)))):
				continue
			if ai_type == "AI_SPECIAL_POWER_HEAL_AOE" and int(candidate.get("health", 0)) >= int(candidate.get("maximum_health", candidate.get("health", 0))):
				continue
			var point = Vector2(candidate.get("position", origin))
			var distance := origin.distance_to(point)
			if search_range > 0.0 and distance > search_range:
				continue
			if bool(policy.get("spell_makes_structure", false)) and not _ai_special_power_structure_site_clear(point, maxf(radius, 1.0)):
				continue
			candidates.append({"id": candidate_id, "kind": "battalion", "point": point, "distance": distance, "score": _ai_special_power_cluster_score(source, point, radius, allies)})
	if candidates.is_empty():
		return {"ok": false, "reason": "no-eligible-target"}
	candidates.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		var score_a := int(a.get("score", 0)); var score_b := int(b.get("score", 0))
		if score_a != score_b: return score_a > score_b
		var distance_a := float(a.get("distance", 0.0)); var distance_b := float(b.get("distance", 0.0))
		return distance_a < distance_b or (is_equal_approx(distance_a, distance_b) and int(a.get("id", 0)) < int(b.get("id", 0)))
	)
	var selected := 0
	if bool(policy.get("randomize_target_location", false)) and candidates.size() > 1:
		selected = _sim.logic_random_int(0, candidates.size() - 1)
	var output := candidates[selected].duplicate(true)
	output["ok"] = true
	return output


func _ai_special_power_ability_rule(source: Dictionary, command: String) -> Dictionary:
	for rule_value in sim._unit_ability_rules.get(String(source.get("unit_type", "")), []) as Array:
		var rule := rule_value as Dictionary
		if String(rule.get("ability_id", "")) == command:
			return rule
	return {}


# De-staticed on extraction (instance sim access).
func _ai_special_power_is_self(ai_type: String) -> bool:
	return ai_type in ["AI_SPECIAL_POWER_BASIC_SELF_BUFF", "AI_SPECIAL_POWER_CHARGE", "AI_SPECIAL_POWER_ELENDIL", "AI_SPECIAL_POWER_GOBLINKING_MOUNTED", "AI_SPECIAL_POWER_SELFAOEHEALHEROS", "AI_SPECIAL_POWER_TOGGLE_MOUNTED", "AI_SPECIAL_POWER_TOGGLE_SIEGE", "AI_SPELLBOOK_ALWAYS_FIRE", "AI_SPELLBOOK_CALLTHEHORDE", "AI_SPELLBOOK_SHROUD_REVEAL"]


# De-staticed on extraction (instance sim access).
func _ai_special_power_targets_allies(ai_type: String) -> bool:
	return ai_type in ["AI_SPECIAL_POWER_GIVEXP_AOE", "AI_SPECIAL_POWER_HEAL_AOE", "AI_SPECIAL_POWER_LEGOLAS_TRAINARCHERS", "AI_SPELLBOOK_ASSIST_BATTLE_BUFF", "AI_SPELLBOOK_BUFFECONOMYBUILDING", "AI_SPELLBOOK_BUFFTERRAIN", "AI_SPELLBOOK_ENSHROUDINGMIST", "AI_SPELLBOOK_HEAL", "AI_SPELLBOOK_REBUILD"]


# De-staticed on extraction (instance sim access).
func _ai_special_power_targets_structure(ai_type: String) -> bool:
	return ai_type in ["AI_SPECIAL_POWER_CAPTURE_BUILDING", "AI_SPECIAL_POWER_ENEMY_TYPE_KILLER_STRUCTURES", "AI_SPELLBOOK_BUFFECONOMYBUILDING", "AI_SPELLBOOK_CITADEL", "AI_SPELLBOOK_REBUILD", "AI_SPELLBOOK_STRUCTURE_BASEKILL", "AI_SPELLBOOK_STRUCTURE_BREAKER", "AI_SPELLBOOK_STRUCTURE_BREAKER_PREF_WALLS"]


func _ai_special_power_cluster_score(source: Dictionary, point: Vector2, radius: float, allies: bool) -> int:
	var _sim = sim
	if radius <= 0.0:
		return 1
	var score := 0
	for candidate_id in _sim.entity_ids():
		var candidate = _sim.entities[candidate_id] as Dictionary
		if int(candidate.get("health", 0)) <= 0 or Vector2(candidate.get("position", point)).distance_to(point) > radius:
			continue
		var same_team := int(candidate.get("team", -1)) == int(source.get("team", -1))
		if allies == same_team and (allies or _sim._is_hostile(int(source.get("team", -1)), int(candidate.get("team", -1)))):
			score += 1
	return score


func _ai_special_power_structure_site_clear(point: Vector2, radius: float) -> bool:
	var _sim = sim
	for structure_id in _sim.structure_ids():
		var structure := _sim.structures[structure_id] as Dictionary
		if int(structure.get("health", 0)) > 0 and Vector2(structure.get("position", point)).distance_to(point) <= radius:
			return false
	return true


func _attach_respawn_update_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("respawn_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var rules := fields.get("RespawnRules", {}) as Dictionary
	var receipts: Array[String] = []
	for key in ["DeathAnim", "DeathFX", "InitialSpawnFX", "RespawnAnim", "RespawnFX", "ButtonImage"]:
		if fields.has(key):
			receipts.append("presentation_binding:%s" % key)
	row["respawn_update"] = {
		"auto_spawn": bool(rules.get("autoSpawn", false)),
		"cost": maxi(0, int(rules.get("cost", 0))),
		"time_ticks": _sim._ship_contract_delay_ticks(float(rules.get("timeMilliseconds", 0.0))),
		"health_fraction": clampf(float(rules.get("healthPercent", 100.0)) / 100.0, 0.0, 1.0),
		"anchor_filter": _sim._typed_contract_tokens(fields, "AutoRespawnAtObjectFilter"),
		"respawn_as_template": String(_sim._module_contract_value(fields, "RespawnAsTemplate", "")),
		"entries": (fields.get("RespawnEntry", []) as Array).duplicate(true),
		"death_animation_ticks": _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields, "DeathAnimationTime", 0.0))),
		"respawn_animation_ticks": _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields, "RespawnAnimationTime", 0.0))),
		"unsupported_semantics": receipts,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func _schedule_respawn_update(entity_id: int, row: Dictionary, death_type:String="NORMAL", attacker_id:int=0) -> void:
	var _sim = sim
	if not row.has("respawn_update"):
		_sim._attach_module_contracts(row)
	var policy := row.get("respawn_update", {}) as Dictionary
	if policy.is_empty() or _sim.respawn_schedules.has(entity_id):
		return
	var body:=row.get("respawn_body",{}) as Dictionary
	if not body.is_empty():
		if not bool(body.get("can_respawn",true)):
			_sim._emit_event("respawn.blocked",entity_id,attacker_id,{"reason":"RespawnBody.CanRespawn","death_type":death_type});return
		var killer_filter:=body.get("permanently_killed_filter",[]) as Array
		if not killer_filter.is_empty() and _sim.entities.has(attacker_id) and _sim._transport_filter_accepts(_sim.entities[attacker_id] as Dictionary,killer_filter):
			_sim._emit_event("respawn.blocked",entity_id,attacker_id,{"reason":"RespawnBody.PermanentlyKilledByFilter","death_type":death_type});return
	var level := int(row.get("level", 1))
	var cost := int(policy.get("cost", 0))
	var delay := int(policy.get("time_ticks", 0))
	for entry_value in policy.get("entries", []) as Array:
		var entry := entry_value as Dictionary
		if int(entry.get("level", 0)) == level:
			cost = maxi(0, int(entry.get("cost", cost)))
			delay = _sim._ship_contract_delay_ticks(float(entry.get("timeMilliseconds", delay * _sim.TICK_SECONDS * 1000.0)))
			break
	_sim.respawn_schedules[entity_id] = {
		"entity": row.duplicate(true),
		"ready_tick": _sim.tick_index + maxi(0, delay),
		"cost": cost,
		"requested": bool(policy.get("auto_spawn", false)),
	}
	_sim._emit_event("respawn.scheduled", entity_id, 0, {"ready_tick": _sim.tick_index + maxi(0, delay), "cost": cost, "auto_spawn": bool(policy.get("auto_spawn", false))})


func request_respawn(entity_id: int) -> Dictionary:
	var _sim = sim
	if not _sim.respawn_schedules.has(entity_id):
		return {"ok": false, "reason": "respawn-not-scheduled"}
	var schedule := _sim.respawn_schedules[entity_id] as Dictionary
	var row := schedule.get("entity", {}) as Dictionary
	var team := int(row.get("team", -1))
	var cost := int(schedule.get("cost", 0))
	if _sim.resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	_sim.team_resources[team] = _sim.resources_for_team(team) - cost
	schedule["cost_paid"] = true
	schedule["requested"] = true
	_sim.respawn_schedules[entity_id] = schedule
	return {"ok": true, "reason": ""}


func _step_respawn_updates() -> void:
	var _sim = sim
	for entity_id_value in _sim.respawn_schedules.keys().duplicate():
		var entity_id := int(entity_id_value)
		var schedule := _sim.respawn_schedules[entity_id] as Dictionary
		if not bool(schedule.get("requested", false)) or _sim.tick_index < int(schedule.get("ready_tick", 0)):
			continue
		var row := (schedule.get("entity", {}) as Dictionary).duplicate(true)
		var policy := row.get("respawn_update", {}) as Dictionary
		var team := int(row.get("team", -1))
		var anchor_id := _respawn_anchor(team, policy.get("anchor_filter", []) as Array)
		if anchor_id == 0:
			continue
		if not bool(schedule.get("cost_paid", false)):
			var cost := int(schedule.get("cost", 0))
			if _sim.resources_for_team(team) < cost:
				continue
			_sim.team_resources[team] = _sim.resources_for_team(team) - cost
		var template := String(policy.get("respawn_as_template", ""))
		if template != "":
			row["unit_type"] = template
			row["object_id"] = template
		row["position"] = (_sim.structures[anchor_id] as Dictionary).get("position", row.get("position", Vector2.ZERO))
		var member_max := maxi(1, int(row.get("member_maximum_health", 1)))
		var health_values: Array = row.get("member_health", []) as Array
		var revived_health := maxi(1, roundi(float(member_max) * float(policy.get("health_fraction", 1.0))))
		for index in health_values.size():
			health_values[index] = revived_health
		row["member_health"] = health_values
		row["health"] = revived_health * health_values.size()
		row["maximum_health"] = member_max * health_values.size()
		row["state"] = "idle"
		row["target_id"] = 0
		row["corpse_expire_tick"] = -1
		row.erase("death_tick")
		row["respawn_animation_until_tick"] = _sim.tick_index + int(policy.get("respawn_animation_ticks", 0))
		_sim.entities[entity_id] = row
		_sim._spatial_sync(row)
		_sim.respawn_schedules.erase(entity_id)
		_sim._emit_event("respawn.completed", entity_id, anchor_id, {"template": template, "health": int(row.get("health", 0))})


func _respawn_anchor(team: int, filter: Array) -> int:
	var _sim = sim
	for structure_id in _sim.structure_ids(team):
		var structure := _sim.structures[structure_id] as Dictionary
		if int(structure.get("health", 0)) <= 0:
			continue
		if filter.is_empty() or _sim._transport_filter_accepts({"category":String(structure.get("structure_kind", "")), "kind_of":structure.get("kind_of", [])}, filter):
			return structure_id
	return 0


func _attach_fire_weapon_update_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("fire_weapon_updates"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var nuggets: Array[Dictionary] = []
	for nugget_value in fields.get("FireWeaponNugget", []) as Array:
		var nugget := nugget_value as Dictionary
		var delay = _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(nugget, "FireDelay", 0.0)))
		var offset := Vector2.ZERO
		var offset_z := 0.0
		var offset_value: Variant = _sim._module_contract_value(nugget, "Offset", null)
		if typeof(offset_value) == TYPE_DICTIONARY:
			var coord := offset_value as Dictionary
			offset = Vector2(float(coord.get("x", 0.0)), float(coord.get("y", 0.0)))
			offset_z = float(coord.get("z", 0.0))
		nuggets.append({"weapon":String(_sim._module_contract_value(nugget,"WeaponName","")),"one_shot":bool(_sim._module_contract_value(nugget,"OneShot",false)),"delay_ticks":maxi(1,delay),"next_fire_tick":_sim.tick_index+delay,"offset_source":offset,"offset_z_source":offset_z,"fired":false})
	row["fire_weapon_updates"] = {
		"charging_trigger": bool(_sim._module_contract_value(fields,"ChargingModeTrigger",false)),
		"alive_only": bool(_sim._module_contract_value(fields,"AliveOnly",false)),
		"hero_mode_trigger": bool(_sim._module_contract_value(fields,"HeroModeTrigger",false)),
		"nuggets": nuggets,
	}


func _step_fire_weapon_updates() -> void:
	var _sim = sim
	for table_value in [_sim.entities, _sim.structures]:
		var table = table_value as Dictionary
		var ids: Array = table.keys()
		ids.sort()
		for id_value in ids:
			var id = int(id_value)
			var row := table[id] as Dictionary
			if not row.has("fire_weapon_updates") and not row.has("module_contracts"):
				if table == _sim.entities:
					_sim._attach_module_contracts(row)
				else:
					_sim._attach_structure_module_contracts(row)
			var policy := row.get("fire_weapon_updates", {}) as Dictionary
			if policy.is_empty() or (bool(policy.get("alive_only",false)) and int(row.get("health",0))<=0):
				continue
			if bool(policy.get("charging_trigger",false)) and not bool(row.get("charging_mode",false)):
				continue
			if bool(policy.get("hero_mode_trigger",false)) and not bool(row.get("hero_mode",false)):
				continue
			var nuggets: Array = policy.get("nuggets",[]) as Array
			for index in nuggets.size():
				var nugget := nuggets[index] as Dictionary
				if (bool(nugget.get("one_shot",false)) and bool(nugget.get("fired",false))) or _sim.tick_index<int(nugget.get("next_fire_tick",0)):
					continue
				var weapon := String(nugget.get("weapon",""))
				var facing := Vector2(row.get("facing",Vector2.RIGHT)).normalized()
				var local = _sim._retail_source_to_sim_offset(Vector2(nugget.get("offset_source",Vector2.ZERO)))
				var point = Vector2(row.get("position",Vector2.ZERO))+local.rotated(facing.angle())
				_sim._fire_death_weapon({"weapon_id":weapon,"weapon_rule":(_sim._death_weapon_rules.get(weapon,{}) as Dictionary).duplicate(true),"point":point,"team":int(row.get("team",-1)),"source_id":id,"height_source":float(nugget.get("offset_z_source",0.0)),"death_type":"FIRE_WEAPON_UPDATE"})
				nugget["fired"] = true
				nugget["next_fire_tick"] = _sim.tick_index + maxi(1,int(nugget.get("delay_ticks",1)))
				nuggets[index] = nugget
				_sim._emit_event("module.fire_weapon_update",id,0,{"weapon_id":weapon,"one_shot":bool(nugget.get("one_shot",false))})
			policy["nuggets"] = nuggets
			row["fire_weapon_updates"] = policy


func _attach_deletion_update_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("deletion_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var low := _resolve_deletion_bound(fields.get("MinLifetime"))
	var high := _resolve_deletion_bound(fields.get("MaxLifetime"))
	if bool(low.get("indefinite",false)) or bool(high.get("indefinite",false)):
		row["deletion_update"]={"indefinite":true,"unsupported_semantics":[]}
		return
	if not bool(low.get("resolved",false)) or not bool(high.get("resolved",false)):
		row["deletion_update"]={"indefinite":false,"unsupported_semantics":["unresolved_lifetime_expression"],"expire_tick":-1}
		return
	var low_ticks=_sim._ship_contract_delay_ticks(float(low.get("milliseconds",0.0)))
	var high_ticks=maxi(low_ticks,_sim._ship_contract_delay_ticks(float(high.get("milliseconds",0.0))))
	var lifetime =low_ticks if low_ticks==high_ticks else _sim.logic_random_int(low_ticks,high_ticks)
	row["deletion_update"]={"indefinite":false,"expire_tick":_sim.tick_index+lifetime,"selected_ticks":lifetime,"unsupported_semantics":[]}


func _resolve_deletion_bound(value: Variant) -> Dictionary:
	if typeof(value)!=TYPE_DICTIONARY:return {"resolved":false}
	var bound:=value as Dictionary
	if bool(bound.get("indefinite",false)):return {"resolved":true,"indefinite":true}
	if bound.has("milliseconds"):return {"resolved":true,"indefinite":false,"milliseconds":float(bound.get("milliseconds",0.0))}
	var name:=String(bound.get("name",bound.get("expression","")))
	var defines:=sim._rules.get("deletion_lifetime_defines",{}) as Dictionary
	if typeof(defines.get(name)) in [TYPE_INT,TYPE_FLOAT]:return {"resolved":true,"indefinite":false,"milliseconds":float(defines[name])}
	return {"resolved":false,"indefinite":false}


func _step_deletion_updates() -> void:
	var _sim = sim
	for id in _sim.entity_ids():
		var row:=_sim.entities[id] as Dictionary
		if not row.has("deletion_update") and not row.has("module_contracts"):_sim._attach_module_contracts(row)
		var policy:=row.get("deletion_update",{}) as Dictionary
		if not bool(policy.get("indefinite",false)) and int(policy.get("expire_tick",-1))>=0 and _sim.tick_index>=int(policy.get("expire_tick",-1)):
			_sim._expire_lifetime_entity(id,row,"FADED")
	for id in _sim.structure_ids():
		var row:=_sim.structures[id] as Dictionary
		if not row.has("deletion_update") and not row.has("module_contracts"):_sim._attach_structure_module_contracts(row)
		var policy:=row.get("deletion_update",{}) as Dictionary
		if not bool(policy.get("indefinite",false)) and int(policy.get("expire_tick",-1))>=0 and _sim.tick_index>=int(policy.get("expire_tick",-1)):
			_sim._expire_lifetime_structure(id,row,"FADED")


func _attach_production_update_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("production_update"): return
	var fields:=contract.get("fields",{}) as Dictionary
	var modifiers:Array[Dictionary]=[]
	for value in fields.get("ProductionModifier",[]) as Array:
		var source:=value as Dictionary
		modifiers.append({"required_upgrade":String(_sim._module_contract_value(source,"RequiredUpgrade","")),"cost_multiplier":float(_sim._module_contract_value(source,"CostMultiplier",1.0)),"time_multiplier":float(_sim._module_contract_value(source,"TimeMultiplier",1.0)),"filter":_sim._typed_contract_tokens(source,"ModifierFilter"),"hero_purchase":bool(_sim._module_contract_value(source,"HeroPurchase",false)),"hero_revive":bool(_sim._module_contract_value(source,"HeroRevive",false))})
	var receipts:Array[String]=[]
	for key in ["NumDoorAnimations","DoorOpeningTime","DoorWaitOpenTime","DoorCloseTime","ConstructionCompleteDuration","SetBonusModelConditionOnSpeedBonus","BonusForType","SpeedBonusAudioLoop"]:
		if fields.has(key):receipts.append("presentation_binding:%s"%key)
	for key in ["GiveNoXP","VeteranUnitsFromVeteranFactory","UnitInvulnerableTime"]:
		if fields.has(key):receipts.append("unsupported_production_semantic:%s"%key)
	for modifier in modifiers:
		if bool(modifier.get("hero_revive",false)):
			receipts.append("unsupported_production_semantic:ProductionModifier.HeroRevive")
	# MaxQueueEntries is authored on 2 of 423 retail ProductionUpdate blocks
	# (angmarthrallmaster.ini:587, dwarvenbattlewagon.ini:492, both `= 1`).
	# Absent must land as 0 = UNCAPPED, never as an invented default.
	row["production_update"]={"maximum_queue_entries":int(_sim._module_contract_value(fields,"MaxQueueEntries",0)),"give_no_xp":bool(_sim._module_contract_value(fields,"GiveNoXP",false)),"veteran_units":bool(_sim._module_contract_value(fields,"VeteranUnitsFromVeteranFactory",false)),"unit_invulnerable_ticks":_sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields,"UnitInvulnerableTime",0.0))),"modifiers":modifiers,"unsupported_semantics":receipts}


func _attach_getting_built_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("getting_built_behavior"):return
	var fields:=contract.get("fields",{}) as Dictionary
	var spawn:=_resolve_build_seconds(fields.get("SpawnTimer"))
	var rebuild:=_resolve_build_seconds(fields.get("RebuildTimeSeconds"))
	var receipts:Array[String]=[]
	for key in ["WorkerName","EvilWorkerName","SelfBuildingLoop","SelfRepairFromDamageLoop","SelfRepairFromRubbleLoop","TestFaction"]:
		if fields.has(key):receipts.append("presentation_or_worker_binding:%s"%key)
	if not bool(spawn.get("resolved",false)) and not bool(spawn.get("disabled",false)):receipts.append("unresolved_define:SpawnTimer")
	if fields.has("RebuildTimeSeconds") and not bool(rebuild.get("resolved",false)):receipts.append("unresolved_define:RebuildTimeSeconds")
	if bool(spawn.get("resolved",false)) and not bool(spawn.get("disabled",false)):receipts.append("unsupported_construction_semantic:SpawnTimer")
	if bool(rebuild.get("resolved",false)):receipts.append("unsupported_construction_semantic:RebuildTimeSeconds")
	for key in ["RebuildWhenDead","UseSpawnTimerWithoutWorker","DisallowRebuildRange","DisallowRebuildFilter"]:
		if fields.has(key):receipts.append("unsupported_construction_semantic:%s"%key)
	row["getting_built_behavior"]={"spawn_disabled":bool(spawn.get("disabled",false)),"spawn_ticks":int(spawn.get("ticks",-1)),"rebuild_ticks":int(rebuild.get("ticks",-1)),"rebuild_when_dead":bool(_sim._module_contract_value(fields,"RebuildWhenDead",false)),"use_timer_without_worker":bool(_sim._module_contract_value(fields,"UseSpawnTimerWithoutWorker",false)),"disallow_range_source":float(_sim._module_contract_value(fields,"DisallowRebuildRange",0.0)),"disallow_filter":_sim._typed_contract_tokens(fields,"DisallowRebuildFilter"),"unsupported_semantics":receipts}


func _resolve_build_seconds(value:Variant)->Dictionary:
	var _sim = sim
	if typeof(value)!=TYPE_DICTIONARY:return {"resolved":false,"disabled":false}
	var field:=value as Dictionary
	if bool(field.get("disabled",false)):return {"resolved":true,"disabled":true,"ticks":-1}
	if field.has("milliseconds"):return {"resolved":true,"disabled":false,"ticks":_sim._ship_contract_delay_ticks(float(field.get("milliseconds",0.0)))}
	var name:=String(field.get("define",""));var defines:=_sim._rules.get("getting_built_time_defines",{}) as Dictionary
	if typeof(defines.get(name)) in [TYPE_INT,TYPE_FLOAT]:return {"resolved":true,"disabled":false,"ticks":_sim._ship_contract_delay_ticks(float(defines[name])*1000.0)}
	return {"resolved":false,"disabled":false,"ticks":-1}


func _attach_building_behavior_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed" or row.has("building_behavior"):return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[]
	for key in ["NightWindowName","FireWindowName","GlowWindowName","FireName"]:
		if fields.has(key):receipts.append("model_subobject_binding:%s"%key)
	var fire_names:Array[String]=[]
	for fire_value in fields.get("FireName",[]) as Array:
		var name:=String((fire_value as Dictionary).get("value","")).strip_edges()
		if name!="":fire_names.append(name)
	row["building_behavior"]={
		"night_windows":_sim._typed_contract_tokens(fields,"NightWindowName"),
		"fire_windows":_sim._typed_contract_tokens(fields,"FireWindowName"),
		"glow_windows":_sim._typed_contract_tokens(fields,"GlowWindowName"),
		"fire_names":fire_names,
		# Rendering/model visibility is outside the deterministic sim.  Preserve
		# every authored ordered binding and emit an explicit consumer receipt.
		"unsupported_semantics":receipts,
	}


func _attach_queue_production_exit_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed" or row.has("queue_production_exit_update"):return
	var fields:=contract.get("fields",{}) as Dictionary;var points:Array[Vector2]=[];var rallies:Array[Vector2]=[];var receipts:Array[String]=[]
	for value in fields.get("UnitCreatePoint",[]) as Array:
		var point =value as Dictionary
		if not bool(point.get("validNumeric",false)) or typeof(point.get("value"))!=TYPE_DICTIONARY:
			receipts.append("invalid_numeric_coordinate:UnitCreatePoint");continue
		var coord:=point.get("value",{}) as Dictionary;points.append(Vector2(float(coord.get("x",0.0)),float(coord.get("y",0.0))))
	for value in fields.get("NaturalRallyPoint",[]) as Array:
		var point =value as Dictionary
		if not bool(point.get("validNumeric",false)) or typeof(point.get("value"))!=TYPE_DICTIONARY:
			receipts.append("invalid_numeric_coordinate:NaturalRallyPoint");continue
		var coord:=point.get("value",{}) as Dictionary;rallies.append(Vector2(float(coord.get("x",0.0)),float(coord.get("y",0.0))))
	var delays:Array[int]=[]
	for value in fields.get("ExitDelay",[]) as Array:
		var delay:=value as Dictionary
		if typeof(delay.get("milliseconds")) in [TYPE_INT,TYPE_FLOAT]:delays.append(_sim._ship_contract_delay_ticks(float(delay.get("milliseconds",0.0))))
		else:
			var expression:=String(delay.get("expression",""));var defines:=_sim._rules.get("queue_exit_time_defines",{}) as Dictionary
			if typeof(defines.get(expression)) in [TYPE_INT,TYPE_FLOAT]:delays.append(_sim._ship_contract_delay_ticks(float(defines[expression])))
			else:receipts.append("unresolved_exit_delay:%s"%expression)
	if fields.has("AllowAirborneCreation"):receipts.append("presentation_or_movement_binding:AllowAirborneCreation")
	if fields.has("UseReturnToFormation"):receipts.append("presentation_or_movement_binding:UseReturnToFormation")
	var executable:=not points.is_empty() and not receipts.any(func(receipt:String)->bool:return receipt.begins_with("invalid_numeric_coordinate:") or receipt.begins_with("unresolved_exit_delay:"))
	if fields.has("InitialBurst"):receipts.append("unsupported_exit_semantic:InitialBurst")
	row["queue_production_exit_update"]={"executable":executable,"create_points_source":points,"rally_points_source":rallies,"placement_angles":_typed_contract_numbers(fields,"PlacementViewAngle"),"exit_delay_ticks":delays,"initial_burst":int(_sim._module_contract_value(fields,"InitialBurst",0)),"no_exit_path":bool(_sim._module_contract_value(fields,"NoExitPath",false)),"next_index":0,"unsupported_semantics":receipts}


func _attach_rebuild_hole_expose_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("rebuild_hole_expose"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var hole_name = String(_sim._module_contract_value(fields, "HoleName", ""))
	var hole_health = int(round(float(_sim._module_contract_value(fields, "HoleMaxHealth", 0.0))))
	if hole_name == "" or hole_health <= 0:
		return
	var transfer_attackers = bool(_sim._module_contract_value(fields, "TransferAttackers", false))
	var fade_in_ticks = maxi(0, _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields, "FadeInTimeSeconds", 0.0)) * 1000.0))
	var receipts: Array[String] = []
	if fade_in_ticks > 0:
		receipts.append("presentation_binding:FadeInTimeSeconds")
	if transfer_attackers:
		receipts.append("unsupported_rebuild_semantic:TransferAttackers")
	row["rebuild_hole_expose"] = {
		"hole_object_id": hole_name,
		"hole_max_health": hole_health,
		"fade_in_ticks": fade_in_ticks,
		"exempt_statuses": _sim._typed_contract_tokens(fields, "ExemptStatus"),
		"transfer_attackers": transfer_attackers,
		"unsupported_semantics": receipts,
		"exposed": false,
	}


func _attach_rebuild_hole_behavior_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("rebuild_hole_behavior"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var delay_value := fields.get("WorkerRespawnDelay", {}) as Dictionary
	var regen_value := fields.get("HoleHealthRegen%PerSecond", {}) as Dictionary
	if typeof(delay_value.get("milliseconds")) not in [TYPE_INT, TYPE_FLOAT] or typeof(regen_value.get("ratio")) not in [TYPE_INT, TYPE_FLOAT]:
		return
	row["rebuild_hole_behavior"] = {
		"worker_object_id": String(_sim._module_contract_value(fields, "WorkerObjectName", "")),
		"respawn_ticks": _sim._ship_contract_delay_ticks(float(delay_value.get("milliseconds", 0.0))),
		"health_regen_ratio_per_second": float(regen_value.get("ratio", 0.0)),
		"regen_remainder": 0.0,
	}


func _expose_rebuild_hole(owner_id: int, owner: Dictionary, attacker_id: int) -> int:
	var _sim = sim
	var policy := owner.get("rebuild_hole_expose", {}) as Dictionary
	if policy.is_empty() or bool(policy.get("exposed", false)):
		return -1
	if bool(policy.get("transfer_attackers", false)):
		_sim._emit_event("rebuild_hole.expose_refused", attacker_id, owner_id, {"reason": "TransferAttackers-unresolved"})
		return -1
	var statuses: Array[String] = []
	var status_value: Variant = owner.get("object_status", [])
	if typeof(status_value) == TYPE_ARRAY:
		for value in status_value as Array: statuses.append(String(value).to_upper())
	elif typeof(status_value) == TYPE_DICTIONARY:
		for value in (status_value as Dictionary).keys():
			if bool((status_value as Dictionary)[value]): statuses.append(String(value).to_upper())
	for status in policy.get("exempt_statuses", []) as Array:
		if statuses.has(String(status).to_upper()):
			return -1
	var hole_id = _sim.spawn_scenario_structure(
		String(policy.get("hole_object_id", "")), int(owner.get("team", -1)),
		Vector2(owner.get("position", Vector2.ZERO)), "object-creation-list"
	)
	if hole_id <= 0 or not _sim.structures.has(hole_id):
		_sim._emit_event("rebuild_hole.expose_refused", attacker_id, owner_id, {"hole_object_id": String(policy.get("hole_object_id", ""))})
		return -1
	var hole := _sim.structures[hole_id] as Dictionary
	var maximum := int(policy.get("hole_max_health", 0))
	hole["maximum_health"] = maximum
	hole["health"] = maximum
	hole["rebuild_owner_object_id"] = String(owner.get("source_object_id", owner.get("object_id", "")))
	hole["rebuild_owner_team"] = int(owner.get("team", -1))
	hole["rebuild_owner_id"] = owner_id
	hole["rebuild_owner_position"] = Vector2(owner.get("position", Vector2.ZERO))
	hole["rebuild_transfer_attacker_id"] = 0
	var rebuild := hole.get("rebuild_hole_behavior", {}) as Dictionary
	if not rebuild.is_empty():
		rebuild["respawn_tick"] = _sim.tick_index + int(rebuild.get("respawn_ticks", 0))
		hole["rebuild_hole_behavior"] = rebuild
	policy["exposed"] = true
	policy["hole_id"] = hole_id
	owner["rebuild_hole_expose"] = policy
	_sim._emit_event("rebuild_hole.exposed", owner_id, hole_id, {"hole_object_id": String(policy.get("hole_object_id", "")), "maximum_health": maximum})
	return hole_id


func _step_rebuild_holes() -> void:
	var _sim = sim
	for hole_id in _sim.structure_ids():
		if not _sim.structures.has(hole_id):
			continue
		var hole := _sim.structures[hole_id] as Dictionary
		var policy := hole.get("rebuild_hole_behavior", {}) as Dictionary
		if policy.is_empty() or int(hole.get("health", 0)) <= 0 or not hole.has("rebuild_owner_object_id"):
			continue
		var maximum := maxi(1, int(hole.get("maximum_health", 1)))
		var regen = float(policy.get("health_regen_ratio_per_second", 0.0)) * float(maximum) * _sim.TICK_SECONDS + float(policy.get("regen_remainder", 0.0))
		var applied := floori(regen)
		policy["regen_remainder"] = regen - float(applied)
		if applied > 0:
			hole["health"] = mini(maximum, int(hole.get("health", 0)) + applied)
		if _sim.tick_index < int(policy.get("respawn_tick", _sim.tick_index + 1)):
			hole["rebuild_hole_behavior"] = policy
			continue
		var rebuilt_id = _sim.spawn_scenario_structure(
			String(hole.get("rebuild_owner_object_id", "")), int(hole.get("rebuild_owner_team", -1)),
			Vector2(hole.get("rebuild_owner_position", hole.get("position", Vector2.ZERO))), "lair-spawn"
		)
		if rebuilt_id <= 0:
			policy["respawn_refused"] = "owner-scenario-admission-rejected"
			hole["rebuild_hole_behavior"] = policy
			continue
		_sim.structures.erase(hole_id)
		_sim._emit_event("rebuild_hole.rebuilt", hole_id, rebuilt_id, {"object_id": String(hole.get("rebuild_owner_object_id", "")), "worker_object_id": String(policy.get("worker_object_id", ""))})


func _typed_contract_numbers(fields:Dictionary,key:String)->Array[float]:
	var output:Array[float]=[]
	for value in fields.get(key,[]) as Array:
		var item:=value as Dictionary
		if typeof(item.get("value")) in [TYPE_INT,TYPE_FLOAT]:output.append(float(item.get("value")))
	return output


func _attach_banner_carrier_update_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed" or row.has("banner_carrier_update"):return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[]
	for key in ["BannerMorphFX","UnitSpawnFX","MorphCondition"]:
		if fields.has(key):receipts.append("presentation_binding:%s"%key)
	if fields.has("MeleeFreeUnitSpawnTime"):receipts.append("unsupported_banner_semantic:MeleeFreeUnitSpawnTime")
	var scan:=_resolve_respawn_body_expression(fields.get("ScanHordeDistance"))
	if fields.has("ScanHordeDistance") and not bool(scan.get("resolved",false)):receipts.append("unresolved_banner_scan_expression")
	row["banner_carrier_update"]={"idle_spawn_ticks":maxi(1,_sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields,"IdleSpawnRate",0.0)))),"melee_unit_spawn_ticks":_sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields,"MeleeFreeUnitSpawnTime",0.0))),"has_respawn_timer":fields.has("DiedRespawnTime") or fields.has("MeleeFreeBannerRespawnTime"),"died_respawn_ticks":_sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields,"DiedRespawnTime",0.0))),"melee_banner_respawn_ticks":_sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields,"MeleeFreeBannerRespawnTime",0.0))),"upgrade_required":String(_sim._module_contract_value(fields,"UpgradeRequired","")),"replenish_nearby":bool(_sim._module_contract_value(fields,"ReplenishNearbyHorde",false)),"replenish_all":bool(_sim._module_contract_value(fields,"ReplenishAllNearbyHordes",false)),"scan_range_source":float(scan.get("value",0.0)) if bool(scan.get("resolved",false)) else -1.0,"next_replenish_tick":_sim.tick_index,"unsupported_semantics":receipts}


func _resolve_respawn_body_expression(field:Variant)->Dictionary:
	if typeof(field)!=TYPE_DICTIONARY:return {"resolved":false}
	var value:=field as Dictionary
	if typeof(value.get("value")) in [TYPE_INT,TYPE_FLOAT]:return {"resolved":true,"value":float(value.get("value"))}
	var name:=String(value.get("define",""));var defines:=sim._rules.get("respawn_body_defines",{}) as Dictionary
	if typeof(defines.get(name)) in [TYPE_INT,TYPE_FLOAT]:return {"resolved":true,"value":float(defines[name])}
	return {"resolved":false,"define":name}


func _attach_respawn_body_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed" or row.has("respawn_body"):return
	var fields:=contract.get("fields",{}) as Dictionary;var maximum:=_resolve_respawn_body_expression(fields.get("MaxHealth"));var recovery:=_resolve_respawn_body_expression(fields.get("RecoveryTime"));var damaged:=_resolve_respawn_body_expression(fields.get("MaxHealthDamaged"));var receipts:Array[String]=[]
	if not bool(maximum.get("resolved",false)):receipts.append("unresolved_body_define:MaxHealth:%s"%String(maximum.get("define","")))
	if fields.has("RecoveryTime") and not bool(recovery.get("resolved",false)):receipts.append("unresolved_body_define:RecoveryTime:%s"%String(recovery.get("define","")))
	for key in ["BurningDeathFX","HealingBuffFX","MaxHealthDamaged"]:
		if fields.has(key):receipts.append("presentation_binding:%s"%key)
	if fields.has("DodgePercent"):receipts.append("unsupported_combat_semantic:DodgePercent")
	if fields.has("BurningDeathBehavior"):receipts.append("unsupported_death_semantic:BurningDeathBehavior")
	if fields.has("RecoveryTime"):receipts.append("unsupported_body_semantic:RecoveryTime")
	if fields.has("CheerRadius"):receipts.append("presentation_or_audio_binding:CheerRadius")
	row["respawn_body"]={"max_health":int(maximum.get("value",0.0)) if bool(maximum.get("resolved",false)) else -1,"recovery_ticks":_sim._ship_contract_delay_ticks(float(recovery.get("value",0.0))) if bool(recovery.get("resolved",false)) else -1,"max_health_damaged":int(damaged.get("value",0.0)) if bool(damaged.get("resolved",false)) else -1,"can_respawn":bool(_sim._module_contract_value(fields,"CanRespawn",true)),"permanently_killed_filter":_sim._typed_contract_tokens(fields,"PermanentlyKilledByFilter"),"cheer_radii":_typed_contract_numbers(fields,"CheerRadius"),"unsupported_semantics":receipts}
	if int((row["respawn_body"] as Dictionary).get("max_health",-1))>0:
		var member_health:=row.get("member_health",[]) as Array;var old_member_max:=maxi(1,int(row.get("member_maximum_health",1)));var new_member_max:=int((row["respawn_body"] as Dictionary).get("max_health"));var total:=0
		for index in member_health.size():member_health[index]=mini(new_member_max,roundi(float(member_health[index])*float(new_member_max)/float(old_member_max)));total+=int(member_health[index])
		row["member_health"]=member_health;row["member_maximum_health"]=new_member_max;row["maximum_health"]=new_member_max*member_health.size();row["health"]=total


func _resolve_contract_milliseconds(field:Variant,define_key:String)->Dictionary:
	var _sim = sim
	if typeof(field)!=TYPE_DICTIONARY:return {"resolved":false}
	var value:=field as Dictionary
	if typeof(value.get("milliseconds")) in [TYPE_INT,TYPE_FLOAT]:return {"resolved":true,"ticks":_sim._ship_contract_delay_ticks(float(value.get("milliseconds")))}
	var expression:=String(value.get("expression",value.get("define","")));var defines:=_sim._rules.get(define_key,{}) as Dictionary
	if typeof(defines.get(expression)) in [TYPE_INT,TYPE_FLOAT]:return {"resolved":true,"ticks":_sim._ship_contract_delay_ticks(float(defines[expression]))}
	return {"resolved":false,"expression":expression}


func _attach_give_upgrade_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed":return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[];var phases:Dictionary={}
	for key in ["UnpackTime","PreparationTime","PersistentPrepTime","PackTime"]:
		var resolved:=_resolve_contract_milliseconds(fields.get(key),"give_upgrade_time_defines")
		if not bool(resolved.get("resolved",false)):receipts.append("unresolved_upgrade_time:%s:%s"%[key,String(resolved.get("expression",""))])
		else:phases[key]=int(resolved.get("ticks",0))
	for key in ["SpawnOutFX","FadeOutSpeed"]:
		if fields.has(key):receipts.append("presentation_binding:%s"%key)
	if bool(_sim._module_contract_value(fields,"ApproachRequiresLOS",false)):receipts.append("unsupported_targeting_semantic:ApproachRequiresLOS")
	var rows:=row.get("give_upgrade_updates",[]) as Array
	rows.append({"special_power":String(_sim._module_contract_value(fields,"SpecialPowerTemplate","")),"range_source":float(_sim._module_contract_value(fields,"StartAbilityRange",0.0)),"deliver_upgrade":bool(_sim._module_contract_value(fields,"DeliverUpgrade",false)),"phase_ticks":phases,"unsupported_semantics":receipts,"tag":String(contract.get("tag","")),"line":int(contract.get("line",0))});row["give_upgrade_updates"]=rows


func request_give_upgrade(source_id:int,target_id:int,upgrade_id:String,special_power:String="")->Dictionary:
	var _sim = sim
	if not _sim.entities.has(source_id):return {"ok":false,"reason":"source-missing"}
	var target_table =_sim.structures if _sim.structures.has(target_id) else _sim.entities
	if not target_table.has(target_id):return {"ok":false,"reason":"target-missing"}
	var source:=_sim.entities[source_id] as Dictionary
	if not source.has("give_upgrade_updates"):_sim._attach_module_contracts(source)
	if source.has("give_upgrade_action"):return {"ok":false,"reason":"upgrade-delivery-busy"}
	var selected:Dictionary={}
	for value in source.get("give_upgrade_updates",[]) as Array:
		var policy:=value as Dictionary
		if special_power=="" or String(policy.get("special_power",""))==special_power:selected=policy;break
	if selected.is_empty():return {"ok":false,"reason":"typed-give-upgrade-contract-missing"}
	if not (selected.get("unsupported_semantics",[]) as Array).filter(func(v):return String(v).begins_with("unresolved_upgrade_time:")).is_empty():return {"ok":false,"reason":"unresolved-upgrade-timing"}
	var target:=target_table[target_id] as Dictionary
	if int(target.get("team",-1))!=int(source.get("team",-1)):return {"ok":false,"reason":"target-not-allied"}
	var distance:=Vector2(source.get("position",Vector2.ZERO)).distance_to(Vector2(target.get("position",Vector2.ZERO)));var range_sim:=float(selected.get("range_source",0.0))*float(_sim._rules.get("source_unit_scale",0.1))
	if distance>range_sim:return {"ok":false,"reason":"out-of-range"}
	if upgrade_id.strip_edges()=="" or not bool(selected.get("deliver_upgrade",false)):return {"ok":false,"reason":"upgrade-delivery-disabled"}
	var phases:=selected.get("phase_ticks",{}) as Dictionary;var delivery_tick =_sim.tick_index+int(phases.get("UnpackTime",0))+int(phases.get("PreparationTime",0))+int(phases.get("PersistentPrepTime",0))
	source["give_upgrade_action"]={"target_id":target_id,"target_kind":"structure" if _sim.structures.has(target_id) else "entity","upgrade_id":upgrade_id,"delivery_tick":delivery_tick,"complete_tick":delivery_tick+int(phases.get("PackTime",0))};return {"ok":true,"reason":"","delivery_tick":delivery_tick}


func _step_give_upgrade_updates()->void:
	var _sim = sim
	for source_id in _sim.entity_ids():
		var source:=_sim.entities[source_id] as Dictionary;var action:=source.get("give_upgrade_action",{}) as Dictionary
		if action.is_empty():continue
		if int(source.get("health",0))<=0:source.erase("give_upgrade_action");continue
		if not bool(action.get("delivered",false)) and _sim.tick_index>=int(action.get("delivery_tick",0)):
			var table =_sim.structures if String(action.get("target_kind"))=="structure" else _sim.entities;var target_id:=int(action.get("target_id",0))
			if table.has(target_id):
				var target:=table[target_id] as Dictionary
				if int(target.get("team",-1))==int(source.get("team",-2)):
					var upgrades:=target.get("completed_upgrades",[]) as Array;var upgrade:=String(action.get("upgrade_id",""));if not upgrades.has(upgrade):upgrades.append(upgrade);target["completed_upgrades"]=upgrades;_sim._emit_event("upgrade.delivered",source_id,target_id,{"upgrade_id":upgrade})
			action["delivered"]=true;source["give_upgrade_action"]=action
		if _sim.tick_index>=int(action.get("complete_tick",0)):source.erase("give_upgrade_action")


func _attach_gate_open_close_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed" or row.has("gate_behavior"):return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[]
	for key in ["SoundOpeningGateLoop","SoundClosingGateLoop","SoundFinishedOpeningGate","SoundFinishedClosingGate","TimeBeforePlayingOpenSound","TimeBeforePlayingClosedSound"]:
		if fields.has(key):receipts.append("audio_binding:%s"%key)
	var opened=bool(_sim._module_contract_value(fields,"OpenByDefault",false));row["gate_behavior"]={"open":opened,"pathing_open":opened,"open_fraction":1.0 if opened else 0.0,"reset_ticks":_sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields,"ResetTimeInMilliseconds",0.0))),"pathing_threshold":float(_sim._module_contract_value(fields,"PercentOpenForPathing",100.0))/100.0,"repel_colliding":bool(_sim._module_contract_value(fields,"RepelCollidingUnits",false)),"close_tick":-1,"unsupported_semantics":receipts}


func _attach_ai_gate_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed" or row.has("ai_gate_update"):return
	var fields=contract.get("fields",{}) as Dictionary;row["ai_gate_update"]={"trigger_width_source":Vector2(float(_sim._module_contract_value(fields,"TriggerWidthX",0.0)),float(_sim._module_contract_value(fields,"TriggerWidthY",0.0)))}


func _attach_fake_pathfind_portal_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed" or row.has("fake_pathfind_portal"):return
	var fields=contract.get("fields",{}) as Dictionary;row["fake_pathfind_portal"]={"allow_enemies":bool(_sim._module_contract_value(fields,"AllowEnemies",false)),"allow_non_skirmish_ai":bool(_sim._module_contract_value(fields,"AllowNonSkirmishAIUnits",false))}


func request_gate_open(structure_id:int,requester_id:int=0)->Dictionary:
	var _sim = sim
	if not _sim.structures.has(structure_id):return {"ok":false,"reason":"gate-missing"}
	var gate:=_sim.structures[structure_id] as Dictionary
	if not gate.has("gate_behavior"):_sim._attach_structure_module_contracts(gate)
	var policy:=gate.get("gate_behavior",{}) as Dictionary
	if policy.is_empty():return {"ok":false,"reason":"typed-gate-contract-missing"}
	if requester_id!=0 and not gate_portal_allows(structure_id,requester_id):return {"ok":false,"reason":"portal-denied"}
	policy["open"]=true;policy["open_fraction"]=1.0;policy["pathing_open"]=true;policy["close_tick"]=_sim.tick_index+int(policy.get("reset_ticks",0));gate["gate_behavior"]=policy;_sim._emit_event("gate.opened",requester_id,structure_id,{"close_tick":policy["close_tick"]});return {"ok":true,"reason":"","close_tick":policy["close_tick"]}
	_sync_gate_passage(structure_id)


func _sync_gate_passage(structure_id: int) -> void:
	## The gate's pathing state mirrored onto the ground grid (see
	## RetailMapData.set_gate_passage): open gates are passages through the
	## painted wall band; closed gates seal them again. Called at seeding and on
	## every pathing_open transition (open, timed close, manual close, death).
	var _sim = sim
	if _sim.route_provider == null or not _sim.route_provider.has_method("set_gate_passage"):
		return
	if not _sim.structures.has(structure_id):
		return
	var gate: Dictionary = _sim.structures[structure_id]
	var policy: Dictionary = gate.get("gate_behavior", {})
	if policy.is_empty():
		return
	var geometries: Dictionary = gate.get("gate_geometries", {})
	var closed: Dictionary = geometries.get("Closed", {})
	# The authored closed box: MajorRadius along the facing (door thickness),
	# MinorRadius across it (the passage half-width).
	var half_width := float(closed.get("minorRadius", 0.0))
	if half_width <= 0.0:
		return
	var open := bool(policy.get("pathing_open", false)) or int(gate.get("health", 0)) <= 0
	var result: Dictionary = _sim.route_provider.call("set_gate_passage", structure_id, Vector2(gate.get("position", Vector2.ZERO)), half_width, open)
	if bool(result.get("changed", false)):
		_sim._emit_event("gate.passage", 0, structure_id, {"open": open, "cells": int(result.get("cells", 0)), "depth_fwd": result.get("depth_fwd"), "depth_back": result.get("depth_back")})


func gate_portal_allows(structure_id:int,requester_id:int)->bool:
	## The gate's AUTO-OPEN policy (AIGateUpdate rectangle / manual toggle):
	## pure geometry + ownership. FakePathfindPortalBehaviour's rules restrict
	## the PATHING PORTAL and live in _castle_gate_blocking_discs, not here —
	## a human owner's own gate always swings for his troops.
	var _sim = sim
	if not _sim.structures.has(structure_id) or not _sim.entities.has(requester_id):return false
	var gate:=_sim.structures[structure_id] as Dictionary;var requester:=_sim.entities[requester_id] as Dictionary
	return int(requester.get("team",-1))==int(gate.get("team",-1))


func _step_gate_updates()->void:
	var _sim = sim
	for structure_id in _sim.structure_ids():
		var gate:=_sim.structures[structure_id] as Dictionary;var policy:=gate.get("gate_behavior",{}) as Dictionary
		if policy.is_empty():continue
		var ai:=gate.get("ai_gate_update",{}) as Dictionary
		if not ai.is_empty():
			var half:=Vector2(ai.get("trigger_width_source",Vector2.ZERO))*float(_sim._rules.get("source_map_transform_scale",0.1))*0.5;var origin:=Vector2(gate.get("position",Vector2.ZERO))
			for id in _sim.entity_ids():
				var unit:=_sim.entities[id] as Dictionary;var delta =Vector2(unit.get("position",Vector2.ZERO))-origin
				if int(unit.get("team",-1))==int(gate.get("team",-1)) and absf(delta.x)<=half.x and absf(delta.y)<=half.y:request_gate_open(structure_id,id);break
		if bool(policy.get("open",false)) and int(policy.get("close_tick",-1))>=0 and _sim.tick_index>=int(policy.get("close_tick")):
			policy["open"]=false;policy["pathing_open"]=false;policy["open_fraction"]=0.0;policy["close_tick"]=-1;gate["gate_behavior"]=policy;_sim._emit_event("gate.closed",structure_id,0)
			_sync_gate_passage(structure_id)


func _typed_effect_graph(contract: Dictionary, kind: String, target_mode: String) -> Dictionary:
	var graph_value: Variant = contract.get("effectGraph", contract.get("effect_graph", null))
	if typeof(graph_value) != TYPE_DICTIONARY:
		return {}
	var graph := graph_value as Dictionary
	if String(graph.get("kind", "")) != kind or String(graph.get("targetMode", "")) != target_mode:
		return {}
	return graph


func _attach_stop_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	## StopSpecialPower is a self command which cancels one exact, compiler-linked
	## special-power template.  The consumer never guesses a target from a command
	## name: both field ids must agree with the typed effect graph.
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed":
		return
	var graph := _typed_effect_graph(contract, "stop-special-power", "SELF")
	var fields := contract.get("fields", {}) as Dictionary
	var own_template = String(_sim._module_contract_value(fields, "SpecialPowerTemplate", "")).strip_edges()
	var stop_template = String(_sim._module_contract_value(fields, "StopPowerTemplate", "")).strip_edges()
	if (
		graph.is_empty() or own_template == "" or stop_template == ""
		or String(graph.get("specialPowerTemplateId", "")) != own_template
		or String(graph.get("stopPowerTemplateId", "")) != stop_template
		or not bool(graph.get("interruptsCurrentOrder", false))
		or typeof(graph.get("linkedModule")) != TYPE_DICTIONARY
	):
		return
	var linked := graph.get("linkedModule", {}) as Dictionary
	if String(linked.get("kind", "")).strip_edges() == "" or String(linked.get("sourceIni", "")).strip_edges() == "" or int(linked.get("line", 0)) <= 0:
		return
	var policies := row.get("stop_special_powers", []) as Array
	var key := "%s:%d" % [String(contract.get("tag", "")), int(contract.get("line", 0))]
	for existing_value in policies:
		if String((existing_value as Dictionary).get("key", "")) == key:
			return
	policies.append({
		"key": key,
		"special_power_template": own_template,
		"stop_power_template": stop_template,
		"interrupts_current_order": true,
		"linked_module": linked.duplicate(true),
		"unsupported_semantics": [],
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	})
	row["stop_special_powers"] = policies


func _active_special_power_channel_key(row: Dictionary, target_template: String) -> String:
	## These are the authoritative scheduled channels which record a template id.
	## A subsystem without that identity is deliberately invisible to Stop: a
	## guessed ability-id/template mapping could cancel the wrong retail power.
	for key in [
		"activate_module_channel", "dominate_enemy_channel", "grab_passenger_channel",
		"fling_passenger_channel", "repair_structure_channel", "siege_deploy_channel",
	]:
		var channel_value: Variant = row.get(key)
		if typeof(channel_value) != TYPE_DICTIONARY:
			continue
		var channel := channel_value as Dictionary
		if String(channel.get("special_power_template_id", "")) == target_template:
			return key
	return ""


func activate_stop_special_power(entity_id: int, special_power_template: String, team: int = -1) -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row := _sim.entities[entity_id] as Dictionary
	if team >= 0 and int(row.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "unit-defeated"}
	if not row.has("stop_special_powers"):
		_sim._attach_module_contracts(row)
	var policy: Dictionary = {}
	for policy_value in row.get("stop_special_powers", []) as Array:
		var candidate = policy_value as Dictionary
		if String(candidate.get("special_power_template", "")) == special_power_template:
			policy = candidate
			break
	if policy.is_empty():
		return {"ok": false, "reason": "typed-stop-special-power-missing"}
	if not (policy.get("unsupported_semantics", []) as Array).is_empty():
		return {"ok": false, "reason": "unsupported-semantics", "receipts": policy.get("unsupported_semantics")}
	var stop_template := String(policy.get("stop_power_template", ""))
	var channel_key := _active_special_power_channel_key(row, stop_template)
	if channel_key == "":
		return {"ok": false, "reason": "stop-power-not-active", "stop_power_template": stop_template}
	var stopped_channel := (row.get(channel_key, {}) as Dictionary).duplicate(true)
	if channel_key == "siege_deploy_channel":
		if String(stopped_channel.get("phase", "")) == "retracting":
			return {"ok": false, "reason": "stop-power-not-active", "stop_power_template": stop_template}
		stopped_channel["phase"] = "retracting"
		stopped_channel["phase_end_tick"] = _sim.tick_index + int(stopped_channel.get("raise_delay_ticks", 0))
		row[channel_key] = stopped_channel
	else:
		row.erase(channel_key)
	if bool(policy.get("interrupts_current_order", false)):
		_sim._clear_pending_route(row, true)
		_sim._clear_member_targets(row)
		row["target_id"] = 0
		row["target_kind"] = ""
		row["attack_move"] = false
		row["order_kind"] = ""
		row["state"] = "ability" if channel_key == "siege_deploy_channel" else "idle"
	_sim._emit_event("ability.special_power_stopped", entity_id, int(stopped_channel.get("current_target_id", stopped_channel.get("target_id", 0))), {
		"special_power_template": special_power_template,
		"stop_power_template": stop_template,
		"channel": channel_key,
		"linked_module": policy.get("linked_module", {}),
	})
	return {"ok": true, "reason": "", "stop_power_template": stop_template, "channel": channel_key}


func _attach_unleash_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	## The stable graph proves a single ObjectCreationUpgrade/SlaveWatcher chain.
	## Only retail's zero-time, instant release is executable here; the schema also
	## accepts other timings and XP awards, which remain explicit receipts.
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed":
		return
	var graph := _typed_effect_graph(contract, "unleash-special-power", "SELF_OWNED_SLAVE")
	var fields := contract.get("fields", {}) as Dictionary
	var template = String(_sim._module_contract_value(fields, "SpecialPowerTemplate", "")).strip_edges()
	var unpack_ms = int(_sim._module_contract_value(fields, "UnpackTime", -1))
	var award_xp = float(_sim._module_contract_value(fields, "AwardXPForTriggering", -1.0))
	var instant_value: Variant = _sim._module_contract_value(fields, "Instant", null)
	var timing := graph.get("timingMs", {}) as Dictionary
	var gate_value: Variant = graph.get("creationGateUpgradeIds")
	var watcher_value: Variant = graph.get("slaveWatcher")
	if (
		graph.is_empty() or template == "" or unpack_ms < 0 or award_xp < 0.0 or typeof(instant_value) != TYPE_BOOL
		or String(graph.get("specialPowerTemplateId", "")) != template
		or int(timing.get("UnpackTime", -1)) != unpack_ms
		or float(graph.get("awardXpForTriggering", -1.0)) != award_xp
		or bool(graph.get("instant", not bool(instant_value))) != bool(instant_value)
		or String(graph.get("spawnedObjectId", "")).strip_edges() == ""
		or typeof(gate_value) != TYPE_ARRAY or (gate_value as Array).is_empty()
		or typeof(watcher_value) != TYPE_DICTIONARY
	):
		return
	var gates: Array[String] = []
	for gate_value_item in gate_value as Array:
		var gate := String(gate_value_item).strip_edges()
		if gate == "":
			return
		gates.append(gate)
	var watcher := watcher_value as Dictionary
	if String(watcher.get("removeUpgradeId", "")).strip_edges() == "" or String(watcher.get("grantUpgradeId", "")).strip_edges() == "" or String(watcher.get("sourceIni", "")).strip_edges() == "" or int(watcher.get("line", 0)) <= 0:
		return
	var unsupported: Array[String] = []
	if unpack_ms != 0:
		unsupported.append("nonzero-unpack-time")
	if not bool(instant_value):
		unsupported.append("non-instant-release")
	if not is_zero_approx(award_xp):
		unsupported.append("nonzero-award-xp")
	var policies := row.get("unleash_special_powers", []) as Array
	var key := "%s:%d" % [String(contract.get("tag", "")), int(contract.get("line", 0))]
	for existing_value in policies:
		if String((existing_value as Dictionary).get("key", "")) == key:
			return
	policies.append({
		"key": key,
		"special_power_template": template,
		"unpack_ticks": _sim._ship_contract_delay_ticks(float(unpack_ms)),
		"award_xp": award_xp,
		"instant": bool(instant_value),
		"spawned_object_id": String(graph.get("spawnedObjectId", "")),
		"creation_gate_upgrade_ids": gates,
		"slave_watcher": watcher.duplicate(true),
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	})
	row["unleash_special_powers"] = policies


func _unleash_owned_slave(owner_id: int, owner: Dictionary, policy: Dictionary) -> int:
	var _sim = sim
	var expected_object := String(policy.get("spawned_object_id", ""))
	var required_gates := policy.get("creation_gate_upgrade_ids", []) as Array
	for creation_value in owner.get("object_creation_upgrades", []) as Array:
		var creation := creation_value as Dictionary
		if String(creation.get("thing_to_spawn", "")) != expected_object:
			continue
		var triggers := creation.get("triggers", []) as Array
		var gate_match := true
		for gate_value in required_gates:
			if not triggers.has(String(gate_value)):
				gate_match = false
				break
		if not gate_match:
			continue
		for slave_value in creation.get("spawned_ids", []) as Array:
			var slave_id := int(slave_value)
			if not _sim.entities.has(slave_id):
				continue
			var slave := _sim.entities[slave_id] as Dictionary
			var identity := String(slave.get("source_object_id", slave.get("object_id", slave.get("unit_type", ""))))
			var slaved := slave.get("slaved_update", {}) as Dictionary
			if identity == expected_object and int(slave.get("health", 0)) > 0 and int(slave.get("team", -1)) == int(owner.get("team", -2)) and int(slaved.get("master_id", 0)) == owner_id:
				return slave_id
	return 0


func activate_unleash_special_power(owner_id: int, special_power_template: String, team: int = -1) -> Dictionary:
	var _sim = sim
	var table = _sim.structures if _sim.structures.has(owner_id) else _sim.entities
	if not table.has(owner_id):
		return {"ok": false, "reason": "owner-missing"}
	var owner := table[owner_id] as Dictionary
	if team >= 0 and int(owner.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(owner.get("health", 0)) <= 0:
		return {"ok": false, "reason": "owner-defeated"}
	if not owner.has("unleash_special_powers"):
		if table == _sim.structures: _sim._attach_structure_module_contracts(owner)
		else: _sim._attach_module_contracts(owner)
	var policy: Dictionary = {}
	for policy_value in owner.get("unleash_special_powers", []) as Array:
		var candidate = policy_value as Dictionary
		if String(candidate.get("special_power_template", "")) == special_power_template:
			policy = candidate
			break
	if policy.is_empty():
		return {"ok": false, "reason": "typed-unleash-special-power-missing"}
	if not (policy.get("unsupported_semantics", []) as Array).is_empty():
		return {"ok": false, "reason": "unsupported-semantics", "receipts": policy.get("unsupported_semantics")}
	var slave_id := _unleash_owned_slave(owner_id, owner, policy)
	if slave_id == 0:
		return {"ok": false, "reason": "owned-slave-not-found"}
	var slave := _sim.entities[slave_id] as Dictionary
	var slaved := slave.get("slaved_update", {}) as Dictionary
	slaved["master_id"] = 0
	slaved.erase("master_kind")
	slave["slaved_update"] = slaved
	slave["unselectable"] = false
	slave["ignores_select_all"] = false
	# SlaveWatcher upgrades are death callbacks. Releasing a living slave must not
	# counterfeit that callback or make a replacement immediately purchasable.
	_sim._emit_event("ability.slave_unleashed", owner_id, slave_id, {
		"slave_id": slave_id,
		"special_power_template": special_power_template,
		"spawned_object_id": policy.get("spawned_object_id"),
		"slave_watcher": policy.get("slave_watcher", {}),
	})
	return {"ok": true, "reason": "", "slave_id": slave_id}


func _attach_special_enemy_sense_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("special_enemy_sense"):
		return
	var graph := _typed_effect_graph(contract, "special-enemy-sense", "PERIODIC_ENEMY_RADIUS_SCAN")
	var fields := contract.get("fields", {}) as Dictionary
	var filter_value: Variant = _sim._module_contract_value(fields, "SpecialEnemyFilter", null)
	var range_value: Variant = _sim._module_contract_value(fields, "ScanRange", null)
	var interval_ms_value: Variant = _sim._module_contract_value(fields, "ScanInterval", null)
	if graph.is_empty() or typeof(filter_value) != TYPE_ARRAY or typeof(range_value) not in [TYPE_INT, TYPE_FLOAT] or typeof(interval_ms_value) not in [TYPE_INT, TYPE_FLOAT]:
		return
	var filter: Array[String] = []
	for token_value in filter_value as Array:
		var token := String(token_value).strip_edges()
		if token == "":
			return
		filter.append(token)
	var range_source := float(range_value)
	var interval_ms := float(interval_ms_value)
	if filter.is_empty() or range_source <= 0.0 or interval_ms <= 0.0 or graph.get("specialEnemyFilter", []) != filter or float(graph.get("scanRange", -1.0)) != range_source or float(graph.get("scanIntervalMs", -1.0)) != interval_ms:
		return
	var unsupported: Array[String] = []
	for token in filter:
		var upper := token.to_upper()
		if upper in ["ANY", "ALL", "NONE"] or token.begins_with("+") or token.begins_with("-"):
			continue
		unsupported.append("unsupported-filter-token:%s" % token)
	row["special_enemy_sense"] = {
		"filter": filter,
		"scan_range_source": range_source,
		"scan_interval_ticks": maxi(1, _sim._ship_contract_delay_ticks(interval_ms)),
		"next_scan_tick": _sim.tick_index,
		"sensed_ids": [],
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	}
	row["special_enemy_sense_active"] = false
	row["special_enemy_sensed_ids"] = []


# De-staticed on extraction (instance sim access).
func _special_enemy_sense_filter_accepts(target: Dictionary, filter: Array) -> bool:
	var traits: Dictionary = {}
	for kind_value in target.get("kind_of", []) as Array:
		traits[String(kind_value).to_upper()] = true
	var category := String(target.get("category", "")).to_upper()
	if category != "":
		traits[category] = true
	for key in ["source_object_id", "object_id", "unit_type"]:
		var identity := String(target.get(key, "")).to_upper()
		if identity != "":
			traits[identity] = true
	var positives: Array[String] = []
	var require_all := false
	for token_value in filter:
		var token := String(token_value).to_upper()
		if token == "ALL":
			require_all = true
		elif token.begins_with("-"):
			if traits.has(token.substr(1)):
				return false
		elif token.begins_with("+"):
			positives.append(token.substr(1))
	if positives.is_empty():
		return false
	if require_all:
		for positive in positives:
			if not traits.has(positive):
				return false
		return true
	for positive in positives:
		if traits.has(positive):
			return true
	return false


func _step_special_enemy_sense_updates() -> void:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		var source := _sim.entities[entity_id] as Dictionary
		var policy := source.get("special_enemy_sense", {}) as Dictionary
		if policy.is_empty() or _sim.tick_index < int(policy.get("next_scan_tick", 0)):
			continue
		policy["next_scan_tick"] = _sim.tick_index + maxi(1, int(policy.get("scan_interval_ticks", 1)))
		var desired: Array[int] = []
		if int(source.get("health", 0)) > 0 and (policy.get("unsupported_semantics", []) as Array).is_empty():
			var origin := Vector2(source.get("position", Vector2.ZERO))
			var radius = float(policy.get("scan_range_source", 0.0)) * float(_sim._rules.get("source_unit_scale", 0.1))
			for target_id in _sim.entity_ids():
				if target_id == entity_id:
					continue
				var target := _sim.entities[target_id] as Dictionary
				if int(target.get("health", 0)) <= 0 or not _sim._is_hostile(int(source.get("team", -1)), int(target.get("team", -1))):
					continue
				if origin.distance_to(Vector2(target.get("position", origin))) <= radius and _special_enemy_sense_filter_accepts(target, policy.get("filter", []) as Array):
					desired.append(target_id)
		desired.sort()
		var prior := policy.get("sensed_ids", []) as Array
		policy["sensed_ids"] = desired
		source["special_enemy_sense"] = policy
		source["special_enemy_sensed_ids"] = desired.duplicate()
		source["special_enemy_sense_active"] = not desired.is_empty()
		if prior != desired:
			_sim._emit_event("module.special_enemy_sense_changed", entity_id, int(desired[0]) if not desired.is_empty() else 0, {
				"active": not desired.is_empty(), "sensed_ids": desired.duplicate(),
				"filter": (policy.get("filter", []) as Array).duplicate(),
			})


func _resolve_invisibility_expression(field: Variant, key: String) -> Dictionary:
	if typeof(field) != TYPE_DICTIONARY:
		return {"resolved": false, "expression": ""}
	var value := field as Dictionary
	if typeof(value.get("value")) in [TYPE_INT, TYPE_FLOAT]:
		return {"resolved": true, "value": float(value.get("value")), "expression": String(value.get("expression", value.get("value", "")))}
	var name := String(value.get("define", value.get("name", value.get("expression", ""))))
	var defines := sim._rules.get("invisibility_defines", {}) as Dictionary
	if typeof(defines.get(name)) in [TYPE_INT, TYPE_FLOAT]:
		return {"resolved": true, "value": float(defines[name]), "expression": name}
	return {"resolved": false, "expression": name, "field": key}


func _attach_invisibility_update_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("invisibility_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var nuggets := fields.get("InvisibilityNugget", []) as Array
	if nuggets.size() != 1 or typeof(nuggets[0]) != TYPE_DICTIONARY:
		return
	var nugget := nuggets[0] as Dictionary
	var invisibility_type = String(_sim._module_contract_value(nugget, "InvisibilityType", "")).to_upper()
	var update_ms = float(_sim._module_contract_value(fields, "UpdatePeriod", 0.0))
	if invisibility_type not in ["CAMOUFLAGE", "STEALTH"] or update_ms <= 0.0:
		return
	var detection := _resolve_invisibility_expression(nugget.get("DetectionRange"), "DetectionRange")
	var broadcast_range := _resolve_invisibility_expression(fields.get("BroadcastRange"), "BroadcastRange")
	var broadcast = bool(_sim._module_contract_value(fields, "Broadcast", false))
	var broadcast_filter = _sim._typed_contract_raw_tokens(fields, "BroadcastObjectFilter")
	var broadcast_filter_resolved = not broadcast or (not broadcast_filter.is_empty() and not (broadcast_filter.size() == 1 and not String(broadcast_filter[0]).begins_with("+") and String(broadcast_filter[0]).to_upper() not in ["ANY", "ALL"]))
	var receipts: Array[String] = []
	if nugget.has("DetectionRange") and not bool(detection.get("resolved", false)):
		receipts.append("unresolved_invisibility_define:DetectionRange:%s" % String(detection.get("expression", "")))
	if broadcast and not bool(broadcast_range.get("resolved", false)):
		receipts.append("unresolved_invisibility_define:BroadcastRange:%s" % String(broadcast_range.get("expression", "")))
	if broadcast and (broadcast_filter.is_empty() or (broadcast_filter.size() == 1 and not String(broadcast_filter[0]).begins_with("+") and String(broadcast_filter[0]).to_upper() not in ["ANY", "ALL"])):
		receipts.append("unresolved_broadcast_filter:%s" % (String(broadcast_filter[0]) if not broadcast_filter.is_empty() else "<missing>"))
	var forbidden = _sim._typed_contract_tokens(nugget, "ForbiddenConditions")
	if forbidden.has("AWAY_FROM_TREES"):
		receipts.append("environment-condition-unresolved:AWAY_FROM_TREES")
	if not _sim._typed_contract_tokens(nugget, "HintDetectableConditions").is_empty():
		receipts.append("presentation-hint-detectable-conditions")
	for key in ["BecomeStealthedFX", "ExitStealthFX"]:
		if nugget.has(key):
			receipts.append("presentation-fx-binding:%s" % key)
	row["invisibility_update"] = {
		"enabled": bool(_sim._module_contract_value(fields, "StartsActive", false)),
		"starts_active": bool(_sim._module_contract_value(fields, "StartsActive", false)),
		"update_ticks": maxi(1, _sim._ship_contract_delay_ticks(update_ms)),
		"next_update_tick": _sim.tick_index,
		"required_upgrades": _sim._typed_contract_raw_tokens(fields, "RequiredUpgrades"),
		"forbidden_upgrades": _sim._typed_contract_raw_tokens(fields, "ForbiddenUpgrades"),
		"broadcast": broadcast,
		"broadcast_range_source": float(broadcast_range.get("value", -1.0)) if bool(broadcast_range.get("resolved", false)) else -1.0,
		"broadcast_filter": broadcast_filter,
		"invisibility_type": invisibility_type,
		"forbidden_conditions": forbidden,
		"forbidden_weapon_conditions": _sim._typed_contract_tokens(nugget, "ForbiddenWeaponConditions"),
		"hint_detectable_conditions": _sim._typed_contract_tokens(nugget, "HintDetectableConditions"),
		"options": _sim._typed_contract_tokens(nugget, "Options"),
		"detection_range_source": float(detection.get("value", 0.0)) if bool(detection.get("resolved", false)) else -1.0,
		"self_executable": (not nugget.has("DetectionRange") or bool(detection.get("resolved", false))) and not forbidden.has("AWAY_FROM_TREES"),
		"broadcast_executable": not broadcast or (bool(broadcast_range.get("resolved", false)) and broadcast_filter_resolved),
		"become_fx_id": String(_sim._module_contract_value(nugget, "BecomeStealthedFX", "")),
		"exit_fx_id": String(_sim._module_contract_value(nugget, "ExitStealthFX", "")),
		"voice_move_role": String(_sim._module_contract_value(fields, "UnitSpecificSoundNameToUseAsVoiceMoveToStealthyArea", "")),
		"voice_enter_role": String(_sim._module_contract_value(fields, "UnitSpecificSoundNameToUseAsVoiceEnterStateMoveToStealthyArea", "")),
		"granted_ids": [],
		"unsupported_semantics": receipts,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("sourceIni", contract.get("source_ini", ""))),
		"line": int(contract.get("line", 0)),
	}


func set_invisibility_update_active(object_id: int, enabled: bool, tag: String = "") -> Dictionary:
	var _sim = sim
	var row: Dictionary = {}
	if _sim.entities.has(object_id): row = _sim.entities[object_id] as Dictionary
	elif _sim.structures.has(object_id): row = _sim.structures[object_id] as Dictionary
	else: return {"ok": false, "reason": "object-missing"}
	if not row.has("invisibility_update"):
		if _sim.structures.has(object_id): _sim._attach_structure_module_contracts(row)
		else: _sim._attach_module_contracts(row)
	var policy := row.get("invisibility_update", {}) as Dictionary
	if policy.is_empty(): return {"ok": false, "reason": "typed-invisibility-contract-missing"}
	if tag != "" and String(policy.get("tag", "")) != tag: return {"ok": false, "reason": "invisibility-tag-missing"}
	policy["enabled"] = enabled
	policy["next_update_tick"] = _sim.tick_index
	row["invisibility_update"] = policy
	if not enabled:
		_revoke_invisibility_policy_sources(object_id, row, policy)
	return {"ok": true, "reason": "", "enabled": enabled}


func _invisibility_upgrade_gate(row: Dictionary, policy: Dictionary) -> bool:
	var _sim = sim
	var team_owned := _sim.team_upgrades.get(int(row.get("team", -1)), {}) as Dictionary
	for value in policy.get("required_upgrades", []) as Array:
		var upgrade := String(value)
		if not _sim._structure_has_completed_upgrade(row, upgrade) and not team_owned.has(upgrade): return false
	for value in policy.get("forbidden_upgrades", []) as Array:
		var upgrade := String(value)
		if _sim._structure_has_completed_upgrade(row, upgrade) or team_owned.has(upgrade): return false
	return true


func _invisibility_condition_set(row: Dictionary) -> Dictionary:
	var conditions = sim._audio_active_conditions(row)
	if Vector2(row.get("destination", row.get("position", Vector2.ZERO))).distance_to(Vector2(row.get("position", Vector2.ZERO))) > 0.001 or float(row.get("current_speed", 0.0)) > 0.001:
		conditions["MOVING"] = true
	if String(row.get("state", "")) in ["attack", "attack-windup"]:
		conditions["ATTACKING"] = true
	for value in row.get("weapon_conditions", []) as Array:
		conditions[String(value).to_upper()] = true
	return conditions


func _invisibility_source_key(object_id: int, policy: Dictionary, prefix: String = "module") -> String:
	return "%s:%d:%s" % [prefix, object_id, String(policy.get("tag", ""))]


func _invisibility_source_active(row: Dictionary, source_key: String) -> bool:
	return (row.get("invisibility_sources", {}) as Dictionary).has(source_key)


func _set_invisibility_source(target: Dictionary, source_key: String, policy: Dictionary, enabled: bool, source_id: int) -> void:
	var _sim = sim
	var sources = target.get("invisibility_sources", {}) as Dictionary
	var was_source_active := sources.has(source_key)
	var was_hidden = _sim._stealth_active(target)
	if enabled:
		sources[source_key] = {
			"forbidden": (policy.get("forbidden_conditions", []) as Array).duplicate(),
			"detection_range_source": float(policy.get("detection_range_source", 0.0)),
			"invisibility_type": String(policy.get("invisibility_type", "STEALTH")),
		}
	else:
		sources.erase(source_key)
	if sources.is_empty():
		target.erase("invisibility_sources")
		if String(target.get("stealth_origin", "")) == "InvisibilityUpdate":
			target.erase("stealth_origin")
			_sim._clear_stealth(target)
	else:
		target["invisibility_sources"] = sources
		var union: Array[String] = []
		var detection_ranges: Array[float] = []
		var type := "CAMOUFLAGE"
		for source_value in sources.values():
			var source := source_value as Dictionary
			for condition_value in source.get("forbidden", []) as Array:
				var condition := String(condition_value); if not union.has(condition): union.append(condition)
			var range_source := float(source.get("detection_range_source", 0.0))
			if range_source >= 0.0: detection_ranges.append(range_source)
			if String(source.get("invisibility_type", "")) == "STEALTH": type = "STEALTH"
		target["stealth_until_tick"] = 0x3FFFFFFF
		target["stealth_forbidden"] = union
		target["stealth_origin"] = "InvisibilityUpdate"
		target["invisibility_type"] = type
		target["invisibility_detection_range_source"] = detection_ranges.min() if not detection_ranges.is_empty() else -1.0
	if was_source_active == enabled:
		return
	var fx_id := String(policy.get("become_fx_id" if enabled else "exit_fx_id", ""))
	_sim._emit_event("module.invisibility_changed", source_id, int(target.get("id", 0)), {"engaged": enabled, "fx_id": fx_id, "invisibility_type": String(policy.get("invisibility_type", "")), "source_key": source_key})
	if enabled and not was_hidden and _sim.entities.has(int(target.get("id", 0))):
		var voice_role := String(policy.get("voice_enter_role", ""))
		if voice_role != "": policy["last_audio_result"] = _sim.emit_typed_audio_intent(int(target.get("id", 0)), voice_role)


func _revoke_invisibility_policy_sources(object_id: int, row: Dictionary, policy: Dictionary) -> void:
	var _sim = sim
	var own_key := _invisibility_source_key(object_id, policy, "structure" if _sim.structures.has(object_id) else "entity")
	_set_invisibility_source(row, own_key, policy, false, object_id)
	var broadcast_key := _invisibility_source_key(object_id, policy, "broadcast")
	for target_id in policy.get("granted_ids", []) as Array:
		if _sim.entities.has(int(target_id)): _set_invisibility_source(_sim.entities[int(target_id)] as Dictionary, broadcast_key, policy, false, object_id)
	policy["granted_ids"] = []


func _step_invisibility_updates() -> void:
	var _sim = sim
	for table_value in [_sim.entities, _sim.structures]:
		var table = table_value as Dictionary
		var ids := table.keys(); ids.sort()
		for id_value in ids:
			var object_id := int(id_value); var row := table[object_id] as Dictionary
			var policy := row.get("invisibility_update", {}) as Dictionary
			if policy.is_empty() or _sim.tick_index < int(policy.get("next_update_tick", 0)): continue
			policy["next_update_tick"] = _sim.tick_index + maxi(1, int(policy.get("update_ticks", 1)))
			var conditions := _invisibility_condition_set(row)
			var blocked := not bool(policy.get("enabled", false)) or not bool(policy.get("self_executable", true)) or not _invisibility_upgrade_gate(row, policy) or int(row.get("health", 0)) <= 0
			for condition_value in policy.get("forbidden_conditions", []) as Array:
				var condition := String(condition_value)
				if condition == "AWAY_FROM_TREES": continue # policy is fail-closed above; no authoritative _sim prop geometry
				if conditions.has(condition): blocked = true; break
			for condition_value in policy.get("forbidden_weapon_conditions", []) as Array:
				if conditions.has(String(condition_value)): blocked = true; break
			row["invisibility_hint_detectable"] = false
			for condition_value in policy.get("hint_detectable_conditions", []) as Array:
				if conditions.has(String(condition_value)): row["invisibility_hint_detectable"] = true; break
			var own_key := _invisibility_source_key(object_id, policy, "structure" if table == _sim.structures else "entity")
			var was_active := _invisibility_source_active(row, own_key)
			if blocked:
				_set_invisibility_source(row, own_key, policy, false, object_id)
				if was_active and (policy.get("options", []) as Array).has("UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH"): policy["enabled"] = false
			else:
				_set_invisibility_source(row, own_key, policy, true, object_id)
			_step_invisibility_broadcast(object_id, row, policy, blocked)
			row["invisibility_update"] = policy


func _step_invisibility_broadcast(source_id: int, source: Dictionary, policy: Dictionary, blocked: bool) -> void:
	var _sim = sim
	var source_key := _invisibility_source_key(source_id, policy, "broadcast")
	var prior := policy.get("granted_ids", []) as Array
	var desired: Array[int] = []
	var radius_source := float(policy.get("broadcast_range_source", -1.0))
	var filter_tokens := policy.get("broadcast_filter", []) as Array
	var unresolved_filter := filter_tokens.is_empty() or (filter_tokens.size() == 1 and not String(filter_tokens[0]).begins_with("+") and String(filter_tokens[0]).to_upper() not in ["ANY", "ALL"])
	if bool(policy.get("broadcast", false)) and bool(policy.get("broadcast_executable", true)) and not blocked and radius_source >= 0.0 and not unresolved_filter:
		var radius = radius_source * float(_sim._rules.get("source_unit_scale", 0.1)); var origin := Vector2(source.get("position", Vector2.ZERO)); var team := int(source.get("team", -1))
		for target_id in _sim.entity_ids():
			var target := _sim.entities[target_id] as Dictionary
			if int(target.get("health", 0)) <= 0 or int(target.get("team", -1)) != team: continue
			if origin.distance_to(Vector2(target.get("position", Vector2.ZERO))) > radius: continue
			if not _sim._transport_filter_accepts(target, filter_tokens): continue
			desired.append(target_id); _set_invisibility_source(target, source_key, policy, true, source_id)
	for target_id in prior:
		if not desired.has(int(target_id)) and _sim.entities.has(int(target_id)): _set_invisibility_source(_sim.entities[int(target_id)] as Dictionary, source_key, policy, false, source_id)
	policy["granted_ids"] = desired


func _attach_stealth_detector_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed" or row.has("stealth_detector_update"):return
	var fields:=contract.get("fields",{}) as Dictionary;var rate:=_resolve_contract_milliseconds(fields.get("DetectionRate"),"stealth_detector_time_defines");var range:=_resolve_respawn_body_expression(fields.get("DetectionRange"));var receipts:Array[String]=[]
	if not bool(rate.get("resolved",false)):receipts.append("unresolved_detection_rate")
	if fields.has("DetectionRange") and not bool(range.get("resolved",false)):receipts.append("unresolved_detection_range")
	if bool(_sim._module_contract_value(fields,"CancelOneRingEffect",false)):receipts.append("unsupported_ring_presentation:CancelOneRingEffect")
	row["stealth_detector_update"]={"rate_ticks":int(rate.get("ticks",-1)),"range_source":float(range.get("value",0.0)) if bool(range.get("resolved",false)) else 0.0,"required_upgrade":String(_sim._module_contract_value(fields,"RequiredUpgrade","")),"while_garrisoned":bool(_sim._module_contract_value(fields,"CanDetectWhileGarrisoned",false)),"while_contained":bool(_sim._module_contract_value(fields,"CanDetectWhileContained",false)),"next_tick":_sim.tick_index,"unsupported_semantics":receipts}


func _step_stealth_detectors()->void:
	var _sim = sim
	var tables:Array=[_sim.entities,_sim.structures]
	for table in tables:
		for id_value in (table as Dictionary).keys():
			var detector:=(table as Dictionary)[id_value] as Dictionary;var policy:=detector.get("stealth_detector_update",{}) as Dictionary
			if policy.is_empty() or int(policy.get("rate_ticks",-1))<0 or _sim.tick_index<int(policy.get("next_tick",0)):continue
			policy["next_tick"]=_sim.tick_index+maxi(1,int(policy.get("rate_ticks",1)));detector["stealth_detector_update"]=policy
			var required:=String(policy.get("required_upgrade",""));if required!="" and not _sim._structure_has_completed_upgrade(detector,required):continue
			if _sim.entity_container.has(int(id_value)) and not bool(policy.get("while_contained",false)):continue
			if bool(detector.get("garrisoned",false)) and not bool(policy.get("while_garrisoned",false)):continue
			var radius =float(policy.get("range_source",0.0))*float(_sim._rules.get("source_unit_scale",0.1));var origin:=Vector2(detector.get("position",Vector2.ZERO))
			for target_id in _sim.entity_ids():
				var target:=_sim.entities[target_id] as Dictionary
				if int(target.get("team",-1))==int(detector.get("team",-1)) or not _sim._stealth_active(target):continue
				if origin.distance_to(Vector2(target.get("position",Vector2.ZERO)))<=radius:target["detected_until_tick"]=_sim.tick_index+maxi(1,int(policy.get("rate_ticks",1)));_sim._emit_event("stealth.detected",int(id_value),target_id,{"until_tick":target["detected_until_tick"]})


func _attach_slaved_update_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed" or row.has("slaved_update"):return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[];var offset:=Vector2.ZERO
	if typeof(fields.get("GuardPositionOffset"))==TYPE_DICTIONARY:
		var value:=(fields.get("GuardPositionOffset") as Dictionary).get("value",{}) as Dictionary;offset=Vector2(float(value.get("x",0.0)),float(value.get("y",0.0)))
	for key in ["UseSlaverAsControlForEvaObjectSightedEvents","FadeOutRange","FadeTime"]:
		if fields.has(key):receipts.append("presentation_binding:%s"%key)
	row["slaved_update"]={"master_id":0,"leash_range_source":float(_sim._module_contract_value(fields,"LeashRange",0.0)),"guard_max_range_source":float(_sim._module_contract_value(fields,"GuardMaxRange",0.0)),"guard_wander_range_source":float(_sim._module_contract_value(fields,"GuardWanderRange",0.0)),"attack_range_source":float(_sim._module_contract_value(fields,"AttackRange",0.0)),"guard_offset_source":offset,"die_on_master_death":bool(_sim._module_contract_value(fields,"DieOnMastersDeath",false)),"mark_unselectable":bool(_sim._module_contract_value(fields,"MarkUnselectable",false)),"unsupported_semantics":receipts}
	if bool((row["slaved_update"] as Dictionary).get("mark_unselectable",false)):row["ignores_select_all"]=true;row["unselectable"]=true;_sim.selected_ids.erase(int(row.get("id",0)))


func bind_slave(slave_id:int,master_id:int)->Dictionary:
	var _sim = sim
	if not _sim.entities.has(slave_id):return {"ok":false,"reason":"slave-missing"}
	if not _sim.entities.has(master_id) and not _sim.structures.has(master_id):return {"ok":false,"reason":"master-missing"}
	var slave:=_sim.entities[slave_id] as Dictionary
	if not slave.has("slaved_update"):_sim._attach_module_contracts(slave)
	var policy:=slave.get("slaved_update",{}) as Dictionary
	if policy.is_empty():return {"ok":false,"reason":"typed-slaved-contract-missing"}
	var master:=(_sim.structures[master_id] if _sim.structures.has(master_id) else _sim.entities[master_id]) as Dictionary;policy["master_id"]=master_id;policy["master_kind"]="structure" if _sim.structures.has(master_id) else "entity"
	# BFME2 1.06 game.dat, SlavedUpdate.cpp bind path 0x8A1A69: one logic-RNG
	# angle is consumed and pinned at GuardMaxRange. Later wandering is a distinct
	# GuardWanderRange operation (0x8A1FDC); conflating the two changes both the
	# stream position and the authored radii.
	var guard_max =_sim._retail_source_to_sim_offset(Vector2(float(policy.get("guard_max_range_source",0.0)),0.0)).x
	var pin_angle =_sim.logic_random_real(0.0,TAU)
	policy["guard_pinned_offset"]=Vector2(cos(pin_angle),sin(pin_angle))*guard_max
	policy["guard_position"]=Vector2(master.get("position",Vector2.ZERO))+_sim._retail_source_to_sim_offset(Vector2(policy.get("guard_offset_source",Vector2.ZERO)))+Vector2(policy.get("guard_pinned_offset",Vector2.ZERO));policy.erase("guard_wander_destination");slave["slaved_update"]=policy;return {"ok":true,"reason":""}


func _step_slaved_updates()->void:
	var _sim = sim
	for slave_id in _sim.entity_ids():
		if not _sim.entities.has(slave_id):continue
		var slave:=_sim.entities[slave_id] as Dictionary;var policy:=slave.get("slaved_update",{}) as Dictionary
		if policy.is_empty() or int(policy.get("master_id",0))==0:continue
		var master_id:=int(policy.get("master_id"));var table =_sim.structures if String(policy.get("master_kind"))=="structure" else _sim.entities
		if not table.has(master_id) or int((table[master_id] as Dictionary).get("health",0))<=0:
			if bool(policy.get("die_on_master_death",false)) and int(slave.get("health",0))>0:_kill_slave_for_master_death(slave_id,slave)
			else:policy["master_id"]=0;slave["slaved_update"]=policy
			continue
		var master:=table[master_id] as Dictionary;var guard =Vector2(master.get("position",Vector2.ZERO))+_sim._retail_source_to_sim_offset(Vector2(policy.get("guard_offset_source",Vector2.ZERO)))+Vector2(policy.get("guard_pinned_offset",Vector2.ZERO));policy["guard_position"]=guard;slave["slaved_update"]=policy
		# Retail SlavedUpdate priority two: if the master has a current victim and
		# AttackRange is authored, move toward that victim but clamp the goal to a
		# circle around the MASTER. This deliberately does not assign the victim to
		# the slave; the original issues an AI move-to-position command.
		var master_target_id:=int(master.get("target_id",0));var master_target_kind:=String(master.get("target_kind","battalion"));var attack_range =_sim._retail_source_to_sim_offset(Vector2(float(policy.get("attack_range_source",0.0)),0.0)).x
		if attack_range>0.0 and master_target_id!=0 and _sim._target_alive(master_target_id,master_target_kind):
			var master_position:=Vector2(master.get("position",Vector2.ZERO));var victim_position =_sim._target_position(master_target_id,master_target_kind);var attack_position =victim_position;var delta =victim_position-master_position
			if delta.length()>attack_range:attack_position=master_position+delta.normalized()*attack_range
			if _sim._assign_route(slave,attack_position):slave["state"]="run"
			else:slave["position"]=attack_position;slave["state"]="idle";_sim._spatial_sync(slave)
			continue
		var leash =_sim._retail_source_to_sim_offset(Vector2(float(policy.get("leash_range_source",0.0)),0.0)).x;var guard_max =_sim._retail_source_to_sim_offset(Vector2(float(policy.get("guard_max_range_source",0.0)),0.0)).x;var beyond =leash>0.0 and Vector2(slave.get("position",Vector2.ZERO)).distance_to(guard)>leash
		var target_id:=int(slave.get("target_id",0));var target_kind:=String(slave.get("target_kind","battalion"));if not beyond and target_id!=0 and guard_max>0.0 and _sim._target_alive(target_id,target_kind):beyond=_sim._target_position(target_id,target_kind).distance_to(guard)>guard_max
		if beyond:
			slave["target_id"]=0;slave["attack_move"]=false;slave["order_kind"]="";_sim._clear_member_targets(slave);_sim._clear_pending_route(slave,false)
			# BFME2 1.06 0x8A1FDC uses GuardWanderRange for the later
			# threshold/reroll radius, not GuardMaxRange. Pin one destination for
			# this return order so a per-tick sim step cannot consume extra draws.
			var return_point:=Vector2(policy.get("guard_wander_destination",guard))
			if not policy.has("guard_wander_destination"):
				var wander =_sim._retail_source_to_sim_offset(Vector2(float(policy.get("guard_wander_range_source",0.0)),0.0)).x
				if wander>0.0:
					var wander_angle =_sim.logic_random_real(0.0,TAU);return_point=guard+Vector2(cos(wander_angle),sin(wander_angle))*wander
				policy["guard_wander_destination"]=return_point;slave["slaved_update"]=policy
			if _sim._assign_route(slave,return_point):slave["state"]="run"
			else:slave["position"]=return_point;slave["state"]="idle";_sim._spatial_sync(slave)
		else:
			policy.erase("guard_wander_destination");slave["slaved_update"]=policy


func _kill_slave_for_master_death(slave_id:int,slave:Dictionary)->void:
	var _sim = sim
	var members:=slave.get("member_health",[]) as Array;var defeated:Array[int]=[]
	for index in members.size():
		if int(members[index])>0:defeated.append(index);members[index]=0
	slave["member_health"]=members;slave["health"]=0;var verdict =_sim._bookkeep_battalion_death(slave_id,slave,"NORMAL",defeated)
	_sim._emit_event("slave.master_death",int((slave.get("slaved_update",{}) as Dictionary).get("master_id",0)),slave_id)
	if bool(verdict.get("destroy_object",false)):_sim.entities.erase(slave_id)


func _attach_castle_upgrade_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed":return
	var fields=contract.get("fields",{}) as Dictionary;var trigger=String(_sim._module_contract_value(fields,"TriggeredBy","")).strip_edges();var upgrade=String(_sim._module_contract_value(fields,"Upgrade","")).strip_edges()
	if trigger=="" or upgrade=="":return
	var receipts:Array[String]=[];var radius =-1.0
	if fields.has("WallUpgradeRadius"):
		var radius_field:=fields.get("WallUpgradeRadius",{}) as Dictionary;var resolved:={"resolved":false,"define":String(radius_field.get("define",radius_field.get("expression","")))};var defines:=_sim._rules.get("castle_upgrade_radius_defines",{}) as Dictionary
		if typeof(radius_field.get("value")) in [TYPE_INT,TYPE_FLOAT]:resolved={"resolved":true,"value":float(radius_field.get("value"))}
		elif typeof(defines.get(String(resolved.get("define","")))) in [TYPE_INT,TYPE_FLOAT]:resolved={"resolved":true,"value":float(defines[String(resolved.get("define"))])}
		if bool(resolved.get("resolved",false)):radius=float(resolved.get("value",-1.0))
		else:receipts.append("unresolved_wall_upgrade_radius:%s"%String(resolved.get("define","")))
	var rows:=row.get("castle_upgrade_contracts",[]) as Array
	for value in rows:
		var existing:=value as Dictionary
		if String(existing.get("triggered_by"))==trigger and String(existing.get("upgrade"))==upgrade:return
	rows.append({"triggered_by":trigger,"upgrade":upgrade,"wall_upgrade_radius_source":radius,"unsupported_semantics":receipts,"tag":String(contract.get("tag","")),"line":int(contract.get("line",0))});row["castle_upgrade_contracts"]=rows


func apply_castle_upgrade_trigger(structure_id:int,trigger_upgrade_id:String)->Dictionary:
	var _sim = sim
	if not _sim.structures.has(structure_id):return {"ok":false,"reason":"structure-missing"}
	var building:=_sim.structures[structure_id] as Dictionary
	if not building.has("castle_upgrade_contracts"):_sim._attach_structure_module_contracts(building)
	var matched:=0
	for value in building.get("castle_upgrade_contracts",[]) as Array:
		var policy:=value as Dictionary
		if String(policy.get("triggered_by",""))!=trigger_upgrade_id:continue
		var upgrade:=String(policy.get("upgrade",""));var recipients:Array[int]=[structure_id]
		for piece in building.get("castle_piece_structure_ids",[]) as Array:recipients.append(int(piece))
		for recipient_id in recipients:
			if not _sim.structures.has(recipient_id):continue
			var recipient:=_sim.structures[recipient_id] as Dictionary;var completed:=recipient.get("completed_upgrades",[]) as Array
			if not completed.has(upgrade):completed.append(upgrade);recipient["completed_upgrades"]=completed
		matched+=1;_sim._emit_event("upgrade.castle_granted",structure_id,0,{"team":int(building.get("team",-1)),"trigger_upgrade_id":trigger_upgrade_id,"upgrade_id":upgrade,"recipient_count":recipients.size()})
	return {"ok":matched>0,"reason":"" if matched>0 else "trigger-not-authored","grants":matched}


func _attach_spawn_behavior_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed" or row.has("spawn_behavior"):return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[]
	for key in ["FadeInTime","KillSpawnsBasedOnModelConditionState","SpawnInsideBuilding"]:
		if fields.has(key):receipts.append("presentation_or_unresolved_spawn_semantic:%s"%key)
	# Compatibility receipt retained for already-cooked packs. The execution path
	# below is independently matched in BFME2 1.06 and RotWK 2.01 binaries; the
	# shipped compare quirk considers only the last distinct authored template.
	var can_reclaim=bool(_sim._module_contract_value(fields,"CanReclaimOrphans",false))
	if can_reclaim:receipts.append("deferred_binary_ambiguous:CanReclaimOrphans")
	if bool(_sim._module_contract_value(fields,"RespectCommandLimit",false)):receipts.append("unsupported_spawn_semantic:RespectCommandLimit")
	var template_field:=fields.get("SpawnTemplateName",{}) as Dictionary;var templates:Array[String]=[]
	for template_value in template_field.get("value",[]) as Array:templates.append(String(template_value))
	row["spawn_behavior"]={"spawn_number":int(_sim._module_contract_value(fields,"SpawnNumber",0)),"replace_ticks":_sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields,"SpawnReplaceDelay",0.0))),"templates":templates,"one_shot":bool(_sim._module_contract_value(fields,"OneShot",false)),"can_reclaim_orphans":can_reclaim,"can_reclaim_runtime":"bfme2-rotwk-binary-proven" if can_reclaim else "disabled","require_spawner":bool(_sim._module_contract_value(fields,"SpawnedRequireSpawner",false)),"share_upgrades":bool(_sim._module_contract_value(fields,"ShareUpgrades",false)),"triggered_by":String(_sim._module_contract_value(fields,"TriggeredBy","")),"initial_remaining":int(_sim._module_contract_value(fields,"InitialBurst",_sim._module_contract_value(fields,"SpawnNumber",0))),"spawned_ids":[],"spawn_serial":0,"spawn_count":-1,"next_spawn_tick":_sim.tick_index,"unsupported_semantics":receipts}


func _step_spawn_behaviors()->void:
	var _sim = sim
	var owners:Array=[]
	for id in _sim.entity_ids():owners.append({"id":id,"kind":"entity"})
	for id in _sim.structure_ids():owners.append({"id":id,"kind":"structure"})
	for owner_value in owners:
		var owner_ref:=owner_value as Dictionary;var table =_sim.structures if String(owner_ref.get("kind"))=="structure" else _sim.entities;var owner_id:=int(owner_ref.get("id"));if not table.has(owner_id):continue
		var owner:=table[owner_id] as Dictionary;var policy:=owner.get("spawn_behavior",{}) as Dictionary
		if policy.is_empty():continue
		var living:Array[int]=[];var lost_count:=0
		for spawned_value in policy.get("spawned_ids",[]) as Array:
			var spawned_id =int(spawned_value)
			if _sim.entities.has(spawned_id) and int((_sim.entities[spawned_id] as Dictionary).get("health",0))>0:living.append(spawned_id)
			else:lost_count+=1
		policy["spawned_ids"]=living
		if lost_count>0:policy["spawn_count"]=int(policy.get("spawn_count",-1))-lost_count
		if int(owner.get("health",0))<=0:
			if bool(policy.get("require_spawner",false)):
				for spawned_id in living:
					if _sim.entities.has(spawned_id):_kill_slave_for_master_death(spawned_id,_sim.entities[spawned_id] as Dictionary)
			else:
				# Proven source-relative onDie seam: surviving children lose their
				# producer and SlavedUpdate master. Whether a later spawner adopts one
				# is the separately receipted binary ambiguity above.
				for spawned_id in living:
					if not _sim.entities.has(spawned_id):continue
					var orphan:=_sim.entities[spawned_id] as Dictionary
					orphan.erase("spawn_behavior_parent_id");orphan.erase("spawn_behavior_parent_kind")
					orphan["spawn_behavior_orphaned_tick"]=_sim.tick_index
					var slave_policy:=orphan.get("slaved_update",{}) as Dictionary
					if not slave_policy.is_empty():slave_policy["master_id"]=0;slave_policy.erase("master_kind");orphan["slaved_update"]=slave_policy
			policy["spawned_ids"]=[];owner["spawn_behavior"]=policy;continue
		var required:=String(policy.get("triggered_by",""));if required!="" and not _sim._structure_has_completed_upgrade(owner,required):owner["spawn_behavior"]=policy;continue
		if lost_count>0 and int(policy.get("next_spawn_tick",0))<=_sim.tick_index:policy["next_spawn_tick"]=_sim.tick_index+int(policy.get("replace_ticks",0))
		var initial:=int(policy.get("initial_remaining",0));var capacity:=int(policy.get("spawn_number",0))-living.size();var due =initial>0 or (not bool(policy.get("one_shot",false)) and _sim.tick_index>int(policy.get("next_spawn_tick",0)))
		while capacity>0 and due:
			var spawned_id =_spawn_behavior_member(owner_id,owner,policy);if spawned_id==0:break
			living.append(spawned_id);capacity-=1
			if initial>0:initial-=1;policy["initial_remaining"]=initial
			else:policy["next_spawn_tick"]=_sim.tick_index+int(policy.get("replace_ticks",0));break
			due=initial>0
		policy["spawned_ids"]=living;owner["spawn_behavior"]=policy


func _spawn_behavior_member(owner_id:int,owner:Dictionary,policy:Dictionary)->int:
	var _sim = sim
	var templates:=policy.get("templates",[]) as Array;if templates.is_empty():return 0
	# Both binaries reserve an exit door before the orphan scan. With no door the
	# due slot remains queued and neither reclaim nor allocation is attempted.
	if not bool(owner.get("spawn_behavior_exit_door_available",true)):return 0
	var reclaimed_id:=_spawn_behavior_reclaim_orphan(owner_id,owner,policy)
	if reclaimed_id>0:return reclaimed_id
	var serial:=int(policy.get("spawn_serial",0));var template:=String(templates[serial%templates.size()]);var unit_rules:=_sim._rules.get("unit_rules",{}) as Dictionary
	var team:=int(owner.get("team",-1));if not _sim._next_dynamic_id.has(team):_sim._next_dynamic_id[team]=900000+team*1000
	var spawned_id =0
	if (unit_rules.get(template,{}) as Dictionary).is_empty():
		# Neutral SpawnBehavior children are deliberately absent from faction
		# production tables. Resolve them only through the selected scenario-unit
		# descriptor and its authored lair-spawn admission.
		spawned_id=_sim.spawn_scenario_unit(template,team,Vector2(owner.get("position",Vector2.ZERO)),"lair-spawn")
	if spawned_id<=0 and (unit_rules.get(template,{}) as Dictionary).is_empty():
		var receipts:=policy.get("unsupported_semantics",[]) as Array
		var receipt ="unresolved_spawn_template:%s"%template
		if not receipts.has(receipt):receipts.append(receipt)
		policy["unsupported_semantics"]=receipts
		return 0
	if spawned_id<=0:
		spawned_id=int(_sim._next_dynamic_id[team]);_sim._next_dynamic_id[team]=spawned_id+1;_sim._add_battalion(spawned_id,team,Vector2(owner.get("position",Vector2.ZERO)),template,template,template,0)
	if not _sim.entities.has(spawned_id):
		var receipts:=policy.get("unsupported_semantics",[]) as Array
		var receipt ="unresolved_spawn_template:%s"%template
		if not receipts.has(receipt):receipts.append(receipt)
		policy["unsupported_semantics"]=receipts
		return 0
	var child:=_sim.entities[spawned_id] as Dictionary;child["spawn_behavior_parent_id"]=owner_id;child["spawn_behavior_parent_kind"]="structure" if _sim.structures.has(owner_id) else "entity"
	if bool(policy.get("share_upgrades",false)):child["completed_upgrades"]=(owner.get("completed_upgrades",[]) as Array).duplicate()
	if child.has("slaved_update"):bind_slave(spawned_id,owner_id)
	policy["spawn_serial"]=serial+1;policy["spawn_count"]=maxi(0,int(policy.get("spawn_count",-1)))+1;_sim._emit_event("spawn_behavior.spawned",owner_id,spawned_id,{"template":template});return spawned_id


func _spawn_behavior_reclaim_orphan(owner_id:int,owner:Dictionary,policy:Dictionary)->int:
	var _sim = sim
	if not bool(policy.get("can_reclaim_orphans",false)):return 0
	var templates:=policy.get("templates",[]) as Array
	if templates.is_empty():return 0
	# The shipped loop resets its closest candidate for every distinct name and
	# skips only consecutive duplicates. It returns the final distinct template's
	# candidate, not the nearest candidate across the authored template list.
	var previous_template:="";var candidate_id:=0
	for template_value in templates:
		var template:=String(template_value)
		if template==previous_template:continue
		previous_template=template
		candidate_id=_spawn_behavior_closest_orphan(owner,template)
	if candidate_id<=0 or not _sim.entities.has(candidate_id):return 0
	var child:=_sim.entities[candidate_id] as Dictionary
	child["spawn_behavior_parent_id"]=owner_id;child["spawn_behavior_parent_kind"]="structure" if _sim.structures.has(owner_id) else "entity"
	# The reclaim helper itself consumes no RNG. Canonical SlavedUpdate::onEnslave
	# is transitive behavior: it consumes exactly one logic draw and repins the
	# GuardMax offset without moving the child's physical position.
	if child.has("slaved_update"):bind_slave(candidate_id,owner_id)
	_sim._emit_event("spawn_behavior.reclaimed",owner_id,candidate_id,{"template":previous_template,"binary_receipt":"bfme2-rotwk-create-spawn-matched"})
	return candidate_id


func _spawn_behavior_closest_orphan(owner:Dictionary,template:String)->int:
	var _sim = sim
	var controlling_player:=int(owner.get("retail_controlling_player",owner.get("team",-1)))
	var candidates:Array[int]=[]
	for entity_id in _sim.entity_ids():
		var candidate =_sim.entities[entity_id] as Dictionary
		if int(candidate.get("retail_controlling_player",candidate.get("team",-1)))!=controlling_player:continue
		if not _spawn_behavior_template_equivalent(candidate,template):continue
		if int(candidate.get("spawn_behavior_parent_id",0))!=0 or int(candidate.get("production_producer_id",0))!=0:continue
		var burning:=false
		for condition_value in candidate.get("model_conditions",[]) as Array:
			if String(condition_value).to_upper()=="BURNINGDEATH":burning=true;break
		if burning:continue
		candidates.append(entity_id)
	candidates.sort_custom(func(left:int,right:int)->bool:
		var a:=_sim.entities[left] as Dictionary;var b:=_sim.entities[right] as Dictionary
		var a_prototype:=int(a.get("retail_team_prototype_ordinal",a.get("team",0)));var b_prototype:=int(b.get("retail_team_prototype_ordinal",b.get("team",0)))
		if a_prototype!=b_prototype:return a_prototype<b_prototype
		var a_instance:=int(a.get("retail_team_instance_ordinal",0));var b_instance:=int(b.get("retail_team_instance_ordinal",0))
		if a_instance!=b_instance:return a_instance>b_instance
		var a_member:=int(a.get("retail_team_member_ordinal",left));var b_member:=int(b.get("retail_team_member_ordinal",right))
		if a_member!=b_member:return a_member>b_member
		return left>right
	)
	var owner_position:=Vector2(owner.get("position",Vector2.ZERO));var maximum_distance =_sim._retail_source_to_sim_offset(Vector2(10000.0,0.0)).x;var closest_squared =maximum_distance*maximum_distance;var closest_id:=0
	for entity_id in candidates:
		var candidate =_sim.entities[entity_id] as Dictionary;var distance_squared:=owner_position.distance_squared_to(Vector2(candidate.get("position",Vector2.ZERO)))
		# Strict comparison preserves the first retail traversal member on a tie
		# and excludes the exact 10,000-source-unit sentinel boundary.
		if distance_squared<closest_squared:closest_squared=distance_squared;closest_id=entity_id
	return closest_id


func _spawn_behavior_template_equivalent(candidate:Dictionary,template:String)->bool:
	for key in ["object_id","source_object_id","unit_type"]:
		if String(candidate.get(key,"")).nocasecmp_to(template)==0:return true
	for equivalent_value in candidate.get("retail_equivalent_template_ids",[]) as Array:
		if String(equivalent_value).nocasecmp_to(template)==0:return true
	return false


func _attach_stealth_update_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed" or row.has("stealth_update"):return
	var fields:=contract.get("fields",{}) as Dictionary;var receipts:Array[String]=[]
	for key in ["FriendlyOpacityMin","FriendlyOpacityMax","PulseFrequency","HintDetectableConditions","DisguisesAsTeam","DisguiseTransitionTime","DisguiseRevealTransitionTime","RemoveTerrainRestrictionOnUpgrade","OrderIdleEnemiesToAttackMeUponReveal"]:
		if fields.has(key):receipts.append("presentation_or_unresolved_stealth_semantic:%s"%key)
	var required_upgrades:Array[String]=[];var required_field:=fields.get("RequiredUpgradeNames",{}) as Dictionary
	for upgrade_value in required_field.get("value",[]) as Array:required_upgrades.append(String(upgrade_value))
	var enabled=bool(_sim._module_contract_value(fields,"StartsActive",false)) or bool(_sim._module_contract_value(fields,"InnateStealth",false));var delay=_sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields,"StealthDelay",0.0)))
	row["stealth_update"]={"enabled":enabled,"delay_ticks":delay,"activation_tick":_sim.tick_index+delay,"forbidden":_sim._typed_contract_tokens(fields,"StealthForbiddenConditions"),"reveal_weapon_sets":_sim._typed_contract_tokens(fields,"RevealWeaponSets"),"required_upgrades":required_upgrades,"detected_range_source":float(_sim._module_contract_value(fields,"DetectedByAnyoneRange",0.0)),"reveal_target_range_source":float(_sim._module_contract_value(fields,"RevealDistanceFromTarget",0.0)),"unsupported_semantics":receipts}


func set_stealth_update_active(entity_id:int,enabled:bool)->Dictionary:
	var _sim = sim
	if not _sim.entities.has(entity_id):return {"ok":false,"reason":"entity-missing"}
	var row=_sim.entities[entity_id] as Dictionary;if not row.has("stealth_update"):_sim._attach_module_contracts(row)
	var policy:=row.get("stealth_update",{}) as Dictionary;if policy.is_empty():return {"ok":false,"reason":"typed-stealth-contract-missing"}
	policy["enabled"]=enabled;policy["activation_tick"]=_sim.tick_index+int(policy.get("delay_ticks",0));row["stealth_update"]=policy;if not enabled:_sim._clear_stealth(row)
	return {"ok":true,"reason":"","activation_tick":policy["activation_tick"]}


func _step_stealth_updates()->void:
	var _sim = sim
	for id in _sim.entity_ids():
		var row:=_sim.entities[id] as Dictionary;var policy:=row.get("stealth_update",{}) as Dictionary
		if policy.is_empty():continue
		var forbidden:=policy.get("forbidden",[]) as Array;var blocked:=false
		if forbidden.has("MOVING") and (Vector2(row.get("destination",row.get("position",Vector2.ZERO))).distance_to(Vector2(row.get("position",Vector2.ZERO)))>0.001 or float(row.get("current_speed",0.0))>0.001):blocked=true
		if forbidden.has("ATTACKING") and String(row.get("state","")) in ["attack","attack-windup"]:blocked=true
		var weapon_flags:=row.get("weapon_set_flags",[]) as Array
		for flag in policy.get("reveal_weapon_sets",[]) as Array:
			if weapon_flags.has(flag):blocked=true;break
		var required_ok:=true
		for upgrade in policy.get("required_upgrades",[]) as Array:
			if not _sim._structure_has_completed_upgrade(row,String(upgrade)):required_ok=false;break
		var origin:=Vector2(row.get("position",Vector2.ZERO));var anyone_range =_sim._retail_source_to_sim_offset(Vector2(float(policy.get("detected_range_source",0.0)),0.0)).x
		if anyone_range>0.0:
			for other_id in _sim.entity_ids():
				if other_id==id:continue
				var other:=_sim.entities[other_id] as Dictionary
				if int(other.get("team",-1))!=int(row.get("team",-1)) and origin.distance_to(Vector2(other.get("position",Vector2.ZERO)))<=anyone_range:blocked=true;break
		var target_id:=int(row.get("target_id",0));var reveal_range =_sim._retail_source_to_sim_offset(Vector2(float(policy.get("reveal_target_range_source",0.0)),0.0)).x
		if target_id!=0 and reveal_range>0.0 and _sim.entities.has(target_id) and origin.distance_to(Vector2((_sim.entities[target_id] as Dictionary).get("position",Vector2.ZERO)))<=reveal_range:blocked=true
		if blocked or not bool(policy.get("enabled",false)) or not required_ok:
			if _sim._stealth_active(row):_sim._clear_stealth(row)
			policy["activation_tick"]=_sim.tick_index+int(policy.get("delay_ticks",0));row["stealth_update"]=policy;continue
		if not _sim._stealth_active(row) and _sim.tick_index>=int(policy.get("activation_tick",0)):_sim._grant_stealth(row,0x3fffffff,forbidden)


func _attach_object_creation_upgrade_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed":return
	var fields:=contract.get("fields",{}) as Dictionary;var triggers:Array[String]=[];var conflicts:Array[String]=[]
	for value in (fields.get("TriggeredBy",{}) as Dictionary).get("value",[]) as Array:triggers.append(String(value))
	for value in (fields.get("ConflictsWith",{}) as Dictionary).get("value",[]) as Array:conflicts.append(String(value))
	var delay:=_resolve_contract_milliseconds(fields.get("Delay"),"object_creation_delay_defines");var receipts:Array[String]=[]
	if fields.has("Delay") and not bool(delay.get("resolved",false)):receipts.append("unresolved_creation_delay:%s"%String(delay.get("expression","")))
	for key in ["FadeInTime","DeathAnimAndDuration"]:
		if fields.has(key):receipts.append("presentation_binding:%s"%key)
	if bool(_sim._module_contract_value(fields,"DestroyWhenSold",false)):receipts.append("unsupported_sale_semantic:DestroyWhenSold")
	if bool(_sim._module_contract_value(fields,"UseBuildingProduction",false)):receipts.append("unsupported_creation_semantic:UseBuildingProduction")
	var offset:=Vector2.ZERO
	if typeof(fields.get("Offset"))==TYPE_DICTIONARY:
		var coord:=((fields.get("Offset") as Dictionary).get("value",{}) as Dictionary);offset=Vector2(float(coord.get("x",0.0)),float(coord.get("y",0.0)))
	var rows:=row.get("object_creation_upgrades",[]) as Array
	rows.append({"triggers":triggers,"requires_all":bool(_sim._module_contract_value(fields,"RequiresAllTriggers",false)),"conflicts":conflicts,"delay_ticks":int(delay.get("ticks",0)) if bool(delay.get("resolved",false)) else -1,"offset_source":offset,"thing_to_spawn":String(_sim._module_contract_value(fields,"ThingToSpawn","")),"grant_upgrade":String(_sim._module_contract_value(fields,"GrantUpgrade","")),"remove_upgrade":String(_sim._module_contract_value(fields,"RemoveUpgrade","")),"upgrade_object":String(_sim._module_contract_value(fields,"UpgradeObject","")),"scheduled_tick":-1,"consumed":false,"spawned_ids":[],"unsupported_semantics":receipts,"tag":String(contract.get("tag","")),"line":int(contract.get("line",0))});row["object_creation_upgrades"]=rows


func _attach_attribute_modifier_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	## AttributeModifierUpgrade is a persistent upgrade mux. The module remains
	## importer-deferred until independent acceptance; this consumer only accepts
	## the exact typed field shape and a fully resolved shared ModifierList.
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	var triggers = _sim._typed_contract_tokens(fields, "TriggeredBy")
	var modifier_name = String(_sim._module_contract_value(fields, "AttributeModifier", "")).strip_edges()
	if triggers.is_empty() or modifier_name == "":
		return
	var requires_all_value: Variant = _sim._module_contract_value(fields, "RequiresAllTriggers", false)
	if typeof(requires_all_value) != TYPE_BOOL:
		return
	var modifier := ((_sim._rules.get("attribute_modifier_rules", {}) as Dictionary).get(modifier_name, {}) as Dictionary).duplicate(true)
	var unsupported: Array[String] = []
	if modifier.is_empty() or (modifier.get("effects", []) as Array).is_empty():
		unsupported.append("unresolved_modifier_list:%s" % modifier_name)
	var presentation: Array[String] = []
	var deferred_value: Variant = fields.get("deferredFields", [])
	if typeof(deferred_value) != TYPE_ARRAY:
		return
	for deferred_row_value in deferred_value as Array:
		if typeof(deferred_row_value) != TYPE_DICTIONARY:
			return
		var deferred_row := deferred_row_value as Dictionary
		if String(deferred_row.get("name", "")) != "CustomAnimAndDuration":
			unsupported.append("unsupported_deferred_field:%s" % String(deferred_row.get("name", "")))
			continue
		presentation.append("CustomAnimAndDuration:%s" % String(deferred_row.get("authored", "")))
	var policies := row.get("attribute_modifier_upgrades", []) as Array
	var tag := String(contract.get("tag", ""))
	var line := int(contract.get("line", 0))
	for existing_value in policies:
		var existing := existing_value as Dictionary
		if String(existing.get("tag", "")) == tag and int(existing.get("line", -1)) == line:
			return
	policies.append({
		"triggers": triggers,
		"requires_all": bool(requires_all_value),
		"conflicts": _sim._typed_contract_tokens(fields, "ConflictsWith"),
		"modifier_name": modifier_name,
		"modifier": modifier,
		"active": false,
		"unsupported_semantics": unsupported,
		"presentation_receipts": presentation,
		"tag": tag,
		"line": line,
	})
	row["attribute_modifier_upgrades"] = policies
	_reconcile_attribute_modifier_upgrades(row)


func _attribute_modifier_upgrade_owned(row: Dictionary, upgrade_id: String) -> bool:
	var _sim = sim
	if _sim._aura_has_upgrade(row.get("completed_upgrades", []) as Array, row.get("applied_upgrades", {}) as Dictionary, upgrade_id):
		return true
	return _sim._aura_has_upgrade([], _sim.team_upgrades.get(int(row.get("team", -1)), {}) as Dictionary, upgrade_id)


func _attribute_modifier_upgrade_should_activate(row: Dictionary, policy: Dictionary) -> bool:
	if not (policy.get("unsupported_semantics", []) as Array).is_empty():
		return false
	for conflict_value in policy.get("conflicts", []) as Array:
		if _attribute_modifier_upgrade_owned(row, String(conflict_value)):
			return false
	var triggers := policy.get("triggers", []) as Array
	if bool(policy.get("requires_all", false)):
		for trigger_value in triggers:
			if not _attribute_modifier_upgrade_owned(row, String(trigger_value)):
				return false
		return not triggers.is_empty()
	for trigger_value in triggers:
		if _attribute_modifier_upgrade_owned(row, String(trigger_value)):
			return true
	return false


func _attribute_modifier_upgrade_key(policy: Dictionary) -> String:
	var modifier := policy.get("modifier", {}) as Dictionary
	var category := String(modifier.get("category", ""))
	var stacking := modifier.get("stacking", {}) as Dictionary
	if category != "" and bool(stacking.get("replaceInCategoryIfLongest", false)):
		return "attribute-modifier-upgrade-category:%s" % category
	return "attribute-modifier-upgrade:%s:%d" % [String(policy.get("tag", "")), int(policy.get("line", 0))]


func _reconcile_attribute_modifier_upgrades(row: Dictionary) -> void:
	var _sim = sim
	var policies := row.get("attribute_modifier_upgrades", []) as Array
	if policies.is_empty():
		return
	var table = row.get("timed_modifiers", {}) as Dictionary
	# Remove every entry owned by this module family, then deterministically
	# rebuild active entries. This makes conflict/removal and category replacement
	# independent of the order in which upgrade APIs were called.
	for key_value in table.keys().duplicate():
		if bool((table[key_value] as Dictionary).get("attribute_modifier_upgrade", false)):
			table.erase(key_value)
	for policy_value in policies:
		var policy := policy_value as Dictionary
		var active := _attribute_modifier_upgrade_should_activate(row, policy)
		policy["active"] = active
		if not active:
			continue
		var modifier := policy.get("modifier", {}) as Dictionary
		var category := String(modifier.get("category", ""))
		var stacking := modifier.get("stacking", {}) as Dictionary
		if (
			bool(stacking.get("ignoreIfAnticategoryActive", false))
			and category == "LEADERSHIP"
			and _sim._refresh_leadership_suppression(row) > _sim.tick_index
		):
			continue
		table[_attribute_modifier_upgrade_key(policy)] = {
			"modifiers": (modifier.get("effects", []) as Array).duplicate(true),
			# Persistent upgrades share the modifier-effect/category core but are
			# removed only by this lifecycle reconciler, never by wall/tick expiry.
			"persistent": true,
			"category": category,
			"modifier_id": String(policy.get("modifier_name", "")),
			"module_tag": String(policy.get("tag", "")),
			"attribute_modifier_upgrade": true,
		}
	row["timed_modifiers"] = table
	row["attribute_modifier_upgrades"] = policies


func _step_attribute_modifier_upgrades() -> void:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		var row := _sim.entities[entity_id] as Dictionary
		if not row.has("attribute_modifier_upgrades"):
			_sim._attach_module_contracts(row)
		_reconcile_attribute_modifier_upgrades(row)
	for structure_id in _sim.structure_ids():
		var row := _sim.structures[structure_id] as Dictionary
		if not row.has("attribute_modifier_upgrades"):
			_sim._attach_structure_module_contracts(row)
		_reconcile_attribute_modifier_upgrades(row)


func _attach_geometry_upgrade_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	var triggers = _sim._typed_contract_tokens(fields, "TriggeredBy")
	if triggers.is_empty():
		return
	var requires_all_value: Variant = _sim._module_contract_value(fields, "RequiresAllTriggers", false)
	if typeof(requires_all_value) != TYPE_BOOL:
		return
	var unsupported: Array[String] = []
	var deferred_value: Variant = fields.get("deferredFields", [])
	if typeof(deferred_value) != TYPE_ARRAY:
		return
	for receipt_value in deferred_value as Array:
		if typeof(receipt_value) != TYPE_DICTIONARY:
			return
		var receipt = receipt_value as Dictionary
		var name := String(receipt.get("name", ""))
		if name not in ["CustomAnimAndDuration", "WallBoundsMesh", "RampMesh1", "RampMesh2"]:
			unsupported.append("unsupported_deferred_field:%s" % name)
		else:
			unsupported.append("%s:%s" % [name, String(receipt.get("authored", ""))])
	var policies := row.get("geometry_upgrades", []) as Array
	var tag := String(contract.get("tag", ""))
	var line := int(contract.get("line", 0))
	for existing_value in policies:
		var existing := existing_value as Dictionary
		if String(existing.get("tag", "")) == tag and int(existing.get("line", -1)) == line:
			return
	policies.append({
		"triggers": triggers,
		"requires_all": bool(requires_all_value),
		"conflicts": _sim._typed_contract_tokens(fields, "ConflictsWith"),
		"show_geometry": _sim._typed_contract_raw_tokens(fields, "ShowGeometry"),
		"hide_geometry": _sim._typed_contract_raw_tokens(fields, "HideGeometry"),
		"active": false,
		"unsupported_semantics": unsupported,
		"tag": tag,
		"line": line,
	})
	row["geometry_upgrades"] = policies
	_reconcile_geometry_upgrades(row)


func _geometry_upgrade_should_activate(row: Dictionary, policy: Dictionary) -> bool:
	for conflict_value in policy.get("conflicts", []) as Array:
		if _attribute_modifier_upgrade_owned(row, String(conflict_value)):
			return false
	var triggers := policy.get("triggers", []) as Array
	if bool(policy.get("requires_all", false)):
		for trigger_value in triggers:
			if not _attribute_modifier_upgrade_owned(row, String(trigger_value)):
				return false
		return not triggers.is_empty()
	for trigger_value in triggers:
		if _attribute_modifier_upgrade_owned(row, String(trigger_value)):
			return true
	return false


func _reconcile_geometry_upgrades(row: Dictionary) -> void:
	var policies := row.get("geometry_upgrades", []) as Array
	if policies.is_empty():
		return
	# Fold in source order. Hide follows show inside one module and therefore
	# wins an internally contradictory authored row without relying on hash order.
	var state_by_token: Dictionary = {}
	var authored_token: Dictionary = {}
	var operations: Array[Dictionary] = []
	for policy_value in policies:
		var policy := policy_value as Dictionary
		var active := _geometry_upgrade_should_activate(row, policy)
		policy["active"] = active
		if not active:
			continue
		for shown_value in policy.get("show_geometry", []) as Array:
			var shown := String(shown_value)
			var shown_key := shown.to_upper()
			state_by_token[shown_key] = true
			authored_token[shown_key] = shown
			operations.append({"token": shown, "visible": true, "tag": String(policy.get("tag", "")), "line": int(policy.get("line", 0))})
		for hidden_value in policy.get("hide_geometry", []) as Array:
			var hidden := String(hidden_value)
			var hidden_key := hidden.to_upper()
			state_by_token[hidden_key] = false
			authored_token[hidden_key] = hidden
			operations.append({"token": hidden, "visible": false, "tag": String(policy.get("tag", "")), "line": int(policy.get("line", 0))})
	var shown_tokens: Array[String] = []
	var hidden_tokens: Array[String] = []
	var keys := state_by_token.keys()
	keys.sort()
	for key_value in keys:
		var token := String(authored_token[key_value])
		if bool(state_by_token[key_value]):
			shown_tokens.append(token)
		else:
			hidden_tokens.append(token)
	row["geometry_upgrades"] = policies
	if shown_tokens.is_empty() and hidden_tokens.is_empty():
		row.erase("geometry_visibility")
	else:
		row["geometry_visibility"] = {"show": shown_tokens, "hide": hidden_tokens, "operations": operations}


func _step_geometry_upgrades() -> void:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		var row := _sim.entities[entity_id] as Dictionary
		if not row.has("geometry_upgrades"):
			_sim._attach_module_contracts(row)
		_reconcile_geometry_upgrades(row)
	for structure_id in _sim.structure_ids():
		var row := _sim.structures[structure_id] as Dictionary
		if not row.has("geometry_upgrades"):
			_sim._attach_structure_module_contracts(row)
		_reconcile_geometry_upgrades(row)


func _emotion_expression_value(fields: Dictionary, key: String, unsupported: Array[String]) -> float:
	var raw: Variant = fields.get(key, {})
	if typeof(raw) != TYPE_DICTIONARY:
		return 0.0
	var field := raw as Dictionary
	if typeof(field.get("value")) in [TYPE_INT, TYPE_FLOAT]:
		return float(field.get("value"))
	var expression := String(field.get("expression", ""))
	var defines := sim._rules.get("emotion_range_defines", {}) as Dictionary
	if typeof(defines.get(expression)) in [TYPE_INT, TYPE_FLOAT]:
		return float(defines[expression])
	if expression != "":
		unsupported.append("unresolved_expression:%s=%s" % [key, expression])
	return 0.0


func _attach_emotion_tracker_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("emotion_tracker"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var unsupported: Array[String] = []
	var emotions: Array[Dictionary] = []
	var emotion_value: Variant = fields.get("AddEmotion", [])
	if typeof(emotion_value) != TYPE_ARRAY:
		return
	for emotion_row_value in emotion_value as Array:
		if typeof(emotion_row_value) != TYPE_DICTIONARY:
			return
		var emotion_row := emotion_row_value as Dictionary
		var name := String(emotion_row.get("name", "")).strip_edges()
		if name == "":
			return
		var duration_ticks := -1
		if emotion_row.has("Duration"):
			var duration = emotion_row.get("Duration", {}) as Dictionary
			if typeof(duration.get("milliseconds")) not in [TYPE_INT, TYPE_FLOAT]:
				return
			duration_ticks = _sim._ship_contract_delay_ticks(float(duration.get("milliseconds")))
		emotions.append({"name": name, "override": bool(emotion_row.get("override", false)), "duration_ticks": duration_ticks, "line": int(emotion_row.get("line", 0))})
	var quarrel := fields.get("QuarrelProbability", {}) as Dictionary
	var quarrel_fraction := float(quarrel.get("fraction", 0.0))
	if quarrel_fraction > 0.0:
		unsupported.append("quarrel_requires_retail_idle-social-pairing")
	if fields.has("TauntAndPointDistance") or fields.has("PointAt") or fields.has("TauntAndPointExcluded"):
		unsupported.append("taunt_and_point_requires_presentation_pairing")
	row["emotion_tracker"] = {
		"afraid_of": _sim._typed_contract_tokens(fields, "AfraidOf"),
		"always_afraid_of": _sim._typed_contract_tokens(fields, "AlwaysAfraidOf"),
		"fear_scan_distance_source": _emotion_expression_value(fields, "FearScanDistance", unsupported),
		"taunt_distance_source": _emotion_expression_value(fields, "TauntAndPointDistance", unsupported),
		"hero_scan_distance_source": _emotion_expression_value(fields, "HeroScanDistance", unsupported),
		"taunt_update_ticks": _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields, "TauntAndPointUpdateDelay", 0.0))),
		"quarrel_fraction": quarrel_fraction,
		"immune_to_fear_level": int(_sim._module_contract_value(fields, "ImmuneToFearLevel", 0)),
		"ignore_veterancy": bool(_sim._module_contract_value(fields, "IgnoreVeterancy", false)),
		"emotions": emotions,
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0)),
	}


func trigger_entity_emotion(entity_id: int, emotion_name: String, duration_ticks: int = -1) -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row := _sim.entities[entity_id] as Dictionary
	if not row.has("emotion_tracker"):
		_sim._attach_module_contracts(row)
	var policy := row.get("emotion_tracker", {}) as Dictionary
	if policy.is_empty():
		return {"ok": false, "reason": "typed-emotion-tracker-missing"}
	var selected: Dictionary = {}
	for emotion_value in policy.get("emotions", []) as Array:
		var emotion := emotion_value as Dictionary
		if String(emotion.get("name", "")).to_upper() == emotion_name.to_upper():
			selected = emotion
			break
	if selected.is_empty():
		return {"ok": false, "reason": "emotion-not-authored:%s" % emotion_name}
	var authored_ticks := int(selected.get("duration_ticks", -1))
	var effective_ticks := authored_ticks if authored_ticks >= 0 else duration_ticks
	if effective_ticks < 0:
		return {"ok": false, "reason": "emotion-duration-unresolved"}
	row["active_emotion"] = String(selected.get("name", ""))
	row["active_emotion_until_tick"] = _sim.tick_index + effective_ticks
	row["active_emotion_override"] = bool(selected.get("override", false))
	_sim._emit_event("emotion.triggered", entity_id, 0, {"emotion": row["active_emotion"], "duration_ticks": effective_ticks, "presentation": "emotion-nugget-animation-unresolved"})
	return {"ok": true, "reason": "", "emotion": row["active_emotion"], "duration_ticks": effective_ticks}


func _step_emotion_trackers() -> void:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		var row := _sim.entities[entity_id] as Dictionary
		if not row.has("emotion_tracker"):
			_sim._attach_module_contracts(row)
		if row.has("active_emotion_until_tick") and _sim.tick_index >= int(row.get("active_emotion_until_tick", -1)):
			row.erase("active_emotion")
			row.erase("active_emotion_until_tick")
			row.erase("active_emotion_override")
			_sim._emit_event("emotion.expired", entity_id, 0, {})


