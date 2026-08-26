extends "res://src/retail_slice/retail_sim_subsystem.gd"
## One-Ring mechanic subsystem carved out of retail_slice_sim.gd (drawer 16): ring runtime contract, Gollum spawn/step, ring drop/pickup, delivery sim.structures, presentation contract.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func _configure_ring_mechanic_contract() -> void:
	var _sim = sim
	_sim._ring_contract = (_sim._rules.get("ring_system", {}) as Dictionary).duplicate(true)
	if String(_sim._ring_contract.get("schema", "")) == "openbfme.ring-system-runtime":
		var compiled_contract = _compiled_ring_runtime_contract(_sim._ring_contract)
		if compiled_contract.is_empty():
			_sim.configuration_error = "Compiled ring-system runtime registration is invalid"
		else:
			_sim._ring_contract = compiled_contract
	for registry_key in ["ring_runtime_documents", "playable_unit_runtimes"]:
		var registry: Variant = _sim._rules.get(registry_key, {})
		if typeof(registry) != TYPE_DICTIONARY:
			continue
		for document_value in (registry as Dictionary).values():
			if typeof(document_value) != TYPE_DICTIONARY:
				continue
			var mechanic: Dictionary = (document_value as Dictionary).get("ringMechanic", {}) as Dictionary
			for block_name in ["gollum", "ring"]:
				var block: Variant = mechanic.get(block_name, {})
				if typeof(block) == TYPE_DICTIONARY:
					_sim._ring_contract.merge(block as Dictionary, false)
					if not (block as Dictionary).is_empty():
						var document_object_id := String((document_value as Dictionary).get("objectId", ""))
						if document_object_id != "":
							_sim._ring_contract["gollumObjectId" if block_name == "gollum" else "ringObjectId"] = document_object_id
						if block_name == "ring" and (block as Dictionary).has("scanRange"):
							_sim._ring_contract["pickupRange"] = float((block as Dictionary)["scanRange"])
	_sim._ring_delivery_kinds.clear()
	for kind_value in (_sim._rules.get("ring_delivery_structure_kinds", []) as Array):
		_sim._ring_delivery_kinds[String(kind_value)] = true
	var producer_kinds: Dictionary = _sim._rules.get("producer_kind_by_source_object", {}) as Dictionary
	var structure_runtimes: Variant = _sim._rules.get("playable_structure_runtimes", {})
	if typeof(structure_runtimes) == TYPE_DICTIONARY:
		for document_value in (structure_runtimes as Dictionary).values():
			if typeof(document_value) != TYPE_DICTIONARY:
				continue
			var document := document_value as Dictionary
			var delivery: Variant = (document.get("ringMechanic", {}) as Dictionary).get("delivery", {})
			if typeof(delivery) != TYPE_DICTIONARY or (delivery as Dictionary).is_empty():
				continue
			var kind := String(producer_kinds.get(String(document.get("objectId", "")), ""))
			if kind != "":
				_sim._ring_delivery_kinds[kind] = (delivery as Dictionary).duplicate(true)
	if _sim.ring_mechanic_enabled and _sim._ring_contract.is_empty():
		_sim._ring_contract = {
			"waypointFamily": "SpawnPoint_SkirmishGollum_", "spawnTeam": _sim.CREEP_TEAM,
			"modeToken": "ringheroes", "gollumObjectId": _sim.RING_DEFAULT_GOLLUM,
			"ringObjectId": _sim.RING_DEFAULT_ITEM, "pickupRange": 10.0,
			"deliveryRange": 10.0, "status": "HOLDING_THE_RING",
		}
		print("[RetailSliceSim] RING_CONTRACT_LIMITATION stale-pack-no-data-ring-system; using named retail constants until data/ring/system.json is shipped")


