extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Object-behavior contracts carved out of retail_sim_module_contracts.gd (crack-2 finish): castle members, collide/squish/crush, flammable, portals, foundation, refund-die, wall hubs, attach/monitor updates, replace-self, citadel, stances, auras, lifetimes, sound selectors, fire spread, fear/poison/damage fields, spawn-unit, hit reactions, animal AI, threat finders.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func _attach_castle_member_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("castle_member_behavior"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var counts_value: Variant = _sim._module_contract_value(fields, "CountsForEvaCastleBreached", true)
	var store_value: Variant = _sim._module_contract_value(fields, "StoreUpgradePrice", false)
	if typeof(counts_value) != TYPE_BOOL or typeof(store_value) != TYPE_BOOL:
		return
	var presentation: Dictionary = {}
	for key in ["BeingBuiltSound", "CampDestroyedOwnerEvaEvent", "CampDestroyedAllyEvaEvent", "CampDestroyedAttackerEvaEvent"]:
		var value = String(_sim._module_contract_value(fields, key, "")).strip_edges()
		if value != "":
			presentation[key] = value
	var unsupported: Array[String] = []
	if bool(store_value):
		# Retail source says this overloads the refund price with purchased
		# upgrades. The sim has upgrade costs, but no typed CastleMember field
		# states whether the later refund is sale, capture, or destruction and at
		# what percentage; retaining the policy is safer than inventing money.
		unsupported.append("StoreUpgradePrice:refund-route-and-percentage-unresolved")
	if presentation.has("BeingBuiltSound"):
		unsupported.append("BeingBuiltSound:presentation-audio-route")
	row["castle_member_behavior"] = {
		"is_castle_member": true,
		"counts_for_eva_castle_breached": bool(counts_value),
		"store_upgrade_price": bool(store_value),
		"presentation": presentation,
		"unsupported_semantics": unsupported,
		"breach_dispatched": false,
		"tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0)),
	}


func _dispatch_castle_member_destroyed(structure_id: int, member: Dictionary, attacker_id: int, reason: String) -> void:
	var _sim = sim
	var policy := member.get("castle_member_behavior", {}) as Dictionary
	if policy.is_empty() or bool(policy.get("breach_dispatched", false)):
		return
	policy["breach_dispatched"] = true
	member["castle_member_behavior"] = policy
	var presentation := policy.get("presentation", {}) as Dictionary
	var attacker_team := int((_sim.entities.get(attacker_id, {}) as Dictionary).get("team", -1))
	var owner_team := int(member.get("team", -1))
	var eva_routes := {
		"owner": String(presentation.get("CampDestroyedOwnerEvaEvent", "")),
		"ally": String(presentation.get("CampDestroyedAllyEvaEvent", "")),
		"attacker": String(presentation.get("CampDestroyedAttackerEvaEvent", "")),
	}
	_sim._emit_event("castle.member_destroyed", attacker_id, structure_id, {"team": owner_team, "attacker_team": attacker_team, "reason": reason, "counts_for_breach": bool(policy.get("counts_for_eva_castle_breached", true)), "eva_routes": eva_routes, "presentation_dispatch": "retail_slice_audio_or_eva_binding_required"})
	if bool(policy.get("counts_for_eva_castle_breached", true)):
		_sim._emit_event("castle.breached", attacker_id, structure_id, {"team": owner_team, "attacker_team": attacker_team, "reason": reason})


func _attach_inactive_body_contract(row: Dictionary, contract: Dictionary) -> void:
	## InactiveBody has no authored activation transition: presence is the whole
	## retail body policy and means no damage/body state changes are accepted.
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	if fields.size() != 1 or typeof(fields.get("indestructible")) != TYPE_BOOL or not bool(fields.get("indestructible")):
		return
	row["inactive_body"] = {"indestructible": true, "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))}
	row["indestructible"] = true


func _attach_squish_collide_contract(row: Dictionary, contract: Dictionary) -> void:
	## Fieldless victim-side marker. Damage, levels, and speed are authored on
	## locomotor/scalar/weapon contracts and stay in the shared crush core.
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields: Variant = contract.get("fields", {})
	if typeof(fields) != TYPE_DICTIONARY or not (fields as Dictionary).is_empty():
		return
	row["squish_collide"] = {"admission": "authored-victim-collision", "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))}


func _squish_collision_admitted(victim: Dictionary) -> bool:
	if not victim.has("squish_collide"):
		sim._attach_module_contracts(victim)
	if victim.has("squish_collide"):
		return true
	# Synthetic/legacy rows without any selected descriptor retain the historic
	# trample lane. Once a descriptor exists, absence of SquishCollide is an
	# authored refusal rather than a fallback.
	return not victim.has("module_contracts")


func _attach_horde_member_collide_contract(row: Dictionary, contract: Dictionary) -> void:
	## Retail's fieldless marker opts an individual member body into the horde
	## collision resolver. This sim currently integrates one battalion transform;
	## formation offsets are attack/presentation coordinates, not independent
	## collision bodies. Preserve the authored marker and exact missing seam, but
	## do not invent separation impulses or mutate the aggregate route.
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields: Variant = contract.get("fields", {})
	if typeof(fields) != TYPE_DICTIONARY or not (fields as Dictionary).is_empty():
		return
	row["horde_member_collide"] = {
		"enabled": true,
		"execution": "deferred-individual-member-collision-world",
		"unsupported_semantics": ["independent-member-body", "member-separation-impulse"],
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func _attach_notify_crushing_contract(row: Dictionary, contract: Dictionary) -> void:
	## This marker has no authored cadence, scan radius, probability, or target
	## response. The current crush core can prove actual contact but cannot infer
	## when retail considered a collision probable. Keep an executable-boundary
	## receipt rather than emitting a late or guessed warning.
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields: Variant = contract.get("fields", {})
	if typeof(fields) != TYPE_DICTIONARY or not (fields as Dictionary).is_empty():
		return
	row["notify_imminent_crushing"] = {
		"enabled": true,
		"execution": "deferred-engine-probability-scan",
		"unsupported_semantics": ["scan-cadence", "prediction-range", "target-evasion-response"],
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func _attach_flammable_update_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("flammable_update"):
		return
	var raw_fields: Variant = contract.get("fields", {})
	if typeof(raw_fields) != TYPE_DICTIONARY:
		return
	var fields := raw_fields as Dictionary
	var allowed := ["AflameDuration", "AflameDamageDelay", "FlameDamageExpiration", "BurnedDelay", "AflameDamageAmount", "FlameDamageLimit", "BurnContained", "SetBurnedStatus", "DamageType", "FireFXList", "BurningSoundName"]
	for key_value in fields.keys():
		if String(key_value) not in allowed:
			return
	for bool_key in ["BurnContained", "SetBurnedStatus"]:
		if fields.has(bool_key) and typeof(_sim._module_contract_value(fields, bool_key, null)) != TYPE_BOOL:
			return
	if fields.has("FireFXList") and typeof(fields.get("FireFXList")) != TYPE_ARRAY:
		return
	var policy := {
		"aflame": false,
		"flame_damage_accumulated": 0.0,
		"flame_damage_expire_tick": -1,
		"aflame_until_tick": -1,
		"next_damage_tick": -1,
		"burned_tick": -1,
		"burn_contained": bool(_sim._module_contract_value(fields, "BurnContained", false)),
		"set_burned_status": bool(_sim._module_contract_value(fields, "SetBurnedStatus", false)),
		"damage_type": String(_sim._module_contract_value(fields, "DamageType", "")),
		"fire_fx": (fields.get("FireFXList", []) as Array).duplicate(true),
		"burning_sound_id": String(_sim._module_contract_value(fields, "BurningSoundName", "")),
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
		"unsupported_semantics": [],
	}
	var numeric_fields := {
		"AflameDuration": "aflame_duration_ticks",
		"AflameDamageDelay": "damage_delay_ticks",
		"FlameDamageExpiration": "flame_expiration_ticks",
		"BurnedDelay": "burned_delay_ticks",
		"AflameDamageAmount": "damage_amount",
		"FlameDamageLimit": "flame_damage_limit",
	}
	for field_name_value in numeric_fields:
		var field_name := String(field_name_value)
		if not fields.has(field_name):
			continue
		var value: Variant = _sim._module_contract_value(fields, field_name, null)
		if typeof(value) not in [TYPE_INT, TYPE_FLOAT]:
			(policy["unsupported_semantics"] as Array).append("unresolved-expression:" + field_name)
			continue
		if float(value) < 0.0:
			return
		var target_key := String(numeric_fields[field_name])
		if field_name in ["AflameDuration", "AflameDamageDelay", "FlameDamageExpiration", "BurnedDelay"]:
			policy[target_key] = _sim._ship_contract_delay_ticks(float(value))
		else:
			policy[target_key] = float(value)
	row["flammable_update"] = policy


func record_flame_damage(object_id: int, amount: float) -> Dictionary:
	var _sim = sim
	var table = _sim.structures if _sim.structures.has(object_id) else _sim.entities
	if not table.has(object_id):
		return {"ok": false, "reason": "object-missing"}
	var row := table[object_id] as Dictionary
	if not row.has("flammable_update"):
		if _sim.structures.has(object_id): _sim._attach_structure_module_contracts(row)
		else: _sim._attach_module_contracts(row)
	var policy := row.get("flammable_update", {}) as Dictionary
	if policy.is_empty():
		return {"ok": false, "reason": "typed-flammable-contract-missing"}
	for required in ["aflame_duration_ticks", "damage_delay_ticks", "flame_expiration_ticks", "damage_amount", "flame_damage_limit"]:
		if not policy.has(required):
			return {"ok": false, "reason": "unresolved-flammable-field", "field": required}
	if _sim.entities.has(object_id) and _sim.entity_container.has(object_id) and not bool(policy.get("burn_contained", false)):
		return {"ok": false, "reason": "contained-burning-disabled"}
	if _sim.tick_index > int(policy.get("flame_damage_expire_tick", -1)):
		policy["flame_damage_accumulated"] = 0.0
	policy["flame_damage_accumulated"] = float(policy.get("flame_damage_accumulated", 0.0)) + maxf(0.0, amount)
	policy["flame_damage_expire_tick"] = _sim.tick_index + maxi(0, int(policy.get("flame_expiration_ticks", 0)))
	var ignited := false
	if not bool(policy.get("aflame", false)) and float(policy.get("flame_damage_accumulated", 0.0)) + 0.0001 >= float(policy.get("flame_damage_limit", INF)):
		ignited = true
		policy["aflame"] = true
		policy["aflame_until_tick"] = _sim.tick_index + maxi(1, int(policy.get("aflame_duration_ticks", 0)))
		policy["next_damage_tick"] = _sim.tick_index + maxi(1, int(policy.get("damage_delay_ticks", 0)))
		policy["burned_tick"] = _sim.tick_index + maxi(0, int(policy.get("burned_delay_ticks", 0))) if bool(policy.get("set_burned_status", false)) else -1
		_set_row_object_status(row, "AFLAME", true)
		if row.has("fire_spread_update"):
			set_fire_spread_active(object_id, true)
		_sim._emit_event("module.flammable_ignited", object_id, 0, {"aflame_until_tick": policy["aflame_until_tick"], "fire_fx": policy.get("fire_fx", []), "burning_sound_id": policy.get("burning_sound_id", "")})
	row["flammable_update"] = policy
	return {"ok": true, "reason": "", "ignited": ignited, "aflame": bool(policy.get("aflame", false)), "accumulated": float(policy.get("flame_damage_accumulated", 0.0))}


func _step_flammable_updates() -> void:
	var _sim = sim
	var ids: Array[Dictionary] = []
	for id in _sim.entity_ids(): ids.append({"id": id, "kind": "entity"})
	for id in _sim.structure_ids(): ids.append({"id": id, "kind": "structure"})
	for value in ids:
		var item := value as Dictionary
		var object_id := int(item.get("id", 0))
		var is_structure := String(item.get("kind", "")) == "structure"
		var table = _sim.structures if is_structure else _sim.entities
		if not table.has(object_id): continue
		var row := table[object_id] as Dictionary
		var policy := row.get("flammable_update", {}) as Dictionary
		if policy.is_empty() or not bool(policy.get("aflame", false)): continue
		if bool(policy.get("set_burned_status", false)) and int(policy.get("burned_tick", -1)) >= 0 and _sim.tick_index >= int(policy.get("burned_tick", -1)):
			_set_row_object_status(row, "BURNED", true)
			policy["burned_tick"] = -1
		if _sim.tick_index >= int(policy.get("aflame_until_tick", -1)):
			policy["aflame"] = false
			policy["next_damage_tick"] = -1
			_set_row_object_status(row, "AFLAME", false)
			if row.has("fire_spread_update"): set_fire_spread_active(object_id, false)
			row["flammable_update"] = policy
			_sim._emit_event("module.flammable_extinguished", object_id, 0, {})
			continue
		if _sim.tick_index < int(policy.get("next_damage_tick", -1)): continue
		policy["next_damage_tick"] = _sim.tick_index + maxi(1, int(policy.get("damage_delay_ticks", 1)))
		row["flammable_update"] = policy
		row["flammable_internal_damage"] = true
		var damage := maxi(0, roundi(float(policy.get("damage_amount", 0.0))))
		var damage_type := String(policy.get("damage_type", "FLAME"))
		if is_structure: _sim._apply_structure_damage(-1, object_id, damage, damage_type)
		else: _sim._apply_damage(-1, object_id, damage, "battalion", "BURNED", damage_type)
		row.erase("flammable_internal_damage")
		_sim._emit_event("module.flammable_damage", object_id, object_id, {"amount": damage, "damage_type": damage_type})


# De-staticed on extraction (instance sim access).
func _set_row_object_status(row: Dictionary, status: String, enabled: bool) -> void:
	var statuses := row.get("object_status", {}) as Dictionary
	if enabled: statuses[status] = true
	else: statuses.erase(status)
	if statuses.is_empty(): row.erase("object_status")
	else: row["object_status"] = statuses


func _attach_dynamic_portal_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("dynamic_portal"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	for required in ["ObjectFilter", "BonePrefix", "NumberOfBones", "WayPoint", "Link"]:
		if not fields.has(required): return
	var filter_value: Variant = _sim._module_contract_value(fields, "ObjectFilter", [])
	var prefix_value: Variant = _sim._module_contract_value(fields, "BonePrefix", "")
	var bone_count_value: Variant = _sim._module_contract_value(fields, "NumberOfBones", 0)
	var waypoint_value: Variant = fields.get("WayPoint")
	var link_value: Variant = fields.get("Link")
	if typeof(filter_value) != TYPE_ARRAY or (filter_value as Array).is_empty() or String(prefix_value) == "" or typeof(bone_count_value) != TYPE_INT or int(bone_count_value) <= 0 or typeof(waypoint_value) != TYPE_ARRAY or typeof(link_value) != TYPE_ARRAY:
		return
	var waypoints := (waypoint_value as Array).duplicate(true)
	var links := (link_value as Array).duplicate(true)
	if waypoints.is_empty() or links.is_empty(): return
	for waypoint_value_row in waypoints:
		if typeof(waypoint_value_row) != TYPE_DICTIONARY: return
		var waypoint := waypoint_value_row as Dictionary
		if typeof(waypoint.get("index")) != TYPE_INT or int(waypoint.get("index")) < 0 or String(waypoint.get("type", "")) == "": return
	for link_value_row in links:
		if typeof(link_value_row) != TYPE_DICTIONARY: return
		var link := link_value_row as Dictionary
		if typeof(link.get("from")) != TYPE_INT or typeof(link.get("to")) != TYPE_INT or typeof(link.get("via", [])) != TYPE_ARRAY: return
		var route_indices: Array = [int(link.get("from"))]
		route_indices.append_array(link.get("via", []) as Array)
		route_indices.append(int(link.get("to")))
		for route_index_value in route_indices:
			if typeof(route_index_value) != TYPE_INT or int(route_index_value) < 0 or int(route_index_value) >= waypoints.size(): return
	var delay_field: Variant = fields.get("ActivationDelaySeconds", {})
	var delay_value: Variant = 0.0
	if typeof(delay_field) == TYPE_DICTIONARY and not (delay_field as Dictionary).is_empty():
		if (delay_field as Dictionary).has("milliseconds"): delay_value = (delay_field as Dictionary).get("milliseconds")
		elif (delay_field as Dictionary).has("value"): delay_value = (delay_field as Dictionary).get("value")
		else: delay_value = null
	var delay_ticks := 0
	var unsupported: Array[String] = ["model-bone-world-transforms", "member-climb-locomotion"]
	if typeof(delay_value) in [TYPE_INT, TYPE_FLOAT] and float(delay_value) >= 0.0:
		delay_ticks = _sim._ship_contract_delay_ticks(float(delay_value))
	elif fields.has("ActivationDelaySeconds"):
		unsupported.append("unresolved-expression:ActivationDelaySeconds")
	var generated = bool(_sim._module_contract_value(fields, "GenerateNow", false))
	var triggered_by = String(_sim._module_contract_value(fields, "TriggeredBy", ""))
	var custom_animation := fields.get("CustomAnimAndDuration", {}) as Dictionary
	if not custom_animation.is_empty(): unsupported.append("presentation-animation:" + String(custom_animation.get("animState", "")))
	row["dynamic_portal"] = {
		"active": generated and triggered_by == "" and not unsupported.has("unresolved-expression:ActivationDelaySeconds") and delay_ticks == 0,
		"activation_ready_tick": _sim.tick_index + delay_ticks if generated and triggered_by == "" and delay_ticks > 0 else -1,
		"activation_delay_ticks": delay_ticks,
		"generate_now": generated,
		"triggered_by": triggered_by,
		"conflicts_with": Array(_sim._module_contract_value(fields, "ConflictsWith", [])).duplicate(),
		"allow_enemies": bool(_sim._module_contract_value(fields, "AllowEnemies", false)),
		"object_filter": (filter_value as Array).duplicate(),
		"bone_prefix": String(prefix_value),
		"number_of_bones": int(bone_count_value),
		"waypoints": waypoints,
		"links": links,
		"top_attack_position_source": _sim._module_contract_value(fields, "TopAttackPos", {}),
		"top_attack_radius_source": _sim._module_contract_value(fields, "TopAttackRadius", null),
		"custom_animation": custom_animation.duplicate(true),
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func _step_dynamic_portals() -> void:
	var _sim = sim
	for structure_id in _sim.structure_ids():
		var row := _sim.structures[structure_id] as Dictionary
		var portal := row.get("dynamic_portal", {}) as Dictionary
		if portal.is_empty(): continue
		var conflicts := false
		for upgrade_value in portal.get("conflicts_with", []) as Array:
			if _sim._structure_has_completed_upgrade(row, String(upgrade_value)):
				conflicts = true
				break
		if conflicts:
			portal["active"] = false
			portal["activation_ready_tick"] = -1
			row["dynamic_portal"] = portal
			continue
		var trigger := String(portal.get("triggered_by", ""))
		var eligible = bool(portal.get("generate_now", false)) if trigger == "" else _sim._structure_has_completed_upgrade(row, trigger)
		if not eligible or (portal.get("unsupported_semantics", []) as Array).has("unresolved-expression:ActivationDelaySeconds"):
			portal["active"] = false
			portal["activation_ready_tick"] = -1
		elif not bool(portal.get("active", false)):
			if int(portal.get("activation_ready_tick", -1)) < 0:
				portal["activation_ready_tick"] = _sim.tick_index + int(portal.get("activation_delay_ticks", 0))
			if _sim.tick_index >= int(portal.get("activation_ready_tick", -1)):
				portal["active"] = true
		row["dynamic_portal"] = portal


func request_dynamic_portal_route(portal_id: int, entity_id: int, from_index: int, to_index: int) -> Dictionary:
	var _sim = sim
	if not _sim.structures.has(portal_id) or not _sim.entities.has(entity_id): return {"ok": false, "reason": "portal-or-entity-missing"}
	var portal_row := _sim.structures[portal_id] as Dictionary
	if not portal_row.has("dynamic_portal"): _sim._attach_structure_module_contracts(portal_row)
	_step_dynamic_portals()
	var portal := portal_row.get("dynamic_portal", {}) as Dictionary
	if portal.is_empty(): return {"ok": false, "reason": "typed-dynamic-portal-contract-missing"}
	if not bool(portal.get("active", false)): return {"ok": false, "reason": "portal-inactive"}
	var entity := _sim.entities[entity_id] as Dictionary
	if not bool(portal.get("allow_enemies", false)) and _sim._is_hostile(int(portal_row.get("team", -1)), int(entity.get("team", -2))): return {"ok": false, "reason": "enemy-refused"}
	if not _sim._transport_filter_accepts(entity, portal.get("object_filter", []) as Array): return {"ok": false, "reason": "object-filter-refused"}
	var selected: Dictionary = {}
	for link_value in portal.get("links", []) as Array:
		var link := link_value as Dictionary
		if int(link.get("from", -1)) == from_index and int(link.get("to", -1)) == to_index:
			selected = link
			break
	if selected.is_empty(): return {"ok": false, "reason": "portal-link-missing"}
	var route_indices: Array = [from_index]
	route_indices.append_array(selected.get("via", []) as Array)
	route_indices.append(to_index)
	var route: Array[Dictionary] = []
	var waypoints := portal.get("waypoints", []) as Array
	for route_index_value in route_indices:
		var ordinal := int(route_index_value)
		var waypoint := waypoints[ordinal] as Dictionary
		route.append({"ordinal": ordinal, "bone": "%s%d" % [String(portal.get("bone_prefix", "")), int(waypoint.get("index", 0)) + 1], "type": String(waypoint.get("type", ""))})
	entity["dynamic_portal_route_receipt"] = {"portal_id": portal_id, "route": route.duplicate(true), "status": "resolved-awaiting-model-bone-world-transforms"}
	_sim._emit_event("portal.route_resolved", portal_id, entity_id, {"route": route})
	return {"ok": true, "reason": "", "route": route, "movement_status": "deferred-model-bone-world-transforms"}


func _attach_foundation_ai_contract(row: Dictionary, contract: Dictionary) -> void:
	## FoundationAIUpdate is an authored foundation selector. The typed contract
	## contains no AI placement heuristic, so an empty marker stays explicit and
	## never guesses the engine's default variation.
	if String(contract.get("extraction", "")) != "typed" or row.has("foundation_ai_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var policy := {
		"build_variation": 0,
		"has_authored_build_variation": false,
		"unsupported_semantics": [],
		"source_ini": String(contract.get("sourceIni", "")),
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}
	if fields.has("BuildVariation"):
		var field_value: Variant = (fields.get("BuildVariation", {}) as Dictionary).get("value")
		if typeof(field_value) != TYPE_INT or int(field_value) < 1:
			return
		policy["build_variation"] = int(field_value)
		policy["has_authored_build_variation"] = true
		row["build_variation"] = int(field_value)
		(policy["unsupported_semantics"] as Array).append("foundation-construction-dispatch-unwired")
	else:
		(policy["unsupported_semantics"] as Array).append("engine-default-build-variation-unresolved")
	row["foundation_ai_update"] = policy


func foundation_build_variation(structure_id: int) -> Dictionary:
	var _sim = sim
	if not _sim.structures.has(structure_id):
		return {"ok": false, "reason": "structure-missing"}
	var row := _sim.structures[structure_id] as Dictionary
	if not row.has("foundation_ai_update"):
		_sim._attach_structure_module_contracts(row)
	var policy := row.get("foundation_ai_update", {}) as Dictionary
	if policy.is_empty():
		return {"ok": false, "reason": "typed-foundation-ai-contract-missing"}
	if not bool(policy.get("has_authored_build_variation", false)):
		return {"ok": false, "reason": "engine-default-build-variation-unresolved"}
	return {"ok": true, "reason": "", "value": int(policy.get("build_variation", 0))}


func _attach_dual_weapon_contract(row: Dictionary, contract: Dictionary) -> void:
	## DualWeaponBehavior authors only the distance boundary. Weapon identities
	## come from the object's already-compiled WeaponSet profiles; absence of a
	## close profile is therefore a hard refusal, never a synthesized weapon.
	if String(contract.get("extraction", "")) != "typed" or row.has("dual_weapon_behavior"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	if fields.size() != 1 or not fields.has("SwitchWeaponOnCloseRangeDistance"):
		return
	var distance_field: Variant = fields.get("SwitchWeaponOnCloseRangeDistance")
	if typeof(distance_field) != TYPE_DICTIONARY:
		return
	var distance := distance_field as Dictionary
	var expression := String(distance.get("expression", "")).strip_edges()
	if expression == "":
		return
	var policy := {
		"switch_distance_source": 0.0,
		"switch_distance": 0.0,
		"close_weapon_mode": String(row.get("close_weapon_mode", "")),
		"executable": false,
		"unsupported_semantics": [],
		"source_ini": String(contract.get("sourceIni", "")),
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
		"field_source_ini": String(distance.get("sourceIni", "")),
		"field_line": int(distance.get("line", 0)),
	}
	if not distance.has("value"):
		(policy["unsupported_semantics"] as Array).append("unresolved-switch-distance-define:%s" % expression)
		# Disable an older independently projected threshold: this typed consumer
		# cannot prove the define's numeric value and must not execute it.
		row["close_weapon_switch_distance"] = 0.0
		row["close_weapon_switch_distance_source"] = 0.0
		row["unsupported_close_weapon"] = false
		row["dual_weapon_behavior"] = policy
		return
	var numeric: Variant = distance.get("value")
	if typeof(numeric) not in [TYPE_INT, TYPE_FLOAT] or not is_finite(float(numeric)) or float(numeric) < 0.0:
		return
	var source_distance := float(numeric)
	var local_distance = sim._retail_source_to_sim_offset(Vector2(source_distance, 0.0)).x
	policy["switch_distance_source"] = source_distance
	policy["switch_distance"] = local_distance
	row["close_weapon_switch_distance_source"] = source_distance
	row["close_weapon_switch_distance"] = local_distance
	var close_mode := String(policy.get("close_weapon_mode", ""))
	if source_distance > 0.0 and (close_mode == "" or not (row.get("weapon_modes", {}) as Dictionary).has(close_mode)):
		(policy["unsupported_semantics"] as Array).append("close-weapon-profile-unresolved")
		row["unsupported_close_weapon"] = true
	else:
		policy["executable"] = true
		row["unsupported_close_weapon"] = false
	row["dual_weapon_behavior"] = policy


func _attach_refund_die_contract(row: Dictionary, contract: Dictionary) -> void:
	var executable := bool(contract.get("executable", false))
	if not executable:
		executable = String(contract.get("runtimeStatus", contract.get("runtime_status", ""))) == "executable"
	if not executable:
		return
	if String(contract.get("extraction", "")) != "typed":
		return
	_cache_refund_die_build_cost(row)
	var fields := contract.get("fields", {}) as Dictionary
	var allowed := {"RefundPercent": true, "UpgradeRequired": true, "BuildingRequired": true}
	for key_value in fields.keys():
		if not allowed.has(String(key_value)): return
	if not fields.has("RefundPercent"):
		return
	var refund_field: Variant = fields.get("RefundPercent")
	if typeof(refund_field) != TYPE_DICTIONARY:
		return
	var refund := refund_field as Dictionary
	var percent_value: Variant = refund.get("percent")
	var fraction_value: Variant = refund.get("fraction")
	if typeof(percent_value) not in [TYPE_INT, TYPE_FLOAT] or typeof(fraction_value) not in [TYPE_INT, TYPE_FLOAT]:
		return
	var percent := float(percent_value)
	var fraction := float(fraction_value)
	if not is_finite(percent) or not is_finite(fraction) or percent < 0.0 or percent > 100.0 or not is_equal_approx(fraction, percent / 100.0):
		return
	var upgrade_required := ""
	if fields.has("UpgradeRequired"):
		var upgrade_field: Variant = fields.get("UpgradeRequired")
		if typeof(upgrade_field) != TYPE_DICTIONARY: return
		upgrade_required = String((upgrade_field as Dictionary).get("value", "")).strip_edges()
		if upgrade_required == "": return
	var building_required: Array[String] = []
	if fields.has("BuildingRequired"):
		var building_field: Variant = fields.get("BuildingRequired")
		if typeof(building_field) != TYPE_DICTIONARY or typeof((building_field as Dictionary).get("value")) != TYPE_ARRAY: return
		for token_value in (building_field as Dictionary).get("value", []) as Array:
			var token := String(token_value).strip_edges()
			if token == "": return
			building_required.append(token)
		if building_required.is_empty(): return
	var policies := row.get("refund_die", []) as Array
	policies.append({
		"fraction": fraction,
		"percent": percent,
		"upgrade_required": upgrade_required,
		"building_required": building_required,
		"death_scope": "object-death-edge",
		"unsupported_semantics": [],
		"source_ini": String(contract.get("sourceIni", "")),
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	})
	row["refund_die"] = policies


func _cache_refund_die_build_cost(row: Dictionary) -> void:
	## Object::getBuildCost is cached per object in retail. Resolve it once when
	## the executable module attaches; later rule/owner changes cannot rewrite it.
	var _sim = sim
	if row.has("cached_build_cost"):
		return
	var direct: Variant = row.get("build_cost")
	if typeof(direct) in [TYPE_INT, TYPE_FLOAT] and float(direct) >= 0.0:
		row["cached_build_cost"] = float(direct)
		return
	var structure_kind := String(row.get("structure_kind", ""))
	if structure_kind != "":
		var team := int(row.get("team", -1))
		var build_rules = _sim.structure_build_rules_for_team(team)
		var structure_rule := build_rules.get(structure_kind, {}) as Dictionary
		if structure_rule.is_empty():
			structure_rule = _sim._expansion_build_rules.get(structure_kind, {}) as Dictionary
		var structure_cost: Variant = structure_rule.get("cost")
		if typeof(structure_cost) in [TYPE_INT, TYPE_FLOAT] and float(structure_cost) >= 0.0:
			row["cached_build_cost"] = float(structure_cost)
		return
	var unit_type := String(row.get("unit_type", ""))
	var production_rule := _sim._unit_production_rules.get(unit_type, {}) as Dictionary
	if not production_rule.is_empty():
		var unit_cost = _sim._production_rule_value(unit_type, "cost_rule", "default_cost")
		if unit_cost >= 0:
			row["cached_build_cost"] = unit_cost


func _attach_wall_hub_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed": return
	var fields := contract.get("fields", {}) as Dictionary
	for required in ["Options", "MaxBuildoutDistance", "EffectiveMaxBuildoutDistance", "SegmentTemplateName", "HubCapTemplateName", "DefaultSegmentTemplateName"]:
		if not fields.has(required): return
	var option = String(_sim._module_contract_value(fields, "Options", ""))
	if option not in ["OPTION_ONE", "OPTION_TWO", "OPTION_THREE"]: return
	var distance_value: Variant = fields.get("MaxBuildoutDistance")
	var segment_value: Variant = fields.get("SegmentTemplateName")
	if typeof(distance_value) != TYPE_ARRAY or (distance_value as Array).is_empty() or typeof(segment_value) != TYPE_ARRAY or (segment_value as Array).is_empty(): return
	var defines := _sim._rules.get("wall_hub_distance_defines", {}) as Dictionary
	var distances: Array[Dictionary] = []
	for value in distance_value as Array:
		if typeof(value) != TYPE_DICTIONARY: return
		var resolved := _wall_hub_distance(value as Dictionary, defines)
		if bool(resolved.get("malformed", false)): return
		distances.append(resolved)
	var effective_field: Variant = fields.get("EffectiveMaxBuildoutDistance")
	if typeof(effective_field) != TYPE_DICTIONARY: return
	var effective := _wall_hub_distance(effective_field as Dictionary, defines)
	if bool(effective.get("malformed", false)): return
	var segments: Array[String] = []
	for value in segment_value as Array:
		if typeof(value) != TYPE_DICTIONARY: return
		var template := String((value as Dictionary).get("value", "")).strip_edges()
		if template == "": return
		segments.append(template)
	var builder_source = float(_sim._module_contract_value(fields, "BuilderRadius", 0.0))
	if not is_finite(builder_source) or builder_source < 0.0: return
	var unsupported: Array[String] = ["segment-spacing-and-build-economy-unresolved"]
	if fields.has("StaggeredBuildFactor"): unsupported.append("staggered-build-factor-engine-define:%s" % String(_sim._module_contract_value(fields, "StaggeredBuildFactor", "")))
	if not bool(effective.get("resolved", false)): unsupported.append("unresolved-max-buildout-distance:%s" % String(effective.get("define", "")))
	var policies := row.get("wall_hub_behaviors", []) as Array
	policies.append({"option":option,"max_buildout_distances":distances,"effective_distance_source":float(effective.get("source",0.0)),"effective_distance":float(effective.get("local",0.0)),"executable":bool(effective.get("resolved",false)),"runtime_scope":"plan-only","segment_templates":segments,"hub_cap_template":String(_sim._module_contract_value(fields,"HubCapTemplateName","")),"default_segment_template":String(_sim._module_contract_value(fields,"DefaultSegmentTemplateName","")),"cliff_cap_template":String(_sim._module_contract_value(fields,"CliffCapTemplateName","")),"builder_radius_source":builder_source,"builder_radius":_sim._retail_source_to_sim_offset(Vector2(builder_source,0.0)).x,"unsupported_semantics":unsupported,"source_ini":String(contract.get("sourceIni","")),"tag":String(contract.get("tag","")),"line":int(contract.get("line",0))})
	row["wall_hub_behaviors"] = policies


func _wall_hub_distance(field: Dictionary, defines: Dictionary) -> Dictionary:
	var _sim = sim
	var output := {"source_ini":String(field.get("sourceIni","")),"line":int(field.get("line",0)),"resolved":false,"source":0.0,"local":0.0,"define":"","malformed":false}
	var numeric: Variant = field.get("value")
	if typeof(numeric) in [TYPE_INT, TYPE_FLOAT]:
		if not is_finite(float(numeric)) or float(numeric) < 0.0: output["malformed"] = true; return output
		output["resolved"] = true; output["source"] = float(numeric); output["local"] = _sim._retail_source_to_sim_offset(Vector2(float(numeric),0.0)).x; return output
	var define := String(field.get("define", field.get("expression", ""))).strip_edges(); output["define"] = define
	if define == "": output["malformed"] = true; return output
	var define_value: Variant = defines.get(define)
	if typeof(define_value) in [TYPE_INT, TYPE_FLOAT] and is_finite(float(define_value)) and float(define_value) >= 0.0:
		output["resolved"] = true; output["source"] = float(define_value); output["local"] = _sim._retail_source_to_sim_offset(Vector2(float(define_value),0.0)).x
	return output


func request_wall_hub_plan(structure_id: int, option: String, endpoint: Vector2) -> Dictionary:
	var _sim = sim
	if not _sim.structures.has(structure_id): return {"ok":false,"reason":"structure-missing"}
	var row := _sim.structures[structure_id] as Dictionary
	if not row.has("wall_hub_behaviors"): _sim._attach_structure_module_contracts(row)
	if int(row.get("health",0)) <= 0: return {"ok":false,"reason":"wall-hub-destroyed"}
	var selected: Dictionary = {}
	for value in row.get("wall_hub_behaviors", []) as Array:
		if String((value as Dictionary).get("option","")) == option: selected = value as Dictionary; break
	if selected.is_empty(): return {"ok":false,"reason":"wall-hub-option-missing"}
	if not bool(selected.get("executable",false)): return {"ok":false,"reason":"max-buildout-distance-unresolved"}
	var origin := Vector2(row.get("position",Vector2.ZERO)); var distance := origin.distance_to(endpoint)
	if distance > float(selected.get("effective_distance",0.0)) + 0.000001: return {"ok":false,"reason":"max-buildout-distance-exceeded","maximum":selected.get("effective_distance")}
	var plan := {"ok":true,"reason":"","option":option,"origin":origin,"endpoint":endpoint,"distance":distance,"maximum_distance":float(selected.get("effective_distance",0.0)),"segment_templates":(selected.get("segment_templates",[]) as Array).duplicate(),"hub_cap_template":String(selected.get("hub_cap_template","")),"default_segment_template":String(selected.get("default_segment_template","")),"cliff_cap_template":String(selected.get("cliff_cap_template","")),"materialization_status":"deferred-segment-spacing-and-build-economy"}
	var receipts := row.get("wall_hub_plan_receipts", []) as Array; receipts.append(plan.duplicate(true)); row["wall_hub_plan_receipts"] = receipts
	_sim._emit_event("wall_hub.plan_resolved",structure_id,0,{"option":option,"distance":distance,"segment_templates":plan["segment_templates"]})
	return plan


func _attach_attach_update_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("attach_update"): return
	var fields := contract.get("fields", {}) as Dictionary
	var filter_value: Variant = _sim._module_contract_value(fields, "ObjectFilter", null)
	if typeof(filter_value) != TYPE_ARRAY or (filter_value as Array).is_empty(): return
	var object_filter: Array[String] = []
	for value in filter_value as Array:
		var token := String(value).strip_edges(); if token == "": return
		object_filter.append(token)
	var scan_authored := fields.has("ScanRange")
	var scan_source = float(_sim._module_contract_value(fields, "ScanRange", 0.0))
	if not is_finite(scan_source) or scan_source < 0.0: return
	var parent_status: Array[String] = []
	for value in _sim._typed_contract_tokens(fields, "ParentStatus"): parent_status.append(String(value))
	var unsupported: Array[String] = []
	if bool(_sim._module_contract_value(fields,"AnchorToTopOfGeometry",false)): unsupported.append("model-geometry-top-transform-unresolved")
	for key in ["ParentOwnerAttachmentEvaEvent","ParentEnemyAttachmentEvaEvent","ParentOwnerDiedEvaEvent"]:
		if fields.has(key): unsupported.append("presentation-eva-event:%s" % key)
	row["attach_update"] = {"object_filter":object_filter,"scan_range_authored":scan_authored,"scan_range_source":scan_source,"scan_range":_sim._retail_source_to_sim_offset(Vector2(scan_source,0.0)).x,"parent_status":parent_status,"always_teleport":bool(_sim._module_contract_value(fields,"AlwaysTeleport",false)),"anchor_to_top":bool(_sim._module_contract_value(fields,"AnchorToTopOfGeometry",false)),"owner_attach_eva":String(_sim._module_contract_value(fields,"ParentOwnerAttachmentEvaEvent","")),"enemy_attach_eva":String(_sim._module_contract_value(fields,"ParentEnemyAttachmentEvaEvent","")),"parent_died_eva":String(_sim._module_contract_value(fields,"ParentOwnerDiedEvaEvent","")),"unsupported_semantics":unsupported,"source_ini":String(contract.get("sourceIni","")),"tag":String(contract.get("tag","")),"line":int(contract.get("line",0))}
	row["attach_parent_id"] = 0; row["attach_parent_kind"] = ""


func request_attach_update(child_id: int, parent_id: int, parent_kind: String = "entity") -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(child_id): return {"ok":false,"reason":"attach-child-missing"}
	var child := _sim.entities[child_id] as Dictionary
	if not child.has("attach_update"): _sim._attach_module_contracts(child)
	var policy := child.get("attach_update", {}) as Dictionary
	if policy.is_empty(): return {"ok":false,"reason":"typed-attach-update-missing"}
	var table = _sim.structures if parent_kind == "structure" else _sim.entities
	if not table.has(parent_id) or (parent_kind == "entity" and parent_id == child_id): return {"ok":false,"reason":"attach-target-missing"}
	var parent := table[parent_id] as Dictionary
	if int(parent.get("health",0)) <= 0: return {"ok":false,"reason":"attach-target-dead"}
	if not _sim._transport_filter_accepts(_attach_filter_probe(parent,parent_kind), policy.get("object_filter",[]) as Array): return {"ok":false,"reason":"attach-target-filter-refused"}
	if bool(policy.get("scan_range_authored",false)):
		var distance := Vector2(child.get("position",Vector2.ZERO)).distance_to(Vector2(parent.get("position",Vector2.ZERO)))
		if distance > float(policy.get("scan_range",0.0)) + 0.000001: return {"ok":false,"reason":"attach-target-out-of-range"}
	_detach_attach_update(child,"replaced")
	child["attach_parent_id"] = parent_id; child["attach_parent_kind"] = parent_kind
	_set_attach_parent_status(parent,child_id,policy.get("parent_status",[]) as Array,true)
	if bool(policy.get("always_teleport",false)) and not bool(policy.get("anchor_to_top",false)): child["position"] = Vector2(parent.get("position",Vector2.ZERO))
	var eva := String(policy.get("owner_attach_eva","")) if int(child.get("team",-1)) == int(parent.get("team",-2)) else String(policy.get("enemy_attach_eva",""))
	_sim._emit_event("module.attach_update_attached",child_id,parent_id,{"parent_kind":parent_kind,"eva_event_id":eva,"presentation_status":"receipt-only"})
	return {"ok":true,"reason":"","parent_id":parent_id,"parent_kind":parent_kind}


func _step_attach_updates() -> void:
	var _sim = sim
	for child_id in _sim.entity_ids():
		var child := _sim.entities[child_id] as Dictionary; var policy := child.get("attach_update", {}) as Dictionary
		if policy.is_empty(): continue
		if int(child.get("health",0)) <= 0:
			_detach_attach_update(child,"child-died")
			continue
		var parent_id := int(child.get("attach_parent_id",0)); var parent_kind := String(child.get("attach_parent_kind",""))
		if parent_id != 0:
			var table = _sim.structures if parent_kind == "structure" else _sim.entities
			if not table.has(parent_id) or int((table[parent_id] as Dictionary).get("health",0)) <= 0:
				_detach_attach_update(child,"parent-died"); continue
			var parent := table[parent_id] as Dictionary
			if bool(policy.get("always_teleport",false)) and not bool(policy.get("anchor_to_top",false)): child["position"] = Vector2(parent.get("position",Vector2.ZERO))
			continue
		if not bool(policy.get("scan_range_authored",false)): continue
		var candidates: Array[Dictionary] = []; var origin := Vector2(child.get("position",Vector2.ZERO)); var radius = float(policy.get("scan_range",0.0))
		for target_id in _sim.entity_ids():
			if target_id == child_id: continue
			var target := _sim.entities[target_id] as Dictionary; var distance := origin.distance_to(Vector2(target.get("position",Vector2.ZERO)))
			if int(target.get("health",0)) > 0 and distance <= radius and _sim._transport_filter_accepts(_attach_filter_probe(target,"entity"),policy.get("object_filter",[]) as Array): candidates.append({"id":target_id,"kind":"entity","distance":distance})
		for target_id in _sim.structure_ids():
			var target := _sim.structures[target_id] as Dictionary; var distance := origin.distance_to(Vector2(target.get("position",Vector2.ZERO)))
			if int(target.get("health",0)) > 0 and distance <= radius and _sim._transport_filter_accepts(_attach_filter_probe(target,"structure"),policy.get("object_filter",[]) as Array): candidates.append({"id":target_id,"kind":"structure","distance":distance})
		candidates.sort_custom(func(a:Dictionary,b:Dictionary)->bool:
			if not is_equal_approx(float(a["distance"]),float(b["distance"])): return float(a["distance"]) < float(b["distance"])
			if String(a["kind"]) != String(b["kind"]): return String(a["kind"]) < String(b["kind"])
			return int(a["id"]) < int(b["id"]))
		if not candidates.is_empty(): request_attach_update(child_id,int(candidates[0]["id"]),String(candidates[0]["kind"]))


func _detach_attach_update(child: Dictionary, reason: String) -> void:
	var _sim = sim
	var parent_id := int(child.get("attach_parent_id",0)); if parent_id == 0: return
	var parent_kind := String(child.get("attach_parent_kind","entity")); var table = _sim.structures if parent_kind == "structure" else _sim.entities; var policy := child.get("attach_update",{}) as Dictionary
	if table.has(parent_id): _set_attach_parent_status(table[parent_id] as Dictionary,int(child.get("id",0)),policy.get("parent_status",[]) as Array,false)
	child["attach_parent_id"] = 0; child["attach_parent_kind"] = ""
	if reason == "parent-died": _sim._emit_event("module.attach_update_parent_died",int(child.get("id",0)),parent_id,{"eva_event_id":String(policy.get("parent_died_eva","")),"presentation_status":"receipt-only"})


# De-staticed on extraction (instance sim access).
func _attach_filter_probe(row: Dictionary, kind: String) -> Dictionary:
	var kinds := (row.get("kind_of",[]) as Array).duplicate(); kinds.append("STRUCTURE" if kind == "structure" else "UNIT")
	for value in [row.get("source_object_id",""),row.get("object_id",""),row.get("structure_kind","")]: if String(value) != "": kinds.append(String(value))
	return {"category":String(row.get("category","structure" if kind == "structure" else "")),"kind_of":kinds}


# De-staticed on extraction (instance sim access).
func _set_attach_parent_status(parent: Dictionary, child_id: int, statuses: Array, enabled: bool) -> void:
	var sources = parent.get("attach_status_sources",{}) as Dictionary
	for value in statuses:
		var status := String(value).trim_prefix("+"); if status == "" or status.begins_with("-") or status in ["ANY","NONE"]: continue
		var ids := sources.get(status,[]) as Array
		if enabled and not ids.has(child_id): ids.append(child_id)
		elif not enabled: ids.erase(child_id)
		if ids.is_empty(): sources.erase(status); _set_row_object_status(parent,status,false)
		else: sources[status] = ids; _set_row_object_status(parent,status,true)
	if sources.is_empty(): parent.erase("attach_status_sources")
	else: parent["attach_status_sources"] = sources


func _attach_monitor_condition_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("monitor_condition_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var model_route := _monitor_condition_route(fields.get("ModelConditionRoute"))
	var weapon_route := _monitor_condition_route(fields.get("WeaponSetRoute"))
	if model_route.is_empty() and weapon_route.is_empty():
		return
	# A present but malformed pair fails the whole typed row closed. Importer
	# validation normally prevents this; the runtime keeps the same boundary.
	if (fields.has("ModelConditionRoute") and model_route.is_empty()) or (fields.has("WeaponSetRoute") and weapon_route.is_empty()):
		return
	var default_set := String(row.get("command_set_id", row.get("default_command_set_id", ""))).strip_edges()
	var unsupported: Array[String] = ["command-surface-consumer-unwired"]
	if not model_route.is_empty(): unsupported.append("model-condition-producer-unwired")
	if default_set == "": unsupported.append("default-command-set-unresolved")
	var thrall_graph := (sim._rules.get("angmar_thrall_replacement", {}) as Dictionary)
	if (
		String(row.get("unit_type", "")) == String(thrall_graph.get("runtimeSourceUnitType", ""))
		and String(row.get("object_id", "")) == String(thrall_graph.get("runtimeSourceObjectId", ""))
		and String(thrall_graph.get("graphStatus", "")) == "executable"
		and String((thrall_graph.get("monitor", {}) as Dictionary).get("commandSetId", "")) == String(model_route.get("command_set", ""))
	):
		unsupported.erase("command-surface-consumer-unwired")
		unsupported.erase("model-condition-producer-unwired")
	row["monitor_condition_update"] = {
		"default_command_set": default_set,
		"active_command_set": default_set,
		"active_route": "default" if default_set != "" else "unresolved",
		"model_condition_route": model_route,
		"weapon_set_route": weapon_route,
		"transition_count": 0,
		"unsupported_semantics": unsupported,
		"source_ini": String(contract.get("sourceIni", "")),
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


# De-staticed on extraction (instance sim access).
func _monitor_condition_route(value: Variant) -> Dictionary:
	if typeof(value) != TYPE_DICTIONARY:
		return {}
	var route := value as Dictionary
	var flags_field: Variant = route.get("flags")
	var command_field: Variant = route.get("commandSet")
	if typeof(flags_field) != TYPE_DICTIONARY or typeof(command_field) != TYPE_DICTIONARY:
		return {}
	var flags_value: Variant = (flags_field as Dictionary).get("value")
	var command_set := String((command_field as Dictionary).get("value", "")).strip_edges()
	if typeof(flags_value) != TYPE_ARRAY or (flags_value as Array).is_empty() or command_set == "":
		return {}
	var flags: Array[String] = []
	for flag_value in flags_value as Array:
		if typeof(flag_value) not in [TYPE_STRING, TYPE_STRING_NAME] or String(flag_value).strip_edges() == "":
			return {}
		flags.append(String(flag_value).to_upper())
	return {"flags": flags, "command_set": command_set}


func _step_monitor_condition_updates() -> void:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		_step_monitor_condition_row(entity_id, _sim.entities[entity_id] as Dictionary)
	for structure_id in _sim.structure_ids():
		_step_monitor_condition_row(structure_id, _sim.structures[structure_id] as Dictionary)


func _step_monitor_condition_row(object_id: int, row: Dictionary) -> void:
	var policy := row.get("monitor_condition_update", {}) as Dictionary
	if policy.is_empty() or (policy.get("unsupported_semantics", []) as Array).has("default-command-set-unresolved"):
		return
	var model_conditions := _upper_token_set(row.get("model_conditions", []))
	# The exact Thrall graph closes the previously deferred producer edge:
	# CanSummonWolfRiders is a timed ModifierList whose MODEL_CONDITION USER_1
	# drives MonitorConditionUpdate.  Keep this scoped to the compiler-joined
	# graph; other modifier consumers remain honestly deferred.
	var thrall_graph := (sim._rules.get("angmar_thrall_replacement", {}) as Dictionary)
	if (
		String(row.get("unit_type", "")) == String(thrall_graph.get("runtimeSourceUnitType", ""))
		and String(row.get("object_id", "")) == String(thrall_graph.get("runtimeSourceObjectId", ""))
		and String(thrall_graph.get("graphStatus", "")) == "executable"
	):
		for timed_value in (row.get("timed_modifiers", {}) as Dictionary).values():
			var timed := timed_value as Dictionary
			if int(timed.get("expires_tick", -1)) <= sim.tick_index:
				continue
			for effect_value in timed.get("modifiers", []) as Array:
				var effect := effect_value as Dictionary
				if String(effect.get("kind", "")).to_upper() != "MODEL_CONDITION":
					continue
				var value: Variant = effect.get("value", effect.get("tokens", []))
				if typeof(value) == TYPE_ARRAY:
					for token in value as Array: model_conditions[String(token).to_upper()] = true
				elif String(value) != "":
					model_conditions[String(value).to_upper()] = true
	var weapon_flags := _upper_token_set(row.get("weapon_set_flags", []))
	var selected_set := String(policy.get("default_command_set", ""))
	var selected_route := "default"
	var model_route := policy.get("model_condition_route", {}) as Dictionary
	var weapon_route := policy.get("weapon_set_route", {}) as Dictionary
	# Retail rows such as Mountain Giant author both: ATTACKING_POSITION is the
	# transient stop-command surface and must override the broader weapon set.
	if not model_route.is_empty() and _monitor_flags_match(model_conditions, model_route.get("flags", []) as Array):
		selected_set = String(model_route.get("command_set", "")); selected_route = "model-condition"
	elif not weapon_route.is_empty() and _monitor_flags_match(weapon_flags, weapon_route.get("flags", []) as Array):
		selected_set = String(weapon_route.get("command_set", "")); selected_route = "weapon-set"
	if selected_set == "" or (selected_set == String(policy.get("active_command_set", "")) and selected_route == String(policy.get("active_route", ""))):
		return
	var prior := String(policy.get("active_command_set", ""))
	row["command_set_id"] = selected_set
	policy["active_command_set"] = selected_set
	policy["active_route"] = selected_route
	policy["transition_count"] = int(policy.get("transition_count", 0)) + 1
	row["monitor_condition_update"] = policy
	sim._emit_event("module.monitor_condition_command_set", object_id, 0, {"from": prior, "to": selected_set, "route": selected_route})


# De-staticed on extraction (instance sim access).
func _upper_token_set(value: Variant) -> Dictionary:
	var output := {}
	if typeof(value) == TYPE_ARRAY:
		for token in value as Array: output[String(token).to_upper()] = true
	return output


# De-staticed on extraction (instance sim access).
func _monitor_flags_match(active: Dictionary, required: Array) -> bool:
	if required.is_empty(): return false
	for flag_value in required:
		if not active.has(String(flag_value).to_upper()): return false
	return true


func set_entity_upgrade_state(entity_id: int, upgrade_id: String, installed: bool) -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	if upgrade_id.strip_edges() == "":
		return {"ok": false, "reason": "upgrade-id-missing"}
	var row := _sim.entities[entity_id] as Dictionary
	var applied := row.get("applied_upgrades", {}) as Dictionary
	var matched_key := ""
	for key_value in applied.keys():
		if String(key_value).to_upper() == upgrade_id.to_upper():
			matched_key = String(key_value)
			break
	if installed:
		if matched_key == "":
			applied[upgrade_id] = _sim.tick_index
	else:
		if matched_key != "":
			applied.erase(matched_key)
	row["applied_upgrades"] = applied
	_sim._reconcile_attribute_modifier_upgrades(row)
	_sim._reconcile_geometry_upgrades(row)
	_sim._emit_event("upgrade.entity_state", 0, entity_id, {"upgrade_id": upgrade_id, "installed": installed})
	return {"ok": true, "reason": "", "installed": installed}


func set_team_upgrade_state(team: int, upgrade_id: String, installed: bool) -> Dictionary:
	var _sim = sim
	if team < 0 or upgrade_id.strip_edges() == "":
		return {"ok": false, "reason": "invalid-team-or-upgrade"}
	var owned := _sim.team_upgrades.get(team, {}) as Dictionary
	var matched_key := ""
	for key_value in owned.keys():
		if String(key_value).to_upper() == upgrade_id.to_upper():
			matched_key = String(key_value)
			break
	if installed:
		if matched_key == "":
			owned[upgrade_id] = true
	else:
		if matched_key != "":
			owned.erase(matched_key)
	_sim.team_upgrades[team] = owned
	_sim._refresh_team_command_set_upgrades(team)
	for entity_id in _sim.entity_ids():
		var row := _sim.entities[entity_id] as Dictionary
		if int(row.get("team", -1)) == team:
			_sim._reconcile_attribute_modifier_upgrades(row)
			_sim._reconcile_geometry_upgrades(row)
	for structure_id in _sim.structure_ids():
		var row := _sim.structures[structure_id] as Dictionary
		if int(row.get("team", -1)) == team:
			_sim._reconcile_attribute_modifier_upgrades(row)
			_sim._reconcile_geometry_upgrades(row)
	_sim._emit_event("upgrade.team_state", 0, 0, {"team": team, "upgrade_id": upgrade_id, "installed": installed})
	return {"ok": true, "reason": "", "installed": installed}


func _step_object_creation_upgrades()->void:
	var _sim = sim
	var owners: Array = []
	for entity_id in _sim.entity_ids():
		owners.append({"id": entity_id, "kind": "entity"})
	for structure_id in _sim.structure_ids():
		owners.append({"id": structure_id, "kind": "structure"})
	for owner_ref_value in owners:
		var owner_ref := owner_ref_value as Dictionary
		var table = _sim.structures if String(owner_ref.get("kind")) == "structure" else _sim.entities
		var owner_id := int(owner_ref.get("id"))
		if not table.has(owner_id):
			continue
		var owner := table[owner_id] as Dictionary
		var rows := owner.get("object_creation_upgrades", []) as Array
		for policy_value in rows:
			var policy := policy_value as Dictionary
			if bool(policy.get("consumed", false)) or int(policy.get("delay_ticks", -1)) < 0:
				continue
			var upgrades := owner.get("completed_upgrades", []) as Array
			var trigger_count := 0
			for trigger in policy.get("triggers", []) as Array:
				if upgrades.has(trigger):
					trigger_count += 1
			var triggered := trigger_count == (policy.get("triggers", []) as Array).size() if bool(policy.get("requires_all", false)) else trigger_count > 0
			for conflict in policy.get("conflicts", []) as Array:
				if upgrades.has(conflict):
					triggered = false
					break
			if not triggered:
				policy["scheduled_tick"] = -1
				continue
			if int(policy.get("scheduled_tick", -1)) < 0:
				policy["scheduled_tick"] = _sim.tick_index + int(policy.get("delay_ticks", 0))
			if _sim.tick_index < int(policy.get("scheduled_tick")):
				continue
			_consume_object_creation_upgrade(owner_id, owner, policy)
			policy["consumed"] = true
		if rows.is_empty():
			owner.erase("object_creation_upgrades")
		else:
			owner["object_creation_upgrades"] = rows


func _consume_object_creation_upgrade(owner_id:int,owner:Dictionary,policy:Dictionary)->void:
	var _sim = sim
	var thing:=String(policy.get("thing_to_spawn",""))
	if thing!="":
		var at =Vector2(owner.get("position",Vector2.ZERO))+_sim._retail_source_to_sim_offset(Vector2(policy.get("offset_source",Vector2.ZERO)));var result =_sim.script_spawn_entity(thing,int(owner.get("team",-1)),at)
		if bool(result.get("ok",false)):var ids:=policy.get("spawned_ids",[]) as Array;ids.append(int(result.get("entity_id",0)));policy["spawned_ids"]=ids
		else:var receipts:=policy.get("unsupported_semantics",[]) as Array;receipts.append("unresolved_creation_object:%s"%thing);policy["unsupported_semantics"]=receipts
	var upgrades:=owner.get("completed_upgrades",[]) as Array;var grant:=String(policy.get("grant_upgrade",""));var remove:=String(policy.get("remove_upgrade",""));if grant!="" and not upgrades.has(grant):upgrades.append(grant);if remove!="":upgrades.erase(remove);owner["completed_upgrades"]=upgrades
	var upgrade_object:=String(policy.get("upgrade_object",""))
	if upgrade_object!="":
		owner["object_id"]=upgrade_object
		owner["unit_type"]=upgrade_object
		var receipts:=policy.get("unsupported_semantics",[]) as Array
		receipts.append("unsupported_creation_semantic:UpgradeObjectTemplateRebuild")
		policy["unsupported_semantics"]=receipts
	_sim._emit_event("object_creation_upgrade.consumed",owner_id,0,{"thing":thing,"grant_upgrade":grant,"remove_upgrade":remove,"upgrade_object":upgrade_object})


func _attach_replace_self_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields := contract.get("fields", {}) as Dictionary
	var replace_with = String(_sim._module_contract_value(fields, "ReplaceWith", "")).strip_edges()
	var triggered_by = String(_sim._module_contract_value(fields, "TriggeredBy", "")).strip_edges()
	var conflicts: Array[String] = []
	var conflict_value: Variant = _sim._module_contract_value(fields, "ConflictsWith", [])
	if typeof(conflict_value) != TYPE_ARRAY:
		return
	for value in conflict_value as Array:
		var conflict := String(value).strip_edges()
		if conflict == "":
			return
		conflicts.append(conflict)
	if replace_with == "" or triggered_by == "" or conflicts.is_empty():
		return
	var additions: Array[String] = []
	var additions_value: Variant = fields.get("AndThenAddA", [])
	if typeof(additions_value) != TYPE_ARRAY:
		return
	for addition_value in additions_value as Array:
		if typeof(addition_value) != TYPE_DICTIONARY:
			return
		var addition := String((addition_value as Dictionary).get("value", "")).strip_edges()
		if addition == "":
			return
		additions.append(addition)
	# The stable compiler accepts either no AndThenAddA rows or exactly two.
	# Repeat the guard at runtime so hand-authored/old packs cannot partly apply.
	if additions.size() not in [0, 2]:
		return
	var policies := row.get("replace_self_upgrades", []) as Array
	policies.append({
		"replace_with": replace_with,
		"triggered_by": triggered_by,
		"conflicts_with": conflicts,
		"and_then_add": additions,
		"consumed": false,
		"unsupported_semantics": [],
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	})
	row["replace_self_upgrades"] = policies


func apply_replace_self_upgrade(structure_id: int, trigger_upgrade_id: String) -> Dictionary:
	var _sim = sim
	if not _sim.structures.has(structure_id):
		return {"ok": false, "reason": "structure-missing"}
	var row := _sim.structures[structure_id] as Dictionary
	if not row.has("replace_self_upgrades"):
		_sim._attach_structure_module_contracts(row)
	var upgrades := row.get("completed_upgrades", []) as Array
	if not upgrades.has(trigger_upgrade_id):
		upgrades.append(trigger_upgrade_id)
		row["completed_upgrades"] = upgrades
	return _apply_due_replace_self_policy(structure_id, row, trigger_upgrade_id)


func _step_replace_self_upgrades() -> void:
	var _sim = sim
	for structure_id in _sim.structure_ids():
		if not _sim.structures.has(structure_id):
			continue
		var row := _sim.structures[structure_id] as Dictionary
		if not row.has("replace_self_upgrades") and not bool(row.get("structure_module_contracts_attached", false)):
			_sim._attach_structure_module_contracts(row)
		var policies := row.get("replace_self_upgrades", []) as Array
		for policy_value in policies:
			var policy := policy_value as Dictionary
			var trigger := String(policy.get("triggered_by", ""))
			if bool(policy.get("consumed", false)) or not (row.get("completed_upgrades", []) as Array).has(trigger):
				continue
			_apply_due_replace_self_policy(structure_id, row, trigger)
			# One object can become only one mutually-exclusive replacement in a
			# tick. A conflict or unresolved target also fails closed here.
			break


func _apply_due_replace_self_policy(structure_id: int, row: Dictionary, trigger_upgrade_id: String) -> Dictionary:
	var _sim = sim
	for policy_value in row.get("replace_self_upgrades", []) as Array:
		var policy := policy_value as Dictionary
		if bool(policy.get("consumed", false)) or String(policy.get("triggered_by", "")) != trigger_upgrade_id:
			continue
		var upgrades := row.get("completed_upgrades", []) as Array
		for conflict_value in policy.get("conflicts_with", []) as Array:
			if upgrades.has(String(conflict_value)):
				# ReplaceSelfUpgrade ConflictsWith (campsandcastles.ini:4420-4447):
				# the five wall routes are mutually exclusive. Named event so a
				# refused route is observable, never silent.
				_sim._emit_event("structure.replace_self_refused", 0, structure_id, {
					"trigger": trigger_upgrade_id,
					"reason": "conflicting-upgrade",
					"conflict": String(conflict_value),
				})
				policy["consumed"] = true
				return {"ok": false, "reason": "conflicting-upgrade", "conflict": String(conflict_value)}
		var replacement_id := String(policy.get("replace_with", ""))
		var primary_spec := _replace_self_structure_spec(int(row.get("team", -1)), replacement_id)
		if primary_spec.is_empty():
			_replace_self_receipt(policy, "unresolved_replacement_template:%s" % replacement_id)
			return {"ok": false, "reason": "replacement-template-unresolved", "object_id": replacement_id}
		var addition_specs: Array[Dictionary] = []
		for addition_value in policy.get("and_then_add", []) as Array:
			var addition_id := String(addition_value)
			var addition_spec := _replace_self_structure_spec(int(row.get("team", -1)), addition_id)
			if addition_spec.is_empty():
				_replace_self_receipt(policy, "unresolved_addition_template:%s" % addition_id)
				return {"ok": false, "reason": "addition-template-unresolved", "object_id": addition_id}
			addition_specs.append(addition_spec)
		var old_identity := String(row.get("source_object_id", row.get("structure_kind", "")))
		var replacement := _replacement_structure_row(structure_id, row, replacement_id, primary_spec, true)
		_sim._note_structure_table_mutation()
		_sim._structure_footprint_radius_cache.erase(structure_id)
		_sim.structures[structure_id] = replacement
		_sim._attach_structure_module_contracts(replacement)
		_sim._apply_structure_create_grants(replacement, true, true)
		_sim._apply_structure_inherit_upgrades(replacement)
		_sim._initialize_structure_auto_deposit(replacement)
		_reconcile_replacement_containment(structure_id, replacement)
		var created_ids: Array[int] = []
		for index in addition_specs.size():
			while _sim.structures.has(_sim._next_dynamic_structure_id):
				_sim._next_dynamic_structure_id += 1
			var created_id = _sim._next_dynamic_structure_id
			_sim._next_dynamic_structure_id += 1
			var addition_id := String((policy.get("and_then_add", []) as Array)[index])
			var addition := _replacement_structure_row(created_id, row, addition_id, addition_specs[index], false)
			_sim._note_structure_table_mutation()
			_sim.structures[created_id] = addition
			_sim._attach_structure_module_contracts(addition)
			_sim._apply_structure_create_grants(addition, true, true)
			_sim._apply_structure_inherit_upgrades(addition)
			_sim._initialize_structure_auto_deposit(addition)
			created_ids.append(created_id)
		policy["consumed"] = true
		_sim._emit_event("upgrade.replace_self", structure_id, 0, {"old_object_id": old_identity, "replacement_object_id": replacement_id, "trigger_upgrade_id": trigger_upgrade_id, "created_object_ids": created_ids.duplicate(), "created_object_templates": (policy.get("and_then_add", []) as Array).duplicate()})
		return {"ok": true, "reason": "", "structure_id": structure_id, "created_object_ids": created_ids}
	return {"ok": false, "reason": "trigger-not-authored"}


func _replace_self_structure_spec(team: int, object_id: String) -> Dictionary:
	var _sim = sim
	var configured := _sim._rules.get("replace_self_structure_templates", {}) as Dictionary
	if typeof(configured.get(object_id)) == TYPE_DICTIONARY:
		return (configured[object_id] as Dictionary).duplicate(true)
	var sources = _sim.structure_source_object_ids_for_team(team)
	for kind_value in sources.keys():
		var ids: Variant = sources[kind_value]
		var matches := String(ids) == object_id if typeof(ids) in [TYPE_STRING, TYPE_STRING_NAME] else (ids as Array).has(object_id) if typeof(ids) == TYPE_ARRAY else false
		if not matches:
			continue
		var kind := String(kind_value)
		var healths = _sim.structure_max_health_for_team(team)
		if not healths.has(kind):
			return {}
		return {"structure_kind": kind, "maximum_health": int(healths[kind])}
	# Replacement targets include castle-wall pieces that are present in the
	# selected playable-structure registry but are intentionally not base-loop
	# build kinds. Resolve their exact object document instead of pretending an
	# absent faction-manifest alias means an absent retail template.
	var document := _playable_structure_runtime_document(object_id)
	if not document.is_empty():
		var registration := document.get("registration", {}) as Dictionary
		var gameplay := registration.get("gameplay", {}) as Dictionary
		var health := gameplay.get("health", {}) as Dictionary
		var primary := health.get("primary", {}) as Dictionary
		var maximum_field := primary.get("maxHealth", {}) as Dictionary
		var maximum: Variant = maximum_field.get("value")
		if typeof(maximum) not in [TYPE_INT, TYPE_FLOAT]:
			var lifecycle := (registration.get("presentation", {}) as Dictionary).get("buildingLifecycle", {}) as Dictionary
			maximum = (lifecycle.get("simulationFacts", {}) as Dictionary).get("maximumHealth")
		if typeof(maximum) in [TYPE_INT, TYPE_FLOAT] and float(maximum) > 0.0:
			var spec := {"structure_kind": String(document.get("slug", object_id)), "maximum_health": int(maximum)}
			var attack := _structure_attack_from_combat(gameplay.get("combat", {}) as Dictionary)
			if not attack.is_empty():
				spec["attack"] = attack
			return spec
	return {}


func _playable_structure_runtime_document(object_id: String) -> Dictionary:
	var _sim = sim
	var runtimes_value: Variant = _sim._rules.get("playable_structure_runtimes", {})
	if typeof(runtimes_value) == TYPE_DICTIONARY:
		var runtimes := runtimes_value as Dictionary
		if typeof(runtimes.get(object_id)) == TYPE_DICTIONARY:
			return (runtimes[object_id] as Dictionary).duplicate(true)
		for key_value in runtimes.keys():
			var candidate: Variant = runtimes[key_value]
			if typeof(candidate) == TYPE_DICTIONARY and String((candidate as Dictionary).get("objectId", "")).nocasecmp_to(object_id) == 0:
				return (candidate as Dictionary).duplicate(true)
	var db = _sim._content_db_ref()
	if db != null and db.has_method("get_playable_structure_runtime"):
		return db.get_playable_structure_runtime(object_id)
	return {}


func _structure_attack_from_combat(combat: Dictionary) -> Dictionary:
	var _sim = sim
	if combat.is_empty():
		return {}
	for field in ["attackRange", "delayBetweenShotsMs", "preAttackDelayMs", "damage"]:
		if typeof(combat.get(field)) != TYPE_DICTIONARY:
			return {}
	var range_value := float((combat["attackRange"] as Dictionary).get("value", -1.0))
	var pre_attack_ms := float((combat["preAttackDelayMs"] as Dictionary).get("value", -1.0))
	var damage := float((combat["damage"] as Dictionary).get("value", 0.0))
	var delay := combat["delayBetweenShotsMs"] as Dictionary
	var delay_ms := float(delay.get("value", -1.0))
	var minimum_ms := int(delay.get("minimumValue", -1))
	var maximum_ms := int(delay.get("maximumValue", -1))
	var interval := String(delay.get("distribution", "")) == "uniform-inclusive-integer"
	if range_value <= 0.0 or pre_attack_ms < 0.0 or damage <= 0.0:
		return {}
	if (not interval and delay_ms < 0.0) or (interval and (minimum_ms < 0 or maximum_ms < minimum_ms)):
		return {}
	var source_scale := float(_sim._rules.get("source_map_transform_scale", 1.0))
	var attack := {
		"range": range_value * source_scale,
		"minimum_range": float((combat.get("minimumAttackRange", {}) as Dictionary).get("value", 0.0)) * source_scale,
		"damage": damage,
		"period_ticks": maxi(1, roundi((float(minimum_ms) if interval else delay_ms) / (_sim.TICK_SECONDS * 1000.0))),
		"pre_attack_ticks": maxi(0, roundi(pre_attack_ms / (_sim.TICK_SECONDS * 1000.0))),
		"projectile_speed": float((combat.get("projectileSpeed", {}) as Dictionary).get("value", 0.0)) * source_scale,
		"projectile_object_id": String(combat.get("projectileObjectId", "")),
		"weapon_id": String(combat.get("weaponId", "")),
	}
	if interval:
		attack["delay_between_shots_distribution"] = "uniform-inclusive-integer"
		attack["delay_between_shots_minimum_ms"] = minimum_ms
		attack["delay_between_shots_maximum_ms"] = maximum_ms
	return attack


func _replacement_structure_row(structure_id: int, previous: Dictionary, object_id: String, spec: Dictionary, preserve_state: bool) -> Dictionary:
	var maximum := maxi(1, int(spec.get("maximum_health", 1)))
	var health := maximum
	if preserve_state:
		var prior_maximum := maxi(1, int(previous.get("maximum_health", 1)))
		health = clampi(roundi(float(int(previous.get("health", 0))) * float(maximum) / float(prior_maximum)), 0, maximum)
	var result = {
		"id": structure_id,
		"team": int(previous.get("team", -1)),
		"kind": "structure",
		"structure_kind": String(spec.get("structure_kind", object_id)),
		"source_object_id": object_id,
		"object_id": object_id,
		"name": String(spec.get("name", object_id)),
		"position": Vector2(previous.get("position", Vector2.ZERO)),
		"rally": Vector2(previous.get("rally", previous.get("position", Vector2.ZERO))),
		"health": health,
		"maximum_health": maximum,
		"construction_progress": 1.0,
		"level": int(previous.get("level", 1)) if preserve_state else 1,
		"completed_upgrades": (previous.get("completed_upgrades", []) as Array).duplicate() if preserve_state else [],
		"upgrade_queue": [],
		"production": (spec.get("production", []) as Array).duplicate(),
		"queue": [],
		"damage_remainders": (previous.get("damage_remainders", {}) as Dictionary).duplicate(true) if preserve_state else {},
		"income_per_payout": int(spec.get("income_per_payout", 0)),
	}
	for key in ["facing", "facing_radians", "rotation", "orientation", "elevation", "castle_owner_structure_id", "castle_piece_of_fortress", "castle_piece_index", "castle_piece_structure_ids", "expansion_pad_id", "expansion_of_fortress", "expansion_pad_index", "build_plot_id", "wall_connection_ids"]:
		if previous.has(key):
			result[key] = previous[key].duplicate(true) if typeof(previous[key]) in [TYPE_ARRAY, TYPE_DICTIONARY] else previous[key]
	if typeof(spec.get("attack")) == TYPE_DICTIONARY:
		result["attack"] = (spec["attack"] as Dictionary).duplicate(true)
	return result


func _reconcile_replacement_containment(structure_id: int, replacement: Dictionary) -> void:
	var _sim = sim
	if not _sim.containment.has(structure_id):
		return
	var capacity := int(replacement.get("transport_capacity", 0)) if replacement.has("horde_transport") else 0
	var passengers := (_sim.containment.get(structure_id, []) as Array).duplicate()
	for index in range(passengers.size() - 1, capacity - 1, -1):
		_sim._finish_transport_exit(structure_id, int(passengers[index]))


# De-staticed on extraction (instance sim access).
func _replace_self_receipt(policy: Dictionary, receipt: String) -> void:
	var receipts := policy.get("unsupported_semantics", []) as Array
	if not receipts.has(receipt):
		receipts.append(receipt)
	policy["unsupported_semantics"] = receipts


func _attach_citadel_slaughter_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("citadel_slaughter"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var passenger_filter = _sim._typed_contract_tokens(fields, "PassengerFilter")
	if passenger_filter == ["GENERIC_FACTION_SLAUGHTERABLE"]:
		# Exact retail gamedata.ini define (BFME2:79 / RotWK:81), expanded here
		# because module contracts intentionally preserve the authored macro token.
		passenger_filter = ["ANY", "+INFANTRY", "+CAVALRY", "-HERO", "-DOZER", "-SUMMONED"]
	var cashback := fields.get("CashBackPercent", {}) as Dictionary
	var ratio: Variant = cashback.get("ratio")
	var capacity: Variant = _sim._module_contract_value(fields, "ContainMax", null)
	if passenger_filter.is_empty() or typeof(ratio) not in [TYPE_INT, TYPE_FLOAT] or typeof(capacity) != TYPE_INT or int(capacity) < 1:
		return
	var upgrades: Array[String] = []
	var upgrade_value: Variant = _sim._module_contract_value(fields, "UpgradeForRingEntry", [])
	if typeof(upgrade_value) != TYPE_ARRAY:
		return
	for value in upgrade_value as Array:
		upgrades.append(String(value))
	var destroy_filter = _sim._typed_contract_tokens(fields, "ObjectToDestroyForRingEntry")
	if destroy_filter.is_empty():
		return
	row["citadel_slaughter"] = {
		"passenger_filter": passenger_filter,
		"contained_statuses": _sim._typed_contract_tokens(fields, "ObjectStatusOfContained"),
		"cashback_ratio": float(ratio),
		"capacity": int(capacity),
		"allow_enemies": bool(_sim._module_contract_value(fields, "AllowEnemiesInside", false)),
		"allow_allies": bool(_sim._module_contract_value(fields, "AllowAlliesInside", false)),
		"allow_neutral": bool(_sim._module_contract_value(fields, "AllowNeutralInside", false)),
		"allow_own": bool(_sim._module_contract_value(fields, "AllowOwnPlayerInsideOverride", false)),
		"enter_sound": String(_sim._module_contract_value(fields, "EnterSound", "")),
		"entry_offset_source": _sim._container_contract_offset(fields, "EntryOffset"),
		"entry_position_source": _sim._container_contract_offset(fields, "EntryPosition"),
		"exit_offset_source": _sim._container_contract_offset(fields, "ExitOffset"),
		"ring_status": String(_sim._module_contract_value(fields, "StatusForRingEntry", "")),
		"ring_upgrades": upgrades,
		"ring_destroy_filter": destroy_filter,
		"ring_fx": String(_sim._module_contract_value(fields, "FXForRingEntry", "")),
		"slaughter_count": 0,
		"cashback_total": 0,
		"ring_entry_count": 0,
		"unsupported_semantics": ["ring_fx_requires_presentation_binding"] if fields.has("FXForRingEntry") else [],
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func enter_citadel_slaughter(structure_id: int, entity_id: int) -> Dictionary:
	var _sim = sim
	if not _sim.structures.has(structure_id) or not _sim.entities.has(entity_id):
		return {"ok": false, "reason": "citadel-or-passenger-missing"}
	var citadel := _sim.structures[structure_id] as Dictionary
	if not citadel.has("citadel_slaughter"):
		_sim._attach_structure_module_contracts(citadel)
	var policy := citadel.get("citadel_slaughter", {}) as Dictionary
	if policy.is_empty():
		return {"ok": false, "reason": "typed-citadel-slaughter-contract-missing"}
	if int(citadel.get("health", 0)) <= 0:
		return {"ok": false, "reason": "citadel-dead"}
	if _sim.entity_container.has(entity_id):
		return {"ok": false, "reason": "entity-already-contained"}
	if _sim.passenger_count(structure_id) >= int(policy.get("capacity", 0)):
		return {"ok": false, "reason": "capacity-full"}
	var passenger := _sim.entities[entity_id] as Dictionary
	if not _sim._transport_filter_accepts(passenger, policy.get("passenger_filter", []) as Array):
		return {"ok": false, "reason": "passenger-filter-refused"}
	var relation = _sim.team_relationship(int(citadel.get("team", -1)), int(passenger.get("team", -2)))
	var admitted = (relation == "local" and (bool(policy.get("allow_own", false)) or bool(policy.get("allow_allies", false)))) or (relation == "allied" and bool(policy.get("allow_allies", false))) or (relation == "enemy" and bool(policy.get("allow_enemies", false))) or (relation == "unavailable" and bool(policy.get("allow_neutral", false)))
	if not admitted:
		return {"ok": false, "reason": "ownership-refused", "relationship": relation}
	var contained = _sim.contain_entity(structure_id, entity_id)
	if not bool(contained.get("ok", false)):
		return contained
	passenger["transport_prior_status"] = (passenger.get("object_status", {}) as Dictionary).duplicate(true)
	var statuses := passenger.get("object_status", {}) as Dictionary
	for value in policy.get("contained_statuses", []) as Array:
		statuses[String(value)] = true
	passenger["object_status"] = statuses
	passenger["position"] = Vector2(citadel.get("position", Vector2.ZERO)) + _sim._retail_source_to_sim_offset(Vector2(policy.get("entry_position_source", Vector2.ZERO)))
	var ring_status := String(policy.get("ring_status", ""))
	var ring_entry := ring_status != "" and bool(statuses.get(ring_status, false))
	if ring_entry:
		return _consume_citadel_ring_entry(structure_id, entity_id, citadel, passenger, policy)
	var cost := int(passenger.get("build_cost", -1))
	if cost < 0:
		var unit_type := String(passenger.get("unit_type", ""))
		if not _sim._unit_production_rules.has(unit_type):
			cost = -1
		else:
			cost = _sim._production_rule_value(unit_type, "cost_rule", "default_cost")
	if cost < 0:
		_sim.exit_entity_container(entity_id)
		passenger["object_status"] = (passenger.get("transport_prior_status", {}) as Dictionary).duplicate(true)
		passenger.erase("transport_prior_status")
		return {"ok": false, "reason": "passenger-cost-unresolved"}
	var cashback := maxi(0, roundi(float(cost) * float(policy.get("cashback_ratio", 0.0))))
	var owner_team := int(citadel.get("team", -1))
	_sim.team_resources[owner_team] = _sim.resources_for_team(owner_team) + cashback
	_sim.exit_entity_container(entity_id)
	passenger["health"] = 0
	passenger["member_health"] = []
	var slaughter_defeated: Array[int] = []
	_sim._bookkeep_battalion_death(entity_id, passenger, "FADED", slaughter_defeated)
	_sim.entities.erase(entity_id)
	policy["slaughter_count"] = int(policy.get("slaughter_count", 0)) + 1
	policy["cashback_total"] = int(policy.get("cashback_total", 0)) + cashback
	_sim._emit_event("citadel.slaughtered", structure_id, entity_id, {"cashback": cashback, "cashback_ratio": float(policy.get("cashback_ratio", 0.0)), "enter_sound": String(policy.get("enter_sound", "")), "entry_offset_source": policy.get("entry_offset_source", Vector2.ZERO)})
	return {"ok": true, "reason": "", "result": "slaughtered", "cashback": cashback}


func _consume_citadel_ring_entry(structure_id: int, entity_id: int, citadel: Dictionary, passenger: Dictionary, policy: Dictionary) -> Dictionary:
	var _sim = sim
	var team := int(citadel.get("team", -1))
	var upgrades := policy.get("ring_upgrades", []) as Array
	if not upgrades.is_empty():
		var team_owned := _sim.team_upgrades.get(team, {}) as Dictionary
		team_owned[String(upgrades[0])] = true
		_sim.team_upgrades[team] = team_owned
		var completed := citadel.get("completed_upgrades", []) as Array
		for index in range(1, upgrades.size()):
			var upgrade := String(upgrades[index])
			if not completed.has(upgrade): completed.append(upgrade)
		completed.sort(); citadel["completed_upgrades"] = completed
	var destroy_passenger = _sim._transport_filter_accepts(passenger, policy.get("ring_destroy_filter", []) as Array)
	_sim.exit_entity_container(entity_id)
	if destroy_passenger:
		passenger["health"] = 0; passenger["member_health"] = []
		var ring_defeated: Array[int] = []
		_sim._bookkeep_battalion_death(entity_id, passenger, "FADED", ring_defeated)
		_sim.entities.erase(entity_id)
	else:
		passenger["object_status"] = (passenger.get("transport_prior_status", {}) as Dictionary).duplicate(true)
		passenger.erase("transport_prior_status")
		passenger["position"] = Vector2(citadel.get("position", Vector2.ZERO)) + _sim._retail_source_to_sim_offset(Vector2(policy.get("exit_offset_source", Vector2.ZERO)))
	policy["ring_entry_count"] = int(policy.get("ring_entry_count", 0)) + 1
	_sim._emit_event("citadel.ring_entry", structure_id, entity_id, {"upgrades": upgrades.duplicate(), "destroyed": destroy_passenger, "fx": String(policy.get("ring_fx", "")), "enter_sound": String(policy.get("enter_sound", ""))})
	return {"ok": true, "reason": "", "result": "ring-entry", "destroyed": destroy_passenger, "upgrades": upgrades.duplicate()}


func _resolve_citadel_slaughter_death(structure_id: int, citadel: Dictionary) -> void:
	var _sim = sim
	var policy := citadel.get("citadel_slaughter", {}) as Dictionary
	if policy.is_empty() or not _sim.containment.has(structure_id):
		return
	for passenger_value in (_sim.containment.get(structure_id, []) as Array).duplicate():
		var passenger_id := int(passenger_value)
		_sim.exit_entity_container(passenger_id)
		if not _sim.entities.has(passenger_id): continue
		var passenger := _sim.entities[passenger_id] as Dictionary
		passenger["object_status"] = (passenger.get("transport_prior_status", {}) as Dictionary).duplicate(true)
		passenger.erase("transport_prior_status")
		passenger["position"] = Vector2(citadel.get("position", Vector2.ZERO)) + _sim._retail_source_to_sim_offset(Vector2(policy.get("exit_offset_source", Vector2.ZERO)))
		_sim._emit_event("citadel.passenger_ejected", structure_id, passenger_id, {"reason": "citadel-death"})


func _attach_ocl_update_contract(row:Dictionary,contract:Dictionary)->void:
	var _sim = sim
	if String(contract.get("extraction",""))!="typed" or row.has("ocl_update"):return
	var fields=contract.get("fields",{}) as Dictionary;var minimum=_sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields,"MinDelay",0.0)));var maximum=_sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields,"MaxDelay",0.0)))
	row["ocl_update"]={"ocl":String(_sim._module_contract_value(fields,"OCL","")),"minimum_ticks":minimum,"maximum_ticks":maximum,"amount":int(_sim._module_contract_value(fields,"Amount",1)),"next_tick":_sim.tick_index+_sim.logic_random_int(minimum,maximum),"emission_count":0,"unsupported_semantics":[]}


func _step_ocl_updates()->void:
	var _sim = sim
	var owners:Array=[]
	for entity_id in _sim.entity_ids():owners.append({"id":entity_id,"kind":"entity"})
	for structure_id in _sim.structure_ids():owners.append({"id":structure_id,"kind":"structure"})
	for owner_ref_value in owners:
		var owner_ref:=owner_ref_value as Dictionary;var table =_sim.structures if String(owner_ref.get("kind"))=="structure" else _sim.entities;var owner_id:=int(owner_ref.get("id"));if not table.has(owner_id):continue
		var owner:=table[owner_id] as Dictionary;var policy:=owner.get("ocl_update",{}) as Dictionary
		if policy.is_empty() or int(owner.get("health",0))<=0 or _sim.tick_index<int(policy.get("next_tick",0)):continue
		for index in int(policy.get("amount",1)):
			var entry:={"team":int(owner.get("team",-1)),"position":owner.get("position",Vector2.ZERO),"creation_list":String(policy.get("ocl","")),"source_entity":owner_id,"tick":_sim.tick_index};var hatch =_sim.hatch_create_object_die_entry(entry);_sim._emit_event("ocl_update.emitted",owner_id,0,{"ocl":policy.get("ocl"),"ordinal":index,"ok":hatch.get("ok",false)})
		policy["emission_count"]=int(policy.get("emission_count",0))+1;policy["next_tick"]=_sim.tick_index+_sim.logic_random_int(int(policy.get("minimum_ticks",0)),int(policy.get("maximum_ticks",0)));owner["ocl_update"]=policy


func _attach_stances_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("stances_behavior"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var template = String(_sim._module_contract_value(fields, "StanceTemplate", "")).strip_edges()
	if template == "":
		return
	var modifier_rules := _sim._rules.get("attribute_modifier_rules", {}) as Dictionary
	var stance_rows: Dictionary = {}
	var unsupported: Array[String] = []
	for stance in ["Aggressive", "HoldGround", "Porcupine"]:
		var modifier_id := "%sStance%s" % [template, stance]
		if modifier_rules.has(modifier_id):
			stance_rows[stance] = (modifier_rules[modifier_id] as Dictionary).duplicate(true)
	if stance_rows.is_empty():
		unsupported.append("unresolved_stance_modifier_template:%s" % template)
	row["stances_behavior"] = {
		"template": template,
		"available_stances": ["HoldGround", "Battle", "Aggressive"] + (["Porcupine"] if stance_rows.has("Porcupine") else []),
		"modifiers": stance_rows,
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}
	if not row.has("stance"):
		row["stance"] = "Battle"
	_apply_stance_modifier(row)


func set_entity_stance(entity_id: int, stance: String) -> Dictionary:
	var _sim = sim
	if _sim.winner != -1:
		return {"ok": false, "reason": "the match is already resolved"}
	if not _sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var row := _sim.entities[entity_id] as Dictionary
	if not row.has("stances_behavior"):
		_sim._attach_module_contracts(row)
	if not row.has("stances_behavior"):
		return {"ok": false, "reason": "typed-stances-contract-missing"}
	var normalized = _normalize_stance_name(stance)
	var policy := row["stances_behavior"] as Dictionary
	if normalized == "" or not (policy.get("available_stances", []) as Array).has(normalized):
		return {"ok": false, "reason": "stance-not-available:%s" % stance}
	if not (policy.get("unsupported_semantics", []) as Array).is_empty():
		return {"ok": false, "reason": String((policy.get("unsupported_semantics", []) as Array)[0])}
	row["stance"] = normalized
	_apply_stance_modifier(row)
	_sim._emit_event("stance.changed", entity_id, 0, {"stance": normalized, "template": String(policy.get("template", ""))})
	return {"ok": true, "reason": "", "stance": normalized}


func toggle_entity_stance(entity_id: int) -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(entity_id):
		return {"ok": false, "reason": "entity-missing"}
	var current := String((_sim.entities[entity_id] as Dictionary).get("stance", "Battle"))
	var order := ["HoldGround", "Battle", "Aggressive"]
	var index := order.find(current)
	return set_entity_stance(entity_id, String(order[(index + 1) % order.size()]))


# De-staticed on extraction (instance sim access).
func _normalize_stance_name(stance: String) -> String:
	match stance.strip_edges().to_lower():
		"holdground", "hold_ground", "hold ground":
			return "HoldGround"
		"battle":
			return "Battle"
		"aggressive":
			return "Aggressive"
		"porcupine":
			return "Porcupine"
	return ""


func _apply_stance_modifier(row: Dictionary) -> void:
	var table = row.get("timed_modifiers", {}) as Dictionary
	table.erase("stance")
	var stance := String(row.get("stance", "Battle"))
	if stance == "Battle":
		if table.is_empty():
			row.erase("timed_modifiers")
		else:
			row["timed_modifiers"] = table
		return
	var policy := row.get("stances_behavior", {}) as Dictionary
	var modifier := (policy.get("modifiers", {}) as Dictionary).get(stance, {}) as Dictionary
	if modifier.is_empty():
		return
	table["stance"] = {
		"modifiers": (modifier.get("effects", []) as Array).duplicate(true),
		"expires_tick": 2147483647,
		"category": String(modifier.get("category", "STANCE")),
		"source_id": int(row.get("id", 0)),
		"modifier_id": "%sStance%s" % [String(policy.get("template", "")), stance],
	}
	row["timed_modifiers"] = table


func _attach_attribute_modifier_aura_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed":
		return
	for existing_value in row.get("attribute_modifier_auras", []) as Array:
		var existing := existing_value as Dictionary
		if String(existing.get("tag", "")) == String(contract.get("tag", "")) and int(existing.get("line", -1)) == int(contract.get("line", -2)):
			return
	var fields := contract.get("fields", {}) as Dictionary
	var starts: Variant = _sim._module_contract_value(fields, "StartsActive", null)
	var bonus = String(_sim._module_contract_value(fields, "BonusName", "")).strip_edges()
	if typeof(starts) != TYPE_BOOL or bonus == "":
		return
	var range_value: Variant = fields.get("Range", {})
	var range_source := -1.0
	var unsupported: Array[String] = []
	if typeof(range_value) == TYPE_DICTIONARY:
		var range_row := range_value as Dictionary
		if typeof(range_row.get("value")) in [TYPE_INT, TYPE_FLOAT]:
			range_source = float(range_row.get("value"))
		else:
			var expression := String(range_row.get("expression", ""))
			var defines := _sim._rules.get("aura_range_defines", {}) as Dictionary
			if typeof(defines.get(expression)) in [TYPE_INT, TYPE_FLOAT]:
				range_source = float(defines[expression])
			else:
				unsupported.append("unresolved_range_expression:%s" % expression)
	if range_source < 0.0:
		return
	var modifier_rules := _sim._rules.get("attribute_modifier_rules", {}) as Dictionary
	var modifier: Dictionary = modifier_rules.get(bonus, {}) as Dictionary
	if modifier.is_empty() or (modifier.get("effects", []) as Array).is_empty():
		unsupported.append("unresolved_modifier_list:%s" % bonus)
	var rows: Array = row.get("attribute_modifier_auras", []) as Array
	rows.append({
		"bonus_name": bonus,
		"starts_active": bool(starts),
		"triggered_by": _sim._typed_contract_tokens(fields, "TriggeredBy"),
		"conflicts_with": _sim._typed_contract_tokens(fields, "ConflictsWith"),
		"object_filter": _sim._typed_contract_tokens(fields, "ObjectFilter"),
		"anti_categories": _sim._typed_contract_tokens(fields, "AntiCategory"),
		"required_conditions": _sim._typed_contract_tokens(fields, "RequiredConditions"),
		"target_enemy": bool(_sim._module_contract_value(fields, "TargetEnemy", false)),
		"allow_self": bool(_sim._module_contract_value(fields, "AllowSelf", false)),
		"run_while_dead": bool(_sim._module_contract_value(fields, "RunWhileDead", false)),
		"affect_contained_only": bool(_sim._module_contract_value(fields, "AffectContainedOnly", false)),
		"max_active_rank": int(_sim._module_contract_value(fields, "MaxActiveRank", 2147483647)),
		"range_source": range_source,
		"refresh_ticks": maxi(1, _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields, "RefreshDelay", 0.0)))),
		"next_refresh_tick": _sim.tick_index,
		"modifier": modifier.duplicate(true),
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	})
	row["attribute_modifier_auras"] = rows


func _resolve_lifetime_bound(field: Variant) -> Dictionary:
	if typeof(field) != TYPE_DICTIONARY:
		return {"ok": false, "reason": "missing-bound"}
	var value := field as Dictionary
	if typeof(value.get("milliseconds")) in [TYPE_INT, TYPE_FLOAT]:
		return {"ok": true, "milliseconds": int(value.get("milliseconds"))}
	var expression := String(value.get("expression", ""))
	var defines := sim._rules.get("lifetime_defines", {}) as Dictionary
	if typeof(defines.get(expression)) in [TYPE_INT, TYPE_FLOAT]:
		return {"ok": true, "milliseconds": int(defines[expression])}
	return {"ok": false, "reason": "unresolved-lifetime-expression:%s" % expression}


func _attach_lifetime_update_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("lifetime_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var wait_for_wakeup = bool(_sim._module_contract_value(fields, "WaitForWakeUp", false))
	var minimum := _resolve_lifetime_bound(fields.get("MinLifetime"))
	var maximum := _resolve_lifetime_bound(fields.get("MaxLifetime"))
	var unsupported: Array[String] = []
	if not bool(minimum.get("ok", false)) or not bool(maximum.get("ok", false)):
		if fields.has("MinLifetime") or fields.has("MaxLifetime"):
			unsupported.append(String(minimum.get("reason", maximum.get("reason", "unresolved-lifetime"))))
		if not wait_for_wakeup:
			return
	var min_ms := int(minimum.get("milliseconds", 0))
	var max_ms := int(maximum.get("milliseconds", 0))
	if min_ms < 0 or max_ms < 0:
		return
	if max_ms < min_ms:
		var swap := min_ms
		min_ms = max_ms
		max_ms = swap
	row["lifetime_update"] = {
		"min_ms": min_ms,
		"max_ms": max_ms,
		"wait_for_wakeup": wait_for_wakeup,
		"awake": not wait_for_wakeup,
		"expire_tick": -1,
		"death_type": String(_sim._module_contract_value(fields, "DeathType", "NORMAL")).to_upper(),
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}
	if not wait_for_wakeup and unsupported.is_empty():
		_arm_lifetime(row)


func _arm_lifetime(row: Dictionary) -> bool:
	var _sim = sim
	var lifetime = row.get("lifetime_update", {}) as Dictionary
	if lifetime.is_empty() or not (lifetime.get("unsupported_semantics", []) as Array).is_empty():
		return false
	if int(lifetime.get("expire_tick", -1)) >= 0:
		return true
	var duration_ms = _sim.logic_random_int(int(lifetime.get("min_ms", 0)), int(lifetime.get("max_ms", 0)))
	lifetime["awake"] = true
	lifetime["selected_duration_ms"] = duration_ms
	lifetime["expire_tick"] = _sim.tick_index + _sim._ship_contract_delay_ticks(float(duration_ms))
	row["lifetime_update"] = lifetime
	return true


func wake_lifetime(object_id: int, object_kind: String = "entity") -> Dictionary:
	var _sim = sim
	var table = _sim.structures if object_kind == "structure" else _sim.entities
	if not table.has(object_id):
		return {"ok": false, "reason": "object-missing"}
	var row := table[object_id] as Dictionary
	if not row.has("lifetime_update"):
		if object_kind == "structure":
			_sim._attach_structure_module_contracts(row)
		else:
			_sim._attach_module_contracts(row)
	if not row.has("lifetime_update"):
		return {"ok": false, "reason": "typed-lifetime-contract-missing"}
	if not bool((row["lifetime_update"] as Dictionary).get("wait_for_wakeup", false)):
		return {"ok": false, "reason": "lifetime-does-not-wait"}
	if not _arm_lifetime(row):
		return {"ok": false, "reason": "lifetime-bound-unresolved"}
	return {"ok": true, "reason": "", "expire_tick": int((row["lifetime_update"] as Dictionary).get("expire_tick", -1))}


func _step_lifetime_updates() -> void:
	var _sim = sim
	var entity_keys = _sim.entity_ids()
	for entity_id in entity_keys:
		if not _sim.entities.has(entity_id):
			continue
		var row := _sim.entities[entity_id] as Dictionary
		if not row.has("lifetime_update") and not row.has("module_contracts"):
			_sim._attach_module_contracts(row)
		var lifetime = row.get("lifetime_update", {}) as Dictionary
		if int(lifetime.get("expire_tick", -1)) >= 0 and _sim.tick_index >= int(lifetime.get("expire_tick", -1)):
			_expire_lifetime_entity(entity_id, row, String(lifetime.get("death_type", "NORMAL")))
	for structure_id in _sim.structure_ids():
		if not _sim.structures.has(structure_id):
			continue
		var row := _sim.structures[structure_id] as Dictionary
		if not row.has("lifetime_update") and not bool(row.get("structure_module_contracts_attached", false)):
			_sim._attach_structure_module_contracts(row)
		var lifetime = row.get("lifetime_update", {}) as Dictionary
		if int(lifetime.get("expire_tick", -1)) >= 0 and _sim.tick_index >= int(lifetime.get("expire_tick", -1)):
			_expire_lifetime_structure(structure_id, row, String(lifetime.get("death_type", "NORMAL")))


func _expire_lifetime_entity(entity_id: int, row: Dictionary, death_type: String) -> void:
	var _sim = sim
	if int(row.get("health", 0)) <= 0:
		return
	var health_values: Array = row.get("member_health", []) as Array
	for index in health_values.size():
		health_values[index] = 0
	row["member_health"] = health_values
	row["health"] = 0
	_sim._schedule_respawn_update(entity_id, row, death_type, 0)
	var no_defeated_members: Array[int] = []
	_sim._apply_playable_unit_death_policy(row, death_type, no_defeated_members)
	_sim._consume_create_object_die(row, death_type)
	_sim._schedule_fire_weapon_when_dead(row, death_type, "battalion")
	_sim._emit_event("lifetime.expired", entity_id, 0, {"death_type": death_type})


func _expire_lifetime_structure(structure_id: int, row: Dictionary, death_type: String) -> void:
	var _sim = sim
	if int(row.get("health", 0)) <= 0:
		return
	row["health"] = 0
	_sim._schedule_fire_weapon_when_dead(row, death_type, "structure")
	_sim._consume_create_object_die(row, death_type)
	_sim._begin_ship_slow_death(structure_id, row, death_type)
	_sim._emit_event("lifetime.expired", structure_id, 0, {"death_type": death_type})


func _step_attribute_modifier_auras() -> void:
	var _sim = sim
	var source_rows: Array[Dictionary] = []
	for entity_id in _sim.entity_ids():
		var row := _sim.entities[entity_id] as Dictionary
		if not row.has("attribute_modifier_auras") and not row.has("module_contracts"):
			_sim._attach_module_contracts(row)
		if row.has("attribute_modifier_auras"):
			source_rows.append(row)
	for structure_id in _sim.structure_ids():
		var row := _sim.structures[structure_id] as Dictionary
		if not row.has("attribute_modifier_auras") and not bool(row.get("structure_module_contracts_attached", false)):
			_sim._attach_structure_module_contracts(row)
		if row.has("attribute_modifier_auras"):
			source_rows.append(row)
	for source in source_rows:
		_step_attribute_modifier_aura_source(source)


func _step_attribute_modifier_aura_source(source: Dictionary) -> void:
	var _sim = sim
	var rules := source.get("attribute_modifier_auras", []) as Array
	for rule_index in rules.size():
		var rule := rules[rule_index] as Dictionary
		if _sim.tick_index < int(rule.get("next_refresh_tick", 0)):
			continue
		var refresh_ticks := int(rule.get("refresh_ticks", 1))
		rule["next_refresh_tick"] = _sim.tick_index + refresh_ticks
		rules[rule_index] = rule
		if not _aura_source_active(source, rule):
			continue
		var modifier := rule.get("modifier", {}) as Dictionary
		if modifier.is_empty() or not (rule.get("unsupported_semantics", []) as Array).is_empty():
			continue
		var origin := Vector2(source.get("position", Vector2.ZERO))
		var scale := float(_sim._rules.get("source_map_transform_scale", 1.0))
		var radius = float(rule.get("range_source", 0.0)) * (scale if scale > 0.0 else 1.0)
		var source_id := int(source.get("id", 0))
		var source_team := int(source.get("team", -1))
		for target_id in _sim.entity_ids():
			var target := _sim.entities[target_id] as Dictionary
			if target_id == source_id and not bool(rule.get("allow_self", false)):
				continue
			var hostile = _sim._is_hostile(source_team, int(target.get("team", -2)))
			if hostile != bool(rule.get("target_enemy", false)):
				continue
			if bool(rule.get("affect_contained_only", false)) and not _sim.entity_container.has(target_id):
				continue
			if not _sim._transport_filter_accepts(target, rule.get("object_filter", []) as Array):
				continue
			if Vector2(target.get("position", Vector2.ZERO)).distance_to(origin) > radius:
				continue
			_apply_typed_aura_to_target(source_id, target, rule, modifier, refresh_ticks)
	source["attribute_modifier_auras"] = rules


func _aura_source_active(source: Dictionary, rule: Dictionary) -> bool:
	if int(source.get("health", 0)) <= 0 and not bool(rule.get("run_while_dead", false)):
		return false
	if int(source.get("level", 1)) > int(rule.get("max_active_rank", 2147483647)):
		return false
	var upgrades: Array = source.get("completed_upgrades", []) as Array
	var applied := source.get("applied_upgrades", {}) as Dictionary
	var triggered := rule.get("triggered_by", []) as Array
	if not bool(rule.get("starts_active", false)):
		var triggered_now := false
		for upgrade_value in triggered:
			if _aura_has_upgrade(upgrades, applied, String(upgrade_value)):
				triggered_now = true
				break
		if not triggered_now:
			return false
	for conflict_value in rule.get("conflicts_with", []) as Array:
		if _aura_has_upgrade(upgrades, applied, String(conflict_value)):
			return false
	var statuses := source.get("object_status", {}) as Dictionary
	for condition_value in rule.get("required_conditions", []) as Array:
		if not bool(statuses.get(String(condition_value), false)):
			return false
	return true


# De-staticed on extraction (instance sim access).
func _aura_has_upgrade(upgrades: Array, applied: Dictionary, sought: String) -> bool:
	var folded := sought.to_upper()
	for upgrade_value in upgrades:
		if String(upgrade_value).to_upper() == folded:
			return true
	for upgrade_value in applied.keys():
		if String(upgrade_value).to_upper() == folded:
			return true
	return false


func _apply_typed_aura_to_target(source_id: int, target: Dictionary, rule: Dictionary, modifier: Dictionary, refresh_ticks: int) -> void:
	var _sim = sim
	var table = target.get("timed_modifiers", {}) as Dictionary
	for anti_value in rule.get("anti_categories", []) as Array:
		var anti := String(anti_value)
		for key_value in table.keys().duplicate():
			if String((table[key_value] as Dictionary).get("category", "")) == anti:
				table.erase(key_value)
	target["timed_modifiers"] = table
	var category := String(modifier.get("category", ""))
	var stacking := modifier.get("stacking", {}) as Dictionary
	if bool(stacking.get("ignoreIfAnticategoryActive", false)) and (rule.get("anti_categories", []) as Array).has(category):
		return
	var duration_ticks = maxi(1, _sim._ship_contract_delay_ticks(float(modifier.get("duration_ms", refresh_ticks * _sim.TICK_SECONDS * 1000.0))))
	var key := "typed-aura:%s:%d" % [String(rule.get("bonus_name", "")), source_id]
	if bool(stacking.get("replaceInCategoryIfLongest", false)) and category != "":
		key = "typed-aura-category:%s" % category
		var current := table.get(key, {}) as Dictionary
		if int(current.get("expires_tick", -1)) > _sim.tick_index + duration_ticks:
			return
	table[key] = {
		"modifiers": (modifier.get("effects", []) as Array).duplicate(true),
		"expires_tick": _sim.tick_index + duration_ticks,
		"category": category,
		"source_id": source_id,
	}
	target["timed_modifiers"] = table
	_sim._emit_event("module.attribute_modifier_aura", source_id, int(target.get("id", 0)), {"bonus_name": String(rule.get("bonus_name", "")), "category": category})


func _attach_model_condition_sound_selector(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed": return
	var states: Variant = (contract.get("fields", {}) as Dictionary).get("SoundState", [])
	if typeof(states) != TYPE_ARRAY or (states as Array).is_empty(): return
	var selectors := row.get("model_condition_sound_selectors", []) as Array
	selectors.append({"states": (states as Array).duplicate(true), "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))})
	row["model_condition_sound_selectors"] = selectors


func _attach_random_sound_selector(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("random_sound_selector"): return
	var fields := contract.get("fields", {}) as Dictionary; var chance := fields.get("Chance", {}) as Dictionary
	var ratio: Variant = chance.get("ratio", chance.get("fraction"))
	if typeof(ratio) not in [TYPE_INT, TYPE_FLOAT]: return
	row["random_sound_selector"] = {"chance_fraction": float(ratio), "reroll_every_frame": bool(_sim._module_contract_value(fields, "RerollOnEveryFrame", false)), "voice_priority": int(_sim._module_contract_value(fields, "VoicePriority", 0)), "unsupported_semantics": ["reroll_every_frame_requires_presentation_frame_clock"] if bool(_sim._module_contract_value(fields, "RerollOnEveryFrame", false)) else [], "rng_receipt": "presentation_sequence_hash_not_gameplay_rng"}


func _attach_upgrade_sound_selector(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or String(contract.get("runtimeStatus", "")) != "executable": return
	var fields := contract.get("fields", {}) as Dictionary
	var clauses_value: Variant = fields.get("SoundUpgrade", [])
	if typeof(clauses_value) != TYPE_ARRAY or (clauses_value as Array).is_empty(): return
	var byte_receipt_value: Variant = fields.get("wavSetByteIdentityReceipt", {})
	if typeof(byte_receipt_value) != TYPE_DICTIONARY: return
	var byte_receipt := byte_receipt_value as Dictionary
	if typeof(byte_receipt.get("logicalEventIds", [])) != TYPE_ARRAY or typeof(byte_receipt.get("leaves", [])) != TYPE_ARRAY or (byte_receipt.get("leaves", []) as Array).is_empty(): return
	for leaf_value in byte_receipt.get("leaves", []) as Array:
		if typeof(leaf_value) != TYPE_DICTIONARY: return
		var leaf := leaf_value as Dictionary
		if String(leaf.get("virtualPath", "")) == "" or String(leaf.get("cookedPath", "")) == "": return
		for sha_key in ["sha256", "cookedSha256"]:
			var sha := String(leaf.get(sha_key, ""))
			if sha.length() != 64 or sha != sha.to_lower(): return
			for index in sha.length():
				if not "0123456789abcdef".contains(sha.substr(index, 1)): return
	var clauses: Array[Dictionary] = []
	for clause_value in clauses_value as Array:
		if typeof(clause_value) != TYPE_DICTIONARY: return
		var clause := clause_value as Dictionary
		var required: Variant = clause.get("requiredUpgrades", [])
		var excluded: Variant = clause.get("excludedUpgrades", [])
		var sounds: Variant = clause.get("sounds", {})
		if typeof(required) != TYPE_ARRAY or (required as Array).is_empty() or typeof(excluded) != TYPE_ARRAY or typeof(sounds) != TYPE_DICTIONARY or (sounds as Dictionary).is_empty(): return
		for sound_ids_value in (sounds as Dictionary).values():
			if typeof(sound_ids_value) != TYPE_ARRAY or (sound_ids_value as Array).is_empty(): return
			for sound_id_value in sound_ids_value as Array:
				if String(sound_id_value) == "": return
		clauses.append({"required_upgrades": (required as Array).duplicate(), "excluded_upgrades": (excluded as Array).duplicate(), "sounds": (sounds as Dictionary).duplicate(true), "line": int(clause.get("line", 0))})
	var selectors := row.get("upgrade_sound_selectors", []) as Array
	var identity := "%s:%d" % [String(contract.get("tag", "")), int(contract.get("line", 0))]
	for selector_value in selectors:
		if String((selector_value as Dictionary).get("identity", "")) == identity: return
	selectors.append({"clauses": clauses, "identity": identity, "receipt": "retail-upgrade-sound-logical-ids-preserved", "wav_set_byte_identity_receipt": byte_receipt.duplicate(true)})
	row["upgrade_sound_selectors"] = selectors


func _attach_large_group_audio_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("large_group_audio"): return
	var fields = contract.get("fields", {}) as Dictionary; var key = _sim._typed_contract_tokens(fields, "Key")
	if key.is_empty(): return
	row["large_group_audio"] = {"category_key": key, "unit_weight": int(_sim._module_contract_value(fields, "UnitWeight", 1)), "rng_receipt": "presentation_sequence_hash_not_gameplay_rng"}


func emit_typed_audio_intent(entity_id: int, sound_role: String) -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(entity_id): return {"ok": false, "reason": "entity-missing"}
	var row := _sim.entities[entity_id] as Dictionary
	if not row.has("model_condition_sound_selectors"): _sim._attach_module_contracts(row)
	var field := _audio_sound_field(sound_role); var active := _audio_active_conditions(row); var candidates: Array[Dictionary] = []
	var owned_upgrades: Dictionary = {}
	for upgrade_value in row.get("completed_upgrades", []) as Array: owned_upgrades[String(upgrade_value).to_lower()] = true
	for upgrade_value in (row.get("applied_upgrades", {}) as Dictionary).keys(): owned_upgrades[String(upgrade_value).to_lower()] = true
	for selector_value in row.get("upgrade_sound_selectors", []) as Array:
		for clause_value in (selector_value as Dictionary).get("clauses", []) as Array:
			var clause := clause_value as Dictionary; var matches := true
			for required_value in clause.get("required_upgrades", []) as Array:
				if not owned_upgrades.has(String(required_value).to_lower()): matches = false; break
			if not matches: continue
			for excluded_value in clause.get("excluded_upgrades", []) as Array:
				if owned_upgrades.has(String(excluded_value).to_lower()): matches = false; break
			if not matches: continue
			var ids_value: Variant = (clause.get("sounds", {}) as Dictionary).get(field, [])
			if typeof(ids_value) != TYPE_ARRAY or (ids_value as Array).is_empty(): continue
			var ids := (ids_value as Array).duplicate()
			if ids != ((selector_value as Dictionary).get("wav_set_byte_identity_receipt", {}) as Dictionary).get("logicalEventIds", []): continue
			candidates.append({"event_id": String(ids[0]), "logical_event_ids": ids, "priority": 2147483647, "line": int(clause.get("line", 0)), "selector_receipt": String((selector_value as Dictionary).get("receipt", "")), "wav_set_byte_identity_receipt": ((selector_value as Dictionary).get("wav_set_byte_identity_receipt", {}) as Dictionary).duplicate(true)})
	# An active upgrade selector overrides the ordinary model-condition route.
	var upgrade_candidates := not candidates.is_empty()
	for selector_value in row.get("model_condition_sound_selectors", []) as Array:
		if upgrade_candidates: break
		for state_value in (selector_value as Dictionary).get("states", []) as Array:
			var state := state_value as Dictionary; var matches := true
			for condition_value in state.get("conditions", []) as Array:
				if not active.has(String(condition_value).to_upper()): matches = false; break
			if not matches: continue
			var sounds := state.get("sounds", {}) as Dictionary; var specific := (state.get("unitSpecificSounds", {}) as Dictionary).get("sounds", {}) as Dictionary
			var sound: Variant = sounds.get(field, specific.get(field))
			if typeof(sound) != TYPE_DICTIONARY: continue
			var priority = int(_sim._module_contract_value(sounds, "VoicePriority", 0)); candidates.append({"event_id": String((sound as Dictionary).get("value", "")), "priority": priority, "line": int(state.get("line", 0))})
	if candidates.is_empty(): return {"ok": false, "reason": "no-matching-model-condition-sound"}
	candidates.sort_custom(func(a: Dictionary, b: Dictionary) -> bool: return int(a.get("priority", 0)) > int(b.get("priority", 0)) if int(a.get("priority", 0)) != int(b.get("priority", 0)) else int(a.get("line", 0)) < int(b.get("line", 0)))
	var selected := candidates[0] as Dictionary; var random := row.get("random_sound_selector", {}) as Dictionary
	if not random.is_empty():
		if bool(random.get("reroll_every_frame", false)): return {"ok": false, "reason": "presentation-frame-reroll-unsupported", "rng_receipt": random.get("rng_receipt", "")}
		var roll = _audio_sequence_roll(entity_id, sound_role); if roll >= float(random.get("chance_fraction", 0.0)): return {"ok": false, "reason": "random-selector-suppressed", "roll": roll, "rng_receipt": random.get("rng_receipt", "")}
		selected["priority"] = maxi(int(selected.get("priority", 0)), int(random.get("voice_priority", 0))); selected["roll"] = roll; selected["rng_receipt"] = random.get("rng_receipt", "")
	_sim._emit_event("audio.typed_selector", entity_id, 0, {"object_id": String(row.get("object_id", "")), "event_id": String(selected.get("event_id", "")), "logical_event_ids": Array(selected.get("logical_event_ids", [selected.get("event_id", "")])).duplicate(), "sound_role": sound_role, "priority": int(selected.get("priority", 0)), "rng_receipt": String(selected.get("rng_receipt", "model_condition_exact")), "selector_receipt": String(selected.get("selector_receipt", "")), "wav_set_byte_identity_receipt": (selected.get("wav_set_byte_identity_receipt", {}) as Dictionary).duplicate(true)})
	selected["ok"] = true; selected["reason"] = ""; return selected


func emit_large_group_audio_intent(entity_ids_value: Array, sound_role: String) -> Dictionary:
	var _sim = sim
	var grouped: Dictionary = {}
	for value in entity_ids_value:
		var entity_id := int(value); if not _sim.entities.has(entity_id): continue
		var row = _sim.entities[entity_id] as Dictionary; if not row.has("large_group_audio"): _sim._attach_module_contracts(row)
		var policy := row.get("large_group_audio", {}) as Dictionary; if policy.is_empty(): continue
		var key := " ".join(policy.get("category_key", []) as Array); var bucket := grouped.get(key, {"weight": 0, "members": []}) as Dictionary; bucket["weight"] = int(bucket.get("weight", 0)) + int(policy.get("unit_weight", 1)); (bucket["members"] as Array).append(entity_id); grouped[key] = bucket
	if grouped.is_empty(): return {"ok": false, "reason": "no-large-group-audio-category"}
	var keys := grouped.keys(); keys.sort(); var best_key := String(keys[0]); for key_value in keys: if int((grouped[key_value] as Dictionary).get("weight", 0)) > int((grouped[best_key] as Dictionary).get("weight", 0)): best_key = String(key_value)
	var members := (grouped[best_key] as Dictionary).get("members", []) as Array; members.sort(); var total := 0; for member_id in members: total += int(((_sim.entities[int(member_id)] as Dictionary).get("large_group_audio", {}) as Dictionary).get("unit_weight", 1))
	var pick := int(floor(_audio_sequence_roll(total, sound_role + best_key) * total)); var selected_id := int(members[0]); var cursor := 0
	for member_id in members:
		cursor += int(((_sim.entities[int(member_id)] as Dictionary).get("large_group_audio", {}) as Dictionary).get("unit_weight", 1)); if pick < cursor: selected_id = int(member_id); break
	var result = emit_typed_audio_intent(selected_id, sound_role); result["category_key"] = best_key; result["group_weight"] = int((grouped[best_key] as Dictionary).get("weight", 0)); result["selected_entity_id"] = selected_id; result["rng_receipt"] = "presentation_sequence_hash_not_gameplay_rng"; return result


func _audio_sequence_roll(entity_id: int, role: String) -> float:
	var _sim = sim
	var hash := 2166136261
	for byte in ("%d:%d:%s" % [_sim._typed_audio_roll_sequence, entity_id, role]).to_utf8_buffer(): hash = ((hash ^ int(byte)) * 16777619) & 0xFFFFFFFF
	_sim._typed_audio_roll_sequence += 1
	return float(hash & 0xFFFFFF) / 16777216.0


func _attach_fire_spread_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("fire_spread_update"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var minimum = _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields, "MinSpreadDelay", 0.0)))
	var maximum = _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields, "MaxSpreadDelay", 0.0)))
	var spread_range = float(_sim._module_contract_value(fields, "SpreadTryRange", -1.0))
	if minimum < 0 or maximum < minimum or spread_range < 0.0:
		return
	row["fire_spread_update"] = {
		"burning": false,
		"minimum_delay_ticks": minimum,
		"maximum_delay_ticks": maximum,
		"spread_range_source": spread_range,
		"next_spread_tick": -1,
		"spread_serial": 0,
		"rng_receipt": "deterministic_delay_hash_unproven_retail_rng_seed",
		# FireSpreadUpdate has no ignition/damage field. The fire/damage consumer
		# must explicitly start this scheduler instead of inventing a weapon.
		"unsupported_semantics": ["ignition_source_owned_by_fire_damage_consumer"],
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func set_fire_spread_active(object_id: int, active: bool) -> Dictionary:
	var _sim = sim
	var table = _sim.structures if _sim.structures.has(object_id) else _sim.entities
	if not table.has(object_id):
		return {"ok": false, "reason": "object-missing"}
	var row := table[object_id] as Dictionary
	if not row.has("fire_spread_update"):
		if _sim.structures.has(object_id): _sim._attach_structure_module_contracts(row)
		else: _sim._attach_module_contracts(row)
	var policy := row.get("fire_spread_update", {}) as Dictionary
	if policy.is_empty():
		return {"ok": false, "reason": "typed-fire-spread-contract-missing"}
	policy["burning"] = active
	policy["next_spread_tick"] = _sim.tick_index + _fire_spread_delay(object_id, policy) if active else -1
	row["fire_spread_update"] = policy
	return {"ok": true, "reason": "", "next_spread_tick": int(policy.get("next_spread_tick", -1)), "rng_receipt": String(policy.get("rng_receipt", ""))}


func _fire_spread_delay(object_id: int, policy: Dictionary) -> int:
	var minimum := int(policy.get("minimum_delay_ticks", 0))
	var maximum := int(policy.get("maximum_delay_ticks", minimum))
	var width := maximum - minimum + 1
	if width <= 1:
		return minimum
	var hash := 2166136261
	for byte in ("%d:%d" % [object_id, int(policy.get("spread_serial", 0))]).to_utf8_buffer():
		hash = ((hash ^ int(byte)) * 16777619) & 0xFFFFFFFF
	return minimum + int(hash % width)


func _step_fire_spread_updates() -> void:
	var _sim = sim
	var sources: Array[Dictionary] = []
	for id in _sim.entity_ids(): sources.append({"id": id, "kind": "entity"})
	for id in _sim.structure_ids(): sources.append({"id": id, "kind": "structure"})
	for source_value in sources:
		var source := source_value as Dictionary
		var source_id := int(source.get("id", 0))
		var source_table = _sim.structures if String(source.get("kind", "")) == "structure" else _sim.entities
		if not source_table.has(source_id): continue
		var row := source_table[source_id] as Dictionary
		var policy := row.get("fire_spread_update", {}) as Dictionary
		if policy.is_empty() or not bool(policy.get("burning", false)) or _sim.tick_index < int(policy.get("next_spread_tick", -1)): continue
		var origin := Vector2(row.get("position", Vector2.ZERO))
		var radius = _sim._retail_source_to_sim_offset(Vector2(float(policy.get("spread_range_source", 0.0)), 0.0)).x
		var candidates: Array[Dictionary] = []
		for target_value in sources:
			var target := target_value as Dictionary
			var target_id := int(target.get("id", 0)); var target_kind := String(target.get("kind", ""))
			if target_id == source_id and target_kind == String(source.get("kind", "")): continue
			var target_table = _sim.structures if target_kind == "structure" else _sim.entities
			if not target_table.has(target_id): continue
			var target_row := target_table[target_id] as Dictionary
			var target_policy := target_row.get("fire_spread_update", {}) as Dictionary
			if target_policy.is_empty() or bool(target_policy.get("burning", false)) or int(target_row.get("health", 1)) <= 0: continue
			var distance := origin.distance_to(Vector2(target_row.get("position", Vector2.ZERO)))
			if distance <= radius: candidates.append({"id": target_id, "kind": target_kind, "distance": distance})
		candidates.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
			if not is_equal_approx(float(a.get("distance", 0.0)), float(b.get("distance", 0.0))): return float(a.get("distance", 0.0)) < float(b.get("distance", 0.0))
			if String(a.get("kind", "")) != String(b.get("kind", "")): return String(a.get("kind", "")) < String(b.get("kind", ""))
			return int(a.get("id", 0)) < int(b.get("id", 0)))
		policy["spread_serial"] = int(policy.get("spread_serial", 0)) + 1
		policy["next_spread_tick"] = _sim.tick_index + _fire_spread_delay(source_id, policy)
		row["fire_spread_update"] = policy
		if candidates.is_empty(): continue
		var chosen := candidates[0] as Dictionary
		var chosen_table = _sim.structures if String(chosen.get("kind", "")) == "structure" else _sim.entities
		var chosen_row := chosen_table[int(chosen.get("id", 0))] as Dictionary
		var chosen_policy := chosen_row.get("fire_spread_update", {}) as Dictionary
		chosen_policy["burning"] = true
		chosen_policy["next_spread_tick"] = _sim.tick_index + _fire_spread_delay(int(chosen.get("id", 0)), chosen_policy)
		chosen_row["fire_spread_update"] = chosen_policy
		_sim._emit_event("module.fire_spread", source_id, int(chosen.get("id", 0)), {"target_kind": String(chosen.get("kind", "")), "distance": float(chosen.get("distance", 0.0)), "rng_receipt": String(policy.get("rng_receipt", ""))})


func _audio_active_conditions(row: Dictionary) -> Dictionary:
	var result = {}; result[String(row.get("state", "")).to_upper()] = true
	for value in row.get("model_conditions", []) as Array: result[String(value).to_upper()] = true
	for value in (row.get("object_status", {}) as Dictionary).keys(): if bool((row.get("object_status", {}) as Dictionary).get(value, false)): result[String(value).to_upper()] = true
	return result


# De-staticed on extraction (instance sim access).
func _audio_sound_field(role: String) -> String:
	var folded := role.replace("_", "").to_lower(); var map := {"move":"VoiceMove","select":"VoiceSelect","attack":"VoiceAttack","attackcharge":"VoiceAttackCharge","attackmachine":"VoiceAttackMachine","attackstructure":"VoiceAttackStructure","fear":"VoiceFear","guard":"VoiceGuard","impact":"SoundImpact","moveloop":"SoundMoveLoop","garrison":"VoiceGarrison","movetotrees":"VoiceMoveToTrees"}; return String(map.get(folded, role))


func _attach_radiate_fear_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("radiate_fear"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var radius_field := fields.get("EmotionPulseRadius", {}) as Dictionary
	var radius_source := -1.0
	var unsupported: Array[String] = []
	if typeof(radius_field.get("value")) in [TYPE_INT, TYPE_FLOAT]:
		radius_source = float(radius_field.get("value"))
	else:
		var define := String(radius_field.get("define", ""))
		var defines := _sim._rules.get("fear_radius_defines", {}) as Dictionary
		if typeof(defines.get(define)) in [TYPE_INT, TYPE_FLOAT]:
			radius_source = float(defines[define])
		else:
			unsupported.append("unresolved_fear_radius:%s" % define)
	if radius_source < 0.0:
		return
	var fear_type = "UNCONTROLLABLE" if bool(_sim._module_contract_value(fields, "GenerateUncontrollableFear", false)) else ("TERROR" if bool(_sim._module_contract_value(fields, "GenerateTerror", false)) else "FEAR")
	row["radiate_fear"] = {"active": bool(_sim._module_contract_value(fields, "InitiallyActive", false)), "triggered_by": String(_sim._module_contract_value(fields, "TriggeredBy", "")), "special_power": int(_sim._module_contract_value(fields, "WhichSpecialPower", -1)), "fear_type": fear_type, "radius_source": radius_source, "interval_ticks": maxi(1, _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields, "EmotionPulseInterval", 0.0)))), "next_tick": _sim.tick_index, "victim_filter": _sim._typed_contract_tokens(fields, "VictimFilter"), "pulse_count": 0, "unsupported_semantics": unsupported, "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))}


func activate_radiate_fear(entity_id: int, special_power: int) -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(entity_id): return {"ok": false, "reason": "entity-missing"}
	var policy := (_sim.entities[entity_id] as Dictionary).get("radiate_fear", {}) as Dictionary
	if policy.is_empty(): return {"ok": false, "reason": "typed-radiate-fear-contract-missing"}
	if int(policy.get("special_power", -1)) != special_power: return {"ok": false, "reason": "special-power-mismatch"}
	policy["active"] = true; policy["next_tick"] = _sim.tick_index
	return {"ok": true, "reason": ""}


func _step_radiate_fear_updates() -> void:
	var _sim = sim
	for source_id in _sim.entity_ids():
		var source := _sim.entities[source_id] as Dictionary; var policy := source.get("radiate_fear", {}) as Dictionary
		if policy.is_empty() or int(source.get("health", 0)) <= 0: continue
		var trigger := String(policy.get("triggered_by", ""))
		if trigger != "" and _aura_has_upgrade(source.get("completed_upgrades", []) as Array, source.get("applied_upgrades", {}) as Dictionary, trigger): policy["active"] = true
		if not bool(policy.get("active", false)) or _sim.tick_index < int(policy.get("next_tick", 0)): continue
		policy["next_tick"] = _sim.tick_index + int(policy.get("interval_ticks", 1)); policy["pulse_count"] = int(policy.get("pulse_count", 0)) + 1
		var origin := Vector2(source.get("position", Vector2.ZERO)); var radius = _sim._retail_source_to_sim_offset(Vector2(float(policy.get("radius_source", 0.0)), 0.0)).x
		for target_id in _sim.entity_ids():
			if target_id == source_id: continue
			var target := _sim.entities[target_id] as Dictionary
			if not _sim._is_hostile(int(source.get("team", -1)), int(target.get("team", -2))) or int(target.get("health", 0)) <= 0 or bool(target.get("fear_resistant", false)): continue
			var filter := policy.get("victim_filter", []) as Array
			if not _fear_victim_filter_accepts(target, filter): continue
			if Vector2(target.get("position", Vector2.ZERO)).distance_to(origin) > radius: continue
			target["fear_state"] = String(policy.get("fear_type", "FEAR")); target["fear_source_id"] = source_id; target["fear_pulse_tick"] = _sim.tick_index
			if String(policy.get("fear_type", "")) in ["TERROR", "UNCONTROLLABLE"]: _sim._apply_fear_scatter(origin, target, radius)
		source["radiate_fear"] = policy


func _fear_victim_filter_accepts(target: Dictionary, tokens: Array) -> bool:
	if tokens.is_empty(): return true
	var object_tokens: Array = []
	for value in tokens:
		if String(value).to_upper() not in ["ALL", "NONE", "ENEMIES", "ALLIES"]: object_tokens.append(value)
	return object_tokens.is_empty() or sim._transport_filter_accepts(target, object_tokens)


func _attach_poisoned_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("poisoned_behavior"): return
	var fields := contract.get("fields", {}) as Dictionary
	row["poisoned_behavior"] = {"interval_ticks": maxi(1, _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields, "PoisonDamageInterval", 0.0)))), "duration_ticks": maxi(1, _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields, "PoisonDuration", 0.0)))), "active": false, "damage_per_pulse": 0.0, "next_tick": -1, "expires_tick": -1, "pulse_count": 0, "unsupported_semantics": []}


