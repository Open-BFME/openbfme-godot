extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Damage and death core carved out of retail_slice_sim.gd (drawer 20): damage application, armor/flanking factors, knockback, battalion/structure death, slow-death core, death weapons, create/keep/destroy-die policies, corpse cleanup.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func _keep_object_die_matches(row: Dictionary, death_type: String) -> bool:
	if not bool(row.get("keep_object_die", false)):
		return false
	var policy: Dictionary = row.get("keep_object_die_policy", {}) as Dictionary
	if policy.is_empty():
		return true
	var death_folded := death_type.strip_edges().to_upper()
	var mode := String(policy.get("death_types", "ALL")).to_upper()
	if mode == "ALL":
		for excluded_value in policy.get("excluded_death_types", []) as Array:
			if String(excluded_value).to_upper() == death_folded:
				return false
		return true
	if mode == "NONE":
		for included_value in policy.get("included_death_types", []) as Array:
			if String(included_value).to_upper() == death_folded:
				return true
	return false


func _destroy_die_matches(
	row: Dictionary, owner_role: String, death_type: String
) -> bool:
	for policy_value in row.get("destroy_die", []) as Array:
		if typeof(policy_value) != TYPE_DICTIONARY:
			continue
		var policy := policy_value as Dictionary
		if String(policy.get("owner_role", "")) != owner_role:
			continue
		var mode := String(policy.get("death_types", "")).to_upper()
		var death_folded := death_type.strip_edges().to_upper()
		var excluded: Array = policy.get("excluded_death_types", []) as Array
		if mode == "ALL" and not excluded.any(
			func(value: Variant) -> bool: return String(value).to_upper() == death_folded
		):
			return true
		if mode == "NONE":
			for included_value in policy.get("included_death_types", []) as Array:
				if String(included_value).to_upper() == death_folded:
					return true
	return false


func _cleanup_expired_corpses() -> void:
	var _sim = sim
	var expired: Array[int] = []
	for id in _sim.entity_ids():
		var row: Dictionary = _sim.entities[id]
		var expire_tick := int(row.get("corpse_expire_tick", -1))
		if int(row.get("health", 0)) <= 0 and expire_tick >= 0 and _sim.tick_index >= expire_tick:
			expired.append(id)
	for id in expired:
		_sim.entities.erase(id)
		_sim.selected_ids.erase(id)
		_sim._emit_event("battalion.corpse_expired", id, 0)
	if not expired.is_empty():
		_sim.prune_control_groups()


func _choose_target_member(
	target: Dictionary,
	attacker_id: int,
	attacker_member_index: int,
	attack_sequence: int
) -> int:
	var health_values: Array = target.get("member_health", [])
	if health_values.is_empty():
		return -1
	var start := posmod(attacker_id * 31 + maxi(0, attacker_member_index) * 17 + attack_sequence * 13, health_values.size())
	for offset in range(health_values.size()):
		var candidate := posmod(start + offset, health_values.size())
		if int(health_values[candidate]) > 0:
			return candidate
	return -1


func _apply_structure_damage(
	attacker_id: int,
	target_id: int,
	amount: int,
	damage_type_override: String = "",
	damage_components_override: Variant = null
) -> void:
	sim._combat_subsystem()._apply_structure_damage(attacker_id, target_id, amount, damage_type_override, damage_components_override)


func _target_alive(target_id: int, target_kind: String) -> bool:
	var _sim = sim
	if target_kind == "structure":
		return _sim.structures.has(target_id) and int((_sim.structures[target_id] as Dictionary).get("health", 0)) > 0
	return _sim.entities.has(target_id) and not _sim.entity_container.has(target_id) and int((_sim.entities[target_id] as Dictionary).get("health", 0)) > 0


func _target_position(target_id: int, target_kind: String) -> Vector2:
	var _sim = sim
	var row: Dictionary = _sim.structures.get(target_id, {}) if target_kind == "structure" else _sim.entities.get(target_id, {})
	return Vector2(row.get("position", Vector2.ZERO))




