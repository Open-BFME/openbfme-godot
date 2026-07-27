extends SceneTree

## Regression runner for three state-boundary defects an adversarial review
## found in the base-building script surface (commit 1e7d6bf), each of which
## would detonate the moment the script layer is wired into a live match:
##
##   1. MATCH RESET LEAKS (retail_slice_sim.setup): expansion pads survived
##      setup() inside the HASHED state - a phantom pad key for the dead
##      dynamic structure an unpack minted (new in 1e7d6bf), and the OLDER
##      shape where an expansion built at a seeded fortress leaked its pad
##      occupancy (expansion_structure_id=9000) and the expansion id counter
##      through a reset. A player reaches this through reset_match()
##      (hud.restart_requested): a reused sim and a freshly built one
##      diverged at tick 0.
##
##   2. SNAPSHOT BOUNDARY (script unit references): the WP16 unit-reference
##      store was world-local and absent from snapshot()/state_hash(), so a
##      peer adopting a snapshot agreed on the hash and then diverged on the
##      first action that resolves a reference. The store now lives IN the
##      sim (script_unit_references, empty-is-absent), so save/load and
##      late-join reproduce it.
##
##   3. FLAG SHADOWING, BOTH DIRECTIONS: bind-time already refused a
##      reference that would shadow a base flag; configuring a flag whose
##      name an existing reference holds was the unguarded edge. Such a
##      configure is now refused whole (false + push_error, nothing applied).
##
## The object-type-identity packet added a fourth boundary subject, proven
## here in the same shape BEFORE it could ship with the class-1/class-2
## defects above:
##
##   4. OBJECT_TYPE_LIST STORES (script_object_type_lists): script-built
##      named type sets, mutated mid-match by OBJECTLIST_ADDOBJECTTYPE and
##      persisted by retail save games - so they live IN the sim, hashed and
##      snapshotted empty-is-absent, restored by restore(), and cleared by
##      setup() so a reused sim hashes identically to a fresh one.
##
## The fifth subject is the LARGEST instance of the class, the one e56a0d4
## left open by name:
##
##   5. THE SCRIPT ENGINE'S OWN STATE (script_env_state): SageScriptEnv's
##      counters, flags, timers, enable bits and tick counter - 64.7% of all
##      retail-AI call sites read or write exactly this. The env now attaches
##      to a sim-owned, team-keyed store (attach_script_env), hashed and
##      snapshotted through a canonical pruned view (zero counters and false
##      flags are absent; every level sorted), cleared by setup() IN PLACE so
##      attached envs stay wired, and restored the same way. Timers are
##      anchored to the interpreter tick that rides the same snapshot, so a
##      peer adopting mid-match expires them on the same absolute tick as the
##      peer that armed them. Without a sim the env keeps its historical
##      standalone backing, byte-for-byte.
##
## The sixth subject is the stream the retail AI's spell-list choice hangs
## on (c930b68's census: SET_RANDOM_COUNTER refused for want of it):
##
##   6. THE LOGIC RANDOM STREAM (_logic_random_state): retail's GameLogic
##      generator, sim-owned. Its six 32-bit words are the entire stream
##      state - stream POSITION changes outcomes, so the words are hashed
##      and snapshotted empty-is-absent (zero bytes until the first draw;
##      the frozen pin stands), cleared by setup(), and carried to adopting
##      peers, who continue the IDENTICAL sequence from the adopted words.
##
## Every fixture is SYNTHETIC; no retail install or content pack is required.
## NOTE: the shadowing and attach-refusal tests intentionally trigger
## push_error lines (marked EXPECTED ERROR below) - they are the loud
## refusals under test.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/retail_state_boundary_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")
const ExecutorScript = preload("res://src/script/script_executor.gd")
const EnvScript = preload("res://src/script/script_env.gd")
const ParamTypes = preload("res://src/script/script_param_types.gd")

const PLAYER := "PlayerOne"
const ENEMY := "EnemyOne"

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_reset_drops_phantom_unpacked_base_pads()
	_test_reset_drops_seeded_fortress_pad_occupancy()
	_test_references_live_inside_the_snapshot_boundary()
	_test_references_are_hash_inert_until_bound_and_reset_by_setup()
	_test_flag_shadowing_is_refused_in_both_directions()
	_test_object_type_lists_live_inside_the_snapshot_boundary()
	_test_object_type_lists_are_hash_inert_until_built_and_reset_by_setup()
	_test_script_env_state_lives_inside_the_snapshot_boundary()
	_test_script_env_state_is_hash_inert_until_used_and_reset_by_setup()
	_test_script_env_unknown_fields_cannot_escape_the_boundary()
	_test_script_env_degrades_without_a_sim_and_attach_refuses()
	_test_logic_random_stream_lives_inside_the_snapshot_boundary()
	_test_logic_random_stream_is_hash_inert_until_drawn_and_reset_by_setup()
	print("RETAIL_STATE_BOUNDARY_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


func _check(name: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("FAIL %s" % name)


# --- Fixtures (the script-world runner's synthetic harness) ----------------


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
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _unit_rule(SimScript.SOLDIER_HORDE_ID, false),
			SimScript.ARCHER_OBJECT_ID: _unit_rule(SimScript.ARCHER_OBJECT_ID, false),
			SimScript.TOWER_GUARD_OBJECT_ID: _unit_rule(SimScript.TOWER_GUARD_OBJECT_ID, false),
			SimScript.KNIGHT_OBJECT_ID: _unit_rule(SimScript.KNIGHT_OBJECT_ID, false),
			SimScript.BUILDER_OBJECT_ID: _unit_rule(SimScript.BUILDER_OBJECT_ID, true),
		},
	}


