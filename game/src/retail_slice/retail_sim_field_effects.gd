extends RefCounted
## Field effects carved out of retail_slice_sim.gd (drawer 22): fire-weapon-when-dead contracts, death-weapon rules, passive area heals/modifiers, banner carrier spawn/step/defeat/replenishment.
## State stays on the sim; the sim keeps one-line delegates under the original names.

var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()

func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)

func _attach_fire_weapon_when_dead_contract(row: Dictionary, contract: Dictionary) -> void:
	## Normalize the typed importer row once at materialization. Opaque rows from
	## older packs fail closed because their timer/offset/status values are not
	## typed; they remain in module_contracts as evidence.
	if String(contract.get("extraction", "")) != "typed":
		return
	var fields: Dictionary = contract.get("fields", {}) as Dictionary
	var starts_value: Variant = sim._module_contract_value(fields, "StartsActive", null)
	if typeof(starts_value) != TYPE_BOOL or not bool(starts_value):
		return
	var weapon_value: Variant = sim._module_contract_value(fields, "DeathWeapon", "")
	var weapon_id := String(weapon_value).strip_edges()
	if weapon_id == "":
		return
	var delay_value: Variant = sim._module_contract_value(fields, "DelayTime", 0.0)
	if typeof(delay_value) not in [TYPE_INT, TYPE_FLOAT]:
		return
	var offset := Vector2.ZERO
	var offset_z := 0.0
	var offset_value: Variant = sim._module_contract_value(fields, "WeaponOffset", {})
	if typeof(offset_value) == TYPE_DICTIONARY:
		var coordinates := offset_value as Dictionary
		if (
			typeof(coordinates.get("x", 0.0)) not in [TYPE_INT, TYPE_FLOAT]
			or typeof(coordinates.get("y", 0.0)) not in [TYPE_INT, TYPE_FLOAT]
		):
			return
		offset = Vector2(float(coordinates.get("x", 0.0)), float(coordinates.get("y", 0.0)))
		if typeof(coordinates.get("z", 0.0)) not in [TYPE_INT, TYPE_FLOAT]:
			return
		offset_z = float(coordinates.get("z", 0.0))
	var rows: Array = row.get("fire_weapon_when_dead", []) as Array
	rows.append({
		"death_types": String(fields.get("deathTypes", "ALL")).to_upper(),
		"included_death_types": Array(fields.get("includedDeathTypes", [])).duplicate(),
		"excluded_death_types": Array(fields.get("excludedDeathTypes", [])).duplicate(),
		"required_status": Array(sim._module_contract_value(fields, "RequiredStatus", [])).duplicate(),
		"exempt_status": Array(sim._module_contract_value(fields, "ExemptStatus", [])).duplicate(),
		"active_during_construction": bool(sim._module_contract_value(fields, "ActiveDuringConstruction", false)),
		"delay_ticks": maxi(0, roundi(float(delay_value) / (sim.TICK_SECONDS * 1000.0))),
		"death_weapon": weapon_id,
		"weapon_offset_source": offset,
		"weapon_offset_z_source": offset_z,
		"tag": String(contract.get("tag", "")),
		"source_ini": String(contract.get("source_ini", contract.get("sourceIni", ""))),
		"line": int(contract.get("line", 0)),
	})
	row["fire_weapon_when_dead"] = rows


func register_death_weapon_rule(weapon_id: String, rule: Dictionary) -> bool:
	## Closed payload consumed when a scheduled DeathWeapon fires. This does not
	## invent data for a named-but-unconverted weapon: absent ids still produce a
	## deterministic unresolved event and no damage.
	var id := weapon_id.strip_edges()
	if id == "":
		return false
	for key in ["damage", "radius_source"]:
		if typeof(rule.get(key)) not in [TYPE_INT, TYPE_FLOAT] or float(rule.get(key)) < 0.0:
			return false
	var normalized := rule.duplicate(true)
	normalized["damage"] = float(rule.get("damage"))
	normalized["radius_source"] = float(rule.get("radius_source"))
	normalized["damage_type"] = String(rule.get("damage_type", ""))
	normalized["affects"] = String(rule.get("affects", "ENEMIES"))
	sim._death_weapon_rules[id] = normalized
	return true