func _incoming_damage_factor(attacker_id: int, target: Dictionary, target_kind: String, damage_type: String, components: Array = []) -> float:
	## The full compiled multiplier an incoming hit applies before rounding:
	## weapon DamageScalar target filters, the armor.ini set (with recorded
	## applied armor upgrades), stance, formation, and ability/aura factors.
	var _sim = sim
	var weapon_factor := 1.0
	if _sim.entities.has(attacker_id):
		var attacker_effect = _sim._applied_weapon_effect(_sim.entities[attacker_id] as Dictionary)
		if not attacker_effect.is_empty():
			weapon_factor = _sim._damage_scalar_factor(attacker_effect.get("scalars", []) as Array, target, target_kind)
	var factor := weapon_factor * _member_body_damage_factor(
		target, damage_type, components
	)
	if target_kind == "battalion" and _is_flanking_hit(attacker_id, target):
		factor *= _flanked_penalty_multiplier(target)
	return factor


func _active_armor_table(target: Dictionary) -> Dictionary:
	var rule: Dictionary = sim._unit_armor.get(String(target.get("object_id", "")), {})
	if rule.is_empty() or bool(rule.get("passthrough", false)):
		return {}
	var table := rule
	var active := String(target.get("active_armor_upgrade", ""))
	if active != "":
		var upgrades: Dictionary = rule.get("upgrades", {})
		if (target.get("applied_upgrades", {}) as Dictionary).has(active) and upgrades.has(active):
			table = upgrades[active]
	return table


func _flanked_penalty_multiplier(target: Dictionary) -> float:
	## armor.ini FlankedPenalty is extra incoming damage when hit from behind
	## the facing hemisphere. SoldierArmor authors 50% → 1.5x. No field → 1.0.
	var table := _active_armor_table(target)
	var penalty := float(table.get("flanked_penalty", 0.0))
	if not is_finite(penalty) or penalty <= 0.0:
		return 1.0
	return 1.0 + penalty


func _flanking_outgoing_multiplier(attacker: Dictionary, target: Dictionary) -> float:
	## weapon.ini FlankingBonus is extra outgoing damage when the hit is
	## behind the target facing. GondorSword 50% → 1.5x. Absent → 1.0.
	var bonus := float(attacker.get("flanking_bonus", 0.0))
	if bonus <= 0.0 or not is_finite(bonus):
		return 1.0
	if not _is_flanking_hit(int(attacker.get("id", 0)), target):
		return 1.0
	return 1.0 + bonus / 100.0


func _is_flanking_hit(attacker_id: int, target: Dictionary) -> bool:
	var _sim = sim
	if not _sim.entities.has(attacker_id):
		return false
	var facing := Vector2(target.get("facing", Vector2.ZERO))
	if facing.length_squared() <= 0.000001:
		return false
	var origin := Vector2(target.get("position", Vector2.ZERO))
	var attacker_at = Vector2((_sim.entities[attacker_id] as Dictionary).get("position", Vector2.ZERO))
	var from_target = attacker_at - origin
	if from_target.length_squared() <= 0.000001:
		return false
	return facing.normalized().dot(from_target.normalized()) < 0.0


func _member_body_damage_factor(
	target: Dictionary, damage_type: String, components: Array = []
) -> float:
	## ActiveBody-side factors only. Outgoing weapon/ability scalars have
	## already formed DamageInfo.in.m_amount before Body::attemptDamage.
	var _sim = sim
	return (
		_sim._member_armor_scalar(target, damage_type, components)
		# INNATE_ARMOR: a damage-TAKEN scalar the object carries for its whole
		# life, as against the timed ability/aura grants above it. Retail's
		# Create-a-Hero armour ladder is authored exactly this way, so a HIGHER
		# step means MORE damage taken and the number arrives already inverted.
		# Absent on every retail unit, which keeps this factor exactly 1.0 there.
		* float(target.get("innate_armor_scalar", 1.0))
		* float(_sim._stance_state(target).get("incomingDamageMultiplier", 1.0))
		* float(_sim._formation_effects(target).get("incoming_damage_multiplier", 1.0))
		* _sim._ability_incoming_multiplier(target)
	)


