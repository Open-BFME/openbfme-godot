class_name SimHostMatch
extends Node3D
## First playable native-core match: host-owned simulation, snapshot-only view.

const SimHostClientScript := preload("res://src/sim/sim_host_client.gd")
const NativeLockstepSessionScript := preload("res://src/net/native_lockstep_session.gd")
const NativeContentLocatorScript := preload("res://src/sim/native_content_locator.gd")
const MapDocumentScript := preload("res://src/map/map_document.gd")
const MapTerrainMeshScript := preload("res://src/map/map_terrain_mesh.gd")
const MapBootstrapScript := preload("res://src/map/map_bootstrap.gd")
const TICK_SECONDS := 1.0 / 30.0
const PICK_RADIUS_PIXELS := 32.0

var _client
var _lockstep
var _mp_active := false
var _mp_status_label: Label
var _renderer
var _camera_rig
var _selection_box: ColorRect
var _selection_rings: MultiMeshInstance3D
var _ring_multimesh: MultiMesh
var _catalog: Array[Dictionary] = []
var _latest_snapshot: Dictionary = {}
var _horde_owners: Dictionary = {}
var _spawned_hordes: Array[int] = []
var _selected_hordes: Array[int] = []
var _accumulator := 0.0
var _tick := 0
var _player_seq := 0
var _drag_start := Vector2.ZERO
var _dragging := false
var _attack_move_armed := false
var _running := false
var _startup_failed := false
var _startup_error := ""
var _damage_events := 0
var _death_events := 0
var _replay_mode := false
var _replay_path := ""
var _map_document
var _terrain
var _profile_elapsed := 0.0
var _profile_seconds := 0
var _profile_step_usec := 0
var _profile_step_max_usec := 0
var _profile_steps := 0
var _profile_draw_usec := 0
var _profile_draw_max_usec := 0
var _profile_frames := 0
var _profile_started_msec := 0
var _profile_next_msec := 0


func _ready() -> void:
	_renderer = get_node("SnapshotInstancedRenderer")
	_camera_rig = get_node("RtsCamera")
	_selection_box = get_node("Overlay/SelectionBox") as ColorRect
	_setup_selection_rings()
	call_deferred("_start_match")


func _exit_tree() -> void:
	shutdown()


func _process(delta: float) -> void:
	if not _running:
		return
	_profile_elapsed += delta
	var draw_usec := int(RenderingServer.get_frame_setup_time_cpu() * 1000.0)
	_profile_draw_usec += draw_usec
	_profile_draw_max_usec = maxi(_profile_draw_max_usec, draw_usec)
	_profile_frames += 1
	_accumulator += minf(delta, 0.25)
	if _mp_active:
		_lockstep.poll()
		if _accumulator >= TICK_SECONDS:
			var snapshot: Dictionary = _lockstep.step()
			if not snapshot.is_empty():
				_accept_snapshot(snapshot)
				_accumulator -= TICK_SECONDS
			else:
				_accumulator = minf(_accumulator, TICK_SECONDS)
		_renderer.render_interpolated(clampf(_accumulator / TICK_SECONDS, 0.0, 1.0))
		return
	var snapshots: Array[Dictionary] = _client.take_stream_snapshots()
	for snapshot in snapshots:
		_accept_snapshot(snapshot)
		_accumulator = 0.0
	var stream_failure: String = _client.stream_error()
	if not stream_failure.is_empty():
		_fail("step stream: %s" % stream_failure)
		return
	_renderer.render_interpolated(clampf(_accumulator / TICK_SECONDS, 0.0, 1.0))
	if Time.get_ticks_msec() >= _profile_next_msec:
		_print_profile()


func _unhandled_input(event: InputEvent) -> void:
	if not _running or _replay_mode:
		return
	if event is InputEventKey and (event as InputEventKey).pressed and not (event as InputEventKey).echo:
		var key_event := event as InputEventKey
		if key_event.keycode == KEY_A:
			_attack_move_armed = true
			get_viewport().set_input_as_handled()
		elif key_event.keycode == KEY_S:
			_send_player_bundle(make_command_bundle(
				_tick + 1, _local_player_seat(), _player_seq, "stop", _selected_hordes
			))
			get_viewport().set_input_as_handled()
	if not (event is InputEventMouseButton or event is InputEventMouseMotion):
		return
	if event is InputEventMouseButton:
		var button := event as InputEventMouseButton
		if button.button_index == MOUSE_BUTTON_LEFT:
			_handle_left_button(button)
		elif button.button_index == MOUSE_BUTTON_RIGHT and button.pressed:
			_handle_right_click(button.position)
	elif event is InputEventMouseMotion and _dragging:
		_update_selection_box((event as InputEventMouseMotion).position)


