extends SceneTree
## Castle maps are PLAYABLE (owner 2026-08-19: "cant play the castle based
## maps"). This runner boots the real skirmish slice on a castle map exactly
## the way the lobby launches it and proves the match reaches ready_ok, the
## battlefield names its capability gaps instead of refusing, and - when the
## mounted maps pack ships fixtures.json - the castle pieces seed as sim
## structures. Map via OPENBFME_CASTLE_BOOT_MAP (default Erebor).
##
##   OPENBFME_CONTENT=<repo>\workspace\content-packs <godot> --headless \
##     --path game --script res://tests/castle_map_live_boot_runner.gd

const RunnerWatchdogScript := preload("res://tests/runner_watchdog.gd")
const BOOT_DEADLINE_MS := 300000
var _runner_watchdog := RunnerWatchdogScript.new()

var _passed := 0
var _failed := 0


func _init() -> void:
	_runner_watchdog.start(self, "CASTLE_LIVE_BOOT_RUNNER", 600000)
	await process_frame
	var map_id := OS.get_environment("OPENBFME_CASTLE_BOOT_MAP")
	if map_id == "":
		map_id = "rotwk.map.wor-erebor"
	OS.set_environment("OPENBFME_SLICE_FACTION", "men")
	OS.set_environment("OPENBFME_SLICE_MAP", map_id)
	var scene: PackedScene = load("res://scenes/retail_vertical_slice.tscn")
	if not _check("scene_loads", scene != null, "vertical slice scene did not load"):
		_finish()
		return
	var slice = scene.instantiate()
	root.add_child(slice)
	var deadline := Time.get_ticks_msec() + BOOT_DEADLINE_MS
	while Time.get_ticks_msec() < deadline:
		await process_frame
		if bool(slice.ready_ok) or String(slice.failure_reason) != "":
			break
	if not _check("castle_slice_ready", bool(slice.ready_ok), "map=%s failure=%s" % [map_id, String(slice.failure_reason)]):
		if slice.simulation != null:
			for sid in slice.simulation.structure_ids():
				var srow: Dictionary = slice.simulation.structure(sid)
				if int(sid) >= 3000 and int(sid) < 3020:
					print("CASTLE_LIVE_BOOT DIAG structure %d kind=%s name=%s object_id=%s presentation=%s" % [int(sid), String(srow.get("structure_kind", "")), String(srow.get("name", "")), String(srow.get("object_id", "")), String(srow.get("presentation", ""))])
		_finish()
		return
	var map_data = slice.source_map_data
	_check("map_is_a_castle_map", not (map_data.castle_required_capabilities as Array).is_empty(), "map=%s carries no castleSiege contract" % map_id)
	var battlefield = slice.battlefield
	_check("battlefield_names_gaps_not_refuses", battlefield != null and (battlefield.castle_gameplay_gaps as Array) == (map_data.castle_gameplay_blockers as Array),
		"battlefield=%s" % str(battlefield))
	var simulation = slice.simulation
	var fixtures_shipped := not (map_data.castle_fixture_placements as Array).is_empty()
	var rules: Dictionary = slice.gameplay_rules
	_check("castle_fixture_rule_matches_shipped_fixtures", bool(rules.get("enable_castle_fixtures", false)) == fixtures_shipped,
		"rule=%s shipped_placements=%d" % [str(rules.get("enable_castle_fixtures", null)), (map_data.castle_fixture_placements as Array).size()])
	if fixtures_shipped:
		var seeded := 0
		for structure_id in simulation.structure_ids():
			var srow: Dictionary = simulation.structure(structure_id)
			if String(srow.get("structure_kind", "")) == "castle_fixture":
				seeded += 1
		# Placements the scenario-placement lane already seeded (creep lairs,
		# scenario structures) are skipped by the fixture seeder so nothing
		# spawns twice; every other placement must be a castle_fixture row.
		var overlap := 0
		var seeded_elsewhere: Dictionary = simulation._scenario_map_seeded_source_indices
		for placement_value in (map_data.castle_fixture_placements as Array):
			if seeded_elsewhere.has(int((placement_value as Dictionary).get("source_index", -1))):
				overlap += 1
		var placements := (map_data.castle_fixture_placements as Array).size()
		_check("castle_pieces_seeded_as_structures", seeded > 0 and seeded + overlap == placements,
			"seeded=%d overlap=%d placements=%d" % [seeded, overlap, placements])
	else:
		print("CASTLE_LIVE_BOOT NOTE mounted maps pack ships no fixtures.json for %s - castle pieces are visual props only until the maps republish" % map_id)
	# Ticks advance and the player's start army exists.
	var player_units := 0
	for id in simulation.entity_ids():
		if int(simulation.entity(id).get("team", -1)) == simulation.PLAYER_TEAM:
			player_units += 1
	_check("player_start_army_present", player_units > 0, "player entities=%d" % player_units)
	var tick_before := int(simulation.tick_index)
	for i in 30:
		await process_frame
	_check("simulation_ticks_on_castle_map", int(simulation.tick_index) > tick_before, "tick %d -> %d" % [tick_before, int(simulation.tick_index)])
	root.remove_child(slice)
	slice.free()
	await process_frame
	_finish()


func _check(name: String, ok: bool, detail: String = "") -> bool:
	if ok:
		_passed += 1
		print("CASTLE_LIVE_BOOT PASS %s" % name)
	else:
		_failed += 1
		print("CASTLE_LIVE_BOOT FAIL %s%s" % [name, (" (%s)" % detail) if detail != "" else ""])
	return ok


func _finish() -> void:
	_runner_watchdog.stop()
	print("CASTLE_LIVE_BOOT_RESULT passed=%d failed=%d" % [_passed, _failed])
	quit(0 if _failed == 0 else 1)
