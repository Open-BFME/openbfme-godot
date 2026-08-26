extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Skirmish-AI controller subsystem extracted from retail_slice_sim.gd (Q81
## strangler-fig extraction #4). Pure code move, extracted AS-IS including
## its known invented-behavior debt (hand-written build order, permille
## difficulty handicaps) — queue Q83 Phase 2 replaces the guts with retail's
## authored skirmishaidata; landing that into this dedicated file is the
## point of moving it now. State (_team_ai_state, counters, logged-summary
## dedup maps) stays on the sim; delegates keep the original names.
##
## NOT here (later passes): the AI special-power cast cluster
## (_step_ai_special_power_updates + helpers, entangled with ability
## contracts), foundation/animal AI contract attachment.

# Weak back-reference: a strong ref would form a RefCounted cycle with the
# sim (which holds this subsystem), leaking freed sims as zombies — the
# script_wiring orphan-refusal contracts catch exactly that. The getter
# keeps plain `sim.` syntax working everywhere below.




func update_ai_controllers() -> void:
	## Runs the single data-driven controller once per AI team, in ascending team
	## order, each on its own difficulty cadence. For the default {0,1} roster this
	## is exactly one team (team 1 @ medium == legacy), gated on `tick_index % 15`,
	## so it fires on the identical ticks — and issues the identical commands — the
	## old ENEMY_TEAM-bound AI did. Iteration is over the sorted AI-state keys so
	## multiple AI teams resolve deterministically regardless of insertion order.
	var teams: Array = sim._team_ai_state.keys()
	teams.sort()
	for team_value in teams:
		var team := int(team_value)
		var ai_state: Dictionary = sim._team_ai_state[team]
		var profile: Dictionary = sim._difficulty_profile(team)
		if sim.tick_index % maxi(1, int(profile.get("scan_interval", 15))) != 0:
			continue
		run_ai_for_team(team, profile, ai_state)


func ensure_ai_state(team: int) -> Dictionary:
	## Lazily materialize a default (legacy/medium) AI-state record for a team so
	## the back-compat fixture seams below never touch a missing key.
	if not sim._team_ai_state.has(team):
		sim._team_ai_state[team] = {
			"difficulty": sim.AI_DEFAULT_DIFFICULTY,
			"construction_attempted": false,
			"construction_resolved": false,
			"build_order_index": 0,
			"last_wave_tick": 0,
		}
	return sim._team_ai_state[team]


func run_ai_for_team(team: int, profile: Dictionary, ai_state: Dictionary) -> void:
	if sim.base_loop_enabled and not bool(ai_state.get("construction_attempted", false)):
		ai_state["construction_attempted"] = true
		if not start_ai_farm(team, ai_state):
			ai_state["construction_resolved"] = true
		else:
			ai_state["build_order_index"] = 1
	if sim.base_loop_enabled and not bool(ai_state.get("construction_resolved", false)) and not ai_construction_is_viable(team):
		abandon_ai_construction(team)
		ai_state["construction_resolved"] = true
	if sim.base_loop_enabled and not bool(ai_state.get("construction_resolved", false)):
		return
	if sim.base_loop_enabled:
		step_ai_base_building(team, ai_state)
	var queue_interval := maxi(15, int(sim._rules.get("ai_queue_interval_ticks", 60)) * int(profile.get("queue_interval_permille", 1000)) / 1000)
	if sim.base_loop_enabled and sim.tick_index % queue_interval == 0:
		var authored_queued := false
		# Q83b: authored consumption is OPT-IN (use_authored_skirmish_ai rule)
		# until it reaches parity strength — live m2 evidence: the one-choice-
		# per-window plan leaves the AI weaker than the proven manifest plan
		# (fortress never falls; base razes it by tick ~7832).
		if bool(sim.skirmish_ai_configured) and bool(sim._rules.get("use_authored_skirmish_ai", false)):
			var choice: Dictionary = sim._skirmish_ai_subsystem().authored_ai_queue_choice(team)
			if bool(choice.get("ok", false)):
				authored_queued = true
				var choice_rules: Dictionary = sim.unit_production_rules_for_team(team)
				var choice_rule: Dictionary = choice_rules.get(String(choice["unit_type"]), {})
				if not choice_rule.is_empty():
					var choice_producer := int(sim.producer_id(team, String(choice_rule.get("producer_kind", ""))))
					if choice_producer != 0:
						sim.queue_unit(team, choice_producer, String(choice["unit_type"]))
			elif not bool(ai_state.get("authored_queue_refusal_reported", false)):
				# A side the authored document cannot serve falls back to the
				# manifest plan below — LOUDLY, once per team, never silently.
				ai_state["authored_queue_refusal_reported"] = true
				push_warning(
					"skirmish-ai team %d falls back to the manifest plan: %s"
					% [team, String(choice.get("reason", ""))]
				)
		if not authored_queued:
			var plan: Array = sim.ai_production_plan_for_team(team)
			if plan.is_empty():
				# Q80: no AI_PRODUCTION_PLAN constant fallback — an empty
				# manifest plan means this AI queues nothing, honestly.
				plan = sim._ai_production_plan
			var team_rules: Dictionary = sim.unit_production_rules_for_team(team)
			for unit_type in plan:
				var production_rule: Dictionary = team_rules.get(unit_type, {})
				if production_rule.is_empty():
					continue
				var producer := int(sim.producer_id(team, String(production_rule.get("producer_kind", ""))))
				if producer != 0:
					sim.queue_unit(team, producer, unit_type)
	# Give hostiles one full production window before the first wave. Economy and
	# production still advance during the preparation time. Higher tiers commit
	# sooner (shorter attack delay), lower tiers later.
	var attack_delay := maxi(0, int(sim._rules.get("ai_attack_delay_ticks", 0)) * int(profile.get("attack_delay_permille", 1000)) / 1000)
	if sim.base_loop_enabled and sim.tick_index < attack_delay:
		return
	# Damaged battalions pull back to regroup (retreat/regroup is strictly hard+;
	# the neutral tiers pass a 0 threshold and this is a no-op, keeping the default
	# match byte-identical).
	if sim.base_loop_enabled:
		ai_apply_retreat(team, profile)
	var weakest := bool(profile.get("weakest_fortress_priority", false))
	var hostiles: Array = sim._hostile_living_ids(team)
	var enemy_fortress := ai_primary_hostile_fortress(team, weakest) if sim.base_loop_enabled else 0
	if hostiles.is_empty() and enemy_fortress == 0:
		return
	# Fresh units mass into a wave at the fortress muster point and strike
	# together instead of trickling one battalion at a time.
	if sim.base_loop_enabled:
		var wave_size := maxi(2, int(sim._rules.get("ai_wave_size", 4)) + int(profile.get("wave_size_delta", 0)))
		var mustering: Array[int] = []
		for id in sim.living_ids(team):
			var row: Dictionary = sim.entities[id]
			if bool(row.get("is_builder", false)) or bool(row.get("ai_in_wave", false)):
				continue
			if int(row["target_id"]) != 0:
				continue
			mustering.append(id)
		# A stalled economy must not hold the last understrength group at the
		# muster point forever — after the patience window it attacks anyway.
		var patience := int(sim._rules.get("ai_wave_patience_ticks", 1200)) * int(profile.get("wave_patience_permille", 1000)) / 1000
		var wave_ready := mustering.size() >= wave_size
		if not wave_ready and not mustering.is_empty() and sim.tick_index - int(ai_state.get("last_wave_tick", 0)) > patience:
			wave_ready = true
		if wave_ready and not mustering.is_empty():
			ai_state["last_wave_tick"] = sim.tick_index
			for id in mustering:
				(sim.entities[id] as Dictionary)["ai_in_wave"] = true
		else:
			var muster: Vector2 = sim._fallback_rally_position(team)
			var muster_fortress := int(sim.fortress_id(team))
			if muster_fortress != 0:
				muster = Vector2((sim.structures[muster_fortress] as Dictionary).get("rally", muster))
			for id in mustering:
				var row: Dictionary = sim.entities[id]
				if (row["route"] as Array).is_empty() and Vector2(row["position"]).distance_to(muster) > 6.0:
					if sim._assign_route(row, muster):
						row["state"] = "run"
	for id in sim.living_ids(team):
		var row: Dictionary = sim.entities[id]
		# Builders construct; they are not combat battalions and have no weapon.
		if bool(row.get("is_builder", false)):
			continue
		if sim.base_loop_enabled and not bool(row.get("ai_in_wave", false)):
			continue
		if int(row["target_id"]) != 0:
			continue
		var target_id := 0
		var target_kind := "battalion"
		if hostiles.is_empty() and enemy_fortress != 0:
			target_id = enemy_fortress
			target_kind = "structure"
		else:
			# Nearest hostile, ties to the lowest id. This was the last quadratic
			# term in the tick: an all-pairs scan of every wave member against
			# every hostile, unbounded in range, once per team every
			# AI_CONTROLLER_BASE_INTERVAL ticks.
			#
			# Converting it required NORMALISING the tie-break first, which is a
			# deliberate behaviour change. The old rule was
			#   distance < closest or (is_equal_approx(distance, closest) and candidate < target_id)
			# and it could not be reproduced from a neighbourhood query for two
			# reasons: is_equal_approx is a tolerance comparison and therefore not
			# transitive, and on an approximate tie the rule reassigned
			# `closest_distance` to a value that could be slightly LARGER than the
			# current best, so the running minimum drifted upward. Both make the
			# winner depend on sequential visit order. The replacement keeps the
			# same intent - closest, lowest id wins - as an exact total order.
			target_id = int(sim._spatial_nearest_hostile(
				row, team, Vector2(row["position"]), sim.SPATIAL_UNBOUNDED_RANGE, 0, true
			))
			if target_id == 0:
				# hostiles is non-empty here, so the sweep always finds one; this
				# only guards a hostile whose row moved out from under the index.
				target_id = hostiles[0]
		var target_position: Vector2 = sim._target_position(target_id, target_kind)
		var target_distance := Vector2(row["position"]).distance_to(target_position)
		if target_kind == "structure":
			# Once no defending battalion remains, the objective fortress is the
			# strategic target. Assign it before routing so the target's own
			# footprint is exempt and melee units can close to weapon range.
			if ai_assign_target_route_with_backoff(row, target_kind, target_id, target_position, profile):
				row["target_id"] = target_id
				row["target_kind"] = target_kind
				row["attack_windup"] = 0
				row["state"] = "run"
				var wave_order_ids_0: Array[int] = [id]
				sim._stamp_order_sequence(wave_order_ids_0)
				sim._emit_music("battle")
			continue
		var vision_range := maxf(float(row.get("attack_range", 1.15)), float(row.get("vision_range", 17.5)))
		if target_distance > vision_range:
			# Strategic AI can advance toward the opposing base, but it does not gain
			# a live combat target or attack animation through unexplored distance.
			# Advance toward the candidate we are actually trying to acquire; when no
			# hostile battalions remain, target_position is the objective fortress.
			var strategic_destination := target_position
			if (row["route"] as Array).is_empty() or Vector2(row.get("destination", row["position"])).distance_to(strategic_destination) > 1.0:
				if ai_assign_target_route_with_backoff(row, target_kind, target_id, strategic_destination, profile):
					row["target_id"] = 0
					row["target_kind"] = "battalion"
					row["attack_windup"] = 0
					row["state"] = "run"
					var wave_order_ids_1: Array[int] = [id]
					sim._stamp_order_sequence(wave_order_ids_1)
			continue
		if ai_assign_target_route_with_backoff(row, target_kind, target_id, target_position, profile):
			row["target_id"] = target_id
			row["target_kind"] = target_kind
			row["attack_windup"] = 0
			row["state"] = "run"
			var wave_order_ids_2: Array[int] = [id]
			sim._stamp_order_sequence(wave_order_ids_2)
			sim._emit_music("battle")


func ai_assign_target_route_with_backoff(
	row: Dictionary,
	target_kind: String,
	target_id: int,
	target_position: Vector2,
	profile: Dictionary
) -> bool:
	var topology_revision := 0
	var component_pair := ""
	if sim.route_provider != null:
		if sim.route_provider.has_method("navigation_topology_revision_value"):
			topology_revision = int(sim.route_provider.call("navigation_topology_revision_value"))
		if sim.route_provider.has_method("navigation_component_pair_key"):
			component_pair = String(sim.route_provider.call(
				"navigation_component_pair_key",
				Vector2(row.get("position", Vector2.ZERO)),
				target_position
			))
	var order_key := "%s:%d:%s" % [target_kind, target_id, component_pair]
	var backoff: Dictionary = row.get("ai_route_backoff", {}) as Dictionary
	if (
		String(backoff.get("order_key", "")) == order_key
		and int(backoff.get("topology_revision", -1)) == topology_revision
		and sim.tick_index < int(backoff.get("retry_tick", 0))
	):
		sim.ai_route_backoff_skip_count += 1
		sim.last_route_rejection = "no-bounded-route"
		return false
	if sim._assign_target_route(row, target_position):
		row.erase("ai_route_backoff")
		return true
	if sim.last_route_rejection != "no-bounded-route":
		row.erase("ai_route_backoff")
		return false
	var failure_count := 1
	if (
		String(backoff.get("order_key", "")) == order_key
		and int(backoff.get("topology_revision", -1)) == topology_revision
	):
		failure_count = int(backoff.get("failure_count", 0)) + 1
	var scan_interval := maxi(1, int(profile.get("scan_interval", 15)))
	var patience := maxi(scan_interval, int(sim._rules.get("ai_wave_patience_ticks", 1200)))
	var retry_delay := scan_interval
	for _failure in range(failure_count):
		retry_delay = mini(patience, retry_delay * 2)
		if retry_delay >= patience:
			break
	row["ai_route_backoff"] = {
		"order_key": order_key,
		"failure_count": failure_count,
		"retry_tick": sim.tick_index + retry_delay,
		"topology_revision": topology_revision,
	}
	return false


func ai_primary_hostile_fortress(team: int, weakest: bool) -> int:
	## The objective fortress for `team`. Default (closest) tiers take the lowest-id
	## hostile team's fortress; for the {0,1} roster that is exactly
	## `fortress_id(PLAYER_TEAM)`, so the default path is byte-identical. Weakest-
	## priority tiers (brutal/morgoth) instead march the whole wave onto the
	## lowest-health hostile fortress (deterministic, tie-broken by id).
	var best := 0
	var best_health := 0
	for other_value in sim._roster_team_ids():
		var other := int(other_value)
		if not sim._is_hostile(team, other):
			continue
		var fortress := int(sim.fortress_id(other))
		if fortress == 0:
			continue
		var health := int((sim.structures[fortress] as Dictionary).get("health", 0))
		if best == 0:
			best = fortress
			best_health = health
		elif weakest and health < best_health:
			best = fortress
			best_health = health
	return best


func ai_apply_retreat(team: int, profile: Dictionary) -> void:
	## Army management (hard+): a committed battalion whose surviving members have
	## fallen below the tier's retreat fraction pulls out of the current wave and
	## routes home to regroup, rejoining the next muster. The threshold is a
	## permille of member count (integer, no floats), so a 0 threshold — every
	## neutral tier — returns immediately and never perturbs the default match.
	var permille := int(profile.get("retreat_member_permille", 0))
	if permille <= 0:
		return
	var muster: Vector2 = sim._fallback_rally_position(team)
	var muster_fortress := int(sim.fortress_id(team))
	if muster_fortress != 0:
		muster = Vector2((sim.structures[muster_fortress] as Dictionary).get("rally", muster))
	for id in sim.living_ids(team):
		var row: Dictionary = sim.entities[id]
		if bool(row.get("is_builder", false)) or not bool(row.get("ai_in_wave", false)):
			continue
		# A battalion already storming an enemy structure presses the assault — it
		# does not turn tail at the gates (turtling there would cede the base race).
		if String(row.get("target_kind", "")) == "structure" and int(row.get("target_id", 0)) != 0:
			continue
		var members: Array = row.get("member_health", [])
		if members.is_empty():
			continue
		var alive := 0
		for value in members:
			if int(value) > 0:
				alive += 1
		if alive * 1000 >= members.size() * permille:
			continue
		row["ai_in_wave"] = false
		row["target_id"] = 0
		row["target_kind"] = "battalion"
		row["attack_windup"] = 0
		if sim._assign_route(row, muster):
			row["state"] = "run"


func step_ai_base_building(team: int, ai_state: Dictionary) -> void:
	# A dead porter is retrained first, even once the authored build order is
	# exhausted (factions with few buildable kinds would otherwise never
	# recover their builder).
	var living_builders: Array[int] = []
	for id in sim.living_ids(team):
		if bool((sim.entities[id] as Dictionary).get("is_builder", false)):
			living_builders.append(id)
	if living_builders.is_empty():
		ai_train_builder(team)
		return
	var order := ai_build_order_for_team(team)
	var index := int(ai_state.get("build_order_index", 0))
	if index >= order.size():
		return
	if ai_construction_is_viable(team):
		return
	var kind := String(order[index])
	var build_rule: Dictionary = sim.structure_build_rules_for_team(team).get(kind, {})
	if build_rule.is_empty():
		ai_state["build_order_index"] = index + 1
		return
	if sim.resources_for_team(team) < int(build_rule.get("cost", 0)):
		return
	var anchor := Vector2((sim.entities[living_builders[0]] as Dictionary).get("position", Vector2.ZERO))
	var team_fortress := int(sim.fortress_id(team))
	if team_fortress != 0:
		anchor = Vector2((sim.structures[team_fortress] as Dictionary).get("position", Vector2.ZERO))
	# The default (medium/easy) search is exactly the historical four rings; tiers
	# that build extra producers get additional outer rings so the larger base has
	# room. Untouched for the default match, so its placement stays byte-identical.
	var radii: Array = [10.0, 14.0, 18.0, 22.0]
	if int(sim._difficulty_profile(team).get("extra_producer_cycles", 0)) > 0:
		radii = [10.0, 14.0, 18.0, 22.0, 26.0, 30.0, 34.0, 38.0]
	if sim.castle_fixtures_enabled:
		if try_castle_ai_construction(team, ai_state, living_builders, kind):
			ai_state["build_order_index"] = index + 1
		return
	for radius_value in radii:
		var radius := float(radius_value)
		for direction_index in range(8):
			var angle := TAU * float(direction_index) / 8.0
			var candidate: Vector2 = anchor + Vector2(cos(angle), sin(angle)) * radius
			if bool(sim._issue_construct_for_team(team, living_builders, kind, candidate).get("ok", false)):
				ai_state["build_order_index"] = index + 1
				return


func ai_build_order_for_team(team: int) -> Array[String]:
	## Faction-derived build order: farm first when the faction declares one,
	## then each AI-plan unit's producer kind in plan order. The fortress is
	## never rebuilt (it is the seeded structure). The historical Men order is
	## just this derivation over the Gondor plan.
	##
	## Economy aggression + build variety (hard and up): the tier appends extra
	## farm->producer CYCLES, giving higher tiers a genuinely larger production
	## base (more parallel producers + income) — the lasting military-throughput
	## edge that banked gold alone cannot buy against a producer-bound opponent.
	## medium/easy add zero cycles, so the default build order is byte-identical.
	var order: Array[String] = []
	var has_farm: bool = sim.structure_build_rules_for_team(team).has("farm")
	var team_rules: Dictionary = sim.unit_production_rules_for_team(team)
	var producers: Array[String] = []
	for unit_type_value in sim.ai_production_plan_for_team(team):
		var rule: Dictionary = team_rules.get(String(unit_type_value), {}) as Dictionary
		var kind := String(rule.get("producer_kind", ""))
		if kind != "" and kind != "fortress" and not producers.has(kind):
			producers.append(kind)
	if has_farm:
		order.append("farm")
	for kind in producers:
		order.append(kind)
	var cycles := int(sim._difficulty_profile(team).get("extra_producer_cycles", 0))
	for _cycle in range(cycles):
		if has_farm:
			order.append("farm")
		for kind in producers:
			order.append(kind)
	return order


func ai_train_builder(team: int) -> void:
	## The porter died: retrain it from the fortress so the base can keep
	## developing. Never double-queue a builder that is already alive or
	## already in a production queue.
	var manifest: Dictionary = sim.team_manifest_for(team)
	var team_rules: Dictionary = sim.unit_production_rules_for_team(team)
	for builder_value in manifest.get("builder_unit_ids", []) as Array:
		var builder_id := String(builder_value)
		var already_covered := false
		for id in sim.living_ids(team):
			if String((sim.entities[id] as Dictionary).get("object_id", "")) == builder_id:
				already_covered = true
				break
		if not already_covered:
			for structure_id in sim.structure_ids(team):
				for item_value in Array((sim.structures[structure_id] as Dictionary).get("queue", [])):
					if String((item_value as Dictionary).get("unit_type", "")) == builder_id:
						already_covered = true
						break
				if already_covered:
					break
		if already_covered:
			return
		for unit_type_value in team_rules.keys():
			var unit_type := String(unit_type_value)
			var rule: Dictionary = team_rules[unit_type]
			if String(rule.get("object_id", "")) != builder_id:
				continue
			var producer := int(sim.producer_id(team, String(rule.get("producer_kind", "fortress"))))
			if producer != 0:
				sim.queue_unit(team, producer, unit_type)
			return


func start_ai_farm(team: int, ai_state: Dictionary = {}) -> bool:
	var builder_ids: Array[int] = []
	for id in sim.living_ids(team):
		if bool((sim.entities[id] as Dictionary).get("is_builder", false)):
			builder_ids.append(id)
	if builder_ids.is_empty():
		return false
	if sim.castle_fixtures_enabled:
		return try_castle_ai_construction(team, ai_state, builder_ids, "farm")
	var builder_position := Vector2((sim.entities[builder_ids[0]] as Dictionary).get("position", Vector2.ZERO))
	# A bounded clockwise search is deterministic and uses the same admission,
	# obstruction, route, cost, and construction path as a player MenPorter.
	for direction in [Vector2.UP, Vector2.RIGHT, Vector2.DOWN, Vector2.LEFT]:
		var candidate: Vector2 = builder_position + direction * 10.0
		var result: Dictionary = sim._issue_construct_for_team(team, builder_ids, "farm", candidate)
		if bool(result.get("ok", false)):
			return true
	return false


func ai_start_waypoint_name(team: int) -> String:
	if sim.castle_fixtures_enabled and sim._extra_team_centers.has(team):
		var center := Vector2(sim._extra_team_centers[team])
		var closest_name := ""
		var closest_distance := INF
		for name_value in sim.source_player_starts.keys():
			var name := String(name_value)
			var distance := center.distance_squared_to(Vector2(sim.source_player_starts[name_value]))
			if distance < closest_distance:
				closest_distance = distance
				closest_name = name
		if closest_name != "":
			return closest_name
	var descriptor: Dictionary = sim._team_descriptors.get(team, {}) as Dictionary
	var start_index := int(descriptor.get("start_index", -1))
	if start_index < 0:
		start_index = int(sim._configured_team_start_indices.get(team, -1))
	if start_index < 0:
		return ""
	return "Player_%d_Start" % (start_index + 1)


func castle_ai_home(team: int) -> Dictionary:
	if sim._extra_team_centers.has(team):
		return {"position": Vector2(sim._extra_team_centers[team]), "source": "castle-start:team-center"}
	var start_name := ai_start_waypoint_name(team)
	if start_name != "" and sim.source_player_starts.has(start_name):
		return {"position": Vector2(sim.source_player_starts[start_name]), "source": "castle-start:%s" % start_name}
	return {"position": sim._team_center(team), "source": "generic-team-center"}


func castle_ai_layout(team: int) -> Dictionary:
	var side_result: Dictionary = sim.team_retail_side(team)
	var side := String(side_result.get("side", ""))
	if side == "":
		return {}
	for layout_value in sim._castle_ai_base_layouts:
		var layout := layout_value as Dictionary
		if String(layout.get("side", "")).to_lower() == side.to_lower():
			return layout
	return {}


func structure_kind_for_source_object(team: int, object_id: String) -> String:
	var wanted := object_id.to_lower()
	for kind_value in sim.structure_source_object_ids_for_team(team).keys():
		var kind := String(kind_value)
		var sources: Variant = sim.structure_source_object_ids_for_team(team)[kind_value]
		if typeof(sources) == TYPE_ARRAY:
			for source_value in sources as Array:
				if String(source_value).to_lower() == wanted:
					return kind
		elif String(sources).to_lower() == wanted:
			return kind
	return ""


func castle_ai_project_source_offset(offset_source: Vector2) -> Vector2:
	# BSE offsets are SAGE XY. Convert Y to Godot Z, then project through the
	# same rotated source frame RetailMapData established from the player starts.
	var source_horizontal := Vector2(offset_source.x, -offset_source.y)
	var scale := float(sim._rules.get("source_map_transform_scale", 0.1))
	return Vector2(source_horizontal.dot(sim._source_map_axis_x), source_horizontal.dot(sim._source_map_axis_z)) * scale


func castle_ai_authored_candidates(team: int, structure_kind: String) -> Array[Dictionary]:
	var candidates: Array[Dictionary] = []
	var home := castle_ai_home(team)
	var anchor := Vector2(home["position"])
	var layout := castle_ai_layout(team)
	if not layout.is_empty():
		for site_value in layout.get("sites", []) as Array:
			var site := site_value as Dictionary
			if structure_kind_for_source_object(team, String(site.get("objectId", ""))) != structure_kind:
				continue
			var offset := site.get("offsetSource", []) as Array
			if offset.size() != 3:
				continue
			candidates.append({
				"position": anchor + castle_ai_project_source_offset(Vector2(float(offset[0]), float(offset[1]))),
				"source": "authored-ai-base:%s:site-%d" % [String(layout.get("side", "")), int(site.get("index", -1))],
			})
	var start_name := ai_start_waypoint_name(team)
	if start_name != "":
		var prefix := start_name.trim_suffix("Start") + "BuildPlot_"
		for plot_index in range(1, 9):
			var waypoint_name := "%s%d" % [prefix, plot_index]
			if sim._ai_build_waypoints.has(waypoint_name):
				candidates.append({
					"position": Vector2(sim._ai_build_waypoints[waypoint_name]),
					"source": "authored-build-plot:%s" % waypoint_name,
				})
	return candidates


func try_castle_ai_site(
	team: int,
	ai_state: Dictionary,
	builder_ids: Array[int],
	structure_kind: String,
	candidate: Vector2,
	source: String
) -> bool:
	var authored_site := source.begins_with("authored-")
	sim.castle_ai_site_dry_runs += 1
	var dry_run: Dictionary = sim._issue_construct_for_team(team, builder_ids, structure_kind, candidate, true, authored_site)
	if not bool(dry_run.get("ok", false)):
		var reason := String(dry_run.get("reason", "rejected"))
		ai_state["last_site_rejection"] = "%s:%s" % [source, reason]
		# One line per authored site is a bounded, useful receipt. The generic
		# navigation-cell scan is not: it produced 99,466 identical lines in the
		# owner's v0.2.8 Minas Tirith boot. Those are summarised once per
		# (team, structure kind) by the caller instead.
		if not source.begins_with(sim.CASTLE_AI_GENERIC_CELL_SOURCE_PREFIX):
			sim.castle_ai_site_reject_prints += 1
			print("[RetailSliceSim] CASTLE_AI_REJECT team=%d structure=%s site_source=%s reason=%s detail=%s" % [team, structure_kind, source, reason, str(dry_run)])
		return false
	var builder := sim.entities[builder_ids[0]] as Dictionary
	if not sim.parity.can_path_between(Vector2(builder.get("position", Vector2.ZERO)), candidate):
		ai_state["last_site_rejection"] = "%s:parity-path-impassable" % source
		return false
	var route: Dictionary = sim._query_route_for_row(builder, Vector2(builder.get("position", Vector2.ZERO)), candidate)
	if not bool(route.get("valid", false)) or (route.get("points", []) as Array).is_empty():
		ai_state["last_site_rejection"] = "%s:%s" % [source, String(route.get("reason", "route-rejected"))]
		return false
	var result: Dictionary = sim._issue_construct_for_team(team, builder_ids, structure_kind, candidate, false, authored_site)
	if not bool(result.get("ok", false)):
		ai_state["last_site_rejection"] = "%s:%s" % [source, String(result.get("reason", "rejected"))]
		return false
	ai_state["home_position"] = Vector2(castle_ai_home(team)["position"])
	ai_state["home_source"] = String(castle_ai_home(team)["source"])
	ai_state["site_source"] = source
	ai_state.erase("last_site_rejection")
	print("[RetailSliceSim] CASTLE_AI_SITE team=%d home_source=%s home=%s structure=%s site_source=%s site=%s" % [team, ai_state["home_source"], str(ai_state["home_position"]), structure_kind, source, str(candidate)])
	return true


func try_castle_ai_construction(
	team: int,
	ai_state: Dictionary,
	builder_ids: Array[int],
	structure_kind: String
) -> bool:
	var builder_position := Vector2((sim.entities[builder_ids[0]] as Dictionary).get("position", Vector2.ZERO))
	var kind_refusal := castle_ai_kind_level_refusal(team, builder_ids, structure_kind, builder_position)
	if kind_refusal != "":
		ai_state["last_site_rejection"] = "kind-level:%s" % kind_refusal
		return false
	var authored_candidates := castle_ai_authored_candidates(team, structure_kind)
	# Retail supplies the sites, while the live porter chooses the shortest
	# deterministic trip. This prevents file/index order from sending a castle
	# porter across the entire keep before it can establish production.
	authored_candidates.sort_custom(func(left: Dictionary, right: Dictionary) -> bool:
		var left_distance := builder_position.distance_squared_to(Vector2(left["position"]))
		var right_distance := builder_position.distance_squared_to(Vector2(right["position"]))
		if not is_equal_approx(left_distance, right_distance):
			return left_distance < right_distance
		return String(left["source"]) < String(right["source"])
	)
	ai_state["start_waypoint"] = ai_start_waypoint_name(team)
	ai_state["authored_site_candidates"] = authored_candidates.size()
	ai_state["available_build_waypoints"] = sim._ai_build_waypoints.size()
	print("[RetailSliceSim] CASTLE_AI_CANDIDATES team=%d structure=%s start=%s authored=%d build_waypoints=%d" % [team, structure_kind, String(ai_state["start_waypoint"]), authored_candidates.size(), sim._ai_build_waypoints.size()])
	for candidate_value in authored_candidates:
		var candidate := candidate_value as Dictionary
		if try_castle_ai_site(team, ai_state, builder_ids, structure_kind, Vector2(candidate["position"]), String(candidate["source"])):
			return true
	if not authored_candidates.is_empty() and ai_state.has("last_site_rejection"):
		ai_state["authored_site_fallback_reason"] = String(ai_state["last_site_rejection"])
	# Maps without a usable authored AIBase/build-plot row use the complete
	# cooked navigation extent. Its dimensions are retail-authored map data, so
	# the fallback has no guessed distance cap and cannot stop inside a castle's
	# wall ring. Every candidate still passes the normal placement and route
	# gates, which prevents building on or behind obstructing fixtures.
	if sim.route_provider == null or not sim.route_provider.has_method("local_to_grid_cell") or not sim.route_provider.has_method("grid_to_local_horizontal") or not sim.route_provider.has_method("is_navigation_walkable"):
		return false
	var home_cell: Vector2i = sim.route_provider.call("local_to_grid_cell", builder_position)
	var navigation_min := Vector2i(sim.route_provider.get("navigation_grid_min"))
	var navigation_max := Vector2i(sim.route_provider.get("navigation_grid_max"))
	var maximum_radius := maxi(
		maxi(absi(home_cell.x - navigation_min.x), absi(home_cell.x - navigation_max.x)),
		maxi(absi(home_cell.y - navigation_min.y), absi(home_cell.y - navigation_max.y))
	)
	var cursors: Dictionary = ai_state.get("generic_scan_next_radius", {}) as Dictionary
	var first_radius := clampi(int(cursors.get(structure_kind, 0)), 0, maximum_radius)
	var budget := castle_ai_generic_scan_cell_budget(home_cell)
	var scanned := 0
	var rejected := 0
	var top_reason := ""
	var radius := first_radius
	while radius <= maximum_radius:
		if scanned >= budget:
			break
		for dy in range(-radius, radius + 1):
			for dx in range(-radius, radius + 1):
				if abs(dx) != radius and abs(dy) != radius:
					continue
				var cell := home_cell + Vector2i(dx, dy)
				if not bool(sim.route_provider.call("is_navigation_walkable", cell)):
					continue
				var position := Vector2(sim.route_provider.call("grid_to_local_horizontal", cell))
				scanned += 1
				if try_castle_ai_site(team, ai_state, builder_ids, structure_kind, position, "%s%d,%d" % [sim.CASTLE_AI_GENERIC_CELL_SOURCE_PREFIX, cell.x, cell.y]):
					cursors.erase(structure_kind)
					castle_ai_store_scan_cursors(ai_state, cursors)
					return true
				rejected += 1
				if top_reason == "":
					top_reason = String(ai_state.get("last_site_rejection", "")).get_slice(":", 2)
		radius += 1
	# One summary line per (team, structure kind) scan slice replaces the
	# per-cell CASTLE_AI_REJECT spam. `resume_radius` is -1 when the scan
	# exhausted the map this tick.
	var exhausted := radius > maximum_radius
	if exhausted:
		cursors.erase(structure_kind)
	else:
		cursors[structure_kind] = radius
	castle_ai_store_scan_cursors(ai_state, cursors)
	var summary := "[RetailSliceSim] CASTLE_AI_CELL_SCAN team=%d structure=%s radius=%d..%d max_radius=%d budget=%d scanned=%d rejected=%d resume_radius=%d reason=%s" % [
		team,
		structure_kind,
		first_radius,
		maxi(first_radius, radius - 1),
		maximum_radius,
		budget,
		scanned,
		rejected,
		-1 if exhausted else radius,
		top_reason if top_reason != "" else "none",
	]
	# The AI retries every tick. Once the scan has wrapped, each slice repeats
	# verbatim, so an unchanged summary is printed once and then stays silent -
	# a 4,000-tick castle run cannot re-fill a log with it.
	var logged: Dictionary = sim._castle_ai_scan_summaries_logged.get(team, {}) as Dictionary
	if String(logged.get(structure_kind, "")) != summary:
		logged[structure_kind] = summary
		sim._castle_ai_scan_summaries_logged[team] = logged
		sim.castle_ai_site_reject_prints += 1
		print(summary)
	return false


func castle_ai_store_scan_cursors(ai_state: Dictionary, cursors: Dictionary) -> void:
	# Written only while a scan is actually mid-map, so maps whose fallback
	# finishes inside one tick add no key to the serialized AI state.
	if cursors.is_empty():
		ai_state.erase("generic_scan_next_radius")
	else:
		ai_state["generic_scan_next_radius"] = cursors


func castle_ai_generic_scan_cell_budget(home_cell: Vector2i) -> int:
	## Cells examinable in one tick = the square of the measured retail base
	## footprint radius (CASTLE_AI_RETAIL_BASE_EXTENT_SOURCE) expressed in this
	## map's navigation cells. Cell span is measured from the provider itself,
	## so no map scale is guessed here.
	var origin := Vector2(sim.route_provider.call("grid_to_local_horizontal", home_cell))
	var neighbour := Vector2(sim.route_provider.call("grid_to_local_horizontal", home_cell + Vector2i(1, 0)))
	var cell_span := origin.distance_to(neighbour)
	if cell_span <= 0.0:
		return 1
	var extent: float = sim.CASTLE_AI_RETAIL_BASE_EXTENT_SOURCE * float(sim._rules.get("source_map_transform_scale", 0.1))
	var radius_cells := maxi(1, int(ceil(extent / cell_span)))
	return (2 * radius_cells + 1) * (2 * radius_cells + 1)


func castle_ai_kind_level_refusal(
	team: int,
	builder_ids: Array[int],
	structure_kind: String,
	probe: Vector2
) -> String:
	## Exactly one construct dry-run settles whether the refusal depends on the
	## site at all. A kind-level reason is printed once per (team, kind) instead
	## of once per navigation cell.
	sim.castle_ai_site_dry_runs += 1
	var receipt: Dictionary = sim._issue_construct_for_team(team, builder_ids, structure_kind, probe, true, false)
	if bool(receipt.get("ok", false)):
		return ""
	var reason := String(receipt.get("reason", "rejected"))
	if not sim.CASTLE_AI_KIND_LEVEL_REFUSALS.has(reason):
		return ""
	var seen: Dictionary = sim._castle_ai_kind_refusals_logged.get(team, {}) as Dictionary
	if String(seen.get(structure_kind, "")) != reason:
		seen[structure_kind] = reason
		sim._castle_ai_kind_refusals_logged[team] = seen
		sim.castle_ai_site_reject_prints += 1
		print("[RetailSliceSim] CASTLE_AI_KIND_REJECT team=%d structure=%s reason=%s detail=%s" % [team, structure_kind, reason, str(receipt)])
	return reason


func ai_construction_is_viable(team: int) -> bool:
	for id in sim.living_ids(team):
		var builder: Dictionary = sim.entities[id]
		if not bool(builder.get("is_builder", false)):
			continue
		var construction_id := int(builder.get("construction_id", 0))
		if construction_id != 0 and sim.structures.has(construction_id) and int((sim.structures[construction_id] as Dictionary).get("health", 0)) > 0:
			return true
	return false


func abandon_ai_construction(team: int) -> void:
	for id in sim.entity_ids():
		var builder: Dictionary = sim.entities[id]
		if int(builder.get("team", -1)) != team or not bool(builder.get("is_builder", false)) or int(builder.get("construction_id", 0)) == 0:
			continue
		var construction_id := int(builder.get("construction_id", 0))
		if sim.structures.has(construction_id):
			var site: Dictionary = sim.structures[construction_id]
			site["builder_id"] = 0
			if int(site.get("health", 0)) > 0 and float(site.get("construction_progress", 1.0)) < 1.0:
				site["health"] = 0
				sim._emit_event("structure.destroyed", 0, construction_id, {"reason": "construction-builder-unavailable"})
		builder["construction_id"] = 0
		builder["order_kind"] = ""
		sim._clear_pending_route(builder, true)
		if int(builder.get("health", 0)) > 0:
			builder["state"] = "idle"