func _compiled_ring_runtime_contract(runtime: Dictionary) -> Dictionary:
	## Consume the importer's canonical openbfme.ring-system-runtime envelope.
	## This is intentionally a projection of its registration, not a second
	## hand-authored Gollum table in the sim.
	if int(runtime.get("schemaVersion", -1)) != 0:
		return {}
	var registration_value: Variant = runtime.get("registration", {})
	if typeof(registration_value) != TYPE_DICTIONARY:
		return {}
	var registration := registration_value as Dictionary
	var system_value: Variant = registration.get("system", {})
	var objects_value: Variant = registration.get("objects", {})
	if typeof(system_value) != TYPE_DICTIONARY or typeof(objects_value) != TYPE_DICTIONARY:
		return {}
	var system := system_value as Dictionary
	var objects := objects_value as Dictionary
	var spawn_value: Variant = system.get("spawn", {})
	if typeof(spawn_value) != TYPE_DICTIONARY:
		return {}
	var spawn := spawn_value as Dictionary
	var gollum_id := String(spawn.get("objectId", ""))
	var gollum_value: Variant = objects.get(gollum_id, {})
	if gollum_id == "" or typeof(gollum_value) != TYPE_DICTIONARY:
		return {}
	var gollum := gollum_value as Dictionary
	var parent_id := String(gollum.get("parentObjectId", ""))
	var parent_value: Variant = objects.get(parent_id, {})
	if parent_id == "" or typeof(parent_value) != TYPE_DICTIONARY:
		return {}
	var parent := parent_value as Dictionary
	var locomotors_value: Variant = parent.get("locomotors", {})
	var body_value: Variant = parent.get("body", {})
	var animal_value: Variant = gollum.get("animalAI", {})
	if typeof(locomotors_value) != TYPE_DICTIONARY or typeof(body_value) != TYPE_DICTIONARY \
			or typeof(animal_value) != TYPE_DICTIONARY:
		return {}
	var locomotors := locomotors_value as Dictionary
	var body := body_value as Dictionary
	var animal := animal_value as Dictionary
	if float(locomotors.get("normal", 0.0)) <= 0.0 or int(body.get("maxHealth", 0)) <= 0:
		return {}
	var spawn_team_value: Variant = spawn.get("team", "")
	var spawn_team = sim.CREEP_TEAM if String(spawn_team_value) == "PlyrCreeps" else int(spawn_team_value) if typeof(spawn_team_value) == TYPE_INT else -1
	if spawn_team < 0:
		return {}
	var contract := {
		"waypointFamily": String(spawn.get("waypointFamily", "")),
		"spawnTeam": spawn_team,
		"modeToken": String(system.get("modeToken", "")),
		"gollumObjectId": gollum_id,
		"ringObjectId": "TheDroppedRing",
		"evaEvents": {},
		"evaEventCatalog": Array(system.get("evaEvents", [])).duplicate(),
		"heroesByFaction": (system.get("ringHeroesByFaction", {}) as Dictionary).duplicate(true),
		"wanderPercentage": int(animal.get("wanderPercentage", 0)),
		"detectionRange": float((parent.get("camouflage", {}) as Dictionary).get("detectionRange", 0.0)),
		"fleeEnemyRange": float(animal.get("fleeRange", 0.0)),
		"fleeDistance": float(animal.get("fleeDistance", 0.0)),
		"_compiledRegistration": registration.duplicate(true),
	}
	var ring_value: Variant = objects.get("TheDroppedRing", {})
	if typeof(ring_value) == TYPE_DICTIONARY:
		var attach_value: Variant = ((ring_value as Dictionary).get("ringMechanic", {}) as Dictionary).get("attach", {})
		if typeof(attach_value) == TYPE_DICTIONARY:
			var attach := attach_value as Dictionary
			contract["pickupRange"] = float(attach.get("scanRange", 0.0))
			contract["status"] = String(attach.get("parentStatus", ""))
			contract["attachFilter"] = (attach.get("filter", {}) as Dictionary).duplicate(true)
	return contract


func _ring_state() -> Dictionary:
	var _sim = sim
	if not _sim.script_surface_bag.has("ring_mechanic") \
			or typeof(_sim.script_surface_bag["ring_mechanic"]) != TYPE_DICTIONARY:
		_sim.script_surface_bag["ring_mechanic"] = {
			"gollum_id": 0, "gollum_spawned": false, "ring_active": false,
			"ring_position": Vector2.ZERO, "carrier_id": 0,
		}
	return _sim.script_surface_bag["ring_mechanic"] as Dictionary


func _mark_ring_delivery_structures() -> void:
	var _sim = sim
	for structure_id in _sim.structure_ids():
		_mark_ring_delivery_structure(_sim.structures[structure_id] as Dictionary)


func _mark_ring_delivery_structure(row: Dictionary) -> void:
	var _sim = sim
	if not _sim.ring_mechanic_enabled:
		return
	var kind := String(row.get("structure_kind", ""))
	if _sim._ring_delivery_kinds.has(kind):
		row["ring_delivery"] = (_sim._ring_delivery_kinds[kind] as Dictionary).duplicate(true) \
			if typeof(_sim._ring_delivery_kinds[kind]) == TYPE_DICTIONARY else {}


