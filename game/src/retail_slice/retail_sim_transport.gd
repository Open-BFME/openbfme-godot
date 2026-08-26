extends RefCounted
## Transport/container/garrison/ship subsystem carved out of retail_slice_sim.gd (drawer 14).
## State stays on the sim; the sim keeps one-line delegates under the original names.

var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()

func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)

func _attach_siege_engine_contain_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("horde_transport"):
		return
	var fields := contract.get("fields", {}) as Dictionary
	var slots: Variant = sim._module_contract_value(fields, "Slots", null)
	var damage := fields.get("DamagePercentToUnits", {}) as Dictionary
	if typeof(slots) != TYPE_INT or int(slots) < 0 or typeof(damage.get("ratio")) not in [TYPE_INT, TYPE_FLOAT]:
		return
	var unsupported: Array[String] = []
	if bool(sim._module_contract_value(fields, "ShowPips", false)):
		unsupported.append("show_pips_requires_hud_binding")
	if fields.has("PassengerBonePrefix"):
		unsupported.append("passenger_bone_requires_model_attachment")
	if int(sim._module_contract_value(fields, "NumberOfExitPaths", 0)) > 1:
		unsupported.append("multiple_exit_path_geometry")
	if fields.has("InitialCrew"):
		unsupported.append("initial_crew_requires_object_factory")
	if fields.has("SpeedPercentPerCrew"):
		unsupported.append("speed_per_crew_requires_mobile_structure_locomotion")
	var initial := fields.get("InitialCrew", {}) as Dictionary
	row["transport_capacity"] = int(slots)
	row["horde_transport"] = {
		"module": "SiegeEngineContain", "contained_statuses": sim._typed_contract_tokens(fields, "ObjectStatusOfContained"),
		"crew_statuses": sim._typed_contract_tokens(fields, "ObjectStatusOfCrew"), "passenger_filter": sim._typed_contract_tokens(fields, "PassengerFilter"),
		"crew_filter": sim._typed_contract_tokens(fields, "CrewFilter"), "crew_max": int(sim._module_contract_value(fields, "CrewMax", 0)),
		"initial_crew_object": String(initial.get("object", "")), "initial_crew_count": int(initial.get("count", 0)), "initial_crew_initialized": initial.is_empty(),
		"damage_ratio": float(damage.get("ratio", 0.0)), "allow_own": bool(sim._module_contract_value(fields, "AllowAlliesInside", false)),
		"allow_allies": bool(sim._module_contract_value(fields, "AllowAlliesInside", false)), "allow_enemies": bool(sim._module_contract_value(fields, "AllowEnemiesInside", false)),
		"allow_neutral": bool(sim._module_contract_value(fields, "AllowNeutralInside", false)), "exit_delay_ticks": sim._ship_contract_delay_ticks(float(sim._module_contract_value(fields, "ExitDelay", 0.0))),
		"kill_passengers_on_death": bool(sim._module_contract_value(fields, "KillPassengersOnDeath", false)), "eject_passengers_on_death": bool(sim._module_contract_value(fields, "EjectPassengersOnDeath", false)),
		"go_aggressive_on_exit": bool(sim._module_contract_value(fields, "GoAggressiveOnExit", false)), "speed_fraction_per_crew": float((fields.get("SpeedPercentPerCrew", {}) as Dictionary).get("fraction", 0.0)),
		"weapon_sets_one": [String(sim._module_contract_value(fields, "TypeOneForWeaponSet", ""))] if fields.has("TypeOneForWeaponSet") else [],
		"bone_condition_states": Array(fields.get("BoneSpecificConditionState", [])).duplicate(true), "passenger_bones": Array(fields.get("PassengerBonePrefix", [])).duplicate(true),
		"unsupported_semantics": unsupported, "tag": String(contract.get("tag", "")), "line": int(contract.get("line", 0)),
	}
	row["siege_crew_ids"] = []


