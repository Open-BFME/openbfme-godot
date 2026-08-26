extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Base-building subsystem extracted from retail_slice_sim.gd (Q81
## strangler-fig extraction #12): structure weapons, construct-site
## validation, expansion pads, build plots, castle behavior unpack,
## unpackable bases. Verbatim move, pin-verified.





func _step_structure_weapons() -> void:
	## Summoned sim.structures (Lone Tower): self-rise over the authored just-built
	## duration (no builder — OCL UseJustBuiltFlag), then the converted bow
	## fires on the nearest enemy battalion in range each converted period.
	var _sim = sim
	for structure_id_value in _sim.structures.keys():
		var structure_id = int(structure_id_value)
		var structure: Dictionary = _sim.structures[structure_id]
		var attack_value: Variant = structure.get("attack", null)
		var attack := attack_value as Dictionary if typeof(attack_value) == TYPE_DICTIONARY else {}
		var pending := attack.get("pending_projectile", {}) as Dictionary
		if not pending.is_empty():
			if _sim.tick_index < int(pending.get("impact_tick", _sim.tick_index)):
				continue
			# An already-launched shell remains authoritative even if its firing
			# expansion is destroyed before impact.
			_resolve_structure_weapon_impact(structure_id, attack, pending)
			continue
		if int(structure.get("health", 0)) <= 0:
			continue
		if String(structure.get("summon_object_id", "")) != "" and float(structure.get("construction_progress", 1.0)) < 1.0:
			var elapsed := int(structure.get("construction_elapsed_ticks", 0)) + 1
			structure["construction_elapsed_ticks"] = elapsed
			var build_ticks := maxi(1, int(structure.get("construction_build_ticks", 1)))
			structure["construction_progress"] = minf(1.0, float(elapsed) / float(build_ticks))
			if elapsed >= build_ticks:
				_sim._apply_structure_create_grants(structure, false, true)
			continue
		if float(structure.get("construction_progress", 1.0)) < 1.0:
			continue
		if typeof(attack_value) != TYPE_DICTIONARY:
			continue
		if _sim.tick_index < int(attack.get("cooldown", 0)):
			continue
		var team := int(structure.get("team", -1))
		var origin := Vector2(structure.get("position", Vector2.ZERO))
		var best_id := 0
		var best_distance := float(attack.get("range", 0.0))
		var minimum_distance := maxf(0.0, float(attack.get("minimum_range", 0.0)))
		for id in _sim.living_ids(1 - team):
			var row: Dictionary = _sim.entities[id]
			var distance := origin.distance_to(Vector2(row.get("position", Vector2.ZERO)))
			if distance >= minimum_distance and distance < best_distance:
				best_distance = distance
				best_id = id
		if best_id == 0:
			continue
		var target: Dictionary = _sim.entities[best_id]
		var health_values: Array = target.get("member_health", [])
		var member_index := -1
		for index in health_values.size():
			if int(health_values[index]) > 0:
				member_index = index
				break
		if member_index < 0:
			continue
		var amount := maxi(1, roundi(float(attack.get("damage", 0.0))))
		var flight_ticks := 0
		var projectile_speed := float(attack.get("projectile_speed", 0.0))
		if projectile_speed > 0.0 and String(attack.get("projectile_object_id", "")) != "":
			flight_ticks = maxi(1, ceili(best_distance / projectile_speed / _sim.TICK_SECONDS))
		var token := int(attack.get("next_projectile_token", 1))
		attack["next_projectile_token"] = token + 1
		attack["pending_projectile"] = {
			"token": token,
			"target_id": best_id,
			"member_index": member_index,
			"amount": amount,
			"impact_tick": _sim.tick_index + int(attack.get("pre_attack_ticks", 0)) + flight_ticks,
		}
		# DelayBetweenShots is measured from this authored attack cycle, not
		# from projectile impact; adding flight time here made artillery fire
		# progressively slower at longer range.
		attack["cooldown"] = _sim.tick_index + _structure_weapon_period_ticks(attack)
		_sim._emit_event("combat.structure_projectile_launched", structure_id, best_id, {
			"projectile_token": token,
			"projectile_object_id": String(attack.get("projectile_object_id", "")),
			"weapon_id": String(attack.get("weapon_id", "")),
			"team": team,
			"launch_position": origin,
			"target_position": Vector2(target.get("position", Vector2.ZERO)),
			"impact_tick": int((attack["pending_projectile"] as Dictionary)["impact_tick"]),
		})
		if int((attack["pending_projectile"] as Dictionary)["impact_tick"]) <= _sim.tick_index:
			_resolve_structure_weapon_impact(
				structure_id, attack, attack["pending_projectile"] as Dictionary
			)


func _structure_weapon_period_ticks(attack: Dictionary) -> int:
	## SAGE DelayBetweenShots Min:/Max: chooses a new inclusive integer
	## millisecond delay for every attack cycle. Draw before quantizing to the
	## fixed tick so the authored millisecond distribution remains authoritative.
	var _sim = sim
	if String(attack.get("delay_between_shots_distribution", "")) == "uniform-inclusive-integer":
		var minimum_ms := int(attack.get("delay_between_shots_minimum_ms", -1))
		var maximum_ms := int(attack.get("delay_between_shots_maximum_ms", -1))
		if minimum_ms >= 0 and maximum_ms >= minimum_ms:
			var delay_ms = _sim.logic_random_int(minimum_ms, maximum_ms)
			return maxi(1, roundi(float(delay_ms) / (_sim.TICK_SECONDS * 1000.0)))
	return maxi(1, int(attack.get("period_ticks", 1)))


func _resolve_structure_weapon_impact(
	structure_id: int, attack: Dictionary, pending: Dictionary
) -> void:
	var _sim = sim
	var target_id := int(pending.get("target_id", 0))
	if not _sim.entities.has(target_id):
		_sim._emit_event("combat.structure_projectile_cancelled", structure_id, target_id, {
			"projectile_token": int(pending.get("token", 0)),
			"projectile_object_id": String(attack.get("projectile_object_id", "")),
		})
		attack.erase("pending_projectile")
		return
	var target := _sim.entities[target_id] as Dictionary
	var health_values: Array = target.get("member_health", [])
	var member_index := int(pending.get("member_index", -1))
	if member_index < 0 or member_index >= health_values.size() or int(health_values[member_index]) <= 0:
		member_index = -1
		for index in health_values.size():
			if int(health_values[index]) > 0:
				member_index = index
				break
	if member_index >= 0:
		var amount := maxi(1, int(pending.get("amount", 1)))
		var prior_health := int(health_values[member_index])
		health_values[member_index] = maxi(0, prior_health - amount)
		target["member_health"] = health_values
		var aggregate_health := 0
		for value in health_values:
			aggregate_health += int(value)
		target["health"] = aggregate_health
		target["last_damage_tick"] = _sim.tick_index
		_sim.record_hit_reaction(target_id, float(amount))
		var defeated_members: Array[int] = []
		if prior_health > 0 and int(health_values[member_index]) == 0:
			defeated_members.append(member_index)
		if aggregate_health <= 0:
			var death_policy = _sim._bookkeep_battalion_death(
				target_id, target, "NORMAL", defeated_members
			)
			_sim._emit_event("battalion.defeated", structure_id, target_id)
			if bool(death_policy.get("destroy_object", false)):
				_sim.entities.erase(target_id)
		else:
			_sim._apply_playable_unit_death_policy(target, "NORMAL", defeated_members)
		_sim._emit_event("power.structure_weapon_hit", structure_id, target_id, {
			"amount": amount,
			"projectile_token": int(pending.get("token", 0)),
			"projectile_object_id": String(attack.get("projectile_object_id", "")),
		})
	else:
		_sim._emit_event("combat.structure_projectile_cancelled", structure_id, target_id, {
			"projectile_token": int(pending.get("token", 0)),
			"projectile_object_id": String(attack.get("projectile_object_id", "")),
		})
	attack.erase("pending_projectile")


func validate_construct_site(builder_ids: Array[int], structure_kind: String, point: Vector2, team: int = sim.PLAYER_TEAM) -> Dictionary:
	# Non-mutating dry run of sim.issue_construct's admission checks so the
	# placement ghost can tint valid/invalid while the player aims. The team is
	# the REQUESTING seat's: a lockstep guest probes its own faction's tables.
	return sim.issue_construct(builder_ids, structure_kind, point, true, team)


# --- Fortress expansion pads (engine-spawned build plots) -------------------
# Pad slot layout around a fortress in local units. PROVISIONAL: retail
# spawns these at the fortress model's BUILDPLOT bones, which no converted
# asset preserves (checked GLBs, map data, docs). Slots keep the retail shape
# (4 corner + 2 side plots ringing the fortress) until bone evidence converts.
# Owner pass: plots sit ATTACHED to the fortress's corners/edges (REF-33,
# tight against the model), not floating in a wide arc — corner diagonals at
# ~8.5 units and sides at 7.5 hug the fortress's ~5-6 unit model half-extent
# while clearing its 4.0 placement radius plus a pad structure's footprint.


func configure_expansion_rules(rules: Dictionary) -> void:
	sim._expansion_build_rules = rules.duplicate(true)


func _seed_expansion_pads_for(fortress_structure_id: int) -> void:
	var _sim = sim
	if _sim.expansion_pads.has(fortress_structure_id) or not _sim.structures.has(fortress_structure_id):
		return
	var fortress: Dictionary = _sim.structures[fortress_structure_id]
	if String(fortress.get("structure_kind", "")) != "fortress":
		return
	var center = Vector2(fortress.get("position", Vector2.ZERO))
	var pads: Array = []
	var castle := _castle_behavior_for_structure(fortress)
	if not castle.is_empty():
		_unpack_castle_behavior_for_structure(fortress_structure_id)
		pads = (fortress.get("castle_expansion_pads", []) as Array).duplicate(true)
	else:
		for pad_kind in _sim.EXPANSION_PAD_LAYOUT.keys():
			for slot in range((_sim.EXPANSION_PAD_LAYOUT[pad_kind] as Array).size()):
				pads.append({
					"slot": slot,
					"pad_kind": pad_kind,
					"position": center + (_sim.EXPANSION_PAD_LAYOUT[pad_kind] as Array)[slot],
					"expansion_structure_id": 0,
				})
	_sim.expansion_pads[fortress_structure_id] = pads


func _unpack_castle_behavior_for_structure(structure_id: int) -> bool:
	## CastleBehavior executes once for every structure that carries the selected
	## pack contract. Every BSE row becomes an authoritative structure object;
	## no pad/citadel is inferred and no missing template gets substituted.
	var _sim = sim
	if not _sim.structures.has(structure_id):
		return false
	var owner: Dictionary = _sim.structures[structure_id]
	if bool(owner.get("castle_behavior_unpacked", false)):
		return true
	var castle := _castle_behavior_for_structure(owner)
	if castle.is_empty():
		return false
	if castle.has("_error"):
		var message := "CastleBehavior for '%s' failed closed: %s" % [String(owner.get("structure_kind", "structure")), String(castle.get("_error", "invalid selected contract"))]
		owner["castle_behavior_error"] = message
		_sim.configuration_error = message
		push_error(message)
		return false
	var child_ids: Array[int] = []
	var pads: Array = []
	var side_slot := 0
	var corner_slot := 0
	for piece_value in castle.get("pieces", []) as Array:
		var piece: Dictionary = piece_value
		var source_object_id := String(piece.get("source_object_id", ""))
		var object_id := String(piece.get("object_id", ""))
		var maximum_health := int(piece.get("maximum_health", 0))
		if source_object_id == "" or object_id == "" or maximum_health <= 0:
			push_error("RetailSliceSim refused incomplete CastleBehavior piece %s" % str(piece))
			return false
		var offset: Vector2 = piece.get("offset_source", Vector2.ZERO)
		var at = Vector2(owner.get("position", Vector2.ZERO)) + _sim._retail_source_to_sim_offset(offset)
		var child_id := _spawn_castle_piece_structure(
			structure_id,
			owner,
			source_object_id,
			object_id,
			at,
			float(piece.get("elevation_source", 0.0)),
			float(piece.get("angle_radians", 0.0)),
			maximum_health,
			int(piece.get("index", -1))
		)
		if child_id == 0:
			return false
		child_ids.append(child_id)
		var folded := source_object_id.to_lower()
		if folded.contains("expansionpad"):
			var pad_kind := "corner" if folded.contains("corner") else "side"
			var slot := corner_slot if pad_kind == "corner" else side_slot
			if pad_kind == "corner":
				corner_slot += 1
			else:
				side_slot += 1
			pads.append({
				"slot": slot,
				"pad_kind": pad_kind,
				"position": at,
				"angle_radians": float(piece.get("angle_radians", 0.0)),
				"source_object_id": source_object_id,
				"castle_piece_structure_id": child_id,
				"expansion_structure_id": 0,
				"castle_piece_index": int(piece.get("index", -1)),
			})
	owner["castle_behavior_unpacked"] = true
	owner["castle_template_token"] = String(castle.get("castle_template_token", ""))
	owner["castle_piece_structure_ids"] = child_ids
	owner["castle_expansion_pads"] = pads
	_sim._emit_event("structure.castle_unpacked", 0, structure_id, {
		"team": int(owner.get("team", -1)),
		"token": String(castle.get("castle_template_token", "")),
		"piece_count": child_ids.size(),
		"pad_count": pads.size(),
	})
	return true


func _castle_behavior_for_structure(structure: Dictionary) -> Dictionary:
	var _sim = sim
	var direct: Variant = structure.get("castle_behavior", {})
	if typeof(direct) == TYPE_DICTIONARY and not (direct as Dictionary).is_empty():
		return direct as Dictionary
	var team := int(structure.get("team", -1))
	var kind := String(structure.get("structure_kind", ""))
	var manifest_source := ""
	var manifest_sources: Variant = _sim.structure_source_object_ids_for_team(team).get(kind, [])
	if typeof(manifest_sources) == TYPE_ARRAY and not (manifest_sources as Array).is_empty():
		manifest_source = String((manifest_sources as Array)[0])
	elif typeof(manifest_sources) in [TYPE_STRING, TYPE_STRING_NAME]:
		manifest_source = String(manifest_sources)
	for key in [
		manifest_source,
		String(structure.get("source_object_id", "")),
		String(structure.get("object_id", "")),
		String(structure.get("name", "")),
	]:
		if key != "" and _sim._castle_behavior_by_source.has(key):
			return (_sim._castle_behavior_by_source[key] as Dictionary).duplicate(true)
		# Case-insensitive source map
		for source_key in _sim._castle_behavior_by_source.keys():
			if String(source_key).to_lower() == key.to_lower():
				return (_sim._castle_behavior_by_source[source_key] as Dictionary).duplicate(true)
	return {}


func _spawn_castle_piece_structure(
	fortress_structure_id: int,
	fortress: Dictionary,
	source_object_id: String,
	object_id: String,
	at: Vector2,
	elevation_source: float,
	angle_radians: float,
	maximum_health: int,
	piece_index: int
) -> int:
	var _sim = sim
	var structure_id = _sim._next_dynamic_structure_id
	_sim._next_dynamic_structure_id += 1
	var team := int(fortress.get("team", -1))
	_sim._note_structure_table_mutation()
	_sim.structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": "castle_piece",
		"name": source_object_id,
		"source_object_id": source_object_id,
		"object_id": object_id,
		"position": at,
		"elevation": float(fortress.get("elevation", 0.0)) + _sim._retail_source_to_sim_offset(Vector2(elevation_source, 0.0)).x,
		"facing_radians": angle_radians + float(fortress.get("facing_radians", 0.0)),
		"rally": at + Vector2(2.0, 0.0),
		"health": maximum_health,
		"maximum_health": maximum_health,
		"construction_progress": 1.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
		"castle_piece_of_fortress": fortress_structure_id,
		"castle_piece_index": piece_index,
	}
	_sim._apply_structure_inherit_upgrades(_sim.structures[structure_id] as Dictionary)
	_sim._initialize_structure_auto_deposit(_sim.structures[structure_id] as Dictionary)
	return structure_id