func _highlander_raw_damage_amount(
	raw_amount: float,
	current_health: int,
	damage_type: String,
	damage_components: Array
) -> float:
	## HighlanderBody mutates raw DamageInfo before ActiveBody applies armor.
	## Only exact DAMAGE_UNRESISTABLE bypasses the clamp. A mixed aggregate
	## cannot preserve the source engine's per-DamageInfo ordering, so refuse
	## it explicitly rather than silently choosing immortal or lethal.
	if damage_type.to_lower() == "unresistable":
		return maxf(0.0, raw_amount)
	var has_unresistable := false
	var has_other := false
	for component_value in damage_components:
		if typeof(component_value) != TYPE_DICTIONARY:
			continue
		var component_type := String(
			(component_value as Dictionary).get("damage_type", "")
		).to_lower()
		if component_type == "unresistable":
			has_unresistable = true
		else:
			has_other = true
	if has_unresistable and has_other:
		return -1
	if has_unresistable:
		return maxf(0.0, raw_amount)
	return minf(maxf(0.0, raw_amount), float(maxi(0, current_health - 1)))


func _structure_uses_highlander_body(target: Dictionary) -> bool:
	return bool(target.get("highlander_body", false))


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
	sim._combat_subsystem()._apply_member_damage(attacker_id, attacker_member_index, target_id, amount, target_kind, attack_sequence, forced_target_member, damage_type_override, death_type, damage_components_override)


func _bookkeep_battalion_death(
	entity_id: int, row: Dictionary, death_type: String, defeated_members: Array[int],
	attacker_id: int = 0
) -> Dictionary:
	## Shared authoritative bookkeeping for every battalion lethal path. Callers
	## retain path-specific kill credit/events, but lifetime, corpse policy,
	## selection, routing, and command-point release cannot drift apart.
	var _sim = sim
	_sim._schedule_respawn_update(entity_id, row, death_type, attacker_id)
	_sim._on_ring_entity_death(entity_id, row)
	_sim._summon_despawn_ticks.erase(entity_id)
	_sim._summon_aura_source_ids.erase(entity_id)
	# A produced hero's death releases its identity on every lethal path, not
	# only ordinary member combat. Spawn-roster heroes were never registered.
	var defeated_identity := "%d:%s" % [
		int(row.get("team", -1)), String(row.get("unit_type", ""))
	]
	if _sim._completed_hero_identities.has(defeated_identity):
		_sim._completed_hero_identities.erase(defeated_identity)
		_sim._emit_event(
			"hero.identity_released", entity_id, 0,
			{"unit_type": String(row.get("unit_type", ""))}
		)
	if _sim.entities.has(attacker_id) and not bool(row.get("is_banner_carrier", false)):
		_sim.award_power_kill(int((_sim.entities[attacker_id] as Dictionary).get("team", -1)))
	var death_policy := _apply_playable_unit_death_policy(
		row, death_type, defeated_members
	)
	row["state"] = "death"
	row["target_id"] = 0
	_sim._clear_pending_route(row, true)
	_sim.selected_ids.erase(entity_id)
	sim._release_command_points_once(row)
	_sim.prune_control_groups()
	return death_policy