func _attach_container_family_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("horde_transport"):
		return
	var module := String(contract.get("module", ""))
	if module not in ["TransportContain", "TunnelContain", "GarrisonContain", "HordeGarrisonContain", "ProductionQueueHordeContain"]:
		return
	var fields := contract.get("fields", {}) as Dictionary
	var capacity_key := "Slots" if module == "TransportContain" else "ContainMax"
	var capacity: Variant = sim._module_contract_value(fields, capacity_key, null)
	if typeof(capacity) != TYPE_INT or int(capacity) < 1:
		return
	var damage_ratio := 0.0
	if fields.has("DamagePercentToUnits"):
		var damage := fields["DamagePercentToUnits"] as Dictionary
		if typeof(damage.get("fraction")) in [TYPE_INT, TYPE_FLOAT]:
			damage_ratio = float(damage.get("fraction"))
		elif typeof(damage.get("ratio")) in [TYPE_INT, TYPE_FLOAT]:
			damage_ratio = float(damage.get("ratio"))
		else:
			return
	var unsupported: Array[String] = []
	if bool(sim._module_contract_value(fields, "ShowPips", false)):
		unsupported.append("show_pips_requires_hud_binding")
	if fields.has("EnterSound"):
		unsupported.append("enter_sound_requires_audio_binding")
	if fields.has("PassengerBonePrefix"):
		unsupported.append("passenger_bone_requires_model_attachment")
	if fields.has("EntryPosition"):
		unsupported.append("entry_position_requires_model_geometry")
	if int(sim._module_contract_value(fields, "NumberOfExitPaths", 0)) > 1:
		unsupported.append("multiple_exit_path_geometry")
	for key in ["GrabWeapon", "ReleaseSnappyness", "CollidePickup", "FireGrabWeaponOnVictim", "CanGrabStructure", "DestroyRidersWhoAreNotFreeToExit"]:
		if fields.has(key):
			unsupported.append("unsupported_container_semantic:%s" % key)
	if fields.has("UpgradeCreationTrigger"):
		unsupported.append("unsupported_container_semantic:UpgradeCreationTriggerObjectFactory")
	if fields.has("FadeFilter"):
		unsupported.append("fade_filter_requires_presentation_binding")
	var entry_offset := _container_contract_offset(fields, "EntryOffset")
	var exit_offset := _container_contract_offset(fields, "ExitOffset")
	var kill_on_death := bool(sim._module_contract_value(fields, "KillPassengersOnDeath", false))
	row["transport_capacity"] = int(capacity)
	row["horde_transport"] = {
		"module": module,
		"contained_statuses": sim._typed_contract_tokens(fields, "ObjectStatusOfContained"),
		"passenger_filter": sim._typed_contract_tokens(fields, "PassengerFilter"),
		"manual_pickup_filter": sim._typed_contract_tokens(fields, "ManualPickUpFilter"),
		"damage_ratio": damage_ratio,
		"allow_own": bool(sim._module_contract_value(fields, "AllowOwnPlayerInsideOverride", false)),
		"allow_allies": bool(sim._module_contract_value(fields, "AllowAlliesInside", false)),
		"allow_enemies": bool(sim._module_contract_value(fields, "AllowEnemiesInside", false)),
		"allow_neutral": bool(sim._module_contract_value(fields, "AllowNeutralInside", false)),
		"exit_delay_ticks": sim._ship_contract_delay_ticks(float(sim._module_contract_value(fields, "ExitDelay", 0.0))),
		"kill_passengers_on_death": kill_on_death,
		"eject_passengers_on_death": bool(sim._module_contract_value(fields, "EjectPassengersOnDeath", false)) or (module in ["GarrisonContain", "HordeGarrisonContain"] and not kill_on_death),
		"force_orientation": bool(sim._module_contract_value(fields, "ForceOrientationContainer", false)),
		"passenger_bones": Array(fields.get("PassengerBonePrefix", [])).duplicate(true),
		"entry_offset_source": entry_offset,
		"exit_offset_source": exit_offset,
		"weapon_sets_one": _container_string_rows(fields, "TypeOneForWeaponSet"),
		"weapon_states_one": _container_string_rows(fields, "TypeOneForWeaponState"),
		"weapon_sets_two": _container_string_rows(fields, "TypeTwoForWeaponSet"),
		"weapon_states_two": _container_string_rows(fields, "TypeTwoForWeaponState"),
		"bone_condition_states": Array(fields.get("BoneSpecificConditionState", [])).duplicate(true),
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func _container_contract_offset(fields: Dictionary, key: String) -> Vector2:
	var field: Variant = fields.get(key)
	if typeof(field) != TYPE_DICTIONARY:
		return Vector2.ZERO
	var value := (field as Dictionary).get("value", {}) as Dictionary
	return Vector2(float(value.get("x", 0.0)), float(value.get("y", 0.0)))


func _container_string_rows(fields: Dictionary, key: String) -> Array[String]:
	var result: Array[String] = []
	for row_value in fields.get(key, []) as Array:
		if typeof(row_value) == TYPE_DICTIONARY:
			result.append(String((row_value as Dictionary).get("value", "")))
	return result


func _attach_horde_transport_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("horde_transport"):
		return
	var fields: Dictionary = contract.get("fields", {}) as Dictionary
	var slots: Variant = sim._module_contract_value(fields, "Slots", null)
	var damage_value: Variant = fields.get("DamagePercentToUnits")
	if typeof(slots) != TYPE_INT or int(slots) < 1 or typeof(damage_value) != TYPE_DICTIONARY:
		return
	var damage_row := damage_value as Dictionary
	if typeof(damage_row.get("ratio")) not in [TYPE_INT, TYPE_FLOAT]:
		return
	var bones_value: Variant = fields.get("PassengerBonePrefix")
	if typeof(bones_value) != TYPE_ARRAY or (bones_value as Array).is_empty():
		return
	var unsupported: Array[String] = []
	if int(sim._module_contract_value(fields, "NumberOfExitPaths", 0)) > 1:
		unsupported.append("multiple_exit_path_geometry")
	if fields.has("InitialPayload"):
		unsupported.append("initial_payload_requires_object_factory")
	if bool(sim._module_contract_value(fields, "ShowPips", false)):
		unsupported.append("show_pips_requires_hud_binding")
	if bool(sim._module_contract_value(fields, "FadePassengerOnEnter", false)) or bool(sim._module_contract_value(fields, "FadePassengerOnExit", false)):
		unsupported.append("fade_timers_require_presentation_binding")
	unsupported.append("passenger_bone_requires_model_attachment")
	row["transport_capacity"] = int(slots)
	row["horde_transport"] = {
		"contained_statuses": sim._typed_contract_tokens(fields, "ObjectStatusOfContained"),
		"passenger_filter": sim._typed_contract_tokens(fields, "PassengerFilter"),
		"fade_filter": sim._typed_contract_tokens(fields, "FadeFilter"),
		"damage_ratio": float(damage_row.get("ratio", 0.0)),
		"allow_own": bool(sim._module_contract_value(fields, "AllowOwnPlayerInsideOverride", false)),
		"allow_allies": bool(sim._module_contract_value(fields, "AllowAlliesInside", false)),
		"allow_enemies": bool(sim._module_contract_value(fields, "AllowEnemiesInside", false)),
		"allow_neutral": bool(sim._module_contract_value(fields, "AllowNeutralInside", false)),
		"exit_delay_ticks": sim._ship_contract_delay_ticks(float(sim._module_contract_value(fields, "ExitDelay", 0.0))),
		"force_orientation": bool(sim._module_contract_value(fields, "ForceOrientationContainer", false)),
		"show_pips": bool(sim._module_contract_value(fields, "ShowPips", false)),
		"kill_passengers_on_death": bool(sim._module_contract_value(fields, "KillPassengersOnDeath", false)),
		"eject_passengers_on_death": bool(sim._module_contract_value(fields, "EjectPassengersOnDeath", false)),
		"fade_on_enter": bool(sim._module_contract_value(fields, "FadePassengerOnEnter", false)),
		"enter_fade_ticks": sim._ship_contract_delay_ticks(float(sim._module_contract_value(fields, "EnterFadeTime", 0.0))),
		"fade_on_exit": bool(sim._module_contract_value(fields, "FadePassengerOnExit", false)),
		"exit_fade_ticks": sim._ship_contract_delay_ticks(float(sim._module_contract_value(fields, "ExitFadeTime", 0.0))),
		"passenger_bones": (bones_value as Array).duplicate(true),
		"enter_sound": String(sim._module_contract_value(fields, "EnterSound", "")),
		"exit_sound": String(sim._module_contract_value(fields, "ExitSound", "")),
		"unsupported_semantics": unsupported,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func _attach_ship_slow_death_contract(row: Dictionary, contract: Dictionary) -> void:
	if String(contract.get("extraction", "")) != "typed" or row.has("ship_slow_death"):
		return
	var fields: Dictionary = contract.get("fields", {}) as Dictionary
	var sink_delay: Variant = sim._module_contract_value(fields, "SinkDelay", null)
	var destruction: Variant = sim._module_contract_value(fields, "DestructionDelay", null)
	var sink_rate: Variant = sim._module_contract_value(fields, "SinkRate", null)
	if typeof(sink_delay) not in [TYPE_INT, TYPE_FLOAT] or typeof(destruction) not in [TYPE_INT, TYPE_FLOAT] or typeof(sink_rate) not in [TYPE_INT, TYPE_FLOAT]:
		return
	if float(sink_delay) < 0.0 or float(destruction) < 0.0 or float(sink_rate) < 0.0:
		return
	var sound_event := ""
	var sound_value: Variant = fields.get("Sound")
	if typeof(sound_value) == TYPE_DICTIONARY:
		var sound := sound_value as Dictionary
		if String(sound.get("phase", "")).to_upper() == "INITIAL":
			sound_event = String(sound.get("event", ""))
	row["ship_slow_death"] = {
		"death_types": String(fields.get("deathTypes", "ALL")),
		"included_death_types": (fields.get("includedDeathTypes", []) as Array).duplicate(),
		"excluded_death_types": (fields.get("excludedDeathTypes", []) as Array).duplicate(),
		"sink_delay_ticks": sim._ship_contract_delay_ticks(float(sink_delay)),
		"destruction_delay_ticks": sim._ship_contract_delay_ticks(float(destruction)),
		"sink_rate_source_per_second": float(sink_rate),
		"sound_event": sound_event,
		"tag": String(contract.get("tag", "")),
		"line": int(contract.get("line", 0)),
	}


func _attach_slow_death_core_contract(row: Dictionary, contract: Dictionary) -> void:
	## Runtime promotion is row-bounded, not kind-wide. The importer deliberately
	## leaves the kind deferred; only its independently evidence-closed effect
	## graph is admitted here. Authored gameplay side effects and ambiguous phase
	## variants remain attached evidence but cannot become a live policy.
	if String(contract.get("extraction", "")) != "typed":
		return
	var graph := contract.get("effect_graph", {}) as Dictionary
	var eligibility := graph.get("executionEligibility", {}) as Dictionary
	if (
		String(eligibility.get("status", "")) != "evidence-closed-core"
		or not (eligibility.get("blockers", []) as Array).is_empty()
	):
		return
	var fields := contract.get("fields", {}) as Dictionary
	for forbidden in ["OCL", "Weapon", "DoNotRandomizeMidpoint"]:
		if fields.has(forbidden):
			return
	for phase_field in ["FX", "Sound"]:
		for phase_value in fields.get(phase_field, []) as Array:
			if (
				typeof(phase_value) != TYPE_DICTIONARY
				or String((phase_value as Dictionary).get("phase", "")).to_upper()
				== "HIT_GROUND"
			):
				return
	var mode := String(fields.get("deathTypes", "ALL")).to_upper()
	if mode not in ["ALL", "NONE"]:
		return
	var probability := int(sim._module_contract_value(fields, "ProbabilityModifier", 10))
	var sink_delay_ms := float(sim._module_contract_value(fields, "SinkDelay", 0.0))
	var sink_rate := float(sim._module_contract_value(fields, "SinkRate", 0.0))
	var destruction_delay_ms := float(
		sim._module_contract_value(fields, "DestructionDelay", 0.0)
	)
	if probability < 0 or sink_delay_ms < 0.0 or destruction_delay_ms < 0.0:
		return
	var presentation_receipts: Array[Dictionary] = []
	for phase_field in ["FX", "Sound"]:
		for phase_value in fields.get(phase_field, []) as Array:
			var phase_row := phase_value as Dictionary
			presentation_receipts.append({
				"kind": phase_field,
				"phase": String(phase_row.get("phase", "")).to_upper(),
				"references": (phase_row.get("references", []) as Array).duplicate(),
				"runtime_status": "deferred-presentation",
				"source_ini": String(phase_row.get("sourceIni", "")),
				"line": int(phase_row.get("line", 0)),
			})
	for presentation_field in [
		"DeathFlags", "DecayBeginTime", "FadeDelay", "FadeTime", "ShadowWhenDead"
	]:
		if fields.has(presentation_field):
			presentation_receipts.append({
				"kind": presentation_field,
				"phase": "PRESENTATION",
				"runtime_status": "deferred-presentation",
			})
	var policies: Array = row.get("slow_death_core_contracts", []) as Array
	policies.append({
		"death_types": mode,
		"included_death_types": (fields.get("includedDeathTypes", []) as Array).duplicate(),
		"excluded_death_types": (fields.get("excludedDeathTypes", []) as Array).duplicate(),
		"probability_weight": probability,
		"sink_delay_ms": sink_delay_ms,
		"sink_rate_source_per_second": sink_rate,
		"destruction_delay_ms": destruction_delay_ms,
		"presentation_receipts": presentation_receipts,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("source_ini", "")),
		"line": int(contract.get("line", 0)),
	})
	row["slow_death_core_contracts"] = policies


func load_transport_entity(carrier_id: int, entity_id: int, manual_pickup: bool = false) -> Dictionary:
	if not sim.structures.has(carrier_id) or not sim.entities.has(entity_id):
		return {"ok": false, "reason": "carrier-or-passenger-missing"}
	var carrier := sim.structures[carrier_id] as Dictionary
	if not carrier.has("horde_transport"):
		sim._attach_structure_module_contracts(carrier)
	if not carrier.has("horde_transport"):
		return {"ok": false, "reason": "typed-horde-transport-contract-missing"}
	if int(carrier.get("health", 0)) <= 0:
		return {"ok": false, "reason": "carrier-dead"}
	if sim.entity_container.has(entity_id):
		return {"ok": false, "reason": "already-contained"}
	var policy := carrier["horde_transport"] as Dictionary
	var occupied_slots = sim.passenger_count(carrier_id)
	if String(policy.get("module", "")) == "SiegeEngineContain":
		occupied_slots -= (carrier.get("siege_crew_ids", []) as Array).size()
	if occupied_slots >= int(carrier.get("transport_capacity", 0)):
		return {"ok": false, "reason": "capacity-full"}
	var passenger := sim.entities[entity_id] as Dictionary
	var relation = sim.team_relationship(int(carrier.get("team", -1)), int(passenger.get("team", -2)))
	var relationship_allowed = (
		(relation == "local" and (bool(policy.get("allow_own", false)) or bool(policy.get("allow_allies", false))))
		or (relation == "allied" and bool(policy.get("allow_allies", false)))
		or (relation == "enemy" and bool(policy.get("allow_enemies", false)))
		or (relation == "unavailable" and bool(policy.get("allow_neutral", false)))
	)
	if not relationship_allowed:
		return {"ok": false, "reason": "relationship-refused:%s" % relation}
	if not _transport_filter_accepts(passenger, policy.get("passenger_filter", []) as Array):
		return {"ok": false, "reason": "passenger-filter-refused"}
	var manual_filter := policy.get("manual_pickup_filter", []) as Array
	if manual_pickup and not manual_filter.is_empty() and not _transport_filter_accepts(passenger, manual_filter):
		return {"ok": false, "reason": "manual-pickup-filter-refused"}
	var result = sim.contain_entity(carrier_id, entity_id)
	if not bool(result.get("ok", false)):
		return result
	passenger["transport_prior_status"] = (passenger.get("object_status", {}) as Dictionary).duplicate(true)
	passenger["transport_prior_vision_range"] = float(passenger.get("vision_range", 0.0))
	var statuses := passenger.get("object_status", {}) as Dictionary
	for status in policy.get("contained_statuses", []) as Array:
		statuses[String(status)] = true
	passenger["object_status"] = statuses
	passenger["transport_bone"] = _transport_bone_for(passenger, policy.get("passenger_bones", []) as Array)
	var garrison_module := String(policy.get("module", "")) in ["GarrisonContain", "HordeGarrisonContain"]
	passenger["position"] = Vector2(carrier.get("position", Vector2.ZERO)) if garrison_module else Vector2(carrier.get("position", Vector2.ZERO)) + sim._retail_source_to_sim_offset(Vector2(policy.get("entry_offset_source", Vector2.ZERO)))
	passenger["presentation_hidden"] = true
	passenger["contained_can_attack"] = garrison_module and (policy.get("contained_statuses", []) as Array).has("CAN_ATTACK")
	if garrison_module:
		var tower_vision_source := float(policy.get("tower_vision_range_source", 0.0))
		if tower_vision_source > 0.0:
			passenger["vision_range"] = tower_vision_source * sim.source_transform_scale()
	if int(carrier.get("team", -1)) == sim.CASTLE_CIVILIAN_TEAM and bool(policy.get("allow_neutral", false)):
		carrier["team"] = int(passenger.get("team", -1))
		sim._emit_event("garrison.neutral_captured", entity_id, carrier_id, {"team": int(carrier.get("team", -1))})
	if bool(policy.get("force_orientation", false)):
		passenger["facing"] = carrier.get("facing", Vector2.ZERO)
	if bool(policy.get("fade_on_enter", false)) and _transport_filter_accepts(passenger, policy.get("fade_filter", []) as Array):
		passenger["transport_fade_until_tick"] = sim.tick_index + int(policy.get("enter_fade_ticks", 0))
	var carrier_object_id := String(carrier.get("source_object_id", ""))
	var passenger_object_id := String(passenger.get("object_id", ""))
	sim._emit_event("transport.enter", carrier_id, entity_id, {
		"sound": String(policy.get("enter_sound", "")),
		"carrier_object_id": carrier_object_id,
		"passenger_object_id": passenger_object_id,
		"voice_candidates": _transport_entry_voice_candidates(passenger_object_id, carrier_object_id),
	})
	_update_container_weapon_state(carrier_id)
	return {"ok": true, "reason": "", "bone": String(passenger.get("transport_bone", ""))}


func issue_garrison(team: int, entity_id: int, structure_id: int) -> Dictionary:
	if not sim.entities.has(entity_id) or int((sim.entities[entity_id] as Dictionary).get("team", -1)) != team:
		return {"ok": false, "reason": "not-owned"}
	return load_transport_entity(structure_id, entity_id)


func issue_exit_garrison(team: int, entity_id: int) -> Dictionary:
	if not sim.entities.has(entity_id) or int((sim.entities[entity_id] as Dictionary).get("team", -1)) != team:
		return {"ok": false, "reason": "not-owned"}
	if not sim.entity_container.has(entity_id):
		return {"ok": false, "reason": "not-contained"}
	var carrier_id := int(sim.entity_container[entity_id])
	_finish_transport_exit(carrier_id, entity_id)
	return {"ok": true, "reason": ""}


func _transport_entry_voice_candidates(passenger_object_id: String, carrier_object_id: String) -> Array[String]:
	if passenger_object_id == "" or carrier_object_id == "":
		return []
	var runtimes_value: Variant = sim._rules.get("playable_unit_runtimes", {})
	if typeof(runtimes_value) != TYPE_DICTIONARY:
		return []
	var runtimes := runtimes_value as Dictionary
	var folded := passenger_object_id.to_lower()
	var keys: Array[String] = []
	for key_value in runtimes.keys():
		keys.append(String(key_value))
	keys.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	for key in keys:
		var document_value: Variant = runtimes.get(key)
		if typeof(document_value) != TYPE_DICTIONARY:
			continue
		var document := document_value as Dictionary
		if (
			key.to_lower() != folded
			and String(document.get("objectId", "")).to_lower() != folded
			and sim.PlayableUnitAdapter.runtime_unit_id(document).to_lower() != folded
			and sim.PlayableUnitAdapter.runtime_member_id(document).to_lower() != folded
		):
			continue
		return sim.PlayableUnitAdapter.transport_entry_audio_event_ids(document, carrier_object_id)
	return []


func load_siege_crew(carrier_id: int, entity_id: int) -> Dictionary:
	if not sim.structures.has(carrier_id) or not sim.entities.has(entity_id):
		return {"ok": false, "reason": "carrier-or-crew-missing"}
	var carrier := sim.structures[carrier_id] as Dictionary
	var policy := carrier.get("horde_transport", {}) as Dictionary
	if String(policy.get("module", "")) != "SiegeEngineContain":
		return {"ok": false, "reason": "typed-siege-engine-contract-missing"}
	var crew_ids := carrier.get("siege_crew_ids", []) as Array
	if crew_ids.size() >= int(policy.get("crew_max", 0)):
		return {"ok": false, "reason": "crew-capacity-full"}
	var crew := sim.entities[entity_id] as Dictionary
	if not _transport_filter_accepts(crew, policy.get("crew_filter", []) as Array):
		return {"ok": false, "reason": "crew-filter-refused"}
	var result = sim.contain_entity(carrier_id, entity_id)
	if not bool(result.get("ok", false)):
		return result
	crew["transport_prior_status"] = (crew.get("object_status", {}) as Dictionary).duplicate(true)
	var statuses := crew.get("object_status", {}) as Dictionary
	for status in policy.get("crew_statuses", []) as Array:
		statuses[String(status)] = true
	crew["object_status"] = statuses
	crew["position"] = carrier.get("position", Vector2.ZERO)
	crew_ids.append(entity_id)
	crew_ids.sort()
	carrier["siege_crew_ids"] = crew_ids
	_update_siege_crew_state(carrier_id)
	return {"ok": true, "reason": ""}


func _update_siege_crew_state(carrier_id: int) -> void:
	if not sim.structures.has(carrier_id):
		return
	var carrier := sim.structures[carrier_id] as Dictionary
	var policy := carrier.get("horde_transport", {}) as Dictionary
	var live: Array = []
	for value in carrier.get("siege_crew_ids", []) as Array:
		var crew_id := int(value)
		if sim.entity_container.get(crew_id, -1) == carrier_id and sim.entities.has(crew_id) and int((sim.entities[crew_id] as Dictionary).get("health", 0)) > 0:
			live.append(crew_id)
	carrier["siege_crew_ids"] = live
	carrier["siege_crew_count"] = live.size()
	carrier["siege_speed_multiplier"] = clampf(float(policy.get("speed_fraction_per_crew", 0.0)) * live.size(), 0.0, 1.0) if float(policy.get("speed_fraction_per_crew", 0.0)) > 0.0 else 1.0


func _transport_filter_accepts(row: Dictionary, filter: Array) -> bool:
	if filter.is_empty():
		return false
	var traits: Dictionary = {_transport_filter_key(String(row.get("category", ""))): true}
	for identity_key in ["object_id", "unit_type", "name"]:
		var identity := _transport_filter_key(String(row.get(identity_key, "")))
		if identity != "":
			traits[identity] = true
	for kind_value in row.get("kind_of", []) as Array:
		traits[_transport_filter_key(String(kind_value))] = true
	var accepted := filter.has("ANY") or filter.has("ALL")
	for token_value in filter:
		var token := String(token_value).to_upper()
		if token.begins_with("-") and traits.has(_transport_filter_key(token.substr(1))):
			return false
		if token.begins_with("+") and traits.has(_transport_filter_key(token.substr(1))):
			accepted = true
	return accepted


func _transport_filter_key(value: String) -> String:
	var leaf := value.get_slice(".", value.get_slice_count(".") - 1)
	return leaf.to_pascal_case().to_upper()


func _transport_bone_for(passenger: Dictionary, bones: Array) -> String:
	for bone_value in bones:
		var bone := bone_value as Dictionary
		if _transport_filter_accepts(passenger, ["+" + String(bone.get("kindOf", ""))]):
			return String(bone.get("passengerBone", ""))
	return ""


func request_transport_exit(entity_id: int) -> Dictionary:
	if not sim.entity_container.has(entity_id):
		return {"ok": false, "reason": "not-contained"}
	var carrier_id := int(sim.entity_container[entity_id])
	if not sim.structures.has(carrier_id):
		return {"ok": false, "reason": "carrier-missing"}
	var carrier := sim.structures[carrier_id] as Dictionary
	var policy := carrier.get("horde_transport", {}) as Dictionary
	var pending := carrier.get("transport_pending_exits", {}) as Dictionary
	pending[entity_id] = sim.tick_index + int(policy.get("exit_delay_ticks", 0))
	carrier["transport_pending_exits"] = pending
	return {"ok": true, "reason": "", "exit_tick": int(pending[entity_id])}


func request_tunnel_exit(entity_id: int, destination_id: int) -> Dictionary:
	if not sim.entity_container.has(entity_id):
		return {"ok": false, "reason": "not-contained"}
	var source_id := int(sim.entity_container[entity_id])
	if not sim.structures.has(source_id) or not sim.structures.has(destination_id):
		return {"ok": false, "reason": "tunnel-missing"}
	var source := sim.structures[source_id] as Dictionary
	var destination := sim.structures[destination_id] as Dictionary
	var source_policy := source.get("horde_transport", {}) as Dictionary
	var destination_policy := destination.get("horde_transport", {}) as Dictionary
	if String(source_policy.get("module", "")) != "TunnelContain" or String(destination_policy.get("module", "")) != "TunnelContain":
		return {"ok": false, "reason": "typed-tunnel-contract-missing"}
	if int(source.get("team", -1)) != int(destination.get("team", -2)) or int(destination.get("health", 0)) <= 0:
		return {"ok": false, "reason": "tunnel-destination-unavailable"}
	var exit_tick = sim.tick_index + int(source_policy.get("exit_delay_ticks", 0))
	var pending := source.get("transport_pending_exits", {}) as Dictionary
	pending[entity_id] = {"tick": exit_tick, "destination_id": destination_id}
	source["transport_pending_exits"] = pending
	return {"ok": true, "reason": "", "exit_tick": exit_tick}


func _step_ship_runtime() -> void:
	var carrier_ids = sim.structures.keys()
	carrier_ids.sort()
	for carrier_id_value in carrier_ids:
		var carrier_id := int(carrier_id_value)
		if not sim.structures.has(carrier_id):
			continue
		var carrier := sim.structures[carrier_id] as Dictionary
		var pending := carrier.get("transport_pending_exits", {}) as Dictionary
		var passenger_ids := pending.keys()
		passenger_ids.sort()
		for entity_id_value in passenger_ids:
			var entity_id := int(entity_id_value)
			var pending_value: Variant = pending[entity_id]
			var exit_tick := int((pending_value as Dictionary).get("tick", 0)) if typeof(pending_value) == TYPE_DICTIONARY else int(pending_value)
			var destination_id := int((pending_value as Dictionary).get("destination_id", carrier_id)) if typeof(pending_value) == TYPE_DICTIONARY else carrier_id
			if sim.tick_index >= exit_tick:
				_finish_transport_exit(carrier_id, entity_id, destination_id)
				pending.erase(entity_id)
		if pending.is_empty():
			carrier.erase("transport_pending_exits")
		else:
			carrier["transport_pending_exits"] = pending
		if int(carrier.get("health", 0)) <= 0 and not bool(carrier.get("container_death_resolved", false)):
			_resolve_container_death(carrier_id, carrier)
		if String(carrier.get("ship_death_phase", "")) != "sinking":
			continue
		if sim.tick_index >= int(carrier.get("ship_sink_start_tick", 0)):
			carrier["height_source"] = float(carrier.get("height_source", 0.0)) - float(carrier.get("ship_sink_rate_source_per_second", 0.0)) * sim.TICK_SECONDS
		if sim.tick_index >= int(carrier.get("ship_destroy_tick", 0)):
			for passenger_value in (sim.containment.get(carrier_id, []) as Array).duplicate():
				_finish_transport_exit(carrier_id, int(passenger_value))
			sim.structures.erase(carrier_id)
			sim._emit_event("ship.destroyed", carrier_id, 0)


func _finish_transport_exit(carrier_id: int, entity_id: int, destination_id: int = -1) -> void:
	if not sim.entities.has(entity_id):
		sim.exit_entity_container(entity_id)
		return
	var passenger := sim.entities[entity_id] as Dictionary
	var carrier := sim.structures.get(carrier_id, {}) as Dictionary
	var policy := carrier.get("horde_transport", {}) as Dictionary
	var was_siege_crew := (carrier.get("siege_crew_ids", []) as Array).has(entity_id)
	sim.exit_entity_container(entity_id)
	passenger["object_status"] = (passenger.get("transport_prior_status", {}) as Dictionary).duplicate(true)
	passenger.erase("transport_prior_status")
	passenger.erase("transport_bone")
	passenger.erase("presentation_hidden")
	passenger.erase("contained_can_attack")
	passenger["vision_range"] = float(passenger.get("transport_prior_vision_range", passenger.get("vision_range", 0.0)))
	passenger.erase("transport_prior_vision_range")
	var destination := sim.structures.get(destination_id, carrier) as Dictionary
	var destination_policy := destination.get("horde_transport", policy) as Dictionary
	passenger["position"] = Vector2(destination.get("position", passenger.get("position", Vector2.ZERO))) + sim._retail_source_to_sim_offset(Vector2(destination_policy.get("exit_offset_source", Vector2.ZERO)))
	if bool(policy.get("fade_on_exit", false)) and _transport_filter_accepts(passenger, policy.get("fade_filter", []) as Array):
		passenger["transport_fade_until_tick"] = sim.tick_index + int(policy.get("exit_fade_ticks", 0))
	if was_siege_crew:
		var crew_ids := carrier.get("siege_crew_ids", []) as Array
		crew_ids.erase(entity_id)
		carrier["siege_crew_ids"] = crew_ids
		_update_siege_crew_state(carrier_id)
	if bool(policy.get("go_aggressive_on_exit", false)):
		passenger["stance"] = "Aggressive"
	sim._emit_event("transport.exit", carrier_id, entity_id, {"sound": String(policy.get("exit_sound", ""))})
	_update_container_weapon_state(carrier_id)


func _resolve_container_death(carrier_id: int, carrier: Dictionary) -> void:
	carrier["container_death_resolved"] = true
	var policy := carrier.get("horde_transport", {}) as Dictionary
	for passenger_value in (sim.containment.get(carrier_id, []) as Array).duplicate():
		var passenger_id := int(passenger_value)
		if bool(policy.get("kill_passengers_on_death", false)):
			_apply_transport_passenger_damage(carrier_id, passenger_id, 2147483647)
			sim.exit_entity_container(passenger_id)
		elif bool(policy.get("eject_passengers_on_death", false)):
			_finish_transport_exit(carrier_id, passenger_id)


func _update_container_weapon_state(carrier_id: int) -> void:
	if not sim.structures.has(carrier_id):
		return
	var carrier := sim.structures[carrier_id] as Dictionary
	var policy := carrier.get("horde_transport", {}) as Dictionary
	var count = sim.passenger_count(carrier_id)
	var states := policy.get("weapon_states_two", []) as Array if count >= 2 else policy.get("weapon_states_one", []) as Array
	var sets := policy.get("weapon_sets_two", []) as Array if count >= 2 else policy.get("weapon_sets_one", []) as Array
	carrier["contained_weapon_states"] = states.duplicate()
	carrier["contained_weapon_sets"] = sets.duplicate()
	var bone_states: Array[String] = []
	for value in policy.get("bone_condition_states", []) as Array:
		var row := value as Dictionary
		if int(row.get("boneIndex", 0)) <= count:
			bone_states.append(String(row.get("conditionState", "")))
	carrier["contained_bone_condition_states"] = bone_states


func _apply_transport_passenger_damage(attacker_id: int, passenger_id: int, amount: int) -> void:
	## HordeTransportContain forwards a scalar portion of hull damage. Apply it
	## deterministically across living member slots without running another armor
	## pass: DamagePercentToUnits describes the already-resolved hull hit.
	if amount <= 0 or not sim.entities.has(passenger_id):
		return
	var passenger := sim.entities[passenger_id] as Dictionary
	var member_health: Array = passenger.get("member_health", []) as Array
	var remaining := amount
	for member_index in member_health.size():
		if remaining <= 0:
			break
		var prior := int(member_health[member_index])
		if prior <= 0:
			continue
		var consumed := mini(prior, remaining)
		member_health[member_index] = prior - consumed
		remaining -= consumed
	passenger["member_health"] = member_health
	var total := 0
	for health_value in member_health:
		total += maxi(0, int(health_value))
	passenger["health"] = total
	if total <= 0:
		var no_defeated_members: Array[int] = []
		sim._apply_playable_unit_death_policy(passenger, "NORMAL", no_defeated_members)
		sim._emit_event("transport.passenger_killed", attacker_id, passenger_id)


func _begin_ship_slow_death(carrier_id: int, carrier: Dictionary, death_type: String) -> bool:
	var policy := carrier.get("ship_slow_death", {}) as Dictionary
	if policy.is_empty() or not sim._death_mux_matches(policy, death_type):
		return false
	carrier["ship_death_phase"] = "sinking"
	carrier["ship_sink_start_tick"] = sim.tick_index + int(policy.get("sink_delay_ticks", 0))
	carrier["ship_destroy_tick"] = sim.tick_index + int(policy.get("destruction_delay_ticks", 0))
	carrier["ship_sink_rate_source_per_second"] = float(policy.get("sink_rate_source_per_second", 0.0))
	var sound_event := String(policy.get("sound_event", ""))
	sim._emit_event("ship.sinking", carrier_id, 0, {"sound": sound_event})
	var transport := carrier.get("horde_transport", {}) as Dictionary
	for passenger_value in (sim.containment.get(carrier_id, []) as Array).duplicate():
		var passenger_id := int(passenger_value)
		if bool(transport.get("kill_passengers_on_death", false)) and sim.entities.has(passenger_id):
			_apply_transport_passenger_damage(carrier_id, passenger_id, 2147483647)
			sim.exit_entity_container(passenger_id)
		elif bool(transport.get("eject_passengers_on_death", false)):
			_finish_transport_exit(carrier_id, passenger_id)
	return true

