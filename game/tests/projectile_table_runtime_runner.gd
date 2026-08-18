extends SceneTree
## Authoritative ordinary-projectile table snapshot/hash acceptance.

const Sim := preload("res://src/retail_slice/retail_slice_sim.gd")
const Watchdog := preload("res://tests/runner_watchdog.gd")

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "PROJECTILE_TABLE_RUNTIME", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	var sim = _empty_sim()
	sim.projectiles[70000] = {
		"id": 70000,
		"attacker_id": 1,
		"attacker_member_index": 0,
		"member_index": 0,
		"target_id": 2,
		"target_kind": "battalion",
		"origin": Vector2(5.0, 0.0),
		"launch_origin": Vector2.ZERO,
		"launch_tick": 0,
		"impact_tick": 2,
		"projectile_object_id": "FixtureRockProjectile",
		"amount": 200,
		"damage_components": [
			{"value": 100.0, "damage_type": "siege", "radius": 2.0, "damage_taper_off": 0.0},
			{"value": 100.0, "damage_type": "siege", "radius": 10.0, "damage_taper_off": 50.0},
		],
		"damage_type": "siege",
		"radius_damage_affects": "ENEMIES",
		"release_token": 1,
		"attack_sequence": 1,
		"bonus_nuggets": [],
	}
	sim._next_projectile_id = 70001
	var in_flight_hash := String(sim.state_hash())
	var snapshot: Variant = sim.snapshot()
	_check(sim.projectiles.size() == 1, "snapshot source carries one projectile in flight")

	var restored = _empty_sim()
	_check(restored.restore(snapshot), "snapshot with projectile table restores")
	_check(
		String(restored.state_hash()) == in_flight_hash
			and restored.projectiles == sim.projectiles
			and restored._next_projectile_id == 70001,
		"in-flight projectile state hash round-trips exactly"
	)
	for _index in 3:
		sim.winner = -1
		restored.winner = -1
		sim.tick()
		restored.tick()
	_check(
		sim.projectiles.is_empty()
			and restored.projectiles.is_empty()
			and String(restored.state_hash()) == String(sim.state_hash()),
		"restored projectile resolves on the same tick with the same hash"
	)

	print("PROJECTILE_TABLE_RUNTIME_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _empty_sim():
	var sim = Sim.new()
	sim.setup({}, {
		"spawn_initial_battalions": false,
		"faction_manifest": {"structure_kinds": ["fortress"]},
	})
	sim.ai_enabled = false
	sim.base_loop_enabled = false
	sim.entities.clear()
	sim.structures.clear()
	return sim


func _check(condition: bool, label: String) -> void:
	if condition:
		passed += 1
		print("PROJECTILE_TABLE_RUNTIME PASS %s" % label)
	else:
		failed += 1
		push_error("PROJECTILE_TABLE_RUNTIME_FAIL: %s" % label)
