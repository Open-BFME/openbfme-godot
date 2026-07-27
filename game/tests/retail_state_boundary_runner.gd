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
## Every fixture is SYNTHETIC; no retail install or content pack is required.
## NOTE: the shadowing tests intentionally trigger two push_error lines
## (marked EXPECTED ERROR below) - they are the loud refusal under test.
##
## Invocation:
##   Godot_v4.7-stable_win64_console.exe --headless --path game \
##     --script res://tests/retail_state_boundary_runner.gd

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const WorldScript = preload("res://src/retail_slice/retail_slice_script_world.gd")

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