func _seed_all_expansion_pads() -> void:
	var _sim = sim
	for structure_id in _sim.structure_ids():
		if String((_sim.structures[structure_id] as Dictionary).get("structure_kind", "")) == "fortress":
			_seed_expansion_pads_for(structure_id)


func expansion_pad_states(fortress_structure_id: int) -> Array:
	return (sim.expansion_pads.get(fortress_structure_id, []) as Array).duplicate(true)


func _seed_build_plots_for_team(team: int) -> void:
	var _sim = sim
	if _sim.build_plots.has(team):
		return
	# Prefer the fortress footprint as the ring center; fall back to the team's
	# spawn anchor when a team has no fortress (defensive — every rostered team
	# seeds one in _initialize_base_loop).
	var center = _sim._team_center(team)
	var fortress = _sim.fortress_id(team)
	if fortress != 0:
		center = Vector2((_sim.structures[fortress] as Dictionary).get("position", center))
	var plots: Array = []
	for offset in _sim.BUILD_PLOT_RING_OFFSETS:
		plots.append({"position": center + (offset as Vector2), "occupant_structure_id": 0})
	_sim.build_plots[team] = plots


func _seed_all_build_plots() -> void:
	for team in sim._roster_team_ids():
		_seed_build_plots_for_team(int(team))


