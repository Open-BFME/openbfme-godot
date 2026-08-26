extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Production subsystem extracted from retail_slice_sim.gd (Q81 strangler-
## fig extraction #7): queue/cancel/step production, production exits,
## command-point commitment. Verbatim move, compiler-guided sim. prefixes,
## pin-verified byte-identical.





func queue_unit(team: int, producer: int, unit_type: String = sim.SOLDIER_HORDE_ID) -> Dictionary:
	var _sim = sim
	if not _sim.base_loop_enabled or _sim.winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not _sim.structures.has(producer):
		return {"ok": false, "reason": "unknown-producer"}
	var building: Dictionary = _sim.structures[producer]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(building.get("health", 0)) <= 0 or float(building.get("construction_progress", 0.0)) < 1.0:
		return {"ok": false, "reason": "producer-unavailable"}
	if not Array(building.get("production", [])).has(unit_type):
		return {"ok": false, "reason": "unsupported-unit"}
	var production_rule: Dictionary = _sim._unit_production_rules.get(unit_type, {})
	if production_rule.is_empty():
		return {"ok": false, "reason": "unsupported-unit"}
	if bool(production_rule.get("is_ring_hero", false)) and not _sim.ring_mechanic_enabled:
		return {"ok": false, "reason": "ring-heroes-disabled"}
	if _sim.created_hero_owner_team(unit_type) not in [-1, team]:
		# Every peer registers every seat's created heroes so the rule tables
		# match; only the seat that made one may buy it.
		return {"ok": false, "reason": "created-hero-not-owned"}
	if String(production_rule.get("category", "")) == "hero" and _sim.hero_unavailable(team, unit_type):
		return {"ok": false, "reason": "hero-unavailable"}
	var missing_production_upgrade := production_gate_unsatisfied(
		unit_type,
		String(building.get("structure_kind", "")),
		Array(building.get("completed_upgrades", [])),
		(_sim.team_upgrades.get(team, {}) as Dictionary).keys(),
	)
	if missing_production_upgrade != "":
		return {"ok": false, "reason": "missing-upgrade", "required_upgrade": missing_production_upgrade}
	if not building.has("production_update") and not building.has("module_contracts"):
		_sim._attach_structure_module_contracts(building)
	var exit_contract:=building.get("queue_production_exit_update",{}) as Dictionary
	if not exit_contract.is_empty() and not bool(exit_contract.get("executable",false)):
		return {"ok":false,"reason":"invalid-production-exit-contract"}
	var queue: Array = building.get("queue", [])
	var maximum_queue := producer_queue_limit(building)
	if maximum_queue > 0 and queue.size() >= maximum_queue:
		return {"ok": false, "reason": "queue-full"}
	var cost := maxi(0, _production_rule_value(unit_type, "cost_rule", "default_cost"))
	# Zero command points is honored when the document says zero (retail
	# porters are free); the historical clamp to one is gone.
	var command_cost := maxi(0, _production_rule_value(unit_type, "command_points_rule", "default_command_points"))
	var queued_command_points = _sim._queued_command_points_for_team(team)
	if (
		_sim.command_points_for_team(team) + queued_command_points + command_cost
		> _sim.command_point_total_for_team(team)
	):
		return {"ok": false, "reason": "command-point-cap"}
	var build_ticks := maxi(1, _production_rule_value(unit_type, "build_ticks_rule", "default_build_ticks"))
	var production_contract := building.get("production_update", {}) as Dictionary
	for modifier_value in production_contract.get("modifiers", []) as Array:
		var modifier := modifier_value as Dictionary
		if not _sim._structure_has_completed_upgrade(building, String(modifier.get("required_upgrade", ""))):
			continue
		var probe := {"category":String(production_rule.get("category", "")), "kind_of":production_rule.get("kind_of", [])}
		if not (modifier.get("filter", []) as Array).is_empty() and not _sim._transport_filter_accepts(probe, modifier.get("filter", []) as Array):
			continue
		var is_hero := String(production_rule.get("category", "")) == "hero"
		if is_hero and not bool(modifier.get("hero_purchase", false)):
			continue
		cost = maxi(0, roundi(float(cost) * float(modifier.get("cost_multiplier", 1.0))))
		build_ticks = maxi(1, roundi(float(build_ticks) * float(modifier.get("time_multiplier", 1.0))))
	var production_multiplier := float(building.get("production_multiplier", 1.0))
	if production_multiplier > 0.0 and production_multiplier != 1.0:
		# The producer's authored PRODUCTION level factor scales its authored
		# build time (retail L2/L3 factory speed); rounding stays deterministic.
		build_ticks = maxi(1, roundi(float(build_ticks) / production_multiplier))
	# Authored ProductionModifier discounts and surcharges are part of the
	# price.  Check the final deterministic price, not the unmodified base.
	if _sim.resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources"}
	var starts_at = _sim.tick_index if queue.is_empty() else int((queue.back() as Dictionary).get("complete_tick", _sim.tick_index))
	var item := {
		"unit_type": unit_type,
		"cost": cost,
		"command_points": command_cost,
		"queued_tick": _sim.tick_index,
		"start_tick": starts_at,
		"duration_ticks": build_ticks,
		"complete_tick": starts_at + build_ticks,
	}
	for route_value in Array(production_rule.get("producer_routes", [])):
		var route := route_value as Dictionary
		if String(route.get("producer_kind", "")) == String(building.get("structure_kind", "")):
			item["command_id"] = String(route.get("command_id", ""))
			break
	queue.append(item)
	building["queue"] = queue
	_sim.team_resources[team] = _sim.resources_for_team(team) - cost
	_sim._emit_event("production.queued", producer, 0, {"team": team, "unit_type": unit_type, "command_id": String(item.get("command_id", "")), "complete_tick": int(item["complete_tick"])})
	return {"ok": true, "reason": "", "producer_id": producer, "item": item.duplicate(true)}


func production_queue_state(producer: int) -> Array[Dictionary]:
	var _sim = sim
	var rows: Array[Dictionary] = []
	if not _sim.structures.has(producer):
		return rows
	var queue: Array = (_sim.structures[producer] as Dictionary).get("queue", [])
	for index in range(queue.size()):
		if typeof(queue[index]) != TYPE_DICTIONARY:
			continue
		var item: Dictionary = queue[index]
		var start_tick := int(item.get("start_tick", item.get("queued_tick", _sim.tick_index)))
		var complete_tick := int(item.get("complete_tick", start_tick + 1))
		var duration_ticks := maxi(1, int(item.get("duration_ticks", complete_tick - start_tick)))
		var active := index == 0
		var elapsed_ticks := clampi(_sim.tick_index - start_tick, 0, duration_ticks) if active else 0
		rows.append({
			"index": index,
			"unit_type": String(item.get("unit_type", _sim.SOLDIER_HORDE_ID)),
			"cost": int(item.get("cost", 0)),
			"command_points": int(item.get("command_points", 0)),
			"queued_tick": int(item.get("queued_tick", start_tick)),
			"start_tick": start_tick,
			"complete_tick": complete_tick,
			"duration_ticks": duration_ticks,
			"elapsed_ticks": elapsed_ticks,
			"progress": float(elapsed_ticks) / float(duration_ticks) if active else 0.0,
			"active": active,
		})
	return rows


func cancel_queued_unit(team: int, producer: int, queue_index: int = 0) -> Dictionary:
	var _sim = sim
	if not _sim.structures.has(producer):
		return {"ok": false, "reason": "unknown-producer"}
	var building: Dictionary = _sim.structures[producer]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	var queue: Array = building.get("queue", [])
	if queue_index < 0 or queue_index >= queue.size():
		return {"ok": false, "reason": "unknown-queue-item"}
	var cancelled: Dictionary = queue[queue_index]
	queue.remove_at(queue_index)
	var cursor = _sim.tick_index if queue_index == 0 else int((queue[queue_index - 1] as Dictionary).get("complete_tick", _sim.tick_index))
	for index in range(queue_index, queue.size()):
		var item: Dictionary = queue[index]
		var prior_start := int(item.get("start_tick", item.get("queued_tick", cursor)))
		var prior_complete := int(item.get("complete_tick", prior_start + 1))
		var duration_ticks := maxi(1, int(item.get("duration_ticks", prior_complete - prior_start)))
		item["start_tick"] = cursor
		item["duration_ticks"] = duration_ticks
		item["complete_tick"] = cursor + duration_ticks
		cursor += duration_ticks
	building["queue"] = queue
	var refund := maxi(0, int(cancelled.get("cost", 0)))
	_sim.team_resources[team] = _sim.resources_for_team(team) + refund
	_sim._emit_event("production.cancelled", producer, 0, {
		"team": team,
		"unit_type": String(cancelled.get("unit_type", _sim.SOLDIER_HORDE_ID)),
		"queue_index": queue_index,
		"refund": refund,
	})
	return {
		"ok": true,
		"reason": "",
		"producer_id": producer,
		"queue_index": queue_index,
		"refund": refund,
		"item": cancelled.duplicate(true),
	}


func producer_queue_limit(producer_row: Dictionary) -> int:
	## Authored `ProductionUpdate MaxQueueEntries`, or 0 for UNCAPPED.
	##
	## RETAIL ORACLE (rotwk 2.01 effective view, counted 2026-08-18): retail
	## authors MaxQueueEntries on exactly TWO of 423 ProductionUpdate blocks --
	## object/evilfaction/units/angmar/angmarthrallmaster.ini:587 and
	## object/goodfaction/units/dwarven/dwarvenbattlewagon.ini:492, both
	## `MaxQueueEntries = 1 ; only allow one queued upgrade at a time`. Every
	## other producer authors nothing, and absent means the engine imposes no
	## limit. Our old code invented a default of 5 (retail_vertical_slice.gd
	## `maximum_queue`) AND inverted the test: `maximum_queue <= 0` was read as
	## "queue full", so a producer whose contract honestly carried no cap was
	## refused outright. Both are gone: 0/absent is uncapped.
	##
	## Both authored caps read "only allow one queued upgrade at a time", i.e.
	## retail counts upgrades in the same ProductionUpdate queue. We still hold
	## structure upgrades in a separate `upgrade_queue` (one at a time by its
	## own rule); merging the two lists is a named open gap, not done here.
	return maxi(0, int((producer_row.get("production_update", {}) as Dictionary).get("maximum_queue_entries", 0)))


func unit_command_point_cost(unit_type: String) -> int:
	## The command points queue_unit will commit for one production of
	## `unit_type` - the admission rule's own number (same rule/default
	## resolution), exposed so HAS_COMMAND_POINTS_TO_BUILD_UNIT can never
	## disagree with the queue that follows it. -1 for an unmodeled type.
	if not sim._unit_production_rules.has(unit_type):
		return -1
	return maxi(0, _production_rule_value(unit_type, "command_points_rule", "default_command_points"))


func _step_production() -> void:
	var _sim = sim
	for id in _sim.structure_ids():
		var building: Dictionary = _sim.structures[id]
		if int(building.get("health", 0)) <= 0:
			continue
		var queue: Array = building.get("queue", [])
		if queue.is_empty():
			continue
		var item: Dictionary = queue[0]
		if _sim.tick_index < int(item.get("complete_tick", _sim.tick_index + 1)):
			continue
		queue.pop_front()
		building["queue"] = queue
		var team := int(building.get("team", -1))
		var new_id := int(_sim._next_dynamic_id.get(team, 10 if team == _sim.PLAYER_TEAM else 110))
		_sim._next_dynamic_id[team] = new_id + 1
		var production_origin := Vector2(building.get("position", Vector2.ZERO))
		var exit_contract:=building.get("queue_production_exit_update",{}) as Dictionary
		var exit_index:=int(exit_contract.get("next_index",0));var authored_points:=exit_contract.get("create_points_source",[]) as Array;var authored_rallies:=exit_contract.get("rally_points_source",[]) as Array
		var rally := Vector2(building.get("rally", production_origin))
		if not authored_rallies.is_empty():rally=production_origin+_sim._retail_source_to_sim_offset(Vector2(authored_rallies[exit_index%authored_rallies.size()]))
		var exit_direction := production_origin.direction_to(rally)
		if exit_direction.length_squared() <= 0.000001:
			exit_direction = Vector2.RIGHT if team == _sim.PLAYER_TEAM else Vector2.LEFT
		var door_point = production_origin + exit_direction * _sim.PRODUCTION_DOOR_INSET_RADIUS
		var create_point = production_origin + exit_direction * _sim.PRODUCTION_EXIT_RADIUS
		if not authored_points.is_empty():create_point=production_origin+_sim._retail_source_to_sim_offset(Vector2(authored_points[exit_index%authored_points.size()]))
		var unit_type := String(item.get("unit_type", _sim.SOLDIER_HORDE_ID))
		# The queued item names its own unit type; the historical Gondor defaults
		# only rescue a queue row that lost its type entirely.
		var production_rule: Dictionary = _sim._unit_production_rules.get(unit_type, _sim._unit_production_rules.get(_sim.SOLDIER_HORDE_ID, {}))
		var object_id := String(production_rule.get("object_id", unit_type))
		var display_name := String(production_rule.get("display_name", unit_type))
		var committed_command_points := int(item.get("command_points", 60))
		# QueueProductionExitUpdate uses a create point at the producer doorway,
		# reveals the horde there, and only then sends it to the rally point.
		_sim._add_battalion(
			new_id, team, door_point, display_name, object_id, unit_type,
			committed_command_points, {}, int(item.get("cost", -1))
		)
		if bool(production_rule.get("is_ring_hero", false)):
			var ring_hero: Dictionary = _sim.entities[new_id]
			ring_hero["ring_hero"] = true
			ring_hero["level"] = 10
			var owned: Dictionary = _sim.team_upgrades.get(team, {}) as Dictionary
			owned.erase("Upgrade_RingHero")
			_sim.team_upgrades[team] = owned
			for team_structure_id in _sim.structure_ids(team):
				var team_structure: Dictionary = _sim.structures[team_structure_id]
				var producer_upgrades: Array = team_structure.get("completed_upgrades", [])
				producer_upgrades.erase("Upgrade_FortressRingHero")
				team_structure["completed_upgrades"] = producer_upgrades
			_sim._emit_event("ring.hero_created", new_id, id, {"team": team, "rank": 10})
		if String(production_rule.get("category", "")) == "hero":
			_sim._completed_hero_identities["%d:%s" % [team, unit_type]] = true
			if team == _sim.PLAYER_TEAM:
				_sim._emit_event("eva.hero_created", new_id, 0, {"team": team, "object_id": object_id, "unit_type": unit_type})
		var produced: Dictionary = _sim.entities[new_id]
		produced["production_producer_id"] = id
		produced["production_exit_start_tick"] = _sim.tick_index
		var authored_delays:=exit_contract.get("exit_delay_ticks",[]) as Array;var exit_ticks: int = _sim.PRODUCTION_EXIT_DURATION_TICKS
		if not authored_delays.is_empty():exit_ticks=int(authored_delays[exit_index%authored_delays.size()])
		produced["production_exit_duration_ticks"] = exit_ticks
		produced["production_exit_progress"] = 0.0
		var no_exit_path := bool(exit_contract.get("no_exit_path", false))
		# Retail's NoExitPath suppresses the normal doorway path; it does not
		# rewrite ExitDelay. Keep the authored timer while placing the unit at its
		# create point immediately, so there is no synthetic travel segment.
		# Keep the legacy authoritative byte shape for the overwhelmingly common
		# false/default case. Presence means the authored NoExitPath branch is live;
		# serializing an inert false key would move every ordinary production state.
		if no_exit_path:
			produced["production_exit_no_path"] = true
		else:
			produced.erase("production_exit_no_path")
		produced["production_exit_origin"] = create_point if no_exit_path else door_point
		produced["production_exit_destination"] = create_point
		produced["production_rally"] = rally
		produced["facing"] = exit_direction
		var angles:=exit_contract.get("placement_angles",[]) as Array
		if not angles.is_empty():
			var producer_facing := Vector2(building.get("facing", Vector2.ZERO))
			if producer_facing.length_squared() <= 0.000001:
				producer_facing = Vector2.RIGHT.rotated(float(building.get("facing_radians", 0.0)))
			produced["facing"] = producer_facing.normalized().rotated(deg_to_rad(float(angles[exit_index%angles.size()])))
		if no_exit_path:
			produced["position"] = create_point
			_sim._spatial_sync(produced)
		if not exit_contract.is_empty():exit_contract["next_index"]=exit_index+1;building["queue_production_exit_update"]=exit_contract
		_sim.team_command_points[team] = _sim.command_points_for_team(team) + committed_command_points
		_sim._emit_event("production.complete", id, new_id, {
			"team": team,
			"unit_type": unit_type,
			"object_id": object_id,
			"category": String(production_rule.get("category", "")),
			"production_origin": production_origin,
			"create_point": create_point,
			"rally": rally,
			"exit_duration_ticks": exit_ticks,
			"exit_route_accepted": false,
		})


func _step_production_exit(row: Dictionary) -> bool:
	var _sim = sim
	var duration := int(row.get("production_exit_duration_ticks", 0))
	var start_tick := int(row.get("production_exit_start_tick", -1))
	if duration <= 0 or start_tick < 0:
		row["production_exit_progress"] = 1.0
		return false
	var elapsed := maxi(0, _sim.tick_index - start_tick)
	var progress := clampf(float(elapsed) / float(duration), 0.0, 1.0)
	row["production_exit_progress"] = progress
	var exit_origin := Vector2(row.get("production_exit_origin", row.get("position", Vector2.ZERO)))
	var exit_destination := Vector2(row.get("production_exit_destination", exit_origin))
	row["position"] = exit_origin.lerp(exit_destination, smoothstep(0.0, 1.0, progress))
	_sim._spatial_sync(row)
	var exit_direction := exit_origin.direction_to(exit_destination)
	if exit_direction.length_squared() > 0.000001:
		row["facing"] = exit_direction
	if elapsed >= duration:
		row["production_exit_start_tick"] = -1
		row["production_exit_duration_ticks"] = 0
		row["production_exit_progress"] = 1.0
		var rally := Vector2(row.get("production_rally", exit_destination))
		if not rally.is_equal_approx(exit_destination) and _sim._assign_route(row, rally):
			row["state"] = "run"
		else:
			_sim._clear_pending_route(row, true)
			row["state"] = "idle"
		_sim._emit_event("production.exit_complete", int(row.get("production_producer_id", 0)), int(row.get("id", 0)), {
			"rally": rally,
			"exit_destination": exit_destination,
		})
		return false
	# The horde presentation reveals retail members through the door one by one.
	# Keep its authoritative root at the source-derived doorway until all members
	# have emerged, then let the already accepted rally route advance normally.
	row["state"] = "run"
	return true


func _release_command_points_once(row: Dictionary) -> void:
	var _sim = sim
	if not _sim.base_loop_enabled or bool(row.get("command_points_released", false)):
		return
	row["command_points_released"] = true
	var row_team := int(row.get("team", -1))
	_sim.team_command_points[row_team] = maxi(
		0,
		_sim.command_points_for_team(row_team)
		- _entity_command_point_commitment(row)
	)


func _entity_command_point_commitment(row: Dictionary) -> int:
	return maxi(
		0,
		int(row.get("command_points", sim._rules.get("soldier_command_points", 60)))
	)


func production_gate_unsatisfied(
	unit_type: String, producer_kind: String, completed_upgrades: Array,
	team_completed_upgrades: Array = []
) -> String:
	## Returns the upgrade id the gate is still waiting on, or "" when it holds.
	## The ALL-of set must be owned entirely; the ANY-of group (when authored)
	## needs any single member. An absent group is never a gate.
	var _sim = sim
	for required_value in _sim.required_upgrades_for_unit(unit_type, producer_kind):
		if not completed_upgrades.has(String(required_value)) \
				and not team_completed_upgrades.has(String(required_value)):
			return String(required_value)
	var any_group = _sim.required_upgrade_any_group_for_unit(unit_type, producer_kind)
	if any_group.is_empty():
		return ""
	for candidate_value in any_group:
		if completed_upgrades.has(String(candidate_value)) \
				or team_completed_upgrades.has(String(candidate_value)):
			return ""
	return String(any_group[0])


# Retail armory tech tree: upgrade.ini:1555-1593 (Upgrade_GondorForgedBlades,
# Upgrade_GondorFireArrows, Upgrade_GondorHeavyArmor) research at the forge for
# GONDOR_TECH_*_BUILDCOST = 1000 and GONDOR_TECH_*_BUILDTIME = 30s
# (gamedata.ini:1281-1288). The slice's research conflates the retail PLAYER
# technology unlock with the per-battalion OBJECT purchase (300 each,
# gamedata.ini:1300-1306): completion auto-equips every matching horde,
# recorded per-horde in applied_upgrades. The per-battalion purchase command
# flow is a recorded gap, not an invented value. The damage/armor effects are
# the compiled WeaponSetUpgrade/ArmorUpgrade tables from each unit document —
# no hand-tuned multipliers remain.


func _production_rule_value(unit_type: String, rule_key: String, default_key: String) -> int:
	var _sim = sim
	var production_rule: Dictionary = _sim._unit_production_rules.get(unit_type, {})
	if production_rule.is_empty():
		return 0
	var gameplay_rule := String(production_rule.get(rule_key, ""))
	return int(_sim._rules.get(gameplay_rule, int(production_rule.get(default_key, 0))))


func producer_id(team: int, kind: String = "barracks") -> int:
	var _sim = sim
	for id in _sim.structure_ids(team):
		var row: Dictionary = _sim.structures[id]
		if String(row.get("structure_kind", "")) == kind and int(row.get("health", 0)) > 0:
			return id
	return 0
