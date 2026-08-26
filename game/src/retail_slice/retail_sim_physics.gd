extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Physics objects carved out of retail_slice_sim.gd (drawer 22): fling/airborne physics spawns, gravity stepping, landings, recovery phases, radius damage taper.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func spawn_physics_object(
	source_object_id: String,
	position: Vector2,
	height_source: float,
	horizontal_velocity: Vector2,
	vertical_velocity_source: float,
	contract: Dictionary
) -> int:
	## Materialize one explicitly thrown/knocked-back body from the typed
	## importer contract. Opaque legacy rows fail closed; this runtime must not
	## reinterpret authored strings or silently invent fields.
	if String(contract.get("module", "")) != "PhysicsBehavior":
		return -1
	if String(contract.get("extraction", "")) != "typed":
		return -1
	var fields: Dictionary = contract.get("fields", {}) as Dictionary
	var gravity_value: Variant = sim._module_contract_value(fields, "GravityMult", 1.0)
	var first_height_value: Variant = sim._module_contract_value(fields, "FirstHeight", 0.0)
	var second_height_value: Variant = sim._module_contract_value(fields, "SecondHeight", 0.0)
	var bounce_value: Variant = sim._module_contract_value(fields, "AllowBouncing", false)
	var orient_value: Variant = sim._module_contract_value(fields, "OrientToFlightPath", false)
	var kill_value: Variant = sim._module_contract_value(fields, "KillWhenRestingOnGround", false)
	var low_value: Variant = sim._module_contract_value(fields, "ShockStunnedTimeLow", 0.0)
	var high_value: Variant = sim._module_contract_value(fields, "ShockStunnedTimeHigh", 0.0)
	var standing_value: Variant = sim._module_contract_value(fields, "ShockStandingTime", 0.0)
	for number_value in [gravity_value, first_height_value, second_height_value, low_value, high_value, standing_value]:
		if typeof(number_value) not in [TYPE_INT, TYPE_FLOAT]:
			return -1
	if typeof(bounce_value) != TYPE_BOOL or typeof(orient_value) != TYPE_BOOL or typeof(kill_value) != TYPE_BOOL:
		return -1
	if float(gravity_value) < 0.0 or float(first_height_value) < 0.0 or float(second_height_value) < 0.0:
		return -1
	if float(low_value) < 0.0 or float(high_value) < 0.0 or float(standing_value) < 0.0:
		return -1
	var low_ms := mini(roundi(float(low_value)), roundi(float(high_value)))
	var high_ms := maxi(roundi(float(low_value)), roundi(float(high_value)))
	var unsupported: Array[String] = []
	if bool(bounce_value) and float(first_height_value) <= 0.0 and float(second_height_value) <= 0.0:
		# The typed descriptor does not carry collision restitution. Retail rows
		# that only say AllowBouncing therefore remain explicitly unresolved at
		# the impact boundary instead of acquiring an invented coefficient.
		unsupported.append("bounce_restitution_without_authored_heights")
	var id = sim._next_physics_object_id
	sim._next_physics_object_id += 1
	sim.physics_objects[id] = {
		"id": id,
		"source_object_id": source_object_id,
		"position": position,
		"height_source": maxf(0.0, height_source),
		"horizontal_velocity": horizontal_velocity,
		"vertical_velocity_source": vertical_velocity_source,
		"gravity_multiplier": float(gravity_value),
		"allow_bouncing": bool(bounce_value),
		"orient_to_flight_path": bool(orient_value),
		"kill_when_resting_on_ground": bool(kill_value),
		"first_height_source": float(first_height_value),
		"second_height_source": float(second_height_value),
		"shock_stunned_low_ms": low_ms,
		"shock_stunned_high_ms": high_ms,
		"shock_standing_ms": roundi(float(standing_value)),
		"bounce_count": 0,
		"phase": "airborne",
		"phase_ticks_remaining": 0,
		"yaw_radians": 0.0,
		"pitch_radians": 0.0,
		"unsupported_semantics": unsupported,
		"contract_tag": String(contract.get("tag", "")),
		"contract_line": int(contract.get("line", 0)),
	}
	return id


func _step_physics_objects() -> void:
	if sim.physics_objects.is_empty():
		return
	var ids = sim.physics_objects.keys()
	ids.sort()
	var gravity_source = maxf(0.0, float(sim._rules.get("physics_gravity_source_per_second_squared", 0.0)))
	for id_value in ids:
		var id := int(id_value)
		if not sim.physics_objects.has(id):
			continue
		var row = sim.physics_objects[id] as Dictionary
		match String(row.get("phase", "airborne")):
			"airborne":
				_step_airborne_physics_object(id, row, gravity_source)
			"shock_stunned":
				_step_physics_recovery_phase(row, "shock_standing")
			"shock_standing":
				_step_physics_recovery_phase(row, "recovered")


func _step_projectiles() -> void:
	sim._projectiles_subsystem().step()


func _resolve_member_projectile_impact(projectile_id: int, projectile: Dictionary) -> void:
	sim._projectiles_subsystem().resolve_member_projectile_impact(projectile_id, projectile)


func _radius_relation_allowed(attacker_team: int, victim_team: int, affects: String) -> bool:
	return sim._projectiles_subsystem().radius_relation_allowed(attacker_team, victim_team, affects)


func _tapered_radius_amount(amount: float, distance: float, radius: float, taper_off: float) -> int:
	return sim._projectiles_subsystem().tapered_radius_amount(amount, distance, radius, taper_off)