func _make_sim() -> RetailSliceSim:
	var sim: RetailSliceSim = SimScript.new()
	sim._rules = _harness_rules()
	sim.setup({}, {})
	sim.ai_enabled = false
	return sim


func _expansion_rules() -> Dictionary:
	return {
		"synth_pit": {
			"cost": 300,
			"seconds": 5.0,
			"health": 500,
			"pad_kinds": ["corner", "side"],
			"name": "Synth Pit",
			"object_id": "SynthPitType",
		},
	}


func _base_flags() -> Dictionary:
	return {
		"BASE_FLAG_1": {"position": Vector2(60.0, 60.0), "cost": 500},
		"BASE_FLAG_2": {"position": Vector2(70.0, -60.0), "cost": 500},
	}


func _configure_bases(sim: RetailSliceSim) -> void:
	sim.configure_expansion_rules(_expansion_rules())
	_check("fixture: base flags configure", sim.configure_unpackable_bases(_base_flags()))


func _make_world(sim: RetailSliceSim) -> RetailSliceScriptWorld:
	var world: RetailSliceScriptWorld = WorldScript.new(sim)
	world.bind_player(PLAYER, SimScript.PLAYER_TEAM)
	world.bind_player(ENEMY, SimScript.ENEMY_TEAM)
	world.bind_script_player(PLAYER)
	return world


func _sorted_pad_keys(sim: RetailSliceSim) -> Array:
	var keys: Array = sim.expansion_pads.keys()
	keys.sort()
	return keys


# --- Defect 1 (new shape): phantom pads from an unpacked base --------------


func _test_reset_drops_phantom_unpacked_base_pads() -> void:
	## configure -> unpack_base -> setup({},{}) must be byte-identical to a
	## configure -> setup twin. Pre-fix: the unpacked base's pads survived
	## under the dead dynamic structure id (3000) inside the hashed state,
	## and the stale keys blocked re-seeding.
	var reused := _make_sim()
	_configure_bases(reused)
	var unpacked: Dictionary = reused.unpack_base(0, "BASE_FLAG_1", true)
	_check("fixture: the flag unpacks", bool(unpacked.get("ok", false)))
	var minted_id := int(unpacked.get("structure_id", 0))
	_check("fixture: the unpack minted a dynamic structure id", minted_id == 3000)
	reused.setup({}, {})

	var fresh := _make_sim()
	_configure_bases(fresh)
	fresh.setup({}, {})

	_check(
		"reset drops the phantom pad key for the dead dynamic structure",
		not reused.expansion_pads.has(minted_id)
	)
	_check(
		"reset and fresh sims carry identical pad keys",
		_sorted_pad_keys(reused) == _sorted_pad_keys(fresh)
	)
	_check(
		"a reused sim hashes identically to a freshly built one after reset",
		reused.state_hash() == fresh.state_hash()
	)


# --- Defect 1 (pre-existing shape): seeded-fortress pad occupancy ----------


func _test_reset_drops_seeded_fortress_pad_occupancy() -> void:
	## The OLDER instance of the same class (predates 1e7d6bf): an expansion
	## built at a SEEDED fortress leaked expansion_structure_id=9000 into the
	## surviving pad row, and the expansion id counter leaked as 9001 - both
	## inside the hashed state.
	var reused := _make_sim()
	reused.configure_expansion_rules(_expansion_rules())
	var fortress: int = reused.fortress_id(0)
	var built: Dictionary = reused.issue_expansion_construct(0, fortress, "synth_pit")
	_check("fixture: an expansion builds at the seeded fortress", bool(built.get("ok", false)))
	_check(
		"fixture: the expansion took the first id in the 9000 range",
		int(built.get("structure_id", 0)) == 9000
	)
	reused.setup({}, {})

	var fresh := _make_sim()
	fresh.configure_expansion_rules(_expansion_rules())
	fresh.setup({}, {})

	var pad_row: Dictionary = (reused.expansion_pads.get(fortress, [{}]) as Array)[0]
	_check(
		"reset frees the pad the expansion occupied",
		int(pad_row.get("expansion_structure_id", -1)) == 0
	)
	_check(
		"reset returns the expansion id counter to its seed",
		reused._next_expansion_structure_id == fresh._next_expansion_structure_id
	)
	_check(
		"a reused sim hashes identically to a freshly built one after reset",
		reused.state_hash() == fresh.state_hash()
	)


# --- Defect 2: unit references inside the snapshot/hash boundary -----------