func _configure_death_weapon_rules_from_rules() -> void:
	sim._death_weapon_rules.clear()
	var configured: Variant = sim._rules.get("death_weapon_rules", {})
	if typeof(configured) != TYPE_DICTIONARY:
		return
	var ids := (configured as Dictionary).keys()
	ids.sort()
	for id_value in ids:
		var rule_value: Variant = (configured as Dictionary).get(id_value)
		if typeof(rule_value) == TYPE_DICTIONARY:
			register_death_weapon_rule(String(id_value), rule_value as Dictionary)


func _passive_area_effect_number(fields: Dictionary, key: String) -> float:
	return sim._contracts_subsystem()._passive_area_effect_number(fields, key)


func _passive_area_effect_percent(text: String) -> float:
	return sim._contracts_subsystem()._passive_area_effect_percent(text)


func _passive_area_effect_yes(fields: Dictionary, key: String) -> bool:
	return sim._contracts_subsystem()._passive_area_effect_yes(fields, key)


func _step_passive_area_effect_heals() -> void:
	## PassiveAreaEffectBehavior's healing branch. Retail wells/fortress healing
	## author a periodic percent-of-member-max heal, a radius, an object filter,
	## optional upgrade gate, and NonStackable. Dead members are not revived.
	## ModifierName leadership rows are deliberately not handled here: they need
	## their referenced ModifierList resolved into the shared modifier core.
	var candidates: Dictionary = {}
	var scale = float(sim._rules.get("source_map_transform_scale", 0.0))
	if scale <= 0.0:
		scale = 1.0
	for structure_id in sim.structure_ids():
		var structure: Dictionary = sim.structures[structure_id]
		if not bool(structure.get("structure_module_contracts_attached", false)):
			sim._attach_structure_module_contracts(structure)
		if (
			int(structure.get("health", 0)) <= 0
			or float(structure.get("construction_progress", 1.0)) < 1.0
		):
			continue
		var team := int(structure.get("team", -1))
		if team < 0:
			continue
		var rules: Array = structure.get("passive_area_effect_heals", []) as Array
		for rule_index in rules.size():
			var rule: Dictionary = rules[rule_index]
			var upgrade_required := String(rule.get("upgrade_required", ""))
			if upgrade_required != "" and not _structure_has_completed_upgrade(
				structure, upgrade_required
			):
				continue
			var next_ping = int(rule.get("next_ping_tick", sim.tick_index + 1))
			var ping_ticks := maxi(1, int(rule.get("ping_ticks", 1)))
			var due = sim.tick_index >= next_ping
			if due:
				# Preserve cadence even if a test/operator advances this subsystem after
				# its deadline; normal gameplay calls it exactly once per sim tick.
				while next_ping <= sim.tick_index:
					next_ping += ping_ticks
				rule["next_ping_tick"] = next_ping
				rules[rule_index] = rule
			var radius = float(rule.get("radius_source", 0.0)) * scale
			var rate := float(rule.get("heal_fraction_per_second", 0.0))
			if radius <= 0.0 or rate <= 0.0:
				continue
			var origin := Vector2(structure.get("position", Vector2.ZERO))
			var filter_text := String(rule.get("allow_filter", ""))
			for entity_id in sim.living_ids(team):
				var target: Dictionary = sim.entities[entity_id]
				if not sim._ability_filter_accepts(target, filter_text):
					continue
				if Vector2(target.get("position", Vector2.ZERO)).distance_to(origin) > radius:
					continue
				if int(target.get("health", 0)) >= int(target.get("maximum_health", 0)):
					continue
				var raw_amount = (
					float(target.get("member_maximum_health", 0))
					* rate
					* float(ping_ticks)
					* sim.TICK_SECONDS
				)
				if raw_amount <= 0.0:
					continue
				var target_candidates: Array = candidates.get(entity_id, []) as Array
				target_candidates.append({
					"raw_amount": raw_amount,
					"strength": rate,
					"source_id": structure_id,
					"due": due,
					"remainder_key": (
						"nonstackable"
						if bool(rule.get("non_stackable", false))
						else "%d:%s" % [structure_id, String(rule.get("tag", rule_index))]
					),
					"non_stackable": bool(rule.get("non_stackable", false)),
				})
				candidates[entity_id] = target_candidates
		if rules.is_empty():
			structure.erase("passive_area_effect_heals")
		else:
			structure["passive_area_effect_heals"] = rules
	for entity_id_value in candidates.keys():
		var entity_id := int(entity_id_value)
		if not sim.entities.has(entity_id):
			continue
		var stackable: Array = []
		var best_nonstackable: Dictionary = {}
		for candidate_value in candidates[entity_id] as Array:
			var candidate := candidate_value as Dictionary
			if bool(candidate.get("non_stackable", false)):
				if (
					best_nonstackable.is_empty()
					or float(candidate.get("strength", 0.0))
						> float(best_nonstackable.get("strength", 0.0))
					or (
						is_equal_approx(
							float(candidate.get("strength", 0.0)),
							float(best_nonstackable.get("strength", 0.0))
						)
						and int(candidate.get("source_id", 0))
							< int(best_nonstackable.get("source_id", 0))
					)
				):
					best_nonstackable = candidate
			elif bool(candidate.get("due", false)):
				stackable.append(candidate)
		if not best_nonstackable.is_empty() and bool(best_nonstackable.get("due", false)):
			stackable.append(best_nonstackable)
		for candidate_value in stackable:
			_apply_passive_area_effect_heal(
				sim.entities[entity_id] as Dictionary, candidate_value as Dictionary
			)


