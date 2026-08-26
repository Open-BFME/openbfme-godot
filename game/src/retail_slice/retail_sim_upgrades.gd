extends RefCounted
## Upgrade subsystem extracted from retail_slice_sim.gd (Q81 strangler-fig
## extraction #10): structure/battalion upgrade queues, stepping, granted/
## inherited application. Verbatim move, compiler-guided sim. prefixes,
## pin-verified byte-identical.

var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()


func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)


func queue_structure_upgrade(team: int, structure_id: int, upgrade_id: String) -> Dictionary:
	if not sim.base_loop_enabled or sim.winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not sim.structures.has(structure_id):
		return {"ok": false, "reason": "unknown-structure"}
	var building: Dictionary = sim.structures[structure_id]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(building.get("health", 0)) <= 0 or float(building.get("construction_progress", 0.0)) < 1.0:
		return {"ok": false, "reason": "structure-unavailable"}
	var contract: Dictionary = sim.structure_upgrade_contracts_for_team(team).get(upgrade_id, {})
	if contract.is_empty() or String(contract.get("structure_kind", "")) != String(building.get("structure_kind", "")):
		return {"ok": false, "reason": "unsupported-upgrade"}
	if Array(building.get("completed_upgrades", [])).has(upgrade_id):
		return {"ok": false, "reason": "already-completed"}
	if bool(contract.get("team_tech", false)) and (sim.team_upgrades.get(team, {}) as Dictionary).has(upgrade_id):
		# Team techs are owned once, no matter which forge researched them.
		return {"ok": false, "reason": "already-completed"}
	var queue: Array = building.get("upgrade_queue", [])
	if not queue.is_empty():
		return {"ok": false, "reason": "upgrade-in-progress"}
	if int(building.get("level", 1)) >= int(contract.get("level_cap", 1)):
		return {"ok": false, "reason": "level-cap"}
	var required_prior := String(contract.get("requires_upgrade_id", ""))
	if required_prior != "" and not Array(building.get("completed_upgrades", [])).has(required_prior):
		# The authored chain sells each step on the command set the prior step
		# unlocks; L3 can never be purchased before L2.
		return {"ok": false, "reason": "missing-prior-upgrade", "required_upgrade": required_prior}
	var missing_gate = sim._research_gate_unsatisfied(team, building, contract)
	if missing_gate != "":
		# The button's authored NeededUpgrade row (a team technology or a
		# structure level) gates the research; retail shows it greyed.
		return {"ok": false, "reason": "missing-upgrade", "required_upgrade": missing_gate}
	var cost := maxi(0, int(contract.get("cost", 0)))
	if sim.resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	var duration_ticks := maxi(1, int(contract.get("duration_ticks", 1)))
	var item := {
		"upgrade_id": upgrade_id,
		"cost": cost,
		"queued_tick": sim.tick_index,
		"duration_ticks": duration_ticks,
		"complete_tick": sim.tick_index + duration_ticks,
		"cancelable": bool(contract.get("cancelable", false)),
	}
	queue.append(item)
	building["upgrade_queue"] = queue
	sim.team_resources[team] = sim.resources_for_team(team) - cost
	sim._emit_event("upgrade.queued", structure_id, 0, {"team": team, "upgrade_id": upgrade_id, "complete_tick": int(item["complete_tick"])})
	return {"ok": true, "reason": "", "structure_id": structure_id, "item": item.duplicate(true)}


func structure_upgrade_queue_state(structure_id: int) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	if not sim.structures.has(structure_id):
		return result
	for item_value in Array((sim.structures[structure_id] as Dictionary).get("upgrade_queue", [])):
		if typeof(item_value) != TYPE_DICTIONARY:
			continue
		var item := item_value as Dictionary
		var duration_ticks := maxi(1, int(item.get("duration_ticks", 1)))
		var elapsed_ticks := clampi(sim.tick_index - int(item.get("queued_tick", sim.tick_index)), 0, duration_ticks)
		var row := item.duplicate(true)
		row["elapsed_ticks"] = elapsed_ticks
		row["progress"] = float(elapsed_ticks) / float(duration_ticks)
		result.append(row)
	return result