func apply_poison(entity_id: int, damage_per_pulse: float) -> Dictionary:
	var _sim = sim
	if not _sim.entities.has(entity_id) or damage_per_pulse <= 0.0: return {"ok": false, "reason": "invalid-poison-target"}
	var row := _sim.entities[entity_id] as Dictionary; var policy := row.get("poisoned_behavior", {}) as Dictionary
	if policy.is_empty(): return {"ok": false, "reason": "typed-poison-contract-missing"}
	policy["active"] = true; policy["damage_per_pulse"] = damage_per_pulse; policy["next_tick"] = _sim.tick_index + int(policy.get("interval_ticks", 1)); policy["expires_tick"] = _sim.tick_index + int(policy.get("duration_ticks", 1)); policy["pulse_count"] = 0
	return {"ok": true, "reason": "", "next_tick": policy["next_tick"], "expires_tick": policy["expires_tick"]}


func _step_poisoned_behaviors() -> void:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		var row := _sim.entities[entity_id] as Dictionary; var policy := row.get("poisoned_behavior", {}) as Dictionary
		if policy.is_empty() or not bool(policy.get("active", false)) or int(row.get("health", 0)) <= 0: continue
		if _sim.tick_index > int(policy.get("expires_tick", -1)): policy["active"] = false; continue
		if _sim.tick_index >= int(policy.get("next_tick", 0)):
			_sim._apply_area_damage_to_battalion(entity_id, float(policy.get("damage_per_pulse", 0.0)), "POISON")
			policy["pulse_count"] = int(policy.get("pulse_count", 0)) + 1; policy["next_tick"] = _sim.tick_index + int(policy.get("interval_ticks", 1))
		row["poisoned_behavior"] = policy


