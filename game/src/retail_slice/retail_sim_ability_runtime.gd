extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Hero-ability runtime carved out of retail_slice_sim.gd (drawer 18): cast_ability dispatch, activate-module graphs, special-power validation, deploy/disguise/dominate/grab/volley/capture channels, timed modifiers, leadership auras, per-tick hero ability stepper.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func ability_rules_for_unit(unit_type: String) -> Array:
	return (sim._unit_ability_rules.get(unit_type, []) as Array).duplicate(true)


func _ensure_capture_building_ability(unit_type: String, document: Dictionary) -> void:
	## Hordes author Command_CaptureBuilding on the CommandSet but do not
	## compile CaptureBuilding.inc as an ability row (AISpecialPowerUpdate is
	## unsupported). Bind the shared include so infantry can capture flags.
	var existing: Array = sim._unit_ability_rules.get(unit_type, []) as Array
	for rule_value in existing:
		var effect: Dictionary = (rule_value as Dictionary).get("effect", {})
		if String(effect.get("kind", "")) == "capture-building":
			return
	var has_command := false
	for command_value in sim.PlayableUnitAdapter.selection_commands(document):
		if String((command_value as Dictionary).get("commandId", "")) == "Command_CaptureBuilding":
			has_command = true
			break
	if not has_command:
		return
	var capture_rows: Array[Dictionary] = [{
		"ability_id": "Command_CaptureBuilding",
		"slot": 12,
		"special_power_id": "SpecialAbilityCaptureBuilding",
		"targeting": "enemy-object",
		"cooldown_ticks": 0,
		"required_level": 1,
		"level_gate_resolved": true,
		"castable": true,
		"availability_reason": "",
		"limitations": ["capture uses CaptureBuilding.inc StartAbilityRange/PreparationTime"],
		"effect": {
			"kind": "capture-building",
			"startAbilityRange": sim.CAPTURE_BUILDING_RANGE_SOURCE,
			"unpackMs": sim.CAPTURE_BUILDING_UNPACK_MS,
			"preparationMs": sim.CAPTURE_BUILDING_PREPARATION_MS,
			"packMs": sim.CAPTURE_BUILDING_PACK_MS,
			"doCaptureFx": true,
			"sourceIni": "data/ini/object/includes/CaptureBuilding.inc",
		},
		"icon_id": "UPBeacon",
		"label_id": "CONTROLBAR:CaptureBuilding",
		"tooltip_id": "CONTROLBAR:ToolTipCaptureBuilding",
		"fallback_label": "Capture Building",
		"fallback_tooltip": "Take control of targeted structure",
	}]
	var scaled = sim._scaled_ability_rules(
		capture_rows, float(sim._rules.get("source_map_transform_scale", 0.0))
	)
	var combined: Array = existing.duplicate()
	combined.append_array(scaled)
	sim._unit_ability_rules[unit_type] = combined


func ability_states_for(hero_id: int) -> Dictionary:
	if not sim.entities.has(hero_id):
		return {}
	return ((sim.entities[hero_id] as Dictionary).get("ability_states", {}) as Dictionary).duplicate(true)


func _ability_object_kind_tokens(row: Dictionary) -> Array[String]:
	var tokens: Array[String] = ["ANY"]
	for token_value in row.get("kind_of", []) as Array:
		var token := String(token_value).to_upper()
		if token != "" and not tokens.has(token):
			tokens.append(token)
	var kind = String(sim.ABILITY_CATEGORY_KINDS.get(String(row.get("category", "")), ""))
	if kind != "":
		tokens.append(kind)
	if String(row.get("horde_id", "")) != "" and String(row.get("unit_type", "")) != String(row.get("horde_id", "")):
		tokens.append("HORDE")
	return tokens


func _ability_filter_accepts(row: Dictionary, filter_text: String) -> bool:
	## Authored HealAffects/KindOf subset: ANY, +include, -exclude terms.
	if filter_text.strip_edges() == "":
		return true
	var kinds := _ability_object_kind_tokens(row)
	var included := false
	for term_value in filter_text.split(" ", false):
		var term := String(term_value)
		if term == "" or term == "NONE" or term == "ALLIES" or term == "ENEMIES":
			continue
		if term.begins_with("-"):
			if kinds.has(term.trim_prefix("-")):
				return false
		elif term.begins_with("+"):
			if kinds.has(term.trim_prefix("+")):
				included = true
		elif kinds.has(term):
			included = true
	return included


func cast_ability(hero_id: int, ability_id: String, target_point: Vector2, team: int = -1) -> Dictionary:
	## Cast one converted hero ability, validating ownership, evidence,
	## cooldown, and the authored level gate before applying the bound effect.
	## `team` >= 0 is the issuing seat (the lockstep command path always passes
	## it): a peer can only cast ITS OWN hero's abilities — fail-closed.
	if not sim.entities.has(hero_id):
		return {"ok": false, "reason": "unknown-hero"}
	var row: Dictionary = sim.entities[hero_id]
	if team >= 0 and int(row.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "unit-defeated"}
	var rule: Dictionary = {}
	for rule_value in sim._unit_ability_rules.get(String(row.get("unit_type", "")), []) as Array:
		if String((rule_value as Dictionary).get("ability_id", "")) == ability_id:
			rule = rule_value as Dictionary
			break
	if rule.is_empty():
		return {"ok": false, "reason": "unknown-ability"}
	if not bool(rule.get("castable", false)):
		var unavailable := String(rule.get("availability_reason", ""))
		return {"ok": false, "reason": "unimplemented" if unavailable == "" else "unimplemented:%s" % unavailable}
	if not bool(rule.get("level_gate_resolved", true)):
		return {"ok": false, "reason": "level-gate-unresolved"}
	var level := int(row.get("level", 1))
	var required_level := int(rule.get("required_level", 1))
	if level < required_level:
		return {"ok": false, "reason": "level-required", "level": level, "required_level": required_level}
	var states: Dictionary = row.get("ability_states", {}) as Dictionary
	var state: Dictionary = states.get(ability_id, {}) as Dictionary
	var ready_tick := int(state.get("cooldown_ready_tick", 0))
	var power_contract := rule.get("special_power_contract", {}) as Dictionary
	var shared_key := "%d:%s" % [int(row.get("team", -1)), String(rule.get("special_power_id", ability_id))]
	if bool(power_contract.get("sharedSyncedTimer", false)):
		ready_tick = maxi(ready_tick, int(sim._shared_ability_cooldowns.get(shared_key, 0)))
	if sim.tick_index < ready_tick:
		return {"ok": false, "reason": "cooldown-active", "ready_tick": ready_tick}
	var effect: Dictionary = rule.get("effect", {}) as Dictionary
	var targeting := String(rule.get("targeting", "self"))
	var hero_position := Vector2(row.get("position", Vector2.ZERO))
	if targeting != "self":
		var range_limit := float(effect.get("range", 0.0))
		var max_cast_range := float(power_contract.get("maxCastRangeScaled", 0.0))
		if max_cast_range > 0.0:
			range_limit = max_cast_range if range_limit <= 0.0 else minf(range_limit, max_cast_range)
		if range_limit > 0.0 and hero_position.distance_to(target_point) > range_limit:
			return {"ok": false, "reason": "out-of-range"}
	var activation_gate := _validate_special_power_activation(row, power_contract, targeting, target_point)
	if not bool(activation_gate.get("ok", false)):
		return activation_gate
	var effect_kind := String(effect.get("kind", "none"))
	var result: Dictionary = {}
	match effect_kind:
		"weapon-blast":
			if targeting == "enemy-object" and _ability_enemies_near(int(row.get("team", -1)), target_point, maxf(float(effect.get("damage_radius", 0.0)), 1.5)).is_empty():
				return {"ok": false, "reason": "no-target"}
			result = _apply_ability_weapon_blast(row, effect, target_point)
		"heal":
			var epicenter := target_point if targeting == "point" else hero_position
			result = _apply_ability_heal(row, effect, epicenter)
		"attribute-modifier":
			result = _apply_ability_modifier(row, ability_id, effect)
		"summon":
			result = _apply_ability_summon(row, effect, target_point if targeting == "point" else hero_position)
		"weapon-toggle":
			result = _apply_ability_weapon_toggle(row, effect)
		"terror":
			result = _apply_ability_terror(row, ability_id, effect)
		"mount-toggle":
			result = _apply_ability_mount_toggle(row, effect)
		"capture-building":
			result = _apply_ability_capture_building(row, effect, target_point)
		"experience-grant":
			result = _apply_ability_experience_grant(row, effect, target_point if targeting == "point" else hero_position)
		"arrow-storm":
			result = _apply_ability_arrow_storm(row, effect, target_point if targeting == "point" else hero_position)
		"stealth-toggle":
			result = _apply_ability_stealth_toggle(row, effect)
		"teleport":
			result = _apply_ability_teleport(row, effect, target_point)
		"curse":
			result = _apply_ability_curse(row, effect, target_point)
		"leadership-strip":
			result = _apply_ability_leadership_strip(row, effect)
		"activate-module-graph":
			result = _apply_ability_activate_module_graph(row, ability_id, effect, target_point, targeting)
		"weapon-mode-special-power":
			result = _apply_ability_weapon_mode_special_power(row, effect)
		"dominate-enemy":
			result = _apply_ability_dominate_enemy(row, ability_id, effect, target_point, targeting)
		"grab-passenger":
			result = _apply_ability_grab_passenger(row, ability_id, effect, target_point)
		"fling-passenger":
			result = _apply_ability_fling_passenger(row, ability_id, effect)
		"repair-structure":
			result = _apply_ability_repair_structure(row, ability_id, effect, target_point)
		"stop-special-power":
			result = sim.activate_stop_special_power(hero_id, String(effect.get("specialPowerTemplateId", "")), int(row.get("team", -1)))
		"siege-deploy":
			result = _apply_ability_siege_deploy(row, effect, target_point, power_contract)
		"toggle-deploy":
			result = _apply_ability_toggle_deploy(row, effect)
		"special-disguise":
			result = _apply_ability_special_disguise(row, effect)
		"unleash-special-power":
			result = sim.activate_unleash_special_power(hero_id, String(effect.get("specialPowerTemplateId", "")), int(row.get("team", -1)))
		_:
			return {"ok": false, "reason": "no-effect"}
	if not bool(result.get("ok", false)):
		return result
	if effect_kind != "stealth-toggle":
		# Retail InvisibilityNugget ForbiddenConditions: casting another
		# ability while cloaked drops a USING_ABILITY-forbidden stealth.
		_break_stealth(row, "USING_ABILITY")
	state["cooldown_ready_tick"] = sim.tick_index + int(rule.get("cooldown_ticks", 0))
	if bool(power_contract.get("sharedSyncedTimer", false)):
		sim._shared_ability_cooldowns[shared_key] = state["cooldown_ready_tick"]
	states[ability_id] = state
	row["ability_states"] = states
	_apply_special_power_unit_cost(row, power_contract)
	sim._emit_event("ability.cast", hero_id, 0, {
		"team": int(row.get("team", -1)),
		"ability_id": ability_id,
		"special_power_id": String(rule.get("special_power_id", "")),
		"effect_kind": effect_kind,
		"affected": int(result.get("affected", 0)),
		"summoned": result.get("summoned", []),
		"sound_id": String(rule.get("initiate_sound_id", rule.get("unit_specific_sound_id", ""))),
		# Authored FX identity and the map-scaled radii the converted leaf gave
		# this cast. The presentation layer had no way to tell one ability's
		# cue from another's before this; spellbook casts already carried their
		# fx_lists on power.cast, hero abilities carried nothing.
		"fx_lists": _ability_fx_list_ids(effect),
		"fx_radius": snappedf(_ability_fx_radius(effect), 0.001),
		"damage_type": String(effect.get("damageType", "")),
		"point": [snappedf(target_point.x, 0.001), snappedf(target_point.y, 0.001)],
	})
	return result