func _apply_playable_unit_death_policy(
	row: Dictionary, death_type: String, defeated_members: Array[int]
) -> Dictionary:
	## The single policy boundary for every materialized playable-unit lethal
	## path. Executable container/object DestroyDie removes the authoritative
	## battalion object. primaryMember remains descriptor evidence only because
	## this simulation has no independently materialized member-corpse object.
	## Callers retain their path-specific kill credit and event bookkeeping.
	var _sim = sim
	var member_ticks: Array = row.get("member_corpse_expire_ticks", [])
	for member_index in defeated_members:
		if member_index >= 0 and member_index < member_ticks.size():
			member_ticks[member_index] = _sim.tick_index + _sim.CORPSE_LIFETIME_TICKS
	if not defeated_members.is_empty():
		row["member_corpse_expire_ticks"] = member_ticks
		_award_own_guys_die_experience(row, defeated_members.size())
	var destroy_object := (
		int(row.get("health", 0)) <= 0
		and (
			_destroy_die_matches(row, "container", death_type)
			or _destroy_die_matches(row, "object", death_type)
		)
	)
	# KeepObjectDie module contract: destroyOnDeath=false keeps a readable
	# corpse when the death type matches the attached policy (excluded types
	# do not keep — TOPPLED under ALL -TOPPLED still erases if DestroyDie says so).
	if _keep_object_die_matches(row, death_type):
		destroy_object = false
	if int(row.get("health", 0)) <= 0:
		# RefundDie is a generic Object DieMux behavior (MordorTributeCart is the
		# retail non-structure carrier), so battalion/object deaths share the same
		# once-only dispatch as sim.structures.
		_sim._apply_refund_die_on_death(row)
		_schedule_fire_weapon_when_dead(row, death_type, "entity")
		var slow_death_started := _begin_slow_death_core(row, death_type)
		# A matched DestroyDie used to erase the object on the SAME tick, which
		# handed the simulation an effective fade window of 0. Retail authors
		# that window as SlowDeathBehavior DestructionDelay on a
		# `DeathTypes = NONE +FADED` module (pure-retail range 1000..10000 ms).
		# When the pack carries the authored delay for THIS death type, the
		# object stays until it elapses; with no authored delay the immediate
		# removal is unchanged, so a pack that predates the emission behaves
		# exactly as before.
		if slow_death_started:
			# SlowDeath owns destruction even when DestroyDie also matched; do not
			# let the caller erase the object in the death callback.
			destroy_object = false
		else:
			var fade_ticks := _slow_death_fade_ticks(row, death_type) if destroy_object else 0
			row["corpse_expire_tick"] = (
				_sim.tick_index + fade_ticks if destroy_object else _sim.tick_index + _sim.CORPSE_LIFETIME_TICKS
			)
		_consume_create_object_die(row, death_type)
	return {
		"destroy_object": destroy_object,
	}


func _slow_death_core_matches(policy: Dictionary, death_type: String) -> bool:
	var folded := death_type.strip_edges().to_upper()
	if folded == "":
		folded = "NORMAL"
	var mode := String(policy.get("death_types", "")).to_upper()
	if mode == "ALL":
		for excluded_value in policy.get("excluded_death_types", []) as Array:
			if String(excluded_value).to_upper() == folded:
				return false
		return true
	if mode == "NONE":
		for included_value in policy.get("included_death_types", []) as Array:
			if String(included_value).to_upper() == folded:
				return true
	return false


func _slow_death_delay_ticks(milliseconds: float) -> int:
	if milliseconds <= 0.0:
		return 0
	# Retail parseDurationUnsignedInt rounds partial logic frames upward.
	return maxi(1, ceili(milliseconds / 1000.0 / sim.TICK_SECONDS))