## Wall-upgrade slot types already named this match (one line per type).


func structure_upgrade_commands(structure_id: int) -> Array[Dictionary]:
	## The building's purchasable upgrade steps on its CURRENT command set,
	## doc-driven: the chain step whose authored from-command-set matches the
	## building's live set (its base set before any purchase). One entry per
	## pending step; completed/capped chains surface nothing.
	var result: Array[Dictionary] = []
	if not sim.structures.has(structure_id):
		return result
	var building: Dictionary = sim.structures[structure_id]
	var kind := String(building.get("structure_kind", ""))
	var current_set := String(building.get("command_set", ""))
	var completed: Array = building.get("completed_upgrades", [])
	var contracts = sim.structure_upgrade_contracts_for_team(int(building.get("team", -1)))
	var upgrade_ids: Array[String] = []
	for upgrade_id_value in contracts.keys():
		upgrade_ids.append(String(upgrade_id_value))
	upgrade_ids.sort()
	for upgrade_id in upgrade_ids:
		var contract: Dictionary = contracts[upgrade_id]
		if String(contract.get("structure_kind", "")) != kind:
			continue
		if bool(contract.get("team_tech", false)):
			if not bool(contract.get("research", false)):
				# Legacy team techs ride their own surface; compiled research
				# rows surface here exactly like chain steps.
				continue
			if (sim.team_upgrades.get(int(building.get("team", -1)), {}) as Dictionary).has(upgrade_id):
				continue
			# Research rides every per-level command set of the building, so no
			# from-set match applies; the authored NeededUpgrade gate is data.
			var row := {
				"upgrade_id": upgrade_id,
				"command_id": String(contract.get("command_id", "")),
				"cost": int(contract.get("cost", 0)),
				"duration_ticks": int(contract.get("duration_ticks", 1)),
				"to_level": 0,
				"cancelable": bool(contract.get("cancelable", false)),
				"slot": int(contract.get("slot", 0)),
				"label_id": String(contract.get("label_id", "")),
				"tooltip_id": String(contract.get("tooltip_id", "")),
				"image_id": String(contract.get("image_id", "")),
				"research": true,
				"lacks_prerequisite_label_id": String(contract.get("lacks_prerequisite_label_id", "")),
				"needed_upgrade_ids": Array(contract.get("needed_upgrade_ids", [])).duplicate(),
				"gate_satisfied": sim._research_gate_unsatisfied(int(building.get("team", -1)), building, contract) == "",
			}
			result.append(row)
			continue
		if completed.has(upgrade_id):
			continue
		if bool(contract.get("castle_upgrade", false)):
			# Fortress improvements ride the fortress's upgrades page on EVERY
			# command set it can be on (they never swap it), so no from-set match
			# applies — exactly like compiled research, but bought per building.
			result.append({
				"upgrade_id": upgrade_id,
				"command_id": String(contract.get("command_id", "")),
				"cost": int(contract.get("cost", 0)),
				"duration_ticks": int(contract.get("duration_ticks", 1)),
				"to_level": 0,
				"cancelable": bool(contract.get("cancelable", false)),
				"slot": int(contract.get("slot", 0)),
				"label_id": String(contract.get("label_id", "")),
				"tooltip_id": String(contract.get("tooltip_id", "")),
				"image_id": String(contract.get("image_id", "")),
				"castle_upgrade": true,
				"grants_upgrade_id": String(contract.get("grants_upgrade_id", "")),
				"lacks_prerequisite_label_id": String(contract.get("lacks_prerequisite_label_id", "")),
				"needed_upgrade_ids": Array(contract.get("needed_upgrade_ids", [])).duplicate(),
				"gate_satisfied": sim._research_gate_unsatisfied(int(building.get("team", -1)), building, contract) == "",
			})
			continue
		var from_set := String(contract.get("from_command_set", ""))
		if current_set == "":
			# Before any purchase the building sits on its base command set: the
			# chain step whose from-set is not itself another step's to-set.
			var is_downstream := false
			for other_id in upgrade_ids:
				var other: Dictionary = contracts[other_id]
				if String(other.get("structure_kind", "")) == kind and String(other.get("to_command_set", "")) == from_set:
					is_downstream = true
					break
			if is_downstream:
				continue
		elif from_set != current_set:
			continue
		result.append({
			"upgrade_id": upgrade_id,
			"command_id": String(contract.get("command_id", "")),
			"cost": int(contract.get("cost", 0)),
			"duration_ticks": int(contract.get("duration_ticks", 1)),
			"to_level": int(contract.get("to_level", 0)),
			"cancelable": bool(contract.get("cancelable", false)),
			"slot": int(contract.get("slot", 0)),
			"label_id": String(contract.get("label_id", "")),
			"tooltip_id": String(contract.get("tooltip_id", "")),
			"image_id": String(contract.get("image_id", "")),
		})
	return result