func _test_references_live_inside_the_snapshot_boundary() -> void:
	## Peer A unpacks binding AI_REF; peer B adopts A's snapshot and rebuilds
	## its world. Pre-fix the hashes agreed AND the identical follow-up action
	## diverged (A built, B refused "unbound"), splitting the sim hashes. Now
	## the reference rides the snapshot: both agree before and after.
	var sim_a := _make_sim()
	_configure_bases(sim_a)
	var world_a := _make_world(sim_a)
	_check(
		"fixture: peer A unpacks binding AI_REF",
		world_a.ai().base_unpack("BASE_FLAG_1", true, "AI_REF")
	)

	var sim_b := _make_sim()
	_check("fixture: peer B adopts A's snapshot", sim_b.restore(sim_a.snapshot()))
	var world_b := _make_world(sim_b)
	_check("adopted snapshot agrees on the state hash", sim_a.state_hash() == sim_b.state_hash())
	_check(
		"the adopting peer resolves the reference the minting peer bound",
		world_b.resolve_script_object("AI_REF") == world_a.resolve_script_object("AI_REF")
	)

	var built_a: bool = world_a.ai().build_base_building("SynthPitType", "AI_REF", "AI_PIT")
	var built_b: bool = world_b.ai().build_base_building("SynthPitType", "AI_REF", "AI_PIT")
	_check("the identical action succeeds on the minting peer", built_a)
	_check("the identical action succeeds on the adopting peer too", built_b)
	_check(
		"peers agree on the state hash after the identical action",
		sim_a.state_hash() == sim_b.state_hash()
	)


func _test_references_are_hash_inert_until_bound_and_reset_by_setup() -> void:
	## The empty-is-absent discipline (the state-pin property), plus the
	## match-reset direction: setup() clears the reference store with the
	## structures the references pointed at.
	var sim := _make_sim()
	_check(
		"an unbound sim snapshot carries NO script_unit_references key",
		not (bytes_to_var(sim.snapshot()) as Dictionary).has("script_unit_references")
	)
	var pristine := sim.state_hash()
	# Only the flag table here - expansion rules are durable CONFIG that
	# would legitimately survive setup and move the pristine comparison.
	_check(
		"fixture: base flags configure",
		sim.configure_unpackable_bases(_base_flags())
	)
	var configured := sim.state_hash()
	var world := _make_world(sim)
	_check(
		"fixture: an unpack binds AI_REF",
		world.ai().base_unpack("BASE_FLAG_1", true, "AI_REF")
	)
	_check(
		"binding a reference MOVES the hash (the state is not invisible)",
		sim.state_hash() != configured
	)
	_check(
		"the bound reference serializes",
		(bytes_to_var(sim.snapshot()) as Dictionary).has("script_unit_references")
	)
	sim.setup({}, {})
	sim.ai_enabled = false  # the harness disables AI; setup() re-enables it
	_check(
		"setup() clears the reference store with the structures it pointed at",
		sim.script_unit_references.is_empty()
	)
	_check(
		"a reference cleared by reset no longer resolves",
		world.resolve_script_object("AI_REF").is_empty()
	)
	sim.configure_unpackable_bases({})
	_check(
		"clearing the flag table after reset returns to the pristine hash exactly",
		sim.state_hash() == pristine
	)


# --- Defect 3: flag shadowing refused in both directions -------------------


func _test_flag_shadowing_is_refused_in_both_directions() -> void:
	var sim := _make_sim()
	sim.configure_expansion_rules(_expansion_rules())
	_check(
		"fixture: EARLY_FLAG configures alone",
		sim.configure_unpackable_bases({
			"EARLY_FLAG": {"position": Vector2(60.0, 60.0), "cost": 500},
		})
	)
	var world := _make_world(sim)
	# Direction 1 (bind after configure) - already guarded: a reference that
	# would shadow an existing flag refuses before the sim mutates.
	_check(
		"bind-time direction: a reference shadowing an existing flag refuses",
		not world.ai().base_unpack("EARLY_FLAG", true, "EARLY_FLAG")
	)
	# EXPECTED ERROR: the sim-level backstop refuses a flag name loudly.
	_check(
		"the sim-level bind backstop refuses a flag name too",
		not sim.bind_script_unit_reference(0, "EARLY_FLAG", 1234)
	)
	_check(
		"the refused backstop bind stored nothing",
		sim.script_unit_reference(0, "EARLY_FLAG") == 0
	)
	# Direction 2 (configure after bind) - the previously unguarded edge.
	_check(
		"fixture: LATE_FLAG binds as a unit reference before any such flag exists",
		world.ai().base_unpack("EARLY_FLAG", true, "LATE_FLAG")
	)
	var hash_before := sim.state_hash()
	# EXPECTED ERROR: the colliding configure is the loud refusal under test.
	_check(
		"configure-time direction: a flag colliding with a bound reference refuses",
		not sim.configure_unpackable_bases({
			"EARLY_FLAG": {"position": Vector2(60.0, 60.0), "cost": 500},
			"LATE_FLAG": {"position": Vector2(70.0, -60.0), "cost": 500},
		})
	)
	_check(
		"the refused configure applied NOTHING (fail closed, never half a table)",
		sim.state_hash() == hash_before and sim.unpackable_base_names() == ["EARLY_FLAG"]
	)
	_check(
		"the reference still resolves to its bind-time structure",
		String(world.resolve_script_object("LATE_FLAG").get("kind", "")) == "structure"
	)
	# The property the invariant protects: two worlds over byte-identical
	# sims answer the same. Pre-fix, the world that held the stale local
	# reference answered true while a rebuilt world saw the packed flag.
	var twin_sim := _make_sim()
	_check("fixture: a twin adopts the snapshot", twin_sim.restore(sim.snapshot()))
	var twin_world := _make_world(twin_sim)
	var mine: SageWorldQuery = world.players().can_build_at_base(PLAYER, "LATE_FLAG", "")
	var theirs: SageWorldQuery = twin_world.players().can_build_at_base(PLAYER, "LATE_FLAG", "")
	_check(
		"byte-identical sims answer can_build_at_base identically",
		mine.ok == theirs.ok and mine.value == theirs.value
	)
	# A non-colliding reconfigure still works: the refusal is the collision's,
	# not the subsystem's.
	_check(
		"a collision-free reconfigure is still accepted",
		sim.configure_unpackable_bases({
			"EARLY_FLAG": {"position": Vector2(60.0, 60.0), "cost": 500},
			"OTHER_FLAG": {"position": Vector2(70.0, -60.0), "cost": 500},
		})
	)


