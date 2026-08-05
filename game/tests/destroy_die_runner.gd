extends SceneTree

const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var all_policy := Adapter._resolved_destroy_die([{
		"ownerRole": "object",
		"module": "DestroyDie",
		"deathTypes": "ALL",
		"excludedDeathTypes": [],
	}])
	_check(
		"adapter_projects_destroy_die",
		all_policy == [{
			"owner_role": "object",
			"death_types": "ALL",
			"excluded_death_types": [],
		}]
	)
	var projected_rule := Adapter.simulation_rule(_document_with_destroy_die(
		[{
			"ownerRole": "object",
			"module": "DestroyDie",
			"deathTypes": "ALL",
			"excludedDeathTypes": [],
		}]
	))
	_check(
		"simulation_rule_carries_destroy_die",
		projected_rule.get("destroy_die", []) == all_policy
	)
	var normalized_rule := Adapter.normalized_unit_rule(projected_rule, 0.1)
	var executable_policy: Array = normalized_rule.get("destroy_die", [])
	_check(
		"normalized_rule_carries_validated_destroy_die",
		executable_policy == [{
			"owner_role": "object",
			"death_types": "ALL",
		}]
	)
	_check(
		"adapter_refuses_non_executable_filter",
		Adapter._resolved_destroy_die([{
			"ownerRole": "object",
			"module": "DestroyDie",
			"deathTypes": "ALL",
			"excludedDeathTypes": ["KNOCKBACK"],
		}]).is_empty()
	)

	var ordinary: RetailSliceSim = _make_sim([])
	_check(
		"unauthored_spawn_keeps_destroy_die_absent",
		not (ordinary.entities[101] as Dictionary).has("destroy_die")
			and not (
				(ordinary._authoritative_state().get("entities", {}) as Dictionary)
					.get(101, {}) as Dictionary
			).has("destroy_die")
	)
	ordinary._apply_member_damage(1, 0, 101, 99999, "battalion", 0, 0)
	_check(
		"ordinary_death_keeps_readable_corpse",
		ordinary.entities.has(101)
			and int((ordinary.entities[101] as Dictionary).get(
				"corpse_expire_tick", -1
			)) == ordinary.tick_index + Sim.CORPSE_LIFETIME_TICKS
	)

	var immediate: RetailSliceSim = _make_sim_from_descriptor(
		_document_with_destroy_die([{
			"ownerRole": "object",
			"module": "DestroyDie",
			"deathTypes": "ALL",
			"excludedDeathTypes": [],
		}])
	)
	_check(
		"descriptor_policy_reaches_spawned_entity",
		(immediate.entities[101] as Dictionary).get("destroy_die", [])
			== executable_policy
			and (
				(immediate._authoritative_state().get("entities", {}) as Dictionary)
					.get(101, {}) as Dictionary
			).get("destroy_die", []) == executable_policy
	)
	immediate._apply_member_damage(1, 0, 101, 99999, "battalion", 0, 0)
	_check(
		"destroy_die_erases_object_in_death_callback",
		not immediate.entities.has(101)
	)

	_check(
		"adapter_does_not_claim_cinematic_toppled_subset",
		Adapter._resolved_destroy_die([{
			"ownerRole": "object",
			"module": "DestroyDie",
			"deathTypes": "ALL",
			"excludedDeathTypes": ["TOPPLED"],
		}]).is_empty()
	)

	var primary_member_policy := Adapter._resolved_destroy_die([{
		"ownerRole": "primaryMember",
		"module": "DestroyDie",
		"deathTypes": "ALL",
		"excludedDeathTypes": [],
	}])
	_check(
		"primary_member_destroy_die_is_runtime_deferred",
		primary_member_policy.is_empty()
			and not Adapter.simulation_rule(_document_with_destroy_die([{
				"ownerRole": "primaryMember",
				"module": "DestroyDie",
				"deathTypes": "ALL",
				"excludedDeathTypes": [],
			}])).has("destroy_die")
	)

	var area: RetailSliceSim = _make_sim(executable_policy)
	area._apply_area_damage_to_battalion(101, 99999.0, "")
	_check(
		"area_damage_uses_shared_death_policy",
		not area.entities.has(101)
	)

	var summon_expiry: RetailSliceSim = _make_sim(executable_policy)
	summon_expiry._summon_despawn_ticks[101] = summon_expiry.tick_index
	summon_expiry._step_summon_despawns()
	_check(
		"summon_expiry_uses_shared_death_policy",
		not summon_expiry.entities.has(101)
	)

	var structure_weapon: RetailSliceSim = _make_sim(executable_policy)
	(structure_weapon.entities[101] as Dictionary)["position"] = Vector2(1.0, 0.0)
	structure_weapon.structures[900] = {
		"health": 100,
		"team": 0,
		"position": Vector2.ZERO,
		"construction_progress": 1.0,
		"attack": {
			"cooldown": 0,
			"range": 100.0,
			"damage": 99999.0,
			"period_ticks": 1,
			"pre_attack_ticks": 0,
		},
	}
	structure_weapon._step_structure_weapons()
	_check(
		"structure_weapon_uses_shared_death_policy",
		not structure_weapon.entities.has(101)
	)

	_test_summon_rule_does_not_leak_to_non_summon_spawn()
	_test_script_killed_summon_bookkeeping()
	_test_script_killed_hero_releases_identity()
	_test_combat_killed_faded_summon_keeps_corpse()
	_test_team_transfer_and_delete_move_then_release_commitment()
	_test_command_point_default_is_shared()
	_test_authored_fade_delay_replaces_the_zero_window()

	# ALL -TOPPLED remains compiler evidence only. These two retail carriers
	# are cinematic/unmaterialized, so this runner deliberately does not
	# manufacture an executable TOPPLED object.
	var compiler_only_toppled := [{
		"ownerRole": "object",
		"module": "DestroyDie",
		"deathTypes": "ALL",
		"excludedDeathTypes": ["TOPPLED"],
	}]
	_check(
		"toppled_shape_remains_compiler_evidence_only",
		compiler_only_toppled.size() == 1
			and Adapter._resolved_destroy_die(compiler_only_toppled).is_empty()
	)

	print("DESTROY_DIE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _test_authored_fade_delay_replaces_the_zero_window() -> void:
	## A matched DestroyDie used to erase the object on the same tick, so the
	## authored fade window was effectively 0. Retail authors it as
	## SlowDeathBehavior DestructionDelay on a `DeathTypes = NONE +FADED`
	## module; the compiler now carries those milliseconds and this is the join.
	var authored := [{
		"ownerRole": "object",
		"module": "SlowDeathBehavior",
		"moduleTag": "ModuleTag_Fade",
		"deathTypes": ["NONE", "+FADED"],
		"destructionDelayAuthored": true,
		"destructionDelayMs": 2500,
	}]
	var projected := Adapter._resolved_slow_death_fades(authored)
	_check(
		"authored_faded_fade_window_projects",
		projected.size() == 1
			and String(projected[0].get("death_types", "")) == "NONE"
			and (projected[0].get("included_death_types", []) as Array) == ["FADED"]
			and is_equal_approx(float(projected[0].get("destruction_delay_ms", 0.0)), 2500.0)
	)
	# No authored delay is an absence, never a 0: the row must not project at
	# all, so packs that predate the emission keep their existing behaviour.
	_check(
		"unauthored_fade_window_projects_nothing",
		Adapter._resolved_slow_death_fades([{
			"ownerRole": "object",
			"module": "SlowDeathBehavior",
			"deathTypes": ["NONE", "+FADED"],
			"destructionDelayAuthored": false,
		}]).is_empty()
	)
	# An unresolved define expression is recorded by the compiler but is not a
	# number, so it must not become an executable window either.
	_check(
		"unresolved_fade_expression_projects_nothing",
		Adapter._resolved_slow_death_fades([{
			"ownerRole": "object",
			"module": "SlowDeathBehavior",
			"deathTypes": ["NONE", "+FADED"],
			"destructionDelayAuthored": true,
			"destructionDelayUnresolvedExpression": "SOME_UNKNOWN_DEFINE",
		}]).is_empty()
	)
	# `ALL` slow deaths describe a multi-phase death sequence this simulation
	# does not run; claiming a window for it would advertise absent behaviour.
	_check(
		"all_death_type_slow_death_stays_evidence_only",
		Adapter._resolved_slow_death_fades([{
			"ownerRole": "object",
			"module": "SlowDeathBehavior",
			"deathTypes": ["ALL"],
			"destructionDelayAuthored": true,
			"destructionDelayMs": 4000,
		}]).is_empty()
	)

	var sim: RetailSliceSim = _make_sim([])
	var row: Dictionary = (sim.entities[101] as Dictionary).duplicate(true)
	_check(
		"entity_without_fade_rows_keeps_immediate_removal",
		sim._slow_death_fade_ticks(row, "FADED") == 0
	)
	row["slow_death_fades"] = projected
	var expected_ticks := maxi(1, roundi(2500.0 / 1000.0 / Sim.TICK_SECONDS))
	_check(
		"authored_fade_window_becomes_ticks",
		sim._slow_death_fade_ticks(row, "FADED") == expected_ticks
			and expected_ticks > 0
	)
	# The window belongs to the death type retail authored it for. A NORMAL
	# death must not inherit the FADED fade.
	_check(
		"fade_window_is_scoped_to_its_authored_death_type",
		sim._slow_death_fade_ticks(row, "NORMAL") == 0
	)


func _test_summon_rule_does_not_leak_to_non_summon_spawn() -> void:
	var sim: RetailSliceSim = _make_sim([])
	var spawned := _spawn_faded_summon(sim, 2)
	var ordinary_id := sim.spawn_script_object(Sim.SOLDIER_OBJECT_ID, 0, Vector2(5.0, 0.0))
	_check(
		"summon_scoped_faded_rule_does_not_leak_to_non_summon_spawn",
		spawned > 0 and ordinary_id > 0
			and not (sim.entities[ordinary_id] as Dictionary).has("destroy_die")
			and not sim._summon_despawn_ticks.has(ordinary_id)
	)
	sim.advance(3)
	_check(
		"non_summon_same_object_never_fades_at_summon_lifetime_tick",
		sim.entities.has(ordinary_id)
			and int((sim.entities[ordinary_id] as Dictionary).get("health", 0)) > 0
	)


func _test_script_killed_summon_bookkeeping() -> void:
	var sim: RetailSliceSim = _make_sim([])
	sim.base_loop_enabled = true
	var summoned_id := _spawn_faded_summon(sim, 3)
	var result := sim.script_kill_entity(summoned_id)
	_check(
		"script_killed_summon_erases_lifetime_and_releases_once",
		bool(result.get("ok", false))
			and not sim._summon_despawn_ticks.has(summoned_id)
			and bool((sim.entities[summoned_id] as Dictionary).get("command_points_released", false))
	)
	var expiry_events_before := _event_count(sim, "power.summon_expired")
	sim.advance(4)
	_check(
		"script_killed_summon_never_emits_bogus_expiry",
		_event_count(sim, "power.summon_expired") == expiry_events_before
	)


func _test_script_killed_hero_releases_identity() -> void:
	var sim: RetailSliceSim = _make_sim([])
	var hero_id := 1
	var hero_type := "FixtureProducedHero"
	var hero: Dictionary = sim.entities[hero_id]
	hero["category"] = "hero"
	hero["unit_type"] = hero_type
	sim._unit_production_rules[hero_type] = {"category": "hero"}
	sim._completed_hero_identities["0:%s" % hero_type] = true
	var result := sim.script_kill_entity(hero_id)
	_check(
		"script_killed_hero_releases_identity_and_is_retrainable",
		bool(result.get("ok", false))
			and not sim._completed_hero_identities.has("0:%s" % hero_type)
			and not sim.hero_unavailable(0, hero_type)
			and _event_count(sim, "hero.identity_released") == 1
	)


func _test_combat_killed_faded_summon_keeps_corpse() -> void:
	var sim: RetailSliceSim = _make_sim([])
	var summoned_id := _spawn_faded_summon(sim, 3)
	sim._apply_member_damage(1, 0, summoned_id, 99999, "battalion", 0, 0)
	var corpse: Dictionary = sim.entities.get(summoned_id, {}) as Dictionary
	_check(
		"combat_killed_faded_summon_uses_normal_corpse_path",
		not corpse.is_empty()
			and int(corpse.get("health", 1)) == 0
			and int(corpse.get("corpse_expire_tick", -1)) == sim.tick_index + Sim.CORPSE_LIFETIME_TICKS
			and not sim._summon_despawn_ticks.has(summoned_id)
	)
	var expiry_events_before := _event_count(sim, "power.summon_expired")
	sim.advance(4)
	_check(
		"combat_killed_faded_summon_does_not_expire_again",
		sim.entities.has(summoned_id)
			and _event_count(sim, "power.summon_expired") == expiry_events_before
	)


func _test_team_transfer_and_delete_move_then_release_commitment() -> void:
	var sim: RetailSliceSim = _make_sim([])
	sim.base_loop_enabled = true
	var entity_id := 1
	var row: Dictionary = sim.entities[entity_id]
	row["command_points"] = 25
	sim.team_command_points[0] = 25
	sim.team_command_points[1] = 0
	sim._summon_despawn_ticks[entity_id] = sim.tick_index + 100
	var moved := sim.set_entity_team(entity_id, 1)
	_check(
		"team_transfer_moves_command_point_commitment",
		bool(moved.get("ok", false))
			and int(sim.team_command_points.get(0, -1)) == 0
			and int(sim.team_command_points.get(1, -1)) == 25
	)
	var deleted := sim.delete_entity(entity_id)
	_check(
		"delete_erases_summon_registry_and_releases_new_team_commitment",
		bool(deleted.get("ok", false))
			and not sim.entities.has(entity_id)
			and not sim._summon_despawn_ticks.has(entity_id)
			and int(sim.team_command_points.get(1, -1)) == 0
	)


func _test_command_point_default_is_shared() -> void:
	var sim: RetailSliceSim = _make_sim([])
	sim.base_loop_enabled = true
	var entity_id := 1
	var row: Dictionary = sim.entities[entity_id]
	row.erase("command_points")
	sim._rules["soldier_command_points"] = 17
	sim.team_command_points[0] = 17
	sim.team_command_points[1] = 0
	var moved := sim.set_entity_team(entity_id, 1)
	var deleted := sim.delete_entity(entity_id)
	_check(
		"team_transfer_and_release_share_command_point_default",
		bool(moved.get("ok", false)) and bool(deleted.get("ok", false))
			and int(sim.team_command_points.get(0, -1)) == 0
			and int(sim.team_command_points.get(1, -1)) == 0
	)


func _spawn_faded_summon(sim: RetailSliceSim, lifetime_ticks: int) -> int:
	var base_rule: Dictionary = (
		(sim._rules.get("unit_rules", {}) as Dictionary).get(Sim.SOLDIER_OBJECT_ID, {}) as Dictionary
	).duplicate(true)
	base_rule["destroy_die"] = [{
		"owner_role": "object", "death_types": "NONE",
		"excluded_death_types": [], "included_death_types": ["FADED"],
	}]
	var spawned: Array = sim._spawn_summon_targets(0, Vector2.ZERO, [{
		"object_id": Sim.SOLDIER_OBJECT_ID,
		"count": 1,
		"lifetime_ticks": lifetime_ticks,
		"lifetime_death_type": "FADED",
		"rule": base_rule,
	}])
	return int(spawned[0]) if not spawned.is_empty() else -1


func _event_count(sim: RetailSliceSim, kind: String) -> int:
	var count := 0
	for event in sim.events:
		if String((event as Dictionary).get("kind", "")) == kind:
			count += 1
	return count


func _make_sim(
	destroy_die: Array[Dictionary], member_count: int = 1
) -> RetailSliceSim:
	var rules: Dictionary = {}
	for object_id in [
		Sim.SOLDIER_OBJECT_ID,
		Sim.ARCHER_OBJECT_ID,
		Sim.TOWER_GUARD_OBJECT_ID,
		Sim.KNIGHT_OBJECT_ID,
	]:
		rules[object_id] = _unit_rule(destroy_die, member_count)
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {
		"member_health": 100,
		"unit_rules": rules,
		"faction_manifest": {
			"structure_armor": _fixture_structure_armor(),
		},
	})
	sim.ai_enabled = false
	return sim