## --- Per-battalion OBJECT upgrade purchases ---
## Retail's two-tier armory: a PLAYER technology researched at a building
## unlocks each horde's authored OBJECT_UPGRADE buttons. Eligibility is the
## unit document's compiled command set cross-checked against its compiled
## weapon/armor/level effect tables — never a class-name split. The purchase
## costs the authored amount (less any authored discount the team's
## sim.structures declare) and applies to that one battalion.


func queue_battalion_upgrade(team: int, entity_id: int, upgrade_id: String) -> Dictionary:
	if not sim.base_loop_enabled or sim.winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not sim.entities.has(entity_id):
		return {"ok": false, "reason": "unknown-entity"}
	var row: Dictionary = sim.entities[entity_id]
	if int(row.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "entity-unavailable"}
	var command: Dictionary = {}
	for candidate_value in _sorted_unit_upgrade_commands(String(row.get("unit_type", ""))):
		if String((candidate_value as Dictionary).get("upgrade_id", "")) == upgrade_id:
			command = candidate_value
			break
	if command.is_empty():
		# Never authored for this unit: the compiled document offers no button.
		return {"ok": false, "reason": "unsupported-upgrade"}
	var applied: Dictionary = row.get("applied_upgrades", {})
	if applied.has(upgrade_id):
		return {"ok": false, "reason": "already-completed"}
	var queue: Array = row.get("upgrade_queue", [])
	if not queue.is_empty():
		return {"ok": false, "reason": "upgrade-in-progress"}
	var missing = sim._battalion_gate_unsatisfied(team, command)
	if missing != "":
		return {"ok": false, "reason": "missing-upgrade", "required_upgrade": missing}
	var cost := _discounted_battalion_upgrade_cost(team, command)
	if sim.resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	var duration_ticks := maxi(1, int(command.get("duration_ticks", 1)))
	var item := {
		"upgrade_id": upgrade_id,
		"cost": cost,
		"queued_tick": sim.tick_index,
		"duration_ticks": duration_ticks,
		"complete_tick": sim.tick_index + duration_ticks,
		"cancelable": bool(command.get("cancelable", false)),
	}
	queue.append(item)
	row["upgrade_queue"] = queue
	sim.team_resources[team] = sim.resources_for_team(team) - cost
	sim._emit_event("battalion_upgrade.queued", 0, entity_id, {"team": team, "upgrade_id": upgrade_id, "complete_tick": int(item["complete_tick"])})
	return {"ok": true, "reason": "", "entity_id": entity_id, "item": item.duplicate(true)}