func _ring_gollum_object_id() -> String:
	var _sim = sim
	return String(_sim._ring_contract.get("gollumObjectId", _sim.RING_DEFAULT_GOLLUM))


func _ring_spawn_team() -> int:
	var _sim = sim
	var value: Variant = _sim._ring_contract.get("spawnTeam", _sim.CREEP_TEAM)
	return int(value) if typeof(value) in [TYPE_INT, TYPE_FLOAT] else _sim.CREEP_TEAM


func _ring_eva(name: String) -> String:
	return String((sim._ring_contract.get("evaEvents", {}) as Dictionary).get(name, ""))


func ring_presentation_contract() -> Dictionary:
	var _sim = sim
	var offset_value: Variant = _sim._ring_contract.get("carrierOffsetSource", Vector2(0.0, -10.0))
	var offset := Vector2(0.0, -10.0)
	if typeof(offset_value) == TYPE_VECTOR2:
		offset = offset_value as Vector2
	elif typeof(offset_value) == TYPE_ARRAY and (offset_value as Array).size() >= 2:
		offset = Vector2(float((offset_value as Array)[0]), float((offset_value as Array)[1]))
	elif typeof(offset_value) == TYPE_DICTIONARY:
		var offset_row := offset_value as Dictionary
		offset = Vector2(float(offset_row.get("x", 0.0)), float(offset_row.get("y", -10.0)))
	return {
		"enabled": _sim.ring_mechanic_enabled,
		"object_id": String(_sim._ring_contract.get("ringObjectId", _sim.RING_DEFAULT_ITEM)),
		"status": String(_sim._ring_contract.get("status", "HOLDING_THE_RING")),
		"offset_source": offset,
	}


func _is_ring_gollum(row: Dictionary) -> bool:
	var wanted := _ring_gollum_object_id()
	return String(row.get("object_id", "")) == wanted \
		or String(row.get("unit_type", "")) == wanted \
		or String(row.get("source_object_id", "")) == wanted


func _existing_ring_gollum_id() -> int:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		var row: Dictionary = _sim.entities[entity_id]
		if _is_ring_gollum(row) and int(row.get("health", 0)) > 0:
			return entity_id
	return 0


func _spawn_ring_gollum_fallback() -> void:
	var _sim = sim
	var state := _ring_state()
	if bool(state.get("gollum_spawned", false)):
		return
	var existing := _existing_ring_gollum_id()
	if existing != 0:
		state["gollum_id"] = existing
		state["gollum_spawned"] = true
		return
	var family = String(_sim._ring_contract.get("waypointFamily", "SpawnPoint_SkirmishGollum_"))
	var rolled = _sim.logic_random_int(1, 8)
	var waypoint := "%s%d" % [family, rolled]
	var at: Vector2
	if _sim.script_waypoints.has(waypoint):
		at = Vector2(_sim.script_waypoints[waypoint])
	else:
		var candidates: Array[String] = []
		for name_value in _sim.script_waypoints.keys():
			if String(name_value).begins_with(family):
				candidates.append(String(name_value))
		candidates.sort()
		if not candidates.is_empty():
			waypoint = candidates[posmod(rolled - 1, candidates.size())]
			at = Vector2(_sim.script_waypoints[waypoint])
		else:
			waypoint = "fallback-map-centre"
			at = Vector2.ZERO
	push_warning("RING_GOLLUM_FALLBACK scripted spawn absent after tick %d; deterministic fallback waypoint=%s roll=%d" % [_sim.RING_FALLBACK_TICK, waypoint, rolled])
	var gollum_id = _sim.spawn_script_object(
		_ring_gollum_object_id(), _ring_spawn_team(), at, true
	)
	if gollum_id <= 0:
		push_error("RING_GOLLUM_FALLBACK_FAILED stale pack lacks playable Gollum rule (%s)" % _ring_gollum_object_id())
		state["gollum_spawned"] = true
		state["limitation"] = "stale-pack-no-playable-gollum"
		return
	state["gollum_id"] = gollum_id
	state["gollum_spawned"] = true
	_configure_ring_gollum(_sim.entities[gollum_id] as Dictionary)
	_sim._emit_event("ring.gollum_spawned", gollum_id, 0, {"waypoint": waypoint, "fallback": true})