func _apply_ability_activate_module_graph(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2, targeting: String) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_activate_module_graph(row, ability_id, effect, target_point, targeting)


func _activate_module_target_identity(row: Dictionary, target_point: Vector2, targeting: String) -> Dictionary:
	if targeting == "self":
		return {"id": int(row.get("id", 0)), "kind": "battalion"}
	var best: Dictionary = {}
	var best_distance := 2.0
	for entity_id in sim.entity_ids():
		var distance = Vector2((sim.entities[entity_id] as Dictionary).get("position", target_point)).distance_to(target_point)
		if distance <= best_distance:
			best_distance = distance; best = {"id": entity_id, "kind": "battalion"}
	for structure_id in sim.structure_ids():
		var distance = Vector2((sim.structures[structure_id] as Dictionary).get("position", target_point)).distance_to(target_point)
		if distance <= best_distance:
			best_distance = distance; best = {"id": structure_id, "kind": "structure"}
	if not best.is_empty():
		return best
	# Command APIs currently carry an object selection as a point. Retain the
	# live attack target only as a fallback when no object occupies that point;
	# this prevents a stale combat target hijacking a newly clicked power target.
	var target_id := int(row.get("target_id", 0))
	var target_kind := String(row.get("target_kind", "battalion"))
	if target_kind == "battalion" and sim.entities.has(target_id):
		return {"id": target_id, "kind": target_kind}
	if target_kind == "structure" and sim.structures.has(target_id):
		return {"id": target_id, "kind": target_kind}
	return best


func _step_activate_module_graph(row: Dictionary) -> void:
	var channel := row.get("activate_module_channel", {}) as Dictionary
	if channel.is_empty():
		return
	var unavoidable_interrupt = int(row.get("health", 0)) <= 0 or bool(row.get("knocked_down", false)) or sim.tick_index < int(row.get("stun_until_tick", -1)) or sim.tick_index < int(row.get("cower_until_tick", -1))
	var voluntary_interrupt := not bool(channel.get("must_finish", false)) and int(row.get("order_sequence", 0)) != int(channel.get("order_sequence_at_start", 0))
	if unavoidable_interrupt or voluntary_interrupt:
		row.erase("activate_module_channel")
		sim._emit_event("ability.graph_interrupted", int(row.get("id", 0)), int(channel.get("current_target_id", 0)), {"ability_id": channel.get("ability_id"), "unavoidable": unavoidable_interrupt, "must_finish": channel.get("must_finish")})
		return
	if not bool(channel.get("dispatched", false)) and sim.tick_index >= int(channel.get("activation_tick", 0)):
		var results: Array = []
		for route_value in channel.get("routes", []) as Array:
			var route := route_value as Dictionary
			var target := _activate_module_route_target(row, channel, String(route.get("targetMode", "")))
			var result := {"ok": false, "reason": "activate-module-target-lost"}
			if bool(target.get("ok", false)):
				var leaf := _activate_module_effect_range(route.get("effect", {}) as Dictionary, float(channel.get("effect_range_scaled", 0.0)))
				leaf["moduleTag"] = String(route.get("moduleTag", ""))
				result = _dispatch_activate_module_leaf(row, String(channel.get("ability_id", "")), leaf, Vector2(target.get("point", row.get("position", Vector2.ZERO))))
			var receipt := {"module_tag": String(route.get("moduleTag", "")), "target_mode": String(route.get("targetMode", "")), "ok": bool(result.get("ok", false)), "reason": String(result.get("reason", ""))}
			results.append(receipt)
			sim._emit_event("ability.graph_route", int(row.get("id", 0)), int(target.get("id", 0)), receipt)
		channel["route_results"] = results
		channel["dispatched"] = true
		row["activate_module_channel"] = channel
	if sim.tick_index >= int(channel.get("finish_tick", 0)):
		row.erase("activate_module_channel")
		row["state"] = "idle"
		sim._emit_event("ability.graph_finished", int(row.get("id", 0)), int(channel.get("current_target_id", 0)), {"ability_id": channel.get("ability_id"), "route_results": channel.get("route_results", [])})


func _activate_module_route_target(row: Dictionary, channel: Dictionary, mode: String) -> Dictionary:
	if mode == "SELF":
		return {"ok": true, "id": int(row.get("id", 0)), "kind": "battalion", "point": Vector2(row.get("position", Vector2.ZERO))}
	if mode == "LOCATION":
		return {"ok": true, "id": 0, "kind": "point", "point": Vector2(channel.get("location", row.get("position", Vector2.ZERO)))}
	if mode == "CURRENT_TARGET":
		var target_id := int(channel.get("current_target_id", 0)); var kind := String(channel.get("current_target_kind", ""))
		if kind == "battalion" and sim.entities.has(target_id) and int((sim.entities[target_id] as Dictionary).get("health", 0)) > 0:
			return {"ok": true, "id": target_id, "kind": kind, "point": Vector2((sim.entities[target_id] as Dictionary).get("position", Vector2.ZERO))}
		if kind == "structure" and sim.structures.has(target_id) and int((sim.structures[target_id] as Dictionary).get("health", 0)) > 0:
			return {"ok": true, "id": target_id, "kind": kind, "point": Vector2((sim.structures[target_id] as Dictionary).get("position", Vector2.ZERO))}
	return {"ok": false, "id": 0, "kind": mode, "point": Vector2.ZERO}


func _activate_module_effect_range(effect: Dictionary, effect_range: float) -> Dictionary:
	var leaf := effect.duplicate(true)
	if effect_range <= 0.0:
		return leaf
	match String(leaf.get("kind", "")):
		"weapon-blast": if float(leaf.get("damage_radius", 0.0)) <= 0.0: leaf["damage_radius"] = effect_range
		"heal", "curse", "leadership-strip": if float(leaf.get("radius_scaled", 0.0)) <= 0.0: leaf["radius_scaled"] = effect_range
		"attribute-modifier": if float(leaf.get("range_scaled", 0.0)) <= 0.0: leaf["range_scaled"] = effect_range
		"experience-grant": if float(leaf.get("radius_scaled", 0.0)) <= 0.0: leaf["radius_scaled"] = effect_range
		"arrow-storm": if float(leaf.get("target_radius_scaled", 0.0)) <= 0.0: leaf["target_radius_scaled"] = effect_range
	return leaf


func _dispatch_activate_module_leaf(row: Dictionary, ability_id: String, effect: Dictionary, point: Vector2) -> Dictionary:
	match String(effect.get("kind", "")):
		"weapon-blast": return _apply_ability_weapon_blast(row, effect, point)
		"heal": return _apply_ability_heal(row, effect, point)
		"attribute-modifier": return _apply_ability_modifier(row, "%s:%s" % [ability_id, String(effect.get("moduleTag", "route"))], effect)
		"summon": return _apply_ability_summon(row, effect, point)
		"weapon-toggle": return _apply_ability_weapon_toggle(row, effect)
		"terror": return _apply_ability_terror(row, ability_id, effect)
		"mount-toggle": return _apply_ability_mount_toggle(row, effect)
		"experience-grant": return _apply_ability_experience_grant(row, effect, point)
		"arrow-storm": return _apply_ability_arrow_storm(row, effect, point)
		"stealth-toggle": return _apply_ability_stealth_toggle(row, effect)
		"teleport": return _apply_ability_teleport(row, effect, point)
		"curse": return _apply_ability_curse(row, effect, point)
		"leadership-strip": return _apply_ability_leadership_strip(row, effect)
		"trigger-fx":
			sim._emit_event("ability.graph_fx", int(row.get("id", 0)), 0, {"fx_id": String(effect.get("fxId", "")), "point": point})
			return {"ok": true, "reason": "", "affected": 0, "presentation_only": true}
	return {"ok": false, "reason": "activate-module-leaf-unsupported:%s" % String(effect.get("kind", ""))}