func battalion_upgrade_queue_state(entity_id: int) -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	if not sim.entities.has(entity_id):
		return result
	for item_value in Array((sim.entities[entity_id] as Dictionary).get("upgrade_queue", [])):
		if typeof(item_value) != TYPE_DICTIONARY:
			continue
		var item := item_value as Dictionary
		var duration_ticks := maxi(1, int(item.get("duration_ticks", 1)))
		var elapsed_ticks := clampi(sim.tick_index - int(item.get("queued_tick", sim.tick_index)), 0, duration_ticks)
		var row := item.duplicate(true)
		row["elapsed_ticks"] = elapsed_ticks
		row["progress"] = float(elapsed_ticks) / float(duration_ticks)
		result.append(row)
	return result


func _step_structure_upgrades() -> void:
	for structure_id in sim.structure_ids():
		var building: Dictionary = sim.structures[structure_id]
		if int(building.get("health", 0)) <= 0:
			continue
		var queue: Array = building.get("upgrade_queue", [])
		if queue.is_empty():
			continue
		var item: Dictionary = queue[0]
		if sim.tick_index < int(item.get("complete_tick", sim.tick_index + 1)):
			continue
		var upgrade_id := String(item.get("upgrade_id", ""))
		var contract: Dictionary = sim.structure_upgrade_contracts_for_team(int(building.get("team", -1))).get(upgrade_id, {})
		if contract.is_empty():
			sim.configuration_error = "queued structure upgrade lost its contract"
			continue
		queue.pop_front()
		building["upgrade_queue"] = queue
		var completed: Array = building.get("completed_upgrades", [])
		if not completed.has(upgrade_id):
			completed.append(upgrade_id)
		building["completed_upgrades"] = completed
		# Retail's CastleUpgrade hop: a fortress improvement button buys the
		# *Trigger* upgrade, and the fortress's own CastleUpgrade module hands
		# the real upgrade to the castle. Without this the purchase completes
		# and nothing downstream ever fires.
		sim._apply_castle_upgrade_grants(building, upgrade_id)
		if bool(contract.get("team_tech", false)):
			var team := int(building.get("team", -1))
			var owned: Dictionary = sim.team_upgrades.get(team, {}) as Dictionary
			owned[upgrade_id] = true
			sim.team_upgrades[team] = owned
			sim._refresh_team_command_set_upgrades(team)
			if bool(contract.get("legacy_provisional", false)):
				# Recorded provisional only (stale pack): the conflated research
				# auto-equips matching hordes. Compiled research grants the
				# technology; the per-battalion purchase path equips it.
				_apply_team_upgrade_to_hordes(team, upgrade_id)
		building["level"] = mini(
			int(contract.get("level_cap", 1)),
			int(building.get("level", 1)) + int(contract.get("levels_to_gain", 0))
		)
		if not bool(contract.get("castle_upgrade", false)):
			# A fortress improvement never swaps the command set (retail keeps
			# the fortress on its own set and only flips model/weapon state), so
			# it must not blank the building's live set on completion.
			building["command_set"] = String(contract.get("to_command_set", ""))
		# Per-level authored effects ride the completed upgrade: health additions
		# raise the building's pool, PRODUCTION factors compound into its
		# build-speed multiplier (SAGE level modifiers are permanent).
		var health_add := int(contract.get("health_add", 0))
		if health_add != 0:
			building["maximum_health"] = int(building.get("maximum_health", 0)) + health_add
			building["health"] = int(building.get("health", 0)) + health_add
		var production_factor := float(contract.get("production_multiplier", 1.0))
		if production_factor != 1.0:
			building["production_multiplier"] = snappedf(
				float(building.get("production_multiplier", 1.0)) * production_factor,
				0.0001
			)
		sim._emit_event("upgrade.completed", structure_id, 0, {
			"team": int(building.get("team", -1)),
			"upgrade_id": upgrade_id,
			"level": int(building["level"]),
			"command_set": String(building.get("command_set", "")),
			"health_add": health_add,
			"production_multiplier": float(building.get("production_multiplier", 1.0)),
		})