func _step_passive_area_effect_modifiers() -> void:
	## Statue/heroic-statue leadership branch. The importer resolves ModifierName
	## into typed ModifierList effects, duration, category and stacking policy;
	## refresh those effects through the shared timed-modifier core.
	var scale = float(sim._rules.get("source_map_transform_scale", 0.0))
	if scale <= 0.0:
		scale = 1.0
	for structure_id in sim.structure_ids():
		var structure: Dictionary = sim.structures[structure_id]
		if not bool(structure.get("structure_module_contracts_attached", false)):
			sim._attach_structure_module_contracts(structure)
		if int(structure.get("health", 0)) <= 0 or float(structure.get("construction_progress", 1.0)) < 1.0:
			continue
		var team := int(structure.get("team", -1))
		if team < 0:
			continue
		var rules: Array = structure.get("passive_area_effect_modifiers", []) as Array
		for rule_index in rules.size():
			var rule: Dictionary = rules[rule_index]
			var upgrade_required := String(rule.get("upgrade_required", ""))
			if upgrade_required != "" and not _structure_has_completed_upgrade(structure, upgrade_required):
				continue
			var next_ping = int(rule.get("next_ping_tick", sim.tick_index))
			if sim.tick_index < next_ping:
				continue
			var ping_ticks := maxi(1, int(rule.get("ping_ticks", 1)))
			while next_ping <= sim.tick_index:
				next_ping += ping_ticks
			rule["next_ping_tick"] = next_ping
			rules[rule_index] = rule
			var radius = float(rule.get("radius_source", 0.0)) * scale
			var duration_ticks := maxi(1, int(rule.get("duration_ticks", 1)))
			var category := String(rule.get("category", ""))
			var modifier_id := String(rule.get("id", ""))
			var stacking: Dictionary = rule.get("stacking", {}) as Dictionary
			var origin := Vector2(structure.get("position", Vector2.ZERO))
			for entity_id in sim.living_ids(team):
				var target: Dictionary = sim.entities[entity_id]
				if not sim._ability_filter_accepts(target, String(rule.get("allow_filter", ""))):
					continue
				if Vector2(target.get("position", Vector2.ZERO)).distance_to(origin) > radius:
					continue
				if bool(stacking.get("ignoreIfAnticategoryActive", false)) and category == "LEADERSHIP" and sim._refresh_leadership_suppression(target) > sim.tick_index:
					continue
				var key := "passive:%s:%s" % [category, modifier_id]
				if bool(stacking.get("replaceInCategoryIfLongest", false)) or bool(rule.get("non_stackable", false)):
					key = "passive-category:%s" % category
					var current: Dictionary = (target.get("timed_modifiers", {}) as Dictionary).get(key, {}) as Dictionary
					if int(current.get("expires_tick", -1)) > sim.tick_index + duration_ticks:
						continue
				sim._set_timed_modifier(target, key, rule.get("effects", []) as Array, sim.tick_index + duration_ticks)
				sim._emit_event("module.passive_area_effect_modifier", structure_id, entity_id, {"modifier_id": modifier_id, "category": category})
		if rules.is_empty():
			structure.erase("passive_area_effect_modifiers")
		else:
			structure["passive_area_effect_modifiers"] = rules