func _validate_special_power_activation(row: Dictionary, contract: Dictionary, targeting: String, target_point: Vector2) -> Dictionary:
	for condition_value in contract.get("preventActivationConditions", []) as Array:
		var condition := String(condition_value).to_upper()
		if condition == "MOVING" and (not (row.get("route", []) as Array).is_empty() or float(row.get("current_speed", 0.0)) > 0.0):
			return {"ok": false, "reason": "activation-condition:MOVING"}
		if condition.begins_with("FIRING") and String(row.get("state", "")) == "attack":
			return {"ok": false, "reason": "activation-condition:%s" % condition}
		if bool((row.get("object_status", {}) as Dictionary).get(condition, false)):
			return {"ok": false, "reason": "activation-condition:%s" % condition}
	# Retail's SpecialPower validator passes the candidate destination to the
	# NO_FORBIDDEN_OBJECTS partition query. Targeted casts therefore scan around
	# their destination, not around the caster.
	var origin := Vector2(row.get("position", Vector2.ZERO)) if targeting == "self" else target_point
	var flags: Array = contract.get("flags", []) as Array
	if flags.has("PATHABLE_ONLY"):
		# Retail's PATHABLE_ONLY is a target-location admission rule.  Unlike
		# ordinary movement helpers, this must fail closed when no map navigation
		# authority is attached: silently treating an unknown cell as walkable
		# would consume the power on a target retail refuses.
		if (
			sim.route_provider == null
			or not sim.route_provider.has_method("is_local_inside_navigation")
			or not sim.route_provider.has_method("local_to_grid_cell")
			or not sim.route_provider.has_method("is_navigation_walkable")
			or not bool(sim.route_provider.call("is_local_inside_navigation", target_point))
			or not bool(sim.route_provider.call("is_navigation_walkable", sim.route_provider.call("local_to_grid_cell", target_point)))
		):
			return {"ok": false, "reason": "target-unpathable"}
	elif targeting == "point" and not flags.has("WATER_OK"):
		# WATER_OK is PATHABLE_ONLY's complement and retail never authors the
		# two together: it is the permission to land a point power on a water
		# cell. RotWK authors it on 24 SpecialPowers — Drogoth's Incinerate and
		# the spellbook powers dropped across rivers — so a point power that
		# does NOT carry it refuses the same cell. The refusal only fires on a
		# cell a map authority calls water; with no authority attached there is
		# no water to refuse, and nothing is substituted for the answer.
		if (
			sim.route_provider != null
			and sim.route_provider.has_method("is_local_inside_navigation")
			and sim.route_provider.has_method("local_to_grid_cell")
			and sim.route_provider.has_method("is_water_cell")
			and bool(sim.route_provider.call("is_local_inside_navigation", target_point))
			and bool(sim.route_provider.call("is_water_cell", sim.route_provider.call("local_to_grid_cell", target_point)))
		):
			return {"ok": false, "reason": "target-over-water"}
	var forbidden_range := float(contract.get("forbiddenObjectRangeScaled", 0.0))
	if forbidden_range > 0.0:
		for candidate_id in sim.entity_ids():
			if candidate_id == int(row.get("id", 0)):
				continue
			var candidate = sim.entities[candidate_id] as Dictionary
			if origin.distance_to(Vector2(candidate.get("position", origin))) <= forbidden_range and _ability_token_filter_accepts(candidate, contract.get("forbiddenObjectFilter", []) as Array):
				return {"ok": false, "reason": "forbidden-object-nearby", "object_id": candidate_id}
		for structure_id in sim.structure_ids():
			var candidate = sim.structures[structure_id] as Dictionary
			if int(candidate.get("health", 0)) <= 0:
				continue
			if origin.distance_to(Vector2(candidate.get("position", origin))) <= forbidden_range and _ability_token_filter_accepts(candidate, contract.get("forbiddenObjectFilter", []) as Array):
				return {"ok": false, "reason": "forbidden-object-nearby", "object_id": structure_id, "object_kind": "structure"}
	if targeting in ["enemy-object", "object"] and not (contract.get("objectFilter", []) as Array).is_empty():
		var matched := false
		for candidate_id in sim.entity_ids():
			var candidate = sim.entities[candidate_id] as Dictionary
			if targeting == "enemy-object" and int(candidate.get("team", -1)) == int(row.get("team", -1)):
				continue
			if Vector2(candidate.get("position", Vector2.ZERO)).distance_to(target_point) <= 1.5 and _ability_token_filter_accepts(candidate, contract.get("objectFilter", []) as Array):
				matched = true
				break
		if not matched:
			for structure_id in sim.structure_ids():
				var candidate = sim.structures[structure_id] as Dictionary
				if int(candidate.get("health", 0)) <= 0:
					continue
				if targeting == "enemy-object" and int(candidate.get("team", -1)) == int(row.get("team", -1)):
					continue
				if Vector2(candidate.get("position", Vector2.ZERO)).distance_to(target_point) <= 1.5 and _ability_token_filter_accepts(candidate, contract.get("objectFilter", []) as Array):
					matched = true
					break
		if not matched:
			return {"ok": false, "reason": "object-filter-refused"}
	return {"ok": true, "reason": ""}


func _ability_token_filter_accepts(row: Dictionary, tokens: Array) -> bool:
	if tokens.is_empty():
		return true
	var probe := {"category": String(row.get("category", "")), "kind_of": _ability_object_kind_tokens(row)}
	return sim._transport_filter_accepts(probe, tokens)


func _apply_special_power_unit_cost(row: Dictionary, contract: Dictionary) -> void:
	var cost := maxi(0, int(contract.get("unitCost", 0)))
	if cost <= 0:
		return
	var health_values: Array = row.get("member_health", []) as Array
	var consumed := 0
	for index in range(health_values.size() - 1, -1, -1):
		if consumed >= cost:
			break
		if int(health_values[index]) > 0:
			health_values[index] = 0
			consumed += 1
	row["member_health"] = health_values
	var aggregate := 0
	for health_value in health_values:
		aggregate += int(health_value)
	row["health"] = aggregate
	sim._emit_event("ability.unit_cost", int(row.get("id", 0)), 0, {"count": consumed, "death_types": contract.get("unitCostDeathTypes", [])})


func _ability_fx_list_ids(effect: Dictionary) -> Array:
	## Authored FXList ids on a converted ability leaf, in a stable order. The
	## converter emits these under kind-specific keys (fireFxId on weapon
	## leaves, healFxId on heals, levelFxId on level grants); nothing is
	## synthesised when a leaf authors none.
	var ids: Array = []
	for key in ["fireFxId", "healFxId", "levelFxId", "fxId", "dominatedFxId", "triggerFxId"]:
		var value := String(effect.get(key, ""))
		if value != "" and not ids.has(value):
			ids.append(value)
	return ids


func _ability_fx_radius(effect: Dictionary) -> float:
	## The largest map-scaled radius this ability actually acts over, so the
	## presentation cue covers exactly the ground the sim affected. Ability
	## kinds that author no radius return 0 and get no radial cue.
	var radius := 0.0
	for key in ["knockback_radius", "damage_radius", "radius_scaled", "target_radius_scaled", "dominate_radius_scaled"]:
		radius = maxf(radius, float(effect.get(key, 0.0)))
	return radius


func _ability_enemies_near(team: int, point: Vector2, radius: float) -> Array[int]:
	var result: Array[int] = []
	# Ability blasts cover a bounded disc, so this is a neighbourhood query.
	# Sorted, because callers apply damage in the returned order.
	for id in sim._spatial_gather_sorted(point, radius):
		if not sim.entities.has(id):
			continue
		var row: Dictionary = sim.entities[id]
		if int(row.get("team", -1)) == team or int(row.get("health", 0)) <= 0:
			continue
		if Vector2(row.get("position", Vector2.ZERO)).distance_to(point) <= radius:
			result.append(id)
	return result


