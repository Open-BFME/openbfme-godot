extends RefCounted
## Per-entity state machine carved out of retail_slice_sim.gd (drawer 20): the _step_entity tick core (movement, orders, engagement, recovery phases).
## State stays on the sim; the sim keeps one-line delegates under the original names.

var _sim_ref: WeakRef
var sim:
	get:
		return _sim_ref.get_ref()

func _init(owning_sim) -> void:
	_sim_ref = weakref(owning_sim)

func _step_entity(id: int) -> void:
	var row: Dictionary = sim.entities[id]
	# The carrier is a real damageable simulation entity, but its movement and
	# presentation are owned by its horde; it never runs independent orders/AI.
	if bool(row.get("is_banner_carrier", false)):
		return
	if int(row["health"]) <= 0:
		row["state"] = "death"
		sim._clear_pending_route(row, true)
		sim._rearm_mood_idle_cadence(row)
		return
	if row.has("grab_passenger_channel") or row.has("fling_passenger_channel") or row.has("repair_structure_channel"):
		row["current_speed"] = 0.0
		row["state"] = "ability"
		sim._rearm_mood_idle_cadence(row)
		return
	if not (row.get("dominate_enemy_channel", {}) as Dictionary).is_empty():
		# DominateEnemySpecialPower owns locomotion for unpack/preparation and
		# the authored post-trigger AI freeze. The ability step resolves its
		# interrupt/trigger/finish boundary deterministically after this pass.
		row["current_speed"] = 0.0
		row["state"] = "ability"
		sim._rearm_mood_idle_cadence(row)
		return
	if sim.tick_index < int(row.get("cower_until_tick", -1)):
		row["current_speed"] = 0.0
		row["state"] = "cower"
		sim._rearm_mood_idle_cadence(row)
		return
	elif row.has("cower_until_tick"):
		row.erase("cower_until_tick")
		row["state"] = "idle"
	if sim.entity_container.has(id):
		var horde_ai := row.get("horde_ai_update", {}) as Dictionary
		if horde_ai.is_empty():
			sim._attach_module_contracts(row)
			horde_ai = row.get("horde_ai_update", {}) as Dictionary
		# Contained hordes are position-owned by the carrier. They may execute
		# weapon logic only when retail authors the opt-in; otherwise they are
		# inert. Route clearing prevents independent movement in either case.
		sim._clear_pending_route(row, true)
		row["attack_move"] = false
		row["current_speed"] = 0.0
		var carrier_id = int(sim.entity_container.get(id, 0))
		if sim.structures.has(carrier_id):
			row["position"] = Vector2((sim.structures[carrier_id] as Dictionary).get("position", row.get("position", Vector2.ZERO)))
		if not bool(row.get("contained_can_attack", false)) and not bool(horde_ai.get("can_attack_while_contained", false)):
			row["state"] = "contained"
			return
	if sim.tick_index < int(row.get("stun_until_tick", -1)):
		# Cloud Break disruption: the battalion holds position and cannot act
		# until the authored weather duration elapses.
		row["current_speed"] = 0.0
		sim._rearm_mood_idle_cadence(row)
		return
	var knockdown_ticks := int(row.get("knockdown_ticks", 0))
	if knockdown_ticks > 0:
		# Knocked-down battalions lie incapacitated: no movement, attacks, or
		# ability steps until the counter drains, then they stand back up.
		# Being bowled over interrupts a capture channel outright.
		if not (row.get("capture_channel", {}) as Dictionary).is_empty():
			var channel: Dictionary = row.get("capture_channel", {}) as Dictionary
			row.erase("capture_channel")
			sim._emit_event("structure.capture_cancelled", id, int(channel.get("structure_id", 0)), {"team": int(row.get("team", -1))})
		knockdown_ticks -= 1
		row["knockdown_ticks"] = knockdown_ticks
		row["current_speed"] = 0.0
		if knockdown_ticks <= 0:
			row["knocked_down"] = false
			sim._emit_event("combat.stand_up", id, 0)
			# Standing back up resumes the order the charge interrupted, from
			# the spot the battalion was thrown to. Without this a trampled
			# battalion stands up ownerless and idle, so the player's attack
			# order is destroyed by the first hoof that touches it.
			if not sim._resume_order_after_knockdown(row):
				row["state"] = "idle"
		else:
			row["state"] = "knocked_down"
		sim._rearm_mood_idle_cadence(row)
		return
	row["attack_cooldown"] = maxi(0, int(row["attack_cooldown"]) - 1)
	if not (row.get("activate_module_channel", {}) as Dictionary).is_empty():
		# ActivateModuleSpecialPower owns locomotion through its authored
		# unpack/prep/duration/pack envelope. A voluntary order is observed and
		# resolved by _step_activate_module_graph after this entity pass; a
		# MustFinish graph keeps the order queued and resumes it after finish.
		row["current_speed"] = 0.0
		row["state"] = "ability"
		sim._rearm_mood_idle_cadence(row)
		return
	if not (row.get("siege_deploy_channel", {}) as Dictionary).is_empty():
		row["current_speed"] = 0.0
		row["state"] = "deployed" if String((row.get("siege_deploy_channel", {}) as Dictionary).get("phase", "")) == "deployed" else "ability"
		sim._rearm_mood_idle_cadence(row)
		return
	if sim._step_capture_channel(row):
		sim._rearm_mood_idle_cadence(row)
		return
	if sim._step_volley_channel(row):
		sim._rearm_mood_idle_cadence(row)
		return
	if sim.tick_index < int(row.get("ability_hold_until_tick", -1)):
		# Authored post-ability busy envelope (teleport BusyForDuration): the
		# hero holds this tick; accepted orders resume once the hold expires.
		row["current_speed"] = 0.0
		row["state"] = "idle"
		sim._rearm_mood_idle_cadence(row)
		return
	if sim._step_production_exit(row):
		sim._rearm_mood_idle_cadence(row)
		return
	if bool(row.get("is_builder", false)):
		sim._rearm_mood_idle_cadence(row)
		var construction_id := int(row.get("construction_id", 0))
		if construction_id != 0 and sim.structures.has(construction_id):
			var site: Dictionary = sim.structures[construction_id]
			if float(site.get("construction_progress", 1.0)) < 1.0:
				var site_position := Vector2(site.get("position", row["position"]))
				# A successful navigation query may end at the closest walkable
				# perimeter cell rather than the structure's obstructed center.
				# Exhausting that accepted route is arrival; requiring another
				# invented center-distance strands real-map construction sites.
				# Arrival is the FOOTPRINT EDGE, not a flat 2.0 from the centre: a
				# route that ends at the centre (no nav-grid obstruction for the
				# fresh site) leaves the porter pressed against the site's own
				# collision disc - placement radius plus its body - and a
				# centre-distance rule strands it there forever with the order
				# still armed (Angmar porter, second fortress: stuck 5.2 out).
				# Retail porters build from the foundation's edge.
				var footprint = sim._structure_placement_radius(String(site.get("structure_kind", "")))
				var arrival_reach := maxf(2.0, footprint + 2.0)
				if String(row.get("order_kind", "")) == "construct" and ((row["route"] as Array).is_empty() or Vector2(row["position"]).distance_to(site_position) <= arrival_reach):
					sim._clear_pending_route(row, true)
					row["state"] = "construct"
					return
		if not (row["route"] as Array).is_empty():
			sim._step_route(row)
		else:
			row["state"] = "idle"
		return
	var target_id := int(row["target_id"])
	if (
		target_id != 0
		or not (row["route"] as Array).is_empty()
		or bool(row.get("attack_move", false))
	):
		sim._rearm_mood_idle_cadence(row)
	if target_id == 0 and (row["route"] as Array).is_empty() and not bool(row.get("attack_move", false)):
		var scan_now := true
		var mood_rate := int(row.get("mood_attack_check_rate_ticks", 0))
		if mood_rate > 0:
			var policy_allows_idle_scan = (
				bool(row.get("auto_acquire_enabled", true))
				and (
					not sim._stealth_active(row)
					or bool(row.get("auto_acquire_while_stealthed", true))
				)
			)
			scan_now = (
				policy_allows_idle_scan
				and sim.tick_index >= int(row.get("mood_next_check_tick", -1))
			)
		if scan_now:
			var auto_target = sim._nearest_auto_target(row)
			if mood_rate > 0:
				var interval := mood_rate
				if bool(row.get("mood_randomize_next_check", true)):
					var half_rate := mood_rate >> 1
					interval += sim.logic_random_int(-half_rate, half_rate)
					row["mood_randomize_next_check"] = false
				row["mood_next_check_tick"] = sim.tick_index + maxi(1, interval)
			if not auto_target.is_empty():
				target_id = int(auto_target["id"])
				row["target_id"] = target_id
				row["target_kind"] = String(auto_target["kind"])
				row["order_kind"] = "auto_attack"
				row["auto_attack_origin"] = row.get("position", Vector2.ZERO)
	if target_id == 0 and bool(row.get("attack_move", false)) and not (row["route"] as Array).is_empty():
		var acquired = sim._nearest_attack_move_target(row)
		if acquired != 0:
			row["target_id"] = acquired
			row["target_kind"] = "battalion"
			target_id = acquired
	if target_id != 0:
		var target_kind := String(row.get("target_kind", "battalion"))
		if String(row.get("order_kind", "")) == "auto_attack":
			var ai_policy := row.get("ai_update_interface", {}) as Dictionary
			var stop_source := float(ai_policy.get("stop_chase_distance_source", 0.0))
			var source_scale = float(sim._rules.get("source_map_transform_scale", 1.0))
			var stop_distance = stop_source * (source_scale if source_scale > 0.0 else 1.0)
			if stop_distance > 0.0 and Vector2(row.get("position", Vector2.ZERO)).distance_to(Vector2(row.get("auto_attack_origin", row.get("position", Vector2.ZERO)))) > stop_distance:
				row["target_id"] = 0
				row["target_kind"] = "battalion"
				row["order_kind"] = ""
				row.erase("auto_attack_origin")
				sim._clear_pending_route(row, true)
				row["state"] = "idle"
				return
		if not sim._target_alive(target_id, target_kind):
			row["target_id"] = 0
			row["target_kind"] = "battalion"
			row["attack_windup"] = 0
			sim._clear_member_attack_schedule(row)
			sim._clear_member_targets(row)
			if bool(row.get("attack_move", false)) and sim._assign_route(row, Vector2(row.get("attack_move_destination", row["position"]))):
				row["state"] = "run"
			else:
				sim._clear_pending_route(row, true)
				row["state"] = "idle"
			return
		var auto_target_invalid = (
			String(row.get("order_kind", "")) == "auto_attack"
			and (
				not bool(row.get("auto_acquire_enabled", true))
				or (
					sim._stealth_active(row)
					and not bool(row.get("auto_acquire_while_stealthed", true))
				)
				or (
					target_kind == "battalion"
					and sim._stealth_active(sim.entities[target_id] as Dictionary)
				)
				or (
					target_kind == "structure"
					and not bool(row.get("auto_acquire_attack_buildings", true))
				)
			)
		)
		if auto_target_invalid:
			# Re-evaluate an AUTO target for temporal policy changes (source or
			# target cloaking) without touching explicit attack/retaliation
			# orders. A later tick may reacquire after the condition clears.
			row["target_id"] = 0
			row["target_kind"] = "battalion"
			row["attack_windup"] = 0
			sim._clear_member_attack_schedule(row)
			sim._clear_member_targets(row)
			sim._clear_pending_route(row, true)
			row["state"] = "idle"
			return
		if target_kind == "battalion" and not sim._can_engage_battalion(row, sim.entities[target_id] as Dictionary):
			# Safety net for targets acquired through paths other than the
			# guarded acquisition funnels (e.g. retaliation): melee cannot
			# chase an airborne battalion it can never reach.
			row["target_id"] = 0
			row["target_kind"] = "battalion"
			row["attack_windup"] = 0
			sim._clear_member_attack_schedule(row)
			sim._clear_member_targets(row)
			sim._clear_pending_route(row, true)
			row["state"] = "idle"
			return
		var target_position = sim._target_position(target_id, target_kind)
		# SURFACE-TO-SURFACE against a structure; centre-to-centre otherwise.
		#
		# This used to be centre-to-centre unconditionally, copied from a partial
		# open-source reimplementation that compares raw translation distance
		# against AttackRange with no bounding-radius expansion. The original
		# engine instead asks its partition manager for a bounding-sphere-to-
		# bounding-sphere distance, i.e. the GAP between the two footprints.
		#
		# MEASURED consequence of the simplification, in the live slice: a melee
		# horde ordered onto the enemy fortress ended in state `attack` at d=0.24
		# and then d=0.00 — standing ON the fortress centre — because a 0.305
		# AttackRange against a 1.96-radius footprint is only satisfiable at the
		# centre. Melee is supposed to stand at the wall and swing. Bracketed
		# from both sides by _test_structure_surface_range in
		# game/tests/banner_castle_sim_runner.gd.
		#
		# See sim._target_footprint_radius for why the attacker's own radius is NOT
		# subtracted (it is a horde centre, not a soldier).
		var distance := maxf(
			0.0,
			Vector2(row["position"]).distance_to(target_position)
			- sim._target_footprint_radius(target_id, target_kind)
		)
		var selected_weapon_mode = sim._weapon_mode_for_distance(row, distance)
		if selected_weapon_mode == "unsupported-close":
			row["state"] = "idle"
			row["attack_windup"] = 0
			sim._clear_pending_route(row, true)
			sim._clear_member_attack_schedule(row)
			return
		sim._apply_weapon_mode(row, selected_weapon_mode)
		var minimum_range := float(row.get("minimum_attack_range", 0.0))
		if distance <= float(row["attack_range"]) and (minimum_range <= 0.0 or distance >= minimum_range):
			row["state"] = "attack"
			sim._clear_pending_route(row, true)
			sim._step_member_attacks(id, row, target_id, target_kind)
			return
		row["attack_windup"] = 0
		sim._clear_member_attack_schedule(row)
		if (
			String(row.get("stance", "Battle")) == "HoldGround"
			and String(row.get("order_kind", "")) in ["", "auto_attack"]
		):
			# HoldGround never leaves its post to chase: an auto-acquired or
			# retaliation target that steps out of weapon range is dropped
			# instead of pursued. Explicit player attack orders still pursue.
			row["target_id"] = 0
			row["target_kind"] = "battalion"
			sim._clear_pending_route(row, true)
			row["state"] = "idle"
			return
		if minimum_range > 0.0 and distance < minimum_range:
			if sim.entity_container.has(id):
				row["state"] = "contained"
				return
			# Inside minimum range: back away to re-establish the firing ring.
			# Routing toward the target here walked min-range units (trebuchets)
			# into their attacker and jammed them permanently.
			var away = Vector2(row["position"]) - target_position
			var direction = away / away.length() if away.length() > 0.001 else Vector2.RIGHT
			var fallback = Vector2(row["position"]) + direction * (minimum_range - distance + 1.0)
			sim._clear_pending_route(row, true)
			if not sim._assign_route(row, fallback):
				row["state"] = "idle"
				return
			row["state"] = "run"
			sim._step_route(row)
			return
		if (row["route"] as Array).is_empty():
			if sim.entity_container.has(id):
				row["state"] = "contained"
				return
			if not sim._assign_target_route(row, target_position):
				row["target_id"] = 0
				sim._clear_pending_route(row, true)
				row["state"] = "idle"
				return
		row["state"] = "run"
		sim._step_route(row)
		return
	if not (row["route"] as Array).is_empty():
		row["state"] = "run"
		sim._step_route(row)
	else:
		row["attack_move"] = false
		row["state"] = "idle"


func set_structure_rally(team: int, structure_id: int, position: Vector2) -> Dictionary:
	if not sim.structures.has(structure_id):
		return {"ok": false, "reason": "unknown-structure"}
	var row: Dictionary = sim.structures[structure_id]
	if int(row.get("team", -1)) != team:
		return {"ok": false, "reason": "not-owned"}
	if int(row.get("health", 0)) <= 0:
		return {"ok": false, "reason": "structure-destroyed"}
	if sim.playable_outline.size() >= 3 and not Geometry2D.is_point_in_polygon(position, sim.playable_outline):
		return {"ok": false, "reason": "outside-playable-area"}
	row["rally"] = position
	sim._emit_event("structure.rally_set", 0, structure_id, {"team": team})
	return {"ok": true}