# --- Subject 4: OBJECT_TYPE_LIST stores inside the boundary ----------------


func _test_object_type_lists_live_inside_the_snapshot_boundary() -> void:
	## Peer A's scripts build a list; peer B adopts A's snapshot and rebuilds
	## its world. Both must agree on the hash AND resolve the list name to
	## the same members - the defect class this guards against is exactly
	## defect 2's: state that steers a later script answer sitting outside
	## what save/load reproduces (an unlisted store would make byte-equal
	## sims answer PLAYER_HAS_OBJECT_COMPARISON differently).
	var sim_a := _make_sim()
	var world_a := _make_world(sim_a)
	_check(
		"fixture: peer A's scripts build a list",
		world_a.meta().object_list_change("BOUNDARY_LIST", "SynthTypeA", true)
		and world_a.meta().object_list_change("BOUNDARY_LIST", "SynthTypeB", true)
	)

	var sim_b := _make_sim()
	_check("fixture: peer B adopts A's snapshot", sim_b.restore(sim_a.snapshot()))
	var world_b := _make_world(sim_b)
	_check("adopted snapshot agrees on the state hash", sim_a.state_hash() == sim_b.state_hash())
	_check(
		"the adopting peer resolves the list the minting peer built",
		sim_b.resolve_object_type_names("BOUNDARY_LIST")
		== sim_a.resolve_object_type_names("BOUNDARY_LIST")
		and sim_b.resolve_object_type_names("BOUNDARY_LIST") == ["SynthTypeA", "SynthTypeB"]
	)
	# The identical follow-up edit lands identically on both peers.
	var edited_a: bool = world_a.meta().object_list_change("BOUNDARY_LIST", "SynthTypeA", false)
	var edited_b: bool = world_b.meta().object_list_change("BOUNDARY_LIST", "SynthTypeA", false)
	_check("the identical edit succeeds on both peers", edited_a and edited_b)
	_check(
		"peers agree on the state hash after the identical edit",
		sim_a.state_hash() == sim_b.state_hash()
	)


func _test_object_type_lists_are_hash_inert_until_built_and_reset_by_setup() -> void:
	## The empty-is-absent discipline (the state-pin property) in all four
	## directions, plus the match-reset direction: setup() clears the store,
	## so a reused sim hashes identically to a freshly built one.
	var sim := _make_sim()
	_check(
		"an unbuilt sim snapshot carries NO script_object_type_lists key",
		not (bytes_to_var(sim.snapshot()) as Dictionary).has("script_object_type_lists")
	)
	var pristine := sim.state_hash()
	var world := _make_world(sim)
	_check(
		"fixture: a list member is added",
		world.meta().object_list_change("RESET_LIST", "SynthTypeA", true)
	)
	_check(
		"building a list MOVES the hash (the state is not invisible)",
		sim.state_hash() != pristine
	)
	_check(
		"the built list serializes",
		(bytes_to_var(sim.snapshot()) as Dictionary).has("script_object_type_lists")
	)
	_check(
		"removing the last member returns to the pristine hash exactly",
		world.meta().object_list_change("RESET_LIST", "SynthTypeA", false)
		and sim.state_hash() == pristine
	)
	# The reset direction: a list left standing at reset must not survive.
	world.meta().object_list_change("RESET_LIST", "SynthTypeA", true)
	sim.setup({}, {})
	sim.ai_enabled = false  # the harness disables AI; setup() re-enables it
	_check(
		"setup() clears the OBJECT_TYPE_LIST store",
		sim.script_object_type_lists.is_empty()
	)
	var fresh := _make_sim()
	_check(
		"a reused sim hashes identically to a freshly built one after reset",
		sim.state_hash() == fresh.state_hash()
	)


# --- Subject 5: the script engine's own state inside the boundary -----------
#
# Decoded-payload fixtures in the exact sage_scb.py shape (the executor
# runner's builders, trimmed to what these tests need).


func _argument(argument_type: int, integer: int = 0, real: float = 0.0, text: String = "") -> Dictionary:
	return {"argumentType": argument_type, "integer": integer, "real": real, "text": text}


func _condition(opcode: String, arguments: Array) -> Dictionary:
	return {
		"name": "Condition",
		"version": 6,
		"value": {
			"contentType": 0,
			"internalName": {"name": opcode, "wireTypeCode": 3},
			"arguments": arguments,
			"enabled": true,
			"inverted": false,
		},
	}