func _start_match() -> void:
	var requested_replay := _replay_argument()
	if not requested_replay.is_empty():
		_start_replay(requested_replay)
		return
	var match := _load_json(_repo_path("contracts/fixtures/match-launch-v1.json"))
	if match.is_empty():
		_fail("match-launch fixture could not be read")
		return
	var preferred_map := String((match.get("map", {}) as Dictionary).get("path", ""))
	var native_content: Dictionary = NativeContentLocatorScript.resolve(preferred_map)
	var bundle_path := String(native_content.get("bundle", ""))
	var bundle_source := String(native_content.get("bundle_source", "unresolved"))
	if (
		OS.get_environment("OPENBFME_BUNDLE").strip_edges().is_empty()
		and not FileAccess.file_exists(bundle_path)
	):
		bundle_path = _repo_path("workspace/logs/lane-cook-c/corpus-bundle-full.json")
		bundle_source = "default"
	var map_path := String(native_content.get("map", ""))
	var map_source := String(native_content.get("map_source", "unresolved"))
	if (
		OS.get_environment("OPENBFME_MAP").strip_edges().is_empty()
		and not FileAccess.file_exists(map_path)
	):
		map_path = _repo_path("workspace/logs/lane-map-scene/fords.map-v1.json")
		map_source = "default"
	print("SIM_HOST_MATCH_BUNDLE source=%s path=%s" % [bundle_source, bundle_path])
	if not FileAccess.file_exists(bundle_path):
		_fail("bundle is absent at %s" % bundle_path)
		return
	var host_requested := OS.get_environment("OPENBFME_MP_HOST") == "1"
	var join_requested := OS.get_environment("OPENBFME_MP_JOIN").strip_edges()
	_mp_active = host_requested or not join_requested.is_empty()
	var players := match.get("players", []) as Array
	if players.size() >= 2 and not _mp_active:
		var opponent := players[1] as Dictionary
		opponent["controller"] = "ai"
		opponent["ai_difficulty"] = "medium"
	elif players.size() >= 2 and not (players[1] as Dictionary).has("controller"):
		# Multiplayer seats are human unless the launch explicitly marks AI.
		(players[1] as Dictionary)["controller"] = "human"
	if FileAccess.file_exists(map_path):
		_map_document = MapDocumentScript.new()
		if not _map_document.load_path(map_path):
			_fail(_map_document.error)
			return
		print("SIM_HOST_MATCH_MAP source=%s path=%s" % [map_source, map_path])
	else:
		map_path = ""
		print("SIM_HOST_MATCH_MAP source=%s path=<absent-mapless-fallback>" % map_source)
	_client = SimHostClientScript.new()
	var launched: bool = (
		_client.launch_bundle(match, bundle_path)
		if map_path.is_empty()
		else _client.launch_bundle_map(match, bundle_path, map_path)
	)
	if not launched:
		_fail(_client.last_error())
		return
	if _map_document != null:
		var map_reply := _client.launch_reply().get("map", {}) as Dictionary
		if map_reply.is_empty():
			_fail("native host did not load the selected map")
			return
		print("SIM_HOST_MATCH_MAP loaded=true spawned=%d" % int(
			map_reply.get("objects_spawned", 0)
		))
	_replay_path = _new_replay_path()
	if not _client.record(_replay_path):
		_fail("replay record: %s" % _client.last_error())
		return
	_catalog = _client.templates()
	if _catalog.is_empty():
		_fail(_client.last_error())
		return
	_renderer.configure_templates(_catalog)
	if _map_document != null:
		_terrain = MapTerrainMeshScript.new()
		_terrain.name = "NativeMapTerrain"
		add_child(_terrain)
		if not _terrain.build(_map_document):
			_fail("map terrain mesh build failed")
			return
		var old_ground := get_node_or_null("Ground") as MeshInstance3D
		if old_ground != null:
			old_ground.visible = false
		var setup: Dictionary = MapBootstrapScript.spawn_match(
			_client, match, _map_document, _catalog
		)
		if setup.has("error"):
			_fail(String(setup.error))
			return
		for id_value in setup.get("hordes", []) as Array:
			_spawned_hordes.append(int(id_value))
		_horde_owners = (setup.get("horde_owners", {}) as Dictionary).duplicate()
		var player_start := int((players[0] as Dictionary).get("start_position", 0))
		var opponent_start := int((players[1] as Dictionary).get("start_position", 1))
		_camera_rig.focus_toward(
			_map_document.start_world(player_start),
			_map_document.start_world(opponent_start)
		)
	else:
		if not _spawn_mapless_armies():
			return
	var initial: Array[Dictionary] = _client.step(1, "packed")
	if initial.size() != 1:
		_fail("initial snapshot: %s" % _client.last_error())
		return
	_accept_snapshot(initial[0])
	_running = true
	_client.reset_profile()
	_renderer.reset_profile()
	_profile_started_msec = Time.get_ticks_msec()
	_profile_next_msec = _profile_started_msec + 5000
	if _mp_active:
		if not _start_lockstep(match, bundle_path, map_path, host_requested, join_requested):
			return
	else:
		if not _client.start_packed_stream():
			_fail("step stream: %s" % _client.last_error())
			return
	print(
		"SIM_HOST_MATCH_READY hordes=%d members=%d tick=%d loaded=%d failed=%d"
		% [
			_spawned_hordes.size(),
			int(_latest_snapshot.get("object_count", 0)),
			_tick,
			int(_client.launch_reply().get("templates_loaded", 0)),
			int(_client.launch_reply().get("templates_failed", 0)),
		]
	)


