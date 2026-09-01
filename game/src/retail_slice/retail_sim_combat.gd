extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Damage-resolution core extracted from retail_slice_sim.gd (Q81
## strangler-fig extraction #5). Verbatim move: function names unchanged;
## sim state/services reached through the weak back-reference (compiler-
## guided sim. prefixes, pin-verified byte-identical).

# Weak back-reference: a strong ref would form a RefCounted cycle with the
# sim (which holds this subsystem), leaking freed sims as zombies.




func _apply_damage(attacker_id: int, target_id: int, amount: int, target_kind: String = "battalion", death_type: String = "NORMAL", damage_type_override: String = "") -> void:
	var _sim = sim
	if target_kind == "structure":
		_apply_structure_damage(attacker_id, target_id, amount, damage_type_override)
		return
	if not _sim.entities.has(target_id):
		return
	if _sim.entity_container.has(target_id):
		return
	var target: Dictionary = _sim.entities[target_id]
	if not target.has("inactive_body"):
		_sim._attach_module_contracts(target)
	if bool(target.get("indestructible", false)):
		_sim._emit_event("combat.damage_refused", attacker_id, target_id, {"reason": "inactive-body", "target": "entity"})
		return
	var remaining := maxi(0, amount)
	var damage_type := damage_type_override
	if damage_type == "" and _sim.entities.has(attacker_id):
		damage_type = String((_sim.entities[attacker_id] as Dictionary).get("damage_type", ""))
	if bool(target.get("highlander_body", false)) and damage_type.to_lower() != "unresistable":
		var target_member = _sim._choose_target_member(
			target, attacker_id, 0, int(target.get("attack_sequence", 0))
		)
		if target_member >= 0:
			_apply_member_damage(
				attacker_id,
				-1,
				target_id,
				remaining,
				"battalion",
				int(target.get("attack_sequence", 0)),
				target_member,
				damage_type_override,
				death_type,
			)
		return
	while remaining > 0 and int(target.get("health", 0)) > 0:
		var target_member = _sim._choose_target_member(target, attacker_id, 0, int(target.get("attack_sequence", 0)))
		if target_member < 0:
			break
		var health_values: Array = target.get("member_health", [])
		# Each member must receive exactly the raw amount its compiled factors
		# reduce to a kill; capping at current member health would grind
		# geometrically against sub-1.0 armor and stall short of lethal.
		# No override on this path: the attacker's own authored mix applies.
		var factor = _sim._incoming_damage_factor(attacker_id, target, "battalion", damage_type, _sim._damage_components_for(attacker_id, ""))
		if factor <= 0.0:
			# A 0% armor match (e.g. LOGICAL_FIRE vs fortress) makes the hit a
			# retail no-op; the remaining raw damage has no path through.
			break
		var applied := mini(remaining, maxi(1, ceili(float(health_values[target_member]) / factor)))
		_apply_member_damage(attacker_id, -1, target_id, applied, "battalion", int(target.get("attack_sequence", 0)), target_member, damage_type_override, death_type)
		remaining -= applied


func _apply_member_bonus_nuggets(
	attacker_id: int,
	member_index: int,
	row: Dictionary,
	target_id: int,
	target_kind: String,
	forced_target: int,
	weapon_effect: Dictionary
) -> void:
	var _sim = sim
	for nugget_value in weapon_effect.get("bonus_nuggets", []) as Array:
		if not _sim._target_alive(target_id, target_kind):
			break
		var nugget: Dictionary = nugget_value
		var bonus_target: Dictionary = _sim.structures.get(target_id, {}) if target_kind == "structure" else _sim.entities.get(target_id, {})
		var bonus_factor = _sim._damage_scalar_factor(nugget.get("scalars", []) as Array, bonus_target, target_kind)
		var bonus_amount := maxi(0, roundi(float(nugget.get("damage", 0.0)) * bonus_factor))
		if bonus_amount <= 0:
			continue
		_apply_member_damage(
			attacker_id, member_index, target_id, bonus_amount, target_kind,
			int(row.get("attack_sequence", 0)), forced_target,
			String(nugget.get("damage_type", ""))
		)


func _apply_member_damage(
	attacker_id: int,
	attacker_member_index: int,
	target_id: int,
	amount: int,
	target_kind: String,
	attack_sequence: int,
	forced_target_member: int = -1,
	damage_type_override: String = "",
	death_type: String = "NORMAL",
	damage_components_override: Variant = null
) -> void:
	var _sim = sim
	if target_kind == "structure":
		_apply_structure_damage(attacker_id, target_id, amount, damage_type_override, damage_components_override)
		return
	if not _sim.entities.has(target_id):
		return
	if _sim.entity_container.has(target_id):
		return
	var target: Dictionary = _sim.entities[target_id]
	if not target.has("inactive_body"):
		_sim._attach_module_contracts(target)
	if bool(target.get("indestructible", false)):
		_sim._emit_event("combat.damage_refused", attacker_id, target_id, {"reason": "inactive-body", "target": "entity-member"})
		return
	if int(target.get("health", 0)) <= 0:
		return
	var health_values: Array = target.get("member_health", [])
	var target_member := forced_target_member
	if target_member < 0:
		target_member = _sim._choose_target_member(target, attacker_id, attacker_member_index, attack_sequence)
	if target_member < 0 or target_member >= health_values.size():
		return
	var prior_health := int(health_values[target_member])
	if prior_health <= 0:
		return
	# armor.ini, compiled end-to-end: attacker damage type vs the victim's
	# authored ArmorSet (a recorded applied armor upgrade swaps the set).
	var damage_type := damage_type_override
	# A multi-nugget weapon resolves each component against its own armor
	# column; an explicit override replaces the mix rather than blending in.
	var damage_components = (
		damage_components_override as Array
		if typeof(damage_components_override) == TYPE_ARRAY
		else _sim._damage_components_for(attacker_id, damage_type_override)
	)
	var weapon_factor := 1.0
	if _sim.entities.has(attacker_id):
		var attacker: Dictionary = _sim.entities[attacker_id]
		if damage_type == "":
			damage_type = String(attacker.get("damage_type", ""))
		var attacker_effect = _sim._applied_weapon_effect(attacker)
		if not attacker_effect.is_empty():
			weapon_factor = _sim._damage_scalar_factor(attacker_effect.get("scalars", []) as Array, target, target_kind)
	var rally_factor := 1.0
	if (
		_sim.entities.has(attacker_id)
		and _sim.tick_index
			< int((_sim.entities[attacker_id] as Dictionary).get(
				"rally_until_tick", -1
			))
	):
		rally_factor = float(
			(_sim.entities[attacker_id] as Dictionary).get("rally_damage_mult", 1.5)
		)
	var stance_adjusted_amount := 0
	if bool(target.get("highlander_body", false)):
		var highlander_amount = _sim._highlander_raw_damage_amount(
			float(amount) * weapon_factor * rally_factor,
			prior_health,
			damage_type,
			damage_components,
		)
		if highlander_amount < 0:
			_sim._emit_event("combat.damage_refused", attacker_id, target_id, {
				"reason": "highlander-mixed-unresistable",
				"target": "member %d of %s" % [
					target_member, String(target.get("object_id", ""))
				],
			})
			return
		stance_adjusted_amount = maxi(0, roundi(
			highlander_amount
			* _sim._member_body_damage_factor(target, damage_type, damage_components)
		))
	else:
		stance_adjusted_amount = maxi(0, roundi(
			float(amount)
			* _sim._incoming_damage_factor(
				attacker_id,
				target,
				target_kind,
				damage_type,
				damage_components,
			)
		))
	if rally_factor != 1.0 and not bool(target.get("highlander_body", false)):
		# Rallying Call: the converted SpellBookRallyingCallModifier leaf
		# (DAMAGE_MULT 150%, 60s) rides the row from cast time until expiry.
		stance_adjusted_amount = int(ceil(stance_adjusted_amount * rally_factor))
	health_values[target_member] = maxi(0, prior_health - stance_adjusted_amount)
	target["member_health"] = health_values
	target["last_damage_tick"] = _sim.tick_index
	_sim.record_hit_reaction(target_id, float(stance_adjusted_amount))
	# Authored InvisibilityNugget ForbiddenConditions: the hit breaks a
	# TAKING_DAMAGE-forbidden cloak on the victim and a FIRING_ANY-forbidden
	# cloak on the attacker.
	_sim._break_stealth(target, "TAKING_DAMAGE")
	if _sim.entities.has(attacker_id):
		var attacker := _sim.entities[attacker_id] as Dictionary
		_sim._break_stealth(attacker, "FIRING_ANY")
		if attacker.has("special_disguise_channel"):
			_sim._cancel_special_disguise_row(attacker, "attack", false)
	var aggregate_health := 0
	for health_value in health_values:
		aggregate_health += int(health_value)
	target["health"] = aggregate_health
	if not bool(target.get("flammable_internal_damage", false)) and damage_type.to_upper() in ["FLAME", "FIRE"]:
		_sim.record_flame_damage(target_id, float(mini(prior_health, stance_adjusted_amount)))
	_sim._emit_event("combat.hit", attacker_id, target_id, {
		"attacker_member_index": attacker_member_index,
		"target_member_index": target_member,
		"amount": mini(prior_health, stance_adjusted_amount),
		"target_member_health": int(health_values[target_member]),
		"target_object_id": String(target.get("object_id", "")),
		"damage_type": damage_type,
		"armor_scalar": _sim._member_armor_scalar(target, damage_type, damage_components),
		"weapon_factor": weapon_factor,
	})
	var defeated_members: Array[int] = []
	if prior_health > 0 and int(health_values[target_member]) == 0:
		defeated_members.append(target_member)
	var death_policy: Dictionary = {}
	if int(target["health"]) == 0:
		death_policy = _sim._bookkeep_battalion_death(
			target_id, target, death_type, defeated_members, attacker_id
		)
	else:
		death_policy = _sim._apply_playable_unit_death_policy(
			target, death_type, defeated_members
		)
	if not defeated_members.is_empty():
		_sim._emit_event("battalion.member_defeated", attacker_id, target_id, {"member_index": target_member, "object_id": String(target.get("object_id", ""))})
		_sim._award_scavenger_bounty(attacker_id, target, "unit-member")
		# Veterancy: the kill pays the victim's authored ExperienceAward at the
		# victim's current level into the attacker's XP pool.
		_sim._award_member_kill_experience(attacker_id, target)
	if int(target["target_id"]) == 0 and int(target["health"]) > 0 and _sim.entities.has(attacker_id) \
			and int(target.get("knockdown_ticks", 0)) <= 0 \
			and _sim._can_engage_battalion(target, _sim.entities[attacker_id] as Dictionary):
		# Sprawled battalions cannot retaliate, and melee never chases an
		# airborne attacker it can never reach.
		var attacker_position := Vector2((_sim.entities[attacker_id] as Dictionary)["position"])
		var target_distance := Vector2(target["position"]).distance_to(attacker_position)
		if String(target.get("stance", "Battle")) == "HoldGround":
			# HoldGround retaliates without abandoning its post: engage only if
			# the attacker is already inside weapon range.
			if target_distance <= float(target.get("attack_range", 0.0)):
				target["target_id"] = attacker_id
				# Typed array built locally: an untyped literal does not convert
				# to Array[int] across the subsystem call boundary.
				var hold_retaliation_ids: Array[int] = [target_id]
				_sim._stamp_order_sequence(hold_retaliation_ids)
				_sim._emit_music("battle")
		elif _sim._assign_route(target, attacker_position):
			target["target_id"] = attacker_id
			var chase_retaliation_ids: Array[int] = [target_id]
			_sim._stamp_order_sequence(chase_retaliation_ids)
			_sim._emit_music("battle")
	if int(target["health"]) == 0:
		# Banner carriers notify the owning horde before ordinary kill bookkeeping.
		if bool(target.get("is_banner_carrier", false)):
			_sim._on_banner_carrier_defeated(target)
		_sim._emit_event("battalion.defeated", attacker_id, target_id, {
			"object_id": String(target.get("object_id", "")),
			"team": int(target.get("team", -1)),
			"category": String(target.get("category", "")),
		})
		if bool(target.get("is_banner_carrier", false)):
			_sim._on_banner_carrier_defeated(target)
		if bool(death_policy.get("destroy_object", false)) or bool(target.get("is_banner_carrier", false)):
			# SAGE DestroyDie::onDie calls destroyObject in the death callback;
			# it does not enter the ordinary readable-corpse lifetime.
			# Banner carriers are presentation/sim attachments without corpses.
			_sim.entities.erase(target_id)


func _apply_structure_damage(
	attacker_id: int,
	target_id: int,
	amount: int,
	damage_type_override: String = "",
	damage_components_override: Variant = null
) -> void:
	var _sim = sim
	if not _sim.structures.has(target_id):
		return
	var target: Dictionary = _sim.structures[target_id]
	if not target.has("inactive_body") and not bool(target.get("structure_module_contracts_attached", false)):
		_sim._attach_structure_module_contracts(target)
	if int(target.get("health", 0)) <= 0:
		return
	if bool(target.get("indestructible", false)):
		# Map-authored castle fixtures carry SAGE objectIndestructible verbatim
		# (Erebor authors it on 501 of 609 fixture rows); retail never damages
		# those sim.structures, so the sim refuses the damage by name.
		_sim._emit_event("combat.damage_refused", attacker_id, target_id, {
			"reason": "indestructible",
			"target": "structure %s" % String(target.get("structure_kind", "")),
		})
		return
	var damage_type := damage_type_override
	var damage_components = (
		damage_components_override as Array
		if typeof(damage_components_override) == TYPE_ARRAY
		else _sim._damage_components_for(attacker_id, damage_type_override)
	)
	if damage_type == "":
		damage_type = "default"
		if _sim.entities.has(attacker_id):
			damage_type = String((_sim.entities[attacker_id] as Dictionary).get("damage_type", "default"))
			if damage_type == "" and not damage_components.is_empty():
				# Typed per component, not untyped: name the mix so the
				# per-type remainder accumulator keeps it separate.
				damage_type = "mixed"
	var weapon_factor := 1.0
	if _sim.entities.has(attacker_id):
		var attacker_effect = _sim._applied_weapon_effect(
			_sim.entities[attacker_id] as Dictionary
		)
		if not attacker_effect.is_empty():
			weapon_factor = _sim._damage_scalar_factor(
				attacker_effect.get("scalars", []) as Array,
				target,
				"structure",
			)
	var body_amount := float(maxi(0, amount)) * weapon_factor
	if _sim._structure_uses_highlander_body(target):
		var highlander_amount = _sim._highlander_raw_damage_amount(
			body_amount,
			int(target.get("health", 0)),
			damage_type,
			damage_components,
		)
		if highlander_amount < 0:
			_sim._emit_event("combat.damage_refused", attacker_id, target_id, {
				"reason": "highlander-mixed-unresistable",
				"target": "structure %s" % String(target.get("structure_kind", "")),
			})
			return
		body_amount = highlander_amount
	# Every structure kind scales by its own compiled armor.ini table; kinds
	# without one refuse damage (loud failure logged at configure).
	var kind := String(target.get("structure_kind", ""))
	var table: Dictionary = _sim._structure_armor.get(kind, {})
	if table.is_empty():
		_sim._emit_event("combat.damage_refused", attacker_id, target_id, {
			"reason": "missing-compiled-structure-armor",
			"target": "structure %s" % kind,
		})
		return
	var scalars: Dictionary = table.get("scalars", {})
	var scalar := 1.0
	if not damage_components.is_empty():
		scalar = float(table.get("damage_scalar", 1.0)) * _sim._weighted_armor_scalar(scalars, damage_components, damage_type)
	else:
		scalar = float(table.get("damage_scalar", 1.0)) * float(scalars.get(damage_type.to_lower(), scalars.get("default", 1.0)))
	var remainder_by_type: Dictionary = target.get("damage_remainders", {})
	var accumulated := (
		float(remainder_by_type.get(damage_type, 0.0))
		+ body_amount * scalar
	)
	var applied := floori(accumulated)
	remainder_by_type[damage_type] = accumulated - float(applied)
	target["damage_remainders"] = remainder_by_type
	if applied <= 0:
		return
	if _sim.entities.has(attacker_id):
		# Firing on a structure breaks a FIRING_ANY-forbidden cloak too.
		var attacker := _sim.entities[attacker_id] as Dictionary
		_sim._break_stealth(attacker, "FIRING_ANY")
		if attacker.has("special_disguise_channel"):
			_sim._cancel_special_disguise_row(attacker, "attack", false)
	target["health"] = maxi(0, int(target["health"]) - applied)
	if not bool(target.get("flammable_internal_damage", false)) and damage_type.to_upper() in ["FLAME", "FIRE"]:
		_sim.record_flame_damage(target_id, float(applied))
	if target.has("horde_transport"):
		var passenger_damage_ratio := float((target["horde_transport"] as Dictionary).get("damage_ratio", 0.0))
		var passenger_damage := floori(float(applied) * passenger_damage_ratio)
		if passenger_damage > 0:
			for passenger_value in (_sim.containment.get(target_id, []) as Array).duplicate():
				_sim._apply_transport_passenger_damage(attacker_id, int(passenger_value), passenger_damage)
	var structure_kind := String(target.get("structure_kind", ""))
	_sim._emit_event("combat.hit_structure", attacker_id, target_id, {
		"raw_amount": maxi(0, amount),
		"applied_amount": applied,
		"damage_type": damage_type,
		"armor_scalar": scalar,
		"structure_kind": structure_kind,
		"team": int(target.get("team", -1)),
		"health": int(target["health"]),
		"maximum_health": int(target.get("maximum_health", 0)),
	})
	if int(target.get("team", -1)) == _sim.PLAYER_TEAM and _sim.tick_index - _sim._last_base_under_attack_tick >= _sim.EVA_BASE_UNDER_ATTACK_DEBOUNCE_TICKS:
		# EVA "base under attack" announces the first hit on a player structure,
		# then stays quiet for the retail 30s TimeBetweenEventsMS debounce.
		_sim._last_base_under_attack_tick = _sim.tick_index
		_sim._emit_event("eva.base_under_attack", 0, target_id, {"team": _sim.PLAYER_TEAM, "structure_kind": structure_kind})
	if int(target["health"]) == 0:
		# Gate destruction is itself a pathing transition. Synchronize while the
		# zero-health structure still carries its typed gate contract and before
		# any death callback can observe the battlefield topology.
		if target.has("gate_behavior"):
			_sim._sync_gate_passage(target_id)
		_sim._record_cah_structure_kill(attacker_id, target)
		_sim._award_scavenger_bounty(attacker_id, target, "structure")
		var queue: Array = target.get("queue", [])
		# Queued costs stay spent, matching the deterministic no-refund contract.
		queue.clear()
		target["queue"] = queue
		var upgrade_queue: Array = target.get("upgrade_queue", [])
		upgrade_queue.clear()
		target["upgrade_queue"] = upgrade_queue
		# Authored RefundDie rows (Siege Materials) refund their compiled
		# percent when the owning team keeps the required building.
		_sim._apply_structure_death_refund(target)
		# Structure-carried death modules when contracts were attached at spawn
		# or are discovered lazily on this first death callback.
		if not bool(target.get("structure_module_contracts_attached", false)):
			_sim._attach_structure_module_contracts(target)
		_sim._dispatch_castle_member_destroyed(target_id, target, attacker_id, "destroyed")
		_sim._resolve_citadel_slaughter_death(target_id, target)
		_sim._begin_ship_slow_death(target_id, target, "NORMAL")
		_sim._schedule_fire_weapon_when_dead(target, "NORMAL", "structure")
		_sim._expose_rebuild_hole(target_id, target, attacker_id)
		# Structure-carried CreateObjectDie (debris/refund eggs) when contracts
		# were attached at spawn or via register_structure_module_contracts.
		if not bool(target.get("create_object_die", false)):
			_sim._attach_structure_module_contracts(target)
		_sim._consume_create_object_die(target, "NORMAL")
		_sim._emit_event("structure.destroyed", attacker_id, target_id, {"structure_kind": structure_kind, "team": int(target.get("team", -1))})
		if int(target.get("team", -1)) == _sim.PLAYER_TEAM:
			_sim._emit_event("eva.building_lost", 0, target_id, {"team": _sim.PLAYER_TEAM, "structure_kind": structure_kind})
