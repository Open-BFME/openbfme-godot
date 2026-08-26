extends "res://src/retail_slice/retail_sim_subsystem.gd"
## Match seeding carved out of retail_slice_sim.gd (drawer 21): base-loop initialization, home layouts, spawn anchors, battalion creation.
## State stays on the sim; the sim keeps one-line delegates under the original names.



func _initialize_base_loop() -> void:
	var _sim = sim
	_sim.structures.clear()
	_sim._note_structure_table_mutation()
	# Same contract as _restore_authoritative_state: ids are about to be reused
	# by a new match, so the id-keyed footprint memo must not survive.
	_sim._structure_footprint_radius_cache.clear()
	var layout = _sim._home_layout if not _sim._home_layout.is_empty() else _derive_home_layout()
	for team in _sim._roster_team_ids():
		var team_layout: Dictionary = layout.get(team, layout.get(str(team), {}))
		var base_id = _sim._team_structure_base(team)
		var team_seed_kinds = _sim.seed_structure_kinds_for_team(team)
		if team_seed_kinds.is_empty():
			team_seed_kinds = _sim.structure_kinds_for_team(team)
		var team_max_health = _sim.structure_max_health_for_team(team)
		var team_build_rules = _sim.structure_build_rules_for_team(team)
		var team_production_order = _sim.production_unit_order_for_team(team)
		var team_production_rules = _sim.unit_production_rules_for_team(team)
		var team_scope = _sim.production_scope_for_team(team)
		for index in range(team_seed_kinds.size()):
			var kind := String(team_seed_kinds[index])
			var position := Vector2(team_layout.get(kind, _fallback_structure_position(team, index)))
			var maximum_health := int(team_max_health[kind])
			var production: Array[String] = []
			for unit_type in team_production_order:
				if not team_scope.is_empty() and not team_scope.has(String(unit_type)):
					continue
				if _sim.created_hero_owner_team(String(unit_type)) not in [-1, team]:
					continue
				var production_rule: Dictionary = team_production_rules[unit_type]
				var producer_kinds_for_rule: Array = production_rule.get("producer_kinds", [String(production_rule.get("producer_kind", ""))])
				if producer_kinds_for_rule.has(kind):
					production.append(unit_type)
			var structure_id = base_id + index + 1
			_sim._note_structure_table_mutation()
			_sim.structures[structure_id] = {
				"id": structure_id,
				"team": team,
				"kind": "structure",
				"structure_kind": kind,
				"name": kind.replace("_", " ").capitalize(),
				"position": position,
				"rally": Vector2(team_layout.get("rally", _fallback_rally_position(team))),
				"health": maximum_health,
				"maximum_health": maximum_health,
				"construction_progress": 1.0,
				"level": 1,
				"completed_upgrades": [],
				"upgrade_queue": [],
				"production": production,
				"queue": [],
				"damage_remainders": {},
				"income_per_payout": int(_sim._rules.get("farm_income", 25)) if kind == "farm" else 0,
			}
			if bool((team_build_rules.get(kind, {}) as Dictionary).get("highlander_body", false)):
				_sim.structures[structure_id]["highlander_body"] = true
			# Retail object identity for EVERY seeded kind, not just fortresses,
			# so this path and issue_construct stamp the same table for the same
			# kind and a building cannot have two identities depending on
			# whether the map placed it or a porter raised it.
			#
			# MEASURED, not assumed: the id is one input to
			# _structure_footprint_radius, which raised the worry that stamping
			# it would move structure attack-range and unit eviction. On the
			# mounted Men and Angmar packs it does not — the runner prints
			# with/without/seed radii for a fortress and a non-fortress kind and
			# all three agree to four decimals (FORTRESS_FOOTPRINT,
			# NON_FORTRESS_FOOTPRINT), because the resolver already falls back to
			# the same per-kind geometry. The 3000-tick state pin is unchanged.
			# The symmetry is worth having on its own terms; it is not a fix for
			# a divergence anyone has demonstrated.
			var seed_sources: Variant = _sim.structure_source_object_ids_for_team(team).get(kind, [])
			if typeof(seed_sources) == TYPE_ARRAY and not (seed_sources as Array).is_empty():
				_sim.structures[structure_id]["source_object_id"] = String((seed_sources as Array)[0])
			elif typeof(seed_sources) in [TYPE_STRING, TYPE_STRING_NAME]:
				_sim.structures[structure_id]["source_object_id"] = String(seed_sources)
			_sim._apply_structure_create_grants(
				_sim.structures[structure_id] as Dictionary, true, true
			)
			_sim._apply_structure_inherit_upgrades(_sim.structures[structure_id] as Dictionary)
			_sim._initialize_structure_auto_deposit(_sim.structures[structure_id] as Dictionary)
			_sim._unpack_castle_behavior_for_structure(structure_id)
	_sim._seed_all_expansion_pads()
	if _sim.build_plots_only:
		_sim._seed_all_build_plots()