func _attach_damage_field_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("damage_field"): return
	var fields := contract.get("fields", {}) as Dictionary; var nugget := fields.get("FireWeaponNugget", {}) as Dictionary
	var weapon = String(_sim._module_contract_value(nugget, "WeaponName", "")); var unsupported: Array[String] = []
	if not _sim._death_weapon_rules.has(weapon): unsupported.append("unresolved_damage_field_weapon:%s" % weapon)
	for token in _sim._typed_contract_tokens(fields, "ObjectFilter"):
		if token not in ["ALL", "NONE", "ENEMIES", "ALLIES"]:
			unsupported.append("unsupported_damage_field_filter_token:%s" % token)
	row["damage_field"] = {"radius_source": float(_sim._module_contract_value(fields, "Radius", 0.0)), "object_filter": _sim._typed_contract_tokens(fields, "ObjectFilter"), "required_upgrade": String(_sim._module_contract_value(fields, "RequiredUpgrade", "")), "weapon": weapon, "delay_ticks": maxi(1, _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(nugget, "FireDelay", 0.0)))), "one_shot": bool(_sim._module_contract_value(nugget, "OneShot", false)), "next_tick": _sim.tick_index + maxi(1, _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(nugget, "FireDelay", 0.0)))), "fired": false, "unsupported_semantics": unsupported}


