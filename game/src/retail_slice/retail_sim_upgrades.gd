extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Upgrade subsystem extracted from retail_slice_sim.gd (Q81 strangler-fig
## extraction #10): structure/battalion upgrade queues, stepping, granted/
## inherited application. Verbatim move, compiler-guided sim. prefixes,
## pin-verified byte-identical.





func queue_structure_upgrade(team: int, structure_id: int, upgrade_id: String) -> Dictionary:
	var _sim = sim
	if not _sim.base_loop_enabled or _sim.winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not _sim.structures.has(structure_id):
		return {"ok": false, "reason": "unknown-structure"}
	var building: Dictionary = _sim.structures[structure_id]
	if int(building.get("team", -1)) != team:
		return {"ok": false, "reason": "wrong-owner"}
	if int(building.get("health", 0)) <= 0 or float(building.get("construction_progress", 0.0)) < 1.0:
		return {"ok": false, "reason": "structure-unavailable"}
	var contract: Dictionary = _sim.structure_upgrade_contracts_for_team(team).get(upgrade_id, {})
	if contract.is_empty() or String(contract.get("structure_kind", "")) != String(building.get("structure_kind", "")):
		return {"ok": false, "reason": "unsupported-upgrade"}
	if Array(building.get("completed_upgrades", [])).has(upgrade_id):
		return {"ok": false, "reason": "already-completed"}
	if bool(contract.get("team_tech", false)) and (_sim.team_upgrades.get(team, {}) as Dictionary).has(upgrade_id):
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
	var missing_gate = _sim._research_gate_unsatisfied(team, building, contract)
	if missing_gate != "":
		# The button's authored NeededUpgrade row (a team technology or a
		# structure level) gates the research; retail shows it greyed.
		return {"ok": false, "reason": "missing-upgrade", "required_upgrade": missing_gate}
	var cost := maxi(0, int(contract.get("cost", 0)))
	if _sim.resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	var duration_ticks := maxi(1, int(contract.get("duration_ticks", 1)))
	var item := {
		"upgrade_id": upgrade_id,
		"cost": cost,
		"queued_tick": _sim.tick_index,
		"duration_ticks": duration_ticks,
		"complete_tick": _sim.tick_index + duration_ticks,
		"cancelable": bool(contract.get("cancelable", false)),
	}
	queue.append(item)
	building["upgrade_queue"] = queue
	_sim.team_resources[team] = _sim.resources_for_team(team) - cost
	_sim._emit_event("upgrade.queued", structure_id, 0, {"team": team, "upgrade_id": upgrade_id, "complete_tick": int(item["complete_tick"])})
	return {"ok": true, "reason": "", "structure_id": structure_id, "item": item.duplicate(true)}


func structure_upgrade_queue_state(structure_id: int) -> Array[Dictionary]:
	var _sim = sim
	var result: Array[Dictionary] = []
	if not _sim.structures.has(structure_id):
		return result
	for item_value in Array((_sim.structures[structure_id] as Dictionary).get("upgrade_queue", [])):
		if typeof(item_value) != TYPE_DICTIONARY:
			continue
		var item := item_value as Dictionary
		var duration_ticks := maxi(1, int(item.get("duration_ticks", 1)))
		var elapsed_ticks := clampi(_sim.tick_index - int(item.get("queued_tick", _sim.tick_index)), 0, duration_ticks)
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
	var _sim = sim
	var result: Array[Dictionary] = []
	if not _sim.structures.has(structure_id):
		return result
	var building: Dictionary = _sim.structures[structure_id]
	var kind := String(building.get("structure_kind", ""))
	var current_set := String(building.get("command_set", ""))
	var completed: Array = building.get("completed_upgrades", [])
	var contracts = _sim.structure_upgrade_contracts_for_team(int(building.get("team", -1)))
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
			if (_sim.team_upgrades.get(int(building.get("team", -1)), {}) as Dictionary).has(upgrade_id):
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
				"gate_satisfied": _sim._research_gate_unsatisfied(int(building.get("team", -1)), building, contract) == "",
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
				"gate_satisfied": _sim._research_gate_unsatisfied(int(building.get("team", -1)), building, contract) == "",
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
	var _sim = sim
	if not _sim.base_loop_enabled or _sim.winner != -1:
		return {"ok": false, "reason": "match-unavailable"}
	if not _sim.entities.has(entity_id):
		return {"ok": false, "reason": "unknown-entity"}
	var row: Dictionary = _sim.entities[entity_id]
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
	var missing = _sim._battalion_gate_unsatisfied(team, command)
	if missing != "":
		return {"ok": false, "reason": "missing-upgrade", "required_upgrade": missing}
	var cost := _discounted_battalion_upgrade_cost(team, command)
	if _sim.resources_for_team(team) < cost:
		return {"ok": false, "reason": "insufficient-resources", "cost": cost}
	var duration_ticks := maxi(1, int(command.get("duration_ticks", 1)))
	var item := {
		"upgrade_id": upgrade_id,
		"cost": cost,
		"queued_tick": _sim.tick_index,
		"duration_ticks": duration_ticks,
		"complete_tick": _sim.tick_index + duration_ticks,
		"cancelable": bool(command.get("cancelable", false)),
	}
	queue.append(item)
	row["upgrade_queue"] = queue
	_sim.team_resources[team] = _sim.resources_for_team(team) - cost
	_sim._emit_event("battalion_upgrade.queued", 0, entity_id, {"team": team, "upgrade_id": upgrade_id, "complete_tick": int(item["complete_tick"])})
	return {"ok": true, "reason": "", "entity_id": entity_id, "item": item.duplicate(true)}


func battalion_upgrade_queue_state(entity_id: int) -> Array[Dictionary]:
	var _sim = sim
	var result: Array[Dictionary] = []
	if not _sim.entities.has(entity_id):
		return result
	for item_value in Array((_sim.entities[entity_id] as Dictionary).get("upgrade_queue", [])):
		if typeof(item_value) != TYPE_DICTIONARY:
			continue
		var item := item_value as Dictionary
		var duration_ticks := maxi(1, int(item.get("duration_ticks", 1)))
		var elapsed_ticks := clampi(_sim.tick_index - int(item.get("queued_tick", _sim.tick_index)), 0, duration_ticks)
		var row := item.duplicate(true)
		row["elapsed_ticks"] = elapsed_ticks
		row["progress"] = float(elapsed_ticks) / float(duration_ticks)
		result.append(row)
	return result


func _step_structure_upgrades() -> void:
	var _sim = sim
	for structure_id in _sim.structure_ids():
		var building: Dictionary = _sim.structures[structure_id]
		if int(building.get("health", 0)) <= 0:
			continue
		var queue: Array = building.get("upgrade_queue", [])
		if queue.is_empty():
			continue
		var item: Dictionary = queue[0]
		if _sim.tick_index < int(item.get("complete_tick", _sim.tick_index + 1)):
			continue
		var upgrade_id := String(item.get("upgrade_id", ""))
		var contract: Dictionary = _sim.structure_upgrade_contracts_for_team(int(building.get("team", -1))).get(upgrade_id, {})
		if contract.is_empty():
			_sim.configuration_error = "queued structure upgrade lost its contract"
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
		_sim._apply_castle_upgrade_grants(building, upgrade_id)
		if bool(contract.get("team_tech", false)):
			var team := int(building.get("team", -1))
			var owned: Dictionary = _sim.team_upgrades.get(team, {}) as Dictionary
			owned[upgrade_id] = true
			_sim.team_upgrades[team] = owned
			_sim._refresh_team_command_set_upgrades(team)
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
		_sim._emit_event("upgrade.completed", structure_id, 0, {
			"team": int(building.get("team", -1)),
			"upgrade_id": upgrade_id,
			"level": int(building["level"]),
			"command_set": String(building.get("command_set", "")),
			"health_add": health_add,
			"production_multiplier": float(building.get("production_multiplier", 1.0)),
		})


func _step_battalion_upgrades() -> void:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		var row: Dictionary = _sim.entities[entity_id]
		if int(row.get("health", 0)) <= 0:
			continue
		var queue: Array = row.get("upgrade_queue", [])
		if queue.is_empty():
			continue
		var item: Dictionary = queue[0]
		if _sim.tick_index < int(item.get("complete_tick", _sim.tick_index + 1)):
			continue
		queue.pop_front()
		row["upgrade_queue"] = queue
		var upgrade_id := String(item.get("upgrade_id", ""))
		var member_id := String(row.get("object_id", ""))
		var level_rule: Dictionary = (_sim._unit_level_upgrades.get(member_id, {}) as Dictionary).get(upgrade_id, {})
		if not level_rule.is_empty():
			# Basic Training: the authored LevelUpUpgrade grants its level; the
			# banner-carrier member visual is the recorded unsupported remainder.
			var next_level := mini(
				int(level_rule.get("level_cap", 2)),
				int(row.get("level", 1)) + int(level_rule.get("levels_to_gain", 1))
			)
			row["level"] = next_level
			_sim._refresh_banner_carrier_state(row)
			var applied: Dictionary = row.get("applied_upgrades", {})
			applied[upgrade_id] = _sim.tick_index
			row["applied_upgrades"] = applied
			_sim._emit_event("battalion.upgrade_applied", 0, entity_id, {
				"team": int(row.get("team", -1)),
				"upgrades": [upgrade_id],
				"unsupported_effects": ["banner-carrier-member-spawn"],
			})
		else:
			_sim._apply_equipment_to_horde(row, [upgrade_id])
		_sim._emit_event("battalion_upgrade.completed", 0, entity_id, {
			"team": int(row.get("team", -1)),
			"upgrade_id": upgrade_id,
		})


func _apply_team_upgrade_to_hordes(team: int, upgrade_id: String) -> void:
	var _sim = sim
	var equipment = _sim._equipment_ids_for_forge_upgrade(upgrade_id)
	for id in _sim.entity_ids():
		var row: Dictionary = _sim.entities[id]
		if int(row.get("team", -1)) != team:
			continue
		_sim._apply_equipment_to_horde(row, equipment)


func _apply_structure_granted_upgrade(building: Dictionary, grant: Dictionary) -> void:
	var _sim = sim
	var upgrade_id := String(grant.get("upgradeId", ""))
	var team := int(building.get("team", -1))
	if upgrade_id == "" or team < 0:
		return
	if String(grant.get("upgradeType", "")) == "PLAYER":
		var owned: Dictionary = _sim.team_upgrades.get(team, {}) as Dictionary
		owned[upgrade_id] = true
		_sim.team_upgrades[team] = owned
		_sim._refresh_team_command_set_upgrades(team)
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
	var _sim = sim
	var team := int(building.get("team", -1))
	var kind := String(building.get("structure_kind", ""))
	var rules: Array = _sim.structure_inherit_upgrades_for_team(team).get(kind, [])
	if team < 0 or rules.is_empty():
		return
	var carrier_id := int(building.get("id", 0))
	var carrier_position := Vector2(building.get("position", Vector2.ZERO))
	var scale := maxf(
		0.000001, float(_sim._rules.get("source_map_transform_scale", 1.0))
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
		for donor_id in _sim.structure_ids():
			if donor_id == carrier_id:
				continue
			var donor: Dictionary = _sim.structures[donor_id]
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
			var donor_source_ids = _sim.structure_source_object_ids_for_team(donor_team)
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
	var _sim = sim
	var cost := int(command.get("cost", 0))
	var upgrade_id := String(command.get("upgrade_id", ""))
	var owned: Dictionary = _sim.team_upgrades.get(team, {}) as Dictionary
	for structure_id in _sim.structure_ids(team):
		var building: Dictionary = _sim.structures[structure_id]
		if int(building.get("health", 0)) <= 0:
			continue
		var bundle: Dictionary = _sim.structure_upgrade_effects_for_team(team).get(String(building.get("structure_kind", "")), {})
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

func _register_forge_upgrade_contracts_for_team(team: int) -> void:
	var _sim = sim
	if not _sim.structure_build_rules_for_team(team).has("forge"):
		return
	if _sim.compiled_research_kinds_for_team(team).has("forge"):
		# The compiled research surface (the forge's authored PLAYER technology
		# sales) replaces the recorded provisional contracts below; research
		# completion then grants the technology and the per-battalion purchase
		# path equips it — the retail two-tier flow, not the conflation.
		return
	var contracts = _sim.structure_upgrade_contracts_for_team(team)
	for upgrade_id_value in _sim.FORGE_UPGRADE_CONTRACTS.keys():
		var upgrade_id := String(upgrade_id_value)
		if contracts.has(upgrade_id):
			continue
		var source: Dictionary = _sim.FORGE_UPGRADE_CONTRACTS[upgrade_id]
		contracts[upgrade_id] = {
			"structure_kind": String(source["structure_kind"]),
			"cost": int(source["cost"]),
			"duration_ticks": maxi(1, roundi(float(source["duration_seconds"]) / _sim.TICK_SECONDS)),
			"level_cap": int(source["level_cap"]),
			"levels_to_gain": int(source["levels_to_gain"]),
			"cancelable": bool(source["cancelable"]),
			"to_command_set": "",
			"team_tech": true,
			# Recorded provisional (stale pack without the compiled research
			# surface): completion still auto-equips matching hordes.
			"legacy_provisional": true,
		}


func _equipment_ids_for_forge_upgrade(upgrade_id: String) -> Array:
	## The OBJECT upgrade ids a completed research equips per horde (defaults
	## to the research id itself; fire arrows map to both authored buttons).
	return Array(sim.FORGE_UPGRADE_EQUIPMENT.get(upgrade_id, [upgrade_id]))


func _apply_equipment_to_horde(row: Dictionary, equipment: Array) -> void:
	## Record the compiled armor/weapon upgrade effects a horde carries. Retail
	## applies equipment per battalion; the slice records it per horde row.
	var _sim = sim
	var object_id := String(row.get("object_id", ""))
	var armor_upgrades: Dictionary = (_sim._unit_armor.get(object_id, {}) as Dictionary).get("upgrades", {})
	var weapon_upgrades: Dictionary = _sim._unit_weapon_upgrades.get(object_id, {})
	var applied: Dictionary = row.get("applied_upgrades", {})
	var changed := false
	for upgrade_id_value in equipment:
		var upgrade_id := String(upgrade_id_value)
		if applied.has(upgrade_id):
			continue
		if armor_upgrades.has(upgrade_id):
			applied[upgrade_id] = _sim.tick_index
			# Retail ArmorUpgrade swaps the ArmorSet; the last applied swap wins.
			row["active_armor_upgrade"] = upgrade_id
			changed = true
		elif weapon_upgrades.has(upgrade_id):
			applied[upgrade_id] = _sim.tick_index
			changed = true
	if changed:
		row["applied_upgrades"] = applied
		_sim._emit_event("battalion.upgrade_applied", 0, int(row.get("id", 0)), {"team": int(row.get("team", -1)), "upgrades": applied.keys()})


func _apply_structure_create_grants(
	building: Dictionary,
	apply_create_when_complete: bool,
	apply_build_complete: bool
) -> void:
	## GrantUpgradeCreate is idempotent in retail's upgrade sinks. Keep the
	## converted lifecycle edge explicit: an UNDER_CONSTRUCTION exemption may
	## grant on creation only for an already-complete object, while the BFME
	## foundation form grants when construction completes.
	var team := int(building.get("team", -1))
	var kind := String(building.get("structure_kind", ""))
	var grants: Array = sim.structure_create_grants_for_team(team).get(kind, [])
	for grant_value in grants:
		var grant := grant_value as Dictionary
		if (
			apply_create_when_complete
			and bool(grant.get("onCreateWhenComplete", false))
			and float(building.get("construction_progress", 0.0)) >= 1.0
		):
			_apply_structure_granted_upgrade(building, grant)
		if apply_build_complete and bool(grant.get("onBuildComplete", false)):
			_apply_structure_granted_upgrade(building, grant)




func _document_is_wall_upgrade_slot(document: Dictionary) -> bool:
	## A map-placed wall piece retail authors weaponless: it offers upgrade
	## commands (Upgrade_TrebuchetTurret / OpenGarrison / PosternGate) on its
	## trained command sets, or grants Upgrade_TrebuchetTurret on creation.
	var gameplay := ((document.get("registration", {}) as Dictionary).get("gameplay", {}) as Dictionary)
	for grant_value in gameplay.get("createGrants", []) as Array:
		if String(JSON.stringify(grant_value)).to_lower().contains("upgrade_trebuchetturret"):
			return true
	for set_value in gameplay.get("trainedCommandSets", []) as Array:
		for slot_value in (set_value as Dictionary).get("slots", []) as Array:
			var command_id := String((slot_value as Dictionary).get("commandId", "")).to_lower()
			if command_id.contains("trebuchetturret") or command_id.contains("opengarrison") or command_id.contains("posterngate"):
				return true
	return false


func _battalion_gate_unsatisfied(team: int, command: Dictionary) -> String:
	## "" when the purchase button's NeededUpgrade technology row is owned.
	var needed: Array = command.get("needed_upgrade_ids", [])
	if needed.is_empty():
		return ""
	var owned: Dictionary = sim.team_upgrades.get(team, {}) as Dictionary
	var satisfied := 0
	var first_missing := ""
	for needed_value in needed:
		var needed_id := String(needed_value)
		if owned.has(needed_id):
			satisfied += 1
		elif first_missing == "":
			first_missing = needed_id
	if bool(command.get("needed_upgrade_any", false)):
		return "" if satisfied > 0 else (first_missing if first_missing != "" else String(needed[0]))
	return "" if satisfied == needed.size() else first_missing


func _team_has_required_object(team: int, requirement: String) -> bool:
	## Authored BuildingRequired/UpgradeMustBePresent filters ("ANY +Object"):
	## the team must field a living structure of any named object kind.
	var _sim = sim
	var tokens := requirement.split(" ", false)
	var names: Array[String] = []
	for token in tokens:
		if token == "ANY" or token == "NONE":
			continue
		names.append(String(token).trim_prefix("+"))
	if names.is_empty():
		return requirement.strip_edges() == ""
	var registry: Dictionary = _sim._rules.get("producer_kind_registry", {}) as Dictionary
	if registry.is_empty():
		registry = (_sim._rules.get("faction_manifest", {}) as Dictionary).get("producer_kind_registry", {}) as Dictionary
	for name in names:
		var kind := ""
		for object_id_value in registry.keys():
			if String(object_id_value).to_lower() == name.to_lower():
				kind = String(registry[object_id_value])
				break
		if kind == "":
			continue
		for structure_id in _sim.structure_ids(team):
			var building: Dictionary = _sim.structures[structure_id]
			if String(building.get("structure_kind", "")) == kind and int(building.get("health", 0)) > 0:
				return true
	return false


func battalion_upgrade_commands(entity_id: int) -> Array[Dictionary]:
	## The battalion's authored purchase surface with live gate/applied/cost
	## state; the same command-surface data the building research rows ride.
	var _sim = sim
	var result: Array[Dictionary] = []
	if not _sim.entities.has(entity_id):
		return result
	var row: Dictionary = _sim.entities[entity_id]
	var team := int(row.get("team", -1))
	var applied: Dictionary = row.get("applied_upgrades", {})
	var queued: Array = row.get("upgrade_queue", [])
	for command_value in _sorted_unit_upgrade_commands(String(row.get("unit_type", ""))):
		var command := command_value as Dictionary
		var upgrade_id := String(command.get("upgrade_id", ""))
		var missing := _battalion_gate_unsatisfied(team, command)
		result.append({
			"upgrade_id": upgrade_id,
			"command_id": String(command.get("command_id", "")),
			"cost": _discounted_battalion_upgrade_cost(team, command),
			"base_cost": int(command.get("cost", 0)),
			"duration_ticks": int(command.get("duration_ticks", 1)),
			"slot": int(command.get("slot", 0)),
			"label_id": String(command.get("label_id", "")),
			"tooltip_id": String(command.get("tooltip_id", "")),
			"image_id": String(command.get("image_id", "")),
			"lacks_prerequisite_label_id": String(command.get("lacks_prerequisite_label_id", "")),
			"needed_upgrade_ids": Array(command.get("needed_upgrade_ids", [])).duplicate(),
			"cancelable": bool(command.get("cancelable", false)),
			"multi_select": bool(command.get("multi_select", false)),
			"research_owned": missing == "",
			"required_upgrade": missing,
			"applied": applied.has(upgrade_id),
			"queued": not queued.is_empty(),
		})
	return result


func _apply_structure_death_refund(building: Dictionary) -> void:
	## Compatibility entry point retained for focused runners and old callers.
	## RefundDie is an Object DieMux, not a structure-only behavior.
	if not bool(building.get("structure_module_contracts_attached", false)):
		sim._attach_structure_module_contracts(building)
	_apply_refund_die_on_death(building)


func _apply_refund_die_on_death(owner: Dictionary) -> void:
	## Retail game.dat RefundDie::onDie: the death edge is delivered once by the
	## DieMux, then UNDER_CONSTRUCTION/SOLD, current owner, prerequisites and the
	## object's cached build cost are evaluated in that order.  A failed check is
	## not a deferred opportunity: there is no second death callback later.
	var _sim = sim
	var typed_policies := owner.get("refund_die", []) as Array
	if not typed_policies.is_empty():
		if bool(owner.get("refund_die_death_dispatched", false)):
			return
		owner["refund_die_death_dispatched"] = true
		var statuses := owner.get("object_status", {}) as Dictionary
		var death_blocker := ""
		if (
			bool(statuses.get("UNDER_CONSTRUCTION", false))
			or (
				owner.has("construction_progress")
				and float(owner.get("construction_progress", 1.0)) < 1.0
			)
		):
			death_blocker = "UNDER_CONSTRUCTION"
		elif bool(statuses.get("SOLD", false)):
			death_blocker = "SOLD"
		if death_blocker != "":
			for policy_index in typed_policies.size():
				var blocked_policy := typed_policies[policy_index] as Dictionary
				blocked_policy["death_blocker"] = death_blocker
				typed_policies[policy_index] = blocked_policy
			owner["refund_die"] = typed_policies
			return
		var team := int(owner.get("team", -1))
		if not _sim.team_resources.has(team):
			return
		var build_cost_value: Variant = owner.get("cached_build_cost")
		for policy_index in typed_policies.size():
			var policy := typed_policies[policy_index] as Dictionary
			var upgrade_required := String(policy.get("upgrade_required", ""))
			if upgrade_required != "" and not (_sim.team_upgrades.get(team, {}) as Dictionary).has(upgrade_required): continue
			var building_filter := policy.get("building_required", []) as Array
			if not building_filter.is_empty() and not _team_has_required_building_filter(team, building_filter): continue
			if typeof(build_cost_value) not in [TYPE_INT, TYPE_FLOAT] or float(build_cost_value) < 0.0:
				var unsupported := policy.get("unsupported_semantics", []) as Array
				if not unsupported.has("structure-build-cost-unresolved"): unsupported.append("structure-build-cost-unresolved")
				policy["unsupported_semantics"] = unsupported
				typed_policies[policy_index] = policy
				continue
			var amount := ceili(float(build_cost_value) * float(policy.get("fraction", 0.0)))
			policy["refund_amount"] = amount
			typed_policies[policy_index] = policy
			if amount <= 0: continue
			_sim.team_resources[team] = _sim.resources_for_team(team) + amount
			_sim._emit_event("economy.refund", int(owner.get("id", 0)), 0, {"team": team, "amount": amount, "upgrade_id": upgrade_required, "building_required": building_filter, "module": "RefundDie"})
		owner["refund_die"] = typed_policies
		return
	var team := int(owner.get("team", -1))
	var kind := String(owner.get("structure_kind", ""))
	# Compatibility only for stale structure documents predating typed
	# moduleContracts. A typed row never falls through and cannot double-refund.
	var bundle: Dictionary = _sim.structure_upgrade_effects_for_team(team).get(kind, {})
	var owned: Dictionary = _sim.team_upgrades.get(team, {}) as Dictionary
	for effect_value in Array(bundle.get("effects", [])):
		var effect := effect_value as Dictionary
		if String(effect.get("kind", "")) != "refund-on-death":
			continue
		var upgrade_id := String(effect.get("upgrade_id", ""))
		if not owned.has(upgrade_id):
			continue
		if not _team_has_required_object(team, String(effect.get("building_required", ""))):
			continue
		var build_rule: Dictionary = _sim._structure_build_rules.get(kind, {})
		var refund := roundi(float(build_rule.get("cost", 0)) * float(effect.get("refund_percent", 0.0)) / 100.0)
		if refund <= 0:
			continue
		_sim.team_resources[team] = _sim.resources_for_team(team) + refund
		_sim._emit_event("economy.refund", int(owner.get("id", 0)), 0, {"team": team, "amount": refund, "upgrade_id": upgrade_id})


func _team_has_required_building_filter(team: int, filter: Array) -> bool:
	## BuildingRequired is a SAGE object filter. Preserve every token and test
	## it against living authored structure identities, not display names.
	var _sim = sim
	if filter.is_empty(): return true
	var registry: Dictionary = _sim._rules.get("producer_kind_registry", {}) as Dictionary
	if registry.is_empty(): registry = (_sim._rules.get("faction_manifest", {}) as Dictionary).get("producer_kind_registry", {}) as Dictionary
	for structure_id in _sim.structure_ids(team):
		var candidate = _sim.structures[structure_id] as Dictionary
		if int(candidate.get("health", 0)) <= 0: continue
		var candidate_status := candidate.get("object_status", {}) as Dictionary
		if (
			bool(candidate_status.get("EFFECTIVELY_DEAD", false))
			or bool(candidate_status.get("DESTROYED", false))
		):
			continue
		var traits := {"STRUCTURE": true}
		for value in [candidate.get("source_object_id", ""), candidate.get("object_id", ""), candidate.get("structure_kind", ""), candidate.get("category", "")]:
			if String(value) != "": traits[String(value).to_upper()] = true
		var inert := false
		for kind_value in candidate.get("kind_of", []) as Array:
			var kind_token := String(kind_value).to_upper()
			traits[kind_token] = true
			if kind_token == "INERT": inert = true
		if inert: continue
		for object_id_value in registry.keys():
			if String(registry[object_id_value]).to_lower() == String(candidate.get("structure_kind", "")).to_lower(): traits[String(object_id_value).to_upper()] = true
		var positive: Array[String] = []
		var excluded := false
		for token_value in filter:
			var token := String(token_value).to_upper()
			if token.begins_with("+"): positive.append(token.substr(1))
			elif token.begins_with("-") and traits.has(token.substr(1)): excluded = true
		if excluded: continue
		if not positive.is_empty():
			for required_trait in positive:
				if traits.has(required_trait): return true
		elif filter.has("ANY") or filter.has("ALL"):
			return true
	return false


func _income_with_upgrade_bonus(team: int, building: Dictionary, base_income: int) -> int:
	return sim._economy_subsystem().income_with_upgrade_bonus(team, building, base_income)


func _queued_command_points_for_team(team: int) -> int:
	var _sim = sim
	var total := 0
	for structure_id in _sim.structure_ids(team):
		for item_value in Array((_sim.structures[structure_id] as Dictionary).get("queue", [])):
			if typeof(item_value) == TYPE_DICTIONARY:
				total += int((item_value as Dictionary).get("command_points", 0))
	return total


## --- Spellbook powers ---
## Tree, costs, OR prerequisite groups, authored palantir purchase slots,
## reload cooldowns, and cast bindings all derive from the selected pack's
## openbfme.spellbook-runtime document (menspellbook.json). A missing or
## malformed document fails closed: the tree stays empty and every purchase or
## cast rejects with "spellbook-unavailable". Powers whose converted effect
## leaves do not fully support a faithful runtime effect stay locked with the
## reason recorded on the power row — no invented effects.
##
## PROVISIONAL (recorded, not hidden): the retail power-point earn rate is not
## resolved from source data — the spellbook document carries no economy rule.
## The kill-based rate lives in rules key "power_point_kills" (default below)
## so the data lane can replace it without code edits; do not treat it as
## retail-final.
## NONPRESSABLE passive/one-shot activations and their live Scavenger scale.
## Both are authoritative match state and therefore snapshot/hash state below.
## Picks made since the last ACCEPT: RESET refunds exactly these ("unspent
## picks" — casting a staged pick spends it and it can no longer be refunded).
## Per-team spellbook TREE overrides for cross-faction matches (two different
## factions in one sim). Empty by default: every team then resolves against the
## single global tree above, so the default same-faction match is byte-identical
## and the signature does not move. A team present here plays its OWN faction's
## powers/costs/prereqs/reloads — team ownership overlays (points, purchased,
## sciences, cooldowns, staged) stay in the per-team maps already declared.
## team:int -> {ready, powers, order, sciences, science_to_power, intrinsic, document}
## Single-player pause seam for the spellbook orb: while the palantir is open
## the sim clock halts (ticks, production, AI — everything in tick()). The
## slice drives this through set_spellbook_orb_open; the escape-menu pause in
## the slice composes independently (either one halts the clock).