func _begin_slow_death_core(row: Dictionary, death_type: String) -> bool:
	var _sim = sim
	if row.has("slow_death_state"):
		return true
	if not row.has("slow_death_core_contracts"):
		_sim._attach_module_contracts(row)
	var applicable: Array[Dictionary] = []
	var total_weight := 0
	for policy_value in row.get("slow_death_core_contracts", []) as Array:
		if typeof(policy_value) != TYPE_DICTIONARY:
			continue
		var policy := policy_value as Dictionary
		if not _slow_death_core_matches(policy, death_type):
			continue
		var weight := maxi(0, int(policy.get("probability_weight", 0)))
		if weight <= 0:
			continue
		applicable.append(policy)
		total_weight += weight
	if applicable.is_empty() or total_weight <= 0:
		return false
	# Retail always draws 1..total, including a single applicable row.
	var roll = _sim.logic_random_int(1, total_weight)
	var selected: Dictionary = applicable[-1]
	for policy_value in applicable:
		var policy := policy_value as Dictionary
		roll -= int(policy.get("probability_weight", 0))
		if roll <= 0:
			selected = policy
			break
	# Retail also executes both zero-variance draws before its midpoint draw.
	var sink_delay_ms := float(selected.get("sink_delay_ms", 0.0))
	var destruction_delay_ms := float(selected.get("destruction_delay_ms", 0.0))
	_sim.logic_random_int(0, 0)
	_sim.logic_random_int(0, 0)
	var sink_ticks := _slow_death_delay_ticks(sink_delay_ms)
	var destruction_ticks := _slow_death_delay_ticks(destruction_delay_ms)
	var midpoint_offset = int(_sim.logic_random_real(
		0.35 * float(destruction_ticks),
		0.65 * float(destruction_ticks)
	))
	var state := {
		"death_type": death_type.strip_edges().to_upper(),
		"selected_tag": String(selected.get("tag", "")),
		"selected_source_ini": String(selected.get("source_ini", "")),
		"selected_line": int(selected.get("line", 0)),
		"initial_tick": _sim.tick_index,
		"sink_start_tick": _sim.tick_index + sink_ticks,
		"midpoint_tick": _sim.tick_index + midpoint_offset,
		"destruction_tick": _sim.tick_index + destruction_ticks,
		"sink_rate_source_per_second": float(
			selected.get("sink_rate_source_per_second", 0.0)
		),
		"sink_depth_source": 0.0,
		"executed_phases": [],
		"presentation_receipts": (
			selected.get("presentation_receipts", []) as Array
		).duplicate(true),
	}
	row["slow_death_state"] = state
	row["corpse_expire_tick"] = _sim.tick_index + destruction_ticks
	_record_slow_death_phase(row, "INITIAL")
	return true


func _record_slow_death_phase(row: Dictionary, phase: String) -> void:
	var _sim = sim
	var state := row.get("slow_death_state", {}) as Dictionary
	if state.is_empty():
		return
	var phases := state.get("executed_phases", []) as Array
	if phases.has(phase):
		return
	phases.append(phase)
	state["executed_phases"] = phases
	# Retail chooses one FX vector entry with the logic stream at each phase.
	# Preserve that deterministic choice as a receipt, but do not emit an FX.
	var fx_candidates: Array[String] = []
	for receipt_value in state.get("presentation_receipts", []) as Array:
		if typeof(receipt_value) != TYPE_DICTIONARY:
			continue
		var receipt := receipt_value as Dictionary
		if (
			String(receipt.get("kind", "")) == "FX"
			and String(receipt.get("phase", "")) == phase
		):
			for reference_value in receipt.get("references", []) as Array:
				fx_candidates.append(String(reference_value))
	if not fx_candidates.is_empty():
		var choice_index = _sim.logic_random_int(0, fx_candidates.size() - 1)
		var choices := state.get("presentation_choices", []) as Array
		choices.append({
			"kind": "FX",
			"phase": phase,
			"selected_reference": fx_candidates[choice_index],
			"runtime_status": "deferred-presentation",
		})
		state["presentation_choices"] = choices
	row["slow_death_state"] = state
	_sim._emit_event("slow_death.phase_receipt", int(row.get("id", 0)), 0, {
		"phase": phase,
		"presentation_status": "deferred",
		"selected_tag": String(state.get("selected_tag", "")),
	})


func _step_slow_death_core() -> void:
	var _sim = sim
	for entity_id in _sim.entity_ids():
		if not _sim.entities.has(entity_id):
			continue
		var row = _sim.entities[entity_id] as Dictionary
		var state := row.get("slow_death_state", {}) as Dictionary
		if state.is_empty() or int(row.get("health", 0)) > 0:
			continue
		var rate := float(state.get("sink_rate_source_per_second", 0.0))
		if rate > 0.0 and _sim.tick_index >= int(state.get("sink_start_tick", 0)):
			state["sink_depth_source"] = (
				float(state.get("sink_depth_source", 0.0)) + rate * _sim.TICK_SECONDS
			)
			row["slow_death_state"] = state
		if _sim.tick_index >= int(state.get("midpoint_tick", 0)):
			_record_slow_death_phase(row, "MIDPOINT")
		if _sim.tick_index >= int(state.get("destruction_tick", 0)):
			_record_slow_death_phase(row, "FINAL")


