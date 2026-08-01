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
