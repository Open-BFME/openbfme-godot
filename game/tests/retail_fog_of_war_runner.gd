extends SceneTree

## Proof runner for the retail shroud / fog-of-war model
## (src/retail_slice/retail_fog_of_war.gd).
##
## ORACLES. Every number this runner asserts comes from outside the module:
##
##   * PartitionCellSize = 40.0 - retail gamedata.ini. SAGE dimensions its
##     shroud grid off the heightmap playable border in partition cells of that
##     size (OpenSAGE PartitionCellManager.cs, which recovered the layout from
##     retail .sav files). The shroud grid is the partition grid: one signed
##     16-bit value per (cell, player).
##   * The tri-state encoding - OpenSAGE PartitionCellManager.DrawDiagnostic:
##     State < 0 means "N active lookers" (CLEAR), State == 0 means never seen
##     (SHROUDED), State == 1 means seen-and-remembered (FOGGED). Nothing else
##     is legal. This module keeps the same three states with an explicit
##     look-count plus an explored bit rather than one packed short, because a
##     packed short buys nothing in GDScript and hides the transition.
##   * ShroudAlpha = 0, FogAlpha = 127, ClearAlpha = 255 - retail gamedata.ini,
##     and the convention is INVERTED: 0 is opaque, 255 is clear.
##   * The world scale 0.02649232738129 is the slice's own source->sim transform
##     (retail_map_data.gd local_transform_scale), so a retail VisionRange in
##     source units becomes a sim-space radius by multiplying by it.
##
## THE DEFECT THIS RUNNER WAS WRITTEN TO CATCH FIRST.
## retail_slice_parity.gd has carried `FOG_CELL_SIZE := 50.0` since the fog
## verbs landed, and that constant is in SIM units. 50 sim units is
## 50 / 0.02649232738129 = 1887 source units per cell - 47x retail's 40. On a
## map whose playable extent is a few tens of sim units across, the whole
## battlefield falls inside a handful of cells, so "revealed" is effectively a
## single global bit and no fog picture can be drawn from it. The first check
## below asserts the retail-derived cell size and was RED before this lane.
##
## WHAT IS NOT ASSERTED HERE, and is named in the report as a follow-up:
## UnlookPersistDuration (retail 1: the delay before a vacated cell falls back
## to fog - this model falls back on the same tick), GhostObject fidelity
## (retail remembers per-player model poses for structures; this model
## remembers only that a structure was seen), and any GAMEPLAY enforcement
## (targeting/acquisition into shroud), which is deliberately absent so the
## authoritative state hash cannot move.
##
## Invocation:
##   <godot> --headless --path game \
##     --script res://tests/retail_fog_of_war_runner.gd

const Fog := preload("res://src/retail_slice/retail_fog_of_war.gd")
const Overlay := preload("res://src/retail_slice/retail_shroud_overlay.gd")
const PlayableUnitAdapter := preload("res://src/retail_slice/playable_unit_runtime_adapter.gd")
const SimScript := preload("res://src/retail_slice/retail_slice_sim.gd")
const ParityScript := preload("res://src/retail_slice/retail_slice_parity.gd")
const Watchdog := preload("res://tests/runner_watchdog.gd")

## Retail gamedata.ini PartitionCellSize, in source (retail world) units.
const RETAIL_PARTITION_CELL_SIZE := 40.0
## The slice's source->sim transform for the pinned battlefield.
const SLICE_SCALE := 0.02649232738129
## A retail VisionRange in source units (GondorRanger horde, compiled value).
const RANGER_VISION_SOURCE := 470.0

const EXPECTED_CHECKS := 63

var passed := 0
var failed := 0
var _watchdog := Watchdog.new()