func _award_own_guys_die_experience(row: Dictionary, defeated_count: int) -> void:
	sim._experience_subsystem().award_own_guys_die_experience(row, defeated_count)


func _slow_death_fade_ticks(row: Dictionary, death_type: String) -> int:
	## Authored DestructionDelay for this death type, in ticks, or 0.
	##
	## Fail-closed by construction: the adapter only projects rows whose delay
	## retail actually authored and whose expression resolved to a number, so an
	## absent or unresolvable delay reaches here as no row at all and the
	## removal stays immediate. The longest matching authored window wins when
	## an object stacks several modules on one death type.
	var rows: Array = row.get("slow_death_fades", []) as Array
	if rows.is_empty():
		return 0
	var death_folded := death_type.strip_edges().to_upper()
	if death_folded == "":
		death_folded = "NORMAL"
	var delay_ms := 0.0
	for policy_value in rows:
		if typeof(policy_value) != TYPE_DICTIONARY:
			continue
		var policy := policy_value as Dictionary
		if String(policy.get("death_types", "")).to_upper() != "NONE":
			continue
		for included_value in policy.get("included_death_types", []) as Array:
			if String(included_value).to_upper() == death_folded:
				delay_ms = maxf(delay_ms, float(policy.get("destruction_delay_ms", 0.0)))
				break
	if delay_ms <= 0.0:
		return 0
	return maxi(1, roundi(delay_ms / 1000.0 / sim.TICK_SECONDS))


func _schedule_fire_weapon_when_dead(row: Dictionary, death_type: String, source_kind: String) -> void:
	var _sim = sim
	var policies: Array = row.get("fire_weapon_when_dead", []) as Array
	if policies.is_empty():
		return
	var scheduled: Dictionary = row.get("fire_weapon_when_dead_scheduled", {}) as Dictionary
	for policy_value in policies:
		if typeof(policy_value) != TYPE_DICTIONARY:
			continue
		var policy := policy_value as Dictionary
		var once_key := "%s:%d" % [String(policy.get("tag", "")), int(policy.get("line", 0))]
		if scheduled.has(once_key):
			continue
		if not _death_mux_matches(policy, death_type):
			continue
		if not _death_status_mux_matches(row, policy):
			continue
		if (
			source_kind == "structure"
			and float(row.get("construction_progress", 1.0)) < 1.0
			and not bool(policy.get("active_during_construction", false))
		):
			continue
		var facing := Vector2(row.get("facing", Vector2.RIGHT))
		if facing.length_squared() <= 0.000001:
			facing = Vector2.RIGHT
		var local_offset = _sim._retail_source_to_sim_offset(
			Vector2(policy.get("weapon_offset_source", Vector2.ZERO))
		)
		var point = Vector2(row.get("position", Vector2.ZERO)) + local_offset.rotated(facing.angle())
		var weapon_id := String(policy.get("death_weapon", ""))
		_sim._pending_power_effects.append({
			"kind": "death_weapon",
			"fire_tick": _sim.tick_index + maxi(0, int(policy.get("delay_ticks", 0))),
			"team": int(row.get("team", -1)),
			"source_id": int(row.get("id", 0)),
			"source_kind": source_kind,
			"death_type": death_type.to_upper(),
			"weapon_id": weapon_id,
			"point": point,
			"height_source": float(policy.get("weapon_offset_z_source", 0.0)),
			"weapon_rule": (_sim._death_weapon_rules.get(weapon_id, {}) as Dictionary).duplicate(true),
		})
		scheduled[once_key] = true
		_sim._emit_event("module.death_weapon_scheduled", int(row.get("id", 0)), 0, {
			"weapon_id": weapon_id,
			"fire_tick": _sim.tick_index + maxi(0, int(policy.get("delay_ticks", 0))),
			"point": [snappedf(point.x, 0.001), snappedf(point.y, 0.001)],
			"height_source": float(policy.get("weapon_offset_z_source", 0.0)),
			"death_type": death_type.to_upper(),
		})
	row["fire_weapon_when_dead_scheduled"] = scheduled