func _print_profile() -> void:
	_profile_seconds = int((Time.get_ticks_msec() - _profile_started_msec) / 1000)
	var client_profile: Dictionary = _client.take_profile()
	var renderer_profile: Dictionary = _renderer.take_profile()
	var documents := maxi(1, int(client_profile.get("documents", 0)))
	var uploads := maxi(1, int(renderer_profile.get("uploads", 0)))
	var steps := maxi(1, int(client_profile.get("steps", 0)))
	var frames := maxi(1, _profile_frames)
	print(
		"SIM_HOST_MAP_PROFILE seconds=%d objects=%d step_ms=%.3f step_max_ms=%.3f read_ms=%.3f read_max_ms=%.3f parse_ms=%.3f parse_max_ms=%.3f concat_ms=%.3f upload_ms=%.3f upload_max_ms=%.3f draw_ms=%.3f draw_max_ms=%.3f frames=%d snapshots=%d chars=%d"
		% [
			_profile_seconds,
			object_count(),
			float(client_profile.get("step_usec", 0)) / float(steps) / 1000.0,
			float(client_profile.get("step_max_usec", 0)) / 1000.0,
			float(client_profile.get("read_usec", 0)) / float(documents) / 1000.0,
			float(client_profile.get("read_max_usec", 0)) / 1000.0,
			float(client_profile.get("parse_usec", 0)) / float(documents) / 1000.0,
			float(client_profile.get("parse_max_usec", 0)) / 1000.0,
			float(client_profile.get("concat_usec", 0)) / float(documents) / 1000.0,
			float(renderer_profile.get("upload_usec", 0)) / float(uploads) / 1000.0,
			float(renderer_profile.get("upload_max_usec", 0)) / 1000.0,
			float(_profile_draw_usec) / float(frames) / 1000.0,
			float(_profile_draw_max_usec) / 1000.0,
			_profile_frames,
			int(client_profile.get("documents", 0)),
			int(client_profile.get("characters", 0)),
		]
	)
	_profile_elapsed = 0.0
	_profile_next_msec += 5000
	_profile_step_usec = 0
	_profile_step_max_usec = 0
	_profile_steps = 0
	_profile_draw_usec = 0
	_profile_draw_max_usec = 0
	_profile_frames = 0


