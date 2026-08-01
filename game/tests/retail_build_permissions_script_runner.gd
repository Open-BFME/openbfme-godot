extends SceneTree

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")

const PLAYER := "PlayerHuman"
const OBJECT_TYPE := "MenFortress"
const EXPECTED_CHECKS := 30

var passed := 0
var failed := 0
var worlds: Array = []


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var sim = _make_sim()
	var world = _make_world(sim)
	var before_resources: int = sim.resources_for_team(SimScript.PLAYER_TEAM)
	var before_structures: int = sim.structures.size()
	var before_id: int = sim._next_dynamic_structure_id
	var pristine_hash: String = sim.state_hash()
	_check("disallow is accepted", world.ai().set_buildings_allowed(PLAYER, OBJECT_TYPE, false))
	_check("permission changes authoritative hash", sim.state_hash() != pristine_hash)
	_check(
		"canonical snapshot records exact object type",
		sim.state_snapshot().get("building_permissions", [])
			== [[SimScript.PLAYER_TEAM, OBJECT_TYPE, false]]
	)
	var denied_dry: Dictionary = sim.issue_construct(
		[3], "fortress", Vector2(25.0, 25.0), true, SimScript.PLAYER_TEAM
	)
	_check("dry-run is denied", not bool(denied_dry.get("ok", false)))
	_check("dry-run reports building-disallowed", denied_dry.get("reason", "") == "building-disallowed")
	_check("dry-run preserves resources", sim.resources_for_team(0) == before_resources)
	_check("dry-run preserves structures", sim.structures.size() == before_structures)
	_check("dry-run preserves next id", sim._next_dynamic_structure_id == before_id)
	var denied_real: Dictionary = sim.issue_construct(
		[3], "fortress", Vector2(25.0, 25.0), false, SimScript.PLAYER_TEAM
	)
	_check("real construction is denied", not bool(denied_real.get("ok", false)))
	_check("real denial reports exact object type", denied_real.get("object_type", "") == OBJECT_TYPE)
	_check("real denial preserves resources", sim.resources_for_team(0) == before_resources)
	_check("real denial preserves structures", sim.structures.size() == before_structures)
	_check("real denial preserves next id", sim._next_dynamic_structure_id == before_id)
	var adopted = _make_sim()
	_check("permission snapshot restores", adopted.restore(sim.snapshot()))
	_check("restored authoritative hash matches", adopted.state_hash() == sim.state_hash())
	_check(
		"restored permission still denies",
		not bool(adopted.issue_construct(
			[3], "fortress", Vector2(25.0, 25.0), true, SimScript.PLAYER_TEAM
		).get("ok", false))
	)
	_check("allow is accepted", world.ai().set_buildings_allowed(PLAYER, OBJECT_TYPE, true))
	_check("allow returns hash to pristine", sim.state_hash() == pristine_hash)
	var allowed_dry: Dictionary = sim.issue_construct(
		[3], "fortress", Vector2(25.0, 25.0), true, SimScript.PLAYER_TEAM
	)
	_check("allow restores dry-run admission", bool(allowed_dry.get("ok", false)))
	var allowed_real: Dictionary = sim.issue_construct(
		[3], "fortress", Vector2(25.0, 25.0), false, SimScript.PLAYER_TEAM
	)
	_check("allow restores real construction", bool(allowed_real.get("ok", false)))
	sim.configure_expansion_rules({
		"synth_wall": {
			"cost": 100,
			"seconds": 1.0,
			"health": 500,
			"pad_kinds": ["corner", "side"],
			"name": "Synthetic Wall Hub",
			# Pack-driven expansion rules carry the converter's normalized
			# runtime id, while the script action names the retail source id.
			"object_id": "bfme2.object.synth-wall-hub",
		},
	})
	var fortress_id: int = sim.fortress_id(SimScript.PLAYER_TEAM)
	var expansion_resources: int = sim.resources_for_team(SimScript.PLAYER_TEAM)
	var expansion_id: int = sim._next_expansion_structure_id
	var expansion_pads: Array = sim.expansion_pad_states(fortress_id)
	_check(
		"case-varied expansion disallow is accepted",
		world.ai().set_buildings_allowed(PLAYER, "synthwallhub", false)
	)
	var denied_expansion: Dictionary = sim.issue_expansion_construct(
		SimScript.PLAYER_TEAM, fortress_id, "synth_wall"
	)
	_check("expansion construction is denied", not bool(denied_expansion.get("ok", false)))
	_check("expansion denial reports building-disallowed", denied_expansion.get("reason", "") == "building-disallowed")
	_check("expansion denial preserves resources", sim.resources_for_team(0) == expansion_resources)
	_check("expansion denial preserves next id", sim._next_expansion_structure_id == expansion_id)
	_check("expansion denial preserves pads", sim.expansion_pad_states(fortress_id) == expansion_pads)
	_check(
		"case-varied expansion allow is accepted",
		world.ai().set_buildings_allowed(PLAYER, "SynthWallHub", true)
	)
	_check(
		"allow restores expansion admission",
		bool(sim.issue_expansion_construct(
			SimScript.PLAYER_TEAM, fortress_id, "synth_wall"
		).get("ok", false))
	)
	adopted.setup({}, {})
	_add_builder(adopted)
	_check("setup clears permission state", adopted.building_permissions_by_team.is_empty())
	_check(
		"setup-cleared match admits construction",
		bool(adopted.issue_construct(
			[3], "fortress", Vector2(25.0, 25.0), true, SimScript.PLAYER_TEAM
		).get("ok", false))
	)
	_release_worlds()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("FAIL liveness ran=%d expected=%d" % [ran, EXPECTED_CHECKS])
	print("RETAIL_BUILD_PERMISSIONS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("FAIL %s" % name)


func _make_sim() -> RetailSliceSim:
	var sim: RetailSliceSim = SimScript.new()
	sim._rules = {
		"enable_base_loop": true,
		"spawn_initial_battalions": false,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 100000,
		"producer_kind_by_source_object": {OBJECT_TYPE: "fortress"},
		"unit_rules": {
			SimScript.BUILDER_OBJECT_ID: {
				"horde_id": SimScript.BUILDER_OBJECT_ID,
				"speed": 1.0,
				"speed_source": 10.0,
				"acceleration": 1.0,
				"acceleration_source": 10.0,
				"turn_rate_degrees_per_second": 180.0,
				"braking": 1.0,
				"braking_source": 10.0,
				"attack_range": 0.0,
				"attack_range_source": 0.0,
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
				"member_damage": 0,
				"member_health": 200,
				"member_count": 1,
				"formation_positions": [Vector3.ZERO],
				"provenance": {},
				"is_builder": true,
			},
		},
	}
	sim.setup({}, {})
	_add_builder(sim)
	sim.ai_enabled = false
	return sim


func _add_builder(sim: RetailSliceSim) -> void:
	sim._add_battalion(
		3,
		SimScript.PLAYER_TEAM,
		Vector2.ZERO,
		"Builder",
		SimScript.BUILDER_OBJECT_ID,
		SimScript.BUILDER_OBJECT_ID,
		0
	)


func _make_world(sim: RetailSliceSim) -> RetailSliceScriptWorld:
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	worlds.append(world)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_script_player(PLAYER)
	return world


func _release_worlds() -> void:
	for world in worlds:
		for facet in world._facets.values():
			facet.world = null
		world._facets.clear()
	worlds.clear()