func _make_sim_from_descriptor(document: Dictionary) -> RetailSliceSim:
	var simulation := Adapter.simulation_rule(document)
	var rule := Adapter.normalized_unit_rule(simulation, 0.1)
	var object_id := String(simulation.get("object_id", ""))
	var rules: Dictionary = {}
	for roster_object_id in [
		Sim.SOLDIER_OBJECT_ID,
		Sim.ARCHER_OBJECT_ID,
		Sim.TOWER_GUARD_OBJECT_ID,
		Sim.KNIGHT_OBJECT_ID,
	]:
		rules[roster_object_id] = (
			rule if roster_object_id == object_id else _unit_rule([])
		)
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {
		"member_health": 100,
		"unit_rules": rules,
		"faction_manifest": {
			"structure_armor": _fixture_structure_armor(),
		},
	})
	sim.ai_enabled = false
	return sim


func _document_with_destroy_die(policies: Array) -> Dictionary:
	return {
		"objectId": "GondorFighter",
		"category": "infantry",
		"registration": {
			"composition": {
				"containerObjectId": "GondorFighter",
				"primaryMemberObjectId": "GondorFighter",
			},
			"production": [{
				"producerObjectId": "FixtureFactory",
				"commandSetId": "FixtureCommandSet",
				"commandId": "Command_BuildFixture",
				"surface": "command-socket",
				"slot": 1,
				"rosterOrdinal": 0,
				"prerequisites": [],
				"commandSetTransition": [],
			}],
			"simulation": {
				"displayName": "Fixture",
				"buildCost": 100,
				"buildTimeSeconds": 1.0,
				"commandPoints": 1,
				"memberCount": 1,
				"memberHealth": 100,
				"speed": 10.0,
				"visionRange": 100.0,
				"combat": {
					"attackRange": 10.0,
					"minimumAttackRange": 0.0,
					"delayBetweenShotsMs": 100.0,
					"preAttackDelayMs": 0.0,
					"firingDurationMs": 0.0,
					"damage": 10,
				},
				"movement": {
					"acceleration": 10.0,
					"braking": 10.0,
					"turnRateDegreesPerSecond": 180.0,
				},
				"formation": {
					"memberCount": 1,
					"positions": [{"x": 0.0, "y": 0.0}],
				},
				"fearResistant": false,
				"weaponModes": {},
				"destroyDie": policies,
			},
		},
	}


func _unit_rule(
	destroy_die: Array[Dictionary], member_count: int = 1
) -> Dictionary:
	var positions: Array[Vector3] = []
	for member_index in member_count:
		positions.append(Vector3(float(member_index), 0.0, 0.0))
	var rule := {
		"horde_id": Sim.SOLDIER_HORDE_ID,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.0,
		"attack_range_source": 10.0,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 10.0,
		"vision_range_source": 100.0,
		"delay_between_shots_ms": 100.0,
		"pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0,
		"attack_period_ticks": 1,
		"pre_attack_ticks": 0,
		"firing_duration_ticks": 0,
		"member_damage": 100,
		"member_health": 100,
		"member_count": member_count,
		"formation_positions": positions,
		"provenance": {},
	}
	if not destroy_die.is_empty():
		rule["destroy_die"] = destroy_die.duplicate(true)
	return rule


func _fixture_structure_armor() -> Dictionary:
	var armor := {}
	for kind_value in Sim.STRUCTURE_KINDS:
		armor[String(kind_value)] = {
			"set_id": "FixtureArmor",
			"damage_scalar": 1.0,
			"scalars": {"default": 1.0},
		}
	return armor


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("DESTROY_DIE_FAIL %s" % label)
