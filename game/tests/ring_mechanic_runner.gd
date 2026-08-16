extends SceneTree
## Synthetic compiled ring-contract integration gate. Drives two real sims
## through the retail One Ring lifecycle and compares authoritative hashes at
## every 30-tick barrier.

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const Adapter = preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const Watchdog = preload("res://tests/runner_watchdog.gd")
const GOLLUM := "NeutralGollum_RingHero"
const CARRIER := "fixture.carrier"
const HERO := "fixture.ring-hero"
var passed := 0
var failed := 0
var watchdog = Watchdog.new()


func _initialize() -> void:
	watchdog.start(self, "RING_MECHANIC_RUNNER")
	call_deferred("_run")


func _run() -> void:
	var route_doc := {
		"category": "hero",
		"registration": {"production": [{
			"surface": "hero-roster", "slot": 0, "rosterOrdinal": 1,
			"producerObjectId": "FixtureFortress", "commandSetId": "__engine__/BuildableRingHeroesMP",
			"commandId": "Command_ConstructRingHero", "sourceField": "BuildableRingHeroesMP",
			"prerequisites": ["Upgrade_RingHero", "Upgrade_FortressRingHero"],
		}]},
	}
	_check(Adapter.is_ring_hero_summon(route_doc), "compiled sourceField identifies ring hero")
	_test_stale_pack_prerequisites()
	_test_source_range_scaling()
	_test_unseeded_ring_state()
	_test_sim_waypoint_gate()
	_test_document_contract_precedence()
	_test_canonical_runtime_wrapper()
	var a = _make_sim(true)
	var b = _make_sim(true)
	_check(String(a.configuration_error) == "" and String(b.configuration_error) == "", "synthetic compiled contracts configure")
	for _i in range(5):
		_tick_pair(a, b)
	var gollum := int(a._ring_state().get("gollum_id", 0))
	_check(gollum != 0 and gollum == int(b._ring_state().get("gollum_id", 0)), "rule on spawns exactly one deterministic Gollum")
	_check(_living_gollums(a) == 1, "one Gollum per match")
	var start := Vector2((a.entities[gollum] as Dictionary).get("position", Vector2.ZERO))
	while a.tick_index < 60:
		_tick_pair(a, b)
	var wandered := int((a.entities[gollum] as Dictionary).get("ring_wander_count", 0)) > 0 \
		or Vector2((a.entities[gollum] as Dictionary).get("destination", start)) != start
	_check(wandered, "Gollum deterministically wanders")
	_kill_pair(a, b, gollum)
	_check(bool(a._ring_state().get("ring_active", false)), "Gollum death drops ring")
	var drop := Vector2(a._ring_state().get("ring_position", Vector2.ZERO))
	var carrier_a := _add_fixture_unit(a, 501, 0, drop, CARRIER)
	var carrier_b := _add_fixture_unit(b, 501, 0, drop, CARRIER)
	_tick_pair(a, b)
	_check(int(a._ring_state().get("carrier_id", 0)) == carrier_a, "eligible ground unit auto-picks ring")
	_check(a.entity_has_object_status(carrier_a, "HOLDING_THE_RING"), "carrier gains HOLDING_THE_RING")
	_kill_pair(a, b, carrier_a)
	_check(bool(a._ring_state().get("ring_active", false)), "carrier death re-drops ring")
	drop = Vector2(a._ring_state().get("ring_position", Vector2.ZERO))
	var second_a := _add_fixture_unit(a, 502, 0, drop, CARRIER)
	var second_b := _add_fixture_unit(b, 502, 0, drop, CARRIER)
	_tick_pair(a, b)
	var fortress_a := _fortress(a, 0)
	var fortress_b := _fortress(b, 0)
	a.script_teleport_entity(second_a, Vector2((a.structures[fortress_a] as Dictionary).position))
	b.script_teleport_entity(second_b, Vector2((b.structures[fortress_b] as Dictionary).position))
	_tick_pair(a, b)
	_check((a.team_upgrades[0] as Dictionary).has("Upgrade_RingHero"), "delivery grants PLAYER ring upgrade")
	_check(Array((a.structures[fortress_a] as Dictionary).completed_upgrades).has("Upgrade_FortressRingHero"), "delivery grants fortress OBJECT upgrade")
	_configure_ring_hero_purchase(a, fortress_a)
	_configure_ring_hero_purchase(b, fortress_b)
	_check(a.production_gate_unsatisfied(HERO, "fortress", Array((a.structures[fortress_a] as Dictionary).completed_upgrades), (a.team_upgrades[0] as Dictionary).keys()) == "", "fortress ring-hero prerequisites satisfiable")
	var queued_a: Dictionary = a.queue_unit(0, fortress_a, HERO)
	var queued_b: Dictionary = b.queue_unit(0, fortress_b, HERO)
	_check(bool(queued_a.get("ok", false)) and bool(queued_b.get("ok", false)), "10000-cost ring hero purchase queues")
	for _i in range(3000):
		_tick_pair(a, b)
	var hero_a := _find_unit(a, HERO)
	var hero_b := _find_unit(b, HERO)
	_check(hero_a != 0 and hero_b != 0 and int((a.entities[hero_a] as Dictionary).get("level", 0)) == 10, "300s purchase creates rank-10 hero")
	_check(not (a.team_upgrades[0] as Dictionary).has("Upgrade_RingHero") and not Array((a.structures[fortress_a] as Dictionary).completed_upgrades).has("Upgrade_FortressRingHero"), "creation strips both upgrades")
	_kill_pair(a, b, hero_a)
	_check(bool(a._ring_state().get("ring_active", false)), "ring hero death re-drops ring")
	_check(a.state_hash() == b.state_hash(), "two-sim full-loop final hash identical")
	var off = _make_sim(false)
	for _i in range(30): off.tick()
	_check(_living_gollums(off) == 0, "rule off spawns no Gollum")
	_configure_ring_hero_purchase(off, _fortress(off, 0))
	_check(String(off.queue_unit(0, _fortress(off, 0), HERO).get("reason", "")) == "ring-heroes-disabled", "rule off refuses ring hero purchase")
	var scripted_after_fallback = _make_sim(true)
	for _i in range(6): scripted_after_fallback.tick()
	var absorbed: int = scripted_after_fallback.spawn_script_object(GOLLUM, Sim.CREEP_TEAM, Vector2(20.0, 0.0))
	_check(absorbed == int(scripted_after_fallback._ring_state().get("gollum_id", 0)) and _living_gollums(scripted_after_fallback) == 1, "script Gollum after fallback is absorbed")
	var scripted_before_fallback = _make_sim(true)
	var scripted_id: int = scripted_before_fallback.spawn_script_object(GOLLUM, Sim.CREEP_TEAM, Vector2(20.0, 0.0))
	for _i in range(10): scripted_before_fallback.tick()
	_check(scripted_id > 0 and _living_gollums(scripted_before_fallback) == 1, "script Gollum before fallback prevents fallback duplicate")
	print("RING_MECHANIC_RESULT passed=%d failed=%d hash=%s" % [passed, failed, a.state_hash()])
	quit(0 if failed == 0 else 1)