func _apply_ability_weapon_blast(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_weapon_blast(hero_row, effect, point)


func _apply_ability_heal(hero_row: Dictionary, effect: Dictionary, epicenter: Vector2) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_heal(hero_row, effect, epicenter)


func _apply_ability_modifier(hero_row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_modifier(hero_row, ability_id, effect)


func _apply_ability_weapon_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_weapon_toggle(hero_row, effect)


func _siege_deploy_target(source: Dictionary, effect: Dictionary, point: Vector2, contract: Dictionary) -> int:
	if String(effect.get("targetMode", "")) != "TARGET_STRUCTURE":
		return 0
	var chosen_id := 0
	var chosen_distance := 1.5
	for structure_id in sim.structure_ids():
		var candidate = sim.structures[structure_id] as Dictionary
		if int(candidate.get("health", 0)) <= 0:
			continue
		var distance := Vector2(candidate.get("position", Vector2.ZERO)).distance_to(point)
		if distance > chosen_distance:
			continue
		if not _ability_token_filter_accepts(candidate, contract.get("objectFilter", []) as Array):
			continue
		if chosen_id == 0 or distance < chosen_distance or (is_equal_approx(distance, chosen_distance) and structure_id < chosen_id):
			chosen_id = structure_id
			chosen_distance = distance
	return chosen_id


func _apply_ability_siege_deploy(row: Dictionary, effect: Dictionary, point: Vector2, contract: Dictionary) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_siege_deploy(row, effect, point, contract)


func _toggle_deploy_set_model_condition(row: Dictionary, condition: String) -> void:
	var conditions: Array = (row.get("model_conditions", []) as Array).duplicate()
	for deploy_condition in ["UNPACKING", "PACKING", "DEPLOYED"]:
		conditions.erase(deploy_condition)
	if condition != "" and not conditions.has(condition):
		conditions.append(condition)
	conditions.sort()
	if conditions.is_empty():
		row.erase("model_conditions")
	else:
		row["model_conditions"] = conditions


func _toggle_deploy_set_modifier(row: Dictionary, modifiers: Array, active: bool) -> void:
	const KEY := "ability:toggle-deploy"
	var table := row.get("timed_modifiers", {}) as Dictionary
	if active:
		table[KEY] = {
			"modifiers": modifiers.duplicate(true),
			"expires_tick": -1,
			"persistent": true,
		}
	else:
		table.erase(KEY)
	if table.is_empty():
		row.erase("timed_modifiers")
	else:
		row["timed_modifiers"] = table


func _apply_ability_toggle_deploy(row: Dictionary, effect: Dictionary) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_toggle_deploy(row, effect)


func _step_toggle_deploy(row: Dictionary) -> void:
	var channel := row.get("toggle_deploy_channel", {}) as Dictionary
	if channel.is_empty() or sim.tick_index < int(channel.get("phase_end_tick", 0)):
		return
	var phase := String(channel.get("phase", ""))
	var statuses := row.get("object_status", {}) as Dictionary
	if phase == "unpacking":
		statuses["DEPLOYED"] = true
		row["object_status"] = statuses
		_toggle_deploy_set_model_condition(row, "DEPLOYED")
		_toggle_deploy_set_modifier(row, channel.get("modifiers", []) as Array, true)
		row["toggle_deployed"] = true
		row.erase("toggle_deploy_channel")
		row["state"] = "idle"
		sim._emit_event("ability.toggle_deployed", int(row.get("id", 0)), 0, {
			"modifier_id": channel.get("modifier_id", ""),
			"presentation_receipt": "model-condition:DEPLOYED",
		})
	elif phase == "packing":
		statuses.erase("DEPLOYED")
		if statuses.is_empty():
			row.erase("object_status")
		else:
			row["object_status"] = statuses
		_toggle_deploy_set_model_condition(row, "")
		_toggle_deploy_set_modifier(row, [], false)
		row.erase("toggle_deployed")
		row.erase("toggle_deploy_channel")
		row["state"] = "idle"
		sim._emit_event("ability.toggle_undeployed", int(row.get("id", 0)), 0, {
			"modifier_id": channel.get("modifier_id", ""),
			"presentation_receipt": "model-condition:CLEAR_DEPLOYED",
		})


func _step_siege_deploy(row: Dictionary) -> void:
	var channel := row.get("siege_deploy_channel", {}) as Dictionary
	if channel.is_empty():
		return
	var phase := String(channel.get("phase", ""))
	if phase == "lowering" and sim.tick_index >= int(channel.get("phase_end_tick", 0)):
		var statuses := row.get("object_status", {}) as Dictionary
		statuses["DEPLOYED"] = true
		row["object_status"] = statuses
		row["siege_deployed"] = true
		channel["phase"] = "deployed"
		channel["phase_end_tick"] = -1
		row["siege_deploy_channel"] = channel
		var evacuated: Array[int] = []
		if bool(channel.get("evacuate_passengers", false)):
			var passenger_ids = (sim.containment.get(int(row.get("id", 0)), []) as Array).duplicate()
			passenger_ids.sort()
			for passenger_value in passenger_ids:
				var passenger_id := int(passenger_value)
				sim._finish_transport_exit(int(row.get("id", 0)), passenger_id)
				evacuated.append(passenger_id)
		sim._emit_event("ability.siege_deployed", int(row.get("id", 0)), int(channel.get("target_id", 0)), {"evacuated_ids": evacuated, "model_receipts": channel.get("model_receipts", [])})
		return
	if phase == "retracting" and sim.tick_index >= int(channel.get("phase_end_tick", 0)):
		var statuses := row.get("object_status", {}) as Dictionary
		statuses.erase("DEPLOYED")
		if statuses.is_empty():
			row.erase("object_status")
		else:
			row["object_status"] = statuses
		row.erase("siege_deployed")
		row.erase("siege_deploy_channel")
		row["state"] = "idle"
		sim._emit_event("ability.siege_retracted", int(row.get("id", 0)), int(channel.get("target_id", 0)), {"model_receipts": channel.get("model_receipts", [])})


func _apply_ability_weapon_mode_special_power(row: Dictionary, effect: Dictionary) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_weapon_mode_special_power(row, effect)


func _attach_weapon_mode_special_power_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	var template = String(sim._module_contract_value(fields, "SpecialPowerTemplate", "")).strip_edges()
	if template == "":
		return
	var policies: Array = row.get("weapon_mode_special_powers", []) as Array
	var key := "%s:%d" % [String(contract.get("tag", "")), int(contract.get("line", 0))]
	for existing_value in policies:
		if String((existing_value as Dictionary).get("key", "")) == key:
			return
	var duration_field := fields.get("Duration", {}) as Dictionary
	var duration_ms := -1.0
	var unsupported: Array[String] = []
	if duration_field.has("milliseconds"):
		duration_ms = float(duration_field.get("milliseconds", -1.0))
	else:
		var define := String(duration_field.get("define", ""))
		var defines = sim._rules.get("weapon_mode_duration_defines", {}) as Dictionary
		if define != "" and typeof(defines.get(define)) in [TYPE_INT, TYPE_FLOAT] and float(defines[define]) >= 0.0:
			duration_ms = float(defines[define])
		else:
			unsupported.append("unresolved_duration_define:%s" % define)
	var modifier_name = String(sim._module_contract_value(fields, "AttributeModifier", ""))
	var modifier = ((sim._rules.get("attribute_modifier_rules", {}) as Dictionary).get(modifier_name, {}) as Dictionary).duplicate(true)
	if modifier_name != "" and (modifier.is_empty() or (modifier.get("effects", []) as Array).is_empty()):
		unsupported.append("unresolved_modifier_list:%s" % modifier_name)
	var flags = sim._typed_contract_tokens(fields, "WeaponSetFlags")
	var lock_slot = String(sim._module_contract_value(fields, "LockWeaponSlot", "")).to_lower()
	var mode := _resolve_weapon_mode_special_power_profile(row, flags, lock_slot)
	if (not flags.is_empty() or lock_slot != "") and mode == "":
		unsupported.append("weapon_mode_profile_unavailable:%s" % (lock_slot if lock_slot != "" else " ".join(flags)))
	policies.append({
		"key": key, "special_power_template": template,
		"duration_ticks": sim._ship_contract_delay_ticks(duration_ms) if duration_ms >= 0.0 else 0,
		"starts_paused": bool(sim._module_contract_value(fields, "StartsPaused", false)),
		"paused": bool(sim._module_contract_value(fields, "StartsPaused", false)),
		"modifier_name": modifier_name, "modifier": modifier,
		"weapon_set_flags": flags, "lock_weapon_slot": lock_slot, "mode": mode,
		"active": false, "expires_tick": -1, "prior_mode": "", "prior_toggle_mode": "", "prior_weapon_set_flags": [],
		"activation_count": 0, "unsupported_semantics": unsupported,
	})
	row["weapon_mode_special_powers"] = policies


func _resolve_weapon_mode_special_power_profile(row: Dictionary, flags: Array, lock_slot: String) -> String:
	var modes := row.get("weapon_modes", {}) as Dictionary
	var requested: Array[String] = []
	for flag in flags:
		requested.append(String(flag).to_lower())
	if lock_slot != "":
		for mode_key in modes.keys():
			if String((modes[mode_key] as Dictionary).get("weapon_slot", "")) == lock_slot:
				requested.append(String(mode_key))
	for candidate in requested:
		if modes.has(candidate):
			return candidate
	return ""


func set_weapon_mode_special_power_paused(entity_id: int, special_power_template: String, paused: bool) -> Dictionary:
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row = sim.entities[entity_id] as Dictionary
	if not row.has("weapon_mode_special_powers"):
		sim._attach_module_contracts(row)
	var policies := row.get("weapon_mode_special_powers", []) as Array
	for index in policies.size():
		var policy := policies[index] as Dictionary
		if String(policy.get("special_power_template", "")) == special_power_template:
			policy["paused"] = paused
			policies[index] = policy; row["weapon_mode_special_powers"] = policies
			return {"ok": true, "reason": "", "paused": paused}
	return {"ok": false, "reason": "weapon-mode-special-power-missing"}


func activate_weapon_mode_special_power(entity_id: int, special_power_template: String, team: int = -1) -> Dictionary:
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row = sim.entities[entity_id] as Dictionary
	if team >= 0 and int(row.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "unit-defeated"}
	if not row.has("weapon_mode_special_powers"):
		sim._attach_module_contracts(row)
	var policies := row.get("weapon_mode_special_powers", []) as Array
	for index in policies.size():
		var policy := policies[index] as Dictionary
		if String(policy.get("special_power_template", "")) != special_power_template:
			continue
		if bool(policy.get("paused", false)):
			return {"ok": false, "reason": "special-power-paused"}
		if not (policy.get("unsupported_semantics", []) as Array).is_empty():
			return {"ok": false, "reason": String((policy.get("unsupported_semantics", []) as Array)[0])}
		if bool(policy.get("active", false)):
			_end_weapon_mode_special_power(row, policy)
		policy["prior_mode"] = String(row.get("active_weapon_mode", row.get("default_weapon_mode", "default")))
		policy["prior_toggle_mode"] = String(row.get("weapon_toggle_mode", ""))
		policy["prior_weapon_set_flags"] = (row.get("weapon_set_flags", []) as Array).duplicate()
		var mode := String(policy.get("mode", ""))
		if mode != "" and not sim._apply_weapon_mode(row, mode):
			return {"ok": false, "reason": "weapon-mode-unavailable:%s" % mode}
		if mode != "":
			row["weapon_toggle_mode"] = mode
		if not (policy.get("weapon_set_flags", []) as Array).is_empty():
			row["weapon_set_flags"] = (policy.get("weapon_set_flags", []) as Array).duplicate()
		var modifier := policy.get("modifier", {}) as Dictionary
		var expiry = sim.tick_index + int(policy.get("duration_ticks", 0))
		if not modifier.is_empty():
			var modifier_key := "weapon-mode-special:%s" % special_power_template
			_set_timed_modifier(row, modifier_key, modifier.get("effects", []) as Array, expiry)
			(row.get("timed_modifiers", {}) as Dictionary)[modifier_key]["category"] = String(modifier.get("category", ""))
		policy["active"] = true; policy["expires_tick"] = expiry; policy["activation_count"] = int(policy.get("activation_count", 0)) + 1
		policies[index] = policy; row["weapon_mode_special_powers"] = policies
		sim._emit_event("ability.weapon_mode_started", entity_id, 0, {"special_power_template":special_power_template,"mode":mode,"expires_tick":expiry,"weapon_set_flags":policy.get("weapon_set_flags"),"lock_weapon_slot":policy.get("lock_weapon_slot")})
		return {"ok": true, "reason": "", "mode": mode, "expires_tick": expiry}
	return {"ok": false, "reason": "weapon-mode-special-power-missing"}


func _step_weapon_mode_special_powers(row: Dictionary) -> void:
	var policies := row.get("weapon_mode_special_powers", []) as Array
	for index in policies.size():
		var policy := policies[index] as Dictionary
		if bool(policy.get("active", false)) and sim.tick_index >= int(policy.get("expires_tick", 0)):
			_end_weapon_mode_special_power(row, policy)
			policies[index] = policy
	if policies.is_empty():
		row.erase("weapon_mode_special_powers")
	else:
		row["weapon_mode_special_powers"] = policies


func _end_weapon_mode_special_power(row: Dictionary, policy: Dictionary) -> void:
	var prior := String(policy.get("prior_mode", row.get("default_weapon_mode", "default")))
	if prior != "":
		sim._apply_weapon_mode(row, prior)
	row["weapon_toggle_mode"] = String(policy.get("prior_toggle_mode", ""))
	row["weapon_set_flags"] = (policy.get("prior_weapon_set_flags", []) as Array).duplicate()
	var table := row.get("timed_modifiers", {}) as Dictionary
	table.erase("weapon-mode-special:%s" % String(policy.get("special_power_template", "")))
	if table.is_empty(): row.erase("timed_modifiers")
	else: row["timed_modifiers"] = table
	policy["active"] = false; policy["expires_tick"] = -1
	sim._emit_event("ability.weapon_mode_finished", int(row.get("id", 0)), 0, {"special_power_template":policy.get("special_power_template"),"restored_mode":prior})


func _apply_ability_dominate_enemy(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2, targeting: String) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_dominate_enemy(row, ability_id, effect, target_point, targeting)


func _apply_ability_grab_passenger(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_grab_passenger(row, ability_id, effect, target_point)


func _apply_ability_repair_structure(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_repair_structure(row, ability_id, effect, target_point)


func _step_repair_structure(row: Dictionary) -> void:
	var channel := row.get("repair_structure_channel", {}) as Dictionary
	if channel.is_empty():
		return
	var target_id := int(channel.get("target_id", 0))
	if int(row.get("health", 0)) <= 0 or int(row.get("order_sequence", 0)) != int(channel.get("order_sequence_at_start", 0)) or not sim.structures.has(target_id):
		row.erase("repair_structure_channel")
		sim._emit_event("ability.repair_interrupted", int(row.get("id", 0)), target_id)
		return
	var structure = sim.structures[target_id] as Dictionary
	if int(structure.get("team", -1)) != int(row.get("team", -2)) or int(structure.get("health", 0)) <= 0:
		row.erase("repair_structure_channel"); return
	var maximum := maxi(1, int(structure.get("maximum_health", structure.get("health", 1))))
	if int(structure.get("health", 0)) >= maximum:
		row.erase("repair_structure_channel"); row["state"] = "idle"
		sim._emit_event("ability.repair_finished", int(row.get("id", 0)), target_id, {"health": maximum})
		return
	var amount = float(channel.get("fractional_health", 0.0)) + maximum * float(channel.get("max_health_fraction_per_second", 0.0)) * sim.TICK_SECONDS
	var whole := floori(amount)
	channel["fractional_health"] = amount - whole
	if whole > 0:
		structure["health"] = mini(maximum, int(structure.get("health", 0)) + whole)
	row["repair_structure_channel"] = channel
	sim._emit_event("ability.repair_tick", int(row.get("id", 0)), target_id, {"applied": whole, "health": int(structure.get("health", 0)), "fractional_health": channel.get("fractional_health")})


func _grab_passenger_target(source: Dictionary, effect: Dictionary, point: Vector2) -> int:
	var best_id := 0
	var best_distance := 1.5
	var admission := effect.get("targetAdmission", {}) as Dictionary
	var contain := effect.get("containment", {}) as Dictionary
	var tree_ids := admission.get("treeObjectIds", []) as Array
	var manual_filter: Array = String(admission.get("passengerFilter", "")).split(" ", false)
	for target_id in sim.entity_ids():
		if target_id == int(source.get("id", 0)) or sim.entity_container.has(target_id):
			continue
		var target = sim.entities[target_id] as Dictionary
		if int(target.get("health", 0)) <= 0:
			continue
		var distance := Vector2(target.get("position", Vector2.ZERO)).distance_to(point)
		if distance > best_distance:
			continue
		var source_object_id := String(target.get("source_object_id", target.get("object_id", "")))
		var exact_tree := bool(effect.get("allowTree", false)) and tree_ids.has(source_object_id)
		if bool(effect.get("allowTree", false)) and not exact_tree:
			continue
		if not sim._transport_filter_accepts(target, manual_filter):
			continue
		var relation = sim.team_relationship(int(source.get("team", -1)), int(target.get("team", -2)))
		var allowed = (
			(relation == "local" and bool(contain.get("allowAlliesInside", false)))
			or (relation == "allied" and bool(contain.get("allowAlliesInside", false)))
			or (relation == "enemy" and bool(contain.get("allowEnemiesInside", false)))
			or (relation == "unavailable" and bool(contain.get("allowNeutralInside", false)))
		)
		if not allowed:
			continue
		best_id = target_id; best_distance = distance
	return best_id


func _step_grab_passenger(row: Dictionary) -> void:
	var channel := row.get("grab_passenger_channel", {}) as Dictionary
	if channel.is_empty():
		return
	if int(row.get("health", 0)) <= 0:
		row.erase("grab_passenger_channel")
		_eject_grabbed_passengers(int(row.get("id", 0)), row)
		sim._emit_event("ability.grab_interrupted", int(row.get("id", 0)), int(channel.get("target_id", 0)), {"reason": "carrier-dead"})
		return
	if not bool(channel.get("triggered", false)) and int(row.get("order_sequence", 0)) != int(channel.get("order_sequence_at_start", 0)):
		row.erase("grab_passenger_channel")
		sim._emit_event("ability.grab_interrupted", int(row.get("id", 0)), int(channel.get("target_id", 0)), {"reason": "order-interrupt"})
		return
	if not bool(channel.get("triggered", false)) and sim.tick_index >= int(channel.get("trigger_tick", 0)):
		var target_id := int(channel.get("target_id", 0))
		if not sim.entities.has(target_id) or sim.entity_container.has(target_id):
			row.erase("grab_passenger_channel")
			sim._emit_event("ability.grab_interrupted", int(row.get("id", 0)), target_id, {"reason": "target-unavailable"})
			return
		var target = sim.entities[target_id] as Dictionary
		var contained = sim.contain_entity(int(row.get("id", 0)), target_id)
		if not bool(contained.get("ok", false)):
			row.erase("grab_passenger_channel")
			return
		var effect := channel.get("effect", {}) as Dictionary
		var contain := effect.get("containment", {}) as Dictionary
		target["grab_prior_status"] = (target.get("object_status", {}) as Dictionary).duplicate(true)
		var statuses := target.get("object_status", {}) as Dictionary
		for status_value in contain.get("objectStatusOfContained", []) as Array:
			statuses[String(status_value)] = true
		target["object_status"] = statuses; target["state"] = "contained"; target["position"] = row.get("position", Vector2.ZERO)
		row["grab_weapon_set_types"] = (contain.get("weaponSetTypes", []) as Array).duplicate(true)
		row["grab_weapon_state_types"] = (contain.get("weaponStateTypes", []) as Array).duplicate(true)
		var acquire := effect.get("acquire", {}) as Dictionary
		var heal_percent := float(acquire.get("healGainPercent", 0.0))
		if heal_percent > 0.0:
			var maximum := maxi(1, int(row.get("maximum_health", row.get("health", 1))))
			row["health"] = mini(maximum, int(row.get("health", 0)) + roundi(maximum * heal_percent / 100.0))
		var award_xp := int(acquire.get("awardXp", 0))
		if award_xp > 0:
			sim._award_experience(row, award_xp)
		channel["triggered"] = true; row["grab_passenger_channel"] = channel
		sim._emit_event("ability.passenger_grabbed", int(row.get("id", 0)), target_id, {"heal_gain_percent": heal_percent, "award_xp": award_xp, "statuses": contain.get("objectStatusOfContained", []), "weapon_sets": contain.get("weaponSetTypes", []), "weapon_states": contain.get("weaponStateTypes", [])})
	if sim.tick_index >= int(channel.get("finish_tick", 0)):
		row.erase("grab_passenger_channel"); row["state"] = "idle"
		sim._emit_event("ability.grab_finished", int(row.get("id", 0)), int(channel.get("target_id", 0)))


func _eject_grabbed_passengers(carrier_id: int, carrier: Dictionary) -> void:
	for target_value in (sim.containment.get(carrier_id, []) as Array).duplicate():
		var target_id := int(target_value)
		sim.exit_entity_container(target_id)
		if sim.entities.has(target_id):
			var target = sim.entities[target_id] as Dictionary
			target["object_status"] = (target.get("grab_prior_status", {}) as Dictionary).duplicate(true)
			target.erase("grab_prior_status"); target["state"] = "idle"; target["position"] = carrier.get("position", Vector2.ZERO)


func _apply_ability_fling_passenger(row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_fling_passenger(row, ability_id, effect)


func release_grabbed_passenger(carrier_id: int, release_index: int = 0) -> Dictionary:
	if not sim.entities.has(carrier_id):
		return {"ok": false, "reason": "carrier-missing"}
	var carrier = sim.entities[carrier_id] as Dictionary
	var policies: Array = []
	for rule_value in sim._unit_ability_rules.get(String(carrier.get("unit_type", "")), []) as Array:
		var effect := (rule_value as Dictionary).get("effect", {}) as Dictionary
		if String(effect.get("kind", "")) == "grab-passenger":
			policies = effect.get("releaseAbilities", []) as Array; break
	if release_index < 0 or release_index >= policies.size():
		return {"ok": false, "reason": "release-ability-missing"}
	return _apply_ability_fling_passenger(carrier, "nested-release:%d" % release_index, policies[release_index] as Dictionary)


func _step_fling_passenger(row: Dictionary) -> void:
	var channel := row.get("fling_passenger_channel", {}) as Dictionary
	if channel.is_empty():
		return
	var effect := channel.get("effect", {}) as Dictionary
	if int(row.get("health", 0)) <= 0:
		row.erase("fling_passenger_channel"); _eject_grabbed_passengers(int(row.get("id", 0)), row); return
	if not bool(effect.get("mustFinishAbility", false)) and not bool(channel.get("triggered", false)) and int(row.get("order_sequence", 0)) != int(channel.get("order_sequence_at_start", 0)):
		row.erase("fling_passenger_channel"); sim._emit_event("ability.fling_interrupted", int(row.get("id", 0)), int(channel.get("passenger_id", 0))); return
	if not bool(channel.get("triggered", false)) and sim.tick_index >= int(channel.get("trigger_tick", 0)):
		var passenger_id := int(channel.get("passenger_id", 0))
		if not sim.entities.has(passenger_id) or int(sim.entity_container.get(passenger_id, -1)) != int(row.get("id", 0)):
			row.erase("fling_passenger_channel"); return
		var passenger = sim.entities[passenger_id] as Dictionary
		sim.exit_entity_container(passenger_id)
		var physics_id := _spawn_fling_physics_object(row, passenger, effect)
		sim.entities.erase(passenger_id)
		channel["triggered"] = true; channel["physics_object_id"] = physics_id; row["fling_passenger_channel"] = channel
		sim._emit_event("ability.passenger_flung", int(row.get("id", 0)), passenger_id, {"physics_object_id": physics_id, "velocity": effect.get("velocity", {}), "custom_animation": effect.get("customAnimation", {})})
	if sim.tick_index >= int(channel.get("finish_tick", 0)):
		row.erase("fling_passenger_channel"); row["state"] = "idle"
		sim._emit_event("ability.fling_finished", int(row.get("id", 0)), int(channel.get("passenger_id", 0)))


func _spawn_fling_physics_object(carrier: Dictionary, passenger: Dictionary, effect: Dictionary) -> int:
	var id = sim._next_physics_object_id; sim._next_physics_object_id += 1
	sim.physics_objects[id] = {
		"id": id, "source_object_id": String(passenger.get("source_object_id", passenger.get("object_id", ""))),
		"position": Vector2(carrier.get("position", Vector2.ZERO)), "height_source": 0.001,
		"horizontal_velocity": Vector2(effect.get("horizontal_velocity_scaled", Vector2.ZERO)),
		"vertical_velocity_source": float(effect.get("vertical_velocity_source", 0.0)),
		"gravity_multiplier": 1.0, "allow_bouncing": false, "orient_to_flight_path": true,
		"kill_when_resting_on_ground": true, "first_height_source": 0.0, "second_height_source": 0.0,
		"shock_stunned_low_ms": 0, "shock_stunned_high_ms": 0, "shock_standing_ms": 0,
		"bounce_count": 0, "phase": "airborne", "phase_ticks_remaining": 0,
		"yaw_radians": 0.0, "pitch_radians": 0.0, "landing_warhead": (effect.get("landingWarhead", {}) as Dictionary).duplicate(true),
		"fling_attacker_id": int(carrier.get("id", 0)), "unsupported_semantics": [],
	}
	return id


func _step_dominate_enemy(row: Dictionary) -> void:
	var channel := row.get("dominate_enemy_channel", {}) as Dictionary
	if channel.is_empty():
		return
	var interrupted = (
		int(row.get("health", 0)) <= 0
		or bool(row.get("knocked_down", false))
		or sim.tick_index < int(row.get("stun_until_tick", -1))
		or sim.tick_index < int(row.get("cower_until_tick", -1))
		or (not bool(channel.get("triggered", false)) and int(row.get("order_sequence", 0)) != int(channel.get("order_sequence_at_start", 0)))
	)
	if interrupted:
		row.erase("dominate_enemy_channel")
		sim._emit_event("ability.dominate_interrupted", int(row.get("id", 0)), 0, {"ability_id": channel.get("ability_id"), "triggered": channel.get("triggered")})
		return
	if not bool(channel.get("triggered", false)) and sim.tick_index >= int(channel.get("activation_tick", 0)):
		var effect := {
			"affectsFilter": channel.get("affects_filter", ""),
			"dominate_radius_scaled": channel.get("dominate_radius_scaled", 0.0),
		}
		var affected := _dominate_enemy_candidates(row, effect, Vector2(channel.get("target_point", Vector2.ZERO)), String(channel.get("targeting", "point")))
		for target_id in affected:
			_dominate_enemy_convert(
				row, sim.entities[int(target_id)] as Dictionary,
				bool(channel.get("permanently_convert", false)),
				int(channel.get("temporary_defect_duration_ticks", 0))
			)
		channel["affected_ids"] = affected
		channel["triggered"] = true
		row["dominate_enemy_channel"] = channel
		sim._emit_event("ability.dominate_triggered", int(row.get("id", 0)), int(affected[0]) if not affected.is_empty() else 0, {
			"ability_id": channel.get("ability_id"), "affected_ids": affected,
			"presentation": channel.get("presentation"),
		})
	if sim.tick_index >= int(channel.get("finish_tick", 0)):
		row.erase("dominate_enemy_channel")
		row["state"] = "idle"
		sim._emit_event("ability.dominate_finished", int(row.get("id", 0)), 0, {"ability_id": channel.get("ability_id"), "affected_ids": channel.get("affected_ids", [])})


func _dominate_enemy_candidates(source: Dictionary, effect: Dictionary, point: Vector2, targeting: String) -> Array[int]:
	var radius := float(effect.get("dominate_radius_scaled", 0.0))
	if radius <= 0.0:
		radius = 1.5
	if targeting == "enemy-object":
		var chosen_id := 0
		var chosen_distance := 1.5
		for target_id in sim.entity_ids():
			if target_id == int(source.get("id", 0)):
				continue
			var target = sim.entities[target_id] as Dictionary
			var distance := Vector2(target.get("position", Vector2.ZERO)).distance_to(point)
			if int(target.get("health", 0)) > 0 and (chosen_id == 0 and distance <= chosen_distance or chosen_id != 0 and distance < chosen_distance) and _dominate_enemy_filter_accepts(source, target, String(effect.get("affectsFilter", ""))):
				chosen_distance = distance
				chosen_id = target_id
		var single: Array[int] = []
		if chosen_id != 0:
			single.append(chosen_id)
		return single
	var result: Array[int] = []
	for target_id in sim.entity_ids():
		if target_id == int(source.get("id", 0)):
			continue
		var target = sim.entities[target_id] as Dictionary
		if int(target.get("health", 0)) <= 0 or Vector2(target.get("position", Vector2.ZERO)).distance_to(point) > radius:
			continue
		if not _dominate_enemy_filter_accepts(source, target, String(effect.get("affectsFilter", ""))):
			continue
		result.append(target_id)
	return result


func _dominate_enemy_filter_accepts(source: Dictionary, target: Dictionary, filter_text: String) -> bool:
	var tokens: Array = filter_text.split(" ", false)
	if tokens.is_empty():
		return false
	var source_team := int(source.get("team", -1))
	var target_team := int(target.get("team", -1))
	var relation_allowed := false
	var has_relation := false
	var trait_tokens: Array = []
	for token_value in tokens:
		var token := String(token_value).to_upper()
		if token == "ENEMIES":
			has_relation = true; relation_allowed = relation_allowed or sim._is_hostile(source_team, target_team)
		elif token == "NEUTRAL":
			has_relation = true; relation_allowed = relation_allowed or target_team == sim.NEUTRAL_TEAM or target_team < 0
		elif token == "ALLIES":
			has_relation = true; relation_allowed = relation_allowed or (target_team == source_team or (not sim._is_hostile(source_team, target_team) and target_team != sim.NEUTRAL_TEAM and target_team >= 0))
		else:
			trait_tokens.append(token)
	if has_relation and not relation_allowed:
		return false
	var traits: Dictionary = {}
	for trait_value in _ability_object_kind_tokens(target):
		traits[String(trait_value).to_upper()] = true
	var source_rule = ((sim._rules.get("unit_rules", {}) as Dictionary).get(String(target.get("object_id", "")), {}) as Dictionary)
	if source_rule.is_empty():
		source_rule = ((sim._rules.get("unit_rules", {}) as Dictionary).get(String(target.get("unit_type", "")), {}) as Dictionary)
	for identity in [source_rule.get("source_object_id", ""), target.get("source_object_id", ""), target.get("object_id", ""), target.get("unit_type", "")]:
		if String(identity) != "":
			traits[String(identity).to_upper()] = true
	var accepted := trait_tokens.has("ALL") or trait_tokens.has("ANY")
	for token_value in trait_tokens:
		var token := String(token_value)
		if token.begins_with("-") and traits.has(token.substr(1)):
			return false
		if token.begins_with("+") and traits.has(token.substr(1)):
			accepted = true
	return accepted


func _dominate_enemy_convert(source: Dictionary, target: Dictionary, permanent: bool = true, temporary_duration_ticks: int = 0) -> void:
	var prior_team := int(target.get("team", -1))
	var new_team := int(source.get("team", -1))
	if prior_team == new_team:
		return
	if not target.has("dominated_from_team"):
		target["dominated_from_team"] = prior_team
	if permanent:
		target.erase("temporary_defect")
	else:
		if temporary_duration_ticks <= 0:
			return
		var original_team := int(target.get("dominated_from_team", prior_team))
		var existing := target.get("temporary_defect", {}) as Dictionary
		if not existing.is_empty():
			original_team = int(existing.get("original_team", original_team))
		target["temporary_defect"] = {
			"original_team": original_team,
			"defecting_team": new_team,
			"source_id": int(source.get("id", 0)),
			"started_tick": sim.tick_index,
			"expires_tick": sim.tick_index + temporary_duration_ticks,
			"duration_ticks": temporary_duration_ticks,
		}
	target["team"] = new_team
	target["target_id"] = 0
	target["target_kind"] = "battalion"
	target["destination"] = Vector2(target.get("position", Vector2.ZERO))
	target["route"] = []
	target["route_cells"] = []
	target["state"] = "idle"
	sim._clear_member_attack_schedule(target)
	sim._clear_member_targets(target)
	sim._emit_event("ability.unit_dominated", int(source.get("id", 0)), int(target.get("id", 0)), {"prior_team": prior_team, "team": new_team, "permanent": permanent, "restore_tick": sim.tick_index + temporary_duration_ticks if not permanent else -1})


func _step_temporary_defect(row: Dictionary) -> void:
	var defect := row.get("temporary_defect", {}) as Dictionary
	if defect.is_empty():
		return
	if int(row.get("health", 0)) <= 0:
		row.erase("temporary_defect")
		sim._emit_event("ability.temporary_defect_ended", int(defect.get("source_id", 0)), int(row.get("id", 0)), {"reason": "target-dead", "restored": false})
		return
	if sim.tick_index < int(defect.get("expires_tick", 0)):
		return
	var restored := int(row.get("team", -1)) == int(defect.get("defecting_team", -1))
	if restored:
		row["team"] = int(defect.get("original_team", -1))
		row["target_id"] = 0
		row["destination"] = Vector2(row.get("position", Vector2.ZERO))
		row["route"] = []
		row["route_cells"] = []
		row["state"] = "idle"
		sim._clear_member_attack_schedule(row)
		sim._clear_member_targets(row)
	row.erase("temporary_defect")
	row.erase("dominated_from_team")
	sim._emit_event("ability.temporary_defect_ended", int(defect.get("source_id", 0)), int(row.get("id", 0)), {"reason": "expired", "restored": restored, "team": int(row.get("team", -1))})


func _apply_ability_mount_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_mount_toggle(hero_row, effect)


func _apply_ability_special_disguise(row: Dictionary, effect: Dictionary) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_special_disguise(row, effect)


func special_disguise_opacity(row: Dictionary) -> float:
	var channel := row.get("special_disguise_channel", {}) as Dictionary
	if channel.is_empty():
		return 1.0
	var target := float(channel.get("opacity_target", 1.0))
	var phase := String(channel.get("phase", ""))
	if phase in ["preparation", "persistent-hold"]:
		return target
	if phase not in ["unpacking", "packing"]:
		return 1.0
	var start_tick = int(channel.get("phase_start_tick", sim.tick_index))
	var end_tick := int(channel.get("phase_end_tick", start_tick))
	var progress = clampf(float(sim.tick_index - start_tick) / float(maxi(1, end_tick - start_tick)), 0.0, 1.0)
	return lerpf(1.0, target, progress) if phase == "unpacking" else lerpf(target, 1.0, progress)


func _step_special_disguise(row: Dictionary) -> void:
	var channel := row.get("special_disguise_channel", {}) as Dictionary
	if channel.is_empty():
		return
	if int(row.get("health", 0)) <= 0:
		# The binary packet does not close death/respawn/reset abort ordering.
		# Freeze the channel and label the boundary instead of inventing a reset.
		row["special_disguise_deferred_boundary"] = "death-reset-ordering"
		return
	var phase := String(channel.get("phase", ""))
	if phase == "persistent-hold" or sim.tick_index < int(channel.get("phase_end_tick", 0)):
		return
	match phase:
		"unpacking":
			channel["phase"] = "preparation"
			channel["phase_start_tick"] = sim.tick_index
			channel["phase_end_tick"] = sim.tick_index + int(channel.get("preparation_ticks", 1))
			row["special_disguise_channel"] = channel
		"preparation":
			sim._set_row_object_status(row, "DISGUISED", true)
			channel["phase"] = "persistent-hold"
			channel["phase_start_tick"] = sim.tick_index
			# PersistentPrepTime is an authored hold cadence, not permission to
			# retrigger the disguise body every 250ms. The shipped override fires
			# once and remains held until cancel.
			channel["phase_end_tick"] = sim.tick_index + int(channel.get("persistent_prep_ticks", 1))
			row["special_disguise_channel"] = channel
			_emit_special_disguise_presentation(row, "owner-disguised-presentation", String(channel.get("owner_disguise_template_id", "")), true)
			sim._emit_event("ability.special_disguise_triggered", int(row.get("id", 0)), 0, {
				"template_id": String(channel.get("owner_disguise_template_id", "")),
				"authoritative_object_id": String(row.get("unit_type", "")),
				"viewer_perspective": "deferred-owner-only",
			})
		"packing":
			row.erase("special_disguise_channel")
			row.erase("special_disguise_deferred_boundary")
			sim._emit_event("ability.special_disguise_packed", int(row.get("id", 0)), 0, {"opacity": 1.0})


func cancel_special_disguise(entity_id: int, reason: String = "explicit", suppress_exit_fx: bool = false) -> Dictionary:
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "unknown-entity"}
	return _cancel_special_disguise_row(sim.entities[entity_id] as Dictionary, reason, suppress_exit_fx)


func _cancel_special_disguise_row(row: Dictionary, reason: String, suppress_exit_fx: bool) -> Dictionary:
	var channel := row.get("special_disguise_channel", {}) as Dictionary
	if channel.is_empty():
		return {"ok": false, "reason": "special-disguise-inactive"}
	if String(channel.get("phase", "")) == "packing":
		return {"ok": false, "reason": "special-disguise-already-packing"}
	sim._set_row_object_status(row, "DISGUISED", false)
	channel["phase"] = "packing"
	channel["phase_start_tick"] = sim.tick_index
	channel["phase_end_tick"] = sim.tick_index + int(channel.get("pack_ticks", 1))
	row["special_disguise_channel"] = channel
	_emit_special_disguise_presentation(row, "owner-mounted-presentation", String(channel.get("owner_object_id", "")), false)
	var exit_fx := "" if suppress_exit_fx else String(channel.get("disguise_fx_id", ""))
	sim._emit_event("ability.special_disguise_cancelled", int(row.get("id", 0)), 0, {
		"reason": reason, "suppress_exit_fx": suppress_exit_fx,
		"exit_fx_id": exit_fx, "phase_end_tick": int(channel.get("phase_end_tick", 0)),
	})
	return {"ok": true, "reason": "", "effect": "special-disguise-cancel", "phase": "packing", "exit_fx_id": exit_fx}


func _emit_special_disguise_presentation(row: Dictionary, role: String, template_id: String, force_mounted: bool) -> void:
	sim._emit_event("ability.special_disguise_presentation", int(row.get("id", 0)), 0, {
		"role": role, "template_id": template_id,
		"authoritative_object_id": String(row.get("unit_type", "")),
		"opacity": special_disguise_opacity(row),
		"force_mounted": force_mounted,
		"viewer_perspective": "deferred-owner-only",
		"presentation_prerequisite_sha256": String((row.get("special_disguise_channel", {}) as Dictionary).get("presentation_prerequisite_sha256", "")),
	})


func _rescale_member_health_preserving_fraction(row: Dictionary, new_member_maximum: int) -> void:
	## ChildObject-style state swaps that change member max health keep the
	## live health FRACTION (retail mount contract). A zero/absent target max
	## (the Men mounts: same body both states) leaves health untouched.
	if new_member_maximum <= 0 or new_member_maximum == int(row.get("member_maximum_health", 0)):
		return
	var old_maximum := maxi(1, int(row.get("member_maximum_health", 1)))
	var health_values: Array = row.get("member_health", [])
	var aggregate := 0
	for index in range(health_values.size()):
		var current := int(health_values[index])
		if current > 0:
			health_values[index] = clampi(roundi(float(current) / float(old_maximum) * float(new_member_maximum)), 1, new_member_maximum)
		aggregate += int(health_values[index]) if int(health_values[index]) > 0 else 0
	row["member_maximum_health"] = new_member_maximum
	row["maximum_health"] = new_member_maximum * int(row.get("member_count", 1))
	row["member_health"] = health_values
	row["health"] = aggregate


func _apply_ability_capture_building(hero_row: Dictionary, effect: Dictionary, target_point: Vector2) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_capture_building(hero_row, effect, target_point)


func _step_capture_channel(row: Dictionary) -> bool:
	## Advance one entity's capture channel. Returns true while the channel
	## holds the hero in place (the entity step then goes no further).
	var channel: Dictionary = row.get("capture_channel", {}) as Dictionary
	if channel.is_empty():
		return false
	var structure_id := int(channel.get("structure_id", 0))
	var structure: Dictionary = sim.structures.get(structure_id, {})
	var team := int(row.get("team", -1))
	var interrupted = (
		structure.is_empty()
		or int(structure.get("health", 0)) <= 0
		or int(structure.get("team", -1)) != sim.NEUTRAL_TEAM
		or not (row["route"] as Array).is_empty()
		or int(row.get("target_id", 0)) != 0
	)
	if interrupted:
		row.erase("capture_channel")
		sim._emit_event("structure.capture_cancelled", int(row.get("id", 0)), structure_id, {"team": team})
		return false
	if sim.tick_index >= int(channel.get("complete_tick", sim.tick_index + 1)):
		structure["team"] = team
		sim._award_auto_deposit_capture(structure, team)
		sim._transfer_linked_capture(structure, team)
		row.erase("capture_channel")
		row["state"] = "idle"
		sim._emit_event("structure.captured", int(row.get("id", 0)), structure_id, {
			"team": team,
			"structure_id": structure_id,
			"structure_kind": String(structure.get("structure_kind", "")),
		})
		return false
	row["state"] = "capture"
	row["current_speed"] = 0.0
	return true


func _apply_ability_terror(hero_row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_terror(hero_row, ability_id, effect)


func _apply_fear_scatter(center: Vector2, row: Dictionary, strength: float) -> void:
	## Fear displacement: throw the victim radially away from the terror
	## source onto the nearest walkable spot (same deterministic fraction
	## ladder as knockback) and drop its orders — but never the knockdown
	## sprawl, so the battalion can flee/act again immediately.
	var position := Vector2(row.get("position", Vector2.ZERO))
	var distance := position.distance_to(center)
	var direction := (position - center) / distance if distance > 0.001 else Vector2.RIGHT
	var landed := position
	for fraction in [1.0, 0.5, 0.25]:
		var candidate := position + direction * strength * float(fraction)
		if sim._position_walkable(candidate):
			landed = candidate
			break
	row["position"] = landed
	sim._spatial_sync(row)
	row["current_speed"] = 0.0
	row["attack_windup"] = 0
	row["target_id"] = 0
	row["target_kind"] = "battalion"
	row["attack_move"] = false
	sim._clear_member_attack_schedule(row)
	sim._clear_member_targets(row)
	sim._clear_pending_route(row, true)
	row["state"] = "idle"
	sim._emit_event("combat.fear_scatter", int(row.get("id", 0)), 0, {
		"center": [snappedf(center.x, 0.001), snappedf(center.y, 0.001)],
		"landed": [snappedf(landed.x, 0.001), snappedf(landed.y, 0.001)],
	})


func _apply_ability_summon(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_summon(hero_row, effect, point)


func _apply_ability_summon_chain(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_summon_chain(team, effect, point)


func _summon_unit_type_for(source_object_id: String) -> String:
	var member_id = sim.PlayableUnitAdapter.runtime_object_id(source_object_id)
	for unit_type_value in sim._unit_production_rules.keys():
		if String((sim._unit_production_rules[unit_type_value] as Dictionary).get("object_id", "")) == member_id:
			return String(unit_type_value)
	return ""


func _apply_ability_experience_grant(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return sim._experience_subsystem().apply_ability_experience_grant(hero_row, effect, point)


func _apply_ability_arrow_storm(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_arrow_storm(hero_row, effect, point)


func _step_volley_channel(row: Dictionary) -> bool:
	## Advance one entity's arrow-storm volley. Returns true while the channel
	## holds the hero in place (the entity step then goes no further). Any
	## later order (route or target) cancels the volley outright.
	var channel: Dictionary = row.get("volley_channel", {}) as Dictionary
	if channel.is_empty():
		return false
	if not (row["route"] as Array).is_empty() or int(row.get("target_id", 0)) != 0:
		row.erase("volley_channel")
		sim._emit_event("ability.volley_cancelled", int(row.get("id", 0)), 0, {"team": int(row.get("team", -1))})
		return false
	if sim.tick_index >= int(channel.get("next_shot_tick", 0)):
		var team := int(row.get("team", -1))
		var point := Vector2(channel.get("point", Vector2.ZERO))
		var enemy_ids := _ability_enemies_near(team, point, float(channel.get("radius", 0.0)))
		if enemy_ids.is_empty() and not bool(channel.get("can_shoot_empty_ground", false)):
			row.erase("volley_channel")
			row["state"] = "idle"
			sim._emit_event("ability.volley_complete", int(row.get("id", 0)), 0, {"team": team, "shots": int(channel.get("shots_fired", 0))})
			return false
		var shots := mini(int(channel.get("shots_per_burst", 1)), int(channel.get("shots_left", 0)))
		var damage := int(channel.get("damage", 0))
		var fired := int(channel.get("shots_fired", 0))
		for shot_index in range(shots):
			if enemy_ids.is_empty():
				continue
			var target_id := int(enemy_ids[(fired + shot_index) % enemy_ids.size()])
			if sim.entities.has(target_id) and int((sim.entities[target_id] as Dictionary).get("health", 0)) > 0:
				sim._apply_damage(int(row.get("id", 0)), target_id, damage, "battalion")
		channel["shots_fired"] = fired + shots
		channel["shots_left"] = int(channel.get("shots_left", 0)) - shots
		channel["next_shot_tick"] = sim.tick_index + int(channel.get("interval_ticks", 1))
		if int(channel.get("shots_left", 0)) <= 0:
			row.erase("volley_channel")
			row["state"] = "idle"
			sim._emit_event("ability.volley_complete", int(row.get("id", 0)), 0, {"team": team, "shots": fired + shots})
			return false
		row["volley_channel"] = channel
	row["state"] = "volley"
	row["current_speed"] = 0.0
	return true


func _apply_ability_stealth_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_stealth_toggle(hero_row, effect)


func _stealth_active(row: Dictionary) -> bool:
	return sim.tick_index < int(row.get("stealth_until_tick", -1)) and sim.tick_index >= int(row.get("detected_until_tick", -1))


func _grant_stealth(row: Dictionary, until_tick: int, forbidden: Array) -> void:
	row["stealth_until_tick"] = until_tick
	row["stealth_forbidden"] = forbidden.duplicate()
	sim._emit_event("ability.stealth", int(row.get("id", 0)), 0, {"engaged": true, "until_tick": until_tick})


func _clear_stealth(row: Dictionary) -> void:
	if not row.has("stealth_until_tick"):
		return
	row.erase("stealth_until_tick")
	row.erase("stealth_forbidden")
	sim._emit_event("ability.stealth", int(row.get("id", 0)), 0, {"engaged": false})


func _break_stealth(row: Dictionary, condition: String) -> void:
	## Break an active cloak when its authored ForbiddenConditions include the
	## triggering condition (TAKING_DAMAGE / FIRING_ANY / USING_ABILITY).
	if not _stealth_active(row):
		return
	if not (row.get("stealth_forbidden", []) as Array).has(condition):
		return
	var policy := row.get("invisibility_update", {}) as Dictionary
	if (
		not policy.is_empty()
		and (policy.get("forbidden_conditions", []) as Array).has(condition)
		and (policy.get("options", []) as Array).has("UNTOGGLE_HIDDEN_WHEN_LEAVING_STEALTH")
	):
		policy["enabled"] = false
		var object_id := int(row.get("id", 0))
		sim._revoke_invisibility_policy_sources(object_id, row, policy)
		row["invisibility_update"] = policy
	_clear_stealth(row)


func _apply_ability_teleport(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_teleport(hero_row, effect, point)


func _apply_ability_curse(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_curse(hero_row, effect, point)


func _apply_ability_leadership_strip(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	return sim._abilities_subsystem()._apply_ability_leadership_strip(hero_row, effect)


func _set_timed_modifier(row: Dictionary, key: String, modifiers: Array, expires_tick: int) -> void:
	var table: Dictionary = row.get("timed_modifiers", {}) as Dictionary
	table[key] = {"modifiers": modifiers.duplicate(true), "expires_tick": expires_tick}
	row["timed_modifiers"] = table


func _timed_modifier_product(row: Dictionary, kind: String) -> float:
	var factor := 1.0
	for entry_value in (row.get("timed_modifiers", {}) as Dictionary).values():
		for modifier_value in (entry_value as Dictionary).get("modifiers", []) as Array:
			var modifier := modifier_value as Dictionary
			if String(modifier.get("kind", "")) == kind:
				factor *= float(modifier.get("value", 1.0))
	return factor


func _timed_modifier_active(row: Dictionary, kind: String) -> bool:
	for entry_value in (row.get("timed_modifiers", {}) as Dictionary).values():
		for modifier_value in (entry_value as Dictionary).get("modifiers", []) as Array:
			var modifier := modifier_value as Dictionary
			if String(modifier.get("kind", "")) == kind and float(modifier.get("value", 0.0)) >= 1.0:
				return true
	return false


func _ability_outgoing_multiplier(row: Dictionary) -> float:
	## COVERAGE, DECIDED AND RECORDED 2026-08-04 (round 18) — do not "fix" this
	## silently in either direction.
	##
	## This multiplier is consulted at exactly ONE site: the per-member melee/
	## ranged swing in _step_attacks. It therefore does NOT ride
	##   - upgrade-gated bonus nuggets (fire-arrow flame components),
	##   - hero cleave splash,
	##   - trample,
	##   - ability/power direct damage.
	## That is consistent with SpellBookDarkness and every hero leadership aura,
	## which enter through the same accumulator, and it is narrower than the
	## Rallying Call `rally_factor`, which rides inside sim._apply_damage and so
	## scales everything.
	##
	## What retail means: attributemodifier.ini authors DAMAGE_MULT with the
	## comment "Multiplicitive. Damage multiplied by this, will compound in
	## multiple bonuses", i.e. the object's damage OUTPUT — all of it. So the
	## correct model is the wide one, and this narrow model is a known gap.
	## OpenSAGE is NOT an oracle here: its AttributeModifier.Apply
	## (Logic/ModifierList.cs:39-52) implements only Production and Health and
	## drops DAMAGE_MULT on the floor, so it cannot arbitrate the scope.
	##
	## NOT widened in this round, deliberately: moving it into sim._apply_damage
	## moves every hero-leadership damage number in the game (leadership auras
	## write DAMAGE_MULT into this same table and are live throughout the pinned
	## slice scenarios), so it needs its own failing-first evidence and a
	## conscious pin re-mint rather than riding along with a parser fix. Round 16
	## reached the current narrow coverage as a side effect of moving taint off
	## its private row fields; this comment is the record that the narrowing was
	## noticed and accepted, not that it was chosen as correct.
	return _timed_modifier_product(row, "DAMAGE_MULT")


func _ability_incoming_multiplier(row: Dictionary) -> float:
	if _timed_modifier_active(row, "INVULNERABLE"):
		return 0.0
	var armor := 0.0
	for entry_value in (row.get("timed_modifiers", {}) as Dictionary).values():
		for modifier_value in (entry_value as Dictionary).get("modifiers", []) as Array:
			var modifier := modifier_value as Dictionary
			if String(modifier.get("kind", "")) == "ARMOR":
				armor += float(modifier.get("value", 0.0))
	return maxf(1.0 - sim.ABILITY_ARMOR_CAP, 1.0 - armor)


func _ability_speed_multiplier(row: Dictionary) -> float:
	return _timed_modifier_product(row, "SPEED")


func _ability_vision_multiplier(row: Dictionary) -> float:
	return _timed_modifier_product(row, "VISION")


func _recompute_leadership_auras() -> void:
	## Passive leadership auras: every living, standing hero radiates its
	## compiled leadership-aura rows to allied battalions inside the authored
	## range (plus itself when AffectsSelf), keyed by the compiled BonusName.
	## Same-named leaderships never stack (one key), different heroes' auras
	## with different names do — the retail AttributeModifier stacking rule.
	## Sweep order is ascending entity id; grants expire one interval after
	## the last refresh, so death/knockdown/leaving-radius drops them.
	for id in sim.entity_ids():
		var hero: Dictionary = sim.entities[id]
		if String(hero.get("category", "")) != "hero":
			continue
		if int(hero.get("health", 0)) <= 0 or int(hero.get("knockdown_ticks", 0)) > 0:
			continue
		var rules: Array = sim._unit_ability_rules.get(String(hero.get("unit_type", "")), []) as Array
		if rules.is_empty():
			continue
		var team := int(hero.get("team", -1))
		var origin := Vector2(hero.get("position", Vector2.ZERO))
		var level := int(hero.get("level", 1))
		for rule_value in rules:
			var rule := rule_value as Dictionary
			var effect: Dictionary = rule.get("effect", {}) as Dictionary
			if String(effect.get("kind", "")) != "leadership-aura":
				continue
			if not bool(effect.get("startsActive", true)):
				# Upgrade-gated auras stay off until the gate system lands
				# (importer follow-up); fail closed, never invented-on.
				continue
			if level < int(rule.get("required_level", 1)):
				continue
			var bonus_name := String(effect.get("bonusName", ""))
			var range_limit := float(effect.get("range_scaled", 0.0))
			var modifiers: Array = effect.get("modifiers", []) as Array
			if bonus_name == "" or range_limit <= 0.0 or modifiers.is_empty():
				# Fail closed on placeholder rows that carry no aura data.
				continue
			var filter_text := String(effect.get("affects", ""))
			var expiry = sim.tick_index + sim.ABILITY_AURA_INTERVAL_TICKS
			# An aura reaches a bounded radius, so only that neighbourhood of the
			# owning team can receive a grant. The hero itself always lands in
			# the gathered set (distance zero), preserving the AffectsSelf seam.
			for ally_id in sim._spatial_gather_sorted(origin, range_limit):
				if not sim.entities.has(ally_id):
					continue
				var ally: Dictionary = sim.entities[ally_id]
				if int(ally.get("team", -1)) != team or int(ally.get("health", 0)) <= 0:
					continue
				if ally_id == id:
					if not bool(effect.get("affectsSelf", false)):
						continue
				elif Vector2(ally.get("position", Vector2.ZERO)).distance_to(origin) > range_limit:
					continue
				if not _ability_filter_accepts(ally, filter_text):
					continue
				if sim.tick_index < sim._refresh_leadership_suppression(ally):
					# An anti-category strip (Horn of Gondor) suppresses new
					# leadership grants for its authored duration.
					continue
				_set_timed_modifier(ally, "aura:%s" % bonus_name, modifiers, expiry)


func _step_hero_abilities() -> void:
	## Refresh leadership auras on the fixed cadence, expire timed modifiers
	## whose authored duration elapsed (exact tick), and apply HEALTH-modifier
	## regeneration. Cooldowns are absolute ready ticks, so nothing counts
	## down here.
	if sim.tick_index % sim.ABILITY_AURA_INTERVAL_TICKS == 0:
		_recompute_leadership_auras()
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		_step_temporary_defect(row)
		_step_activate_module_graph(row)
		_step_weapon_mode_special_powers(row)
		_step_dominate_enemy(row)
		_step_grab_passenger(row)
		_step_fling_passenger(row)
		_step_repair_structure(row)
		_step_toggle_deploy(row)
		_step_siege_deploy(row)
		_step_special_disguise(row)
		# Expiry sweeps for the per-row ability fields (exact tick, then the
		# field leaves the row so default rows never carry it).
		if row.has("stealth_until_tick") and sim.tick_index >= int(row["stealth_until_tick"]):
			_clear_stealth(row)
		sim._refresh_leadership_suppression(row)
		if row.has("ability_hold_until_tick") and sim.tick_index >= int(row["ability_hold_until_tick"]):
			row.erase("ability_hold_until_tick")
		var table: Dictionary = row.get("timed_modifiers", {}) as Dictionary
		if table.is_empty():
			continue
		var expired: Array[String] = []
		for key_value in table.keys():
			if bool((table[key_value] as Dictionary).get("persistent", false)):
				continue
			if sim.tick_index >= int((table[key_value] as Dictionary).get("expires_tick", -1)):
				expired.append(String(key_value))
		if not expired.is_empty():
			expired.sort()
			for key in expired:
				table.erase(key)
			row["timed_modifiers"] = table
		if table.is_empty() or int(row.get("health", 0)) <= 0:
			continue
		# HEALTH modifier regeneration (provisional interpretation, recorded:
		# value v > 1.0 restores (v - 1.0) of member max health per second
		# while active; exact retail magnitudes are an importer follow-up).
		var health_factor := _timed_modifier_product(row, "HEALTH")
		if health_factor > 1.0:
			var member_maximum := int(row.get("member_maximum_health", 0))
			var amount = maxi(1, roundi(float(member_maximum) * (health_factor - 1.0) * sim.TICK_SECONDS))
			var health_values: Array = row.get("member_health", [])
			var remaining = amount
			var restored := false
			for index in range(health_values.size()):
				if remaining <= 0:
					break
				var current := int(health_values[index])
				if current <= 0 or current >= member_maximum:
					continue
				var healed := mini(remaining, member_maximum - current)
				health_values[index] = current + healed
				remaining -= healed
				restored = true
			if restored:
				row["member_health"] = health_values
				var aggregate := 0
				for value in health_values:
					aggregate += int(value)
				row["health"] = aggregate


