extends SceneTree

## N-team foundation proof (step 1-2 of docs/N_TEAM_SCOPING.md). Exercises the
## new team-registry injection path and the per-team faction-manifest plumbing
## WITHOUT flipping any gate (victory/AI/geometry/menu/lockstep are later
## packets). Boots the real private Men slice once to obtain the actual gameplay
## rules + map configuration, then constructs sims directly:
##   - a 3-team roster (0,1,2) via configure_team_roster() has independent
##     per-team resource/CP/power dicts,
##   - a 3-team snapshot round-trips (snapshot -> restore -> equal state_hash),
##   - state_hash is sensitive to a lone team-2 resource change,
##   - injecting the default 2-team roster is byte-identical to no injection, and
##   - two default sims advanced 200 identical ticks land on the same state_hash.
## Only Men is loaded, so teams 1 and 2 share the Men manifest — that is enough
## to prove the plumbing; distinct factions await the packet that removes the
## cross-faction reject.

const SimScript = preload("res://src/retail_slice/retail_slice_sim.gd")

const DEFAULT_ROSTER := [
	{"team": 0, "faction": "", "is_ai": false},
	{"team": 1, "faction": "", "is_ai": true},
]
const TRI_ROSTER := [
	{"team": 0, "faction": "", "is_ai": false},
	{"team": 1, "faction": "", "is_ai": true},
	{"team": 2, "faction": "", "is_ai": true},
]

var passed := 0
var failed := 0


func _initialize() -> void:
	OS.set_environment("OPENBFME_STARTER_ARMY", "1")
	call_deferred("_run")


func _run() -> void:
	root.size = Vector2i(1920, 1080)
	var packed: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	_check("scene_parses", packed != null)
	if packed == null:
		_finish()
		return
	var slice = packed.instantiate()
	root.add_child(slice)
	await process_frame
	await process_frame
	_check("slice_ready", bool(slice.ready_ok), String(slice.failure_reason))
	if not bool(slice.ready_ok) or slice.source_map_data == null:
		_finish()
		return

	var base_rules: Dictionary = slice.gameplay_rules.duplicate(true)
	var map_config: Dictionary = slice.source_map_data.simulation_configuration()

	_run_registry_injection_checks(base_rules, map_config)
	_run_three_team_plumbing_checks(base_rules, map_config)
	_run_three_team_snapshot_checks(base_rules, map_config)
	_run_default_reproducibility_checks(base_rules, map_config)

	slice.queue_free()
	_finish()


func _make_sim(roster: Variant, base_rules: Dictionary, map_config: Dictionary):
	var sim = SimScript.new()
	if roster != null:
		sim.configure_team_roster(roster)
	sim.setup(map_config.duplicate(true), base_rules.duplicate(true))
	return sim


func _run_registry_injection_checks(base_rules: Dictionary, map_config: Dictionary) -> void:
	# Injecting the historical 2-team roster must be byte-identical to injecting
	# nothing at all: same keys, same order, same values, same hash.
	var no_injection = _make_sim(null, base_rules, map_config)
	var explicit_default = _make_sim(DEFAULT_ROSTER, base_rules, map_config)
	_check("default_roster_is_zero_one", no_injection.team_ids() == [0, 1])
	_check(
		"default_injection_matches_no_injection_hash",
		no_injection.state_hash() == explicit_default.state_hash(),
		"%s != %s" % [no_injection.state_hash(), explicit_default.state_hash()]
	)