func _reconcile_build_plots(team: int) -> void:
	## A plot is free when it has no occupant, or its occupant structure no longer
	## exists / has been razed (health <= 0). Reconciling lazily keeps plot freeing
	## deterministic without hooking every structure-destruction path.
	var _sim = sim
	if not _sim.build_plots.has(team):
		return
	for plot_value in _sim.build_plots[team] as Array:
		var plot: Dictionary = plot_value
		var occupant := int(plot.get("occupant_structure_id", 0))
		if occupant == 0:
			continue
		if not _sim.structures.has(occupant) or int((_sim.structures[occupant] as Dictionary).get("health", 0)) <= 0:
			plot["occupant_structure_id"] = 0


func build_plot_states(team: int) -> Array:
	## Reconciled (razed plots freed) copy of a team's build plots for the HUD and
	## tests. Empty unless build_plots_only is on.
	_reconcile_build_plots(team)
	return (sim.build_plots.get(team, []) as Array).duplicate(true)


func _free_build_plot_index_near(team: int, position: Vector2) -> int:
	## Nearest free plot within sim.BUILD_PLOT_PICK_RADIUS of the click, ties broken by
	## lowest index (deterministic). -1 when none is close enough.
	var _sim = sim
	_reconcile_build_plots(team)
	var plots: Array = _sim.build_plots.get(team, [])
	var best := -1
	var best_dist = _sim.BUILD_PLOT_PICK_RADIUS + 1.0
	for index in plots.size():
		var plot: Dictionary = plots[index]
		if int(plot.get("occupant_structure_id", 0)) != 0:
			continue
		var dist := Vector2(plot.get("position", Vector2.ZERO)).distance_to(position)
		if dist <= _sim.BUILD_PLOT_PICK_RADIUS and dist < best_dist:
			best_dist = dist
			best = index
	return best


