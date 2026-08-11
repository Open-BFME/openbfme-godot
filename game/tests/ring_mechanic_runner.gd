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