func _team_center(team: int) -> Vector2:
	## Each roster team's spawn anchor. Teams 0/1 keep their exact historical
	## centers derived from spawn ids 1/2 and 101/102; teams >=2 read their own
	## anchor injected by the map layer (Player_N_Start), falling back to the map
	## centroid so a base still seeds when a start was not supplied.
	var _sim = sim
	if team == _sim.PLAYER_TEAM:
		return (Vector2(_sim._spawn_positions[1]) + Vector2(_sim._spawn_positions[2])) * 0.5
	if team == _sim.ENEMY_TEAM:
		return (Vector2(_sim._spawn_positions[101]) + Vector2(_sim._spawn_positions[102])) * 0.5
	if _sim._extra_team_centers.has(team):
		return Vector2(_sim._extra_team_centers[team])
	return _two_team_map_center()


func _two_team_map_center() -> Vector2:
	var _sim = sim
	return ((Vector2(_sim._spawn_positions[1]) + Vector2(_sim._spawn_positions[2])) * 0.5 + (Vector2(_sim._spawn_positions[101]) + Vector2(_sim._spawn_positions[102])) * 0.5) * 0.5


func _map_centroid() -> Vector2:
	## Average of every rostered team's spawn anchor. For the {0,1} default this is
	## exactly the midpoint of the two team centers the old code used, so the
	## derived outward/rally directions are unchanged.
	var teams = sim._roster_team_ids()
	if teams.size() <= 2:
		return _two_team_map_center()
	var sum := Vector2.ZERO
	for team in teams:
		sum += _team_center(int(team))
	return sum / float(teams.size())


func _derive_home_layout() -> Dictionary:
	var _sim = sim
	var map_center := _map_centroid()
	var result: Dictionary = {}
	for team in _sim._roster_team_ids():
		var anchor := _team_center(int(team))
		var outward := anchor.direction_to(map_center) * -1.0
		if outward.length_squared() < 0.01:
			outward = Vector2.LEFT if team == _sim.PLAYER_TEAM else Vector2.RIGHT
		var side := Vector2(-outward.y, outward.x)
		result[team] = {
			"fortress": anchor + outward * 10.0,
			"farm": anchor + side * 11.0 + outward * 2.0,
			"barracks": anchor - side * 11.0 + outward * 1.0,
			"archery_range": anchor + side * 18.0 - outward * 3.0,
			"stable": anchor - side * 18.0 - outward * 3.0,
			"rally": anchor - outward * 8.0,
		}
	return result


func _fallback_structure_position(team: int, index: int) -> Vector2:
	var _sim = sim
	var anchor := _team_center(team)
	# Structures tile away from the map centroid. Teams 0/1 keep their historical
	# left/right sign (player centroid-outward points -x on the two-corner maps,
	# enemy +x); teams >=2 derive the sign from their own outward direction.
	var sign_value = -1.0 if team == _sim.PLAYER_TEAM else 1.0
	if team != _sim.PLAYER_TEAM and team != _sim.ENEMY_TEAM:
		var outward := anchor.direction_to(_map_centroid()) * -1.0
		sign_value = signf(outward.x) if not is_zero_approx(outward.x) else 1.0
	if index < 5:
		return anchor + Vector2(sign_value * (8.0 + float(index) * 2.5), (float(index) - 2.0) * 7.0)
	# Factions whose manifests declare more base sim.structures than the historical
	# Men five tile the overflow in bounded extra rows beside the original
	# layout, so every seeded structure stays near its team's anchor.
	var sub := index - 5
	var row := sub / 5
	var col := sub % 5
	return anchor + Vector2(sign_value * (20.5 + float(row) * 5.0 + float(col) * 2.5), (float(col) - 2.0) * 7.0)