func _initialize() -> void:
	_watchdog.start(self, "RETAIL_FOG", 0, 0, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	_test_cell_geometry_is_retail_derived()
	_test_default_state_is_shroud()
	_test_a_look_clears_a_circle()
	_test_leaving_falls_back_to_fog_never_to_shroud()
	_test_reveal_and_shroud_and_permanence()
	_test_named_permanent_reveals_undo_by_name()
	_test_vision_is_per_team()
	_test_out_of_bounds_is_shroud_not_a_crash()
	_test_alpha_ramp_matches_retail()
	_test_two_instances_agree_bit_for_bit()
	_test_sim_absent_unless_enabled()
	_test_sim_tick_drives_the_grid()
	_test_presentation_overlay_hides_units_but_not_structures()
	_test_the_compiled_deshroud_range_reaches_the_entity_row()
	_test_hostile_pick_candidates_are_shroud_gated()
	_test_script_reveal_radius_is_scaled_into_sim_space()
	var ran := passed + failed
	if ran != EXPECTED_CHECKS:
		failed += 1
		printerr(
			"RETAIL_FOG FAIL liveness: ran %d checks, expected %d - a function aborted before its assertions"
			% [ran, EXPECTED_CHECKS]
		)
	print("RETAIL_FOG_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)


# --- Fixture ----------------------------------------------------------------


func _grid() -> RefCounted:
	## A 80x80 sim-unit battlefield centred on the origin, which is the order of
	## magnitude of the slice's own playable extent under SLICE_SCALE.
	var fog = Fog.new()
	fog.configure(Vector2(-40.0, -40.0), Vector2(40.0, 40.0), SLICE_SCALE)
	return fog


# --- Geometry ---------------------------------------------------------------


func _test_cell_geometry_is_retail_derived() -> void:
	var fog = _grid()
	var expected_cell := RETAIL_PARTITION_CELL_SIZE * SLICE_SCALE
	_check(
		"the cell size is retail PartitionCellSize under the slice transform",
		absf(float(fog.cell_size) - expected_cell) < 1e-9,
		"cell_size=%.9f expected=%.9f" % [float(fog.cell_size), expected_cell]
	)
	# The legacy parity grid this lane leaves untouched: 50 SIM units per cell,
	# which is 1887 source units - the defect named in the class comment. The
	# assertion is on the NEW model, and this one records the gap so a later
	# lane that unifies them has a number to move.
	_check(
		"the legacy parity fog grid is coarser than retail by more than 40x",
		float(ParityScript.FOG_CELL_SIZE) / expected_cell > 40.0,
		"legacy=%.3f retail-derived=%.6f" % [float(ParityScript.FOG_CELL_SIZE), expected_cell]
	)
	# ceil(80 / cell) + 1: SAGE adds one cell of slop per axis on ZH+ engines,
	# which BFME2 is (OpenSAGE PartitionCellManager, `extra`).
	var expected_cells := int(ceil(80.0 / expected_cell)) + 1
	_check(
		"the grid spans the configured extent plus SAGE's one cell of slop",
		int(fog.cells_x) == expected_cells and int(fog.cells_y) == expected_cells,
		"cells=%dx%d expected=%d" % [int(fog.cells_x), int(fog.cells_y), expected_cells]
	)
	_check(
		"a sim position maps to the cell that contains it",
		fog.cell_index(Vector2(-40.0, -40.0)) == Vector2i(0, 0)
			and fog.cell_index(Vector2(-40.0 + expected_cell * 1.5, -40.0)) == Vector2i(1, 0),
		"origin=%s offset=%s" % [
			fog.cell_index(Vector2(-40.0, -40.0)),
			fog.cell_index(Vector2(-40.0 + expected_cell * 1.5, -40.0)),
		]
	)


func _test_default_state_is_shroud() -> void:
	var fog = _grid()
	_check(
		"an untouched grid is fully shrouded",
		int(fog.state_at(0, Vector2.ZERO)) == Fog.SHROUDED
			and not fog.is_visible(0, Vector2.ZERO)
			and not fog.is_explored(0, Vector2.ZERO),
		"state=%d" % int(fog.state_at(0, Vector2.ZERO))
	)


# --- Looks ------------------------------------------------------------------


func _test_a_look_clears_a_circle() -> void:
	var fog = _grid()
	var radius := RANGER_VISION_SOURCE * SLICE_SCALE  # 12.4514... sim units
	fog.begin_look_pass()
	fog.add_look(0, Vector2.ZERO, radius)
	fog.commit_look_pass()
	_check(
		"the looker's own cell is clear",
		int(fog.state_at(0, Vector2.ZERO)) == Fog.CLEAR,
		"state=%d" % int(fog.state_at(0, Vector2.ZERO))
	)
	_check(
		"a point well inside the vision radius is clear",
		int(fog.state_at(0, Vector2(radius * 0.8, 0.0))) == Fog.CLEAR,
		"state=%d at r*0.8=%.3f" % [int(fog.state_at(0, Vector2(radius * 0.8, 0.0))), radius * 0.8]
	)
	# One cell diagonal of tolerance: a cell is CLEAR when the circle touches it,
	# so the reached set may exceed the radius by up to the cell's own diagonal.
	var slack := float(fog.cell_size) * sqrt(2.0)
	_check(
		"a point beyond the vision radius plus one cell diagonal is not clear",
		int(fog.state_at(0, Vector2(radius + slack + 0.01, 0.0))) != Fog.CLEAR,
		"state=%d at %.3f" % [
			int(fog.state_at(0, Vector2(radius + slack + 0.01, 0.0))), radius + slack
		]
	)
	# (0.8r, 0.8r) is 1.131r from the centre - comfortably outside the circle
	# even after the half-cell-diagonal slack the rectangle test allows - but
	# well inside the bounding SQUARE of side 2r. A stamp that filled its
	# bounding box would pass every check above and fail this one.
	_check(
		"the cleared set is a circle, not the bounding square",
		int(fog.state_at(0, Vector2(radius * 0.8, radius * 0.8))) != Fog.CLEAR,
		"corner state=%d (|d|=%.3f vs r=%.3f)" % [
			int(fog.state_at(0, Vector2(radius * 0.8, radius * 0.8))),
			Vector2(radius * 0.8, radius * 0.8).length(),
			radius,
		]
	)
	_check(
		"clearing a cell also marks it explored",
		fog.is_explored(0, Vector2.ZERO) and fog.is_visible(0, Vector2.ZERO)
	)


func _test_leaving_falls_back_to_fog_never_to_shroud() -> void:
	var fog = _grid()
	var radius := RANGER_VISION_SOURCE * SLICE_SCALE
	fog.begin_look_pass()
	fog.add_look(0, Vector2.ZERO, radius)
	fog.commit_look_pass()
	# The looker walks away. Retail: the cell's look count reaches zero and the
	# cell becomes 1 (fogged) - it never returns to 0 (shrouded).
	fog.begin_look_pass()
	fog.add_look(0, Vector2(60.0, 60.0), radius)
	fog.commit_look_pass()
	_check(
		"a vacated but previously seen cell is fogged, not reshrouded",
		int(fog.state_at(0, Vector2.ZERO)) == Fog.FOGGED,
		"state=%d" % int(fog.state_at(0, Vector2.ZERO))
	)
	_check(
		"a fogged cell reads explored but not visible",
		fog.is_explored(0, Vector2.ZERO) and not fog.is_visible(0, Vector2.ZERO)
	)
	_check(
		"a cell no looker ever reached is still shrouded",
		int(fog.state_at(0, Vector2(-35.0, -35.0))) == Fog.SHROUDED,
		"state=%d" % int(fog.state_at(0, Vector2(-35.0, -35.0)))
	)
	# Two lookers on the same cell, one leaves: retail refcounts, so the cell
	# stays clear.
	fog.begin_look_pass()
	fog.add_look(0, Vector2(5.0, 0.0), radius)
	fog.add_look(0, Vector2(-5.0, 0.0), radius)
	fog.commit_look_pass()
	fog.begin_look_pass()
	fog.add_look(0, Vector2(5.0, 0.0), radius)
	fog.commit_look_pass()
	_check(
		"a cell still covered by a second looker stays clear",
		int(fog.state_at(0, Vector2(4.0, 0.0))) == Fog.CLEAR,
		"state=%d" % int(fog.state_at(0, Vector2(4.0, 0.0)))
	)


# --- Script reveals ---------------------------------------------------------


func _test_reveal_and_shroud_and_permanence() -> void:
	var fog = _grid()
	fog.reveal(0, Vector2(10.0, 10.0), 4.0, false)
	_check(
		"a script reveal makes the region visible",
		int(fog.state_at(0, Vector2(10.0, 10.0))) == Fog.CLEAR,
		"state=%d" % int(fog.state_at(0, Vector2(10.0, 10.0)))
	)
	fog.shroud(0, Vector2(10.0, 10.0), 4.0)
	_check(
		"a script shroud takes a non-permanent reveal all the way back to shroud",
		int(fog.state_at(0, Vector2(10.0, 10.0))) == Fog.SHROUDED,
		"state=%d" % int(fog.state_at(0, Vector2(10.0, 10.0)))
	)
	fog.reveal(0, Vector2(-10.0, -10.0), 4.0, true)
	fog.shroud(0, Vector2(-10.0, -10.0), 4.0)
	_check(
		"a permanent reveal survives a script shroud",
		int(fog.state_at(0, Vector2(-10.0, -10.0))) == Fog.CLEAR,
		"state=%d" % int(fog.state_at(0, Vector2(-10.0, -10.0)))
	)
	# A permanent reveal must also survive the per-tick look pass, which
	# rebuilds the clear set from scratch.
	fog.begin_look_pass()
	fog.commit_look_pass()
	_check(
		"a permanent reveal survives a look pass with no lookers",
		int(fog.state_at(0, Vector2(-10.0, -10.0))) == Fog.CLEAR,
		"state=%d" % int(fog.state_at(0, Vector2(-10.0, -10.0)))
	)


func _test_named_permanent_reveals_undo_by_name() -> void:
	# Retail keys permanent reveals by a MapRevealName so a script can undo one
	# without disturbing the others (OpenSAGE ScriptingSystem.MapReveal carries
	# Name/Waypoint/Radius/Player, and MAP_UNDO_REVEAL_PERMANENTLY_AT_WAYPOINT
	# removes by that name).
	var fog = _grid()
	fog.reveal(0, Vector2(12.0, 0.0), 3.0, true, "watchpost")
	fog.reveal(0, Vector2(-12.0, 0.0), 3.0, true, "beacon")
	_check(
		"two named permanent reveals both apply",
		int(fog.state_at(0, Vector2(12.0, 0.0))) == Fog.CLEAR
			and int(fog.state_at(0, Vector2(-12.0, 0.0))) == Fog.CLEAR
	)
	_check(
		"undoing an unknown reveal name changes nothing and reports false",
		not fog.undo_permanent_reveal_named(0, "nosuchreveal")
			and int(fog.state_at(0, Vector2(12.0, 0.0))) == Fog.CLEAR
	)
	_check(
		"undoing a named reveal drops only that one",
		fog.undo_permanent_reveal_named(0, "watchpost")
			and int(fog.state_at(0, Vector2(12.0, 0.0))) == Fog.FOGGED
			and int(fog.state_at(0, Vector2(-12.0, 0.0))) == Fog.CLEAR,
		"watchpost=%d beacon=%d" % [
			int(fog.state_at(0, Vector2(12.0, 0.0))),
			int(fog.state_at(0, Vector2(-12.0, 0.0))),
		]
	)


func _test_vision_is_per_team() -> void:
	var fog = _grid()
	var radius := RANGER_VISION_SOURCE * SLICE_SCALE
	fog.begin_look_pass()
	fog.add_look(1, Vector2.ZERO, radius)
	fog.commit_look_pass()
	_check(
		"team 1 sees its own look",
		int(fog.state_at(1, Vector2.ZERO)) == Fog.CLEAR
	)
	_check(
		"team 0 is not given team 1's vision",
		int(fog.state_at(0, Vector2.ZERO)) == Fog.SHROUDED,
		"state=%d" % int(fog.state_at(0, Vector2.ZERO))
	)


func _test_out_of_bounds_is_shroud_not_a_crash() -> void:
	var fog = _grid()
	fog.begin_look_pass()
	fog.add_look(0, Vector2(1000.0, -1000.0), 5.0)
	fog.commit_look_pass()
	_check(
		"a position outside the configured extent reads shrouded",
		int(fog.state_at(0, Vector2(500.0, 500.0))) == Fog.SHROUDED
			and int(fog.state_at(0, Vector2(-500.0, -500.0))) == Fog.SHROUDED
	)
	_check(
		"an off-grid look does not corrupt the in-grid cells",
		int(fog.state_at(0, Vector2(39.0, -39.0))) == Fog.SHROUDED
	)


func _test_alpha_ramp_matches_retail() -> void:
	# gamedata.ini: ShroudAlpha 0, FogAlpha 127, ClearAlpha 255, and the
	# convention is inverted - 0 is OPAQUE. The module exposes the retail bytes;
	# the presentation converts them to an overlay opacity.
	var fog = _grid()
	_check(
		"the three retail alpha stops are carried verbatim",
		int(Fog.RETAIL_SHROUD_ALPHA) == 0
			and int(Fog.RETAIL_FOG_ALPHA) == 127
			and int(Fog.RETAIL_CLEAR_ALPHA) == 255
	)
	_check(
		"shrouded ground reports the opaque stop",
		int(fog.visibility_alpha(0, Vector2.ZERO)) == 0
	)
	fog.reveal(0, Vector2.ZERO, 3.0, false)
	_check(
		"cleared ground reports the clear stop",
		int(fog.visibility_alpha(0, Vector2.ZERO)) == 255
	)
	fog.shroud(0, Vector2.ZERO, 3.0)
	fog.reveal(0, Vector2.ZERO, 3.0, false)
	fog.begin_look_pass()
	fog.commit_look_pass()
	_check(
		"remembered ground reports the fog stop",
		int(fog.visibility_alpha(0, Vector2.ZERO)) == 127,
		"alpha=%d state=%d" % [
			int(fog.visibility_alpha(0, Vector2.ZERO)), int(fog.state_at(0, Vector2.ZERO))
		]
	)


# --- Determinism ------------------------------------------------------------


func _test_two_instances_agree_bit_for_bit() -> void:
	var a = _grid()
	var b = _grid()
	for fog in [a, b]:
		var radius := RANGER_VISION_SOURCE * SLICE_SCALE
		for step in range(24):
			fog.begin_look_pass()
			fog.add_look(0, Vector2(-20.0 + float(step) * 1.7, sin(float(step)) * 6.0), radius)
			fog.add_look(1, Vector2(20.0 - float(step) * 1.3, cos(float(step)) * 6.0), radius * 0.5)
			fog.commit_look_pass()
			if step == 7:
				fog.reveal(0, Vector2(0.0, 12.0), 5.0, true, "scripted")
			if step == 15:
				fog.shroud(1, Vector2(10.0, 0.0), 6.0)
			if step == 19:
				fog.undo_permanent_reveal_named(0, "scripted")
	_check(
		"two instances driven identically produce identical state",
		_digest(a) == _digest(b),
		"a=%s b=%s" % [_digest(a).substr(0, 16), _digest(b).substr(0, 16)]
	)
	var restored = _grid()
	restored.from_state(a.to_state())
	_check(
		"state survives a to_state/from_state round trip",
		_digest(restored) == _digest(a),
		"restored=%s a=%s" % [_digest(restored).substr(0, 16), _digest(a).substr(0, 16)]
	)


func _digest(fog) -> String:
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(var_to_bytes(fog.to_state()))
	return context.finish().hex_encode()


# --- Simulation integration -------------------------------------------------


func _test_sim_absent_unless_enabled() -> void:
	# THE PIN CONTRACT. The authoritative state hash is pinned at 3000 ticks
	# (retail_state_pin_runner). Fog is presentation-derived and contributes
	# ZERO bytes to the authoritative state whether it is on or off, so no
	# scenario's hash can move because of it. This check is what makes that a
	# tested property rather than a claim in a comment.
	var off = _sim_with_fog(false)
	var on = _sim_with_fog(true)
	_check(
		"a sim with fog off exposes a disabled fog model",
		not off.fog_of_war().enabled
	)
	_check(
		"a sim with fog on exposes an enabled fog model",
		on.fog_of_war().enabled
	)
	_check(
		"fog contributes no key to the authoritative state, on or off",
		not off._authoritative_state().has("fog_of_war")
			and not on._authoritative_state().has("fog_of_war")
	)
	var hash_off := String(off.state_hash())
	var hash_on := String(on.state_hash())
	# The two sims differ only by the fog rule, which lives in `rules` - a
	# hashed static key - so their hashes MAY differ by that one rule value and
	# nothing else. Tick them both and assert they stay in lockstep with each
	# other's fog-free content by comparing the dynamic half.
	_check(
		"enabling fog does not change any hashed dynamic state",
		_dynamic_digest(off) == _dynamic_digest(on),
		"off=%s on=%s (full off=%s on=%s)" % [
			_dynamic_digest(off).substr(0, 12), _dynamic_digest(on).substr(0, 12),
			hash_off.substr(0, 12), hash_on.substr(0, 12),
		]
	)
	for _i in range(60):
		off.tick()
		on.tick()
	_check(
		"and still does not after sixty ticks",
		_dynamic_digest(off) == _dynamic_digest(on),
		"off=%s on=%s" % [_dynamic_digest(off).substr(0, 12), _dynamic_digest(on).substr(0, 12)]
	)


func _dynamic_digest(sim) -> String:
	var state: Dictionary = sim._authoritative_state()
	for key in sim._state_hash_static_keys():
		state.erase(key)
	var context := HashingContext.new()
	context.start(HashingContext.HASH_SHA256)
	context.update(var_to_bytes(sim._canonicalize(state)))
	return context.finish().hex_encode()


func _sim_with_fog(enabled: bool):
	## A BARE sim: `setup({}, rules)` with no faction manifest seeds no roster
	## and no structures at all, so the two entity rows below are the entire
	## match and this runner owns every input the fog pass reads. Both teams get
	## a row on purpose - a one-team match is decided on tick 1, and a decided
	## match returns from `tick()` before any gameplay step runs, which would
	## make a working fog pass look broken.
	# `unit_rules` is supplied for the four seeded object ids the sim looks up at
	# setup. Without them setup pushes "missing selected-pack unit rule" errors,
	# and the retail gate fails any runner whose output carries an error line
	# even when its result line is green.
	var rules := {
		"starting_resources": 1000,
		"source_map_transform_scale": SLICE_SCALE,
		"unit_rules": {
			SimScript.SOLDIER_OBJECT_ID: _stub_unit_rule(SimScript.SOLDIER_HORDE_ID),
			SimScript.ARCHER_OBJECT_ID: _stub_unit_rule(SimScript.ARCHER_OBJECT_ID),
			SimScript.TOWER_GUARD_OBJECT_ID: _stub_unit_rule(SimScript.TOWER_GUARD_OBJECT_ID),
			SimScript.KNIGHT_OBJECT_ID: _stub_unit_rule(SimScript.KNIGHT_OBJECT_ID),
			SimScript.BUILDER_OBJECT_ID: _stub_unit_rule(SimScript.BUILDER_OBJECT_ID),
		},
	}
	if enabled:
		rules["enable_fog_of_war"] = true
	var sim = SimScript.new()
	sim._rules = rules.duplicate(true)
	sim.setup({}, rules)
	sim.ai_enabled = false
	for entity_id in sim.entity_ids():
		sim.entities.erase(entity_id)
	sim.entities[901] = _fog_entity(901, 0, Vector2(-8.0, 0.0))
	sim.entities[902] = _fog_entity(902, 1, Vector2(20.0, 0.0))
	return sim


func _stub_unit_rule(horde_id: String) -> Dictionary:
	## The same shape retail_state_pin_runner's `_unit_rule` uses. Copied rather
	## than shared because several of these fields are read with PROPERTY syntax
	## (`rule.delay_between_shots_ms`), which throws on a Dictionary that lacks
	## the key instead of defaulting - so a partial rule is not a smaller rule,
	## it is an error-emitting one.
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
		"vision_range": RANGER_VISION_SOURCE * SLICE_SCALE,
		"vision_range_source": RANGER_VISION_SOURCE,
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
		"is_builder": false,
	}


func _fog_entity(entity_id: int, team: int, position: Vector2) -> Dictionary:
	# No `shroud_clearing_range` on purpose: the fallback to `vision_range` is
	# the path every pack compiled before this lane's importer change takes, so
	# it is the path under test.
	return {
		"id": entity_id,
		"team": team,
		"position": position,
		"destination": position,
		"health": 100,
		"vision_range": RANGER_VISION_SOURCE * SLICE_SCALE,
		"vision_range_source": RANGER_VISION_SOURCE,
		# `attack_cooldown` is read with property syntax (`row.attack_cooldown`)
		# by the combat step, which throws on a Dictionary that lacks the key
		# rather than defaulting. Present so this runner emits no engine errors -
		# the retail gate treats any "error" line in a runner's output as a
		# failure regardless of the result line.
		"attack_cooldown": 0,
		"target_id": 0,
		"attack_range": 1.15,
		"attack_range_source": 11.5,
		"route": PackedVector2Array(),
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"maximum_health": 100,
	}


func _test_sim_tick_drives_the_grid() -> void:
	var sim = _sim_with_fog(true)
	sim.tick()
	var fog = sim.fog_of_war()
	_check(
		"a living entity clears fog for its own team at its own position",
		int(fog.state_at(0, Vector2(-8.0, 0.0))) == Fog.CLEAR,
		"state=%d" % int(fog.state_at(0, Vector2(-8.0, 0.0)))
	)
	_check(
		"the enemy's position is shrouded to team 0 when out of vision",
		int(fog.state_at(0, Vector2(20.0, 0.0))) == Fog.SHROUDED,
		"state=%d" % int(fog.state_at(0, Vector2(20.0, 0.0)))
	)
	_check(
		"each team gets its own grid from the same tick",
		int(fog.state_at(1, Vector2(20.0, 0.0))) == Fog.CLEAR
			and int(fog.state_at(1, Vector2(-8.0, 0.0))) == Fog.SHROUDED
	)
	# The wp24 script surface must drive THIS model, not only the legacy parity
	# dictionary, or a map that reveals ground still renders it black.
	sim.fog_of_war().reveal(0, Vector2(20.0, 0.0), 4.0, false)
	_check(
		"a script reveal is visible to the presentation query",
		sim.fog_of_war().is_visible(0, Vector2(20.0, 0.0))
	)
	sim.tick()
	_check(
		"a non-permanent script reveal lapses on the next look pass",
		int(sim.fog_of_war().state_at(0, Vector2(20.0, 0.0))) == Fog.FOGGED,
		"state=%d" % int(sim.fog_of_war().state_at(0, Vector2(20.0, 0.0)))
	)


# --- The compiled range actually reaching a row -----------------------------


## A retail fortress shape: VisionRange 400, ShroudClearingRange 800
## (MenFortressCitadel, effective-assets). The two numbers must not be
## interchangeable anywhere along the path.
const DESHROUD_FIXTURE_VISION := 400.0
const DESHROUD_FIXTURE_SHROUD := 800.0


func _test_the_compiled_deshroud_range_reaches_the_entity_row() -> void:
	## THE GAP THIS WAS WRITTEN TO CATCH. The importer compiling
	## `shroudClearingRange` is worth nothing on its own: the value has to cross
	## the runtime adapter, the unit rule, and the entity row before
	## `_shroud_clearing_radius` can read it. Before this check existed nothing
	## in game/src wrote `shroud_clearing_range` at all, so the sim's
	## `row.get("shroud_clearing_range", 0.0)` was permanently 0, the fallback to
	## vision was permanent, and a republish would have changed NOTHING.
	##
	## Each step is asserted separately so a break names the seam that dropped
	## the value rather than just "the radius is wrong".
	var document := _deshroud_document(true)
	var simulation: Dictionary = PlayableUnitAdapter.simulation_rule(document, false)
	_check(
		"the runtime adapter carries the compiled deshroud range off the document",
		absf(float(simulation.get("shroud_clearing_range_source", 0.0)) - DESHROUD_FIXTURE_SHROUD) < 0.001,
		"got %s" % simulation.get("shroud_clearing_range_source", "<absent>")
	)
	var rule: Dictionary = PlayableUnitAdapter.normalized_unit_rule(simulation, SLICE_SCALE)
	_check(
		"the unit rule scales it into sim space, distinct from vision",
		absf(float(rule.get("shroud_clearing_range", 0.0)) - DESHROUD_FIXTURE_SHROUD * SLICE_SCALE) < 0.0001
			and absf(float(rule.get("shroud_clearing_range_source", 0.0)) - DESHROUD_FIXTURE_SHROUD) < 0.001
			and absf(float(rule.get("vision_range_source", 0.0)) - DESHROUD_FIXTURE_VISION) < 0.001,
		"rule shroud=%s source=%s vision=%s" % [
			rule.get("shroud_clearing_range", "<absent>"),
			rule.get("shroud_clearing_range_source", "<absent>"),
			rule.get("vision_range_source", "<absent>"),
		]
	)
	var sim = _sim_with_fog(true)
	var row: Dictionary = _row_from_rule(sim, 950, rule)
	_check(
		"the entity row keeps the deshroud range",
		absf(float(row.get("shroud_clearing_range", 0.0)) - DESHROUD_FIXTURE_SHROUD * SLICE_SCALE) < 0.0001,
		"row=%s" % row.get("shroud_clearing_range", "<absent>")
	)
	_check(
		"the fog pass deshrouds at the 800 radius, not the 400 vision radius",
		absf(sim._shroud_clearing_radius(row) - DESHROUD_FIXTURE_SHROUD * SLICE_SCALE) < 0.0001,
		"radius=%.6f vision-derived would be %.6f" % [
			sim._shroud_clearing_radius(row), DESHROUD_FIXTURE_VISION * SLICE_SCALE
		]
	)
	# ABSENT MUST STAY ABSENT. A pack compiled before this change authors no
	# deshroud range, and emitting a 0 (or a copy of vision) for it would both
	# lie about the source data and add a key to the hashed entity row, which is
	# what the 3000-tick pin would notice.
	var legacy_rule: Dictionary = PlayableUnitAdapter.normalized_unit_rule(
		PlayableUnitAdapter.simulation_rule(_deshroud_document(false), false), SLICE_SCALE
	)
	var legacy_row: Dictionary = _row_from_rule(sim, 951, legacy_rule)
	_check(
		"a pack with no compiled deshroud range adds no key anywhere",
		not legacy_rule.has("shroud_clearing_range")
			and not legacy_rule.has("shroud_clearing_range_source")
			and not legacy_row.has("shroud_clearing_range"),
		"rule keys present=%s row key present=%s" % [
			legacy_rule.has("shroud_clearing_range"), legacy_row.has("shroud_clearing_range")
		]
	)
	_check(
		"and falls back to vision, which is what every published pack does today",
		absf(sim._shroud_clearing_radius(legacy_row) - DESHROUD_FIXTURE_VISION * SLICE_SCALE) < 0.0001,
		"radius=%.6f" % sim._shroud_clearing_radius(legacy_row)
	)


func _row_from_rule(sim, entity_id: int, rule: Dictionary) -> Dictionary:
	## Goes through `_add_battalion`'s `unit_rule_override`, the PRODUCTION row
	## builder, rather than a test-only hook - a hook could keep passing while
	## the real path dropped the field.
	sim._add_battalion(
		entity_id, 0, Vector2(-30.0, 30.0), "Fog fixture",
		"bfme2.object.fog-fixture", "bfme2.object.fog-fixture-horde", -1, rule
	)
	return sim.entities.get(entity_id, {}) as Dictionary


func _deshroud_document(with_shroud_range: bool) -> Dictionary:
	var resolved := {
		"displayNameId": {"value": "OBJECT:FogFixture"},
		"buildCost": {"value": 100},
		"buildTimeSeconds": {"value": 5.0},
		"commandPoints": {"value": 10},
		"memberCount": {"value": 1},
		"memberHealth": {"value": 500},
		"speed": {"value": 40.0},
		"visionRange": {"value": DESHROUD_FIXTURE_VISION},
		"combat": {
			"attackRange": {"value": 100.0},
			"minimumAttackRange": {"value": 0.0, "defined": true},
			"delayBetweenShotsMs": {"value": 600.0},
			"preAttackDelayMs": {"value": 200.0},
			"firingDurationMs": {"value": 200.0},
			"damage": {"value": 10.0},
		},
		"movement": {
			"acceleration": {"value": 40.0},
			"braking": {"value": 40.0},
			"turnRateDegreesPerSecond": {"value": 180.0},
		},
		"formation": {"positions": [{"x": 0.0, "y": 0.0}]},
		"fearResistant": {"value": false},
	}
	if with_shroud_range:
		resolved["shroudClearingRange"] = {"value": DESHROUD_FIXTURE_SHROUD}
	return {
		"objectId": "FogFixtureHorde",
		"unitId": "bfme2.object.fog-fixture-horde",
		"memberId": "bfme2.object.fog-fixture",
		"category": "infantry",
		# `status: ready` on purpose: that is the branch the compiled-document
		# path takes, and it is the only branch that reads `resolved`.
		"registration": {"simulation": {"status": "ready", "resolved": resolved}},
	}


# --- Presentation -----------------------------------------------------------


func _test_presentation_overlay_hides_units_but_not_structures() -> void:
	## The presentation half, gated WITHOUT a GPU: the overlay's per-object
	## answers and the radar layer it hands the minimap are pure functions of the
	## grid, so they get a cheap unit test and the rendered capture is left to
	## prove only what a capture can prove (that the shader actually samples it).
	var fog = _grid()
	var radius := RANGER_VISION_SOURCE * SLICE_SCALE
	# A visible spot, a remembered spot, and a spot nobody has ever been near.
	fog.begin_look_pass()
	fog.add_look(0, Vector2(-20.0, 0.0), radius)
	fog.commit_look_pass()
	fog.begin_look_pass()
	fog.add_look(0, Vector2(10.0, 0.0), radius)
	fog.commit_look_pass()
	var clear_point := Vector2(10.0, 0.0)
	var fogged_point := Vector2(-20.0, 0.0)
	var shrouded_point := Vector2(-38.0, 38.0)
	_check(
		"the fixture really has one cell of each state",
		int(fog.state_at(0, clear_point)) == Fog.CLEAR
			and int(fog.state_at(0, fogged_point)) == Fog.FOGGED
			and int(fog.state_at(0, shrouded_point)) == Fog.SHROUDED,
		"clear=%d fogged=%d shrouded=%d" % [
			int(fog.state_at(0, clear_point)),
			int(fog.state_at(0, fogged_point)),
			int(fog.state_at(0, shrouded_point)),
		]
	)
	var off = Overlay.new()
	off.configure(null, 0)
	_check(
		"an overlay with no fog model shows everything",
		off.unit_visible(shrouded_point) and off.structure_visible(shrouded_point)
	)
	var overlay = Overlay.new()
	overlay.configure(fog, 0)
	_check("the overlay is enabled by an enabled grid", not overlay.enabled)
	fog.enabled = true
	overlay.configure(fog, 0)
	_check("and reports enabled once the match authored fog", overlay.enabled)
	_check(
		"a unit is drawn only on currently visible ground",
		overlay.unit_visible(clear_point)
			and not overlay.unit_visible(fogged_point)
			and not overlay.unit_visible(shrouded_point)
	)
	_check(
		"a structure survives into fog but not into shroud",
		overlay.structure_visible(clear_point)
			and overlay.structure_visible(fogged_point)
			and not overlay.structure_visible(shrouded_point)
	)
	_check(
		"the overlay rebuilt its textures on the first forced update",
		overlay.update(true) and int(overlay.rebuild_count()) == 1
			and overlay.texture() != null and overlay.minimap_texture() != null
	)
	_check(
		"the radar layer is the exact complement of the retail visibility byte",
		_minimap_alpha(overlay, fog, clear_point) == 255 - Fog.RETAIL_CLEAR_ALPHA
			and _minimap_alpha(overlay, fog, fogged_point) == 255 - Fog.RETAIL_FOG_ALPHA
			and _minimap_alpha(overlay, fog, shrouded_point) == 255 - Fog.RETAIL_SHROUD_ALPHA,
		"clear=%d fogged=%d shrouded=%d" % [
			_minimap_alpha(overlay, fog, clear_point),
			_minimap_alpha(overlay, fog, fogged_point),
			_minimap_alpha(overlay, fog, shrouded_point),
		]
	)
	_check(
		"the overlay's grid bounds match the model's, so the shader UV lines up",
		overlay.bounds() == fog.grid_bounds()
			and overlay.grid_cells() == Vector2i(int(fog.cells_x), int(fog.cells_y))
	)


func _minimap_alpha(overlay, fog, position: Vector2) -> int:
	var cell: Vector2i = fog.cell_index(position)
	var image: Image = overlay.minimap_texture().get_image()
	return int(round(image.get_pixelv(cell).a * 255.0))


# --- Hover / picking --------------------------------------------------------


func _test_hostile_pick_candidates_are_shroud_gated() -> void:
	## THE OWNER-VISIBLE BUG. An enemy standing in fog is not drawn, but the
	## hostile pick that feeds the cursor never asked the shroud - so the mouse
	## found it anyway and the attack cursor lit up over what looks like empty
	## ground, promising an attack on nothing the player can see.
	##
	## Units use the CLEAR test and structures the explored test, the same split
	## the battlefield and the radar use, so the cursor can never disagree with
	## what is on screen.
	var fog = _grid()
	fog.enabled = true
	var radius := RANGER_VISION_SOURCE * SLICE_SCALE
	fog.begin_look_pass()
	fog.add_look(0, Vector2(-20.0, 0.0), radius)
	fog.commit_look_pass()
	fog.begin_look_pass()
	fog.add_look(0, Vector2(10.0, 0.0), radius)
	fog.commit_look_pass()
	var positions := {
		1: Vector2(10.0, 0.0),    # in the clear
		2: Vector2(-20.0, 0.0),   # remembered, not currently seen
		3: Vector2(-38.0, 38.0),  # never seen
	}
	var lookup := func(id: int) -> Vector2: return positions.get(id, Vector2.ZERO)
	var overlay = Overlay.new()
	overlay.configure(fog, 0)
	_check(
		"only the enemy on cleared ground is a unit pick candidate",
		overlay.visible_unit_ids([1, 2, 3], lookup) == [1],
		"got %s" % [overlay.visible_unit_ids([1, 2, 3], lookup)]
	)
	_check(
		"an enemy structure stays pickable in fog but not in shroud",
		overlay.visible_structure_ids([1, 2, 3], lookup) == [1, 2],
		"got %s" % [overlay.visible_structure_ids([1, 2, 3], lookup)]
	)
	_check(
		"the gate preserves the caller's ordering, which the pick relies on",
		overlay.visible_unit_ids([3, 1, 2], lookup) == [1]
			and overlay.visible_structure_ids([3, 2, 1], lookup) == [2, 1]
	)
	var off = Overlay.new()
	off.configure(null, 0)
	_check(
		"a fog-off match gates nothing, so every legacy pick is unchanged",
		off.visible_unit_ids([1, 2, 3], lookup) == [1, 2, 3]
			and off.visible_structure_ids([1, 2, 3], lookup) == [1, 2, 3]
	)


# --- Script reveal radius units ---------------------------------------------


func _test_script_reveal_radius_is_scaled_into_sim_space() -> void:
	## A map script authors its reveal radius in SOURCE units, like every other
	## retail length. The slice's fog target reader converts the CENTRE into sim
	## space but leaves the radius alone, so the value reaching the grid must be
	## scaled or a modest authored radius uncovers the whole battlefield.
	##
	## Asserted as a UNIT relationship rather than through the script world,
	## because the script world needs a mounted pack: a 500-source-unit reveal
	## must light a 13.2-sim-unit circle, not a 500-sim-unit one (which is
	## 18,872 source units - larger than any retail map).
	var fog = _grid()
	fog.enabled = true
	var authored_source_radius := 500.0
	fog.reveal(0, Vector2.ZERO, authored_source_radius * SLICE_SCALE, false)
	var edge := authored_source_radius * SLICE_SCALE  # 13.246 sim units
	_check(
		"a 500-source-unit reveal lights ground just inside its scaled radius",
		fog.is_visible(0, Vector2(edge * 0.9, 0.0))
	)
	_check(
		"and leaves the far side of the battlefield shrouded",
		int(fog.state_at(0, Vector2(-38.0, -38.0))) == Fog.SHROUDED
			and int(fog.state_at(0, Vector2(38.0, 38.0))) == Fog.SHROUDED,
		"an unscaled 500 would have covered the entire %s grid" % fog.grid_bounds().size
	)
	# The failure mode this pins: the same number read as sim units.
	var unscaled = _grid()
	unscaled.enabled = true
	unscaled.reveal(0, Vector2.ZERO, authored_source_radius, false)
	_check(
		"reading the authored radius as sim units would uncover the whole map",
		unscaled.is_visible(0, Vector2(-38.0, -38.0))
			and unscaled.is_visible(0, Vector2(38.0, 38.0))
	)


# --- Reporting --------------------------------------------------------------


func _check(name: String, condition: bool, detail: String = "") -> void:
	_watchdog.note(name)
	if condition:
		passed += 1
		print("RETAIL_FOG PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_FOG FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])