func _run_three_team_plumbing_checks(base_rules: Dictionary, map_config: Dictionary) -> void:
	var tri = _make_sim(TRI_ROSTER, base_rules, map_config)
	_check("tri_roster_ids", tri.team_ids() == [0, 1, 2])
	var per_team_dicts := {
		"resources": tri.team_resources,
		"command_points": tri.team_command_points,
		"power_points": tri.team_power_points,
		"purchased_powers": tri.purchased_powers,
		"team_upgrades": tri.team_upgrades,
		"kills_toward_power_point": tri._kills_toward_power_point,
		"team_sciences": tri._team_sciences,
		"power_cooldown_until": tri._power_cooldown_until,
		"staged_purchases": tri._staged_purchases,
		"next_dynamic_id": tri._next_dynamic_id,
	}
	var all_present := true
	for label in per_team_dicts:
		var dict: Dictionary = per_team_dicts[label]
		if not (dict.has(0) and dict.has(1) and dict.has(2)):
			all_present = false
			_check("tri_dict_has_three_teams_%s" % label, false, str(dict.keys()))
	_check("tri_all_per_team_dicts_present", all_present)

	# Accessors resolve every team, including the injected team 2.
	_check("tri_accessor_resources_team2", tri.resources_for_team(2) == tri.resources_for_team(0))
	_check("tri_next_dynamic_id_team2", int(tri._next_dynamic_id.get(2, -1)) == 210)

	# Per-team manifest plumbing: every team maps to a manifest and to the six
	# derived tables (aliased to the single Men manifest today).
	_check("tri_manifest_map_covers_all_teams",
		not tri.team_manifest_for(0).is_empty()
		and tri.team_manifest_for(2) == tri.team_manifest_for(0)
		and not tri.unit_production_rules_for_team(2).is_empty()
		and not tri.structure_build_rules_for_team(2).is_empty()
		and not tri.structure_max_health_for_team(2).is_empty())

	# Independence: mutating one team's mutable seed leaves the others untouched,
	# proving _seed_team_map handed each team a fresh array (not a shared alias).
	(tri.purchased_powers[2] as Array).append("proof.power")
	tri._kills_toward_power_point[2] = 7
	_check(
		"tri_power_lists_independent",
		(tri.purchased_powers[0] as Array).is_empty()
		and (tri.purchased_powers[1] as Array).is_empty()
		and (tri.purchased_powers[2] as Array).size() == 1
		and int(tri._kills_toward_power_point[0]) == 0
		and int(tri._kills_toward_power_point[1]) == 0
		and int(tri._kills_toward_power_point[2]) == 7
	)


func _run_three_team_snapshot_checks(base_rules: Dictionary, map_config: Dictionary) -> void:
	var snap_sim = _make_sim(TRI_ROSTER, base_rules, map_config)
	snap_sim.ai_enabled = false
	snap_sim.advance(30)
	var hash_before: String = snap_sim.state_hash()
	var bytes: PackedByteArray = snap_sim.snapshot()
	var restored = SimScript.new()
	_check("tri_snapshot_restores", restored.restore(bytes))
	_check("tri_restored_roster", restored.team_ids() == [0, 1, 2])
	_check(
		"tri_snapshot_round_trip_hash",
		restored.state_hash() == hash_before,
		"%s != %s" % [restored.state_hash(), hash_before]
	)

	# state_hash is sensitive to a lone team-2 resource change (the new team must
	# be part of the authoritative hash, not a silent extra key).
	var probe_a = _make_sim(TRI_ROSTER, base_rules, map_config)
	var probe_b = _make_sim(TRI_ROSTER, base_rules, map_config)
	_check("tri_baseline_hash_equal", probe_a.state_hash() == probe_b.state_hash())
	probe_b.team_resources[2] = int(probe_b.team_resources[2]) + 1
	_check("tri_hash_sensitive_to_team2_resources", probe_a.state_hash() != probe_b.state_hash())


func _run_default_reproducibility_checks(base_rules: Dictionary, map_config: Dictionary) -> void:
	# Two independently constructed default 2-team sims, advanced 200 identical
	# ticks under the same command, must land on the same state_hash — the
	# registry rewrite kept the default path deterministic and unchanged.
	var sim_a = _make_sim(null, base_rules, map_config)
	var sim_b = _make_sim(null, base_rules, map_config)
	var command := _first_move_command(sim_a)
	if not command.is_empty():
		sim_a.submit_command(command.duplicate(true))
		sim_b.submit_command(command.duplicate(true))
	sim_a.advance(200)
	sim_b.advance(200)
	_check(
		"default_two_team_200_tick_hash_reproducible",
		sim_a.state_hash() == sim_b.state_hash(),
		"%s != %s" % [sim_a.state_hash(), sim_b.state_hash()]
	)


func _first_move_command(sim) -> Dictionary:
	var ids: Array = sim.living_ids(0)
	if ids.is_empty():
		return {}
	return {
		"tick": 4,
		"team": 0,
		"seq": 0,
		"type": "issue_move",
		"args": {"ids": [int(ids[0])], "destination": Vector2(0.0, 0.0)},
	}


func _check(name: String, condition: bool, detail: String = "") -> void:
	if condition:
		passed += 1
		print("RETAIL_MULTITEAM PASS %s" % name)
	else:
		failed += 1
		printerr("RETAIL_MULTITEAM FAIL %s%s" % [name, " (%s)" % detail if detail != "" else ""])


func _finish() -> void:
	print("RETAIL_MULTITEAM_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