func _fallback_rally_position(team: int) -> Vector2:
	# Fixture sims configured without a map (no player starts) must not crash
	# the AI muster path; real matches always carry spawn positions.
	if sim._spawn_positions.is_empty():
		return Vector2.ZERO
	return _team_center(team)


func _builder_spawn_position(team: int) -> Vector2:
	var _sim = sim
	var layout = _sim._home_layout if not _sim._home_layout.is_empty() else _derive_home_layout()
	var team_layout: Dictionary = layout.get(team, layout.get(str(team), {}))
	return Vector2(team_layout.get("rally", _fallback_rally_position(team)))


func _spawn_anchor_position(anchor: String, team: int = sim.PLAYER_TEAM) -> Vector2:
	## Named map anchors keep the faction roster data-driven while spawn
	## geometry stays derived from the cooked source map's player starts. Teams
	## beyond 0/1 resolve the generalized anchors around their own spawn center.
	var _sim = sim
	if team != _sim.PLAYER_TEAM and team != _sim.ENEMY_TEAM:
		if anchor.ends_with("builder"):
			return _builder_spawn_position(team) if _sim.base_loop_enabled else _team_center(team) + Vector2(0.0, -4.0)
		return _team_center(team)
	match anchor:
		"player_spawn_primary":
			return Vector2(_sim._spawn_positions[1])
		"player_spawn_secondary":
			return Vector2(_sim._spawn_positions[2])
		"enemy_spawn_primary":
			return Vector2(_sim._spawn_positions[101])
		"enemy_spawn_secondary":
			return Vector2(_sim._spawn_positions[102])
		"enemy_reserve":
			return (Vector2(_sim._spawn_positions[101]) + Vector2(_sim._spawn_positions[102])) * 0.5
		"player_builder":
			return Vector2(_sim._spawn_positions[1]) + Vector2(0.0, 4.0)
		"enemy_builder":
			return _builder_spawn_position(_sim.ENEMY_TEAM) if _sim.base_loop_enabled else Vector2(_sim._spawn_positions[101]) + Vector2(0.0, -4.0)
	return Vector2(_sim._spawn_positions[1])