func _step_damage_fields() -> void:
	var _sim = sim
	var sources: Array = []
	for entity_id in _sim.entity_ids(): sources.append(_sim.entities[entity_id])
	for structure_id in _sim.structure_ids(): sources.append(_sim.structures[structure_id])
	for source_value in sources:
		var source := source_value as Dictionary; var policy := source.get("damage_field", {}) as Dictionary
		if policy.is_empty() or int(source.get("health", 0)) <= 0 or _sim.tick_index < int(policy.get("next_tick", 0)) or (bool(policy.get("one_shot", false)) and bool(policy.get("fired", false))): continue
		var required := String(policy.get("required_upgrade", "")); if required != "" and not _aura_has_upgrade(source.get("completed_upgrades", []) as Array, source.get("applied_upgrades", {}) as Dictionary, required): continue
		if not (policy.get("unsupported_semantics", []) as Array).is_empty(): continue
		var weapon := String(policy.get("weapon", "")); var rule := (_sim._death_weapon_rules.get(weapon, {}) as Dictionary).duplicate(true); rule["radius_source"] = float(policy.get("radius_source", 0.0)); var filter := policy.get("object_filter", []) as Array; rule["affects"] = "ALLIES ENEMIES" if filter.has("ALLIES") and filter.has("ENEMIES") else ("ALLIES" if filter.has("ALLIES") else "ENEMIES")
		_sim._fire_death_weapon({"weapon_id": weapon, "weapon_rule": rule, "point": source.get("position", Vector2.ZERO), "team": int(source.get("team", -1)), "source_id": int(source.get("id", 0)), "death_type": "DAMAGE_FIELD"}); policy["fired"] = true; policy["next_tick"] = _sim.tick_index + int(policy.get("delay_ticks", 1)); source["damage_field"] = policy


