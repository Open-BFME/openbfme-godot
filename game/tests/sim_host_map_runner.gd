extends SceneTree
## Headless full-map native-core determinism and combat proof.

const SimHostClientScript := preload("res://src/sim/sim_host_client.gd")
const SimHostMatchScript := preload("res://src/sim/sim_host_match.gd")
const MapDocumentScript := preload("res://src/map/map_document.gd")
const MapBootstrapScript := preload("res://src/map/map_bootstrap.gd")
const WatchdogScript := preload("res://tests/runner_watchdog.gd")
const TICKS := 1800

var passed := 0
var failed := 0
var _watchdog := WatchdogScript.new()


func _initialize() -> void:
	_watchdog.start(self, "SIM_HOST_MAP", 900_000, 30_000, true)
	_watchdog.set_result_provider(func() -> Vector2i: return Vector2i(passed, failed))
	call_deferred("_run")


func _run() -> void:
	var bundle := OS.get_environment("OPENBFME_BUNDLE").strip_edges()
	if bundle.is_empty():
		bundle = _repo_path("workspace/logs/lane-cook-c/corpus-bundle-full.json")
	var map_path := OS.get_environment("OPENBFME_MAP").strip_edges()
	if map_path.is_empty():
		map_path = _repo_path("workspace/logs/lane-map-scene/fords.map-v1.json")
	if not FileAccess.file_exists(bundle) or not FileAccess.file_exists(map_path):
		print("SIM_HOST_MAP SKIP private corpus bundle or Fords map-v1 absent")
		_finish()
		return
	var match := _load_json(_repo_path("contracts/fixtures/match-launch-v1.json"))
	var players := match.get("players", []) as Array
	if players.size() >= 2:
		(players[1] as Dictionary)["controller"] = "ai"
		(players[1] as Dictionary)["ai_difficulty"] = "medium"
	var document = MapDocumentScript.new()
	_check("map_document_loaded", document.load_path(map_path))
	if failed > 0:
		_finish()
		return
	var first := _run_session(match, bundle, map_path, document, true)
	if OS.get_environment("OPENBFME_MAP_DEBUG_SINGLE") == "1":
		print("SIM_HOST_MAP_DEBUG hash=%s damage=%d" % [String(first.get("hash", "")), int(first.get("damage", 0))])
		_watchdog.stop()
		quit(0)
		return
	var second := _run_session(match, bundle, map_path, document, false)
	_check("first_hash_present", not String(first.get("hash", "")).is_empty())
	_check("second_hash_present", not String(second.get("hash", "")).is_empty())
	_check("twin_hash_after_1800", String(first.get("hash", "")) == String(second.get("hash", "")))
	_check("damage_events_observed", int(first.get("damage", 0)) > 0)
	_finish()


