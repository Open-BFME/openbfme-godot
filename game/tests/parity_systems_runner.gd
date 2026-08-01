extends SceneTree

## Focused proof for residual parity subsystems: FoW, threat, transport,
## wall upgrade, OCL CreateObjectDie hatch, mood, path.
##
## Invocation:
##   Godot --headless --path game -s res://tests/parity_systems_runner.gd

const Sim = preload("res://src/retail_slice/retail_slice_sim.gd")
const World = preload("res://src/retail_slice/retail_slice_script_world.gd")

const EXPECTED_CHECKS := 17

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim: RetailSliceSim = Sim.new()
	sim.setup({}, {
		"member_health": 100,
		"unit_rules": {
			Sim.SOLDIER_OBJECT_ID: _unit_rule(),
			Sim.ARCHER_OBJECT_ID: _unit_rule(),
			Sim.TOWER_GUARD_OBJECT_ID: _unit_rule(),
			Sim.KNIGHT_OBJECT_ID: _unit_rule(),
		},
		"faction_manifest": {"structure_armor": {}},
	})
	sim.ai_enabled = false
	_check("parity_attached", sim.parity != null)

	# FoW
	sim.parity.fog_reveal(0, Vector2(0, 0), 80.0, true)
	_check("fog_reveals_cell", sim.parity.fog_is_revealed(0, Vector2(10, 10)))
	sim.parity.fog_shroud(0, Vector2(0, 0), 80.0)
	_check("fog_permanent_survives_shroud", sim.parity.fog_is_revealed(0, Vector2(10, 10)))

	# Threat
	var eid := int(sim.living_ids(Sim.ENEMY_TEAM)[0])
	var origin: Vector2 = (sim.entities[eid] as Dictionary).get("position", Vector2.ZERO)
	var threat: float = float(sim.parity.threat_in_radius(sim, Sim.PLAYER_TEAM, origin, 5000.0))
	_check("threat_positive_with_enemies", threat > 0.0)

	# Path
	_check("path_open_by_default", sim.parity.can_path_between(Vector2.ZERO, Vector2(100, 0)))
	sim.parity.set_path_impassable(Vector2(50, 0), true)
	_check("path_blocked_by_impassable", not sim.parity.can_path_between(Vector2.ZERO, Vector2(100, 0)))

	# Wall upgrade
	sim.structures[500] = {
		"health": 100,
		"team": Sim.PLAYER_TEAM,
		"position": Vector2(0, 0),
		"kind": "castle_wall",
		"completed_upgrades": [],
	}
	var wall: Dictionary = sim.parity.apply_wall_upgrade(sim, 500, "Upgrade_TestTurret")
	_check("wall_upgrade_ok", bool(wall.get("ok", false)))
	_check(
		"wall_upgrade_on_structure",
		(sim.structures[500].get("completed_upgrades", []) as Array).has("Upgrade_TestTurret")
	)

	# Transport capacity
	sim.structures[501] = {
		"health": 100,
		"team": Sim.PLAYER_TEAM,
		"position": Vector2(5, 5),
		"kind": "barracks",
		"transport_capacity": 2,
	}
	var peid := int(sim.living_ids(Sim.PLAYER_TEAM)[0])
	var can: Dictionary = sim.parity.can_load_entity(sim, 501, peid)
	_check("transport_can_load", bool(can.get("ok", false)))
	sim.contain_entity(501, peid)
	_check("transport_passenger_count", sim.passenger_count(501) == 1)

	# Mood
	var row: Dictionary = sim.entities[peid] if sim.entities.has(peid) else {}
	if row.is_empty():
		row = sim.entities[int(sim.living_ids(Sim.ENEMY_TEAM)[0])]
	sim.parity.apply_attitude_mood(row, 3)
	_check("mood_attack_sets_stance_battle", String(row.get("stance", "")) == "Battle")
	_check("mood_sets_check_rate", int(row.get("mood_attack_check_rate_ticks", 0)) > 0)

	# OCL hatch
	sim.register_ocl_leaf("OCL_TestDie", {
		"createObjects": [{
			"objects": [Sim.SOLDIER_HORDE_ID],
			"fields": [{"key": "Count", "resolved": 1}],
		}],
	})
	var hatch: Dictionary = sim.hatch_create_object_die_entry({
		"team": Sim.PLAYER_TEAM,
		"position": Vector2(20, 20),
		"creation_list": "OCL_TestDie",
		"source_entity": 0,
	})
	_check("ocl_hatch_ok", bool(hatch.get("ok", false)), str(hatch))
	_check("ocl_hatch_spawned", (hatch.get("spawned", []) as Array).size() >= 1)

	# Meta time freeze freezes gameplay after scripts step (tick_index advances).
	sim.parity.time_frozen = true
	var t0 := sim.tick_index
	sim.advance(2)
	_check("time_frozen_still_advances_tick_index", sim.tick_index == t0 + 2)
	sim.parity.time_frozen = false

	_check("world_parity_present", sim.parity != null)
	_check("scoring_enabled_default", bool(sim.parity.scoring_enabled))

	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("PARITY_SYSTEMS FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("PARITY_SYSTEMS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _unit_rule() -> Dictionary:
	return {
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
		"member_damage": 25,
		"member_health": 100,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
	}


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		push_error("PARITY_SYSTEMS_FAIL %s %s" % [label, detail])
