extends SceneTree
## Deterministic gate for the BFME1 "build plots only" mode toggle
## (gameplay_rules["build_plots_only"]). Proves: mode OFF is byte-identical to
## today's freeform construction (twin-run vs a no-flag sim), mode ON restricts
## issue_construct to designated empty plots, plot occupancy is tracked and
## freed on razing, plot state round-trips through a snapshot, and mode-ON runs
## stay deterministic across twin sims.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")
const ManifestScript = preload("res://src/retail_slice/retail_faction_manifest.gd")

var passed := 0
var failed := 0


const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
# Turns a GDScript runtime error inside `_run` — which unwinds past every
# `quit()` and would otherwise leave this headless process idling forever —
# into a loud non-zero exit. See tests/runner_watchdog.gd.
var _runner_watchdog := RunnerWatchdogScript.new()


func _initialize() -> void:
	_runner_watchdog.start(self, "RETAIL_BUILD_PLOTS_RUNNER")
	call_deferred("_run")


func _run() -> void:
	_test_mode_off_is_byte_identical()
	_test_mode_on_rejects_off_plot()
	_test_mode_on_accepts_on_plot_and_tracks_occupancy()
	_test_mode_on_snapshot_round_trip()
	_test_mode_on_twin_determinism()
	_test_grant_upgrade_create_on_build_complete()
	_test_inherit_upgrade_create_exact_creation_edge()
	await _test_mode_on_non_men_and_cross_faction()
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


func _test_grant_upgrade_create_on_build_complete() -> void:
	var sim = _make_sim(false, false)
	sim.configure_expansion_rules({
		"test_grant_expansion": {
			"cost": 1,
			"seconds": 0.05,
			"health": 1000,
			"pad_kinds": ["side", "corner"],
			"name": "Test Grant Expansion",
			"object_id": "bfme2.object.test-grant-expansion",
			"highlander_body": true,
			"create_grants": [
			{
				"upgradeId": "Upgrade_TestBuiltObject",
				"upgradeType": "OBJECT",
				"onCreateWhenComplete": false,
				"onBuildComplete": true,
			},
			{
				"upgradeId": "Upgrade_TestBuiltPlayer",
				"upgradeType": "PLAYER",
				"onCreateWhenComplete": false,
				"onBuildComplete": true,
			},
			],
		},
	})
	var built: Dictionary = sim.issue_expansion_construct(
		0, sim.fortress_id(0), "test_grant_expansion"
	)
	var structure_id := int(built.get("structure_id", 0))
	var structure: Dictionary = sim.structures.get(structure_id, {})
	_check(
		"expansion_carries_authored_highlander_body_policy",
		structure.get("highlander_body") == true,
		str(structure)
	)
	structure["construction_elapsed_ticks"] = int(
		structure.get("construction_build_ticks", 1)
	) - 1
	sim._step_construction()
	# A repeated completion callback must remain idempotent.
	sim._apply_structure_create_grants(structure, false, true)
	_check(
		"grant_upgrade_create_object_on_build_complete",
		Array(structure.get("completed_upgrades", [])).has("Upgrade_TestBuiltObject"),
		"built=%s progress=%s grants=%s completed=%s" % [
			built,
			structure.get("construction_progress"),
			sim.structure_create_grants_for_team(0),
			structure.get("completed_upgrades"),
		]
	)
	_check(
		"grant_upgrade_create_player_on_build_complete",
		(sim.team_upgrades.get(0, {}) as Dictionary).has("Upgrade_TestBuiltPlayer"),
		str(sim.team_upgrades.get(0, {}))
	)
	_check(
		"grant_upgrade_create_object_is_idempotent",
		Array(structure.get("completed_upgrades", [])).count(
			"Upgrade_TestBuiltObject"
		) == 1
	)