func expansion_commands_for(fortress_structure_id: int) -> Array:
	## Expansion commands with a free matching pad (retail hides exhausted
	## options). Kinds with no free pad are simply unavailable.
	var _sim = sim
	var result: Array = []
	if not _sim.expansion_pads.has(fortress_structure_id):
		return result
	var pads: Array = _sim.expansion_pads[fortress_structure_id]
	for kind_value in _sim._expansion_build_rules.keys():
		var rule: Dictionary = _sim._expansion_build_rules[kind_value]
		var pad_kinds: Array = rule.get("pad_kinds", [])
		for pad_value in pads:
			var pad: Dictionary = pad_value
			if int(pad.get("expansion_structure_id", 0)) == 0 and pad_kinds.has(String(pad.get("pad_kind", ""))):
				result.append(String(kind_value))
				break
	return result


func issue_expansion_construct(team: int, fortress_structure_id: int, expansion_kind: String, requested_pad_index: int = -1) -> Dictionary:
	var _sim = sim
	if not _sim.base_loop_enabled or _sim.winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not _sim.structures.has(fortress_structure_id):
		return {"ok": false, "reason": "unknown-structure"}
	var fortress: Dictionary = _sim.structures[fortress_structure_id]
	if int(fortress.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(fortress.get("health", 0)) <= 0 or float(fortress.get("construction_progress", 0.0)) < 1.0:
		return {"ok": false, "reason": "structure-unavailable"}
	var rule: Dictionary = _sim._expansion_build_rules.get(expansion_kind, {})
	if rule.is_empty():
		return {"ok": false, "reason": "unsupported-expansion"}
	var permission = _sim.building_permission_for_kind(team, expansion_kind)
	if not bool(permission.get("known", false)):
		return {
			"ok": false,
			"reason": "building-permission-identity-unresolved",
			"detail": String(permission.get("reason", "")),
		}
	if not bool(permission.get("allowed", false)):
		return {
			"ok": false,
			"reason": "building-disallowed",
			"object_type": String(permission.get("object_type", "")),
		}
	if not _sim.expansion_pads.has(fortress_structure_id):
		return {"ok": false, "reason": "no-expansion-pads"}
	var pad_kinds: Array = rule.get("pad_kinds", [])
	var pads: Array = _sim.expansion_pads[fortress_structure_id]
	var pad_index := -1
	if requested_pad_index >= 0:
		# The player clicked a specific plot: it must be free and accept this
		# expansion kind (corner-only kinds reject side plots and vice versa).
		if requested_pad_index >= pads.size():
			return {"ok": false, "reason": "unknown-pad"}
		var requested_pad: Dictionary = pads[requested_pad_index]
		if int(requested_pad.get("expansion_structure_id", 0)) != 0:
			return {"ok": false, "reason": "pad-occupied"}
		if not pad_kinds.has(String(requested_pad.get("pad_kind", ""))):
			return {"ok": false, "reason": "pad-kind-mismatch"}
		pad_index = requested_pad_index
	else:
		for index in pads.size():
			var pad: Dictionary = pads[index]
			if int(pad.get("expansion_structure_id", 0)) == 0 and pad_kinds.has(String(pad.get("pad_kind", ""))):
				pad_index = index
				break
	if pad_index < 0:
		return {"ok": false, "reason": "no-free-pad"}
	var cost := maxi(0, int(rule.get("cost", 0)))
	if _sim.resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	_sim.team_resources[team] = _sim.resources_for_team(team) - cost
	var structure_id = _sim._next_expansion_structure_id
	_sim._next_expansion_structure_id += 1
	var maximum_health := int(rule.get("health", 1000))
	var position: Vector2 = (pads[pad_index] as Dictionary).get("position", Vector2.ZERO)
	var build_ticks := maxi(1, roundi(float(rule.get("seconds", 20.0)) / _sim.TICK_SECONDS))
	# Foundation behavior: the plot builds the expansion itself (no porter).
	_sim._note_structure_table_mutation()
	_sim.structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": expansion_kind,
		"name": String(rule.get("name", expansion_kind.replace("_", " ").capitalize())),
		"position": position,
		"rally": position + Vector2(2.0, 0.0),
		"health": maximum_health,
		"maximum_health": maximum_health,
		"construction_progress": 0.0,
		"construction_build_ticks": build_ticks,
		"construction_elapsed_ticks": 0,
		"builder_free": true,
		"builder_id": 0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
		"expansion_of_fortress": fortress_structure_id,
		"expansion_pad_index": pad_index,
	}
	_sim._stamp_refund_die_creation_cost(_sim.structures[structure_id] as Dictionary, cost)
	var attack_value: Variant = rule.get("attack")
	if typeof(attack_value) == TYPE_DICTIONARY and not (attack_value as Dictionary).is_empty():
		var attack := (attack_value as Dictionary).duplicate(true)
		attack["cooldown"] = _sim.tick_index
		_sim.structures[structure_id]["attack"] = attack
		var spawned_object_id := String(attack.get("spawned_object_id", ""))
		if spawned_object_id != "":
			_sim.structures[structure_id]["spawned_weapon_object_id"] = spawned_object_id
	if bool(rule.get("highlander_body", false)):
		_sim.structures[structure_id]["highlander_body"] = true
	_sim._apply_structure_inherit_upgrades(_sim.structures[structure_id] as Dictionary)
	_sim._initialize_structure_auto_deposit(_sim.structures[structure_id] as Dictionary)
	(pads[pad_index] as Dictionary)["expansion_structure_id"] = structure_id
	_sim._emit_event("construction.started", 0, structure_id, {"team": team, "structure_kind": expansion_kind, "cost": cost, "build_ticks": build_ticks})
	return {"ok": true, "structure_id": structure_id, "cost": cost, "build_ticks": build_ticks}