func _structure_has_completed_upgrade(structure: Dictionary, upgrade_id: String) -> bool:
	if (structure.get("completed_upgrades", []) as Array).has(upgrade_id):
		return true
	var applied: Variant = structure.get("applied_upgrades", {})
	return typeof(applied) == TYPE_DICTIONARY and (applied as Dictionary).has(upgrade_id)


func _apply_passive_area_effect_heal(target: Dictionary, candidate: Dictionary) -> void:
	var remainders: Dictionary = target.get("passive_area_heal_remainders", {}) as Dictionary
	var key := String(candidate.get("remainder_key", ""))
	var accumulated := float(remainders.get(key, 0.0)) + float(candidate.get("raw_amount", 0.0))
	# Percent text such as 2% and a 300 ms cadence has the exact rational
	# result 0.6, but binary float accumulation can land at 5.999999... on the
	# tenth ping. A tiny deterministic epsilon preserves the authored rational
	# boundary without ever promoting a materially sub-integer value.
	var amount := floori(accumulated + 0.000001)
	remainders[key] = maxf(0.0, accumulated - float(amount))
	target["passive_area_heal_remainders"] = remainders
	if amount <= 0:
		return
	var health_values: Array = target.get("member_health", []) as Array
	var member_maximum := int(target.get("member_maximum_health", 0))
	var remaining := amount
	for member_index in health_values.size():
		if remaining <= 0:
			break
		var current := int(health_values[member_index])
		if current <= 0 or current >= member_maximum:
			continue
		var healed := mini(remaining, member_maximum - current)
		health_values[member_index] = current + healed
		remaining -= healed
	target["member_health"] = health_values
	var aggregate := 0
	for value in health_values:
		aggregate += int(value)
	target["health"] = aggregate
	sim._emit_event("module.passive_area_effect_heal", int(candidate.get("source_id", 0)), int(target.get("id", 0)), {
		"amount": amount - remaining,
		"team": int(target.get("team", -1)),
	})


func _attach_experience_state(row: Dictionary) -> void:
	sim._experience_subsystem().attach_experience_state(row)


func _refresh_banner_carrier_state(row: Dictionary) -> void:
	## Retail BannerCarriersAllowed: once the horde reaches minLevel, keep one
	## linked banner entity (or re-spawn after authored BannerCarrierUpdate
	## timers). Presentation reads banner_carrier_spawned / object_id / offset.
	var rule: Dictionary = sim._unit_banner_carriers.get(String(row.get("unit_type", "")), {}) as Dictionary
	if rule.is_empty():
		rule = sim._unit_banner_carriers.get(String(row.get("object_id", "")), {}) as Dictionary
	if rule.is_empty():
		return
	row["banner_carrier_object_id"] = String(rule.get("object_id", ""))
	row["banner_carrier_offset_source"] = rule.get("offset_source", Vector2.ZERO)
	row["banner_carrier_destroy_horde_on_death"] = bool(rule.get("destroy_horde_on_death", false))
	if int(row.get("level", 1)) < int(rule.get("min_level", 2)):
		return
	# AllowBannerSpawnUpgrade is authored on fortress garrison expansions. It
	# gates only a horde currently contained by that expansion; uncontained
	# hordes and containers without the module retain their normal banner path.
	if sim.entity_container.has(int(row.get("id", 0))):
		var container_id = int(sim.entity_container[int(row.get("id", 0))])
		if sim.structures.has(container_id) and not sim.structure_allows_banner_spawn(container_id):
			return
	var banner_id := int(row.get("banner_entity_id", 0))
	if banner_id != 0 and sim.entities.has(banner_id) and int((sim.entities[banner_id] as Dictionary).get("health", 0)) > 0:
		_sync_banner_entity_transform(row, banner_id, rule)
		row["banner_carrier_spawned"] = true
		return
	var respawn_remaining := int(row.get("banner_respawn_ticks_remaining", -1))
	if respawn_remaining > 0:
		return
	_spawn_banner_carrier_entity(row, rule)