func _action(opcode: String, arguments: Array) -> Dictionary:
	return {
		"name": "ScriptAction",
		"version": 3,
		"value": {
			"contentType": 0,
			"internalName": {"name": opcode, "wireTypeCode": 3},
			"arguments": arguments,
			"enabled": true,
		},
	}


func _or_condition(conditions: Array) -> Dictionary:
	return {"name": "OrCondition", "version": 1, "value": {"records": conditions}}


func _script(name: String, records: Array, options: Dictionary = {}) -> Dictionary:
	return {
		"name": name,
		"comment": "",
		"conditionsComment": "",
		"actionsComment": "",
		"isActive": bool(options.get("isActive", true)),
		"deactivateUponSuccess": bool(options.get("deactivateUponSuccess", false)),
		"activeInEasy": true,
		"activeInMedium": true,
		"activeInHard": true,
		"isSubroutine": bool(options.get("isSubroutine", false)),
		"evaluationInterval": 0,
		"actionsFireSequentially": false,
		"loopActions": false,
		"loopCount": 0,
		"sequentialTargetType": 1,
		"sequentialTargetName": "",
		"scope": "ALL",
		"records": records,
	}


## The subject-5 fixture: every kind of env state the boundary must carry.
## "Arm" fires ONCE (tick 1): a counter, a flag, a 2.0 s timer (20 interpreter
## ticks at 10/s, so it expires on tick 21 - a LITERAL below, never
## recomputed), and its own enable bit via deactivateUponSuccess. "Pump"
## counts every tick. "Assault Gate" fires when the timer expires, then
## deactivates. "Call Sub" tallies through CALL_SUBROUTINE every tick after
## the assault flag rises - which also proves the env's subroutine seam (a
## weakref Callable, deliberately unserializable) survives attachment.
func _env_payloads() -> Array:
	var counter_c := _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, "c")
	var counter_war := _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, "war_chest")
	var counter_timer := _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, "assault")
	var counter_tally := _argument(ParamTypes.ARGUMENT_COUNTER_NAME, 0, 0.0, "tally")
	var flag_mustered := _argument(ParamTypes.ARGUMENT_FLAG_NAME, 0, 0.0, "mustered")
	var flag_launched := _argument(ParamTypes.ARGUMENT_FLAG_NAME, 0, 0.0, "assault_launched")
	var true_arg := _argument(ParamTypes.ARGUMENT_BOOLEAN, 1)
	return [
		_script("Arm", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("SET_COUNTER", [counter_war, _argument(ParamTypes.ARGUMENT_INTEGER, 3)]),
			_action("SET_FLAG", [flag_mustered, true_arg]),
			_action("SET_MILLISECOND_TIMER", [counter_timer, _argument(ParamTypes.ARGUMENT_REAL, 0, 2.0)]),
		], {"deactivateUponSuccess": true}),
		_script("Pump", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("INCREMENT_COUNTER", [_argument(ParamTypes.ARGUMENT_INTEGER, 1), counter_c]),
		]),
		_script("Assault Gate", [
			_or_condition([_condition("TIMER_EXPIRED", [counter_timer])]),
			_action("SET_FLAG", [flag_launched, true_arg]),
		], {"deactivateUponSuccess": true}),
		_script("Call Sub", [
			_or_condition([_condition("FLAG", [flag_launched, true_arg])]),
			_action("CALL_SUBROUTINE", [_argument(99, 0, 0.0, "Sub Tally")]),
		]),
		_script("Sub Tally", [
			_or_condition([_condition("CONDITION_TRUE", [])]),
			_action("INCREMENT_COUNTER", [_argument(ParamTypes.ARGUMENT_INTEGER, 1), counter_tally]),
		], {"isSubroutine": true}),
	]


func _make_attached_executor(sim: RetailSliceSim, world: RetailSliceScriptWorld) -> SageScriptExecutor:
	var executor: SageScriptExecutor = ExecutorScript.new(world)
	_check(
		"fixture: the env attaches to the sim",
		sim.attach_script_env(executor.env, SimScript.PLAYER_TEAM)
	)
	executor.load_script_payloads(_env_payloads())
	return executor


## The 2.0 s timer armed on tick 1 has 20 ticks remaining; env.advance()
## decrements it on ticks 2..21, so TIMER_EXPIRED first answers true on
## interpreter tick 21. Literals, per the timer-unit incident rule.
const TIMER_EXPIRY_TICK := 21
const ADOPTION_TICK := 7