func _death_mux_matches(policy: Dictionary, death_type: String) -> bool:
	var folded := death_type.strip_edges().to_upper()
	if folded == "":
		folded = "NORMAL"
	var mode := String(policy.get("death_types", "ALL")).to_upper()
	if mode == "ALL":
		for excluded_value in policy.get("excluded_death_types", []) as Array:
			if String(excluded_value).to_upper() == folded:
				return false
		return true
	if mode == "NONE":
		for included_value in policy.get("included_death_types", []) as Array:
			if String(included_value).to_upper() == folded:
				return true
	return false


func _death_status_mux_matches(row: Dictionary, policy: Dictionary) -> bool:
	var statuses: Dictionary = row.get("object_status", {}) as Dictionary
	for required_value in policy.get("required_status", []) as Array:
		if not bool(statuses.get(String(required_value), false)):
			return false
	for exempt_value in policy.get("exempt_status", []) as Array:
		if bool(statuses.get(String(exempt_value), false)):
			return false
	return true


func _create_object_die_matches(row: Dictionary, death_type: String) -> bool:
	if not bool(row.get("create_object_die", false)):
		return false
	var policy: Dictionary = row.get("create_object_die_policy", {}) as Dictionary
	if policy.is_empty():
		return false
	var death_folded := death_type.strip_edges().to_upper()
	if death_folded == "":
		death_folded = "NORMAL"
	var mode := String(policy.get("death_types", "ALL")).to_upper()
	if mode == "ALL":
		for excluded_value in policy.get("excluded_death_types", []) as Array:
			if String(excluded_value).to_upper() == death_folded:
				return false
	elif mode == "NONE":
		var included: Array = policy.get("included_death_types", []) as Array
		var hit := false
		for included_value in included:
			if String(included_value).to_upper() == death_folded:
				hit = true
				break
		if not hit:
			return false
	else:
		return false
	return String(policy.get("creation_list", "")) != ""


func _consume_create_object_die(row: Dictionary, death_type: String) -> void:
	## Queue CreationList spawn intent, then hatch through registered OCL leaves
	## when present (same machinery as spellbook summon chains).
	var _sim = sim
	if not _create_object_die_matches(row, death_type):
		return
	var policy: Dictionary = row.get("create_object_die_policy", {}) as Dictionary
	var entry := {
		"team": int(row.get("team", -1)),
		"position": row.get("position", Vector2.ZERO),
		"creation_list": String(policy.get("creation_list", "")),
		"source_entity": int(row.get("id", 0)),
		"source_unit_type": String(row.get("unit_type", "")),
		"death_type": death_type,
		"tick": _sim.tick_index,
	}
	_sim.create_object_die_pending.append(entry)
	_sim._emit_event(
		"module.create_object_die",
		int(row.get("id", 0)),
		0,
		{
			"creation_list": entry["creation_list"],
			"team": entry["team"],
		}
	)
	var hatch := hatch_create_object_die_entry(entry)
	entry["hatch"] = hatch
	_sim.create_object_die_pending[_sim.create_object_die_pending.size() - 1] = entry