func _spawn_mapless_armies() -> bool:
	var names := {
		"p0_fighter": choose_horde_template(_catalog, "GondorFighterHorde", "gondor", "fighter"),
		"p0_archer": choose_horde_template(_catalog, "GondorArcherHorde", "gondor", "archer"),
		"p1_fighter": choose_horde_template(_catalog, "MordorFighterHorde", "mordor", "fighter"),
		"p1_archer": choose_horde_template(_catalog, "MordorArcherHorde", "mordor", "archer"),
	}
	for key in names:
		if String(names[key]).is_empty():
			_fail("no loaded horde equivalent for %s" % key)
			return false
	print(
		"SIM_HOST_MATCH_TEMPLATES player0_fighter=%s player0_archer=%s player1_fighter=%s player1_archer=%s"
		% [names.p0_fighter, names.p0_archer, names.p1_fighter, names.p1_archer]
	)
	var requests := [
		[names.p0_fighter, 0, Vector2(1140.0, 760.0)],
		[names.p0_fighter, 0, Vector2(1200.0, 800.0)],
		[names.p0_fighter, 0, Vector2(1260.0, 840.0)],
		[names.p0_archer, 0, Vector2(1190.0, 910.0)],
		[names.p1_fighter, 1, Vector2(1740.0, 1060.0)],
		[names.p1_fighter, 1, Vector2(1800.0, 1100.0)],
		[names.p1_fighter, 1, Vector2(1860.0, 1140.0)],
		[names.p1_archer, 1, Vector2(1810.0, 990.0)],
	]
	for request in requests:
		var reply: Dictionary = _client.spawn(String(request[0]), int(request[1]), request[2] as Vector2)
		if reply.is_empty():
			_fail(_client.last_error())
			return false
		var id := int(reply.get("id", 0))
		_spawned_hordes.append(id)
		_horde_owners[id] = int(request[1])
	return true


func _start_replay(path: String) -> void:
	_replay_mode = true
	_replay_path = ProjectSettings.globalize_path(path) if path.begins_with("user://") else path
	_client = SimHostClientScript.new()
	var result: Dictionary = _client.replay(_replay_path, true)
	if result.is_empty():
		_fail("replay: %s" % _client.last_error())
		return
	_catalog = _client.templates()
	if _catalog.is_empty():
		_fail("replay templates: %s" % _client.last_error())
		return
	_renderer.configure_templates(_catalog)
	var all_hashes_ok := true
	for row_value in result.get("progress", []) as Array:
		var row := row_value as Dictionary
		all_hashes_ok = all_hashes_ok and bool(row.get("hash_ok", false))
		var snapshot_value: Variant = row.get("snapshot")
		if snapshot_value is Dictionary:
			_accept_snapshot(snapshot_value as Dictionary)
	var done := result.get("done", {}) as Dictionary
	if not all_hashes_ok or done.get("divergence_tick") != null:
		_fail("replay diverged at %s" % str(done.get("divergence_tick")))
		return
	print("SIM_HOST_MATCH_REPLAY_DONE ticks=%d path=%s" % [int(done.get("ticks", 0)), _replay_path])


static func choose_horde_template(
	template_rows: Array[Dictionary], preferred: String, faction_token: String, role_token: String
) -> String:
	var candidates: Array[String] = []
	for row in template_rows:
		if not bool(row.get("horde", false)):
			continue
		var name := String(row.get("name", ""))
		if name.nocasecmp_to(preferred) == 0:
			return name
		var folded := name.to_lower()
		if folded.contains(faction_token.to_lower()) and folded.contains(role_token.to_lower()):
			candidates.append(name)
	if candidates.is_empty():
		for row in template_rows:
			if bool(row.get("horde", false)):
				var name := String(row.get("name", ""))
				if name.to_lower().contains(faction_token.to_lower()):
					candidates.append(name)
	candidates.sort_custom(func(a: String, b: String) -> bool: return a.naturalnocasecmp_to(b) < 0)
	return "" if candidates.is_empty() else candidates[0]


static func selection_from_screen_points(
	horde_points: Array[Dictionary], rectangle: Rect2, owner: int
) -> Array[int]:
	var selected: Array[int] = []
	for row in horde_points:
		if int(row.get("owner", -1)) != owner:
			continue
		var point: Vector2 = row.get("point", Vector2.ZERO)
		if rectangle.has_point(point):
			selected.append(int(row.get("id", 0)))
	selected.sort()
	return selected


static func make_command_bundle(
	tick: int,
	seat: int,
	seq: int,
	command_type: String,
	objects: Array[int],
	position: Vector2 = Vector2.ZERO,
	target: int = 0
) -> Dictionary:
	if objects.is_empty():
		return {}
	var args := {"objects": objects.duplicate()}
	if command_type in ["move", "attack_move"]:
		args["x"] = position.x
		args["y"] = position.y
	elif command_type == "attack":
		args["target"] = target
	return {
		"schema": "openbfme.command.v1",
		"tick": tick,
		"seat": seat,
		"seq": seq,
		"commands": [{"type": command_type, "args": args}],
	}