func _test_script_env_state_lives_inside_the_snapshot_boundary() -> void:
	## Peer A's scripts write counters, flags, a timer and enable bits; peer B
	## adopts A's snapshot mid-match and rebuilds its executor. Both then run
	## the identical script stream: every read B's scripts make must come from
	## the state A minted, the hashes must agree on EVERY tick, and the timer
	## A armed must expire for both peers on the same absolute interpreter
	## tick. Pre-fix, every one of those bits lived outside the snapshot: B
	## restarted from zero while the hashes claimed agreement.
	var sim_a := _make_sim()
	var world_a := _make_world(sim_a)
	var executor_a := _make_attached_executor(sim_a, world_a)
	var pristine := sim_a.state_hash()
	executor_a.run_ticks(ADOPTION_TICK)
	_check(
		"running scripts MOVES the hash (the state is not invisible)",
		sim_a.state_hash() != pristine
	)
	_check(
		"the script-engine state serializes",
		(bytes_to_var(sim_a.snapshot()) as Dictionary).has("script_env_state")
	)

	var sim_b := _make_sim()
	_check("fixture: peer B adopts A's snapshot", sim_b.restore(sim_a.snapshot()))
	var world_b := _make_world(sim_b)
	var executor_b := _make_attached_executor(sim_b, world_b)
	_check("adopted snapshot agrees on the state hash", sim_a.state_hash() == sim_b.state_hash())
	_check(
		"the adopting peer reads the counters and flags the minting peer wrote",
		executor_b.env.counter("c") == ADOPTION_TICK
		and executor_b.env.counter("war_chest") == 3
		and executor_b.env.flag("mustered")
	)
	_check(
		# Armed on tick 1 at 20 remaining, decremented once per tick from tick
		# 2: after the adoption tick (7) it holds 14 - which is also expiry
		# minus adoption, since the last decrement (to 0) lands ON tick 21.
		"the adopting peer sees the armed timer at its surviving remainder",
		executor_b.env.timer_remaining_ticks("assault") == float(TIMER_EXPIRY_TICK - ADOPTION_TICK)
	)
	_check(
		"the adopting peer sees the enable bit deactivateUponSuccess wrote",
		not executor_b.env.is_script_enabled("Arm", true)
	)

	# Both peers now run the identical stream to past the expiry.
	var expiry_a := -1
	var expiry_b := -1
	var first_divergence := -1
	for _step in range(ADOPTION_TICK + 1, 31):
		executor_a.tick()
		executor_b.tick()
		if expiry_a < 0 and executor_a.env.flag("assault_launched"):
			expiry_a = executor_a.env.tick_index
		if expiry_b < 0 and executor_b.env.flag("assault_launched"):
			expiry_b = executor_b.env.tick_index
		if first_divergence < 0 and sim_a.state_hash() != sim_b.state_hash():
			first_divergence = executor_a.env.tick_index
	_check(
		"the timer expires on the same absolute tick for minter and adopter",
		expiry_a == TIMER_EXPIRY_TICK and expiry_b == TIMER_EXPIRY_TICK
	)
	_check("peers agree on the state hash on every subsequent tick", first_divergence < 0)
	_check(
		"the post-expiry subroutine tallies identically on both peers",
		executor_a.env.counter("tally") == executor_b.env.counter("tally")
		and executor_a.env.counter("tally") == 30 - TIMER_EXPIRY_TICK + 1
	)


func _test_script_env_state_is_hash_inert_until_used_and_reset_by_setup() -> void:
	## The empty-is-absent discipline (the state-pin property) plus the
	## CANONICAL property: state returned to its pristine VALUES returns to
	## the pristine HASH exactly (a zero counter and a false flag are
	## indistinguishable from absent by every read, so they contribute no
	## bytes), plus the match-reset and restore directions - both of which
	## must mutate the shared store IN PLACE so attached envs stay wired.
	var sim := _make_sim()
	var pristine := sim.state_hash()
	_check(
		"an untouched sim snapshot carries NO script_env_state key",
		not (bytes_to_var(sim.snapshot()) as Dictionary).has("script_env_state")
	)
	var env: SageScriptEnv = EnvScript.new()
	_check("fixture: a bare env attaches", sim.attach_script_env(env, SimScript.PLAYER_TEAM))
	_check("attaching alone does not move the hash", sim.state_hash() == pristine)
	env.set_counter("gold", 7)
	_check(
		"writing a counter MOVES the hash (the state is not invisible)",
		sim.state_hash() != pristine
	)
	_check(
		"the written state serializes",
		(bytes_to_var(sim.snapshot()) as Dictionary).has("script_env_state")
	)
	env.set_counter("gold", 0)
	_check(
		"a counter returned to zero returns to the pristine hash exactly",
		sim.state_hash() == pristine
	)
	env.set_flag("rallied", true)
	_check("writing a flag moves the hash", sim.state_hash() != pristine)
	env.set_flag("rallied", false)
	_check(
		"a flag returned to false returns to the pristine hash exactly",
		sim.state_hash() == pristine
	)
	# An EXPLICIT enable bit is state in BOTH polarities: absent means "the
	# authored default applies", so false must NOT prune to absent.
	env.set_script_enabled("Some Script", false)
	_check("an explicit false enable bit is hash-visible state", sim.state_hash() != pristine)
	env.set_timer_ticks("siege", 5.0)
	env.advance()

	# The reset direction: setup() clears the store IN PLACE.
	sim.setup({}, {})
	sim.ai_enabled = false  # the harness disables AI; setup() re-enables it
	_check("setup() clears the script-engine state", sim.script_env_state.is_empty())
	var fresh := _make_sim()
	_check(
		"a reused sim hashes identically to a freshly built one after reset",
		sim.state_hash() == fresh.state_hash()
	)
	# Identity preserved THROUGH the reset: the attached env still writes into
	# the sim's hashed store, not an orphan.
	env.set_counter("after_reset", 1)
	_check(
		"an env attached before reset still writes inside the boundary",
		not sim.script_env_state.is_empty() and sim.state_hash() != fresh.state_hash()
	)

	# Identity preserved through RESTORE the same way.
	var mid := sim.snapshot()
	env.set_counter("after_reset", 9)
	_check("fixture: the sim restores its own snapshot", sim.restore(mid))
	_check(
		"an env attached before restore reads the restored state",
		env.counter("after_reset") == 1
	)
	env.set_counter("after_reset", 2)
	var team_entry: Dictionary = (
		(bytes_to_var(sim.snapshot()) as Dictionary).get("script_env_state", {}) as Dictionary
	).get(SimScript.PLAYER_TEAM, {})
	_check(
		"an env attached before restore still writes inside the boundary",
		int((team_entry.get("counters", {}) as Dictionary).get("after_reset", 0)) == 2
	)


