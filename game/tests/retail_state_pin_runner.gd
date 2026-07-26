extends SceneTree

## Cross-platform state pin for the GDScript authoritative simulation.
##
## Emits one line:  RETAIL_STATE_PIN ticks=<n> hash=<sha256>
##
## Purpose: the C# engine has CrossPlatformPinTests, which proves its
## fixed-point kernel is bit-identical on Windows and Linux. The GDScript
## simulation - which is the one that actually runs the game - has no such
## proof. Every existing determinism assertion compares two sims inside a
## single process, which cannot detect a platform-dependent result. CI runs
## this runner on Windows and Ubuntu and compares the emitted hash.
##
## The fixture below is DELIBERATELY FROZEN. It duplicates the setup in
## retail_lockstep_determinism_runner.gd rather than sharing it, because a pin
## whose scenario can drift is not a pin - any change to the shared fixture
## would silently change the hash and destroy the comparison's meaning. If this
## fixture must change, treat it as minting a new pin and say so explicitly.
##
## KNOWN COVERAGE GAP - read before trusting a green result.
## This scenario does NOT exercise Godot's AStarGrid2D. The simulation reaches
## pathfinding through an injected `route_provider` (retail_slice_sim.gd
## _query_route); this harness injects none, so routing takes the bounded
## direct fallback. AStarGrid2D uses float32 costs and an internal priority
## queue whose tie-breaking is an implementation detail of the engine, with no
## cross-platform or cross-version stability guarantee - it is the single most
## likely source of divergence and the one the project cannot repair itself.
## A green pin here therefore proves the simulation's own float math, ordering,
## and trigonometry agree across platforms. It proves nothing about pathing.
## Closing that gap needs a second pin backed by a real route_provider.
##
## What this DOES cover: entity and structure state, economy, combat, damage,
## stance and formation changes, production, construction, AI ticks, and the
## six transcendental sites (sin/cos/rotated/angle) reachable from them.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const PIN_TICKS := 3000
const SUBMIT_THROUGH_TICK := 1500


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim = _make_sim()
	var accepted := true
	for cmd in _scripted_log():
		if int(cmd["tick"]) <= SUBMIT_THROUGH_TICK:
			accepted = sim.submit_command(cmd) and accepted
	if not accepted:
		printerr("RETAIL_STATE_PIN FAIL a scripted command was rejected during submission")
		quit(1)
		return

	for _tick in range(1, PIN_TICKS + 1):
		sim.tick()

	print("RETAIL_STATE_PIN ticks=%d hash=%s" % [PIN_TICKS, sim.state_hash()])
	quit(0)


func _make_sim():
	var sim = SimScript.new()
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = true
	for structure_id in sim.structure_ids():
		if structure_id != 1003:
			sim.structures.erase(structure_id)
	for entity_id in sim.entity_ids():
		if not [1, 2, 3, 101].has(entity_id):
			sim.entities.erase(entity_id)
	sim.expansion_pads.clear()
	sim._ai_production_plan.clear()
	sim.force_ai_construction_complete()
	return sim


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 4000,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _unit_rule(horde_id: String, is_builder: bool) -> Dictionary:
	return {
		"horde_id": horde_id,
		"speed": 1.0,
		"speed_source": 10.0,
		"acceleration": 1.0,
		"acceleration_source": 10.0,
		"turn_rate_degrees_per_second": 180.0,
		"braking": 1.0,
		"braking_source": 10.0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 40.0,
		"vision_range_source": 400.0,
		"delay_between_shots_ms": 600.0,
		"pre_attack_delay_ms": 200.0,
		"firing_duration_ms": 200.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 2,
		"firing_duration_ticks": 2,
		"member_damage": 10,
		"member_health": 200,
		"member_count": 1,
		"formation_positions": [Vector3.ZERO],
		"provenance": {},
		"is_builder": is_builder,
	}


func _command(tick: int, seq: int, type: String, args: Dictionary, team: int = 0) -> Dictionary:
	return {"tick": tick, "team": team, "seq": seq, "type": type, "args": args}


func _scripted_log() -> Array[Dictionary]:
	return [
		_command(1, 1, "issue_move", {"ids": [1], "destination": Vector2(-20.0, -14.0)}),
		_command(3, 2, "issue_attack_move", {"ids": [2], "destination": Vector2(0.0, 14.0)}),
		_command(5, 3, "issue_toggle_stance", {"ids": [1]}),
		_command(7, 4, "issue_construct", {"ids": [3], "structure_kind": "farm", "position": Vector2(-28.0, -8.0)}),
		_command(9, 5, "queue_unit", {"producer": 1003, "unit_type": "bfme2.object.gondor-fighter-horde"}),
		_command(15, 6, "issue_attack", {"ids": [1], "target_id": 101}),
		_command(30, 8, "issue_stop", {"ids": [2]}),
		_command(30, 7, "issue_move", {"ids": [2], "destination": Vector2(-5.0, 12.0)}),
		_command(1600, 9, "issue_set_stance", {"ids": [1], "stance": "HoldGround"}),
		_command(2000, 10, "issue_set_formation", {"ids": [2], "formation": "Block"}),
		_command(2500, 11, "issue_stop", {"ids": [1, 2]}),
	]
