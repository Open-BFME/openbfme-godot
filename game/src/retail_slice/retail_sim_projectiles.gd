extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Member-projectile subsystem extracted from retail_slice_sim.gd (Q81a,
## strangler-fig extraction #1). Pure code move: the authoritative state
## (sim.projectiles, sim._next_projectile_id) stays on the sim so direct
## test access and serialization are untouched; this class is stateless
## logic operating through the sim back-reference. The sim keeps one-line
## delegates under the original names, so call sites and tick order are
## byte-identical.
##
## Scope: ordinary member weapon projectiles (launch, flight-by-impact-tick,
## impact resolution, radius/splash damage). NOT here: structure
## pending_projectile bookkeeping, scenario bezier flights, and fling
## physics landings — separate systems, later extractions.

# Weak back-reference: a strong ref would form a RefCounted cycle with the
# sim (which holds this subsystem), leaking freed sims as zombies — the
# script_wiring orphan-refusal contracts catch exactly that. The getter
# keeps plain `sim.` syntax working everywhere below.




func step() -> void:
	if sim.projectiles.is_empty():
		return
	var ids: Array = sim.projectiles.keys()
	ids.sort()
	for id_value in ids:
		var projectile_id := int(id_value)
		if not sim.projectiles.has(projectile_id):
			continue
		var projectile := sim.projectiles[projectile_id] as Dictionary
		if sim.tick_index < int(projectile.get("impact_tick", sim.tick_index)):
			continue
		resolve_member_projectile_impact(projectile_id, projectile)


func resolve_member_projectile_impact(projectile_id: int, projectile: Dictionary) -> void:
	var attacker_id := int(projectile.get("attacker_id", 0))
	var target_id := int(projectile.get("target_id", 0))
	var target_kind := String(projectile.get("target_kind", "battalion"))
	var forced_member := int(projectile.get("member_index", -1))
	var target_live: bool = sim._target_alive(target_id, target_kind)
	if target_live and target_kind == "battalion":
		var target := sim.entities.get(target_id, {}) as Dictionary
		var health_values: Array = target.get("member_health", [])
		if forced_member < 0 or forced_member >= health_values.size() or int(health_values[forced_member]) <= 0:
			forced_member = -1
			for index in health_values.size():
				if int(health_values[index]) > 0:
					forced_member = index
					break
		target_live = forced_member >= 0
	if not target_live:
		sim._emit_event("combat.projectile_cancelled", attacker_id, target_id, {
			"projectile_token": projectile_id,
			"projectile_object_id": String(projectile.get("projectile_object_id", "")),
		})
		sim.projectiles.erase(projectile_id)
		return
	var components := projectile.get("damage_components", []) as Array
	var death_type := "NORMAL"
	if not components.is_empty():
		death_type = String((components[0] as Dictionary).get("death_type", "NORMAL"))
	sim._apply_member_damage(
		attacker_id,
		int(projectile.get("attacker_member_index", -1)),
		target_id,
		maxi(1, int(projectile.get("amount", 1))),
		target_kind,
		int(projectile.get("attack_sequence", 0)),
		forced_member,
		String(projectile.get("damage_type", "")),
		death_type,
		components
	)
	# Upgrade-gated projectile nuggets share the shell's impact boundary; none
	# are allowed to leak damage onto the launch tick.
	if sim.entities.has(attacker_id):
		sim._apply_member_bonus_nuggets(
			attacker_id,
			int(projectile.get("attacker_member_index", -1)),
			sim.entities[attacker_id] as Dictionary,
			target_id,
			target_kind,
			forced_member,
			{"bonus_nuggets": projectile.get("bonus_nuggets", [])}
		)
	var origin := Vector2(projectile.get("origin", Vector2.ZERO))
	for component_value in components:
		if typeof(component_value) != TYPE_DICTIONARY:
			continue
		var component := component_value as Dictionary
		var radius := maxf(0.0, float(component.get("radius", 0.0)))
		if radius <= 0.0:
			continue
		apply_radius_damage(
			attacker_id,
			origin,
			radius,
			float(component.get("value", 0.0)),
			String(component.get("damage_type", projectile.get("damage_type", ""))),
			float(component.get("damage_taper_off", 0.0)),
			String(projectile.get("radius_damage_affects", "ENEMIES")),
			target_id,
			String(component.get("death_type", "NORMAL"))
		)
	sim._emit_event("combat.projectile_impact", attacker_id, target_id, {
		"projectile_token": projectile_id,
		"projectile_object_id": String(projectile.get("projectile_object_id", "")),
		"origin": origin,
	})
	sim.projectiles.erase(projectile_id)


func radius_relation_allowed(attacker_team: int, victim_team: int, affects: String) -> bool:
	var tokens: Array[String] = []
	for token_value in affects.to_upper().split(" ", false):
		var token := String(token_value).strip_edges()
		if token != "" and not tokens.has(token):
			tokens.append(token)
	if sim._is_hostile(attacker_team, victim_team):
		return tokens.has("ENEMIES")
	if sim._is_combatant_team(victim_team):
		return tokens.has("ALLIES")
	return tokens.has("NEUTRALS")


func tapered_radius_amount(amount: float, distance: float, radius: float, taper_off: float) -> int:
	if amount <= 0.0 or radius <= 0.0 or distance > radius:
		return 0
	# OpenSAGE's DamageTaperOff reading: a percentage of the base damage is
	# removed linearly across the radius. Thus taper=50 leaves 50% at the edge.
	var edge_loss := clampf(taper_off, 0.0, 100.0) / 100.0
	var multiplier := 1.0 - edge_loss * clampf(distance / radius, 0.0, 1.0)
	return maxi(0, roundi(amount * multiplier))


func apply_radius_damage(
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
	if not sim.entities.has(attacker_id):
		return
	var attacker_team := int((sim.entities[attacker_id] as Dictionary).get("team", -1))
	for candidate in sim._spatial_gather_sorted(origin, radius):
		if candidate == exclude_target_id or not sim.entities.has(candidate):
			continue
		var target := sim.entities[candidate] as Dictionary
		if int(target.get("health", 0)) <= 0 or not radius_relation_allowed(attacker_team, int(target.get("team", -1)), affects):
			continue
		var distance := origin.distance_to(Vector2(target.get("position", Vector2.ZERO)))
		var tapered_amount := tapered_radius_amount(amount, distance, radius, taper_off)
		if tapered_amount > 0:
			sim._apply_member_damage(attacker_id, -1, candidate, tapered_amount, "battalion", 0, -1, damage_type, death_type)
	var structure_keys: Array = sim.structure_ids()
	structure_keys.sort()
	for structure_id_value in structure_keys:
		var structure_id := int(structure_id_value)
		if structure_id == exclude_target_id or not sim.structures.has(structure_id):
			continue
		var target := sim.structures[structure_id] as Dictionary
		if int(target.get("health", 0)) <= 0 or not radius_relation_allowed(attacker_team, int(target.get("team", -1)), affects):
			continue
		var distance := origin.distance_to(Vector2(target.get("position", Vector2.ZERO)))
		var tapered_amount := tapered_radius_amount(amount, distance, radius, taper_off)
		if tapered_amount > 0:
			sim._apply_structure_damage(attacker_id, structure_id, tapered_amount, damage_type)


func member_weapon_has_projectile(row: Dictionary) -> bool:
	return (
		String(row.get("projectile_object_id", "")) != ""
		and float(row.get("projectile_speed", 0.0)) > 0.0
	)


func scaled_projectile_components(components: Array, outgoing_amount: int) -> Array:
	var total := 0.0
	for component_value in components:
		if typeof(component_value) == TYPE_DICTIONARY:
			total += maxf(0.0, float((component_value as Dictionary).get("value", 0.0)))
	if total <= 0.0:
		return []
	var scale := float(outgoing_amount) / total
	var output: Array = []
	for component_value in components:
		if typeof(component_value) != TYPE_DICTIONARY:
			continue
		var component := (component_value as Dictionary).duplicate(true)
		component["value"] = maxf(0.0, float(component.get("value", 0.0)) * scale)
		output.append(component)
	return output


func launch_member_projectile(
	attacker_id: int,
	member_index: int,
	row: Dictionary,
	target_id: int,
	target_kind: String,
	forced_target: int,
	swing_damage: int,
	weapon_effect: Dictionary,
	release_token: int
) -> void:
	var target: Dictionary = sim.structures.get(target_id, {}) if target_kind == "structure" else sim.entities.get(target_id, {})
	if target.is_empty():
		return
	var launch_origin := Vector2(row.get("position", Vector2.ZERO))
	var impact_origin := Vector2(target.get("position", Vector2.ZERO))
	var distance := launch_origin.distance_to(impact_origin)
	var speed := float(row.get("projectile_speed", 0.0))
	var flight_ticks := maxi(1, ceili(distance / speed / sim.TICK_SECONDS))
	var projectile_id: int = sim._next_projectile_id
	sim._next_projectile_id += 1
	sim.projectiles[projectile_id] = {
		"id": projectile_id,
		"attacker_id": attacker_id,
		"attacker_member_index": member_index,
		"member_index": forced_target,
		"target_id": target_id,
		"target_kind": target_kind,
		"origin": impact_origin,
		"launch_origin": launch_origin,
		"launch_tick": sim.tick_index,
		"impact_tick": sim.tick_index + flight_ticks,
		"projectile_object_id": String(row.get("projectile_object_id", "")),
		"amount": swing_damage,
		"damage_components": scaled_projectile_components(row.get("damage_components", []) as Array, swing_damage),
		"damage_type": String(row.get("damage_type", "")),
		"radius_damage_affects": String(row.get("radius_damage_affects", "ENEMIES")),
		"release_token": release_token,
		"attack_sequence": int(row.get("attack_sequence", 0)),
		"bonus_nuggets": (weapon_effect.get("bonus_nuggets", []) as Array).duplicate(true),
	}
	sim._emit_event("combat.projectile_launched", attacker_id, target_id, {
		"projectile_token": projectile_id,
		"impact_tick": sim.tick_index + flight_ticks,
		"projectile_object_id": String(row.get("projectile_object_id", "")),
		"team": int(row.get("team", -1)),
		"launch_position": launch_origin,
		"target_position": impact_origin,
		"member_index": member_index,
		"member_release_token": release_token,
	})