func _make_sim(enabled: bool):
	var sim = Sim.new()
	var rules := {
		"enable_base_loop": true, "allow_ring_heroes": enabled,
		"spawn_initial_battalions": false, "starting_resources": 20000,
		"command_point_cap": 1000, "logic_random_seed": 8675309,
		"unit_rules": {GOLLUM: _unit_rule(GOLLUM, 200, 4.0), CARRIER: _unit_rule(CARRIER, 500, 0.0), HERO: _unit_rule(HERO, 5000, 6.0)},
		"ring_system": {
			"waypointFamily": "SpawnPoint_SkirmishGollum_", "spawnTeam": Sim.CREEP_TEAM,
			"modeToken": "ringheroes", "gollumObjectId": GOLLUM, "ringObjectId": "TheDroppedRing",
			"deliveryRange": 10.0,
		},
		"ring_runtime_documents": {
			GOLLUM: {"objectId": GOLLUM, "ringMechanic": {"gollum": {"wanderPercentage": 80, "detectionRange": 120.0, "fleeEnemyRange": 300.0, "fleeDistance": 800.0}}},
			"TheDroppedRing": {"objectId": "TheDroppedRing", "ringMechanic": {"ring": {"attachFilter": {"excludedObjectIds": [GOLLUM], "excludedCategories": ["flyer"]}, "scanRange": 10.0, "status": "HOLDING_THE_RING"}}},
		},
		"playable_structure_runtimes": {"FixtureFortress": {"objectId": "FixtureFortress", "ringMechanic": {"delivery": {"scanRange": 10.0}}}},
		"producer_kind_by_source_object": {"FixtureFortress": "fortress"},
	}
	sim.setup({}, rules)
	sim.ai_enabled = false
	for i in range(1, 9): sim.register_script_waypoint("SpawnPoint_SkirmishGollum_%d" % i, Vector2(i * 3.0, 0.0))
	return sim