static func right_click_command_from_pick(
	objects: Array[int], tick: int, seq: int, enemy_horde: int, ground: Vector2, seat: int = 0
) -> Dictionary:
	return make_command_bundle(
		tick,
		seat,
		seq,
		"attack" if enemy_horde > 0 else "move",
		objects,
		ground,
		enemy_horde
	)


func _accept_snapshot(snapshot: Dictionary) -> void:
	_latest_snapshot = snapshot
	_tick = int(snapshot.get("tick", _tick))
	for event_value in snapshot.get("events", []) as Array:
		if not (event_value is Dictionary):
			continue
		var kind := String((event_value as Dictionary).get("kind", ""))
		_damage_events += 1 if kind == "damage" else 0
		_death_events += 1 if kind == "death" else 0
	_renderer.submit_snapshot(snapshot)
	_update_selection_rings()


func _handle_left_button(button: InputEventMouseButton) -> void:
	if button.pressed and _attack_move_armed:
		var hit: Variant = _camera_rig.screen_to_ground(button.position)
		if hit is Vector3:
			var point := hit as Vector3
			_send_player_bundle(make_command_bundle(
				_tick + 1,
				_local_player_seat(),
				_player_seq,
				"attack_move",
				_selected_hordes,
				Vector2(point.x, point.z)
			))
		_attack_move_armed = false
		get_viewport().set_input_as_handled()
		return
	if button.pressed:
		_drag_start = button.position
		_dragging = true
		_update_selection_box(button.position)
	else:
		var rect := _screen_rect(_drag_start, button.position)
		if rect.size.length() < 8.0:
			rect = Rect2(button.position - Vector2(12.0, 12.0), Vector2(24.0, 24.0))
		_selected_hordes = selection_from_screen_points(
			_horde_screen_points(), rect, _local_player_seat()
		)
		_dragging = false
		_selection_box.visible = false
		_update_selection_rings()
	get_viewport().set_input_as_handled()


func _handle_right_click(screen_position: Vector2) -> void:
	if _selected_hordes.is_empty():
		return
	var enemy := _enemy_horde_at(screen_position)
	var hit: Variant = _camera_rig.screen_to_ground(screen_position)
	if not (hit is Vector3):
		return
	var point := hit as Vector3
	_send_player_bundle(right_click_command_from_pick(
		_selected_hordes, _tick + 1, _player_seq, enemy, Vector2(point.x, point.z),
		_local_player_seat()
	))
	get_viewport().set_input_as_handled()


func _send_player_bundle(bundle: Dictionary) -> void:
	if bundle.is_empty():
		return
	if _mp_active:
		if _lockstep.local_commands(bundle):
			_player_seq += 1
		else:
			_fail("lockstep refused local command: %s" % _lockstep.last_error())
		return
	var scheduled := bundle.duplicate(true)
	if _client.is_streaming():
		scheduled["tick"] = maxi(int(scheduled.get("tick", 0)), _tick + 2)
	if _client.send_commands(scheduled):
		_player_seq += 1
	else:
		_fail("player command: %s" % _client.last_error())


func _horde_screen_points() -> Array[Dictionary]:
	var result: Array[Dictionary] = []
	var camera = _camera_rig.camera()
	for horde_value in _latest_snapshot.get("hordes", []) as Array:
		var horde := horde_value as Dictionary
		var world := _horde_centroid(int(horde.get("id", 0)))
		if camera == null or camera.is_position_behind(world):
			continue
		result.append({
			"id": int(horde.get("id", 0)),
			"owner": int(horde.get("owner", -1)),
			"point": camera.unproject_position(world),
		})
	return result


func _enemy_horde_at(screen_position: Vector2) -> int:
	var best_id := 0
	var best_distance := PICK_RADIUS_PIXELS
	var local_seat := _local_player_seat()
	for row in _horde_screen_points():
		if int(row.get("owner", -1)) == local_seat:
			continue
		var distance_pixels := screen_position.distance_to(row.get("point", Vector2.ZERO))
		if distance_pixels <= best_distance:
			best_distance = distance_pixels
			best_id = int(row.get("id", 0))
	return best_id