func _apply_radius_damage(
	attacker_id: int,
	origin: Vector2,
	radius: float,
	amount: float,
	damage_type: String,
	taper_off: float,
	affects: String,
	exclude_target_id: int,
	death_type: String = "NORMAL"
) -> void:
	sim._projectiles_subsystem().apply_radius_damage(attacker_id, origin, radius, amount, damage_type, taper_off, affects, exclude_target_id, death_type)


func _step_airborne_physics_object(id: int, row: Dictionary, gravity_source: float) -> void:
	row["position"] = Vector2(row.get("position", Vector2.ZERO)) + Vector2(row.get("horizontal_velocity", Vector2.ZERO)) * sim.TICK_SECONDS
	var vertical_velocity := float(row.get("vertical_velocity_source", 0.0))
	vertical_velocity -= gravity_source * float(row.get("gravity_multiplier", 1.0)) * sim.TICK_SECONDS
	row["vertical_velocity_source"] = vertical_velocity
	row["height_source"] = float(row.get("height_source", 0.0)) + vertical_velocity * sim.TICK_SECONDS
	if bool(row.get("orient_to_flight_path", false)):
		var horizontal := Vector2(row.get("horizontal_velocity", Vector2.ZERO))
		if not horizontal.is_zero_approx():
			row["yaw_radians"] = horizontal.angle()
		row["pitch_radians"] = atan2(vertical_velocity, horizontal.length())
	if float(row.get("height_source", 0.0)) > 0.0:
		return
	row["height_source"] = 0.0
	var bounce_count := int(row.get("bounce_count", 0))
	var rebound_height := 0.0
	if bool(row.get("allow_bouncing", false)):
		if bounce_count == 0:
			rebound_height = float(row.get("first_height_source", 0.0))
		elif bounce_count == 1:
			rebound_height = float(row.get("second_height_source", 0.0))
	if rebound_height > 0.0 and gravity_source * float(row.get("gravity_multiplier", 1.0)) > 0.0:
		row["bounce_count"] = bounce_count + 1
		row["vertical_velocity_source"] = sqrt(2.0 * gravity_source * float(row.get("gravity_multiplier", 1.0)) * rebound_height)
		return
	row["horizontal_velocity"] = Vector2.ZERO
	row["vertical_velocity_source"] = 0.0
	if not (row.get("landing_warhead", {}) as Dictionary).is_empty():
		_resolve_fling_landing(row)
	if bool(row.get("kill_when_resting_on_ground", false)):
		sim.physics_objects.erase(id)
		return
	_begin_physics_recovery(row)


func _resolve_fling_landing(projectile: Dictionary) -> void:
	var warhead := projectile.get("landing_warhead", {}) as Dictionary
	var point := Vector2(projectile.get("position", Vector2.ZERO))
	var radius := float(warhead.get("radius_scaled", 0.0))
	var force_filter: Array = String(warhead.get("forceKillObjectFilter", "")).split(" ", false)
	var affected: Array[int] = []
	for target_id in sim.entity_ids():
		var target = sim.entities[target_id] as Dictionary
		if int(target.get("health", 0)) <= 0 or Vector2(target.get("position", Vector2.ZERO)).distance_to(point) > radius + 0.000001:
			continue
		if not sim._transport_filter_accepts(target, force_filter):
			continue
		sim._apply_damage(int(projectile.get("fling_attacker_id", 0)), target_id, 2147483647, "battalion", String(warhead.get("deathType", "NORMAL")), String(warhead.get("damageType", "")))
		affected.append(target_id)
	sim._emit_event("ability.fling_landed", int(projectile.get("fling_attacker_id", 0)), 0, {"warhead_id": String(warhead.get("id", "")), "damage_type": String(warhead.get("damageType", "")), "death_type": String(warhead.get("deathType", "")), "affected_ids": affected, "special_filter_without_damage_amount": String(warhead.get("specialObjectFilter", ""))})


func _begin_physics_recovery(row: Dictionary) -> void:
	var low_ms := int(row.get("shock_stunned_low_ms", 0))
	var high_ms := int(row.get("shock_stunned_high_ms", 0))
	if high_ms > 0:
		var stunned_ms = sim.logic_random_int(low_ms, high_ms)
		row["phase"] = "shock_stunned"
		row["phase_ticks_remaining"] = maxi(1, ceili(float(stunned_ms) / (sim.TICK_SECONDS * 1000.0)))
		return
	var standing_ms := int(row.get("shock_standing_ms", 0))
	if standing_ms > 0:
		row["phase"] = "shock_standing"
		row["phase_ticks_remaining"] = maxi(1, ceili(float(standing_ms) / (sim.TICK_SECONDS * 1000.0)))
		return
	row["phase"] = "recovered"
	row["phase_ticks_remaining"] = 0


func _step_physics_recovery_phase(row: Dictionary, next_phase: String) -> void:
	var remaining := int(row.get("phase_ticks_remaining", 0)) - 1
	if remaining > 0:
		row["phase_ticks_remaining"] = remaining
		return
	if next_phase == "shock_standing":
		var standing_ms := int(row.get("shock_standing_ms", 0))
		if standing_ms > 0:
			row["phase"] = "shock_standing"
			row["phase_ticks_remaining"] = maxi(1, ceili(float(standing_ms) / (sim.TICK_SECONDS * 1000.0)))
			return
	row["phase"] = "recovered"
	row["phase_ticks_remaining"] = 0