func _test_stale_pack_prerequisites() -> void:
	var sim = Sim.new()
	var runtime_id := "bfme2.object.fixture-ring-hero"
	var rules := _rules_for(true)
	(rules["unit_rules"] as Dictionary)[runtime_id] = _unit_rule(runtime_id, 5000, 6.0)
	rules["playable_unit_runtimes"] = {"FixtureRingHero": _pack_ring_hero_document([])}
	rules["producer_kind_by_source_object"] = {"FixtureFortress": "fortress"}
	sim.setup({}, rules)
	sim.ai_enabled = false
	var fortress := _fortress(sim, 0)
	var refused: Dictionary = sim.queue_unit(0, fortress, runtime_id)
	_check(String(refused.get("reason", "")) == "missing-upgrade", "stale pack ring hero is unbuyable without synthesized prerequisites")
	(sim.team_upgrades[0] as Dictionary)["Upgrade_RingHero"] = true
	var completed: Array = (sim.structures[fortress] as Dictionary).completed_upgrades
	completed.append("Upgrade_FortressRingHero")
	completed.sort()
	var accepted: Dictionary = sim.queue_unit(0, fortress, runtime_id)
	_check(bool(accepted.get("ok", false)), "stale pack ring hero is buyable after both synthesized prerequisites")
	var off_rules := _rules_for(false)
	(off_rules["unit_rules"] as Dictionary)[runtime_id] = _unit_rule(runtime_id, 5000, 6.0)
	off_rules["playable_unit_runtimes"] = {"FixtureRingHero": _pack_ring_hero_document([])}
	off_rules["producer_kind_by_source_object"] = {"FixtureFortress": "fortress"}
	var off_sim = Sim.new()
	off_sim.setup({}, off_rules)
	var off_reason := String(off_sim.queue_unit(0, _fortress(off_sim, 0), runtime_id).get("reason", ""))
	_check(off_reason == "ring-heroes-disabled", "real pack-shaped rule-off purchase returns ring-heroes-disabled")
	_check(off_sim.spawn_script_object(GOLLUM, Sim.CREEP_TEAM, Vector2.ZERO) == -1, "rule off does not grant the ring creep-team id space")
	var compiled_rules := _rules_for(true)
	(compiled_rules["unit_rules"] as Dictionary)[runtime_id] = _unit_rule(runtime_id, 5000, 6.0)
	compiled_rules["playable_unit_runtimes"] = {"FixtureRingHero": _pack_ring_hero_document(["Upgrade_CompiledRingGate"])}
	compiled_rules["producer_kind_by_source_object"] = {"FixtureFortress": "fortress"}
	var compiled_sim = Sim.new()
	compiled_sim.setup({}, compiled_rules)
	_check(compiled_sim.production_gate_unsatisfied(runtime_id, "fortress", [], []) == "Upgrade_CompiledRingGate", "non-empty compiled ring prerequisites supersede synthesis")


