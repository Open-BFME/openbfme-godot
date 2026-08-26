extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Ability-effect subsystem extracted from retail_slice_sim.gd (Q81
## strangler-fig extraction #9): the 23 _apply_ability_* effect resolvers.
## Verbatim move, compiler-guided sim. prefixes, pin-verified.





func _apply_ability_activate_module_graph(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2, targeting: String) -> Dictionary:
	if not (row.get("activate_module_channel", {}) as Dictionary).is_empty():
		return {"ok": false, "reason": "ability-channel-active"}
	var routes := effect.get("routes", []) as Array
	if routes.is_empty():
		return {"ok": false, "reason": "activate-module-routes-empty"}
	var current_target = sim._activate_module_target_identity(row, target_point, targeting)
	for route_value in routes:
		var route := route_value as Dictionary
		if String(route.get("targetMode", "")) == "CURRENT_TARGET" and current_target.is_empty():
			return {"ok": false, "reason": "activate-module-current-target-missing", "module_tag": String(route.get("moduleTag", ""))}
	var timing := effect.get("timing_ticks", {}) as Dictionary
	var activation_delay := 0
	for key in ["StartDelay", "UnpackTime", "PreparationTime", "PersistentPrepTime"]:
		activation_delay += int(timing.get(key, 0))
	var duration := int(timing.get("SpecialPowerDuration", 0))
	var pack := int(timing.get("PackTime", 0))
	var channel := {
		"ability_id": ability_id,
		"special_power_template_id": String(effect.get("specialPowerTemplateId", "")),
		"routes": routes.duplicate(true),
		"location": target_point,
		"current_target_id": int(current_target.get("id", 0)),
		"current_target_kind": String(current_target.get("kind", "")),
		"effect_range_scaled": float(effect.get("effect_range_scaled", 0.0)),
		"must_finish": bool(effect.get("mustFinishAbility", false)),
		"unpacking_variation": int(effect.get("unpackingVariation", 0)),
		"order_sequence_at_start": int(row.get("order_sequence", 0)),
		"start_tick": sim.tick_index,
		"activation_tick": sim.tick_index + activation_delay,
		"active_end_tick": sim.tick_index + activation_delay + duration,
		"finish_tick": sim.tick_index + activation_delay + duration + pack,
		"dispatched": false,
		"route_results": [],
	}
	row["activate_module_channel"] = channel
	row["current_speed"] = 0.0
	row["state"] = "ability"
	sim._emit_event("ability.graph_started", int(row.get("id", 0)), int(channel.get("current_target_id", 0)), {
		"ability_id": ability_id, "special_power_template_id": channel.get("special_power_template_id"),
		"activation_tick": channel.get("activation_tick"), "finish_tick": channel.get("finish_tick"),
		"must_finish": channel.get("must_finish"), "unpacking_variation": channel.get("unpacking_variation"),
	})
	return {"ok": true, "reason": "", "effect": "activate-module-graph", "affected": 0, "scheduled": true}


func _apply_ability_weapon_blast(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	## Converted SpecialWeapon blast: base DamageNugget damage at the target
	## point, radius-scaled when the weapon authors one.
	var attacker_id := int(hero_row.get("id", 0))
	var team := int(hero_row.get("team", -1))
	var radius := float(effect.get("damage_radius", 0.0))
	var damage := maxi(1, int(effect.get("damage", 1)))
	var affected := 0
	if radius > 0.0:
		for id in sim._ability_enemies_near(team, point, radius):
			sim._apply_damage(attacker_id, id, damage, "battalion")
			affected += 1
		for structure_id in sim.structure_ids():
			var structure: Dictionary = sim.structures[structure_id]
			if int(structure.get("team", -1)) == team or int(structure.get("health", 0)) <= 0:
				continue
			if Vector2(structure.get("position", Vector2.ZERO)).distance_to(point) <= radius:
				sim._apply_structure_damage(attacker_id, structure_id, damage)
				affected += 1
	else:
		var best_id := -1
		var best_distance := 2.0
		for id in sim._ability_enemies_near(team, point, 2.0):
			var distance := Vector2((sim.entities[id] as Dictionary).get("position", Vector2.ZERO)).distance_to(point)
			if distance <= best_distance:
				best_distance = distance
				best_id = id
		if best_id >= 0:
			sim._apply_damage(attacker_id, best_id, damage, "battalion")
			affected = 1
	# Blast shockwave: abilities whose compiled rule authors knockback fields
	# throw enemies radially away from the impact point. Damage stays on the
	# damage_radius path above (knockback itself adds none).
	var knockback_radius := float(effect.get("knockback_radius", 0.0))
	var knockback_strength := float(effect.get("knockback_strength", 0.0))
	if knockback_radius > 0.0 and knockback_strength > 0.0:
		sim._apply_knockback(point, knockback_radius, knockback_strength, team, 0, "ability-blast", attacker_id)
	return {"ok": true, "reason": "", "effect": "weapon-blast", "affected": affected}


func _apply_ability_heal(hero_row: Dictionary, effect: Dictionary, epicenter: Vector2) -> Dictionary:
	## Converted heal leaf: flat burst (AutoHealBehavior) or max-health
	## fraction (PlayerHealSpecialPower) inside the authored radius, honoring
	## the authored kind filter.
	var hero_id := int(hero_row.get("id", 0))
	var team := int(hero_row.get("team", -1))
	var radius := float(effect.get("radius_scaled", 0.0))
	var amount := float(effect.get("amount", 0.0))
	var flat := String(effect.get("amountKind", "flat")) == "flat"
	var only_others := bool(effect.get("onlyOthers", false))
	var filter_text := String(effect.get("affects", ""))
	var healed := 0
	for id in sim.living_ids(team):
		if only_others and id == hero_id:
			continue
		var row: Dictionary = sim.entities[id]
		if not sim._ability_filter_accepts(row, filter_text):
			continue
		if radius > 0.0 and Vector2(row.get("position", Vector2.ZERO)).distance_to(epicenter) > radius:
			continue
		var maximum_member := int(row.get("member_maximum_health", 1))
		var amount_i := maxi(1, roundi(amount)) if flat else maxi(1, roundi(float(maximum_member) * amount))
		var health_values: Array = row.get("member_health", [])
		var restored := false
		for member_index in health_values.size():
			var current := int(health_values[member_index])
			if current <= 0 or current >= maximum_member:
				continue
			health_values[member_index] = mini(maximum_member, current + amount_i)
			restored = true
		if restored:
			row["member_health"] = health_values
			var aggregate := 0
			for value in health_values:
				aggregate += int(value)
			row["health"] = aggregate
			healed += 1
	if healed == 0:
		return {"ok": false, "reason": "no-wounded-allies-in-range"}
	return {"ok": true, "reason": "", "effect": "heal", "affected": healed}


func _apply_ability_modifier(hero_row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	## Converted attribute modifier with authored duration: rides the hero (and
	## allies inside the authored range when the module authors one) until
	## expiry. Keyed by the ability id: a recast refreshes the same grant
	## instead of stacking it (retail same-named AttributeModifier rule).
	var hero_id := int(hero_row.get("id", 0))
	var team := int(hero_row.get("team", -1))
	var duration_ticks := int(effect.get("duration_ticks", 1))
	var modifiers: Array = (effect.get("modifiers", []) as Array).duplicate(true)
	var range_limit := float(effect.get("range_scaled", 0.0))
	var targets: Array[int] = [hero_id]
	if range_limit > 0.0:
		for id in sim.living_ids(team):
			if id == hero_id:
				continue
			var ally: Dictionary = sim.entities[id]
			if Vector2(ally.get("position", Vector2.ZERO)).distance_to(Vector2(hero_row.get("position", Vector2.ZERO))) <= range_limit:
				targets.append(id)
	var expiry = sim.tick_index + duration_ticks
	for id in targets:
		sim._set_timed_modifier(sim.entities[id] as Dictionary, "ability:%s" % ability_id, modifiers, expiry)
	return {"ok": true, "reason": "", "effect": "attribute-modifier", "affected": targets.size()}


func _apply_ability_weapon_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	## TOGGLE_WEAPONSET: pin combat to the compiled toggle weapon-mode profile
	## (Legolas knives, Aragorn bow class), or release back to the default set
	## when already engaged. Fail-closed: the mode must exist among the unit's
	## compiled weapon modes or nothing changes.
	var toggle_mode := String(effect.get("toggleMode", ""))
	var modes: Dictionary = hero_row.get("weapon_modes", {}) as Dictionary
	if toggle_mode == "" or not modes.has(toggle_mode):
		return {"ok": false, "reason": "toggle-mode-unavailable:%s" % toggle_mode}
	var engaged := String(hero_row.get("weapon_toggle_mode", "")) != toggle_mode
	var resolved := toggle_mode if engaged else String(hero_row.get("default_weapon_mode", "default"))
	if not sim._apply_weapon_mode(hero_row, resolved):
		return {"ok": false, "reason": "weapon-mode-unavailable:%s" % toggle_mode}
	hero_row["weapon_toggle_mode"] = toggle_mode if engaged else ""
	hero_row["weapon_set_flags"] = [toggle_mode.to_upper()] if engaged else []
	return {"ok": true, "reason": "", "effect": "weapon-toggle", "affected": 1, "mode": resolved, "engaged": engaged}


func _apply_ability_siege_deploy(row: Dictionary, effect: Dictionary, point: Vector2, contract: Dictionary) -> Dictionary:
	if row.has("siege_deploy_channel"):
		return {"ok": false, "reason": "siege-deploy-already-active"}
	var target_id = sim._siege_deploy_target(row, effect, point, contract)
	if target_id == 0:
		return {"ok": false, "reason": "siege-deploy-target-missing"}
	if not bool(effect.get("skipAdjustPosition", false)):
		# Every BFME2/RotWK retail row authors Yes. Refuse an unseen grammar rather
		# than inventing SAGE's wall-contact path adjustment.
		return {"ok": false, "reason": "siege-deploy-position-adjustment-unsupported"}
	var lower_ticks = sim._ship_contract_delay_ticks(float(effect.get("lowerDelayMs", 0)))
	var channel := {
		"special_power_template_id": String(effect.get("specialPowerTemplateId", "")),
		"target_id": target_id,
		"current_target_id": target_id,
		"phase": "lowering",
		"phase_end_tick": sim.tick_index + lower_ticks,
		"raise_delay_ticks": sim._ship_contract_delay_ticks(float(effect.get("raiseDelayMs", 0))),
		"evacuate_passengers": bool(effect.get("evacuatePassengersOnDeploy", false)),
		"initiate_sound_id": String(effect.get("initiateSoundId", "")),
		"model_receipts": (effect.get("modelReceipts", []) as Array).duplicate(),
	}
	if effect.has("extraWallDistanceSource"):
		channel["extra_wall_distance_source"] = float(effect.get("extraWallDistanceSource", 0.0))
	row["siege_deploy_channel"] = channel
	sim._clear_pending_route(row, true)
	sim._clear_member_targets(row)
	row["target_id"] = 0
	row["target_kind"] = ""
	row["attack_move"] = false
	row["order_kind"] = ""
	row["state"] = "ability"
	sim._emit_event("ability.siege_deploy_started", int(row.get("id", 0)), target_id, {
		"lower_delay_ticks": lower_ticks,
		"initiate_sound_id": channel["initiate_sound_id"],
		"model_receipts": channel["model_receipts"],
	})
	sim._step_siege_deploy(row)
	return {"ok": true, "reason": "", "affected": 1, "target_id": target_id, "initiate_sound_id": channel["initiate_sound_id"], "model_receipts": channel["model_receipts"]}


func _apply_ability_toggle_deploy(row: Dictionary, effect: Dictionary) -> Dictionary:
	## Dwarven Demolisher self-toggle. Unlike SiegeDeploySpecialPower this has
	## no wall target, passenger evacuation or position adjustment: the paired
	## DeployStyleAIUpdate owns the pack/unpack clocks and persistent modifier.
	if String(effect.get("targetMode", "")) != "SELF":
		return {"ok": false, "reason": "toggle-deploy-target-mode-invalid"}
	if not bool(effect.get("ignoreFacingCheck", false)):
		return {"ok": false, "reason": "toggle-deploy-facing-check-unsupported"}
	if row.has("toggle_deploy_channel"):
		return {"ok": false, "reason": "toggle-deploy-transition-active"}
	var statuses := row.get("object_status", {}) as Dictionary
	var deploying := not bool(statuses.get("DEPLOYED", false))
	var sound_id := String(effect.get("soundDeployId" if deploying else "soundUndeployId", ""))
	if sound_id == "":
		return {"ok": false, "reason": "toggle-deploy-sound-missing"}
	var phase := "unpacking" if deploying else "packing"
	var duration_ticks := int(effect.get("unpack_ticks" if deploying else "pack_ticks", 0))
	if duration_ticks <= 0:
		return {"ok": false, "reason": "toggle-deploy-duration-invalid"}
	var modifier_leaf := effect.get("deployedAttributeModifier", {}) as Dictionary
	var modifiers := (modifier_leaf.get("modifiers", []) as Array).duplicate(true)
	if modifiers.is_empty():
		return {"ok": false, "reason": "toggle-deploy-modifier-missing"}
	row["toggle_deploy_channel"] = {
		"phase": phase,
		"phase_end_tick": sim.tick_index + duration_ticks,
		"sound_id": sound_id,
		"modifier_id": String(modifier_leaf.get("id", "")),
		"modifiers": modifiers,
		"presentation_receipt": "model-condition:%s" % phase.to_upper(),
	}
	sim._toggle_deploy_set_model_condition(row, phase.to_upper())
	sim._clear_pending_route(row, true)
	sim._clear_member_targets(row)
	row["target_id"] = 0
	row["target_kind"] = ""
	row["attack_move"] = false
	row["order_kind"] = ""
	row["state"] = "ability"
	sim._emit_event("ability.toggle_deploy_started", int(row.get("id", 0)), 0, {
		"phase": phase,
		"duration_ticks": duration_ticks,
		"sound_id": sound_id,
		"modifier_id": String(modifier_leaf.get("id", "")),
		"presentation_receipt": "model-condition:%s" % phase.to_upper(),
	})
	return {"ok": true, "reason": "", "affected": 1, "phase": phase, "sound_id": sound_id}


func _apply_ability_weapon_mode_special_power(row: Dictionary, effect: Dictionary) -> Dictionary:
	var template := String(effect.get("specialPowerTemplateId", ""))
	if template == "" or int(effect.get("duration_ticks", 0)) <= 0:
		return {"ok": false, "reason": "weapon-mode-special-power-malformed"}
	var policies := row.get("weapon_mode_special_powers", []) as Array
	var found := false
	for policy_value in policies:
		if String((policy_value as Dictionary).get("special_power_template", "")) == template:
			found = true; break
	if not found:
		var modifier_leaf := effect.get("attributeModifier", {}) as Dictionary
		var unsupported: Array[String] = []
		for unsupported_value in modifier_leaf.get("unsupportedModifiers", []) as Array:
			unsupported.append("unsupported_modifier_kind:%s" % String(unsupported_value))
		var mode = sim._resolve_weapon_mode_special_power_profile(row, effect.get("weaponSetFlags", []) as Array, String(effect.get("lockWeaponSlot", "")).to_lower())
		if (not (effect.get("weaponSetFlags", []) as Array).is_empty() or String(effect.get("lockWeaponSlot", "")) != "") and mode == "":
			unsupported.append("weapon_mode_profile_unavailable")
		policies.append({"key":"descriptor:%s"%template,"special_power_template":template,"duration_ticks":int(effect.get("duration_ticks",0)),"starts_paused":bool(effect.get("startsPaused",false)),"paused":bool(effect.get("startsPaused",false)),"modifier_name":String(modifier_leaf.get("id","")),"modifier":{"effects":(modifier_leaf.get("modifiers",[]) as Array).duplicate(true),"category":String(modifier_leaf.get("category",""))},"weapon_set_flags":(effect.get("weaponSetFlags",[]) as Array).duplicate(),"lock_weapon_slot":String(effect.get("lockWeaponSlot","")).to_lower(),"mode":mode,"active":false,"expires_tick":-1,"prior_mode":"","prior_toggle_mode":"","prior_weapon_set_flags":[],"activation_count":0,"unsupported_semantics":unsupported})
		row["weapon_mode_special_powers"] = policies
	return sim.activate_weapon_mode_special_power(int(row.get("id", 0)), template, int(row.get("team", -1)))


func _apply_ability_dominate_enemy(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2, targeting: String) -> Dictionary:
	## DominateEnemySpecialPower fires after its authored unpack/preparation
	## envelope and defects every accepted unit in the authored radius. Retail's
	## non-permanent form delegates restoration to the target's compiled
	## TemporarilyDefectUpdate::DefectDuration. Missing target evidence refuses.
	var permanent := bool(effect.get("permanentlyConvert", false))
	var defect_duration_ticks := int(effect.get("temporary_defect_duration_ticks", 0))
	if not permanent and defect_duration_ticks <= 0:
		return {"ok": false, "reason": "temporary-defect-duration-unresolved"}
	if not (row.get("dominate_enemy_channel", {}) as Dictionary).is_empty():
		return {"ok": false, "reason": "ability-channel-active"}
	var candidates = sim._dominate_enemy_candidates(row, effect, target_point, targeting)
	if candidates.is_empty():
		return {"ok": false, "reason": "no-domination-target"}
	var timing := effect.get("timing_ticks", {}) as Dictionary
	var activation_delay := int(timing.get("UnpackTime", 0)) + int(timing.get("PreparationTime", 0))
	var freeze_ticks := int(timing.get("FreezeAfterTriggerDuration", 0))
	var channel := {
		"ability_id": ability_id,
		"special_power_template_id": String(effect.get("specialPowerTemplateId", "")),
		"target_point": target_point,
		"targeting": targeting,
		"affects_filter": String(effect.get("affectsFilter", "")),
		"dominate_radius_scaled": float(effect.get("dominate_radius_scaled", 0.0)),
		"unpacking_variation": int(effect.get("unpackingVariation", 0)),
		"start_tick": sim.tick_index,
		"activation_tick": sim.tick_index + activation_delay,
		"finish_tick": sim.tick_index + activation_delay + freeze_ticks,
		"order_sequence_at_start": int(row.get("order_sequence", 0)),
		"triggered": false,
		"affected_ids": [],
		"permanently_convert": permanent,
		"temporary_defect_duration_ticks": defect_duration_ticks,
		"presentation": {
			"dominated_fx_id": String(effect.get("dominatedFxId", "")),
			"trigger_fx_id": String(effect.get("triggerFxId", "")),
			"trigger_sound_id": String(effect.get("triggerSoundId", "")),
			"trigger_model_condition": (effect.get("triggerModelCondition", {}) as Dictionary).duplicate(true),
			"trigger_model_condition_ticks": int(timing.get("TriggerModelConditionDuration", 0)),
		},
	}
	row["dominate_enemy_channel"] = channel
	row["current_speed"] = 0.0
	row["state"] = "ability"
	sim._emit_event("ability.dominate_started", int(row.get("id", 0)), int(candidates[0]), {
		"ability_id": ability_id, "activation_tick": channel.get("activation_tick"),
		"finish_tick": channel.get("finish_tick"), "unpacking_variation": channel.get("unpacking_variation"),
		"presentation": channel.get("presentation"),
	})
	return {"ok": true, "reason": "", "effect": "dominate-enemy", "affected": 0, "scheduled": true}


func _apply_ability_grab_passenger(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2) -> Dictionary:
	if row.has("grab_passenger_channel"):
		return {"ok": false, "reason": "ability-channel-active"}
	var containment_rule := effect.get("containment", {}) as Dictionary
	if sim.passenger_count(int(row.get("id", 0))) >= int(containment_rule.get("slots", 0)):
		return {"ok": false, "reason": "capacity-full"}
	var target_id = sim._grab_passenger_target(row, effect, target_point)
	if target_id <= 0:
		return {"ok": false, "reason": "no-admissible-passenger"}
	var acquire := effect.get("acquire", {}) as Dictionary
	for condition_value in acquire.get("rejectedConditions", []) as Array:
		var condition := String(condition_value).to_upper()
		if condition == "WEAPON_TOGGLE" and String(row.get("weapon_toggle_mode", "")) != "":
			return {"ok": false, "reason": "rejected-condition:WEAPON_TOGGLE"}
		if bool((row.get("object_status", {}) as Dictionary).get(condition, false)):
			return {"ok": false, "reason": "rejected-condition:%s" % condition}
	var timing := acquire.get("timing_ticks", {}) as Dictionary
	var animation := acquire.get("animation", {}) as Dictionary
	var prep_end = sim.tick_index + int(timing.get("UnpackTime", 0)) + int(timing.get("PreparationTime", 0))
	var trigger_tick = prep_end + int(animation.get("trigger_ticks", 0))
	var animation_end = prep_end + int(animation.get("duration_ticks", 0))
	var finish_tick := maxi(trigger_tick, animation_end) + int(timing.get("PersistentPrepTime", 0)) + int(timing.get("PackTime", 0))
	row["grab_passenger_channel"] = {
		"ability_id": ability_id, "target_id": target_id, "effect": effect.duplicate(true),
		"start_tick": sim.tick_index, "preparation_end_tick": prep_end,
		"trigger_tick": trigger_tick, "animation_end_tick": animation_end,
		"finish_tick": finish_tick, "order_sequence_at_start": int(row.get("order_sequence", 0)),
		"triggered": false,
	}
	row["state"] = "ability"; row["current_speed"] = 0.0
	if bool(effect.get("updateModuleStartsAttack", false)):
		row["target_id"] = target_id
	sim._emit_event("ability.grab_started", int(row.get("id", 0)), target_id, {"ability_id": ability_id, "trigger_tick": trigger_tick, "finish_tick": finish_tick, "animation": animation, "initiate_fx_id": String(effect.get("initiateFxId", ""))})
	return {"ok": true, "reason": "", "effect": "grab-passenger", "scheduled": true, "target_id": target_id}


func _apply_ability_repair_structure(row: Dictionary, ability_id: String, effect: Dictionary, target_point: Vector2) -> Dictionary:
	var rate := effect.get("repairRate", {}) as Dictionary
	if String(rate.get("status", "")) != "authored":
		return {"ok": false, "reason": "repair-rate-engine-default-unresolved"}
	var fraction := float(rate.get("maxHealthFractionPerSecond", 0.0))
	if fraction <= 0.0:
		return {"ok": false, "reason": "repair-rate-invalid"}
	var contact := effect.get("contactPoint", {}) as Dictionary
	if not bool(contact.get("authored", false)) or String(contact.get("name", "")) != "Repair":
		return {"ok": false, "reason": "repair-contact-point-unresolved"}
	var economy := effect.get("economy", {}) as Dictionary
	if String(economy.get("status", "")) != "no-authored-resource-field" or economy.get("resourceCost") != null:
		return {"ok": false, "reason": "repair-economy-unresolved"}
	var target_id := 0
	var best_distance := INF
	for structure_id in sim.structure_ids():
		var structure := sim.structures[structure_id] as Dictionary
		if int(structure.get("team", -1)) != int(row.get("team", -2)) or int(structure.get("health", 0)) <= 0:
			continue
		var maximum := maxi(1, int(structure.get("maximum_health", structure.get("health", 1))))
		if int(structure.get("health", 0)) >= maximum:
			continue
		var distance := Vector2(structure.get("position", Vector2.ZERO)).distance_to(target_point)
		if distance > sim._structure_footprint_radius(structure) or distance >= best_distance:
			continue
		target_id = structure_id; best_distance = distance
	if target_id <= 0:
		return {"ok": false, "reason": "no-damaged-allied-structure-at-repair-contact"}
	row["repair_structure_channel"] = {
		"ability_id": ability_id, "target_id": target_id,
		"max_health_fraction_per_second": fraction, "fractional_health": 0.0,
		"order_sequence_at_start": int(row.get("order_sequence", 0)),
		"contact_point": contact.duplicate(true), "economy": economy.duplicate(true),
	}
	row["state"] = "repair"; row["current_speed"] = 0.0; row["target_id"] = target_id; row["target_kind"] = "structure"
	sim._emit_event("ability.repair_started", int(row.get("id", 0)), target_id, {"ability_id": ability_id, "fraction_per_second": fraction, "contact_point": "Repair", "resource_cost": null})
	return {"ok": true, "reason": "", "effect": "repair-structure", "scheduled": true, "target_id": target_id}


func _apply_ability_fling_passenger(row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	if row.has("fling_passenger_channel"):
		return {"ok": false, "reason": "ability-channel-active"}
	var passengers := sim.containment.get(int(row.get("id", 0)), []) as Array
	if passengers.is_empty():
		return {"ok": false, "reason": "no-contained-passenger"}
	var timing := effect.get("timing_ticks", {}) as Dictionary
	row["fling_passenger_channel"] = {
		"ability_id": ability_id, "passenger_id": int(passengers[0]), "effect": effect.duplicate(true),
		"start_tick": sim.tick_index, "trigger_tick": sim.tick_index + int(timing.get("UnpackTime", 0)),
		"finish_tick": sim.tick_index + int(timing.get("UnpackTime", 0)) + int(timing.get("PackTime", 0)),
		"order_sequence_at_start": int(row.get("order_sequence", 0)), "triggered": false,
	}
	row["state"] = "ability"; row["current_speed"] = 0.0
	return {"ok": true, "reason": "", "effect": "fling-passenger", "scheduled": true}


func _apply_ability_mount_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	## SpecialAbilityToggleMounted: the mounted state swaps the compiled
	## SET_MOUNTED locomotor speed and (when authored) pins combat to the
	## compiled "mounted" weapon-mode profile; dismounting restores the foot
	## stats. Health is untouched by the same-object condition swap — the
	## authored fraction is preserved exactly. When a future compiled rule
	## authors a mounted member health, the swap rescales preserving the live
	## health fraction instead of resetting it.
	if bool(hero_row.get("mounted", false)):
		# The shipped SpecialDisguiseUpdate cancel(bool) distinguishes a
		# dismount-driven abort from an explicit/attack abort: both clear the
		# DISGUISED state and pack opacity, but dismount suppresses DisguiseFX.
		if hero_row.has("special_disguise_channel"):
			sim._cancel_special_disguise_row(hero_row, "dismount", true)
		# Dismount: restore the recorded foot profile.
		if (
			String(hero_row.get("weapon_toggle_mode", "")) == "mounted"
			and not sim._apply_weapon_mode(
				hero_row,
				String(hero_row.get("default_weapon_mode", "default"))
			)
		):
			return {"ok": false, "reason": "weapon-mode-unavailable:default"}
		hero_row["speed"] = float(hero_row.get("dismounted_speed", hero_row.get("speed", 0.0)))
		hero_row["speed_source"] = float(hero_row.get("dismounted_speed_source", hero_row.get("speed_source", 0.0)))
		hero_row.erase("dismounted_speed")
		hero_row.erase("dismounted_speed_source")
		if String(hero_row.get("weapon_toggle_mode", "")) == "mounted":
			hero_row["weapon_toggle_mode"] = ""
		hero_row["mounted"] = false
		# ERASE, never set to "". The alt-form voice key is read by
		# `_voice_event_identity` and is otherwise ABSENT: a key that exists on
		# every hero row (even empty) would be walked by `_canonicalize` and
		# would move the determinism pin for units that never mount.
		hero_row.erase("form")
		sim._rescale_member_health_preserving_fraction(hero_row, int(effect.get("dismountedMemberHealth", 0)))
		return {"ok": true, "reason": "", "effect": "mount-toggle", "affected": 1, "mounted": false}
	var mounted_speed_scaled := float(effect.get("mounted_speed_scaled", 0.0))
	if mounted_speed_scaled <= 0.0:
		return {"ok": false, "reason": "mount-speed-unresolved"}
	var mode := String(effect.get("mountedWeaponModeKey", ""))
	if mode != "" and not (hero_row.get("weapon_modes", {}) as Dictionary).has(mode):
		# The compiled rule authored a mounted weapon the unit rule does not
		# carry: never a partial swap.
		return {"ok": false, "reason": "mount-mode-unavailable:%s" % mode}
	hero_row["dismounted_speed"] = float(hero_row.get("speed", 0.0))
	hero_row["dismounted_speed_source"] = float(hero_row.get("speed_source", 0.0))
	hero_row["speed"] = mounted_speed_scaled
	hero_row["speed_source"] = float(effect.get("mountedSpeed", 0.0))
	if mode != "":
		if not sim._apply_weapon_mode(hero_row, mode):
			var restored_speed := float(hero_row.get("dismounted_speed", hero_row.get("speed", 0.0)))
			var restored_speed_source := float(hero_row.get("dismounted_speed_source", hero_row.get("speed_source", 0.0)))
			hero_row.erase("dismounted_speed")
			hero_row.erase("dismounted_speed_source")
			hero_row["speed"] = restored_speed
			hero_row["speed_source"] = restored_speed_source
			return {"ok": false, "reason": "weapon-mode-unavailable:%s" % mode}
		hero_row["weapon_toggle_mode"] = mode
	hero_row["mounted"] = true
	# Retail heroes with a MOUNTED ModelConditionFlag author a SECOND voice set
	# for the mounted form (theoden.ini: VoiceSelect = TheodenVoiceSelectMS
	# TheodenVoiceSelectMountedMS, VoiceMove = TheodenVoiceMove
	# TheodenVoiceMoveMounted). The audio module already classifies and prefers
	# those alternates; the form is the one thing only the sim can know.
	hero_row["form"] = "mounted"
	sim._rescale_member_health_preserving_fraction(hero_row, int(effect.get("mountedMemberHealth", 0)))
	return {"ok": true, "reason": "", "effect": "mount-toggle", "affected": 1, "mounted": true}


func _apply_ability_special_disguise(row: Dictionary, effect: Dictionary) -> Dictionary:
	if row.has("special_disguise_channel"):
		return {"ok": false, "reason": "special-disguise-active"}
	if not bool(effect.get("forceMountedWhenDisguising", false)):
		return {"ok": false, "reason": "special-disguise-force-mount-unproven"}
	if not bool(row.get("mounted", false)):
		var mount_effect: Dictionary = {}
		for rule_value in sim._unit_ability_rules.get(String(row.get("unit_type", "")), []) as Array:
			var candidate := (rule_value as Dictionary).get("effect", {}) as Dictionary
			if String(candidate.get("kind", "")) == "mount-toggle":
				if not mount_effect.is_empty():
					return {"ok": false, "reason": "special-disguise-mount-ambiguous"}
				mount_effect = candidate
		if mount_effect.is_empty():
			return {"ok": false, "reason": "special-disguise-mount-unavailable"}
		var mounted := _apply_ability_mount_toggle(row, mount_effect)
		if not bool(mounted.get("ok", false)) or not bool(row.get("mounted", false)):
			return {"ok": false, "reason": "special-disguise-force-mount-failed"}
	sim._clear_pending_route(row, true)
	row["order_kind"] = ""
	row["state"] = "idle"
	var unpack_ticks := _special_disguise_duration_ticks(effect.get("unpackTimeMs", 0))
	row["special_disguise_channel"] = {
		"phase": "unpacking",
		"phase_start_tick": sim.tick_index,
		"phase_end_tick": sim.tick_index + unpack_ticks,
		"unpack_ticks": unpack_ticks,
		"preparation_ticks": _special_disguise_duration_ticks(effect.get("preparationTimeMs", 0)),
		"persistent_prep_ticks": _special_disguise_duration_ticks(effect.get("persistentPrepTimeMs", 0)),
		"pack_ticks": _special_disguise_duration_ticks(effect.get("packTimeMs", 0)),
		"opacity_target": float(effect.get("opacityTarget", 1.0)),
		"owner_object_id": String(effect.get("ownerObjectId", "")),
		"owner_disguise_template_id": String(effect.get("ownerDisguiseTemplateId", "")),
		"hostile_disguise_template_id": String(effect.get("hostileDisguiseTemplateId", "")),
		"disguise_fx_id": String(effect.get("disguiseFxId", "")),
		"presentation_prerequisite_sha256": String(effect.get("presentationPrerequisiteSha256", "")),
		"deferred_boundaries": (effect.get("deferredBoundaries", []) as Array).duplicate(),
	}
	sim._emit_special_disguise_presentation(row, "owner-mounted-presentation", String(effect.get("ownerObjectId", "")), true)
	sim._emit_event("ability.special_disguise_started", int(row.get("id", 0)), 0, {
		"phase": "unpacking", "phase_end_tick": sim.tick_index + unpack_ticks,
		"presentation_prerequisite_sha256": String(effect.get("presentationPrerequisiteSha256", "")),
	})
	return {"ok": true, "reason": "", "effect": "special-disguise", "affected": 1, "phase": "unpacking"}


# De-staticed on extraction: it reads the sim const through the instance ref.
func _special_disguise_duration_ticks(milliseconds: Variant) -> int:
	return maxi(1, ceili(float(milliseconds) / (sim.TICK_SECONDS * 1000.0)))


func _apply_ability_capture_building(hero_row: Dictionary, effect: Dictionary, target_point: Vector2) -> Dictionary:
	## Capture building, tier-1 honest scope: a hero channels the authored
	## unpack+preparation+pack envelope on a NEUTRAL structure flagged
	## capturable; completion transfers ownership (_step_entity finishes or
	## cancels the channel). Owned sim.structures are out of tier-1 scope and
	## fail closed with their own reason.
	if not (hero_row.get("capture_channel", {}) as Dictionary).is_empty():
		return {"ok": false, "reason": "capture-in-progress"}
	var hero_team := int(hero_row.get("team", -1))
	var best_id := 0
	var best_distance := 2.0
	var found_owned := false
	for structure_id in sim.structure_ids():
		var structure: Dictionary = sim.structures[structure_id]
		if int(structure.get("health", 0)) <= 0 or not bool(structure.get("capturable", false)):
			continue
		var distance := Vector2(structure.get("position", Vector2.ZERO)).distance_to(target_point)
		if distance > best_distance:
			continue
		if int(structure.get("team", -1)) != sim.NEUTRAL_TEAM:
			found_owned = int(structure.get("team", -1)) != hero_team or found_owned
			continue
		best_distance = distance
		best_id = structure_id
	if best_id == 0:
		return {"ok": false, "reason": "capture-tier1-neutral-only" if found_owned else "no-capturable-structure"}
	var channel_ticks := maxi(1, int(effect.get("channel_ticks", 1)))
	hero_row["capture_channel"] = {
		"structure_id": best_id,
		"complete_tick": sim.tick_index + channel_ticks,
	}
	# Channeling holds the hero: drop any live order/target so the capture
	# stance is unambiguous (a later order cancels the channel).
	hero_row["target_id"] = 0
	hero_row["target_kind"] = "battalion"
	hero_row["attack_windup"] = 0
	sim._clear_member_attack_schedule(hero_row)
	sim._clear_member_targets(hero_row)
	sim._clear_pending_route(hero_row, true)
	hero_row["state"] = "capture"
	sim._emit_event("structure.capture_started", int(hero_row.get("id", 0)), best_id, {
		"team": hero_team,
		"structure_id": best_id,
		"channel_ticks": channel_ticks,
	})
	return {"ok": true, "reason": "", "effect": "capture-building", "affected": 1, "structure_id": best_id}


func _apply_ability_terror(hero_row: Dictionary, ability_id: String, effect: Dictionary) -> Dictionary:
	## Terror/fear (Screech family): every enemy battalion inside the authored
	## radius takes the compiled penalty modifiers for the authored duration,
	## and an authored scatter displaces ground victims away from the caster
	## through the shared walkable-fraction displacement — without the
	## knockdown sprawl (fleeing, not bowled over). Fear-resistant units (only
	## when the compiled rule authors the flag) and RESIST_FEAR carriers are
	## immune to the debuff; flyers are immune to the scatter only.
	var team := int(hero_row.get("team", -1))
	var origin := Vector2(hero_row.get("position", Vector2.ZERO))
	var radius := float(effect.get("radius_scaled", 0.0))
	var duration_ticks := int(effect.get("duration_ticks", 1))
	var modifiers: Array = (effect.get("modifiers", []) as Array).duplicate(true)
	if radius <= 0.0 or modifiers.is_empty():
		return {"ok": false, "reason": "terror-fields-missing"}
	var scatter := float(effect.get("scatter_strength_scaled", 0.0))
	var filter_text := String(effect.get("affects", ""))
	var expiry = sim.tick_index + duration_ticks
	var affected := 0
	for id in sim._ability_enemies_near(team, origin, radius):
		var target: Dictionary = sim.entities[id]
		if bool(target.get("fear_resistant", false)) or sim._timed_modifier_active(target, "RESIST_FEAR"):
			continue
		if not sim._ability_filter_accepts(target, filter_text):
			continue
		sim._set_timed_modifier(target, "fear:%s" % ability_id, modifiers, expiry)
		affected += 1
		if scatter > 0.0 and not bool(target.get("flying", false)):
			sim._apply_fear_scatter(origin, target, scatter)
	return {"ok": true, "reason": "", "effect": "terror", "affected": affected}


func _apply_ability_summon(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	## Converted ObjectCreationList summon: each created object must resolve to
	## a converted unit rule or the cast fails closed (never a stand-in).
	var team := int(hero_row.get("team", -1))
	# Retail hero summons hatch: the ability's ObjectCreationList creates a
	# model-less egg (AragornArmyofTheDeadSmallEgg) whose SlowDeathBehavior OCL
	# spawns the real battalions. `effect.objects` names only that egg, and no
	# playable-unit document ever describes it, so the legacy lookup below
	# always failed closed and the power did nothing at all. When the converter
	# published the ability's leaf closure, walk the same egg -> hatch -> horde
	# -> member chain the spellbook powers already walk.
	var chained := _apply_ability_summon_chain(team, effect, point)
	if not chained.is_empty():
		return chained
	var summoned: Array = []
	var ordinal := 0
	for entry_value in effect.get("objects", []) as Array:
		var entry := entry_value as Dictionary
		var source_id := String(entry.get("id", ""))
		var unit_type = sim._summon_unit_type_for(source_id)
		if unit_type == "":
			return {"ok": false, "reason": "summon-object-unconverted:%s" % source_id}
		var rule: Dictionary = sim._unit_production_rules.get(unit_type, {}) as Dictionary
		var member_id := String(rule.get("object_id", ""))
		if member_id == "":
			return {"ok": false, "reason": "summon-object-unconverted:%s" % source_id}
		var count := clampi(int(entry.get("count", 1)), 1, sim.ABILITY_SUMMON_MAX_COUNT)
		for _index in range(count):
			ordinal += 1
			var new_id := int(sim._next_dynamic_id.get(team, 0))
			sim._next_dynamic_id[team] = new_id + 1
			var offset := Vector2(sim.ABILITY_SUMMON_OFFSET_STEP * float(ordinal), 0.0)
			sim._add_battalion(new_id, team, point + offset, String(rule.get("display_name", unit_type)), member_id, unit_type)
			if not sim.entities.has(new_id):
				return {"ok": false, "reason": "summon-spawn-failed:%s" % source_id}
			sim._emit_event("unit.summoned", new_id, 0, {"team": team, "object_id": member_id, "unit_type": unit_type})
			summoned.append(new_id)
	return {"ok": true, "reason": "", "effect": "summon", "affected": summoned.size(), "summoned": summoned}


func _apply_ability_summon_chain(team: int, effect: Dictionary, point: Vector2) -> Dictionary:
	## Resolve a hero summon through its converted leaf closure with the proven
	## spellbook OCL machinery. Returns {} when the ability carries no closure
	## (older documents) so the caller keeps its playable-unit lookup, and a
	## fail-closed result carrying the exact leaf gap when the closure is
	## present but the chain does not convert — never a stand-in summon.
	var leaves: Dictionary = effect.get("leaves", {}) as Dictionary
	if leaves.is_empty():
		return {}
	var ocl_id := String(effect.get("oclId", ""))
	if ocl_id == "":
		return {}
	var object_leaves: Dictionary = {}
	for object_value in Array(leaves.get("objects", [])):
		if typeof(object_value) == TYPE_DICTIONARY:
			object_leaves[String((object_value as Dictionary).get("id", ""))] = object_value
	var ocl_leaves: Dictionary = {}
	for ocl_value in Array(leaves.get("objectCreationLists", [])):
		if typeof(ocl_value) == TYPE_DICTIONARY:
			ocl_leaves[String((ocl_value as Dictionary).get("id", ""))] = ocl_value
	sim.ingest_ocl_leaves_from_document({"leaves": leaves})
	var weapon_leaves: Dictionary = {}
	for weapon_value in Array(leaves.get("weapons", [])):
		if typeof(weapon_value) == TYPE_DICTIONARY:
			weapon_leaves[String((weapon_value as Dictionary).get("id", ""))] = weapon_value
	var modifier_leaves: Dictionary = {}
	for modifier_value in Array(leaves.get("attributeModifiers", [])):
		if typeof(modifier_value) == TYPE_DICTIONARY:
			modifier_leaves[String((modifier_value as Dictionary).get("id", ""))] = modifier_value
	var support = sim._spellbook_ocl_support(
		{}, {"objectCreationLists": [ocl_id]}, modifier_leaves, object_leaves, ocl_leaves, weapon_leaves
	)
	if not bool(support.get("ok", false)):
		return {"ok": false, "reason": "summon-chain-unconverted:%s" % String(support.get("reason", ""))}
	var resolved: Dictionary = support.get("effect", {}) as Dictionary
	if String(resolved.get("kind", "")) != "summon":
		# Fire-weapon receptacles and structure spawns are other powers' shapes;
		# a hero summon button never presents them.
		return {"ok": false, "reason": "summon-chain-unsupported-kind:%s" % String(resolved.get("kind", ""))}
	# Resolve declarative choice groups and schedule through the exact spellbook
	# cast path. This keeps hero and spellbook summons on one RNG/timing contract.
	var cast = sim._cast_spellbook_summon(team, resolved, point)
	if not bool(cast.get("ok", false)):
		return {"ok": false, "reason": "summon-chain-%s" % String(cast.get("reason", "cast-failed"))}
	var pending := int(cast.get("summon_count", 0))
	return {
		"ok": true,
		"reason": "",
		"effect": "summon",
		"affected": pending,
		"summon_count": pending,
		"summoned": [],
	}


func _apply_ability_arrow_storm(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	## ArrowStormUpdate barrage (Legolas Arrow Storm, Gandalf Lightning Sword):
	## the hero channels MaxShots weapon shots in ShotsPerBurst volleys on the
	## authored PersistentPrepTime cadence against enemies inside the authored
	## TargetRadius of the target point (deterministic round-robin, ascending
	## entity ids). Fail-closed: every consumed magnitude must be authored,
	## and a cast onto empty ground needs the authored CanShootEmptyGround.
	if not (hero_row.get("volley_channel", {}) as Dictionary).is_empty():
		return {"ok": false, "reason": "volley-in-progress"}
	var damage := int(effect.get("weaponDamage", 0))
	var radius := float(effect.get("target_radius_scaled", 0.0))
	var max_shots := int(effect.get("maxShots", 0))
	if damage <= 0 or radius <= 0.0 or max_shots <= 0:
		return {"ok": false, "reason": "arrow-storm-fields-missing"}
	var team := int(hero_row.get("team", -1))
	var can_shoot_empty_ground := bool(effect.get("canShootEmptyGround", false))
	if not can_shoot_empty_ground and sim._ability_enemies_near(team, point, radius).is_empty():
		return {"ok": false, "reason": "no-target"}
	hero_row["volley_channel"] = {
		"point": point,
		"radius": radius,
		"damage": damage,
		"shots_left": max_shots,
		"shots_fired": 0,
		"shots_per_burst": maxi(1, int(effect.get("shotsPerBurst", 1))),
		"interval_ticks": maxi(1, int(effect.get("shot_interval_ticks", 1))),
		"can_shoot_empty_ground": can_shoot_empty_ground,
		"next_shot_tick": sim.tick_index + 1,
	}
	# Channeling holds the hero (same stance as the capture envelope): drop
	# any live order/target so a later order cancels the volley cleanly.
	hero_row["target_id"] = 0
	hero_row["target_kind"] = "battalion"
	hero_row["attack_windup"] = 0
	sim._clear_member_attack_schedule(hero_row)
	sim._clear_member_targets(hero_row)
	sim._clear_pending_route(hero_row, true)
	hero_row["state"] = "volley"
	return {"ok": true, "reason": "", "effect": "arrow-storm", "affected": 0}


func _apply_ability_stealth_toggle(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	## ToggleHiddenSpecialAbilityUpdate / InvisibilitySpecialPower: cloak the
	## hero (and, when the module authors a BroadcastRadius, allies inside it)
	## for the authored EffectDuration. Stealthed battalions are skipped by
	## enemy auto-acquisition; the authored ForbiddenConditions break the
	## cloak early. Recasting the toggle drops it (retail toggle-hidden).
	var duration_ticks := int(effect.get("duration_ticks", 0))
	var untimed := bool(effect.get("untimed", false))
	if duration_ticks <= 0 and not untimed:
		return {"ok": false, "reason": "stealth-fields-missing"}
	if sim._stealth_active(hero_row):
		sim._clear_stealth(hero_row)
		return {"ok": true, "reason": "", "effect": "stealth-toggle", "affected": 1, "engaged": false}
	var forbidden: Array = (effect.get("forbiddenConditions", []) as Array).duplicate()
	# A ToggleHiddenSpecialAbilityUpdate that authors no EffectDuration holds
	# until the player recasts it or a ForbiddenCondition breaks it, so it gets
	# an expiry tick no run reaches instead of a timer retail never authored.
	var until_tick = sim.STEALTH_UNTIMED_EXPIRY_TICK if untimed else sim.tick_index + duration_ticks
	sim._grant_stealth(hero_row, until_tick, forbidden)
	var affected := 1
	var broadcast := float(effect.get("broadcast_radius_scaled", 0.0))
	if broadcast > 0.0:
		var team := int(hero_row.get("team", -1))
		var origin := Vector2(hero_row.get("position", Vector2.ZERO))
		var filter_text := String(effect.get("affects", ""))
		for id in sim.living_ids(team):
			if id == int(hero_row.get("id", 0)):
				continue
			var ally: Dictionary = sim.entities[id]
			if Vector2(ally.get("position", Vector2.ZERO)).distance_to(origin) > broadcast:
				continue
			if not sim._ability_filter_accepts(ally, filter_text):
				continue
			sim._grant_stealth(ally, until_tick, forbidden)
			affected += 1
	return {"ok": true, "reason": "", "effect": "stealth-toggle", "affected": affected, "engaged": true}


func _apply_ability_teleport(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	## TeleportSpecialAbilityUpdate: deterministic relocation to the requested
	## point. A positive authored MaxDistance is enforced by the generic cast
	## gate; omission is the retail unlimited/default form. This module does not
	## itself author a pathability gate, so relocation must not invent one.
	var destination_weapon := effect.get("destinationWeapon", {}) as Dictionary
	if not destination_weapon.is_empty() and (
		String(destination_weapon.get("affects", "")) != "ENEMIES"
		or int(destination_weapon.get("damage", -1)) != 0
		or float(destination_weapon.get("knockback_radius", 0.0)) <= 0.0
		or float(destination_weapon.get("knockback_strength", 0.0)) <= 0.0
		or float(destination_weapon.get("knockbackTaperOff", 0.0)) <= 0.0
		or float(destination_weapon.get("knockbackZMult", 0.0)) <= 0.0
	):
		return {"ok": false, "reason": "teleport-destination-weapon-invalid"}
	hero_row["position"] = point
	sim._spatial_sync(hero_row)
	hero_row["target_id"] = 0
	hero_row["target_kind"] = "battalion"
	hero_row["attack_windup"] = 0
	sim._clear_member_attack_schedule(hero_row)
	sim._clear_member_targets(hero_row)
	sim._clear_pending_route(hero_row, true)
	hero_row["current_speed"] = 0.0
	hero_row["state"] = "idle"
	var busy_ticks := int(effect.get("busy_ticks", 0))
	if busy_ticks > 0:
		hero_row["ability_hold_until_tick"] = sim.tick_index + busy_ticks
	var destination_affected := 0
	if not destination_weapon.is_empty():
		destination_affected = sim._apply_knockback(
			point,
			float(destination_weapon["knockback_radius"]),
			float(destination_weapon["knockback_strength"]),
			int(hero_row.get("team", -1)),
			0,
			"teleport-destination",
			int(hero_row.get("id", 0)),
			float(destination_weapon["knockbackTaperOff"]),
			float(destination_weapon["knockbackZMult"]),
		)
		var fire_fx_id := String(destination_weapon.get("fireFxId", ""))
		if fire_fx_id != "":
			sim._emit_event("ability.graph_fx", int(hero_row.get("id", 0)), 0, {"fx_id": fire_fx_id, "point": point})
	return {"ok": true, "reason": "", "effect": "teleport", "affected": 1, "destination_affected": destination_affected}


func _apply_ability_curse(hero_row: Dictionary, effect: Dictionary, point: Vector2) -> Dictionary:
	## CurseSpecialPower (Hour of the Witch-King): the nearest enemy hero
	## inside the authored radius cursor loses the authored CursePercentage of
	## every ability recharge — its cooldowns restart scaled by the authored
	## percentage, never beyond one full authored recharge.
	var percentage := float(effect.get("cursePercentage", 0.0))
	var radius := float(effect.get("radius_scaled", 0.0))
	if percentage <= 0.0 or radius <= 0.0:
		return {"ok": false, "reason": "curse-fields-missing"}
	var team := int(hero_row.get("team", -1))
	var best_id := -1
	var best_distance := radius
	for id in sim._ability_enemies_near(team, point, radius):
		var target: Dictionary = sim.entities[id]
		if String(target.get("category", "")) != "hero":
			continue
		var distance := Vector2(target.get("position", Vector2.ZERO)).distance_to(point)
		if distance <= best_distance:
			best_distance = distance
			best_id = id
	if best_id < 0:
		return {"ok": false, "reason": "no-enemy-hero-in-radius"}
	var target_row: Dictionary = sim.entities[best_id]
	var states: Dictionary = target_row.get("ability_states", {}) as Dictionary
	var cursed := 0
	var ability_keys := states.keys()
	ability_keys.sort()
	for ability_key in ability_keys:
		var state: Dictionary = states[ability_key] as Dictionary
		var cooldown_ticks := int(state.get("cooldown_ticks", 0))
		if cooldown_ticks <= 0:
			continue
		var setback = sim.tick_index + roundi(float(cooldown_ticks) * minf(percentage, 100.0) / 100.0)
		state["cooldown_ready_tick"] = maxi(int(state.get("cooldown_ready_tick", 0)), setback)
		states[ability_key] = state
		cursed += 1
	target_row["ability_states"] = states
	return {"ok": true, "reason": "", "effect": "curse", "affected": 1, "target_id": best_id, "cursed_abilities": cursed}


func _apply_ability_leadership_strip(hero_row: Dictionary, effect: Dictionary) -> Dictionary:
	## SpecialPowerModule AntiCategory=LEADERSHIP (Horn of Gondor): enemies
	## inside the authored AttributeModifierRange lose their leadership aura
	## grants and cannot receive new ones for the authored anti-category
	## duration (the paired ModifierList authors only that duration).
	var radius := float(effect.get("radius_scaled", 0.0))
	var duration_ticks := int(effect.get("duration_ticks", 0))
	if radius <= 0.0 or duration_ticks <= 0:
		return {"ok": false, "reason": "leadership-strip-fields-missing"}
	var team := int(hero_row.get("team", -1))
	var origin := Vector2(hero_row.get("position", Vector2.ZERO))
	var suppression_source := "horn:%d:%d" % [
		int(hero_row.get("id", 0)), sim.tick_index
	]
	var affected := 0
	for id in sim._ability_enemies_near(team, origin, radius):
		var target: Dictionary = sim.entities[id]
		var table: Dictionary = target.get("timed_modifiers", {}) as Dictionary
		var stripped: Array[String] = []
		for key_value in table.keys():
			if String(key_value).begins_with("aura:"):
				stripped.append(String(key_value))
		stripped.sort()
		for key in stripped:
			table.erase(key)
		target["timed_modifiers"] = table
		sim._set_leadership_suppression_source(
			target, suppression_source, sim.tick_index + duration_ticks
		)
		affected += 1
	if affected == 0:
		return {"ok": false, "reason": "no-enemies-in-radius"}
	return {"ok": true, "reason": "", "effect": "leadership-strip", "affected": affected}


# --- Shared timed-modifier core ---
# One mechanism serves timed ability buffs, leadership auras, and fear: a
# per-entity keyed table of {modifiers, expires_tick}. Multiplicative kinds
# (DAMAGE_MULT/SPEED/VISION/EXPERIENCE/CRUSH/HEALTH) compound across keys,
# ARMOR sums, flag kinds (INVULNERABLE/RESIST_FEAR) trigger at value >= 1.