# --- Unpackable bases (script base flags) ----------------------------------
#
# The skirmish-AI base model's first piece: map-authored BASE FLAGS that a
# player can UNPACK into a base of their own (retail's castle/camp flags, the
# subjects of NAMED_BASE_UNPACK / NAMED_BASE_UNPACK_FREE and the base flags
# ai_economy_execution polls 64 times per pass). A flag is match configuration
# keyed by its SCRIPT OBJECT NAME; unpacking it creates a completed fortress
# structure with expansion pads at the flag's authored position, owned by the
# unpacking team, paid from that team's resources unless the free variant.
#
# HASH INERTNESS. `sim.unpackable_bases` participates in the authoritative state
# ONLY when non-empty (see _authoritative_state): a match that configures no
# base flags contributes NOTHING to state_hash(), which is what keeps the
# frozen cross-platform pin (retail_state_pin_runner.gd) standing as proof the
# subsystem is inert by default. Do not add an unconditional key for it.
#
# MODELLING CHOICES, stated rather than implied:
#   * The unpacked base completes INSTANTLY (construction_progress 1.0).
#     Retail plays an unpack build-up; the sim models the outcome, not the
#     animation, and an under-construction base would make the bound result
#     reference point at a site later readers cannot build at yet.
#   * The base produces nothing and earns nothing (empty production, zero
#     income): its value is its expansion pads. Anything more would be a
#     guess at retail camp internals nobody has sourced yet.
#   * No proximity or builder requirement: the retail action is a script
#     verb, not a porter order.
#   * Placement is the authored flag position, unchecked: flags are match
#     configuration like the home layout, not a player click.
#   * Unpackability is per-flag, not per-player: the sim models no per-player
#     flag restrictions, so a packed flag is unpackable by ANY rostered team
#     while the match runs and the base loop is on.