func _test_source_range_scaling() -> void:
	var sim = _make_sim(true)
	sim._rules["source_map_transform_scale"] = 0.1
	var gollum := _add_fixture_unit(sim, 801, Sim.CREEP_TEAM, Vector2.ZERO, GOLLUM)
	sim._configure_ring_gollum(sim.entities[gollum] as Dictionary)
	_check(is_equal_approx(float((sim.entities[gollum] as Dictionary).ring_detection_range), 12.0), "Gollum detection range scales source 120 by map scale")
	_check(is_equal_approx(float((sim.entities[gollum] as Dictionary).ring_flee_enemy_range), 30.0), "Gollum flee enemy range scales source 300 by map scale")
	_check(is_equal_approx(float((sim.entities[gollum] as Dictionary).ring_flee_distance), 80.0), "Gollum flee distance scales source 800 by map scale")
	_add_fixture_unit(sim, 802, 0, Vector2(13.0, 0.0), CARRIER)
	sim._step_ring_gollum(gollum)
	_check(sim._stealth_active(sim.entities[gollum] as Dictionary), "detection does not trigger outside scaled 120 range")
	var pickup_sim = _make_sim(true)
	pickup_sim._rules["source_map_transform_scale"] = 0.1
	var pickup_state := pickup_sim._ring_state()
	pickup_state["gollum_spawned"] = true
	pickup_state["ring_active"] = true
	pickup_state["ring_position"] = Vector2.ZERO
	var carrier := _add_fixture_unit(pickup_sim, 803, 0, Vector2(2.0, 0.0), CARRIER)
	pickup_sim._step_ring_mechanic()
	_check(int(pickup_state.get("carrier_id", 0)) == 0, "ring pickup does not trigger outside scaled source 10 range")
	pickup_sim.script_teleport_entity(carrier, Vector2(0.5, 0.0))
	pickup_sim._step_ring_mechanic()
	_check(int(pickup_state.get("carrier_id", 0)) == carrier, "ring pickup triggers inside scaled source 10 range")


func _test_unseeded_ring_state() -> void:
	var sim = _make_sim(true)
	sim.script_surface_bag.erase("ring_mechanic")
	var state := sim._ring_state()
	state["persistence_probe"] = true
	_check(bool(sim._ring_state().get("persistence_probe", false)), "unseeded ring bag lazily stores its state dictionary")
	for _i in range(20): sim.tick()
	_check(_living_gollums(sim) == 1, "unseeded ring bag lazily stores state and spawns exactly one Gollum")


func _test_sim_waypoint_gate() -> void:
	var sim = Sim.new()
	sim.setup({}, _rules_for(false))
	sim._map_script_waypoints = {
		"SpawnPoint_SkirmishGollum_1": Vector2(1.0, 0.0),
		"OrdinaryScriptWaypoint": Vector2(2.0, 0.0),
	}
	sim.setup({}, _rules_for(false))
	_check(not sim.script_waypoints.has("SpawnPoint_SkirmishGollum_1") and sim.script_waypoints.has("OrdinaryScriptWaypoint"), "sim itself gates ring spawn waypoints when rule is off")


func _test_document_contract_precedence() -> void:
	var rules := _rules_for(true)
	(rules["ring_system"] as Dictionary)["gollumObjectId"] = "StaleSystemGollum"
	(rules["ring_system"] as Dictionary)["ringObjectId"] = "StaleSystemRing"
	(rules["ring_system"] as Dictionary)["carrierOffsetSource"] = [3.0, -7.0]
	var sim = Sim.new()
	sim.setup({}, rules)
	var presentation: Dictionary = sim.ring_presentation_contract()
	_check(sim._ring_gollum_object_id() == GOLLUM, "Gollum runtime document objectId supersedes system default")
	_check(String(presentation.get("object_id", "")) == "TheDroppedRing", "ring runtime document objectId supersedes system default")
	_check(Vector2(presentation.get("offset_source", Vector2.ZERO)) == Vector2(3.0, -7.0), "ring presentation offset comes from the contract")
	var late_structure := {"structure_kind": "fortress"}
	sim._mark_ring_delivery_structure(late_structure)
	_check(late_structure.has("ring_delivery"), "structure built mid-match receives ring delivery contract")