func _step_battalion_upgrades() -> void:
	for entity_id in sim.entity_ids():
		var row: Dictionary = sim.entities[entity_id]
		if int(row.get("health", 0)) <= 0:
			continue
		var queue: Array = row.get("upgrade_queue", [])
		if queue.is_empty():
			continue
		var item: Dictionary = queue[0]
		if sim.tick_index < int(item.get("complete_tick", sim.tick_index + 1)):
			continue
		queue.pop_front()
		row["upgrade_queue"] = queue
		var upgrade_id := String(item.get("upgrade_id", ""))
		var member_id := String(row.get("object_id", ""))
		var level_rule: Dictionary = (sim._unit_level_upgrades.get(member_id, {}) as Dictionary).get(upgrade_id, {})
		if not level_rule.is_empty():
			# Basic Training: the authored LevelUpUpgrade grants its level; the
			# banner-carrier member visual is the recorded unsupported remainder.
			var next_level := mini(
				int(level_rule.get("level_cap", 2)),
				int(row.get("level", 1)) + int(level_rule.get("levels_to_gain", 1))
			)
			row["level"] = next_level
			sim._refresh_banner_carrier_state(row)
			var applied: Dictionary = row.get("applied_upgrades", {})
			applied[upgrade_id] = sim.tick_index
			row["applied_upgrades"] = applied
			sim._emit_event("battalion.upgrade_applied", 0, entity_id, {
				"team": int(row.get("team", -1)),
				"upgrades": [upgrade_id],
				"unsupported_effects": ["banner-carrier-member-spawn"],
			})
		else:
			sim._apply_equipment_to_horde(row, [upgrade_id])
		sim._emit_event("battalion_upgrade.completed", 0, entity_id, {
			"team": int(row.get("team", -1)),
			"upgrade_id": upgrade_id,
		})


func _apply_team_upgrade_to_hordes(team: int, upgrade_id: String) -> void:
	var equipment = sim._equipment_ids_for_forge_upgrade(upgrade_id)
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		if int(row.get("team", -1)) != team:
			continue
		sim._apply_equipment_to_horde(row, equipment)


func _apply_structure_granted_upgrade(building: Dictionary, grant: Dictionary) -> void:
	var upgrade_id := String(grant.get("upgradeId", ""))
	var team := int(building.get("team", -1))
	if upgrade_id == "" or team < 0:
		return
	if String(grant.get("upgradeType", "")) == "PLAYER":
		var owned: Dictionary = sim.team_upgrades.get(team, {}) as Dictionary
		owned[upgrade_id] = true
		sim.team_upgrades[team] = owned
		sim._refresh_team_command_set_upgrades(team)
		return
	var completed: Array = building.get("completed_upgrades", [])
	if not completed.has(upgrade_id):
		completed.append(upgrade_id)
		completed.sort()
		building["completed_upgrades"] = completed