## Script base-flag name -> {"position": Vector2, "cost": int, "health": int,
## "unpacked_by": int (team, -1 while packed), "structure_id": int (0 while
## packed)}. Hashed only when non-empty - see the block comment above.


func configure_unpackable_bases(bases: Dictionary) -> bool:
	## bases: {name: {"position": Vector2, "cost": int, "health": int}} - match
	## configuration, set identically on every peer before the match starts.
	## Every row resets to packed. An empty dict clears the subsystem (and its
	## hash contribution disappears entirely - empty-is-absent).
	##
	## THE FLAG-SHADOWING INVARIANT, second direction: a name may never be
	## both a base flag and a bound script unit reference. Bind-time already
	## refuses a reference that would shadow an existing flag (the adapter's
	## _unit_reference_rejection); THIS is the other edge - configuring a flag
	## whose name an existing reference already holds would let the stale
	## reference eclipse the sim-owned flag on every later resolve. Such a
	## configure is REFUSED WHOLE (false, push_error, nothing applied): a
	## half-applied table would be worse than a loud refusal, and nothing
	## enforces configure-before-bind ordering for the caller.
	var _sim = sim
	var names := bases.keys()
	names.sort()
	var collisions: Array[String] = []
	var reference_teams = _sim.script_unit_references.keys()
	reference_teams.sort()
	for name_value in names:
		for team in reference_teams:
			if (_sim.script_unit_references[team] as Dictionary).has(String(name_value)):
				collisions.append(String(name_value))
				break
	if not collisions.is_empty():
		push_error(
			"configure_unpackable_bases refused: %s already bound as script unit "
			% ", ".join(collisions)
			+ "reference(s); a flag of the same name would be eclipsed on every "
			+ "resolve (the flag-shadowing invariant holds in both directions)"
		)
		return false
	_sim.unpackable_bases = {}
	for name_value in names:
		var spec: Dictionary = bases[name_value]
		_sim.unpackable_bases[String(name_value)] = {
			"position": Vector2(spec.get("position", Vector2.ZERO)),
			"cost": maxi(0, int(spec.get("cost", 0))),
			"health": maxi(1, int(spec.get("health", 1000))),
			"unpacked_by": -1,
			"structure_id": 0,
		}
	return true