func _test_canonical_runtime_wrapper() -> void:
	# The selected RotWK Men pack ships this canonical importer shape. The
	# runtime must consume its registration directly; requiring a second,
	# hand-authored playableUnit copy of Gollum is the gap this regression pins.
	var sim = Sim.new()
	sim.setup({}, {
		"enable_base_loop": true, "allow_ring_heroes": true,
		"spawn_initial_battalions": false, "logic_random_seed": 8675309,
		"source_map_transform_scale": 0.1, "unit_rules": {},
		"ring_system": {
			"schema": "openbfme.ring-system-runtime", "schemaVersion": 0,
			"registration": {
				"objects": {
					"NeutralGollum": {
						"objectId": "NeutralGollum", "kindOf": ["HERO", "NEUTRALGOLLUM"],
						"body": {"kind": "ActiveBody", "maxHealth": 200},
						"locomotors": {"normal": 45, "wander": 32},
						"camouflage": {"type": "CAMOUFLAGE", "detectionRange": 120},
					},
					GOLLUM: {
						"objectId": GOLLUM, "parentObjectId": "NeutralGollum",
						"animalAI": {"fleeRange": 300, "fleeDistance": 800, "wanderPercentage": 80, "maxWanderDistance": 50},
						"ringMechanic": {"role": "ring-gollum", "dropsRingOnDeath": true},
					},
					"TheDroppedRing": {"objectId": "TheDroppedRing", "ringMechanic": {"attach": {"scanRange": 10, "parentStatus": "HOLDING_THE_RING", "filter": {}}}},
				},
				"system": {"modeToken": "ringheroes", "evaEvents": [], "ringHeroesByFaction": {"Men": ["ElvenGaladriel_RingHero"]}, "spawn": {"objectId": GOLLUM, "team": "PlyrCreeps", "waypointFamily": "SpawnPoint_SkirmishGollum_"}},
				"delivery": {}, "routes": {}, "upgrades": {}, "objectCreationLists": {}, "excludedObjects": [],
			},
		},
	})
	for i in range(1, 9):
		sim.register_script_waypoint("SpawnPoint_SkirmishGollum_%d" % i, Vector2(i * 3.0, 0.0))
	for _i in range(6):
		sim.tick()
	var gollum := int(sim._ring_state().get("gollum_id", 0))
	_check(String(sim.configuration_error) == "", "canonical importer ring runtime configures")
	_check(gollum > 0 and _living_gollums(sim) == 1, "canonical importer ring runtime spawns Gollum without a duplicate playableUnit")
	if gollum > 0:
		var row := sim.entities[gollum] as Dictionary
		_check(int(row.get("member_maximum_health", 0)) == 200 and is_equal_approx(float(row.get("speed", 0.0)), 4.5), "canonical Gollum health and speed come from retail descriptor fields")


func _rules_for(enabled: bool) -> Dictionary:
	return {
		"enable_base_loop": true, "allow_ring_heroes": enabled,
		"spawn_initial_battalions": false, "starting_resources": 20000,
		"command_point_cap": 1000, "logic_random_seed": 8675309,
		"unit_rules": {GOLLUM: _unit_rule(GOLLUM, 200, 4.0), CARRIER: _unit_rule(CARRIER, 500, 0.0), HERO: _unit_rule(HERO, 5000, 6.0)},
		"ring_system": {
			"waypointFamily": "SpawnPoint_SkirmishGollum_", "spawnTeam": Sim.CREEP_TEAM,
			"modeToken": "ringheroes", "gollumObjectId": GOLLUM, "ringObjectId": "TheDroppedRing",
			"deliveryRange": 10.0,
		},
		"ring_runtime_documents": {
			GOLLUM: {"objectId": GOLLUM, "ringMechanic": {"gollum": {"wanderPercentage": 80, "detectionRange": 120.0, "fleeEnemyRange": 300.0, "fleeDistance": 800.0}}},
			"TheDroppedRing": {"objectId": "TheDroppedRing", "ringMechanic": {"ring": {"attachFilter": {"excludedObjectIds": [GOLLUM], "excludedCategories": ["flyer"]}, "scanRange": 10.0, "status": "HOLDING_THE_RING"}}},
		},
		"playable_structure_runtimes": {"FixtureFortress": {"objectId": "FixtureFortress", "ringMechanic": {"delivery": {"scanRange": 10.0}}}},
		"producer_kind_by_source_object": {"FixtureFortress": "fortress"},
	}


