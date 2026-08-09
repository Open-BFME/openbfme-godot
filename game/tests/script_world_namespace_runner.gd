extends SceneTree

## Proof runner for NAMED-OBJECT-NAMESPACE COMPLETENESS - the three-way split
## that lets the retail-slice script world answer retail's FALSE for names
## genuinely absent from the installed map.
##
## RETAIL SEMANTICS, SOURCED (GPL Generals ScriptEngine + the whale BFME
## reversal; full cites in retail_slice_script_world.gd, SliceUnits class
## comment): the engine's name table (ScriptEngine.h m_namedObjects) is built
## from objects PLACED ON THE MAP and is COMPLETE; getUnitNamed returns NULL
## for a name outside it and every named condition then answers FALSE - no
## throw, no log. Imported script LIBRARIES contribute scripts and teams,
## never objects, so the BASE_FLAG_N names the AI libraries poll are NULL on
## every retail rotwk map (byte-verified: no BASE_FLAG objects exist in any
## of the 128 retail rotwk maps) and the authored "Disable Flag N Check"
## self-disable fires off that false.
##
## The split under proof:
##   (a) name modeled by the sim              -> answered from sim state;
##   (b) name ABSENT from the map's declared
##       complete namedObjects table          -> retail's FALSE (not a gap);
##   (c) name IN the table but unmodeled, OR
##       no table declared                    -> REFUSES, fail-loud unchanged
##       (retail would answer from a real object in the middle class, so
##       false could be a confident wrong answer).
##
## Covers, in order:
##   1. the sim-side store (configure/declared/strcmp membership, hash
##      movement with twin agreement, empty-is-absent, snapshot adoption)
##   2. the adapter's three-way answer for every table-derived boolean member
##      (exists, was_created, was_destroyed, is_dying, is_owned_by), the
##      value-member refusals that stay refusals, and case (a) precedence
##   3. the PRODUCTION install seam: _install_map_scripts_document records
##      the schema-v1 world.namedObjects table when world.available is true,
##      declares nothing for available:false, and records nothing on a failed
##      (atomic) install
##
## Every fixture is SYNTHETIC (the script-world runner's harness rules); no
## retail install or content pack is required.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/script_world_namespace_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")
const ManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")

const PLAYER := "PlayerOne"
const ENEMY := "EnemyOne"
const PLAYER_TEAM_NAME := "teamPlayerOne"
const ENEMY_TEAM_NAME := "teamEnemyOne"

## LIVENESS. A GDScript runtime error aborts the enclosing function on the
## spot without propagating, so every `_check` after the error site never runs
## and `failed` never increments - an inert runner prints a zero-failure
## result and exits 0. Pinning the number of checks a healthy run makes turns
## that silent abort into a loud failure. Raise it deliberately when tests are
## added; never lower it to make a run go green.
const EXPECTED_CHECKS := 46