func _test_inherit_upgrade_create_exact_creation_edge() -> void:
	const UPGRADE := "Upgrade_TestStonework"
	var sim = _make_inherit_sim()
	var fortress_id := int(sim.fortress_id(0))
	var fortress: Dictionary = sim.structures[fortress_id]
	fortress["completed_upgrades"] = [UPGRADE]
	var fortress_position := Vector2(fortress.get("position", Vector2.ZERO))
	var built: Dictionary = sim.issue_construct(
		_ids(), "barracks", fortress_position + Vector2(10.0, 0.0)
	)
	var carrier: Dictionary = sim.structures.get(
		int(built.get("structure_id", 0)), {}
	)
	_check(
		"inherit_upgrade_create_runs_on_real_construct_edge",
		bool(built.get("ok", false))
			and Array(carrier.get("completed_upgrades", [])).has(UPGRADE),
		"built=%s carrier=%s" % [built, carrier]
	)

	var snapshot: PackedByteArray = sim.snapshot()
	var restored = _make_inherit_sim()
	var restored_ok: bool = restored.restore(snapshot)
	_check(
		"inherit_upgrade_create_snapshot_and_hash_stable",
		restored_ok
			and restored.state_hash() == sim.state_hash()
			and Array(
				(restored.structures.get(int(built.get("structure_id", 0)), {}) as Dictionary)
				.get("completed_upgrades", [])
			).has(UPGRADE),
		"%s != %s" % [restored.state_hash(), sim.state_hash()]
	)

	var probe := {
		"id": 99001,
		"team": 0,
		"structure_kind": "barracks",
		"position": fortress_position + Vector2(20.001, 0.0),
		"completed_upgrades": [],
	}
	sim._apply_structure_inherit_upgrades(probe)
	_check(
		"inherit_upgrade_create_rejects_outside_radius",
		not Array(probe["completed_upgrades"]).has(UPGRADE)
	)

	probe["position"] = fortress_position + Vector2(10.0, 0.0)
	var boundary := probe.duplicate(true)
	boundary["position"] = fortress_position + Vector2(20.0, 0.0)
	boundary["completed_upgrades"] = []
	sim._apply_structure_inherit_upgrades(boundary)
	_check(
		"inherit_upgrade_create_includes_exact_scaled_radius_boundary",
		Array(boundary["completed_upgrades"]).has(UPGRADE)
	)

	for field in ["health", "construction_progress", "completed_upgrades"]:
		var donor_sim = _make_inherit_sim()
		var donor: Dictionary = donor_sim.structures[int(donor_sim.fortress_id(0))]
		donor["completed_upgrades"] = [UPGRADE]
		match field:
			"health":
				donor["health"] = 0
			"construction_progress":
				donor["construction_progress"] = 0.5
			"completed_upgrades":
				donor["completed_upgrades"] = []
		var negative := probe.duplicate(true)
		negative["completed_upgrades"] = []
		donor_sim._apply_structure_inherit_upgrades(negative)
		_check(
			"inherit_upgrade_create_any_filter_%s" % field,
			Array(negative["completed_upgrades"]).has(UPGRADE)
				if field != "completed_upgrades"
				else not Array(negative["completed_upgrades"]).has(UPGRADE)
		)

	var wrong_type_sim = _make_inherit_sim()
	var wrong_donor: Dictionary = wrong_type_sim.structures[
		int(wrong_type_sim.fortress_id(0))
	]
	wrong_donor["completed_upgrades"] = [UPGRADE]
	(wrong_type_sim.team_manifest_for(0)["structure_source_object_ids"] as Dictionary)[
		"fortress"
	] = ["NotTheCitadel"]
	var wrong_type_probe := probe.duplicate(true)
	wrong_type_probe["completed_upgrades"] = []
	wrong_type_sim._apply_structure_inherit_upgrades(wrong_type_probe)
	_check(
		"inherit_upgrade_create_requires_exact_source_type",
		not Array(wrong_type_probe["completed_upgrades"]).has(UPGRADE)
	)

	var enemy_only_sim = _make_inherit_sim()
	var own_fortress: Dictionary = enemy_only_sim.structures[
		int(enemy_only_sim.fortress_id(0))
	]
	own_fortress["completed_upgrades"] = []
	var enemy_fortress: Dictionary = enemy_only_sim.structures[
		int(enemy_only_sim.fortress_id(1))
	]
	enemy_fortress["completed_upgrades"] = [UPGRADE]
	var enemy_probe := probe.duplicate(true)
	enemy_probe["position"] = Vector2(enemy_fortress.get("position", Vector2.ZERO))
	enemy_probe["completed_upgrades"] = []
	enemy_only_sim._apply_structure_inherit_upgrades(enemy_probe)
	_check(
		"inherit_upgrade_create_any_filter_allows_enemy_source",
		Array(enemy_probe["completed_upgrades"]).has(UPGRADE)
	)

	var foreign_enemy_sim = _make_inherit_sim(true)
	var foreign_own_fortress: Dictionary = foreign_enemy_sim.structures[
		foreign_enemy_sim.fortress_id(0)
	]
	foreign_own_fortress["completed_upgrades"] = []
	var dwarven_fortress: Dictionary = foreign_enemy_sim.structures[
		foreign_enemy_sim.fortress_id(1)
	]
	dwarven_fortress["position"] = Vector2(-40.0, 18.0)
	dwarven_fortress["completed_upgrades"] = [UPGRADE]
	var foreign_probe: Dictionary = probe.duplicate(true)
	foreign_probe["position"] = Vector2(-40.0, 18.0)
	foreign_probe["completed_upgrades"] = []
	foreign_enemy_sim._apply_structure_inherit_upgrades(foreign_probe)
	_check(
		"inherit_upgrade_create_rejects_enemy_with_same_kind_but_foreign_identity",
		not Array(foreign_probe["completed_upgrades"]).has(UPGRADE)
	)

	var one_shot_sim = _make_inherit_sim()
	var one_shot_fortress: Dictionary = one_shot_sim.structures[
		int(one_shot_sim.fortress_id(0))
	]
	one_shot_fortress["completed_upgrades"] = []
	var one_shot_built: Dictionary = one_shot_sim.issue_construct(
		_ids(),
		"barracks",
		Vector2(one_shot_fortress.get("position", Vector2.ZERO))
			+ Vector2(10.0, 0.0)
	)
	var one_shot_carrier: Dictionary = one_shot_sim.structures[
		int(one_shot_built.get("structure_id", 0))
	]
	one_shot_fortress["completed_upgrades"] = [UPGRADE]
	one_shot_sim.advance(5)
	_check(
		"inherit_upgrade_create_does_not_retroactively_rerun",
		not Array(one_shot_carrier.get("completed_upgrades", [])).has(UPGRADE)
	)