func _attach_spawn_unit_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("spawn_unit_behavior"): return
	var fields := contract.get("fields", {}) as Dictionary
	row["spawn_unit_behavior"] = {"unit_name": String(_sim._module_contract_value(fields, "UnitName", "")), "unit_command": String(_sim._module_contract_value(fields, "UnitCommand", "")), "spawn_once": bool(_sim._module_contract_value(fields, "SpawnOnce", false)), "spawned": false, "spawned_ids": [], "pending": not fields.has("UnitCommand"), "unsupported_semantics": []}


func request_spawn_unit_command(owner_id: int, command: String) -> Dictionary:
	var _sim = sim
	var table = _sim.structures if _sim.structures.has(owner_id) else _sim.entities
	if not table.has(owner_id): return {"ok": false, "reason": "owner-missing"}
	var policy := (table[owner_id] as Dictionary).get("spawn_unit_behavior", {}) as Dictionary
	if policy.is_empty(): return {"ok": false, "reason": "typed-spawn-unit-contract-missing"}
	if String(policy.get("unit_command", "")) != command: return {"ok": false, "reason": "command-mismatch"}
	if bool(policy.get("spawn_once", false)) and bool(policy.get("spawned", false)): return {"ok": false, "reason": "already-spawned"}
	policy["pending"] = true; return {"ok": true, "reason": ""}