func _apply_structure_inherit_upgrades(building: Dictionary) -> void:
	## InheritUpgradeCreate is a one-shot create-module query, not an aura.
	## Search stable structure ids and require the exact authored source Object
	## identity. ObjectFilter = ANY +Type does not author owner, health, or
	## completion restrictions, so this path must not invent them.
	var team := int(building.get("team", -1))
	var kind := String(building.get("structure_kind", ""))
	var rules: Array = sim.structure_inherit_upgrades_for_team(team).get(kind, [])
	if team < 0 or rules.is_empty():
		return
	var carrier_id := int(building.get("id", 0))
	var carrier_position := Vector2(building.get("position", Vector2.ZERO))
	var scale := maxf(
		0.000001, float(sim._rules.get("source_map_transform_scale", 1.0))
	)
	var completed: Array = building.get("completed_upgrades", [])
	for rule_value in rules:
		var rule := rule_value as Dictionary
		var upgrade_id := String(rule.get("upgradeId", ""))
		var source_id := String(rule.get("sourceObjectId", ""))
		var source_kind := String(rule.get("sourceKind", ""))
		var radius_source := float(
			(rule.get("radius", {}) as Dictionary).get("value", 0.0)
		)
		if upgrade_id == "" or source_id == "" or radius_source <= 0.0:
			continue
		var limit_squared := pow(radius_source * scale, 2.0)
		for donor_id in sim.structure_ids():
			if donor_id == carrier_id:
				continue
			var donor: Dictionary = sim.structures[donor_id]
			if not Array(donor.get("completed_upgrades", [])).has(upgrade_id):
				continue
			var donor_kind := String(donor.get("structure_kind", ""))
			if source_kind != "" and donor_kind != source_kind:
				continue
			# Runtime kinds such as "fortress" are deliberately shared across
			# factions. Resolve the exact retail identity through the donor's
			# own faction manifest, never through the carrier's aliases: a
			# DwarvenFortress must not satisfy +MenFortressCitadel merely
			# because both rows use the generic fortress runtime kind.
			var donor_team := int(donor.get("team", -1))
			var donor_source_ids = sim.structure_source_object_ids_for_team(donor_team)
			var aliases: Array = donor_source_ids.get(donor_kind, [])
			var exact_source_match := false
			for alias_value in aliases:
				if String(alias_value).nocasecmp_to(source_id) == 0:
					exact_source_match = true
					break
			if not exact_source_match:
				continue
			var donor_position := Vector2(donor.get("position", Vector2.ZERO))
			if carrier_position.distance_squared_to(donor_position) > limit_squared:
				continue
			if not completed.has(upgrade_id):
				completed.append(upgrade_id)
				completed.sort()
				building["completed_upgrades"] = completed
			break


func _sorted_upgrade_ids(applied: Variant) -> Array[String]:
	## Deterministic snapshot view of a horde's recorded equipment upgrades.
	var ids: Array[String] = []
	if typeof(applied) != TYPE_DICTIONARY:
		return ids
	for key_value in (applied as Dictionary).keys():
		ids.append(String(key_value))
	ids.sort()
	return ids


func _sorted_unit_upgrade_commands(unit_type: String) -> Array:
	## Deterministic surface order: authored slot first, then upgrade id.
	var rows: Array = Array(sim._unit_upgrade_commands.get(unit_type, [])).duplicate(true)
	rows.sort_custom(func(a: Dictionary, b: Dictionary) -> bool:
		if int(a.get("slot", 0)) != int(b.get("slot", 0)):
			return int(a.get("slot", 0)) < int(b.get("slot", 0))
		return String(a.get("upgrade_id", "")).naturalnocasecmp_to(String(b.get("upgrade_id", ""))) < 0
	)
	return rows


func _discounted_battalion_upgrade_cost(team: int, command: Dictionary) -> int:
	## The authored CostModifierUpgrade discount (Iron Ore) applies while the
	## team owns the technology and fields the structure declaring it.
	var cost := int(command.get("cost", 0))
	var upgrade_id := String(command.get("upgrade_id", ""))
	var owned: Dictionary = sim.team_upgrades.get(team, {}) as Dictionary
	for structure_id in sim.structure_ids(team):
		var building: Dictionary = sim.structures[structure_id]
		if int(building.get("health", 0)) <= 0:
			continue
		var bundle: Dictionary = sim.structure_upgrade_effects_for_team(team).get(String(building.get("structure_kind", "")), {})
		for effect_value in Array(bundle.get("effects", [])):
			var effect := effect_value as Dictionary
			if String(effect.get("kind", "")) != "upgrade-discount" or not bool(effect.get("upgrade_discount", false)):
				continue
			if not owned.has(String(effect.get("upgrade_id", ""))):
				continue
			var covered := false
			for covered_value in Array(effect.get("apply_to_upgrade_ids", [])):
				if String(covered_value) == upgrade_id:
					covered = true
					break
			if covered:
				cost = roundi(cost * (100.0 + float(effect.get("percent", 0.0))) / 100.0)
	return maxi(0, cost)