## Non-Men + plots and cross-faction + plots (the user-reported load failure):
## boots the real private slice once for authentic manifests/map, then proves
## plots-only mode seeds each team's 8-plot ring around ITS OWN faction's
## fortress for (a) an all-Elves roster and (b) a Men-vs-Elves roster, and that
## the cross-faction plots match is twin-run deterministic.
func _test_mode_on_non_men_and_cross_faction() -> void:
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("faction_slice_scene_parses", packed != null)
	if packed == null:
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame
	_check("faction_slice_boots", bool(slice.ready_ok), String(slice.failure_reason))
	if not bool(slice.ready_ok) or slice.source_map_data == null:
		slice.queue_free()
		await process_frame
		return
	var base_rules: Dictionary = slice.gameplay_rules.duplicate(true)
	var map_config: Dictionary = slice.source_map_data.simulation_configuration()
	var men_manifest: Dictionary = slice.faction_manifest.duplicate(true)
	slice._classify_faction_units("elves")
	var elves_manifest: Dictionary = slice._resolve_faction_manifest("elves")
	slice.queue_free()
	await process_frame
	var elves_ok := not elves_manifest.is_empty() and not elves_manifest.has("_error")
	_check("elves_manifest_resolves", elves_ok, String(elves_manifest.get("_error", "empty")))
	if not elves_ok:
		return

	# (a) Non-Men + plots: both teams seed from the Elves manifest.
	var elves_sim = _make_manifest_sim(base_rules, map_config, {0: elves_manifest, 1: elves_manifest}, [
		{"team": 0, "faction": "elves", "is_ai": false},
		{"team": 1, "faction": "elves", "is_ai": false},
	])
	_check("non_men_setup_has_no_configuration_error", String(elves_sim.configuration_error) == "", String(elves_sim.configuration_error))
	for team in [0, 1]:
		_check_ring_seeded("non_men_team_%d" % team, elves_sim, team)

	# (b) Cross-faction + plots: Men (team 0) vs Elves (team 1), each ring
	# centered on that team's own fortress.
	var cross_manifests := {0: men_manifest, 1: elves_manifest}
	var cross_roster := [
		{"team": 0, "faction": "men", "is_ai": false},
		{"team": 1, "faction": "elves", "is_ai": false},
	]
	var cross_sim = _make_manifest_sim(base_rules, map_config, cross_manifests, cross_roster)
	_check("cross_faction_setup_has_no_configuration_error", String(cross_sim.configuration_error) == "", String(cross_sim.configuration_error))
	_check(
		"cross_faction_distinct_manifests",
		String(cross_sim.team_manifest_for(0).get("faction", "")) == "men"
			and String(cross_sim.team_manifest_for(1).get("faction", "")) == "elves"
	)
	for team in [0, 1]:
		_check_ring_seeded("cross_faction_team_%d" % team, cross_sim, team)

	# Twin determinism over the cross-faction plots setup.
	var twin_a = _make_manifest_sim(base_rules, map_config, cross_manifests, cross_roster)
	var twin_b = _make_manifest_sim(base_rules, map_config, cross_manifests, cross_roster)
	twin_a.advance(200)
	twin_b.advance(200)
	_check(
		"cross_faction_plots_twin_deterministic_200_ticks",
		twin_a.state_hash() == twin_b.state_hash(),
		"%s != %s" % [twin_a.state_hash(), twin_b.state_hash()]
	)