func _spawn_banner_carrier_entity(parent: Dictionary, rule: Dictionary) -> void:
	var team := int(parent.get("team", -1))
	if team < 0:
		return
	if not sim._next_dynamic_id.has(team):
		sim._next_dynamic_id[team] = 900000 + team * 1000
	var banner_object_id := String(rule.get("object_id", ""))
	var banner_max_health := int(rule.get("banner_max_health", 0))
	if banner_object_id == "" or banner_max_health <= 0:
		parent["banner_carrier_spawned"] = false
		parent["banner_carrier_error"] = "selected banner template is incomplete"
		return
	var entity_id = int(sim._next_dynamic_id[team])
	sim._next_dynamic_id[team] = entity_id + 1
	var offset_source: Vector2 = rule.get("offset_source", Vector2.ZERO)
	var at = Vector2(parent.get("position", Vector2.ZERO)) + sim._retail_source_to_sim_offset(offset_source)
	var unit_rules_value: Variant = sim._rules.get("unit_rules", {})
	var has_rule := (
		typeof(unit_rules_value) == TYPE_DICTIONARY
		and not ((unit_rules_value as Dictionary).get(banner_object_id, {}) as Dictionary).is_empty()
	)
	if has_rule:
		sim._add_battalion(
			entity_id,
			team,
			at,
			banner_object_id,
			banner_object_id,
			banner_object_id,
			0
		)
	else:
		# Banner runtimes may be non-producible (no UNIT_BUILD rule). Spawn a
		# minimal 1-member entity so death/respawn still execute fail-closed.
		sim.entities[entity_id] = {
			"id": entity_id,
			"team": team,
			"name": banner_object_id,
			"object_id": banner_object_id,
			"unit_type": banner_object_id,
			"position": at,
			"facing": Vector2(parent.get("facing", Vector2.RIGHT)),
			"destination": at,
			"route": [],
			"route_cells": [],
			"state": "idle",
			"target_id": 0,
			"target_kind": "battalion",
			"health": banner_max_health,
			"maximum_health": banner_max_health,
			"member_maximum_health": banner_max_health,
			"member_health": [banner_max_health],
			"damage": 0,
			"member_damage": 0,
			"speed": 0.0,
			"speed_source": 0.0,
			"current_speed": 0.0,
			"command_points": 0,
			"level": 1,
			"order_kind": "",
			"is_builder": false,
			"formation_positions": [Vector3.ZERO],
			"formation_positions_base": [Vector3.ZERO],
			"timed_modifiers": {},
			"applied_upgrades": {},
		}
	if not sim.entities.has(entity_id):
		return
	var banner: Dictionary = sim.entities[entity_id]
	if not banner.has("module_contracts"):
		sim._attach_module_contracts(banner)
	banner["is_banner_carrier"] = true
	banner["banner_parent_entity_id"] = int(parent.get("id", 0))
	banner["command_points"] = 0
	banner["banner_carrier_object_id"] = banner_object_id
	# Linked carriers are not free-command battalions.
	banner["ignores_select_all"] = true
	sim._spatial_sync(banner)
	parent["banner_entity_id"] = entity_id
	parent["banner_carrier_spawned"] = true
	parent["banner_respawn_ticks_remaining"] = -1
	parent.erase("banner_respawn_armed_tick")
	sim._emit_event("battalion.banner_spawned", int(parent.get("id", 0)), entity_id, {
		"team": team,
		"banner_object_id": banner_object_id,
		"parent_unit_type": String(parent.get("unit_type", "")),
	})