var passed := 0
var failed := 0
var worlds_to_release: Array = []


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_map_named_object_namespace()
	_test_map_namespace_install_seam()
	_release_worlds()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr("SCRIPT_WORLD_NAMESPACE FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions" % [ran, EXPECTED_CHECKS])
	print("SCRIPT_WORLD_NAMESPACE_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("FAIL %s" % name)


func _check_hit(name: String, query: SageWorldQuery, expected: Variant) -> void:
	_check("%s (answered)" % name, query.ok)
	if query.ok:
		_check("%s (value)" % name, query.value == expected)


func _check_refused(name: String, query: SageWorldQuery) -> void:
	_check("%s (refused)" % name, not query.ok and query.detail != "")


# --- Fixtures (the script-world runner's synthetic harness) -----------------


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


func _harness_rules() -> Dictionary:
	return {
		"enable_base_loop": true,
		"starting_resources": 10000,
		"ai_attack_delay_ticks": 100000,
		"retail_faction_sides": ManifestScript.retail_faction_sides(),
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _make_sim(roster: Array = []) -> RetailSliceSim:
	var sim: RetailSliceSim = SimScript.new()
	sim._rules = _harness_rules()
	if not roster.is_empty():
		sim.configure_team_roster(roster)
	sim.setup({}, {})
	sim.ai_enabled = false
	return sim


func _make_world(sim: RetailSliceSim) -> RetailSliceScriptWorld:
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	worlds_to_release.append(world)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_player(ENEMY, SimScript.ENEMY_TEAM)
	world.bind_team(PLAYER_TEAM_NAME, SimScript.PLAYER_TEAM)
	world.bind_team(ENEMY_TEAM_NAME, SimScript.ENEMY_TEAM)
	return world


func _configure_base_building(sim: RetailSliceSim) -> void:
	## Synthetic base flags, the script-world runner's shape: sim-modeled
	## names that deliberately do NOT appear in any declared map table.
	sim.configure_unpackable_bases({
		"BASE_FLAG_1": {"position": Vector2(60.0, 60.0), "cost": 500},
		"BASE_FLAG_2": {"position": Vector2(70.0, -60.0), "cost": 500},
		"BASE_FLAG_3": {"position": Vector2(-70.0, 60.0), "cost": 700},
	})


func _make_base_world(sim: RetailSliceSim) -> RetailSliceScriptWorld:
	_configure_base_building(sim)
	var world := _make_world(sim)
	world.bind_script_player(PLAYER)
	return world


func _release_worlds() -> void:
	for world_value in worlds_to_release:
		var world := world_value as RetailSliceScriptWorld
		world._release_facets()
		world.sim = null
	worlds_to_release.clear()


# --- 1/2. The store and the three-way answer --------------------------------


func _test_map_named_object_namespace() -> void:
	var sim := _make_sim()
	var world := _make_base_world(sim)
	# The twin carries the SAME world bindings (bind_player/bind_team register
	# hashed sim-side script-team state), so the only hash delta under test is
	# the declared namespace itself.
	var twin := _make_sim()
	var _twin_world := _make_base_world(twin)
	# (c, undeclared): the pre-namespace refusal contract, re-pinned here as
	# the baseline this test's declaration changes.
	_check("no map namespace is declared by default", not sim.map_named_object_namespace_declared())
	_check_refused(
		"without a declared namespace an unknown name refuses",
		world.units().exists("GHOST")
	)
	_check("fixture: twins agree before the namespace is declared", sim.state_hash() == twin.state_hash())
	# Declare the map's complete table: ONE name, which the sim does not model.
	sim.configure_map_named_object_namespace(["MapOnlyStructure"])
	_check("the declared namespace records", sim.map_named_object_namespace_declared())
	_check(
		"a declared namespace is authoritative state (hash moves)",
		sim.state_hash() != twin.state_hash()
	)
	twin.configure_map_named_object_namespace(["MapOnlyStructure"])
	_check(
		"identically declared twins hash identically",
		sim.state_hash() == twin.state_hash()
	)
	# (b): absent from the complete table -> each member's sourced NULL-unit
	# answer. exists/NAMED_NOT_DESTROYED (evaluateNamedUnitExists), was_created
	# (getUnitNamed != NULL), was_destroyed (didUnitExist: the name never had
	# an object), is_dying and is_owned_by are all FALSE for a NULL unit.
	_check_hit("units.exists answers retail false for a map-absent name", world.units().exists("GHOST"), false)
	_check_hit("units.was_created answers retail false for a map-absent name", world.units().was_created("GHOST"), false)
	_check_hit(
		"units.was_destroyed answers retail false (didUnitExist: never existed)",
		world.units().was_destroyed("GHOST"),
		false
	)
	_check_hit("units.is_dying answers retail false for a map-absent name", world.units().is_dying("GHOST"), false)
	_check_hit(
		"units.is_owned_by answers retail false for a map-absent name",
		world.units().is_owned_by("GHOST", PLAYER),
		false
	)
	_check_hit(
		"units.is_owned_by short-circuits the NULL unit before the player, like retail",
		world.units().is_owned_by("GHOST", "Nobody"),
		false
	)
	# strcmp case sensitivity cuts BOTH ways: a case-mismatched spelling of a
	# modeled name is simply another name the complete table does not hold.
	_check_hit(
		"a case-mismatched spelling is absent from the table (strcmp)",
		world.units().exists("base_flag_1"),
		false
	)
	# Value members have no retail FALSE to give - retail's false lives in the
	# condition that read the NULL pointer, and these report values. They keep
	# refusing (with the absence on the record) rather than inventing one.
	_check_refused("units.position still refuses a map-absent name", world.units().position("GHOST"))
	_check_refused("units.owner still refuses a map-absent name", world.units().owner("GHOST"))
	_check_refused("units.health_percent still refuses a map-absent name", world.units().health_percent("GHOST"))
	# (c, declared): a name the MAP authors but the sim does not model keeps
	# the fail-loud refusal - retail would answer from a real object here.
	_check_refused(
		"a declared name the sim does not model refuses",
		world.units().exists("MapOnlyStructure")
	)
	# (a): sim-modeled names answer regardless of the declared table - the
	# sim's namespace (flags, bound references) is runtime state layered OVER
	# the map's authored table, exactly like retail's live name table.
	_check_hit(
		"a sim-modeled flag answers although the declared table omits it",
		world.units().exists("BASE_FLAG_1"),
		true
	)
	_check("fixture: a reference binds beside the declared table", world.units().set_reference("AI_REF", "BASE_FLAG_1"))
	_check_hit(
		"a bound reference answers from the sim, not the table",
		world.units().was_created("AI_REF"),
		true
	)
	# The absent-name answer is a QUERY: repeated evaluation moves nothing.
	var before := sim.state_hash()
	world.units().exists("GHOST")
	world.units().is_owned_by("GHOST", PLAYER)
	world.units().was_destroyed("GHOST")
	_check("map-absent answers leave state_hash untouched", sim.state_hash() == before)
	# THE SNAPSHOT BOUNDARY: a peer adopting a snapshot must give the same
	# retail FALSE (and the same case-c refusal) as the peer that minted it.
	var adopted: RetailSliceSim = SimScript.new()
	adopted._rules = _harness_rules()
	adopted.setup({}, {})
	adopted.ai_enabled = false
	adopted.restore(sim.snapshot())
	_check("the declared namespace survives snapshot/restore", adopted.map_named_object_namespace_declared())
	var adopted_world := _make_world(adopted)
	adopted_world.bind_script_player(PLAYER)
	_check_hit(
		"the adopted peer answers the same retail false",
		adopted_world.units().exists("GHOST"),
		false
	)
	_check_refused(
		"the adopted peer keeps the same case-c refusal",
		adopted_world.units().exists("MapOnlyStructure")
	)


# --- 3. The production install seam -----------------------------------------


func _test_map_namespace_install_seam() -> void:
	var slice_script: GDScript = load("res://src/retail_slice/retail_vertical_slice.gd")
	var roster := [
		{"team": SimScript.PLAYER_TEAM, "faction": "men", "is_ai": false, "start_index": 0},
		{"team": SimScript.ENEMY_TEAM, "faction": "men", "is_ai": true, "start_index": 1},
	]
	var document := {
		"schema": "openbfme.map-scripts",
		"schemaVersion": 1,
		"world": {
			"available": true,
			"players": [
				{"index": 0, "name": ""},
				{"index": 1, "name": "Player_1"},
			],
			"namedObjects": [{
				"name": "Seam Present Structure",
				"typeName": "NotMaterialized",
				"godotPosition": [0.0, 0.0, 0.0],
				"godotYawRadians": 0.0,
				"originalOwner": "Player_1/Seam Team",
				"owner": "Player_1",
				"team": "Seam Team",
			}],
			"teams": [
				{"index": 0, "name": "teamPlayer_1", "owner": "Player_1", "objectCount": 0, "namedMembers": [], "units": []},
			],
		},
		"scripts": [{
			"playerIndex": 1,
			"payload": {"name": "seam-fixture", "isActive": true, "records": []},
		}],
	}
	var sim := _make_sim(roster)
	var slice = slice_script.new()
	slice.simulation = sim
	_check(
		"seam fixture: the v1 document installs",
		bool(slice._install_map_scripts_document(document, "namespace-seam-fixture"))
	)
	_check("the install seam declared the map namespace", sim.map_named_object_namespace_declared())
	var installed_world: RetailSliceScriptWorld = null
	if not slice.script_runtimes.is_empty():
		installed_world = (slice.script_runtimes[0] as Dictionary)["world"]
	if installed_world != null:
		# The census shape itself: BASE_FLAG_1 is absent from the map's table
		# and the sim configures no unpackable bases -> retail's FALSE, the
		# answer that lets the authored Disable Flag N Check scripts
		# self-disable exactly as retail does on flagless rotwk maps.
		_check_hit(
			"the installed world answers retail false for BASE_FLAG_1",
			installed_world.units().exists("BASE_FLAG_1"),
			false
		)
		_check_hit(
			"NAMED_OWNED_BY_PLAYER's read answers retail false for BASE_FLAG_1",
			installed_world.units().is_owned_by("BASE_FLAG_1", "Player_1"),
			false
		)
		_check_refused(
			"a map-authored name the sim does not materialize keeps refusing",
			installed_world.units().exists("Seam Present Structure")
		)
	_cleanup_slice_install(slice, sim)
	# available:false - the importer decoded no script world, so there is no
	# authoritative table and unknown names keep refusing.
	var unavailable := document.duplicate(true)
	(unavailable["world"] as Dictionary)["available"] = false
	var sim_unavailable := _make_sim(roster)
	var slice_unavailable = slice_script.new()
	slice_unavailable.simulation = sim_unavailable
	_check(
		"seam fixture: the available:false document installs",
		bool(slice_unavailable._install_map_scripts_document(unavailable, "namespace-unavailable-fixture"))
	)
	_check(
		"available:false declares no namespace",
		not sim_unavailable.map_named_object_namespace_declared()
	)
	var unavailable_world: RetailSliceScriptWorld = null
	if not slice_unavailable.script_runtimes.is_empty():
		unavailable_world = (slice_unavailable.script_runtimes[0] as Dictionary)["world"]
	if unavailable_world != null:
		_check_refused(
			"without a declared table BASE_FLAG_1 keeps refusing",
			unavailable_world.units().exists("BASE_FLAG_1")
		)
	_cleanup_slice_install(slice_unavailable, sim_unavailable)
	# Atomic failure: a document that dies AFTER normalization (malformed
	# payload) must record nothing. The refusal is silenced (report_errors
	# false), matching the retail-slice runner's atomic-failure convention.
	var late_failure := document.duplicate(true)
	(late_failure["scripts"] as Array)[0]["payload"] = {}
	var sim_failed := _make_sim(roster)
	var slice_failed = slice_script.new()
	slice_failed.simulation = sim_failed
	_check(
		"seam fixture: the malformed-payload document fails closed",
		not bool(slice_failed._install_map_scripts_document(late_failure, "namespace-late-failure", false))
	)
	_check(
		"a failed install declares no namespace",
		not sim_failed.map_named_object_namespace_declared()
	)
	_cleanup_slice_install(slice_failed, sim_failed)


func _cleanup_slice_install(slice, sim: RetailSliceSim) -> void:
	## Teardown for a slice used only as the install seam (the census/startup
	## runners' cleanup shape): release facet back-references, unregister
	## executors, then free the slice node.
	for runtime_value in slice.script_runtimes:
		var runtime := runtime_value as Dictionary
		var runtime_world = runtime.get("world")
		if runtime_world == null:
			continue
		for facet in runtime_world._facets.values():
			facet.world = null
		runtime_world._facets.clear()
	for team_value in sim.registered_script_executor_teams():
		sim.unregister_script_executor(int(team_value))
	slice.script_runtimes = []
	slice.simulation = null
	slice.free()