func _horde_centroid(horde_id: int) -> Vector3:
	var horde: Dictionary = {}
	for value in _latest_snapshot.get("hordes", []) as Array:
		if int((value as Dictionary).get("id", 0)) == horde_id:
			horde = value as Dictionary
			break
	if horde.is_empty():
		return Vector3.ZERO
	var objects := _latest_snapshot.get("objects", {}) as Dictionary
	var slots: Dictionary = {}
	var ids := objects.get("id", []) as Array
	for index in ids.size():
		slots[int(ids[index])] = index
	var sum := Vector3.ZERO
	var count := 0
	for member_value in horde.get("members", []) as Array:
		var member := int(member_value)
		if not slots.has(member):
			continue
		var slot := int(slots[member])
		sum += Vector3(
			float((objects.get("x", []) as Array)[slot]),
			float((objects.get("y", []) as Array)[slot]),
			float((objects.get("z", []) as Array)[slot])
		)
		count += 1
	return Vector3.ZERO if count == 0 else sum / float(count)


func _player_centroid() -> Vector3:
	var sum := Vector3.ZERO
	var count := 0
	var local_seat := _local_player_seat()
	for id in _spawned_hordes:
		if int(_horde_owners.get(id, -1)) == local_seat and _horde_exists(id):
			sum += _horde_centroid(id)
			count += 1
	return Vector3(1200.0, 0.0, 800.0) if count == 0 else sum / float(count)


func _local_player_seat() -> int:
	if _mp_active and _lockstep != null and _lockstep.local_seat >= 0:
		return _lockstep.local_seat
	return 0


func _horde_exists(horde_id: int) -> bool:
	for horde_value in _latest_snapshot.get("hordes", []) as Array:
		if int((horde_value as Dictionary).get("id", 0)) == horde_id:
			return true
	return false


func _setup_selection_rings() -> void:
	_selection_rings = get_node("SelectionRings") as MultiMeshInstance3D
	_ring_multimesh = MultiMesh.new()
	_ring_multimesh.transform_format = MultiMesh.TRANSFORM_3D
	var ring := TorusMesh.new()
	ring.inner_radius = 19.0
	ring.outer_radius = 22.0
	ring.rings = 24
	ring.ring_segments = 6
	var material := StandardMaterial3D.new()
	material.albedo_color = Color(0.2, 1.0, 0.3, 0.9)
	material.emission_enabled = true
	material.emission = Color(0.1, 0.8, 0.2)
	ring.material = material
	_ring_multimesh.mesh = ring
	_selection_rings.multimesh = _ring_multimesh


func _update_selection_rings() -> void:
	if _ring_multimesh == null:
		return
	var live: Array[int] = []
	for id in _selected_hordes:
		if _horde_exists(id):
			live.append(id)
	_selected_hordes = live
	_ring_multimesh.instance_count = live.size()
	_ring_multimesh.visible_instance_count = live.size()
	for index in live.size():
		var position := _horde_centroid(live[index])
		position.y += 0.25
		_ring_multimesh.set_instance_transform(index, Transform3D(Basis.IDENTITY, position))


func _update_selection_box(current: Vector2) -> void:
	var rect := _screen_rect(_drag_start, current)
	_selection_box.position = rect.position
	_selection_box.size = rect.size
	_selection_box.visible = true


static func _screen_rect(first: Vector2, second: Vector2) -> Rect2:
	return Rect2(
		Vector2(minf(first.x, second.x), minf(first.y, second.y)),
		Vector2(absf(second.x - first.x), absf(second.y - first.y))
	)


func shutdown() -> bool:
	_running = false
	if _lockstep != null:
		_lockstep.shutdown()
		_lockstep = null
	if _client == null:
		_print_replay_path()
		return true
	var clean: bool = _client.quit()
	_client = null
	_print_replay_path()
	return clean


func _print_replay_path() -> void:
	if not _replay_path.is_empty():
		print("SIM_HOST_MATCH_REPLAY path=%s mode=%s" % [
			_replay_path, "playback" if _replay_mode else "recorded"
		])


func is_running() -> bool:
	return _running


func startup_failed() -> bool:
	return _startup_failed


func startup_error() -> String:
	return _startup_error


func tick_index() -> int:
	return _tick


func object_count() -> int:
	return int(_latest_snapshot.get("object_count", 0))


func event_counts() -> Vector2i:
	return Vector2i(_damage_events, _death_events)


func model_resolution_summary() -> String:
	return _renderer.model_resolution_summary()


func _fail(message: String) -> void:
	_startup_failed = true
	_startup_error = message
	_running = false
	printerr("SIM_HOST_MATCH FAIL %s" % message)
	if _client != null:
		_client.quit()
		_client = null