func _step_spawn_unit_behaviors() -> void:
	var _sim = sim
	var owners: Array = []
	for entity_id in _sim.entity_ids(): owners.append(_sim.entities[entity_id])
	for structure_id in _sim.structure_ids(): owners.append(_sim.structures[structure_id])
	for owner_value in owners:
		var owner := owner_value as Dictionary; var policy := owner.get("spawn_unit_behavior", {}) as Dictionary
		if policy.is_empty() or not bool(policy.get("pending", false)) or (bool(policy.get("spawn_once", false)) and bool(policy.get("spawned", false))): continue
		var spawned_id = _sim.spawn_script_object(String(policy.get("unit_name", "")), int(owner.get("team", -1)), Vector2(owner.get("position", Vector2.ZERO)))
		if spawned_id > 0:
			var ids := policy.get("spawned_ids", []) as Array; ids.append(spawned_id); policy["spawned_ids"] = ids; policy["spawned"] = true; policy["pending"] = false
		else:
			var receipts := policy.get("unsupported_semantics", []) as Array; var receipt = "unresolved_spawn_unit:%s" % String(policy.get("unit_name", "")); if not receipts.has(receipt): receipts.append(receipt); policy["unsupported_semantics"] = receipts; policy["pending"] = false
		owner["spawn_unit_behavior"] = policy