func unpackable_base_names() -> Array[String]:
	var names: Array[String] = []
	for name_value in sim.unpackable_bases.keys():
		names.append(String(name_value))
	names.sort()
	return names


func unpackable_base_state(base_name: String) -> Dictionary:
	## Deep copy of one flag row; {} for a name the sim does not model.
	return (sim.unpackable_bases.get(base_name, {}) as Dictionary).duplicate(true)


func base_flag_unpackable(base_name: String) -> Dictionary:
	## Side-effect-free read behind NAMED_BASE_UNPACKABLE_FOR_PLAYER.
	## {"known": false} when the sim does not model this flag (the caller must
	## refuse, not answer false); otherwise {"known": true, "unpackable": bool}.
	## Unpackable = the base loop is on, the match is unresolved, and the flag
	## is still packed. Money is deliberately NOT consulted: affordability is
	## the unpack ACTION's concern, and folding it in here would make the
	## condition flicker with the treasury.
	var _sim = sim
	if not _sim.unpackable_bases.has(base_name):
		return {"known": false}
	var row: Dictionary = _sim.unpackable_bases[base_name]
	return {
		"known": true,
		"unpackable": _sim.base_loop_enabled and _sim.winner == -1 and int(row.get("unpacked_by", -1)) < 0,
	}