func _start_lockstep(
	match: Dictionary,
	bundle_path: String,
	map_path: String,
	host_requested: bool,
	join_requested: String
) -> bool:
	var bundle_document := _load_json(bundle_path)
	var bundle_source := bundle_document.get("source", {}) as Dictionary
	var bundle_identity := String(bundle_source.get("effective_tree_sha256", ""))
	var map_identity := String((match.get("map", {}) as Dictionary).get("sha256", ""))
	if map_identity.is_empty() and not map_path.is_empty():
		var map_document := _load_json(map_path)
		map_identity = String((map_document.get("source", {}) as Dictionary).get("sha256", ""))
	if map_identity.is_empty():
		map_identity = String((match.get("map", {}) as Dictionary).get("path", "mapless"))
	var input_ticks := maxi(1, int(OS.get_environment("OPENBFME_MP_INPUT_DELAY"))) \
		if not OS.get_environment("OPENBFME_MP_INPUT_DELAY").is_empty() else 3
	var hash_ticks := maxi(1, int(OS.get_environment("OPENBFME_MP_HASH_INTERVAL"))) \
		if not OS.get_environment("OPENBFME_MP_HASH_INTERVAL").is_empty() else 30
	_lockstep = NativeLockstepSessionScript.new()
	var report_path := _repo_path("workspace/logs/lane-net-b/desync-match.json")
	if not _lockstep.configure(
		_client, match, bundle_identity, map_identity, input_ticks, hash_ticks, report_path
	):
		_fail(_lockstep.last_error())
		return false
	_lockstep.status_changed.connect(_show_mp_message)
	_lockstep.desync_detected.connect(_on_lockstep_desync)
	var port := 7777
	if not OS.get_environment("OPENBFME_MP_PORT").is_empty():
		port = int(OS.get_environment("OPENBFME_MP_PORT"))
	var result: Error
	if host_requested:
		result = _lockstep.host(port)
	else:
		var split := join_requested.rsplit(":", true, 1)
		if split.size() != 2 or int(split[1]) < 1:
			_fail("OPENBFME_MP_JOIN must be host:port")
			return false
		result = _lockstep.join(String(split[0]), int(split[1]))
	if result != OK:
		_fail(_lockstep.last_error())
		return false
	_show_mp_message("Native lockstep starting")
	return true


func _show_mp_message(message: String) -> void:
	if _mp_status_label == null:
		_mp_status_label = Label.new()
		_mp_status_label.name = "MultiplayerStatus"
		_mp_status_label.position = Vector2(24.0, 24.0)
		_mp_status_label.add_theme_font_size_override("font_size", 22)
		_mp_status_label.add_theme_color_override("font_color", Color.WHITE)
		_mp_status_label.add_theme_color_override("font_shadow_color", Color.BLACK)
		_mp_status_label.add_theme_constant_override("shadow_offset_x", 2)
		_mp_status_label.add_theme_constant_override("shadow_offset_y", 2)
		get_node("Overlay").add_child(_mp_status_label)
	_mp_status_label.text = message
	_mp_status_label.visible = true


func _on_lockstep_desync(tick: int, _local_hash: String, _remote_hash: String, report_path: String) -> void:
	_show_mp_message("DESYNC at tick %d - paused%s" % [
		tick, "\nReport: %s" % report_path if not report_path.is_empty() else ""
	])


func _load_json(path: String) -> Dictionary:
	var file := FileAccess.open(path, FileAccess.READ)
	if file == null:
		return {}
	var parsed: Variant = JSON.parse_string(file.get_as_text())
	return parsed as Dictionary if parsed is Dictionary else {}


func _repo_path(relative: String) -> String:
	var game_root := ProjectSettings.globalize_path("res://").trim_suffix("/").trim_suffix("\\")
	return game_root.get_base_dir().path_join(relative)


func _new_replay_path() -> String:
	var directory := ProjectSettings.globalize_path("user://replays")
	DirAccess.make_dir_recursive_absolute(directory)
	var timestamp := Time.get_datetime_string_from_system().replace(":", "-")
	return directory.path_join("%s.replay.json" % timestamp)


func _replay_argument() -> String:
	var args := OS.get_cmdline_user_args()
	for index in args.size() - 1:
		if String(args[index]) == "--replay":
			return String(args[index + 1])
	return ""