func _add_battalion(
	id: int,
	team: int,
	at: Vector2,
	display_name: String,
	object_id: String = sim.SOLDIER_OBJECT_ID,
	unit_type: String = sim.SOLDIER_HORDE_ID,
	command_points: int = -1,
	unit_rule_override: Dictionary = {},
	cached_build_cost: int = -1
) -> void:
	var _sim = sim
	var unit_rules_value: Variant = _sim._rules.get("unit_rules", {})
	var unit_rule: Dictionary = (
		unit_rule_override
		if not unit_rule_override.is_empty()
		else (
			(unit_rules_value as Dictionary).get(object_id, {}) as Dictionary
			if typeof(unit_rules_value) == TYPE_DICTIONARY
			else {}
		)
	)
	if unit_rule.is_empty():
		push_error("RetailSliceSim missing selected-pack unit rule for %s" % object_id)
		return
	var member_health = maxi(1, int(unit_rule.get("member_health", _sim._rules.get("member_health", 200))))
	var member_count := maxi(1, int(unit_rule.get("member_count", 0)))
	var maximum_health = member_health * member_count
	var member_health_values: Array[int] = []
	var member_attack_tokens: Array[int] = []
	var member_attack_start_ticks: Array[int] = []
	var member_attack_hit_ticks: Array[int] = []
	var member_attack_release_tokens: Array[int] = []
	var member_corpse_expire_ticks: Array[int] = []
	var member_target_indices: Array[int] = []
	var member_weapon_modes: Array[String] = []
	for _member_index in range(member_count):
		member_health_values.append(member_health)
		member_attack_tokens.append(0)
		member_attack_start_ticks.append(-1)
		member_attack_hit_ticks.append(-1)
		member_attack_release_tokens.append(0)
		member_corpse_expire_ticks.append(-1)
		member_target_indices.append(-1)
		member_weapon_modes.append(String(unit_rule.get("default_weapon_mode", "default")))
	# Only the sealed scenario noncombatant contract may preserve zero damage.
	# Legacy/malformed rules that merely omit or zero damage retain the historic
	# clamp to one and cannot smuggle a new semantic through a numeric sentinel.
	var noncombatant := bool(unit_rule.get("noncombatant", false))
	var member_damage := 0 if noncombatant else maxi(1, int(unit_rule.get("member_damage", 0)))
	var fallback_weapon := {
		"name": "legacy-default",
		"weapon_slot": String(unit_rule.get("default_weapon_slot", "")),
		"attack_range": float(unit_rule["attack_range"]),
		"attack_range_source": float(unit_rule["attack_range_source"]),
		"minimum_attack_range": float(unit_rule["minimum_attack_range"]),
		"minimum_attack_range_source": float(unit_rule["minimum_attack_range_source"]),
		"delay_between_shots_ms": float(unit_rule["delay_between_shots_ms"]),
		"pre_attack_delay_ms": float(unit_rule["pre_attack_delay_ms"]),
		"firing_duration_ms": float(unit_rule["firing_duration_ms"]),
		"attack_period_ticks": maxi(1, int(unit_rule["attack_period_ticks"])),
		"pre_attack_ticks": maxi(0, int(unit_rule["pre_attack_ticks"])),
		"firing_duration_ticks": maxi(0, int(unit_rule["firing_duration_ticks"])),
		"member_damage": member_damage,
	}
	if unit_rule.has("pre_attack_type"):
		fallback_weapon["pre_attack_type"] = String(unit_rule["pre_attack_type"])
	if unit_rule.has("pre_attack_random_amount_ms"):
		fallback_weapon["pre_attack_random_amount_ms"] = float(unit_rule["pre_attack_random_amount_ms"])
	for optional_weapon_field in [
		"projectile_object_id", "projectile_speed", "projectile_speed_source",
		"radius_damage_affects", "damage_components", "damage_type",
	]:
		if unit_rule.has(optional_weapon_field):
			fallback_weapon[optional_weapon_field] = unit_rule[optional_weapon_field]
	var weapon_modes: Dictionary = (unit_rule.get("weapon_modes", {}) as Dictionary).duplicate(true)
	if weapon_modes.is_empty():
		weapon_modes["default"] = fallback_weapon
	var battalion_damage := member_damage * member_count
	var committed_command_points := command_points
	if committed_command_points < 0:
		committed_command_points = _sim._production_rule_value(unit_type, "command_points_rule", "default_command_points")
	if String(unit_rule.get("category", "")) == "hero":
		_sim._has_hero_units = true
	_sim.entities[id] = {
		"id": id,
		"team": team,
		"name": display_name,
		"object_id": object_id,
		"position": at,
		"facing": Vector2.RIGHT if team == _sim.PLAYER_TEAM else Vector2.LEFT,
		"destination": at,
		"route": [],
		"route_cells": [],
		"route_ford": "",
		"order_sequence": 0,
		"state": "idle",
		"target_id": 0,
		"target_kind": "battalion",
		"health": maximum_health,
		"maximum_health": maximum_health,
		"member_maximum_health": member_health,
		"member_health": member_health_values,
		"damage": battalion_damage,
		"member_damage": member_damage,
		"speed": float(unit_rule["speed"]),
		"speed_source": float(unit_rule["speed_source"]),
		"current_speed": 0.0,
		# 0.0 is the UNAUTHORED sentinel, not a default: _step_route and
		# _retail_turn_rate_degrees both refuse to act on it and name the unit
		# in a push_error. A rule that omits these keys is a NAMED GAP already
		# reported at configuration time (today: the M3 trebuchet contract,
		# whose pack predates the CatapultLocomotor binding), so spawning must
		# not hard-index them and abort the whole spawn path.
		"acceleration": float(unit_rule.get("acceleration", 0.0)),
		"acceleration_source": float(unit_rule.get("acceleration_source", 0.0)),
		"turn_rate_degrees_per_second": float(unit_rule.get("turn_rate_degrees_per_second", 0.0)),
		"braking": float(unit_rule.get("braking", 0.0)),
		"braking_source": float(unit_rule.get("braking_source", 0.0)),
		"category": String(unit_rule.get("category", "")),
		"trample_cooldown": 0,
		# Flyers ignore ground navigation and cannot be hit by melee or bowled
		# over; knockdown_ticks > 0 means sprawled on the ground (no acting,
		# no orders) until the counter drains. Plain dict entries so both
		# serialize through snapshot()/state_hash() automatically.
		"flying": bool(unit_rule.get("is_flyer", false)),
		"knockdown_ticks": 0,
		"knocked_down": false,
		"attack_range": float(unit_rule["attack_range"]),
		"attack_range_source": float(unit_rule["attack_range_source"]),
		"minimum_attack_range": float(unit_rule["minimum_attack_range"]),
		"minimum_attack_range_source": float(unit_rule["minimum_attack_range_source"]),
		"vision_range": float(unit_rule["vision_range"]),
		"vision_range_source": float(unit_rule["vision_range_source"]),
		"damage_type": _sim._recorded_damage_type(object_id, unit_rule),
		"damage_components": (unit_rule.get("damage_components", _sim._unit_damage_components.get(object_id, [])) as Array).duplicate(true),
		"delay_between_shots_ms": float(unit_rule["delay_between_shots_ms"]),
		"pre_attack_delay_ms": float(unit_rule["pre_attack_delay_ms"]),
		"firing_duration_ms": float(unit_rule["firing_duration_ms"]),
		"attack_period_ticks": maxi(1, int(unit_rule["attack_period_ticks"])),
		"pre_attack_ticks": maxi(0, int(unit_rule["pre_attack_ticks"])),
		"firing_duration_ticks": maxi(0, int(unit_rule["firing_duration_ticks"])),
		"attack_cooldown": 0,
		"attack_windup": 0,
		"attack_sequence": 0,
		"continuous_fire_count": 0,
		"continuous_fire_expiration_tick": -1,
		"member_attack_tokens": member_attack_tokens,
		"member_attack_start_ticks": member_attack_start_ticks,
		"member_attack_hit_ticks": member_attack_hit_ticks,
		"member_attack_release_tokens": member_attack_release_tokens,
		"member_corpse_expire_ticks": member_corpse_expire_ticks,
		"corpse_expire_tick": -1,
		"member_target_indices": member_target_indices,
		"member_weapon_modes": member_weapon_modes,
		"weapon_modes": weapon_modes,
		"default_weapon_mode": String(unit_rule.get("default_weapon_mode", "default")),
		"close_weapon_mode": String(unit_rule.get("close_weapon_mode", "")),
		"close_weapon_switch_distance": float(unit_rule.get("close_weapon_switch_distance", 0.0)),
		"close_weapon_switch_distance_source": float(unit_rule.get("close_weapon_switch_distance_source", 0.0)),
		"unsupported_close_weapon": bool(unit_rule.get("unsupported_close_weapon", false)),
		"clip_size": int(unit_rule.get("clip_size", 0)),
		"clip_reload_time_ms": float(unit_rule.get("clip_reload_time_ms", 0.0)),
		"continuous_fire_one": int(unit_rule.get("continuous_fire_one", 0)),
		"continuous_fire_coast_ticks": int(unit_rule.get("continuous_fire_coast_ticks", 0)),
		"continuous_fire_rate_multiplier": float(unit_rule.get("continuous_fire_rate_multiplier", 1.0)),
		"active_weapon_mode": String(unit_rule.get("default_weapon_mode", "default")),
		# LockWeaponCreate applies at build completion and remains permanent.
		# The importer currently accepts only the exact retail PRIMARY corpus.
		"permanent_weapon_locks": Array(
			unit_rule.get("permanent_weapon_locks", [])
		).duplicate(),
		"stance": String((unit_rule.get("stances", {}) as Dictionary).get("default", "Battle")),
		"stance_contract": (unit_rule.get("stances", {}) as Dictionary).duplicate(true),
		"formation_mode": "Line",
		"formation_positions_base": Array(unit_rule["formation_positions"]).duplicate(),
		"order_kind": "",
		"is_builder": bool(unit_rule.get("is_builder", false)),
		"last_damage_tick": -1000000,
		"construction_id": 0,
		"member_count": member_count,
		"horde_id": String(unit_rule["horde_id"]),
		"formation_positions": Array(unit_rule["formation_positions"]).duplicate(),
		"retail_rule_provenance": (unit_rule["provenance"] as Dictionary).duplicate(true),
		"unit_type": unit_type,
		"command_points": committed_command_points,
		"production_producer_id": 0,
		"production_exit_start_tick": -1,
		"production_exit_duration_ticks": 0,
		"production_exit_progress": 1.0,
		"production_exit_origin": at,
		"production_exit_destination": at,
		"production_rally": at,
		# Compiled forge equipment recorded per horde (retail applies it per
		# battalion); spawned hordes of a tech-owning team arrive equipped.
		"applied_upgrades": {},
		"active_armor_upgrade": "",
		# Shared timed-modifier table: every buff/debuff/aura source (timed
		# ability buffs, leadership auras, fear) writes a keyed entry
		# {modifiers, expires_tick}; damage/attack/speed/vision/experience
		# calculations consult one helper family over this table. Keying by
		# source name gives retail stacking for free: same-named grants
		# overwrite (no stack), different names stack. Plain dict entries so
		# it serializes through snapshot()/state_hash() automatically.
		"timed_modifiers": {},
		# TOGGLE_WEAPONSET state: non-empty pins combat to that compiled
		# weapon-mode profile until toggled back (persistent across snapshots).
		"weapon_toggle_mode": "",
		# SpecialAbilityToggleMounted state: true while riding (mounted speed
		# and, when authored, the "mounted" weapon-mode profile are live).
		"mounted": false,
		# Honored only when the compiled unit rule authors it (fear-resistance
		# extraction is an importer follow-up; absent means not resistant).
		"fear_resistant": bool(unit_rule.get("fear_resistant", false)),
	}
	if cached_build_cost >= 0 and _sim._contracts_have_executable_refund_die(
		_sim._unit_module_contracts.get(unit_type, []) as Array
	):
		# Production supplies the queue item's final charged price after every
		# authored cost modifier; death must never recompute it from current rules.
		_sim.entities[id]["cached_build_cost"] = cached_build_cost
	# Optional sealed scenario policy. False is the historical combatant default
	# and must remain absent, otherwise every ordinary unit gains a meaningless
	# state byte and moves the frozen cross-platform pin.
	if noncombatant:
		_sim.entities[id]["noncombatant"] = true
	if String(unit_rule.get("default_command_set_id", "")) != "":
		_sim.entities[id]["default_command_set_id"] = String(unit_rule.get("default_command_set_id", ""))
		_sim.entities[id]["command_set_id"] = String(unit_rule.get("default_command_set_id", ""))
	# PreAttackType / random amount ride the compiled rule. Absent on the
	# synthetic pin harness (which never authors them) so the 3000-tick pin
	# stays put; `_step_member_attacks` defaults missing type to PER_SHOT.
	if unit_rule.has("pre_attack_type"):
		_sim.entities[id]["pre_attack_type"] = String(unit_rule["pre_attack_type"])
	if unit_rule.has("pre_attack_random_amount_ms"):
		_sim.entities[id]["pre_attack_random_amount_ms"] = float(unit_rule["pre_attack_random_amount_ms"])
	# Absent-unless-authored keeps melee and no-projectile state byte-identical.
	for optional_projectile_field in [
		"projectile_object_id", "projectile_speed", "projectile_speed_source",
		"radius_damage_affects",
	]:
		if unit_rule.has(optional_projectile_field):
			_sim.entities[id][optional_projectile_field] = unit_rule[optional_projectile_field]
	# ShroudClearingRange, the deshroud radius. Absent unless the compiled rule
	# authors one, exactly like the body scalars below and for the same reason:
	# a key that appears unconditionally would change every unit's snapshot and
	# move the 3000-tick pin. Absent means the fog pass falls back to vision and
	# says so (_shroud_clearing_radius).
	if unit_rule.has("shroud_clearing_range"):
		_sim.entities[id]["shroud_clearing_range"] = maxf(
			0.0, float(unit_rule["shroud_clearing_range"])
		)
		_sim.entities[id]["shroud_clearing_range_source"] = maxf(
			0.0, float(unit_rule.get("shroud_clearing_range_source", 0.0))
		)
	# Exact effective Object BountyValue. No field means no authored bounty and
	# must remain distinguishable from an authored zero in state/save/hash.
	if unit_rule.has("bounty_value"):
		_sim.entities[id]["bounty_value"] = maxi(0, int(unit_rule["bounty_value"]))
	if unit_rule.has("max_turn_without_reform_degrees"):
		_sim.entities[id]["max_turn_without_reform_degrees"] = float(
			unit_rule["max_turn_without_reform_degrees"]
		)
	if unit_rule.has("slow_turn_radius"):
		_sim.entities[id]["slow_turn_radius"] = maxf(0.0, float(unit_rule["slow_turn_radius"]))
	if unit_rule.has("fast_turn_radius"):
		_sim.entities[id]["fast_turn_radius"] = maxf(0.0, float(unit_rule["fast_turn_radius"]))
	if unit_rule.has("min_turn_speed"):
		_sim.entities[id]["min_turn_speed"] = clampf(float(unit_rule["min_turn_speed"]), 0.0, 1.0)
	if String(unit_rule.get("turn_rate_source", "")) != "":
		_sim.entities[id]["turn_rate_source"] = String(unit_rule["turn_rate_source"])
	for crush_int_key in ["crusher_level", "crushable_level", "crush_damage", "crush_revenge_damage"]:
		if unit_rule.has(crush_int_key):
			_sim.entities[id][crush_int_key] = int(unit_rule[crush_int_key])
	if unit_rule.has("crush_weapon_id"):
		_sim.entities[id]["crush_weapon_id"] = String(unit_rule["crush_weapon_id"])
	if unit_rule.has("crush_revenge_weapon_id"):
		_sim.entities[id]["crush_revenge_weapon_id"] = String(unit_rule["crush_revenge_weapon_id"])
	for crush_float_key in [
		"min_crush_velocity_percent",
		"crush_deceleration_percent",
		"crush_knockback",
	]:
		if unit_rule.has(crush_float_key):
			_sim.entities[id][crush_float_key] = float(unit_rule[crush_float_key])
	if typeof(unit_rule.get("formation_toggle")) == TYPE_DICTIONARY and not (unit_rule.get("formation_toggle") as Dictionary).is_empty():
		# Absent unless the unit's own CommandSet authored a
		# HORDE_TOGGLE_FORMATION button (commandbutton.ini). A unit with no
		# such button gets no key, and cannot be put into a formation.
		_sim.entities[id]["formation_toggle"] = (unit_rule.get("formation_toggle") as Dictionary).duplicate(true)
	if unit_rule.has("flanking_bonus"):
		_sim.entities[id]["flanking_bonus"] = float(unit_rule["flanking_bonus"])
	if unit_rule.has("wait_for_formation"):
		_sim.entities[id]["wait_for_formation"] = bool(unit_rule["wait_for_formation"])
	if typeof(unit_rule.get("kind_of")) == TYPE_ARRAY and not (unit_rule.get("kind_of") as Array).is_empty():
		_sim.entities[id]["kind_of"] = (unit_rule.get("kind_of") as Array).duplicate()
	# Body policy is optional authoritative state. Keep the key absent for
	# ordinary ActiveBody units so their snapshots/hashes do not change.
	if unit_rule.get("highlander_body") == true:
		_sim.entities[id]["highlander_body"] = true
	# Innate body scalars (damage taken, regeneration rate). Absent unless the
	# compiled rule authors them, so no retail unit's snapshot or authoritative
	# hash gains a byte.
	if unit_rule.has("innate_armor_scalar"):
		_sim.entities[id]["innate_armor_scalar"] = maxf(0.0, float(unit_rule["innate_armor_scalar"]))
	if unit_rule.has("auto_heal_multiplier"):
		_sim.entities[id]["auto_heal_multiplier"] = maxf(0.0, float(unit_rule["auto_heal_multiplier"]))
	# Optional object lifecycle policy. Its absence contributes no entity,
	# snapshot, or authoritative-hash bytes.
	if unit_rule.has("destroy_die"):
		_sim.entities[id]["destroy_die"] = Array(
			unit_rule["destroy_die"]
		).duplicate(true)
	if unit_rule.has("slow_death_fades"):
		_sim.entities[id]["slow_death_fades"] = Array(
			unit_rule["slow_death_fades"]
		).duplicate(true)
	if unit_rule.has("keep_object_die"):
		_sim.entities[id]["keep_object_die"] = bool(unit_rule.get("keep_object_die", false))
		_sim.entities[id]["keep_object_die_policy"] = (unit_rule.get("keep_object_die_policy", {}) as Dictionary).duplicate(true)
	if unit_rule.has("summon_auras"):
		_sim.entities[id]["summon_auras"] = Array(unit_rule["summon_auras"]).duplicate(true)
	# Optional AIUpdateInterface field slice. Keep the keys absent unless the
	# compiler authored the complete contract, so legacy/missing-field entity
	# snapshots and hashes remain byte-identical.
	if (
		unit_rule.has("auto_acquire_enabled")
		and unit_rule.has("auto_acquire_attack_buildings")
		and unit_rule.has("auto_acquire_while_stealthed")
	):
		_sim.entities[id]["auto_acquire_enabled"] = bool(unit_rule["auto_acquire_enabled"])
		_sim.entities[id]["auto_acquire_attack_buildings"] = bool(
			unit_rule["auto_acquire_attack_buildings"]
		)
		_sim.entities[id]["auto_acquire_while_stealthed"] = bool(
			unit_rule["auto_acquire_while_stealthed"]
		)
	# Optional AIUpdateInterface idle-rescan cadence. The one-shot jitter flag
	# is armed at spawn/reset; the next-check tick is created only by the first
	# eligible idle scan. Absent authoring preserves legacy bytes and RNG order.
	if (
		int(unit_rule.get("mood_attack_check_rate_ticks", 0)) > 0
		and unit_rule.has("auto_acquire_enabled")
		and unit_rule.has("auto_acquire_attack_buildings")
		and unit_rule.has("auto_acquire_while_stealthed")
	):
		_sim.entities[id]["mood_attack_check_rate_ticks"] = int(
			unit_rule["mood_attack_check_rate_ticks"]
		)
		_sim.entities[id]["mood_randomize_next_check"] = true
	# File the new battalion immediately: units spawned mid-tick (production
	# exits, summons) must be acquirable by battalions stepped later in the same
	# tick, exactly as the old full scans saw them.
	_sim._spatial_sync(_sim.entities[id])
	for tech_id_value in (_sim.team_upgrades.get(team, {}) as Dictionary).keys():
		_sim._apply_equipment_to_horde(_sim.entities[id], _sim._equipment_ids_for_forge_upgrade(String(tech_id_value)))
	if (
		String(unit_rule.get("category", "")) == "hero"
		or not (_sim._unit_ability_rules.get(String(_sim.entities[id].get("unit_type", "")), []) as Array).is_empty()
	):
		_sim._attach_hero_ability_state(_sim.entities[id])
	var has_registered_experience := not (
		_sim._unit_experience_rules.get(String(_sim.entities[id].get("unit_type", "")), {}) as Dictionary
	).is_empty()
	_sim._attach_experience_state(_sim.entities[id])
	_sim._attach_module_contracts(_sim.entities[id])
	# ExperienceLevelCreate is a creation module, independent of whether this
	# bounded summon leaf carries a complete ExperienceLevel progression table.
	# Ordinary unit rules omit this key and preserve their existing XP path.
	if int(unit_rule.get("creation_experience_rank", 0)) > 0:
		_sim.entities[id]["level"] = int(unit_rule["creation_experience_rank"])
		if not has_registered_experience:
			_sim._apply_experience_level_effects(
				_sim.entities[id],
				unit_rule.get("creation_experience_effects", {}) as Dictionary
			)
	_sim._record_hero_rank_attainment(_sim.entities[id])
	_sim._refresh_banner_carrier_state(_sim.entities[id])