func _test_script_env_unknown_fields_cannot_escape_the_boundary() -> void:
	## Regression for the 0dce37e adversarial review: _script_env_state_view()
	## was a field WHITELIST (tick/counters/flags/timers/script_enabled), so a
	## write with an unrecognised shape was silently pruned from BOTH the hash
	## and the snapshot - invisible to the desync barrier and dropped on peer
	## adoption, the exact silent-fallback class e56a0d4 closed three instances
	## of. Harmless while the env writes only those five fields; lethal the day
	## a sixth is added without teaching the view. An unrecognised field must
	## be LOUD (reported once) and CARRIED - into the hash, the snapshot, and
	## an adopting peer - never dropped.
	var sim := _make_sim()
	var pristine := sim.state_hash()
	# A future env collection nobody taught the view about.
	sim.script_env_state[SimScript.PLAYER_TEAM] = {"ghost_collection": {"x": 1}}
	# EXPECTED ERROR (once): the boundary names the field it does not know.
	_check(
		"an unrecognised env field MOVES the hash (never silently pruned)",
		sim.state_hash() != pristine
	)
	_check("the unrecognised field is a counted view fault", sim.script_env_view_faults >= 1)
	_check(
		"the unrecognised field rides the snapshot",
		(
			((bytes_to_var(sim.snapshot()) as Dictionary).get("script_env_state", {}) as Dictionary)
			.get(SimScript.PLAYER_TEAM, {}) as Dictionary
		).has("ghost_collection")
	)
	# Peer adoption carries it: the adopting sim holds the same bytes and the
	# same hash (and reports the same fault loudly on ITS side).
	var twin := _make_sim()
	# EXPECTED ERROR (once, on the twin): the adopted field is unrecognised
	# there too.
	_check("fixture: a peer adopts the snapshot", twin.restore(sim.snapshot()))
	_check("the adopting peer carries the field (hash agreement)", twin.state_hash() == sim.state_hash())
	_check("the report happens once per field, not once per hash", sim.script_env_view_faults == 1)

	# The same discipline one level down: a timer row's extra field was pruned
	# by the remaining/running projection.
	var timer_sim := _make_sim()
	timer_sim.script_env_state[SimScript.PLAYER_TEAM] = {
		"timers": {"assault": {"remaining": 5.0, "running": true, "ghost": 42}},
	}
	var clean_sim := _make_sim()
	clean_sim.script_env_state[SimScript.PLAYER_TEAM] = {
		"timers": {"assault": {"remaining": 5.0, "running": true}},
	}
	# EXPECTED ERROR (once): the timer row's stray field is named.
	_check(
		"a timer row's unrecognised field moves the hash",
		timer_sim.state_hash() != clean_sim.state_hash()
	)
	# The known five fields still take the canonical path: writes through the
	# env prune back to the pristine hash exactly as before (the frozen-pin
	# property is untouched by the loud path).
	var canonical_sim := _make_sim()
	var canonical_pristine := canonical_sim.state_hash()
	var env: SageScriptEnv = EnvScript.new()
	_check("fixture: an env attaches", canonical_sim.attach_script_env(env, SimScript.PLAYER_TEAM))
	env.set_counter("gold", 3)
	env.set_counter("gold", 0)
	_check(
		"known fields keep the canonical prune (pristine hash returns exactly)",
		canonical_sim.state_hash() == canonical_pristine
	)
	_check("the canonical path reported no view fault", canonical_sim.script_env_view_faults == 0)