func _configure_ring_gollum(row: Dictionary) -> void:
	var _sim = sim
	var source_scale = maxf(0.000001, float(_sim._rules.get("source_map_transform_scale", 1.0)))
	row["ring_gollum"] = true
	row["ring_wander_percentage"] = int(_sim._ring_contract.get("wanderPercentage", 80))
	row["ring_detection_range"] = float(_sim._ring_contract.get("detectionRange", 120.0)) * source_scale
	row["ring_flee_enemy_range"] = float(_sim._ring_contract.get("fleeEnemyRange", 300.0)) * source_scale
	row["ring_flee_distance"] = float(_sim._ring_contract.get("fleeDistance", 800.0)) * source_scale
	if not _sim._stealth_active(row):
		_sim._grant_stealth(row, 0x3FFFFFFF, [])


func _drop_ring(at: Vector2, source_id: int, reason: String) -> void:
	var _sim = sim
	var state := _ring_state()
	var old_carrier := int(state.get("carrier_id", 0))
	if old_carrier != 0 and _sim.entities.has(old_carrier):
		_sim.set_entity_object_status(old_carrier, String(_sim._ring_contract.get("status", "HOLDING_THE_RING")), false)
	state["ring_active"] = true
	state["ring_position"] = at
	state["carrier_id"] = 0
	_sim._emit_event("ring.dropped", source_id, 0, {"position": at, "reason": reason, "object_id": String(_sim._ring_contract.get("ringObjectId", _sim.RING_DEFAULT_ITEM)), "eva": _ring_eva("dropped")})


func _on_ring_entity_death(entity_id: int, row: Dictionary) -> void:
	if not sim.ring_mechanic_enabled:
		return
	var state := _ring_state()
	if entity_id == int(state.get("gollum_id", 0)) or bool(row.get("ring_gollum", false)):
		_drop_ring(Vector2(row.get("position", Vector2.ZERO)), entity_id, "gollum-killed")
	elif entity_id == int(state.get("carrier_id", 0)):
		_drop_ring(Vector2(row.get("position", Vector2.ZERO)), entity_id, "carrier-killed")
	elif bool(row.get("ring_hero", false)):
		_drop_ring(Vector2(row.get("position", Vector2.ZERO)), entity_id, "ring-hero-killed")


func _step_ring_gollum(gollum_id: int) -> void:
	var _sim = sim
	if not _sim.entities.has(gollum_id):
		return
	var row: Dictionary = _sim.entities[gollum_id]
	if int(row.get("health", 0)) <= 0:
		return
	var position := Vector2(row.get("position", Vector2.ZERO))
	var detect_range := float(row.get("ring_detection_range", 120.0))
	var gollum_team := int(row.get("team", _ring_spawn_team()))
	var detector = _sim._spatial_nearest_hostile(row, gollum_team, position, detect_range, 0, true)
	if detector != 0:
		if _sim._stealth_active(row):
			_sim._clear_stealth(row)
	else:
		if not _sim._stealth_active(row):
			_sim._grant_stealth(row, 0x3FFFFFFF, [])
	var flee_range := float(row.get("ring_flee_enemy_range", 300.0))
	var threat = _sim._spatial_nearest_hostile(row, gollum_team, position, flee_range, 0, true)
	if threat != 0:
		var away = Vector2((_sim.entities[threat] as Dictionary).get("position", position)).direction_to(position)
		if away.length_squared() < 0.000001:
			away = Vector2.RIGHT.rotated(float(posmod(gollum_id, 8)) * TAU / 8.0)
		var destination = position + away * float(row.get("ring_flee_distance", 800.0))
		if _sim._assign_route(row, destination):
			row["state"] = "run"
			row["ring_flee_target"] = threat
			_sim._emit_event("ring.gollum_flee", gollum_id, threat, {"destination": destination})
		return
	if not (row.get("route", []) as Array).is_empty() or (_sim.tick_index + gollum_id) % 20 != 0:
		return
	if _sim.logic_random_int(1, 100) > int(row.get("ring_wander_percentage", 80)):
		return
	var angle = TAU * float(_sim.logic_random_int(0, 359)) / 360.0
	var distance = float(_sim.logic_random_int(8, 24))
	if _sim._assign_route(row, position + Vector2.RIGHT.rotated(angle) * distance):
		row["state"] = "run"
		row["ring_wander_count"] = int(row.get("ring_wander_count", 0)) + 1
		_sim._emit_event("ring.gollum_wander", gollum_id, 0, {"destination": row.get("destination", position)})