func _run_session(
	match: Dictionary, bundle: String, map_path: String, document, inspect: bool
) -> Dictionary:
	var client = SimHostClientScript.new()
	_check("launch_%s" % inspect, client.launch_bundle_map(match, bundle, map_path))
	if not client.last_error().is_empty():
		return {}
	var map_reply := client.launch_reply().get("map", {}) as Dictionary
	var grid := map_reply.get("grid", {}) as Dictionary
	_check("map_grid_%s" % inspect, int(grid.get("width", 0)) == 415 and int(grid.get("height", 0)) == 353)
	_check("map_cell_size_%s" % inspect, int(grid.get("cell_size", 0)) == 10)
	_check("map_two_starts_%s" % inspect, (map_reply.get("start_positions", {}) as Dictionary).size() == 2)
	var plots := map_reply.get("plots_per_player", {}) as Dictionary
	_check("map_plots_%s" % inspect, int(plots.get("0", 0)) > 0 and int(plots.get("1", 0)) > 0)
	_check("map_objects_%s" % inspect, int(map_reply.get("objects_spawned", 0)) > 1000)
	var catalog: Array[Dictionary] = client.templates()
	_check("catalog_%s" % inspect, not catalog.is_empty())
	var setup: Dictionary = MapBootstrapScript.spawn_match(client, match, document, catalog)
	_check("bootstrap_%s" % inspect, not setup.has("error"))
	if setup.has("error"):
		client.quit()
		return {}
	_check("fortresses_%s" % inspect, (setup.get("fortresses", []) as Array).size() == 2)
	var player_start: Vector2 = document.start_horizontal(
		int((match.players[0] as Dictionary).get("start_position", 0))
	)
	var opponent_start: Vector2 = document.start_horizontal(
		int((match.players[1] as Dictionary).get("start_position", 1))
	)
	var inward: Vector2 = player_start.direction_to(opponent_start)
	var side_axis: Vector2 = Vector2(-inward.y, inward.x)
	var probe_template := MapBootstrapScript.choose_horde_template(
		catalog, "MordorFighterHorde", "Mordor", "fighter"
	)
	var probe_position: Vector2 = player_start + inward * 830.0 - side_axis * 170.0
	var probe: Dictionary = client.spawn(probe_template, 1, probe_position)
	_check("combat_probe_%s" % inspect, not probe.is_empty())
	if inspect:
		print("SIM_HOST_MAP_COMBAT_PROBE template=%s position=%s" % [probe_template, str(probe_position)])
	var hordes: Array[int] = []
	for id_value in setup.get("hordes", []) as Array:
		if int((setup.get("horde_owners", {}) as Dictionary).get(int(id_value), -1)) == 0:
			hordes.append(int(id_value))
	_check("player_hordes_%s" % inspect, hordes.size() == 4)
	var attack := SimHostMatchScript.make_command_bundle(1, 0, 0, "attack_move", hordes, opponent_start)
	_check("attack_move_%s" % inspect, client.send_commands(attack))
	var damage := 0
	var ticks_seen := 0
	var final_snapshot: Dictionary = {}
	while ticks_seen < TICKS:
		# The full Fords object set must exercise the same packed steady-state path
		# as the native match. JSON snapshots can exceed the 500 ms frame deadline.
		var snapshots: Array[Dictionary] = client.step(1, "packed")
		if snapshots.size() != 1:
			_check("step_%s_%d" % [inspect, ticks_seen], false)
			break
		for snapshot in snapshots:
			final_snapshot = snapshot
			for event_value in snapshot.get("events", []) as Array:
				if String((event_value as Dictionary).get("kind", "")) == "damage":
					damage += 1
		ticks_seen += 1
		if ticks_seen % 300 == 0:
			_watchdog.note("session_%s_tick_%d" % [inspect, ticks_seen])
			if inspect:
				print("SIM_HOST_MAP_POSITIONS tick=%d %s" % [ticks_seen, _horde_positions(final_snapshot, setup.get("hordes", []) as Array)])
		if OS.get_environment("OPENBFME_MAP_DEBUG_SINGLE") == "1" and damage > 0:
			var debug_hash := client.hash()
			client.quit()
			return {"hash": debug_hash, "damage": damage}
	_check("ticks_1800_%s" % inspect, ticks_seen == TICKS)
	var digest := client.hash()
	_check("quit_%s" % inspect, client.quit())
	return {"hash": digest, "damage": damage}


func _horde_positions(snapshot: Dictionary, horde_ids: Array) -> String:
	var objects := snapshot.get("objects", {}) as Dictionary
	var slots := {}
	var object_ids := objects.get("id", []) as Array
	for slot in object_ids.size():
		slots[int(object_ids[slot])] = slot
	var rows := []
	for wanted_index in [0, 4]:
		if wanted_index >= horde_ids.size():
			continue
		var wanted := int(horde_ids[wanted_index])
		var members: Array = []
		for horde_value in snapshot.get("hordes", []) as Array:
			if int((horde_value as Dictionary).get("id", 0)) == wanted:
				members = (horde_value as Dictionary).get("members", []) as Array
				break
		var sum := Vector2.ZERO
		var count := 0
		for member_value in members:
			var member := int(member_value)
			if not slots.has(member):
				continue
			var slot := int(slots[member])
			sum += Vector2(float((objects.get("x", []) as Array)[slot]), float((objects.get("z", []) as Array)[slot]))
			count += 1
		rows.append("h%d=%s" % [wanted, str(sum / float(count) if count > 0 else Vector2.ZERO)])
	return " ".join(rows)


func _load_json(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	file.close()
	return parsed as Dictionary if parsed is Dictionary else {}


func _repo_path(relative: String) -> String:
	var game_root := ProjectSettings.globalize_path("res://").trim_suffix("/").trim_suffix("\\")
	return game_root.get_base_dir().path_join(relative)


func _check(label: String, condition: bool) -> void:
	_watchdog.note(label)
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("SIM_HOST_MAP FAIL %s" % label)


func _finish() -> void:
	print("SIM_HOST_MAP_RESULT passed=%d failed=%d" % [passed, failed])
	_watchdog.stop()
	quit(0 if failed == 0 else 1)
