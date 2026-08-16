extends SceneTree

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_last_distinct_and_transitive_bind()
	_test_eligibility_traversal_and_due_tick()
	_test_new_spawn_door_retry()
	_test_parent_death_paths()
	print("SPAWN_RECLAIM_BINARY_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _test_last_distinct_and_transitive_bind() -> void:
	var sim = _sim()
	var owner := _owner(50, 0, Vector2.ZERO)
	sim.structures[50] = owner
	sim._attach_spawn_behavior_contract(owner, _spawn_contract(
		["MordorGoblinSwordsman_Slaved", "MordorGoblinArcher_Slaved"], 1, 1, true, false, 100
	))
	var sword := _candidate(10, "MordorGoblinSwordsman_Slaved", 0, Vector2(1, 0), 1)
	var archer := _candidate(11, "MordorGoblinArcher_Slaved", 0, Vector2(8, 0), 2)
	archer["destination"] = Vector2(77, 88)
	archer["slaved_update"] = _slave_policy()
	sim.entities[10] = sword
	sim.entities[11] = archer
	var before_position := Vector2(archer["position"])
	var before_health := int(archer["health"])
	var before_destination := Vector2(archer["destination"])
	var before_draws: int = sim.logic_random_draws
	sim._step_spawn_behaviors()
	var policy := owner["spawn_behavior"] as Dictionary
	_check("last_distinct_template_only", policy.get("spawned_ids", []) == [11])
	_check("earlier_template_candidate_remains_orphan", not sword.has("spawn_behavior_parent_id"))
	_check("reclaim_rebinds_producer", int(archer.get("spawn_behavior_parent_id", 0)) == 50)
	_check("reclaim_rebinds_slaved_master", int((archer["slaved_update"] as Dictionary).get("master_id", 0)) == 50)
	_check("slaved_rebind_consumes_one_rng_draw", sim.logic_random_draws == before_draws + 1)
	_check("reclaim_preserves_physical_state", Vector2(archer["position"]) == before_position and int(archer["health"]) == before_health and Vector2(archer["destination"]) == before_destination)
	_check("reclaim_does_not_advance_template", int(policy.get("spawn_serial", -1)) == 0)
	_check("reclaim_preserves_retail_spawn_count_quirk", int(policy.get("spawn_count", 99)) == -1)
	_check("reclaim_has_no_create_event", not sim.events.any(func(event: Dictionary) -> bool: return String(event.get("kind", "")) == "spawn_behavior.spawned"))
	_check("reclaim_event_is_explicit", sim.events.any(func(event: Dictionary) -> bool: return String(event.get("kind", "")) == "spawn_behavior.reclaimed" and int(event.get("target_id", 0)) == 11))
	var snapshot: PackedByteArray = sim.snapshot()
	var expected_hash: String = sim.state_hash()
	var restored = _sim()
	_check("snapshot_restores_reclaim_state", restored.restore(snapshot))
	_check("snapshot_hash_preserves_reclaim_state", restored.state_hash() == expected_hash)
	_check("snapshot_preserves_traversal_and_master", int((restored.entities[11] as Dictionary).get("retail_team_member_ordinal", -1)) == 2 and int(((restored.entities[11] as Dictionary).get("slaved_update", {}) as Dictionary).get("master_id", 0)) == 50)


func _test_eligibility_traversal_and_due_tick() -> void:
	var sim = _sim()
	var owner := _owner(60, 0, Vector2.ZERO)
	sim.structures[60] = owner
	sim._attach_spawn_behavior_contract(owner, _spawn_contract(["NeutralWolf"], 1, 0, true, false, 100))
	var policy := owner["spawn_behavior"] as Dictionary
	policy["next_spawn_tick"] = 10
	owner["spawn_behavior"] = policy
	var burning := _candidate(20, "NeutralWolf", 0, Vector2(1, 0), 20)
	burning["model_conditions"] = ["BURNINGDEATH"]
	var produced := _candidate(21, "NeutralWolf", 0, Vector2(2, 0), 21)
	produced["production_producer_id"] = 999
	var wrong_player := _candidate(22, "NeutralWolf", 1, Vector2(3, 0), 22)
	var boundary := _candidate(23, "NeutralWolf", 0, Vector2(10000, 0), 23)
	var other_team := _candidate(24, "NeutralWolf", 7, Vector2(5, 0), 24)
	other_team["retail_controlling_player"] = 0
	other_team["retail_team_prototype_ordinal"] = 0
	other_team["retail_team_instance_ordinal"] = 2
	var equal_later := _candidate(25, "NeutralWolf", 0, Vector2(5, 0), 25)
	equal_later["retail_team_prototype_ordinal"] = 1
	for row in [burning, produced, wrong_player, boundary, other_team, equal_later]:
		sim.entities[int((row as Dictionary)["id"])] = row
	sim.tick_index = 10
	var draws: int = sim.logic_random_draws
	sim._step_spawn_behaviors()
	_check("strict_due_tick_not_reached_at_equal", (owner["spawn_behavior"] as Dictionary).get("spawned_ids", []).is_empty())
	sim.tick_index = 11
	sim._step_spawn_behaviors()
	policy = owner["spawn_behavior"] as Dictionary
	_check("due_plus_one_reclaims", policy.get("spawned_ids", []) == [24])
	_check("burningdeath_rejected", not burning.has("spawn_behavior_parent_id"))
	_check("noninvalid_producer_rejected", not produced.has("spawn_behavior_parent_id"))
	_check("other_player_rejected", not wrong_player.has("spawn_behavior_parent_id"))
	_check("distance_boundary_is_strict", not boundary.has("spawn_behavior_parent_id"))
	_check("retail_team_traversal_wins_equal_distance", not equal_later.has("spawn_behavior_parent_id"))
	_check("same_player_other_team_retained", int(other_team.get("team", -1)) == 7)
	_check("nonslaved_reclaim_draws_no_rng", sim.logic_random_draws == draws)
	_check("legacy_deferred_receipt_remains_compatible", (policy.get("unsupported_semantics", []) as Array).has("deferred_binary_ambiguous:CanReclaimOrphans"))
	_check("binary_execution_receipt_is_present", String(policy.get("can_reclaim_runtime", "")) == "bfme2-rotwk-binary-proven")


func _test_new_spawn_door_retry() -> void:
	var sim = _sim()
	var owner := _owner(70, 0, Vector2.ZERO)
	owner["spawn_behavior_exit_door_available"] = false
	sim.structures[70] = owner
	sim._attach_spawn_behavior_contract(owner, _spawn_contract(["NeutralWolf"], 1, 0, true, false, 100))
	var policy := owner["spawn_behavior"] as Dictionary
	policy["next_spawn_tick"] = 4
	owner["spawn_behavior"] = policy
	sim.tick_index = 5
	sim._step_spawn_behaviors()
	_check("no_door_keeps_due_slot", (owner["spawn_behavior"] as Dictionary).get("spawned_ids", []).is_empty() and int((owner["spawn_behavior"] as Dictionary).get("next_spawn_tick", -1)) == 4)
	owner["spawn_behavior_exit_door_available"] = true
	sim._step_spawn_behaviors()
	policy = owner["spawn_behavior"] as Dictionary
	_check("door_retry_creates", (policy.get("spawned_ids", []) as Array).size() == 1)
	_check("new_create_advances_template", int(policy.get("spawn_serial", 0)) == 1)
	_check("new_create_updates_spawn_count", int(policy.get("spawn_count", 0)) == 1)
	_check("new_create_event_emitted", sim.events.any(func(event: Dictionary) -> bool: return String(event.get("kind", "")) == "spawn_behavior.spawned"))


func _test_parent_death_paths() -> void:
	var survivor_sim = _sim()
	var owner := _owner(80, 0, Vector2.ZERO)
	survivor_sim.structures[80] = owner
	survivor_sim._attach_spawn_behavior_contract(owner, _spawn_contract(["NeutralWolf"], 1, 1, true, false, 100))
	survivor_sim._step_spawn_behaviors()
	var child_id := int(((owner["spawn_behavior"] as Dictionary).get("spawned_ids", []) as Array)[0])
	var child := survivor_sim.entities[child_id] as Dictionary
	child["slaved_update"] = _slave_policy()
	survivor_sim.bind_slave(child_id, 80)
	var child_health := int(child["health"])
	owner["health"] = 0
	survivor_sim._step_spawn_behaviors()
	_check("nonrequired_child_survives", int(child["health"]) == child_health)
	_check("parent_death_clears_producer", not child.has("spawn_behavior_parent_id"))
	_check("parent_death_clears_master", int((child["slaved_update"] as Dictionary).get("master_id", -1)) == 0)
	var required_sim = _sim()
	var required_owner := _owner(90, 0, Vector2.ZERO)
	required_sim.structures[90] = required_owner
	required_sim._attach_spawn_behavior_contract(required_owner, _spawn_contract(["NeutralWolf"], 1, 1, false, true, 100))
	required_sim._step_spawn_behaviors()
	var required_id := int(((required_owner["spawn_behavior"] as Dictionary).get("spawned_ids", []) as Array)[0])
	required_owner["health"] = 0
	required_sim._step_spawn_behaviors()
	_check("required_spawner_kills_child", int((required_sim.entities[required_id] as Dictionary).get("health", 1)) == 0)


func _sim():
	var sim = Sim.new()
	sim.setup({}, {
		"game": "rotwk",
		"spawn_initial_battalions": false,
		"source_map_transform_scale": 1.0,
		"source_unit_scale": 1.0,
		"scenario_unit_runtimes": {
			"NeutralWolf": _unit_document("NeutralWolf"),
			"MordorGoblinSwordsman_Slaved": _unit_document("MordorGoblinSwordsman_Slaved"),
			"MordorGoblinArcher_Slaved": _unit_document("MordorGoblinArcher_Slaved"),
		},
	})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _owner(id: int, team: int, position: Vector2) -> Dictionary:
	return {"id": id, "team": team, "health": 100, "maximum_health": 100, "position": position, "completed_upgrades": []}


func _candidate(id: int, template: String, team: int, position: Vector2, ordinal: int) -> Dictionary:
	return {
		"id": id, "team": team, "retail_controlling_player": team,
		"retail_team_prototype_ordinal": 0, "retail_team_instance_ordinal": 0,
		"retail_team_member_ordinal": ordinal, "object_id": template, "unit_type": template,
		"health": 100, "maximum_health": 100, "member_health": [100],
		"position": position, "destination": position, "route": [], "state": "idle",
		"model_conditions": [], "production_producer_id": 0,
	}


func _slave_policy() -> Dictionary:
	return {
		"master_id": 0, "leash_range_source": 250.0, "guard_max_range_source": 40.0,
		"guard_wander_range_source": 75.0, "attack_range_source": 0.0,
		"guard_offset_source": Vector2.ZERO, "die_on_master_death": false,
		"mark_unselectable": false, "unsupported_semantics": [],
	}


func _spawn_contract(templates: Array, number: int, burst: int, reclaim: bool, require_spawner: bool, delay_ms: int) -> Dictionary:
	return {"module": "SpawnBehavior", "extraction": "typed", "fields": {
		"SpawnNumber": {"value": number}, "InitialBurst": {"value": burst},
		"SpawnReplaceDelay": {"milliseconds": delay_ms}, "SpawnTemplateName": {"value": templates},
		"CanReclaimOrphans": {"value": reclaim}, "SpawnedRequireSpawner": {"value": require_spawner},
	}}


func _unit_document(object_id: String) -> Dictionary:
	return {"objectId": object_id, "category": "monster", "registration": {
		"production": [], "composition": {"containerObjectId": object_id, "primaryMemberObjectId": object_id},
		"scenarioAdmission": {"kind": "authored-non-buildable", "role": "creature", "surfaces": ["lair-spawn"], "buildCommandExposed": false},
		"presentation": {}, "simulation": {"displayName": object_id, "buildCost": 0, "buildTimeSeconds": 1.0, "commandPoints": 1, "memberCount": 1, "memberHealth": 100, "speed": 90.0, "visionRange": 200.0, "movement": {"acceleration": 90.0, "braking": 90.0, "turnRateDegreesPerSecond": 360.0}, "formation": {"positions": [{"x": 0.0, "y": 0.0}]}, "combat": {"attackRange": 20.0, "delayBetweenShotsMs": 1500.0, "preAttackDelayMs": 500.0, "firingDurationMs": 500.0, "damage": 45}},
	}}


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("SPAWN_RECLAIM_BINARY_RUNTIME_FAIL " + label)