func _pack_ring_hero_document(prerequisites: Array) -> Dictionary:
	return {
		"objectId": "FixtureRingHero", "category": "hero",
		"registration": {
			"composition": {"containerObjectId": "FixtureRingHero", "primaryMemberObjectId": "FixtureRingHero"},
			"production": [{
				"surface": "hero-roster", "slot": 0, "rosterOrdinal": 1,
				"producerObjectId": "FixtureFortress", "commandSetId": "__engine__/BuildableRingHeroesMP",
				"commandId": "Command_ConstructRingHero", "sourceField": "BuildableRingHeroesMP",
				"prerequisites": prerequisites,
			}],
			"simulation": {
				"displayName": "Ring Hero", "buildCost": 10000, "buildTimeSeconds": 300.0,
				"commandPoints": 0, "memberCount": 1, "memberHealth": 5000,
				"speed": 60.0, "visionRange": 200.0, "combat": {"damage": 100},
			},
		},
	}


func _unit_rule(id: String, health: int, speed: float) -> Dictionary:
	return {"horde_id": id, "member_count": 1, "member_health": health, "member_damage": 1,
		"speed": speed, "speed_source": speed * 10.0, "acceleration": 20.0, "acceleration_source": 200.0,
		"turn_rate_degrees_per_second": 360.0, "braking": 20.0, "braking_source": 200.0,
		"attack_range": 0.0, "attack_range_source": 0.0, "minimum_attack_range": 0.0, "minimum_attack_range_source": 0.0,
		"vision_range": 20.0, "vision_range_source": 200.0, "delay_between_shots_ms": 1000.0,
		"pre_attack_delay_ms": 0.0, "firing_duration_ms": 0.0, "attack_period_ticks": 10,
		"pre_attack_ticks": 0, "firing_duration_ticks": 0, "formation_positions": [Vector3.ZERO],
		"stances": {"default": "Battle", "cycleOrder": ["HoldGround", "Battle", "Aggressive"], "states": {"HoldGround": {}, "Battle": {}, "Aggressive": {}}},
		"category": "hero" if id == HERO else "infantry", "provenance": {}}


func _configure_ring_hero_purchase(sim, fortress: int) -> void:
	sim._unit_production_rules[HERO] = {"category": "hero", "producer_kind": "fortress", "producer_kinds": ["fortress"], "object_id": HERO, "display_name": "Ring Hero", "default_cost": 10000, "default_build_ticks": 3000, "default_command_points": 0, "is_ring_hero": true}
	sim._unit_prerequisites[HERO] = {"fortress": ["Upgrade_RingHero", "Upgrade_FortressRingHero"]}
	sim._unit_prerequisite_any_groups[HERO] = {"fortress": []}
	var production: Array = (sim.structures[fortress] as Dictionary).production
	if not production.has(HERO): production.append(HERO)


func _add_fixture_unit(sim, id: int, team: int, at: Vector2, object_id: String) -> int:
	sim._add_battalion(id, team, at, object_id, object_id, object_id, 0)
	return id


func _tick_pair(a, b) -> void:
	a.tick(); b.tick()
	if a.tick_index % 30 == 0:
		_check(a.state_hash() == b.state_hash(), "lockstep hash identical at tick %d" % a.tick_index)


func _kill_pair(a, b, id: int) -> void:
	a.script_kill_entity(id); b.script_kill_entity(id)


func _living_gollums(sim) -> int:
	var count := 0
	for id in sim.entity_ids():
		if sim._is_ring_gollum(sim.entities[id] as Dictionary) and int((sim.entities[id] as Dictionary).get("health", 0)) > 0: count += 1
	return count


func _fortress(sim, team: int) -> int:
	for id in sim.structure_ids(team):
		if String((sim.structures[id] as Dictionary).get("structure_kind", "")) == "fortress": return id
	return 0


func _find_unit(sim, unit_type: String) -> int:
	for id in sim.entity_ids():
		if String((sim.entities[id] as Dictionary).get("unit_type", "")) == unit_type and int((sim.entities[id] as Dictionary).get("health", 0)) > 0: return id
	return 0


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1; print("RING_MECHANIC PASS %s" % label)
	else:
		failed += 1; push_error("RING_MECHANIC FAIL %s" % label)