func _sync_banner_entity_transform(parent: Dictionary, banner_id: int, rule: Dictionary) -> void:
	if not sim.entities.has(banner_id):
		return
	var banner: Dictionary = sim.entities[banner_id]
	var offset_source: Vector2 = rule.get("offset_source", Vector2.ZERO)
	banner["position"] = Vector2(parent.get("position", Vector2.ZERO)) + sim._retail_source_to_sim_offset(offset_source)
	banner["destination"] = banner["position"]
	banner["facing"] = Vector2(parent.get("facing", banner.get("facing", Vector2.RIGHT)))
	banner["team"] = int(parent.get("team", banner.get("team", -1)))
	sim._spatial_sync(banner)


func _step_banner_carriers() -> void:
	## Follow parents, count respawn timers, re-spawn when due.
	for id in sim.entity_ids():
		var row: Dictionary = sim.entities[id]
		if bool(row.get("is_banner_carrier", false)):
			_step_banner_replenishment(row)
			var parent_id := int(row.get("banner_parent_entity_id", 0))
			if int(row.get("health", 0)) <= 0:
				_on_banner_carrier_defeated(row)
				sim.entities.erase(id)
				continue
			if parent_id == 0 or not sim.entities.has(parent_id) or int((sim.entities[parent_id] as Dictionary).get("health", 0)) <= 0:
				sim.entities.erase(id)
				continue
			if parent_id != 0 and sim.entities.has(parent_id):
				var parent: Dictionary = sim.entities[parent_id]
				var rule: Dictionary = sim._unit_banner_carriers.get(String(parent.get("unit_type", "")), {}) as Dictionary
				if rule.is_empty():
					rule = sim._unit_banner_carriers.get(String(parent.get("object_id", "")), {}) as Dictionary
				if not rule.is_empty():
					_sync_banner_entity_transform(parent, id, rule)
			continue
		var remaining := int(row.get("banner_respawn_ticks_remaining", -1))
		if remaining < 0:
			# Keep living banner glued even when not a banner carrier itself.
			if int(row.get("banner_entity_id", 0)) != 0:
				_refresh_banner_carrier_state(row)
			continue
		if remaining > 0:
			if int(row.get("banner_respawn_armed_tick", -1)) == sim.tick_index:
				continue
			row["banner_respawn_ticks_remaining"] = remaining - 1
			if int(row["banner_respawn_ticks_remaining"]) > 0:
				continue
		# remaining hit 0 — attempt respawn if level still qualifies.
		_refresh_banner_carrier_state(row)