func _make_manifest_sim(base_rules: Dictionary, map_config: Dictionary, manifests: Dictionary, roster: Array):
	var sim = SimScript.new()
	var rules := base_rules.duplicate(true)
	rules["enable_base_loop"] = true
	rules["spawn_initial_battalions"] = false
	rules["build_plots_only"] = true
	rules["team_faction_manifests"] = manifests.duplicate(true)
	sim.configure_team_roster(roster)
	sim.setup(map_config.duplicate(true), rules)
	sim.ai_enabled = false
	return sim


func _check_ring_seeded(label: String, sim, team: int) -> void:
	var fortress := int(sim.fortress_id(team))
	_check("%s_seeds_a_fortress" % label, fortress != 0)
	var plots: Array = sim.build_plot_states(team)
	_check("%s_seeds_full_plot_ring" % label, plots.size() == SimScript.BUILD_PLOT_RING_OFFSETS.size(), str(plots.size()))
	if fortress == 0 or plots.size() != SimScript.BUILD_PLOT_RING_OFFSETS.size():
		return
	var center := Vector2((sim.structures[fortress] as Dictionary).get("position", Vector2.ZERO))
	var ring_matches := true
	for index in plots.size():
		var expected: Vector2 = center + Vector2(SimScript.BUILD_PLOT_RING_OFFSETS[index])
		if Vector2((plots[index] as Dictionary).get("position", Vector2.INF)) != expected:
			ring_matches = false
			break
	_check("%s_ring_centers_on_own_fortress" % label, ring_matches)


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


func _make_inherit_sim(heterogeneous_enemy: bool = false):
	var manifest: Dictionary = ManifestScript.default_manifest()
	manifest["structure_source_object_ids"] = {
		"fortress": ["MenFortress", "MenFortressCitadel"],
		"barracks": ["GondorBarracks"],
	}
	manifest["structure_inherit_upgrades"] = {
		"barracks": [
			{
				"radius": {"authored": "40", "value": 40.0},
				"upgradeId": "Upgrade_TestStonework",
				"upgradeType": "OBJECT",
				"objectFilter": "ANY +MenFortressCitadel",
				"sourceObjectId": "MenFortressCitadel",
				"module": "InheritUpgradeCreate",
			},
		],
	}
	var enemy_manifest := manifest
	if heterogeneous_enemy:
		enemy_manifest = manifest.duplicate(true)
		enemy_manifest["faction"] = "dwarves"
		enemy_manifest["structure_source_object_ids"] = {
			"fortress": ["DwarvenFortress", "DwarvenFortressCitadel"],
			"barracks": ["DwarvenBarracks"],
		}
	var sim = SimScript.new()
	sim.setup({}, {
		"enable_base_loop": true,
		"starting_resources": 100000,
		"member_health": 100,
		"unit_rules": _unit_rules(),
		"farm_income": 25,
		"source_map_transform_scale": 0.5,
		"team_faction_manifests": {0: manifest, 1: enemy_manifest},
	})
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