func _attach_hit_reaction_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("hit_reaction"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var tiers: Array[Dictionary] = []
	for index in range(1, 4):
		var timer_key := "HitReactionLifeTimer%d" % index
		var threshold_key := "HitReactionThreshold%d" % index
		if not fields.has(timer_key):
			continue
		var timer: Variant = _sim._module_contract_value(fields, timer_key, null)
		var threshold: Variant = _sim._module_contract_value(fields, threshold_key, null)
		if typeof(timer) != TYPE_INT or typeof(threshold) not in [TYPE_INT, TYPE_FLOAT]:
			return
		tiers.append({"tier": index, "life_ticks": _sim._ship_contract_delay_ticks(float(timer)), "threshold": float(threshold)})
	if tiers.size() not in [1, 3]:
		return
	row["hit_reaction"] = {"tiers": tiers, "fast_hits_reset": bool(_sim._module_contract_value(fields, "FastHitsResetReaction", false)), "hits": [], "active_tier": 0, "expires_tick": -1, "unsupported_semantics": ["reaction_animation_requires_presentation_binding"], "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))}


func record_hit_reaction(entity_id: int, damage: float) -> Dictionary:
	var _sim = sim
	if damage <= 0.0 or not _sim.entities.has(entity_id):
		return {"ok": false, "reason": "invalid-hit"}
	var row := _sim.entities[entity_id] as Dictionary
	if not row.has("hit_reaction"):
		_sim._attach_module_contracts(row)
	var policy := row.get("hit_reaction", {}) as Dictionary
	if policy.is_empty():
		return {"ok": false, "reason": "typed-hit-reaction-contract-missing"}
	var hits := policy.get("hits", []) as Array
	hits.append({"tick": _sim.tick_index, "damage": damage})
	var highest := 0
	var selected_life := 0
	for tier_value in policy.get("tiers", []) as Array:
		var tier := tier_value as Dictionary
		var total := 0.0
		var earliest = _sim.tick_index - int(tier.get("life_ticks", 0))
		for hit_value in hits:
			var hit := hit_value as Dictionary
			if int(hit.get("tick", 0)) >= earliest:
				total += float(hit.get("damage", 0.0))
		if total >= float(tier.get("threshold", 0.0)):
			highest = int(tier.get("tier", 0))
			selected_life = int(tier.get("life_ticks", 0))
	if highest > 0:
		if highest >= int(policy.get("active_tier", 0)) or bool(policy.get("fast_hits_reset", false)):
			policy["active_tier"] = highest
			policy["expires_tick"] = _sim.tick_index + selected_life
			_sim._emit_event("module.hit_reaction", entity_id, 0, {"tier": highest, "damage": damage})
	policy["hits"] = hits
	row["hit_reaction"] = policy
	return {"ok": true, "reason": "", "tier": int(policy.get("active_tier", 0)), "expires_tick": int(policy.get("expires_tick", -1))}


func _step_hit_reactions() -> void:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		var row := _sim.entities[entity_id] as Dictionary
		var policy := row.get("hit_reaction", {}) as Dictionary
		if policy.is_empty():
			continue
		if int(policy.get("expires_tick", -1)) >= 0 and _sim.tick_index >= int(policy.get("expires_tick", -1)):
			policy["active_tier"] = 0
			policy["expires_tick"] = -1
		var max_life := 0
		for tier_value in policy.get("tiers", []) as Array:
			max_life = maxi(max_life, int((tier_value as Dictionary).get("life_ticks", 0)))
		var retained: Array = []
		for hit_value in policy.get("hits", []) as Array:
			if int((hit_value as Dictionary).get("tick", 0)) >= _sim.tick_index - max_life:
				retained.append(hit_value)
		policy["hits"] = retained
		row["hit_reaction"] = policy


func _attach_animal_ai_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("animal_ai"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	row["animal_ai"] = {"flee_range_source": float(_sim._module_contract_value(fields, "FleeRange", 0.0)), "flee_distance_source": float(_sim._module_contract_value(fields, "FleeDistance", _sim._module_contract_value(fields, "MaxWanderDistance", 0.0))), "wander_percent": float(_sim._module_contract_value(fields, "WanderPercentage", 0.0)), "max_wander_distance_source": float(_sim._module_contract_value(fields, "MaxWanderDistance", 0.0)), "max_wander_radius_source": float(_sim._module_contract_value(fields, "MaxWanderRadius", 0.0)), "update_ticks": maxi(1, _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields, "UpdateTimer", 1000.0)))), "next_tick": _sim.tick_index, "home": Vector2(row.get("position", Vector2.ZERO)), "unsupported_semantics": [], "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))}


func _step_animal_ai_updates() -> void:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		var row := _sim.entities[entity_id] as Dictionary
		var policy := row.get("animal_ai", {}) as Dictionary
		if policy.is_empty() or int(row.get("health", 0)) <= 0 or _sim.tick_index < int(policy.get("next_tick", 0)):
			continue
		policy["next_tick"] = _sim.tick_index + int(policy.get("update_ticks", 1))
		var origin := Vector2(row.get("position", Vector2.ZERO))
		var threat_id := _nearest_hostile_entity(entity_id, float(policy.get("flee_range_source", 0.0)))
		if threat_id != 0:
			var away := origin - Vector2((_sim.entities[threat_id] as Dictionary).get("position", Vector2.ZERO))
			if away.length_squared() <= 0.000001:
				away = Vector2.RIGHT if entity_id % 2 == 0 else Vector2.LEFT
			row["destination"] = origin + away.normalized() * _sim._retail_source_to_sim_offset(Vector2(float(policy.get("flee_distance_source", 0.0)), 0.0)).x
			row["state"] = "flee"
		elif _sim.logic_random_int(1, 10000) <= int(round(float(policy.get("wander_percent", 0.0)) * 100.0)):
			var angle := TAU * float(_sim.logic_random_int(0, 359)) / 360.0
			var distance_source := float(_sim.logic_random_int(0, int(round(float(policy.get("max_wander_distance_source", 0.0))))))
			var candidate = origin + Vector2(cos(angle), sin(angle)) * _sim._retail_source_to_sim_offset(Vector2(distance_source, 0.0)).x
			var home := Vector2(policy.get("home", origin))
			var max_radius = _sim._retail_source_to_sim_offset(Vector2(float(policy.get("max_wander_radius_source", 0.0)), 0.0)).x
			if candidate.distance_to(home) > max_radius and max_radius > 0.0:
				candidate = home + (candidate - home).normalized() * max_radius
			row["destination"] = candidate
			row["state"] = "wander"
		row["animal_ai"] = policy


func _attach_threat_finder_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("threat_finder"):
		return
	var radius: Variant = _sim._module_contract_value(contract.get("fields", {}) as Dictionary, "DefaultRadius", null)
	if typeof(radius) not in [TYPE_INT, TYPE_FLOAT] or float(radius) < 0.0:
		return
	row["threat_finder"] = {"radius_source": float(radius), "next_tick": _sim.tick_index, "update_ticks": 1, "last_target_id": 0, "unsupported_semantics": [], "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0))}


func _step_threat_finders() -> void:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		var row := _sim.entities[entity_id] as Dictionary
		var policy := row.get("threat_finder", {}) as Dictionary
		if policy.is_empty() or int(row.get("health", 0)) <= 0 or _sim.tick_index < int(policy.get("next_tick", 0)):
			continue
		policy["next_tick"] = _sim.tick_index + int(policy.get("update_ticks", 1))
		var previous_target := int(policy.get("last_target_id", 0))
		var target_id := _nearest_hostile_entity(entity_id, float(policy.get("radius_source", 0.0)))
		policy["last_target_id"] = target_id
		if target_id != 0:
			row["target_id"] = target_id
			row["target_kind"] = "battalion"
		elif previous_target != 0 and int(row.get("target_id", 0)) == previous_target:
			row["target_id"] = 0
		row["threat_finder"] = policy


func _nearest_hostile_entity(source_id: int, radius_source: float) -> int:
	var _sim = sim
	if not _sim.entities.has(source_id):
		return 0
	var source := _sim.entities[source_id] as Dictionary
	var origin := Vector2(source.get("position", Vector2.ZERO))
	var radius = _sim._retail_source_to_sim_offset(Vector2(radius_source, 0.0)).x
	var best_id := 0
	var best_distance := INF
	for candidate_id in _sim.entity_ids():
		if candidate_id == source_id:
			continue
		var candidate = _sim.entities[candidate_id] as Dictionary
		if int(candidate.get("health", 0)) <= 0 or not _sim._is_hostile(int(source.get("team", -1)), int(candidate.get("team", -2))):
			continue
		var distance := origin.distance_to(Vector2(candidate.get("position", Vector2.ZERO)))
		if distance <= radius and (distance < best_distance or (is_equal_approx(distance, best_distance) and candidate_id < best_id)):
			best_id = candidate_id
			best_distance = distance
	return best_id


func _attach_large_group_bonus_contract(row: Dictionary, contract: Dictionary) -> void:
	var _sim = sim
	if String(contract.get("extraction", "")) != "typed" or row.has("large_group_bonus"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var modifier_name = String(_sim._module_contract_value(fields, "AttributeModifier", ""))
	var modifiers := _sim._rules.get("attribute_modifier_rules", {}) as Dictionary
	var modifier := modifiers.get(modifier_name, {}) as Dictionary
	var unsupported: Array[String] = []
	if modifier.is_empty() or (modifier.get("effects", []) as Array).is_empty():
		unsupported.append("unresolved_modifier_list:%s" % modifier_name)
	row["large_group_bonus"] = {
		"update_ticks": maxi(1, _sim._ship_contract_delay_ticks(float(_sim._module_contract_value(fields, "UpdateRate", 0.0)))),
		"next_tick": _sim.tick_index,
		"member_filter": _sim._typed_contract_tokens(fields, "HordeMemberFilter"),
		"count": int(_sim._module_contract_value(fields, "Count", 1)),
		"radius_source": float(_sim._module_contract_value(fields, "Radius", 0.0)),
		"ruboff_radius_source": float(_sim._module_contract_value(fields, "RubOffRadius", 0.0)),
		"allies_only": bool(_sim._module_contract_value(fields, "AlliesOnly", false)),
		"modifier_name": modifier_name,
		"modifier": modifier.duplicate(true),
		"active": false,
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0)),
	}


func _step_large_group_bonus_updates() -> void:
	var _sim = sim
	for source_id in _sim.entity_ids():
		var source := _sim.entities[source_id] as Dictionary
		if not source.has("large_group_bonus"):
			_sim._attach_module_contracts(source)
		var policy := source.get("large_group_bonus", {}) as Dictionary
		if policy.is_empty() or _sim.tick_index < int(policy.get("next_tick", 0)):
			continue
		var update_ticks := int(policy.get("update_ticks", 1))
		policy["next_tick"] = _sim.tick_index + update_ticks
		var radius_source := float(policy.get("ruboff_radius_source" if bool(policy.get("active", false)) else "radius_source", 0.0))
		var radius = _sim._retail_source_to_sim_offset(Vector2(radius_source, 0.0)).x
		var count := 0
		for target_id in _sim.entity_ids():
			var target := _sim.entities[target_id] as Dictionary
			if int(target.get("health", 0)) <= 0:
				continue
			if bool(policy.get("allies_only", false)) and _sim._is_hostile(int(source.get("team", -1)), int(target.get("team", -2))):
				continue
			if not _sim._transport_filter_accepts(target, policy.get("member_filter", []) as Array):
				continue
			if Vector2(target.get("position", Vector2.ZERO)).distance_to(Vector2(source.get("position", Vector2.ZERO))) <= radius:
				count += 1
		policy["active"] = count >= int(policy.get("count", 1))
		var table = source.get("timed_modifiers", {}) as Dictionary
		var key := "large-group:%s" % String(policy.get("modifier_name", ""))
		if bool(policy.get("active", false)) and (policy.get("unsupported_semantics", []) as Array).is_empty():
			var modifier := policy.get("modifier", {}) as Dictionary
			table[key] = {"modifiers": (modifier.get("effects", []) as Array).duplicate(true), "expires_tick": _sim.tick_index + update_ticks + 1, "category": String(modifier.get("category", "")), "source_id": source_id}
		else:
			table.erase(key)
		source["timed_modifiers"] = table
		source["large_group_bonus"] = policy


# De-staticed on extraction (family cohesion with sim._passive_area_effect_field).
func _passive_area_effect_number(fields: Dictionary, key: String) -> float:
	var raw: Variant = fields.get(key, fields.get(key.to_lower(), null))
	if typeof(raw) == TYPE_DICTIONARY:
		var row := raw as Dictionary
		if typeof(row.get("value")) in [TYPE_INT, TYPE_FLOAT]:
			return float(row.get("value"))
	var text = sim._passive_area_effect_field(fields, key)
	return float(text) if text.is_valid_float() else 0.0


# De-staticed on extraction (family cohesion with sim._passive_area_effect_field).
func _passive_area_effect_percent(text: String) -> float:
	var value := text.strip_edges()
	if not value.ends_with("%"):
		return 0.0
	value = value.trim_suffix("%").strip_edges()
	return float(value) / 100.0 if value.is_valid_float() else 0.0


# De-staticed on extraction (family cohesion with sim._passive_area_effect_field).
func _passive_area_effect_yes(fields: Dictionary, key: String) -> bool:
	return sim._passive_area_effect_field(fields, key).to_lower() in ["yes", "true", "1"]