func _step_ring_mechanic() -> void:
	var _sim = sim
	if not _sim.ring_mechanic_enabled:
		return
	var state := _ring_state()
	if not bool(state.get("gollum_spawned", false)):
		for entity_id in _sim.entity_ids():
			if _is_ring_gollum(_sim.entities[entity_id] as Dictionary):
				state["gollum_id"] = entity_id
				state["gollum_spawned"] = true
				_configure_ring_gollum(_sim.entities[entity_id] as Dictionary)
				_sim._emit_event("ring.gollum_spawned", entity_id, 0, {"fallback": false})
				break
	if not bool(state.get("gollum_spawned", false)) and _sim.tick_index >= _sim.RING_FALLBACK_TICK:
		_spawn_ring_gollum_fallback()
	var gollum_id := int(state.get("gollum_id", 0))
	if gollum_id != 0:
		_step_ring_gollum(gollum_id)
	var carrier_id := int(state.get("carrier_id", 0))
	if carrier_id != 0 and _sim.entities.has(carrier_id) and int((_sim.entities[carrier_id] as Dictionary).get("health", 0)) > 0:
		var carrier: Dictionary = _sim.entities[carrier_id]
		state["ring_position"] = Vector2(carrier.get("position", Vector2.ZERO))
		_sim._ensure_parity()
		for team in _sim._roster_team_ids():
			_sim.parity.fog_reveal(int(team), state["ring_position"], 1.0, true)
		for structure_id in _sim.structure_ids(int(carrier.get("team", -1))):
			var structure: Dictionary = _sim.structures[structure_id]
			if not structure.has("ring_delivery"):
				continue
			var delivery: Dictionary = structure.get("ring_delivery", {}) as Dictionary
			var range = float(delivery.get("scanRange", _sim._ring_contract.get("deliveryRange", 10.0))) * maxf(0.000001, float(_sim._rules.get("source_map_transform_scale", 1.0)))
			if Vector2(structure.get("position", Vector2.ZERO)).distance_to(Vector2(carrier.get("position", Vector2.ZERO))) <= range:
				var team := int(carrier.get("team", -1))
				var owned: Dictionary = _sim.team_upgrades.get(team, {}) as Dictionary
				owned["Upgrade_RingHero"] = true
				_sim.team_upgrades[team] = owned
				var completed: Array = structure.get("completed_upgrades", [])
				if not completed.has("Upgrade_FortressRingHero"):
					completed.append("Upgrade_FortressRingHero")
					completed.sort()
				structure["completed_upgrades"] = completed
				_sim.set_entity_object_status(carrier_id, String(_sim._ring_contract.get("status", "HOLDING_THE_RING")), false)
				state["ring_active"] = false
				state["carrier_id"] = 0
				_sim._emit_event("ring.delivered", carrier_id, structure_id, {"team": team, "eva": _ring_eva("delivered")})
				return
	if not bool(state.get("ring_active", false)) or int(state.get("carrier_id", 0)) != 0:
		return
	var ring_position := Vector2(state.get("ring_position", Vector2.ZERO))
	var pickup_range = float(_sim._ring_contract.get("pickupRange", 10.0)) * maxf(0.000001, float(_sim._rules.get("source_map_transform_scale", 1.0)))
	for entity_id in _sim.entity_ids():
		var candidate: Dictionary = _sim.entities[entity_id]
		if not _ring_pickup_eligible(candidate):
			continue
		if Vector2(candidate.get("position", Vector2.ZERO)).distance_to(ring_position) > pickup_range:
			continue
		state["ring_active"] = false
		state["carrier_id"] = entity_id
		_sim.set_entity_object_status(entity_id, String(_sim._ring_contract.get("status", "HOLDING_THE_RING")), true)
		_sim._emit_event("ring.picked_up", entity_id, 0, {"position": ring_position, "eva": _ring_eva("pickedUp")})
		_sim._emit_event("ring.carrier_revealed", entity_id, 0, {"teams": _sim._roster_team_ids()})
		break


func _ring_pickup_eligible(candidate: Dictionary) -> bool:
	## Narrow consumer for the converter's compiled ObjectFilter projection.
	## Unknown optional fields do not broaden admission; the mandatory retail
	## ground/Gollum exclusions are always applied.
	if int(candidate.get("health", 0)) <= 0 or bool(candidate.get("flying", false)) \
			or _is_ring_gollum(candidate):
		return false
	var filter: Dictionary = sim._ring_contract.get("attachFilter", {}) as Dictionary
	var object_id := String(candidate.get("object_id", ""))
	var category := String(candidate.get("category", ""))
	if (filter.get("excludedObjectIds", []) as Array).has(object_id):
		return false
	if (filter.get("excludedCategories", []) as Array).has(category):
		return false
	return true