func unpack_base(team: int, base_name: String, free: bool) -> Dictionary:
	## NAMED_BASE_UNPACK (`free` false) / NAMED_BASE_UNPACK_FREE (`free` true):
	## the flag at `base_name` unpacks into a completed fortress (with expansion
	## pads) owned by `team`. Paid unpacks charge the flag's authored cost; the
	## free variant charges nothing. Returns {"ok": true, "structure_id": id,
	## "cost": paid} or {"ok": false, "reason": ...}.
	var _sim = sim
	if not _sim.base_loop_enabled or _sim.winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not _sim.unpackable_bases.has(base_name):
		return {"ok": false, "reason": "unknown-base"}
	if not _sim._roster_team_ids().has(team):
		return {"ok": false, "reason": "unknown-team"}
	var row: Dictionary = _sim.unpackable_bases[base_name]
	if int(row.get("unpacked_by", -1)) >= 0:
		return {"ok": false, "reason": "base-already-unpacked"}
	var cost := 0 if free else int(row.get("cost", 0))
	if _sim.resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	_sim.team_resources[team] = _sim.resources_for_team(team) - cost
	var structure_id = _sim._next_dynamic_structure_id
	_sim._next_dynamic_structure_id += 1
	var maximum_health := int(row.get("health", 1000))
	var position := Vector2(row.get("position", Vector2.ZERO))
	_sim._note_structure_table_mutation()
	_sim.structures[structure_id] = {
		"id": structure_id,
		"team": team,
		"kind": "structure",
		"structure_kind": "fortress",
		"name": "Fortress",
		"position": position,
		"rally": position + Vector2(4.0, 0.0),
		"health": maximum_health,
		"maximum_health": maximum_health,
		"construction_progress": 1.0,
		"level": 1,
		"completed_upgrades": [],
		"upgrade_queue": [],
		"production": [],
		"queue": [],
		"damage_remainders": {},
		"income_per_payout": 0,
		"unpacked_from_base": base_name,
	}
	var fortress_build_rule: Dictionary = (
		_sim.structure_build_rules_for_team(team).get("fortress", {}) as Dictionary
	)
	if bool(fortress_build_rule.get("highlander_body", false)):
		_sim.structures[structure_id]["highlander_body"] = true
	# Prefer faction fortress source id so castleBehavior BSE contracts resolve.
	var fortress_sources: Dictionary = _sim.structure_source_object_ids_for_team(team)
	var fortress_aliases: Array = fortress_sources.get("fortress", []) as Array
	if not fortress_aliases.is_empty():
		_sim.structures[structure_id]["source_object_id"] = String(fortress_aliases[0])
	_sim._apply_structure_create_grants(
		_sim.structures[structure_id] as Dictionary, true, true
	)
	_sim._apply_structure_inherit_upgrades(_sim.structures[structure_id] as Dictionary)
	_sim._initialize_structure_auto_deposit(_sim.structures[structure_id] as Dictionary)
	_seed_expansion_pads_for(structure_id)
	row["unpacked_by"] = team
	row["structure_id"] = structure_id
	_sim._emit_event("base.unpacked", 0, structure_id, {"team": team, "base": base_name, "cost": cost, "free": free})
	return {"ok": true, "structure_id": structure_id, "cost": cost}


# --- Map named-object namespace (retail's COMPLETE name table) --------------
#
# The CLOSED set of script object names the installed map authors (schema-v1
# world.namedObjects, recorded by the install seam when the importer decoded
# the map's script world - world.available true). Retail's ScriptEngine name
# table (m_namedObjects, GPL Generals ScriptEngine.h + whale reversal) is
# built from objects PLACED ON THE MAP; imported script LIBRARIES contribute
# scripts and teams, never objects, so a name a library authors but the map
# does not place resolves to NULL and every named condition answers FALSE.
# This table is what lets the script-world adapter give that retail FALSE for
# a name genuinely absent from the map (case b of the three-way split in
# SliceUnits) instead of refusing: with the namespace declared, "the sim does
# not model that name" AND "the map does not author it" together reproduce
# retail's grounded false. Names the map DOES author but the sim does not
# model keep refusing (case c) - retail would answer from a real object there.
#
# SIM-owned (not adapter state) for the sim.script_unit_references reason: the
# answer changes script outcomes, so a peer adopting a snapshot must answer
# named conditions exactly as the peer that minted it.
#
# HASH INERTNESS: participates in the authoritative state ONLY when declared
# (empty-is-absent, the sim.unpackable_bases discipline), so every scriptless
# scenario keeps its frozen cross-platform pin untouched.

## {} = no authoritative namespace declared (every unknown name refuses);
## {"names": {name: true, ...}} = the map's complete named-object table
## (declared even when the map authors zero names - then EVERY unknown name
## is retail-false). Match configuration: set by the install seam, kept
## across reset_match() like script-team identities.