func _on_banner_carrier_defeated(banner: Dictionary) -> void:
	## Mirror C# BannerCarrierModule: destroy horde when authored, else arm
	## authored respawn ticks (never invent a default timer).
	var parent_id := int(banner.get("banner_parent_entity_id", 0))
	if parent_id == 0 or not sim.entities.has(parent_id):
		return
	var parent: Dictionary = sim.entities[parent_id]
	if int(parent.get("banner_entity_id", 0)) == int(banner.get("id", 0)):
		parent["banner_entity_id"] = 0
	parent["banner_carrier_spawned"] = false
	if bool(parent.get("banner_carrier_destroy_horde_on_death", false)):
		# Lethal destroy of the owning horde (retail thrall-style contracts).
		parent["health"] = 0
		var member_health: Array = parent.get("member_health", []) as Array
		var defeated_members: Array[int] = []
		for member_index in member_health.size():
			if int(member_health[member_index]) > 0:
				defeated_members.append(member_index)
			member_health[member_index] = 0
		parent["member_health"] = member_health
		var death_policy = sim._bookkeep_battalion_death(
			parent_id, parent, "NORMAL", defeated_members
		)
		sim._emit_event("battalion.defeated", int(banner.get("id", 0)), parent_id, {
			"object_id": String(parent.get("object_id", "")),
			"team": int(parent.get("team", -1)),
			"category": String(parent.get("category", "")),
			"reason": "banner-carrier-destroy-horde-on-death",
		})
		if bool(death_policy.get("destroy_object", false)):
			sim.entities.erase(parent_id)
		return
	var banner_object_id := String(banner.get("object_id", parent.get("banner_carrier_object_id", "")))
	var rule: Dictionary = sim._unit_banner_carriers.get(String(parent.get("unit_type", "")), {}) as Dictionary
	if rule.is_empty():
		rule = sim._unit_banner_carriers.get(String(parent.get("object_id", "")), {}) as Dictionary
	var respawn_ticks := int(rule.get("respawn_ticks", -1))
	var typed_update:=banner.get("banner_carrier_update",{}) as Dictionary
	if typed_update.is_empty():
		sim._attach_module_contracts(banner);typed_update=banner.get("banner_carrier_update",{}) as Dictionary
	if bool(typed_update.get("has_respawn_timer",false)):respawn_ticks=maxi(int(typed_update.get("died_respawn_ticks",0)),int(typed_update.get("melee_banner_respawn_ticks",0)))
	if sim._banner_respawn_ticks_by_object.has(banner_object_id):
		respawn_ticks = int(sim._banner_respawn_ticks_by_object[banner_object_id])
	# Fall back through retail source name on the rule.
	if respawn_ticks < 0 and not rule.is_empty():
		var source_name := String(rule.get("source_banner_object_id", ""))
		if source_name != "" and sim._banner_respawn_ticks_by_object.has(source_name):
			respawn_ticks = int(sim._banner_respawn_ticks_by_object[source_name])
		var rule_oid := String(rule.get("object_id", ""))
		if respawn_ticks < 0 and rule_oid != "" and sim._banner_respawn_ticks_by_object.has(rule_oid):
			respawn_ticks = int(sim._banner_respawn_ticks_by_object[rule_oid])
	if respawn_ticks >= 0:
		parent["banner_respawn_ticks_remaining"] = respawn_ticks
		parent["banner_respawn_armed_tick"] = sim.tick_index
		sim._emit_event("battalion.banner_respawn_armed", parent_id, int(banner.get("id", 0)), {
			"respawn_ticks": respawn_ticks,
			"banner_object_id": banner_object_id,
		})
	else:
		parent["banner_respawn_ticks_remaining"] = -1
		parent.erase("banner_respawn_armed_tick")


func _step_banner_replenishment(banner:Dictionary)->void:
	var update:=banner.get("banner_carrier_update",{}) as Dictionary
	if update.is_empty():return
	if sim.tick_index<int(update.get("next_replenish_tick",0)):return
	update["next_replenish_tick"]=sim.tick_index+maxi(1,int(update.get("idle_spawn_ticks",1)));banner["banner_carrier_update"]=update
	var parent_id:=int(banner.get("banner_parent_entity_id",0));var required:=String(update.get("upgrade_required",""))
	if required!="" and (parent_id==0 or not sim.entities.has(parent_id) or not _structure_has_completed_upgrade(sim.entities[parent_id] as Dictionary,required)):return
	var candidates:Array[int]=[]
	if bool(update.get("replenish_nearby",false)) and parent_id!=0:candidates.append(parent_id)
	if bool(update.get("replenish_all",false)):
		var radius=float(update.get("scan_range_source",0.0))*float(sim._rules.get("source_unit_scale",0.1));var origin=Vector2(banner.get("position",Vector2.ZERO))
		for id in sim.entity_ids():
			if id==int(banner.get("id",0)) or candidates.has(id):continue
			if int((sim.entities[id] as Dictionary).get("team",-1))!=int(banner.get("team",-1)):continue
			if radius<=0.0 or origin.distance_to(Vector2((sim.entities[id] as Dictionary).get("position",Vector2.ZERO)))<=radius:candidates.append(id)
	for id in candidates:
		if not sim.entities.has(id):continue
		var target=sim.entities[id] as Dictionary;var members=target.get("member_health",[]) as Array;var replenished=-1
		for index in members.size():
			if int(members[index])<=0:members[index]=maxi(1,int(target.get("member_maximum_health",1)));replenished=index;break
		if replenished<0:continue
		target["member_health"]=members;var total:=0;for health in members:total+=int(health);target["health"]=total
		sim._emit_event("battalion.banner_replenished",int(banner.get("id",0)),id,{"member_index":replenished})