func hatch_create_object_die_entry(entry: Dictionary) -> Dictionary:
	## Resolve OCL_id -> createObjects -> sim.script_spawn_entity for each converted
	## object type. Unconverted leaves stay pending with reason (fail-closed).
	var _sim = sim
	var ocl_id := String(entry.get("creation_list", ""))
	var team := int(entry.get("team", -1))
	var at: Vector2 = entry.get("position", Vector2.ZERO)
	if ocl_id == "" or team < 0:
		return {"ok": false, "reason": "invalid-entry"}
	var ocl: Dictionary = {}
	if _sim.parity != null:
		ocl = (_sim.parity.ocl_leaves as Dictionary).get(ocl_id, {}) as Dictionary
	if ocl.is_empty():
		# Try spellbook leaf tables already loaded on this sim.
		ocl = _ocl_leaf_lookup(ocl_id)
	if ocl.is_empty():
		return {"ok": false, "reason": "ocl-not-registered:%s" % ocl_id}
	var spawned: Array = []
	var ordinal := 0
	for create_value in Array(ocl.get("createObjects", [])):
		if typeof(create_value) != TYPE_DICTIONARY:
			continue
		var create := create_value as Dictionary
		var count := 1
		for field_value in Array(create.get("fields", [])):
			if typeof(field_value) == TYPE_DICTIONARY and String(
				(field_value as Dictionary).get("key", "")
			) == "Count":
				count = maxi(1, int((field_value as Dictionary).get("resolved", 1)))
		for object_name_value in Array(create.get("objects", [])):
			var object_name := String(object_name_value)
			for _i in range(count):
				ordinal += 1
				var offset := Vector2(8.0 * float(ordinal), 0.0)
				var result: Dictionary = _sim.spawn_scenario_object(
					object_name, team, at + offset, "object-creation-list"
				)
				if not bool(result.get("ok", false)):
					result = _sim.script_spawn_entity(
						object_name, team, at + offset, "object-creation-list"
					)
				if bool(result.get("ok", false)):
					var spawned_id := int(result.get("id", result.get("entity_id", 0)))
					spawned.append(spawned_id)
					_sim._emit_event(
						"module.create_object_die.hatch",
						spawned_id,
						int(entry.get("source_entity", 0)),
						{
							"ocl": ocl_id,
							"object": object_name,
							"kind": String(result.get("kind", "unit")),
						}
					)
				else:
					# Fail-closed for this object; continue other creates.
					_sim._emit_event(
						"module.create_object_die.hatch_failed",
						int(entry.get("source_entity", 0)),
						0,
						{
							"ocl": ocl_id,
							"object": object_name,
							"reason": String(result.get("reason", "")),
						}
					)
	if spawned.is_empty():
		return {"ok": false, "reason": "no-spawns", "ocl": ocl_id}
	return {"ok": true, "spawned": spawned, "ocl": ocl_id}


func _ocl_leaf_lookup(ocl_id: String) -> Dictionary:
	var _sim = sim
	if _sim.parity != null and (_sim.parity.ocl_leaves as Dictionary).has(ocl_id):
		return (_sim.parity.ocl_leaves as Dictionary)[ocl_id]
	return {}


func register_ocl_leaf(ocl_id: String, leaf: Dictionary) -> void:
	var _sim = sim
	_sim._ensure_parity()
	_sim.parity.register_ocl_leaf(ocl_id, leaf)


func register_object_leaf(object_id: String, leaf: Dictionary) -> void:
	var _sim = sim
	_sim._ensure_parity()
	_sim.parity.register_object_leaf(object_id, leaf)


func ingest_ocl_leaves_from_document(document: Dictionary) -> int:
	## Register ObjectCreationList leaves from a spellbook/summon/pack document
	## so CreateObjectDie hatch can resolve CreationList ids without inventing
	## createObjects. Returns the number of OCL ids registered.
	sim._ensure_parity()
	var registered := 0
	var leaves: Dictionary = document.get("leaves", {}) as Dictionary
	for ocl_value in Array(leaves.get("objectCreationLists", [])):
		if typeof(ocl_value) != TYPE_DICTIONARY:
			continue
		var ocl := ocl_value as Dictionary
		var ocl_id := String(ocl.get("id", ""))
		if ocl_id == "":
			continue
		register_ocl_leaf(ocl_id, ocl)
		registered += 1
	# Top-level array form used by some converters.
	for ocl_value2 in Array(document.get("objectCreationLists", [])):
		if typeof(ocl_value2) != TYPE_DICTIONARY:
			continue
		var ocl2 := ocl_value2 as Dictionary
		var ocl_id2 := String(ocl2.get("id", ""))
		if ocl_id2 == "":
			continue
		register_ocl_leaf(ocl_id2, ocl2)
		registered += 1
	return registered



