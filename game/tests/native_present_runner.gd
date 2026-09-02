extends SceneTree
## Headless proof that native presentation resolves the private retail map.

const MapDocumentScript := preload("res://src/map/map_document.gd")
const MapBootstrapScript := preload("res://src/map/map_bootstrap.gd")
const SimHostClientScript := preload("res://src/sim/sim_host_client.gd")
const SimHostMatchScript := preload("res://src/sim/sim_host_match.gd")
const NativeTerrainScript := preload("res://src/present/native_terrain.gd")
const NativeAudioScript := preload("res://src/present/native_audio.gd")
const NativeFxScript := preload("res://src/present/native_fx.gd")
const MAX_RECORD_TICKS := 1800

var passed := 0
var failed := 0


func _initialize() -> void:
	call_deferred("_run")


func _run() -> void:
	var map_path := OS.get_environment("OPENBFME_MAP").strip_edges()
	var content_root := OS.get_environment("OPENBFME_CONTENT").strip_edges()
	var document = MapDocumentScript.new()
	_check("map_v1_loaded", document.load_path(map_path))
	var terrain = NativeTerrainScript.new()
	root.add_child(terrain)
	_check("terrain_configured", terrain.configure(document, content_root))
	_check("cooked_path_selected", terrain.presentation_path == "cooked-retail-map")
	_check("texture_layers", terrain.texture_layer_count > 0)
	_check("water_polygons", terrain.water_polygon_count > 0)
	_check("water_triangles", terrain.water_triangle_count > 0)
	_check("blend_cells", terrain.primary_blend_cell_count > 0)
	_check("source_terrain_lights", terrain.terrain_light_count == 3)
	_check("water_in_map_v1_frame", Rect2(Vector2.ZERO, Vector2(
		document.grid_width * document.cell_size,
		document.grid_height * document.cell_size
	)).has_point(Vector2(terrain.water_focus_world.x, terrain.water_focus_world.z)))
	var recording := _record_host_event_stream(document, map_path)
	var catalog: Array[Dictionary] = recording.get("catalog", []) as Array[Dictionary]
	var stream: Array[Dictionary] = recording.get("snapshots", []) as Array[Dictionary]
	var selected_hordes: Array[int] = recording.get("selected_hordes", []) as Array[int]
	var event_counts := recording.get("event_counts", {}) as Dictionary
	_check("host_template_catalog", catalog.size() > 8)
	_check("host_recorded_stream", not stream.is_empty())
	_check("host_fire_recorded", int(event_counts.get("fire", 0)) > 0)
	_check("host_damage_recorded", int(event_counts.get("damage", 0)) > 0)
	_check("host_death_recorded", int(event_counts.get("death", 0)) > 0)
	if stream.is_empty():
		_finish(terrain, null, null)
		return
	var native_audio = NativeAudioScript.new()
	root.add_child(native_audio)
	_check("audio_configured", native_audio.configure(catalog, terrain.pack_root, content_root, false))
	for horde_name in ["GondorFighterHorde", "GondorArcherHorde", "MordorFighterHorde", "MordorArcherHorde"]:
		var voice := native_audio.resolve_voice_rows(horde_name)
		_check("voice_%s" % horde_name, int(voice.get("line_count", 0)) > 0)
		print("NATIVE_PRESENT_VOICE template=%s object=%s lines=%d events=%s" % [
			horde_name, String(voice.get("object_id", "")), int(voice.get("line_count", 0)), JSON.stringify(voice.get("events", {}))
		])
	var first_snapshot := stream[0]
	native_audio.present_selection(selected_hordes, first_snapshot)
	native_audio.present_order("attack_move", selected_hordes, first_snapshot)
	var audio_replayed := true
	for snapshot in stream:
		audio_replayed = native_audio.submit_snapshot(snapshot) and audio_replayed
	_check("audio_event_replay", audio_replayed)
	_check("audio_voice_plays", int(native_audio.play_counts.select) > 0 and int(native_audio.play_counts.attack) > 0)
	_check("audio_fire_plays", int(native_audio.play_counts.fire) > 0)
	_check("audio_death_plays", int(native_audio.play_counts.death) > 0)
	print("NATIVE_PRESENT_AUDIO counts=%s routes=%d" % [JSON.stringify(native_audio.play_counts), native_audio.route_log.size()])
	var native_fx = NativeFxScript.new()
	root.add_child(native_fx)
	_check("fx_configured", native_fx.configure(
		catalog,
		terrain.map_data,
		terrain.pack_root,
		func(point: Vector2) -> float: return document.height_at_world(point)
	))
	for horde_name in ["GondorFighterHorde", "GondorArcherHorde", "MordorFighterHorde", "MordorArcherHorde"]:
		var fx := native_fx.resolve_fx_names(horde_name)
		_check("fx_%s" % horde_name, not String(fx.get("weapon_fx", "")).is_empty())
		print("NATIVE_PRESENT_FX template=%s weapon=%s resolved=%s" % [
			horde_name, String(fx.get("weapon_fx", "")), JSON.stringify(fx.get("resolved", []))
		])
	var fx_replayed := true
	for snapshot in stream:
		fx_replayed = native_fx.submit_snapshot(snapshot) and fx_replayed
	_check("fx_event_replay", fx_replayed)
	_check("fx_fire_play", int(native_fx.play_counts.fire) > 0)
	_check("fx_impact_play", int(native_fx.play_counts.impact) > 0)
	_check("fx_death_play", int(native_fx.play_counts.death) > 0)
	print("NATIVE_PRESENT_FX_COUNTS counts=%s routes=%d" % [JSON.stringify(native_fx.play_counts), native_fx.route_log.size()])
	print("NATIVE_PRESENT_TERRAIN textures=%d primary_blends=%d three_way=%d cliffs=%d water_polygons=%d water_triangles=%d cooked=%s" % [
		terrain.texture_layer_count,
		terrain.primary_blend_cell_count,
		terrain.three_way_blend_cell_count,
		terrain.cliff_cell_count,
		terrain.water_polygon_count,
		terrain.water_triangle_count,
		terrain.cooked_map_path,
	])
	_finish(terrain, native_audio, native_fx)


