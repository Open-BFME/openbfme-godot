extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Movement/locomotion subsystem extracted from retail_slice_sim.gd (Q81
## strangler-fig extraction #8): authored heading-bounded stepping, route
## assignment/query, arrival bookkeeping. Verbatim move, compiler-guided
## sim. prefixes, pin-verified byte-identical.





func _retail_reform_threshold_degrees(row: Dictionary) -> float:
	## Retail authors MaxTurnWithoutReform on 12 templates only: 45 on six
	## (NormalMeleeHordeLocomotor locomotor.ini:774, NormalChargeMeleeHorde :820,
	## ScaredMeleeHorde :841, NormalRangedHorde :862, NormalAmphibiousRangedHorde
	## :882, AODHorde :2199, TestWallScalingHorde :3075, WallScalingMeleeHorde
	## :3098), 55 on SlowMeleeHordeLocomotor :795, and 100 on the three cavalry
	## horde locomotors (NormalCavalryHorde :899, NormalSpiderlingHorde :921,
	## WargCavalryHorde :943). The importer compiles all of them, so an authored
	## value is always available where retail has one. Everywhere else there is
	## no reform gate — -1.0 — and no category-keyed guess.
	return float(row.get("max_turn_without_reform_degrees", -1.0))


func _retail_turn_rate_degrees(row: Dictionary) -> float:
	var authored := float(row.get("turn_rate_degrees_per_second", 0.0))
	if authored > 0.0:
		return authored
	push_error(
		"unauthored locomotor turn rate for %s"
		% String(row.get("horde_id", row.get("source_object_id", "<unknown>")))
	)
	return 0.0


func _step_retail_heading(
	row: Dictionary,
	movement_direction: Vector2,
	braking: float,
	effective_turn_rate_degrees_per_second: float
) -> bool:
	## Rotate the horde's facing toward travel at the authored TurnTime-derived
	## cap (possibly reduced by the selected authored radius), and answer whether
	## the horde is REFORMING.
	##
	## The authored locomotor field MaxTurnWithoutReform splits turning in two.
	##
	## Inside the arc the horde WHEELS - it keeps advancing while it turns. That
	## is why every member class is authored a slightly faster MEMBER speed than
	## its HORDE speed: members on the outside of a wheel have further to travel
	## and must catch up.
	##
	## Beyond the arc it REFORMS - the horde stops, pivots about its own centre,
	## and members re-take their slots against the new heading. Melee horde
	## locomotors also author no turning while moving, so a reform is stationary
	## by construction.
	##
	## Deterministic: fixed sim.TICK_SECONDS step, no wall clock, no RNG draw.
	var facing_now := Vector2(row.get("facing", movement_direction))
	if facing_now.length_squared() <= 0.000001:
		facing_now = movement_direction
	var delta_angle := wrapf(movement_direction.angle() - facing_now.angle(), -PI, PI)
	var turn_step = deg_to_rad(effective_turn_rate_degrees_per_second) * sim.TICK_SECONDS
	row["facing"] = facing_now.rotated(clampf(delta_angle, -turn_step, turn_step))
	var reform_threshold := _retail_reform_threshold_degrees(row)
	if reform_threshold < 0.0:
		# No authored MaxTurnWithoutReform on the bound template (116 of retail's
		# 128 author none). Retail has no reform gate there at all, so the horde
		# always wheels: it turns AND keeps advancing, however sharp the turn.
		# The old code guessed 45 (100 for cavalry) here.
		return false
	if absf(delta_angle) <= deg_to_rad(reform_threshold):
		return false
	# Reform: bleed speed off the authored braking ramp rather than snapping to a
	# standing start, and hold position while the pivot completes.
	row["current_speed"] = maxf(0.0, float(row.get("current_speed", 0.0)) - braking * sim.TICK_SECONDS)
	row["route_stall_ticks"] = 0
	return true


func _step_route(row: Dictionary) -> void:
	var route: Array = row["route"]
	if route.is_empty():
		# Brake toward stop when no route remains.
		var idle_speed := maxf(0.0, float(row.get("current_speed", 0.0)) - float(row.get("braking", float(row.get("speed", 0.0)))) * sim.TICK_SECONDS)
		row["current_speed"] = idle_speed
		return
	var position := Vector2(row["position"])
	var tick_start_position := position
	var waypoint := Vector2(route[0])
	var base_speed := float(row["speed"])
	var group_cap := float(row.get("group_speed_cap", 0.0))
	if group_cap > 0.0 and (sim.retail_formation_movement or bool(row.get("wait_for_formation", false))):
		# WaitForFormation (locomotor.ini:713) - a group order advances at the
		# slowest authored speed in the group so the selection arrives together
		# instead of stringing out by unit class.
		base_speed = minf(base_speed, group_cap)
	var max_speed = base_speed * float(sim._stance_state(row).get("speedMultiplier", 1.0)) * float(sim._formation_effects(row).get("speed_multiplier", 1.0)) * sim._ability_speed_multiplier(row) * float(row.get("siege_speed_multiplier", 1.0))
	# Acceleration and Braking are authored on the Locomotor template every
	# object's LocomotorSet binds (HumanLocomotor locomotor.ini:142 authors
	# 510/510; HorseLocomotor :1026 authors 1500/2000). All 494 movement blocks
	# in the selected packs carry both, so an absence is a real gap: say so and
	# leave the unit where it is rather than inventing a ramp.
	var acceleration := float(row.get("acceleration", 0.0))
	if acceleration <= 0.0:
		push_error(
			"unauthored locomotor acceleration for %s"
			% String(row.get("horde_id", row.get("source_object_id", "<unknown>")))
		)
		return
	var braking := float(row.get("braking", 0.0))
	if braking <= 0.0:
		push_error(
			"unauthored locomotor braking for %s"
			% String(row.get("horde_id", row.get("source_object_id", "<unknown>")))
		)
		return
	var current_speed := float(row.get("current_speed", 0.0))
	# Accelerate toward max; near the final waypoint begin braking for snappier
	# stops. Braking only ever applies on the last leg, so summing the whole
	# remaining route was wasted per-tick work.
	var stop_distance := (current_speed * current_speed) / maxf(0.001, 2.0 * braking)
	if route.size() <= 1 and position.distance_to(waypoint) <= stop_distance:
		current_speed = maxf(0.0, current_speed - braking * sim.TICK_SECONDS)
	else:
		current_speed = minf(max_speed, current_speed + acceleration * sim.TICK_SECONDS)
	row["current_speed"] = current_speed
	var step_distance = current_speed * sim.TICK_SECONDS
	var movement_direction := position.direction_to(waypoint)
	var pre_move_gap := position.distance_to(waypoint)
	var heading_bounded_ground_move := false
	var turning_on_authored_heading := false
	var slow_turn_manoeuvre := false
	var pre_q55_slow_clamp := false
	var selected_turn_radius := -1.0
	var effective_turn_rate_degrees := 0.0
	var minimum_turn_speed := 0.0
	var min_turn_speed_fraction := 0.0
	if movement_direction.length_squared() > 0.000001:
		if sim._should_honor_turn_rate(row):
			var facing_before_turn := Vector2(row.get("facing", movement_direction)).normalized()
			turning_on_authored_heading = absf(
				wrapf(movement_direction.angle() - facing_before_turn.angle(), -PI, PI)
			) > 0.0001
			effective_turn_rate_degrees = _retail_turn_rate_degrees(row)
			if turning_on_authored_heading:
				if row.has("fast_turn_radius"):
					var authored_fast_radius := maxf(0.0, float(row["fast_turn_radius"]))
					if authored_fast_radius > 0.0 and pre_move_gap <= authored_fast_radius + 0.0001:
						# A waypoint inside the moving circle cannot be reached by
						# continuing that circle. Brake on the authored ramp until the
						# MinTurnSpeed split selects SlowTurnRadius, pivot, then advance.
						current_speed = maxf(0.0, current_speed - braking * sim.TICK_SECONDS)
						row["current_speed"] = current_speed
				if row.has("min_turn_speed"):
					min_turn_speed_fraction = clampf(float(row["min_turn_speed"]), 0.0, 1.0)
					minimum_turn_speed = max_speed * min_turn_speed_fraction
					if row.has("fast_turn_radius") and min_turn_speed_fraction >= 1.0 - 0.0001:
						# NormalCavalryHordeLocomotor says SlowTurnRadius=0 is for
						# "a standing start" (locomotor.ini:843), while its authored
						# Acceleration=800 produces 8 sim units/s after the first 0.1s
						# tick. Only speeds genuinely below that authored first ramp
						# output are slow; accelerating sub-top-speed ticks are fast.
						var first_accelerating_speed := minf(
							max_speed, acceleration * sim.TICK_SECONDS
						)
						slow_turn_manoeuvre = (
							current_speed + 0.0001 < first_accelerating_speed
						)
					else:
						slow_turn_manoeuvre = current_speed <= minimum_turn_speed + 0.0001
				if row.has("fast_turn_radius"):
					if slow_turn_manoeuvre and row.has("slow_turn_radius"):
						selected_turn_radius = maxf(0.0, float(row["slow_turn_radius"]))
					else:
						selected_turn_radius = maxf(0.0, float(row["fast_turn_radius"]))
				if selected_turn_radius > 0.0 and current_speed > 0.0:
					# v = r*w. A radius wider than the natural authored-rate arc
					# therefore lowers w to v/r; a tighter radius keeps the authored
					# rate and the translation bound below shortens the arc instead.
					effective_turn_rate_degrees = minf(
						effective_turn_rate_degrees,
						rad_to_deg(current_speed / selected_turn_radius)
					)
			var reforming := _step_retail_heading(
				row, movement_direction, braking, effective_turn_rate_degrees
			)
			if reforming and sim._should_reform(row):
				# Reforming: the horde pivots about its own centre and does not
				# translate this tick. Only when MaxTurnWithoutReform is on the
				# row (or the old formation flag). Otherwise wheel: turn and
				# still walk — no invented 45° stop.
				return
			heading_bounded_ground_move = (
				not bool(row.get("flying", false))
				and String(row.get("pathing_layer", "ground")) == "ground"
			)
		else:
			row["facing"] = movement_direction
	if turning_on_authored_heading and row.has("min_turn_speed"):
		# Locomotor MinTurnSpeed is an authored fraction of top speed. Braking
		# toward a close waypoint may not drop a turning unit below that forward
		# floor; doing so alternated full-speed and zero-speed ticks and produced
		# an infinite orbit around lateral final waypoints.
		var accelerating_full_threshold := (
			row.has("fast_turn_radius") and min_turn_speed_fraction >= 1.0 - 0.0001
		)
		if not accelerating_full_threshold:
			current_speed = maxf(current_speed, minimum_turn_speed)
			var turn_radians_per_second := deg_to_rad(_retail_turn_rate_degrees(row))
			var current_turn_radius := current_speed / maxf(turn_radians_per_second, 0.0001)
			if pre_move_gap <= current_turn_radius + 0.0001:
				# The waypoint lies inside the current arc: drop to the authored
				# minimum turning speed so SlowTurnRadius owns the close maneuver.
				current_speed = minimum_turn_speed
		pre_q55_slow_clamp = current_speed <= minimum_turn_speed + 0.0001
		row["current_speed"] = current_speed
	step_distance = current_speed * sim.TICK_SECONDS
	var travel_step := Vector2.ZERO
	var travel_direction := movement_direction
	if heading_bounded_ground_move:
		travel_direction = Vector2(row.get("facing", movement_direction)).normalized()
		if turning_on_authored_heading:
			var effective_turn_step = deg_to_rad(effective_turn_rate_degrees) * sim.TICK_SECONDS
			if selected_turn_radius >= 0.0:
				# The selected authored radius binds the position arc as s <= r*dtheta.
				# Together with the v/r heading cap above this traces r whether r is
				# wider or tighter than the natural speed/authored-rate arc.
				step_distance = minf(
					step_distance, selected_turn_radius * effective_turn_step
				)
			elif pre_q55_slow_clamp and row.has("slow_turn_radius"):
				# A document with no authored FastTurnRadius stays on the exact old
				# clamp. This absence path is byte-pinned in retail_turn_model_runner.
				var fallback_slow_turn_radius := maxf(0.0, float(row["slow_turn_radius"]))
				step_distance = minf(
					step_distance, fallback_slow_turn_radius * effective_turn_step
				)
	# A heading-bounded unit may initially face away from a nearby waypoint. It
	# must complete the authored turn instead of teleporting sideways onto the
	# point merely because its scalar step is longer than the remaining gap.
	var facing_toward_waypoint := travel_direction.dot(movement_direction) > 0.0
	if pre_move_gap <= maxf(step_distance, 0.001) and facing_toward_waypoint:
		travel_step = waypoint - position
		position = waypoint
		route.pop_front()
		sim._consume_route_point_layer(row)
	else:
		travel_step = travel_direction * step_distance
		var candidate_position := position + travel_step
		if (
			heading_bounded_ground_move
			and not travel_direction.is_equal_approx(movement_direction)
			and not sim._position_walkable(candidate_position)
		):
			# Turning may point briefly outside the routed navigation corridor.
			# Hold the last walkable point and shed speed through the locomotor's
			# authored Braking value; never slide sideways or invent a turn-speed
			# scalar. The next tick continues rotating toward the waypoint. Once the
			# heading equals the routed bearing, the route provider owns the segment:
			# this matters for accepted final legs to obstructed structure centres.
			travel_step = Vector2.ZERO
			current_speed = maxf(0.0, current_speed - braking * sim.TICK_SECONDS)
			row["current_speed"] = current_speed
		else:
			position = candidate_position
	if not bool(row.get("flying", false)) and String(row.get("pathing_layer", "ground")) == "ground":
		# Flyers pass straight over building footprints. The step that produced
		# this position is threaded in so a footprint the unit is walking THROUGH
		# slides it tangentially around the disc rather than standing it off
		# radially — see _tangential_slide_point for the deadlock that fixes.
		position = sim._deflect_around_structures(position, row, travel_step)
	# Grid routes ignore structure footprints, so a waypoint can sit inside a
	# blocked disc; deflection then pins the unit on the ring making zero
	# progress. Only a sustained stall pops the waypoint — a single flat tick
	# is normal while sliding tangentially around a footprint.
	if not route.is_empty() and Vector2(route[0]) == waypoint and step_distance > 0.001 \
			and position.distance_to(waypoint) >= pre_move_gap - 0.001:
		var stall_ticks := int(row.get("route_stall_ticks", 0)) + 1
		row["route_stall_ticks"] = stall_ticks
		if stall_ticks >= 3:
			row["route_stall_ticks"] = 0
			# Never drop the last waypoint — that is the order destination. A
			# 1-point LOS route through a building would otherwise abandon the
			# click after three stalled ticks on the ring.
			if route.size() > 1:
				route.pop_front()
				sim._consume_route_point_layer(row)
	else:
		row["route_stall_ticks"] = 0
	row["position"] = position
	sim._spatial_sync(row)
	row["route"] = route
	# Authored crush, or the legacy cavalry trample when crush fields are absent.
	# Eligibility is the actual displacement this tick. The locomotor speed can
	# contain the MinTurnSpeed floor during a zero-distance slow pivot and must
	# not manufacture a standing-still crush pulse.
	var actual_translation_speed = tick_start_position.distance_to(position) / sim.TICK_SECONDS
	if sim._should_attempt_crush(row, actual_translation_speed, max_speed):
		sim._try_cavalry_trample(row)
	if route.is_empty():
		_clear_pending_route(row, int(row["target_id"]) == 0)
		if int(row["target_id"]) == 0:
			row["state"] = "idle"


func _build_route(from: Vector2, to: Vector2) -> Array[Vector2]:
	var result := _query_route(from, to)
	if not bool(result.get("valid", false)):
		return []
	var points: Array[Vector2] = []
	points.assign(result.get("points", []))
	return points


func _assign_route(row: Dictionary, destination: Vector2) -> bool:
	if row.has("toggle_deploy_channel") or bool(row.get("toggle_deployed", false)):
		sim.last_route_rejection = "toggle-deploy-immobile"
		return false
	if bool(row.get("flying", false)):
		# Flyers ignore ground navigation entirely: straight-line route over
		# water, void, and structures — no walkability query, no ford logic.
		var direct: Array[Vector2] = []
		direct.append(destination)
		var no_cells: Array[Vector2i] = []
		row["destination"] = destination
		row["route"] = direct
		row["route_cells"] = no_cells
		row["route_ford"] = ""
		return true
	var uses_walk_surface := _row_route_uses_walk_surface(row, destination)
	# Parity path ledger: refuse ground routes that cross impassable cells. The
	# ledger is a ground-domain raster and must not veto an authored deck route;
	# layered routes are still checked by the map-owned wall grid below.
	# Ships use the water grid; the land ledger would reject every river.
	if not _is_naval_row(row) and not uses_walk_surface:
		sim._ensure_parity()
		# A heading-bounded arc can finish just inside the navigation raster's
		# blocked edge. Only a provider that explicitly resolves that origin may
		# recover it, and the recovered point still goes through the sim.parity ledger.
		var parity_origin := Vector2(row["position"])
		if not sim._position_walkable(parity_origin):
			if sim.route_provider == null or not sim.route_provider.has_method("resolve_walkable_position"):
				sim.last_route_rejection = "route-origin-not-walkable"
				return false
			parity_origin = Vector2(sim.route_provider.call("resolve_walkable_position", parity_origin))
			if not sim._position_walkable(parity_origin):
				sim.last_route_rejection = "route-origin-recovery-failed"
				return false
		if not sim.parity.can_path_between(parity_origin, destination):
			sim.last_route_rejection = "parity-path-impassable"
			return false
	var result := _query_route_for_row(row, Vector2(row["position"]), destination)
	if not bool(result.get("valid", false)):
		sim.last_route_rejection = String(result.get("reason", "route-rejected"))
		return false
	var points: Array[Vector2] = []
	points.assign(result.get("points", []))
	if points.is_empty():
		sim.last_route_rejection = String(result.get("reason", ""))
		if sim.last_route_rejection.is_empty():
			sim.last_route_rejection = "empty-route"
		return false
	var cells: Array[Vector2i] = []
	cells.assign(result.get("cells", []))
	var layered := bool(result.get("uses_walk_surface", false))
	var point_layers: Array = result.get("point_layers", []) as Array
	var point_elevations: Array = result.get("point_elevations", []) as Array
	if layered and (point_layers.size() != points.size() or point_elevations.size() != points.size()):
		sim.last_route_rejection = "invalid-layered-route"
		return false
	row["destination"] = destination
	row["route"] = points
	row["route_cells"] = cells
	row["route_ford"] = String(result.get("ford_name", ""))
	if layered:
		row["route_point_layers"] = point_layers.duplicate()
		row["route_point_elevations"] = point_elevations.duplicate()
		row["route_surface_roles"] = (result.get("surface_roles", []) as Array).duplicate()
	else:
		row.erase("route_point_layers")
		row.erase("route_point_elevations")
		row.erase("route_surface_roles")
	return not points.is_empty()


func _assign_target_route(row: Dictionary, target_position: Vector2) -> bool:
	## Combat follows the target's live position for range and damage, but a
	## battalion can legitimately stand on a raster cell that ground navigation
	## marks blocked (footprint eviction, knockback, or a heading-bounded arc at a
	## cell edge). RetailMapData exposes its deterministic nearest-walkable
	## resolver for exactly this map-owned question. Movement routes to that
	## resolved approach point; the target id/position remains unchanged.
	if _assign_route(row, target_position):
		return true
	if (
		sim.last_route_rejection != "blocked-destination"
		or sim.route_provider == null
		or not sim.route_provider.has_method("resolve_walkable_position")
	):
		return false
	var approach := Vector2(sim.route_provider.call("resolve_walkable_position", target_position))
	if approach.is_equal_approx(target_position):
		return false
	var unit_type := String(row.get("unit_type", row.get("source_object_id", "<unknown>")))
	if not sim._target_route_resolution_unit_types.has(unit_type):
		sim._target_route_resolution_unit_types[unit_type] = true
		print(
			"RETAIL_TURN_MODEL target_route_walkable_resolution unit_type=%s"
			% unit_type
		)
	return _assign_route(row, approach)


func _query_route(from: Vector2, to: Vector2) -> Dictionary:
	if sim.route_provider != null and sim.route_provider.has_method("query_route"):
		var value: Variant = sim.route_provider.call("query_route", from, to)
		if typeof(value) == TYPE_DICTIONARY:
			var ground := value as Dictionary
			if bool(ground.get("valid", false)) or not sim.route_provider.has_method("query_layered_bridge_route"):
				return ground
			var bridge_value: Variant = sim.route_provider.call("query_layered_bridge_route", from, to)
			if typeof(bridge_value) == TYPE_DICTIONARY and bool((bridge_value as Dictionary).get("valid", false)):
				return bridge_value as Dictionary
			return ground
	# The non-retail fallback remains bounded and direct. The selected retail
	# slice cannot reach this branch because configuration requires a provider.
	return {"valid": true, "reason": "", "points": [to], "cells": [], "ford_name": ""}


func _query_route_for_row(row: Dictionary, from: Vector2, to: Vector2) -> Dictionary:
	if _is_naval_row(row):
		# Water is the only domain a hull has. A provider that cannot answer for
		# water leaves the ship with no route at all — the land grid is not a
		# substitute, and neither is _query_route's direct fallback, which would
		# hand back a confident straight line across dry ground.
		if sim.route_provider == null or not sim.route_provider.has_method("query_water_route"):
			return {"valid": false, "reason": "water-navigation-unavailable", "points": [], "cells": []}
		var water_value: Variant = sim.route_provider.call("query_water_route", from, to)
		if typeof(water_value) == TYPE_DICTIONARY:
			return water_value as Dictionary
		return {"valid": false, "reason": "water-navigation-unavailable", "points": [], "cells": []}
	if _row_route_uses_walk_surface(row, to):
		if sim.route_provider == null or not sim.route_provider.has_method("query_layered_route"):
			return {"valid": false, "reason": "walk-surface-navigation-unavailable", "points": [], "cells": []}
		var layered_value: Variant = sim.route_provider.call(
			"query_layered_route", from, to, String(row.get("pathing_layer", "ground"))
		)
		if typeof(layered_value) == TYPE_DICTIONARY:
			return layered_value as Dictionary
		return {"valid": false, "reason": "walk-surface-navigation-unavailable", "points": [], "cells": []}
	return _query_route(from, to)


func _row_route_uses_walk_surface(row: Dictionary, destination: Vector2) -> bool:
	if _is_naval_row(row) or sim.route_provider == null:
		return false
	if String(row.get("pathing_layer", "ground")) in ["ramp", "deck"]:
		return true
	return (
		sim.route_provider.has_method("is_walk_surface_at")
		and bool(sim.route_provider.call("is_walk_surface_at", destination))
	)


func _is_naval_row(row: Dictionary) -> bool:
	var category := String(row.get("category", "")).strip_edges().to_lower()
	if category == "naval":
		return true
	var kinds: Array = row.get("kind_of", []) as Array
	for kind_value in kinds:
		var kind := String(kind_value).to_upper()
		if kind == "SHIP" or kind == "NAVAL_UNIT":
			return true
	return false


func _clear_pending_route(row: Dictionary, settle_destination: bool) -> void:
	if row.has("group_speed_cap") and (sim.retail_formation_movement or bool(row.get("wait_for_formation", false))):
		# The group cohesion cap belongs to one order, not to the unit.
		row["group_speed_cap"] = 0.0
	row["route"] = []
	row["route_cells"] = []
	row["route_ford"] = ""
	if row.has("route_point_layers"):
		row["route_point_layers"] = []
	if row.has("route_point_elevations"):
		row["route_point_elevations"] = []
	if settle_destination:
		row["destination"] = Vector2(row["position"])