func _test_script_env_degrades_without_a_sim_and_attach_refuses() -> void:
	## The env must keep TODAY'S behaviour with no sim attached - the executor
	## and every handler runner construct it standalone - so a standalone
	## executor and a sim-attached one fed the identical payloads must agree
	## on the env's full order-stable snapshot at every step. And attachment
	## refuses loudly rather than guessing: null envs, unrostered teams,
	## double attachment, and an env that already holds local state.
	var standalone: SageScriptExecutor = ExecutorScript.new()
	standalone.load_script_payloads(_env_payloads())
	var sim := _make_sim()
	var world := _make_world(sim)
	var attached := _make_attached_executor(sim, world)
	var first_divergence := -1
	for step in range(1, 26):
		standalone.tick()
		attached.tick()
		if first_divergence < 0 and str(standalone.env.snapshot()) != str(attached.env.snapshot()):
			first_divergence = step
	_check("standalone and attached envs agree tick-for-tick", first_divergence < 0)
	_check(
		"the parity fixture is not vacuous",
		standalone.env.flag("assault_launched")
		and standalone.env.counter("tally") == 25 - TIMER_EXPIRY_TICK + 1
		and standalone.env.counter("c") == 25
	)
	# The subroutine seam survives attachment as a live Callable (the weakref
	# design from e6c9448); the tally above already proves it EXECUTES.
	_check(
		"both envs still hold a working subroutine runner",
		standalone.env.has_subroutine_runner() and attached.env.has_subroutine_runner()
	)

	# EXPECTED ERROR: null env refused loudly.
	_check("attaching a null env refuses", not sim.attach_script_env(null, SimScript.PLAYER_TEAM))
	# EXPECTED ERROR: unrostered team refused loudly.
	_check(
		"attaching to an unrostered team refuses",
		not sim.attach_script_env(EnvScript.new(), 99)
	)
	# EXPECTED ERROR: an env may attach exactly once.
	_check(
		"attaching an already-attached env refuses",
		not sim.attach_script_env(attached.env, SimScript.PLAYER_TEAM)
	)
	# EXPECTED ERROR: an env that already holds unhashed local history must
	# not smuggle it inside the boundary.
	var used: SageScriptEnv = EnvScript.new()
	used.set_counter("pre_attach", 1)
	_check(
		"attaching an env with local state refuses",
		not sim.attach_script_env(used, SimScript.PLAYER_TEAM)
	)
	_check(
		"the refused attaches stored nothing in the sim",
		not sim.script_env_state.has(99) and used.counter("pre_attach") == 1
	)


# --- Defect-class subject 6: the logic random stream ------------------------


func _test_logic_random_stream_lives_inside_the_snapshot_boundary() -> void:
	## Stream POSITION is outcome-bearing state: peer A draws five spell-list
	## choices, peer B adopts A's snapshot, and both must then continue the
	## IDENTICAL sequence with hashes agreeing on every draw. The expected
	## continuation is pinned from the independent reference implementation
	## (literals, not values recomputed from the code under test), so this
	## also fails if snapshotting perturbed the words. Pre-boundary, a stream
	## held outside the snapshot would replay from the seed on B: same hash,
	## different AI - the exact silent-divergence shape of subjects 2/4/5.
	var sim_a := _make_sim()
	var world_a := _make_world(sim_a)
	var pristine := sim_a.state_hash()
	for _draw in range(5):
		world_a.random_int(1, 3)
	_check("drawing MOVES the hash (stream position is not invisible)", sim_a.state_hash() != pristine)
	_check(
		"the stream words serialize",
		(bytes_to_var(sim_a.snapshot()) as Dictionary).has("logic_random_state")
	)

	var sim_b := _make_sim()
	_check("fixture: peer B adopts A's snapshot", sim_b.restore(sim_a.snapshot()))
	var world_b := _make_world(sim_b)
	_check("adopted snapshot agrees on the state hash", sim_a.state_hash() == sim_b.state_hash())
	var continuation_a: Array = []
	var continuation_b: Array = []
	var first_divergence := -1
	for step in range(6):
		continuation_a.append(world_a.random_int(1, 100))
		continuation_b.append(world_b.random_int(1, 100))
		if first_divergence < 0 and sim_a.state_hash() != sim_b.state_hash():
			first_divergence = step
	_check(
		"minter and adopter continue the identical pinned sequence",
		continuation_a == continuation_b
		and continuation_a == [63, 88, 23, 73, 74, 19]
	)
	_check("peers agree on the state hash after every draw", first_divergence < 0)


func _test_logic_random_stream_is_hash_inert_until_drawn_and_reset_by_setup() -> void:
	## The empty-is-absent discipline (the frozen-pin property) and both
	## reset directions: an undrawn stream contributes ZERO bytes; setup()
	## clears the words so a reused sim both hashes AND draws like a freshly
	## built one; restore of a pre-draw snapshot returns the stream to the
	## underived form (the adopter derives from the shared rules seed).
	var sim := _make_sim()
	var pristine := sim.state_hash()
	_check(
		"an undrawn sim snapshot carries NO logic_random_state key",
		not (bytes_to_var(sim.snapshot()) as Dictionary).has("logic_random_state")
	)
	var pre_draw := sim.snapshot()
	var world := _make_world(sim)
	var first_fresh := world.random_int(1, 3)
	_check("fixture: the first seed-0 [1,3] draw is the pinned 3", first_fresh == 3)
	_check("a draw moves the hash", sim.state_hash() != pristine)

	# Reset direction 1: setup() (the reset_match path routes through it).
	sim.setup({}, {})
	sim.ai_enabled = false  # the harness disables AI; setup() re-enables it
	_check("setup() clears the stream to the undrawn form", sim._logic_random_state.is_empty())
	var fresh := _make_sim()
	_check(
		"a reused sim hashes identically to a freshly built one after reset",
		sim.state_hash() == fresh.state_hash()
	)
	_check(
		"a reused sim REPLAYS the sequence from the seed after reset",
		world.random_int(1, 3) == first_fresh
	)

	# Reset direction 2: restoring a pre-draw snapshot.
	_check("fixture: the sim restores its pre-draw snapshot", sim.restore(pre_draw))
	_check(
		"restoring a pre-draw snapshot returns the stream to the undrawn form",
		sim._logic_random_state.is_empty() and sim.state_hash() == pristine
	)
	_check(
		"the restored sim derives from the shared rules seed on first draw",
		world.random_int(1, 3) == first_fresh
	)
