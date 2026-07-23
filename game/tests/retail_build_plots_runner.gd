extends SceneTree
## Deterministic gate for the BFME1 "build plots only" mode toggle
## (gameplay_rules["build_plots_only"]). Proves: mode OFF is byte-identical to
## today's freeform construction (twin-run vs a no-flag sim), mode ON restricts
## issue_construct to designated empty plots, plot occupancy is tracked and
## freed on razing, plot state round-trips through a snapshot, and mode-ON runs
## stay deterministic across twin sims.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	_test_mode_off_is_byte_identical()
	_test_mode_on_rejects_off_plot()
	_test_mode_on_accepts_on_plot_and_tracks_occupancy()
	_test_mode_on_snapshot_round_trip()
	_test_mode_on_twin_determinism()
	print("RETAIL_BUILD_PLOTS_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)


## Mode OFF (and the absent-key default) must reproduce the freeform path exactly.
func _test_mode_off_is_byte_identical() -> void:
	# sim_a: no build_plots_only key at all (the every-legacy-runner default).
	var sim_a = _make_sim(false, false)
	# sim_b: build_plots_only explicitly false.
	var sim_b = _make_sim(true, false)
	_check("mode_off_no_plots_seeded", sim_a.build_plot_states(0).is_empty() and sim_b.build_plot_states(0).is_empty())
	var res_a = sim_a.issue_construct(_ids(), "farm", Vector2(-20, 30))
	var res_b = sim_b.issue_construct(_ids(), "farm", Vector2(-20, 30))
	_check("mode_off_freeform_construct_still_works", bool(res_a.get("ok", false)) and bool(res_b.get("ok", false)), "%s / %s" % [res_a, res_b])
	for _tick in range(200):
		sim_a.advance(1)
		sim_b.advance(1)
	_check(
		"mode_off_twin_signature_equal_200_ticks",
		sim_a.state_signature() == sim_b.state_signature(),
		"%s != %s" % [sim_a.state_signature(), sim_b.state_signature()]
	)


func _test_mode_on_rejects_off_plot() -> void:
	var sim = _make_sim(true, true)
	var plots: Array = sim.build_plot_states(0)
	_check("mode_on_seeds_plot_ring", plots.size() == SimScript.BUILD_PLOT_RING_OFFSETS.size(), str(plots.size()))
	# A click far from every plot is rejected with the honest reason.
	var far := Vector2(300.0, 300.0)
	var rejected = sim.issue_construct(_ids(), "barracks", far)
	_check(
		"mode_on_rejects_off_plot_construct",
		not bool(rejected.get("ok", false)) and String(rejected.get("reason", "")).begins_with("build-plots-only"),
		str(rejected)
	)


func _test_mode_on_accepts_on_plot_and_tracks_occupancy() -> void:
	var sim = _make_sim(true, true)
	var plots: Array = sim.build_plot_states(0)
	var plot_position := Vector2(plots[0].get("position", Vector2.ZERO))
	var built = sim.issue_construct(_ids(), "barracks", plot_position)
	_check("mode_on_accepts_on_plot_construct", bool(built.get("ok", false)), str(built))
	var structure_id := int(built.get("structure_id", 0))
	var after: Array = sim.build_plot_states(0)
	var occupied := 0
	for plot_value in after:
		if int((plot_value as Dictionary).get("occupant_structure_id", 0)) == structure_id:
			occupied += 1
	_check("mode_on_plot_becomes_occupied", occupied == 1 and structure_id != 0, "occupied=%d id=%d" % [occupied, structure_id])
	# A second construct snapping to the SAME plot is refused (plot taken); a free
	# plot still accepts, proving occupancy actually gates placement.
	var same_plot = sim.issue_construct(_ids(), "barracks", plot_position)
	_check("mode_on_occupied_plot_refuses_rebuild", not bool(same_plot.get("ok", false)), str(same_plot))
	# Raze the plot building: its plot must free for a rebuild.
	(sim.structures[structure_id] as Dictionary)["health"] = 0
	sim._emit_event("structure.destroyed", 0, structure_id, {"reason": "test-raze"})
	var freed: Array = sim.build_plot_states(0)
	var still_occupied := false
	for plot_value in freed:
		if int((plot_value as Dictionary).get("occupant_structure_id", 0)) == structure_id:
			still_occupied = true
	_check("mode_on_razed_plot_frees_for_rebuild", not still_occupied)
	sim.structures.erase(structure_id)
	var rebuilt = sim.issue_construct(_ids(), "barracks", plot_position)
	_check("mode_on_freed_plot_accepts_rebuild", bool(rebuilt.get("ok", false)), str(rebuilt))


func _test_mode_on_snapshot_round_trip() -> void:
	var sim = _make_sim(true, true)
	var plots: Array = sim.build_plot_states(0)
	var built = sim.issue_construct(_ids(), "stable", Vector2(plots[1].get("position", Vector2.ZERO)))
	_check("snapshot_setup_construct_ok", bool(built.get("ok", false)), str(built))
	for _tick in range(50):
		sim.advance(1)
	var bytes = sim.snapshot()
	var restored = _make_sim(true, true)
	var ok = restored.restore(bytes)
	_check("snapshot_restore_succeeds", ok)
	_check("snapshot_preserves_build_plots_only", bool(restored.build_plots_only))
	_check(
		"snapshot_round_trips_plot_state",
		ok and restored.state_hash() == sim.state_hash() and _plots_equal(restored.build_plot_states(0), sim.build_plot_states(0)),
		"%s vs %s" % [restored.state_hash(), sim.state_hash()]
	)


func _test_mode_on_twin_determinism() -> void:
	var sim_a = _make_sim(true, true)
	var sim_b = _make_sim(true, true)
	var plots: Array = sim_a.build_plot_states(0)
	for index in [0, 2, 4]:
		var pos := Vector2(plots[index].get("position", Vector2.ZERO))
		sim_a.issue_construct(_ids(), "barracks", pos)
		sim_b.issue_construct(_ids(), "barracks", pos)
	var diverged := -1
	for tick in range(1, 201):
		sim_a.advance(1)
		sim_b.advance(1)
		if diverged < 0 and sim_a.state_hash() != sim_b.state_hash():
			diverged = tick
	_check("mode_on_twin_determinism_200_ticks", diverged < 0, "first_divergence=%d" % diverged)


func _make_sim(set_flag: bool, plots_only: bool):
	var sim = SimScript.new()
	var rules := {
		"enable_base_loop": true,
		"starting_resources": 100000,
		"member_health": 100,
		"unit_rules": _unit_rules(),
		"farm_income": 25,
	}
	if set_flag:
		rules["build_plots_only"] = plots_only
	sim.setup({}, rules)
	sim.ai_enabled = false
	return sim


func _ids() -> Array[int]:
	var ids: Array[int] = [3]
	return ids


func _plots_equal(a: Array, b: Array) -> bool:
	if a.size() != b.size():
		return false
	for index in a.size():
		var pa: Dictionary = a[index]
		var pb: Dictionary = b[index]
		if int(pa.get("occupant_structure_id", 0)) != int(pb.get("occupant_structure_id", 0)):
			return false
		if Vector2(pa.get("position", Vector2.ZERO)) != Vector2(pb.get("position", Vector2.ZERO)):
			return false
	return true


func _unit_rules() -> Dictionary:
	var result := {}
	for object_id in [SimScript.SOLDIER_OBJECT_ID, SimScript.ARCHER_OBJECT_ID, SimScript.TOWER_GUARD_OBJECT_ID, SimScript.KNIGHT_OBJECT_ID]:
		result[object_id] = _unit_rule(object_id, 5, false)
	result[SimScript.BUILDER_OBJECT_ID] = _unit_rule(SimScript.BUILDER_OBJECT_ID, 1, true)
	return result


func _unit_rule(object_id: String, member_count: int, builder: bool) -> Dictionary:
	return {
		"horde_id": object_id,
		"member_count": member_count,
		"member_health": 500 if builder else 100,
		"member_damage": 1 if builder else 10,
		"speed": 6.0 if builder else 5.5,
		"speed_source": 60.0 if builder else 55.0,
		"acceleration": 6.0,
		"acceleration_source": 60.0,
		"turn_rate_degrees_per_second": 360.0,
		"braking": 6.0,
		"braking_source": 60.0,
		"attack_range": 0.0 if builder else 1.15,
		"attack_range_source": 0.0 if builder else 11.5,
		"minimum_attack_range": 0.0,
		"minimum_attack_range_source": 0.0,
		"vision_range": 2.5 if builder else 17.5,
		"vision_range_source": 25.0 if builder else 175.0,
		"delay_between_shots_ms": 1000.0,
		"pre_attack_delay_ms": 0.0,
		"firing_duration_ms": 0.0,
		"attack_period_ticks": 10,
		"pre_attack_ticks": 0,
		"firing_duration_ticks": 0,
		"formation_positions": [Vector3.ZERO] if builder else [Vector3.ZERO, Vector3.RIGHT, Vector3.LEFT, Vector3.FORWARD, Vector3.BACK],
		"stances": {"default": "Battle", "cycleOrder": ["HoldGround", "Battle", "Aggressive"], "states": {"HoldGround": {}, "Battle": {}, "Aggressive": {}}},
		"is_builder": builder,
		"provenance": {},
	}


func _check(label: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_BUILD_PLOTS PASS %s" % label)
	else:
		failed += 1
		push_error("RETAIL_BUILD_PLOTS FAIL %s%s" % [label, " (%s)" % detail if detail != "" else ""])