func _check(label: String, condition: bool) -> void:
	if condition:
		passed += 1
	else:
		failed += 1
		printerr("NATIVE_PRESENT FAIL %s" % label)


func _record_host_event_stream(document, map_path: String) -> Dictionary:
	var bundle := OS.get_environment("OPENBFME_BUNDLE").strip_edges()
	var match := _load_json(_repo_path("contracts/fixtures/match-launch-v1.json"))
	var client = SimHostClientScript.new()
	_check("host_launch", client.launch_bundle_map(match, bundle, map_path))
	if not client.last_error().is_empty():
		return {}
	var catalog: Array[Dictionary] = client.templates()
	var setup: Dictionary = MapBootstrapScript.spawn_match(client, match, document, catalog)
	_check("host_bootstrap", not setup.has("error"))
	if setup.has("error"):
		client.quit()
		return {}
	var player_start: Vector2 = document.start_horizontal(0)
	var opponent_start: Vector2 = document.start_horizontal(1)
	var inward := player_start.direction_to(opponent_start)
	var side_axis := Vector2(-inward.y, inward.x)
	var probe_template := MapBootstrapScript.choose_horde_template(
		catalog, "MordorFighterHorde", "Mordor", "fighter"
	)
	var probe: Dictionary = client.spawn(
		probe_template, 1, player_start + inward * 830.0 - side_axis * 170.0
	)
	_check("host_combat_probe", not probe.is_empty())
	var replay_path := _record_path()
	_check("host_recording_started", client.record(replay_path))
	var initial: Array[Dictionary] = client.step(1, "packed")
	_check("host_initial_snapshot", initial.size() == 1)
	if initial.size() != 1:
		client.quit()
		return {}
	var selected_hordes: Array[int] = []
	for id_value in setup.get("hordes", []) as Array:
		if int((setup.get("horde_owners", {}) as Dictionary).get(int(id_value), -1)) == 0:
			selected_hordes.append(int(id_value))
	_check("host_attack_order", client.send_commands(
		SimHostMatchScript.make_command_bundle(2, 0, 0, "attack_move", selected_hordes, opponent_start)
	))
	var recorded: Array[Dictionary] = [initial[0]]
	var counts := {"fire": 0, "damage": 0, "death": 0}
	var ticks := 0
	while ticks < MAX_RECORD_TICKS and (
		int(counts.fire) == 0 or int(counts.damage) == 0 or int(counts.death) == 0
	):
		var rows: Array[Dictionary] = client.step(1, "packed")
		if rows.size() != 1:
			break
		var snapshot := rows[0]
		var events := snapshot.get("events", []) as Array
		if not events.is_empty():
			recorded.append(snapshot)
		for event_value in events:
			var kind := String((event_value as Dictionary).get("kind", ""))
			if counts.has(kind):
				counts[kind] = int(counts[kind]) + 1
		ticks += 1
	_check("host_recording_finished", client.quit())
	print("NATIVE_PRESENT_HOST_RECORD path=%s ticks=%d snapshots=%d events=%s" % [
		replay_path, ticks, recorded.size(), JSON.stringify(counts)
	])
	return {
		"catalog": catalog,
		"snapshots": recorded,
		"selected_hordes": selected_hordes,
		"event_counts": counts,
	}


func _record_path() -> String:
	var directory := OS.get_environment("OPENBFME_LOG_DIR").strip_edges()
	if directory.is_empty():
		directory = _repo_path("workspace/logs/lane-present-a")
	DirAccess.make_dir_recursive_absolute(directory)
	return directory.path_join("native-present-host.replay.json")


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


func _finish(terrain, native_audio, native_fx) -> void:
	if terrain != null:
		terrain.queue_free()
	if native_audio != null:
		native_audio.queue_free()
	if native_fx != null:
		native_fx.queue_free()
	print("NATIVE_PRESENT_RESULT passed=%d failed=%d" % [passed, failed])
	quit(0 if failed == 0 else 1)
